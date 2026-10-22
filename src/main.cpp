#include "main.hpp"

#include "BsrQueue.hpp"
#include "TwitchIRC.hpp"

#include "HMUI/ViewController.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/RenderMode.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "TMPro/TMP_Text.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/Helpers/creation.hpp"
#include "custom-types/shared/macros.hpp"
#include "GlobalNamespace/LevelSelectionFlowCoordinator.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/SelectLevelCategoryViewController.hpp"
#include "HMUI/NoTransitionsButton.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "songcore/shared/SongCore.hpp"
#include "System/Nullable_1.hpp"

#include <deque>
#include <functional>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <memory>
#include <vector>

using namespace TwitchBsr;

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// ---------------------------------------------------------------- config --
// Stored as a tiny text file: line 1 = twitch username, line 2 = show chat (0/1)
static constexpr const char* kCfg = "/sdcard/ModData/com.beatgames.beatsaber/Configs/twitch-bsr.txt";
static std::string g_username;
static bool g_showChat = true;
static int g_maxLines = 6;
static int g_chatPosition = 0;

static void LoadConfig() {
    std::ifstream f(kCfg);
    std::string a, b;
    std::getline(f, a);
    std::getline(f, b);
    g_username = a;
    g_showChat = (b != "0");
}
static void SaveConfig() {
    std::filesystem::create_directories("/sdcard/ModData/com.beatgames.beatsaber/Configs");
    std::ofstream f(kCfg, std::ios::trunc);
    f << g_username << "\n" << (g_showChat ? "1" : "0") << "\n";
}

// ----------------------------------------------------------- chat overlay --
static UnityEngine::GameObject* g_chatGo = nullptr;
static TMPro::TextMeshProUGUI* g_chatText = nullptr;
static std::deque<std::string> g_lines;

static std::string Escape(std::string s) {
    // TMP rich text: neutralise '<' so chatters can't inject tags.
    std::string o;
    for (char c : s) o += (c == '<') ? std::string("<noparse><</noparse>") : std::string(1, c);
    return o;
}

static void EnsureOverlay() {
    // A chat panel must exist before the first IRC message arrives.
    // Attach to the active player camera, not a fixed world position.
    auto camera = UnityEngine::Camera::get_main();
    if (!camera || !camera->m_CachedPtr.m_value) {
        INFO("TwitchBSR: chat overlay waiting for main camera");
        return;
    }
    if (!g_chatGo || !g_chatGo->m_CachedPtr.m_value) {
        g_chatGo = UnityEngine::GameObject::New_ctor("TwitchBsrChatOverlay");
        UnityEngine::Object::DontDestroyOnLoad(g_chatGo);
        auto canvas = g_chatGo->AddComponent<UnityEngine::Canvas*>();
        canvas->set_renderMode(UnityEngine::RenderMode::WorldSpace);
        canvas->set_sortingOrder(100);
        auto rt = g_chatGo->GetComponent<UnityEngine::RectTransform*>();
        rt->set_sizeDelta({100.0f, 65.0f});
        g_chatText = BSML::Lite::CreateText(g_chatGo->get_transform(),
            "TWITCH CHAT\nWaiting for messages...", {0.0f, 0.0f}, {96.0f, 62.0f});
        g_chatText->set_fontSize(5.0f);
        g_chatText->set_enableWordWrapping(true);
        g_chatText->set_richText(true);
        g_chatText->set_enableAutoSizing(false);
        g_chatText->set_raycastTarget(false);
        INFO("TwitchBSR: chat overlay created");
    }
    auto canvas = g_chatGo->GetComponent<UnityEngine::Canvas*>();
    if (canvas) canvas->set_worldCamera(camera);
    auto transform = g_chatGo->get_transform();
    transform->SetParent(camera->get_transform(), false);
    // Cycle through left, right, and centre without recreating the canvas.
    float x = g_chatPosition == 0 ? -0.30f : (g_chatPosition == 1 ? 0.30f : 0.0f);
    transform->set_localPosition({x, 0.05f, 1.25f});
    transform->set_localEulerAngles({0.0f, 0.0f, 0.0f});
    transform->set_localScale({0.009f, 0.009f, 0.009f});
}

