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
set(RTS_SOURCE_URL "https://github.com/community-outpost/genonline-client" CACHE STRING "Public repository of this source that the audio library notice names as the source of the build.")

add_feature_info(OptimizedBuild RTS_BUILD_OPTION_OPTIMIZED "Aggressive Release optimization")
add_feature_info(PGO RTS_BUILD_OPTION_PGO "Profile-guided optimization mode")

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

# Modern media decides the FFmpeg video block in Core/GameEngineDevice. It turns the upstream option on and leaves a value passed by the user alone otherwise.
if(RTS_BUILD_OPTION_MODERN_MEDIA)
    set(RTS_BUILD_OPTION_FFMPEG ON)
endif()

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
