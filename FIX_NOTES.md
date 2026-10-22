# Hardened button callbacks + visible feedback (source-only)

## Targeted fixes for the three reported dead buttons
1. **Twitch Connection** (home page)
   - Now logs on press, sets status "Opening Connection...", then switches page.
   - Connection page itself has instructional text so it never looks empty.

2. **BSR: ON/OFF** (Song Requests page)
   - Logs the previous state, flips the flag, updates the button label, writes status "BSR enabled/disabled".

3. **Test Chat Overlay** (Chat Settings page)
   - Logs the current showChat flag.
   - If overlay is off, status becomes "Enable overlay first".
   - Otherwise pushes a test line, calls EnsureOverlay(), schedules RefreshOverlay, and sets status "Test message sent - look for overlay".

## Supporting changes
- Home page now has a permanent status line (`g_homeStatus`) that updates on every action, so feedback is always visible even if you stay on Home.
- `show()` page switch is defensive (null/m_CachedPtr checks) and logs the target page.
- `RefreshPageText()` also refreshes the home status line.
- Every important button now emits an INFO log so logcat can prove whether the callback fired.
- PageButton return type is explicit `UnityEngine::UI::Button*`.

## Still experimental / not Quest-tested
- If a button still produces zero logcat output when pressed, the click is not reaching the BSML button (raycast / hierarchy issue) rather than the C++ callback being empty.
- Capture logcat after pressing the exact button and look for lines starting with "TwitchBSR:".
