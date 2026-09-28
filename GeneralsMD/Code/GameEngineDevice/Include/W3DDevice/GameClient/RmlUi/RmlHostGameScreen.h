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

// FILE: RmlHostGameScreen.h //////////////////////////////////////////////////
// RmlScreen for Assets/UI/HostGame.rml -- the Create Game popup
// (PopupHostGame.wnd / GSOVERLAY_GAMEOPTIONS). Like RmlPlayerInfoScreen this is
// a popup, not a shell screen: it stays up OVER the lobby instead of going
// through RmlUiManager::showScreen()'s single-current-screen swap, so it loads/
// shows/hides its own document directly.
//
// GameSpyOverlay.cpp's GameSpyOpenOverlay()/GameSpyCloseOverlay()/
// GameSpyIsOverlayOpen() route GSOVERLAY_GAMEOPTIONS through
// RmlUiScreenRegistry::open/close("Menus/PopupHostGame.wnd") whenever it's
// registered (same isRegistered() gate RmlPlayerInfoScreen uses), so every
// existing caller (WOLLobbyMenu.cpp/OnlineLobbyActions::hostGame()) keeps
// working unchanged.
//
// This screen owns none of PopupHostGame.cpp's GameWindow lookups; it reads/
// writes the same shared, widget-agnostic data HostGameData.h/HostGameActions.h
// expose instead -- both headers only pull in Common/UnicodeString.h, so this
// .cpp never has to include GameNetwork/GeneralsOnline or GameSpyOverlay.h
// headers directly (those pull winsock2.h, which conflicts with <windows.h>
// in a GameEngineDevice TU; see RmlPlayerInfoScreen.cpp's comment).
//
// Not carried over: the ladder combo box / ladder password field and
// GSOVERLAY_LADDERSELECT (dead under GENERALS_ONLINE, see HostGameActions.h),
// and TextEntryGameDescription (visible in PopupHostGame.wnd but its value is
// never read by createGame(), so it does nothing under GENERALS_ONLINE either).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlHostGameScreen
{
public:
	static RmlHostGameScreen &instance();

	void open();
	void close();
	bool isVisible() const;

private:
	RmlHostGameScreen() {}

	void load(Rml::Context *context);

	void onCreateGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	struct Model
	{
		Rml::String gameName;
		Rml::String password;
		bool useStats = false;
		bool limitArmies = false;
		bool allowObservers = false;
	} m_model;
};

// Registry entry point (see RmlUiManager::init() / GameSpyOverlay.cpp).
void OpenRmlHostGameScreen();
void CloseRmlHostGameScreen();
