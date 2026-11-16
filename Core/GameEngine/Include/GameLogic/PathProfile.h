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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding counters (-pathProfile <file>), measurement only.
//
// Counts what the pathfinder does, per logic frame and per match, and writes it to <file>.frames.csv (one row per frame)
// and <file>.json / <file>.summary.csv (one record per match). Off unless the switch is given.
//
// It never changes the simulation: it only reads game state, its tables are fixed-size statics (it allocates no game
// memory, so object addresses and pool use stay as they are), and its files go through the C runtime only. A replay
// played with and without the switch gives the same CRC on every frame.

#pragma once

#include "Lib/BaseType.h"
#include "Common/GameType.h"

class Path;
class Object;

namespace PathProfile
{
	// The pathfinder's entry points. Coarse (hierarchical) searches run inside the others; they are counted on their own
	// and are not added to the totals of the search that ran them.
	enum SearchType
	{
		FIND,			///< findPath: a path to the goal (fine A*, after a coarse check)
		CLOSEST,		///< findClosestPath: a path to the goal or as near as possible (fine A*)
		ATTACK,			///< findAttackPath: a path to a place the target can be hit from
		SAFE,			///< findSafePath: away from repulsors
		PATCH,			///< patchPath: a repath from the current position back onto the current path
		GROUP,			///< findGroundPath: the wide ground path of a group move (coarse)
		AIRCRAFT,		///< getAircraftPath
		COARSE,			///< internal_findHierarchicalPath (zone level), nested in the others
		TYPE_COUNT
	};

	enum Result { OK, PARTIAL, FAILED, OUT_OF_POOL };

	extern Bool Enabled;	///< -pathProfile was given

	void Set_Output(const char * file);		///< CommandLine: the switch's file
	void Set_No_Time();						///< CommandLine -pathProfileNoTime: every time column 0 (runs compare byte for byte)
	const char * Output_Base();				///< the switch's file without its extension ("" without the switch)
	void Pool_Exhausted();					///< PathfindCell::allocateInfo found no free cell info

	/// One search: start and finish around the call. cellCounter is the pathfinder's m_cumulativeCellsAllocated, which
	/// cleanOpenAndClosedLists raises by the cells each search used.
	class Search
	{
	public:
		Search(SearchType type, const Int & cellCounter, const Object * unit = nullptr);	///< unit: the searching unit, if any
		void Finish(Result result, Path * path = nullptr);
		static Result Classify(Path * path, const void * goal, Bool closestOK);	///< goal: const Coord3D *, nullptr: no check
	private:
		SearchType m_type;
		ObjectID m_unit;
		const Int * m_cells;
		Int m_cellsAtStart;
		Int m_poolAtStart;
		Int64 m_start;
		Bool m_on;
		Bool m_nested;
	};

	void Queue_Request(Int outcome);	///< queueForPath: 0 queued, 1 already queued, 2 refused (queue full)
	void Queue_State(Int depth);		///< after processPathfindQueue: requests still waiting

	class Zones	///< a zone rebuild (calculateZones), timed
	{
	public:
		Zones();
		~Zones();
	private:
		Int64 m_start;
		Bool m_on;
	};

	void End_Frame(UnsignedInt frame);	///< GameLogic::update, before the frame number goes up: the frame's row, the unit checks
	void End_Match();					///< the match is over (GameLogic::clearGameData): its summary

	// ---------------------------------------------------------------------------------------------------------------
	// Groups (-pathScenario): named sets of units followed one by one, written per phase to <file>.groups.csv (a row per
	// group) and <file>.units.csv (a row per unit). Measurement only, as the rest; works with or without -pathProfile's
	// other files (the group files need -pathProfile for their name).
	enum OrderKind { ORDER_MOVE, ORDER_ATTACKMOVE, ORDER_GUARD, ORDER_STOP, ORDER_REPAIR, ORDER_ENTER, ORDER_EVACUATE,
		ORDER_GATHER, ORDER_REVERSE };

	void Groups_On();						///< a scenario runs: follow groups (from its start)
	Int Group_Index(const char * name);		///< the group's number, made on first use (-1: too many groups)
	const char * Group_Name(Int group);
	void Group_Add(Int group, const Object * unit);
	void Group_Order(Int group, Int kind, Real x, Real y, UnsignedInt frame);	///< an order given to the whole group (world coordinates)
	Real Group_Arrived(Int group);			///< the share (0..1) of the group's units that arrived since its last order
	Bool Group_Near(Int group, Real x, Real y, Real radius);	///< is a unit of the group within radius (world units) of x, y?
	void Event(const char * name, UnsignedInt frame);		///< a map change (a bridge killed or repaired): re-route times count from it
	void Phase(const char * name, UnsignedInt frame);		///< closes the current phase's rows and starts a new phase
	/// a group move gave the unit a column move: joined (the column's lane waypoints) or left out (its own route). units.csv
	/// "column": 1 joined, 0 left out, -1 no column move since its last order
	void Column_Move(const Object * unit, Bool joined);
	/// the group's goal finder gave the unit this goal (units.csv given_x, given_y, given_held)
	void Given_Goal(const Object * unit, const Coord3D * goal);
	/// the unit's move has this many waypoints (units.csv legs; 1, a direct move, unless a column move says otherwise)
	void Move_Legs(const Object * unit, Int legs);
}
