# Standalone test of the chat completion engine.
# It also covers the chat box command sets and the chat mention matcher. It needs no game build:
#   cmake -S Tests/ChatCompletion -B <build dir> -G Ninja
#   cmake --build <build dir>
#   ctest --test-dir <build dir> --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(ChatCompletionTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(chat_completion_test
    ChatCompletionTest.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameClient/GUI/ChatCompletion.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameClient/GUI/ChatCompletionSets.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameClient/GUI/ChatMentions.cpp
)
target_include_directories(chat_completion_test PRIVATE ${REPO_ROOT}/Core/GameEngine/Include)
if(MSVC)
    target_compile_options(chat_completion_test PRIVATE /W4 /utf-8 /EHsc)
endif()

enable_testing()
add_test(NAME chat_completion COMMAND chat_completion_test)
