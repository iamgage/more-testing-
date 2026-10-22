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
        INFO("TwitchBSR: chat overlay created");
    }
    auto canvas = g_chatGo->GetComponent<UnityEngine::Canvas*>();
    if (canvas) canvas->set_worldCamera(camera);
    auto transform = g_chatGo->get_transform();
    transform->SetParent(camera->get_transform(), false);
    transform->set_localPosition({-0.30f, 0.05f, 1.25f});
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
static UnityW<TMPro::TMP_Text> g_settingsQueue;
static UnityW<TMPro::TMP_Text> g_modsQueue;
static UnityW<TMPro::TMP_Text> g_settingsInput;
static UnityW<TMPro::TMP_Text> g_modsInput;

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
        out += std::to_string(i++) + ". [" + r.key + "] " + r.requester + "\n";
    }
    return out.empty() ? "Queue empty (viewers use !bsr key)" : out;
}
static void RefreshPageText() {
    if (g_settingsInput.isAlive()) g_settingsInput->set_text("Channel: " + g_username);
    if (g_modsInput.isAlive()) g_modsInput->set_text("Channel: " + g_username);
    if (g_settingsStatus.isAlive()) g_settingsStatus->set_text(StatusString());
    if (g_modsStatus.isAlive()) g_modsStatus->set_text(StatusString());
    if (g_settingsQueue.isAlive()) g_settingsQueue->set_text(QueueString());
    if (g_modsQueue.isAlive()) g_modsQueue->set_text(QueueString());
}
static void BuildBsrPage(HMUI::ViewController* self, bool firstActivation,
                         UnityW<TMPro::TMP_Text>& status, UnityW<TMPro::TMP_Text>& queue,
                         UnityW<TMPro::TMP_Text>& input) {
    INFO("TwitchBSR: BuildBsrPage called");
    if (!firstActivation) { RefreshPageText(); return; }

    // Use the scrollable settings container's built-in vertical layout.
    // Avoid horizontal rows of full-size BSML buttons: on Quest those
    // controls have a large minimum width and overlap neighbouring buttons.
    auto container = BSML::Lite::CreateScrollableSettingsContainer(self->get_transform());
    auto parent = container->get_transform();
    input = BSML::Lite::CreateText(parent, "Channel: " + g_username);

    auto home = BSML::Lite::CreateVerticalLayoutGroup(parent);
    auto connection = BSML::Lite::CreateVerticalLayoutGroup(parent);
    auto chat = BSML::Lite::CreateVerticalLayoutGroup(parent);
    auto requests = BSML::Lite::CreateVerticalLayoutGroup(parent);
    auto keyboard = BSML::Lite::CreateVerticalLayoutGroup(parent);
    auto show = [home, connection, chat, requests, keyboard](int page) {
        home->get_gameObject()->SetActive(page == 0);
        connection->get_gameObject()->SetActive(page == 1);
        chat->get_gameObject()->SetActive(page == 2);
        requests->get_gameObject()->SetActive(page == 3);
        keyboard->get_gameObject()->SetActive(page == 4);
    };

    // Main navigation is vertical: only three large, non-overlapping buttons.
    BSML::Lite::CreateText(home->get_transform(), "Twitch BSR");
    BSML::Lite::CreateUIButton(home->get_transform(), "Twitch Connection", [show] { show(1); });
    BSML::Lite::CreateUIButton(home->get_transform(), "Chat Settings", [show] { show(2); });
    BSML::Lite::CreateUIButton(home->get_transform(), "Song Requests", [show] { show(3); });

    auto conn = connection->get_transform();
    BSML::Lite::CreateText(conn, "Twitch Connection");
    status = BSML::Lite::CreateText(conn, StatusString());
    BSML::Lite::CreateUIButton(conn, "Edit Channel Name", [show] { show(4); });
    BSML::Lite::CreateUIButton(conn, "Connect / Save", [] {
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
    BSML::Lite::CreateUIButton(conn, "Disconnect", [] {
        TwitchIRC::Get().Disconnect();
        g_statusOverride = "Disconnected";
        RefreshPageText();
    });
    BSML::Lite::CreateUIButton(conn, "Back", [show] { show(0); });

    auto ch = chat->get_transform();
    BSML::Lite::CreateText(ch, "Chat Settings");
    BSML::Lite::CreateToggle(ch, "Show Chat Overlay", g_showChat, [](bool enabled) {
        g_showChat = enabled;
        SaveConfig();
        if (!enabled) {
            if (g_chatGo && g_chatGo->m_CachedPtr.m_value) g_chatGo->SetActive(false);
        } else {
            BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });
        }
    });
    BSML::Lite::CreateUIButton(ch, "Test Chat Overlay", [] {
        g_lines.push_back("<color=#9146FF>TEST:</color> Chat overlay test");
        while ((int)g_lines.size() > g_maxLines) g_lines.pop_front();
        BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });
    });
    BSML::Lite::CreateUIButton(ch, "Reposition Chat", [] {
        BSML::MainThreadScheduler::Schedule([] { RefreshOverlay(); });
    });
    BSML::Lite::CreateUIButton(ch, "Back", [show] { show(0); });

    auto req = requests->get_transform();
    BSML::Lite::CreateText(req, "Song Requests");
    BSML::Lite::CreateToggle(req, "Enable BSR", BsrQueue::Get().enabled,
        [](bool enabled) { BsrQueue::Get().enabled = enabled; });
    queue = BSML::Lite::CreateText(req, "Queue empty");
    BSML::Lite::CreateUIButton(req, "Download Queued Songs", [] {
        BsrQueue::Get().DownloadAll();
        RefreshPageText();
    });
    BSML::Lite::CreateUIButton(req, "Play Last Download", [] {
        BSML::MainThreadScheduler::Schedule([] { PlayLatestDownloaded(); });
    });
    BSML::Lite::CreateUIButton(req, "Clear Queue", [] {
        BsrQueue::Get().Clear();
        RefreshPageText();
    });
    BSML::Lite::CreateUIButton(req, "Back", [show] { show(0); });

    // A full keyboard cannot fit BSML Lite's default button widths in a
    // horizontal row on Quest. Show six vertical keys per page instead.
    auto kb = keyboard->get_transform();
    BSML::Lite::CreateText(kb, "Edit Channel");
    auto pages = std::make_shared<std::vector<UnityEngine::GameObject*>>();
    const std::string keys = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    for (size_t offset = 0; offset < keys.size(); offset += 6) {
        auto group = BSML::Lite::CreateVerticalLayoutGroup(kb);
        pages->push_back(group->get_gameObject());
        for (char c : keys.substr(offset, 6)) {
            BSML::Lite::CreateUIButton(group->get_transform(), std::string(1, c),
                [c] { TypeKey(c); });
        }
    }
    auto pageIndex = std::make_shared<size_t>(0);
    auto setPage = [pages, pageIndex](size_t page) {
        *pageIndex = page;
        for (size_t i = 0; i < pages->size(); ++i)
            (*pages)[i]->SetActive(i == page);
    };
    BSML::Lite::CreateUIButton(kb, "Next Letters", [pages, pageIndex, setPage] {
        setPage((*pageIndex + 1) % pages->size());
    });
    BSML::Lite::CreateUIButton(kb, "Previous Letters", [pages, pageIndex, setPage] {
        setPage((*pageIndex + pages->size() - 1) % pages->size());
    });
    BSML::Lite::CreateUIButton(kb, "Backspace", [] {
        if (!g_username.empty()) g_username.pop_back();
        RefreshPageText();
    });
    BSML::Lite::CreateUIButton(kb, "Done", [show] { show(1); });
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
