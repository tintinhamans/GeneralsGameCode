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

// TexturePathDialog.cpp : implementation file
//

#include "StdAfx.h"
#include "W3DView.h"
#include "W3DViewDoc.h"
#include "TexturePathDialog.h"
#include "Utils.h"
#include <atlbase.h>
#include <shobjidl.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////
//
// TexturePathDialogClass
//
/////////////////////////////////////////////////////////////////////////////
TexturePathDialogClass::TexturePathDialogClass(CWnd* pParent /*=nullptr*/)
	: CDialog(TexturePathDialogClass::IDD, pParent)
{
	//{{AFX_DATA_INIT(TexturePathDialogClass)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


/////////////////////////////////////////////////////////////////////////////
//
// DoDataExchange
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::DoDataExchange (CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(TexturePathDialogClass)
	DDX_Control(pDX, IDC_PATH_LIST, m_PathList);
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(TexturePathDialogClass, CDialog)
	//{{AFX_MSG_MAP(TexturePathDialogClass)
	ON_BN_CLICKED(IDC_ADD_PATH, OnAddPath)
	ON_BN_CLICKED(IDC_REMOVE_PATH, OnRemovePath)
	ON_BN_CLICKED(IDC_MOVE_UP, OnMoveUp)
	ON_BN_CLICKED(IDC_MOVE_DOWN, OnMoveDown)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
//
// OnInitDialog
//
/////////////////////////////////////////////////////////////////////////////
BOOL
TexturePathDialogClass::OnInitDialog ()
{
	CDialog::OnInitDialog ();

	CW3DViewDoc *doc = ::GetCurrentDocument ();

	// Set up the list control columns
	m_PathList.InsertColumn(0, _T("Texture Path"), LVCFMT_LEFT, 400);

	// Populate the list with existing texture paths
	// Registry order (TexturePath0, 1, 2) matches UI display order directly
	for (int i = 0; i < doc->Get_Texture_Path_Count (); i++) {
		const CString &path = doc->Get_Texture_Path (i);
		m_PathList.InsertItem(i, path);
	}

	return TRUE;
}


/////////////////////////////////////////////////////////////////////////////
//
// OnOK
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::OnOK ()
{
	CW3DViewDoc *doc = ::GetCurrentDocument ();

	// Clear the existing texture paths to start fresh
	doc->Clear_Texture_Paths ();

	// Add paths from UI to document in forward order (registry indices match UI order)
	for (int i = 0; i < m_PathList.GetItemCount (); i++) {
		doc->Add_Texture_Path (m_PathList.GetItemText (i, 0));
	}

	// Save the paths to the registry
	doc->Save_Texture_Paths_To_Registry ();

	// Reset and rebuild the file factory search path from the updated vector
	doc->Refresh_File_Factory_Registrations ();

	CDialog::OnOK ();
}


/////////////////////////////////////////////////////////////////////////////
//
// OnAddPath
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::OnAddPath (void)
{
	// Use modern folder picker dialog (IFileDialog with RAII COM wrappers)
	CComPtr<IFileDialog> pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

	if (SUCCEEDED(hr)) {
		DWORD dwOptions = 0;
		pfd->GetOptions(&dwOptions);
		pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
		pfd->SetTitle(L"Select Texture Directory");

		if (SUCCEEDED(pfd->Show(m_hWnd))) {
			CComPtr<IShellItem> psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				CComHeapPtr<WCHAR> pszPath;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
					CString path(pszPath);

					// Check if path already exists
					bool exists = false;
					for (int i = 0; i < m_PathList.GetItemCount (); i++) {
						if (m_PathList.GetItemText (i, 0).CompareNoCase (path) == 0) {
							exists = true;
							break;
						}
					}

					if (!exists) {
						m_PathList.InsertItem(m_PathList.GetItemCount (), path);
					}
				}
			}
		}
	}

	return ;
}


/////////////////////////////////////////////////////////////////////////////
//
// OnRemovePath
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::OnRemovePath ()
{
	// Get the selected item
	int nItem = m_PathList.GetNextItem(-1, LVNI_SELECTED);

	if (nItem >= 0) {
		m_PathList.DeleteItem(nItem);
	}
}


/////////////////////////////////////////////////////////////////////////////
//
// OnMoveUp
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::OnMoveUp ()
{
	// Get the selected item
	int nItem = m_PathList.GetNextItem(-1, LVNI_SELECTED);

	// Can only move up if item is selected and not already at the top
	if (nItem > 0) {
		CString path = m_PathList.GetItemText (nItem, 0);
		m_PathList.DeleteItem(nItem);
		m_PathList.InsertItem(nItem - 1, path);
		m_PathList.SetItemState(nItem - 1, LVIS_SELECTED, LVIS_SELECTED);
	}

	return ;
}


/////////////////////////////////////////////////////////////////////////////
//
// OnMoveDown
//
/////////////////////////////////////////////////////////////////////////////
void
TexturePathDialogClass::OnMoveDown (void)
{
	// Get the selected item
	int nItem = m_PathList.GetNextItem(-1, LVNI_SELECTED);

	// Can only move down if item is selected and not already at the bottom
	if (nItem >= 0 && nItem < m_PathList.GetItemCount () - 1) {
		CString path = m_PathList.GetItemText (nItem, 0);
		m_PathList.DeleteItem(nItem);
		m_PathList.InsertItem(nItem + 1, path);
		m_PathList.SetItemState(nItem + 1, LVIS_SELECTED, LVIS_SELECTED);
	}
}


