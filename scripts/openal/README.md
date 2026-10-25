# OpenAL Soft dependency

Pinned source: OpenAL Soft **1.25.2**, official `kcat/openal-soft` tag archive. SHA256: `fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894`.

From an x86 Visual Studio 2022 developer environment with CMake, Ninja and tar available:

```powershell
powershell -File scripts/openal/build-soft.ps1 -WorkDirectory build/openal-soft
```

The script downloads source only, verifies the pinned digest, and builds a local Release shared library. It never installs tools globally or removes existing directories. A predownloaded archive can be placed in the fresh work directory for offline use. The validated toolchain is MSVC 19.44.35229 x86; the matching configuration built WASAPI, WaveFile and Null backends. Loopback support is part of OpenAL Soft itself. SDL, EAX, examples, utilities and installed HRTF/config/preset data are disabled. No macOS implementation is introduced.

Set `OpenAL_DIR` to `install/lib/cmake/OpenAL`. `RTS_BUILD_OPTION_OPENAL=ON` compiles the new device/PCM primitives but deliberately does not select a game audio backend or activate old movie OpenAL paths. This slice requires `ALC_EXT_thread_local_context` and `AL_SOFT_callback_buffer`; unsupported implementations fail initialization rather than using a second queue implementation.

Keep OpenAL Soft's LGPL notices and applicable source/build information with distribution. No DLLs or commercial media are checked in.

## Opt-in Zero Hour manager

`RTS_BUILD_AUDIO_BACKEND` defaults to `MILES`. Select `OPENAL` explicitly with both
`RTS_BUILD_OPTION_OPENAL=ON` and `RTS_BUILD_OPTION_FFMPEG=ON`, plus `OpenAL_DIR` and
`FFMPEG_ROOT` pointing at the pinned local installations. The alternative is for
modern Zero Hour only; it does not replace the Generals factory or enable VC6.
The public engine header does not expose OpenAL headers: backend construction
lives in the Win32 implementation file.

The manager derives from the real `AudioManager`, retaining INI event generation,
`SoundManager` admission and `MusicManager` request creation. Device controls,
request processing, source retirement, cache ownership and decoding remain on the
owner thread. The real-time callback only reads bounded PCM and immutable control
state. It never calls game classes, decoders, logging, locks or allocation.

Supported paths include UI/2D and mono positional effects, pitch and category gain,
Z-up listener orientation, game-range attenuation without duplicate OpenAL rolloff,
limits/priority preemption and object voice queries, delayed requests, attack/main/
decay and finite/permanent loops, speech interruption gating, category pause,
music navigation/fades and track round queries. Finished sources are retired
before new admission; stop/error states are not counted as playing. Existing
`AudioEventRTS` AC_ALL behavior is retained, including its pre-existing first-sample
limitation; this is not an unrelated AC_ALL fix.

Resource pools are separate from category controls: music and speech share stream
slots and priority preemption, while their volume and pause settings remain
independent. Uninterruptible speech is excluded from generic pool preemption. Replacement requests prepare their asset, initial PCM, source and
properties before stopping old voices or changing speech/music state. Failed
preparation retains existing playback, including failed next/previous music
requests. At the stream limit, valid music replaces an existing music voice only
after preparation succeeds; with headroom, the previous track fades. Preparation
requires a spare native OpenAL source:
if the physical source limit prevents preparation, the request is rejected rather
than stopping an old voice for a destructive retry. Starting a prepared source and
stopping its victims are owner-thread operations, not an atomic mixer transaction.

A loop stop publishes a finite immutable decay tail to the callback. The callback
finishes the current callback-read portion, ignores queued future loop PCM and then
copies the tail; the producer never rewinds ring indices or treats pause as mixer
quiescence. Publication is ordered before reading the ring's write index. A single
lock-free Normal/Replacement/Closed state arbitrates publication against callback
EOF; a command that arrives after EOF is latched cannot replace already-prefetched
samples. Mixer-prefetched audio and renderer fade tails remain unavoidable latency,
not a promise of sample-accurate audible stop timing. The stop tail is capped at
16 MiB; oversized custom tails retain the ordinary queued shutdown with a diagnostic.

`AudioFootprintInBytes` bounds cached PCM vector capacity, not allocator/map overhead
or decoder scratch. Referenced clips cannot be evicted; new cache admission fails
when active pins occupy the budget, and the voice falls back to bounded streaming.
Decoding grows the PCM vector geometrically within free budget; when that headroom
is unavailable it reserves only the required growth, evicting unreferenced clips
as needed. Accounting uses actual capacity. Vector reallocation can temporarily
retain both the old and new allocation; this transient copy peak is not a hard
process-memory bound and is separate from decoder scratch.
Ordinary voices use 4,096-frame effect rings or 16,384-frame speech/music rings, plus
at most 65,536 pending converted frames. Cached tiny loops have a separate bounded
copy budget rather than sharing the 32-packet per-tick decoding budget. Timing uses
`MSEC_PER_LOGICFRAME_REAL` (60 Hz in the validated GO configuration), not a hardcoded
retail tick interval.

Blocking loading-screen speech is fully predecoded into a one-shot ring, independent
of the effect cache's configured size. Its explicit 16 MiB PCM cap covers every WAV
in the inspected installation (the longest observed speech was about 39.25 seconds,
about 7.54 MB at 48 kHz stereo S16). Previous forced speech is replaced, and oversized
or unreadable custom input is diagnosed rather than truncated. Ordinary streaming
music still requires regular manager updates and may starve during blocking loads.
No FFmpeg work occurs on the mixer thread.

Source-sample duration queries use one integer-millisecond policy in OpenAL normal/
headless managers and modern FFmpeg-enabled Miles builds. MP3 delay/padding trimming
is included; sample-rate-changing input is rejected rather than summed ambiguously.
The bounded metadata cache is cleared on reset. Headless initialization and update
need no device or tactical view; the modern metadata-only Miles dummy does not shut
down Miles unless that manager actually called startup. VC6 and FFmpeg-disabled
Miles retain the old path. Mixed feature configurations are not claimed compatible.
`hasMusicTrackCompleted` is used by scripts and still observes asynchronous callback
prefetch, not an audible clock: no multiplayer CRC determinism is established here.

This remains opt-in and is not closed-library removal or full Miles feature parity.
Movie audio is a separate slice: no legacy OpenAL movie macros are enabled and no
OpenAL pointer is passed through Bink's opaque DirectSound handle. The backend uses
the OS speaker layout, exposes a software provider, and does not implement legacy
MSS hardware selection, forced speaker layouts or Miles delay/low-pass DSP effects.
The game build stages the selected OpenAL shared runtime and the five known FFmpeg
DLL families into its build output only; no install or deployment is invoked.

See `tests/audio_manager/README.md` for actual engine-linked offline coverage and
its limits. The harness links the production manager, File/INI/archive and memory
classes without normal GameEngine startup, network login, anti-cheat or production
traffic. Physical-device and in-game playback are not established by loopback tests.
