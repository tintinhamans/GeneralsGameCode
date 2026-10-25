# OpenAL Soft dependency

Pinned source: OpenAL Soft **1.25.2**, official `kcat/openal-soft` tag archive. SHA256: `fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894`.

From an x86 Visual Studio 2022 developer environment with CMake, Ninja and tar available:

```powershell
powershell -File scripts/openal/build-soft.ps1 -WorkDirectory build/openal-soft
```

The script downloads source only, verifies the pinned digest, and builds a local Release shared library. It never installs tools globally or removes existing directories. A predownloaded archive can be placed in the fresh work directory for offline use. The validated toolchain is MSVC 19.44.35229 x86; the matching configuration built WASAPI, WaveFile and Null backends. Loopback support is part of OpenAL Soft itself. SDL, EAX, examples, utilities and installed HRTF/config/preset data are disabled. No macOS implementation is introduced.

Set `OpenAL_DIR` to `install/lib/cmake/OpenAL`. `RTS_BUILD_OPTION_OPENAL=ON` compiles the new device/PCM primitives but deliberately does not select a game audio backend or activate old movie OpenAL paths. This slice requires `ALC_EXT_thread_local_context` and `AL_SOFT_callback_buffer`; unsupported implementations fail initialization rather than using a second queue implementation.

Keep OpenAL Soft's LGPL notices and applicable source/build information with distribution. No DLLs or commercial media are checked in.
