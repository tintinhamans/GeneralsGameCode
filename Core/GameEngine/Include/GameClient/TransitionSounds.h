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

// FILE: TransitionSounds.h /////////////////////////////////////////////////////
// The sounds of the shell's window transitions (WindowTransitions.ini) for screens that do not run
// them, e.g. the RmlUi menus: a group's sounds come from the loaded INI through
// TheTransitionHandler, and fire at the frames they would in the original.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

namespace TransitionSounds
{
	// Queues the sounds TheTransitionHandler->setGroup(group) (reversed FALSE) or ->reverse(group)
	// would play. delayFrames starts it behind a group played before, like a group set while another
	// is still running. Returns the group's length in frames, 0 if there is no such group.
	Int play(const char *group, Bool reversed = FALSE, Int delayFrames = 0);

	// Drops the group's sounds that have not fired yet, like TheTransitionHandler->remove(group).
	void stop(const char *group);

	void update(); ///< once per frame: fires the sounds that are due
	void reset();
}
