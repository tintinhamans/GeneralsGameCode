add_executable(g_w3dview WIN32)

target_link_libraries(g_w3dview PRIVATE
    core_wwstub # avoid linking GameEngine
    corei_w3dview # this interface gets the source files for the tool
    d3d8
    d3d8lib
    d3dx8
    g_wwaudio
    g_wwvegas
    gi_always
    imm32
    milesloader
    Version
    vfw32
    winmm
)

if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    target_compile_definitions(g_w3dview PRIVATE _AFXDLL)
    set_target_properties(g_w3dview PROPERTIES
        OUTPUT_NAME "W3DViewV${RTS_BUILD_OUTPUT_SUFFIX}"
        # TheSuperHackers @build Disable linker auto-manifest; manifest is embedded via RC file instead
        LINK_FLAGS "/MANIFEST:NO"
    )
else()
    set_target_properties(g_w3dview PROPERTIES OUTPUT_NAME "w3dviewv${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

# TheSuperHackers @feature W3DView material viewer Qt panel: deploy the Qt
# runtime (core DLLs + windows platform plugin) next to the exe. The imported
# Qt5 targets are directory-scoped, so repeat the (cached) find_package here.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_package(Qt5 QUIET COMPONENTS Widgets HINTS "C:/Qt/5.15.2/msvc2019")
endif()
if(WIN32 AND TARGET Qt5::Widgets)
    add_custom_command(TARGET g_w3dview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt5::Widgets>
            $<TARGET_FILE:Qt5::Gui>
            $<TARGET_FILE:Qt5::Core>
            $<TARGET_FILE_DIR:g_w3dview>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:g_w3dview>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt5::QWindowsIntegrationPlugin>
            $<TARGET_FILE_DIR:g_w3dview>/platforms
        COMMENT "Copying Qt runtime next to W3DViewV.exe"
    )
endif()
