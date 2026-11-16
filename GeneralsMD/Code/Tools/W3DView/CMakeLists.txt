add_executable(z_w3dview WIN32)

target_link_libraries(z_w3dview PRIVATE
    core_wwstub # avoid linking GameEngine
    corei_w3dview # this interface gets the source files for the tool
    d3d8
    d3d8lib
    d3dx8
    imm32
    milesloader
    Version
    vfw32
    winmm
    z_wwaudio
    z_wwvegas
    zi_always
)

if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    target_compile_definitions(z_w3dview PRIVATE _AFXDLL)
    set_target_properties(z_w3dview PROPERTIES
        OUTPUT_NAME "W3DViewZH${RTS_BUILD_OUTPUT_SUFFIX}"
        # TheSuperHackers @build Disable linker auto-manifest; manifest is embedded via RC file instead
        LINK_FLAGS "/MANIFEST:NO"
    )

    # Ninja tracks the resource script, but not the bitmaps and icons it embeds
    file(GLOB_RECURSE W3DVIEW_RES_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Core/Tools/W3DView/Res/*")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/Core/Tools/W3DView/W3DView.rc"
        PROPERTIES OBJECT_DEPENDS "${W3DVIEW_RES_FILES}")
else()
    set_target_properties(z_w3dview PROPERTIES OUTPUT_NAME "w3dviewzh${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

# Copy mss32.dll (the milesstub shared output) next to the exe so W3DView runs
# without "mss32.dll not found". The shared milesstub is required by the
# transitive WW3D2/WWAudio link and ships as an empty-stub mss32.dll.
if(WIN32 AND TARGET milesstub)
    add_custom_command(TARGET z_w3dview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:milesstub>
            $<TARGET_FILE_DIR:z_w3dview>
        COMMENT "Copying mss32.dll next to W3DViewZH.exe"
    )
endif()

# TheSuperHackers @feature W3DView material viewer Qt panel: deploy the Qt
# runtime (core DLLs + windows platform plugin) next to the exe. The imported
# Qt5 targets are directory-scoped, so repeat the (cached) find_package here.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_package(Qt5 QUIET COMPONENTS Widgets HINTS "C:/Qt/5.15.2/msvc2019")
endif()
if(WIN32 AND TARGET Qt5::Widgets)
    add_custom_command(TARGET z_w3dview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt5::Widgets>
            $<TARGET_FILE:Qt5::Gui>
            $<TARGET_FILE:Qt5::Core>
            $<TARGET_FILE_DIR:z_w3dview>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:z_w3dview>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt5::QWindowsIntegrationPlugin>
            $<TARGET_FILE_DIR:z_w3dview>/platforms
        COMMENT "Copying Qt runtime next to W3DViewZH.exe"
    )
endif()
