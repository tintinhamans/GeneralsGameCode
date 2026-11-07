# OpenAL Soft dependency

Pinned source: OpenAL Soft **1.25.2**, official `kcat/openal-soft` tag archive. SHA256: `fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894`.

From an x86 Visual Studio 2022 developer environment with CMake, Ninja and tar available:

```powershell
powershell -File scripts/openal/build-soft.ps1 -WorkDirectory build/openal-soft
```

The script downloads source only, verifies the pinned digest, and builds a local Release shared library. It never installs tools globally or removes existing directories. A predownloaded archive can be placed in the fresh work directory for offline use. The validated toolchain is MSVC 19.44.35229 x86; the matching configuration built WASAPI, WaveFile and Null backends. SDL, EAX, examples, utilities and installed HRTF/config/preset data are disabled. No macOS implementation is introduced.

The build applies the checked-in `LoadSamples<f32>` specialization before configuring.
It preserves float bits while deinterleaving through direct pointers; it changes no
panning, filtering or resampling arithmetic. The helper accepts only the pinned
1.25.2 source digest (or the exact already-patched digest), verifies the complete
result before writing, and records the patch identity in `provenance.json`. No Git
executable or diagnostic compiler flags are required. The build still refuses
existing source/build/install directories; use a fresh work directory to rebuild.

Retain the fragment and build information with the corresponding LGPL source
distribution.

Set `OpenAL_DIR` to `install/lib/cmake/OpenAL`. This slice requires `AL_SOFT_callback_buffer`; unsupported implementations fail initialization rather than using a second queue implementation.

Keep OpenAL Soft's LGPL notices and applicable source/build information with distribution. No DLLs or commercial media are checked in.

`-WorkDirectory` defaults to `build/openal-soft` in the repository. `-Static` builds a static library instead (`LIBTYPE=STATIC`, DLL C runtime through `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` and `FORCE_STATIC_VCRT=OFF`) with the same backends, options and float loader, and defaults to `build/openal-soft-static`. The game then ships no `OpenAL32.dll`; the build copies the OpenAL Soft LGPL and PFFFT notices next to the executable.

## Static audio build

The `win32-vcpkg-generalsmd-openal` preset builds Zero Hour with the OpenAL audio backend and FFmpeg movies, both linked statically. It selects Zero Hour only and leaves both media options on (see below). Modern media turns on the `openal-audio` manifest feature, and vcpkg builds its two overlay ports from `cmake/ports`:

- `ffmpeg`: FFmpeg 9.0.2 with the same trimmed configure as `scripts/ffmpeg/build-minimal.sh --static`, release static libraries against the DLL C runtime. It replaces the stock vcpkg `ffmpeg` port.
- `openal-soft`: OpenAL Soft 1.25.2 with the float loader as `0001-float-loader.patch` and the same options as `build-soft.ps1 -Static`. The port checks the patched `core/voice.cpp` digest.

