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

// FILE: OnlineGameSetupSession.h /////////////////////////////////////////////
// Widget-agnostic Generals Online (NGMP) game setup lifecycle + async events:
// WOLGameSetupMenuInit()'s NGMP callback registration, WOLGameSetupMenuShutdown()'s
// deregistration, PopBackToLobby()'s network-leave/pop, and the non-widget slice of
// WOLGameSetupMenuUpdate() (anticheat teardown, host migration, host-left, and the
// match-start countdown tick), moved out from under GameWindow -- same "shared
// actions" precedent as OnlineGameSetupActions.h. Every place these paths used to
// write straight to a widget (chat listbox, slot/option refresh, button enable
// state, the Communicator badge) now goes through the EventSink the caller passes
// in, so a future non-.wnd front end (RmlOnlineGameSetupScreen) can own the same
// room without any GameWindow. The legacy GameSpy peer-message-queue drain and
// TheNAT->update() in WOLGameSetupMenuUpdate() are NOT covered here -- they're
// classic-GameSpy plumbing entangled with TheGameSpyPeerMessageQueue/TheGameSpyInfo,
// not part of the NGMP async-event set this session models; WOLGameSetupMenu.cpp
// keeps running them directly after calling update(). No GameWindow type appears in
// this header or its .cpp -- GameEngineDevice callers (RmlOnlineGameSetupScreen)
// can't mix NGMP/winsock headers with windows.h in the same translation unit.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"
#include "GameClient/Color.h"

#include <functional>

class UnicodeString;
class NGMPGame;

namespace OnlineGameSetupSession
{
	// Plain wrappers around NGMP_OnlineServices_LobbyInterface so a GameEngineDevice caller
	// (RmlOnlineGameSetupScreen) can get at "the current lobby game"/"am I host" without ever
	// including NGMP_interfaces.h itself, which can't coexist with windows.h in the same TU.
	NGMPGame *getCurrentGame();
	Bool isHost();

	// Every write the async NGMP callbacks and the per-frame update used to make
	// straight to a GameWindow now happens through here instead -- same shape as
	// OnlineGameSetupActions::StartPressCallbacks, just covering the lifecycle/event
	// surface instead of a single button press.
	struct EventSink
	{
		// A chat/system-notice line, same text/color a GadgetListBoxAddEntryText(listboxGameSetupChat, ...)
		// call would have produced.
		std::function<void( const UnicodeString &text, Color color )> chatLine;

		// WOLDisplaySlotList()/WOLDisplayGameOptions() need a refresh: roster change, a mesh
		// connection event, or host migration all end in this.
		std::function<void()> slotsChanged;
		std::function<void()> optionsChanged;

		// Host migration made the local player the new host: re-enable Start/SelectMap/starting-cash/
		// limit-superweapons controls, relabel Start back to GUI:Start, and reset initialAcceptEnable --
		// same set WOLGameSetupMenuUpdate's host-migration branch flips.
		std::function<void()> becameHost;

		// Re-enable/disable Back and Start after a countdown starts, cancels, or a migration completes.
		std::function<void( Bool enabled )> setBackButtonEnabled;
		std::function<void( Bool enabled )> setStartButtonEnabled;

		// The Communicator/buddy button toggles on host migration (disabled for the new host until
		// the roster settles) independently of the badge-count text below.
		std::function<void( Bool enabled )> setCommunicatorButtonEnabled;

		// WOLLockSettings(): lock the host-controlled settings widgets. The session only decides
		// *when* (one second left on the match-start countdown) -- the widget lock itself stays
		// at the call site.
		std::function<void()> lockSettings;

		// Communicator/buddy badge text needs its notification count refreshed.
		std::function<void( int numNotifications )> communicatorCountChanged;
	};

	// WOLGameSetupMenuInit's host/client game-state setup: the host slot's accept flag, color,
	// player template, ping string, starting cash / superweapon restriction / old-factions-only
	// (recorded-stats rules), forcing the other slots open, the map CRC/size + adjustSlotsForMap()
	// for the host, and the map CRC/size re-check for a client. Widget-touching lines (gadget
	// enable/disable, WOLDisplaySlotList/WOLDisplayGameOptions, button text) stay at the call site.
	// Call before enter() so a widget-free screen gets the same initial state as the .wnd.
	void prepareGameState();

	// WOLGameSetupMenuInit's NGMP async-callback registration: chat, cannot-connect-to-lobby, mesh
	// connection events, player-doesn't-have-map, roster-needs-refresh, game-start-packet, and the
	// Communicator/social notification-count callback. Callback bodies are ported verbatim, routed
	// through sink instead of touching listboxGameSetupChat/buttonBuddy directly. sink is copied and
	// kept alive for the registration's lifetime (until leave()).
	void enter( const EventSink &sink );

	// WOLGameSetupMenuShutdown's dereg calls for everything enter() registered.
	void leave();

	// WOLGameSetupMenuUpdate's non-widget per-frame logic: the AnticheatPlugInterface::g_bPendingExitLobby
	// trip, host migration, host-left handling, and (under GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
	// the match-start countdown tick -- same order, same guards. Returns TRUE if the caller must stop
	// processing this frame (mirrors WOLGameSetupMenuUpdate's early `return;` on host-left), FALSE to
	// keep going (anticheat and countdown never stop the frame, matching today's behaviour).
	Bool update( const EventSink &sink );

	// Deletes TheNAT, resets TheNGMPGame and leaves the lobby; safe with no setup screen up.
	void leaveLobby();

	// leaveLobby() followed by TheShell->pop().
	void backToLobby();
}
