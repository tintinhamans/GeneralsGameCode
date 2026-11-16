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
 *                     $Archive:: /Commando/Code/Tools/W3DView/ViewerAssetMgr.h             $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 4/12/01 3:00p                                               $*
 *                                                                                             *
 *                    $Revision:: 7                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WW3D2/assetmgr.h"
#include "WWLib/hashtemplate.h"
#include "WWLib/wwstring.h"


/////////////////////////////////////////////////////////////////////////////
//
// ViewerAssetMgrClass
//
/////////////////////////////////////////////////////////////////////////////
class ViewerAssetMgrClass : public WW3DAssetManager
{
public:

	///////////////////////////////////////////////////
	//	Public constructors/destructors
	///////////////////////////////////////////////////
	ViewerAssetMgrClass () {}
	virtual ~ViewerAssetMgrClass () override {}

	///////////////////////////////////////////////////
	//	Public methods
	///////////////////////////////////////////////////

	//
	// Base class overrides
	//
	virtual bool						Load_3D_Assets (FileClass &w3dfile) override;

	// TheSuperHackers @performance Override the filename-based Load_3D_Assets to
	// short-circuit when a prototype matching the file's basename already exists.
	// The engine's load-on-demand path (Create_Render_Obj / Get_HAnim / Get_HTree)
	// repeatedly calls this overload for cross-referenced files; without a guard,
	// the same .w3d gets re-parsed once per referencing file in a batch import.
	// The game-side W3DAssetManager already has this same pattern.
	virtual bool						Load_3D_Assets (const char *filename) override;

	//
	//	Texture caching overrides
	//
	virtual void						Open_Texture_File_Cache(const char * /*prefix*/);
	virtual void						Close_Texture_File_Cache();

	//
	// TheSuperHackers @performance Index-based prototype access for callers
	// that need to enumerate "new prototypes since a known count" without
	// re-scanning the entire prototype list each time.
	//
	int									Get_Prototype_Count() const { return Prototypes.Count(); }
	const char *						Get_Prototype_Name(int index) const;

	// TheSuperHackers @performance Tria 2026-05-19 Reset the missing-file cache.
	// Call this when the file factory's search path changes so previously missing
	// files get re-probed against the new search locations.
	void								Reset_Missing_File_Cache() { m_MissingFileCache.Remove_All (); }

private:

	// TheSuperHackers @performance Tria 2026-05-19 Cache of filenames whose last
	// Load_3D_Assets() probe failed. Load-on-demand (Create_Render_Obj / Get_HTree)
	// synthesizes names like "BOX01.w3d" / "ENGINE01.w3d" from every mesh subobject
	// and bone in a scene, then probes them via the file factory. Most are not real
	// files and each miss costs a CreateFile against every search-path entry. Without
	// caching, the same nonexistent name is probed every time the tree walker, anim
	// list, or render iterator asks for that subobject.
	HashTemplateClass<StringClass, bool>	m_MissingFileCache;
};
