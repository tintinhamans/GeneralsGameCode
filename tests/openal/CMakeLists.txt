cmake_minimum_required(VERSION 3.25)
project(openal_pcm_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
find_package(OpenAL CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
add_executable(openal_pcm_tests test.cpp
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALDevice.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALPcmStream.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioProducer.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegFile.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegAudioFile.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegPcmConverter.cpp")
target_include_directories(openal_pcm_tests PRIVATE ../ffmpeg/adapters
    "${REPO_ROOT}/Core/GameEngineDevice/Source"
    "${REPO_ROOT}/Core/GameEngineDevice/Include" ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(openal_pcm_tests PRIVATE OpenAL::OpenAL ${FFMPEG_LIBRARIES})
if(WIN32)
    add_custom_command(TARGET openal_pcm_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:openal_pcm_tests>")
    if(FFMPEG_ROOT)
        file(GLOB _ffmpeg_dlls "${FFMPEG_ROOT}/bin/*.dll")
        foreach(_dll IN LISTS _ffmpeg_dlls)
            add_custom_command(TARGET openal_pcm_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:openal_pcm_tests>")
        endforeach()
    endif()
endif()
enable_testing()
add_test(NAME openal_loopback COMMAND openal_pcm_tests "${REPO_ROOT}/tests/ffmpeg/fixtures/gapless.mp3")
add_test(NAME openal_null_stress COMMAND openal_pcm_tests --stress)
set_tests_properties(openal_loopback openal_null_stress PROPERTIES TIMEOUT 30
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${CMAKE_CURRENT_LIST_DIR}/alsoft.conf")

# Interpose only cleanup calls in a separate build of the production stream.
add_library(openal_cleanup_stream OBJECT
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALPcmStream.cpp")
target_include_directories(openal_cleanup_stream PRIVATE "${REPO_ROOT}/Core/GameEngineDevice/Include")
target_link_libraries(openal_cleanup_stream PRIVATE OpenAL::OpenAL)
if(MSVC)
    target_compile_options(openal_cleanup_stream PRIVATE "/FI${CMAKE_CURRENT_LIST_DIR}/cleanup_calls.h")
else()
    target_compile_options(openal_cleanup_stream PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/cleanup_calls.h")
endif()
add_executable(openal_cleanup_tests cleanup_test.cpp cleanup_calls.cpp
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALDevice.cpp"
    $<TARGET_OBJECTS:openal_cleanup_stream>)
target_include_directories(openal_cleanup_tests PRIVATE "${REPO_ROOT}/Core/GameEngineDevice/Include")
target_link_libraries(openal_cleanup_tests PRIVATE OpenAL::OpenAL)
if(WIN32)
    add_custom_command(TARGET openal_cleanup_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:openal_cleanup_tests>")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
foreach(fault pending detach source buffer stop)
    add_test(NAME openal_cleanup_${fault} COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_LIST_DIR}/check_cleanup.py" $<TARGET_FILE:openal_cleanup_tests> ${fault})
    set_tests_properties(openal_cleanup_${fault} PROPERTIES TIMEOUT 15
        ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${CMAKE_CURRENT_LIST_DIR}/alsoft.conf")
endforeach()

add_test(NAME openal_explicit_output COMMAND openal_pcm_tests --output-modes)
set_tests_properties(openal_explicit_output PROPERTIES TIMEOUT 30
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${CMAKE_CURRENT_LIST_DIR}/alsoft-hrtf.conf")

add_library(openal_gain_stream OBJECT
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALPcmStream.cpp")
target_include_directories(openal_gain_stream PRIVATE "${REPO_ROOT}/Core/GameEngineDevice/Include")
target_link_libraries(openal_gain_stream PRIVATE OpenAL::OpenAL)
if(MSVC)
    target_compile_options(openal_gain_stream PRIVATE "/FI${CMAKE_CURRENT_LIST_DIR}/gain_calls.h")
else()
    target_compile_options(openal_gain_stream PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/gain_calls.h")
endif()
add_executable(openal_gain_tests gain_test.cpp gain_calls.cpp
    "${REPO_ROOT}/Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALDevice.cpp"
    $<TARGET_OBJECTS:openal_gain_stream>)
target_include_directories(openal_gain_tests PRIVATE "${REPO_ROOT}/Core/GameEngineDevice/Include")
target_link_libraries(openal_gain_tests PRIVATE OpenAL::OpenAL)
if(WIN32)
    add_custom_command(TARGET openal_gain_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:openal_gain_tests>")
endif()
add_test(NAME openal_gain_cache COMMAND openal_gain_tests)
set_tests_properties(openal_gain_cache PROPERTIES TIMEOUT 15
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${CMAKE_CURRENT_LIST_DIR}/alsoft.conf")

include("${CMAKE_CURRENT_LIST_DIR}/recovery.cmake")
add_executable(openal_recovery_tests recovery_test.cpp recovery_calls.cpp $<TARGET_OBJECTS:openal_recovery_stream>)
target_include_directories(openal_recovery_tests PRIVATE "${REPO_ROOT}/Core/GameEngineDevice/Include")
target_link_libraries(openal_recovery_tests PRIVATE OpenAL::OpenAL)
if(WIN32)
    add_custom_command(TARGET openal_recovery_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:openal_recovery_tests>")
endif()
add_test(NAME openal_device_recovery COMMAND openal_recovery_tests)
set_tests_properties(openal_device_recovery PROPERTIES TIMEOUT 30
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${CMAKE_CURRENT_LIST_DIR}/alsoft.conf")
