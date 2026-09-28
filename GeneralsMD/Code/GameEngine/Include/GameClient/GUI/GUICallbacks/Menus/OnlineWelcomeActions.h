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

// FILE: OnlineWelcomeActions.h ///////////////////////////////////////////////////
// Widget-agnostic button/lifecycle actions extracted from WOLWelcomeMenu.cpp's live
// GENERALS_ONLINE code path (WOLWelcomeMenuSystem()'s GBM_SELECTED cases and
// WOLWelcomeMenuUpdate()'s pending-full-teardown branch). Each action here does the
// same NGMP/overlay/shell call the original did; each still leaves navigation
// (TheShell->pop()/push()) to the caller, same split as OnlineLoginActions.h and
// RmlOnlineLoginScreen.h -- the .wnd path keeps its own deferred fade-then-push via
// nextScreen/buttonPushed, while a future RmlUi front end can push/pop directly.
///////////////////////////////////////////////////////////////////////////////

#pragma once

namespace OnlineWelcomeActions
{
	// Mirrors buttonQuickMatchID's resolution-lock gate exactly, including showing the
	// "GUI:QuickMatch800x600" Ok box itself when blocked. Returns TRUE if quick match may proceed
	// (caller still does its own nextScreen/pop or push), FALSE if blocked.
	Bool canStartQuickMatch();

	// Mirrors buttonBackID's live GENERALS_ONLINE body exactly (SetPendingFullTeardown +
	// DEBUG_LOG). Caller still does its own TheShell->pop() afterward, unchanged from the .wnd path.
	void requestLogout();

	// Mirrors buttonMyInfoID's handler exactly (SetLookAtPlayer + GameSpyToggleOverlay(GSOVERLAY_PLAYERINFO)).
	void openMyInfo();

	// Mirrors buttonOptionsID's handler exactly (GameSpyOpenOverlay(GSOVERLAY_OPTIONS)).
	void openOptions();

	// Mirrors buttonBuddiesID's handler exactly (GameSpyToggleOverlay(GSOVERLAY_BUDDY)).
	void toggleBuddiesOverlay();

	// Mirrors WOLWelcomeMenuUpdate()'s pending-full-teardown check exactly: TRUE if a teardown was
	// pending, in which case it is consumed as a side effect. Caller must then pop itself and call
	// TearDownGeneralsOnline() afterward, in that order, matching the .wnd's own
	// "buttonPushed=TRUE; TheShell->pop(); TearDownGeneralsOnline();" sequence exactly.
	Bool consumePendingFullTeardown();
}
