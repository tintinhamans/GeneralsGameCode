# Minimal FFmpeg for the decoder foundation

This builds the tested FFmpeg **9.0.2**, x86 Windows shared-library profile. It downloads official source only; no commercial game assets or third-party runtime binaries are checked in. The pinned SHA256 is `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`. The original archive was verified with the official FFmpeg release key, fingerprint `FCF986EA15E6E293A5644F10B4322F04D67658D8`.

Prerequisites: Visual Studio 2022 C++ tools, MSYS2 bash/make/coreutils/curl/tar, and NASM. The validated setup used MSVC 19.44.35229 and NASM 2.16.03. No tools are installed by this script. Keep runtime CPU detection and assembly enabled; do not add `--disable-asm` to bypass a missing assembler.

From an **x86 VS 2022 developer command prompt**, expose NASM on PATH, then run (adjust tool locations):

```bat
set PATH=C:\path\to\nasm;%PATH%
set MSYS2_PATH_TYPE=inherit
C:\tools\msys64\usr\bin\bash.exe scripts/ffmpeg/build-minimal.sh build/ffmpeg-minimal 4
```

Run from the repository root, or use absolute script/work paths. The work directory defaults to `build/ffmpeg-minimal` in the repository. An existing archive may be placed in the work directory for offline use; its hash is always checked. Existing source/build/install directories are refused rather than deleted. Output stays entirely in the requested directory.

Five libraries are produced: avformat, avcodec, avutil, swscale, swresample. Only Bink/WAV/MP3 demuxing, Bink video/DCT audio, PCM16LE, IMA WAV and floating-point MP3 decoding, and the MPEG-audio parser are enabled. No network, file protocols, encoders, devices, filters or programs are built; the engine supplies custom AVIO. Release optimization and `/OPT:REF /OPT:ICF` are explicit. Debug symbols are disabled. The configure summary, compiler/tool versions, build logs and measured DLL sizes are retained beside the installation.

Point `FFMPEG_ROOT` at the resulting `install` directory for the game option or standalone tests. This is a reproducible source/configuration recipe, not a promise of byte-identical PE output across compiler versions or timestamps. Do not ship import libraries, build tools or test executables as runtime dependencies. Preserve applicable FFmpeg notices/source/configuration when distributing the DLLs; this profile reports LGPL 2.1-or-later.

## Static libraries

`--static` builds the same trimmed component set as static libraries (`--enable-static --disable-shared`) compiled against the DLL C runtime (`--extra-cflags=-MD`), so the game links FFmpeg in and ships no FFmpeg DLLs:

```bat
C:\tools\msys64\usr\bin\bash.exe scripts/ffmpeg/build-minimal.sh --static
```

The `win32-vcpkg-generalsmd-openal` CMake preset no longer needs this: the vcpkg overlay port `cmake/ports/ffmpeg-minimal` builds the same configuration, pinned archive and static libraries as part of `cmake --preset`. This script stays an optional manual route; pass its `install` folder as `FFMPEG_ROOT` to use it instead. The work directory then defaults to `build/ffmpeg-static`. A work directory and job count can still follow the flag. The install holds `avformat.lib`, `avcodec.lib`, `avutil.lib`, `swscale.lib` and `swresample.lib` and no `bin` folder; `cmake/FindFFMPEG.cmake` detects that and links the Windows libraries FFmpeg needs. Measured library sizes go to `library-sizes.txt`. The game build copies the FFmpeg LGPL notice next to the executable. See `scripts/openal/README.md` for the complete static audio build.
