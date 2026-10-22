#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace TwitchBsr {

struct SongRequest {
    std::string key;        // BeatSaver key, e.g. "3a2b1"
    std::string requester;
    std::string songName;
    std::string songAuthor;
    std::string mapper;
    std::string hash;
    std::string downloadUrl;
    bool downloaded = false;
};

class BsrQueue {
public:
    static BsrQueue& Get();

    // Parse a chat line. Handles "!bsr <key>", "!bsr <beatsaver url>", "!oops".
    // Returns true if the line was a command we consumed.
    bool HandleChat(const std::string& user, const std::string& text, bool privileged);

    std::vector<SongRequest> Snapshot();
    void Remove(const std::string& key);
    void Clear();
    void DownloadAll();                       // fetch + extract + refresh songs
    void SetOnChanged(std::function<void()> cb) { onChanged = std::move(cb); }

    // Simple limits streamers can tweak in the UI.
    int maxQueue = 25;
    int maxPerUser = 2;
    bool enabled = true;

private:
    void Resolve(std::string key, std::string user);
    void Download(const SongRequest& req);
    static std::optional<std::string> ExtractKey(const std::string& arg);

    std::mutex mtx;
    std::vector<SongRequest> queue;
    std::function<void()> onChanged;
};

} // namespace TwitchBsr