static void RefreshOverlay() {
    if (!g_showChat) { if (g_chatGo && g_chatGo->m_CachedPtr.m_value) g_chatGo->SetActive(false); return; }
    EnsureOverlay();
    if (!g_chatText || !g_chatGo || !g_chatGo->m_CachedPtr.m_value) return;
    INFO("TwitchBSR: refreshing chat overlay, lines={}", g_lines.size());
    g_chatGo->SetActive(g_showChat);
    std::string all;
    for (auto& l : g_lines) all += l + "\n";
    g_chatText->set_text(all.empty() ? "TWITCH CHAT\nWaiting for messages..." : all);
}

// Runs on the Unity main thread (hooked below).
// Navigate to the latest downloaded song, following BetterSongSearchQuest's
// LevelSelectionFlowCoordinator::State + SoloFreePlayFlowCoordinator::Setup pattern.
static void PlayLatestDownloaded() {
    auto req = BsrQueue::Get().LastDownloaded();
    if (!req || req->hash.empty()) { INFO("Play Song: no downloaded map yet"); return; }
    auto level = SongCore::API::Loading::GetLevelByHash(req->hash);
    if (!level) { INFO("Play Song: song not loaded by SongCore yet: {}", req->hash); return; }
    auto pack = SongCore::API::Loading::GetCustomLevelPack();
    if (!pack) { INFO("Play Song: custom level pack unavailable"); return; }
    auto container = BSML::Helpers::GetDiContainer();
    if (!container) { INFO("Play Song: menu DI container unavailable"); return; }
    auto solo = container->Resolve<GlobalNamespace::SoloFreePlayFlowCoordinator*>();
    if (!solo) { INFO("Play Song: solo coordinator unavailable"); return; }
    auto state = GlobalNamespace::LevelSelectionFlowCoordinator::State::New_ctor(
        pack, static_cast<GlobalNamespace::BeatmapLevel*>(level));
    auto category = System::Nullable_1<GlobalNamespace::SelectLevelCategoryViewController::LevelCategory>();
    category.value = GlobalNamespace::SelectLevelCategoryViewController::LevelCategory(
        GlobalNamespace::SelectLevelCategoryViewController::LevelCategory::All);
    category.hasValue = true;
    state->___levelCategory = category;
    solo->Setup(state);
    auto soloButton = UnityEngine::GameObject::Find("SoloButton");
    if (soloButton) {
        auto button = soloButton->GetComponent<HMUI::NoTransitionsButton*>();
        if (button) button->Press();
    } else {
        INFO("Play Song: SoloButton not found; return to main menu and try again");
    }
}

static void RefreshPageText();
static void Pump() {
    static auto lastRefresh = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - lastRefresh > std::chrono::seconds(1)) {
        lastRefresh = now;
        RefreshPageText();
    }
    auto msgs = TwitchIRC::Get().Drain();
    if (msgs.empty()) return;
    for (auto& m : msgs) {
        bool privileged = m.isMod || m.isBroadcaster;
        BsrQueue::Get().HandleChat(m.user, m.text, privileged);  // process commands AND show them in chat
        std::string color = m.colorHex.empty() ? "#9146FF" : m.colorHex;
        g_lines.push_back("<b><color=" + color + ">" + Escape(m.user) + "</color></b>: " + Escape(m.text));
        while ((int)g_lines.size() > g_maxLines) g_lines.pop_front();
    }
    RefreshOverlay();
}

// IRC messages are delivered to Unity's main thread via BSML's scheduler.
// No ARM64 hooks or custom IL2CPP MonoBehaviour registration are required.
void ScheduleTwitchBsrPump() {
    BSML::MainThreadScheduler::Schedule([] { Pump(); });
}

