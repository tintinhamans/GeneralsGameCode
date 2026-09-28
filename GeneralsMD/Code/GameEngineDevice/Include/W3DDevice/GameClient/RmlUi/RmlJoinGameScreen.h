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

// FILE: RmlJoinGameScreen.h //////////////////////////////////////////////////
// RmlScreen for Assets/UI/JoinGame.rml -- the password-prompt popup shown when
// joining a passworded lobby (PopupJoinGame.wnd / GSOVERLAY_GAMEPASSWORD). Same
// popup precedent as RmlHostGameScreen/RmlPlayerInfoScreen: loads/shows/hides
// its own document directly instead of going through RmlUiManager::showScreen().
//
// GameSpyOverlay.cpp routes GSOVERLAY_GAMEPASSWORD through
// RmlUiScreenRegistry::open/close("Menus/PopupJoinGame.wnd") whenever it's
// registered, so every existing caller keeps working unchanged.
//
// Reads/writes JoinGameData.h/JoinGameActions.h's shared, widget-agnostic data
// only -- same winsock/<windows.h> conflict avoidance as RmlHostGameScreen.h.
// The password field commits the same way the .wnd's GEM_EDIT_DONE did: on
// change, not via a separate button (PopupJoinGame.wnd has none).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlJoinGameScreen
{
public:
	static RmlJoinGameScreen &instance();

	void open();
	void close();
	bool isVisible() const;

private:
	RmlJoinGameScreen() {}

	void load(Rml::Context *context);

	void onPasswordCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	struct Model
	{
		Rml::String lobbyName;
		Rml::String password;
	} m_model;
};

// Registry entry point (see RmlUiManager::init() / GameSpyOverlay.cpp).
void OpenRmlJoinGameScreen();
void CloseRmlJoinGameScreen();
