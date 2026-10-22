# Functional + UI feedback fixes (source-only, experimental)

## Previous fixes retained
- Prevent deadlocks: `!oops`, Clear Queue, and Remove now invoke callbacks after releasing the queue mutex.
- Download status is tracked per request and shown in the queue.
- Automatic downloads use the exact resolved request rather than a potentially different `queue.back()`.
- Reposition Chat cycles Left / Right / Centre and updates the button label.
- Test Chat respects the Show Chat toggle.
- Play Song still relies on the existing experimental SoloButton navigation and is not confirmed working.

## New UI / feedback fixes (addressing “buttons have no functions or UIs”)
- Page switch (`show()`) now always calls `RefreshPageText()` so status / channel / queue stay current.
- Pages start with only Home active; others are explicitly deactivated.
- Home page has a short subtitle explaining the mod.
- Connection page has instructional text (“Use Edit to type channel, then Connect”).
- Every action button now writes a clear `g_statusOverride` message and refreshes the UI immediately:
  - Connect / Disconnect / invalid name
  - Chat Overlay ON/OFF, Test Chat, Position change
  - BSR enable/disable
  - Download (shows count or “Queue is empty”)
  - Play Last Download
  - Clear Queue
- This makes every button give visible feedback even when the queue is empty or no channel is set.

## Still experimental
- The zip has not been compiled or tested on Quest.
- Play Song navigation remains unconfirmed.
- If buttons still appear dead, capture logcat after pressing a specific button and check for BSML / CreateUIButton errors.
