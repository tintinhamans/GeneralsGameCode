/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// TheSuperHackers @feature W3DView material viewer edit mode.
// Writes an edited MaterialDocument back into its .w3d file. Only the material
// payloads the editor exposes are patched (see W3dChunkTree for the
// structure-preserving rewrite); everything else is copied byte-identically.
//
// Save pipeline: round-trip identity gate -> apply edits -> write <file>.tmp
// -> re-parse the tmp and verify it matches the edited document -> create
// <file>.bak (once) -> atomically replace the original. A failure at any step
// leaves the original file untouched.

#pragma once

#include "W3dMaterialData.h"

#include <string>

namespace W3dMaterialViewer
{

// Saves the edited document to document.resolvedDiskPath. Returns false and
// fills `errorMessage` on any failure; the original file is never modified on
// a failed save.
bool SaveMaterialDocument(const MaterialDocument &document, std::string &errorMessage);

// Whether a save makes a one-time pristine <file>.bak before replacing the
// original (File > Backup File Before Change). Default true. When off, saves
// overwrite in place with no .bak created.
void Set_Backup_Before_Save(bool enabled);
bool Get_Backup_Before_Save();

} // namespace W3dMaterialViewer
