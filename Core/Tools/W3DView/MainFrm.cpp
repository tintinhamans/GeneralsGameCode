/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// MainFrm.cpp : implementation of the CMainFrame class
//

#include "StdAfx.h"
#include <afxdlgs.h>	// CColorDialog
#include "W3DView.h"

#include "MainFrm.h"
#include "DataTreeView.h"
#include "GraphicView.h"
#include "DeviceSelectionDialog.h"
#include "Globals.h"
#include "W3DViewDoc.h"
#include "ViewerAssetMgr.h"
#include "AssetPropertySheet.h"
#include "MeshPropPage.h"
#include "AnimationPropPage.h"
#include "HierarchyPropPage.h"
#include "resource.h"
#include "WW3D2/distlod.h"
#include "AnimationSpeed.h"
#include "AmbientLightDialog.h"
#include "SceneLightDialog.h"
#include "BackgroundColorDialog.h"
#include "SaveSettingsDialog.h"
#include "EditLODDialog.h"
#include "WW3D2/w3derr.h"
#include "BackgroundObjectDialog.h"
#include "BackgroundBMPDialog.h"
#include "Toolbar.h"
#include "EmitterPropertySheet.h"
#include "WW3D2/part_ldr.h"
#include "WW3D2/agg_def.h"
#include "BoneMgrDialog.h"
#include "GroundHeightDialog.h"
#include "Utils.h"
#include "LogDialog.h"
#include "WW3D2/light.h"
#include "AggregateNameDialog.h"
#include "LODDefs.h"
#include "WW3D2/part_emt.h"
#include "RestrictedFileDialog.h"
#include "WW3D2/hlod.h"
#include "ViewerScene.h"
#include "W3DDarkMode.h"
#include "MaterialViewer/MaterialViewerFrame.h"
#include "W3DViewMcpBridge.h"
#ifdef W3DVIEW_HAS_QT
#include "MaterialViewer/MaterialPanel.h"	// W3dMaterialViewer::PickColor (Qt colour dialog)
#endif
#include "JobSystem.h"

#include <string>
#include <shobjidl.h>
#include <shlwapi.h>
#include "EmitterInstanceList.h"
#include "mmsystem.h"
#include "AdvancedAnimSheet.h"
#include "CameraSettingsDialog.h"
#include "DirectoryDialog.h"
#include "TexturePathDialog.h"
#include "ResolutionDialog.h"
#include "SpherePropertySheet.h"
#include "RingPropertySheet.h"
#include "AddToLineupDialog.h"
#include "CameraDistanceDialog.h"
#include "SoundEditDialog.h"
#include "WWAudio/WWAudio.h"
#include "WW3D2/soundrobj.h"
#include "WW3D2/rddesc.h"
#include "ScaleDialog.h"
#include "GammaDialog.h"
#include "AnimatedSoundOptionsDialog.h"


//#undef STRICT
#include "WW3D2/ww3d.h"
#include "WW3D2/shader.h"
#include "WW3D2/texturefilter.h"
#include "WW3D2/dx8wrapper.h"

// TheSuperHackers @feature Tria 17/04/2026 Track anisotropic filter level for menu radio state.
static int s_AnisotropicLevel = 0;


#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////
//
//  External Functions
//
void Set_Highest_LOD (RenderObjClass *render_obj);	// DataTreeView.cpp


/////////////////////////////////////////////////////////////////////////////
//
//  Local Constants
//
const int SPECIAL_MENU_SLOT     = 6;


/////////////////////////////////////////////////////////////////////////////
//
//  Local Inlines
//
__inline void Adjust_Light_Intensity (Vector3 &color, float inc)
{
	color.X = color.X + inc;
	color.Y = color.Y + inc;
	color.Z = color.Z + inc;
	color.X = (color.X < 0) ? 0 : color.X;
	color.Y = (color.Y < 0) ? 0 : color.Y;
	color.Z = (color.Z < 0) ? 0 : color.Z;
	color.X = (color.X > 1.0F) ? 1.0F : color.X;
	color.Y = (color.Y > 1.0F) ? 1.0F : color.Y;
	color.Z = (color.Z > 1.0F) ? 1.0F : color.Z;
}




/////////////////////////////////////////////////////////////////////////////
// CMainFrame

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
	ON_WM_CREATE()
	ON_COMMAND(IDM_OBJECT_PROPERTIES, OnObjectProperties)
	ON_UPDATE_COMMAND_UI(IDM_OBJECT_PROPERTIES, OnUpdateObjectProperties)
	ON_COMMAND(IDM_LOD_GENERATE, OnLodGenerate)
	ON_WM_ACTIVATEAPP()
	ON_COMMAND(ID_FILE_OPEN, OnFileOpen)
	ON_COMMAND(ID_FILE_OPEN_FOLDER, OnFileOpenFolder)
	ON_COMMAND(IDM_ANI_SPEED, OnAniSpeed)
	ON_COMMAND(IDM_ANI_STOP, OnAniStop)
	ON_COMMAND(IDM_ANI_START, OnAniStart)
	ON_COMMAND(IDM_ANI_PAUSE, OnAniPause)
	ON_COMMAND(IDM_CAMERA_BACK, OnCameraBack)
	ON_COMMAND(IDM_CAMERA_BOTTOM, OnCameraBottom)
	ON_COMMAND(IDM_CAMERA_FRONT, OnCameraFront)
	ON_COMMAND(IDM_CAMERA_LEFT, OnCameraLeft)
	ON_COMMAND(IDM_CAMERA_RESET, OnCameraReset)
	ON_COMMAND(IDM_CAMERA_RIGHT, OnCameraRight)
	ON_COMMAND(IDM_CAMERA_TOP, OnCameraTop)
	ON_COMMAND(IDM_OBJECT_ROTATE_Z, OnObjectRotateZ)
	ON_COMMAND(IDM_OBJECT_ROTATE_Y, OnObjectRotateY)
	ON_COMMAND(IDM_OBJECT_ROTATE_X, OnObjectRotateX)
	ON_COMMAND(IDM_LIGHT_AMBIENT, OnLightAmbient)
	ON_COMMAND(IDM_LIGHT_SCENE, OnLightScene)
	ON_COMMAND(IDM_BACKGROUND_COLOR, OnBackgroundColor)
	ON_COMMAND(IDM_BACKGROUND_BMP, OnBackgroundBMP)
	ON_COMMAND(IDM_SAVE_SETTINGS, OnSaveSettings)
	ON_COMMAND(IDM_LOAD_SETTINGS, OnLoadSettings)
	ON_COMMAND(IDM_LOD_SET_SWITCH, OnLODSetSwitch)
	ON_COMMAND(IDM_LOD_SAVE, OnLODSave)
	ON_COMMAND(IDM_LOD_SAVEALL, OnLODSaveAll)
	ON_COMMAND(IDM_BACKGROUND_OBJECT, OnBackgroundObject)
	ON_UPDATE_COMMAND_UI(IDM_VIEW_ANIMATION_BAR, OnUpdateViewAnimationBar)
	ON_UPDATE_COMMAND_UI(IDM_VIEW_OBJECT_BAR, OnUpdateViewObjectBar)
	ON_COMMAND(IDM_VIEW_ANIMATION_BAR, OnViewAnimationBar)
	ON_COMMAND(IDM_VIEW_OBJECT_BAR, OnViewObjectBar)
	ON_COMMAND(IDM_ANI_STEP_FWD, OnAniStepFwd)
	ON_COMMAND(IDM_ANI_STEP_BKWD, OnAniStepBkwd)
	ON_COMMAND(IDM_OBJECT_RESET, OnObjectReset)
	ON_COMMAND(IDM_CAMERA_ALLOW_ROTATE_X, OnCameraAllowRotateX)
	ON_COMMAND(IDM_CAMERA_ALLOW_ROTATE_Y, OnCameraAllowRotateY)
	ON_COMMAND(IDM_CAMERA_ALLOW_ROTATE_Z, OnCameraAllowRotateZ)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_ALLOW_ROTATE_X, OnUpdateCameraAllowRotateX)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_ALLOW_ROTATE_Y, OnUpdateCameraAllowRotateY)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_ALLOW_ROTATE_Z, OnUpdateCameraAllowRotateZ)
	ON_UPDATE_COMMAND_UI(IDM_OBJECT_ROTATE_X, OnUpdateObjectRotateX)
	ON_UPDATE_COMMAND_UI(IDM_OBJECT_ROTATE_Y, OnUpdateObjectRotateY)
	ON_UPDATE_COMMAND_UI(IDM_OBJECT_ROTATE_Z, OnUpdateObjectRotateZ)
	ON_COMMAND(IDM_DEVICE_CHANGE, OnDeviceChange)
	ON_COMMAND(IDM_VIEW_FULLSCREEN, OnViewFullscreen)
	ON_UPDATE_COMMAND_UI(IDM_VIEW_FULLSCREEN, OnUpdateViewFullscreen)
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_GETMINMAXINFO()
	ON_COMMAND(IDM_CREATE_EMITTER, OnCreateEmitter)
	ON_COMMAND(IDM_EDIT_EMITTER, OnEditEmitter)
	ON_UPDATE_COMMAND_UI(IDM_EDIT_EMITTER, OnUpdateEditEmitter)
	ON_COMMAND(IDM_SAVE_EMITTER, OnSaveEmitter)
	ON_UPDATE_COMMAND_UI(IDM_SAVE_EMITTER, OnUpdateSaveEmitter)
	ON_COMMAND(IDM_BONE_AUTO_ASSIGN, OnBoneAutoAssign)
	ON_COMMAND(IDM_BONE_MANAGEMENT, OnBoneManagement)
	ON_COMMAND(IDM_SAVE_AGGREGATE, OnSaveAggregate)
	ON_COMMAND(IDM_CAMERA_ANIMATE, OnCameraAnimate)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_ANIMATE, OnUpdateCameraAnimate)
	ON_UPDATE_COMMAND_UI(IDM_LOD_SAVE, OnUpdateLodSave)
	ON_UPDATE_COMMAND_UI(IDM_SAVE_AGGREGATE, OnUpdateSaveAggregate)
	ON_COMMAND(IDM_CAMERA_RESET_ON_LOAD, OnCameraResetOnLoad)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_RESET_ON_LOAD, OnUpdateCameraResetOnLoad)
	ON_COMMAND(IDM_CAMERA_INVERT_Y, OnCameraInvertY)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_INVERT_Y, OnUpdateCameraInvertY)
	ON_COMMAND(IDM_OBJECT_ROTATE_Y_BACK, OnObjectRotateYBack)
	ON_COMMAND(IDM_OBJECT_ROTATE_Z_BACK, OnObjectRotateZBack)
	ON_COMMAND(IDM_LIGHT_ROTATE_Y, OnLightRotateY)
	ON_COMMAND(IDM_LIGHT_ROTATE_Y_BACK, OnLightRotateYBack)
	ON_COMMAND(IDM_LIGHT_ROTATE_Z, OnLightRotateZ)
	ON_COMMAND(IDM_LIGHT_ROTATE_Z_BACK, OnLightRotateZBack)
	ON_WM_DESTROY()
	ON_COMMAND(IDM_DEC_LIGHT, OnDecLight)
	ON_COMMAND(IDM_INC_LIGHT, OnIncLight)
	ON_COMMAND(IDM_DEC_AMBIENT_LIGHT, OnDecAmbientLight)
	ON_COMMAND(IDM_INC_AMBIENT_LIGHT, OnIncAmbientLight)
	ON_COMMAND(IDM_MAKE_AGGREGATE, OnMakeAggregate)
	ON_COMMAND(IDM_RENAME_AGGREGATE, OnRenameAggregate)
	ON_COMMAND(IDM_CRASH_APP, OnCrashApp)
	ON_COMMAND(IDM_LOD_RECORD_SCREEN_AREA, OnLODRecordScreenArea)
	ON_COMMAND(IDM_LOD_INCLUDE_NULL, OnLODIncludeNull)
	ON_UPDATE_COMMAND_UI(IDM_LOD_INCLUDE_NULL, OnUpdateLODIncludeNull)
	ON_COMMAND(IDM_LOD_PREV_LEVEL, OnLodPrevLevel)
	ON_UPDATE_COMMAND_UI(IDM_LOD_PREV_LEVEL, OnUpdateLodPrevLevel)
	ON_COMMAND(IDM_LOD_NEXT_LEVEL, OnLodNextLevel)
	ON_UPDATE_COMMAND_UI(IDM_LOD_NEXT_LEVEL, OnUpdateLodNextLevel)
	ON_COMMAND(IDM_LOD_AUTOSWITCH, OnLodAutoswitch)
	ON_UPDATE_COMMAND_UI(IDM_LOD_AUTOSWITCH, OnUpdateLodAutoswitch)
	ON_UPDATE_COMMAND_UI(IDM_MAKE_MOVIE, OnUpdateMakeMovie)
	ON_COMMAND(IDM_MAKE_MOVIE, OnMakeMovie)
	ON_COMMAND(IDM_SAVE_SCREENSHOT, OnSaveScreenshot)
	ON_COMMAND(IDM_SLIDESHOW_DOWN, OnSlideshowDown)
	ON_COMMAND(IDM_SLIDESHOW_UP, OnSlideshowUp)
	ON_COMMAND(IDM_ADVANCED_ANIM, OnAdvancedAnim)
	ON_UPDATE_COMMAND_UI(IDM_ADVANCED_ANIM, OnUpdateAdvancedAnim)
	ON_COMMAND(IDM_CAMERA_SETTINGS, OnCameraSettings)
	ON_COMMAND(IDM_COPY_SCREEN_SIZE, OnCopyScreenSize)
	ON_COMMAND(IDC_LIST_MISSING_TEXTURES, OnListMissingTextures)
	ON_COMMAND(IDC_COPY_ASSETS, OnCopyAssets)
	ON_UPDATE_COMMAND_UI(IDC_COPY_ASSETS, OnUpdateCopyAssets)
	ON_COMMAND(IDM_LIGHTING_EXPOSE, OnLightingExpose)
	ON_UPDATE_COMMAND_UI(IDM_LIGHTING_EXPOSE, OnUpdateLightingExpose)
	ON_COMMAND(IDM_TEXTURE_PATH, OnTexturePath)
	ON_COMMAND(IDM_CHANGE_RESOLUTION, OnChangeResolution)
	ON_COMMAND(IDM_CREATE_SPHERE, OnCreateSphere)
	ON_COMMAND(IDM_CREATE_RING, OnCreateRing)
	ON_UPDATE_COMMAND_UI(IDM_EDIT_PRIMITIVE, OnUpdateEditPrimitive)
	ON_COMMAND(IDM_EDIT_PRIMITIVE, OnEditPrimitive)
	ON_COMMAND(IDM_EXPORT_PRIMITIVE, OnExportPrimitive)
	ON_UPDATE_COMMAND_UI(IDM_EXPORT_PRIMITIVE, OnUpdateExportPrimitive)
	ON_COMMAND(IDM_KILL_SCENE_LIGHT, OnKillSceneLight)
	ON_COMMAND(IDM_PRELIT_MULTIPASS, OnPrelitMultipass)
	ON_UPDATE_COMMAND_UI(IDM_PRELIT_MULTIPASS, OnUpdatePrelitMultipass)
	ON_COMMAND(IDM_PRELIT_MULTITEX, OnPrelitMultitex)
	ON_UPDATE_COMMAND_UI(IDM_PRELIT_MULTITEX, OnUpdatePrelitMultitex)
	ON_COMMAND(IDM_PRELIT_VERTEX, OnPrelitVertex)
	ON_UPDATE_COMMAND_UI(IDM_PRELIT_VERTEX, OnUpdatePrelitVertex)
	ON_COMMAND(IDC_ADD_TO_LINEUP, OnAddToLineup)
	ON_UPDATE_COMMAND_UI(IDC_ADD_TO_LINEUP, OnUpdateAddToLineup)
	ON_COMMAND(IDM_IMPORT_FACIAL_ANIMS, OnImportFacialAnims)
	ON_UPDATE_COMMAND_UI(IDM_IMPORT_FACIAL_ANIMS, OnUpdateImportFacialAnims)
	ON_COMMAND(IDM_RESTRICT_ANIMS, OnRestrictAnims)
	ON_UPDATE_COMMAND_UI(IDM_RESTRICT_ANIMS, OnUpdateRestrictAnims)
	ON_COMMAND(IDM_BIND_SUBOBJECT_LOD, OnBindSubobjectLod)
	ON_UPDATE_COMMAND_UI(IDM_BIND_SUBOBJECT_LOD, OnUpdateBindSubobjectLod)
	ON_COMMAND(IDM_SET_CAMERA_DISTANCE, OnSetCameraDistance)
	ON_COMMAND(IDM_OBJECT_ALTERNATE_MATERIALS, OnObjectAlternateMaterials)
	ON_COMMAND(IDM_CREATE_SOUND_OBJECT, OnCreateSoundObject)
	ON_COMMAND(IDM_EDIT_SOUND_OBJECT, OnEditSoundObject)
	ON_UPDATE_COMMAND_UI(IDM_EDIT_SOUND_OBJECT, OnUpdateEditSoundObject)
	ON_COMMAND(IDM_EXPORT_SOUND_OBJ, OnExportSoundObj)
	ON_UPDATE_COMMAND_UI(IDM_EXPORT_SOUND_OBJ, OnUpdateExportSoundObj)
	ON_COMMAND(IDM_WIREFRAME_MODE, OnWireframeMode)
	ON_UPDATE_COMMAND_UI(IDM_WIREFRAME_MODE, OnUpdateWireframeMode)
	ON_UPDATE_COMMAND_UI(IDM_BACKGROUND_FOG, OnUpdateBackgroundFog)
	ON_COMMAND(IDM_BACKGROUND_FOG, OnBackgroundFog)
	ON_UPDATE_COMMAND_UI(IDM_SCALE_EMITTER, OnUpdateScaleEmitter)
	ON_COMMAND(IDM_SCALE_EMITTER, OnScaleEmitter)
	ON_UPDATE_COMMAND_UI(IDM_TOGGLE_SORTING, OnUpdateToggleSorting)
	ON_COMMAND(IDM_TOGGLE_SORTING, OnToggleSorting)
	ON_UPDATE_COMMAND_UI(IDM_TOGGLE_ALPHA, OnUpdateToggleAlpha)
	ON_COMMAND(IDM_TOGGLE_ALPHA, OnToggleAlpha)
	ON_COMMAND(IDM_FILTER_POINT, OnFilterPoint)
	ON_COMMAND(IDM_FILTER_BILINEAR, OnFilterBilinear)
	ON_COMMAND(IDM_FILTER_TRILINEAR, OnFilterTrilinear)
	ON_COMMAND(IDM_FILTER_ANISO_2X, OnFilterAniso2x)
	ON_COMMAND(IDM_FILTER_ANISO_4X, OnFilterAniso4x)
	ON_COMMAND(IDM_FILTER_ANISO_8X, OnFilterAniso8x)
	ON_COMMAND(IDM_FILTER_ANISO_16X, OnFilterAniso16x)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_POINT, OnUpdateFilterPoint)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_BILINEAR, OnUpdateFilterBilinear)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_TRILINEAR, OnUpdateFilterTrilinear)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_ANISO_2X, OnUpdateFilterAniso2x)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_ANISO_4X, OnUpdateFilterAniso4x)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_ANISO_8X, OnUpdateFilterAniso8x)
	ON_UPDATE_COMMAND_UI(IDM_FILTER_ANISO_16X, OnUpdateFilterAniso16x)
	ON_COMMAND(IDM_MSAA_NONE, OnMsaaNone)
	ON_COMMAND(IDM_MSAA_2X, OnMsaa2x)
	ON_COMMAND(IDM_MSAA_4X, OnMsaa4x)
	ON_COMMAND(IDM_MSAA_8X, OnMsaa8x)
	ON_UPDATE_COMMAND_UI(IDM_MSAA_NONE, OnUpdateMsaaNone)
	ON_UPDATE_COMMAND_UI(IDM_MSAA_2X, OnUpdateMsaa2x)
	ON_UPDATE_COMMAND_UI(IDM_MSAA_4X, OnUpdateMsaa4x)
	ON_UPDATE_COMMAND_UI(IDM_MSAA_8X, OnUpdateMsaa8x)
	ON_COMMAND(IDM_CAMERA_BONE_POS_X, OnCameraBonePosX)
	ON_UPDATE_COMMAND_UI(IDM_CAMERA_BONE_POS_X, OnUpdateCameraBonePosX)
	ON_COMMAND(ID_VIEW_PATCH_GAP_FILL, OnViewPatchGapFill)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PATCH_GAP_FILL, OnUpdateViewPatchGapFill)
	ON_COMMAND(ID_VIEW_SUBDIVISION_1, OnViewSubdivision1)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_1, OnUpdateViewSubdivision1)
	ON_COMMAND(ID_VIEW_SUBDIVISION_2, OnViewSubdivision2)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_2, OnUpdateViewSubdivision2)
	ON_COMMAND(ID_VIEW_SUBDIVISION_3, OnViewSubdivision3)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_3, OnUpdateViewSubdivision3)
	ON_COMMAND(ID_VIEW_SUBDIVISION_4, OnViewSubdivision4)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_4, OnUpdateViewSubdivision4)
	ON_COMMAND(ID_VIEW_SUBDIVISION_5, OnViewSubdivision5)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_5, OnUpdateViewSubdivision5)
	ON_COMMAND(ID_VIEW_SUBDIVISION_6, OnViewSubdivision6)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_6, OnUpdateViewSubdivision6)
	ON_COMMAND(ID_VIEW_SUBDIVISION_7, OnViewSubdivision7)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_7, OnUpdateViewSubdivision7)
	ON_COMMAND(ID_VIEW_SUBDIVISION_8, OnViewSubdivision8)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SUBDIVISION_8, OnUpdateViewSubdivision8)
	ON_COMMAND(IDM_MUNGE_SORT_ON_LOAD, OnMungeSortOnLoad)
	ON_UPDATE_COMMAND_UI(IDM_MUNGE_SORT_ON_LOAD, OnUpdateMungeSortOnLoad)
	ON_COMMAND(IDM_ENABLE_GAMMA_CORRECTION, OnEnableGammaCorrection)
	ON_UPDATE_COMMAND_UI(IDM_ENABLE_GAMMA_CORRECTION, OnUpdateEnableGammaCorrection)
	ON_COMMAND(IDM_SET_GAMMA, OnSetGamma)
	ON_COMMAND(IDM_EDIT_ANIMATED_SOUNDS_OPTIONS, OnEditAnimatedSoundsOptions)
	// TheSuperHackers @feature Async batch file loading
	ON_MESSAGE(WM_USER_LOAD_NEXT_FILE, OnLoadNextFile)
	// TheSuperHackers @feature Reload assets with updated texture path priority (Ctrl+Shift+R)
	ON_COMMAND(IDM_RELOAD_ASSETS, OnReloadAssets)
	ON_UPDATE_COMMAND_UI(IDM_RELOAD_ASSETS, OnUpdateReloadAssets)
	// TheSuperHackers @feature Tria 18/04/2026 Bones submenu in View menu.
	ON_COMMAND(IDM_SHOW_BONE_PIVOTS, OnShowBonePivots)
	ON_UPDATE_COMMAND_UI(IDM_SHOW_BONE_PIVOTS, OnUpdateShowBonePivots)
	ON_COMMAND(IDM_BONE_SIZE_TINY, OnBoneSizeTiny)
	ON_COMMAND(IDM_BONE_SIZE_SMALL, OnBoneSizeSmall)
	ON_COMMAND(IDM_BONE_SIZE_MEDIUM, OnBoneSizeMedium)
	ON_COMMAND(IDM_BONE_SIZE_LARGE, OnBoneSizeLarge)
	ON_COMMAND(IDM_BONE_SIZE_HUGE, OnBoneSizeHuge)
	ON_UPDATE_COMMAND_UI(IDM_BONE_SIZE_TINY, OnUpdateBoneSizeTiny)
	ON_UPDATE_COMMAND_UI(IDM_BONE_SIZE_SMALL, OnUpdateBoneSizeSmall)
	ON_UPDATE_COMMAND_UI(IDM_BONE_SIZE_MEDIUM, OnUpdateBoneSizeMedium)
	ON_UPDATE_COMMAND_UI(IDM_BONE_SIZE_LARGE, OnUpdateBoneSizeLarge)
	ON_UPDATE_COMMAND_UI(IDM_BONE_SIZE_HUGE, OnUpdateBoneSizeHuge)
	// TheSuperHackers @feature Tria 18/04/2026 W3D Shader filter toggles.
	ON_COMMAND(IDM_SHADER_ADDITIVE, OnShaderAdditive)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ADDITIVE, OnUpdateShaderAdditive)
	ON_COMMAND(IDM_SHADER_ALPHA_TEST, OnShaderAlphaTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_TEST, OnUpdateShaderAlphaTest)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND, OnShaderAlphaBlend)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND, OnUpdateShaderAlphaBlend)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND_TEST, OnShaderAlphaBlendTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND_TEST, OnUpdateShaderAlphaBlendTest)
	ON_COMMAND(IDM_SHADER_DOUBLE_SIDED, OnShaderDoubleSided)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_DOUBLE_SIDED, OnUpdateShaderDoubleSided)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT, OnShaderBackfaceTint)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_BACKFACE_TINT, OnUpdateShaderBackfaceTint)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT_COLOR, OnShaderBackfaceTintColor)
	ON_COMMAND(IDM_GROUND_SHOW, OnGroundShow)
	ON_UPDATE_COMMAND_UI(IDM_GROUND_SHOW, OnUpdateGroundShow)
	ON_COMMAND(IDM_GROUND_HEIGHT, OnGroundHeight)
	ON_UPDATE_COMMAND_UI(IDM_GROUND_HEIGHT, OnUpdateGroundHeight)
	ON_COMMAND(IDM_GROUND_RESET, OnGroundReset)
	ON_UPDATE_COMMAND_UI(IDM_GROUND_RESET, OnUpdateGroundReset)
	ON_COMMAND(IDM_SHOW_SUBOBJ_NAMES, OnShowSubObjNames)
	ON_UPDATE_COMMAND_UI(IDM_SHOW_SUBOBJ_NAMES, OnUpdateShowSubObjNames)
	ON_COMMAND(IDM_SHOW_BONE_NAMES, OnShowBoneNames)
	ON_UPDATE_COMMAND_UI(IDM_SHOW_BONE_NAMES, OnUpdateShowBoneNames)
	ON_COMMAND(IDM_SHOW_BONES_AND_SUBOBJECTS, OnShowBonesAndSubObjects)
	ON_UPDATE_COMMAND_UI(IDM_SHOW_BONES_AND_SUBOBJECTS, OnUpdateShowBonesAndSubObjects)
	// TheSuperHackers @feature Tria 18/04/2026 Batch drag-drop through async loader with status bar.
	ON_WM_DROPFILES()
	// TheSuperHackers @feature W3DView native dark mode (Light/Dark/Auto).
	ON_COMMAND(IDM_LOCK_TOOLBARS, OnLockToolbars)
	ON_UPDATE_COMMAND_UI(IDM_LOCK_TOOLBARS, OnUpdateLockToolbars)
	ON_COMMAND(IDM_THEME_LIGHT, OnThemeLight)
	ON_COMMAND(IDM_THEME_DARK,  OnThemeDark)
	ON_COMMAND(IDM_THEME_AUTO,  OnThemeAuto)
	ON_UPDATE_COMMAND_UI(IDM_THEME_LIGHT, OnUpdateThemeLight)
	ON_UPDATE_COMMAND_UI(IDM_THEME_DARK,  OnUpdateThemeDark)
	ON_UPDATE_COMMAND_UI(IDM_THEME_AUTO,  OnUpdateThemeAuto)
	ON_MESSAGE(WM_SETTINGCHANGE, OnSettingChangeRaw)
	ON_MESSAGE(WM_W3DVIEW_THEME_CHANGED, OnW3DViewThemeChanged)
	// TheSuperHackers @feature W3D Material Viewer window.
	ON_COMMAND(IDM_MATERIAL_VIEWER, OnMaterialViewer)
	ON_EN_CHANGE(IDC_TREE_FILTER, OnTreeFilterChanged)
	// TheSuperHackers @feature F5 refreshes the viewport (re-displays the current asset).
	ON_COMMAND(IDM_REFRESH_VIEWPORT, OnRefreshViewport)
	ON_WM_COPYDATA()
	ON_COMMAND(IDM_MCP_SERVER_ENABLED, OnMcpServerEnabled)
	ON_UPDATE_COMMAND_UI(IDM_MCP_SERVER_ENABLED, OnUpdateMcpServerEnabled)
	ON_COMMAND(IDM_MCP_SERVER_INFORMATION, OnMcpServerInformation)

	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

