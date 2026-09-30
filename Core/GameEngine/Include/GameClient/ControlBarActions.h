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

class UnicodeString;

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

	// The beacon's text typed in the view and entered: into EditBeaconText and on to the bar (GEM_EDIT_DONE),
	// which sends it with MSG_SET_BEACON_TEXT like the .wnd entry's Enter.
	void setBeaconText( const UnicodeString &text );
	// Escape in the beacon's text: deselects the beacon, like BeaconWindowInput.
	void leaveBeacon();

	// The pointer resting on a button or readout, every frame, like the window manager calling the .wnd window's
	// tooltip callback while the mouse is over it: the build tooltip comes up after its delay (ControlBarData).
	void hover( const ControlBarButtonId &id );

	// The pointer entering the promotions panel: GeneralsExpPointsInput drops any building placement.
	void enterSciencePanel();

	// The radar: the view keeps the headless LeftHUD window on its own radar box (the radar logic converts
	// pixels through that window), has the engine draw it there (W3DLeftHUDDraw: the radar or the radar movie),
	// and gives it the pointer (LeftHUDInput: cursors, look at, move and attack-move orders, targeted powers).
	void placeRadar( Int x, Int y, Int width, Int height );
	void drawRadar();
	void radarInput( UnsignedInt message ); ///< GWM_MOUSE_POS, GWM_LEFT_DOWN, ... at the pointer
	// A look button held while dragging keeps looking, like its press (no selection, or the right button,
	// the left with the alternate mouse).
	void radarDrag( Bool right );
}
