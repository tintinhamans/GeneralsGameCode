# OpenAL Soft and FFmpeg media

`RTS_BUILD_OPTION_OPENAL_FFMPEG=ON` plays game audio through OpenAL Soft and movies
through FFmpeg instead of Miles and Bink. Both libraries come from vcpkg: the
`openal-ffmpeg` feature in `vcpkg.json` brings the `openal-soft` port and the minimal
FFmpeg overlay port in `cmake/vcpkg/ports/ffmpeg`, which keeps only the demuxers and
decoders the game uses. The former option name `RTS_BUILD_OPTION_FFMPEG=ON` turns the
same option on.

OpenAL audio is built for the Win32 engine of Generals and Zero Hour with Visual Studio
2022 or newer. Other compilers and platforms get FFmpeg movies only and keep Miles
audio. VC6 builds Miles and Bink only.

## Building

```
cmake --preset win32-vcpkg -DRTS_BUILD_OPTION_OPENAL_FFMPEG=ON
cmake --build build/win32-vcpkg --target z_generals
```

`RTS_BUILD_OPTION_MILES_BINK` stays `ON` by default, so the build contains both media
backends. With it `OFF`, the game builds without Miles and Bink as long as the tools,
core extras and tests that still need them are off too.

## LegacyMedia

When a build contains both backends, `LegacyMedia = yes` in `Options.ini` picks Miles
audio with Bink movies at startup. `no` or a missing entry picks OpenAL Soft with
FFmpeg movies. The value is read once, so a change needs a restart.

## Static linking

`RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC=ON` links FFmpeg and OpenAL Soft into the exe
instead of shipping their DLLs next to it.

## Licenses

FFmpeg and OpenAL Soft are LGPL libraries. The build copies their license texts and
`audio-libraries-NOTICE.txt` from `Core/GameEngineDevice/Licenses` next to the exe, for
DLL and static builds alike.
