# Contains options for VCPKG features

option(RTS_BUILD_OPTION_OPENAL_FFMPEG "Build FFmpeg movies, and OpenAL audio on Windows" OFF)
option(RTS_BUILD_OPTION_MILES_BINK "Build Miles audio and Bink movies" ON)
# RTS_BUILD_OPTION_FFMPEG is the former name of RTS_BUILD_OPTION_OPENAL_FFMPEG
if(RTS_BUILD_OPTION_FFMPEG AND NOT RTS_BUILD_OPTION_OPENAL_FFMPEG)
    message(DEPRECATION "RTS_BUILD_OPTION_FFMPEG is deprecated; use RTS_BUILD_OPTION_OPENAL_FFMPEG")
    set(RTS_BUILD_OPTION_OPENAL_FFMPEG ON)
endif()
if(RTS_BUILD_OPTION_OPENAL_FFMPEG)
    list(APPEND VCPKG_MANIFEST_FEATURES "openal-ffmpeg")
endif()

# FFmpeg and OpenAL Soft are LGPL DLLs unless this is on
option(RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC "Link FFmpeg and OpenAL Soft statically instead of as DLLs" OFF)
# cmake/vcpkg/triplets/x86-windows.cmake reads this through VCPKG_ENV_PASSTHROUGH
if(RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC)
    set(ENV{RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC} ON)
else()
    set(ENV{RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC} OFF)
endif()
