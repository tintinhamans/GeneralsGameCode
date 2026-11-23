# Generals Online part of GeneralsMD/Code/GameEngine/CMakeLists.txt, included after its source list.

include(${CMAKE_SOURCE_DIR}/GenOnline/cmake/localapi.cmake)

macro(genonline_zerohour_gameengine_targets)
    genonline_localapi_proto(z_localapi_proto z_gameengine)

    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    add_compile_definitions(GENONLINE)
endmacro()

cmake_language(DEFER CALL genonline_zerohour_gameengine_targets)
