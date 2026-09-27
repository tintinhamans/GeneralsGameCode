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

// FILE: GameSetupData.h ////////////////////////////////////////////////////////
// Widget-agnostic skirmish game setup content: what the setup screen displays for
// every slot and for the game options, with no GameWindow/gadget coupling. Built
// from a GameInfo (SkirmishGameInfo today; LAN/online lobby's GameInfo subclasses
// tomorrow) so a future non-.wnd front end (e.g. RmlUi) can render the same thing
// SkirmishGameOptionsMenu.cpp's InitSkirmishGameGadgets()/skirmishUpdateSlotList()/
// updateSkirmishGameOptions() build from GameSlot/GameInfo/MapMetaData directly.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/Money.h"
#include "GameNetwork/GameInfo.h"

#include <vector>

// One slot row: occupant state (open/closed/AI difficulty/human), and the
// faction/color/team/start-position selections that only make sense once
// occupied. Mirrors GameSlot exactly; carried as its own struct so a renderer
// never needs GameSlot/GameInfo internals.
struct GameSetupSlotRow
{
	SlotState m_state = SLOT_OPEN;
	UnicodeString m_name;
	Int m_color = -1;
	Int m_playerTemplate = PLAYERTEMPLATE_RANDOM;
	Int m_teamNumber = -1;
	Int m_startPosition = -1;
	Bool m_isAI = FALSE;

	// True for the slot the local machine controls (the only slot a non-host can edit).
	Bool m_isLocalSlot = FALSE;

	// True if this slot can be edited from here: always true for the host, true for
	// non-hosts only on their own local slot.
	Bool m_canEdit = FALSE;
};

// Map/options half of the setup screen: what updateSkirmishGameOptions() /
// InitSkirmishGameGadgets() derive from the current map and GameInfo.
struct GameSetupOptionsData
{
	AsciiString m_mapName;
	UnicodeString m_mapDisplayName;
	Bool m_mapFound = FALSE;

	// False for a solo-campaign map picked from the map selector (Skirmish now allows
	// selecting them); gates the per-player gadgets exactly like updateSkirmishGameOptions().
	Bool m_mapIsMultiplayer = TRUE;
	Int m_mapNumPlayers = 0;

	Money m_startingCash;
	Bool m_superweaponsRestricted = FALSE;
};

struct GameSetupData
{
	std::vector<GameSetupSlotRow> m_slots; // always MAX_SLOTS entries
	GameSetupOptionsData m_options;

	// Snapshot the current slots and map/options state out of game. Read-only: no side
	// effects, safe to call every frame a renderer needs to refresh.
	static GameSetupData build( GameInfo *game );
};
