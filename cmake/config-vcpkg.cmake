# Contains options for VCPKG features

option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)
if(RTS_BUILD_OPTION_FFMPEG)
    list(APPEND VCPKG_MANIFEST_FEATURES "ffmpeg")
endif()

# FFmpeg is an LGPL DLL unless this is on
option(RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC "Link FFmpeg statically instead of as DLLs" OFF)
# cmake/vcpkg/triplets/x86-windows.cmake reads this through VCPKG_ENV_PASSTHROUGH
if(RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC)
    set(ENV{RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC} ON)
else()
    set(ENV{RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC} OFF)
endif()
