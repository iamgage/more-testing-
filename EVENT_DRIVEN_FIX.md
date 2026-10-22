# Event-driven chat pump fix

Removed the custom `ChatPumpBehaviour` and its `DECLARE_CLASS_CODE`/`DEFINE_TYPE` registration, which failed compilation. TwitchIRC now schedules `Pump()` on Unity's main thread when a chat message arrives, via BSML MainThreadScheduler. This avoids both `A64HookFunction` and custom MonoBehaviour compilation issues.

Note: No per-frame polling occurs. Status text refreshes on UI actions and incoming messages; it may not update immediately on socket connection without a chat message. This has not been built or tested on Quest.
