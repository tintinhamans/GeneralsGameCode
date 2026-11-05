# Contains options for VCPKG features

option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)
# a local FFMPEG_ROOT (scripts/ffmpeg) replaces the vcpkg FFmpeg
if(RTS_BUILD_OPTION_FFMPEG AND NOT FFMPEG_ROOT)
    list(APPEND VCPKG_MANIFEST_FEATURES "ffmpeg")
endif()
