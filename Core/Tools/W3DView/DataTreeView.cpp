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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3DView                                                      *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Tools/W3DView/DataTreeView.cpp               $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 6/18/01 9:11a                                               $*
 *                                                                                             *
 *                    $Revision:: 35                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "StdAfx.h"
#include <algorithm>
#include <math.h>
#include <set>
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "uxtheme.lib")
#include "W3DView.h"
#include "DataTreeView.h"
#include "WW3D2/rendobj.h"
#include "ViewerAssetMgr.h"
#include "Globals.h"
#include "W3DViewDoc.h"
#include "MainFrm.h"
#include "WW3D2/distlod.h"
#include "WW3D2/animobj.h"
#include "WW3D2/hcanim.h"
#include "AssetInfo.h"
#include "Utils.h"
#include "WWLib/Vector.h"
#include "W3DDarkMode.h"
#include "MaterialViewer/MaterialViewerFrame.h"
#include "WW3D2/part_emt.h"
#include "WW3D2/agg_def.h"
#include "WW3D2/bmp2d.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/w3d_file.h"
#include "ViewerScene.h"
#include "WW3D2/texture.h"
#include "WWLib/hashtemplate.h"
#include "WWLib/wwstring.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/htree.h"

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


////////////////////////////////////////////////////////////////////////////
//	Local Prototypes
////////////////////////////////////////////////////////////////////////////
void Set_Highest_LOD (RenderObjClass *render_obj);


////////////////////////////////////////////////////////////////////////////
//	MFC Stuff
////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNCREATE(CDataTreeView, CTreeView)


////////////////////////////////////////////////////////////////////////////
//
//  CDataTreeView
//
////////////////////////////////////////////////////////////////////////////
CDataTreeView::CDataTreeView ()
        : m_iPrimitivesIcon (-1),
          m_iAnimationIcon (-1),
          m_iTCAnimationIcon(-1),
			 m_iADAnimationIcon(-1),
          m_iMeshIcon (-1),
          m_iMaterialIcon (-1),
          m_iLODIcon (-1),
			 m_iAggregateIcon (-1),
			 m_iEmitterIcon (-1),
			 m_iSoundIcon (-1),
			 m_iBoneIcon (-1),
			 m_hRightClickItem (nullptr),
			 m_RestrictAnims (true),
			 m_hLastMultiSelectAnchor (nullptr),
			 m_hSoloItem (nullptr),
			 m_Rebuilding (false)

