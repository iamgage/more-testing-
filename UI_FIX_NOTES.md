# Twitch BSR Quest UI repair attempt

- Replaced complex tab-selector BSML markup with a basic single-page layout.
- Removed the undeclared `xsi` namespace and custom `<bg>` wrapper.
- Preserved the settings host, connection callbacks, chat toggle, request toggle, and queue buttons.
- Retained the existing GitHub Actions build workflow.

**Important:** This is a source-level candidate fix, not a confirmed runtime fix. Build in GitHub Actions, install the resulting qmod, and inspect logcat if the menu still fails. Check for `late_load reached, registering BSR menu`, BSML parser errors, and missing runtime dependencies.
