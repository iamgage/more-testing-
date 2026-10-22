# Quest loading diagnosis

The Quest log reports an unresolved `A64HookFunction` when Scotland2 loads `libtwitch-bsr.so`. That prevents `late_load()` and therefore all UI registration.

This patch removes `-Wl,-z,undefs` and enforces `--no-undefined`. It is a **diagnostic/build-safety fix**, not a verified implementation of the missing hook dependency. GitHub may now fail at link time, which is preferable to shipping a qmod that cannot load.

If the linker reports `A64HookFunction`, the next step is to locate the actual arm64 implementation in the restored beatsaber-hook dependencies and add it as a proper link target/source. Share the linker output and `extern.cmake` from the GitHub build to identify that target. Do not restore `-z undefs`.
