# Standalone contract test of the GenOnline realtime protocol. It needs no game build:
#   cmake -S Tests/GenOnlineRealtime -B <build dir>
#   cmake --build <build dir>
#   ctest --test-dir <build dir> --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(GenOnlineRealtimeTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(genonline_realtime_contract_test RealtimeContractTest.cpp)
target_include_directories(genonline_realtime_contract_test PRIVATE ${REPO_ROOT}/Core/GameEngine/Include)
target_link_libraries(genonline_realtime_contract_test PRIVATE bcrypt)
if(MSVC)
    target_compile_options(genonline_realtime_contract_test PRIVATE /W4 /utf-8 /EHsc)
endif()

enable_testing()
add_test(NAME genonline_realtime_contract
    COMMAND genonline_realtime_contract_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${CMAKE_CURRENT_SOURCE_DIR}/auth-fixtures)
