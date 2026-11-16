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

// W3DViewDoc.h : interface of the CW3DViewDoc class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "WW3D2/scene.h"
#include "WWLib/chunkio.h"
#include "WW3D2/hanim.h"
#include "WW3D2/hcanim.h"
#include "WW3D2/dynamesh.h"
#include "WW3D2/rendobj.h"
#include "LODDefs.h"
#include "WWLib/hashtemplate.h"
#include "WWLib/wwstring.h"
#include "AssetTypes.h"


///////////////////////////////////////////////////////////
//
//  Constants
//
const DWORD SAVE_SETTINGS_LIGHT     = 0x00000001;
const DWORD SAVE_SETTINGS_BACK      = 0x00000002;
const DWORD SAVE_SETTINGS_CAMERA    = 0x00000004;

// TheSuperHackers @feature Per-theme default viewport background.
// Light = 127/127/127, Dark = 28/28/28 (Vector3 components are 0..1).
const float W3DVIEW_BG_LIGHT_F      = 127.0f / 255.0f;
const float W3DVIEW_BG_DARK_F       =  28.0f / 255.0f;


// Forward declarations
class ParticleEmitterClass;
class CameraClass;
class SceneClass;
class LightClass;
class RenderObjClass;
class HAnimClass;
class CGraphicView;
class CDataTreeView;
class DistLODClass;
class Bitmap2DObjClass;
class AssetInfoClass;
class HLodPrototypeClass;
class HLodClass;
class ViewerSceneClass;
class EmitterInstanceListClass;
class ScreenCursorClass;
class DazzleLayerClass;


/////////////////////////////////////////////////////////////////////
//
//  CW3DViewDoc
//
/////////////////////////////////////////////////////////////////////
class CW3DViewDoc : public CDocument
{
protected: // create from serialization only
	CW3DViewDoc();
	DECLARE_DYNCREATE(CW3DViewDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CW3DViewDoc)
	public:
	virtual BOOL OnNewDocument() override;
	virtual void Serialize(CArchive& ar) override;
	virtual BOOL OnOpenDocument(LPCTSTR lpszPathName) override;
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CW3DViewDoc();
#ifdef RTS_DEBUG
	virtual void AssertValid() const override;
	virtual void Dump(CDumpContext& dc) const override;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CW3DViewDoc)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

