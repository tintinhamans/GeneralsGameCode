# Standalone checks of the client.json file rules. Configure this folder on its own, it does not need the game:
#   cmake -S Tests/GenOnlineClientFile -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.25)
project(genonline_client_file_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GAMEENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/GameEngine)

add_executable(genonline_client_file_test
    ClientFileTest.cpp
    ${GAMEENGINE_DIR}/Source/GameNetwork/GenOnline/GenOnlineClientFile.cpp
)
target_include_directories(genonline_client_file_test PRIVATE ${GAMEENGINE_DIR}/Include)
target_link_libraries(genonline_client_file_test PRIVATE shell32 ole32)

enable_testing()
add_test(NAME genonline_client_file_test COMMAND genonline_client_file_test)
