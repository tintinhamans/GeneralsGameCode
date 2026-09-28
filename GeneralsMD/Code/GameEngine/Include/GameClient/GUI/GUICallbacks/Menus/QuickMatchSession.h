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

// FILE: QuickMatchSession.h ///////////////////////////////////////////////////
// Widget-agnostic Quick Match (Generals Online) lobby lifecycle + async events,
// extracted from WOLQuickMatchMenu.cpp -- same split as OnlineGameSetupSession.h:
// WOLQuickMatchMenuInit()'s NGMP callback registration (the 7 RegisterForMatchmaking*/
// RegisterForJoinLobbyCallback registrations plus the buddy notification-badge
// callback), WOLQuickMatchMenuShutdown()'s matching deregistration + CancelMatchmaking,
// and the non-widget slice of WOLQuickMatchMenuUpdate() (match-found lobby timeout and
// the match-start countdown tick) move out from under GameWindow. Every place these
// paths used to write straight to a widget (the status listbox, Back/Stop/Widen button
// enable state, the Buddies notification badge) now goes through the EventSink the
// caller passes in, so a future non-.wnd front end (RmlQuickMatchScreen) can own the
// same lobby without any GameWindow. No GameWindow type appears in this header or its
// .cpp -- GameEngineDevice callers can't mix NGMP/GameNetwork headers with windows.h in
// the same translation unit.
//
// Out of scope (left in WOLQuickMatchMenu.cpp, see the quick match triage note):
// RetrievePlaylists() (one-shot playlist-combo population, not a persistent async
// registration), the welcome/credits/Elo text posted once at Init, and the legacy
// GameSpy PeerResponse switch/HandleBuddyResponses() ticking, none of which are part of
// the NGMP lobby-session async-event set this session models.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"
#include "GameClient/Color.h"

#include <functional>

class UnicodeString;

namespace QuickMatchSession
{
	// Every write the async NGMP lobby callbacks and the per-frame update used to make
	// straight to a GameWindow now happens through here instead -- same shape as
	// OnlineGameSetupSession::EventSink.
	struct EventSink
	{
		// A status/chat line, same text/color a GadgetListBoxAddEntryText(quickmatchTextWindow, ...)
		// + GadgetListBoxSetItemData(..., (void*)-1, ...) pair would have produced.
		std::function<void( const UnicodeString &text, Color color )> statusLine;

		// Back/Stop/Widen enable state: flipped on match-found (Back/Stop disabled), requeue
		// (all three re-enabled) and lobby timeout (Back/Stop re-enabled).
		std::function<void( Bool enabled )> setBackButtonEnabled;
		std::function<void( Bool enabled )> setStopButtonEnabled;
		std::function<void( Bool enabled )> setWidenButtonEnabled;

		// The Buddies button disables once the match-start packet arrives. NOTE: the original
		// inline code looked this up by the wrong window name (GameSpyGameOptionsMenu.wnd's
		// ButtonCommunicator, not this screen's ButtonBuddies) and so was always a silent no-op --
		// see the commit message for this behaviour difference.
		std::function<void( Bool enabled )> setCommunicatorButtonEnabled;

		// Buddies badge text needs its notification count refreshed.
		std::function<void( int numNotifications )> communicatorCountChanged;
	};

	// WOLQuickMatchMenuInit's NGMP async-callback registration: cannot-connect-to-lobby,
	// matchmaking message/match-found/requeue/setup-progress/start-game, join-lobby (incl. the
	// debug-only mesh connection-event chat spam), and the Buddies notification-count callback.
	// Callback bodies are ported verbatim, routed through sink instead of touching
	// quickmatchTextWindow/buttonBack/buttonStop/buttonWiden/buttonBuddies directly. Also resets
	// the match-found timeout/countdown state and seeds the Buddies badge from any notifications
	// already pending, same as Init's tail. sink is copied and kept alive until leave().
	void enter( const EventSink &sink );

	// WOLQuickMatchMenuShutdown's dereg calls for everything enter() registered, plus the
	// conditional CancelMatchmaking() call (skipped while a game is starting, same guard as today).
	void leave();

	// WOLQuickMatchMenuUpdate's non-widget per-frame logic: the match-start countdown tick and the
	// match-found lobby timeout fallback. Same order, same guards.
	void update( const EventSink &sink );
}
