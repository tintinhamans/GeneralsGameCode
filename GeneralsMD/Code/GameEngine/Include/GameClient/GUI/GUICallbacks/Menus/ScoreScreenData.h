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

// FILE: ScoreScreenData.h ////////////////////////////////////////////////////
// Widget-agnostic score screen content: what the score screen displays for every
// mode (single player, skirmish, LAN, internet, replay), with no GameWindow/gadget
// coupling. ScoreScreen.cpp's grabMultiPlayerInfo()/grabSinglePlayerInfo() build
// this once per screen entry and fill their .wnd gadgets from it, so a future
// non-.wnd front end (e.g. RmlUi) can build the same data and render it itself.
//
// Building this data can have the same side effects ScoreScreen.cpp has always had
// on screen entry (battle honor writes, online stats posting): call build* exactly
// once per screen entry, same as today.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameClient/Color.h"

#include <vector>

class Image;
class Player;

// Aggregated score totals for one player, or (single player only) for a group of
// allied/enemy AI opponents shown as a single combined row.
struct ScoreGather
{
	Int m_totalMoneyEarned = 0;
	Int m_totalMoneySpent = 0;
	Int m_totalUnitsDestroyed = 0;
	Int m_totalUnitsBuilt = 0;
	Int m_totalUnitsLost = 0;
	Int m_totalBuildingsDestroyed = 0;
	Int m_totalBuildingsBuilt = 0;
	Int m_totalBuildingsLost = 0;
	const Image *m_sideImage = nullptr;
};

// One row of the score screen's player list: a real player, an observer, or (single
// player only) an aggregated allied/enemy side.
struct ScoreScreenPlayerRow
{
	UnicodeString m_displayName;
	Color m_textColor = 0;
	Bool m_isObserver = FALSE;
	ScoreGather m_stats;

	// True for the row backed by ThePlayerList->getLocalPlayer() (never true for the
	// "observer watching a human" substitute row in single player). Gates the academy
	// advice listbox's visibility in populatePlayerInfo() -- unhidden whenever this is the
	// local player's row, even with an empty m_academyAdvice (e.g. single player campaign).
	Bool m_isLocalPlayerRow = FALSE;

	// Winner/side icon. m_touchSideIcon mirrors the source code's own inconsistency between
	// populatePlayerInfo() (always unhides the icon gadget) and populateSideInfo() (only
	// touches it when there is an image, otherwise leaves the gadget at its .wnd default):
	// false means "don't touch the icon gadget at all", matching populateSideInfo() exactly.
	const Image *m_sideIconImage = nullptr;
	Bool m_touchSideIcon = FALSE;

	// Academy advice tips; non-empty only for the local player's row, and only in
	// skirmish/internet modes.
	std::vector<UnicodeString> m_academyAdvice;
};

enum ScoreScreenModeType
{
	SCORESCREENMODE_SINGLEPLAYER = 0,
	SCORESCREENMODE_SKIRMISH,
	SCORESCREENMODE_LAN,
	SCORESCREENMODE_INTERNET,
	SCORESCREENMODE_REPLAY
};

struct ScoreScreenData
{
	ScoreScreenModeType m_mode = SCORESCREENMODE_SINGLEPLAYER;
	std::vector<ScoreScreenPlayerRow> m_rows;

	// Score screen backdrop image (per side, or the generic multiplayer backdrop).
	const Image *m_backgroundImage = nullptr;
	Bool m_hasBackgroundImage = FALSE;

	// Multiplayer (skirmish/LAN/internet/replay): one row per occupied slot, sorted by
	// score descending, observers appended with no stats. mode selects the per-mode
	// side effects populatePlayerInfo() used to run inline (skirmish battle honors,
	// internet stats posting). Moved from ScoreScreen.cpp's
	// grabMultiPlayerInfo()/populatePlayerInfo().
	static ScoreScreenData buildForMultiPlayer(ScoreScreenModeType mode);

	// Single player (campaign/challenge, and single player replay): the local player's
	// row, followed by one aggregated row per allied/enemy side that has participants.
	// Moved from ScoreScreen.cpp's grabSinglePlayerInfo().
	static ScoreScreenData buildForSinglePlayer();

	// Whether there is another queued simulation replay to advance to. Moved from
	// ScoreScreen.cpp's showReplayButtonContinue().
	static Bool replayHasMoreEntries();
};
