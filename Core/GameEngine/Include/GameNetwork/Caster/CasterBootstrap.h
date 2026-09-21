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

// The typed match-start bootstrap: everything the recorder's replay header
// carries, in typed form. A cast starts straight from it, and the recorder
// writes the optional local .rep copy's header from the same typed state.

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Caster/CasterLobby.h"

struct ReplayStartData;

namespace CasterBootstrap
{

/// Bound on one carried version/build string (UTF-8 bytes).
static const UnsignedInt MAX_VERSION_BYTES = 64;

/// Bound on the rebuilt GameInfo options string. The LAN options string is
/// capped at 400 characters, so this always has room.
static const UnsignedInt MAX_OPTIONS_BYTES = 512;

/// Replay header slot count. The recorder always writes MAX_SLOTS entries.
static const UnsignedInt REPLAY_SLOTS = 8;

/**
 * One match start, as every player publishes it once the slots are final.
 * Purely typed state: no replay bytes, no engine objects.
 */
struct MatchStart
{
	UnsignedInt gameUid;
	UnsignedInt frameCount;			///< recorded frame count; 0 for a live match
	UnsignedInt versionNumber;
	UnsignedInt exeCRC;
	UnsignedInt iniCRC;
	Int localPlayerIndex;			///< the recording player's slot (the host's)
	Int difficulty;
	Int originalGameMode;
	Int rankPoints;
	Int maxFPS;
	Char versionString[MAX_VERSION_BYTES + 1];		///< UTF-8
	Char versionTimeString[MAX_VERSION_BYTES + 1];	///< UTF-8
	CasterLobby::RoomSnapshot room;
};

/// Zeroes every field; a cleared bootstrap is never valid.
void clear(MatchStart& start);

/// TRUE when the bootstrap can start a playback: a game, a map, a slot table
/// and a local slot inside it.
Bool isValid(const MatchStart& start);

/**
 * Rebuilds the recorder's GameInfo options string from the typed room snapshot
 * (the exact inverse of GameInfoToAsciiString for the fields a snapshot holds).
 * Returns the written length without the terminator, or 0 when it does not fit.
 */
UnsignedInt buildGameOptions(const CasterLobby::RoomSnapshot& room, char* out, UnsignedInt cap);

/**
 * Fills the recorder's typed playback start data directly from a match start, so
 * starting a cast needs no replay bytes and no file at all.
 * Returns FALSE on invalid input or overflow.
 */
Bool fillReplayStart(const MatchStart& start, ReplayStartData& out);

}
