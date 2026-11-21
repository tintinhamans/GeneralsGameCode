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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding test runner: -pathScenario <file> and -pathBench <file>.
// Inert without the switches.
//
// -pathScenario: starts a skirmish from the file (map, seed, players), then on fixed logic frames, or when a condition is
// met, spawns units sim-side, gives orders as the owning player's messages through the command list (the same path a
// player's click takes, recorded in the replay), kills or repairs bridges, starts measurement phases and ends the run.
// PathProfile counts everything (groups included); with -headless the logic runs unthrottled.
//
// -pathBench: starts the map with one player, then runs findPath for a list of start/goal pairs and templates (moving
// nothing) and an exact A* for each pair as the reference, and writes <file>.bench.csv.
//
// The file format is described at the top of PathScenario.cpp. The same file on the same build gives the same run.

#pragma once

#include "Lib/BaseType.h"

namespace PathScenario
{
	void Set_Scenario(const char * file);	///< CommandLine -pathScenario
	void Set_Bench(const char * file);		///< CommandLine -pathBench
	Bool Active();							///< either switch was given
	Bool Start(Bool viaCommandList);		///< read the file, set up the skirmish, queue the new game (FALSE: failed)
	Int Run_Headless();						///< GameMain with -headless: start and run the logic to the end
	void Frame(UnsignedInt frame);			///< GameLogic::update, after the scripts: this frame's commands
	Bool Done();
}
