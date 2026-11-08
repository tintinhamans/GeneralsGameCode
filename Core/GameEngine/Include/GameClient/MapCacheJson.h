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

// FILE: MapCacheJson.h ///////////////////////////////////////////////////////////////////////////
// Desc: Generals Online mapcache.json next to the user MapCache.ini. It keeps the typed supply and
//       tech objects per map, which MapCache.ini cannot hold, keyed by the portable map path and
//       reused while the file size, CRC and format version match.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

#if defined(GENONLINE)

class MapCache;

// TheSuperHackers @feature arcticdolphin 08/11/2026 Expanded GO map cache with typed objects.

enum { MAP_CACHE_JSON_VERSION = 1 };

/// Fills maps without typed objects from mapcache.json, records freshly parsed maps and rewrites the file when it changed.
void syncMapCacheJson( MapCache &cache );

/// Makes sure the cached map has typed objects, parsing it once when mapcache.json has none; returns TRUE when it has them.
Bool ensureMapTypedObjects( const AsciiString &mapName );

#endif
