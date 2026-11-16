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
 *                     $Archive:: /Commando/Code/Tools/W3DView/DirectoryDialog.cpp            $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 1/25/00 2:56p                                               $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "StdAfx.h"
#include "DirectoryDialog.h"
#include "Utils.h"
#include <atlbase.h>
#include <shobjidl.h>


////////////////////////////////////////////////////////////////////////////
//
//	Browse_For_Folder
//
//  TheSuperHackers @refactor Tria 18/04/2026 Replace old GetOpenFileName
//  hook hack with modern IFileDialog folder picker.
//
////////////////////////////////////////////////////////////////////////////
bool
Browse_For_Folder (HWND parent_wnd, LPCTSTR initial_path, CString &path)
{
	bool retval = false;

	CComPtr<IFileDialog> pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

	if (SUCCEEDED(hr)) {
		DWORD dwOptions = 0;
		pfd->GetOptions(&dwOptions);
		pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
		pfd->SetTitle(L"Choose Directory");

		// Set initial folder if provided
		if (initial_path != nullptr && initial_path[0] != 0) {
			CComPtr<IShellItem> psiFolder;
			CStringW wInitialPath(initial_path);
			if (SUCCEEDED(SHCreateItemFromParsingName(wInitialPath, nullptr, IID_PPV_ARGS(&psiFolder)))) {
				pfd->SetFolder(psiFolder);
			}
		}

		if (SUCCEEDED(pfd->Show(parent_wnd))) {
			CComPtr<IShellItem> psi;
			if (SUCCEEDED(pfd->GetResult(&psi))) {
				CComHeapPtr<WCHAR> pszPath;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
					path = CString(pszPath);
					retval = true;
				}
			}
		}
	}

	return retval;
}
