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

#include "W3DDevice/GameClient/RmlUi/RmlHostGameScreen.h"

#include "Common/AsciiString.h"
#include "GameClient/GUI/GUICallbacks/Menus/HostGameActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/HostGameData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

// No GeneralsOnline/NGMP or GameSpyOverlay.h include here on purpose: those pull winsock headers
// that conflict with <windows.h> below when both land in a GameEngineDevice translation unit (see
// RmlHostGameScreen.h / RmlOnlineLobbyScreen.cpp's identical comment). Every touch point goes
// through HostGameData/HostGameActions (plain types only), included above.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

namespace
{
	// Same conversion as RmlOnlineLobbyScreen.cpp/RmlPlayerInfoScreen.cpp.
	Rml::String unicodeToUtf8(const UnicodeString &str)
	{
		const WideChar *wide = str.str();
		if (!wide || !*wide)
			return Rml::String();

		int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
		if (len <= 0)
			return Rml::String();

		Rml::String utf8;
		utf8.resize((size_t)len - 1); // len includes the null terminator
		::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
		return utf8;
	}

	UnicodeString utf8ToUnicode(const Rml::String &utf8)
	{
		AsciiString ascii(utf8.c_str());
		UnicodeString text;
		text.translate(ascii);
		return text;
	}
}

//-------------------------------------------------------------------------------------------------
RmlHostGameScreen &RmlHostGameScreen::instance()
{
	static RmlHostGameScreen s_screen;
	return s_screen;
}

void RmlHostGameScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("hostgame");
	if (constructor)
	{
		constructor.Bind("game_name", &m_model.gameName);
		constructor.Bind("password", &m_model.password);
		constructor.Bind("use_stats", &m_model.useStats);
		constructor.Bind("limit_armies", &m_model.limitArmies);
		constructor.Bind("allow_observers", &m_model.allowObservers);

		constructor.BindEventCallback("create_game", &RmlHostGameScreen::onCreateGame, this);
		constructor.BindEventCallback("cancel", &RmlHostGameScreen::onCancel, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/HostGame.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlHostGameScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	// Same defaults PopupHostGameInit()'s GENERALS_ONLINE branch reads.
	const HostGameData::InitialState state = HostGameData::getInitialState();
	m_model.gameName = unicodeToUtf8(state.gameName);
	m_model.password = Rml::String();
	m_model.useStats = state.useStats;
	m_model.limitArmies = state.limitArmies;
	m_model.allowObservers = state.allowObservers;

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_document->Show();
}

void RmlHostGameScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlHostGameScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void RmlHostGameScreen::onCreateGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonCreateGame's GBM_SELECTED body exactly (see HostGameActions::createGame()).
	HostGameActions::createGame(utf8ToUnicode(m_model.gameName), utf8ToUnicode(m_model.password),
		m_model.allowObservers, m_model.useStats, m_model.limitArmies);
}

void RmlHostGameScreen::onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonCancel's GBM_SELECTED body exactly.
	HostGameActions::cancel();
}

void RmlHostGameScreen::back()
{
	HostGameActions::cancel();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlHostGameScreen()
{
	RmlHostGameScreen::instance().open();
}

void CloseRmlHostGameScreen()
{
	RmlHostGameScreen::instance().close();
}
