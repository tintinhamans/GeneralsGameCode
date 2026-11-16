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

// TheSuperHackers @feature W3DView material viewer.
// Reads the material-related chunks of a .w3d file directly. The runtime WW3D
// objects do not preserve everything the 3ds Max material UI shows (mapper args,
// texture attribute bits, surface type, ...), so the material panel is fed from
// the raw chunk data instead.

#pragma once

#include "W3dMaterialData.h"

namespace W3dMaterialViewer
{

// Parses all mesh material data from the given .w3d file (absolute path).
// Returns false if the file could not be opened or contains no meshes.
bool ParseMaterialDocument(const char *path, MaterialDocument &document);

// Same, but resolves a bare filename (e.g. from the asset source-file map)
// through the registered file factory search paths.
bool ParseMaterialDocumentFromFactory(const char *filename, MaterialDocument &document);

} // namespace W3dMaterialViewer