public:
	void SetChannelQCompression(bool bCompress){	m_bCompress_channel_Q = bCompress;}
	//
	//  Accessors
	//
	CameraClass *			Get2DCamera () const				{ return m_pC2DCamera.Peek(); }
	CameraClass *			GetBackObjectCamera () const		{ return m_pCBackObjectCamera.Peek(); }
	SceneClass *			Get2DScene () const					{ return m_pC2DScene.Peek(); }
	SceneClass *			GetCursorScene () const			{ return m_pCursorScene.Peek(); }
	ViewerSceneClass *	GetScene () const					{ return m_pCScene.Peek(); }
	SceneClass *			GetBackObjectScene () const		{ return m_pCBackObjectScene.Peek(); }
	LightClass *			GetSceneLight () const				{ return m_pCSceneLight.Peek(); }
	RenderObjClass *		GetDisplayedObject () const		{ return m_pCRenderObj.Peek(); }
	HAnimClass *			GetCurrentAnimation () const		{ return m_pCAnimation.Peek(); }
	// TheSuperHackers @feature Tria 18/04/2026 Public accessor for frame scrubber.
	int					GetCurrentFrame () const			{ return (int)m_CurrentFrame; }
	// Sub-frame-precision accessors used by Reload-Assets state restore (MainFrm.cpp).
	float					GetCurrentFrameF () const			{ return m_CurrentFrame; }
	void					SetCurrentFrame (float frame);
	const HTreeClass *	Get_Current_HTree () const;

	//
	// Creation/destruction methods
	//
	void					InitScene ();
	void					LoadAssetsFromFile (LPCTSTR lpszPathName);
	HLodPrototypeClass *GenerateLOD (LPCTSTR pszLODBaseName, LOD_NAMING_TYPE type);
	void					CleanupResources ();
	bool					Is_Initialized ()	{ return m_IsInitialized; }

	void					Reload_Displayed_Object ();
	void					Display_Emitter (ParticleEmitterClass *pemitter = nullptr, bool use_global_reset_flag = true, bool allow_reset = true);
	void					DisplayObject (RenderObjClass *pCModel = nullptr, bool use_global_reset_flag = true, bool allow_reset = true, bool add_ghost = false);
	BOOL					SaveSettings (LPCTSTR pszFilename, DWORD dwSettingsMask);
	BOOL					LoadSettings (LPCTSTR pszFileName);
	CGraphicView *		GetGraphicView ();
	CDataTreeView *	GetDataTreeView ();

	void					Build_Emitter_List (EmitterInstanceListClass *emitter_list, LPCTSTR emitter_name, RenderObjClass *render_obj = nullptr);

	//
	//  Animation methods
	//
	void					Make_Movie ();
	void					ResetAnimation ();
	void					StepAnimation (int frame_inc = 1);
	void					PlayAnimation (RenderObjClass *pobj, LPCTSTR panim_name = nullptr, bool use_global_reset_flag = true, bool allow_reset = true);
	void					PlayAnimation (RenderObjClass *pobj, HAnimComboClass *pcombo, bool use_global_reset_flag = true, bool allow_reset = true);
	void					UpdateFrame (float time_slice);
	void					SetAnimationBlend (BOOL bBlend)	{ m_bAnimBlend = bBlend; }
	bool					GetChannelQCompression(){ return m_bCompress_channel_Q;}
	int					GetChannelQnBytes(){return m_nChannelQnBytes;}
	void					SetChannelQnBytes(int n_bytes){m_nChannelQnBytes = n_bytes;}
	BOOL					GetAnimationBlend () const	{ return m_bAnimBlend; }
	bool					Is_Camera_Animated () const	{ return m_bAnimateCamera; }
	void					Animate_Camera (bool banimate);
	void					Import_Facial_Animation (const CString &hierarchy_name, const CString &filename);
	void					Play_Animation_Sound ();

	//
	//	Camera methods
	//
	bool					Is_Camera_Auto_Reset_On () const	{ return m_bAutoCameraReset; }
	void					Turn_Camera_Auto_Reset_On (bool onoff) { m_bAutoCameraReset = onoff; }

	//
	//  Background color methods
	//
	const Vector3 &	GetBackgroundColor () const								{ return m_backgroundColor; }
	void					SetBackgroundColor (const Vector3 &backgroundColor);
	// TheSuperHackers @feature Snap viewport background to the active theme's
	// default (light=127, dark=28) only when the current value is the OTHER
	// theme's default — i.e. the user hasn't customized it.
	void					ApplyThemeBackgroundIfDefault (void);

	//
	//  Background BMP methods
	//
	const CString &	GetBackgroundBMP  () const						{ return m_stringBackgroundBMP; }
	void					SetBackgroundBMP  (LPCTSTR pszBackgroundBMP);

	//
	//  Background Object methods
	//
	const CString &	GetBackgroundObjectName () const				{ return m_stringBackgroundObject; }
	void					SetBackgroundObject (LPCTSTR pszBackgroundObjectName);

	//
	//  Fogging methods
	//
	bool					IsFogEnabled () const								{ return m_bFogEnabled; }
	void					EnableFog (bool enable=true);

	//
	//	Scene methods
	//
	void					Remove_Object_From_Scene (RenderObjClass *prender_obj = nullptr);

	//
	//	Emitter serialization methods
	//
	bool					Save_Selected_Emitter ();
	bool					Save_Current_Emitter (const CString &filename);

	//
	//	Primitive serialization methods
	//
	bool					Save_Selected_Primitive ();
	bool					Save_Current_Sphere (const CString &filename);
	bool					Save_Current_Ring (const CString &filename);

	//
	//	Aggregate methods
	//
	void					Auto_Assign_Bones ();
	bool					Save_Selected_Aggregate ();
	bool					Save_Current_Aggregate (const CString &filename);

	//
	//	Sound object methods
	//
	bool					Save_Selected_Sound_Object ();
	bool					Save_Current_Sound_Object (const CString &filename);

	//
	//  LOD methods
	//
	bool					Save_Current_LOD (const CString &filename);
	bool					Save_Selected_LOD ();
	void					Switch_LOD (int increment = 1, RenderObjClass *render_obj = nullptr);

	//
	// Alternate Material interface.
	//
	void					Toggle_Alternate_Materials(RenderObjClass * obj = nullptr);

	//
	//	Prototype methods
	//
	void					Update_Aggregate_Prototype (RenderObjClass &render_obj);
	void					Update_LOD_Prototype (HLodClass &hlod);

	//
	//	Cursor management
	//
	void					Show_Cursor (bool onoff);
	void					Set_Cursor (LPCTSTR resource_name);
	bool					Is_Cursor_Shown () const;
	void					Create_Cursor ();

	//
	//	Particle methods
	//
	int					Count_Particles (RenderObjClass *render_obj = nullptr);
	void					Update_Particle_Count ();

	//
	//	Manual settings
	//
	void					Set_Manual_FOV (bool manual)			{ m_ManualFOV = manual; }
	void					Set_Manul_Clip_Planes  (bool manual){ m_ManualClipPlanes = manual; }
	bool					Is_FOV_Manual () const				{ return m_ManualFOV; }
	bool					Are_Clip_Planes_Manual () const	{ return m_ManualClipPlanes; }

	void					Update_Camera ();
	void					Save_Camera_Settings ();
	void					Load_Camera_Settings ();

	//
	//	File methods
	//
	void					Copy_Assets_To_Dir (LPCTSTR directory);
	bool					Lookup_Path (LPCTSTR asset_name, CString &path);
	const char *		Get_Last_Path () const { return (m_LastPath.IsEmpty () ? nullptr : (const char *)m_LastPath); }

	//
	//	Texture search paths
	//
	int						Get_Texture_Path_Count () const { return m_TexturePaths.Count(); }
	const CString &	Get_Texture_Path (int index) const { return m_TexturePaths[index]; }

	int						Get_Load_List_Count () const { return m_LoadList.Count(); }
	const CString &	Get_Load_List_Item (int i) const { return m_LoadList[i]; }

	void					Add_Texture_Path (LPCTSTR path);
	void					Remove_Texture_Path (int index);
	void					Clear_Texture_Paths (void) { m_TexturePaths.Delete_All(); }
	void					Save_Texture_Paths_To_Registry (void);
	void					Refresh_File_Factory_Registrations (void);

	//
	// Dazzle rendering support
	//
	void					Render_Dazzles(CameraClass * camera);

