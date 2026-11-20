# Interface libraries to set common defines and includes, avoid duplication later
add_library(core_wwcommon INTERFACE)

target_link_libraries(core_wwcommon INTERFACE
    # Ronin @build 09/10/2026 DX9: the whole DirectX set here, as upstream's d3d8lib carries it; targets inherit it (#3432).
    d3d9
    d3dx9
    dinput8
    dxguid
    deps_dbghelp
    deps_usp10
    stlport
)

target_include_directories(core_wwcommon INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(WW3D2)
if(RTS_NEEDS_LEGACY_MEDIA)
    target_link_libraries(core_wwcommon INTERFACE deps_miles)
add_subdirectory(WWAudio)
endif()
add_subdirectory(WWDebug)
add_subdirectory(WWDownload)
add_subdirectory(WWLib)
add_subdirectory(WWMath)
add_subdirectory(WWStub)

# Helpful interface to bundle the ww modules together.
add_library(core_wwvegas INTERFACE)

target_include_directories(core_wwvegas INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(core_wwvegas INTERFACE
    # core_ww3d2
    # core_wwaudio
    core_wwdebug
    # core_wwdownload
    core_wwlib
    core_wwmath
)

target_include_directories(corei_ww3d2 INTERFACE
    ../../../GameEngine/Include
    ../../../GameEngineDevice/Include
)
