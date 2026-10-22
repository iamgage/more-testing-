# Functional fixes (source-only, experimental)
- Prevent deadlocks: `!oops`, Clear Queue, and Remove now invoke callbacks after releasing the queue mutex.
- Download status is tracked per request and shown in the queue.
- Automatic downloads use the exact resolved request rather than a potentially different `queue.back()`.
- Reposition Chat cycles Left / Right / Centre and updates the button label.
- Test Chat respects the Show Chat toggle.
- Play Song still relies on the existing experimental SoloButton navigation and is not confirmed working.
- The zip has not been compiled or tested on Quest.
