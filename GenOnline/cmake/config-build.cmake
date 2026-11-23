# Generals Online build options of cmake/config-build.cmake, included after its upstream options.

option(RTS_BUILD_OPTION_OPTIMIZED "Enable aggressive MSVC Release optimizations." OFF)
set(RTS_BUILD_OPTION_PGO "OFF" CACHE STRING "MSVC profile-guided optimization mode.")
set_property(CACHE RTS_BUILD_OPTION_PGO PROPERTY STRINGS OFF GENERATE USE)
set(RTS_PGO_FILE "${CMAKE_BINARY_DIR}/genzh.pgd" CACHE FILEPATH "MSVC PGO database path.")
# RTS_BUILD_OPTION_MODERN_MEDIA and RTS_BUILD_OPTION_LEGACY_MEDIA live in config-vcpkg.cmake. With both, media.legacy set to true in settings.client.json picks Miles and Bink at startup
if(RTS_BUILD_OPTION_MODERN_MEDIA AND (IS_VS6_BUILD OR NOT MSVC OR MSVC_VERSION LESS 1930))
    message(STATUS "Modern media needs Visual Studio 2022 or newer, building legacy media only")
    set(RTS_BUILD_OPTION_MODERN_MEDIA OFF)
endif()
if(NOT RTS_BUILD_OPTION_MODERN_MEDIA AND NOT RTS_BUILD_OPTION_LEGACY_MEDIA)
    message(FATAL_ERROR "Enable RTS_BUILD_OPTION_MODERN_MEDIA, RTS_BUILD_OPTION_LEGACY_MEDIA or both")
endif()
if(RTS_BUILD_OPTION_MODERN_MEDIA)
    target_compile_definitions(core_config INTERFACE RTS_HAS_MODERN_MEDIA)
endif()
set(RTS_EXE_CRC "" CACHE STRING "Exe CRC this build reports in lobbies and network games instead of the real one, hex (0xC8FC6170) or decimal. Empty uses the real exe CRC.")
set(RTS_REPLAY_EXE_CRC "" CACHE STRING "Exe CRC written to replays instead of the real one, hex (0xC8FC6170) or decimal. Empty writes the real exe CRC.")
set(RTS_SOURCE_URL "https://github.com/community-outpost/genonline-client" CACHE STRING "Public repository of this source that the audio library notice names as the source of the build.")
set(RTS_REPLAY_COMPATIBLE_EXE_CRCS "" CACHE STRING "Extra exe CRCs that replays treat as this build, hex or decimal separated by ; or ,. The real and written exe CRC always match.")

# Converts a hex or decimal 32 bit value to a padded 0xXXXXXXXX string.
function(rts_replay_normalize_crc Name Value OutVar)
    if(Value MATCHES "^0[xX]([0-9a-fA-F]+)$")
        string(REGEX REPLACE "^0+" "" Digits "${CMAKE_MATCH_1}")
        string(LENGTH "${Digits}" DigitsLen)
        if(DigitsLen GREATER 8)
            message(FATAL_ERROR "${Name}: '${Value}' does not fit in 32 bits")
        endif()
        if(DigitsLen EQUAL 0)
            set(Digits "0")
        endif()
        math(EXPR Parsed "0x${Digits}" OUTPUT_FORMAT HEXADECIMAL)
    elseif(Value MATCHES "^[0-9]+$")
        string(REGEX REPLACE "^0+" "" Digits "${Value}")
        string(LENGTH "${Digits}" DigitsLen)
        if(DigitsLen GREATER 10)
            message(FATAL_ERROR "${Name}: '${Value}' does not fit in 32 bits")
        endif()
        if(DigitsLen EQUAL 0)
            set(Digits "0")
        endif()
        math(EXPR Dec "${Digits}")
        if(Dec GREATER 4294967295)
            message(FATAL_ERROR "${Name}: '${Value}' does not fit in 32 bits")
        endif()
        math(EXPR Parsed "${Dec}" OUTPUT_FORMAT HEXADECIMAL)
    else()
        message(FATAL_ERROR "${Name}: '${Value}' is not a hex (0x...) or decimal number")
    endif()
    string(REGEX REPLACE "^0[xX]" "" Hex "${Parsed}")
    string(LENGTH "${Hex}" HexLen)
    math(EXPR Pad "8 - ${HexLen}")
    if(Pad GREATER 0)
        string(REPEAT "0" ${Pad} Zeros)
        set(Hex "${Zeros}${Hex}")
    endif()
    string(TOUPPER "${Hex}" Hex)
    set(${OutVar} "0x${Hex}" PARENT_SCOPE)
endfunction()

add_feature_info(OptimizedBuild RTS_BUILD_OPTION_OPTIMIZED "Aggressive Release optimization")
add_feature_info(PGO RTS_BUILD_OPTION_PGO "Profile-guided optimization mode")

# The exe CRC options become compile definitions that override the GameDefines.h defaults.
if(NOT "${RTS_EXE_CRC}" STREQUAL "")
    string(STRIP "${RTS_EXE_CRC}" RtsExeCrc)
    rts_replay_normalize_crc(RTS_EXE_CRC "${RtsExeCrc}" RtsExeCrc)
    target_compile_definitions(core_config INTERFACE RTS_EXE_CRC=${RtsExeCrc}u)
    message(STATUS "Exe CRC override: ${RtsExeCrc}")
endif()

