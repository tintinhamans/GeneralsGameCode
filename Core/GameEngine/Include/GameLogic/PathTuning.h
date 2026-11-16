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
//   crossings  the coarse pass skips block crossings a 4x4 or 5x5 footprint cannot pass (cells within radius - 1 off the
//              map, or water or cliff it cannot use), instead of contraZH's clearance-map test at the two crossing cells
//   octile     among equal totals the cell further along (larger cost so far) is tried first on every open-list insert
//              (contraZH's estimate is already exact octile; it keeps first in, first out on tunnelling and coarse
//              bridge-seed inserts)
//   cost32     search costs keep 32 bits; contraZH already does, so this switch changes nothing here (it is kept so
//              the names, and the CRC, match the other trees)
//   pool       150,000 search cell records instead of contraZH's 100,000, and the search code that returns every
//              record it takes from the start (instead of only after a retail failure case)
//   cap        every search may use SEARCH_CAP_CELLS (10,000) cells, whatever else searched that frame (contraZH gives a
//              search what the frame has left, 3,000 to 10,000); a capped search's partial route and the unit's further
//              legs are contraZH's
//   clearance  a 2x2 to 5x5 footprint keeps off water and cliffs it cannot use, by our rules instead of contraZH's: water
//              and cliff rings are kept apart (an amphibious unit still keeps off cliffs), a step whose footprint would hang
//              CLEARANCE_HARD or more rings past the shore is refused unless it hangs less than the cell it leaves (or as far
//              while still near the start; never a bridge's first cell or the structure the unit goes into), one that hangs
//              past it, or past the playable edge, costs CLEARANCE_COST more; goals, straight lines and column waypoints
//              keep off the shore the same way. contraZH's locomotor turning check and its group slots keep its own test
//   headon     two units meeting head on (more than 120 degrees apart, both moving) pass after 8 frames, not 1 s
//   groupgoals a group move's and an attack-move's goals come from one flood from the click instead of contraZH's arrival
//              slots: no goal across a cliff or river from the click, the first to arrive take the deepest goals, footprints
//              apart (big ones a cell apart), each only where its own footprint got to from the click; a path that ends well
//              short of an unchanged goal asks again (3 times, 2 s apart). contraZH's column join rule is kept
//   dead       every dead unit leaves the pathfind map at once and is not put back while it slides (contraZH removes
//              only a dying unit that is not a ground mover)
//
// All of these change routes, so every player of a game, and a replay's playback, must use the same set. When any is on,
// the set goes into the pathfinder's CRC, so players with different sets see a mismatch at once. An unknown name stops
// the game at start.

#pragma once

#include "Lib/BaseType.h"

namespace PathTuning
{
	extern Bool CoarseFix;
	extern Bool CoarseFootprint;
	extern Bool Octile;
	extern Bool Cost32;
	extern Bool Pool;
	extern Bool SearchCap;
	extern Bool Clearance;
	extern Bool HeadOnPass;
	extern Bool GroupGoals;
	extern Bool DeadLeaveGrid;

	const Int CLEARANCE_MAX = 15;			///< "clearance": rings counted (more is open ground)
	const Int CLEARANCE_HARD = 2;			///< "clearance": rings past the shore that refuse a step
	const Int CLEARANCE_COST = 40;			///< "clearance": the cost of a step that hangs over (four straight steps)
	const Int POOL_CELLS = 150000;			///< "pool"
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
