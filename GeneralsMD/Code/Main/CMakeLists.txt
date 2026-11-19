add_executable(z_generals WIN32)

# Use a binary name that doesn't conflict with original game.
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
else()
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

target_link_libraries(z_generals PRIVATE
    comctl32
    imm32
    vfw32
    winmm
    z_gameenginedevice
    zi_always
)

# TODO Originally referred to build host and user, replace with git info perhaps?
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
"
)

# Based on original binary values for these variables.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 4
#define VERSION_BUILDNUM 601
"
)

if(MSVC)
    target_link_options(z_generals PRIVATE
    "/NODEFAULTLIB:libci.lib"
    "/LARGEADDRESSAWARE"
    )
endif()

target_include_directories(z_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_sources(z_generals PRIVATE
    WinMain.cpp
    WinMain.h
        ${CMAKE_SOURCE_DIR}/Core/GameEngine/Source/Common/GpuPreferenceExports.cpp
)

# RC files optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(z_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(z_generals PRIVATE
        RTS.RC
    )
endif()

if(MSVC)
    # Ronin @build 02/11/2025 AUTOMATIC deployment to Zero Hour folder
    # This copies exe + pdb to Zero Hour installation after every build
    set(ZEROHOUR_INSTALL_PATH "C:/Program Files (x86)/Steam/steamapps/common/Command & Conquer Generals - Zero Hour")
    
    # Ronin @build 18/02/2026 Path to the compiled instancing/distortion vertex/pixel shaders
    set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shaders")

    add_custom_command(TARGET z_generals POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Deploying to Zero Hour installation..."
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:z_generals>
   "${ZEROHOUR_INSTALL_PATH}/generalszh.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
   $<TARGET_FILE_DIR:z_generals>/generalszh.pdb
        "${ZEROHOUR_INSTALL_PATH}/generalszh.pdb"
        # Ronin @build 18/02/2026 Deploy instancing vertex shaders to shaders subfolder
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${ZEROHOUR_INSTALL_PATH}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/RigidInstance.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/RigidInstance.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/RigidInstance_NoColor.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/RigidInstance_NoColor.vso"
        # Ronin @build 18/02/2026 Deploy instancing pixel shader to shaders subfolder
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/RigidInstance.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/RigidInstance.pso"
        # Ronin @build 03/03/2026 Deploy distortion pixel shader to shaders subfolder
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/distortion.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/distortion.pso"
        # Ronin @build 03/03/2026 Deploy distortion vertex shader to shaders subfolder
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/distortion_vs.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/distortion_vs.vso"
        # Ronin @build 14/04/2026 Deploy terrain per-material pixel shader to shaders subfolder.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/terrainpermaterial.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/terrainpermaterial.pso"
        # Ronin @build 14/04/2026 Deploy terrain per-material vertex shader to shaders subfolder.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/terrainpermaterial_vs.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/terrainpermaterial_vs.vso"
        # Ronin @build 05/07/2026 Deploy reflection pixel shader to shaders subfolder.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/ReflectiveRigid.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/ReflectiveRigid.pso"
        # Ronin @build 13/11/2026 Deploy the faded reflective panel's pixel shader (ReflectiveRigid_ps.hlsl, REFLECTIVE_BODY) to shaders subfolder.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/ReflectiveRigidBody.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/ReflectiveRigidBody.pso"
        # Ronin @build 07/11/2026 Deploy the additive parts' pixel shader to shaders subfolder.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/AdditivePart.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/AdditivePart.pso"
        # Ronin @build 14/08/2026 Deploy terrain shadows vertex shader to shaders subfolder.````
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TerrainShadow.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TerrainShadow.vso"
        # Ronin @build 10/10/2026 Deploy terrain shadows pixel shader too.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TerrainShadow.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TerrainShadow.pso"
        # Ronin @build 25/08/2026 §29i.4 Deploy tree shaders; override the vs_1_1 pair in ShadersZH.big.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/Trees.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/Trees.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/Trees.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/Trees.pso"
        # Ronin @build 13/09/2026 SSAO — screen quad, scene depth debug view, raw AO.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/ScreenQuad.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/ScreenQuad.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/SceneDepthDebug.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/SceneDepthDebug.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/SsaoRaw.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/SsaoRaw.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/SsaoBlur.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/SsaoBlur.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/SsaoShow.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/SsaoShow.pso"
                # Ronin @build 20/09/2026 TAA — the resolve pass.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaResolve.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaResolve.pso"
        # Ronin @build 18/11/2026 the glow round a lamp - its art drawn again, and its blur (R3DTaa).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/GlowArt.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/GlowArt.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/GlowBlur.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/GlowBlur.pso"
        # Ronin @build 23/09/2026 TAA — the depth history. This is the history-validation signal; a stale one
        # silently disables it.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaDepthStore.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaDepthStore.pso"
        # Ronin @build 24/09/2026 TAA — motion vectors drawn from the meshes (§14).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaVelMesh.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaVelMesh.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaVelMesh.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaVelMesh.pso"
        # Ronin @build 27/10/2026 TAA - the moving decals' marks (ability rings on the cursor).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaDecalMark.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaDecalMark.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaDecalMark.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaDecalMark.pso"
        # Ronin @build 09/11/2026 Tread relief - the track grooves (R3DTreadMarks).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TreadRelief.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TreadRelief.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TreadRelief.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TreadRelief.pso"
        # Ronin @build 09/11/2026 Waypoint ribbons - the waypoint and rally lines (R3DWaypointLines).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaypointRibbon.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaypointRibbon.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaypointRibbon.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaypointRibbon.pso"
        # Ronin @build 26/09/2026 TAA — skinned-mesh velocity (infantry), §14 stage 2.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaVelSkin.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaVelSkin.vso"
        # Ronin @build 27/09/2026 TAA — the moving-shadow mask (AntiAliasing_Work.md §18).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaMoverLight.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaMoverLight.pso"
        # Ronin @build 03/10/2026 TAA - the water's own reactive mask, copied into the velocity target (Water_Work.md §6).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/TaaWaterMask.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/TaaWaterMask.pso"
        # Ronin @build 28/09/2026 Water — the WaterType 2 sea, SM3 ports of wave.nvv/.nvp (Water_Work.md §6).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterSea.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterSea.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterSea.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterSea.pso"
        # Ronin @build 02/10/2026 Water - the WaterSea variants (WaterSea_ps.hlsl's header): debug views, rivers, rivers + views
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterSeaViews.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterSeaViews.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterSeaRiver.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterSeaRiver.pso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterSeaRiverViews.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterSeaRiverViews.pso"
        # Ronin @build 03/10/2026 Water - wakes: the pair that draws a unit's trail into the wake texture (Water_Work.md §6)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterWake.vso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterWake.vso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/WaterWake.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/WaterWake.pso"
        # Ronin @build 09/10/2026 Local lights - the screen-space light pass (R3DLocalLights).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_SOURCE_DIR}/LocalLight.pso"
            "${ZEROHOUR_INSTALL_PATH}/shaders/LocalLight.pso"
        # Ronin @build 28/09/2026 Water — the WaterSea normal map (scripts/water/GenWaterNormal.ps1). Textures load from
        # Art/Textures, which the install does not have yet.
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/WaterNormal.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/WaterNormal.tga"
        # Ronin @build 02/10/2026 Water - the lake's normal map (GenWaterNormal.ps1, its lake recipe)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/WaterNormalLake.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/WaterNormalLake.tga"
        # Ronin @build 02/10/2026 Water - the river's normal map (GenWaterNormal.ps1, its river recipe)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/WaterNormalRiver.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/WaterNormalRiver.tga"
        # Ronin @build 09/11/2026 Tread relief - the four track textures' relief maps (scripts/treads/GenTreadMaps.py)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/extnktrack_nrm.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/extnktrack_nrm.tga"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/extiretrack_nrm.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/extiretrack_nrm.tga"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/extiretrack2_nrm.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/extiretrack2_nrm.tga"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Textures/extiretrack03_nrm.tga"
            "${ZEROHOUR_INSTALL_PATH}/Art/Textures/extiretrack03_nrm.tga"
        # Ronin @build 28/10/2026 The RmlUi layer's documents, style sheets and fonts: Assets/UI to the game's Data/UI.
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/Assets/UI"
            "${ZEROHOUR_INSTALL_PATH}/Data/UI"
        COMMENT "Auto-deploying to Zero Hour installation folder"



    )
    
    # Ronin @build 02/11/2025 Set debugging working directory for Visual Studio
    # This makes F5 debugging run from the Zero Hour installation folder
    set_target_properties(z_generals PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${ZEROHOUR_INSTALL_PATH}"
 VS_DEBUGGER_COMMAND_ARGUMENTS "-win -quickstart -xres 1280 -yres 720"
    )
endif()
