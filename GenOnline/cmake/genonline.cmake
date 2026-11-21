# Generals Online settings of the root CMakeLists.txt, included before the Core and game directories.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
include(${CMAKE_CURRENT_LIST_DIR}/third-party-notices.cmake)

set(RTS_NEEDS_LEGACY_MEDIA TRUE)
if(RTS_BUILD_OPTION_MODERN_MEDIA AND NOT RTS_BUILD_OPTION_LEGACY_MEDIA AND NOT RTS_BUILD_GENERALS_TOOLS AND
   NOT RTS_BUILD_ZEROHOUR_TOOLS AND NOT RTS_BUILD_CORE_EXTRAS)
    set(RTS_NEEDS_LEGACY_MEDIA FALSE)
endif()

# Generals Online code in Core and its libraries checks this, so every target needs it.
add_compile_definitions(GENONLINE)
