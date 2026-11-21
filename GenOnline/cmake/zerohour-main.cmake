# Generals Online part of GeneralsMD/Code/Main/CMakeLists.txt, included right after add_executable(z_generals).

rts_copy_modern_media_files(z_generals)
rts_add_license_files(z_generals)

target_sources(z_generals PRIVATE
    VCRuntimeCheck.cpp
)

macro(genonline_zerohour_main_targets)
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "genonline_zerohour${RTS_BUILD_OUTPUT_SUFFIX}")

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
            set_source_files_properties(VCRuntimeCheck.cpp PROPERTIES COMPILE_DEFINITIONS
                "GENONLINE_VCREDIST_MAJOR=${CMAKE_MATCH_1};GENONLINE_VCREDIST_MINOR=${CMAKE_MATCH_2};GENONLINE_VCREDIST_BUILD=${CMAKE_MATCH_3}")
        else()
            message(WARNING "Could not read the toolset's msvcp140.dll version, the VC++ runtime check falls back to the compiler version")
        endif()

        # New STL features live in satellite DLLs older redistributables lack, and a missing import stops the
        # loader before any game code runs. Delay-load them so VCRuntimeCheck.cpp can detect and install instead.
        # LNK4199 is the harmless "no imports found" warning for satellites the exe doesn't use (yet).
        target_link_options(z_generals PRIVATE
            "/DELAYLOAD:msvcp140_atomic_wait.dll"
            "/DELAYLOAD:msvcp140_1.dll"
            "/DELAYLOAD:msvcp140_2.dll"
            "/DELAYLOAD:msvcp140_codecvt_ids.dll"
            "/IGNORE:4199"
        )
        target_link_libraries(z_generals PRIVATE delayimp)
    endif()

    if(WIN32)
        add_custom_command(TARGET z_generals POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC/discord-rpc.dll"
                "$<TARGET_FILE_DIR:z_generals>/discord-rpc.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC/LICENSE.txt"
                "$<TARGET_FILE_DIR:z_generals>/discord-rpc-LICENSE.txt"
        )

        if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe")
            # sentry's native backend runs its crash daemon as a separate process next to the game.
            # The daemon is renamed to CrashReportHandler.exe because EAC lets a process whose name starts with CrashReport
            # read the game after a crash. InitSentry passes this name to sentry_options_set_handler_pathw.
            add_custom_command(TARGET z_generals POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe"
                    "$<TARGET_FILE_DIR:z_generals>/CrashReportHandler.exe"
            )
        endif()
    endif()
endmacro()

macro(genonline_zerohour_install)
    if(RTS_INSTALL_PREFIX_ZEROHOUR)
        rts_install_license_files(z_generals "${RTS_INSTALL_PREFIX_ZEROHOUR}")
        if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe")
            # sentry's native backend needs its crash daemon next to the game, see the post build step above.
            install(PROGRAMS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe"
                DESTINATION "${RTS_INSTALL_PREFIX_ZEROHOUR}" RENAME CrashReportHandler.exe)
        endif()
        if(WIN32)
            # discord-rpc.dll is copied next to the build output by the post build step above.
            set(DISCORD_RPC_DIR "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC")
            install(FILES "${DISCORD_RPC_DIR}/discord-rpc.dll" DESTINATION "${RTS_INSTALL_PREFIX_ZEROHOUR}")
            install(FILES "${DISCORD_RPC_DIR}/LICENSE.txt" DESTINATION "${RTS_INSTALL_PREFIX_ZEROHOUR}" RENAME discord-rpc-LICENSE.txt)
        endif()
    endif()
endmacro()

cmake_language(DEFER CALL genonline_zerohour_main_targets)
# The install rules need the install path that GeneralsMD/CMakeLists.txt reads after this directory.
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}/GeneralsMD CALL genonline_zerohour_install)
