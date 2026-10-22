# UI layout and freeze mitigation (experimental)
- Replace unconfigured GameObjects used as layout containers with BSML vertical layout groups.
- Do not create/refresh the Unity overlay during settings page activation.
- Schedule reposition refresh after the button callback.
- This does not resolve all possible freezing causes or verify overlay rendering or Play Song.
- Not compiled or Quest-tested. If freezes persist, provide logcat captured after pressing a specific button.
