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

// FILE: OnlineSessionExit.h //////////////////////////////////////////////////
// The pending-full-teardown exit shared by the Generals Online lobby-stage screens that
// WOLQuickMatchMenuUpdate()/WOLGameSetupMenuUpdate() serve as .wnd and that
// RmlQuickMatchScreen/RmlOnlineGameSetupScreen replace: once the online services report a full
// teardown, leave as soon as no match is in progress and no shell/transition animation is
// running. (The custom lobby has its own variant, OnlineLobbySession::handlePendingTeardown().)
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"

namespace OnlineSessionExit
{
	// TRUE once a full teardown is pending and it is safe to leave right now.
	Bool isTeardownReady();

	// What a screen does after shutting itself down when isTeardownReady(): requests the engine
	// teardown, then pops the screen -- same order as the .wnd updates.
	void tearDownAndPop();
}
