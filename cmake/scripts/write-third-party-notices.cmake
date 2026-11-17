# Writes THIRD_PARTY_NOTICES.txt for one game exe. Runs as a post build step through cmake -P.
# Inputs: MANIFEST, VCPKG_INSTALLED_DIR, VCPKG_TARGET_TRIPLET, SOURCE_DIR, PROGRAM, OUTPUT.
# The output depends only on its inputs so that equal dependency sets give identical files.

cmake_minimum_required(VERSION 3.28)

set(licenses_dir "${SOURCE_DIR}/resources/licenses")
set(rule "================================================================================")

# Reads a license text with LF line endings, optionally cut from the line holding begin up to end.
function(read_license_text file begin end out_var)
    file(READ "${file}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    if(NOT begin STREQUAL "")
        string(FIND "${text}" "${begin}" pos)
        if(pos LESS 0)
            message(FATAL_ERROR "Third party notices: '${begin}' not found in ${file}")
        endif()
        string(SUBSTRING "${text}" 0 ${pos} before)
        string(FIND "${before}" "\n" line_start REVERSE)
        math(EXPR line_start "${line_start} + 1")
        string(SUBSTRING "${text}" ${line_start} -1 text)
        if(NOT end STREQUAL "")
            string(FIND "${text}" "${end}" end_pos)
            if(end_pos GREATER 0)
                string(SUBSTRING "${text}" 0 ${end_pos} text)
            endif()
        endif()
    endif()
    string(REGEX REPLACE "^[ \t\n]*\n" "" text "${text}")
    string(REGEX REPLACE "[ \t\n]+$" "" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Appends one notice section to sections and its title to contents.
macro(add_section title license url text_var)
    string(APPEND sections "\n${rule}\n${title}\nLicense: ${license}\n")
    if(NOT "${url}" STREQUAL "")
        string(APPEND sections "Source: ${url}\n")
    endif()
    string(APPEND sections "${rule}\n\n${${text_var}}\n")
    string(APPEND contents "  ${title}\n")
endmacro()

set(sections "")
set(contents "")

# Libraries from vcpkg: every port installed for the target triplet, which is the manifest dependency closure.
set(ports "")
if(NOT VCPKG_INSTALLED_DIR STREQUAL "" AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
    file(GLOB lists "${VCPKG_INSTALLED_DIR}/vcpkg/info/*_${VCPKG_TARGET_TRIPLET}.list")
    foreach(list_file IN LISTS lists)
        get_filename_component(list_name "${list_file}" NAME)
        if(list_name MATCHES "^([a-z0-9-]+)_")
            list(APPEND ports "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(SORT ports)
endif()

if(ports)
    string(APPEND contents "\nLibraries from vcpkg\n")
endif()
set(triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
foreach(port IN LISTS ports)
    set(share "${triplet_dir}/share/${port}")
    set(version "")
    set(license "see below")
    set(url "")
    if(EXISTS "${share}/vcpkg.spdx.json")
        file(READ "${share}/vcpkg.spdx.json" spdx)
        string(JSON count LENGTH "${spdx}" packages)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON package_name GET "${spdx}" packages ${i} name)
            if(package_name STREQUAL port)
                string(JSON version GET "${spdx}" packages ${i} versionInfo)
                string(JSON license ERROR_VARIABLE no_license GET "${spdx}" packages ${i} licenseConcluded)
                if(no_license)
                    set(license "see below")
                endif()
                string(JSON url ERROR_VARIABLE no_homepage GET "${spdx}" packages ${i} homepage)
                if(no_homepage)
                    set(url "")
                endif()
                break()
            endif()
        endforeach()
    endif()

    if(port STREQUAL "sentry-native")
        # The vcpkg copyright file of sentry-native also carries Linux and Android only code.
        read_license_text("${licenses_dir}/sentry-native-NOTICE.txt" "" "" text)
    elseif(EXISTS "${share}/copyright")
        read_license_text("${share}/copyright" "" "" text)
    else()
        message(FATAL_ERROR "Third party notices: vcpkg port ${port} has no copyright file")
    endif()
    add_section("${port} ${version}" "${license}" "${url}" text)

    if(port STREQUAL "gamenetworkingsockets" AND EXISTS "${triplet_dir}/lib/webrtc-lite.lib")
        # The webrtc feature builds WebRTC into GameNetworkingSockets, and the port installs only its own license.
        set(webrtc_ref "")
        file(READ "${SOURCE_DIR}/cmake/ports/gamenetworkingsockets/portfile.cmake" portfile)
        if(portfile MATCHES "webrtc\\.googlesource\\.com/src[ \t\r\n]+REF[ \t]+([0-9a-f]+)")
            set(webrtc_ref " commit ${CMAKE_MATCH_1}")
        endif()
        read_license_text("${licenses_dir}/WebRTC-LICENSE.txt" "" "" webrtc_license)
        read_license_text("${licenses_dir}/WebRTC-PATENTS.txt" "" "" webrtc_patents)
        set(text "${webrtc_license}\n\n${webrtc_patents}")
        add_section("WebRTC${webrtc_ref}, built into gamenetworkingsockets" "BSD-3-Clause with the WebRTC patent grant"
            "https://webrtc.googlesource.com/src" text)
    endif()
endforeach()

# Libraries built from source by this tree and code vendored in it, in manifest order.
set(NOTICE_IDS "")
include("${MANIFEST}")
if(NOTICE_IDS)
    string(APPEND contents "\nLibraries built from source and code in the source tree\n")
endif()
foreach(id IN LISTS NOTICE_IDS)
    read_license_text("${NOTICE_${id}_FILE}" "${NOTICE_${id}_BEGIN}" "${NOTICE_${id}_END}" text)
    set(title "${NOTICE_${id}_NAME}")
    if(NOT NOTICE_${id}_VERSION STREQUAL "")
        string(APPEND title " ${NOTICE_${id}_VERSION}")
    endif()
    add_section("${title}" "${NOTICE_${id}_LICENSE}" "${NOTICE_${id}_URL}" text)
endforeach()

set(audio_note "")
if("ffmpeg" IN_LIST ports OR "openal-soft" IN_LIST ports)
    set(audio_note "FFmpeg and OpenAL Soft are linked statically under the GNU Lesser General Public
License, see audio-libraries-NOTICE.txt for how to obtain their source.
")
endif()

set(notices "Third party software in ${PROGRAM}

This program is licensed under the GNU General Public License version 3 with
the additional terms of Electronic Arts, see LICENSE-GPL3.txt. It contains the
third party software listed below, each under the license that follows its entry.
${audio_note}
Contents
${contents}${sections}")
string(REPLACE "\n" "\r\n" notices "${notices}")

set(old "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" old)
endif()
if(NOT old STREQUAL notices)
    file(WRITE "${OUTPUT}" "${notices}")
endif()
