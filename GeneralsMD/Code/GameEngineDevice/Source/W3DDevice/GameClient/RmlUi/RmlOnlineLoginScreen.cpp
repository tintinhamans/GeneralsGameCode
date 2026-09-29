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

#include "W3DDevice/GameClient/RmlUi/RmlOnlineLoginScreen.h"

#include "GameClient/GUI/GUICallbacks/Menus/OnlineLoginActions.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

//-------------------------------------------------------------------------------------------------
RmlOnlineLoginScreen &RmlOnlineLoginScreen::instance()
{
	static RmlOnlineLoginScreen s_screen;
	return s_screen;
}

void RmlOnlineLoginScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;
	m_document = context->LoadDocument("UI/OnlineLogin.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLoginScreen::show()
{
	if (!m_document)
		return;

	m_document->Show();

	// NGMP: shared login flow (see OnlineLoginActions.h). beginLogin() itself shows "Please wait...",
	// then NGMP_OnlineServices_AuthInterface drives the rest of the message-box sequence.
	m_connections.disconnect();
	m_connections.add(OnlineLoginSignals::alreadyLeaving().connect(&RmlOnlineLoginScreen::isAlreadyLeaving));
	m_connections.add(OnlineLoginSignals::succeeded().connect(&RmlOnlineLoginScreen::onLoginSucceeded));
	m_connections.add(OnlineLoginSignals::failed().connect(&RmlOnlineLoginScreen::onLoginFailed));
	// cancelled has no listener, same as the .wnd front end (see OnlineLoginActions.h)
	OnlineLoginActions::beginLogin();
}

void RmlOnlineLoginScreen::hide()
{
	if (m_document)
		m_document->Hide();

	OnlineLoginActions::endLogin();
	m_connections.disconnect();
}

bool RmlOnlineLoginScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOnlineLoginScreen::onBack()
{
	// No-op: see the header comment above -- Escape has no effect on the real .wnd screen either
	// under GENERALS_ONLINE (buttonBack is never assigned).
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLoginScreen::onLoginSucceeded()
{
	// Mirrors checkLogin()'s live body: pop this screen, then go to the welcome menu.
	TheShell->pop();
	TheShell->push("Menus/WOLWelcomeMenu.wnd");
}

void RmlOnlineLoginScreen::onLoginFailed()
{
	// Mirrors NGMP_WOLLoginMenu_LoginCallback()'s "Login failed." Ok box handler exactly.
	TheShell->pop();
}

Bool RmlOnlineLoginScreen::isAlreadyLeaving()
{
	return !instance().isVisible();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOnlineLoginScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlOnlineLoginScreen::instance());
}

void CloseRmlOnlineLoginScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlOnlineLoginScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
