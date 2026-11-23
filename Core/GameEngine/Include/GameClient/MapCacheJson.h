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

///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

#if defined(GENONLINE)

class MapCache;

enum { MAP_CACHE_JSON_VERSION = 1 };

void syncMapCacheJson( MapCache &cache );

/// Parses the cached map once when mapcache.json has no typed objects for it. Returns TRUE when it has them.
Bool ensureMapTypedObjects( const AsciiString &mapName );

#endif