static UINT indicators[] =
{
	ID_SEPARATOR,           // status line indicator
	IDS_POLY_PANE,
	IDS_PARTICLE_PANE,
	IDS_DISTANCE_PANE,
	IDS_FRAME_PANE,
	IDS_FPS_PANE,
	IDS_RESOLUTION_PANE
};

typedef enum
{
	PANE_POLYS			= 1,
	PANE_PARTICLES,
	PANE_DISTANCE,
	PANE_FRAMES,
	PANE_FPS,
	PANE_RESOLUTION,
	PANE_MAX
} STATBAR_PANES;


////////////////////////////////////////////////////////////////////////////
//
//  CMainFrame
//
////////////////////////////////////////////////////////////////////////////
CMainFrame::CMainFrame ()
    : m_currentAssetType (TypeUnknown),
      m_bShowAnimationBar (TRUE),
		m_bInitialized (FALSE),
		m_pendingLoadIndex (0),
		m_totalLoadCount (0),
		m_lastPrefetchedIndex (-1),
		m_polyPaneMode (0),
		m_cachedPolys (0),
		m_cachedVertices (0),
		m_cachedTriangles (0),
		m_toolbarImgLight (nullptr),
		m_bToolbarsLocked (false)
{
	m_bToolbarsLocked =
		::AfxGetApp()->GetProfileInt("Config", "LockToolbars", 0) != 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  ~CMainFrame
//
////////////////////////////////////////////////////////////////////////////
CMainFrame::~CMainFrame ()
{
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreate
//
////////////////////////////////////////////////////////////////////////////
int
CMainFrame::OnCreate (LPCREATESTRUCT lpCreateStruct)
{
	//
	//  HACK HACK
	//
	//  This is done so the other pieces of the code
	//  'know' who their main window is...
	//
	theApp.m_pMainWnd = this;

	if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
		return -1;
	}

	if (::AfxGetApp()->GetProfileInt("Config", "McpServerEnabled", 1) != 0) {
		W3DViewMcpBridge::Attach(m_hWnd);
	}

	if (!m_wndToolBar.Create(this) ||
		 !m_wndToolBar.LoadToolBar(IDR_MAINFRAME)) {
		TRACE0("Failed to create toolbar\n");
		return -1;      // fail to create
	}

	// TheSuperHackers @feature Build the dark-mode toolbar image list from the
	// dark Toolbar.bmp (mirrors how LoadToolBar(IDR_MAINFRAME) builds the light
	// list). We keep a non-owning pointer to the light list so we can swap back.
	{
		m_toolbarImgLight = m_wndToolBar.GetToolBarCtrl().GetImageList();

		// 17 buttons * 16px wide, 15px tall, palette index 15 as the transparency
		// mask color (matches the convention used by CToolBar::LoadToolBar).
		// A 4-bit list would snap the strip's own palette colours to the VGA ones.
		m_toolbarImgDark.Create (16, 15, ILC_COLOR24 | ILC_MASK, 17, 0);
		HBITMAP hbm = ::LoadBitmap (::AfxGetResourceHandle (),
			MAKEINTRESOURCE (IDR_MAINFRAME_DM));
		if (hbm) {
			::ImageList_AddMasked (m_toolbarImgDark.GetSafeHandle (), hbm,
				RGB (192, 192, 192));   // standard MFC toolbar transparency color
			::DeleteObject (hbm);
		}

		if (W3DDarkMode::IsDark () && m_toolbarImgDark.GetSafeHandle ()) {
			m_wndToolBar.GetToolBarCtrl ().SetImageList (&m_toolbarImgDark);
		}
	}

	// The filter box for the asset tree sits at the end of the toolbar, in a
	// separator wide enough to hold it
	{
		CToolBarCtrl &bar = m_wndToolBar.GetToolBarCtrl ();
		TBBUTTON slot = { 0 };
		slot.iBitmap = TREE_FILTER_WIDTH;
		slot.idCommand = IDC_TREE_FILTER;
		slot.fsStyle = TBSTYLE_SEP;
		bar.AddButtons (1, &slot);

		CRect rect;
		bar.GetItemRect (bar.GetButtonCount () - 1, &rect);
		rect.DeflateRect (4, 1);
		m_TreeFilterEdit.Create (WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rect, &m_wndToolBar, IDC_TREE_FILTER);
		m_TreeFilterEdit.SetFont (CFont::FromHandle ((HFONT)::GetStockObject (DEFAULT_GUI_FONT)));
		m_TreeFilterEdit.SendMessage (EM_SETCUEBANNER, FALSE, (LPARAM)L"Filter assets");
	}

	if (!m_wndStatusBar.Create(this) ||
		 !m_wndStatusBar.SetIndicators(indicators, sizeof(indicators)/sizeof(UINT))) {
		TRACE0("Failed to create status bar\n");
		return -1;      // fail to create
	}

	m_objectToolbar.Create ("Object controls", this, 101);
	m_objectToolbar.AddButton (IDB_LOCK_X_UP, IDB_LOCK_X_DN, IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::Type2State);
	m_objectToolbar.AddButton (IDB_LOCK_Y_UP, IDB_LOCK_Y_DN, IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::Type2State);
	m_objectToolbar.AddButton (IDB_LOCK_Z_UP, IDB_LOCK_Z_DN, IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::Type2State);
	m_objectToolbar.AddButton (IDB_ROTATE_Z_UP, IDB_ROTATE_Z_DN, IDM_OBJECT_ROTATE_Z, CFancyToolbar::Type2State);

	m_animationToolbar.Create ("Animation controls", this, 102);
	m_animationToolbar.AddButton (IDB_PLAY_UP, IDB_PLAY_DN, IDM_ANI_START, CFancyToolbar::Type2State);
	m_animationToolbar.AddButton (IDB_STOP_UP, IDB_STOP_DN, IDM_ANI_STOP);
	m_animationToolbar.AddButton (IDB_PAUSE_UP, IDB_PAUSE_DN, IDM_ANI_PAUSE, CFancyToolbar::Type2State);
	m_animationToolbar.AddButton (IDB_REVERSE_UP, IDB_REVERSE_DN, IDM_ANI_STEP_BKWD);
	m_animationToolbar.AddButton (IDB_FFWD_UP, IDB_FFWD_DN, IDM_ANI_STEP_FWD);
	m_animationToolbar.ShowWindow (SW_HIDE);

	// TODO: Remove this if you don't want tool tips or a resizeable toolbar
	m_wndToolBar.SetBarStyle(m_wndToolBar.GetBarStyle () |
		CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC);

	// TODO: Delete these three lines if you don't want the toolbar to
	//  be dockable
	m_wndToolBar.ModifyStyle(0, TBSTYLE_FLAT);
	m_wndToolBar.EnableDocking (CBRS_ALIGN_ANY);
	EnableDocking (CBRS_ALIGN_ANY);
	DockControlBar (&m_wndToolBar);

	// Get the bounding rectangle of the window
	RECT rect;
	GetWindowRect (&rect);

	// Show the object toolbar
	FloatControlBar (&m_objectToolbar, CPoint(rect.left + 10, rect.bottom-100), CBRS_ALIGN_LEFT);

	// Float the animation bar, but don't show it
	FloatControlBar (&m_animationToolbar, CPoint(rect.left + 210, rect.bottom-100), CBRS_ALIGN_LEFT);
	ShowControlBar (&m_animationToolbar, FALSE, FALSE);

	// Don't show anything in these panes for now
	m_wndStatusBar.SetPaneText (PANE_POLYS, "");
	m_wndStatusBar.SetPaneText (PANE_PARTICLES, "");
	m_wndStatusBar.SetPaneText (PANE_DISTANCE, "");
	m_wndStatusBar.SetPaneText (PANE_FRAMES, "");
	m_wndStatusBar.SetPaneText (PANE_FPS, "");
	m_wndStatusBar.SetPaneText (PANE_RESOLUTION, "");

	// Make sure load on demand is activated
	_TheAssetMgr->Set_WW3D_Load_On_Demand (true);

	// Make sure fogging is turned on for all assets.
	_TheAssetMgr->Set_Activate_Fog_On_Load(true);

	GetWindowRect (&m_OrigRect);

	m_hEmittersSubMenu = ::GetSubMenu (::GetMenu (m_hWnd), 3);
	m_hEmittersSubMenu = ::GetSubMenu (m_hEmittersSubMenu, 3);


	Restore_Window_State ();
	m_bInitialized = TRUE;

	return (g_iDeviceIndex != -1) ? 0 : -1;
}


////////////////////////////////////////////////////////////////////////////
//
//  Restore_Window_State
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Restore_Window_State ()
{
	//
	// Read the cached window information from the registry
	//
	CRect rect;
	rect.left	= theApp.GetProfileInt ("Window", "Left", -1);
	rect.right	= theApp.GetProfileInt ("Window", "Right", -1);
	rect.top		= theApp.GetProfileInt ("Window", "Top", -1);
	rect.bottom	= theApp.GetProfileInt ("Window", "Bottom", -1);
	bool is_max	= (theApp.GetProfileInt ("Window", "Maximized", -1) == 1);

	if (rect.left != -1 && rect.right != -1 && rect.top != -1 && rect.bottom != -1) {

		// TheSuperHackers @bugfix Tria 18/04/2026 Validates restored window rectangle against
		// virtual screen bounds to prevent off-screen or tiny window from corrupted registry data.
		int screenLeft   = ::GetSystemMetrics (SM_XVIRTUALSCREEN);
		int screenTop    = ::GetSystemMetrics (SM_YVIRTUALSCREEN);
		int screenRight  = screenLeft + ::GetSystemMetrics (SM_CXVIRTUALSCREEN);
		int screenBottom = screenTop + ::GetSystemMetrics (SM_CYVIRTUALSCREEN);

		bool valid = (rect.Width () >= 200 && rect.Height () >= 150
			&& rect.right > screenLeft && rect.left < screenRight
			&& rect.bottom > screenTop && rect.top < screenBottom);

		if (!valid) {
			return;
		}

		if (is_max) {
			::ShowWindow (m_hWnd, SW_MAXIMIZE);
		} else {
			::SetWindowPos (m_hWnd, nullptr, rect.left, rect.top, rect.Width (), rect.Height (), SWP_NOZORDER);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  RestoreOriginalSize
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::RestoreOriginalSize ()
{
	// Resize the window so its the same size it was when the application loaded
	SetWindowPos (nullptr, 0, 0, m_OrigRect.right-m_OrigRect.left, m_OrigRect.bottom-m_OrigRect.top, SWP_NOMOVE | SWP_NOZORDER);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreateClient
//
////////////////////////////////////////////////////////////////////////////
BOOL
CMainFrame::OnCreateClient
(
	LPCREATESTRUCT /*lpcs*/,
	CCreateContext* pContext
)
{
	//
	// Start up the audio system
	//
	WWAudioClass *audio_mgr = new WWAudioClass;
	audio_mgr->Initialize ();

	// Create the main splitter window for the application
	BOOL bReturn = m_wndSplitter.CreateStatic (this, 1, 2);
	ASSERT (bReturn);

	if (bReturn) {

		// Create the tree view which will contain the textual contents
		// of the W3D file.
		bReturn &= m_wndSplitter.CreateView (	0,
															0,
															RUNTIME_CLASS (CDataTreeView),
															CSize (340, 10),
															pContext);

		// Create the 'graphic' view which will contain a pictoral representation
		// of the currently selected object
		bReturn &= m_wndSplitter.CreateView (	0,
															1,
															RUNTIME_CLASS (CGraphicView),
															CSize (120, 10),
															pContext);

		ASSERT (bReturn);
		if (bReturn) {

			// Get a pointer to the 'graphic' pane's window
			CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
			BOOL bReturn = (pCGraphicView != nullptr);

			// Were we successful in view's getting the pointer?
			ASSERT (pCGraphicView);
			if (pCGraphicView) {

				TCHAR szFileName[MAX_PATH];
				::GetModuleFileName (nullptr, szFileName, sizeof (szFileName));
				LPTSTR pszPath = ::strrchr (szFileName, '\\');
				if (pszPath) {
					pszPath[0] = 0;
					::SetCurrentDirectory (szFileName);
				}

				// Initialize the WW3D engine using the window handle from
				// the graphic viewer class.
				bReturn = (WW3D::Init ((HWND)*pCGraphicView) == WW3D_ERROR_OK);
				ASSERT (bReturn);
				WW3D::Enable_Static_Sort_Lists(true);

				//
				//	Initialize the device
				//
				g_iWidth		= theApp.GetProfileInt ("Config", "DeviceWidth", 640);
				g_iHeight	= theApp.GetProfileInt ("Config", "DeviceHeight", 480);
				Select_Device (false);

				//
				// Register the prototype loaders we'll need
				//
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_HLodLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_DistLODLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_ParticleEmitterLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_AggregateLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_RingLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_SphereLoader);
				WW3DAssetManager::Get_Instance()->Register_Prototype_Loader (&_SoundRenderObjLoader);

				//
				// Restore the N-Patch Subdivision Level and Gap-Filling settings from the last session.
				//
				int subdivision_level	= ::AfxGetApp()->GetProfileInt("Config", "NPatchesSubdivision", 4);
				int gap_filling			= ::AfxGetApp()->GetProfileInt("Config", "NPatchesGapFilling", 0);
				WW3D::Set_NPatches_Gap_Filling_Mode(gap_filling ? WW3D::NPATCHES_GAP_FILLING_ENABLED : WW3D::NPATCHES_GAP_FILLING_DISABLED);
				WW3D::Set_NPatches_Level((unsigned int)subdivision_level);

				//
				// Restore munge sort on load settings
				//
				int munge_sort=::AfxGetApp()->GetProfileInt("Config", "MungeSortOnLoad",0);
				WW3D::Enable_Munge_Sort_On_Load(munge_sort==1?true:false);

				int sort=::AfxGetApp()->GetProfileInt("Config", "EnableSorting",1);
				WW3D::Enable_Sorting(sort==1?true:false);

				// TheSuperHackers @feature xezon 17/04/2026 Restore alpha transparency preview setting.
				int alpha=::AfxGetApp()->GetProfileInt("Config", "ForceAlphaBlend",0);
				ShaderClass::Force_Alpha_Blending(alpha==1?true:false);

				// TheSuperHackers @feature Tria 17/04/2026 Restore MSAA setting.
				int msaa=::AfxGetApp()->GetProfileInt("Config", "MSAA", D3DMULTISAMPLE_NONE);
				if (msaa != D3DMULTISAMPLE_NONE) {
					WW3D::Set_MSAA_Mode((WW3D::MultiSampleModeEnum)msaa);
					DX8Wrapper::Reset_Device(true);
				}

				// TheSuperHackers @feature Tria 17/04/2026 Restore texture filtering setting.
				// Applied after MSAA reset to ensure filter state is not lost.
				int texfilter=::AfxGetApp()->GetProfileInt("Config", "TextureFilter", TextureFilterClass::TEXTURE_FILTER_BILINEAR);
				WW3D::Set_Texture_Filter(texfilter);
				int anisolevel=::AfxGetApp()->GetProfileInt("Config", "AnisotropicLevel", 0);
				if (texfilter == TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC && anisolevel > 0) {
					TextureFilterClass::_Set_Max_Anisotropy((TextureFilterClass::AnisotropicFilterMode)anisolevel);
				}
				s_AnisotropicLevel = anisolevel;

				// restore gamma settings
				int setting=::AfxGetApp()->GetProfileInt("Config","EnableGamma",0);
				if (setting) {
					float gamma=::AfxGetApp()->GetProfileInt("Config","Gamma",10);
					gamma=gamma/10.0f;
					if (gamma<1.0) gamma=1.0;
					if (gamma>3.0) gamma=3.0;
					DX8Wrapper::Set_Gamma(gamma,0.0f,1.0f);
				}
			}
		}
	}


	// Return the TRUE/FALSE result code
	return bReturn;
}


////////////////////////////////////////////////////////////////////////////
//
//  PreCreateWindow
//
////////////////////////////////////////////////////////////////////////////
BOOL
CMainFrame::PreCreateWindow (CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CFrameWnd::PreCreateWindow(cs);
}


/////////////////////////////////////////////////////////////////////////////
// CMainFrame diagnostics

#ifdef RTS_DEBUG
void CMainFrame::AssertValid() const
{
	CFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CFrameWnd::Dump(dc);
}

#endif //RTS_DEBUG


////////////////////////////////////////////////////////////////////////////
//
//  WindowProc
//
////////////////////////////////////////////////////////////////////////////
LRESULT
CMainFrame::WindowProc
(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
	if (message == WM_CLOSE) {

		// We're closing the application so cleanup resources
		CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
		if (pdoc != nullptr) {

			// Ask the Doc to free its resources
			pdoc->CleanupResources ();
		}

	} else if (message == WM_COMMAND) {

		switch (LOWORD (wParam))
		{
			case IDM_SETTINGS1:
			case IDM_SETTINGS2:
			case IDM_SETTINGS3:
			case IDM_SETTINGS4:
			case IDM_SETTINGS5:
			case IDM_SETTINGS6:
			case IDM_SETTINGS7:
			case IDM_SETTINGS8:
			case IDM_SETTINGS9:
			{

				// Get the directory where this executable was run from
				TCHAR filename[MAX_PATH];
				::GetModuleFileName (nullptr, filename, sizeof (filename));

				// Strip the filename from the path
				LPTSTR ppath = ::strrchr (filename, '\\');
				if (ppath != nullptr) {
					ppath[0] = 0;
				}

				// Concat the default.dat filename onto the path
				TCHAR full_path[MAX_PATH];
				strlcat (filename, "\\settings", ARRAY_SIZE(filename));
				::wsprintf (full_path, "%s%d.dat", filename, (LOWORD(wParam) - IDM_SETTINGS1) + 1);

				// Does the file exist in the directory?
				if (::GetFileAttributes (full_path) != 0xFFFFFFFF) {

					// Ask the document to load the settings from this data file
					CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
					if (pdoc != nullptr) {
						pdoc->LoadSettings (full_path);
					}
				}
			}
			break;
		}
	}

	// Open frame scrubber popup when Frame pane clicked; cycle poly/vert/tri when Poly pane clicked.
	if (message == WM_NOTIFY)
	{
		NMHDR *pNMHDR = (NMHDR *)lParam;
		if (pNMHDR != nullptr && pNMHDR->hwndFrom == m_wndStatusBar.m_hWnd && pNMHDR->code == NM_CLICK)
		{
			NMMOUSE *pNMMouse = (NMMOUSE *)lParam;
			CPoint pt (pNMMouse->pt);

			CRect rcPoly;
			m_wndStatusBar.GetItemRect (PANE_POLYS, &rcPoly);
			if (rcPoly.PtInRect (pt))
			{
				m_polyPaneMode = (m_polyPaneMode + 1) % 3;
				UpdatePolygonCount (m_cachedPolys, m_cachedVertices, m_cachedTriangles);
			}
			CRect rcPane;
			m_wndStatusBar.GetItemRect (PANE_FRAMES, &rcPane);
			if (rcPane.PtInRect (pt))
			{
				if (m_frameScrubberDlg.m_hWnd == nullptr)
					m_frameScrubberDlg.Create (IDD_FRAME_SCRUBBER, this);

				if (!m_frameScrubberDlg.IsWindowVisible ())
				{
					// Position near the status bar frame pane
					CRect rcScreen;
					m_wndStatusBar.GetWindowRect (&rcScreen);
					m_frameScrubberDlg.SetWindowPos (nullptr, rcScreen.left + rcPane.left, rcScreen.top - 70, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

					// Initialize slider with current values
					CW3DViewDoc *pDoc = (CW3DViewDoc *)GetActiveDocument ();
					if (pDoc != nullptr && pDoc->GetCurrentAnimation () != nullptr)
					{
						int iTotalFrames = pDoc->GetCurrentAnimation ()->Get_Num_Frames ();
						int iCurrentFrame = pDoc->GetCurrentFrame ();
						m_frameScrubberDlg.UpdateSlider (iCurrentFrame, iTotalFrames);
					}
				}
				else
				{
					m_frameScrubberDlg.ShowWindow (SW_HIDE);
				}
			}
		}
	}

	// Allow the base class to process this message
	return CFrameWnd::WindowProc (message, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectProperties
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectProperties ()
{
    // Dislay the properties for the currently selected object.
    ShowObjectProperties ();
}


////////////////////////////////////////////////////////////////////////////
//
//  ShowObjectProperties
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::ShowObjectProperties ()
{
	// Get a pointer to the 'graphic' pane's window
    CDataTreeView *pCDataTreeView = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
    BOOL bReturn = (pCDataTreeView != nullptr);

    // Were we successful in getting the view's pointer?
    ASSERT (pCDataTreeView);
    if (pCDataTreeView)
    {
        // What type of object is currently selected?
        switch (pCDataTreeView->GetCurrentSelectionType ())
        {
            case TypeMesh:
            {
                // Create a one-page property sheet that will display property information
                // for the mesh
                CMeshPropPage meshPropPage (pCDataTreeView->GetCurrentSelectionName ());
                CAssetPropertySheet propertySheet (IDS_MESH_PROP_TITLE, &meshPropPage, this);

                // Show the property sheet
                propertySheet.DoModal ();
            }
            break;

            case TypeHierarchy:
            {
                // Create a one-page property sheet that will display property information
                // for the hierarchy
                CHierarchyPropPage hierarchyPropPage (pCDataTreeView->GetCurrentSelectionName ());
                CAssetPropertySheet propertySheet (IDS_HIERARCHY_PROP_TITLE, &hierarchyPropPage, this);

                // Show the property sheet
                propertySheet.DoModal ();
            }
            break;

            case TypeAnimation:
            {
                // Create a one-page property sheet that will display property information
                // for the animation
                CAnimationPropPage animationPropPage;
                CAssetPropertySheet propertySheet (IDS_ANIMATION_PROP_TITLE, &animationPropPage, this);

                // Show the property sheet
                propertySheet.DoModal ();
            }
            break;

				case TypeSound:
					OnEditSoundObject ();
					break;

				case TypeEmitter:
					OnEditEmitter ();
					break;

				case TypePrimitives:
					OnEditPrimitive ();
					break;
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateObjectProperties
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateObjectProperties (CCmdUI* pCmdUI)
{
	// Get a pointer to the 'graphic' pane's window
    CDataTreeView *pCDataTreeView = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
    BOOL bReturn = (pCDataTreeView != nullptr);

    // Were we successful in view's getting the pointer?
    ASSERT (pCDataTreeView);
    if (pCDataTreeView)
    {
        // Get the name of the currently selected object
        pCmdUI->Enable (pCDataTreeView->GetCurrentSelectionName () != nullptr);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSelectionChanged
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSelectionChanged (ASSET_TYPE newAssetType)
{
    if (m_currentAssetType != newAssetType)
    {
        // What was the old type?
        switch (m_currentAssetType)
        {
            case TypeAnimation:
				case TypeCompressedAnimation:
            case TypeLOD:
            case TypeHierarchy:
				case TypeAggregate:
            {
                CMenu *pMainMenu = GetMenu ();
                if (pMainMenu)
                {
                    // Remove the 'special' menu from the menubar
                    pMainMenu->RemoveMenu (SPECIAL_MENU_SLOT, MF_BYPOSITION);
                    DrawMenuBar ();
                }

                if ((m_currentAssetType == TypeAnimation) || (m_currentAssetType == TypeCompressedAnimation))
                {
                    // Remember whether or not to show the animation bar next time
                    m_bShowAnimationBar = m_animationToolbar.IsWindowVisible ();

                    // Hide the animation control bar
                    ShowControlBar (&m_animationToolbar, FALSE, FALSE);
                }
            }
            break;
        }

        // Whats the new type?
        switch (newAssetType)
        {
				case TypeCompressedAnimation:
            case TypeAnimation:
            {
                CMenu *pMainMenu = GetMenu ();
                if (pMainMenu)
                {
                    // Load the menu from the resources
                    HMENU hSubMenu = ::LoadMenu (::AfxGetResourceHandle (), MAKEINTRESOURCE(IDR_ANI_MENU));
                    hSubMenu = ::GetSubMenu (hSubMenu, 0);

                    // Add this menu to the menu bar
                    MENUITEMINFO menuInfo = { sizeof (MENUITEMINFO), 0 };
                    menuInfo.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA;
                    menuInfo.hSubMenu = hSubMenu;
                    menuInfo.fType = MFT_STRING;
                    menuInfo.dwTypeData = const_cast<LPSTR>("&Animation");
                    ::InsertMenuItem (*pMainMenu, SPECIAL_MENU_SLOT, TRUE, &menuInfo);

                    // Redrew the menu
                    DrawMenuBar ();
                }

                if (m_bShowAnimationBar)
                {
                    // Show the animation bar
                    OnViewAnimationBar ();
                }
            }
            break;

            case TypeHierarchy:
            {
                CMenu *pMainMenu = GetMenu ();
                if (pMainMenu)
                {
                    // Load the menu from the resources
                    HMENU hSubMenu = ::LoadMenu (::AfxGetResourceHandle (), MAKEINTRESOURCE(IDR_HIERARCHY_MENU));
                    hSubMenu = ::GetSubMenu (hSubMenu, 0);

                    // Add this menu to the menu bar
                    MENUITEMINFO menuInfo = { sizeof (MENUITEMINFO), 0 };
                    menuInfo.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA;
                    menuInfo.hSubMenu = hSubMenu;
                    menuInfo.fType = MFT_STRING;
                    menuInfo.dwTypeData = const_cast<LPSTR>("&Hierarchy");
                    ::InsertMenuItem (*pMainMenu, SPECIAL_MENU_SLOT, TRUE, &menuInfo);

                    // Redrew the menu
                    DrawMenuBar ();
                }
            }
            break;

            case TypeAggregate:
            {
                CMenu *pMainMenu = GetMenu ();
                if (pMainMenu)
                {
                    // Load the menu from the resources
                    HMENU hSubMenu = ::LoadMenu (::AfxGetResourceHandle (), MAKEINTRESOURCE(IDR_AGGREGATE_MENU));
                    hSubMenu = ::GetSubMenu (hSubMenu, 0);

                    // Add this menu to the menu bar
                    MENUITEMINFO menuInfo = { sizeof (MENUITEMINFO), 0 };
                    menuInfo.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA;
                    menuInfo.hSubMenu = hSubMenu;
                    menuInfo.fType = MFT_STRING;
                    menuInfo.dwTypeData = const_cast<LPSTR>("&Aggregate");
                    ::InsertMenuItem (*pMainMenu, SPECIAL_MENU_SLOT, TRUE, &menuInfo);

                    // Redrew the menu
                    DrawMenuBar ();
                }
            }
            break;

            case TypeLOD:
            {
                CMenu *pMainMenu = GetMenu ();
                if (pMainMenu)
                {
                    // Load the menu from the resources
                    HMENU hSubMenu = ::LoadMenu (::AfxGetResourceHandle (), MAKEINTRESOURCE(IDR_LOD_MENU));
                    hSubMenu = ::GetSubMenu (hSubMenu, 0);

                    // Add this menu to the menu bar
                    MENUITEMINFO menuInfo = { sizeof (MENUITEMINFO), 0 };
                    menuInfo.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA;
                    menuInfo.hSubMenu = hSubMenu;
                    menuInfo.fType = MFT_STRING;
                    menuInfo.dwTypeData = const_cast<LPSTR>("&LOD");
                    ::InsertMenuItem (*pMainMenu, SPECIAL_MENU_SLOT, TRUE, &menuInfo);

                    // Redrew the menu
                    DrawMenuBar ();
                }
            }
            break;
        }

        // Remember the new asset type for later
        m_currentAssetType = newAssetType;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLodGenerate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLodGenerate ()
{
	// Get a pointer to the 'data' pane's window
	CDataTreeView *ptree_view = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);

	// Were we successful in view's getting the pointer?
	ASSERT (ptree_view != nullptr);
	if ((ptree_view != nullptr) &&
		 ptree_view->GetCurrentSelectionName ()) {

		// Get the name of the currently selected hierarchy
		LPCTSTR pszName = ptree_view->GetCurrentSelectionName ();

		// Does this name fit with the format expected?
		LOD_NAMING_TYPE type = TYPE_COMMANDO;
		if (Is_LOD_Name_Valid (pszName, type)) {

			// Get the 'base' name from the hierarchy's name
			CString stringName = pszName;

			if (type == TYPE_COMMANDO) {
				stringName = stringName.Left (stringName.GetLength () - 2);
			} else {
				stringName = stringName.Left (stringName.GetLength () - 1);
			}

			// Get a pointer to the document so we can create an LOD
			CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
			ASSERT (pdoc != nullptr);
			if (pdoc != nullptr) {

				// Attempt to generate an LOD from the name of the
				// currently selected hierarchy
				HLodPrototypeClass *plod_prototype = pdoc->GenerateLOD (stringName, type);
				if (plod_prototype != nullptr) {

					// Add this prototype to the asset manager
					WW3DAssetManager::Get_Instance ()->Add_Prototype (plod_prototype);

					// Add this LOD to the tree view
					ptree_view->Add_Asset_To_Tree (plod_prototype->Get_Name (), TypeLOD, true);
				}
			}
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnActivateApp
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnActivateApp
(
    BOOL bActive,
    HTASK_OR_DWORD hTask
)
{
	// Get a pointer to the 'graphic' pane's window
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);

    // Were we successful in view's getting the pointer?
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Let the view know whether or not to actively update
        // its display (animation, etc)
        pCGraphicView->SetActiveUpdate (bActive);
    }

	// Allow the base class to process this message
    CFrameWnd::OnActivateApp(bActive, hTask);
}


////////////////////////////////////////////////////////////////////////////
//
//  Update_Frame_Time
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Update_Frame_Time (DWORD clocks)
{
	static DWORD frames = 0;
	static DWORD total_clocks = 0;
	static DWORD last_update = 0;

	total_clocks += clocks;
	frames ++;

	//if (frames >= 20) {
	if ((::GetTickCount () - last_update) >= 1000) {

		//
		//	Average the frame time
		//
		float frame_time = ((float) total_clocks) / ((float) frames);
		DWORD elapsed_ms = ::GetTickCount () - last_update;
		float fps = (elapsed_ms > 0) ? ((float) frames * 1000.0f / (float) elapsed_ms) : 0.0f;
		CString text;
		text.Format ("FPS: %.1f  Clocks: %.2f", fps, frame_time);

		//
		//	Update the UI
		//
		m_wndStatusBar.SetPaneText (PANE_FPS, text);

		frames = 0;
		total_clocks = 0;
		last_update = ::GetTickCount ();
	}

	// Update the resolution display
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pCGraphicView != nullptr) {

		CRect rect;
		pCGraphicView->GetWindowRect(&rect);

		CString text;
		text.Format (" %d x %d ",rect.Width(),rect.Height());

		m_wndStatusBar.SetPaneText (PANE_RESOLUTION, text);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  UpdatePolygonCount
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::UpdatePolygonCount (int iPolygons)
{
	UpdatePolygonCount (iPolygons, 0, iPolygons);
}

void
CMainFrame::UpdatePolygonCount (int iPolygons, int iVertices, int iTriangles)
{
	m_cachedPolys     = iPolygons;
	m_cachedVertices  = iVertices;
	m_cachedTriangles = iTriangles;

	CString text;
	switch (m_polyPaneMode)
	{
		case 1:  text.Format ("Verts %d",  m_cachedVertices);  break;
		case 2:  text.Format ("Tris %d",   m_cachedTriangles); break;
		default: text.Format ("Polys %d",  m_cachedPolys);     break;
	}
	m_wndStatusBar.SetPaneText (PANE_POLYS, text);
}


////////////////////////////////////////////////////////////////////////////
//
//  Update_Particle_Count
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Update_Particle_Count (int particles)
{
    CString count_string;
    count_string.Format ("Particles %d", particles);

    m_wndStatusBar.SetPaneText (PANE_PARTICLES, count_string);
}


////////////////////////////////////////////////////////////////////////////
//
//  UpdateFrameCount
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::UpdateFrameCount
(
    int		iCurrentFrame,
    int		iTotalFrames,
	 float	frame_rate
)
{
    CString frames;
    frames.Format ("Frame %d/%d at %.2f fps", iCurrentFrame, iTotalFrames, frame_rate);

    m_wndStatusBar.SetPaneText (PANE_FRAMES, frames);

    // TheSuperHackers @feature Tria 18/04/2026 Update frame scrubber dialog if open.
    m_frameScrubberDlg.UpdateSlider (iCurrentFrame, iTotalFrames);
}


////////////////////////////////////////////////////////////////////////////
//
//  UpdateCameraDistance
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::UpdateCameraDistance (float cameraDistance)
{
    CString distance_string;
    distance_string.Format ("Camera %.3f", cameraDistance);

    m_wndStatusBar.SetPaneText (PANE_DISTANCE, distance_string);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDropFiles
//
//  TheSuperHackers @feature Tria 18/04/2026 Batch drag-dropped files through
//  the async loader with status bar progress instead of MFC's default
//  per-file OnOpenDocument which rebuilds the tree after every file.
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnDropFiles (HDROP hDropInfo)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr) {
		::DragFinish (hDropInfo);
		return ;
	}

	UINT fileCount = ::DragQueryFile (hDropInfo, 0xFFFFFFFF, nullptr, 0);
	if (fileCount == 0) {
		::DragFinish (hDropInfo);
		return ;
	}

	m_pendingLoadFiles.Delete_All ();
	char szFilePath[MAX_PATH];
	for (UINT i = 0; i < fileCount; i++) {
		if (::DragQueryFile (hDropInfo, i, szFilePath, MAX_PATH)) {
			m_pendingLoadFiles.Add (szFilePath);
		}
	}
	::DragFinish (hDropInfo);

	if (m_pendingLoadFiles.Count () > 0) {
		m_pendingLoadIndex = 0;
		m_lastPrefetchedIndex = -1;
		m_totalLoadCount   = m_pendingLoadFiles.Count ();
		SetCursor (::LoadCursor (nullptr, IDC_WAIT));
		PostMessage (WM_USER_LOAD_NEXT_FILE);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnFileOpen
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnFileOpen ()
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr) {
		return ;
	}

	// TheSuperHackers @bugfix Use a large dynamic buffer to support 100+ files without overflow.
	// OFN_ALLOWMULTISELECT stores "dir\0file1\0file2\0...\0\0"; MAX_PATH*10 was far too small.
	static const int FILEBUF_CHARS = MAX_PATH * 512;
	CString fileBuffer;
	LPTSTR pszBuf = fileBuffer.GetBuffer(FILEBUF_CHARS);
	pszBuf[0] = 0;

	CFileDialog openFileDialog (TRUE,
								".w3d",
								nullptr,
								OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_ALLOWMULTISELECT | OFN_EXPLORER,
								"Westwood 3D Files (*.w3d)|*.w3d||",
								this);

	openFileDialog.m_ofn.lpstrFile			= pszBuf;
	openFileDialog.m_ofn.nMaxFile			= FILEBUF_CHARS;
	openFileDialog.m_ofn.lpstrInitialDir	= doc->Get_Last_Path ();

	if (openFileDialog.DoModal () == IDOK) {
		// Collect all selected paths into the pending queue
		m_pendingLoadFiles.Delete_All ();
		POSITION pPos = openFileDialog.GetStartPosition ();
		while (pPos != nullptr) {
			m_pendingLoadFiles.Add (openFileDialog.GetNextPathName (pPos));
		}
		fileBuffer.ReleaseBuffer ();

		// TheSuperHackers @feature Kick off async loading: one file processed per message-pump cycle
		if (m_pendingLoadFiles.Count () > 0) {
			m_pendingLoadIndex = 0;
			m_lastPrefetchedIndex = -1;
			m_totalLoadCount   = m_pendingLoadFiles.Count ();
			SetCursor (::LoadCursor (nullptr, IDC_WAIT));
			PostMessage (WM_USER_LOAD_NEXT_FILE);
		}
	} else {
		fileBuffer.ReleaseBuffer ();
	}

	return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  Enumerate_W3D_Files
//
// TheSuperHackers @feature Helper for Open Folder. Walks a directory and (if
// recursive) its subdirectories, appending every *.w3d file to outFiles.
// Skips folders flagged FILE_ATTRIBUTE_HIDDEN or FILE_ATTRIBUTE_SYSTEM so we
// don't waste time walking $Recycle.Bin, System Volume Information, etc.
//
////////////////////////////////////////////////////////////////////////////
static void Enumerate_W3D_Files (const CString &folder,
                                 bool recursive,
                                 DynamicVectorClass<CString> &outFiles)
{
	CString search = folder;
	if (search.IsEmpty ()) return;
	if (search[search.GetLength () - 1] != '\\') search += "\\";

	// Phase 1: files in this directory matching *.w3d
	{
		CString pattern = search + "*.w3d";
		WIN32_FIND_DATAA fd;
		HANDLE h = ::FindFirstFileExA (pattern, FindExInfoBasic, &fd,
		                              FindExSearchNameMatch, nullptr, 0);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					outFiles.Add (search + fd.cFileName);
				}
			} while (::FindNextFileA (h, &fd));
			::FindClose (h);
		}
	}

	if (!recursive) return;

	// Phase 2: subdirectories
	CString dirPattern = search + "*";
	WIN32_FIND_DATAA fd;
	HANDLE h = ::FindFirstFileExA (dirPattern, FindExInfoBasic, &fd,
	                              FindExSearchLimitToDirectories, nullptr, 0);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
		if (fd.cFileName[0] == '.' &&
		    (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))) continue;
		Enumerate_W3D_Files (search + fd.cFileName, true, outFiles);
	} while (::FindNextFileA (h, &fd));
	::FindClose (h);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnFileOpenFolder
//
// TheSuperHackers @feature Modern folder-picker (IFileDialog) with an
// "Include subfolders" checkbox. Enumerates .w3d files under the chosen
// folder and feeds them into the existing async batch-load pipeline.
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnFileOpenFolder (void)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr) return;

	HRESULT coHr = ::CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool coOwned = SUCCEEDED (coHr);

	IFileDialog *pDlg = nullptr;
	HRESULT hr = ::CoCreateInstance (CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
	                                 IID_PPV_ARGS (&pDlg));
	if (FAILED (hr) || pDlg == nullptr) {
		if (coOwned) ::CoUninitialize ();
		::AfxMessageBox ("Failed to open folder picker.", MB_ICONERROR | MB_OK);
		return;
	}

	const DWORD kRecurseCheckId = 1001;
	bool recursive = true; // recommended default - matches user's "ask each time" choice

	{
		DWORD opts = 0;
		pDlg->GetOptions (&opts);
		pDlg->SetOptions (opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
		pDlg->SetTitle (L"Choose a folder to load all .w3d files from");

		IFileDialogCustomize *pCust = nullptr;
		if (SUCCEEDED (pDlg->QueryInterface (IID_PPV_ARGS (&pCust))) && pCust != nullptr) {
			pCust->AddCheckButton (kRecurseCheckId, L"Include subfolders", TRUE);
			pCust->Release ();
		}

		// Set initial folder from doc's last path if available
		LPCTSTR lastPath = doc->Get_Last_Path ();
		if (lastPath != nullptr && lastPath[0] != '\0') {
			wchar_t wpath[MAX_PATH] = {0};
			::MultiByteToWideChar (CP_ACP, 0, lastPath, -1, wpath, MAX_PATH);
			IShellItem *pItem = nullptr;
			if (SUCCEEDED (::SHCreateItemFromParsingName (wpath, nullptr, IID_PPV_ARGS (&pItem)))
			    && pItem != nullptr) {
				pDlg->SetFolder (pItem);
				pItem->Release ();
			}
		}
	}

	hr = pDlg->Show (m_hWnd);
	if (FAILED (hr)) {
		pDlg->Release ();
		if (coOwned) ::CoUninitialize ();
		return; // user cancelled
	}

	// Pull the checkbox state.
	{
		IFileDialogCustomize *pCust = nullptr;
		if (SUCCEEDED (pDlg->QueryInterface (IID_PPV_ARGS (&pCust))) && pCust != nullptr) {
			BOOL checked = TRUE;
			if (SUCCEEDED (pCust->GetCheckButtonState (kRecurseCheckId, &checked))) {
				recursive = checked ? true : false;
			}
			pCust->Release ();
		}
	}

	// Get chosen folder as ANSI path.
	CString folderPath;
	{
		IShellItem *pItem = nullptr;
		if (SUCCEEDED (pDlg->GetResult (&pItem)) && pItem != nullptr) {
			PWSTR pszPath = nullptr;
			if (SUCCEEDED (pItem->GetDisplayName (SIGDN_FILESYSPATH, &pszPath)) && pszPath != nullptr) {
				char apath[MAX_PATH * 2] = {0};
				::WideCharToMultiByte (CP_ACP, 0, pszPath, -1, apath, sizeof(apath), nullptr, nullptr);
				folderPath = apath;
				::CoTaskMemFree (pszPath);
			}
			pItem->Release ();
		}
	}

	pDlg->Release ();
	if (coOwned) ::CoUninitialize ();

	if (folderPath.IsEmpty ()) return;

	// Enumerate.
	SetCursor (::LoadCursor (nullptr, IDC_WAIT));
	DynamicVectorClass<CString> files;
	Enumerate_W3D_Files (folderPath, recursive, files);
	SetCursor (::LoadCursor (nullptr, IDC_ARROW));

	if (files.Count () == 0) {
		CString msg;
		msg.Format ("No .w3d files found in:\n%s%s", (LPCTSTR)folderPath,
		            recursive ? "\n(searched subfolders too)" : "");
		::AfxMessageBox (msg, MB_ICONINFORMATION | MB_OK);
		return;
	}

	// Feed the existing async pipeline.
	m_pendingLoadFiles.Delete_All ();
	for (int i = 0; i < files.Count (); ++i) {
		m_pendingLoadFiles.Add (files[i]);
	}
	m_pendingLoadIndex = 0;
	m_lastPrefetchedIndex = -1;
	m_totalLoadCount   = m_pendingLoadFiles.Count ();
	SetCursor (::LoadCursor (nullptr, IDC_WAIT));
	PostMessage (WM_USER_LOAD_NEXT_FILE);
}


////////////////////////////////////////////////////////////////////////////
//
//  Prefetch_File
//
// TheSuperHackers @performance Read the file once on a worker thread purely
// to warm the OS file-cache, so the subsequent LoadAssetsFromFile() on the
// main thread hits warm pages instead of waiting on disk. The worker never
// touches the (single-threaded) WW3D asset manager - it only reads bytes
// into a throwaway buffer. The path is owned by std::string so we don't
// share the non-thread-safe CString refcounted buffer across threads.
//
////////////////////////////////////////////////////////////////////////////
static void Prefetch_File (const std::string &path)
{
	HANDLE h = ::CreateFileA (path.c_str (), GENERIC_READ,
							  FILE_SHARE_READ | FILE_SHARE_WRITE,
							  nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
							  nullptr);
	if (h == INVALID_HANDLE_VALUE) return;

	char buf[64 * 1024];
	DWORD bytesRead = 0;
	while (::ReadFile (h, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
		// Just walk the file; the goal is to populate the cache, not to keep the data.
	}
	::CloseHandle (h);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLoadNextFile
//
////////////////////////////////////////////////////////////////////////////
LRESULT
CMainFrame::OnLoadNextFile (WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr || m_pendingLoadIndex >= m_pendingLoadFiles.Count ()) {
		m_pendingLoadFiles.Delete_All ();
		m_pendingLoadIndex = 0;
		m_lastPrefetchedIndex = -1;
		SetCursor (::LoadCursor (nullptr, IDC_ARROW));
		return 0;
	}

	// TheSuperHackers @performance Process a batch of files per WM_USER_LOAD_NEXT_FILE
	// instead of one. Single-file dispatch added a full MFC message-pump round-trip,
	// status-bar redraw, and AddToRecentFileList call per file - several ms each, which
	// adds up to many seconds across thousands of files. Batching amortizes that without
	// freezing the UI: between batches the pump still runs, so cancel/redraw still work.
	const int BATCH_SIZE = 16;
	int batchStart = m_pendingLoadIndex;
	int batchEnd   = m_pendingLoadIndex + BATCH_SIZE;
	if (batchEnd > m_pendingLoadFiles.Count ()) batchEnd = m_pendingLoadFiles.Count ();

	// Submit OS-cache prefetch jobs for files BEYOND the current batch only.
	// TheSuperHackers @performance Tria 2026-05-19 The synchronous batch loop below
	// reads each file in the current batch via the asset manager - prefetching them
	// is a redundant native open per file (most painful on single-file/small-batch
	// opens where prefetch == the entire load). We still warm the next batch's window
	// so workers stay useful while the main thread parses.
	{
		const int PREFETCH_AHEAD = BATCH_SIZE;
		int prefetchFrom = batchEnd;
		if (prefetchFrom <= m_lastPrefetchedIndex) prefetchFrom = m_lastPrefetchedIndex + 1;
		int upTo = batchEnd + PREFETCH_AHEAD;
		if (upTo > m_pendingLoadFiles.Count ()) upTo = m_pendingLoadFiles.Count ();
		for (int i = prefetchFrom; i < upTo; ++i) {
			std::string p ((LPCTSTR)m_pendingLoadFiles[i]);
			W3DViewJobs::Submit ([p]() { Prefetch_File (p); });
			m_lastPrefetchedIndex = i;
		}
	}

	// One status-bar update per batch (SetPaneText forces a redraw, so doing
	// it 4000 times during a 4000-file load is itself a perceptible cost).
	{
		const CString &firstFile = m_pendingLoadFiles[batchStart];
		CString statusText;
		statusText.Format ("Loading %d / %d: %s", batchStart + 1, m_totalLoadCount,
						   ::Get_Filename_From_Path (firstFile));
		m_wndStatusBar.SetPaneText (0, statusText);
	}

	CString lastFileName;
	for (int i = batchStart; i < batchEnd; ++i) {
		const CString &fileName = m_pendingLoadFiles[i];
		doc->LoadAssetsFromFile (fileName);
		::AfxGetApp ()->AddToRecentFileList (fileName);
		lastFileName = fileName;
	}
	m_pendingLoadIndex = batchEnd;

	if (m_pendingLoadIndex < m_pendingLoadFiles.Count ()) {
		// More files remain: post next message so the pump stays alive between batches
		PostMessage (WM_USER_LOAD_NEXT_FILE);
	} else {
		// All files loaded: rebuild tree and clean up
		CDataTreeView *pCDataTreeView = doc->GetDataTreeView ();
		if (pCDataTreeView) {
			pCDataTreeView->LoadAssetsIntoTree ();
		}
		// TheSuperHackers @feature Tria 18/04/2026 Set the document title to the last loaded file.
		doc->SetTitle (::Get_Filename_From_Path (lastFileName));

		m_pendingLoadFiles.Delete_All ();
		m_pendingLoadIndex = 0;
		m_lastPrefetchedIndex = -1;
		m_wndStatusBar.SetPaneText (0, "");
		SetCursor (::LoadCursor (nullptr, IDC_ARROW));

		// Reload Assets: restore the previously displayed object, animation
		// state and camera. No-op if this wasn't a reload (valid=false) or
		// the previous asset no longer exists.
		RestoreReloadState ();
	}

	return 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  CaptureReloadState
//
//  Snapshot the user-visible state that OnNewDocument is about to destroy
//  during Reload Assets: the displayed asset, current animation + frame +
//  playback state, camera transform, and any sub-object/bone highlight.
//  RestoreReloadState reapplies it after the last file finishes loading.
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::CaptureReloadState (void)
{
	m_reloadRestore = ReloadRestoreState (); // reset to defaults

	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr) {
		return;
	}

	RenderObjClass *prender_obj = doc->GetDisplayedObject ();
	if (prender_obj == nullptr) {
		return; // nothing displayed — nothing to restore
	}

	m_reloadRestore.displayedAssetName = prender_obj->Get_Name ();

	HAnimClass *panim = doc->GetCurrentAnimation ();
	if (panim != nullptr) {
		m_reloadRestore.animationName = panim->Get_Name ();
	}
	m_reloadRestore.currentFrame = doc->GetCurrentFrameF ();

	CGraphicView *pview = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pview != nullptr) {
		m_reloadRestore.animState  = pview->GetAnimationState ();
		m_reloadRestore.animSpeed  = pview->GetAnimationSpeed ();
		CameraClass *pcamera = pview->GetCamera ();
		if (pcamera != nullptr) {
			m_reloadRestore.cameraTransform = pcamera->Get_Transform ();
		}
		m_reloadRestore.cameraDistance = pview->Get_Camera_Distance ();
	}

	m_reloadRestore.selectedSubItemName = doc->GetSelectedItemName ();
	m_reloadRestore.selectedSubItemType = doc->GetSelectedItemType ();

	m_reloadRestore.valid = true;
	return;
}


////////////////////////////////////////////////////////////////////////////
//
//  Run_Posted_Commands
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Run_Posted_Commands (void)
{
	MSG message;
	while (::PeekMessage (&message, m_hWnd, WM_COMMAND, WM_COMMAND, PM_REMOVE)) {
		::DispatchMessage (&message);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  RestoreReloadState
//
//  Called from OnLoadNextFile after the last file batch has loaded and the
//  tree has been rebuilt by LoadAssetsIntoTree. Re-selects the previously
//  displayed asset (which triggers the canonical Display_Asset path),
//  rebinds the previous animation and frame, restores playback state and
//  speed, and snaps the camera back to its previous viewpoint. Best-effort:
//  silently skips anything that no longer exists after reload.
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::RestoreReloadState (void)
{
	if (!m_reloadRestore.valid) {
		return;
	}
	// Clear up-front so an early return / exception doesn't replay later.
	m_reloadRestore.valid = false;

	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr) {
		return;
	}
	CDataTreeView *ptree = doc->GetDataTreeView ();
	if (ptree == nullptr) {
		return;
	}

	HTREEITEM hitem = ptree->Find_Asset_Item_By_Name (m_reloadRestore.displayedAssetName);
	if (hitem == nullptr) {
		return; // asset is gone after reload — leave viewport empty
	}

	// Selecting the item fires CDataTreeView::OnSelChanged -> Display_Asset
	// which calls doc->DisplayObject. This is the canonical "switch the
	// viewport to render object X" path; reusing it keeps us out of the
	// render-obj lifetime business.
	ptree->GetTreeCtrl ().SelectItem (hitem);
	ptree->GetTreeCtrl ().EnsureVisible (hitem);

	// Restore animation if the named one still exists in the asset manager.
	if (m_reloadRestore.animationName.Get_Length () > 0) {
		HAnimClass *panim = WW3DAssetManager::Get_Instance ()->Get_HAnim (m_reloadRestore.animationName);
		if (panim != nullptr) {
			panim->Release_Ref (); // Get_HAnim adds a ref; PlayAnimation will add its own
			RenderObjClass *pmodel = doc->GetDisplayedObject ();
			if (pmodel != nullptr) {
				doc->PlayAnimation (pmodel, m_reloadRestore.animationName);

				// The posted start would otherwise arrive later and undo a paused or stopped state
				Run_Posted_Commands ();
			}
		}
	}

	// Restore animation playback state and speed. SetAnimationState may snap
	// the frame back to 0 (AnimStopped branch), so we do it BEFORE
	// SetCurrentFrame.
	CGraphicView *pview = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pview != nullptr) {
		pview->SetAnimationSpeed (m_reloadRestore.animSpeed);
		pview->SetAnimationState (m_reloadRestore.animState);
	}
	doc->SetCurrentFrame (m_reloadRestore.currentFrame);

	// Restore camera last: Set_Camera_Distance overwrites the transform, so
	// apply distance first, then the saved transform wins.
	if (pview != nullptr) {
		pview->Set_Camera_Distance (m_reloadRestore.cameraDistance);
		CameraClass *pcamera = pview->GetCamera ();
		if (pcamera != nullptr) {
			pcamera->Set_Transform (m_reloadRestore.cameraTransform);
		}
	}

	// Sub-object / bone highlight is cleared by DisplayObject; reapply it
	// here so the highlight survives reload.
	if (m_reloadRestore.selectedSubItemName.Get_Length () > 0) {
		doc->SetSelectedItem (m_reloadRestore.selectedSubItemName,
									 m_reloadRestore.selectedSubItemType);
	}

	return;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnReloadAssets
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnReloadAssets (void)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc == nullptr || doc->Get_Load_List_Count () == 0) {
		return ;
	}

	// Snapshot what's currently displayed so RestoreReloadState can bring it
	// back after OnNewDocument wipes everything below.
	CaptureReloadState ();

	// Copy current load list before OnNewDocument wipes it
	m_pendingLoadFiles.Delete_All ();
	for (int i = 0; i < doc->Get_Load_List_Count (); i++) {
		m_pendingLoadFiles.Add (doc->Get_Load_List_Item (i));
	}
	m_pendingLoadIndex = 0;
	m_lastPrefetchedIndex = -1;
	m_totalLoadCount   = m_pendingLoadFiles.Count ();

	// Reset scene, tree and asset manager (clears m_LoadList)
	doc->OnNewDocument ();

	// Apply current texture path priority order before reloading
	doc->Refresh_File_Factory_Registrations ();

	// Kick off async reload so the message pump stays alive between files
	SetCursor (::LoadCursor (nullptr, IDC_WAIT));
	PostMessage (WM_USER_LOAD_NEXT_FILE);

	return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateReloadAssets
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateReloadAssets (CCmdUI *pCmdUI)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	pCmdUI->Enable (doc != nullptr && doc->Get_Load_List_Count () > 0);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniSpeed
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniSpeed ()
{
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        float initialSpeed = pCGraphicView->GetAnimationSpeed ();

	    CAnimationSpeed animationSpeedDialog (this);
        if (animationSpeedDialog.DoModal () != IDOK)
        {
            pCGraphicView->SetAnimationSpeed (initialSpeed);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniStop
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniStop ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Stop the animation
        pCGraphicView->SetAnimationState (CGraphicView::AnimStopped);

        // Pop the start and pause buttons on the toolbar
        m_animationToolbar.SetButtonState (IDM_ANI_START, CFancyToolbar::StateUp);
        m_animationToolbar.SetButtonState (IDM_ANI_PAUSE, CFancyToolbar::StateUp);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniStart
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniStart ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Start the animation
        pCGraphicView->SetAnimationState (CGraphicView::AnimPlaying);

        // Pop the pause button on the toolbar
        m_animationToolbar.SetButtonState (IDM_ANI_PAUSE, CFancyToolbar::StateUp);

        // Push the 'play' button
        m_animationToolbar.SetButtonState (IDM_ANI_START, CFancyToolbar::StateDn);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniPause
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniPause ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        if (pCGraphicView->GetAnimationState () == CGraphicView::AnimPlaying)
        {
            // Pause the animation
            pCGraphicView->SetAnimationState (CGraphicView::AnimPaused);

            // Push the pause button on the toolbar
            m_animationToolbar.SetButtonState (IDM_ANI_PAUSE, CFancyToolbar::StateDn);
        }
        else if (pCGraphicView->GetAnimationState () == CGraphicView::AnimPaused)
        {
            // Play the animation
            pCGraphicView->SetAnimationState (CGraphicView::AnimPlaying);

            // Pop the pause button on the toolbar
            m_animationToolbar.SetButtonState (IDM_ANI_PAUSE, CFancyToolbar::StateUp);
        }
        else
        {
            // Pop the pause button on the toolbar
            m_animationToolbar.SetButtonState (IDM_ANI_PAUSE, CFancyToolbar::StateUp);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraBack ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraBack);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraBottom
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraBottom ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraBottom);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraFront
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraFront ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraFront);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraLeft
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraLeft ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraLeft);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraReset
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraReset ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Get a pointer to the current document
        CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetActiveDocument ();
        if (pCDoc && pCDoc->GetDisplayedObject ())
        {
				// Reset the camera data
				RenderObjClass *prender_obj = pCDoc->GetDisplayedObject ();
				if (prender_obj->Class_ID () == RenderObjClass::CLASSID_PARTICLEEMITTER) {
					pCGraphicView->Reset_Camera_To_Display_Emitter (*((ParticleEmitterClass *)prender_obj));
				} else {
					pCGraphicView->Reset_Camera_To_Display_Object (*prender_obj);
				}
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraRight
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraRight ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraRight);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraTop
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraTop ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Position the camera as requested
        pCGraphicView->SetCameraPos (CGraphicView::CameraTop);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateZ
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectRotateZ ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        int iZRotation = (pCGraphicView->GetObjectRotation () ^ (CGraphicView::RotateZ));
		  iZRotation &= ~CGraphicView::RotateZBack;

        // Start or stop the rotation around Z
        pCGraphicView->RotateObject ((CGraphicView::OBJECT_ROTATION)iZRotation);

        if (iZRotation & ROTATION_Z)
        {
            // Force the toolbar button to be up
            m_objectToolbar.SetButtonState (IDM_OBJECT_ROTATE_Z, CFancyToolbar::StateDn);
        }
        else
        {
            // Force the toolbar button to be up
            m_objectToolbar.SetButtonState (IDM_OBJECT_ROTATE_Z, CFancyToolbar::StateUp);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectRotateY ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        int iYRotation = (pCGraphicView->GetObjectRotation () ^ (CGraphicView::RotateY));
		  iYRotation &= ~CGraphicView::RotateYBack;

        // Start or stop the rotation around Y
        pCGraphicView->RotateObject ((CGraphicView::OBJECT_ROTATION)iYRotation);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateX
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectRotateX ()
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)

    {
        int iXRotation = (pCGraphicView->GetObjectRotation () ^ (CGraphicView::RotateX));
		  iXRotation &= ~CGraphicView::RotateXBack;

        // Start or stop the rotation around X
        pCGraphicView->RotateObject ((CGraphicView::OBJECT_ROTATION)iXRotation);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLightAmbient
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightAmbient ()
{
    // Show the ambient light dialog
    CAmbientLightDialog ambientLightDialog (this);
    ambientLightDialog.DoModal ();

}


////////////////////////////////////////////////////////////////////////////
//
//  OnLightAmbient
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightScene ()
{
    // Show the scene light dialog
    CSceneLightDialog sceneLightDialog (this);
    sceneLightDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnBackgroundColor
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBackgroundColor ()
{
    // Show the background color
    CBackgroundColorDialog backgroundColorDialog (this);
    backgroundColorDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateBackgroundFog
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateBackgroundFog (CCmdUI* pCmdUI)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc) {
		pCmdUI->SetCheck(pdoc->IsFogEnabled());
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnBackgroundFog
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBackgroundFog ()
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc) {
		// Toggle the fog setting.
		pdoc->EnableFog(!pdoc->IsFogEnabled());
	}
}



////////////////////////////////////////////////////////////////////////////
//
//  OnBackgroundBMP
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBackgroundBMP ()
{
    // Show the background BMP dialog
    CBackgroundBMPDialog backgroundBMPDialog (this);
    backgroundBMPDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSaveSettings
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSaveSettings ()
{
    // Show the save settings dialog
    CSaveSettingsDialog saveSettingsDialog (this);
    saveSettingsDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLoadSettings
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLoadSettings ()
{
    // Get the active document
    CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetActiveDocument ();
    if (pCDoc)
    {
        CFileDialog openFileDialog (TRUE,
                                    ".dat",
                                    nullptr,
                                    OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER,
                                    "Settings data files (*.dat)|*.dat||",
                                    this);

        // Ask the user what settings file they wish to load
        if (openFileDialog.DoModal () == IDOK)
        {
            // Ask the doc to load the settings from this file
            pCDoc->LoadSettings (openFileDialog.GetPathName ());
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLODSetSwitch
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLODSetSwitch ()
{
    // Display the edit LOD dialog
    CEditLODDialog editLODDialog (this);
    editLODDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLODSave
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLODSave ()
{
	// Get the controlling doc object so we can have it save the
	// LOD for us.
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc != nullptr) {
		pdoc->Save_Selected_LOD ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLODSaveAll
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLODSaveAll ()
{
    MessageBox ("Not implemented yet.", "Test", MB_OK | MB_ICONEXCLAMATION);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnBackgroundObject
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBackgroundObject ()
{
    // Display the background object dialog
	CBackgroundObjectDialog backgroundObjectDialog (this);
    backgroundObjectDialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateViewAnimationBar
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateViewAnimationBar (CCmdUI* pCmdUI)
{
    // TheSuperHackers @bugfix Tria 19/04/2026 Fix logic so animation bar can be enabled when an animation is loaded.
    // Are we currently displaying an animation?
    if ((m_currentAssetType != TypeAnimation) && (m_currentAssetType != TypeCompressedAnimation))
    {
        // Disable the option and clear the check
        pCmdUI->Enable (FALSE);
        pCmdUI->SetCheck (FALSE);
    }
    else
    {
        // Enable the option and set the correct state of the check
        pCmdUI->Enable (TRUE);
        pCmdUI->SetCheck (m_animationToolbar.IsWindowVisible ());
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateViewObjectBar
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateViewObjectBar (CCmdUI* pCmdUI)
{
    // Enable the option and set the correct state of the check
    pCmdUI->Enable (TRUE);
    pCmdUI->SetCheck (m_objectToolbar.IsWindowVisible ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnViewAnimationBar
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnViewAnimationBar ()
{
    if (m_animationToolbar.IsWindowVisible () == FALSE)
    {
        // Show the animation control bar
        ShowControlBar (&m_animationToolbar, TRUE, FALSE);

        // Remember whether or not to auto show this toolbar
        m_bShowAnimationBar = TRUE;
    }
    else
    {
        // Hide the animation control bar
        ShowControlBar (&m_animationToolbar, FALSE, FALSE);

        // Remember whether or not to auto show this toolbar
        m_bShowAnimationBar = FALSE;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnViewObjectBar
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnViewObjectBar ()
{
    if (m_objectToolbar.IsWindowVisible () == FALSE)
    {
        // Show the object control bar
        ShowControlBar (&m_objectToolbar, TRUE, FALSE);
    }
    else
    {
        // Hide the object control bar
        ShowControlBar (&m_objectToolbar, FALSE, FALSE);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniStepFwd
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniStepFwd ()
{
    // Get the current doc
    CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetActiveDocument ();
    if (pCDoc)
    {
        // Ask the doc to step the animation forward one frame
        pCDoc->StepAnimation (1);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAniStepBkwd
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAniStepBkwd ()
{
    // Get the current doc
    CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetActiveDocument ();
    if (pCDoc)
    {
        // Ask the doc to step the animation backward one frame
        pCDoc->StepAnimation (-1);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectReset
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectReset ()
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        // Ask the view to reset the rotation of the current object
        pCGraphicView->ResetObject ();
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraAllowRotateX
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraAllowRotateX ()
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        if (pCGraphicView->GetAllowedCameraRotation () != CGraphicView::OnlyRotateX)
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::OnlyRotateX);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateDn);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateUp);
        }
        else
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::FreeRotation);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateUp);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraAllowRotateY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraAllowRotateY ()
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        if (pCGraphicView->GetAllowedCameraRotation () != CGraphicView::OnlyRotateY)
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::OnlyRotateY);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateDn);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateUp);
        }
        else
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::FreeRotation);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateUp);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraAllowRotateZ
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraAllowRotateZ ()
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        if (pCGraphicView->GetAllowedCameraRotation () != CGraphicView::OnlyRotateZ)
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::OnlyRotateZ);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateDn);
        }
        else
        {
            // Enable rotatation about this axis
            pCGraphicView->SetAllowedCameraRotation (CGraphicView::FreeRotation);

            // Make sure the toolbar buttons are in the right state
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_X, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Y, CFancyToolbar::StateUp);
            m_objectToolbar.SetButtonState (IDM_CAMERA_ALLOW_ROTATE_Z, CFancyToolbar::StateUp);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraAllowRotateX
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraAllowRotateX (CCmdUI* pCmdUI)
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        // Either turn the check on or off depending on the view's settings
        pCmdUI->SetCheck (pCGraphicView->GetAllowedCameraRotation () == CGraphicView::OnlyRotateX);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraAllowRotateY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraAllowRotateY (CCmdUI* pCmdUI)
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        // Either turn the check on or off depending on the view's settings
        pCmdUI->SetCheck (pCGraphicView->GetAllowedCameraRotation () == CGraphicView::OnlyRotateY);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraAllowRotateZ
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraAllowRotateZ (CCmdUI* pCmdUI)
{
    // Get the graphic view
    CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    if (pCGraphicView)
    {
        // Either turn the check on or off depending on the view's settings
        pCmdUI->SetCheck (pCGraphicView->GetAllowedCameraRotation () == CGraphicView::OnlyRotateZ);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateObjectRotateX
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateObjectRotateX (CCmdUI* pCmdUI)
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Set the check if we are currently rotating around X
        pCmdUI->SetCheck ((pCGraphicView->GetObjectRotation () & (CGraphicView::RotateX)));
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateObjectRotateY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateObjectRotateY (CCmdUI* pCmdUI)
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Set the check if we are currently rotating around Y
        pCmdUI->SetCheck ((pCGraphicView->GetObjectRotation () & (CGraphicView::RotateY)));
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateObjectRotateZ
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateObjectRotateZ (CCmdUI* pCmdUI)
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Set the check if we are currently rotating around Z
        pCmdUI->SetCheck ((pCGraphicView->GetObjectRotation () & (CGraphicView::RotateZ)));
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  Select_Device
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Select_Device (bool show_dlg)
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
    ASSERT (pCGraphicView);
    if (pCGraphicView)
    {
        // Show a dialog to the user asking them which
        // device they would like to use.
        CDeviceSelectionDialog deviceSelDialog (show_dlg == false, this);
        if (deviceSelDialog.DoModal () == IDOK)
        {
            // Record the user's selection from the dialog (as globals for now)
            g_iDeviceIndex = deviceSelDialog.GetDeviceIndex ();
            g_iBitsPerPixel = deviceSelDialog.GetBitsPerPixel ();

            // Ask the view to initialize itself with the graphics engine
            BOOL bReturn = pCGraphicView->InitializeGraphicView ();
            ASSERT (bReturn);

				if (bReturn) {

					//
					//	Get information about the current device
					//
					const RenderDeviceDescClass &device_desc = WW3D::Get_Render_Device_Desc ();
					CString driver_name = deviceSelDialog.GetDriverName ();
					CString chipset = device_desc.Get_Hardware_Chipset ();
					CString string_version = device_desc.Get_Driver_Version ();
					chipset.MakeUpper ();
					driver_name.MakeLower ();

					//
					//	Check to ensure the drivers are valid if the user choose glide
					//
					if (::strstr (driver_name, "glide2") != nullptr) {

						// Is this glide driver an acceptable version?
						float driver_version = ::atof (string_version);
						bool is_voodoo2 = (::strstr (chipset , "VOODOO2") != nullptr);
						if ((is_voodoo2 && (driver_version < 2.54F)) ||
							 ((is_voodoo2 == false) && (driver_version < 2.46F))) {

							// Let the user know we can't use these drivers
							CString message;
							message.LoadString (IDS_UNACCEPTABLE_GLIDE_MSG);
							::MessageBox (nullptr, message, "Invalid Device", MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);

							// Force the user to choose a new device
							Select_Device (true);
						}
					}
				}
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDeviceChange
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnDeviceChange ()
{
#ifdef WW3D_DX8
	Select_Device (true);
#else
	::MessageBox(m_hWnd,"Feature removed during conversion to DX8.","Unsupported Feature",MB_OK|MB_ICONEXCLAMATION);
#endif
}


////////////////////////////////////////////////////////////////////////////
//
//  OnViewFullscreen
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnViewFullscreen ()
{
#ifdef WW3D_DX8
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pCGraphicView->Is_Fullscreen ())
	{
		RestoreOriginalSize ();
	}
	pCGraphicView->Set_Fullscreen (!pCGraphicView->Is_Fullscreen ());
#else
	::MessageBox(m_hWnd,"Feature removed during conversion to DX8.","Unsupported Feature",MB_OK|MB_ICONEXCLAMATION);
#endif
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateViewFullscreen
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateViewFullscreen (CCmdUI* pCmdUI)
{
	CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	pCmdUI->SetCheck (pCGraphicView->Is_Fullscreen ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnWindowPosChanging
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnWindowPosChanging (WINDOWPOS FAR* lpwndpos)
{
	CFrameWnd::OnWindowPosChanging (lpwndpos);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnGetMinMaxInfo
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnGetMinMaxInfo (MINMAXINFO FAR* lpMMI)
{
	CFrameWnd::OnGetMinMaxInfo(lpMMI);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreateEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCreateEmitter ()
{
	// Clear the current display
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc) {
		pdoc->DisplayObject ((RenderObjClass *)nullptr);
	}

	// Display the emitter property sheet
	EmitterPropertySheetClass prop_sheet (nullptr,
													  IDS_EMITTER_PROP_TITLE,
													  this);
	prop_sheet.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnEditEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnEditEmitter ()
{
	// Get a pointer to the doc object
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc != nullptr) {

		//
		// Make a list of emitters containing the currently displayed emitter
		//
		ParticleEmitterClass *emitter = (ParticleEmitterClass *)pdoc->GetDisplayedObject ();
		EmitterInstanceListClass *instance_list = new EmitterInstanceListClass;
		instance_list->Add_Emitter (emitter);

		//
		// Show the emitter property sheet
		//
		EmitterPropertySheetClass prop_sheet (instance_list, IDS_EMITTER_PROP_TITLE, this);
		prop_sheet.DoModal ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateEditEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateEditEmitter (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeEmitter);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnScaleEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnScaleEmitter ()
{
	// Get a pointer to the doc object
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	if (pdoc != nullptr) {

		//
		// Display a dialog that allows the user to choose the scaling factor
		// to be applied to the current particle emitter.
		//
		ScaleDialogClass dlg(1.0, this, "Enter the scaling factor you want to apply to the current particle emitter");
		if (dlg.DoModal() == IDCANCEL)
			return ;
		float scale = dlg.Get_Scale();

		//
		// Get and scale the current emitter.
		//
		ParticleEmitterClass *emitter = (ParticleEmitterClass *)pdoc->GetDisplayedObject();
		emitter->Scale(scale);

		//
		// Ensure the prototype for this particle emitter is also updated,
		// otherwise the change will be lost when the user switches away from
		// the emitter (because the render object will be destroyed).
		//
		const char *name = emitter->Get_Name();
		if (name && *name) {
			WW3DAssetManager::Get_Instance()->Remove_Prototype (name);
		}
		ParticleEmitterDefClass *pdefinition = emitter->Build_Definition();
		ParticleEmitterPrototypeClass *pprototype	= new ParticleEmitterPrototypeClass (pdefinition);
		WW3DAssetManager::Get_Instance()->Add_Prototype (pprototype);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateScaleEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateScaleEmitter (CCmdUI* pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeEmitter);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSaveEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSaveEmitter ()
{
	((CW3DViewDoc *)GetActiveDocument ())->Save_Selected_Emitter ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateSaveEmitter
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateSaveEmitter (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeEmitter);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnBoneAutoAssign
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBoneAutoAssign ()
{
	((CW3DViewDoc *)GetActiveDocument ())->Auto_Assign_Bones ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnBoneManagement
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBoneManagement ()
{
	// Get the currently selected hierarchy model
	RenderObjClass *prender_obj = ((CW3DViewDoc *)GetActiveDocument ())->GetDisplayedObject ();

	// Show the bone manager dialog
	BoneMgrDialogClass dialog (prender_obj, this);
	dialog.DoModal ();
	Update_Emitters_List ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSaveAggregate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSaveAggregate ()
{
	((CW3DViewDoc *)GetActiveDocument ())->Save_Selected_Aggregate ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraAnimate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraAnimate ()
{
	// Toggel the animated state
	bool banimated = ((CW3DViewDoc *)GetActiveDocument ())->Is_Camera_Animated ();
	((CW3DViewDoc *)GetActiveDocument ())->Animate_Camera (banimated == false);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSaveAggregate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraAnimate (CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck (((CW3DViewDoc *)GetActiveDocument ())->Is_Camera_Animated ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateLodSave
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateLodSave (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeLOD);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateSaveAggregate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateSaveAggregate (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeAggregate);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraResetOnLoad
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraResetOnLoad ()
{
	// Toggle the auto reset state of the menu option
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	pdoc->Turn_Camera_Auto_Reset_On ((pdoc->Is_Camera_Auto_Reset_On () == false));
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraResetOnLoad
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraResetOnLoad (CCmdUI *pCmdUI)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument ();
	pCmdUI->SetCheck (pdoc->Is_Camera_Auto_Reset_On ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraInvertY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraInvertY (void)
{
	CGraphicView *pview = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pview != nullptr) {
		pview->Set_Invert_Camera_Y (pview->Is_Invert_Camera_Y () == false);
	}
	return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraInvertY
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraInvertY (CCmdUI *pCmdUI)
{
	CGraphicView *pview = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	pCmdUI->SetCheck ((pview != nullptr) && pview->Is_Invert_Camera_Y ());
	return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateYBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectRotateYBack ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Y
		int rotation = (pgraphic_view->GetObjectRotation () ^ (CGraphicView::RotateYBack));
		rotation &= ~CGraphicView::RotateY;
		pgraphic_view->RotateObject ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateZBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectRotateZBack ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Z
		int rotation = (pgraphic_view->GetObjectRotation () ^ (CGraphicView::RotateZBack));
		rotation &= ~CGraphicView::RotateZ;
		pgraphic_view->RotateObject ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnObjectRotateZBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightRotateY ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Y
		int rotation = (pgraphic_view->Get_Light_Rotation () ^ (CGraphicView::RotateY));
		rotation &= ~CGraphicView::RotateYBack;
		pgraphic_view->Rotate_Light ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLightRotateYBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightRotateYBack ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Y
		int rotation = (pgraphic_view->Get_Light_Rotation () ^ (CGraphicView::RotateYBack));
		rotation &= ~CGraphicView::RotateY;
		pgraphic_view->Rotate_Light ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLightRotateZ
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightRotateZ ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Z
		int rotation = (pgraphic_view->Get_Light_Rotation () ^ (CGraphicView::RotateZ));
		rotation &= ~CGraphicView::RotateZBack;
		pgraphic_view->Rotate_Light ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLightRotateZBack
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLightRotateZBack ()
{
	CGraphicView *pgraphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	ASSERT (pgraphic_view != nullptr);
	if (pgraphic_view != nullptr) {

		// Start or stop the rotation around Y
		int rotation = (pgraphic_view->Get_Light_Rotation () ^ (CGraphicView::RotateZBack));
		rotation &= ~CGraphicView::RotateZ;
		pgraphic_view->Rotate_Light ((CGraphicView::OBJECT_ROTATION)rotation);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDestroy
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnDestroy ()
{
	W3DViewMcpBridge::Detach(m_hWnd);

	CRect rect;
	WINDOWPLACEMENT wnd_info = { sizeof (WINDOWPLACEMENT), 0 };
	::GetWindowPlacement (m_hWnd, &wnd_info);
	::GetWindowRect (m_hWnd, &rect);

	//
	// Cache this information in the registry
	//
	theApp.WriteProfileInt ("Window", "Left", rect.left);
	theApp.WriteProfileInt ("Window", "Right", rect.right);
	theApp.WriteProfileInt ("Window", "Top", rect.top);
	theApp.WriteProfileInt ("Window", "Bottom", rect.bottom);
	theApp.WriteProfileInt ("Window", "Maximized", int(wnd_info.showCmd == SW_SHOWMAXIMIZED));

	// Cache this information in the registry
	theApp.WriteProfileInt ("Config",
									"AnimateCamera",
									(int)((CW3DViewDoc *)GetActiveDocument ())->Is_Camera_Animated ());

	// Cache this information in the registry
	theApp.WriteProfileInt ("Config",
									"ResetCamera",
									(int)((CW3DViewDoc *)GetActiveDocument ())->Is_Camera_Auto_Reset_On ());

	CFrameWnd::OnDestroy ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDecLight
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnDecLight ()
{
	CW3DViewDoc *pdoc = ::GetCurrentDocument ();
	LightClass *plight = pdoc->GetSceneLight ();
	if (plight != nullptr) {

		// Get the current light settings
		Vector3 diffuse;
		Vector3 specular;
		plight->Get_Diffuse (&diffuse);
		plight->Get_Specular (&specular);

		// Decrement the intensity and pass it back to the light
		Adjust_Light_Intensity (diffuse, -0.05F);
		Adjust_Light_Intensity (specular, -0.05F);
		plight->Set_Diffuse (diffuse);
		plight->Set_Specular (specular);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnIncLight
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnIncLight ()
{
	CW3DViewDoc *pdoc = ::GetCurrentDocument ();
	LightClass *plight = pdoc->GetSceneLight ();
	if (plight != nullptr) {

		// Get the current light settings
		Vector3 diffuse;
		Vector3 specular;
		plight->Get_Diffuse (&diffuse);
		plight->Get_Specular (&specular);

		// Increment the intensity and pass it back to the light
		Adjust_Light_Intensity (diffuse, 0.05F);
		Adjust_Light_Intensity (specular, 0.05F);
		plight->Set_Diffuse (diffuse);
		plight->Set_Specular (specular);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDecAmbientLight
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnDecAmbientLight ()
{
	CW3DViewDoc *pdoc = ::GetCurrentDocument ();
	if (pdoc->GetScene () != nullptr) {

		// Get the current ambient light settings
		Vector3 color = pdoc->GetScene ()->Get_Ambient_Light ();

		// Decrement the intensity and pass it back to the light
		Adjust_Light_Intensity (color, -0.05F);
		pdoc->GetScene ()->Set_Ambient_Light (color);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnIncAmbientLight
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnIncAmbientLight ()
{
	CW3DViewDoc *pdoc = ::GetCurrentDocument ();
	if (pdoc->GetScene () != nullptr) {

		// Get the current ambient light settings
		Vector3 color = pdoc->GetScene ()->Get_Ambient_Light ();

		// Increment the intensity and pass it back to the light
		Adjust_Light_Intensity (color, 0.05F);
		pdoc->GetScene ()->Set_Ambient_Light (color);
	}
}

void CMainFrame::OnLightingExpose()
{
	// Toggle.
	WW3D::Expose_Prelit (!WW3D::Expose_Prelit());
}

void CMainFrame::OnUpdateLightingExpose (CCmdUI *pcmdui)
{
	pcmdui->SetCheck (WW3D::Expose_Prelit());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMakeAggregate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnMakeAggregate ()
{
	// Show the name dialog to the user
	AggregateNameDialogClass dialog (this);
	if (dialog.DoModal () == IDOK) {

		CDataTreeView *pdata_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
		RenderObjClass *prender_obj = ::GetCurrentDocument ()->GetDisplayedObject ();
		if (prender_obj != nullptr) {

			// Build a definition object from the hierarchy
			AggregateDefClass *pdefinition = new AggregateDefClass (*prender_obj);
			AggregatePrototypeClass *pprototype = new AggregatePrototypeClass (pdefinition);
			pdefinition->Set_Name (dialog.Get_Name ());

			// Add this prototype to the asset manager
			WW3DAssetManager::Get_Instance ()->Remove_Prototype (dialog.Get_Name ());
			WW3DAssetManager::Get_Instance ()->Add_Prototype (pprototype);

			// Add an entry for this aggregate in the tree control
			pdata_tree->Add_Asset_To_Tree (dialog.Get_Name (), TypeAggregate, true);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnRenameAggregate
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnRenameAggregate ()
{
	// Get a pointer to the current aggregate
	RenderObjClass *prender_obj = (::GetCurrentDocument ())->GetDisplayedObject ();
	if (prender_obj != nullptr) {

		// Show the rename dialog to the user
		const char *old_name = prender_obj->Get_Name ();
		AggregateNameDialogClass dialog (IDD_RENAME_AGGREGATE, prender_obj->Get_Name (), this);
		if (dialog.DoModal () == IDOK) {

			// Rename the prototype
			::Rename_Aggregate_Prototype (old_name, dialog.Get_Name ());

			// Refresh the UI
			CDataTreeView *pdata_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
			pdata_tree->Refresh_Asset (dialog.Get_Name (), old_name, TypeAggregate);

			// Now let the actual displayed render object know its new name...
			prender_obj->Set_Name (dialog.Get_Name ());
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCommand
//
////////////////////////////////////////////////////////////////////////////
BOOL
CMainFrame::OnCommand
(
	WPARAM wParam,
	LPARAM lParam
)
{
	if ((lParam == 0) &&
		 (LOWORD (wParam) >= 1000) &&
		 (LOWORD (wParam) < 1100)) {
		MENUITEMINFO info = { sizeof (MENUITEMINFO), MIIM_DATA | MIIM_TYPE, 0 };
		TCHAR emitter_name[200];
		info.dwTypeData = emitter_name;
		info.cch = sizeof (emitter_name);
		if (::GetMenuItemInfo (m_hEmittersSubMenu, LOWORD (wParam), FALSE, &info)) {

			//
			// Make a list of emitters with the given name
			//
			EmitterInstanceListClass *instance_list = new EmitterInstanceListClass;
			::GetCurrentDocument ()->Build_Emitter_List (instance_list, emitter_name);

			//
			// Show the emitter property sheet
			//
			EmitterPropertySheetClass prop_sheet (instance_list, IDS_EMITTER_PROP_TITLE, this);
			prop_sheet.DoModal ();
		}
	}

	// Allow the base class to process this message
	return CFrameWnd::OnCommand (wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCmdMsg
//
////////////////////////////////////////////////////////////////////////////
BOOL
CMainFrame::OnCmdMsg
(
	UINT nID,
	int nCode,
	void *pExtra,
	AFX_CMDHANDLERINFO* pHandlerInfo
)
{
	// Hack to get MFC to enable the 'Editable Emitters List' submenu...
	if (nCode == CN_UPDATE_COMMAND_UI) {
		CCmdUI *pCmdUI = (CCmdUI *)pExtra;
		if (pCmdUI != nullptr && (pCmdUI->m_nID >= 1000) && (pCmdUI->m_nID < 1100)) {
			pCmdUI->Enable (TRUE);
			return TRUE;
		}
	}

	// Allow the base class to process this message
	return CFrameWnd::OnCmdMsg (nID, nCode, pExtra, pHandlerInfo);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnTreeFilterChanged
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnTreeFilterChanged ()
{
	// The box exists before the panes do
	if (!::IsWindow (m_wndSplitter.m_hWnd) || !::IsWindow (m_TreeFilterEdit.m_hWnd))
	{
		return;
	}

	CDataTreeView *tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
	if (tree != nullptr)
	{
		CString text;
		m_TreeFilterEdit.GetWindowText (text);
		tree->Request_Filter (text);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  PreTranslateMessage
//
////////////////////////////////////////////////////////////////////////////
BOOL
CMainFrame::PreTranslateMessage (MSG *pMsg)
{
	// Keys typed into the filter box are text. The accelerators would take the digits and arrows.
	if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
		 m_TreeFilterEdit.m_hWnd != nullptr && pMsg->hwnd == m_TreeFilterEdit.m_hWnd)
	{
		if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE))
		{
			// Escape clears the filter; both hand the keyboard back to the tree
			if (pMsg->wParam == VK_ESCAPE)
			{
				m_TreeFilterEdit.SetWindowText ("");
			}
			CWnd *tree = ::IsWindow (m_wndSplitter.m_hWnd) ? m_wndSplitter.GetPane (0, 0) : nullptr;
			if (tree != nullptr)
			{
				tree->SetFocus ();
			}
			return TRUE;
		}

		::TranslateMessage (pMsg);
		::DispatchMessage (pMsg);
		return TRUE;
	}

	// 7 toggles the poly and vertex totals. Keys from other windows pass through here too.
	if (::GetActiveWindow () == m_hWnd && ViewportStats::Is_Toggle_Key (pMsg))
	{
		ViewportStats::Toggle ();
		return TRUE;
	}

	return CFrameWnd::PreTranslateMessage (pMsg);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCrashApp
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCrashApp ()
{
	// Usefull HACK to get the program to crash when needed...
	LPTSTR hack = nullptr;
	(*hack) = 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLODRecordScreenArea
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLODRecordScreenArea ()
{
	// Make sure the current object is an LOD
	RenderObjClass *prender_obj = ::GetCurrentDocument ()->GetDisplayedObject ();
	if ((prender_obj != nullptr) &&
		 (prender_obj->Class_ID () == RenderObjClass::CLASSID_HLOD)) {

		CGraphicView *graphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);

		//
		//	TODO - Calculate the real screen size of the LOD and pass it onto
		// the object.
		//
		CameraClass *pcamera = graphic_view->GetCamera ();
		float screen_size = prender_obj->Get_Screen_Size (*pcamera);

		// Let the LOD know what its new clamp is.
		int index = ((HLodClass *)prender_obj)->Get_LOD_Level ();
		((HLodClass *)prender_obj)->Set_Max_Screen_Size (index, screen_size);

		// Update the prototype for this lod to reflect the changes
		::GetCurrentDocument ()->Update_LOD_Prototype (*((HLodClass *)prender_obj));
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLODIncludeNull
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLODIncludeNull ()
{
	// Make sure the current object is an LOD
	RenderObjClass *prender_obj = ::GetCurrentDocument ()->GetDisplayedObject ();
	if ((prender_obj != nullptr) &&
		 (prender_obj->Class_ID () == RenderObjClass::CLASSID_HLOD)) {

		// Toggle the nullptr lod
		bool include = ((HLodClass *)prender_obj)->Is_NULL_Lod_Included ();
		((HLodClass *)prender_obj)->Include_NULL_Lod (!include);

		// Update the prototype for this lod to reflect the changes
		::GetCurrentDocument ()->Update_LOD_Prototype (*((HLodClass *)prender_obj));
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateLODIncludeNull
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateLODIncludeNull (CCmdUI *pCmdUI)
{
	// Make sure the current object is an LOD
	RenderObjClass *prender_obj = (::GetCurrentDocument ())->GetDisplayedObject ();
	if ((prender_obj != nullptr) &&
		 (prender_obj->Class_ID () == RenderObjClass::CLASSID_HLOD)) {

		// Check or uncheck the menu option depending on the state of the LOD
		bool check = ((HLodClass *)prender_obj)->Is_NULL_Lod_Included ();
		pCmdUI->SetCheck (check);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLodPrevLevel
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLodPrevLevel ()
{
	::GetCurrentDocument ()->Switch_LOD (-1);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateLodPrevLevel
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateLodPrevLevel (CCmdUI *pCmdUI)
{
	// Make sure the current object is an LOD
	RenderObjClass *prender_obj = (::GetCurrentDocument ())->GetDisplayedObject ();
	if ((prender_obj != nullptr) &&
		 (prender_obj->Class_ID () == RenderObjClass::CLASSID_HLOD)) {

		// Enable the menu option if there is a previous lod to display
		int current_lod = ((HLodClass *)prender_obj)->Get_LOD_Level ();
		pCmdUI->Enable (current_lod > 0);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLodNextLevel
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLodNextLevel ()
{
	::GetCurrentDocument ()->Switch_LOD (1);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateLodNextLevel
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateLodNextLevel (CCmdUI *pCmdUI)
{
	// Make sure the current object is an LOD
	RenderObjClass *prender_obj = (::GetCurrentDocument ())->GetDisplayedObject ();
	if ((prender_obj != nullptr) &&
		 (prender_obj->Class_ID () == RenderObjClass::CLASSID_HLOD)) {

		// Enable the menu option if there is another lod to display
		int current_lod = ((HLodClass *)prender_obj)->Get_LOD_Level ();
		int lod_count = ((HLodClass *)prender_obj)->Get_LOD_Count ();
		pCmdUI->Enable ((current_lod + 1) < lod_count);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLodAutoswitch
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnLodAutoswitch ()
{
	// Toggle the autoswitch setting
	ViewerSceneClass *pscene = ::GetCurrentDocument ()->GetScene ();
	pscene->Allow_LOD_Switching (!pscene->Are_LODs_Switching ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateLodAutoswitch
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateLodAutoswitch (CCmdUI *pCmdUI)
{
	ViewerSceneClass *pscene = ::GetCurrentDocument ()->GetScene ();
	pCmdUI->SetCheck (pscene->Are_LODs_Switching ());
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateMakeMovie
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateMakeMovie (CCmdUI *pCmdUI)
{
	// Enable the 'movie' option if this is an animation
	CDataTreeView *pCDataTreeView = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);

	int atype = pCDataTreeView->GetCurrentSelectionType();
	bool enabled = ((atype == TypeAnimation) || (atype == TypeCompressedAnimation));

	pCmdUI->Enable ( enabled );
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMakeMovie
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnMakeMovie ()
{
	// Force a resolution change
	//WW3D::Set_Resolution (800, 600, g_iBitsPerPixel, 0);

	::GetCurrentDocument ()->Make_Movie ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSaveScreenshot
//
// TheSuperHackers @bugfix Tria 19/04/2026 Use currently selected object name as the screenshot
// filename and prompt for a save location with a folder dialog.
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSaveScreenshot ()
{
	// Determine default filename from the currently displayed object
	CString defaultName ("ScreenShot");
	CW3DViewDoc *pdoc = GetCurrentDocument ();
	if (pdoc != nullptr) {
		RenderObjClass *prender_obj = pdoc->GetDisplayedObject ();
		if (prender_obj != nullptr && prender_obj->Get_Name () != nullptr) {
			defaultName = prender_obj->Get_Name ();
		}
	}

	// Prompt user for save location
	CFileDialog dlg (FALSE, "tga", defaultName,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER,
		"TGA Files (*.tga)|*.tga||", this);
	dlg.m_ofn.lpstrTitle = "Screenshot...";

	if (dlg.DoModal () != IDOK) {
		return;
	}

	CString savePath = dlg.GetPathName ();

	// Strip the extension — Make_Screen_Shot appends its own numbering and extension
	int dotPos = savePath.ReverseFind ('.');
	if (dotPos >= 0) {
		savePath = savePath.Left (dotPos);
	}

	//
	// Take the actual screen shot
	//
	bool cursor_shown = GetCurrentDocument ()->Is_Cursor_Shown ();
	GetCurrentDocument ()->Show_Cursor (false);
	Get_Graphic_View ()->RepaintView ();
	WW3D::Make_Screen_Shot ((LPCTSTR)savePath);
	GetCurrentDocument ()->Show_Cursor (cursor_shown);
}


////////////////////////////////////////////////////////////////////////////
//
//  Update_Emitters_List
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::Update_Emitters_List ()
{
	::EnableMenuItem (::GetSubMenu (::GetMenu (m_hWnd), 3), 3, MF_BYPOSITION | MF_ENABLED);
	HMENU hsub_menu = Get_Emitters_List_Menu ();
	int index = 0;
	while (::RemoveMenu (hsub_menu, 0, MF_BYPOSITION)) {
		//index ++;
	}
	RenderObjClass *prender_obj = GetCurrentDocument ()->GetDisplayedObject ();

	if (prender_obj != nullptr) {
		DynamicVectorClass<CString> list;
		Build_Emitter_List (*prender_obj, list);

		for (int index = 0; index < list.Count (); index ++) {
			MENUITEMINFO info = { sizeof (MENUITEMINFO), 0 };
			info.fMask = MIIM_TYPE | MIIM_DATA | MIIM_ID;
			info.fType = MFT_STRING;
			info.dwTypeData = (char *)(LPCTSTR)list[index];
			info.wID = 1000 + index;
			::InsertMenuItem (hsub_menu, index, TRUE, &info);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSlideshowDown
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSlideshowDown ()
{
	CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
	if (data_tree != nullptr) {
		data_tree->Select_Next ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSlideshowUp
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSlideshowUp ()
{
	CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
	if (data_tree != nullptr) {
		data_tree->Select_Prev ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnAdvancedAnim
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAdvancedAnim()
{
	// Display the advanced animation control dialog and render the object
	// with the mix of animations the user specified.
	CAdvancedAnimSheet	dlg;
	dlg.DoModal();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateAdvancedAnim
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateAdvancedAnim(CCmdUI* pCmdUI)
{
	// Enable the menu item if the selected hierarchy has at least one
	// animation we can apply.
	RenderObjClass *prender_obj = ::GetCurrentDocument()->GetDisplayedObject();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCameraSettings
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraSettings ()
{
	CameraSettingsDialogClass dialog (this);
	dialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCopyScreenSize
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCopyScreenSize ()
{
	//
	//	Determine the current screen size of the displayed object
	//
	CGraphicView *graphic_view = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	CameraClass *camera			= graphic_view->GetCamera ();
	RenderObjClass *render_obj	= ::GetCurrentDocument ()->GetDisplayedObject ();
	float screen_size				= render_obj->Get_Screen_Size (*camera);

	//
	//	Convert the float to a string
	//
	CString size_string;
	size_string.Format ("MaxScreenSize=%f", screen_size);
	int len = size_string.GetLength () + 1;

	//
	//	Allocate a chunk of clipboard-safe memory, and
	// copy the string to it.
	//
	HGLOBAL global_mem	= ::GlobalAlloc (GHND, len);
	LPTSTR global_ptr		= (LPTSTR)::GlobalLock (global_mem);
	::memcpy (global_ptr, size_string, len);
	::GlobalUnlock (global_mem);

	//
	//	Copy the string to the clipboard
	//
	OpenClipboard ();
	::EmptyClipboard ();
	::SetClipboardData (CF_TEXT, global_mem);
	CloseClipboard ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnListMissingTextures
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnListMissingTextures ()
{
	//
	//	Get the list of missing textures and preset it to the user
	//
	// Walk all loaded textures and collect those that resolved to the missing-texture fallback.
	// Done at button-press time so async loading has already completed.
	CString message;
	HashTemplateIterator<StringClass, TextureClass*> it(_TheAssetMgr->Texture_Hash());
	for (; !it.Is_Done(); it.Next()) {
		TextureClass* tex = it.Peek_Value();
		if (tex != nullptr && tex->Is_Missing_Texture()) {
			message += it.Peek_Key().str();
			message += "\r\n";
		}
	}

	if (!message.IsEmpty()) {
		LogDialog dlg(IDD_MISSING_TEXTURES_LOG, message);
		dlg.DoModal();
	} else {
		::MessageBox (::AfxGetMainWnd ()->m_hWnd, "No Missing Textures!", "Texture Info", MB_ICONINFORMATION | MB_OK);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCopyAssets
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCopyAssets ()
{
	CString path;
	if (::Browse_For_Folder (m_hWnd, nullptr, path)) {

		//
		//	Copy all dependent asset files to the selected directory
		//
		CW3DViewDoc *doc = ::GetCurrentDocument ();
		if (doc != nullptr) {
			doc->Copy_Assets_To_Dir (path);
			CString msg;
			msg.Format ("Assets successfully copied to:\n%s", (LPCTSTR)path);
			::AfxMessageBox (msg, MB_ICONINFORMATION);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCopyAssets
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCopyAssets (CCmdUI *pCmdUI)
{
	CW3DViewDoc *doc = ::GetCurrentDocument ();
	if (doc != nullptr) {

		//
		//	Only enable this option if we are viewing an object
		//
		pCmdUI->Enable (doc->GetDisplayedObject () != nullptr);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnTexturePath
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnTexturePath ()
{
	TexturePathDialogClass dialog (this);
	dialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//	OnChangeResolution
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnChangeResolution ()
{
#ifdef WW3D_DX8
	ResolutionDialogClass dialog (this);
	dialog.DoModal ();
#else
	::MessageBox(m_hWnd,"Feature removed during conversion to DX8.","Unsupported Feature",MB_OK|MB_ICONEXCLAMATION);
#endif
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreateSphere
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCreateSphere ()
{
	// Clear the current display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc) {
		doc->DisplayObject ((RenderObjClass *)nullptr);
	}

	//
	// Display the sphere property sheet
	//
	SpherePropertySheetClass dialog (nullptr, IDS_SPHERE_PROP_TITLE, this);
	dialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreateRing
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCreateRing ()
{
	// Clear the current display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc) {
		doc->DisplayObject ((RenderObjClass *)nullptr);
	}

	//
	// Display the ring property sheet
	//
	RingPropertySheetClass dialog (nullptr, IDS_RING_PROP_TITLE, this);
	dialog.DoModal ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnEditPrimitive
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnEditPrimitive ()
{
	// Get a pointer to the doc object
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc != nullptr) {

		//
		// Make a list of emitters containing the currently displayed emitter
		//
		RenderObjClass *render_obj = doc->GetDisplayedObject ();

		if (render_obj != nullptr) {
			if (render_obj->Class_ID () == RenderObjClass::CLASSID_SPHERE) {

				//
				// Display the sphere property sheet
				//
				SpherePropertySheetClass dialog ((SphereRenderObjClass *)render_obj, IDS_SPHERE_PROP_TITLE, this);
				dialog.DoModal ();
			} else if (render_obj->Class_ID () == RenderObjClass::CLASSID_RING) {

				//
				// Display the ring property sheet
				//
				RingPropertySheetClass dialog ((RingRenderObjClass *)render_obj, IDS_RING_PROP_TITLE, this);
				dialog.DoModal ();
			}
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateEditPrimitive
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateEditPrimitive (CCmdUI *pCmdUI)
{
	CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
	if (data_tree != nullptr && data_tree->GetCurrentSelectionType () == TypePrimitives) {
		pCmdUI->Enable (true);
	} else {
		pCmdUI->Enable (false);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnExportPrimitive
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnExportPrimitive ()
{
	((CW3DViewDoc *)GetActiveDocument ())->Save_Selected_Primitive ();
}


////////////////////////////////////////////////////////////////////////////
//
//  OnUpdateExportPrimitive
//
////////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateExportPrimitive (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypePrimitives);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnKillSceneLight
//
////////////////////////////////////////////////////////////////////////////
void CMainFrame::OnKillSceneLight()
{
	CW3DViewDoc *pdoc	  = ::GetCurrentDocument();
	LightClass	*plight = pdoc->GetSceneLight ();

	if (plight != nullptr) {

		const Vector3 black (0.0f, 0.0f, 0.0f);

		plight->Set_Diffuse (black);
		plight->Set_Specular (black);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnPrelitMultipass
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnPrelitMultipass ()
{
	if (WW3D::Get_Prelit_Mode () != WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS) {

		//
		//	Change the loading mode
		//
		WW3D::Set_Prelit_Mode (WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS);

		//
		//	Reload the lightmap models
		//
		CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
		data_tree->Reload_Lightmap_Models ();
		::GetCurrentDocument ()->Reload_Displayed_Object ();
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdatePrelitMultipass
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdatePrelitMultipass (CCmdUI *pCmdUI)
{
	bool enable = (WW3D::Get_Prelit_Mode () == WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS);
	pCmdUI->SetCheck (enable);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnPrelitMultitex
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnPrelitMultitex ()
{
	if (WW3D::Get_Prelit_Mode () != WW3D::PRELIT_MODE_LIGHTMAP_MULTI_TEXTURE) {

		//
		//	Change the loading mode
		//
		WW3D::Set_Prelit_Mode (WW3D::PRELIT_MODE_LIGHTMAP_MULTI_TEXTURE);

		//
		//	Reload the lightmap models
		//
		CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
		data_tree->Reload_Lightmap_Models ();
		::GetCurrentDocument ()->Reload_Displayed_Object ();
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdatePrelitMultitex
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdatePrelitMultitex (CCmdUI *pCmdUI)
{
	bool enable = (WW3D::Get_Prelit_Mode () == WW3D::PRELIT_MODE_LIGHTMAP_MULTI_TEXTURE);
	pCmdUI->SetCheck (enable);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnPrelitVertex
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnPrelitVertex ()
{
	if (WW3D::Get_Prelit_Mode () != WW3D::PRELIT_MODE_VERTEX) {

		//
		//	Change the loading mode
		//
		WW3D::Set_Prelit_Mode (WW3D::PRELIT_MODE_VERTEX);


		//
		//	Reload the lightmap models
		//
		CDataTreeView *data_tree = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
		data_tree->Reload_Lightmap_Models ();
		::GetCurrentDocument ()->Reload_Displayed_Object ();
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdatePrelitVertex
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdatePrelitVertex (CCmdUI *pCmdUI)
{
	bool enable = (WW3D::Get_Prelit_Mode () == WW3D::PRELIT_MODE_VERTEX);
	pCmdUI->SetCheck (enable);
}



//////////////////////////////////////////////////////////////////////////
//
//  OnAddToLineup
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnAddToLineup ()
{
	// Display a dialog that will let the user choose a render object
	// to add to the current scene. The reason I call it a lineup is
	// that the objects we add in this manner are stacked in a horizontal
	// row, just like a lineup.
	CW3DViewDoc *pDoc = (CW3DViewDoc*)GetActiveDocument();
	ViewerSceneClass *pScene = nullptr;
	if (pDoc)
		pScene = pDoc->GetScene();
	CAddToLineupDialog dlg(pScene, this);
	if (dlg.DoModal() == IDOK)
	{
		// Create the named render object.
		RenderObjClass *obj = WW3DAssetManager::Get_Instance()->Create_Render_Obj(dlg.m_Object);
		if (obj)
		{
			// Set the render object to it's highest LOD.
			Set_Highest_LOD(obj);

			// Add the object to the scene's lineup.
			pScene->Add_To_Lineup(obj);
		}
		else
		{
			// Tell the user that the render object could not be created.
			CString msg;
			msg.Format("Unable to create render object '%s'!", static_cast<const char*>(dlg.m_Object));
			::AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateAddToLineup
//
//////////////////////////////////////////////////////////////////////////
void CMainFrame::OnUpdateAddToLineup(CCmdUI* pCmdUI)
{
	bool enable = false;

	// The button should only be enabled if there is a valid scene,
	// and there is a valid current render object, and that render
	// object is an object that can be lined up.
	CW3DViewDoc *pDoc = (CW3DViewDoc*)GetActiveDocument();
	if (pDoc)
	{
		ViewerSceneClass *pScene = pDoc->GetScene();
		RenderObjClass *pObj = pDoc->GetDisplayedObject();
		if (pScene && pObj)
		{
			if (pScene->Can_Line_Up(pObj))
				enable = true;
		}
	}

	pCmdUI->Enable(enable);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnImportFacialAnims
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnImportFacialAnims ()
{
	//
	// Look up the currently selected hierarchy
	//
	CW3DViewDoc *doc			= ::GetCurrentDocument ();
	const HTreeClass *htree = 	doc->Get_Current_HTree ();
	ASSERT (htree != nullptr);
	if (htree != nullptr) {

		CFileDialog dialog (	TRUE,
									".txt",
									nullptr,
									OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_ALLOWMULTISELECT | OFN_EXPLORER,
									"Animation Description (*.txt)|*.txt||",
									this);

		TCHAR filename_list[MAX_PATH * 20] = { 0 };
		dialog.m_ofn.lpstrFile	= filename_list;
		dialog.m_ofn.nMaxFile	= sizeof (filename_list);

		//
		// Ask the user what files they want to import
		//
		if (dialog.DoModal () == IDOK) {
			CWaitCursor wait_cursor;


			//
			// Loop over all the selected files
			//
			POSITION pos = dialog.GetStartPosition ();
			while (pos != nullptr) {

				// Ask the doc to load the assets from this file into memory
				CString filename = dialog.GetNextPathName (pos);
				doc->Import_Facial_Animation (htree->Get_Name (), filename);
			}

			//
			// Re-load the data list to include all new assets
			//
			CDataTreeView *data_tree = doc->GetDataTreeView ();
			if (data_tree != nullptr) {
				data_tree->LoadAssetsIntoTree ();
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateImportFacialAnims
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateImportFacialAnims (CCmdUI *pCmdUI)
{
	CW3DViewDoc *doc = ::GetCurrentDocument ();
	if (doc != nullptr) {

		//
		// Enable this command only if the user has an htree
		// currently selected
		//
		const HTreeClass *htree = doc->Get_Current_HTree ();
		pCmdUI->Enable (htree != nullptr);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnRestrictAnims
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnRestrictAnims ()
{
	CDataTreeView *data_tree = ::GetCurrentDocument ()->GetDataTreeView ();
	if (data_tree != nullptr) {
		bool enabled = data_tree->Are_Anims_Restricted ();
		data_tree->Restrict_Anims (!enabled);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateRestrictAnims
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateRestrictAnims (CCmdUI *pCmdUI)
{
	bool check = true;

	CDataTreeView *data_tree = ::GetCurrentDocument ()->GetDataTreeView ();
	if (data_tree != nullptr) {
		check = data_tree->Are_Anims_Restricted ();
	}

	pCmdUI->SetCheck (check);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnBindSubobjectLod
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnBindSubobjectLod ()
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc != nullptr && doc->GetDisplayedObject () != nullptr) {

		//
		//	Toggle the state of the currently displayed object
		//
		RenderObjClass *render_obj = doc->GetDisplayedObject ();
		bool is_enabled = (render_obj->Is_Sub_Objects_Match_LOD_Enabled () != 0);
		render_obj->Set_Sub_Objects_Match_LOD (!is_enabled);
		doc->Update_Aggregate_Prototype (*render_obj);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateBindSubobjectLod
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateBindSubobjectLod (CCmdUI *pCmdUI)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc != nullptr && doc->GetDisplayedObject () != nullptr) {

		//
		//	Set the check if we are currently forcing sub object matching
		//
		RenderObjClass *render_obj = doc->GetDisplayedObject ();
		bool is_enabled = (render_obj->Is_Sub_Objects_Match_LOD_Enabled () != 0);
		pCmdUI->SetCheck (is_enabled);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnSetCameraDistance
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnSetCameraDistance ()
{
	CameraDistanceDialogClass dialog (this);
	dialog.DoModal ();
}


//////////////////////////////////////////////////////////////////////////
//
//  OnObjectAlternateMaterials
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnObjectAlternateMaterials ()
{
	::GetCurrentDocument ()->Toggle_Alternate_Materials ();
}


//////////////////////////////////////////////////////////////////////////
//
//  OnCreateSoundObject
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCreateSoundObject ()
{
	SoundEditDialogClass dialog (this);
	dialog.DoModal ();
}


//////////////////////////////////////////////////////////////////////////
//
//  OnEditSoundObject
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnEditSoundObject ()
{
	//
	// Get a pointer to the doc object
	//
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument ();
	if (doc != nullptr) {

		//
		//	Get a pointer to the currently displayed sound object
		//
		SoundRenderObjClass *sound_obj = (SoundRenderObjClass *)doc->GetDisplayedObject ();
		if (sound_obj != nullptr) {

			//
			//	Display the sound edit dialog
			//
			SoundEditDialogClass dialog (this);
			dialog.Set_Sound (sound_obj);
			dialog.DoModal ();
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateEditSoundObject
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateEditSoundObject (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeSound);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnExportSoundObj
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnExportSoundObj ()
{
	((CW3DViewDoc *)GetActiveDocument ())->Save_Selected_Sound_Object ();
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateExportSoundObj
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateExportSoundObj (CCmdUI *pCmdUI)
{
	pCmdUI->Enable (m_currentAssetType == TypeSound);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnWireframeMode
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnWireframeMode ()
{
	ViewerSceneClass *scene = ::GetCurrentDocument ()->GetScene ();

	bool enable = (scene->Get_Polygon_Mode () != SceneClass::LINE);
	scene->Set_Polygon_Mode (enable ? SceneClass::LINE : SceneClass::FILL);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateWireframeMode
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateWireframeMode (CCmdUI *pCmdUI)
{
	ViewerSceneClass *scene = ::GetCurrentDocument ()->GetScene ();
	pCmdUI->SetCheck (scene->Get_Polygon_Mode () == SceneClass::LINE);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnToggleSorting
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnToggleSorting()
{
	// Toggle the polygon sorting state.
	bool sorting=!WW3D::Is_Sorting_Enabled();
	WW3D::_Invalidate_Mesh_Cache();
	WW3D::Enable_Sorting(sorting);
	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "EnableSorting",sorting?1:0);
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateToggleSorting
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateToggleSorting(CCmdUI* pCmdUI)
{
	// Check the menu item if sorting is enabled, clear it otherwise.
	pCmdUI->SetCheck(WW3D::Is_Sorting_Enabled() ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
//
//  OnToggleAlpha
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnToggleAlpha()
{
	// TheSuperHackers @feature xezon 17/04/2026 Toggle alpha blending to preview texture transparency.
	bool alpha = !ShaderClass::Is_Alpha_Blending_Forced();
	ShaderClass::Force_Alpha_Blending(alpha);
	::AfxGetApp()->WriteProfileInt("Config", "ForceAlphaBlend", alpha ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateToggleAlpha
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateToggleAlpha(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Is_Alpha_Blending_Forced() ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
//
//  Texture Filtering handlers
//
// TheSuperHackers @feature Tria 17/04/2026 Add texture filtering options to View menu.
//////////////////////////////////////////////////////////////////////////

static void SetTextureFilter(int filter, int aniso = 0)
{
	WW3D::Set_Texture_Filter(filter);
	if (filter == TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC && aniso > 0) {
		TextureFilterClass::_Set_Max_Anisotropy((TextureFilterClass::AnisotropicFilterMode)aniso);
	}
	s_AnisotropicLevel = aniso;
	::AfxGetApp()->WriteProfileInt("Config", "TextureFilter", filter);
	::AfxGetApp()->WriteProfileInt("Config", "AnisotropicLevel", aniso);
}

static bool IsTextureFilter(int filter, int aniso = 0)
{
	if (WW3D::Get_Texture_Filter() != filter) return false;
	if (filter == TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC && aniso > 0) {
		return s_AnisotropicLevel == aniso;
	}
	return true;
}

void CMainFrame::OnFilterPoint()    { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_POINT); }
void CMainFrame::OnFilterBilinear() { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_BILINEAR); }
void CMainFrame::OnFilterTrilinear(){ SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_TRILINEAR); }
void CMainFrame::OnFilterAniso2x()  { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 2); }
void CMainFrame::OnFilterAniso4x()  { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 4); }
void CMainFrame::OnFilterAniso8x()  { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 8); }
void CMainFrame::OnFilterAniso16x() { SetTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 16); }

void CMainFrame::OnUpdateFilterPoint(CCmdUI* pCmdUI)    { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_POINT)); }
void CMainFrame::OnUpdateFilterBilinear(CCmdUI* pCmdUI) { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_BILINEAR)); }
void CMainFrame::OnUpdateFilterTrilinear(CCmdUI* pCmdUI){ pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_TRILINEAR)); }
void CMainFrame::OnUpdateFilterAniso2x(CCmdUI* pCmdUI)  { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 2)); }
void CMainFrame::OnUpdateFilterAniso4x(CCmdUI* pCmdUI)  { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 4)); }
void CMainFrame::OnUpdateFilterAniso8x(CCmdUI* pCmdUI)  { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 8)); }
void CMainFrame::OnUpdateFilterAniso16x(CCmdUI* pCmdUI) { pCmdUI->SetCheck(IsTextureFilter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC, 16)); }

//////////////////////////////////////////////////////////////////////////
//
//  MSAA handlers
//
// TheSuperHackers @feature Tria 17/04/2026 Add MSAA options to View menu.
//////////////////////////////////////////////////////////////////////////

static void SetMSAA(D3DMULTISAMPLE_TYPE mode)
{
	WW3D::Set_MSAA_Mode((WW3D::MultiSampleModeEnum)mode);
	DX8Wrapper::Reset_Device(true);
	::AfxGetApp()->WriteProfileInt("Config", "MSAA", (int)mode);
}

void CMainFrame::OnMsaaNone() { SetMSAA(D3DMULTISAMPLE_NONE); }
void CMainFrame::OnMsaa2x()   { SetMSAA(D3DMULTISAMPLE_2_SAMPLES); }
void CMainFrame::OnMsaa4x()   { SetMSAA(D3DMULTISAMPLE_4_SAMPLES); }
void CMainFrame::OnMsaa8x()   { SetMSAA(D3DMULTISAMPLE_8_SAMPLES); }

void CMainFrame::OnUpdateMsaaNone(CCmdUI* pCmdUI) { pCmdUI->SetCheck(WW3D::Get_MSAA_Mode() == WW3D::MULTISAMPLE_MODE_NONE); }
void CMainFrame::OnUpdateMsaa2x(CCmdUI* pCmdUI)   { pCmdUI->SetCheck(WW3D::Get_MSAA_Mode() == WW3D::MULTISAMPLE_MODE_2X); }
void CMainFrame::OnUpdateMsaa4x(CCmdUI* pCmdUI)   { pCmdUI->SetCheck(WW3D::Get_MSAA_Mode() == WW3D::MULTISAMPLE_MODE_4X); }
void CMainFrame::OnUpdateMsaa8x(CCmdUI* pCmdUI)   { pCmdUI->SetCheck(WW3D::Get_MSAA_Mode() == WW3D::MULTISAMPLE_MODE_8X); }

//////////////////////////////////////////////////////////////////////////
//
//  OnCameraBonePosX
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnCameraBonePosX()
{
   CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pCGraphicView != nullptr) {
		pCGraphicView->Set_Camera_Bone_Pos_X(!pCGraphicView->Is_Camera_Bone_Pos_X());
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnUpdateCameraBonePosX
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnUpdateCameraBonePosX(CCmdUI* pCmdUI)
{
   CGraphicView *pCGraphicView = (CGraphicView *)m_wndSplitter.GetPane (0, 1);
	if (pCGraphicView != nullptr) {
		pCmdUI->SetCheck(pCGraphicView->Is_Camera_Bone_Pos_X());
	}
}


void CMainFrame::OnViewPatchGapFill()
{
	// If gap-filling is on, turn it off. Otherwise turn it on.
	if (WW3D::Get_NPatches_Gap_Filling_Mode() == WW3D::NPATCHES_GAP_FILLING_ENABLED)
	{
		WW3D::Set_NPatches_Gap_Filling_Mode(WW3D::NPATCHES_GAP_FILLING_DISABLED);
		::AfxGetApp()->WriteProfileInt("Config", "NPatchesGapFilling", 0);
	}
	else
	{
		WW3D::Set_NPatches_Gap_Filling_Mode(WW3D::NPATCHES_GAP_FILLING_ENABLED);
		::AfxGetApp()->WriteProfileInt("Config", "NPatchesGapFilling", 1);
	}
}

void CMainFrame::OnUpdateViewPatchGapFill(CCmdUI* pCmdUI)
{
	// Check the menu item if gap-filling is turned on.
	bool enabled = (WW3D::Get_NPatches_Gap_Filling_Mode() == WW3D::NPATCHES_GAP_FILLING_ENABLED);
	pCmdUI->SetCheck((int)enabled);
}

void CMainFrame::OnViewSubdivision1()
{
	// Set the N-Patches Subdivision Level to 1.
	WW3D::Set_NPatches_Level(1);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 1);
}

void CMainFrame::OnUpdateViewSubdivision1(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 1.
	bool checked = (WW3D::Get_NPatches_Level() == 1);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision2()
{
	// Set the N-Patches Subdivision Level to 2.
	WW3D::Set_NPatches_Level(2);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 2);
}

void CMainFrame::OnUpdateViewSubdivision2(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 2.
	bool checked = (WW3D::Get_NPatches_Level() == 2);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision3()
{
	// Set the N-Patches Subdivision Level to 3.
	WW3D::Set_NPatches_Level(3);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 3);
}

void CMainFrame::OnUpdateViewSubdivision3(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 3.
	bool checked = (WW3D::Get_NPatches_Level() == 3);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision4()
{
	// Set the N-Patches Subdivision Level to 4.
	WW3D::Set_NPatches_Level(4);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 4);
}

void CMainFrame::OnUpdateViewSubdivision4(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 4.
	bool checked = (WW3D::Get_NPatches_Level() == 4);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision5()
{
	// Set the N-Patches Subdivision Level to 5.
	WW3D::Set_NPatches_Level(5);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 5);
}

void CMainFrame::OnUpdateViewSubdivision5(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 5.
	bool checked = (WW3D::Get_NPatches_Level() == 5);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision6()
{
	// Set the N-Patches Subdivision Level to 6.
	WW3D::Set_NPatches_Level(6);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 6);
}

void CMainFrame::OnUpdateViewSubdivision6(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 6.
	bool checked = (WW3D::Get_NPatches_Level() == 6);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision7()
{
	// Set the N-Patches Subdivision Level to 7.
	WW3D::Set_NPatches_Level(7);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 7);
}

void CMainFrame::OnUpdateViewSubdivision7(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 7.
	bool checked = (WW3D::Get_NPatches_Level() == 7);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnViewSubdivision8()
{
	// Set the N-Patches Subdivision Level to 8.
	WW3D::Set_NPatches_Level(8);

	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "NPatchesSubdivision", 8);
}

void CMainFrame::OnUpdateViewSubdivision8(CCmdUI* pCmdUI)
{
	// Check the menu item if the current N-Patch Subdivision Level is 8.
	bool checked = (WW3D::Get_NPatches_Level() == 8);
	pCmdUI->SetCheck((int)checked);
}

void CMainFrame::OnMungeSortOnLoad()
{
	bool setting=!WW3D::Is_Munge_Sort_On_Load_Enabled();
	WW3D::Enable_Munge_Sort_On_Load(setting);
	// Save the new value in the registry.
	::AfxGetApp()->WriteProfileInt("Config", "MungeSortOnLoad", setting?1:0);
}

void CMainFrame::OnUpdateMungeSortOnLoad(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(WW3D::Is_Munge_Sort_On_Load_Enabled() ? 1 : 0);
}

void CMainFrame::OnEnableGammaCorrection()
{
	int setting=::AfxGetApp()->GetProfileInt("Config","EnableGamma",0);
	bool enable_gamma=(setting?true:false);
	enable_gamma=!enable_gamma;
	::AfxGetApp()->WriteProfileInt("Config", "EnableGamma", enable_gamma?1:0);
	if (enable_gamma) {
		float gamma=::AfxGetApp()->GetProfileInt("Config","Gamma",10);
		gamma=gamma/10.0f;
		if (gamma<1.0) gamma=1.0;
		if (gamma>3.0) gamma=3.0;
		DX8Wrapper::Set_Gamma(gamma,0.0f,1.0f);
	} else {
		DX8Wrapper::Set_Gamma(1.0,0.0f,1.0f);
	}
}

void CMainFrame::OnUpdateEnableGammaCorrection(CCmdUI* pCmdUI)
{
	int setting=::AfxGetApp()->GetProfileInt("Config","EnableGamma",0);
	pCmdUI->SetCheck(setting);
}

void CMainFrame::OnSetGamma()
{
	int setting=::AfxGetApp()->GetProfileInt("Config","EnableGamma",0);
	if (setting) {
		GammaDialogClass gammadialog;
		gammadialog.DoModal();
	} else {
		MessageBox("Gamma is disabled.\nEnable in File Menu.","Warning");
	}
}


//////////////////////////////////////////////////////////////////////////
//
//  OnEditAnimatedSoundsOptions
//
//////////////////////////////////////////////////////////////////////////
void
CMainFrame::OnEditAnimatedSoundsOptions ()
{
	AnimatedSoundOptionsDialogClass dialog (this);
	dialog.DoModal ();
}


//////////////////////////////////////////////////////////////////////////
//
// TheSuperHackers @feature Tria 18/04/2026 Bones submenu handlers for View menu.
//
//////////////////////////////////////////////////////////////////////////

static const float s_BoneSizes[] = { 0.05f, 0.10f, 0.15f, 0.30f, 0.60f };

void CMainFrame::OnShowBonePivots()
{
	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->SetShowBonePivots(!doc->GetShowBonePivots());
		::AfxGetApp()->WriteProfileInt("Config", "ShowBonePivots", doc->GetShowBonePivots() ? 1 : 0);
	}
}

void CMainFrame::OnUpdateShowBonePivots(CCmdUI *pCmdUI)
{
	CW3DViewDoc *doc = ::GetCurrentDocument();
	pCmdUI->SetCheck(doc != nullptr && doc->GetShowBonePivots());
}

static void SetBoneSize(int index)
{
	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->SetBoneDiamondSize(s_BoneSizes[index]);
		::AfxGetApp()->WriteProfileInt("Config", "BoneDiamondSize", index);
	}
}

static void UpdateBoneSize(CCmdUI *pCmdUI, int index)
{
	CW3DViewDoc *doc = ::GetCurrentDocument();
	float size = (doc != nullptr) ? doc->GetBoneDiamondSize() : 0.15f;
	float diff = size - s_BoneSizes[index];
	pCmdUI->SetCheck(diff > -0.001f && diff < 0.001f);
}

void CMainFrame::OnBoneSizeTiny()   { SetBoneSize(0); }
void CMainFrame::OnBoneSizeSmall()  { SetBoneSize(1); }
void CMainFrame::OnBoneSizeMedium() { SetBoneSize(2); }
void CMainFrame::OnBoneSizeLarge()  { SetBoneSize(3); }
void CMainFrame::OnBoneSizeHuge()   { SetBoneSize(4); }

void CMainFrame::OnUpdateBoneSizeTiny(CCmdUI *pCmdUI)   { UpdateBoneSize(pCmdUI, 0); }
void CMainFrame::OnUpdateBoneSizeSmall(CCmdUI *pCmdUI)  { UpdateBoneSize(pCmdUI, 1); }
void CMainFrame::OnUpdateBoneSizeMedium(CCmdUI *pCmdUI) { UpdateBoneSize(pCmdUI, 2); }
void CMainFrame::OnUpdateBoneSizeLarge(CCmdUI *pCmdUI)  { UpdateBoneSize(pCmdUI, 3); }
void CMainFrame::OnUpdateBoneSizeHuge(CCmdUI *pCmdUI)   { UpdateBoneSize(pCmdUI, 4); }


//////////////////////////////////////////////////////////////////////////
//
// TheSuperHackers @feature Tria 18/04/2026 W3D Shader filter toggles.
//
//////////////////////////////////////////////////////////////////////////

void CMainFrame::OnShaderAdditive()
{
	ShaderClass::Set_Filter_Additive(!ShaderClass::Get_Filter_Additive());
}

void CMainFrame::OnUpdateShaderAdditive(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Get_Filter_Additive() ? 1 : 0);
}

void CMainFrame::OnShaderAlphaTest()
{
	ShaderClass::Set_Filter_Alpha_Test(!ShaderClass::Get_Filter_Alpha_Test());
}

void CMainFrame::OnUpdateShaderAlphaTest(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Get_Filter_Alpha_Test() ? 1 : 0);
}

void CMainFrame::OnShaderAlphaBlend()
{
	ShaderClass::Set_Filter_Alpha_Blend(!ShaderClass::Get_Filter_Alpha_Blend());
}

void CMainFrame::OnUpdateShaderAlphaBlend(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Get_Filter_Alpha_Blend() ? 1 : 0);
}

void CMainFrame::OnShaderAlphaBlendTest()
{
	ShaderClass::Set_Filter_Alpha_Blend_Test(!ShaderClass::Get_Filter_Alpha_Blend_Test());
}

void CMainFrame::OnUpdateShaderAlphaBlendTest(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Get_Filter_Alpha_Blend_Test() ? 1 : 0);
}

// TheSuperHackers @feature Tria 22/04/2026 Toggle double-sided rendering for previewing back faces.
void CMainFrame::OnShaderDoubleSided()
{
	ShaderClass::Force_Double_Sided(!ShaderClass::Is_Double_Sided_Forced());
}

void CMainFrame::OnUpdateShaderDoubleSided(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(ShaderClass::Is_Double_Sided_Forced() ? 1 : 0);
}

// TheSuperHackers @feature Tria Toggle rendering of the normally-culled back faces
// as a flat tint. Driven by a second render pass in CGraphicView (the mesh renderer
// defers draws past the scene pass, so the tint is applied at that outer render
// level rather than inside SceneClass::Render).
void CMainFrame::OnShaderBackfaceTint()
{
	CGraphicView::Set_Show_Backface_Tint(!CGraphicView::Is_Show_Backface_Tint());
}

void CMainFrame::OnUpdateShaderBackfaceTint(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(CGraphicView::Is_Show_Backface_Tint() ? 1 : 0);
}

// TheSuperHackers @feature Tria Pick the back-face tint colour. Uses the Qt colour
// picker when the material viewer's Qt panel is built (matching the rest of that UI),
// falling back to the MFC colour dialog otherwise. (0..1 float Vector3 <-> 0..255 COLORREF.)
void CMainFrame::OnShaderBackfaceTintColor()
{
	COLORREF initial = CGraphicView::Tint_Color_To_ColorRef(CGraphicView::Get_Backface_Tint_Color());

	COLORREF chosen = initial;
	bool ok;
#ifdef W3DVIEW_HAS_QT
	ok = W3dMaterialViewer::PickColor(initial, chosen);
#else
	CColorDialog dlg(initial, CC_FULLOPEN, this);
	ok = (dlg.DoModal() == IDOK);
	if (ok) chosen = dlg.GetColor();
#endif
	if (ok) {
		CGraphicView::Set_Backface_Tint_Color(CGraphicView::ColorRef_To_Tint_Color(chosen));
	}
}

// TheSuperHackers @feature Tria Ground menu: checkered ground plane with a blob
// shadow under the displayed object (same drawing the Material Viewer preview
// uses). Height... opens a live slider dialog; Reset re-snaps the plane to the
// object's bounding-box bottom.
void CMainFrame::OnGroundShow()
{
	CGraphicView *view = ::Get_Graphic_View();
	if (view != nullptr) {
		view->Set_Show_Ground(!view->Is_Show_Ground());
	}
}

void CMainFrame::OnUpdateGroundShow(CCmdUI *pCmdUI)
{
	CGraphicView *view = ::Get_Graphic_View();
	pCmdUI->Enable(view != nullptr);
	pCmdUI->SetCheck((view != nullptr && view->Is_Show_Ground()) ? 1 : 0);
}

void CMainFrame::OnGroundHeight()
{
	CGroundHeightDialog dialog(this);
	dialog.DoModal();
}

void CMainFrame::OnUpdateGroundHeight(CCmdUI *pCmdUI)
{
	CGraphicView *view = ::Get_Graphic_View();
	pCmdUI->Enable(view != nullptr && view->Is_Show_Ground());
}

void CMainFrame::OnGroundReset()
{
	CGraphicView *view = ::Get_Graphic_View();
	if (view != nullptr) {
		view->Reset_Ground_To_Object();
	}
}

void CMainFrame::OnUpdateGroundReset(CCmdUI *pCmdUI)
{
	CGraphicView *view = ::Get_Graphic_View();
	pCmdUI->Enable(view != nullptr && view->Is_Show_Ground());
}

void CMainFrame::OnShowSubObjNames()
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	if (pdoc != nullptr) {
		pdoc->SetShowSubObjNames(!pdoc->GetShowSubObjNames());
		::AfxGetApp()->WriteProfileInt("Config", "ShowSubObjNames", pdoc->GetShowSubObjNames() ? 1 : 0);
	}
}

void CMainFrame::OnUpdateShowSubObjNames(CCmdUI *pCmdUI)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	pCmdUI->SetCheck(pdoc != nullptr && pdoc->GetShowSubObjNames() ? 1 : 0);
}

void CMainFrame::OnShowBoneNames()
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	if (pdoc != nullptr) {
		pdoc->SetShowBoneNames(!pdoc->GetShowBoneNames());
		::AfxGetApp()->WriteProfileInt("Config", "ShowBoneNames", pdoc->GetShowBoneNames() ? 1 : 0);
	}
}

void CMainFrame::OnUpdateShowBoneNames(CCmdUI *pCmdUI)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	pCmdUI->SetCheck(pdoc != nullptr && pdoc->GetShowBoneNames() ? 1 : 0);
}

void CMainFrame::OnShowBonesAndSubObjects()
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	if (pdoc != nullptr) {
		bool newState = !pdoc->GetShowBones();
		pdoc->SetShowBones(newState);
		::AfxGetApp()->WriteProfileInt("Config", "ShowBones", newState ? 1 : 0);
	}
}

void CMainFrame::OnUpdateShowBonesAndSubObjects(CCmdUI *pCmdUI)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetActiveDocument();
	pCmdUI->SetCheck(pdoc != nullptr && pdoc->GetShowBones() ? 1 : 0);
}

// TheSuperHackers @feature W3DView native dark mode (Light/Dark/Auto).
static void SetTheme(HWND hwnd, W3DDarkMode::Mode mode)
{
	W3DDarkMode::SetMode(mode, hwnd);
	::AfxGetApp()->WriteProfileInt("Config", "Theme", static_cast<int>(mode));

	// The material viewer is an owned popup; the descendant broadcast misses it.
	CMaterialViewerFrame::NotifyThemeChanged();

	// Snap the viewport background to the new theme's default (28 dark / 127 light)
	// when the user hasn't customized it. SetMode has already flipped IsDark().
	CFrameWnd *pFrame = static_cast<CFrameWnd *> (::AfxGetMainWnd ());
	if (pFrame)
	{
		CW3DViewDoc *pDoc = static_cast<CW3DViewDoc *> (pFrame->GetActiveDocument ());
		if (pDoc)
			pDoc->ApplyThemeBackgroundIfDefault ();
	}
}

void CMainFrame::OnThemeLight() { SetTheme(m_hWnd, W3DDarkMode::Mode::Light); }
void CMainFrame::OnThemeDark()  { SetTheme(m_hWnd, W3DDarkMode::Mode::Dark);  }
void CMainFrame::OnThemeAuto()  { SetTheme(m_hWnd, W3DDarkMode::Mode::Auto);  }

void CMainFrame::OnUpdateThemeLight(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(W3DDarkMode::GetMode() == W3DDarkMode::Mode::Light ? 1 : 0);
	if (!W3DDarkMode::IsSupported())
		pCmdUI->Enable(FALSE);
}
void CMainFrame::OnUpdateThemeDark(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(W3DDarkMode::GetMode() == W3DDarkMode::Mode::Dark ? 1 : 0);
	if (!W3DDarkMode::IsSupported())
		pCmdUI->Enable(FALSE);
}
void CMainFrame::OnUpdateThemeAuto(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(W3DDarkMode::GetMode() == W3DDarkMode::Mode::Auto ? 1 : 0);
	if (!W3DDarkMode::IsSupported())
		pCmdUI->Enable(FALSE);
}

LRESULT CMainFrame::OnSettingChangeRaw(WPARAM wParam, LPARAM lParam)
{
	// lParam carries a wide-string section name from the OS regardless of app build.
	W3DDarkMode::HandleSettingChange(m_hWnd, lParam);
	CMaterialViewerFrame::NotifyThemeChanged();
	return Default();
}

// TheSuperHackers @feature W3D Material Viewer window. Context-aware (Ctrl+M):
// a mesh selected in the asset tree opens pre-selected to that mesh; anything
// else opens the whole currently displayed object.
void CMainFrame::OnMaterialViewer()
{
	CDataTreeView *pCDataTreeView = (CDataTreeView *)m_wndSplitter.GetPane (0, 0);
	if (pCDataTreeView != nullptr) {
		pCDataTreeView->OpenSelectionInMaterialViewer ();
	} else {
		CMaterialViewerFrame::ShowViewer ();
	}
}

BOOL CMainFrame::OnCopyData(CWnd *pWnd, COPYDATASTRUCT *pCopyDataStruct)
{
	if (W3DViewMcpBridge::IsEnabled(m_hWnd) && W3DViewMcpBridge::IsRequest(pCopyDataStruct)) {
		return W3DViewMcpBridge::HandleRequest(pCopyDataStruct);
	}
	return CFrameWnd::OnCopyData(pWnd, pCopyDataStruct);
}

void CMainFrame::OnMcpServerEnabled()
{
	if (W3DViewMcpBridge::IsEnabled(m_hWnd)) {
		W3DViewMcpBridge::Detach(m_hWnd);
	} else {
		W3DViewMcpBridge::Attach(m_hWnd);
	}
	::AfxGetApp()->WriteProfileInt("Config", "McpServerEnabled", W3DViewMcpBridge::IsEnabled(m_hWnd) ? 1 : 0);
}

void CMainFrame::OnUpdateMcpServerEnabled(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(W3DViewMcpBridge::IsEnabled(m_hWnd) ? 1 : 0);
}

void CMainFrame::OnMcpServerInformation()
{
	CString information;
	information.Format(
		"Status: %s\n"
		"Native bridge version: %lu\n"
		"Process ID: %lu\n"
		"Discovery marker: GeneralsW3DViewMcp\n"
		"Transport: local WM_COPYDATA\n"
		"Request directory: %%TEMP%%\\w3dview-mcp\n\n"
		"The external Python MCP host (python -m tools.w3dview_mcp) runs as a "
		"separate process and connects to this native W3DView endpoint.",
		W3DViewMcpBridge::IsEnabled(m_hWnd) ? "Enabled" : "Disabled",
		static_cast<unsigned long>(W3DViewMcpBridge::BRIDGE_VERSION),
		static_cast<unsigned long>(::GetCurrentProcessId()));
	MessageBox(information, "W3DView MCP Server", MB_OK | MB_ICONINFORMATION);
}

// TheSuperHackers @feature F5: viewport refresh. Re-runs the exact path a
// window resize takes (CGraphicView::OnSize -> WW3D::Set_Device_Resolution ->
// full D3D8 device reset with all resources released and recreated) at the
// current size — the same effect maximizing the window has on the viewport.
void CMainFrame::OnRefreshViewport()
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetActiveDocument();
	CGraphicView *view = (doc != nullptr) ? doc->GetGraphicView() : nullptr;
	if (view != nullptr && ::IsWindow(view->m_hWnd)) {
		CRect rect;
		view->GetClientRect(&rect);
		view->SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.Width(), rect.Height()));
	}
}

// TheSuperHackers @feature View > Lock Toolbars. Short-circuits FloatControlBar so
// neither user drag nor double-click can tear a docked bar off its dock site.
// Also reapplies dark theming to the freshly created CMiniDockFrameWnd, since MFC
// destroys and recreates the mini-frame on every float — themes don't survive that.
void CMainFrame::FloatControlBar(CControlBar* pBar, CPoint point, DWORD dwStyle)
{
	if (m_bToolbarsLocked && pBar && (pBar->GetBarStyle() & CBRS_FLOATING) == 0)
		return;
	CFrameWnd::FloatControlBar(pBar, point, dwStyle);
	if (W3DDarkMode::IsDark())
		W3DDarkMode::ApplyToOwnedPopups(m_hWnd);
}

void CMainFrame::DockControlBar(CControlBar* pBar, CDockBar* pDockBar, LPCRECT lpRect)
{
	CFrameWnd::DockControlBar(pBar, pDockBar, lpRect);
	if (W3DDarkMode::IsDark())
	{
		// Repaint the dock bar that received the docked control.
		W3DDarkMode::AutoThemeChildren(m_hWnd);
		::RedrawWindow(m_hWnd, nullptr, nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

void CMainFrame::OnLockToolbars()
{
	m_bToolbarsLocked = !m_bToolbarsLocked;
	::AfxGetApp()->WriteProfileInt("Config", "LockToolbars", m_bToolbarsLocked ? 1 : 0);
}

void CMainFrame::OnUpdateLockToolbars(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_bToolbarsLocked ? 1 : 0);
}

LRESULT CMainFrame::OnW3DViewThemeChanged(WPARAM wDark, LPARAM /*lParam*/)
{
	CImageList* active = wDark ? &m_toolbarImgDark : m_toolbarImgLight;
	if (active && active->GetSafeHandle ()) {
		m_wndToolBar.GetToolBarCtrl ().SetImageList (active);
	}
	m_wndToolBar.Invalidate ();
	return 0;
}


