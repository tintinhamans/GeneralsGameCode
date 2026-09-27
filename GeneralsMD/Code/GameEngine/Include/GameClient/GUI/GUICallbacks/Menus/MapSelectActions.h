/*
**	Command & Conquer Generals Zero Hour(tm)
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

// FILE: MapSelectActions.h ////////////////////////////////////////////////////
// Widget-agnostic map-select logic: the initial official/custom filter and the
// OK write-back into a GameInfo (map/CRC/size, start-position reset). No
// GameWindow/gadget coupling, so SkirmishMapSelectMenu.cpp's .wnd callbacks and
// RmlSkirmishMapSelectScreen drive the same GameInfo state through these
// functions instead of duplicating it. The map list itself is already
// widget-agnostic (see MapUtil.h's buildFilteredMapList()); this only covers
// what SkirmishMapSelectMenu.cpp did beyond building that list.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"

class AsciiString;
class GameInfo;

namespace MapSelectActions
{
	// Whether the map-select filter should default to system (official) maps: the LAN
	// preference's saved choice, overridden by currentMap's own official/custom status when
	// currentMap is a known map, same as SkirmishMapSelectMenuInit().
	Bool initialUsesSystemMaps( const AsciiString &currentMap );

	// ButtonOK: writes mapName into game (CRC/size from TheMapCache, zeroed if not found) and
	// resets every slot's start position to -1, same as SkirmishMapSelectMenu.cpp's ButtonOK
	// handler. No-op if game is null.
	void applySelectedMap( GameInfo *game, const AsciiString &mapName );
}
