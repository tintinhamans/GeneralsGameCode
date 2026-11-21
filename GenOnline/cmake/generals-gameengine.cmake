# Generals Online is built into both games, like Zero Hour
include(${CMAKE_SOURCE_DIR}/GenOnline/cmake/statsexporter.cmake)

macro(genonline_generals_gameengine_targets)
    genonline_statsexporter_proto(g_statsexporter_proto g_gameengine)
endmacro()

cmake_language(DEFER CALL genonline_generals_gameengine_targets)

add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
add_compile_definitions(GENONLINE)
