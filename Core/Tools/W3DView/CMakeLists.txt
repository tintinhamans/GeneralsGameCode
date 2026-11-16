set(W3DVIEW_SRC
    "AddToLineupDialog.cpp"
    "AddToLineupDialog.h"
    "AdvancedAnimSheet.cpp"
    "AdvancedAnimSheet.h"
    "AggregateNameDialog.cpp"
    "AggregateNameDialog.h"
    "AmbientLightDialog.cpp"
    "AmbientLightDialog.h"
    "AnimatedSoundOptionsDialog.cpp"
    "AnimatedSoundOptionsDialog.h"
    "AnimationPropPage.cpp"
    "AnimationPropPage.h"
    "AnimationSpeed.cpp"
    "AnimationSpeed.h"
    "AnimMixingPage.cpp"
    "AnimMixingPage.h"
    "AnimReportPage.cpp"
    "AnimReportPage.h"
    "AssetInfo.cpp"
    "AssetInfo.h"
    "AssetPropertySheet.cpp"
    "AssetPropertySheet.h"
    "AssetTypes.h"
    "BackgroundBMPDialog.cpp"
    "BackgroundBMPDialog.h"
    "BackgroundColorDialog.cpp"
    "BackgroundColorDialog.h"
    "BackgroundObjectDialog.cpp"
    "BackgroundObjectDialog.h"
    "BoneMgrDialog.cpp"
    "BoneMgrDialog.h"
    "CameraDistanceDialog.cpp"
    "CameraDistanceDialog.h"
    "CameraSettingsDialog.cpp"
    "CameraSettingsDialog.h"
    "ColorBar.cpp"
    "ColorBar.h"
    "ColorPicker.cpp"
    "ColorPicker.h"
    "ColorPickerDialogClass.cpp"
    "ColorPickerDialogClass.h"
    "ColorSelectionDialog.cpp"
    "ColorSelectionDialog.h"
    "ColorUtils.cpp"
    "ColorUtils.h"
    "DataTreeView.cpp"
    "DataTreeView.h"
    "DeviceSelectionDialog.cpp"
    "DeviceSelectionDialog.h"
    "DialogToolbar.cpp"
    "DialogToolbar.h"
    "DirectoryDialog.cpp"
    "DirectoryDialog.h"
    "EditLODDialog.cpp"
    "EditLODDialog.h"
    "EmitterColorPropPage.cpp"
    "EmitterColorPropPage.h"
    "EmitterFramePropPage.cpp"
    "EmitterFramePropPage.h"
    "FrameScrubberDlg.cpp"
    "FrameScrubberDlg.h"
    "EmitterGeneralPropPage.cpp"
    "EmitterGeneralPropPage.h"
    "EmitterInstanceList.cpp"
    "EmitterInstanceList.h"
    "EmitterLineGroupPropPage.cpp"
    "EmitterLineGroupPropPage.h"
    "EmitterLinePropPage.cpp"
    "EmitterLinePropPage.h"
    "EmitterParticlePropPage.cpp"
    "EmitterParticlePropPage.h"
    "EmitterPhysicsPropPage.cpp"
    "EmitterPhysicsPropPage.h"
    "EmitterPropertySheet.cpp"
    "EmitterPropertySheet.h"
    "EmitterRotationPropPage.cpp"
    "EmitterRotationPropPage.h"
    "EmitterSizePropPage.cpp"
    "EmitterSizePropPage.h"
    "EmitterUserPropPage.cpp"
    "EmitterUserPropPage.h"
    "GammaDialog.cpp"
    "GammaDialog.h"
    "LogDialog.cpp"
    "LogDialog.h"
    "Globals.cpp"
    "Globals.h"
    "GraphicView.cpp"
    "GraphicView.h"
    # TheSuperHackers @feature Ground menu height dialog.
    "GroundHeightDialog.cpp"
    "GroundHeightDialog.h"
    "HierarchyPropPage.cpp"
    "HierarchyPropPage.h"
    # TheSuperHackers @feature W3DView job system (background worker pool).
    "JobSystem.cpp"
    "JobSystem.h"
    "LODDefs.h"
    "MainFrm.cpp"
    "MainFrm.h"
    # TheSuperHackers @feature W3DView material viewer (WW3D preview + Qt material panel).
    "MaterialViewer/MaterialPreviewPane.cpp"
    "MaterialViewer/MaterialPreviewPane.h"
    "MaterialViewer/MaterialViewerFrame.cpp"
    "MaterialViewer/MaterialViewerFrame.h"
    "MaterialViewer/W3dChunkTree.cpp"
    "MaterialViewer/W3dChunkTree.h"
    "MaterialViewer/W3dMaterialData.h"
    "MaterialViewer/W3dMaterialParser.cpp"
    "MaterialViewer/W3dMaterialParser.h"
    "MaterialViewer/W3dMaterialWriter.cpp"
    "MaterialViewer/W3dMaterialWriter.h"
    "MeshPropPage.cpp"
    "MeshPropPage.h"
    "OpacitySettingsDialog.cpp"
    "OpacitySettingsDialog.h"
    "OpacityVectorDialog.cpp"
    "OpacityVectorDialog.h"
    "ParticleBlurTimeKeyDialog.cpp"
    "ParticleBlurTimeKeyDialog.h"
    "ParticleFrameKeyDialog.cpp"
    "ParticleFrameKeyDialog.h"
    "ParticleRotationKeyDialog.cpp"
    "ParticleRotationKeyDialog.h"
    "ParticleSizeDialog.cpp"
    "ParticleSizeDialog.h"
    "PlaySoundDialog.cpp"
    "PlaySoundDialog.h"
    "ResolutionDialog.cpp"
    "ResolutionDialog.h"
    "resource.h"
    "RestrictedFileDialog.cpp"
    "RestrictedFileDialog.h"
    "RingColorPropPage.cpp"
    "RingColorPropPage.h"
    "RingGeneralPropPage.cpp"
    "RingGeneralPropPage.h"
    "RingPropertySheet.cpp"
    "RingPropertySheet.h"
    "RingSizePropPage.cpp"
    "RingSizePropPage.h"
    "SaveSettingsDialog.cpp"
    "SaveSettingsDialog.h"
    "ScaleDialog.cpp"
    "ScaleDialog.h"
    "SceneLightDialog.cpp"
    "SceneLightDialog.h"
    "ScreenCursor.cpp"
    "ScreenCursor.h"
    "SoundEditDialog.cpp"
    "SoundEditDialog.h"
    "SphereColorPropPage.cpp"
    "SphereColorPropPage.h"
    "SphereGeneralPropPage.cpp"
    "SphereGeneralPropPage.h"
    "SpherePropertySheet.cpp"
    "SpherePropertySheet.h"
    "SphereSizePropPage.cpp"
    "SphereSizePropPage.h"
    #"SphereUtils.cpp" # Unused and does not compile
    #"SphereUtils.h" # Unused and does not compile
    "StdAfx.cpp"
    "StdAfx.h"
    #"TextureMgrDialog.cpp" # Unused
    #"TextureMgrDialog.h" # Unused
    "TexturePathDialog.cpp"
    "TexturePathDialog.h"
    #"TextureSettingsDialog.cpp" # Unused
    #"TextureSettingsDialog.h" # Unused
    "Toolbar.cpp"
    "Toolbar.h"
    "Utils.cpp"
    "Utils.h"
    "Vector3RndCombo.cpp"
    "Vector3RndCombo.h"
    "ViewerAssetMgr.cpp"
    "ViewerAssetMgr.h"
    "ViewerScene.cpp"
    "ViewerScene.h"
    "ViewportOverlay.cpp"
    "ViewportOverlay.h"
    "VolumeRandomDialog.cpp"
    "VolumeRandomDialog.h"
    "W3DView.cpp"
    "W3DView.h"
    "W3DViewDoc.cpp"
    "W3DViewDoc.h"
    "W3DViewMcpBridge.cpp"
    "W3DViewMcpBridge.h"
    "W3DViewView.cpp"
    "W3DViewView.h"
    # TheSuperHackers @feature W3DView native dark mode (Windows 10+).
    "W3DDarkMode.cpp"
    "W3DDarkMode.h"
    "DarkMode/DarkMode.cpp"
    "DarkMode/DarkMode.h"
    "DarkMode/IatHook.h"
    "DarkMode/UAHMenuBar.h"
)

