# Single-page UI rewrite (source-only)

## Why the previous multi-page UI failed
Page switching with raw Transform* + SetActive was unreliable on Quest.
Some home buttons (Twitch Connection, Chat Settings) never showed a menu,
and toggles / Test Chat Overlay produced no visible effect.

## New approach
One scrollable settings page built with BSML::Lite::CreateScrollableSettingsContainer.
Every control is always present and clickable — no hidden pages.

Sections (top to bottom):
1. Status + Channel
2. Connection (Edit hint, Connect/Save, Disconnect)
3. Chat Settings (Overlay ON/OFF, Test Chat Overlay, Position)
4. Song Requests (BSR ON/OFF, queue text, Download, Play Last, Clear)
5. Simple keyboard (6 keys per row + Backspace + Clear Channel)

## Feedback
- Every button logs with INFO "TwitchBSR: ..."
- Status line at the top updates on every action
- Toggle buttons update their own labels

## Still experimental
Not compiled or tested on Quest. If a button still does nothing, check logcat
for the corresponding "TwitchBSR:" line right after the press.
