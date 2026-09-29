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

// FILE: BuddyOverlaySession.h //////////////////////////////////////////////////
// Widget-agnostic Generals Online buddy overlay lifecycle + async events:
// WOLBuddyOverlayInit's NGMP callback registration, WOLBuddyOverlayShutdown's
// deregistration, and the notification-toast text/timer half of
// showNotificationBox()/deleteNotificationBox()/HandleBuddyResponses()'s dismiss
// check -- moved out from under GameWindow the same "shared session" precedent
// OnlineGameSetupSession.h uses. Every place these paths used to write straight
// to a widget (chat listbox, roster listbox, the toast button's text) now goes
// through BuddyOverlaySignals/BuddyToastSignals, which a front end connects to, so a
// non-.wnd front end (RmlBuddyOverlayScreen) can own the same overlay without any
// GameWindow. No GameWindow type appears in this header or its .cpp.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "Common/UnicodeString.h"

#include <cstdint>

class AsciiString;

// Every write the async NGMP callbacks used to make straight to a GameWindow. Connect before
// BuddyOverlaySession::enter() and drop the connections after leave().
namespace BuddyOverlaySignals
{
	// RegisterForCallback_OnChatMessage forwarded verbatim (WOLBuddyOverlay.cpp:1080-1109).
	// Caller does what the original inline lambda did: append to the chat pane if this is the
	// currently-selected friend and clear their unread count. rosterNeedsRefresh(true, true)
	// always follows, mirroring the lambda's unconditional updateBuddyInfo(true, true) tail.
	Signal3<int64_t, int64_t, const UnicodeString &> &chatMessage(); // sourceUserID, targetUserID, text

	// RegisterForCallback_NewFriendRequest forwarded verbatim (1075-1078). GO's current
	// behaviour has no body of its own here -- the original lambda's only effect was its
	// updateBuddyInfo() tail, covered by rosterNeedsRefresh(false, false) below. No toast fires
	// from this path (the legacy request-toast is dead under GO); kept exactly as today.
	Signal0 &requestArrived();

	// Roster needs a rebuild from the social interface's cache: fires with (false, false) after
	// requestArrived (matching updateBuddyInfo()'s defaults) and with (true, true) after
	// chatMessage (matching updateBuddyInfo(true, true)). Caller re-collects rows via
	// BuddyOverlayData::collectBuddyRows() (or, for the .wnd, calls updateBuddyInfo() directly).
	Signal2<bool, bool> &rosterNeedsRefresh(); // bIsAutoRefresh, bUseCache

	// RegisterForCallback_OnNumberGlobalNotificationsChanged forwarded verbatim. Not consumed by
	// WOLBuddyOverlay.cpp today (it has no badge widget of its own -- the Communicator badge lives
	// on the game setup screen, see OnlineGameSetupSignals::communicatorCount); wired up here so a
	// future RmlUi buddy button's badge can connect without touching the NGMP interface directly.
	Signal1<int> &notificationCountChanged();
}

// The notification toast is shown from other screens while the buddy overlay itself is closed (see
// showToast() below), so its listeners are persistent rather than overlay-scoped. Exactly one
// presenter should be connected at a time: the .wnd one (WOLBuddyOverlay.cpp) connects at static
// init, and a RmlUi toast replaces it via releaseWidgetToast() before connecting its own.
namespace BuddyToastSignals
{
	Signal1<const UnicodeString &> &shown();
	Signal0 &dismissed();
}

namespace BuddyOverlaySession
{
	// Registers the NGMP push callbacks behind BuddyOverlaySignals and calls ClearGlobalNotificatations() (WOLBuddyOverlayInit,
	// 1066-1110) -- valid only while the overlay is open, same "Init is only called when the UI is
	// visible" note the .wnd carries.
	void enter();

	// WOLBuddyOverlayShutdown's DeregisterForRealtimeServiceUpdates() (1157-1161) plus clearing the
	// registered callbacks above.
	void leave();

	// ---- Notification toast -----------------------------------------------------------------
	// Text/timer half of showNotificationBox()/deleteNotificationBox()/HandleBuddyResponses()'s
	// dismiss check (969-1026, 963-966): independent of enter()/leave() because the toast is shown
	// from other screens while the buddy overlay itself is closed (e.g. RequestBuddyAdd(), reachable
	// from the lobby's shared context menu) -- same as today, where these are file-scope statics in
	// WOLBuddyOverlay.cpp, not tied to the overlay's init/shutdown. No GameWindow here: the actual
	// PopupBuddyListNotification.wnd creation/GadgetButtonSetText call stays at the .wnd call site,
	// which is why the presenter connects to BuddyToastSignals once (persistently) instead of per
	// overlay open.

	// Disconnects the .wnd toast presenter (defined in WOLBuddyOverlay.cpp). A RmlUi toast calls this
	// before connecting its own, so only one presenter is live and the RmlUi one wins, same as the
	// old single-slot sink's "last write wins".
	void releaseWidgetToast();

	// showNotificationBox(nick, message, bPlaySound)'s GENERALS_ONLINE behaviour (default
	// bPlaySound = true matches NextGenMP_defines.h's declared default): substitutes nick into
	// message, plays GUICommunicatorIncoming if bPlaySound, starts the 5000ms NOTIFICATION_EXPIRES
	// auto-dismiss timer, and fires BuddyToastSignals::shown(text).
	void showToast( const AsciiString &nick, UnicodeString message, bool bPlaySound = true );

	// deleteNotificationBox() equivalent: clears the timer and fires BuddyToastSignals::dismissed(). No-op
	// if no toast is currently showing.
	void dismissToast();

	// HandleBuddyResponses()'s tail check (963-966) -- call once per frame from anywhere; fires
	// dismissToast() once the timer has run out. No-op if no toast is currently showing.
	void tickToast();
}
