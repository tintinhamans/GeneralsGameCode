# The VC++ runtime check of the game executables, shared by the Zero Hour and Generals targets.

set(GENONLINE_VCRUNTIME_CHECK_SOURCE "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/VCRuntimeCheck.cpp")

function(genonline_vcruntime_check_sources target)
    target_sources(${target} PRIVATE
        "${GENONLINE_VCRUNTIME_CHECK_SOURCE}"
    )
endfunction()

function(genonline_vcruntime_check_link target)
    # Stamp the file version of the runtime this toolset ships, the minimum VCRuntimeCheck.cpp accepts
    if(MSVC AND NOT IS_VS6_BUILD)
        # cl.exe lives in VC/Tools/MSVC/<ver>/bin/Host<arch>/<arch>
        get_filename_component(_go_vc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_go_vc_dir "${_go_vc_dir}/../../../../../.." ABSOLUTE)
        set(_go_redist_version_file "${_go_vc_dir}/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_go_redist_arch x64)
        else()
            set(_go_redist_arch x86)
        endif()

        # The redist of the toolset in use, VS names its newest redist in the default file even when an older toolset compiles
        get_filename_component(_go_toolset_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_go_toolset_dir "${_go_toolset_dir}/../../.." ABSOLUTE)
        get_filename_component(_go_toolset_version "${_go_toolset_dir}" NAME)
        set(_go_msvcp "")
        if(_go_toolset_version MATCHES "^([0-9]+\\.[0-9]+)\\.")
            file(GLOB _go_msvcp "${_go_vc_dir}/Redist/MSVC/${CMAKE_MATCH_1}.*/${_go_redist_arch}/Microsoft.VC*.CRT/msvcp140.dll")
        endif()
        if(NOT _go_msvcp AND EXISTS "${_go_redist_version_file}")
            file(STRINGS "${_go_redist_version_file}" _go_redist_dir_version LIMIT_COUNT 1)
            string(STRIP "${_go_redist_dir_version}" _go_redist_dir_version)
            file(GLOB _go_msvcp "${_go_vc_dir}/Redist/MSVC/${_go_redist_dir_version}/${_go_redist_arch}/Microsoft.VC*.CRT/msvcp140.dll")
        endif()

        set(_go_msvcp_version "")
        if(_go_msvcp)
            list(GET _go_msvcp 0 _go_msvcp)
            execute_process(
                COMMAND powershell -NoProfile -NonInteractive -Command "(Get-Item -LiteralPath '${_go_msvcp}').VersionInfo.FileVersion"
                OUTPUT_VARIABLE _go_msvcp_version
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
        endif()

        if(_go_msvcp_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
            message(STATUS "VC++ runtime check requires ${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3} or newer")
            set_source_files_properties("${GENONLINE_VCRUNTIME_CHECK_SOURCE}" PROPERTIES COMPILE_DEFINITIONS
                "GENONLINE_VCREDIST_MAJOR=${CMAKE_MATCH_1};GENONLINE_VCREDIST_MINOR=${CMAKE_MATCH_2};GENONLINE_VCREDIST_BUILD=${CMAKE_MATCH_3}")
        else()
            message(WARNING "Could not read the toolset's msvcp140.dll version, the VC++ runtime check falls back to the compiler version")
        endif()

        # New STL features live in satellite DLLs older redistributables lack, and a missing import stops the
        # loader before any game code runs. Delay-load them so VCRuntimeCheck.cpp can detect and install instead.
        # LNK4199 is the harmless "no imports found" warning for satellites the exe doesn't use (yet).
        target_link_options(${target} PRIVATE
            "/DELAYLOAD:msvcp140_atomic_wait.dll"
            "/DELAYLOAD:msvcp140_1.dll"
            "/DELAYLOAD:msvcp140_2.dll"
            "/DELAYLOAD:msvcp140_codecvt_ids.dll"
            "/IGNORE:4199"
        )
        target_link_libraries(${target} PRIVATE delayimp)
    endif()
endfunction()
