# FFmpeg decoder foundation tests

This slice supplies seekable engine `File` decoding and device-independent streaming PCM. It does **not** change Miles duration queries, audio playback, OpenAL, movie scheduling, or backend selection. Existing movie frame callbacks remain available.

`FFmpegFile::open` consumes ownership of its `File` on success and failure; `File::close` releases it. Do not pass an already-owned pointer back to either wrapper. Reopening with a new handle closes the previous handle. `decodePacket()` returns false at drained EOF or on error; `hasError()` distinguishes those outcomes. `seekFrame()` seeks to a preceding keyframe, not an exact presentation frame; consumers must decode forward. `rewind()` resets decoder and I/O error state.

`FFmpegAudioFile` selects the first audio stream, converts each frame's actual sample format/layout/rate into caller-selected mono/stereo interleaved S16 PCM, and flushes resampling delay at EOF. Callback pointers are borrowed only until the callback returns; counts are samples **per channel**, not bytes. Callbacks must not close, reopen, seek or recursively decode the same object. The wrapper is single-threaded. Metadata getters describe emitted PCM; duration is computed from emitted samples and becomes final only after successful complete decoding. It is not a cheap container-duration replacement for script timing. Default FFmpeg skip-sample handling trims MP3 encoder delay/padding; the fixture verifies exactly one second, including after rewind.

## Build and run (x86 VS 2022 command prompt)

```powershell
cmake -S tests/ffmpeg -B build/ffmpeg-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DFFMPEG_ROOT=C:/path/to/minimal-ffmpeg/install
cmake --build build/ffmpeg-tests
ctest --test-dir build/ffmpeg-tests --output-on-failure
```

`FFMPEG_ROOT` intentionally selects a local installation. Without it, discovery first permits an existing FFmpeg CMake package and then searches normal include/library locations. No download or global install occurs. The game option remains off by default; enabling it on old MSVC fails explicitly.

Tests compile the actual production decoder/converter sources with narrow `File`/`BaseType` adapters, not copied implementations. These adapters do not prove linkage to the full engine. Unit tests cover sample-exact PCM, resampling drain, independent instances, ownership/reopen, failed open, injected read errors, and recovery by rewind. The generated MP3 fixture covers delay/padding trimming and decoded sample content against its analytic 440 Hz reference. A 5% relative RMS bound permits lossy encoding/resampling differences while rejecting silence, wrong amplitude/phase, and major corruption. Stereo PCM uses different exact left/right values to catch channel swaps and interleaving errors.

For installed assets, `run_corpus.py` accepts a JSON manifest of bounded BIG entries or loose files (`path`, `offset`, `size`, `name`, `sha256`). It performs full decoding, PCM conversion and rewind, with four workers and a 180-second timeout per asset. Optional `--reference` compares every output count with independent decoding results. No commercial assets are included here.

```powershell
python tests/ffmpeg/run_corpus.py build/ffmpeg-tests/ffmpeg_foundation_tests.exe manifest.json build/corpus.jsonl --reference independent-results.jsonl
```

Validated locally against minimal FFmpeg 9.0.2 x86. A full modern MSVC Zero Hour Release x86 game build also compiled and linked successfully (1,101 build steps), with a subsequent incremental build succeeding. Logs are retained locally in `build/go-modern-build.log` and `build/go-modern-final.log`. Both Generals and Zero Hour headers additionally passed focused object compilation. No game was launched: actual engine BIG/File runtime integration, audio devices and playback remain unverified. The PCM foundation is not yet wired into a playback backend.

The pinned minimal-library build recipe is in `scripts/ffmpeg/README.md`.
