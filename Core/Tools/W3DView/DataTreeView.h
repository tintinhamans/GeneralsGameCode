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

#pragma once

// DataTreeView.h : header file
//

#include "AfxCView.h"
#include "AssetTypes.h"
#include "WWLib/Vector.h"
#include "WWLib/hashtemplate.h"
#include "WWLib/wwstring.h"
#include <map>
#include <vector>

// Forward declarations
class RenderObjClass;
class AssetInfoClass;


/////////////////////////////////////////////////////////////////////////////
//
// CDataTreeView view
//
class CDataTreeView : public CTreeView
{
protected:
	CDataTreeView();           // protected constructor used by dynamic creation
	DECLARE_DYNCREATE(CDataTreeView)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDataTreeView)
	public:
	virtual void OnInitialUpdate() override;
	protected:
	virtual void OnDraw(CDC* pDC) override;      // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;
	//}}AFX_VIRTUAL

// Implementation
protected:
	virtual ~CDataTreeView() override;
#ifdef RTS_DEBUG
	virtual void AssertValid() const override;
	virtual void Dump(CDumpContext& dc) const override;
#endif

	// Generated message map functions
protected:
	//{{AFX_MSG(CDataTreeView)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDeleteItem(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnToggleMeshVis();
	afx_msg void OnShowOnlyMesh();
	afx_msg void OnShowBones();
	afx_msg void OnExportInfo();
	afx_msg void OnDumpW3DInfo();
	// TheSuperHackers @feature Open the right-clicked asset in the W3D Material Viewer.
	afx_msg void OnOpenInMaterialViewer();
	// TheSuperHackers @feature Rebuild themed check-state image list on dark/light switch.
	afx_msg LRESULT OnW3DViewThemeChanged(WPARAM wParam, LPARAM lParam);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	// Rebuilds the TVSIL_STATE checkbox image list using the current theme.
	void RebuildStateImageList();

	// Rebuilds the TVSIL_NORMAL tree-node icon list using the current theme.
	// Indices into the list (m_iAnimationIcon, m_iMeshIcon, etc.) stay stable.
	void RebuildNormalImageList();

	public:

		/////////////////////////////////////////////////////////////////////
		//	Public methods
		/////////////////////////////////////////////////////////////////////

		//
		//	Asset insertion methods
		//
		bool					Add_Asset_To_Tree (LPCTSTR name, ASSET_TYPE type, bool bselect);
		void					LoadAssetsIntoTree ();
		void					Refresh_Asset (LPCTSTR new_name, LPCTSTR old_name, ASSET_TYPE type);

		//
		//	Animation insertion methods
		//
		void					LoadAnimationsIntoTree ();
		void					LoadAnimationsIntoTree (HTREEITEM hItem);

	  bool					Are_Anims_Restricted () const			{ return m_RestrictAnims; }
	  void					Restrict_Anims (bool onoff);

		//
		//	Texture insertion methods
		//
		void					Toggle_Mesh_Visibility (HTREEITEM hItem);

		// Hides or shows a part of the displayed object by its full name, and updates its eye
		bool					Set_Displayed_Part_Hidden (LPCTSTR name, bool hidden);

		// Narrows the tree to the names that hold the text, a moment after the last call
		void					Request_Filter (LPCTSTR text);
		void					Set_Filter (LPCTSTR text);

		// An object node waiting to be inserted
		struct ObjectEntry
		{
			CString			name;
			ASSET_TYPE		type;
			int				icon;

			// The filter missed the name, so the node stays only for its matching meshes and bones
			bool				parts_only;
		};

		//
		//	Display methods
		//
		void					Display_Asset (HTREEITEM htree_item = nullptr);
		void					Select_Next ();
		void					Select_Prev ();
		void					Reload_Lightmap_Models ();

		//
		// Information methods
		//
		RenderObjClass *	Get_Current_Render_Obj () const;
		AssetInfoClass *	Get_Current_Asset_Info () const;

		// Opens the Material Viewer for the current tree selection: a selected
		// mesh opens pre-selected to that mesh; any other (or no) selection opens
		// the whole currently displayed object. Used by the Ctrl+M hotkey.
		void					OpenSelectionInMaterialViewer ();
		LPCTSTR				GetCurrentSelectionName ();
		ASSET_TYPE			GetCurrentSelectionType ();
		HTREEITEM			FindChildItem (HTREEITEM hParentItem, LPCTSTR pszChildItemName);
		HTREEITEM			FindChildItem (HTREEITEM hParentItem, RenderObjClass *prender_obj);
		// Searches every asset root (Hierarchies, Meshes, LODs, etc.) for an item
		// whose AssetInfo name matches. Used by Reload-Assets restore in MainFrm.cpp.
		HTREEITEM			Find_Asset_Item_By_Name (LPCTSTR pszName);
		HTREEITEM			FindFirstChildItemBasedOnHierarchyName (HTREEITEM hParentItem, LPCTSTR pszHierarchyName);
		HTREEITEM			FindSiblingItemBasedOnHierarchyName (HTREEITEM hCurrentItem, LPCTSTR pszHierarchyName);
		void					Build_Render_Object_List (DynamicVectorClass <CString> &asset_list, HTREEITEM hparent = TVI_ROOT);

		//
		//	Initialization methods
		//
		void					CreateRootNodes ();
		void					Rebuild_State_Images ();

		// TheSuperHackers @performance Tria 25/04/2026 Bulk-release all AssetInfo data
		// and texture refs in one pass, then zero each item's lParam so the subsequent
		// DeleteAllItems() does not fire per-item TVN_DELETEITEM work.
		void					Free_All_Asset_Data ();

	protected:

		///////////////////////////////////////////////////////////////////////
		//	Protected methods
		///////////////////////////////////////////////////////////////////////
		void					Determine_Tree_Location (ASSET_TYPE type, HTREEITEM &hroot, int &icon_index);
		RenderObjClass *	Create_Render_Obj_To_Display (HTREEITEM htree_item);
		void					Add_Emitters_To_Menu (HMENU hmenu, RenderObjClass &render_obj);
		void					Free_Child_Models (HTREEITEM parent_item);
		bool					Is_Mesh_Sub_Object (HTREEITEM hItem) const;
		bool					Is_Mesh_Reference (HTREEITEM hItem) const;
		void					Set_Mesh_Visibility (HTREEITEM hItem, bool visible);
		void					Set_Mesh_Visibility_Recursive (HTREEITEM hItem, bool visible);

		// The folders of an object node, in display order
		enum FolderKind
		{
			FOLDER_NONE = 0,
			FOLDER_MESHES,
			FOLDER_SKELETON,
			FOLDER_ANIMATIONS,
			FOLDER_TEXTURES,
		};

		// The top level nodes for the assets that are not objects, in display order
		enum Category
		{
			CATEGORY_EMITTER = 0,
			CATEGORY_AGGREGATE,
			CATEGORY_COLLECTION,
			CATEGORY_PRIMITIVE,
			CATEGORY_SOUND,
			CATEGORY_TEXTURE,
			CATEGORY_COUNT,
		};

		bool					Is_Object_Node (HTREEITEM hitem) const;
		HTREEITEM			Get_Owner_Object (HTREEITEM hitem) const;
		FolderKind			Get_Folder_Kind (HTREEITEM hitem) const;
		HTREEITEM			Find_Folder (HTREEITEM hobject, FolderKind kind) const;
		HTREEITEM			Add_Folder (HTREEITEM hobject, FolderKind kind);
		void					Update_Folder_Label (HTREEITEM hfolder);
		HTREEITEM			Get_Category_Root (Category category, bool create);
		void					Insert_Object_Nodes (std::vector<ObjectEntry> &entries);
		void					Populate_Object_Node (HTREEITEM hobject);
		void					Add_Parts (HTREEITEM hobject, RenderObjClass *render_obj);
		void					Add_Textures (HTREEITEM hobject, RenderObjClass *render_obj);
		bool					Is_Owner_Shown (HTREEITEM hitem) const;
		HTREEITEM			Show_Owner_Object (HTREEITEM hitem);
		void					Reset_Mesh_Checks (HTREEITEM hobject);
		void					Show_Only_Mesh (HTREEITEM hitem);
		void					Get_Mesh_Rows (HTREEITEM hobject, std::vector<HTREEITEM> &rows) const;
		void					Sync_Mesh_Checks ();
		void					Forget_Tree_Items ();
		bool					Matches_Filter (LPCTSTR name) const;
		bool					Has_Matching_Part (LPCTSTR object_name) const;
		bool					Keep_Matching_Parts (HTREEITEM hobject);
		bool					Prune_Rows (HTREEITEM hparent);

		enum
		{
			FILTER_TIMER_ID = 0x7F17,
			FILTER_DELAY_MS = 200,
		};

	private:

		///////////////////////////////////////////////////////
		//
		//	Private member data
		//
		HTREEITEM	m_hCategoryRoots[CATEGORY_COUNT];
		std::map<HTREEITEM, FolderKind>		m_FolderKinds;

		// Parts that an object node lists, so they are not also listed alone
		HashTemplateClass<StringClass, int>	m_OwnedMeshes;

		// Textures listed under an object; the rest go under Other Textures
		HashTemplateClass<StringClass, int>	m_ListedTextures;

		// The part that Show Only This Mesh left visible
		HTREEITEM	m_hSoloItem;

		// Upper case; empty shows everything
		CString		m_Filter;
		CString		m_PendingFilter;
		bool			m_Rebuilding;
		// TheSuperHackers @feature Owns the TVSIL_NORMAL image list so we can swap
		// between dark and light icon variants on theme change.
		CImageList	m_normalImageList;
		int			m_iAnimationIcon;
		int			m_iTCAnimationIcon;
		int			m_iADAnimationIcon;
		int			m_iMeshIcon;
		int			m_iMaterialIcon;
		int			m_iLODIcon;
		int			m_iEmitterIcon;
		int			m_iPrimitivesIcon;
		int			m_iAggregateIcon;
		int			m_iHierarchyIcon;
		int			m_iSoundIcon;
		int			m_iBoneIcon;
		HTREEITEM	m_hRightClickItem;
		bool			m_RestrictAnims;
		// TheSuperHackers @feature Tria 23/04/2026 Multi-select set for sub-object visibility toggling.
		DynamicVectorClass<HTREEITEM>	m_selectedMeshItems;
		HTREEITEM							m_hLastMultiSelectAnchor;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.
