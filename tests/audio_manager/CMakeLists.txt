find_package(OpenAL CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(audio_manager_tests test.cpp host.cpp)
target_link_libraries(audio_manager_tests PRIVATE
    comctl32 core_debug core_profile_legacy d3d8 d3dx8 dinput8 dxguid
    imm32 vfw32 winmm z_gameengine z_gameenginedevice zi_always OpenAL::OpenAL)
target_include_directories(audio_manager_tests PRIVATE "${PROJECT_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled")
if(MSVC)
    target_link_options(audio_manager_tests PRIVATE /NODEFAULTLIB:libci.lib)
endif()
add_custom_command(TARGET audio_manager_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL::OpenAL>" "$<TARGET_FILE_DIR:audio_manager_tests>")
if(WIN32 AND FFMPEG_ROOT)
    file(GLOB _ffmpeg_dlls "${FFMPEG_ROOT}/bin/*.dll")
    foreach(_dll IN LISTS _ffmpeg_dlls)
        add_custom_command(TARGET audio_manager_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:audio_manager_tests>")
    endforeach()
endif()
add_test(NAME audio_manager_fixtures COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_LIST_DIR}/make_fixtures.py" "${CMAKE_CURRENT_BINARY_DIR}/fixture")
set_tests_properties(audio_manager_fixtures PROPERTIES FIXTURES_SETUP audio_manager_files TIMEOUT 10)
add_test(NAME audio_manager_engine COMMAND audio_manager_tests "${CMAKE_CURRENT_BINARY_DIR}/fixture")
set_tests_properties(audio_manager_engine PROPERTIES TIMEOUT 30 FIXTURES_REQUIRED audio_manager_files
    ENVIRONMENT "ALSOFT_DRIVERS=null;ALSOFT_CONF=${PROJECT_SOURCE_DIR}/tests/openal/alsoft.conf")
