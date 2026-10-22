# Experimental Play Song integration

After **Download all**, wait for SongCore to finish refreshing and press **Play Song (last download)**. The code looks up the last extracted map by BeatSaver hash, sets up SoloFreePlayFlowCoordinator using the reference BetterSongSearchQuest pattern, and presses the Solo button.

This is an uncompiled integration; the song is not selected until SongCore resolves the hash. If navigation does not work, capture the Quest logcat and GitHub build log. The missing chat overlay is not addressed in this revision.