// ------------------------------------------------------------ BSML Lite UI --
// Construct native Quest BSML Lite controls, matching Darth Maul's approach.
static std::string g_statusOverride;
static void RefreshPageText();
// One character per key; no multi-tap ambiguity.
static void TypeKey(char c) {
    if (g_username.size() < 25) g_username.push_back(c);
    RefreshPageText();
}

static UnityW<TMPro::TMP_Text> g_settingsStatus;
static UnityW<TMPro::TMP_Text> g_modsStatus;
static UnityW<TMPro::TMP_Text> g_homeStatus;  // home-page status line
static UnityW<TMPro::TMP_Text> g_settingsQueue;
static UnityW<TMPro::TMP_Text> g_modsQueue;
static UnityW<TMPro::TMP_Text> g_settingsInput;
static UnityW<TMPro::TMP_Text> g_modsInput;
static std::vector<UnityW<TMPro::TMP_Text>> g_kbInputs;

static std::string StatusString() {
    auto& irc = TwitchIRC::Get();
    if (irc.IsConnected()) return "Connected to #" + irc.Channel();
    return g_statusOverride.empty() ? "Not connected" : g_statusOverride;
}
static std::string QueueString() {
    std::string out;
    int i = 1;
    for (auto& r : BsrQueue::Get().Snapshot()) {
        if (i > 3) { out += "...more requests queued"; break; }
        out += std::to_string(i++) + ". [" + r.key + "] " + (r.downloaded ? "Downloaded" : "Queued") + "\n";
    }
    return out.empty() ? "Queue empty (viewers use !bsr key)" : out;
}
static void RefreshPageText() {
    if (g_settingsInput.isAlive()) g_settingsInput->set_text("Channel: " + g_username);
    if (g_modsInput.isAlive()) g_modsInput->set_text("Channel: " + g_username);
    for (auto& k : g_kbInputs)
        if (k.isAlive()) k->set_text("Channel: " + g_username);
    if (g_settingsStatus.isAlive()) g_settingsStatus->set_text(StatusString());
    if (g_modsStatus.isAlive()) g_modsStatus->set_text(StatusString());
    if (g_settingsQueue.isAlive()) g_settingsQueue->set_text(QueueString());
    if (g_modsQueue.isAlive()) g_modsQueue->set_text(QueueString());
    if (g_homeStatus.isAlive()) g_homeStatus->set_text(StatusString());
}
// ---- Fixed-position page layout -------------------------------------------
// Layout groups / scroll containers were not sizing their children on Quest, so
// every control collapsed onto the same spot. Instead each page is a plain
// full-size RectTransform and every control gets an explicit anchored position
// (units are relative to the centre of the settings panel), so nothing can stack.
static UnityEngine::Transform* MakePage(UnityEngine::Transform* parent, const char* name) {
    auto go = UnityEngine::GameObject::New_ctor(name);
    auto rt = go->AddComponent<UnityEngine::RectTransform*>();
    rt->SetParent(parent, false);
    rt->set_anchorMin({0.0f, 0.0f});
    rt->set_anchorMax({1.0f, 1.0f});
    rt->set_offsetMin({0.0f, 0.0f});
    rt->set_offsetMax({0.0f, 0.0f});
    return rt->get_transform();
}
static TMPro::TMP_Text* PageText(UnityEngine::Transform* p, const std::string& t,
                                 float x, float y, float w, float h, float size) {
    auto txt = BSML::Lite::CreateText(p, t, {x, y}, {w, h});
    txt->set_fontSize(size);
    txt->set_alignment(TMPro::TextAlignmentOptions::Center);
    txt->set_enableWordWrapping(true);
    return txt;
}
static UnityEngine::UI::Button* PageButton(UnityEngine::Transform* p, const std::string& label,
                       float x, float y, float w, std::function<void()> cb) {
    return BSML::Lite::CreateUIButton(p, label, {x, y}, {w, 8.0f}, std::move(cb));
}

