# Generals Online part of Generals/Code/Main/CMakeLists.txt, included right after add_executable(g_generals).

rts_copy_modern_media_files(g_generals)
rts_add_license_files(g_generals)

macro(genonline_generals_main_targets)
    set_target_properties(g_generals PROPERTIES OUTPUT_NAME "genonline_generals${RTS_BUILD_OUTPUT_SUFFIX}")

    if(WIN32)
        add_custom_command(TARGET g_generals POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC/discord-rpc.dll"
                "$<TARGET_FILE_DIR:g_generals>/discord-rpc.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC/LICENSE.txt"
                "$<TARGET_FILE_DIR:g_generals>/discord-rpc-LICENSE.txt"
        )

        if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe")
            # Sentry's crash daemon runs beside the game as CrashReportHandler.exe, the name InitSentry passes to sentry.
            add_custom_command(TARGET g_generals POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe"
                    "$<TARGET_FILE_DIR:g_generals>/CrashReportHandler.exe"
            )
        endif()
    endif()
endmacro()

macro(genonline_generals_install)
    if(RTS_INSTALL_PREFIX_GENERALS)
        rts_install_license_files(g_generals "${RTS_INSTALL_PREFIX_GENERALS}")
        if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe")
            # sentry's native backend needs its crash daemon next to the game, see the post build step above.
            install(PROGRAMS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/sentry-crash.exe"
                DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" RENAME CrashReportHandler.exe)
        endif()
    endif()
endmacro()

cmake_language(DEFER CALL genonline_generals_main_targets)
# The install rules need the install path that Generals/CMakeLists.txt reads after this directory.
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}/Generals CALL genonline_generals_install)
