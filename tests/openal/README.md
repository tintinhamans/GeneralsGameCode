# OpenAL PCM primitive tests

This modern-only slice provides a device/context owner and a bounded mono/stereo S16 PCM stream. It does not transplant the donor AudioManager, change Miles or movie playback, add SDL/macOS support, or select a default game backend.

## Contract

- Open, control, queue, query and destroy objects on the device's opening thread. Other-thread operations are rejected; concurrent control calls or cross-thread destruction are misuse. Wrong-thread destruction terminates rather than freeing data still reachable by the mixer.
- Each OpenAL operation selects and restores the correct thread-local context. Streams retain their context/device lifetime, so closing the device wrapper before a stream is safe.
- Capacity is in frames, with one extra frame reserved internally to distinguish full from empty. `Accepted` copies the entire block; `Full` copies nothing. Blocks exceeding capacity or appended after `markEnd()` are invalid. The caller retains any unaccepted PCM; there is no hidden dropping or holding queue.
- The OpenAL Soft callback copies from a fixed SPSC ring or a single immutable finite replacement tail and zero-fills temporary starvation. It has no allocation, lock, decoder, logging or OpenAL calls; its 32-bit indices/input-state and Boolean flags are compile-time checked lock-free. Producer/control operations stay on one thread while the real mixer consumes concurrently.
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

CTest forces the Null backend and a deterministic loopback configuration, so no physical speakers are used. Tests have bounded 10â€“30-second timeouts. Loopback checks exact channel/sample order across repeated ring wraps, full/no-consumption backpressure, EOF/no replay/eventual silence, empty/starved EOF, pause/resume/stop, full sample continuity across four pauses against an uninterrupted reference, non-null context restoration, two-context isolation and retained lifetime. A 16 kHz mono tone rendered at 48 kHz also checks equal channels, 440 Hz content/crossings, bounded phase and residual, plausible mono panner gain, payload at both ends of the expected second, and eventual silence. Mono gain is renderer-dependent; the test does not assume a stereo duplication gain. The FFmpeg bridge retains each callback block until every chunk is accepted; rendered energy and accepted sample counts are checked. The Null test runs an actual concurrent mixer through repeated feed/pause/stop/close/recreate cycles; it is a bounded stress test, not a formal race proof.

The continuity signal has silence guards around pause transitions to isolate renderer gain ramps; every payload sample and its position must still match, and one-frame loss/repeat negative controls are rejected. Separate cleanup tests compile the actual stream with test-only AL call interposition forwarding to the real library. Pending, detach, source-delete and buffer-delete errors must log and return safely; an injected stop failure must terminate with the exact expected exit code in a timeout-bounded child. No production fault-injection hooks are added.

Gain/pitch content, callback-read counters/reset, and immutable replacement ordering are also tested. Replacement cases cover full queues, empty/null tails, cutoffs already passed by mixer prefetch, paused and previously marked-end sources, and rejection of a second command or later queueing. A real Null mixer exercises 100 EOF/publication races: every accepted command must consume its entire tail, while a latched-EOF rejection must preserve the original finite input. Pause may still prefetch during its fade transition and is not used to justify producer-side ring rewinds.

The production OpenAL classes compile directly without adapters. Only the FFmpeg bridge uses the existing narrow engine `File` adapter. A full modern Zero Hour Release x86 build with the OpenAL option enabled also compiled both new sources and linked successfully (`build/go-openal-build.log`). Unused primitives may be removed by the linker because no playback backend is wired. No game was launched: physical-device playback, game scheduling and engine File runtime integration remain unverified.

## Pinned lifetime evidence

OpenAL Soft 1.25.2 `al/source.cpp` queues `VChangeState::Stop` in `alSourceStopv_`, then `SendVoiceChanges` calls `device->waitForMix()`. `alc/alu.cpp` clears the stopped voice's current/loop buffers before subsequent mixing. `core/voice.cpp::LoadBufferCallback` explains the last-sample EOF tail, and its stopping branch preserves prefetched data/positions during pause. This implementation uses no deferred-update mode.

