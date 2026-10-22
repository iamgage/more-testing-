# Local build (Windows/macOS/Linux with PowerShell 7). Needs: qpm, cmake, ninja, Android NDK r27c.
# Set ANDROID_NDK_HOME to your NDK folder first.
param([switch]$clean)
if (-not $env:ANDROID_NDK_HOME) { throw "Set ANDROID_NDK_HOME to your NDK path" }
if ($clean -and (Test-Path build)) { Remove-Item build -Recurse -Force }
& qpm restore
& cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  "-DCMAKE_TOOLCHAIN_FILE=$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
& cmake --build build
