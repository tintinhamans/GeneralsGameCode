/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding changes, one switch each, all on by default.
//
// -pathFix <name>[,<name>...] edits the set, left to right: "none" turns every change off (the game then runs exactly
// as without this code: contraZH's own pathfinding), "default" turns the set below on, <name> turns one on and -<name>
// turns one off. So "-pathFix -<name>" is the default set without that change.
//   coarse     the coarse pass's corridor is widened by one block along the whole route (contraZH's coarse pass
//              already leaves its start block, uses the unit's own surfaces and stops after 4,000 cells)
//
// All of these change routes, so every player of a game, and a replay's playback, must use the same set. When any is on,
// the set goes into the pathfinder's CRC, so players with different sets see a mismatch at once. An unknown name stops
// the game at start.

#pragma once

#include "Lib/BaseType.h"

namespace PathTuning
{
	extern Bool CoarseFix;

	const Int SEARCH_CAP_CELLS = 10000;		///< "cap": the cells a search may use

	// The words the set writes into the CRC, as the measured builds wrote them: the switch mask, an estimate weight of
	// 1.000, the cell cap and a path queue rate of 6 and 12.
	const Int CRC_WEIGHT = 1000;
	const Int CRC_QUEUE_BASE = 6;
	const Int CRC_QUEUE_MAX = 12;

	Bool Set(const char * list);			///< -pathFix; FALSE: a name was not known (the others are still set)
	void Describe(char * out, Int size);	///< "coarse,crossings,..." (or "none")
	Bool Any();								///< is any change on?
	UnsignedInt Mask();						///< one bit per change (the measured builds' bit numbers)
}
