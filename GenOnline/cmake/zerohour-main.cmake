# Generals Online part of GeneralsMD/Code/Main/CMakeLists.txt, included right after add_executable(z_generals).

rts_copy_modern_media_files(z_generals)
rts_add_license_files(z_generals)

include(${CMAKE_CURRENT_LIST_DIR}/vcruntime-check.cmake)
genonline_vcruntime_check_sources(z_generals)
include(${CMAKE_CURRENT_LIST_DIR}/version-info.cmake)
genonline_version_info(z_generals zerohour "Generals Online client for Zero Hour")

macro(genonline_zerohour_main_targets)
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "genonline_zerohour${RTS_BUILD_OUTPUT_SUFFIX}")

    genonline_vcruntime_check_link(z_generals)

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
