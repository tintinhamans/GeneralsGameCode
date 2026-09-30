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

#include "W3DDevice/GameClient/RmlUi/RmlJoinGameScreen.h"

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/GUI/GUICallbacks/Menus/JoinGameActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/JoinGameData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

// No GeneralsOnline/NGMP or GameSpyOverlay.h include here on purpose: those pull winsock headers
// that conflict with <windows.h> below when both land in a GameEngineDevice translation unit (see
// RmlJoinGameScreen.h / RmlOnlineLobbyScreen.cpp's identical comment). Every touch point goes
// through JoinGameData/JoinGameActions (plain types only), included above.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
RmlJoinGameScreen &RmlJoinGameScreen::instance()
{
	static RmlJoinGameScreen s_screen;
	return s_screen;
}

void RmlJoinGameScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("joingame");
	if (constructor)
	{
		constructor.Bind("lobby_name", &m_model.lobbyName);
		constructor.Bind("password", &m_model.password);

		constructor.BindEventCallback("password_committed", &RmlJoinGameScreen::onPasswordCommitted, this);
		constructor.BindEventCallback("cancel", &RmlJoinGameScreen::onCancel, this);
		constructor.BindEventCallback("join", &RmlJoinGameScreen::onJoin, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/JoinGame.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlJoinGameScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	// Same lobby name PopupJoinGameInit() displays.
	m_model.lobbyName = unicodeToUtf8(JoinGameData::getLobbyName());
	m_model.password = Rml::String();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_document->Show();
}

void RmlJoinGameScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlJoinGameScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void RmlJoinGameScreen::onPasswordCommitted(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	// Change fires on every edit; only Enter (linebreak) commits, like the .wnd's GEM_EDIT_DONE.
	if (!ev.GetParameter<bool>("linebreak", false))
		return;

	commitPassword();
}

// The Join button: the same as Enter in the field.
void RmlJoinGameScreen::onJoin(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	commitPassword();
}

void RmlJoinGameScreen::commitPassword()
{
	// Mirrors TextEntryGamePassword's GEM_EDIT_DONE body exactly: trims, ignores an empty commit
	// (same as the .wnd, which never calls joinGame() for an empty trimmed entry), and clears the
	// field back out (see JoinGameActions::joinGame()'s DEBUG_LOG + GameSpyCloseOverlay).
	UnicodeString password = utf8ToUnicode(m_model.password);
	password.trim();
	m_model.password = Rml::String();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("password");

	if (!password.isEmpty())
		JoinGameActions::joinGame(password);
}

void RmlJoinGameScreen::onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonCancel's GBM_SELECTED body exactly.
	JoinGameActions::cancel();
}

void RmlJoinGameScreen::back()
{
	JoinGameActions::cancel();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlJoinGameScreen()
{
	RmlJoinGameScreen::instance().open();
}

void CloseRmlJoinGameScreen()
{
	RmlJoinGameScreen::instance().close();
}
