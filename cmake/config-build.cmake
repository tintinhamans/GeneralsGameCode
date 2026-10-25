# Do we want to build extra SDK stuff or just the game binary?
option(RTS_BUILD_CORE_TOOLS "Build core tools" ON)
option(RTS_BUILD_CORE_EXTRAS "Build core extra tools/tests" OFF)
option(RTS_BUILD_ZEROHOUR "Build Zero Hour code." ON)
option(RTS_BUILD_GENERALS "Build Generals code." ON)
option(RTS_BUILD_OPTION_PROFILE "Build code with the \"Profile\" configuration." OFF)
option(RTS_BUILD_OPTION_PROFILE_TRACY "Build code with Tracy profiling enabled." OFF)
option(RTS_BUILD_OPTION_DEBUG "Build code with the \"Debug\" configuration." OFF)
option(RTS_BUILD_OPTION_ASAN "Build code with Address Sanitizer." OFF)
option(RTS_BUILD_OPTION_OPTIMIZED "Enable aggressive MSVC Release optimizations." OFF)
set(RTS_BUILD_OPTION_PGO "OFF" CACHE STRING "MSVC profile-guided optimization mode.")
set_property(CACHE RTS_BUILD_OPTION_PGO PROPERTY STRINGS OFF GENERATE USE)
set(RTS_PGO_FILE "${CMAKE_BINARY_DIR}/genzh.pgd" CACHE FILEPATH "MSVC PGO database path.")
option(RTS_BUILD_OPTION_VC6_FULL_DEBUG "Build VC6 with full debug info." OFF)
option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)
set(RTS_REPLAY_EXE_CRC "" CACHE STRING "Exe CRC written to replays instead of the real one, hex (0xC8FC6170) or decimal. Empty writes the real exe CRC.")
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

if(NOT RTS_BUILD_ZEROHOUR AND NOT RTS_BUILD_GENERALS)
    set(RTS_BUILD_ZEROHOUR TRUE)
    message("You must select one project to build, building Zero Hour by default.")
endif()

add_feature_info(CoreTools RTS_BUILD_CORE_TOOLS "Build Core Mod Tools")
add_feature_info(CoreExtras RTS_BUILD_CORE_EXTRAS "Build Core Extra Tools/Tests")
add_feature_info(ZeroHourStuff RTS_BUILD_ZEROHOUR "Build Zero Hour code")
add_feature_info(GeneralsStuff RTS_BUILD_GENERALS "Build Generals code")
add_feature_info(ProfileBuild RTS_BUILD_OPTION_PROFILE "Building as a \"Profile\" build")
add_feature_info(DebugBuild RTS_BUILD_OPTION_DEBUG "Building as a \"Debug\" build")
add_feature_info(AddressSanitizer RTS_BUILD_OPTION_ASAN "Building with address sanitizer")
add_feature_info(OptimizedBuild RTS_BUILD_OPTION_OPTIMIZED "Aggressive Release optimization")
add_feature_info(PGO RTS_BUILD_OPTION_PGO "Profile-guided optimization mode")

# Replay exe CRC options become compile definitions that override the GameDefines.h defaults.
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
add_feature_info(Vc6FullDebug RTS_BUILD_OPTION_VC6_FULL_DEBUG "Building VC6 with full debug info")
add_feature_info(FFmpegSupport RTS_BUILD_OPTION_FFMPEG "Building with FFmpeg support")

set(RTS_BUILD_OUTPUT_SUFFIX "" CACHE STRING "Suffix appended to output names of installable targets")

if(RTS_BUILD_ZEROHOUR)
    option(RTS_BUILD_ZEROHOUR_TOOLS "Build tools for Zero Hour" ON)
    option(RTS_BUILD_ZEROHOUR_EXTRAS "Build extra tools/tests for Zero Hour" OFF)
    option(RTS_BUILD_ZEROHOUR_DOCS "Build documentation for Zero Hour" OFF)

    add_feature_info(ZeroHourTools RTS_BUILD_ZEROHOUR_TOOLS "Build Zero Hour Mod Tools")
    add_feature_info(ZeroHourExtras RTS_BUILD_ZEROHOUR_EXTRAS "Build Zero Hour Extra Tools/Tests")
    add_feature_info(ZeroHourDocs RTS_BUILD_ZEROHOUR_DOCS "Build Zero Hour Documentation")
endif()

if(RTS_BUILD_GENERALS)
    option(RTS_BUILD_GENERALS_TOOLS "Build tools for Generals" ON)
    option(RTS_BUILD_GENERALS_EXTRAS "Build extra tools/tests for Generals" OFF)
    option(RTS_BUILD_GENERALS_DOCS "Build documentation for Generals" OFF)

    add_feature_info(GeneralsTools RTS_BUILD_GENERALS_TOOLS "Build Generals Mod Tools")
    add_feature_info(GeneralsExtras RTS_BUILD_GENERALS_EXTRAS "Build Generals Extra Tools/Tests")
    add_feature_info(GeneralsDocs RTS_BUILD_GENERALS_DOCS "Build Generals Documentation")
endif()

if(NOT IS_VS6_BUILD)
    # Because we set CMAKE_CXX_STANDARD_REQUIRED and CMAKE_CXX_EXTENSIONS in the compilers.cmake this should be enforced.
    target_compile_features(core_config INTERFACE cxx_std_20)
endif()

if(IS_VS6_BUILD AND RTS_BUILD_OPTION_VC6_FULL_DEBUG)
    target_compile_options(core_config INTERFACE ${RTS_FLAGS} /Zi)
else()
    target_compile_options(core_config INTERFACE ${RTS_FLAGS})
endif()

# This disables a lot of warnings steering developers to use windows only functions/function names.
if(MSVC)
    target_compile_definitions(core_config INTERFACE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS $<$<CONFIG:DEBUG>:_DEBUG_CRT>)
endif()

if(UNIX)
    target_compile_definitions(core_config INTERFACE _UNIX)
endif()

if(RTS_BUILD_OPTION_DEBUG)
    target_compile_definitions(core_config INTERFACE RTS_DEBUG WWDEBUG DEBUG)
else()
    target_compile_definitions(core_config INTERFACE RTS_RELEASE NDEBUG)
endif()

if(RTS_BUILD_OPTION_PROFILE)
    target_compile_definitions(core_config INTERFACE RTS_PROFILE_LEGACY)
endif()

# Define a dummy Tracy target when the build option is disabled.
if(RTS_BUILD_OPTION_PROFILE_TRACY)
    include(cmake/tracy.cmake)
else()
    add_library(core_profile_tracy INTERFACE)
endif()
