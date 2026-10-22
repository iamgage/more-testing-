#include "TwitchIRC.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <random>

using namespace TwitchBsr;

TwitchIRC& TwitchIRC::Get() {
    static TwitchIRC inst;
    return inst;
}

std::optional<std::string> TwitchIRC::SanitizeChannel(std::string in) {
    // Accept "name", "@name", "#name", or a twitch.tv URL.
    auto pos = in.rfind('/');
    if (pos != std::string::npos) in = in.substr(pos + 1);
    std::string out;
    for (char c : in) {
        if (c == '@' || c == '#' || std::isspace((unsigned char)c)) continue;
        out += (char)std::tolower((unsigned char)c);
    }
    if (out.size() < 3 || out.size() > 25) return std::nullopt;
    for (char c : out)
        if (!(std::isalnum((unsigned char)c) || c == '_')) return std::nullopt;
    return out;
}

std::string TwitchIRC::Channel() const {
    std::lock_guard<std::mutex> l(mtx);
    return channel_;
}

bool TwitchIRC::Connect(std::string channel) {
    auto clean = SanitizeChannel(channel);
    if (!clean) return false;
    Disconnect();
    {
        std::lock_guard<std::mutex> l(mtx);
        channel_ = *clean;
        inbox.clear();
    }
    int gen = ++generation;
    worker = std::thread([this, c = *clean, gen] { Run(c, gen); });
    worker.detach();
    return true;
}

void TwitchIRC::Disconnect() {
    ++generation;  // tells any running worker to stop
    int s = sock.exchange(-1);
    if (s >= 0) { shutdown(s, SHUT_RDWR); close(s); }
    connected = false;
}

std::deque<ChatMessage> TwitchIRC::Drain() {
    std::lock_guard<std::mutex> l(mtx);
    std::deque<ChatMessage> out;
    out.swap(inbox);
    return out;
}

static bool SendAll(int s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        ssize_t n = send(s, d.data() + off, d.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static std::map<std::string, std::string> ParseTags(const std::string& raw) {
    std::map<std::string, std::string> tags;
    size_t i = 0;
    while (i < raw.size()) {
        size_t end = raw.find(';', i);
        if (end == std::string::npos) end = raw.size();
        std::string kv = raw.substr(i, end - i);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) tags[kv.substr(0, eq)] = kv.substr(eq + 1);
        i = end + 1;
    }
    return tags;
}

static std::string Unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 's') ? ' ' : n;
        } else o += s[i];
    }
    return o;
}

void TwitchIRC::Run(std::string channel, int gen) {
    int backoff = 2;
    while (generation.load() == gen) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        int s = -1;
        if (getaddrinfo("irc.chat.twitch.tv", "6667", &hints, &res) == 0) {
            for (auto* p = res; p; p = p->ai_next) {
                s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (s < 0) continue;
                timeval tv{60, 0};  // Twitch PINGs ~every 5 min; recv timeout lets us notice dead links
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                if (connect(s, p->ai_addr, p->ai_addrlen) == 0) break;
                close(s); s = -1;
            }
            freeaddrinfo(res);
        }
        if (s >= 0 && generation.load() == gen) {
            sock = s;
            std::mt19937 rng(std::random_device{}());
            std::string nick = "justinfan" + std::to_string(10000 + rng() % 80000);
            bool ok = SendAll(s, "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n") &&
                      SendAll(s, "PASS SCHMOOPIIE\r\n") &&
                      SendAll(s, "NICK " + nick + "\r\n") &&
                      SendAll(s, "JOIN #" + channel + "\r\n");
            if (ok) {
                connected = true;
                backoff = 2;
                std::string buf;
                char tmp[4096];
                while (generation.load() == gen) {
                    ssize_t n = recv(s, tmp, sizeof tmp, 0);
                    if (n <= 0) break;
                    buf.append(tmp, (size_t)n);
                    size_t nl;
                    while ((nl = buf.find("\r\n")) != std::string::npos) {
                        std::string line = buf.substr(0, nl);
                        buf.erase(0, nl + 2);
                        if (line.rfind("PING", 0) == 0) {
                            SendAll(s, "PONG" + line.substr(4) + "\r\n");
                            continue;
                        }
                        size_t pm = line.find(" PRIVMSG #");
                        if (pm == std::string::npos) continue;
                        std::map<std::string, std::string> tags;
                        if (!line.empty() && line[0] == '@') {
                            size_t sp = line.find(' ');
                            tags = ParseTags(line.substr(1, sp - 1));
                        }
                        size_t colon = line.find(" :", pm);
                        if (colon == std::string::npos) continue;
                        ChatMessage m;
                        m.text = line.substr(colon + 2);
                        if (m.text.rfind("\x01" "ACTION ", 0) == 0) {
                            m.text = m.text.substr(8);
                            if (!m.text.empty() && m.text.back() == '\x01') m.text.pop_back();
                        }
                        m.user = Unescape(tags["display-name"]);
                        if (m.user.empty()) {
                            size_t b = line.find('!');
                            size_t st = line.find(':');
                            if (st != std::string::npos && b != std::string::npos && b > st)
                                m.user = line.substr(st + 1, b - st - 1);
                        }
                        m.colorHex = tags["color"];
                        std::string badges = tags["badges"];
                        m.isMod = tags["mod"] == "1" || badges.find("moderator/") != std::string::npos;
                        m.isBroadcaster = badges.find("broadcaster/") != std::string::npos;
                        m.isSub = tags["subscriber"] == "1";
                        std::lock_guard<std::mutex> l(mtx);
                        inbox.push_back(std::move(m));
                        if (inbox.size() > 200) inbox.pop_front();
                    }
                }
            }
            connected = false;
            int expected = s;
            sock.compare_exchange_strong(expected, -1);
            close(s);
        }
        // Reconnect with capped backoff unless we were told to stop.
        for (int i = 0; i < backoff * 10 && generation.load() == gen; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoff = std::min(backoff * 2, 30);
    }
}
