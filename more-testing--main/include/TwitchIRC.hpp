#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace TwitchBsr {

struct ChatMessage {
    std::string user;      // display name
    std::string colorHex;  // "#RRGGBB" or empty
    std::string text;
    bool isMod = false;
    bool isBroadcaster = false;
    bool isSub = false;
};

// Read-only anonymous Twitch chat client. Uses Twitch's documented anonymous
// "justinfan" login, so NO OAuth token is needed -- just a channel name.
// Limitation (by Twitch design): anonymous connections cannot SEND messages.
class TwitchIRC {
public:
    static TwitchIRC& Get();

    // Returns false if the username is not a valid Twitch login name.
    bool Connect(std::string channel);
    void Disconnect();

    bool IsConnected() const { return connected.load(); }
    std::string Channel() const;

    // Called from the Unity main thread; moves queued messages out.
    std::deque<ChatMessage> Drain();

    static std::optional<std::string> SanitizeChannel(std::string input);

private:
    void Run(std::string channel, int generation);
    std::atomic<bool> connected{false};
    std::atomic<int> generation{0};
    std::atomic<int> sock{-1};
    std::thread worker;
    mutable std::mutex mtx;
    std::deque<ChatMessage> inbox;
    std::string channel_;
};

} // namespace TwitchBsr
