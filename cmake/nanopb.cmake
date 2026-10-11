# nanopb C runtime. The generated netproto.pb.[ch] are committed; the tag must match their PB_PROTO_HEADER_VERSION.
include(FetchContent)
FetchContent_Declare(
	nanopb
	GIT_REPOSITORY https://github.com/nanopb/nanopb.git
	GIT_TAG        0.4.9.1
)

FetchContent_GetProperties(nanopb)
if(NOT nanopb_POPULATED)
	FetchContent_Populate(nanopb)
endif()

add_library(nanopb STATIC
	${nanopb_SOURCE_DIR}/pb_common.c
	${nanopb_SOURCE_DIR}/pb_encode.c
	${nanopb_SOURCE_DIR}/pb_decode.c
)
target_include_directories(nanopb PUBLIC ${nanopb_SOURCE_DIR})
target_include_directories(nanopb PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include)
target_compile_definitions(nanopb PUBLIC PB_NO_PACKED_STRUCTS=1)

# VC6 has no <stdint.h>/<stdbool.h>; PB_SYSTEM_HEADER supplies them through stdint_adapter.h.
if(IS_VS6_BUILD)
	# PUBLIC: every consumer of pb.h must see the same defines.
	target_include_directories(nanopb PUBLIC
		${CMAKE_SOURCE_DIR}/Dependencies/Utility
	)
	target_compile_definitions(nanopb PUBLIC
		PB_SYSTEM_HEADER="GameNetwork/GenCaster/nanopb_pb_syshdr.h"
		# VC6 has no static assert; int64_t is the 8 byte __int64.
		PB_NO_STATIC_ASSERT=1
	)
endif()
