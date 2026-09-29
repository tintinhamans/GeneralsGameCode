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

// FILE: OnlineLobbySession.h /////////////////////////////////////////////////
// Widget-agnostic Generals Online custom-lobby network driving, extracted from
// WOLLobbyMenu.cpp -- same split as OnlineGameSetupSession.h/QuickMatchSession.h:
// WOLLobbyMenuInit()'s lobby leave/host-join reset, NGMP callback registration and
// room list fetch + first room join, WOLLobbyMenuShutdown()'s deregistration, and the
// non-widget slice of WOLLobbyMenuUpdate() (pending full teardown exit, lobby-list-dirty
// game list poll). Both WOLLobbyMenu.cpp and RmlOnlineLobbyScreen call it, so the two front
// ends behave identically. Every place these paths used to write straight to a widget goes
// through OnlineLobbySignals (OnlineLobbyData.h), which a front end connects to before
// enter(). No GameWindow type appears here; GameEngineDevice callers can't mix NGMP headers
// with windows.h in the same translation unit.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"

namespace OnlineLobbySession
{
	// Leaves any current lobby, clears the host/join attempt flag and the game/player refresh
	// gates, resets TheNGMPGame, registers the NGMP push callbacks (forwarded into
	// OnlineLobbySignals), then fetches the room list and joins the first room. The result is
	// delivered through OnlineLobbySignals::roomListResult(); the join through roomChanged().
	// Async results that arrive after leave()/markLeaving() are dropped.
	void enter();

	// WOLLobbyMenuShutdown()'s NGMP callback deregistration; also drops any in-flight room list
	// result.
	void leave();

	// The screen is on its way out (back pressed, hosting/joining a game): stop polling and drop
	// in-flight async results. enter() clears it again.
	void markLeaving();

	// WOLLobbyMenuUpdate()'s pending-full-teardown exit. Returns TRUE if a teardown is pending
	// (the caller must skip the rest of its update, exactly as the .wnd always has); exits the
	// screen and requests the engine teardown unless a host/join is in flight.
	Bool handlePendingTeardown();

	// The lobby-list-dirty poll: refreshes the game list (through the same 4 s gate the .wnd
	// uses) whenever the service reports the list stale and we are neither in a lobby nor
	// joining one.
	void update();
}
