# Writes THIRD_PARTY_NOTICES.txt and LICENSE-GPL3.txt next to a game exe at build time.
# The notices list the vcpkg ports installed for the target triplet, the libraries fetched and built
# from source by this tree and the code vendored in it.

set(RTS_NOTICES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/scripts/write-third-party-notices.cmake")
set(RTS_NOTICES_LICENSES "${CMAKE_CURRENT_LIST_DIR}/../licenses")

# Returns the pinned commit of a fetched git checkout, or an empty string.
function(_rts_notices_git_commit dir out_var)
    set(commit "")
    if(EXISTS "${dir}/.git/HEAD")
        file(READ "${dir}/.git/HEAD" head)
        string(STRIP "${head}" head)
        if(head MATCHES "^[0-9a-f]+$")
            set(commit "commit ${head}")
        endif()
    endif()
    set(${out_var} "${commit}" PARENT_SCOPE)
endfunction()

# Returns the first include directory of target, or an empty string when the target does not exist.
function(_rts_notices_target_dir target out_var)
    set(dir "")
    if(TARGET ${target})
        get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
        if(dirs)
            list(GET dirs 0 dir)
        endif()
    endif()
    set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

# Appends one component to the manifest text. FILE is the license text, BEGIN and END cut a notice out of a source file.
function(_rts_notices_add manifest_var)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "ID;NAME;VERSION;LICENSE;URL;FILE;BEGIN;END" "")
    if(NOT EXISTS "${ARG_FILE}")
        message(WARNING "Third party notices: the license text of ${ARG_NAME} is missing at ${ARG_FILE}")
        return()
    endif()
    set(text "${${manifest_var}}")
    string(APPEND text "list(APPEND NOTICE_IDS ${ARG_ID})\n")
    foreach(key NAME VERSION LICENSE URL FILE BEGIN END)
        string(APPEND text "set(NOTICE_${ARG_ID}_${key} [==[${ARG_${key}}]==])\n")
    endforeach()
    set(${manifest_var} "${text}" PARENT_SCOPE)
endfunction()

