# Standalone test of the per-peer transport layer. It needs no game build and no GameNetworkingSockets:
#   cmake -S Tests/GenOnlinePeerTransport -B <build dir>
#   cmake --build <build dir>
#   ctest --test-dir <build dir> --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(GenOnlinePeerTransportTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(genonline_peer_transport_test
    PeerTransportTest.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameNetwork/PeerTransport/PluginPeerTransport.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameNetwork/PeerTransport/PeerTransportLog.cpp
    ${REPO_ROOT}/Core/GameEngine/Source/GameNetwork/PeerTransport/PeerSignalChannel.cpp
)
target_include_directories(genonline_peer_transport_test PRIVATE ${REPO_ROOT}/Core/GameEngine/Include)
if(MSVC)
    target_compile_options(genonline_peer_transport_test PRIVATE /W4 /utf-8 /EHsc)
endif()

enable_testing()
add_test(NAME genonline_peer_transport COMMAND genonline_peer_transport_test)
