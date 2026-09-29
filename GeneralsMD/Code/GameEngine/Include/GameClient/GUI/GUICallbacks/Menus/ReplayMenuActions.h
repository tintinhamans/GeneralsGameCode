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

// FILE: ReplayMenuActions.h /////////////////////////////////////////////////
// Widget-agnostic replay menu logic: selecting, loading, deleting and copying replays and the
// message boxes around them, changing ReplayMenuData and driving the game. ReplayMenu.cpp's
// .wnd callbacks and RmlReplayMenuScreen both call these instead of duplicating the rules.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

namespace ReplayMenuActions
{
	// Start the screen. closeScreen hides it once a replay started playing and may be null.
	void open( void (*closeScreen)() );

	void select( Int row );
	void activate( Int row ); ///< double click: select and load

	// Main buttons.
	void load();
	void remove(); ///< asks first, the delete itself runs from update()
	void copy(); ///< asks first, the copy itself runs from update()
	void back(); ///< pops the shell screen

	// Runs a confirmed delete or copy. The message box is gone by then, so the view calls this
	// every frame like the .wnd's update callback.
	void update();
}
