cmake_minimum_required(VERSION 3.25)
project(ffmpeg_foundation_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
find_package(FFMPEG REQUIRED)
add_executable(ffmpeg_foundation_tests test.cpp
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegFile.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegAudioFile.cpp"
    "${REPO_ROOT}/Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegPcmConverter.cpp")
target_include_directories(ffmpeg_foundation_tests PRIVATE adapters
    "${REPO_ROOT}/Core/GameEngineDevice/Include" ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(ffmpeg_foundation_tests PRIVATE ${FFMPEG_LIBRARIES})
enable_testing()
add_test(NAME ffmpeg_foundation_unit COMMAND ffmpeg_foundation_tests)
if(WIN32 AND FFMPEG_ROOT)
    file(GLOB _runtime_dlls "${FFMPEG_ROOT}/bin/*.dll")
    foreach(_dll IN LISTS _runtime_dlls)
        add_custom_command(TARGET ffmpeg_foundation_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:ffmpeg_foundation_tests>")
    endforeach()
endif()

add_test(NAME ffmpeg_foundation_gapless COMMAND ffmpeg_foundation_tests --gapless "${CMAKE_CURRENT_LIST_DIR}/fixtures/gapless.mp3")
