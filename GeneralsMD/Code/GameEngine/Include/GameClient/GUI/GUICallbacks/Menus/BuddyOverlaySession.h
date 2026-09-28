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
// through the EventSink/ToastSink the caller installs, so a future non-.wnd
// front end (RmlBuddyOverlayScreen) can own the same overlay without any
// GameWindow. No GameWindow type appears in this header or its .cpp.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

#include <cstdint>
#include <functional>

class AsciiString;

namespace BuddyOverlaySession
{
	// Every write the async NGMP callbacks used to make straight to a GameWindow now happens
	// through here instead -- same shape as OnlineGameSetupSession::EventSink.
	struct EventSink
	{
		// RegisterForCallback_OnChatMessage forwarded verbatim (WOLBuddyOverlay.cpp:1080-1109).
		// Caller does what the original inline lambda did: append to the chat pane if this is the
		// currently-selected friend and clear their unread count. rosterNeedsRefresh(true, true)
		// always follows, mirroring the lambda's unconditional updateBuddyInfo(true, true) tail.
		std::function<void( int64_t sourceUserID, int64_t targetUserID, const UnicodeString &text )> chatMessage;

		// RegisterForCallback_NewFriendRequest forwarded verbatim (1075-1078). GO's current
		// behaviour has no body of its own here -- the original lambda's only effect was its
		// updateBuddyInfo() tail, covered by rosterNeedsRefresh(false, false) below. No toast fires
		// from this path (the legacy request-toast is dead under GO); kept exactly as today.
		std::function<void()> requestArrived;

		// Roster needs a rebuild from the social interface's cache: fires with (false, false) after
		// requestArrived (matching updateBuddyInfo()'s defaults) and with (true, true) after
		// chatMessage (matching updateBuddyInfo(true, true)). Caller re-collects rows via
		// BuddyOverlayData::collectBuddyRows() (or, for the .wnd, calls updateBuddyInfo() directly).
		std::function<void( bool bIsAutoRefresh, bool bUseCache )> rosterNeedsRefresh;

		// RegisterForCallback_OnNumberGlobalNotificationsChanged forwarded verbatim. Not consumed by
		// WOLBuddyOverlay.cpp today (it has no badge widget of its own -- the Communicator badge lives
		// on the game setup screen, see OnlineGameSetupSession::EventSink::communicatorCountChanged);
		// wired up here so a future RmlUi buddy button's badge can subscribe without touching the NGMP
		// interface directly.
		std::function<void( int newCount )> notificationCountChanged;
	};

	// Registers the NGMP push callbacks above and calls ClearGlobalNotificatations() (WOLBuddyOverlayInit,
	// 1066-1110) -- valid only while the overlay is open, same "Init is only called when the UI is
	// visible" note the .wnd carries.
	void enter( const EventSink &sink );

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
	// which is why this is a persistent sink installed once (setToastSink()), not part of the
	// overlay-scoped EventSink above.
	struct ToastSink
	{
		std::function<void( const UnicodeString &text )> shown;
		std::function<void()> dismissed;
	};

	// Installs the widget-side toast callbacks. Safe to call more than once (idempotent overwrite);
	// WOLBuddyOverlayInit() installs it every time the overlay opens, but it is never cleared on
	// shutdown, so a toast triggered while the overlay is closed still reaches the .wnd's widget code.
	void setToastSink( const ToastSink &sink );

	// showNotificationBox(nick, message, bPlaySound)'s GENERALS_ONLINE behaviour (default
	// bPlaySound = true matches NextGenMP_defines.h's declared default): substitutes nick into
	// message, plays GUICommunicatorIncoming if bPlaySound, starts the 5000ms NOTIFICATION_EXPIRES
	// auto-dismiss timer, and fires ToastSink::shown(text).
	void showToast( const AsciiString &nick, UnicodeString message, bool bPlaySound = true );

	// deleteNotificationBox() equivalent: clears the timer and fires ToastSink::dismissed(). No-op
	// if no toast is currently showing.
	void dismissToast();

	// HandleBuddyResponses()'s tail check (963-966) -- call once per frame from anywhere; fires
	// dismissToast() once the timer has run out. No-op if no toast is currently showing.
	void tickToast();
}
