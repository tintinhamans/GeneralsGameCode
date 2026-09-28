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

// FILE: QuickMatchData.h /////////////////////////////////////////////////////
// Widget-agnostic Quick Match (Generals Online) view state, same split as
// OnlineGameSetupData.h: a plain snapshot struct built from NGMP service state,
// no GameWindow involved, so WOLQuickMatchMenu.cpp's .wnd path and a future
// RmlUi screen can read the same thing.
//
// Only PlaylistMapInfo is populated in this commit: it replaces two duplicated,
// slightly-diverged inline copies of the playlist-index -> map-paths resolution
// (populateQuickMatchMapSelectListbox()'s copy handled custom/non-official maps,
// saveQuickMatchOptions()'s copy did not -- QuickMatchActions::getPlaylistMapInfo()
// is now the single implementation, matching the more complete one).
//
// The rest of the fields the Quick Match RmlUi screen will eventually need
// (search/status state, buddy badge count, stats via the shared PlayerStatsData)
// live in loose statics and the async NGMP lobby callbacks
// (RegisterForMatchmaking*/RegisterForJoinLobbyCallback) that WOLQuickMatchMenu.cpp
// still owns directly -- extracting those into this struct is the next commit's
// job (see the quick match triage note), not this one.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"

#include <list>

class MapMetaData;

namespace QuickMatchData
{
	// Resolved playlist maps + player-count floor, formatted into game map paths
	// (Maps\\name\\name.map for official EA maps, the full user-data path for custom maps).
	// Shared by populateQuickMatchMapSelectListbox() (map list display) and
	// saveQuickMatchOptions() (preference persistence) via
	// QuickMatchActions::getPlaylistMapInfo().
	struct PlaylistMapInfo
	{
		std::list<AsciiString> m_mapPaths;
		Int m_numPlayers = 0;        // PlaylistEntry::MinPlayers -- map-select player-count floor
		UnsignedShort m_playlistID = 0;
		Int m_minSelectedMaps = 0;   // PlaylistEntry::MinSelectedMaps
		Bool m_valid = FALSE;        // FALSE if playlistIndex didn't resolve to a cached playlist
	};
}