## Explicit output selection

The device requests basic stereo panning by default, rather than allowing headphone
endpoint detection to select HRTF implicitly. Low-level headphone/UHJ output modes
remain available and quad/5.1/7.1 request their corresponding layouts. Game-facing
headphones/surround instead use the measured legacy stereo matrix, not HRTF/UHJ;
the manager maps that policy separately from the physical OpenAL output mode.
The manager reports the renderer actually selected if a backend ignores a request.
Live changes reset the device without replacing contexts or losing source queues.
Loopback rendering remains stereo-only and rejects multi-channel layout changes.

The output-mode regression checks queried renderer modes, stereo/HRTF/UHJ switching,
invalid and unsupported requests, active-source state, unchanged read counters at
reset and complete consumption after switching. A separate process starts with
`stereo-encoding = hrtf` to prove the explicit stereo baseline overrides auto/config
selection in the pinned OpenAL Soft implementation. No physical endpoint is opened.

## Positional stereo mixing

Positional mono input uses a fixed float ring and float stereo callback output.
S16 queueing promotes input on the producer thread; filtered float clips retain
headroom until final source gain and device mixing. Ordinary non-positional S16
streams are unchanged. There is one source, one callback and one read-frame clock.

The callback applies independently published linear left/right gains. Surround
mode blends a one-sample delayed `(left + right) / 2` into left and its inverse
into right using the front/back factor. Owner-published portion markers reset the
delay at exact source-frame boundaries; immutable replacement tails discard old
future markers and reset at their own start. Mode changes do not recreate queues.
The callback allocates nothing and makes no OpenAL or game-camera calls. The
native-rate low-pass filter runs before resampling in the decoder/cache path,
not here, so source-rate cutoff and output-rate delay are not conflated.

Regressions check linear channel coefficients, independent source gain, rear
anti-phase output, ring-wrap identity, exact portion reset, discarded future
markers after replacement, float tail consumption, above-unity input at quiet
source gain, NaN rejection and stop/reuse. A bounded real Null mixer also receives
concurrent float feeds and coherent stereo-control updates. These tests do not
claim bit-identical proprietary fixed-point mixer output or physical listening
parity; provider reference measurements define the policy and DSP tolerances.

## Direct cached playback

`InputMode::CachedSpans` opts sound-effect voices into a bounded ordered span queue. Immutable cached S16/float storage is pinned on the owner thread; the callback only consumes raw spans and acknowledges retired descriptors. Ordinary queued ring input remains supported in the same timeline, so cached-to-decoder fallback and later cached portions need no source recreation. The default input mode remains unchanged for movies and streaming audio.

The primitive regressions compare output exactly with the old scalar mixer for S16 normalization, float headroom, live Fast2D/Dolby changes, portion boundaries, mixed cached/ring ordering, ring wraps and replacement tails. Lifetime tests check pause/resume, stop/reopen, bounded descriptor exhaustion and owner-thread release after callback consumption. A loopback test consumes 2.25 seconds of one cached clip without pumping; the Null stress test additionally sleeps the owner thread for 2.25 seconds while the backend continues asynchronously.

This removes the copied-ring refill deadline within already-published cached storage. It does not let the callback choose random loop portions, run event logic, decode missing assets or avoid silence at an unprepared next-portion boundary. Those decisions remain owner-thread work under the manager's existing prefetch/RNG policy.

The asynchronous Null stress also drives a small 8-frame hybrid ring through four stop/reuse rounds and more than 64 descriptor-pool wraps. Each round mixes immutable S16/float cached spans with copied ring input, observes deterministic ring and descriptor backpressure before playback, and checks the exact total consumed payload frames. Custom deleters and weak references verify that every cached pin is released on the owner thread after explicit collection or synchronized stop. The added stress has one shared 8-second deadline; it never opens a physical audio device.
