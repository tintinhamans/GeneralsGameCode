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

// FILE: ChallengeMenuActions.h //////////////////////////////////////////////
// Widget-agnostic Generals Challenge logic: previewing and choosing a general, starting the
// campaign and the sounds around it, changing ChallengeMenuData. ChallengeMenu.cpp's .wnd
// callbacks and RmlChallengeMenuScreen both call these instead of duplicating the rules.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

namespace ChallengeMenuActions
{
	void open(); ///< prepares the challenge game info and the data

	// close() with popImmediate FALSE is Back: the game info goes. TRUE is the game starting or the
	// shell being hidden, which keeps it for the score screen.
	void close( Bool popImmediate );

	void hover( Int general ); ///< the cursor entered a medallion: preview its bio
	void unhover( Int general ); ///< the cursor left it: back to the chosen general's bio
	void select( Int general ); ///< click on a medallion
	void play(); ///< starts the chosen general's campaign
	void back(); ///< pops the shell screen

	// Types the bio and plays the intro announcement. The view calls this every frame.
	void update();
}
