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

// FILE: RmlOnlineLoginScreen.h /////////////////////////////////////////////////
// RmlScreen for Data/UI/OnlineLogin.rml. Registered for both
// Menus/GameSpyLoginProfile.wnd and Menus/GameSpyLoginQuick.wnd (startOnline(),
// MainMenuUtils.cpp, pushes one or the other depending on
// ALLOW_NON_PROFILED_LOGIN/GameSpyUseProfiles; under GENERALS_ONLINE both are
// handled by the exact same code and behave identically, see WOLLoginMenu.cpp).
//
// Under GENERALS_ONLINE, the .wnd's own controls (email/password combo boxes,
// nickname/TOS/age-gate fields) are ALL dead: every GameWindow lookup for them
// in WOLLoginMenuInit() is inside a live-code `/* ... */` block, so those
// pointers/ids stay null/invalid, and WOLLoginMenuInit() calls
// `layout->hide(TRUE)` unconditionally -- the .wnd never actually shows
// anything of its own. What the player actually sees is the message-box
// sequence NGMP_OnlineServices_AuthInterface drives directly (OnlineServices_
// Auth.cpp): "Logging In / Please wait...", then "Logging In / Please continue
// in your web browser" with a Cancel button (BeginLogin() falls through to
// DoFullLoginFlow(), which opens the system browser via ShellExecuteA to a
// login-code URL), then either "Logged in!" + navigate to WOLWelcomeMenu.wnd,
// or a "Login failed." Ok box. All of that already routes through RmlUi via
// gogoMessageBox()/RmlUiMessageBoxHook regardless of which front end owns this
// screen, so there is no widget/status-text/tooltip parity work left to do
// here -- see OnlineLoginActions.h for the shared start/stop/result logic this
// screen drives. This screen's own document is therefore an empty, invisible
// placeholder (matching the .wnd's own `layout->hide(TRUE)`): its only job is
// to be *something* RmlUiScreenRegistry can show/hide while the login flow
// (message boxes) runs on top of it.
//
// onBack() is a no-op: the original .wnd's GWM_CHAR/KEY_ESC handler sends
// GBM_SELECTED to buttonBack, but buttonBack/buttonBackID are never assigned
// under GENERALS_ONLINE (same dead `/* ... */` block above), so Escape has no
// effect on the real screen today -- the only way off it is the "Please
// continue in your web browser" message box's own Cancel button, or the
// natural Success/Failed outcomes.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

namespace Rml { class Context; class ElementDocument; }

//-------------------------------------------------------------------------------------------------
class RmlOnlineLoginScreen : public RmlScreen
{
public:
	static RmlOnlineLoginScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // no-op; see header comment above

private:
	RmlOnlineLoginScreen() {}

	static void onLoginSucceeded(); // OnlineLoginSignals::succeeded: -> Menus/WOLWelcomeMenu.wnd
	static void onLoginFailed();    // OnlineLoginSignals::failed: matches the "Login failed." Ok box
	static Bool isAlreadyLeaving(); // OnlineLoginSignals::alreadyLeaving: drop a late result once hidden

	SignalConnections m_connections; // OnlineLoginSignals, connected while showing
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
};

// Registry entry point (see RmlUiManager::init()). Shared by both GameSpyLoginProfile.wnd and
// GameSpyLoginQuick.wnd -- see header comment above for why they behave identically.
void OpenRmlOnlineLoginScreen();
void CloseRmlOnlineLoginScreen();
