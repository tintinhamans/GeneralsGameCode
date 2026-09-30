add_executable(z_generals WIN32)

# Use a binary name that doesn't conflict with original game.
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "GeneralsOnlineZH${RTS_BUILD_OUTPUT_SUFFIX}")
else()
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "GeneralsOnlineZH${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

target_link_libraries(z_generals PRIVATE
    binkstub
    comctl32
    core_debug
    core_profile_legacy
    d3d8
    d3dx8
    dinput8
    dxguid
    imm32
    milesstub
    vfw32
    winmm
    z_gameengine
    z_gameenginedevice
    zi_always
)

# TODO Originally referred to build host and user, replace with git info perhaps?
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
"
)

# Based on original binary values for these variables.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 4
#define VERSION_BUILDNUM 601
"
)

if(MSVC)
    target_link_options(z_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(z_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_sources(z_generals PRIVATE
    VCRuntimeCheck.cpp
    WinMain.cpp
    WinMain.h
)

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

    set(_go_msvcp "")
    if(EXISTS "${_go_redist_version_file}")
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
            "GO_VCREDIST_MAJOR=${CMAKE_MATCH_1};GO_VCREDIST_MINOR=${CMAKE_MATCH_2};GO_VCREDIST_BUILD=${CMAKE_MATCH_3}")
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

# Embed the repository's Assets/ folder into the executable so game data (window
# layouts, INI overrides, string tables, ...) ships inside the exe and is served
# through a virtual "450_450_..." archive (see EmbeddedArchiveFile and
# ArchiveFileSystem::loadMods()). Assets/ holds human-authored sources organized
# by type; StageEmbeddedAssets.cmake first lays them out as the game-relative tree
# the archive actually needs (compiling Localization/*.str to Data/<Lang>/*.csf via
# the str2csf host tool, and each language of the multi-language Localization/Languages/
# *.str to Data/Languages/<code>/*.csf, copying everything else) into a generated EmbeddedRoot/
# under the build dir, which GenerateEmbeddedData.cmake then embeds exactly as it
# used to embed a literal Data/ folder. CONFIGURE_DEPENDS reruns the glob whenever
# CMake reconfigures, which happens automatically when a file is added or removed.
set(RTS_ASSETS_DIR "${CMAKE_SOURCE_DIR}/Assets")
file(GLOB_RECURSE RTS_ASSET_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${RTS_ASSETS_DIR}/*")

set(RTS_EMBEDDED_ROOT "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRoot")
set(RTS_STAGE_ASSETS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/StageEmbeddedAssets.cmake")
set(RTS_STAGE_ASSETS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRoot.stamp")

# Same manifest trick as below: a *removed* Assets/ file doesn't make anything
# newer, so the file list's own content (rewritten only when it actually changes)
# is what forces the staging step to rerun on removal too, not just on add/edit.
set(RTS_ASSETS_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedAssetsManifest.txt")
file(GENERATE OUTPUT "${RTS_ASSETS_MANIFEST}" CONTENT "${RTS_ASSET_FILES}")

add_custom_command(
    OUTPUT "${RTS_STAGE_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DASSETS_DIR=${RTS_ASSETS_DIR}"
        "-DOUTPUT_DIR=${RTS_EMBEDDED_ROOT}"
        "-DSTR2CSF_EXE=$<TARGET_FILE:str2csf>"
        -P "${RTS_STAGE_ASSETS_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E touch "${RTS_STAGE_ASSETS_STAMP}"
    DEPENDS ${RTS_ASSET_FILES} str2csf "${RTS_STAGE_ASSETS_SCRIPT}" "${RTS_ASSETS_MANIFEST}"
    COMMENT "Staging Assets/ into the embedded game-relative tree"
    VERBATIM
)

set(RTS_EMBEDDED_DATA_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedDataTable.cpp")
set(RTS_EMBEDDED_DATA_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/GenerateEmbeddedData.cmake")

add_custom_command(
    OUTPUT "${RTS_EMBEDDED_DATA_SOURCE}"
    COMMAND ${CMAKE_COMMAND}
        "-DDATA_DIR=${RTS_EMBEDDED_ROOT}"
        "-DOUTPUT_FILE=${RTS_EMBEDDED_DATA_SOURCE}"
        -P "${RTS_EMBEDDED_DATA_SCRIPT}"
    DEPENDS "${RTS_STAGE_ASSETS_STAMP}" "${RTS_EMBEDDED_DATA_SCRIPT}"
    COMMENT "Generating embedded Data/ table"
    VERBATIM
)

target_sources(z_generals PRIVATE
    "${RTS_EMBEDDED_DATA_SOURCE}"
    EmbeddedDataTable.h
)

if(WIN32)
	add_custom_command(TARGET z_generals POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/../GameEngine/Source/GameNetwork/GeneralsOnline/Vendor/DiscordRPC/discord-rpc.dll"
			"$<TARGET_FILE_DIR:z_generals>/discord-rpc.dll"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/../GameEngine/Source/GameNetwork/GeneralsOnline/Vendor/DiscordRPC/LICENSE.txt"
			"$<TARGET_FILE_DIR:z_generals>/discord-rpc-LICENSE.txt"
	)

	if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/crashpad_handler.exe")
		# sentry's crashpad backend is an out-of-process watcher: it needs its
		# own executable next to the game, at the default handler path sentry
		# looks for (see sentry_options_set_handler_path in sentry.h).
		add_custom_command(TARGET z_generals POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/crashpad_handler.exe"
				"$<TARGET_FILE_DIR:z_generals>/crashpad_handler.exe"
		)
	endif()
endif()

# RC files optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(z_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(z_generals PRIVATE
        RTS.RC
    )
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalszh${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(z_generals)
endif()
