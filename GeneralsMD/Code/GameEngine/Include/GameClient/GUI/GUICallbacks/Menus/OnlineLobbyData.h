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

// FILE: OnlineLobbyData.h //////////////////////////////////////////////////////
// Widget-agnostic Generals Online custom-lobby content (Menus/WOLCustomLobby.wnd):
// game list rows, player list rows and roster diffing. No GameWindow/gadget
// coupling, mirrors LanLobbyData.h's role for the LAN lobby.
//
// Game rows: buildGameRow() reproduces LobbyUtils.cpp's insertGame() column
// computation (name+owner, map, player-count fullness tier, password/observer/
// stats flags, ping tier, buddy highlight, CRC mismatch) as a pure function, so
// both the .wnd listbox (insertGame()) and RmlOnlineLobbyScreen can render the
// same row shape from a LobbyEntry. The "ladder" column is intentionally not
// modeled: insertGame() itself always resolves it to a hardcoded "TODO_NGMP"
// AsciiString (dead/unimplemented on Generals Online), so there is nothing live
// to share.
//
// Player rows: CollectLobbyPlayerRows()/BuildLobbyRosterSignature() moved here
// verbatim from WOLLobbyMenu.cpp (previously static/file-local) so both the .wnd
// listboxLobbyPlayers rebuild (RebuildLobbyPlayerList(), which still lives in
// WOLLobbyMenu.cpp -- too entangled with GadgetListBox/rank-icon-prefetch state
// to move) and RmlOnlineLobbyScreen can build the same roster order/signature.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct LobbyEntry; // GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h

namespace OnlineLobbyData
{
	// Player-count fullness tier for the row's numeric player-count color.
	enum PlayerCountTier
	{
		PLAYERCOUNT_NORMAL = 0,
		PLAYERCOUNT_ALMOST_FULL,
		PLAYERCOUNT_FULL,
	};

	// Ping tier, same <250/<500/else buckets as insertGame()'s pingImages[0..2] selection.
	enum PingTier
	{
		PING_GOOD = 0,
		PING_OK,
		PING_BAD,
	};

	// One row of the game list (see insertGame() in LobbyUtils.cpp, which this mirrors).
	struct GameRow
	{
		int64_t lobbyID = -1;
		std::string displayName;    // "GameName (OwnerName)"
		std::string mapDisplayName;
		std::string playersText;    // "3/8"
		PlayerCountTier playersTier = PLAYERCOUNT_NORMAL;
		bool hasPassword = false;
		bool allowObservers = false;
		bool trackStats = false;
		PingTier pingTier = PING_GOOD;
		bool hasBuddy = false;
		bool crcMismatch = false;
	};

	// Pure computation of a game row from raw lobby data, plus the two flags insertGame() resolves
	// via lobbyHasBuddy()/TheGlobalData CRC comparison (kept out of this function so it has no
	// GameSpy/global-data coupling; callers pass the already-resolved values).
	GameRow buildGameRow( const LobbyEntry &lobby, bool hasBuddy, bool crcMismatch );

	// Room roster: name, then admins, then friends (see CollectLobbyPlayerRows() call sites in
	// WOLLobbyMenu.cpp/PopulateLobbyPlayerListbox()).
	struct PlayerRow
	{
		int64_t userID = 0;
		bool isAdmin = false;
		bool isFriend = false;
		bool isIgnored = false;
		std::string displayName;
		std::string sortKey; // lowercase display name
	};

	// Current room's members, sorted name-then-admins-then-friends. Empty if there is no rooms
	// interface or no current room.
	std::vector<PlayerRow> collectPlayerRows();

	// Cheap membership+flags signature so callers can skip an expensive rebuild when nothing changed
	// (same purpose as WOLLobbyMenu.cpp's s_lobbyRosterSignature).
	std::string buildRosterSignature( const std::vector<PlayerRow> &rows );
}
