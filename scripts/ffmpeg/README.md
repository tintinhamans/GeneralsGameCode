# Minimal FFmpeg for the decoder foundation

This builds the tested FFmpeg **9.0.2**, x86 Windows shared-library profile. It downloads official source only; no commercial game assets or third-party runtime binaries are checked in. The pinned SHA256 is `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`. The original archive was verified with the official FFmpeg release key, fingerprint `FCF986EA15E6E293A5644F10B4322F04D67658D8`.

Prerequisites: Visual Studio 2022 C++ tools, MSYS2 bash/make/coreutils/curl/tar, and NASM. The validated setup used MSVC 19.44.35229 and NASM 2.16.03. No tools are installed by this script. Keep runtime CPU detection and assembly enabled; do not add `--disable-asm` to bypass a missing assembler.

From an **x86 VS 2022 developer command prompt**, expose NASM on PATH, then run (adjust tool locations):

```bat
set PATH=C:\path\to\nasm;%PATH%
set MSYS2_PATH_TYPE=inherit
C:\tools\msys64\usr\bin\bash.exe scripts/ffmpeg/build-minimal.sh build/ffmpeg-minimal 4
```

Run from the repository root, or use absolute script/work paths. An existing archive may be placed in the work directory for offline use; its hash is always checked. Existing source/build/install directories are refused rather than deleted. Output stays entirely in the requested directory.

Five libraries are produced: avformat, avcodec, avutil, swscale, swresample. Only Bink/WAV/MP3 demuxing, Bink video/DCT audio, PCM16LE, IMA WAV and floating-point MP3 decoding, and the MPEG-audio parser are enabled. No network, file protocols, encoders, devices, filters or programs are built; the engine supplies custom AVIO. Release optimization and `/OPT:REF /OPT:ICF` are explicit. Debug symbols are disabled. The configure summary, compiler/tool versions, build logs and measured DLL sizes are retained beside the installation.

Point `FFMPEG_ROOT` at the resulting `install` directory for the game option or standalone tests. This is a reproducible source/configuration recipe, not a promise of byte-identical PE output across compiler versions or timestamps. Do not ship import libraries, build tools or test executables as runtime dependencies. Preserve applicable FFmpeg notices/source/configuration when distributing the DLLs; this profile reports LGPL 2.1-or-later.
