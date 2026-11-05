# Isolated FFmpeg movie audio tests

Enable `RTS_BUILD_VIDEO_AUDIO_TESTS=ON` in a modern Zero Hour configuration with FFmpeg and the OpenAL backend selected. Build `video_audio_tests` and run:

```powershell
cmake --build build/go-modern --target video_audio_tests
ctest --test-dir build/go-modern -R video_audio_ --output-on-failure
```

`video_audio_synthetic` always runs. It verifies that the default legacy completion query requires both the last frame index and frame readiness. It creates planar float and packed signed-16 audio frames in memory and checks stereo polarity, 44.1-to-48 kHz conversion, exact duration, drain/reset and malformed-input handling. Actual movie voices exercise independent ownership, source completion without replay, and device shutdown.

For the engine-linked movie test, configure both local file paths:

```powershell
cmake -S . -B build/go-modern -DRTS_BUILD_VIDEO_AUDIO_TESTS=ON `
  -DRTS_VIDEO_AUDIO_TEST_AUDIO_FILE="<installed game>/Data/Movies/GC_Background.bik" `
  -DRTS_VIDEO_AUDIO_TEST_SILENT_FILE="<installed game>/Data/English/Movies/Comp_AirGen_inv_000.bik"
```

No commercial movies are copied into the repository or generated fixture directory. These optional inputs are read-only. The test expects a short audible movie and a short silent movie; the shown installed Zero Hour files have 180 and 13 video frames. The minimal FFmpeg build cannot encode Bink, so there is no fabricated substitute for this runtime coverage.

The executable links the actual engine libraries and reuses the audio-manager console host symbols. It initializes memory pools and the actual local `FileSystem`, but never initializes `GameEngine`, the normal `WinMain`, OnlineServices, networking or anticheat. It opens movies through the production player's protected file-based constructor path, avoiding registry movie lookup and user settings. No renderer, camera, game install mutation or physical audio device is used. OpenAL renders into a loopback buffer.

Runtime checks compare every RGB24 frame against a separate sequential `FFmpegFile` decode and compare emitted stereo PCM against `FFmpegAudioFile`'s first-track decode. They cover bounded video decode-ahead, callback-prefetch not advancing presentation, restart and midpoint seek, last-image presentation before audio drain and separate playback completion, no EOF replay, silent movies, the Bink movie volume curve (unity and percent 61) and true zero volume, immediate AudioManager mute/unmute without an audio update, continued audio/frame progression while muted, per-window pause/hide/stop with frozen audio and no resume catch-up for audible and silent movies, no-op player-focus hooks, and two simultaneous movies where closing one must leave the other unchanged. Gain controls select a verified continuously audible reference window so silence in a real movie cannot give a false pass. A `Win32LocalFile` subclass only injects a read failure; the real demux/decode/error path remains intact.

A reduced video queue budget admits exactly one real decoded frame, with that frame occupying more than half the budget. The completion loop must still release it and reach demux EOF without a hang. An actual movie voice is closed after audio-capped progress; the next frame must stay unready until the rebased silent clock reaches its timestamp, rather than jumping to old wall time.

A movie is paced by the real monotonic clock, not accelerated by treating callback-prefetched samples as already presented. Tests have bounded watchdogs. This is not a full game launch, visual display, physical-device latency or perceptual A/V-sync test; those remain separate checks. It is also not a run of every installed movie. No claim of sample-accurate hardware presentation timing is made.

Recovery scenarios use the real movie decoder and a Null playback device with
native disconnect/reopen fault injection. Only movie-player updates are serviced,
so manual loading loops cannot depend on an AudioManager tick to recover. Cases
cover a playing movie, panel pause before/during loss, seek/re-prime, final-frame
completion, and a new movie on an already-recovered device. Progress freezes while
audio is pending and unavailable, then the clock rebases from its last logical
position; completed audio does not freeze a remaining silent video tail.

The no-catch-up check has a strict frame-count upper bound and separate bounded
liveness check. It starts after steady playback: the existing audio-first demux
backpressure can delay the second image at startup, independently of device loss.
These tests do not establish acoustic/sample-perfect recovery or hardware A/V
synchronization.
