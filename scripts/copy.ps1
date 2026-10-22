# Pushes the built .so to a connected Quest for quick testing (adb required).
& adb push build/libtwitch-bsr.so /sdcard/ModData/com.beatgames.beatsaber/Mods/libtwitch-bsr.so
& adb shell am force-stop com.beatgames.beatsaber
& adb shell am start com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity
