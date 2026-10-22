#include "main.hpp"

#include "BsrQueue.hpp"
#include "TwitchIRC.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/Canvas.hpp"
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

#include <deque>
#include <fstream>

using namespace TwitchBsr;

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// ---------------------------------------------------------------- config --
// Stored as a tiny text file: line 1 = twitch username, line 2 = show chat (0/1)
static constexpr const char* kCfg = "/sdcard/ModData/com.beatgames.beatsaber/Configs/twitch-bsr.txt";
static std::string g_username;
static bool g_showChat = true;
static int g_maxLines = 8;

static void LoadConfig() {
    std::ifstream f(kCfg);
    std::string a, b;
    std::getline(f, a);
    std::getline(f, b);
    g_username = a;
    g_showChat = (b != "0");
}
static void SaveConfig() {
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
    if (g_chatGo && g_chatGo->m_CachedPtr.m_value) return;  // still alive
    g_chatGo = UnityEngine::GameObject::New_ctor("TwitchBsrChat");
    UnityEngine::Object::DontDestroyOnLoad(g_chatGo);
    auto canvas = g_chatGo->AddComponent<UnityEngine::Canvas*>();
    canvas->set_renderMode(UnityEngine::RenderMode::WorldSpace);
    auto rt = g_chatGo->GetComponent<UnityEngine::RectTransform*>();
    rt->set_sizeDelta({120.0f, 80.0f});
    // Left of the player, tilted slightly in; tweak to taste / expose in UI later.
    g_chatGo->get_transform()->set_position({-2.4f, 1.6f, 2.2f});
    g_chatGo->get_transform()->set_eulerAngles({0.0f, -25.0f, 0.0f});
    g_chatGo->get_transform()->set_localScale({0.01f, 0.01f, 0.01f});
    g_chatText = BSML::Lite::CreateText(g_chatGo->get_transform(), "", {0, 0}, {120.0f, 80.0f});
    g_chatText->set_fontSize(4.0f);
    g_chatText->set_enableWordWrapping(true);
    g_chatText->set_richText(true);
}

static void RefreshOverlay() {
    EnsureOverlay();
    if (!g_chatText) return;
    g_chatGo->SetActive(g_showChat);
    std::string all;
    for (auto& l : g_lines) all += l + "\n";
    g_chatText->set_text(all);
}

// Runs on the Unity main thread (hooked below).
static void Pump() {
    auto msgs = TwitchIRC::Get().Drain();
    if (msgs.empty()) return;
    for (auto& m : msgs) {
        bool privileged = m.isMod || m.isBroadcaster;
        if (BsrQueue::Get().HandleChat(m.user, m.text, privileged)) continue;  // hide commands from chat view
        std::string color = m.colorHex.empty() ? "#9146FF" : m.colorHex;
        g_lines.push_back("<b><color=" + color + ">" + Escape(m.user) + "</color></b>: " + Escape(m.text));
        while ((int)g_lines.size() > g_maxLines) g_lines.pop_front();
    }
    RefreshOverlay();
}

MAKE_HOOK_MATCH(Preview_Update, &GlobalNamespace::SongPreviewPlayer::Update, void,
                GlobalNamespace::SongPreviewPlayer* self) {
    Preview_Update(self);
    Pump();
}
MAKE_HOOK_MATCH(Sync_Update, &GlobalNamespace::AudioTimeSyncController::Update, void,
                GlobalNamespace::AudioTimeSyncController* self) {
    Sync_Update(self);
    Pump();
}

// ------------------------------------------------------------ BSML Lite UI --
// Construct native Quest BSML Lite controls, matching Darth Maul's approach.
static std::string g_statusOverride;
static UnityW<TMPro::TMP_Text> g_settingsStatus;
static UnityW<TMPro::TMP_Text> g_modsStatus;
static UnityW<TMPro::TMP_Text> g_settingsQueue;
static UnityW<TMPro::TMP_Text> g_modsQueue;

