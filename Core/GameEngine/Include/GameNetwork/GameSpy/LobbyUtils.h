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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: LobbyUtils.h //////////////////////////////////////////////////////
// Generals lobby utils
// Author: Matthew D. Campbell, Sept 2002

#pragma once

class GameWindow;

GameWindow *GetGameListBox();
GameWindow *GetGameInfoListBox();
NameKeyType GetGameListBoxID();
NameKeyType GetGameInfoListBoxID();
void GrabWindowInfo();
void ReleaseWindowInfo();
void RefreshGameInfoListBox( GameWindow *mainWin, GameWindow *win );
void RefreshGameListBoxes();
void ToggleGameListType();

#if defined(GENONLINE)
// The game listbox's item data is a row index (32-bit safe), not the 64-bit lobby ID itself.
// Resolve the real ID with this before calling anything that needs it (e.g. GetLobbyFromID).
int64_t ResolveGameListLobbyID(Int rowItemData);

#endif
void playerTemplateComboBoxTooltip(GameWindow *wndComboBox, WinInstanceData *instData, UnsignedInt mouse);
void playerTemplateListBoxTooltip(GameWindow *wndListBox, WinInstanceData *instData, UnsignedInt mouse);

enum GameSortType CPP_11(: Int)
{
#if defined(GENONLINE)
	GAMESORT_AGE_ASCENDING = 0, // was alpha
    GAMESORT_AGE_DESCENDING,	// was alpha
	GAMESORT_MAP_ASCENDING,		// was ping
	GAMESORT_MAP_DESCENDING,	// was ping
	// GenOnline @feature arcticdolphin 08/11/2026 Sort the game list by player count or ping from its column headers, and the GameSpy sort keeps the alpha values.
	GAMESORT_ALPHA_ASCENDING,
#else
	GAMESORT_ALPHA_ASCENDING = 0,
#endif
	GAMESORT_ALPHA_DESCENDING,
#if defined(GENONLINE)
	GAMESORT_PLAYERS_ASCENDING,
	GAMESORT_PLAYERS_DESCENDING,
#endif
	GAMESORT_PING_ASCENDING,
	GAMESORT_PING_DESCENDING,
};

Bool HandleSortButton( NameKeyType sortButton );
void PopulateLobbyPlayerListbox();
#if defined(GENONLINE)

enum LobbyGameModeFilter CPP_11(: Int)
{
    LOBBY_FILTER_ALL = 0,
    LOBBY_FILTER_1V1,
    LOBBY_FILTER_TEAM,
    LOBBY_FILTER_FFA,
    LOBBY_FILTER_AOD,
    LOBBY_FILTER_BUDDIES,
    LOBBY_FILTER_JOINABLE,
};
#endif
