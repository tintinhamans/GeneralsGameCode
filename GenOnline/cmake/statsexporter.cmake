# Wire schema of the live stats pipe, shared by both games. The sources live in Core and are listed in core-gameengine.cmake.

# The code is generated at build time with the same protoc and libprotobuf that GameNetworkingSockets uses, in its own

# library per game so it stays out of the engine PCH. Call it from the directory of the game engine target.

macro(genonline_statsexporter_proto proto_target gameengine_target)

    if(NOT IS_VS6_BUILD)

        find_package(Protobuf CONFIG REQUIRED)



        add_library(${proto_target} STATIC)

        protobuf_generate(

            TARGET ${proto_target}

            LANGUAGE cpp

            PROTOS ${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/Common/StatsExporter.proto

            IMPORT_DIRS ${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/Common

            PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/StatsExporterProto

        )

        target_include_directories(${proto_target} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/StatsExporterProto)

        target_link_libraries(${proto_target} PUBLIC protobuf::libprotobuf)



        target_link_libraries(${gameengine_target} PUBLIC ${proto_target})

    endif()

endmacro()
