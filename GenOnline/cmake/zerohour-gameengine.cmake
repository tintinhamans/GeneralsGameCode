# Generals Online part of GeneralsMD/Code/GameEngine/CMakeLists.txt, included after its source list.

list(APPEND GAMEENGINE_SRC
    Include/Common/StatsExporter.h
    Include/Common/StatsExporterControl.h
    Include/Common/StatsExporterLive.h
    Include/Common/StatsExporterLobby.h
    Include/Common/StatsExporterPipe.h
    Include/Common/StatsExporterQuery.h
    Include/Common/StatsUploader.h
    Source/Common/StatsExporter.cpp
    Source/Common/StatsExporterControl.cpp
    Source/Common/StatsExporterLive.cpp
    Source/Common/StatsExporterLobby.cpp
    Source/Common/StatsExporterPipe.cpp
    Source/Common/StatsExporterQuery.cpp
    Source/Common/StatsUploader.cpp
)

macro(genonline_zerohour_gameengine_targets)
    # The live stats pipe of Generals Online, Zero Hour only.
    if(NOT IS_VS6_BUILD)
        find_package(Protobuf CONFIG REQUIRED)

        # Wire schema of the live stats pipe. The code is generated at build time with the same protoc and
        # libprotobuf that GameNetworkingSockets uses, in its own library so it stays out of the engine PCH.
        add_library(z_statsexporter_proto STATIC)
        protobuf_generate(
            TARGET z_statsexporter_proto
            LANGUAGE cpp
            PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Common/StatsExporter.proto
            IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Common
            PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/StatsExporterProto
        )
        target_include_directories(z_statsexporter_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/StatsExporterProto)
        target_link_libraries(z_statsexporter_proto PUBLIC protobuf::libprotobuf)

        target_link_libraries(z_gameengine PUBLIC z_statsexporter_proto)
    endif()

    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    add_compile_definitions(GENONLINE)
endmacro()

cmake_language(DEFER CALL genonline_zerohour_gameengine_targets)