The OpenAL Soft port checks the SHA256 pinned by its script; the FFmpeg port fetches the n9.0.2 tag from the GitHub mirror with a pinned SHA512. vcpkg fetches MSYS2 and NASM for the FFmpeg build itself, so the only prerequisite is Visual Studio 18 (or 2022) with the v143 x86 C++ tools, its bundled CMake, Ninja and vcpkg. Run from the repository root in an x86 tools environment, which also sets `VCPKG_ROOT`; `CC`/`CXX` keep CMake on `cl` when another compiler is on `PATH`:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat" -vcvars_ver=14.44
set CC=cl
set CXX=cl
cmake --preset win32-vcpkg-generalsmd-openal
cmake --build --preset win32-vcpkg-generalsmd-openal
```

The executable lands in `build/win32-vcpkg-generalsmd-openal/GeneralsMD/Release` with `audio-libraries-NOTICE.txt`, `ffmpeg-LICENSE.txt`, `openal-soft-LICENSE.txt` and `openal-soft-pffft-LICENSE.txt` beside it, and imports no FFmpeg or OpenAL DLL.

The scripts remain an optional manual route: build `scripts/ffmpeg/build-minimal.sh --static` and `build-soft.ps1 -Static` as described above, then add `-DFFMPEG_ROOT=<repo>/build/ffmpeg-static/install` and `-DOpenAL_DIR=<repo>/build/openal-soft-static/install/lib/cmake/OpenAL` to the configure. Those take precedence over the vcpkg packages, which vcpkg still installs.

## Media build options

Two options pick what a build contains; both default to `ON`:

| `RTS_BUILD_OPTION_MODERN_MEDIA` | `RTS_BUILD_OPTION_LEGACY_MEDIA` | Build |
|---|---|---|
| `ON` | `ON` | Both; `LegacyMedia = yes` in `Options.ini` picks Miles with Bink movies at startup, `no` or no entry picks OpenAL with FFmpeg movies |
| `ON` | `OFF` | OpenAL with FFmpeg movies only |
| `OFF` | `ON` | Miles with Bink movies only, as retail |
| `OFF` | `OFF` | Configuration error |

`LegacyMedia` is read once at startup, so a change needs a restart. Miles with FFmpeg
movies and Bink with OpenAL are not offered: FFmpeg movie audio plays only through
OpenAL, and Bink movie audio only through Miles.

Modern media needs Zero Hour and Visual Studio 2022 or newer, with `OpenAL_DIR` and
`FFMPEG_ROOT` pointing at the pinned local installations. VC6, MinGW and
Generals-only configurations turn it off and build legacy media only; Generals
always plays through Miles and Bink. The option is read before the CMake
project/toolchain and selects the optional vcpkg `ffmpeg` feature. The public engine
header does not expose OpenAL headers: backend construction lives in the Win32
implementation file.

With legacy media off, `RTS_BUILD_GENERALS=OFF`, `RTS_BUILD_ZEROHOUR_TOOLS=OFF` and
`RTS_BUILD_CORE_EXTRAS=OFF`, the build does not fetch, compile or link the Miles/Bink
SDK stubs or their engine backends. Generals, WorldBuilder/W3DView and core extras
still need them, so enabling any of those keeps them in. The isolated audio/movie
tests need neither SDK.

The manager derives from the real `AudioManager`, retaining INI event generation,
`SoundManager` admission and `MusicManager` request creation. Device controls,
request processing, source retirement, cache ownership and decoding remain on the
owner thread. The real-time callback only reads bounded PCM and immutable control
state. It never calls game classes, decoders, logging, locks or allocation.

Supported paths include UI/2D and mono positional effects, pitch and category gain,
game-range attenuation without duplicate OpenAL rolloff,
limits/priority preemption and object voice queries, delayed requests, attack/main/
decay and finite/permanent loops, speech interruption gating, category pause,
music navigation/fades and track round queries. Finished sources are retired
before new admission; stop/error states are not counted as playing. Existing
`AudioEventRTS` AC_ALL behavior is retained, including its pre-existing first-sample
limitation; this is not an unrelated AC_ALL fix.

Category pause affects existing voices only, matching Miles; new menu sounds can
play while gameplay voices remain paused. Ambient pause remains persistent, and
its state is combined with each voice's category pause before resuming playback.
Paused voices still receive volume changes, and their current gain is applied
before resuming. Fade progression remains frozen during pause. Forced briefing
playback honors per-event overrides through the speech slider; disabled cleanup
uses the active event volume rather than override-table history.

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

Cached sound effects are stored as 16-bit PCM at the file's own sample rate and
channel count, like Miles keeps the files as stored. An effect voice plays at its
first portion's rate and OpenAL Soft resamples it to the device; a later portion with
another rate gets a cache variant converted to the voice rate (no retail event has a
later portion above its first portion's rate). 2D voices keep stereo files stereo
and play mono files on both channels at unity, as the old duplicated stereo did;
3D voices cache mono and keep the panning and gain mix described below. Speech,
music and blocking loading-screen speech still decode at the output rate.
`AudioFootprintInBytes` was sized for Miles, which caches the sound files as stored.
The retail sound effects decoded at their own rate are 1.41 times the file bytes
(22 kHz PCM 1x, IMA ADPCM 4x), so the cache budget is twice the INI value, capped at
128 MiB unless the INI value is already larger (retail 4 MiB gives 8 MiB). The budget
bounds cached PCM vector capacity, not allocator/map overhead or decoder scratch.
Referenced clips cannot be evicted; new cache admission fails when active pins occupy the budget, and the voice falls back to bounded streaming.
Decoding grows the PCM vector geometrically within free budget; when that headroom
is unavailable it reserves only the required growth, evicting unreferenced clips
as needed. A finished clip is trimmed to its sample count, so the growth slack
is not kept. Accounting uses actual capacity. Vector reallocation and the final
trim can temporarily retain both the old and new allocation; this transient copy
peak is not a hard process-memory bound and is separate from decoder scratch.
Effects use a 4,096-frame queued fallback with immutable cached spans; ordinary
speech/music retain 16,384-frame rings and at most 65,536 pending converted frames.
A resolved cached portion is referenced in full, so it can outlast an owner stall
without refill. Its clip remains pinned until consumption is collected on the owner.
A separate 4,096-frame logical producer horizon retains the previous portion/RNG
lookahead. Loop selection, delays and decoding stay on the owner: a stall can still
reach an unprepared randomized portion or starve a decoder-backed voice. This is
not automatic indefinite looping or a general streaming-underrun fix. Timing uses
`MSEC_PER_LOGICFRAME_REAL` (60 Hz in the validated GO configuration), not a hardcoded
retail tick interval.

Blocking loading-screen speech is fully predecoded into one immutable span, independent
of the effect cache's configured size. Its transport uses the fixed 4,096-frame mode
budget, not clip-length ring or descriptor allocation; completion is published with
the span and needs no later manager refill. Its explicit 16 MiB PCM cap covers every WAV
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
No legacy OpenAL movie macros are enabled and no OpenAL pointer is passed through
Bink's opaque DirectSound handle. Two-channel compatibility mixing follows the
bundled Miles Fast 2D and Dolby providers: nonlinear volume response, angular
linear panning, native inverse-distance attenuation in addition to the game curve,
and Dolby's one-sample phase matrix. The game speaker choices select Fast 2D
(`2 Speakers`) or Dolby (`Headphones` / `Surround Sound`), backed by explicit basic
stereo output with HRTF off. Other speaker types keep the current one.

Off-screen `LowPassCutoff` uses the measured source-rate Butterworth response
before resampling. Cached filtered variants are 16-bit like ordinary clips and
saturate overshoot at full scale, and so does the uncached decoder fallback.
Each prepared attack/main/decay or loop portion has an immutable filtered variant; changing camera visibility does
not rewrite PCM already queued. This is not bit-exact proprietary integer mixing
or support for the historical hardware/EAX providers and arbitrary Miles plugins.
The game build stages the selected OpenAL shared runtime and the five known FFmpeg
DLL families into its build output only; no install or deployment is invoked.

## FFmpeg movie audio

Selected modern Zero Hour OpenAL builds use one typed `OpenALMovieVoice` per
FFmpeg movie on the manager's existing device/context. These voices are separate
from gameplay stream admission; physical source exhaustion produces a diagnostic
and video-only playback. The manager holds weak registrations and closes movie
sources on reset/device close, so movies cannot keep a dangling manager pointer.
Miles-selected FFmpeg builds remain video-only; no Bink/Miles handle is requested.

`FFmpegPcmConverter` owns the single swresample conversion implementation shared by
movie AVFrames and `FFmpegAudioFile`. The movie uses its existing demuxer and only
the first audio/video tracks. Each source has a 16,384-frame stereo S16 ring,
65,536 pending audio frames, and up to eight future video frames. Current plus
queued video buffers are capped at 64 MiB. Demux pauses under backpressure, using
the largest observed frame allocation to avoid decoding into known insufficient
space. A single packet/frame exceeding these limits fails diagnostically rather
than silently dropping samples/frames or allocating without bounds. Decoder
internal allocations and transient cloning/resampling overhead are not included.

Readiness uses steady monotonic time, rational video timestamps and a cap from
callback-read audio progress. Prefetch cannot make video advance faster than real
time; starvation limits video progress until audio is fed again. This is not a
hardware presentation clock or a sample-accurate AV-sync guarantee. Initial audio
PTS offset is represented by silence or trimming; continuous game-movie audio is
the intended input, not arbitrary discontinuous/edit-list media. Silent movies and
explicit device-failure fallback use steady time without opening another device.
Failure rebases that clock at the last audio-capped playback position, rather than
jumping forward across an earlier underrun.

Windows focus changes mute/unmute through the audio manager while movie playback
continues. Volume changes apply to existing voices and movie audio immediately,
even inside loading loops that do not update the audio manager. Explicit video-player
focus hooks are no-ops, matching Bink. WindowVideo state changes explicitly pause
the stream voice and clock; resuming preserves position instead of catching up.
This intentionally pauses panel audio immediately rather than reproducing Bink's
buffer-drain delay. Bink window playback itself is unchanged.
The Bink path's 0.8 gain factor is retained without its nonzero-volume workaround,
so zero truly mutes. Seek clears queued frames/PCM and resampler state, rewinds
the demuxer, decodes to the requested
image and trims preceding audio. Starting from zero reconstructs Bink audio overlap
and resampler history instead of guessing a codec-specific preroll. Long seeks cost
more decoding/latency, serviced in bounded packet batches; the movie clock starts
when the target image is available, not while preroll is still decoding.

`isFrameReady` reports only a due image. `frameNext` consumes that image and releases
its decoder buffer after rendering, including the last image; it does not loop.
`isPlaybackComplete` separately waits for all images to be consumed, the final
image interval, demux/converter EOF and audio drain. Window/UI callers explicitly
restart looping movies with `frameGoto(0)`. The default completion query preserves
the existing Bink last-frame readiness contract.

Releasing the presented image also lets a single-frame queue reach demux EOF
without retaining a large last frame while waiting for completion. Frame and
completion polling service decoding, including blocking loading/score screens
that do not update the normal subsystems. No background decoder or new production
startup/test mode is introduced.

## Device loss

Real-device playback checks connection health on the owning thread, at most every
100 ms of explicit service. A disconnected endpoint is reopened at most once per
second using `ALC_SOFT_reopen_device`. The device always plays on the Windows
default output: recovery reopens the current default, and a change of the system
default (`ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT`) moves an open device to the
new default without a recovery. The device list is refreshed before reopening.

A successful native return is insufficient: connection and output mode must also
be valid. Context/source objects and queued PCM remain owned throughout recovery.
A recovery epoch restores each source's playing intent once; paused sources stay
paused and callback-completed sources are not replayed. Health getters and gain
setters do not reopen devices or query native connection health per voice. The
manager's update and the movie voice's explicit update service this path, including
manual movie loops. A movie keeps playing silently on the wall clock while its
device is lost; on recovery its voice restarts at the current playback position, so
the missed audio is skipped rather than replayed. A paused panel still holds its
position.

Automatic recovery needs `ALC_EXT_disconnect` and `ALC_SOFT_reopen_device`, available
in the pinned OpenAL Soft build. There is no new engine background thread. Retry
frequency is bounded, but a native reopen may still wait for the audio driver.
Already-buffered hardware output can be lost; this is not a promise of gapless or
sample-perfect A/V recovery.

Direct owners call `device.update()` once per tick, then `stream.update()` for
each stream. `OpenALMovieVoice::update()` performs both for manual movie loops.
Status, health and gain access never retry a device.