# Writes the manifest of the components built from source once per build tree and returns its path.
function(_rts_notices_manifest out_var)
    set(manifest_file "${CMAKE_BINARY_DIR}/third-party-notices/components.cmake")
    get_property(written GLOBAL PROPERTY RTS_NOTICES_MANIFEST_WRITTEN)
    if(written)
        set(${out_var} "${manifest_file}" PARENT_SCOPE)
        return()
    endif()

    set(manifest "")

    FetchContent_GetProperties(gamespy)
    if(gamespy_SOURCE_DIR)
        _rts_notices_git_commit("${gamespy_SOURCE_DIR}" version)
        _rts_notices_add(manifest ID gamespy NAME "GameSpy SDK" VERSION "${version}" LICENSE "BSD-3-Clause"
            URL "https://github.com/TheSuperHackers/GamespySDK" FILE "${gamespy_SOURCE_DIR}/LICENSE")
    endif()

    FetchContent_GetProperties(gamemath)
    if(gamemath_SOURCE_DIR)
        _rts_notices_git_commit("${gamemath_SOURCE_DIR}" version)
        _rts_notices_add(manifest ID gamemath NAME "GameMath" VERSION "${version}" LICENSE "BSD-2-Clause AND FDLIBM"
            URL "https://github.com/TheSuperHackers/GameMath" FILE "${gamemath_SOURCE_DIR}/LICENSE.md")
    endif()

    _rts_notices_target_dir(liblzhl lzhl_dir)
    if(lzhl_dir)
        _rts_notices_git_commit("${lzhl_dir}" version)
        _rts_notices_add(manifest ID lzhl NAME "LZH-Light 1.0" VERSION "${version}" LICENSE "LZH-Light notice"
            URL "https://github.com/TheSuperHackers/lzhl-1.0" FILE "${lzhl_dir}/lzhl.h" BEGIN "LZH-Light algorithm implementation" END " */")
    endif()

    _rts_notices_target_dir(monocypher monocypher_dir)
    if(monocypher_dir)
        get_filename_component(monocypher_dir "${monocypher_dir}" DIRECTORY)
        _rts_notices_git_commit("${monocypher_dir}" version)
        _rts_notices_add(manifest ID monocypher NAME "Monocypher 4.0.2" VERSION "${version}" LICENSE "BSD-2-Clause OR CC0-1.0"
            URL "https://github.com/LoupVaillant/Monocypher" FILE "${monocypher_dir}/LICENCE.md")
    endif()

    _rts_notices_target_dir(libzlib zlib_dir)
    if(zlib_dir)
        _rts_notices_git_commit("${zlib_dir}" version)
        _rts_notices_add(manifest ID zlib114 NAME "zlib 1.1.4" VERSION "${version}" LICENSE "Zlib"
            URL "https://github.com/TheSuperHackers/zlib-1.1.4" FILE "${zlib_dir}/zlib.h" BEGIN "Copyright" END "*/")
    endif()

    FetchContent_GetProperties(stb)
    if(stb_SOURCE_DIR)
        _rts_notices_git_commit("${stb_SOURCE_DIR}" version)
        _rts_notices_add(manifest ID stb_fetched NAME "stb" VERSION "${version}" LICENSE "MIT OR Unlicense"
            URL "https://github.com/TheSuperHackers/stb" FILE "${stb_SOURCE_DIR}/LICENSE")
    endif()

    FetchContent_GetProperties(tracy)
    if(tracy_SOURCE_DIR AND TARGET TracyClient)
        _rts_notices_git_commit("${tracy_SOURCE_DIR}" version)
        _rts_notices_add(manifest ID tracy NAME "Tracy Profiler 0.13.1" VERSION "${version}" LICENSE "BSD-3-Clause"
            URL "https://github.com/TheSuperHackers/tracy" FILE "${tracy_SOURCE_DIR}/LICENSE")
    endif()

    FetchContent_GetProperties(dx8)
    if(dx8_SOURCE_DIR AND MSVC)
        _rts_notices_git_commit("${dx8_SOURCE_DIR}" version)
        _rts_notices_add(manifest ID dx8 NAME "Microsoft DirectX 8 SDK libraries" VERSION "${version}" LICENSE "Microsoft DirectX 8 SDK terms"
            URL "https://github.com/TheSuperHackers/min-dx8-sdk" FILE "${RTS_NOTICES_LICENSES}/DirectX8-SDK-NOTICE.txt")
    endif()

    set(vendor "${CMAKE_SOURCE_DIR}/Core/GameEngine/Include/GameNetwork/GenOnline")
    _rts_notices_add(manifest ID nlohmann_json NAME "JSON for Modern C++" VERSION "3.11.3" LICENSE "MIT"
        URL "https://github.com/nlohmann/json" FILE "${RTS_NOTICES_LICENSES}/nlohmann-json-LICENSE.txt")
    _rts_notices_add(manifest ID stb_vendored NAME "stb_image_write and stb_image_resize" LICENSE "MIT OR Unlicense"
        URL "https://github.com/nothings/stb" FILE "${vendor}/Vendor/stb_image/stb_image_write.h"
        BEGIN "This software is available under 2 licenses" END "*/")
    _rts_notices_add(manifest ID rsa_md5 NAME "RSA Data Security, Inc. MD5 Message-Digest Algorithm" LICENSE "RSA-MD"
        URL "https://www.ietf.org/rfc/rfc1321.txt" FILE "${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib/md5.cpp"
        BEGIN "Copyright (C) 1991-2, RSA Data Security" END " */")
    _rts_notices_add(manifest ID discord_rpc NAME "Discord RPC, shipped as discord-rpc.dll" LICENSE "MIT"
        URL "https://github.com/discord/discord-rpc"
        FILE "${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/GenOnline/Vendor/DiscordRPC/LICENSE.txt")

    file(CONFIGURE OUTPUT "${manifest_file}" CONTENT "${manifest}" @ONLY)
    set_property(GLOBAL PROPERTY RTS_NOTICES_MANIFEST_WRITTEN TRUE)
    set(${out_var} "${manifest_file}" PARENT_SCOPE)
endfunction()

# Writes THIRD_PARTY_NOTICES.txt and copies the GPL text as LICENSE-GPL3.txt next to the exe of target.
function(rts_add_license_files target)
    _rts_notices_manifest(manifest_file)
    set(vcpkg_dir "")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        set(vcpkg_dir "${VCPKG_INSTALLED_DIR}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DMANIFEST=${manifest_file}"
            "-DVCPKG_INSTALLED_DIR=${vcpkg_dir}"
            "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DPROGRAM=$<TARGET_FILE_NAME:${target}>"
            "-DOUTPUT=$<TARGET_FILE_DIR:${target}>/THIRD_PARTY_NOTICES.txt"
            -P "${RTS_NOTICES_SCRIPT}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE.md" "$<TARGET_FILE_DIR:${target}>/LICENSE-GPL3.txt"
        VERBATIM)
endfunction()

# Installs the license files that the post build steps place next to the exe of target.
function(rts_install_license_files target destination)
    install(FILES
        "$<TARGET_FILE_DIR:${target}>/THIRD_PARTY_NOTICES.txt"
        "$<TARGET_FILE_DIR:${target}>/LICENSE-GPL3.txt"
        DESTINATION "${destination}")
    if(WIN32 AND RTS_BUILD_OPTION_MODERN_MEDIA)
        install(FILES
            "$<TARGET_FILE_DIR:${target}>/audio-libraries-NOTICE.txt"
            "$<TARGET_FILE_DIR:${target}>/ffmpeg-LICENSE.txt"
            "$<TARGET_FILE_DIR:${target}>/openal-soft-LICENSE.txt"
            "$<TARGET_FILE_DIR:${target}>/openal-soft-pffft-LICENSE.txt"
            DESTINATION "${destination}")
    endif()
endfunction()
