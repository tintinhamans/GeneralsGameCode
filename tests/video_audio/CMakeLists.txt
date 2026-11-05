find_package(OpenAL CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
add_executable(video_audio_tests test.cpp "${PROJECT_SOURCE_DIR}/tests/audio_manager/host.cpp")
target_link_libraries(video_audio_tests PRIVATE
    comctl32 core_debug core_profile_legacy d3d8 d3dx8 dinput8 dxguid
    imm32 vfw32 winmm z_gameengine z_gameenginedevice zi_always
    OpenAL::OpenAL ${FFMPEG_LIBRARIES})
target_include_directories(video_audio_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled" ${FFMPEG_INCLUDE_DIRS})
if(MSVC)
    target_link_options(video_audio_tests PRIVATE /NODEFAULTLIB:libci.lib)
endif()
add_custom_command(TARGET video_audio_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:video_audio_tests>")
if(WIN32 AND FFMPEG_ROOT)
    file(GLOB _ffmpeg_dlls "${FFMPEG_ROOT}/bin/*.dll")
    foreach(_dll IN LISTS _ffmpeg_dlls)
        add_custom_command(TARGET video_audio_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_dll}" "$<TARGET_FILE_DIR:video_audio_tests>")
    endforeach()
endif()
add_test(NAME video_audio_synthetic COMMAND video_audio_tests --synthetic)
set_tests_properties(video_audio_synthetic PROPERTIES TIMEOUT 20
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${PROJECT_SOURCE_DIR}/tests/openal/alsoft.conf")
set(RTS_VIDEO_AUDIO_TEST_AUDIO_FILE "" CACHE FILEPATH "Local Bink movie with audio for isolated runtime tests")
set(RTS_VIDEO_AUDIO_TEST_SILENT_FILE "" CACHE FILEPATH "Local short silent Bink movie for isolated runtime tests")
if(RTS_VIDEO_AUDIO_TEST_AUDIO_FILE AND RTS_VIDEO_AUDIO_TEST_SILENT_FILE)
    add_test(NAME video_audio_engine COMMAND video_audio_tests --movies
        "${RTS_VIDEO_AUDIO_TEST_AUDIO_FILE}" "${RTS_VIDEO_AUDIO_TEST_SILENT_FILE}")
    set_tests_properties(video_audio_engine PROPERTIES TIMEOUT 60
        ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${PROJECT_SOURCE_DIR}/tests/openal/alsoft.conf")
endif()

include("${PROJECT_SOURCE_DIR}/tests/openal/recovery.cmake")
target_sources(video_audio_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/openal/recovery_calls.cpp" $<TARGET_OBJECTS:openal_recovery_stream>)
