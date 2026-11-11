# Do we want to build extra SDK stuff or just the game binary?
option(RTS_BUILD_CORE_TOOLS "Build core tools" ON)
option(RTS_BUILD_CORE_EXTRAS "Build core extra tools/tests" OFF)
option(RTS_BUILD_ZEROHOUR "Build Zero Hour code." ON)
option(RTS_BUILD_GENERALS "Build Generals code." ON)
option(RTS_BUILD_OPTION_PROFILE "Build code with the \"Profile\" configuration." OFF)
option(RTS_BUILD_OPTION_PROFILE_TRACY "Build code with Tracy profiling enabled." OFF)
option(RTS_BUILD_OPTION_DEBUG "Build code with the \"Debug\" configuration." OFF)
option(RTS_BUILD_OPTION_ASAN "Build code with Address Sanitizer." OFF)
option(RTS_BUILD_OPTION_VC6_FULL_DEBUG "Build VC6 with full debug info." OFF)
cmake_dependent_option(RTS_BUILD_OPTION_TESTS "Build the unit tests." OFF "NOT IS_VS6_BUILD" OFF)
cmake_dependent_option(RTS_BUILD_OPTION_BENCHMARKS "Build the benchmarks." OFF "NOT IS_VS6_BUILD" OFF)
# RTS_BUILD_OPTION_OPENAL_FFMPEG and RTS_BUILD_OPTION_MILES_BINK live in config-vcpkg.cmake. With both, LegacyMedia = yes in Options.ini picks Miles and Bink at startup
if(RTS_BUILD_OPTION_OPENAL_FFMPEG AND IS_VS6_BUILD)
    message(STATUS "FFmpeg and OpenAL need a modern compiler; building Miles and Bink only")
    set(RTS_BUILD_OPTION_OPENAL_FFMPEG OFF)
endif()
# Only the Win32 engine creates the OpenAL audio backend, tested with Zero Hour and MSVC 2022+;
# elsewhere the option brings FFmpeg movies only
set(RTS_OPENAL_AUDIO OFF)
if(RTS_BUILD_OPTION_OPENAL_FFMPEG AND WIN32 AND MSVC AND NOT MSVC_VERSION LESS 1930 AND RTS_BUILD_ZEROHOUR)
    set(RTS_OPENAL_AUDIO ON)
elseif(RTS_BUILD_OPTION_OPENAL_FFMPEG)
    message(STATUS "Only the Win32 engine creates OpenAL audio, tested with Zero Hour and Visual Studio 2022 or newer; building FFmpeg movies only")
endif()
if(NOT RTS_OPENAL_AUDIO AND NOT RTS_BUILD_OPTION_MILES_BINK)
    message(FATAL_ERROR "Without OpenAL audio, RTS_BUILD_OPTION_MILES_BINK must stay on")
endif()
if(RTS_OPENAL_AUDIO)
    target_compile_definitions(core_config INTERFACE RTS_HAS_OPENAL_FFMPEG)
    # A static OpenAL Soft package links Threads::Threads without finding it
    find_package(Threads REQUIRED)
endif()

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
add_feature_info(Vc6FullDebug RTS_BUILD_OPTION_VC6_FULL_DEBUG "Building VC6 with full debug info")
add_feature_info(OpenALFFmpeg RTS_BUILD_OPTION_OPENAL_FFMPEG "Building FFmpeg movies, and OpenAL audio on Windows")
add_feature_info(MilesBink RTS_BUILD_OPTION_MILES_BINK "Building Miles audio and Bink movies")
add_feature_info(Tests RTS_BUILD_OPTION_TESTS "Building the unit tests")
add_feature_info(Benchmarks RTS_BUILD_OPTION_BENCHMARKS "Building the benchmarks")

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
    target_compile_features(deps_config INTERFACE cxx_std_20)
endif()

if(IS_VS6_BUILD AND RTS_BUILD_OPTION_VC6_FULL_DEBUG)
    target_compile_options(deps_config INTERFACE ${RTS_FLAGS} /Zi)
else()
    target_compile_options(deps_config INTERFACE ${RTS_FLAGS})
endif()

if(UNIX)
    target_compile_definitions(deps_config INTERFACE _UNIX)
endif()

# This disables a lot of warnings steering developers to use windows only functions/function names.
if(MSVC)
    target_compile_definitions(core_config INTERFACE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS $<$<CONFIG:DEBUG>:_DEBUG_CRT>)
endif()

if(RTS_BUILD_OPTION_DEBUG)
    target_compile_definitions(core_config INTERFACE RTS_DEBUG WWDEBUG DEBUG)
else()
    target_compile_definitions(core_config INTERFACE RTS_RELEASE NDEBUG)
endif()

if(RTS_BUILD_OPTION_PROFILE)
    target_compile_definitions(core_config INTERFACE RTS_PROFILE_LEGACY)
endif()

set(RTS_NEEDS_MILES_BINK TRUE)
if(RTS_OPENAL_AUDIO AND NOT RTS_BUILD_OPTION_MILES_BINK AND NOT RTS_BUILD_GENERALS AND
   NOT RTS_BUILD_ZEROHOUR_TOOLS AND NOT RTS_BUILD_CORE_EXTRAS AND NOT RTS_BUILD_OPTION_TESTS)
    set(RTS_NEEDS_MILES_BINK FALSE)
endif()
