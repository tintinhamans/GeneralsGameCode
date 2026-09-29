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

// FILE: PopupReplayActions.h ////////////////////////////////////////////////
// Widget-agnostic save replay popup logic: naming, overwrite confirmation and copying the last
// replay into place, changing PopupReplayData. PopupReplay.cpp's .wnd callbacks and
// RmlPopupReplayScreen both call these instead of duplicating the rules.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

namespace PopupReplayActions
{
	// Start the popup. closePopup hides it and may be null.
	void open( void (*closePopup)() );

	void select( Int row );
	void setName( const UnicodeString &name );

	// Save button and Enter in the name entry: does nothing while the name is empty.
	void save();
	void back(); ///< closes the popup

	// Takes the "Replay Saved" notice down and closes the popup once it has been up long enough;
	// the view calls this every frame.
	void update();
}
