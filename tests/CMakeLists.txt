cmake_minimum_required(VERSION 3.15)
project(IniNameRegistryTests LANGUAGES CXX)

add_executable(ini_name_registry_test IniNameRegistryTest.cpp)
target_include_directories(ini_name_registry_test PRIVATE ../GeneralsMD/Code/GameEngine/Include)
target_compile_definitions(ini_name_registry_test PRIVATE _CRT_SECURE_NO_WARNINGS)

enable_testing()
add_test(NAME ini_name_registry COMMAND ini_name_registry_test)
