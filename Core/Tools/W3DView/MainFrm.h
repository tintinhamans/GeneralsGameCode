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

// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DataTreeView.h"
#include "Toolbar.h"
#include "WWLib/Vector.h"
#include "FrameScrubberDlg.h"
#include "WWMath/matrix3d.h"
#include "WWLib/wwstring.h"
#include "GraphicView.h"

// TheSuperHackers @feature Message used to process one file per pump cycle during async batch loading
#define WM_USER_LOAD_NEXT_FILE (WM_USER + 100)


#if defined(_MSC_VER) && _MSC_VER < 1300
typedef HTASK HTASK_OR_DWORD;
#else
typedef DWORD HTASK_OR_DWORD;
#endif


class CMainFrame : public CFrameWnd
{
protected: // create from serialization only
	CMainFrame();
	DECLARE_DYNCREATE(CMainFrame)

// Attributes
protected:
	CSplitterWnd m_wndSplitter;
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMainFrame)
	public:
	virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext) override;
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;
	virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) override;
	virtual BOOL PreTranslateMessage(MSG* pMsg) override;
	// TheSuperHackers @feature View > Lock Toolbars suppresses drag-to-float by
	// short-circuiting FloatControlBar when m_bToolbarsLocked is set. Both
	// overrides also re-apply dark theming after a layout change because MFC
	// destroys/recreates the mini-frame and invalidates the dock bars.
	virtual void FloatControlBar(CControlBar* pBar, CPoint point,
		DWORD dwStyle = CBRS_ALIGN_TOP);
	virtual void DockControlBar(CControlBar* pBar, CDockBar* pDockBar = nullptr,
		LPCRECT lpRect = nullptr);
	protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CMainFrame() override;
#ifdef RTS_DEBUG
	virtual void AssertValid() const override;
	virtual void Dump(CDumpContext& dc) const override;
#endif

protected:  // control bar embedded members
	CStatusBar  m_wndStatusBar;
	CToolBar    m_wndToolBar;

	// Filter box for the asset tree, hosted at the end of the toolbar
	enum { TREE_FILTER_WIDTH = 160 };
	CEdit       m_TreeFilterEdit;
	afx_msg void OnTreeFilterChanged();

