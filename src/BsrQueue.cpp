#include "BsrQueue.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

#include <future>
#include <thread>

#include "ZipExtract.hpp"
#include "beatsaber-hook/shared/rapidjson/include/rapidjson/document.h"
#include "main.hpp"
#include "songcore/shared/SongCore.hpp"
#include "web-utils/shared/WebUtils.hpp"

using namespace TwitchBsr;

static constexpr const char* kCustomLevels =
    "/sdcard/ModData/com.beatgames.beatsaber/Mods/SongLoader/CustomLevels/";

BsrQueue& BsrQueue::Get() {
    static BsrQueue q;
    return q;
}

std::optional<std::string> BsrQueue::ExtractKey(const std::string& arg) {
    // Accept a bare key (hex) or https://beatsaver.com/maps/<key>
    static const std::regex keyRe("^[0-9a-fA-F]{1,6}$");
    static const std::regex urlRe("beatsaver\\.com/maps/([0-9a-fA-F]{1,6})");
    std::smatch m;
    if (std::regex_search(arg, m, urlRe)) return m[1].str();
    if (std::regex_match(arg, keyRe)) {
        std::string k = arg;
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        return k;
    }
    return std::nullopt;
}

bool BsrQueue::HandleChat(const std::string& user, const std::string& text, bool privileged) {
    if (!enabled) return false;
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.rfind("!bsr ", 0) == 0) {
        std::string arg = text.substr(5);
        arg.erase(0, arg.find_first_not_of(" \t"));
        arg = arg.substr(0, arg.find_first_of(" \t"));
        auto key = ExtractKey(arg);
        if (!key) return true;  // consumed, but not a valid key
        {
            std::lock_guard<std::mutex> l(mtx);
            if ((int)queue.size() >= maxQueue) return true;
            int mine = 0;
            for (auto& r : queue) {
                if (r.key == *key) return true;  // already queued
                if (r.requester == user) mine++;
            }
            if (!privileged && mine >= maxPerUser) return true;
        }
        Resolve(*key, user);
        return true;
    }
    if (lower == "!oops") {
        std::lock_guard<std::mutex> l(mtx);
        for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
            if (it->requester == user) { queue.erase(std::next(it).base()); break; }
        }
        if (onChanged) onChanged();
        return true;
    }
    return false;
}

static std::string Str(const rapidjson::Value& v, const char* k) {
    if (v.IsObject() && v.HasMember(k) && v[k].IsString()) return v[k].GetString();
    return {};
}

void BsrQueue::Resolve(std::string key, std::string user) {
    std::thread([this, key, user] {
        std::string url = "https://api.beatsaver.com/maps/id/" + key;
        WebUtils::URLOptions opts(url);
        auto resp = WebUtils::GetAsync<WebUtils::StringResponse>(opts).get();
        if (!resp.IsSuccessful() || !resp.responseData) return;
        rapidjson::Document d;
        d.Parse(resp.responseData->c_str());
        if (d.HasParseError() || !d.IsObject() || !d.HasMember("versions")) return;
        auto& v = d["versions"];
        if (!v.IsArray() || v.Empty()) return;

        SongRequest r;
        r.key = key;
        r.requester = user;
        r.songName = Str(d, "name");
        if (d.HasMember("uploader")) r.mapper = Str(d["uploader"], "name");
        if (d.HasMember("metadata")) r.songAuthor = Str(d["metadata"], "songAuthorName");
        r.hash = Str(v[0], "hash");
        r.downloadUrl = Str(v[0], "downloadURL");
        if (r.downloadUrl.empty()) return;
        {
            std::lock_guard<std::mutex> l(mtx);
            queue.push_back(std::move(r));
        }
        if (onChanged) onChanged();
    }).detach();
}

std::vector<SongRequest> BsrQueue::Snapshot() {
    std::lock_guard<std::mutex> l(mtx);
    return queue;
}

void BsrQueue::Remove(const std::string& key) {
    std::lock_guard<std::mutex> l(mtx);
    queue.erase(std::remove_if(queue.begin(), queue.end(),
                               [&](auto& r) { return r.key == key; }), queue.end());
    if (onChanged) onChanged();
}

void BsrQueue::Clear() {
    std::lock_guard<std::mutex> l(mtx);
    queue.clear();
    if (onChanged) onChanged();
}

std::optional<SongRequest> BsrQueue::LastDownloaded() {
    std::lock_guard<std::mutex> lock(mtx);
    return lastDownloaded;
}

void BsrQueue::Download(const SongRequest& req) {
    std::string url = req.downloadUrl;
    WebUtils::URLOptions opts(url);
    auto resp = WebUtils::GetAsync<WebUtils::DataResponse>(opts).get();
    if (!resp.IsSuccessful() || !resp.responseData) return;

    // Folder name: "<key> (<song> - <mapper>)", with illegal characters replaced.
    std::string name = req.key + " (" + req.songName + " - " + req.mapper + ")";
    for (char& c : name)
        if (std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
    std::string dir = std::string(kCustomLevels) + name;
    auto& data = *resp.responseData;
    if (!TwitchBsr::ExtractZip((const uint8_t*)data.data(), data.size(), dir)) {
        INFO("zip extract failed for {}", req.key);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        lastDownloaded = req;
    }
}

void BsrQueue::DownloadAll() {
    auto snap = Snapshot();
    std::thread([this, snap] {
        for (auto& r : snap) Download(r);
        // Ask SongCore to rescan so new maps show up without restarting.
        SongCore::API::Loading::RefreshSongs(false);
        if (onChanged) onChanged();
    }).detach();
}
