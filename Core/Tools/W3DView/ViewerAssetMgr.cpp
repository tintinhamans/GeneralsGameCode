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
 *                 Project Name : LevelEdit                                                    *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Tools/W3DView/ViewerAssetMgr.cpp             $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 6/27/01 1:39p                                               $*
 *                                                                                             *
 *                    $Revision:: 10                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "StdAfx.h"

#include "ViewerAssetMgr.h"
#include "WW3D2/proto.h"


////////////////////////////////////////////////////////////////////////
//
//	Load_3D_Assets
//
////////////////////////////////////////////////////////////////////////
bool
ViewerAssetMgrClass::Load_3D_Assets (FileClass &w3dfile)
{
	//
	//	Allow the base class to process
	//
	bool retval = WW3DAssetManager::Load_3D_Assets (w3dfile);
	if (retval) {

	}

	return retval;
}


////////////////////////////////////////////////////////////////////////
//
//	Load_3D_Assets (filename overload)
//
//	TheSuperHackers @performance Tria 2026-05-19 Mirror the game-side
//	W3DAssetManager pattern: if a prototype with the same basename as
//	this file already exists, skip the re-parse. The engine's load-on-
//	demand path (Create_Render_Obj / Get_HAnim / Get_HTree) calls this
//	overload repeatedly during batch imports for any cross-referenced
//	file, causing the same shared skeleton/animation .w3d to be read
//	from disk dozens to hundreds of times. The short-circuit drops
//	those redundant reads. User-initiated loads come through
//	LoadAssetsFromFile -> WW3DAssetManager::Load_3D_Assets(FileClass&),
//	which bypasses this overload, so first-time loads are unaffected.
//
////////////////////////////////////////////////////////////////////////
bool
ViewerAssetMgrClass::Load_3D_Assets (const char *filename)
{
	if (filename == nullptr) {
		return false;
	}

	char basename[512];
	::strncpy (basename, filename, sizeof(basename) - 1);
	basename[sizeof(basename) - 1] = '\0';
	char *pext = ::strrchr (basename, '.');
	if (pext) *pext = '\0';

	// Strip the "..\\" prefix that load-on-demand adds for parent-dir retries
	// so its lookup hits the same cache entry as the first attempt.
	const char *bareName = basename;
	while (bareName[0] == '.' && bareName[1] == '.' && (bareName[2] == '\\' || bareName[2] == '/')) {
		bareName += 3;
	}

	if (Find_Prototype (bareName) != nullptr) {
		// Already loaded - nothing to do.
		return true;
	}

	// TheSuperHackers @performance Tria 2026-05-19 Short-circuit names that
	// already failed a Load_3D_Assets probe. Without this, the engine fires
	// CreateFile against every search-path candidate for every fake subobject
	// .w3d name (BOX01.w3d, ENGINE01.w3d, HOUSECOLOR*.w3d, ...) once per
	// referencing prototype - hundreds of nonexistent-file probes per import.
	StringClass missKey (bareName);
	if (m_MissingFileCache.Exists (missKey)) {
		return false;
	}

	bool result = WW3DAssetManager::Load_3D_Assets (filename);
	if (!result) {
		m_MissingFileCache.Insert (missKey, true);
	}
	return result;
}



////////////////////////////////////////////////////////////////////////
//
//	Open_Texture_File_Cache
//
////////////////////////////////////////////////////////////////////////
void
ViewerAssetMgrClass::Open_Texture_File_Cache (const char * /*prefix*/)
{
}


////////////////////////////////////////////////////////////////////////
//
//	Close_Texture_File_Cache
//
////////////////////////////////////////////////////////////////////////
void
ViewerAssetMgrClass::Close_Texture_File_Cache ()
{
}


////////////////////////////////////////////////////////////////////////
//
//	Get_Prototype_Name
//
//	TheSuperHackers @performance Index-based lookup so callers can walk
//	the tail of the prototype list after Load_3D_Assets in O(new) rather
//	than re-iterating from the beginning each time.
//
////////////////////////////////////////////////////////////////////////
const char *
ViewerAssetMgrClass::Get_Prototype_Name (int index) const
{
	if (index < 0 || index >= Prototypes.Count ()) return nullptr;
	PrototypeClass *p = Prototypes[index];
	return p ? p->Get_Name () : nullptr;
}