{
	for (int category = 0; category < CATEGORY_COUNT; category ++)
	{
		m_hCategoryRoots[category] = nullptr;
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  ~CDataTreeView
//
CDataTreeView::~CDataTreeView ()
{
}


BEGIN_MESSAGE_MAP(CDataTreeView, CTreeView)
	//{{AFX_MSG_MAP(CDataTreeView)
	ON_WM_CREATE()
	ON_NOTIFY_REFLECT(TVN_SELCHANGED, OnSelChanged)
	ON_NOTIFY_REFLECT(TVN_DELETEITEM, OnDeleteItem)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnDblclk)
	ON_NOTIFY_REFLECT(NM_RCLICK, OnRClick)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_TIMER()
	ON_COMMAND(IDM_TOGGLE_MESH_VIS, OnToggleMeshVis)
	ON_COMMAND(IDM_SHOW_ONLY_MESH, OnShowOnlyMesh)
	ON_COMMAND(IDM_SHOW_BONES, OnShowBones)
	ON_COMMAND(IDM_EXPORT_INFO, OnExportInfo)
	ON_COMMAND(IDM_DUMP_W3D_INFO, OnDumpW3DInfo)
	ON_COMMAND(IDM_MATVIEWER_FROM_TREE, OnOpenInMaterialViewer)
	ON_MESSAGE(WM_W3DVIEW_THEME_CHANGED, OnW3DViewThemeChanged)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


////////////////////////////////////////////////////////////////////////////
//
//  OnDraw
//
void
CDataTreeView::OnDraw (CDC *pDC)
{
}

/////////////////////////////////////////////////////////////////////////////
// CDataTreeView diagnostics

#ifdef RTS_DEBUG
void CDataTreeView::AssertValid() const
{
	CTreeView::AssertValid();
}

void CDataTreeView::Dump(CDumpContext& dc) const
{
	CTreeView::Dump(dc);
}
#endif //RTS_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CDataTreeView message handlers


////////////////////////////////////////////////////////////////////////////
//
//  PreCreateWindow
//
BOOL
CDataTreeView::PreCreateWindow (CREATESTRUCT& cs)
{
    // Modify the style bits for the window so it will
    // have buttons and lines between nodes.
    cs.style |= TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;

    // Allow the base class to process this message
	return CTreeView::PreCreateWindow(cs);
}

////////////////////////////////////////////////////////////////////////////
//
//  OnInitialUpdate
//
void
CDataTreeView::OnInitialUpdate ()
{
	// Allow the base class to process this message
    CTreeView::OnInitialUpdate ();

	// TODO: Add your specialized code here and/or call the base class
}


////////////////////////////////////////////////////////////////////////////
//
//  CreateRootNodes
//
//  Called once the tree is empty. A category node is made when its first
//  asset arrives, so this only forgets the old tree.
//
void
CDataTreeView::CreateRootNodes ()
{
	m_OwnedMeshes.Remove_All ();
	m_ListedTextures.Remove_All ();
	Forget_Tree_Items ();
}


////////////////////////////////////////////////////////////////////////////
//
//  Forget_Tree_Items
//
void
CDataTreeView::Forget_Tree_Items ()
{
	for (int category = 0; category < CATEGORY_COUNT; category ++)
	{
		m_hCategoryRoots[category] = nullptr;
	}

	m_FolderKinds.clear ();
	m_selectedMeshItems.Delete_All ();
	m_hLastMultiSelectAnchor = nullptr;
	m_hRightClickItem = nullptr;
	m_hSoloItem = nullptr;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnCreate
//
int
CDataTreeView::OnCreate (LPCREATESTRUCT lpCreateStruct)
{
	if (CTreeView::OnCreate(lpCreateStruct) == -1)
		return -1;

    // Build the tree-node icon list. Pulled into a helper so it can be re-run when
    // the user toggles dark mode at runtime.
    RebuildNormalImageList ();

    // TheSuperHackers @feature Tria 22/04/2026 Themed checkbox state icons for mesh visibility.
    // The actual bitmap construction is delegated to RebuildStateImageList so it can be
    // re-invoked when the user toggles dark mode at runtime.
    RebuildStateImageList ();

    // Create the root nodes that will hold the contain
    // asset types.
    CreateRootNodes ();
	return 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Tree layout
//
//  Objects (hierarchies, LODs and meshes that no object owns) are the top
//  level, in name order. Each owns up to four folders: Meshes, Skeleton,
//  Animations and Textures. The other asset types follow in category nodes,
//  which exist only while they have something to hold.
//
////////////////////////////////////////////////////////////////////////////
namespace
{

const char * const FOLDER_NAMES[] = { "", "Meshes", "Skeleton", "Animations", "Textures" };
const char * const CATEGORY_NAMES[] = { "Emitters", "Aggregates", "Mesh Collections", "Primitives", "Sounds", "Other Textures" };

// Asset names differ in case between a prototype and the object that uses it
StringClass Name_Key (const char *name)
{
	StringClass key (name);
	::_strupr (key.Peek_Buffer ());
	return key;
}

bool Is_Object_Type (ASSET_TYPE type)
{
	return type == TypeHierarchy || type == TypeLOD || type == TypeMesh;
}

void Collect_Textures (RenderObjClass *render_obj, std::map<CString, TextureClass *> &textures)
{
	if (render_obj == nullptr)
	{
		return;
	}

	MaterialInfoClass *material_info = render_obj->Get_Material_Info ();
	if (material_info != nullptr)
	{
		for (int index = 0; index < material_info->Texture_Count (); index ++)
		{
			TextureClass *texture = material_info->Peek_Texture (index);
			if (texture != nullptr)
			{
				textures[CString ((const char *)texture->Get_Full_Path ())] = texture;
			}
		}
		REF_PTR_RELEASE (material_info);
	}

	int sub_count = render_obj->Get_Num_Sub_Objects ();
	for (int index = 0; index < sub_count; index ++)
	{
		RenderObjClass *sub_obj = render_obj->Get_Sub_Object (index);
		if (sub_obj != nullptr)
		{
			Collect_Textures (sub_obj, textures);
			sub_obj->Release_Ref ();
		}
	}
}

int Count_Descendants (CTreeCtrl &tree, HTREEITEM hparent)
{
	int count = 0;
	for (HTREEITEM hchild = tree.GetChildItem (hparent); hchild != nullptr; hchild = tree.GetNextSiblingItem (hchild))
	{
		count += 1 + Count_Descendants (tree, hchild);
	}
	return count;
}

bool Object_Entry_Less (const CDataTreeView::ObjectEntry &left, const CDataTreeView::ObjectEntry &right)
{
	return ::_stricmp (left.name, right.name) < 0;
}

}	// namespace


////////////////////////////////////////////////////////////////////////////
//
//  Is_Object_Node
//
bool
CDataTreeView::Is_Object_Node (HTREEITEM hitem) const
{
	if (hitem == nullptr || GetTreeCtrl ().GetParentItem (hitem) != nullptr)
	{
		return false;
	}

	// A category node is top level too, but carries no asset
	return GetTreeCtrl ().GetItemData (hitem) != 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Owner_Object
//
HTREEITEM
CDataTreeView::Get_Owner_Object (HTREEITEM hitem) const
{
	HTREEITEM htop = hitem;
	while (htop != nullptr && GetTreeCtrl ().GetParentItem (htop) != nullptr)
	{
		htop = GetTreeCtrl ().GetParentItem (htop);
	}
	return Is_Object_Node (htop) ? htop : nullptr;
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Folder_Kind
//
CDataTreeView::FolderKind
CDataTreeView::Get_Folder_Kind (HTREEITEM hitem) const
{
	std::map<HTREEITEM, FolderKind>::const_iterator found = m_FolderKinds.find (hitem);
	return (found != m_FolderKinds.end ()) ? found->second : FOLDER_NONE;
}


////////////////////////////////////////////////////////////////////////////
//
//  Find_Folder
//
HTREEITEM
CDataTreeView::Find_Folder (HTREEITEM hobject, FolderKind kind) const
{
	if (hobject == nullptr)
	{
		return nullptr;
	}

	for (HTREEITEM hchild = GetTreeCtrl ().GetChildItem (hobject); hchild != nullptr; hchild = GetTreeCtrl ().GetNextSiblingItem (hchild))
	{
		if (Get_Folder_Kind (hchild) == kind)
		{
			return hchild;
		}
	}
	return nullptr;
}


////////////////////////////////////////////////////////////////////////////
//
//  Add_Folder
//
HTREEITEM
CDataTreeView::Add_Folder (HTREEITEM hobject, FolderKind kind)
{
	CTreeCtrl &tree = GetTreeCtrl ();

	// Folders keep the order of the enum, whichever is made first
	HTREEITEM hafter = TVI_FIRST;
	for (HTREEITEM hchild = tree.GetChildItem (hobject); hchild != nullptr; hchild = tree.GetNextSiblingItem (hchild))
	{
		FolderKind child_kind = Get_Folder_Kind (hchild);
		if (child_kind == kind)
		{
			return hchild;
		}
		if (child_kind != FOLDER_NONE && child_kind < kind)
		{
			hafter = hchild;
		}
	}

	const int icons[] = { 0, m_iMeshIcon, m_iBoneIcon, m_iAnimationIcon, m_iMaterialIcon };
	HTREEITEM hfolder = tree.InsertItem (FOLDER_NAMES[kind], icons[kind], icons[kind], hobject, hafter);
	m_FolderKinds[hfolder] = kind;

	if (kind == FOLDER_MESHES)
	{
		tree.SetItemState (hfolder, INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);
	}
	return hfolder;
}


////////////////////////////////////////////////////////////////////////////
//
//  Update_Folder_Label
//
void
CDataTreeView::Update_Folder_Label (HTREEITEM hfolder)
{
	FolderKind kind = Get_Folder_Kind (hfolder);
	if (kind != FOLDER_NONE)
	{
		// Under a texture are the meshes that use it, which are not textures
		int count = 0;
		if (kind == FOLDER_TEXTURES)
		{
			for (HTREEITEM hchild = GetTreeCtrl ().GetChildItem (hfolder); hchild != nullptr; hchild = GetTreeCtrl ().GetNextSiblingItem (hchild))
			{
				count ++;
			}
		}
		else
		{
			count = Count_Descendants (GetTreeCtrl (), hfolder);
		}

		CString label;
		label.Format ("%s (%d)", FOLDER_NAMES[kind], count);
		GetTreeCtrl ().SetItemText (hfolder, label);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Category_Root
//
HTREEITEM
CDataTreeView::Get_Category_Root (Category category, bool create)
{
	if (m_hCategoryRoots[category] != nullptr || !create)
	{
		return m_hCategoryRoots[category];
	}

	CTreeCtrl &tree = GetTreeCtrl ();

	// It goes after the category before it, or after the last object when it is the first
	HTREEITEM hafter = TVI_FIRST;
	for (int index = category - 1; index >= 0 && hafter == TVI_FIRST; index --)
	{
		if (m_hCategoryRoots[index] != nullptr)
		{
			hafter = m_hCategoryRoots[index];
		}
	}
	if (hafter == TVI_FIRST)
	{
		for (HTREEITEM hitem = tree.GetRootItem (); hitem != nullptr && tree.GetItemData (hitem) != 0; hitem = tree.GetNextSiblingItem (hitem))
		{
			hafter = hitem;
		}
	}

	const int icons[CATEGORY_COUNT] = { m_iEmitterIcon, m_iAggregateIcon, m_iMeshIcon, m_iPrimitivesIcon, m_iSoundIcon, m_iMaterialIcon };
	m_hCategoryRoots[category] = tree.InsertItem (CATEGORY_NAMES[category], icons[category], icons[category], TVI_ROOT, hafter);
	return m_hCategoryRoots[category];
}


////////////////////////////////////////////////////////////////////////////
//
//  Insert_Object_Nodes
//
void
CDataTreeView::Insert_Object_Nodes (std::vector<ObjectEntry> &entries)
{
	if (entries.empty ())
	{
		return;
	}

	std::sort (entries.begin (), entries.end (), Object_Entry_Less);

	// The object nodes already there are in name order, so one pass over them places every new one
	CTreeCtrl &tree = GetTreeCtrl ();
	HTREEITEM hexisting = tree.GetRootItem ();
	HTREEITEM hprevious = TVI_FIRST;
	for (size_t index = 0; index < entries.size (); index ++)
	{
		const ObjectEntry &entry = entries[index];
		while (hexisting != nullptr && tree.GetItemData (hexisting) != 0 &&
				 ::_stricmp (tree.GetItemText (hexisting), entry.name) <= 0)
		{
			hprevious = hexisting;
			hexisting = tree.GetNextSiblingItem (hexisting);
		}

		HTREEITEM hitem = tree.InsertItem (entry.name, entry.icon, entry.icon, TVI_ROOT, hprevious);
		tree.SetItemData (hitem, (ULONG)new AssetInfoClass (entry.name, entry.type));
		Populate_Object_Node (hitem);
		if (entry.parts_only && !Keep_Matching_Parts (hitem))
		{
			tree.DeleteItem (hitem);
			continue;
		}
		hprevious = hitem;
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Has_Matching_Part
//
//  Whether the filter matches a mesh or a bone of the named object.
//
bool
CDataTreeView::Has_Matching_Part (LPCTSTR object_name) const
{
	RenderObjClass *render_obj = WW3DAssetManager::Get_Instance ()->Create_Render_Obj (object_name);
	if (render_obj == nullptr)
	{
		return false;
	}

	bool found = false;
	int sub_count = render_obj->Get_Num_Sub_Objects ();
	for (int index = 0; index < sub_count && !found; index ++)
	{
		RenderObjClass *sub_obj = render_obj->Get_Sub_Object (index);
		if (sub_obj != nullptr)
		{
			found = Matches_Filter (sub_obj->Get_Name ());
			sub_obj->Release_Ref ();
		}
	}

	const HTreeClass *htree = render_obj->Get_HTree ();
	int pivot_count = (htree != nullptr) ? htree->Num_Pivots () : 0;
	for (int pivot = 1; pivot < pivot_count && !found; pivot ++)
	{
		const char *pivot_name = htree->Get_Bone_Name (pivot);
		found = (pivot_name != nullptr && pivot_name[0] != '\0' && Matches_Filter (pivot_name));
	}

	render_obj->Release_Ref ();
	return found;
}


////////////////////////////////////////////////////////////////////////////
//
//  Keep_Matching_Parts
//
//  Narrows an object's Meshes and Skeleton folders to the rows the filter
//  matches and opens them. Returns false when no row is left.
//
bool
CDataTreeView::Keep_Matching_Parts (HTREEITEM hobject)
{
	CTreeCtrl &tree = GetTreeCtrl ();

	bool any = false;
	const FolderKind kinds[] = { FOLDER_MESHES, FOLDER_SKELETON };
	for (int index = 0; index < 2; index ++)
	{
		HTREEITEM hfolder = Find_Folder (hobject, kinds[index]);
		if (hfolder == nullptr)
		{
			continue;
		}

		if (Prune_Rows (hfolder))
		{
			Update_Folder_Label (hfolder);
			tree.Expand (hfolder, TVE_EXPAND);
			any = true;
		}
		else
		{
			tree.DeleteItem (hfolder);
		}
	}

	if (any)
	{
		tree.Expand (hobject, TVE_EXPAND);
	}
	return any;
}


////////////////////////////////////////////////////////////////////////////
//
//  Prune_Rows
//
//  A row stays when the filter matches it or a row nested below it.
//
bool
CDataTreeView::Prune_Rows (HTREEITEM hparent)
{
	CTreeCtrl &tree = GetTreeCtrl ();

	bool any = false;
	HTREEITEM hchild = tree.GetChildItem (hparent);
	while (hchild != nullptr)
	{
		HTREEITEM hnext = tree.GetNextSiblingItem (hchild);
		AssetInfoClass *info = (AssetInfoClass *)tree.GetItemData (hchild);
		bool has_match_below = Prune_Rows (hchild);
		if (has_match_below || Matches_Filter (tree.GetItemText (hchild)) ||
			 (info != nullptr && Matches_Filter (info->Get_Name ())))
		{
			if (has_match_below)
			{
				tree.Expand (hchild, TVE_EXPAND);
			}
			any = true;
		}
		else
		{
			tree.DeleteItem (hchild);
		}
		hchild = hnext;
	}
	return any;
}


////////////////////////////////////////////////////////////////////////////
//
//  Populate_Object_Node
//
void
CDataTreeView::Populate_Object_Node (HTREEITEM hobject)
{
	AssetInfoClass *object_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hobject);
	if (object_info == nullptr)
	{
		return;
	}

	RenderObjClass *render_obj = WW3DAssetManager::Get_Instance ()->Create_Render_Obj (object_info->Get_Name ());
	if (render_obj == nullptr)
	{
		return;
	}

	if (object_info->Get_Type () != TypeMesh)
	{
		Add_Parts (hobject, render_obj);
	}
	Add_Textures (hobject, render_obj);

	render_obj->Release_Ref ();
}


////////////////////////////////////////////////////////////////////////////
//
//  Add_Parts
//
//  Each pivot is listed once. One that carries a part goes under Meshes,
//  nested below the nearest pivot above it that also carries a part. The
//  bare bones nest the same way under Skeleton.
//
void
CDataTreeView::Add_Parts (HTREEITEM hobject, RenderObjClass *render_obj)
{
	CTreeCtrl &tree = GetTreeCtrl ();

	// A part is named "OBJECT.PART" and hangs on the pivot called "PART"
	std::map<CString, CString> part_names;
	int sub_count = render_obj->Get_Num_Sub_Objects ();
	for (int index = 0; index < sub_count; index ++)
	{
		RenderObjClass *sub_obj = render_obj->Get_Sub_Object (index);
		if (sub_obj == nullptr)
		{
			continue;
		}

		CString full_name (sub_obj->Get_Name ());
		int dot = full_name.Find ('.');
		CString short_name = (dot >= 0) ? full_name.Mid (dot + 1) : full_name;
		short_name.MakeUpper ();
		if (part_names.find (short_name) == part_names.end ())
		{
			part_names[short_name] = full_name;
		}

		StringClass owned_key = Name_Key (full_name);
		if (!m_OwnedMeshes.Exists (owned_key))
		{
			m_OwnedMeshes.Insert (owned_key, 1);
		}
		sub_obj->Release_Ref ();
	}

	HTREEITEM hmeshes = Add_Folder (hobject, FOLDER_MESHES);
	HTREEITEM hskeleton = Add_Folder (hobject, FOLDER_SKELETON);

	const HTreeClass *htree = render_obj->Get_HTree ();
	int pivot_count = (htree != nullptr) ? htree->Num_Pivots () : 0;
	std::vector<HTREEITEM> pivot_items (pivot_count, (HTREEITEM)nullptr);
	std::vector<bool> pivot_has_part (pivot_count, false);
	std::set<CString> placed;

	// Pivot 0 is the root, and a parent always comes before its children
	for (int pivot = 1; pivot < pivot_count; pivot ++)
	{
		const char *pivot_name = htree->Get_Bone_Name (pivot);
		if (pivot_name == nullptr || pivot_name[0] == '\0')
		{
			continue;
		}

		CString key (pivot_name);
		key.MakeUpper ();
		std::map<CString, CString>::const_iterator part = part_names.find (key);
		bool has_part = (part != part_names.end ());
		pivot_has_part[pivot] = has_part;

		int ancestor = htree->Get_Parent_Index (pivot);
		while (ancestor > 0 && (pivot_items[ancestor] == nullptr || pivot_has_part[ancestor] != has_part))
		{
			ancestor = htree->Get_Parent_Index (ancestor);
		}

		HTREEITEM hparent = has_part ? hmeshes : hskeleton;
		if (ancestor > 0)
		{
			hparent = pivot_items[ancestor];
		}

		if (has_part)
		{
			HTREEITEM hitem = tree.InsertItem (pivot_name, m_iMeshIcon, m_iMeshIcon, hparent, TVI_SORT);
			tree.SetItemData (hitem, (ULONG)new AssetInfoClass (part->second, TypeMesh));
			tree.SetItemState (hitem, INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);
			pivot_items[pivot] = hitem;
			placed.insert (key);
		}
		else if (FindChildItem (hparent, pivot_name) == nullptr)
		{
			HTREEITEM hitem = tree.InsertItem (pivot_name, m_iBoneIcon, m_iBoneIcon, hparent, TVI_SORT);
			tree.SetItemData (hitem, (ULONG)new AssetInfoClass (pivot_name, TypeBone));
			pivot_items[pivot] = hitem;
		}
	}

	// A skin has no pivot of its own name, so it would otherwise be missing
	for (std::map<CString, CString>::const_iterator part = part_names.begin (); part != part_names.end (); ++part)
	{
		if (placed.find (part->first) == placed.end ())
		{
			HTREEITEM hitem = tree.InsertItem (part->first, m_iMeshIcon, m_iMeshIcon, hmeshes, TVI_SORT);
			tree.SetItemData (hitem, (ULONG)new AssetInfoClass (part->second, TypeMesh));
			tree.SetItemState (hitem, INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);
		}
	}

	HTREEITEM folders[] = { hmeshes, hskeleton };
	for (int index = 0; index < 2; index ++)
	{
		if (tree.GetChildItem (folders[index]) == nullptr)
		{
			tree.DeleteItem (folders[index]);
		}
		else
		{
			Update_Folder_Label (folders[index]);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Add_Textures
//
void
CDataTreeView::Add_Textures (HTREEITEM hobject, RenderObjClass *render_obj)
{
	std::map<CString, TextureClass *> textures;
	Collect_Textures (render_obj, textures);
	if (textures.empty ())
	{
		return;
	}

	// Which parts use each texture: texture -> part label -> part name
	std::map<CString, std::map<CString, CString> > users;
	int sub_count = render_obj->Get_Num_Sub_Objects ();
	for (int index = 0; index < sub_count; index ++)
	{
		RenderObjClass *sub_obj = render_obj->Get_Sub_Object (index);
		if (sub_obj == nullptr)
		{
			continue;
		}

		std::map<CString, TextureClass *> own_textures;
		Collect_Textures (sub_obj, own_textures);

		CString full_name (sub_obj->Get_Name ());
		int dot = full_name.Find ('.');
		CString short_name = (dot >= 0) ? full_name.Mid (dot + 1) : full_name;
		for (std::map<CString, TextureClass *>::const_iterator own = own_textures.begin (); own != own_textures.end (); ++own)
		{
			users[own->first][short_name] = full_name;
		}
		sub_obj->Release_Ref ();
	}

	// The maps are ordered by name, so the rows go in sorted
	CTreeCtrl &tree = GetTreeCtrl ();
	HTREEITEM hfolder = Add_Folder (hobject, FOLDER_TEXTURES);
	for (std::map<CString, TextureClass *>::const_iterator texture = textures.begin (); texture != textures.end (); ++texture)
	{
		if (FindChildItem (hfolder, texture->first) != nullptr)
		{
			continue;
		}

		HTREEITEM hitem = tree.InsertItem (texture->first, m_iMaterialIcon, m_iMaterialIcon, hfolder, TVI_LAST);
		texture->second->Add_Ref ();
		tree.SetItemData (hitem, (ULONG)new AssetInfoClass (texture->first, TypeMaterial, nullptr, (DWORD)texture->second));

		const std::map<CString, CString> &parts = users[texture->first];
		for (std::map<CString, CString>::const_iterator part = parts.begin (); part != parts.end (); ++part)
		{
			// Made as a material, because the mesh type would build a render object just to name this row
			AssetInfoClass *part_info = new AssetInfoClass (part->second, TypeMaterial);
			part_info->Set_Type (TypeMesh);
			HTREEITEM hpart = tree.InsertItem (part->first, m_iMeshIcon, m_iMeshIcon, hitem, TVI_LAST);
			tree.SetItemData (hpart, (ULONG)part_info);
		}

		StringClass listed_key = Name_Key (texture->first);
		if (!m_ListedTextures.Exists (listed_key))
		{
			m_ListedTextures.Insert (listed_key, 1);
		}
	}
	Update_Folder_Label (hfolder);
}


////////////////////////////////////////////////////////////////////////////
//
//  LoadAssetsIntoTree
//
void
CDataTreeView::LoadAssetsIntoTree ()
{
	CTreeCtrl &tree = GetTreeCtrl ();

	// TheSuperHackers @performance Tria 2026-05-19 Lock all window updates while we
	// bulk-mutate the tree. SetRedraw(FALSE) alone does not stop the control from
	// recomputing scrollbar ranges and item layout on every insert.
	::LockWindowUpdate (tree.GetSafeHwnd ());
	tree.SetRedraw (FALSE);

	// What the tree already holds, so a second load only adds
	HashTemplateClass<StringClass, int> existing_objects;
	for (HTREEITEM hitem = tree.GetRootItem (); hitem != nullptr; hitem = tree.GetNextSiblingItem (hitem))
	{
		AssetInfoClass *info = (AssetInfoClass *)tree.GetItemData (hitem);
		if (info != nullptr)
		{
			existing_objects.Insert (Name_Key (info->Get_Name ()), 1);
		}
	}

	HashTemplateClass<StringClass, int> existing_items[CATEGORY_COUNT];
	bool category_changed[CATEGORY_COUNT] = { false };
	for (int category = 0; category < CATEGORY_COUNT; category ++)
	{
		for (HTREEITEM hitem = tree.GetChildItem (m_hCategoryRoots[category]); m_hCategoryRoots[category] != nullptr && hitem != nullptr; hitem = tree.GetNextSiblingItem (hitem))
		{
			existing_items[category].Insert (Name_Key (tree.GetItemText (hitem)), 1);
		}
	}

	std::vector<ObjectEntry> new_objects;
	std::vector<CString> mesh_names;

	// Objects the filter leaves out still own their parts
	std::set<CString> filtered_objects;
	DynamicVectorClass<CString> dist_lod_list;

	RenderObjIterator *asset_iterator = WW3DAssetManager::Get_Instance ()->Create_Render_Obj_Iterator ();
	ASSERT (asset_iterator != nullptr);
	if (asset_iterator != nullptr)
	{
		for (asset_iterator->First (); asset_iterator->Is_Done () == FALSE; asset_iterator->Next ())
		{
			LPCTSTR name = asset_iterator->Current_Item_Name ();

			int category = -1;
			ASSET_TYPE type = TypeUnknown;
			int icon = -1;
			bool is_object = false;

			switch (asset_iterator->Current_Item_Class_ID ())
			{
				case RenderObjClass::CLASSID_MESH:
					// Placed after the objects, once it is known which of them owns it
					mesh_names.push_back (name);
					continue;

				case RenderObjClass::CLASSID_COLLECTION:
					category = CATEGORY_COLLECTION;
					type = TypeMesh;
					icon = m_iMeshIcon;
					break;

				case RenderObjClass::CLASSID_SOUND:
					category = CATEGORY_SOUND;
					type = TypeSound;
					icon = m_iSoundIcon;
					break;

				case RenderObjClass::CLASSID_PARTICLEEMITTER:
					category = CATEGORY_EMITTER;
					type = TypeEmitter;
					icon = m_iEmitterIcon;
					break;

				case RenderObjClass::CLASSID_SPHERE:
				case RenderObjClass::CLASSID_RING:
					category = CATEGORY_PRIMITIVE;
					type = TypePrimitives;
					icon = m_iPrimitivesIcon;
					break;

				case RenderObjClass::CLASSID_DISTLOD:
					dist_lod_list.Add (name);
				case RenderObjClass::CLASSID_HLOD:
					is_object = true;
					type = TypeHierarchy;
					icon = m_iHierarchyIcon;
					if (::Is_Real_LOD (name))
					{
						type = TypeLOD;
						icon = m_iLODIcon;
					}
					break;

				default:
					continue;
			}

			if (::Is_Aggregate (name))
			{
				is_object = false;
				category = CATEGORY_AGGREGATE;
				type = TypeAggregate;
				icon = m_iAggregateIcon;
			}

			StringClass key = Name_Key (name);
			if (!Matches_Filter (name))
			{
				if (is_object)
				{
					filtered_objects.insert (CString ((const char *)key));

					// It stays if one of its meshes or bones matches
					if (!existing_objects.Exists (key) && Has_Matching_Part (name))
					{
						ObjectEntry entry = { name, type, icon, true };
						new_objects.push_back (entry);
						existing_objects.Insert (key, 1);
					}
				}
			}
			else if (is_object)
			{
				if (!existing_objects.Exists (key))
				{
					ObjectEntry entry = { name, type, icon, false };
					new_objects.push_back (entry);
					existing_objects.Insert (key, 1);
				}
			}
			else if (!existing_items[category].Exists (key))
			{
				HTREEITEM hroot = Get_Category_Root ((Category)category, true);
				HTREEITEM hitem = tree.InsertItem (name, icon, icon, hroot, TVI_LAST);
				tree.SetItemData (hitem, (ULONG)new AssetInfoClass (name, type));
				existing_items[category].Insert (key, 1);
				category_changed[category] = true;
			}
		}

		SAFE_DELETE (asset_iterator);
	}

	// Loop through all the old-style dist lod's and convert their prototypes
	// to the new HLOD format
	for (int index = 0; index < dist_lod_list.Count (); index ++)
	{
		HLodClass *plod = (HLodClass *)WW3DAssetManager::Get_Instance ()->Create_Render_Obj (dist_lod_list[index]);
		if (plod != nullptr)
		{
			HLodDefClass *definition = new HLodDefClass (*plod);
			HLodPrototypeClass *prototype = new HLodPrototypeClass (definition);
			WW3DAssetManager::Get_Instance ()->Remove_Prototype (dist_lod_list[index]);
			WW3DAssetManager::Get_Instance ()->Add_Prototype (prototype);
		}
	}

	Insert_Object_Nodes (new_objects);

	// A mesh that no object uses is an object of its own. The viewer's light gizmo has no source file.
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	std::vector<ObjectEntry> new_meshes;
	for (size_t index = 0; index < mesh_names.size (); index ++)
	{
		if (pdoc != nullptr && !pdoc->GetAssetSourceFileMap ().Exists (StringClass (mesh_names[index])))
		{
			continue;
		}

		// A part is named after its object, which the filter may have left out
		CString owner (mesh_names[index]);
		int dot = owner.Find ('.');
		owner = (dot >= 0) ? owner.Left (dot) : CString ();
		owner.MakeUpper ();
		if (!Matches_Filter (mesh_names[index]) || filtered_objects.find (owner) != filtered_objects.end ())
		{
			continue;
		}

		StringClass key = Name_Key (mesh_names[index]);
		if (!m_OwnedMeshes.Exists (key) && !existing_objects.Exists (key))
		{
			ObjectEntry entry = { mesh_names[index], TypeMesh, m_iMeshIcon, false };
			new_meshes.push_back (entry);
		}
	}
	Insert_Object_Nodes (new_meshes);

	// A mesh loaded before its object was listed alone until now
	HTREEITEM hitem = tree.GetRootItem ();
	while (hitem != nullptr)
	{
		HTREEITEM hnext = tree.GetNextSiblingItem (hitem);
		AssetInfoClass *info = (AssetInfoClass *)tree.GetItemData (hitem);
		if (info != nullptr && info->Get_Type () == TypeMesh && m_OwnedMeshes.Exists (Name_Key (info->Get_Name ())))
		{
			tree.DeleteItem (hitem);
		}
		hitem = hnext;
	}

	LoadAnimationsIntoTree ();

	// Textures that no listed object uses, such as an emitter's
	HashTemplateIterator<StringClass, TextureClass *> texture_iterator (WW3DAssetManager::Get_Instance ()->Texture_Hash ());
	for (texture_iterator.First (); !texture_iterator.Is_Done (); texture_iterator.Next ())
	{
		TextureClass *texture = texture_iterator.Peek_Value ();
		if (texture == nullptr)
		{
			continue;
		}

		StringClass key = Name_Key (texture->Get_Full_Path ());
		if (!Matches_Filter (texture->Get_Full_Path ()) || m_ListedTextures.Exists (key) || existing_items[CATEGORY_TEXTURE].Exists (key))
		{
			continue;
		}

		HTREEITEM hroot = Get_Category_Root (CATEGORY_TEXTURE, true);
		HTREEITEM htexture = tree.InsertItem (texture->Get_Full_Path (), m_iMaterialIcon, m_iMaterialIcon, hroot, TVI_LAST);
		texture->Add_Ref ();
		tree.SetItemData (htexture, (ULONG)new AssetInfoClass (texture->Get_Full_Path (), TypeMaterial, nullptr, (DWORD)texture));
		existing_items[CATEGORY_TEXTURE].Insert (key, 1);
		category_changed[CATEGORY_TEXTURE] = true;
	}

	// TheSuperHackers @performance Tria 18/04/2026 Sort each parent node once instead of per-insert.
	for (int category = 0; category < CATEGORY_COUNT; category ++)
	{
		if (category_changed[category])
		{
			tree.SortChildren (m_hCategoryRoots[category]);
		}
	}

	tree.SetRedraw (TRUE);

	// TheSuperHackers @performance Tria 2026-05-19 Release the window-update lock
	// before invalidating so the final repaint actually happens.
	::LockWindowUpdate (nullptr);

	Invalidate (FALSE);
	UpdateWindow ();
}


////////////////////////////////////////////////////////////////////////////
//
//  LoadAnimationsIntoTree
//
void
CDataTreeView::LoadAnimationsIntoTree ()
{
	CTreeCtrl &tree = GetTreeCtrl ();

	// Skeleton name -> the nodes that can play its animations. One shared key matches all while unrestricted.
	std::map<CString, std::vector<HTREEITEM> > targets;
	for (int pass = 0; pass < 2; pass ++)
	{
		HTREEITEM hfirst = (pass == 0) ? tree.GetRootItem () : tree.GetChildItem (m_hCategoryRoots[CATEGORY_AGGREGATE]);
		if (pass == 1 && m_hCategoryRoots[CATEGORY_AGGREGATE] == nullptr)
		{
			hfirst = nullptr;
		}

		for (HTREEITEM hitem = hfirst; hitem != nullptr; hitem = tree.GetNextSiblingItem (hitem))
		{
			AssetInfoClass *info = (AssetInfoClass *)tree.GetItemData (hitem);
			if (info == nullptr || info->Get_Type () == TypeMesh)
			{
				continue;
			}
			targets[m_RestrictAnims ? info->Get_Hierarchy_Name () : CString ("*")].push_back (hitem);
		}
	}

	std::set<HTREEITEM> changed;

	AssetIterator *anim_iterator = WW3DAssetManager::Get_Instance ()->Create_HAnim_Iterator ();
	ASSERT (anim_iterator != nullptr);
	if (anim_iterator != nullptr)
	{
		for (anim_iterator->First (); anim_iterator->Is_Done () == FALSE; anim_iterator->Next ())
		{
			LPCTSTR anim_name = anim_iterator->Current_Item_Name ();
			HAnimClass *anim = WW3DAssetManager::Get_Instance ()->Get_HAnim (anim_name);
			ASSERT (anim != nullptr);
			if (anim == nullptr)
			{
				continue;
			}

			std::map<CString, std::vector<HTREEITEM> >::const_iterator found = targets.find (m_RestrictAnims ? CString (anim->Get_HName ()) : CString ("*"));
			if (found != targets.end ())
			{
				for (size_t index = 0; index < found->second.size (); index ++)
				{
					// An object keeps its animations in a folder; an aggregate lists them directly
					HTREEITEM htarget = found->second[index];
					if (Is_Object_Node (htarget))
					{
						htarget = Add_Folder (htarget, FOLDER_ANIMATIONS);
					}

					if (FindChildItem (htarget, anim_name) == nullptr)
					{
						HTREEITEM hanim = tree.InsertItem (anim_name, m_iAnimationIcon, m_iAnimationIcon, htarget, TVI_LAST);
						tree.SetItemData (hanim, (ULONG)new AssetInfoClass (anim_name, TypeAnimation));
						changed.insert (htarget);
					}
				}
			}

			REF_PTR_RELEASE (anim);
		}

		delete anim_iterator;
		anim_iterator = nullptr;
	}

	for (std::set<HTREEITEM>::const_iterator htarget = changed.begin (); htarget != changed.end (); ++htarget)
	{
		tree.SortChildren (*htarget);
		Update_Folder_Label (*htarget);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  LoadAnimationsIntoTree
//
void
CDataTreeView::LoadAnimationsIntoTree (HTREEITEM hItem)
{
	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hItem);
	ASSERT (asset_info != nullptr);
	if (asset_info == nullptr)
	{
		return;
	}

	HTREEITEM htarget = nullptr;

	AssetIterator *anim_iterator = WW3DAssetManager::Get_Instance ()->Create_HAnim_Iterator ();
	ASSERT (anim_iterator != nullptr);
	if (anim_iterator != nullptr)
	{
		for (anim_iterator->First (); anim_iterator->Is_Done () == FALSE; anim_iterator->Next ())
		{
			LPCTSTR anim_name = anim_iterator->Current_Item_Name ();
			HAnimClass *anim = WW3DAssetManager::Get_Instance ()->Get_HAnim (anim_name);
			ASSERT (anim != nullptr);
			if (anim == nullptr)
			{
				continue;
			}

			if (::lstrcmp (asset_info->Get_Hierarchy_Name (), anim->Get_HName ()) == 0)
			{
				// The folder is made for the first animation, so an object without any has none
				if (htarget == nullptr)
				{
					htarget = Is_Object_Node (hItem) ? Add_Folder (hItem, FOLDER_ANIMATIONS) : hItem;
				}

				if (FindChildItem (htarget, anim_name) == nullptr)
				{
					HTREEITEM hanim = GetTreeCtrl ().InsertItem (anim_name, m_iAnimationIcon, m_iAnimationIcon, htarget, TVI_SORT);
					GetTreeCtrl ().SetItemData (hanim, (ULONG)new AssetInfoClass (anim_name, TypeAnimation));
				}
			}

			REF_PTR_RELEASE (anim);
		}

		delete anim_iterator;
		anim_iterator = nullptr;
	}

	if (htarget != nullptr)
	{
		Update_Folder_Label (htarget);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Determine_Tree_Location
//
void
CDataTreeView::Determine_Tree_Location
(
	ASSET_TYPE type,
	HTREEITEM &hroot,
	int &icon_index
)
{
	// What type of asset is this?
	switch (type) {

		case TypeMesh:
			hroot			= TVI_ROOT;
			icon_index	= m_iMeshIcon;
			break;

		case TypeHierarchy:
			hroot			= TVI_ROOT;
			icon_index	= m_iHierarchyIcon;
			break;

		case TypeLOD:
			hroot			= TVI_ROOT;
			icon_index	= m_iLODIcon;
			break;

		case TypeSound:
			hroot			= Get_Category_Root (CATEGORY_SOUND, true);
			icon_index	= m_iSoundIcon;
			break;

		case TypeAggregate:
			hroot			= Get_Category_Root (CATEGORY_AGGREGATE, true);
			icon_index	= m_iAggregateIcon;
			break;

		case TypeEmitter:
			hroot			= Get_Category_Root (CATEGORY_EMITTER, true);
			icon_index	= m_iEmitterIcon;
			break;

		case TypePrimitives:
			hroot			= Get_Category_Root (CATEGORY_PRIMITIVE, true);
			icon_index	= m_iPrimitivesIcon;
			break;
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Add_Asset_To_Tree
//
bool
CDataTreeView::Add_Asset_To_Tree
(
	LPCTSTR name,
	ASSET_TYPE type,
	bool bselect
)
{
	// Assume failure
	bool retval = false;

	// Param OK?
	ASSERT (name != nullptr);
	if (name != nullptr) {

		// Turn off repainting
		GetTreeCtrl ().SetRedraw (FALSE);

		// Determime where this asset should go
		HTREEITEM hparent = nullptr;
		int icon_index = 0;
		Determine_Tree_Location (type, hparent, icon_index);

		// Is this asset already in the tree?
		HTREEITEM htree_item = FindChildItem (hparent, name);
		if (htree_item == nullptr && hparent != nullptr) {

			if (Is_Object_Type (type)) {

				// An object node also gets its folders, and its place in the name order
				std::vector<ObjectEntry> entries;
				ObjectEntry entry = { name, type, icon_index };
				entries.push_back (entry);
				Insert_Object_Nodes (entries);
				htree_item = FindChildItem (hparent, name);

			} else {

				// Add this object to the tree
				htree_item = GetTreeCtrl ().InsertItem (name,
																	 icon_index,
																	 icon_index,
																	 hparent,
																	 TVI_SORT);

				// Associate the render object with its entry in the tree
				GetTreeCtrl ().SetItemData (htree_item, (ULONG)new AssetInfoClass (name, type));
			}

			// Load the object's animations into the tree (if necessary)
			AssetInfoClass *asset_info = (htree_item != nullptr) ? (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item) : nullptr;
			if (asset_info != nullptr && asset_info->Can_Asset_Have_Animations ()) {
				LoadAnimationsIntoTree (htree_item);
			}

			// Success!
			retval = (htree_item != nullptr);
		}

		// Select the instance (if requested)
		if (bselect && htree_item != nullptr) {
			GetTreeCtrl ().SelectItem (htree_item);
			GetTreeCtrl ().EnsureVisible (htree_item);
		}

		// Turn painting back on
		GetTreeCtrl ().SetRedraw (TRUE);

		// Force the window to be repainted
		Invalidate (FALSE);
		UpdateWindow ();
	}

	// Return the true/false result code
	return retval;
}


////////////////////////////////////////////////////////////////////////////
//
//  FindChildItem
//
HTREEITEM
CDataTreeView::FindChildItem
(
	HTREEITEM hParentItem,
	RenderObjClass *prender_obj
)
{
	// Assume we won't find the item
	HTREEITEM hchild_item = nullptr;

	// Loop through all the children of this node
	for (HTREEITEM htree_item = GetTreeCtrl ().GetChildItem (hParentItem);
		  (htree_item != nullptr) && (hchild_item == nullptr);
		  htree_item = GetTreeCtrl ().GetNextSiblingItem (htree_item)) {

		// Get the data associated with this item
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);

		// Is this the item we were looking for?
		if (asset_info &&
			 asset_info->Peek_Render_Obj () == prender_obj) {

			// This was the item we were looking for, return
			// its handle to the caller
			hchild_item = htree_item;
		}
	}

	// Return the child item handle
	return hchild_item;
}


////////////////////////////////////////////////////////////////////////////
//
//  FindChildItem
//
HTREEITEM
CDataTreeView::FindChildItem
(
    HTREEITEM hParentItem,
    LPCTSTR pszChildItemName
)
{
    // Assume we won't find the item
    HTREEITEM hChildItem = nullptr;

    // Loop through all the children of this node
    for (HTREEITEM hTreeItem = GetTreeCtrl ().GetChildItem (hParentItem);
         (hTreeItem != nullptr) && (hChildItem == nullptr);
         hTreeItem = GetTreeCtrl ().GetNextSiblingItem (hTreeItem))
    {
        // Is this the child item we were looking for?
        if (::lstrcmp (GetTreeCtrl ().GetItemText (hTreeItem), pszChildItemName) == 0)
        {
            // This was the child item we were looking for, return
            // its handle to the caller
            hChildItem = hTreeItem;
        }
    }

    // Return the child item handle
    return hChildItem;
}


////////////////////////////////////////////////////////////////////////////
//
//  Find_Asset_Item_By_Name
//
//  Walks the whole tree (recursively, all roots) and returns the first
//  HTREEITEM whose AssetInfoClass::Get_Name() matches pszName. Used by
//  Reload-Assets restore (MainFrm.cpp) to re-select the previously
//  displayed asset after the tree has been rebuilt.
//
////////////////////////////////////////////////////////////////////////////
static HTREEITEM
Find_Asset_Item_By_Name_Recursive (CTreeCtrl &tree, HTREEITEM hparent, LPCTSTR pszName)
{
	for (HTREEITEM htree_item = tree.GetChildItem (hparent);
		  htree_item != nullptr;
		  htree_item = tree.GetNextSiblingItem (htree_item)) {

		AssetInfoClass *asset_info = (AssetInfoClass *)tree.GetItemData (htree_item);
		if (asset_info != nullptr && ::lstrcmpi (asset_info->Get_Name (), pszName) == 0) {
			return htree_item;
		}

		if (tree.ItemHasChildren (htree_item)) {
			HTREEITEM hfound = Find_Asset_Item_By_Name_Recursive (tree, htree_item, pszName);
			if (hfound != nullptr) {
				return hfound;
			}
		}
	}

	return nullptr;
}

HTREEITEM
CDataTreeView::Find_Asset_Item_By_Name (LPCTSTR pszName)
{
	if (pszName == nullptr || pszName[0] == '\0') {
		return nullptr;
	}
	return Find_Asset_Item_By_Name_Recursive (GetTreeCtrl (), TVI_ROOT, pszName);
}


////////////////////////////////////////////////////////////////////////////
//
//  FindSiblingItemBasedOnHierarchyName
//
HTREEITEM
CDataTreeView::FindSiblingItemBasedOnHierarchyName
(
    HTREEITEM hCurrentItem,
    LPCTSTR pszHierarchyName
)
{
    // Assume we won't find the item
    HTREEITEM hSiblingItem = nullptr;

    // Loop through all the siblings of this node
    HTREEITEM hTreeItem = hCurrentItem;
    while (((hTreeItem = GetTreeCtrl ().GetNextSiblingItem (hTreeItem)) != nullptr) &&
           (hSiblingItem == nullptr))
    {
        // Get the data associated with this item
        AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hTreeItem);

        // Is this the item we were looking for?
        if (m_RestrictAnims == false ||
				(asset_info && ::lstrcmp (asset_info->Get_Hierarchy_Name (), pszHierarchyName) == 0))
        {
            // This was the item we were looking for, return
            // its handle to the caller
            hSiblingItem = hTreeItem;
        }
    }

    // Return the sibling item handle
    return hSiblingItem;
}

////////////////////////////////////////////////////////////////////////////
//
//  FindFirstChildItemBasedOnHierarchyName
//
HTREEITEM
CDataTreeView::FindFirstChildItemBasedOnHierarchyName
(
    HTREEITEM hParentItem,
    LPCTSTR pszHierarchyName
)
{
    // Assume we won't find the item
    HTREEITEM hChildItem = nullptr;

    // Loop through all the children of this node
    for (HTREEITEM hTreeItem = GetTreeCtrl ().GetChildItem (hParentItem);
         (hTreeItem != nullptr) && (hChildItem == nullptr);
         hTreeItem = GetTreeCtrl ().GetNextSiblingItem (hTreeItem))
    {
        // Get the data associated with this item
        AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hTreeItem);

        //
		  // Is this the item we were looking for?
		  //
        if (m_RestrictAnims == false ||
				(asset_info && ::lstrcmp (asset_info->Get_Hierarchy_Name (), pszHierarchyName) == 0))
        {
            // This was the child item we were looking for, return
            // its handle to the caller
            hChildItem = hTreeItem;
        }
    }

    // Return the child item handle
    return hChildItem;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSelChanged
//
void
CDataTreeView::OnSelChanged
(
    NMHDR* pNMHDR,
    LRESULT* pResult
)
{
	(*pResult) = 0;
	if (m_Rebuilding)
	{
		return;
	}

	// Display the new selection
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
	Display_Asset (pNMTreeView->itemNew.hItem);

	// TheSuperHackers @feature Tria 18/04/2026 Update the title bar with the selected asset name.
	HTREEITEM hItem = pNMTreeView->itemNew.hItem;
	if (hItem != nullptr) {
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hItem);
		if (asset_info != nullptr) {
			CDocument *pDoc = GetDocument ();
			if (pDoc != nullptr) {
				pDoc->SetTitle (asset_info->Get_Name ());
			}
		}
	}

	(*pResult) = 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Is_Owner_Shown
//
bool
CDataTreeView::Is_Owner_Shown (HTREEITEM hitem) const
{
	HTREEITEM hobject = Get_Owner_Object (hitem);
	AssetInfoClass *object_info = (hobject != nullptr) ? (AssetInfoClass *)GetTreeCtrl ().GetItemData (hobject) : nullptr;
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (object_info == nullptr || pdoc == nullptr)
	{
		return false;
	}

	RenderObjClass *displayed = pdoc->GetDisplayedObject ();
	return displayed != nullptr && ::lstrcmpi (displayed->Get_Name (), object_info->Get_Name ()) == 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Show_Owner_Object
//
//  The folders and parts of an object act on the object in the viewport, so
//  their object is put there first when another one is showing.
//
HTREEITEM
CDataTreeView::Show_Owner_Object (HTREEITEM hitem)
{
	HTREEITEM hobject = Get_Owner_Object (hitem);
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (hobject == nullptr || pdoc == nullptr || Is_Owner_Shown (hitem))
	{
		return hobject;
	}

	RenderObjClass *render_obj = Create_Render_Obj_To_Display (hobject);
	pdoc->DisplayObject (render_obj);
	REF_PTR_RELEASE (render_obj);
	Reset_Mesh_Checks (hobject);

	CMainFrame *main_wnd = (CMainFrame *)::AfxGetMainWnd ();
	AssetInfoClass *object_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hobject);
	if (main_wnd != nullptr && object_info != nullptr)
	{
		main_wnd->OnSelectionChanged (object_info->Get_Type ());
	}
	return hobject;
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Mesh_Checks
//
//  A freshly made render object shows every part.
//
void
CDataTreeView::Reset_Mesh_Checks (HTREEITEM hobject)
{
	m_hSoloItem = nullptr;

	HTREEITEM hmeshes = Find_Folder (hobject, FOLDER_MESHES);
	if (hmeshes == nullptr)
	{
		return;
	}

	GetTreeCtrl ().SetItemState (hmeshes, INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);

	std::vector<HTREEITEM> rows;
	Get_Mesh_Rows (hobject, rows);
	for (size_t index = 0; index < rows.size (); index ++)
	{
		GetTreeCtrl ().SetItemState (rows[index], INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Mesh_Rows
//
void
CDataTreeView::Get_Mesh_Rows (HTREEITEM hobject, std::vector<HTREEITEM> &rows) const
{
	// Parts nest, and the bones nested among them are not parts
	std::vector<HTREEITEM> pending;
	pending.push_back (Find_Folder (hobject, FOLDER_MESHES));
	while (!pending.empty ())
	{
		HTREEITEM hparent = pending.back ();
		pending.pop_back ();
		if (hparent == nullptr)
		{
			continue;
		}

		for (HTREEITEM hchild = GetTreeCtrl ().GetChildItem (hparent); hchild != nullptr; hchild = GetTreeCtrl ().GetNextSiblingItem (hchild))
		{
			AssetInfoClass *info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hchild);
			if (info != nullptr && info->Get_Type () == TypeMesh)
			{
				rows.push_back (hchild);
			}
			pending.push_back (hchild);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Sync_Mesh_Checks
//
//  Sets the eyes from the object in the viewport, for rows that were just rebuilt.
//
void
CDataTreeView::Sync_Mesh_Checks ()
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	RenderObjClass *displayed = (pdoc != nullptr) ? pdoc->GetDisplayedObject () : nullptr;
	if (displayed == nullptr)
	{
		return;
	}

	HTREEITEM hobject = FindChildItem (TVI_ROOT, displayed->Get_Name ());
	if (!Is_Object_Node (hobject))
	{
		return;
	}

	std::vector<HTREEITEM> rows;
	Get_Mesh_Rows (hobject, rows);
	for (size_t index = 0; index < rows.size (); index ++)
	{
		AssetInfoClass *info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (rows[index]);
		RenderObjClass *sub_obj = displayed->Get_Sub_Object_By_Name (info->Get_Name (), nullptr);
		if (sub_obj != nullptr)
		{
			GetTreeCtrl ().SetItemState (rows[index], INDEXTOSTATEIMAGEMASK (sub_obj->Is_Hidden () ? 1 : 2), TVIS_STATEIMAGEMASK);
			sub_obj->Release_Ref ();
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Matches_Filter
//
bool
CDataTreeView::Matches_Filter (LPCTSTR name) const
{
	if (m_Filter.IsEmpty ())
	{
		return true;
	}

	CString upper (name);
	upper.MakeUpper ();
	return upper.Find (m_Filter) >= 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Request_Filter
//
//  The filter is applied a moment after the last keystroke, because each
//  change rebuilds the tree.
//
void
CDataTreeView::Request_Filter (LPCTSTR text)
{
	m_PendingFilter = text;
	SetTimer (FILTER_TIMER_ID, FILTER_DELAY_MS, nullptr);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnTimer
//
void
CDataTreeView::OnTimer (UINT_PTR nIDEvent)
{
	if (nIDEvent == FILTER_TIMER_ID)
	{
		KillTimer (FILTER_TIMER_ID);
		Set_Filter (m_PendingFilter);
		return;
	}

	CTreeView::OnTimer (nIDEvent);
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Filter
//
//  Shows only the objects and assets whose name holds the text. A tree
//  control cannot hide a row, so the tree is rebuilt from the asset manager.
//  The viewport is left alone.
//
void
CDataTreeView::Set_Filter (LPCTSTR text)
{
	CString filter (text);
	filter.Trim ();
	filter.MakeUpper ();
	if (filter == m_Filter)
	{
		return;
	}
	m_Filter = filter;

	// The selection comes back by name, or by its object's name when it was a folder
	CTreeCtrl &tree = GetTreeCtrl ();
	CString selected_name;
	HTREEITEM hselected = tree.GetSelectedItem ();
	AssetInfoClass *selected_info = (hselected != nullptr) ? (AssetInfoClass *)tree.GetItemData (hselected) : nullptr;
	if (selected_info == nullptr && Get_Owner_Object (hselected) != nullptr)
	{
		selected_info = (AssetInfoClass *)tree.GetItemData (Get_Owner_Object (hselected));
	}
	if (selected_info != nullptr)
	{
		selected_name = selected_info->Get_Name ();
	}

	// With nothing selected, the object in the viewport is the one to come back to
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (selected_name.IsEmpty () && pdoc != nullptr && pdoc->GetDisplayedObject () != nullptr)
	{
		selected_name = pdoc->GetDisplayedObject ()->Get_Name ();
	}

	// Selection changes made by the rebuild must not reach the viewport
	m_Rebuilding = true;
	tree.SetRedraw (FALSE);
	Free_All_Asset_Data ();
	tree.DeleteAllItems ();
	Forget_Tree_Items ();
	LoadAssetsIntoTree ();
	Sync_Mesh_Checks ();

	HTREEITEM hitem = Find_Asset_Item_By_Name (selected_name);
	if (hitem != nullptr)
	{
		tree.SelectItem (hitem);
		tree.EnsureVisible (hitem);
	}
	tree.SetRedraw (TRUE);
	tree.Invalidate ();
	m_Rebuilding = false;
}


////////////////////////////////////////////////////////////////////////////
//
//  Show_Only_Mesh
//
//  Hides every other part of the object and frames the camera on this one.
//  A second call on the same row brings the whole object back.
//
void
CDataTreeView::Show_Only_Mesh (HTREEITEM hitem)
{
	CTreeCtrl &tree = GetTreeCtrl ();
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	AssetInfoClass *asset_info = (hitem != nullptr) ? (AssetInfoClass *)tree.GetItemData (hitem) : nullptr;
	if (pdoc == nullptr || asset_info == nullptr || asset_info->Get_Type () != TypeMesh || !Is_Mesh_Sub_Object (hitem))
	{
		return;
	}

	// Showing the object resets the solo row, so remember whether this row was it
	bool restore = (m_hSoloItem == hitem) && Is_Owner_Shown (hitem);
	HTREEITEM hobject = Show_Owner_Object (hitem);
	HTREEITEM hmeshes = Find_Folder (hobject, FOLDER_MESHES);
	RenderObjClass *displayed = pdoc->GetDisplayedObject ();
	CGraphicView *view = pdoc->GetGraphicView ();
	if (hmeshes == nullptr || displayed == nullptr)
	{
		return;
	}

	// The filter can leave a part without a row
	int sub_count = displayed->Get_Num_Sub_Objects ();
	for (int index = 0; index < sub_count; index ++)
	{
		RenderObjClass *part = displayed->Get_Sub_Object (index);
		if (part != nullptr)
		{
			part->Set_Hidden (!restore);
			part->Release_Ref ();
		}
	}

	for (HTREEITEM hchild = tree.GetChildItem (hmeshes); hchild != nullptr; hchild = tree.GetNextSiblingItem (hchild))
	{
		Set_Mesh_Visibility_Recursive (hchild, restore);
	}
	tree.SetItemState (hmeshes, INDEXTOSTATEIMAGEMASK (2), TVIS_STATEIMAGEMASK);

	if (restore)
	{
		m_hSoloItem = nullptr;
		if (view != nullptr)
		{
			view->Reset_Camera_To_Display_Object (*displayed);
		}
		return;
	}

	Set_Mesh_Visibility (hitem, true);
	m_hSoloItem = hitem;

	RenderObjClass *sub_obj = displayed->Get_Sub_Object_By_Name (asset_info->Get_Name (), nullptr);
	if (sub_obj != nullptr)
	{
		SphereClass sphere = sub_obj->Get_Bounding_Sphere ();
		if (view != nullptr && sphere.Radius > 0.0F)
		{
			view->Reset_Camera_To_Display_Sphere (sphere);
		}
		sub_obj->Release_Ref ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnShowOnlyMesh
//
void
CDataTreeView::OnShowOnlyMesh ()
{
	HTREEITEM hitem = m_hRightClickItem;
	m_hRightClickItem = nullptr;
	Show_Only_Mesh (hitem);
}


////////////////////////////////////////////////////////////////////////////
//
//  Display_Asset
//
void
CDataTreeView::Display_Asset (HTREEITEM htree_item)
{
	// Clear multi-select set whenever the user navigates to a new asset.
	m_selectedMeshItems.Delete_All ();
	m_hLastMultiSelectAnchor = nullptr;

	if (htree_item == nullptr) {
		htree_item = GetTreeCtrl ().GetSelectedItem ();
	}

	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	ASSERT (pdoc != nullptr);
	if (pdoc == nullptr) {
		return;
	}

	CMainFrame *main_wnd = (CMainFrame *)::AfxGetMainWnd ();

	// A folder shows its object and drops any part highlight
	if (htree_item != nullptr && Get_Folder_Kind (htree_item) != FOLDER_NONE) {
		Show_Owner_Object (htree_item);
		pdoc->ClearSelectedItem ();
		return;
	}

	//
	// Get the object associated with this entry
	//
	AssetInfoClass *asset_info = nullptr;
	if (htree_item != nullptr) {
		asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
	}

	if (asset_info == nullptr) {

		// A category node, or nothing at all
		pdoc->DisplayObject ((RenderObjClass *)nullptr);
		if (main_wnd != nullptr) {
			main_wnd->OnSelectionChanged (TypeUnknown);
		}
		return;
	}

	// What type of asset is it?
	switch (asset_info->Get_Type ())
	{
		case TypeCompressedAnimation:
		case TypeAnimation:
		{
			// An object keeps its animations in a folder; an aggregate lists them directly
			HTREEITEM hparent = GetTreeCtrl ().GetParentItem (htree_item);
			if (Get_Folder_Kind (hparent) == FOLDER_ANIMATIONS) {
				hparent = GetTreeCtrl ().GetParentItem (hparent);
			}
			if (hparent != nullptr) {

				// Ask the document to start playing the animation for this object
				RenderObjClass *prender_obj = Create_Render_Obj_To_Display (hparent);
				pdoc->PlayAnimation (prender_obj, asset_info->Get_Name ());
				REF_PTR_RELEASE (prender_obj);
				Reset_Mesh_Checks (hparent);
			}
		}
		break;

		case TypeEmitter:
		{
			// Ask the document to display this object
			ParticleEmitterClass *emitter = (ParticleEmitterClass *)Create_Render_Obj_To_Display (htree_item);
			pdoc->Display_Emitter (emitter);
			REF_PTR_RELEASE (emitter);
		}
		break;

		case TypeBone:
		{
			// TheSuperHackers @feature Tria 23/04/2026 Selecting a bone in the tree highlights
			// it in the viewport.
			Show_Owner_Object (htree_item);
			pdoc->SetSelectedItem (asset_info->Get_Name (), TypeBone);
			return;
		}

		case TypeMesh:
		{
			// A part highlights on its object. The row's label is the pivot it hangs on.
			if (Is_Mesh_Sub_Object (htree_item) || Is_Mesh_Reference (htree_item)) {
				Show_Owner_Object (htree_item);
				pdoc->SetSelectedItem (GetTreeCtrl ().GetItemText (htree_item), TypeMesh);
				return;
			}

			// A mesh that is its own object, or one in a collection
			RenderObjClass *prender_obj = Create_Render_Obj_To_Display (htree_item);
			pdoc->DisplayObject (prender_obj);
			REF_PTR_RELEASE (prender_obj);
		}
		break;

		default:
		{
			// Ask the document to display this object
			RenderObjClass *prender_obj = Create_Render_Obj_To_Display (htree_item);
			pdoc->DisplayObject (prender_obj);
			REF_PTR_RELEASE (prender_obj);
			if (Is_Object_Node (htree_item)) {
				Reset_Mesh_Checks (htree_item);
			}
		}
		break;
	}

	if (main_wnd != nullptr) {

		// Let the main window know our selection type has changed
		main_wnd->OnSelectionChanged (asset_info->Get_Type ());

		if (asset_info->Get_Type () == TypeAggregate) {
			main_wnd->Update_Emitters_List ();
		} else {

			::EnableMenuItem (::GetSubMenu (::GetMenu (*main_wnd), 3), 3, MF_BYPOSITION | MF_DISABLED | MF_GRAYED);
			HMENU hsub_menu = main_wnd->Get_Emitters_List_Menu ();
			int index = 0;
			while (::RemoveMenu (hsub_menu, index, MF_BYPOSITION)) {
				//index ++;
			}
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnRClick
//
// TheSuperHackers @feature Tria 18/04/2026 Right-click context menu to toggle mesh visibility
// on the currently displayed model. Allows hiding/showing individual sub-meshes.
//
void
CDataTreeView::OnRClick (NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 1; // Prevent default processing

	// Hit-test to find which item was right-clicked
	CPoint pt;
	::GetCursorPos (&pt);
	CPoint clientPt = pt;
	GetTreeCtrl ().ScreenToClient (&clientPt);

	UINT uFlags = 0;
	HTREEITEM hItem = GetTreeCtrl ().HitTest (clientPt, &uFlags);
	if (hItem == nullptr)
	{
		return;
	}

	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hItem);
	if (asset_info == nullptr)
	{
		return;
	}

	CMenu menu;
	menu.CreatePopupMenu ();

	if (Is_Mesh_Sub_Object (hItem) && asset_info->Get_Type () == TypeMesh)
	{
		// The commands act on the object in the viewport, so selecting the row brings its object up
		GetTreeCtrl ().SelectItem (hItem);

		bool hidden = ((GetTreeCtrl ().GetItemState (hItem, TVIS_STATEIMAGEMASK) >> 12) & 0xF) != 2;
		menu.AppendMenu (MF_STRING, IDM_TOGGLE_MESH_VIS, hidden ? "Show Mesh" : "Hide Mesh");
		menu.AppendMenu (MF_STRING, IDM_SHOW_ONLY_MESH, (m_hSoloItem == hItem) ? "Show All Meshes" : "Show Only This Mesh");
		menu.AppendMenu (MF_SEPARATOR);
		menu.AppendMenu (MF_STRING, IDM_MATVIEWER_FROM_TREE, "Open in Material Viewer");
	}
	else if (asset_info->Get_Type () == TypeHierarchy || asset_info->Get_Type () == TypeLOD)
	{
		// TheSuperHackers @feature Tria 19/04/2026 Export texture and mesh info for the object.
		menu.AppendMenu (MF_STRING, IDM_DUMP_W3D_INFO, "Dump W3D Info");
		menu.AppendMenu (MF_STRING, IDM_EXPORT_INFO,   "Export Info");
		menu.AppendMenu (MF_SEPARATOR);
		menu.AppendMenu (MF_STRING, IDM_MATVIEWER_FROM_TREE, "Open in Material Viewer");
	}
	else if (asset_info->Get_Type () == TypeMesh)
	{
		menu.AppendMenu (MF_STRING, IDM_MATVIEWER_FROM_TREE, "Open in Material Viewer");
	}
	else
	{
		return;
	}

	// Store the right-clicked item for the command handlers
	m_hRightClickItem = hItem;
	menu.TrackPopupMenu (TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, this);
}


////////////////////////////////////////////////////////////////////////////
//
//  Is_Mesh_Sub_Object
//
// TheSuperHackers @feature Tria 22/04/2026 Returns true if the given tree item is a part
// of an object, which supports visibility toggling.
//
bool
CDataTreeView::Is_Mesh_Sub_Object (HTREEITEM hItem) const
{
	if (hItem == nullptr)
	{
		return false;
	}

	// Parts nest, so the folder can be several levels up
	for (HTREEITEM hancestor = GetTreeCtrl ().GetParentItem (hItem); hancestor != nullptr; hancestor = GetTreeCtrl ().GetParentItem (hancestor))
	{
		if (Get_Folder_Kind (hancestor) == FOLDER_MESHES)
		{
			return true;
		}
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////
//
//  Is_Mesh_Reference
//
//  True for a row under a texture, which names a part that uses the texture.
//
bool
CDataTreeView::Is_Mesh_Reference (HTREEITEM hItem) const
{
	HTREEITEM hparent = (hItem != nullptr) ? GetTreeCtrl ().GetParentItem (hItem) : nullptr;
	if (hparent == nullptr)
	{
		return false;
	}

	return Get_Folder_Kind (GetTreeCtrl ().GetParentItem (hparent)) == FOLDER_TEXTURES;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonDown
//
// TheSuperHackers @feature Tria 18/04/2026 Intercept left-click on eye state icon to toggle
// mesh visibility before the tree control changes the selection.
// TheSuperHackers @feature Tria 23/04/2026 Ctrl+click on item label adds/removes it from the
// multi-select set. Clicking the state icon toggles all selected items at once.
// TheSuperHackers @feature Tria 23/04/2026 Shift+click range-selects siblings between the last
// anchor and the clicked item and toggles them with their sub-objects.
//
void
CDataTreeView::OnLButtonDown (UINT nFlags, CPoint point)
{
	UINT uFlags = 0;
	HTREEITEM hItem = GetTreeCtrl ().HitTest (point, &uFlags);

	bool on_state_icon = (hItem != nullptr) && ((uFlags & TVHT_ONITEMSTATEICON) != 0);
	bool is_meshes_folder = (hItem != nullptr) && (Get_Folder_Kind (hItem) == FOLDER_MESHES);

	// The eye acts on the object in the viewport, so selecting the row brings its object up
	if (on_state_icon && (is_meshes_folder || Is_Mesh_Sub_Object (hItem)) && !Is_Owner_Shown (hItem))
	{
		DynamicVectorClass<HTREEITEM> selected = m_selectedMeshItems;
		GetTreeCtrl ().SelectItem (hItem);
		m_selectedMeshItems = selected;
	}

	// Allow clicking the state icon on the Meshes folder itself to cascade to all children.
	if (on_state_icon && is_meshes_folder)
	{
		// Determine target visibility from first child's state.
		HTREEITEM hFirst = GetTreeCtrl ().GetChildItem (hItem);
		bool newVisible = true;
		if (hFirst != nullptr)
		{
			UINT st = GetTreeCtrl ().GetItemState (hFirst, TVIS_STATEIMAGEMASK);
			newVisible = ((st >> 12) & 0xF) != 2; // invert current first child state
		}
		for (HTREEITEM hChild = GetTreeCtrl ().GetChildItem (hItem); hChild; hChild = GetTreeCtrl ().GetNextSiblingItem (hChild))
		{
			Set_Mesh_Visibility_Recursive (hChild, newVisible);
		}
		GetTreeCtrl ().SetItemState (hItem, INDEXTOSTATEIMAGEMASK (newVisible ? 2 : 1), TVIS_STATEIMAGEMASK);
		m_hSoloItem = nullptr;
		return;
	}

	if (hItem != nullptr && Is_Mesh_Sub_Object (hItem)) {
		if (uFlags & TVHT_ONITEMSTATEICON) {
			// Toggling the checkbox — apply to multi-select set if populated, else single/group toggle.
			Toggle_Mesh_Visibility (hItem);
			m_hSoloItem = nullptr;
			return; // eat the click
		}

		if ((uFlags & TVHT_ONITEM) && (nFlags & MK_SHIFT) && m_hLastMultiSelectAnchor != nullptr) {
			// Shift+click: replace the multi-select set with the range [anchor, hItem] among siblings.
			// The anchor does NOT move (matches Windows Explorer behavior).
			HTREEITEM hParent = GetTreeCtrl ().GetParentItem (hItem);
			HTREEITEM hAnchorParent = GetTreeCtrl ().GetParentItem (m_hLastMultiSelectAnchor);
			if (hParent != nullptr && hParent == hAnchorParent) {
				DynamicVectorClass<HTREEITEM> range;
				bool inRange = false;
				for (HTREEITEM hSib = GetTreeCtrl ().GetChildItem (hParent); hSib; hSib = GetTreeCtrl ().GetNextSiblingItem (hSib)) {
					bool isEndpoint = (hSib == hItem || hSib == m_hLastMultiSelectAnchor);
					if (isEndpoint)
						inRange = !inRange;
					if (inRange || isEndpoint)
						range.Add (hSib);
					if (isEndpoint && !inRange)
						break;
				}

				// Selecting the row clears the set and the anchor, so both go back afterwards
				HTREEITEM hAnchor = m_hLastMultiSelectAnchor;
				GetTreeCtrl ().SelectItem (hItem);
				m_selectedMeshItems = range;
				m_hLastMultiSelectAnchor = hAnchor;
				return;
			}
		}

		if ((uFlags & TVHT_ONITEM) && (nFlags & MK_CONTROL)) {
			// Ctrl+click on the item label: add/remove from multi-select set.
			DynamicVectorClass<HTREEITEM> selected = m_selectedMeshItems;
			bool found = false;
			for (int i = 0; i < selected.Count (); ++i) {
				if (selected[i] == hItem) {
					selected.Delete (i);
					found = true;
					break;
				}
			}
			if (!found)
				selected.Add (hItem);

			// Selecting the row clears the set and the anchor, so both go back afterwards
			GetTreeCtrl ().SelectItem (hItem);
			m_selectedMeshItems = selected;
			m_hLastMultiSelectAnchor = hItem;
			return;
		}
	}

	// Any non-Ctrl/Shift click clears the multi-select set and updates the anchor.
	if (!(nFlags & MK_CONTROL) && !(nFlags & MK_SHIFT)) {
		m_selectedMeshItems.Delete_All ();
	}

	CTreeView::OnLButtonDown (nFlags, point);

	// Selecting the row clears the anchor, so it is set after the click went through
	if (!(nFlags & MK_CONTROL) && !(nFlags & MK_SHIFT)) {
		m_hLastMultiSelectAnchor = (hItem != nullptr && Is_Mesh_Sub_Object (hItem) && (uFlags & TVHT_ONITEM)) ? hItem : nullptr;
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonDblClk
//
void
CDataTreeView::OnLButtonDblClk (UINT nFlags, CPoint point)
{
	UINT uFlags = 0;
	HTREEITEM hItem = GetTreeCtrl ().HitTest (point, &uFlags);
	bool is_part = (hItem != nullptr) && Is_Mesh_Sub_Object (hItem);

	// The second click of a fast pair on an eye is one more toggle
	if (hItem != nullptr && (uFlags & TVHT_ONITEMSTATEICON) && (is_part || Get_Folder_Kind (hItem) == FOLDER_MESHES))
	{
		OnLButtonDown (nFlags, point);
		return;
	}

	// A double click on a part shows it alone. Passing it on would also expand the row.
	AssetInfoClass *asset_info = (hItem != nullptr) ? (AssetInfoClass *)GetTreeCtrl ().GetItemData (hItem) : nullptr;
	if (is_part && (uFlags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)) && asset_info != nullptr && asset_info->Get_Type () == TypeMesh)
	{
		Show_Only_Mesh (hItem);
		return;
	}

	CTreeView::OnLButtonDblClk (nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnToggleMeshVis
//
void
CDataTreeView::OnToggleMeshVis (void)
{
	HTREEITEM hItem = m_hRightClickItem;
	m_hRightClickItem = nullptr;
	if (hItem != nullptr)
		Toggle_Mesh_Visibility (hItem);
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Mesh_Visibility
//
// TheSuperHackers @feature Tria 23/04/2026 Set the hidden state of a single sub-mesh on the
// currently displayed model and update its tree checkbox. Does not recurse into children.
//
void
CDataTreeView::Set_Mesh_Visibility (HTREEITEM hItem, bool visible)
{
	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (hItem);
	if (asset_info == nullptr || asset_info->Get_Type () != TypeMesh)
		return;

	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (pdoc == nullptr)
		return;

	RenderObjClass *pDisplayed = pdoc->GetDisplayedObject ();
	if (pDisplayed == nullptr)
		return;

	RenderObjClass *pSubObj = pDisplayed->Get_Sub_Object_By_Name (asset_info->Get_Name (), nullptr);
	if (pSubObj == nullptr)
		return;

	pSubObj->Set_Hidden (!visible);
	pSubObj->Release_Ref ();

	GetTreeCtrl ().SetItemState (hItem, INDEXTOSTATEIMAGEMASK (visible ? 2 : 1), TVIS_STATEIMAGEMASK);
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Displayed_Part_Hidden
//
bool
CDataTreeView::Set_Displayed_Part_Hidden (LPCTSTR name, bool hidden)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	RenderObjClass *displayed = (pdoc != nullptr) ? pdoc->GetDisplayedObject () : nullptr;
	RenderObjClass *part = (displayed != nullptr) ? displayed->Get_Sub_Object_By_Name (name, nullptr) : nullptr;
	if (part == nullptr)
	{
		return false;
	}

	part->Set_Hidden (hidden);
	part->Release_Ref ();
	Sync_Mesh_Checks ();
	return true;
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Mesh_Visibility_Recursive
//
// TheSuperHackers @feature Tria 23/04/2026 Set visibility on hItem and all its tree descendants.
//
void
CDataTreeView::Set_Mesh_Visibility_Recursive (HTREEITEM hItem, bool visible)
{
	Set_Mesh_Visibility (hItem, visible);
	for (HTREEITEM hChild = GetTreeCtrl ().GetChildItem (hItem); hChild; hChild = GetTreeCtrl ().GetNextSiblingItem (hChild))
		Set_Mesh_Visibility_Recursive (hChild, visible);
}


////////////////////////////////////////////////////////////////////////////
//
//  Toggle_Mesh_Visibility
//
// TheSuperHackers @feature Tria 18/04/2026 Toggle hidden state of a sub-mesh on the currently
// displayed model. If Ctrl is held, adds hItem to the multi-select set. Clicking on a parent
// item cascades the toggle to all children.
//
void
CDataTreeView::Toggle_Mesh_Visibility (HTREEITEM hItem)
{
	// Determine target visibility from the current checkbox state of this item.
	UINT state = GetTreeCtrl ().GetItemState (hItem, TVIS_STATEIMAGEMASK);
	int stateIdx = (state >> 12) & 0xF;
	bool currentlyVisible = (stateIdx == 2);
	bool newVisible = !currentlyVisible;

	// If this item has children (group node), cascade to all descendants.
	if (GetTreeCtrl ().GetChildItem (hItem) != nullptr) {
		Set_Mesh_Visibility_Recursive (hItem, newVisible);
		return;
	}

	// Apply to all items in the multi-select set (if any), plus this item.
	bool appliedToSet = false;
	if (m_selectedMeshItems.Count () > 0) {
		for (int i = 0; i < m_selectedMeshItems.Count (); ++i) {
			HTREEITEM hSel = m_selectedMeshItems[i];
			if (hSel != nullptr)
				Set_Mesh_Visibility_Recursive (hSel, newVisible);
		}
		appliedToSet = true;
	}

	if (!appliedToSet)
		Set_Mesh_Visibility (hItem, newVisible);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnShowBones
//
// TheSuperHackers @feature Tria 18/04/2026 Toggle bone overlay rendering in the viewport.
//
void
CDataTreeView::OnShowBones (void)
{
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (pdoc != nullptr) {
		pdoc->SetShowBones (!pdoc->GetShowBones ());
		::AfxGetApp ()->WriteProfileInt ("Config", "ShowBones", pdoc->GetShowBones () ? 1 : 0);
	}
}



////////////////////////////////////////////////////////////////////////////
//
//  OnExportInfo
//
// TheSuperHackers @feature Tria 19/04/2026 Export texture and mesh info for a hierarchy object
// to a text file. Lists all sub-meshes and textures used by each mesh.
//
void
CDataTreeView::OnExportInfo (void)
{
	if (m_hRightClickItem == nullptr)
		return;

	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (m_hRightClickItem);
	if (asset_info == nullptr)
		return;

	const char *objectName = asset_info->Get_Name ();

	// Create the render object to inspect
	RenderObjClass *robj = WW3DAssetManager::Get_Instance ()->Create_Render_Obj (objectName);
	if (robj == nullptr) {
		::AfxMessageBox ("Could not create render object for inspection.");
		return;
	}

	// Ask the user where to save
	CString defaultName (objectName);
	defaultName += "_info.txt";
	CFileDialog dlg (FALSE, "txt", defaultName,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER,
		"Text Files (*.txt)|*.txt|All Files (*.*)|*.*||", this);
	dlg.m_ofn.lpstrTitle = "Export Info";

	if (dlg.DoModal () != IDOK) {
		robj->Release_Ref ();
		return;
	}

	CString filePath = dlg.GetPathName ();
	FILE *fp = fopen ((LPCTSTR)filePath, "w");
	if (fp == nullptr) {
		::AfxMessageBox ("Could not open file for writing.");
		robj->Release_Ref ();
		return;
	}

	fprintf (fp, "Object: %s\n", objectName);
	fprintf (fp, "========================================\n\n");

	// Collect all meshes and their textures
	int numSub = robj->Get_Num_Sub_Objects ();
	fprintf (fp, "Meshes (%d):\n", numSub);
	fprintf (fp, "----------------------------------------\n");

	HashTemplateClass<StringClass, int> allTextures;

	for (int s = 0; s < numSub; ++s) {
		RenderObjClass *sub = robj->Get_Sub_Object (s);
		if (sub == nullptr)
			continue;

		fprintf (fp, "  [%d] %s\n", s, sub->Get_Name ());

		// Get textures for this sub-object
		MaterialInfoClass *matinfo = sub->Get_Material_Info ();
		if (matinfo != nullptr) {
			for (int t = 0; t < matinfo->Texture_Count (); ++t) {
				TextureClass *tex = matinfo->Peek_Texture (t);
				if (tex != nullptr) {
					const char *texName = tex->Get_Texture_Name ();
					fprintf (fp, "        Texture: %s\n", texName);
					StringClass key (texName);
					if (!allTextures.Exists (key))
						allTextures.Insert (key, 1);
				}
			}
			REF_PTR_RELEASE (matinfo);
		}

		sub->Release_Ref ();
	}

	// If the object itself is a mesh (no sub-objects), check it directly
	if (numSub == 0) {
		fprintf (fp, "  (single mesh) %s\n", objectName);
		MaterialInfoClass *matinfo = robj->Get_Material_Info ();
		if (matinfo != nullptr) {
			for (int t = 0; t < matinfo->Texture_Count (); ++t) {
				TextureClass *tex = matinfo->Peek_Texture (t);
				if (tex != nullptr) {
					const char *texName = tex->Get_Texture_Name ();
					fprintf (fp, "        Texture: %s\n", texName);
					StringClass key (texName);
					if (!allTextures.Exists (key))
						allTextures.Insert (key, 1);
				}
			}
			REF_PTR_RELEASE (matinfo);
		}
	}

	// Summary of all unique textures
	fprintf (fp, "\n========================================\n");
	fprintf (fp, "All Textures:\n");
	fprintf (fp, "----------------------------------------\n");

	HashTemplateIterator<StringClass, int> texIter (allTextures);
	for (texIter.First (); !texIter.Is_Done (); texIter.Next ()) {
		fprintf (fp, "  %s\n", (const char *)texIter.Peek_Key ());
	}

	fprintf (fp, "\n");
	fclose (fp);

	robj->Release_Ref ();

	CString msg;
	msg.Format ("Info exported to:\n%s", (LPCTSTR)filePath);
	::AfxMessageBox (msg, MB_ICONINFORMATION);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDumpW3DInfo
//
// TheSuperHackers @feature Tria 23/04/2026 Show a popup with the hierarchy object's parent name,
// sub-object names, and bone names.
//
void
CDataTreeView::OnDumpW3DInfo (void)
{
	if (m_hRightClickItem == nullptr)
		return;

	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (m_hRightClickItem);
	if (asset_info == nullptr)
		return;

	const char *objectName = asset_info->Get_Name ();

	RenderObjClass *robj = WW3DAssetManager::Get_Instance ()->Create_Render_Obj (objectName);
	if (robj == nullptr) {
		::AfxMessageBox ("Could not create render object for inspection.");
		return;
	}

	// Mirror the tree view hierarchy logic:
	// Iterate HTree pivots, mix sub-objects and bones nested by parent-child relationship,
	// sort children alphabetically at each level, same as TVI_SORT in the tree.
	const HTreeClass *htree = robj->Get_HTree ();
	int numPivots = (htree != nullptr) ? htree->Num_Pivots () : 0;
	int numSubs = robj->Get_Num_Sub_Objects ();

	// Build short-name -> bool mesh presence map (strip "HIERARCHY." prefix)
	HashTemplateClass<StringClass, int> subObjNames;
	for (int s = 0; s < numSubs; ++s) {
		RenderObjClass *sub = robj->Get_Sub_Object (s);
		if (sub == nullptr)
			continue;
		const char *fullName = sub->Get_Name ();
		const char *dot = ::strchr (fullName, '.');
		StringClass shortName (dot != nullptr ? dot + 1 : fullName);
		subObjNames.Insert (shortName, 1);
		sub->Release_Ref ();
	}

	// Build per-pivot child lists (pivot index -> sorted list of child pivot indices)
	// pivot 0 is the root; children of 0 are top-level items
	DynamicVectorClass<DynamicVectorClass<int>> children;
	children.Resize (numPivots);
	for (int p = 1; p < numPivots; ++p) {
		const char *name = htree->Get_Bone_Name (p);
		if (name == nullptr || name[0] == '\0')
			continue;
		int parent = htree->Get_Parent_Index (p);
		if (parent < 0 || parent >= numPivots)
			parent = 0;
		children[parent].Add (p);
	}

	// Sort each child list alphabetically by pivot name
	for (int p = 0; p < numPivots; ++p) {
		DynamicVectorClass<int> &list = children[p];
		// Insertion sort (lists are small)
		for (int i = 1; i < list.Count (); ++i) {
			int key = list[i];
			const char *keyName = htree->Get_Bone_Name (key);
			int j = i - 1;
			while (j >= 0 && ::_stricmp (htree->Get_Bone_Name (list[j]), keyName) > 0) {
				list[j + 1] = list[j];
				--j;
			}
			list[j + 1] = key;
		}
	}

	// Recursively append a sub-object and any children (bones or nested meshes) under it.
	// Bones that are direct children of pivot 0 are excluded here; they go to the Bones section.
	struct PivotPrinter {
		static void Print (CString &out, const HTreeClass *tree,
		                   const DynamicVectorClass<DynamicVectorClass<int>> &ch,
		                   const HashTemplateClass<StringClass, int> &meshNames,
		                   int pivotIdx, int depth)
		{
			const char *name = tree->Get_Bone_Name (pivotIdx);
			if (name == nullptr || name[0] == '\0')
				return;
			CString indent;
			for (int i = 0; i < depth; ++i)
				indent += "  ";
			bool isMesh = meshNames.Exists (StringClass (name));
			out.AppendFormat ("%s%s [%s]\n", (LPCSTR)indent, name, isMesh ? "object" : "bone");
			const DynamicVectorClass<int> &list = ch[pivotIdx];
			for (int i = 0; i < list.Count (); ++i)
				Print (out, tree, ch, meshNames, list[i], depth + 1);
		}
	};

	CString text;
	text.Format ("%s\n", objectName);

	if (numPivots > 1) {
		// Sub-Objects section: root-level pivots that have mesh geometry, plus their children
		CString subObjLines;
		int subObjCount = 0;
		CString rootBoneLines;
		int rootBoneCount = 0;
		const DynamicVectorClass<int> &roots = children[0];
		for (int i = 0; i < roots.Count (); ++i) {
			int p = roots[i];
			const char *name = htree->Get_Bone_Name (p);
			if (name == nullptr || name[0] == '\0')
				continue;
			if (subObjNames.Exists (StringClass (name))) {
				subObjLines.AppendFormat ("  %s [object]\n", name);
				// Print children indented under this sub-object
				const DynamicVectorClass<int> &ch = children[p];
				for (int c = 0; c < ch.Count (); ++c)
					PivotPrinter::Print (subObjLines, htree, children, subObjNames, ch[c], 2);
				++subObjCount;
			} else {
				rootBoneLines.AppendFormat ("  %s\n", name);
				++rootBoneCount;
			}
		}
		text.AppendFormat ("\nSub-Objects (%d):\n", subObjCount);
		text += subObjLines;
		text.AppendFormat ("\nBones (%d):\n", rootBoneCount);
		text += rootBoneLines;
	} else {
		// No HTree — list sub-objects alphabetically, no bones section
		DynamicVectorClass<CString> names;
		for (int s = 0; s < numSubs; ++s) {
			RenderObjClass *sub = robj->Get_Sub_Object (s);
			if (sub == nullptr)
				continue;
			const char *fullName = sub->Get_Name ();
			const char *dot = ::strchr (fullName, '.');
			names.Add (CString (dot != nullptr ? dot + 1 : fullName));
			sub->Release_Ref ();
		}
		for (int i = 1; i < names.Count (); ++i) {
			CString key = names[i];
			int j = i - 1;
			while (j >= 0 && names[j].CompareNoCase (key) > 0) {
				names[j + 1] = names[j];
				--j;
			}
			names[j + 1] = key;
		}
		text.AppendFormat ("\nSub-Objects (%d):\n", names.Count ());
		for (int i = 0; i < names.Count (); ++i)
			text.AppendFormat ("  %s [object]\n", (LPCSTR)names[i]);
	}

	robj->Release_Ref ();

	// Show info with a Copy button using TaskDialog
	TASKDIALOG_BUTTON buttons[] = { { 100, L"Copy to Clipboard" }, { IDOK, L"OK" } };
	CStringW textW (text);
	TASKDIALOGCONFIG cfg = {};
	cfg.cbSize = sizeof (cfg);
	cfg.hwndParent = GetSafeHwnd ();
	cfg.dwFlags = 0;
	cfg.pszWindowTitle = L"W3D Info";
	cfg.pszMainIcon = TD_INFORMATION_ICON;
	cfg.pszContent = textW;
	cfg.pButtons = buttons;
	cfg.cButtons = 2;
	cfg.nDefaultButton = IDOK;
	int pressed = IDOK;
	::TaskDialogIndirect (&cfg, &pressed, nullptr, nullptr);
	if (pressed == 100) {
		if (::OpenClipboard (nullptr)) {
			::EmptyClipboard ();
			HGLOBAL hMem = ::GlobalAlloc (GMEM_MOVEABLE, (text.GetLength () + 1) * sizeof (char));
			if (hMem != nullptr) {
				char *pMem = (char *)::GlobalLock (hMem);
				::memcpy (pMem, (LPCSTR)text, text.GetLength () + 1);
				::GlobalUnlock (hMem);
				::SetClipboardData (CF_TEXT, hMem);
			}
			::CloseClipboard ();
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDeleteItem
//
void
CDataTreeView::OnDeleteItem
(
	NMHDR *pNMHDR,
	LRESULT *pResult
)
{
	// Get the information object for this asset
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
	AssetInfoClass *asset_info = (AssetInfoClass *)pNMTreeView->itemOld.lParam;

	// If this is a texture, then free our hold on its interface
	if (asset_info && (asset_info->Get_Type () == TypeMaterial)) {
		TextureClass *ptexture = (TextureClass *)asset_info->Get_User_Number ();
		REF_PTR_RELEASE (ptexture);
	}

	// Free the asset information object
	SAFE_DELETE (asset_info);

	// Forget what the tree no longer has
	m_FolderKinds.erase (pNMTreeView->itemOld.hItem);
	if (m_hSoloItem == pNMTreeView->itemOld.hItem) {
		m_hSoloItem = nullptr;
	}

	// TheSuperHackers @performance Tria 25/04/2026 Removed redundant SetItemData(... 0).
	// The tree item is being destroyed by the control - setting its data sends a
	// TVM_SETITEM back into the control mid-teardown, which is pointless and adds
	// a SendMessage round-trip per item (very expensive when clearing thousands of items).
	(*pResult) = 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  Free_All_Asset_Data
//
// TheSuperHackers @performance Tria 25/04/2026 Walk the tree once, release
// each AssetInfo's texture ref and free the AssetInfo, then zero the lParam.
// After this returns, OnDeleteItem becomes a no-op for every item, so the
// subsequent DeleteAllItems() call does not have to release thousands of D3D
// textures one-by-one in the middle of tree teardown - the worst part of the
// 2000-asset "New" hang.
//
////////////////////////////////////////////////////////////////////////////
void
CDataTreeView::Free_All_Asset_Data (void)
{
	CTreeCtrl &tree = GetTreeCtrl ();

	// Iterative depth-first walk over every item.
	HTREEITEM hitem = tree.GetRootItem ();
	while (hitem != nullptr) {

		AssetInfoClass *asset_info = (AssetInfoClass *)tree.GetItemData (hitem);
		if (asset_info != nullptr) {
			if (asset_info->Get_Type () == TypeMaterial) {
				TextureClass *ptexture = (TextureClass *)asset_info->Get_User_Number ();
				REF_PTR_RELEASE (ptexture);
			}
			delete asset_info;
			tree.SetItemData (hitem, 0);
		}

		// Descend, then advance to next sibling, climbing back up as needed.
		HTREEITEM hnext = tree.GetChildItem (hitem);
		if (hnext == nullptr) {
			hnext = tree.GetNextSiblingItem (hitem);
			while (hnext == nullptr) {
				hitem = tree.GetParentItem (hitem);
				if (hitem == nullptr) {
					break;
				}
				hnext = tree.GetNextSiblingItem (hitem);
			}
		}
		hitem = hnext;
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Current_Asset_Info
//
AssetInfoClass *
CDataTreeView::Get_Current_Asset_Info () const
{
	AssetInfoClass *asset_info = nullptr;

	// Get the currently selected node from the tree control
	HTREEITEM htree_item = GetTreeCtrl ().GetSelectedItem ();
	if (htree_item != nullptr) {

		// Get the data associated with this item
		asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
	}

	// Return the asset information associated with the current item
	return asset_info;
}


////////////////////////////////////////////////////////////////////////////
//
//  Get_Current_Render_Obj
//
RenderObjClass *
CDataTreeView::Get_Current_Render_Obj () const
{
	RenderObjClass *prender_obj = nullptr;

	// Get the currently selected node from the tree control
	HTREEITEM htree_item = GetTreeCtrl ().GetSelectedItem ();
	if (htree_item != nullptr) {

		// Get the data associated with this item
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
		if (asset_info != nullptr) {

			// Return the render object pointer
			prender_obj = asset_info->Peek_Render_Obj ();
		}
	}

	// Return the render object pointer
	return prender_obj;
}


////////////////////////////////////////////////////////////////////////////
//
//  GetCurrentSelectionName
//
LPCTSTR
CDataTreeView::GetCurrentSelectionName ()
{
	LPCTSTR pname = nullptr;

	// Get the currently selected node from the tree control
	HTREEITEM htree_item = GetTreeCtrl ().GetSelectedItem ();
	if (htree_item != nullptr) {

		// Get the data associated with this item
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
		if (asset_info != nullptr) {

			// Return the name of the asset to the caller
			pname = asset_info->Get_Name ();
		}
	}

	// Return the name of the selected node
	return pname;
}

////////////////////////////////////////////////////////////////////////////
//
//  GetCurrentSelectionType
//
ASSET_TYPE
CDataTreeView::GetCurrentSelectionType ()
{
	ASSET_TYPE type = TypeUnknown;

	// Get the currently selected node from the tree control
	HTREEITEM htree_item = GetTreeCtrl ().GetSelectedItem ();
	if (htree_item != nullptr) {

		// Get the associated asset information for this node.
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
		if (asset_info != nullptr) {
			type = asset_info->Get_Type ();
		}
	}

	// Return the type of the selected node
	return type;
}

////////////////////////////////////////////////////////////////////////////
//
//  OnDblclk
//
void
CDataTreeView::OnDblclk
(
    NMHDR* pNMHDR,
    LRESULT* pResult
)
{
    // Get the main window of our app
    CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
    if (pCMainWnd != nullptr)
    {
        // Display the properties for the currently selected object
        //pCMainWnd->ShowObjectProperties ();
    }

	(*pResult) = 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnOpenInMaterialViewer
//
// TheSuperHackers @feature Opens the right-clicked asset in the W3D Material
// Viewer: the preview shows the render object, and the material panel is fed
// from the asset's source .w3d file (resolved via the source-file map).
//
void
CDataTreeView::OnOpenInMaterialViewer (void)
{
	if (m_hRightClickItem == nullptr)
		return;

	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (m_hRightClickItem);
	if (asset_info == nullptr)
		return;

	const char *name = asset_info->Get_Name ();

	// Look up which .w3d file this asset came from
	StringClass source_file;
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	if (pdoc != nullptr) {
		const HashTemplateClass<StringClass, StringClass> &sourceMap = pdoc->GetAssetSourceFileMap ();
		StringClass key (name);
		if (sourceMap.Exists (key)) {
			source_file = sourceMap.Get (key);
		}
	}

	// Mesh items pre-select themselves in the material panel
	bool is_mesh = (asset_info->Get_Type () == TypeMesh);
	CMaterialViewerFrame::ShowViewerForAsset (name, source_file, is_mesh ? name : nullptr);
}


////////////////////////////////////////////////////////////////////////////
//
//  OpenSelectionInMaterialViewer
//
// TheSuperHackers @feature Ctrl+M entry point. A mesh selected in the tree
// opens the Material Viewer pre-selected to that mesh; any other selection (or
// none) opens the whole currently displayed object instead.
//
void
CDataTreeView::OpenSelectionInMaterialViewer (void)
{
	AssetInfoClass *asset_info = Get_Current_Asset_Info ();

	// A mesh is selected: open that mesh (pre-selected in the panel), mirroring
	// the right-click "Open in Material Viewer" path.
	if (asset_info != nullptr && asset_info->Get_Type () == TypeMesh) {
		const char *name = asset_info->Get_Name ();

		StringClass source_file;
		CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
		if (pdoc != nullptr) {
			const HashTemplateClass<StringClass, StringClass> &sourceMap = pdoc->GetAssetSourceFileMap ();
			StringClass key (name);
			if (sourceMap.Exists (key)) {
				source_file = sourceMap.Get (key);
			}
		}

		CMaterialViewerFrame::ShowViewerForAsset (name, source_file, name);
		return;
	}

	// Otherwise open the whole currently displayed object. Prefer a selected
	// non-mesh asset's name; fall back to whatever the viewport is showing.
	CW3DViewDoc *pdoc = (CW3DViewDoc *)GetDocument ();
	const char *obj_name = nullptr;
	if (asset_info != nullptr) {
		obj_name = asset_info->Get_Name ();
	} else if (pdoc != nullptr && pdoc->GetDisplayedObject () != nullptr) {
		obj_name = pdoc->GetDisplayedObject ()->Get_Name ();
	}

	if (obj_name == nullptr || obj_name[0] == '\0') {
		// Nothing to show a model for: just open the viewer empty.
		CMaterialViewerFrame::ShowViewer ();
		return;
	}

	StringClass source_file;
	if (pdoc != nullptr) {
		const HashTemplateClass<StringClass, StringClass> &sourceMap = pdoc->GetAssetSourceFileMap ();
		StringClass key (obj_name);
		if (sourceMap.Exists (key)) {
			source_file = sourceMap.Get (key);
		}
	}

	// No mesh name -> the panel shows the whole object.
	CMaterialViewerFrame::ShowViewerForAsset (obj_name, source_file, nullptr);
}


////////////////////////////////////////////////////////////////////////////
//
//  Build_Render_Object_List
//
void
CDataTreeView::Build_Render_Object_List
(
	DynamicVectorClass <CString> &asset_list,
	HTREEITEM hparent
)
{
	// Loop through all the children of this node
	for (HTREEITEM htree_item = GetTreeCtrl ().GetChildItem (hparent);
		  (htree_item != nullptr);
		  htree_item = GetTreeCtrl ().GetNextSiblingItem (htree_item)) {

		// The textures, and the parts repeated under them, are not objects to list
		if (Get_Folder_Kind (htree_item) == FOLDER_TEXTURES) {
			continue;
		}

		// A part's label is only its pivot, so the name comes from the asset
		AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
		if ((asset_info != nullptr) &&
			 (asset_info->Get_Type () != TypeAnimation) &&
			 (asset_info->Get_Type () != TypeCompressedAnimation) &&
			 (asset_info->Get_Type () != TypeMaterial) &&
			 (asset_info->Get_Type () != TypeBone))
		{
			asset_list.Add (asset_info->Get_Name ());
		}

		// If this item has children, then add the children recursively
		if (GetTreeCtrl ().ItemHasChildren (htree_item)) {
			Build_Render_Object_List (asset_list, htree_item);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Create_Render_Obj_To_Display
//
////////////////////////////////////////////////////////////////////////////
RenderObjClass *
CDataTreeView::Create_Render_Obj_To_Display (HTREEITEM htree_item)
{
	// Lookup the information object associated with this asset
	RenderObjClass *render_obj = nullptr;
	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
	if (asset_info != nullptr) {

		// Use the asset's instance if there is one, otherwise attempt to create one
		render_obj = asset_info->Get_Render_Obj ();
		if (render_obj == nullptr) {

			// If this is a texture, then create a special BMP obj from it
			if (asset_info->Get_Type () == TypeMaterial) {
				TextureClass *ptexture = (TextureClass *)asset_info->Get_User_Number ();
				if (ptexture != nullptr) {
					render_obj = new Bitmap2DObjClass (ptexture, 0.5F, 0.5F, true, false, false, true);
				}
			}

			//
			// Finally, if we aren't successful, create a new instance based on its name
			//
			if (render_obj == nullptr) {
				render_obj = WW3DAssetManager::Get_Instance()->Create_Render_Obj (asset_info->Get_Name ());
			}
		}
	}

	//
	//	Force the highest level LOD
	//
	if (	render_obj != nullptr &&
			::GetCurrentDocument ()->GetScene ()->Are_LODs_Switching () == false)
	{
		Set_Highest_LOD (render_obj);
	}

	return render_obj;
}


////////////////////////////////////////////////////////////////////////////
//
//  Refresh_Asset
//
void
CDataTreeView::Refresh_Asset
(
	LPCTSTR new_name,
	LPCTSTR old_name,
	ASSET_TYPE type
)
{
	// Params OK?
	if ((new_name != nullptr) && (old_name != nullptr)) {

		// Turn off repainting
		GetTreeCtrl ().SetRedraw (FALSE);

		// Determime where this asset should go
		HTREEITEM hparent = nullptr;
		int icon_index = 0;
		Determine_Tree_Location (type, hparent, icon_index);

		// Can we find the item we are supposed to refresh?
		HTREEITEM htree_item = FindChildItem (hparent, old_name);
		if (htree_item != nullptr) {

			// Refresh the item's text in the tree control
			GetTreeCtrl ().SetItemText (htree_item, new_name);

			// Refresh the associated asset info structure
			AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (htree_item);
			if (asset_info != nullptr) {
				asset_info->Set_Name (new_name);
			}
		} else {

			// This asset wasn't already in the tree, so add it...
			Add_Asset_To_Tree (new_name, type, true);
		}

		// Turn painting back on
		GetTreeCtrl ().SetRedraw (TRUE);

		// Force the window to be repainted
		Invalidate (FALSE);
		UpdateWindow ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Select_Next
//
void
CDataTreeView::Select_Next ()
{
	//
	//	Get the selected entry in the tree control
	//
	HTREEITEM hselected = GetTreeCtrl ().GetSelectedItem ();
	if (hselected != nullptr) {

		//
		//	Select the item that follows the currently selected item
		//
		HTREEITEM hitem = GetTreeCtrl ().GetNextItem (hselected, TVGN_NEXT);
		if (hitem != nullptr) {
			GetTreeCtrl ().SelectItem (hitem);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Select_Prev
//
void
CDataTreeView::Select_Prev ()
{
	//
	//	Get the selected entry in the tree control
	//
	HTREEITEM hselected = GetTreeCtrl ().GetSelectedItem ();
	if (hselected != nullptr) {

		//
		//	Select the item that follows the currently selected item
		//
		HTREEITEM hitem = GetTreeCtrl ().GetNextItem (hselected, TVGN_PREVIOUS);
		if (hitem != nullptr) {
			GetTreeCtrl ().SelectItem (hitem);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Reload_Lightmap_Models
//
////////////////////////////////////////////////////////////////////////////
void
CDataTreeView::Reload_Lightmap_Models ()
{
	// Every object node, and the mesh collections
	CTreeCtrl &tree = GetTreeCtrl ();
	for (HTREEITEM hitem = tree.GetRootItem (); hitem != nullptr; hitem = tree.GetNextSiblingItem (hitem))
	{
		if (tree.GetItemData (hitem) != 0 || hitem == m_hCategoryRoots[CATEGORY_COLLECTION])
		{
			Free_Child_Models (hitem);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Free_Child_Models
//
////////////////////////////////////////////////////////////////////////////
void
CDataTreeView::Free_Child_Models (HTREEITEM parent_item)
{
	//
	// The node itself, when it is a model
	//
	AssetInfoClass *asset_info = (AssetInfoClass *)GetTreeCtrl ().GetItemData (parent_item);
	if (asset_info != nullptr &&
		 (asset_info->Get_Type () == TypeMesh || asset_info->Get_Type () == TypeHierarchy))
	{
		WW3DAssetManager::Get_Instance ()->Remove_Prototype (asset_info->Get_Name ());
	}

	//
	// Loop through all the children of this node
	//
	for (	HTREEITEM tree_item = GetTreeCtrl ().GetChildItem (parent_item);
			tree_item != nullptr;
			tree_item = GetTreeCtrl ().GetNextSiblingItem (tree_item))
	{
		Free_Child_Models (tree_item);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Highest_LOD
//
////////////////////////////////////////////////////////////////////////////
void
Set_Highest_LOD (RenderObjClass *render_obj)
{
	if (render_obj != nullptr) {
		for (int index = 0; index < render_obj->Get_Num_Sub_Objects (); index ++) {
			RenderObjClass *sub_obj = render_obj->Get_Sub_Object (index);
			if (sub_obj != nullptr) {
				Set_Highest_LOD (sub_obj);
			}
			REF_PTR_RELEASE (sub_obj);
		}

		//
		// Switcht this LOD to its highest level
		//
		if (render_obj->Class_ID () == RenderObjClass::CLASSID_HLOD) {
			((HLodClass *)render_obj)->Set_LOD_Level (((HLodClass *)render_obj)->Get_Lod_Count () - 1);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Restrict_Anims
//
////////////////////////////////////////////////////////////////////////////
void
CDataTreeView::Restrict_Anims (bool onoff)
{
	if (m_RestrictAnims != onoff) {
		m_RestrictAnims = onoff;

		//
		//	Reload the tree
		//
		GetTreeCtrl ().DeleteAllItems ();
		CreateRootNodes ();
		LoadAssetsIntoTree ();
	}
}

// TheSuperHackers @feature Eye state icons for part visibility, drawn for the current theme.
// State 1 = hidden (closed eye), state 2 = shown (open eye).
// Index 0 is a reserved transparent dummy; comctl32 treats state image index 0 as
// "no image".
void CDataTreeView::RebuildStateImageList (void)
{
    // GDI does not antialias, so each eye is drawn large and averaged down
    const int SIZE  = 16;
    const int SCALE = 4;
    const int BIG   = SIZE * SCALE;

    const bool isDark = W3DDarkMode::IsDark ();
    const COLORREF bgColor = isDark ? W3DDarkMode::GetCtrlBackgroundColor () : RGB (255, 255, 255);
    const COLORREF fgColor = isDark ? W3DDarkMode::GetTextColor () : RGB (48, 48, 48);
    const COLORREF dimColor = RGB ((GetRValue (fgColor) + GetRValue (bgColor)) / 2,
                                   (GetGValue (fgColor) + GetGValue (bgColor)) / 2,
                                   (GetBValue (fgColor) + GetBValue (bgColor)) / 2);

    CImageList stateImageList;
    stateImageList.Create (SIZE, SIZE, ILC_COLOR32, 3, 1);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof (BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = SIZE;
    bmi.bmiHeader.biHeight      = -SIZE; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    BITMAPINFO bigInfo = bmi;
    bigInfo.bmiHeader.biWidth  = BIG;
    bigInfo.bmiHeader.biHeight = -BIG;

    for (int state = 0; state < 3; state++)
    {
        void *pvBits = nullptr;
        HBITMAP hBmp = ::CreateDIBSection (nullptr, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        if (hBmp == nullptr || pvBits == nullptr)
        {
            continue;
        }

        DWORD *pixels = static_cast<DWORD *> (pvBits);
        for (int i = 0; i < SIZE * SIZE; i++)
        {
            pixels[i] = 0x00000000;
        }

        void *pvBigBits = nullptr;
        HBITMAP hBigBmp = (state != 0) ? ::CreateDIBSection (nullptr, &bigInfo, DIB_RGB_COLORS, &pvBigBits, nullptr, 0) : nullptr;
        if (hBigBmp != nullptr && pvBigBits != nullptr)
        {
            const bool shown = (state == 2);
            const COLORREF color = shown ? fgColor : dimColor;

            HDC hDC = ::CreateCompatibleDC (nullptr);
            HGDIOBJ hOldBmp = ::SelectObject (hDC, hBigBmp);
            HBRUSH hBackground = ::CreateSolidBrush (bgColor);
            RECT rect = { 0, 0, BIG, BIG };
            ::FillRect (hDC, &rect, hBackground);
            ::DeleteObject (hBackground);

            // The lids are two arcs that meet at the corners of the eye
            const int POINTS = 17;
            POINT upper[POINTS];
            POINT lower[POINTS];
            for (int i = 0; i < POINTS; i++)
            {
                const float t = (float)i / (float)(POINTS - 1);
                const int x = 6 + (int)(t * (float)(BIG - 12));
                const int lift = (int)(::sin (t * 3.14159265F) * (float)BIG * 0.27F);
                upper[i].x = x;
                upper[i].y = BIG / 2 - lift;
                lower[i].x = x;
                lower[i].y = BIG / 2 + lift;
            }

            HPEN hPen = ::CreatePen (PS_SOLID, SCALE + 2, color);
            HBRUSH hPupil = ::CreateSolidBrush (color);
            HGDIOBJ hOldPen = ::SelectObject (hDC, hPen);
            HGDIOBJ hOldBrush = ::SelectObject (hDC, hPupil);

            ::Polyline (hDC, lower, POINTS);
            if (shown)
            {
                ::Polyline (hDC, upper, POINTS);
                const int radius = BIG / 7;
                ::Ellipse (hDC, BIG / 2 - radius, BIG / 2 - radius, BIG / 2 + radius, BIG / 2 + radius);
            }
            ::GdiFlush ();

            ::SelectObject (hDC, hOldBrush);
            ::SelectObject (hDC, hOldPen);
            ::SelectObject (hDC, hOldBmp);
            ::DeleteObject (hPupil);
            ::DeleteObject (hPen);
            ::DeleteDC (hDC);

            const DWORD *big = static_cast<const DWORD *> (pvBigBits);
            for (int y = 0; y < SIZE; y++)
            {
                for (int x = 0; x < SIZE; x++)
                {
                    DWORD red = 0;
                    DWORD green = 0;
                    DWORD blue = 0;
                    for (int sy = 0; sy < SCALE; sy++)
                    {
                        for (int sx = 0; sx < SCALE; sx++)
                        {
                            const DWORD sample = big[(y * SCALE + sy) * BIG + (x * SCALE + sx)];
                            red   += (sample >> 16) & 0xFF;
                            green += (sample >> 8) & 0xFF;
                            blue  += sample & 0xFF;
                        }
                    }
                    const DWORD samples = SCALE * SCALE;
                    pixels[y * SIZE + x] = 0xFF000000u | ((red / samples) << 16) | ((green / samples) << 8) | (blue / samples);
                }
            }
        }
        if (hBigBmp != nullptr)
        {
            ::DeleteObject (hBigBmp);
        }

        ::ImageList_Add (stateImageList.GetSafeHandle (), hBmp, nullptr);
        ::DeleteObject (hBmp);
    }

    // SetImageList returns the previous image list; let MFC clean it up via the
    // owning CImageList wrapper going out of scope.
    GetTreeCtrl ().SetImageList (&stateImageList, TVSIL_STATE);
    stateImageList.Detach ();
}

LRESULT CDataTreeView::OnW3DViewThemeChanged (WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (::IsWindow (GetSafeHwnd ()))
    {
        RebuildNormalImageList ();
        RebuildStateImageList ();
        GetTreeCtrl ().Invalidate ();
    }
    return 0;
}

// TheSuperHackers @feature Rebuilds the tree-view's TVSIL_NORMAL icon list using
// the dark-mode resource IDs when dark mode is active, or the original light icons
// otherwise. Index assignments (m_iAnimationIcon, m_iMeshIcon, etc.) are preserved
// in both paths so existing item bindings remain valid.
void CDataTreeView::RebuildNormalImageList (void)
{
    const bool isDark = W3DDarkMode::IsDark ();

    if (m_normalImageList.GetSafeHandle ())
    {
        m_normalImageList.DeleteImageList ();
    }
    m_normalImageList.Create (16, 16, ILC_COLOR32 | ILC_MASK, 12, 0);

    auto addIcon = [&] (UINT idLight, UINT idDark) -> int {
        UINT id = isDark ? idDark : idLight;
        HICON h = (HICON)::LoadImage (::AfxGetResourceHandle (), MAKEINTRESOURCE (id),
            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        return m_normalImageList.Add (h);
    };

    m_iAnimationIcon    = addIcon (IDI_ANIMATION,                  IDI_TREE_ANIMATION_DM);
    m_iTCAnimationIcon  = addIcon (IDI_ANIMATION_COMPRESSED,       IDI_TREE_ANIMATION_COMPRESSED_DM);
    m_iADAnimationIcon  = addIcon (IDI_ANIMATION_COMPRESSED_DELTA, IDI_TREE_ANIMATION_DELTA_DM);
    m_iMeshIcon         = addIcon (IDI_MESH,                       IDI_TREE_MESH_DM);
    m_iMaterialIcon     = addIcon (IDI_MATERIAL,                   IDI_TREE_MATERIAL_DM);
    m_iLODIcon          = addIcon (IDI_LOD,                        IDI_TREE_LOD_DM);
    m_iAggregateIcon    = addIcon (IDI_HIERARCHY,                  IDI_TREE_HIERARCHY_DM);
    m_iEmitterIcon      = addIcon (IDI_HIERARCHY,                  IDI_TREE_HIERARCHY_DM);
    m_iHierarchyIcon    = addIcon (IDI_HIERARCHY,                  IDI_TREE_HIERARCHY_DM);
    m_iPrimitivesIcon   = addIcon (IDI_PRIMITIVES,                 IDI_TREE_PRIMITIVES_DM);
    m_iSoundIcon        = addIcon (IDI_SOUND,                      IDI_TREE_SOUND_DM);
    m_iBoneIcon         = addIcon (IDI_BONE,                       IDI_TREE_BONE_DM);

    GetTreeCtrl ().SetImageList (&m_normalImageList, TVSIL_NORMAL);
}

