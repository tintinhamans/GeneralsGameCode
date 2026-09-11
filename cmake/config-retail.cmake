# Retail game compatibility options
# Sets the RETAIL_COMPATIBLE_* guards from GameDefines.h

set(RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME "DEFAULT" CACHE STRING "Build for retail game compatibility. OFF enables retail-incompatible fixes. DEFAULT follows GameDefines.h.")
set_property(CACHE RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME PROPERTY STRINGS DEFAULT ON OFF)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Core/GameEngine/Include/Common/GameDefines.h" _retail_guards
    REGEX "^#[ \t]*define[ \t]+RETAIL_COMPATIBLE_[A-Z0-9_]+")
set(RTS_RETAIL_COMPATIBLE_GUARDS "")
foreach(_guard IN LISTS _retail_guards)
    string(REGEX MATCH "RETAIL_COMPATIBLE_[A-Z0-9_]+" _guard_name "${_guard}")
    list(APPEND RTS_RETAIL_COMPATIBLE_GUARDS "${_guard_name}")
endforeach()
list(REMOVE_DUPLICATES RTS_RETAIL_COMPATIBLE_GUARDS)

if(NOT RTS_RETAIL_COMPATIBLE_GUARDS)
    message(FATAL_ERROR "No RETAIL_COMPATIBLE_* guards found in GameDefines.h.")
endif()

if(RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME STREQUAL "DEFAULT")
    add_feature_info(RetailCompatibleGame TRUE "Building with retail compatibility from GameDefines.h")
elseif(RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME STREQUAL "ON")
    set(_retail_compatible_value 1)
    add_feature_info(RetailCompatibleGame TRUE "Building with retail compatibility forced on")
elseif(RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME STREQUAL "OFF")
    set(_retail_compatible_value 0)
    add_feature_info(RetailCompatibleGame FALSE "Building with retail compatibility forced off")
else()
    message(FATAL_ERROR "Unhandled RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME value: ${RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME}")
endif()

if(DEFINED _retail_compatible_value)
    foreach(_guard IN LISTS RTS_RETAIL_COMPATIBLE_GUARDS)
        target_compile_definitions(core_config INTERFACE ${_guard}=${_retail_compatible_value})
    endforeach()
endif()

if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "12.0.8804" AND NOT RTS_BUILD_OPTION_RETAIL_COMPATIBLE_GAME STREQUAL "OFF")
    message(NOTICE "")
    message(NOTICE "  Retail compatibility: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is not CRC-compatible with retail.")
    message(NOTICE "  Retail builds need the VC6 SP6 compiler (12.00.8804).")
    message(NOTICE "")
endif()
