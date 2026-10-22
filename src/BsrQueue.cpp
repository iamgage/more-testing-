#include "BsrQueue.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

#include "beatsaber-hook/shared/rapidjson/include/rapidjson/document.h"
#include "beatsaber-hook/shared/utils/logging.hpp"
#include "songcore/shared/SongCore.hpp"
#include "web-utils/shared/WebUtils.hpp"
#include "zip.h"

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

void BsrQueue::Resolve(std::string key, std::string user) {
    WebUtils::URLOptions opts;
    opts.url = "https://api.beatsaver.com/maps/id/" + key;
    WebUtils::GetAsync<WebUtils::StringResponse>(opts).attach_continuation(
        [this, key, user](std::future<WebUtils::StringResponse> fut) {
            auto resp = fut.get();
            if (!resp.IsSuccessful() || !resp.responseData) return;
            rapidjson::Document d;
            d.Parse(resp.responseData->c_str());
            if (d.HasParseError() || !d.IsObject() || !d.HasMember("versions")) return;
            auto& v = d["versions"];
            if (!v.IsArray() || v.Empty()) return;

            SongRequest r;
            r.key = key;
            r.requester = user;
            r.songName = d["name"].GetString();
            r.mapper = d["uploader"]["name"].GetString();
            if (d.HasMember("metadata") && d["metadata"].HasMember("songAuthorName"))
                r.songAuthor = d["metadata"]["songAuthorName"].GetString();
            r.hash = v[0]["hash"].GetString();
            r.downloadUrl = v[0]["downloadURL"].GetString();
            {
                std::lock_guard<std::mutex> l(mtx);
                queue.push_back(std::move(r));
            }
            if (onChanged) onChanged();
        });
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

void BsrQueue::Download(const SongRequest& req) {
    WebUtils::URLOptions opts;
    opts.url = req.downloadUrl;
    auto resp = WebUtils::GetAsync<WebUtils::DataResponse>(opts).get();
    if (!resp.IsSuccessful() || !resp.responseData) return;

    // Sanitise folder name: "<key> (<song> - <mapper>)"
    std::string name = req.key + " (" + req.songName + " - " + req.mapper + ")";
    for (char& c : name)
        if (std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
    std::string dir = std::string(kCustomLevels) + name;
    std::filesystem::create_directories(dir);
    auto& data = *resp.responseData;
    int rc = zip_stream_extract((const char*)data.data(), data.size(), dir.c_str(), nullptr, nullptr);
    if (rc != 0) INFO("zip extract failed for {} ({})", req.key, rc);
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
