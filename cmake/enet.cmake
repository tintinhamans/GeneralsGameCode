include(FetchContent)
FetchContent_Declare(
	enet
	GIT_REPOSITORY https://github.com/lsalzman/enet.git
	GIT_TAG        v1.3.18
)

FetchContent_GetProperties(enet)
if(NOT enet_POPULATED)
	FetchContent_Populate(enet)
endif()

# VC6 is C89: hoist the C99 for-loop declarations in protocol.c (idempotent).
file(READ ${enet_SOURCE_DIR}/protocol.c _enet_protocol)
string(REPLACE "ENetList sentUnreliableCommands;" "ENetList sentUnreliableCommands; int sendPass, continueSending; ENetPeer * currentPeer;" _enet_protocol_new "${_enet_protocol}")
if(NOT _enet_protocol_new STREQUAL _enet_protocol AND NOT _enet_protocol MATCHES "int sendPass, continueSending;")
	string(REPLACE "for (int sendPass = 0, continueSending = 0;" "for (sendPass = 0, continueSending = 0;" _enet_protocol_new "${_enet_protocol_new}")
	string(REPLACE "for (ENetPeer * currentPeer = host -> peers;" "for (currentPeer = host -> peers;" _enet_protocol_new "${_enet_protocol_new}")
	file(WRITE ${enet_SOURCE_DIR}/protocol.c "${_enet_protocol_new}")
endif()
unset(_enet_protocol)
unset(_enet_protocol_new)

add_library(enet STATIC
	${enet_SOURCE_DIR}/callbacks.c
	${enet_SOURCE_DIR}/compress.c
	${enet_SOURCE_DIR}/host.c
	${enet_SOURCE_DIR}/list.c
	${enet_SOURCE_DIR}/packet.c
	${enet_SOURCE_DIR}/peer.c
	${enet_SOURCE_DIR}/protocol.c
	${enet_SOURCE_DIR}/win32.c
)
target_include_directories(enet PUBLIC ${enet_SOURCE_DIR}/include)
target_link_libraries(enet PUBLIC ws2_32 winmm)
if(MSVC)
	# VC6 warns C4761 on every size_t/int argument.
	target_compile_options(enet PRIVATE /w)
endif()

# Separate from corei_gameengine: winsock2.h (ENet) must not meet the winsock.h of PreRTS.h.
add_library(enetlink STATIC
	${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/GameNetwork/EnetLink.cpp
	${CMAKE_SOURCE_DIR}/Core/GameEngine/Include/GameNetwork/EnetLink.h
)
target_include_directories(enetlink PUBLIC ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include)
target_link_libraries(enetlink PUBLIC enet nanopb)
