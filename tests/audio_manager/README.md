# Isolated engine-linked AudioManager tests

Enable `RTS_BUILD_AUDIO_MANAGER_TESTS=ON` on a modern Zero Hour configuration with `RTS_BUILD_AUDIO_BACKEND=OPENAL`, FFmpeg and OpenAL enabled. The target is `audio_manager_tests`:

```powershell
cmake --build build/go-modern --target audio_manager_tests
ctest --test-dir build/go-modern -R audio_manager_ --output-on-failure
```

The normal game configuration must already resolve its networking and graphics build dependencies. `OpenAL_DIR` and `FFMPEG_ROOT` select the local libraries described in `scripts/openal/README.md` and `scripts/ffmpeg/README.md`. Python 3 generates only synthetic PCM WAVs, INI definitions and a minimal BIG archive under the test build directory. A generated one-second MP3 is copied from the existing FFmpeg fixture. CTest recreates these scratch fixtures before every run, including after a failed reload test. No commercial assets are included or modified. The executable requires the generated fixture marker before any reload test can overwrite a scratch media file.

This executable links the actual game libraries and uses the production `AudioManager`, `SoundManager`, `MusicManager`, INI parser, memory pools, local/BIG file implementations, decoder/cache and OpenAL manager. It has its own console entry point. It never calls `GameEngine::init`, the normal `WinMain`, OnlineServices, networking or anticheat startup. The few platform-host symbols required by the engine are defined locally; the `WinMain` symbol is an aborting trap, not a second startup implementation. OpenAL is restricted to loopback or the Null backend, with no physical audio output.

The fixture subclass only opens a loopback device, exposes the manager's real playback tick, and treats events as locally audible. It does not replace the request, priority, playback or completion code. A tick is one actual logic frame; rendered block length is `48000 / LOGICFRAMES_PER_SECOND` (800 frames for the current 60 Hz GeneralsOnline build), not a hardcoded retail 30 Hz interval. Actual `AudioSettings` are initialized explicitly and the settings INI is empty, so the test does not read or write user audio preferences. Empty archive construction plus an explicit synthetic BIG load avoids discovering retail archives through the registry. Language localization may perform the existing read-only language lookup.

Covered behaviour:

- Actual INI events and base request creation; attack/main/decay order, finite/permanent loops, and the existing shared `AC_ALL` first-sound-only behaviour. The latter is an existing `AudioEventRTS` limitation also used by Miles, not fixed in this backend slice.
- Limits, priority preemption, immediate stop visibility, 300 finished-source reuses without resets, pre-add uninterruptible speech-gate retirement, category pause/resume and volume, uninterruptible speech, streaming backpressure, music loop completion, navigation/fade, and pending music/speech cancellation.
- Loop stop preserves the current main portion followed by decay, without future queued loop rounds. Ordered tone windows detect wrong portions; a monotonic bounded renderer tail is distinguished from payload replay.
- Failed missing/corrupt replacements preserve full-pool SFX, same-name interrupt victims, music loop counters, speech and forced-speech gates. PCM comparisons detect unintended fades or interruptions. One-stream next/previous navigation replaces equal-priority music; failed next/previous preserves the old track.
- Music and speech share stream capacity and preempt by priority in both directions; generic preemption protects uninterruptible speech. Category pause/resume remains independent.
- Forced loading speech continues through a full one-second payload without manager ticks, exceeding the ordinary stream-ring duration. Ten completion/replacement cycles run without manager pumps between clips.
- Z-up spatial panning with two listener orientations, global range fade and category gain ratios.
- All 48,000 streamed speech frames compared against expected PCM (maximum observed sample error 1); normal/headless source-sample duration and script-frame equality, public dummy update without a camera/device, a real BIG entry, active cache pins, bounded eviction, roomy/tight/pinned cache capacity and decoded PCM, logic-frame delay and actual-file PCM/metadata reset/reload.

Limits: this is not a full game launch, physical-device, hosted network, retail CRC or renderer/camera test. Player/shroud/ownership filtering is intentionally outside this fixture, as are object-backed voice positions and full gameplay scheduling. The public normal `update()` camera path is not executed; its production playback pipeline is. Standalone OpenAL tests separately cover callback lifetime faults and a concurrent real Null mixer. A single umbrella behaviour test fails on the first assertion; the fixture setup is a separate CTest step.

Cache growth evidence records vector capacity, not allocator-call counts or wall-clock performance: the one-second stereo fixture has 96,000 samples; with 1 MiB available its capacity is 131,072 samples (262,144 bytes), while a 192,000-byte budget fits exactly and 191,999 bytes rejects it. A pinned small clip plus the large clip stays within 200,000 bytes. These checks exercise the geometric-growth and tight-headroom paths without production instrumentation.

Pause regressions check that existing voices remain paused while new UI sounds
play, queued play requests are cleared, UI sounds do not replay on resume, and
resumed PCM is identical with or without intervening UI playback. Separate checks
cover category-scoped resume and persistent ambient pause in both overlap orders.

Behavior regression selectors accept a second argument after the fixture path:
`paused-gain`, `forced-overrides`, `disabled-cleanup`, or `moving-range`.
The normal CTest suite runs all four. They cover paused gain updates and direct
resume (including focus mute and frozen fades), initial/live forced-briefing event
overrides through the speech slider, and disabled-event retirement after override
history is cleared, including protected-speech exclusion release.

The range regression moves the listener while retaining the same critical/global
looping voice, checks near/far gain with curve exponents 1 and 4, then checks
silence beyond range and recovery on return without restarting. The global fixture
uses different event-local ranges to detect accidental use of the wrong limits.
This covers the per-frame range-gain behavior addressed by PR #3511 without changing
our custom curve. It also checks the measured native provider response applied
after the game curve. Ordinary non-global admission and
object-backed movement still require the separate game/world fixture.

The measured spatial reference tests cover the bundled Fast 2D provider's
`volume^(5/3)` response, angular linear panning (including elevated and rotated
sources), inverse-distance attenuation and rear attenuation. They also check
Dolby's one-sample rear phase behavior for both two-channel game surround modes.
A playing positional voice retains its exact source-frame progression through
Fast 2D/Dolby switches; these modes do not silently enable OpenAL HRTF.

Simultaneous left/right 440 Hz world sources and centered 400 Hz UI audio verify
independent category gain changes. Standalone mono files are duplicated at unity
before the measured Miles center-pan factor, rather than inheriting FFmpeg's
movie-oriented equal-power upmix. Legacy quiet-voice retirement is tested with
unequal 2D/3D sliders, muted sliders, and global/critical exemptions.

Off-screen tests check the source-rate Butterworth response, including 22.05 kHz
input resampled to 48 kHz. Filtered/unfiltered cache variants retain independent
pins and account for float payload capacity in the same byte budget. A near-full-
scale filtered waveform under quiet gain detects premature S16 clipping. Loop
and decay checks verify immutable portions: already prepared PCM is unchanged;
visibility changes affect subsequently prepared portions. The fixture counts
preparation queries to bound this lookahead rather than waiting arbitrarily.

The additional selectors are `spatial-reference` and `offscreen-filter`.
Reference formulas and position ratios were checked against the actual bundled
Miles providers using audio-only process-isolated render capture. This does not
establish bit-exact Miles integer mixer arithmetic, physical headphone perception,
or support for unmeasured legacy hardware providers. Camera visibility is sampled
when a portion is prepared, not from the real-time audio callback.
