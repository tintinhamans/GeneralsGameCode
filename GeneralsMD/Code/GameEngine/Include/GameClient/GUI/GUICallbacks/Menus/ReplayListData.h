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

// FILE: ReplayListData.h ////////////////////////////////////////////////////
// Widget-agnostic list of the replay files on disk, shared by ReplayMenu.wnd, PopupReplay.wnd and
// their RmlUi replacements: one row per readable replay with the strings and colors to show.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/Recorder.h"
#include "Common/UnicodeString.h"
#include "GameClient/MapUtil.h"

#include <vector>

// One player of a replay: a human's name or an AI's level, and their colour.
struct ReplayPlayer
{
	UnicodeString m_name;
	UnsignedInt m_rgb = 0; ///< 0x00RRGGBB
	Bool m_hasColor = FALSE;
};

struct ReplayRow
{
	UnicodeString m_name; ///< the file name without extension, or "Last Replay" for the last game's
	UnicodeString m_time;
	UnicodeString m_date;
	UnicodeString m_version;
	UnicodeString m_map;
	UnicodeString m_tooltip; ///< play time and the human players
	UnsignedInt m_color = 0xFFFFFF; ///< 0x00RRGGBB, of everything but the map
	UnsignedInt m_mapColor = 0xFFFFFF;
	AsciiString m_fileName; ///< with extension, in the replay directory
	AsciiString m_mapPath; ///< the map the game was played on, for its preview
	UnicodeString m_duration; ///< hh:mm:ss of game time
	std::vector<ReplayPlayer> m_players; ///< everyone but observers, in slot order
	Bool m_hasMap = FALSE; ///< the map is installed
	Bool m_isCompatible = FALSE; ///< recorded by this game version, so it plays back
	Bool m_isMultiplayer = FALSE;
};

namespace ReplayList
{
	static const Int MAX_ROWS = 100; ///< the .wnd listboxes' LENGTH

	// Reads the header and game options of a replay and looks up its map.
	Bool readMapInfo( const AsciiString &filename, RecorderClass::ReplayHeader &header, ReplayGameInfo &info, const MapMetaData *&mapData );

	// Rebuilds rows from the replay directory. Returns FALSE and leaves rows alone while the map
	// cache does not exist yet.
	Bool scan( std::vector<ReplayRow> &rows );
}
