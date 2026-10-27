cmake_minimum_required(VERSION 3.25)
project(openal_loader_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OPENAL_SOFT_SOURCE_DIR "" CACHE PATH "Pinned OpenAL Soft 1.25.2 source")
if(NOT EXISTS "${OPENAL_SOFT_SOURCE_DIR}/common/altypes.hpp")
    message(FATAL_ERROR "Set OPENAL_SOFT_SOURCE_DIR to the pinned source tree")
endif()
add_executable(openal_float_loader_tests test.cpp)
target_include_directories(openal_float_loader_tests PRIVATE
    "${OPENAL_SOFT_SOURCE_DIR}/common" "${OPENAL_SOFT_SOURCE_DIR}/gsl/include")
enable_testing()
add_test(NAME openal_float_loader_bits COMMAND openal_float_loader_tests)
if(WIN32)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
    add_test(NAME openal_loader_patch COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/test_patch.py"
        --source "${OPENAL_SOFT_SOURCE_DIR}" --powershell "${POWERSHELL_EXECUTABLE}"
        --work-directory "${CMAKE_CURRENT_BINARY_DIR}/patch-tests")
    set_tests_properties(openal_loader_patch PROPERTIES TIMEOUT 60)
endif()
