# Generals Online media options, read before project() because they select vcpkg manifest features.

option(RTS_BUILD_OPTION_MODERN_MEDIA "Build OpenAL audio and FFmpeg movies (modern MSVC only)" ON)
option(RTS_BUILD_OPTION_LEGACY_MEDIA "Build Miles audio and Bink movies" ON)
if(RTS_BUILD_OPTION_MODERN_MEDIA AND (NOT VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET MATCHES "windows"))
    # the trimmed FFmpeg and OpenAL Soft overlay ports
    list(APPEND VCPKG_MANIFEST_FEATURES "openal-audio")
endif()