private:

	//////////////////////////////////////////////////////////////////
	//  Private member data
	//////////////////////////////////////////////////////////////////
	RefCountPtr<ViewerSceneClass>	m_pCScene;
	RefCountPtr<SceneClass>			m_pC2DScene;
	RefCountPtr<SceneClass>			m_pCursorScene;
	RefCountPtr<SceneClass>			m_pCBackObjectScene;
	DazzleLayerClass *	m_pDazzleLayer;
	RefCountPtr<RenderObjClass>		m_pCRenderObj;
	RefCountPtr<RenderObjClass>		m_pCBackgroundObject;
	RefCountPtr<HAnimClass>			m_pCAnimation;
	HAnimComboClass *		m_pCAnimCombo;
	RefCountPtr<LightClass>			m_pCSceneLight;
	RefCountPtr<Bitmap2DObjClass>	m_pCBackgroundBMP;
	RefCountPtr<CameraClass>			m_pC2DCamera;
	RefCountPtr<CameraClass>			m_pCBackObjectCamera;
	RefCountPtr<ScreenCursorClass>	m_pCursor;
	Vector3					m_backgroundColor;
	CString					m_stringBackgroundBMP;
	CString					m_stringBackgroundObject;

	bool						m_bCompress_channel_Q;
	int						m_nChannelQnBytes;
	float						m_CurrentFrame;
	float						m_animTime;
	BOOL						m_bAnimBlend;
	bool						m_bAnimateCamera;
	bool						m_bAutoCameraReset;
	bool						m_bOneTimeReset;
	bool						m_ManualFOV;
	bool						m_ManualClipPlanes;
	bool						m_IsInitialized;
	bool						m_bFogEnabled;

	DynamicVectorClass<CString>	m_TexturePaths;

	CString					m_LastPath;
	CString					m_LastFactoryDir;

	DynamicVectorClass<CString>	m_LoadList;

	// TheSuperHackers @feature Tria 18/04/2026 Track which W3D file each render object came from.
	HashTemplateClass<StringClass, StringClass> m_AssetSourceFileMap;
	// TheSuperHackers @feature Tria 18/04/2026 Toggle bone overlay rendering.
	bool						m_bShowBones;
	bool						m_bShowBonePivots;
	// TheSuperHackers @feature Tria 18/04/2026 Configurable bone diamond size.
	float						m_fBoneDiamondSize;
	// TheSuperHackers @feature Tria 22/04/2026 Toggle sub-object/bone name labels in viewport.
	bool						m_bShowSubObjNames;
	bool						m_bShowBoneNames;
	// TheSuperHackers @feature Tria 23/04/2026 Track which bone or sub-object is selected in the tree.
	StringClass				m_selectedItemName;
	ASSET_TYPE				m_selectedItemType;	// TypeBone or TypeMesh, TypeUnknown when none
public:
	const HashTemplateClass<StringClass, StringClass> & GetAssetSourceFileMap (void) const { return m_AssetSourceFileMap; }
	bool						GetShowBones (void) const { return m_bShowBones; }
	void						SetShowBones (bool b) { m_bShowBones = b; }
	bool						GetShowBonePivots (void) const { return m_bShowBonePivots; }
	void						SetShowBonePivots (bool b) { m_bShowBonePivots = b; }
	float						GetBoneDiamondSize (void) const { return m_fBoneDiamondSize; }
	void						SetBoneDiamondSize (float f) { m_fBoneDiamondSize = f; }
	bool						GetShowSubObjNames (void) const { return m_bShowSubObjNames; }
	void						SetShowSubObjNames (bool b) { m_bShowSubObjNames = b; }
	bool						GetShowBoneNames (void) const { return m_bShowBoneNames; }
	void						SetShowBoneNames (bool b) { m_bShowBoneNames = b; }
	const StringClass &	GetSelectedItemName (void) const { return m_selectedItemName; }
	ASSET_TYPE				GetSelectedItemType (void) const { return m_selectedItemType; }
	void						SetSelectedItem (const char *name, ASSET_TYPE type) { m_selectedItemName = name ? name : ""; m_selectedItemType = type; }
	void						ClearSelectedItem (void) { m_selectedItemName = ""; m_selectedItemType = TypeUnknown; }
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.
