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

// ----------------------------------------------------------------- UI ------
// A BSML settings page: username box, connect, chat toggle, request queue.
DECLARE_CLASS_CODEGEN(TwitchBsr, SettingsHost, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(StringW, username);
    DECLARE_INSTANCE_FIELD(bool, showChat);
    DECLARE_INSTANCE_FIELD(bool, bsrEnabled);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, statusText);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, queueTextUi);
    DECLARE_INSTANCE_METHOD(void, OnConnect);
    DECLARE_INSTANCE_METHOD(void, OnDisconnect);
    DECLARE_INSTANCE_METHOD(void, OnDownloadAll);
    DECLARE_INSTANCE_METHOD(void, OnClearQueue);
    DECLARE_INSTANCE_METHOD(void, OnToggled);
    DECLARE_INSTANCE_METHOD(void, Refresh);
};
DEFINE_TYPE(TwitchBsr, SettingsHost);

static UnityW<TwitchBsr::SettingsHost> g_settingsHost;
static UnityW<TwitchBsr::SettingsHost> g_modsHost;

static std::string g_statusOverride;

void TwitchBsr::SettingsHost::Refresh() {
    auto& irc = TwitchIRC::Get();
    std::string st = !g_statusOverride.empty() ? g_statusOverride
                   : irc.IsConnected() ? "Connected to #" + irc.Channel() : "Not connected";
    std::string q;
    int i = 1;
    for (auto& r : BsrQueue::Get().Snapshot())
        q += std::to_string(i++) + ". " + r.songName + " [" + r.key + "] - " + r.requester + "\n";
    if (q.empty()) q = "(queue empty - viewers type !bsr <key>)";
    if (statusText) statusText->set_text(st);
    if (queueTextUi) queueTextUi->set_text(q);
}
void TwitchBsr::SettingsHost::OnConnect() {
    g_username = std::string(username);
    g_statusOverride.clear();
    if (!TwitchIRC::Get().Connect(g_username)) g_statusOverride = "Invalid Twitch username";
    else { SaveConfig(); g_statusOverride = "Connecting..."; }
    Refresh();
}
void TwitchBsr::SettingsHost::OnDisconnect() { g_statusOverride.clear(); TwitchIRC::Get().Disconnect(); Refresh(); }
void TwitchBsr::SettingsHost::OnDownloadAll() { BsrQueue::Get().DownloadAll(); }
void TwitchBsr::SettingsHost::OnClearQueue() { BsrQueue::Get().Clear(); Refresh(); }
void TwitchBsr::SettingsHost::OnToggled() {
    g_showChat = showChat;
    BsrQueue::Get().enabled = bsrEnabled;
    SaveConfig();
    RefreshOverlay();
}

// Use basic BSML components rather than tabs; older Quest BSML builds can fail
// parsing a tab-selector and prevent the entire settings screen from appearing.
static const char* kBsml = R"(
<vertical child-control-height='false' child-expand-height='false' spacing='1' pad='2'>
  <text text='Twitch BSR' font-size='6' align='Center'/>
  <text text='Twitch channel (without #)' font-size='3.5'/>
  <string-setting text='Channel' value='username' apply-on-change='true'/>
  <horizontal spacing='2'>
    <button text='Connect' on-click='OnConnect'/>
    <button text='Disconnect' on-click='OnDisconnect'/>
  </horizontal>
  <text id='statusText' text='Not connected' font-size='3.5'/>
  <toggle-setting text='Enable !bsr requests' value='bsrEnabled' apply-on-change='true' on-change='OnToggled'/>
  <toggle-setting text='Show chat in game' value='showChat' apply-on-change='true' on-change='OnToggled'/>
  <text text='Song request queue' font-size='4'/>
  <text id='queueTextUi' text='Queue empty' font-size='3' word-wrapping='true'/>
  <horizontal spacing='2'>
    <button text='Download all' on-click='OnDownloadAll'/>
    <button text='Clear queue' on-click='OnClearQueue'/>
  </horizontal>
</vertical>
)";

// Each view owns its own host. Never reuse a raw component pointer across menus.
static void BuildBsrPage(HMUI::ViewController* self, bool firstActivation,
                         UnityW<TwitchBsr::SettingsHost>& host) {
    if (!firstActivation && host.isAlive()) {
        host->Refresh();
        return;
    }
    auto root = self->get_gameObject();
    auto component = root->AddComponent<TwitchBsr::SettingsHost*>();
    host = component;
    component->username = g_username;
    component->showChat = g_showChat;
    component->bsrEnabled = BsrQueue::Get().enabled;
    BSML::parse_and_construct(kBsml, root->get_transform(), component);
    component->Refresh();
}

static void OnSettingsDidActivate(HMUI::ViewController* self, bool firstActivation,
                                  bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_settingsHost);
}

static void OnModsDidActivate(HMUI::ViewController* self, bool firstActivation,
                              bool addedToHierarchy, bool screenSystemEnabling) {
    BuildBsrPage(self, firstActivation, g_modsHost);
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
        BSML::MainThreadScheduler::Schedule([] { if (g_settingsHost.isAlive()) g_settingsHost->Refresh(); if (g_modsHost.isAlive()) g_modsHost->Refresh(); });
    });
    if (!g_username.empty()) TwitchIRC::Get().Connect(g_username);  // auto-reconnect on launch
}