static std::string StatusString() {
    auto& irc = TwitchIRC::Get();
    return !g_statusOverride.empty() ? g_statusOverride :
        (irc.IsConnected() ? "Connected to #" + irc.Channel() : "Not connected");
}
static std::string QueueString() {
    std::string out;
    int i = 1;
    for (auto& r : BsrQueue::Get().Snapshot())
        out += std::to_string(i++) + ". " + r.songName + " [" + r.key + "] - " + r.requester + "\n";
    return out.empty() ? "Queue empty (viewers: !bsr <key>)" : out;
}
static void RefreshPageText() {
    if (g_settingsStatus.isAlive()) g_settingsStatus->set_text(StatusString());
    if (g_modsStatus.isAlive()) g_modsStatus->set_text(StatusString());
    if (g_settingsQueue.isAlive()) g_settingsQueue->set_text(QueueString());
    if (g_modsQueue.isAlive()) g_modsQueue->set_text(QueueString());
}
static void BuildBsrPage(HMUI::ViewController* self, bool firstActivation,
                         UnityW<TMPro::TMP_Text>& status, UnityW<TMPro::TMP_Text>& queue) {
    if (!firstActivation) { RefreshPageText(); return; }
    auto container = BSML::Lite::CreateScrollableSettingsContainer(self->get_transform());
    auto parent = container->get_transform();
    BSML::Lite::CreateText(parent, "Twitch BSR - Quest");
    BSML::Lite::CreateText(parent, "Twitch channel (no #):");
    BSML::Lite::CreateStringSetting(parent, "Channel", g_username,
        [](StringW value) { g_username = std::string(value); SaveConfig(); });
    BSML::Lite::CreateUIButton(parent, "Connect", [] {
        g_statusOverride.clear();
        if (g_username.empty() || !TwitchIRC::Get().Connect(g_username))
            g_statusOverride = "Enter a valid channel name";
        else { SaveConfig(); g_statusOverride = "Connecting..."; }
        RefreshPageText();
    });
    BSML::Lite::CreateUIButton(parent, "Disconnect", [] {
        TwitchIRC::Get().Disconnect(); g_statusOverride.clear(); RefreshPageText();
    });
    status = BSML::Lite::CreateText(parent, StatusString());
    BSML::Lite::CreateToggle(parent, "Enable !bsr requests", BsrQueue::Get().enabled,
        [](bool enabled) { BsrQueue::Get().enabled = enabled; });
    BSML::Lite::CreateToggle(parent, "Show chat in game", g_showChat,
        [](bool enabled) { g_showChat = enabled; SaveConfig(); RefreshOverlay(); });
    BSML::Lite::CreateText(parent, "Song request queue");
    queue = BSML::Lite::CreateText(parent, QueueString());
    BSML::Lite::CreateUIButton(parent, "Download all", [] { BsrQueue::Get().DownloadAll(); RefreshPageText(); });
    BSML::Lite::CreateUIButton(parent, "Clear queue", [] { BsrQueue::Get().Clear(); RefreshPageText(); });
}
static void OnSettingsDidActivate(HMUI::ViewController* self, bool firstActivation,
                                  bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_settingsStatus, g_settingsQueue);
}
static void OnModsDidActivate(HMUI::ViewController* self, bool firstActivation,
                              bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_modsStatus, g_modsQueue);
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
    BSML::Init();
    INFO("late_load reached, registering BSR menu");
    BSML::Register::RegisterSettingsMenu("Twitch BSR", OnSettingsDidActivate, false);
    BSML::Register::RegisterMainMenuViewControllerMethod(
        "Twitch BSR", "Twitch BSR", "Twitch chat and song requests", OnModsDidActivate);

    INSTALL_HOOK(Logger, Preview_Update);
    INSTALL_HOOK(Logger, Sync_Update);

    BsrQueue::Get().SetOnChanged([] {
        BSML::MainThreadScheduler::Schedule([] { RefreshPageText(); });
    });
    if (!g_username.empty()) TwitchIRC::Get().Connect(g_username);  // auto-reconnect on launch
}