add_library(corei_w3dview INTERFACE)

target_sources(corei_w3dview INTERFACE ${W3DVIEW_SRC})

target_precompile_headers(corei_w3dview INTERFACE
    [["Precompiled/precompiled.h"]] # Must be first
    "StdAfx.h"
    [["WWLib/always.h"]]
    [["Utility/STLUtils.h"]]
    [["WWLib/WWCommon.h"]]
)

if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    target_sources(corei_w3dview INTERFACE W3DView.rc)
endif()

# TheSuperHackers @feature W3DView material viewer Qt panel.
# The Qt-based material panel is optional: it needs a 32-bit Qt5 (the tool builds
# Win32). When Qt is not found, W3DView builds without the panel (the material
# viewer window shows a placeholder instead).
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_package(Qt5 QUIET COMPONENTS Widgets HINTS "C:/Qt/5.15.2/msvc2019")
endif()

if(Qt5_FOUND)
    # Qt translation units are isolated in a static lib: no tool PCH (MFC's
    # DEBUG_NEW and WW3D macros clash with Qt headers), no MFC/WW3D includes.
    add_library(core_w3dview_qtpanel STATIC
        MaterialViewer/MaterialPanel.cpp
        MaterialViewer/MaterialPanel.h
        MaterialViewer/W3dMaterialData.h
    )
    target_link_libraries(core_w3dview_qtpanel PUBLIC Qt5::Widgets)
    target_compile_definitions(core_w3dview_qtpanel PUBLIC W3DVIEW_HAS_QT)
    target_link_libraries(corei_w3dview INTERFACE core_w3dview_qtpanel)
endif()
