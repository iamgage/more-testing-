Keyboard/layout revision:
- Removed multi-tap group logic.
- Added one-character-per-key QWERTY rows, digits, underscore and hyphen.
- Placed keyboard keys in horizontal layout groups to avoid full-width button stacking.
- Preserved Test Chat Overlay and Show / Reposition Chat actions.

IMPORTANT: This source has not been built against the Quest BSML headers or tested on hardware. If CreateHorizontalLayoutGroup is unavailable in the installed BSML version, GitHub Actions will show the precise compiler error. The chat overlay is still experimental and may need further runtime diagnostics.