// Generated message map functions
protected:
	//{{AFX_MSG(CMainFrame)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnObjectProperties();
	afx_msg void OnUpdateObjectProperties(CCmdUI* pCmdUI);
	afx_msg void OnLodGenerate();
	afx_msg void OnActivateApp(BOOL bActive, HTASK_OR_DWORD hTask);
	afx_msg void OnFileOpen();
	afx_msg void OnFileOpenFolder();
	afx_msg void OnAniSpeed();
	afx_msg void OnAniStop();
	afx_msg void OnAniStart();
	afx_msg void OnAniPause();
	afx_msg void OnCameraBack();
	afx_msg void OnCameraBottom();
	afx_msg void OnCameraFront();
	afx_msg void OnCameraLeft();
	afx_msg void OnCameraReset();
	afx_msg void OnCameraRight();
	afx_msg void OnCameraTop();
	afx_msg void OnObjectRotateZ();
	afx_msg void OnObjectRotateY();
	afx_msg void OnObjectRotateX();
	afx_msg void OnLightAmbient();
	afx_msg void OnLightScene();
	afx_msg void OnBackgroundColor();
	afx_msg void OnBackgroundBMP();
	afx_msg void OnSaveSettings();
	afx_msg void OnLoadSettings();
	afx_msg void OnLODSetSwitch();
	afx_msg void OnLODSave();
	afx_msg void OnLODSaveAll();
	afx_msg void OnBackgroundObject();
	afx_msg void OnUpdateViewAnimationBar(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewObjectBar(CCmdUI* pCmdUI);
	afx_msg void OnViewAnimationBar();
	afx_msg void OnViewObjectBar();
	afx_msg void OnAniStepFwd();
	afx_msg void OnAniStepBkwd();
	afx_msg void OnObjectReset();
	afx_msg void OnCameraAllowRotateX();
	afx_msg void OnCameraAllowRotateY();
	afx_msg void OnCameraAllowRotateZ();
	afx_msg void OnUpdateCameraAllowRotateX(CCmdUI* pCmdUI);
	afx_msg void OnUpdateCameraAllowRotateY(CCmdUI* pCmdUI);
	afx_msg void OnUpdateCameraAllowRotateZ(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectRotateX(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectRotateY(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectRotateZ(CCmdUI* pCmdUI);
	afx_msg void OnDeviceChange();
	afx_msg void OnViewFullscreen();
	afx_msg void OnUpdateViewFullscreen(CCmdUI* pCmdUI);
	afx_msg void OnWindowPosChanging(WINDOWPOS FAR* lpwndpos);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI);
	afx_msg void OnCreateEmitter();
	afx_msg void OnEditEmitter();
	afx_msg void OnUpdateEditEmitter(CCmdUI* pCmdUI);
	afx_msg void OnSaveEmitter();
	afx_msg void OnUpdateSaveEmitter(CCmdUI* pCmdUI);
	afx_msg void OnBoneAutoAssign();
	afx_msg void OnBoneManagement();
	afx_msg void OnSaveAggregate();
	afx_msg void OnCameraAnimate();
	afx_msg void OnUpdateCameraAnimate(CCmdUI* pCmdUI);
	afx_msg void OnUpdateLodSave(CCmdUI* pCmdUI);
	afx_msg void OnUpdateSaveAggregate(CCmdUI* pCmdUI);
	afx_msg void OnCameraResetOnLoad();
	afx_msg void OnUpdateCameraResetOnLoad(CCmdUI* pCmdUI);
	afx_msg void OnCameraInvertY();
	afx_msg void OnUpdateCameraInvertY(CCmdUI* pCmdUI);
	afx_msg void OnObjectRotateYBack();
	afx_msg void OnObjectRotateZBack();
	afx_msg void OnLightRotateY();
	afx_msg void OnLightRotateYBack();
	afx_msg void OnLightRotateZ();
	afx_msg void OnLightRotateZBack();
	afx_msg void OnDestroy();
	afx_msg void OnDecLight();
	afx_msg void OnIncLight();
	afx_msg void OnDecAmbientLight();
	afx_msg void OnIncAmbientLight();
	afx_msg void OnMakeAggregate();
	afx_msg void OnRenameAggregate();
	afx_msg void OnCrashApp();
	afx_msg void OnLODRecordScreenArea();
	afx_msg void OnLODIncludeNull();
	afx_msg void OnUpdateLODIncludeNull(CCmdUI* pCmdUI);
	afx_msg void OnLodPrevLevel();
	afx_msg void OnUpdateLodPrevLevel(CCmdUI* pCmdUI);
	afx_msg void OnLodNextLevel();
	afx_msg void OnUpdateLodNextLevel(CCmdUI* pCmdUI);
	afx_msg void OnLodAutoswitch();
	afx_msg void OnUpdateLodAutoswitch(CCmdUI* pCmdUI);
	afx_msg void OnUpdateMakeMovie(CCmdUI* pCmdUI);
	afx_msg void OnMakeMovie();
	afx_msg void OnSaveScreenshot();
	afx_msg void OnSlideshowDown();
	afx_msg void OnSlideshowUp();
	afx_msg void OnAdvancedAnim();
	afx_msg void OnUpdateAdvancedAnim(CCmdUI* pCmdUI);
	afx_msg void OnCameraSettings();
	afx_msg void OnCopyScreenSize();
	afx_msg void OnListMissingTextures();
	afx_msg void OnCopyAssets();
	afx_msg void OnUpdateCopyAssets(CCmdUI* pCmdUI);
	afx_msg void OnLightingExpose();
	afx_msg void OnUpdateLightingExpose(CCmdUI* pCmdUI);
	afx_msg void OnTexturePath();
	afx_msg void OnChangeResolution();
	afx_msg void OnCreateSphere();
	afx_msg void OnCreateRing();
	afx_msg void OnUpdateEditPrimitive(CCmdUI* pCmdUI);
	afx_msg void OnEditPrimitive();
	afx_msg void OnExportPrimitive();
	afx_msg void OnUpdateExportPrimitive(CCmdUI* pCmdUI);
	afx_msg void OnKillSceneLight();
	afx_msg void OnPrelitMultipass();
	afx_msg void OnUpdatePrelitMultipass(CCmdUI* pCmdUI);
	afx_msg void OnPrelitMultitex();
	afx_msg void OnUpdatePrelitMultitex(CCmdUI* pCmdUI);
	afx_msg void OnPrelitVertex();
	afx_msg void OnUpdatePrelitVertex(CCmdUI* pCmdUI);
	afx_msg void OnAddToLineup();
	afx_msg void OnUpdateAddToLineup(CCmdUI* pCmdUI);
	afx_msg void OnImportFacialAnims();
	afx_msg void OnUpdateImportFacialAnims(CCmdUI* pCmdUI);
	afx_msg void OnRestrictAnims();
	afx_msg void OnUpdateRestrictAnims(CCmdUI* pCmdUI);
	afx_msg void OnBindSubobjectLod();
	afx_msg void OnUpdateBindSubobjectLod(CCmdUI* pCmdUI);
	afx_msg void OnSetCameraDistance();
	afx_msg void OnObjectAlternateMaterials();
	afx_msg void OnCreateSoundObject();
	afx_msg void OnEditSoundObject();
	afx_msg void OnUpdateEditSoundObject(CCmdUI* pCmdUI);
	afx_msg void OnExportSoundObj();
	afx_msg void OnUpdateExportSoundObj(CCmdUI* pCmdUI);
	afx_msg void OnWireframeMode();
	afx_msg void OnUpdateWireframeMode(CCmdUI* pCmdUI);
	afx_msg void OnUpdateBackgroundFog(CCmdUI* pCmdUI);
	afx_msg void OnBackgroundFog();
	afx_msg void OnUpdateScaleEmitter(CCmdUI* pCmdUI);
	afx_msg void OnScaleEmitter();
	afx_msg void OnUpdateToggleSorting(CCmdUI* pCmdUI);
	afx_msg void OnToggleSorting();
	afx_msg void OnUpdateToggleAlpha(CCmdUI* pCmdUI);
	afx_msg void OnToggleAlpha();
	afx_msg void OnFilterPoint();
	afx_msg void OnFilterBilinear();
	afx_msg void OnFilterTrilinear();
	afx_msg void OnFilterAniso2x();
	afx_msg void OnFilterAniso4x();
	afx_msg void OnFilterAniso8x();
	afx_msg void OnFilterAniso16x();
	afx_msg void OnUpdateFilterPoint(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterBilinear(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterTrilinear(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterAniso2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterAniso4x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterAniso8x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFilterAniso16x(CCmdUI* pCmdUI);
	afx_msg void OnMsaaNone();
	afx_msg void OnMsaa2x();
	afx_msg void OnMsaa4x();
	afx_msg void OnMsaa8x();
	afx_msg void OnUpdateMsaaNone(CCmdUI* pCmdUI);
	afx_msg void OnUpdateMsaa2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateMsaa4x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateMsaa8x(CCmdUI* pCmdUI);
	afx_msg void OnCameraBonePosX();
	afx_msg void OnUpdateCameraBonePosX(CCmdUI* pCmdUI);
	afx_msg void OnViewPatchGapFill();
	afx_msg void OnUpdateViewPatchGapFill(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision1();
	afx_msg void OnUpdateViewSubdivision1(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision2();
	afx_msg void OnUpdateViewSubdivision2(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision3();
	afx_msg void OnUpdateViewSubdivision3(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision4();
	afx_msg void OnUpdateViewSubdivision4(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision5();
	afx_msg void OnUpdateViewSubdivision5(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision6();
	afx_msg void OnUpdateViewSubdivision6(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision7();
	afx_msg void OnUpdateViewSubdivision7(CCmdUI* pCmdUI);
	afx_msg void OnViewSubdivision8();
	afx_msg void OnUpdateViewSubdivision8(CCmdUI* pCmdUI);
	afx_msg void OnMungeSortOnLoad();
	afx_msg void OnUpdateMungeSortOnLoad(CCmdUI* pCmdUI);
	afx_msg void OnEnableGammaCorrection();
	afx_msg void OnUpdateEnableGammaCorrection(CCmdUI* pCmdUI);
	afx_msg void OnSetGamma();
	afx_msg void OnEditAnimatedSoundsOptions();
	// TheSuperHackers @feature Async batch file loading: process one file per message-pump cycle
	afx_msg LRESULT OnLoadNextFile(WPARAM wParam, LPARAM lParam);
	// TheSuperHackers @feature Reload all loaded assets with updated texture path priority
	afx_msg void OnReloadAssets();
	afx_msg void OnUpdateReloadAssets(CCmdUI* pCmdUI);
	// TheSuperHackers @feature Tria 18/04/2026 Batch drag-drop through async loader with status bar.
	afx_msg void OnDropFiles(HDROP hDropInfo);
	// TheSuperHackers @feature Tria 18/04/2026 Bones submenu in View menu.
	afx_msg void OnShowBonePivots();
	afx_msg void OnUpdateShowBonePivots(CCmdUI* pCmdUI);
	afx_msg void OnBoneSizeTiny();
	afx_msg void OnBoneSizeSmall();
	afx_msg void OnBoneSizeMedium();
	afx_msg void OnBoneSizeLarge();
	afx_msg void OnBoneSizeHuge();
	afx_msg void OnUpdateBoneSizeTiny(CCmdUI* pCmdUI);
	afx_msg void OnUpdateBoneSizeSmall(CCmdUI* pCmdUI);
	afx_msg void OnUpdateBoneSizeMedium(CCmdUI* pCmdUI);
	afx_msg void OnUpdateBoneSizeLarge(CCmdUI* pCmdUI);
	afx_msg void OnUpdateBoneSizeHuge(CCmdUI* pCmdUI);
	// TheSuperHackers @feature Tria 18/04/2026 W3D Shader filter toggles.
	afx_msg void OnShaderAdditive();
	afx_msg void OnUpdateShaderAdditive(CCmdUI* pCmdUI);
	afx_msg void OnShaderAlphaTest();
	afx_msg void OnUpdateShaderAlphaTest(CCmdUI* pCmdUI);
	afx_msg void OnShaderAlphaBlend();
	afx_msg void OnUpdateShaderAlphaBlend(CCmdUI* pCmdUI);
	afx_msg void OnShaderAlphaBlendTest();
	afx_msg void OnUpdateShaderAlphaBlendTest(CCmdUI* pCmdUI);
	afx_msg void OnShaderDoubleSided();
	afx_msg void OnUpdateShaderDoubleSided(CCmdUI* pCmdUI);
	afx_msg void OnShaderBackfaceTint();
	afx_msg void OnUpdateShaderBackfaceTint(CCmdUI* pCmdUI);
	afx_msg void OnShaderBackfaceTintColor();
	// TheSuperHackers @feature Tria Ground menu (ground plane in the viewport).
	afx_msg void OnGroundShow();
	afx_msg void OnUpdateGroundShow(CCmdUI* pCmdUI);
	afx_msg void OnGroundHeight();
	afx_msg void OnUpdateGroundHeight(CCmdUI* pCmdUI);
	afx_msg void OnGroundReset();
	afx_msg void OnUpdateGroundReset(CCmdUI* pCmdUI);
	afx_msg void OnShowSubObjNames();
	afx_msg void OnUpdateShowSubObjNames(CCmdUI* pCmdUI);
	afx_msg void OnShowBoneNames();
	afx_msg void OnUpdateShowBoneNames(CCmdUI* pCmdUI);
	afx_msg void OnShowBonesAndSubObjects();
	afx_msg void OnUpdateShowBonesAndSubObjects(CCmdUI* pCmdUI);
	// TheSuperHackers @feature View > Lock Toolbars.
	afx_msg void OnLockToolbars();
	afx_msg void OnUpdateLockToolbars(CCmdUI* pCmdUI);
	// TheSuperHackers @feature W3DView native dark mode (Light/Dark/Auto).
	afx_msg void OnThemeLight();
	afx_msg void OnThemeDark();
	afx_msg void OnThemeAuto();
	afx_msg void OnUpdateThemeLight(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeDark(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeAuto(CCmdUI* pCmdUI);
	afx_msg LRESULT OnSettingChangeRaw(WPARAM wParam, LPARAM lParam);
	// TheSuperHackers @feature Swap toolbar image list when dark/light theme changes.
	afx_msg LRESULT OnW3DViewThemeChanged(WPARAM wParam, LPARAM lParam);
	// TheSuperHackers @feature W3D Material Viewer window.
	afx_msg void OnMaterialViewer();
	// TheSuperHackers @feature F5 refreshes the viewport (re-displays the current asset).
	afx_msg void OnRefreshViewport();
	afx_msg BOOL OnCopyData(CWnd *pWnd, COPYDATASTRUCT *pCopyDataStruct);
	afx_msg void OnMcpServerEnabled();
	afx_msg void OnUpdateMcpServerEnabled(CCmdUI *pCmdUI);
	afx_msg void OnMcpServerInformation();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

public:

	//////////////////////////////////////////////////////////////////////
	//	Public methods
	//////////////////////////////////////////////////////////////////////
	CView *GetPane (int iRow, int iCol) const
	{ return (CView *)m_wndSplitter.GetPane (iRow, iCol); }

	void	ShowObjectProperties ();

	void	OnSelectionChanged (ASSET_TYPE newAssetType);

	void	Update_Frame_Time (DWORD milliseconds);
	void	UpdatePolygonCount (int iPolygons);
	void	UpdatePolygonCount (int iPolygons, int iVertices, int iTriangles);
	void	Update_Particle_Count (int particles);
	void	UpdateCameraDistance (float cameraDistance);
	void	UpdateFrameCount (int iCurrentFrame, int iTotalFrames, float frame_rate);

	// Seeks through the frame scrubber, whether or not its window is open
	bool	Go_To_Animation_Frame (int frame)	{ return m_frameScrubberDlg.Go_To_Frame (frame); }

	// Runs the commands still in the queue, such as the start that PlayAnimation posts
	void	Run_Posted_Commands ();
	void	RestoreOriginalSize ();
	void	Select_Device (bool show_dlg = true);

	HMENU	Get_Emitters_List_Menu () const { return m_hEmittersSubMenu; }
	void	Update_Emitters_List ();

protected:

	//////////////////////////////////////////////////////////////////////
	//	Protected methods
	//////////////////////////////////////////////////////////////////////
	void	Restore_Window_State ();

private:

	//////////////////////////////////////////////////////////////////////
	//	Private member data
	//////////////////////////////////////////////////////////////////////
	ASSET_TYPE m_currentAssetType;
	CFancyToolbar m_objectToolbar;
	CFancyToolbar m_animationToolbar;
	BOOL m_bShowAnimationBar;
	RECT m_OrigRect;
	HMENU m_hEmittersSubMenu;
	BOOL m_bInitialized;
	// TheSuperHackers @feature Async batch file loading state
	DynamicVectorClass<CString> m_pendingLoadFiles;
	int m_pendingLoadIndex;
	int m_totalLoadCount;
	// TheSuperHackers @performance Highest file index already submitted for
	// async OS-cache prefetch. Prefetch jobs ReadFile() upcoming .w3d files
	// on worker threads to warm the disk cache before LoadAssetsFromFile
	// touches them on the main thread.
	int m_lastPrefetchedIndex;
	// TheSuperHackers @feature Tria 18/04/2026 Modeless frame scrubber popup.
	CFrameScrubberDlg m_frameScrubberDlg;
	// Poly pane display mode: 0 = polys, 1 = vertices, 2 = triangles.
	int m_polyPaneMode;
	int m_cachedPolys;
	int m_cachedVertices;
	int m_cachedTriangles;
	// TheSuperHackers @feature Dark-mode toolbar image list. The light variant is
	// the MFC-owned image list installed by LoadToolBar(IDR_MAINFRAME); we keep a
	// non-owning pointer to it so we can swap back.
	CImageList m_toolbarImgDark;
	CImageList* m_toolbarImgLight;
	// TheSuperHackers @feature View > Lock Toolbars — when true, FloatControlBar
	// is suppressed so docked bars can't be torn off by drag/double-click.
	// Persisted in MFC profile under Config\LockToolbars.
	bool m_bToolbarsLocked;

	// TheSuperHackers @feature Reload Assets state preservation. OnReloadAssets
	// calls OnNewDocument which destroys the displayed render object, current
	// animation, and resets the camera. We snapshot the user-visible state here
	// before that wipe, then restore it after the last file finishes reloading
	// in OnLoadNextFile. 'valid' is false until CaptureReloadState writes it.
	struct ReloadRestoreState
	{
		bool						valid;
		StringClass					displayedAssetName;
		StringClass					animationName;
		float						currentFrame;
		CGraphicView::ANIMATION_STATE	animState;
		float						animSpeed;
		Matrix3D					cameraTransform;
		float						cameraDistance;
		StringClass					selectedSubItemName;
		ASSET_TYPE					selectedSubItemType;

		ReloadRestoreState (void)
			:	valid (false),
				currentFrame (0.0f),
				animState (CGraphicView::AnimStopped),
				animSpeed (1.0f),
				cameraTransform (1),
				cameraDistance (0.0f),
				selectedSubItemType (TypeUnknown)
		{
		}
	};
	ReloadRestoreState m_reloadRestore;

	void CaptureReloadState (void);
	void RestoreReloadState (void);
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.
