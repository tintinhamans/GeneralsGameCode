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

// FILE: OnlineLoginActions.h ///////////////////////////////////////////////////
// Widget-agnostic Generals Online login flow, extracted from WOLLoginMenu.cpp's
// live GENERALS_ONLINE code path (WOLLoginMenuInit()/WOLLoginMenuShutdown()/
// NGMP_WOLLoginMenu_LoginCallback()). Under GENERALS_ONLINE that .wnd's own
// controls (email/password/nick/TOS/age fields) are all dead: WOLLoginMenuInit()
// hides the layout immediately and the only thing the player ever sees is the
// message-box sequence driven by NGMP_OnlineServices_AuthInterface
// (OnlineServices_Auth.cpp) -- "Please wait...", then "Please continue in your
// web browser" with a Cancel button, then either "Logged in!"/navigate away or
// a "Login failed." Ok box. Those message boxes already route through RmlUi via
// gogoMessageBox()/RmlUiMessageBoxHook (see GameWindowManager::gogoMessageBox()),
// so no widget work is needed for them; only the login start/stop/result-decision
// logic here is shared between GameSpyLoginProfile.wnd/GameSpyLoginQuick.wnd and
// a future RmlUi front end.
//
// Exactly one front end is active at a time: RmlUiScreenRegistry fully replaces
// .wnd loading for a registered path (GameWindowManager::winCreateLayout()), so
// WOLLoginMenuInit() never runs while an RmlUi screen is registered for these
// paths. The active front end connects to the signals below around its own
// beginLogin()/endLogin() calls and drops the connections when it leaves.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "GameNetwork/GeneralsOnline/NGMP_types.h" // ELoginResult

namespace OnlineLoginSignals
{
	// Checked first by onLoginResult(); if any listener answers TRUE the result is dropped entirely (no
	// message box, no other signal). Mirrors WOLLoginMenuInit()'s (via NGMP_WOLLoginMenu_LoginCallback())
	// "if (!buttonPushed)" guard -- a stray late callback after the player already backed out of the
	// screen must not reopen a message box or navigate again. No listener means "never already leaving".
	Predicate0 &alreadyLeaving();

	// Fired from onLoginResult() on ELoginResult::Success, after ClearGSMessageBoxes(). Mirrors the
	// .wnd's checkLogin(): the .wnd listener additionally sets its own buttonPushed/nextScreen state and
	// signals SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGIN before popping; a RmlUi screen can just push/pop
	// directly (see RmlNetworkDirectConnectScreen::onBack() for the same direct-navigation precedent --
	// RmlUi screens don't replicate the .wnd's deferred fade-then-push).
	Signal0 &succeeded();

	// Fired from the "Login failed." message box's Ok button (ELoginResult::Failed). Mirrors the .wnd's
	// inline lambda, which just calls TheShell->pop().
	Signal0 &failed();

	// Fired on ELoginResult::UserCancelled. Neither front end listens today (matching the .wnd's own
	// handling): the browser-cancel path already tears itself down before this callback ever fires (see
	// OnlineServices_Auth.cpp's DoFullLoginFlow() GSMessageBoxCancel handler).
	Signal0 &cancelled();
}

namespace OnlineLoginActions
{
	// Mirrors WOLLoginMenuInit()'s live GENERALS_ONLINE logic exactly: shows "Please wait...",
	// registers onLoginResult() as the NGMP auth callback, and starts BeginLogin(). Returns FALSE if
	// the auth interface isn't available yet, same condition WOLLoginMenuInit() used to bail out of
	// the rest of its own init (EnableLoginControls()/layout->hide()/TOS check/transition group) --
	// callers must mirror that early return on a FALSE result.
	Bool beginLogin();

	// Mirrors WOLLoginMenuShutdown()'s live GENERALS_ONLINE logic exactly: deregisters the callback.
	void endLogin();

	// Mirrors NGMP_WOLLoginMenu_LoginCallback() exactly, dispatching through the signals above instead
	// of touching GameWindow/.wnd state directly. Registered as the NGMP auth callback by
	// beginLogin(); not normally called directly by a front end.
	void onLoginResult(ELoginResult loginResult);
}
