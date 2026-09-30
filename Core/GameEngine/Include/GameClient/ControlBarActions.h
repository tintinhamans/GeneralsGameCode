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

// FILE: ControlBarActions.h /////////////////////////////////////////////////
// A control bar view's input (the RmlUi HUD over the headless .wnd; see ControlBarData.h). A view's button
// gets what the window manager would give the .wnd button under the pointer, so GadgetPushButtonInput,
// ControlBarSystem and ControlBar::processCommandUI() run unchanged: the click sound, check-like toggles,
// buttons that fire on press or on release, and the GameMessages the command sends.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

struct ControlBarButtonId;

namespace ControlBarActions
{
	// The pointer entering and leaving a button: its hilite and GBM_MOUSE_ENTERING/LEAVING.
	void enter( const ControlBarButtonId &id );
	void leave( const ControlBarButtonId &id );

	// A button pressed and released under the pointer. A disabled one plays GUIClickDisabled on release, like
	// the window manager.
	void press( const ControlBarButtonId &id, Bool right );
	void release( const ControlBarButtonId &id, Bool right );

	// A left release over the view's panels ends a drag-select that started in the world, like GameWinBlockInput
	// over the .wnd bar.
	void panelRelease();
}