if(NOT "${RTS_REPLAY_EXE_CRC}" STREQUAL "")
    string(STRIP "${RTS_REPLAY_EXE_CRC}" RtsReplayExeCrc)
    rts_replay_normalize_crc(RTS_REPLAY_EXE_CRC "${RtsReplayExeCrc}" RtsReplayExeCrc)
    target_compile_definitions(core_config INTERFACE RTS_REPLAY_EXE_CRC=${RtsReplayExeCrc}u)
    message(STATUS "Replay exe CRC override: ${RtsReplayExeCrc}")
endif()

# Each entry keeps a trailing comma so the list expands into an array initializer.
set(RtsReplayCompatItems "")
string(REPLACE "," ";" RtsReplayCompatList "${RTS_REPLAY_COMPATIBLE_EXE_CRCS}")
foreach(Item IN LISTS RtsReplayCompatList)
    string(STRIP "${Item}" Item)
    if(NOT Item STREQUAL "")
        rts_replay_normalize_crc(RTS_REPLAY_COMPATIBLE_EXE_CRCS "${Item}" Item)
        string(APPEND RtsReplayCompatItems "${Item}u,")
    endif()
endforeach()
if(NOT RtsReplayCompatItems STREQUAL "")
    target_compile_definitions(core_config INTERFACE "RTS_REPLAY_COMPATIBLE_EXE_CRCS=${RtsReplayCompatItems}")
    message(STATUS "Replay compatible exe CRCs: ${RtsReplayCompatItems}")
endif()

if(MSVC AND RTS_BUILD_OPTION_OPTIMIZED AND NOT IS_VS6_BUILD)
    target_compile_options(core_config INTERFACE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/arch:SSE2>
    )
    target_link_options(core_config INTERFACE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

if(MSVC AND NOT IS_VS6_BUILD)
    target_compile_options(core_config INTERFACE
        $<$<CONFIG:Release>:/guard:cf>
    )
    target_link_options(core_config INTERFACE
        $<$<CONFIG:Release>:/DYNAMICBASE>
        $<$<CONFIG:Release>:/NXCOMPAT>
        $<$<CONFIG:Release>:/guard:cf>
    )
endif()

if(NOT "${RTS_BUILD_OPTION_PGO}" MATCHES "^(OFF|GENERATE|USE)$")
    message(FATAL_ERROR "RTS_BUILD_OPTION_PGO must be OFF, GENERATE, or USE")
elseif(MSVC AND NOT "${RTS_BUILD_OPTION_PGO}" STREQUAL "OFF" AND NOT IS_VS6_BUILD)
    target_compile_options(core_config INTERFACE $<$<CONFIG:Release>:/GL>)
    target_link_options(core_config INTERFACE $<$<CONFIG:Release>:/LTCG>)
    if("${RTS_BUILD_OPTION_PGO}" STREQUAL "GENERATE")
        target_link_options(core_config INTERFACE
            $<$<CONFIG:Release>:/GENPROFILE:PGD=${RTS_PGO_FILE}>
        )
    else()
        target_link_options(core_config INTERFACE
            $<$<CONFIG:Release>:/USEPROFILE:PGD=${RTS_PGO_FILE}>
        )
    endif()
endif()
add_feature_info(ModernMedia RTS_BUILD_OPTION_MODERN_MEDIA "Building OpenAL audio and FFmpeg movies")
add_feature_info(LegacyMedia RTS_BUILD_OPTION_LEGACY_MEDIA "Building Miles audio and Bink movies")

# The upstream FFmpeg video block in Core/GameEngineDevice follows the modern media option.
set(RTS_BUILD_OPTION_FFMPEG ${RTS_BUILD_OPTION_MODERN_MEDIA})

# Ships the OpenAL and FFmpeg runtime files and the audio library licenses next to the exe of target
function(rts_copy_modern_media_files target)
    if(NOT WIN32 OR NOT RTS_BUILD_OPTION_MODERN_MEDIA)
        return()
    endif()
    find_package(Threads REQUIRED)
    find_package(OpenAL CONFIG REQUIRED)
    get_target_property(openal_library_type OpenAL::OpenAL TYPE)
    if(openal_library_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:${target}>")
    endif()
    if(FFMPEG_ROOT AND NOT FFMPEG_STATIC)
        foreach(component avformat avcodec avutil swscale swresample)
            file(GLOB ffmpeg_runtime_dlls "${FFMPEG_ROOT}/bin/${component}*.dll")
            foreach(dll IN LISTS ffmpeg_runtime_dlls)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
            endforeach()
        endforeach()
    endif()
    set(licenses "${CMAKE_SOURCE_DIR}/GenOnline/licenses/media")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DTEMPLATE=${licenses}/audio-libraries-NOTICE.txt.in"
            "-DOUTPUT=$<TARGET_FILE_DIR:${target}>/audio-libraries-NOTICE.txt"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSOURCE_URL=${RTS_SOURCE_URL}"
            -P "${CMAKE_SOURCE_DIR}/GenOnline/cmake/scripts/write-audio-notice.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${licenses}/FFmpeg-LGPL-2.1.txt" "$<TARGET_FILE_DIR:${target}>/ffmpeg-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${licenses}/OpenAL-Soft-LGPL-2.txt" "$<TARGET_FILE_DIR:${target}>/openal-soft-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${licenses}/OpenAL-Soft-PFFFT.txt" "$<TARGET_FILE_DIR:${target}>/openal-soft-pffft-LICENSE.txt")
endfunction()
