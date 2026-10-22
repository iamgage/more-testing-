# Twitch BSR menu registration fix

Compared with Darth Maul standalone Quest mod.

- Registers both Settings > Mods and left-side Mods menu.
- Uses separate UnityW SettingsHost references for each page to avoid stale pointers.
- Queue changes refresh both visible/available pages.
- Retains existing BSML markup, Twitch connection and queue logic.

Not compiled or tested on a headset. If neither menu appears, inspect Quest logcat for late_load and BSML registration errors; verify dependencies and native library loading.
