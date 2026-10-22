# Twitch BSR (Quest standalone)

Beat Saber Plus-style Twitch chat + !bsr for Quest. Type a Twitch username in the mod's
settings page — no OAuth token.

## How "username only" works
Twitch lets anyone join a channel's chat **read-only** using an anonymous `justinfan#####`
login. That's all this mod uses, so the streamer never pastes a token. Trade-off: Twitch
does not allow anonymous connections to *send* chat, so the mod can't reply
("@viewer song added"). Replies would need a real OAuth token (or Twitch Device Code login),
which is the one thing you asked to avoid.

## Features
- Live chat overlay in menu and in songs (colored names, rich-text injection blocked)
- `!bsr <key>` / `!bsr <beatsaver url>`, `!oops` (viewer removes their last request)
- Limits: max queue size, max per user (mods/broadcaster bypass per-user limit)
- Queue UI + "Download all" (BeatSaver -> CustomLevels -> SongCore refresh)
- Auto-reconnect on drops, remembers the username

## Build
1. `packageVersion` in `mod.template.json` is already set to 1.40.8_7379 (latest moddable); change it only if you target another build.
2. `qpm restore` then `qpm qmod` (or push to GitHub and let Actions build it).
3. Install the .qmod with QuestPatcher/ModsBeta/Resaber-style tooling.

## Untested — expect to fix
This was written without compiling. Most likely trouble spots:
- Dependency version ranges in `qpm.json`
- BSML API names (`RegisterSettingsMenu`, `Lite::CreateText`, `NotifyChange`, `MainThreadScheduler`)
- SongCore refresh call and `web-utils` response types (zip extraction now uses the NDK zlib, no extra package)
- Hook targets `SongPreviewPlayer::Update` / `AudioTimeSyncController::Update` used as the main-thread pump
