# Contains options for VCPKG features

option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)
set(RTS_BUILD_AUDIO_BACKEND "MILES" CACHE STRING "Zero Hour audio backend (MILES or OPENAL)")
# the OpenAL backend takes the trimmed FFmpeg and OpenAL Soft overlay ports
if(RTS_BUILD_AUDIO_BACKEND STREQUAL "OPENAL")
    list(APPEND VCPKG_MANIFEST_FEATURES "openal-audio")
endif()
# FFmpeg video uses the same trimmed port; a local FFMPEG_ROOT (scripts/ffmpeg) replaces it
if(RTS_BUILD_OPTION_FFMPEG AND NOT FFMPEG_ROOT)
    list(APPEND VCPKG_MANIFEST_FEATURES "ffmpeg")
endif()