static void BuildBsrPage(HMUI::ViewController* self, bool firstActivation,
                         UnityW<TMPro::TMP_Text>& status, UnityW<TMPro::TMP_Text>& queue,
                         UnityW<TMPro::TMP_Text>& input) {
    INFO("TwitchBSR: BuildBsrPage called firstActivation={}", firstActivation);
    if (!firstActivation) { RefreshPageText(); return; }

    auto root = self->get_transform();
    auto home = MakePage(root, "BsrHome");
    auto conn = MakePage(root, "BsrConnection");
    auto ch = MakePage(root, "BsrChat");
    auto req = MakePage(root, "BsrRequests");
    auto kb = MakePage(root, "BsrKeyboard");
    auto show = [home, conn, ch, req, kb](int page) {
        INFO("TwitchBSR: switching to page {}", page);
        if (home && home->m_CachedPtr.m_value) home->get_gameObject()->SetActive(page == 0);
        if (conn && conn->m_CachedPtr.m_value) conn->get_gameObject()->SetActive(page == 1);
        if (ch && ch->m_CachedPtr.m_value) ch->get_gameObject()->SetActive(page == 2);
        if (req && req->m_CachedPtr.m_value) req->get_gameObject()->SetActive(page == 3);
        if (kb && kb->m_CachedPtr.m_value) kb->get_gameObject()->SetActive(page == 4);
        RefreshPageText();
    };
    // Start with only the home page visible
    home->get_gameObject()->SetActive(true);
    conn->get_gameObject()->SetActive(false);
    ch->get_gameObject()->SetActive(false);
    req->get_gameObject()->SetActive(false);
    kb->get_gameObject()->SetActive(false);

    const float W = 64.0f;

    // Home: title + status + three navigation buttons
    PageText(home, "Twitch BSR", 0, 32, 90, 8, 6.0f);
    PageText(home, "Chat + !bsr song requests", 0, 24, 90, 6, 3.5f);
    g_homeStatus = PageText(home, StatusString(), 0, 16, 90, 6, 3.5f);
    PageButton(home, "Twitch Connection", 0, 5, W, [show] {
        INFO("TwitchBSR: Twitch Connection button pressed");
        g_statusOverride = "Opening Connection...";
        RefreshPageText();
        show(1);
    });
    PageButton(home, "Chat Settings", 0, -6, W, [show] {
        INFO("TwitchBSR: Chat Settings button pressed");
        g_statusOverride = "Opening Chat Settings...";
        RefreshPageText();
        show(2);
    });
    PageButton(home, "Song Requests", 0, -17, W, [show] {
        INFO("TwitchBSR: Song Requests button pressed");
        g_statusOverride = "Opening Song Requests...";
        RefreshPageText();
        show(3);
    });

    // Connection page
    PageText(conn, "Twitch Connection", 0, 33, 90, 8, 6.0f);
    status = PageText(conn, StatusString(), 0, 24, 90, 7, 4.0f);
    input = PageText(conn, "Channel: " + g_username, 0, 17, 90, 7, 4.5f);
    PageText(conn, "Use Edit to type channel, then Connect", 0, 10, 90, 5, 3.2f);
    PageButton(conn, "Edit Channel Name", 0, 2, W, [show] {
        INFO("TwitchBSR: Edit Channel Name pressed");
        show(4);
    });
    PageButton(conn, "Connect / Save", 0, -7, W, [] {
        INFO("TwitchBSR: Connect / Save pressed, channel={}", g_username);
        auto clean = TwitchIRC::SanitizeChannel(g_username);
        if (!clean) {
            g_statusOverride = "Invalid channel name";
        } else {
            g_username = *clean;
            SaveConfig();
            bool started = TwitchIRC::Get().Connect(g_username);
            g_statusOverride = started ? "Connecting..." : "Connection rejected";
        }
        RefreshPageText();
    });
    PageButton(conn, "Disconnect", 0, -16, W, [] {
        INFO("TwitchBSR: Disconnect pressed");
        TwitchIRC::Get().Disconnect();
        g_statusOverride = "Disconnected";
        RefreshPageText();
    });
    PageButton(conn, "Back", 0, -25, W, [show] { show(0); });

    // Chat settings
    PageText(ch, "Chat Settings", 0, 31, 90, 8, 6.0f);
    auto chatToggle = std::make_shared<UnityEngine::UI::Button*>(nullptr);
    *chatToggle = PageButton(ch, std::string("Chat Overlay: ") + (g_showChat ? "ON" : "OFF"), 0, 20, W,
        [chatToggle] {
            INFO("TwitchBSR: Chat Overlay toggle pressed, was={}", g_showChat);
            g_showChat = !g_showChat;
            SaveConfig();
            if (*chatToggle) BSML::Lite::SetButtonText(*chatToggle, std::string("Chat Overlay: ") + (g_showChat ? "ON" : "OFF"));
            if (!g_showChat) {
                if (g_chatGo && g_chatGo->m_CachedPtr.m_value) g_chatGo->SetActive(false);
            } else {
                BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });
            }
            g_statusOverride = g_showChat ? "Chat overlay ON" : "Chat overlay OFF";
            RefreshPageText();
        });
    PageButton(ch, "Test Chat Overlay", 0, 10.5f, W, [] {
        INFO("TwitchBSR: Test Chat Overlay pressed, showChat={}", g_showChat);
        if (!g_showChat) {
            g_statusOverride = "Enable overlay first";
            RefreshPageText();
            return;
        }
        g_lines.push_back("<color=#9146FF>TEST:</color> Chat overlay test");
        while ((int)g_lines.size() > g_maxLines) g_lines.pop_front();
        EnsureOverlay();
        BSML::MainThreadScheduler::Schedule([] {
            RefreshOverlay();
            INFO("TwitchBSR: Test overlay refreshed, lines={}", g_lines.size());
        });
        g_statusOverride = "Test message sent - look for overlay";
        RefreshPageText();
    });
    auto reposition = std::make_shared<UnityEngine::UI::Button*>(nullptr);
    *reposition = PageButton(ch, "Chat Position: Left", 0, 1, W, [reposition] {
        INFO("TwitchBSR: Chat Position pressed");
        g_chatPosition = (g_chatPosition + 1) % 3;
        const char* names[] = {"Left", "Right", "Centre"};
        if (*reposition) BSML::Lite::SetButtonText(*reposition, std::string("Chat Position: ") + names[g_chatPosition]);
        if (g_showChat) BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });
        g_statusOverride = std::string("Position: ") + names[g_chatPosition];
        RefreshPageText();
    });
    PageButton(ch, "Back", 0, -8.5f, W, [show] { show(0); });

    // Song requests
    PageText(req, "Song Requests", 0, 33, 90, 8, 6.0f);
    auto bsrToggle = std::make_shared<UnityEngine::UI::Button*>(nullptr);
    *bsrToggle = PageButton(req, std::string("BSR: ") + (BsrQueue::Get().enabled ? "ON" : "OFF"), 0, 24, W,
        [bsrToggle] {
            INFO("TwitchBSR: BSR toggle pressed, was={}", BsrQueue::Get().enabled);
            BsrQueue::Get().enabled = !BsrQueue::Get().enabled;
            if (*bsrToggle) BSML::Lite::SetButtonText(*bsrToggle, std::string("BSR: ") + (BsrQueue::Get().enabled ? "ON" : "OFF"));
            g_statusOverride = BsrQueue::Get().enabled ? "BSR enabled" : "BSR disabled";
            RefreshPageText();
        });
    queue = PageText(req, "Queue empty", 0, 12.5f, 90, 13, 3.8f);
    PageButton(req, "Download Queued Songs", 0, -2, W, [] {
        INFO("TwitchBSR: Download Queued Songs pressed");
        auto snap = BsrQueue::Get().Snapshot();
        if (snap.empty()) {
            g_statusOverride = "Queue is empty";
        } else {
            g_statusOverride = "Downloading " + std::to_string(snap.size()) + " song(s)...";
            BsrQueue::Get().DownloadAll();
        }
        RefreshPageText();
    });
    PageButton(req, "Play Last Download", 0, -11, W, [] {
        INFO("TwitchBSR: Play Last Download pressed");
        g_statusOverride = "Trying to play last download...";
        RefreshPageText();
        BSML::MainThreadScheduler::Schedule([] { PlayLatestDownloaded(); });
    });
    PageButton(req, "Clear Queue", 0, -20, W, [] {
        INFO("TwitchBSR: Clear Queue pressed");
        BsrQueue::Get().Clear();
        g_statusOverride = "Queue cleared";
        RefreshPageText();
    });
    PageButton(req, "Back", 0, -29, W, [show] { show(0); });

    // Keyboard: 12 keys per page in a 6x2 grid, then a centred column of controls.
    PageText(kb, "Edit Channel", 0, 34, 90, 7, 5.0f);
    g_kbInputs.push_back(PageText(kb, "Channel: " + g_username, 0, 26, 90, 7, 4.5f));
    auto keyButtons = std::make_shared<std::vector<UnityEngine::GameObject*>>();
    const std::string keys = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    for (size_t i = 0; i < keys.size(); ++i) {
        char c = keys[i];
        size_t slot = i % 12;
        float x = -27.5f + (slot % 6) * 11.0f;
        float y = (slot < 6) ? 15.0f : 5.0f;
        auto button = PageButton(kb, std::string(1, c), x, y, 10, [c] { TypeKey(c); });
        keyButtons->push_back(button->get_gameObject());
    }
    const size_t kPages = (keys.size() + 11) / 12;
    auto pageIndex = std::make_shared<size_t>(0);
    auto setPage = [keyButtons, pageIndex](size_t page) {
        *pageIndex = page;
        for (size_t i = 0; i < keyButtons->size(); ++i)
            (*keyButtons)[i]->SetActive(i / 12 == page);
    };
    PageButton(kb, "Next Keys", 0, -6, W, [pageIndex, setPage, kPages] {
        setPage((*pageIndex + 1) % kPages);
    });
    PageButton(kb, "Previous Keys", 0, -15, W, [pageIndex, setPage, kPages] {
        setPage((*pageIndex + kPages - 1) % kPages);
    });
    PageButton(kb, "Backspace", 0, -24, W, [] {
        if (!g_username.empty()) g_username.pop_back();
        RefreshPageText();
    });
    PageButton(kb, "Done", 0, -33, W, [show] { show(1); });
    setPage(0);
    show(0);
    RefreshPageText();
}
static void OnSettingsDidActivate(HMUI::ViewController* self, bool firstActivation,
                                  bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_settingsStatus, g_settingsQueue, g_settingsInput);
}
static void OnModsDidActivate(HMUI::ViewController* self, bool firstActivation,
                              bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_modsStatus, g_modsQueue, g_modsInput);
}

// ----------------------------------------------------------------- entry ----
MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    LoadConfig();
}

MOD_EXTERN_FUNC void late_load() noexcept {
    Paper::Logger::RegisterFileContextId(MOD_ID);
    il2cpp_functions::Init();
    custom_types::Register::AutoRegister();
    INFO("TwitchBSR: before BSML::Init");
    BSML::Init();
    INFO("TwitchBSR: BSML initialized; registering both menus");
    BSML::Register::RegisterSettingsMenu("Twitch BSR", OnSettingsDidActivate, false);
    BSML::Register::RegisterMainMenuViewControllerMethod(
        "Twitch BSR", "Twitch BSR", "Twitch chat and song requests", OnModsDidActivate);

    INFO("TwitchBSR: menu registration finished");
    INFO("TwitchBSR: event-driven chat pump enabled");
    BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });

    BsrQueue::Get().SetOnChanged([] {
        BSML::MainThreadScheduler::Schedule([] { RefreshPageText(); });
    });
    if (!g_username.empty()) TwitchIRC::Get().Connect(g_username);  // auto-reconnect on launch
}
