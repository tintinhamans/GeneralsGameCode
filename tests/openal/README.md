# OpenAL PCM primitive tests

This modern-only slice provides a device/context owner and a bounded mono/stereo S16 PCM stream. It does not transplant the donor AudioManager, change Miles or movie playback, add SDL/macOS support, or select a default game backend.

## Contract

- Open, control, queue, query and destroy objects on the device's opening thread. Other-thread operations are rejected; concurrent control calls or cross-thread destruction are misuse. Wrong-thread destruction terminates rather than freeing data still reachable by the mixer.
- Each OpenAL operation selects and restores the correct thread-local context. Streams retain their context/device lifetime, so closing the device wrapper before a stream is safe.
- Capacity is in frames, with one extra frame reserved internally to distinguish full from empty. `Accepted` copies the entire block; `Full` copies nothing. Blocks exceeding capacity or appended after `markEnd()` are invalid. The caller retains any unaccepted PCM; there is no hidden dropping or holding queue.
- The OpenAL Soft callback only copies from a fixed SPSC ring and zero-fills temporary starvation. It has no allocation, lock, decoder, logging or OpenAL calls; its 32-bit indices/flags are compile-time checked lock-free. Producer/control operations stay on one thread while the real mixer consumes concurrently.
- `play()` resumes or starts; `pause()` preserves data; `stop()` synchronizes and discards pending PCM/reset EOF. Stop/reset and destruction quiesce the callback before resetting/freeing its state. A failed source stop remains fatal because callback quiescence is unproven. Pre-existing AL errors and later detach/delete errors are reported to stderr without terminating after a successful stop; failed deletion can retain driver objects until context teardown.
- `markEnd()` publishes finite EOF; the callback returns short/zero only after queued PCM drains. Temporary underrun instead emits silence and automatically consumes later PCM without replay. This advances the output clock during starvation; no A/V synchronization or sample-accurate presentation clock is claimed.
- OpenAL Soft may prefetch during the pause transition and retains that data for resume. At EOF it can hold the last sample until the current mixer block ends, then settle to silence. `Finished` means source EOF, not that all device/filter latency has vanished. Tests distinguish that bounded renderer tail from payload replay.

`getLastError()` exposes retained driver/callback errors after opening; failed stream initialization can only report generic invalid-operation because no stream state is retained.

## Run

Build the pinned dependency using `scripts/openal/README.md`, and the FFmpeg profile using `scripts/ffmpeg/README.md`, then run from an x86 VS 2022 environment with Python 3 available:

```powershell
cmake -S tests/openal -B build/openal-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DOpenAL_DIR=C:/path/to/openal/install/lib/cmake/OpenAL -DFFMPEG_ROOT=C:/path/to/ffmpeg/install
cmake --build build/openal-tests
ctest --test-dir build/openal-tests --output-on-failure
```

CTest forces the Null backend and a deterministic loopback configuration, so no physical speakers are used. Tests have bounded 10–30-second timeouts. Loopback checks exact channel/sample order across repeated ring wraps, full/no-consumption backpressure, EOF/no replay/eventual silence, empty/starved EOF, pause/resume/stop, full sample continuity across four pauses against an uninterrupted reference, non-null context restoration, two-context isolation and retained lifetime. A 16 kHz mono tone rendered at 48 kHz also checks equal channels, 440 Hz content/crossings, bounded phase and residual, plausible mono panner gain, payload at both ends of the expected second, and eventual silence. Mono gain is renderer-dependent; the test does not assume a stereo duplication gain. The FFmpeg bridge retains each callback block until every chunk is accepted; rendered energy and accepted sample counts are checked. The Null test runs an actual concurrent mixer through repeated feed/pause/stop/close/recreate cycles; it is a bounded stress test, not a formal race proof.

The continuity signal has silence guards around pause transitions to isolate renderer gain ramps; every payload sample and its position must still match, and one-frame loss/repeat negative controls are rejected. Separate cleanup tests compile the actual stream with test-only AL call interposition forwarding to the real library. Pending, detach, source-delete and buffer-delete errors must log and return safely; an injected stop failure must terminate with the exact expected exit code in a timeout-bounded child. No production fault-injection hooks are added.

The production OpenAL classes compile directly without adapters. Only the FFmpeg bridge uses the existing narrow engine `File` adapter. A full modern Zero Hour Release x86 build with the OpenAL option enabled also compiled both new sources and linked successfully (`build/go-openal-build.log`). Unused primitives may be removed by the linker because no playback backend is wired. No game was launched: physical-device playback, game scheduling and engine File runtime integration remain unverified.

## Pinned lifetime evidence

OpenAL Soft 1.25.2 `al/source.cpp` queues `VChangeState::Stop` in `alSourceStopv_`, then `SendVoiceChanges` calls `device->waitForMix()`. `alc/alu.cpp` clears the stopped voice's current/loop buffers before subsequent mixing. `core/voice.cpp::LoadBufferCallback` explains the last-sample EOF tail, and its stopping branch preserves prefetched data/positions during pause. This implementation uses no deferred-update mode.
