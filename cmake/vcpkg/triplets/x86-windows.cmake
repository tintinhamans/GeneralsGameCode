set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# Per-port static/DLL linkage of the LGPL media libraries (FFmpeg, OpenAL Soft):
# DLLs, or static with RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC (cmake/config-vcpkg.cmake)
if(PORT MATCHES "^(ffmpeg|openal-soft|fmt)$")
    set(VCPKG_ENV_PASSTHROUGH RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC)
    if("$ENV{RTS_BUILD_OPTION_OPENAL_FFMPEG_STATIC}" STREQUAL "ON")
        set(VCPKG_LIBRARY_LINKAGE static)
        # Their dependencies (fmt) are static too: a dependency DLL would allocate memory
        # that the engine operator delete then frees
        # Untracked: compiler tracking already keys the ABI on the real compiler
        set(VCPKG_ENV_PASSTHROUGH_UNTRACKED VCToolsVersion)
        if(DEFINED ENV{VCToolsVersion})
            # A static C++ library must come from the MSVC toolset that links the game
            set(VCPKG_PLATFORM_TOOLSET_VERSION "$ENV{VCToolsVersion}")
        endif()
    endif()
endif()
