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

// FILE: RmlLanLobbyScreen.h /////////////////////////////////////////////////
// RmlScreen for Data/UI/LanLobby.rml. Registered for Menus/LanLobbyMenu.wnd
// (MainMenuActions pushes that path). Unlike the .wnd version this never gets
// a LanLobbyMenuInit()/Shutdown()/Update() callback at all (RmlUiScreenRegistry
// routes the whole placeholder layout, see Shell.cpp's rmlUiScreenInit()), so:
//   - show() calls LanLobbyActions::enterLobby() itself (TheLAN create/reset,
//     IP selection, default name, MOTD) instead of LanLobbyMenuInit().
//   - hide() calls LanLobbyActions::leaveLobby() instead of LanLobbyMenuShutdown().
//   - update() pumps TheLAN->update() and the socket-error message box itself,
//     since LanLobbyMenuUpdate() (the .wnd's per-frame Update callback) never runs
//     for a registry-routed screen either.
// Player list/game list/chat arrive via g_lanLobbyPlayerListHook/g_lanLobbyGameListHook/
// g_lanLobbyChatHook (LANAPICallbacks.h), subscribed in show() and cleared in hide(),
// same lifetime pattern as RmlScoreScreen's g_scoreScreenChatDeliveryHook. Game rows/
// details are built through LanLobbyData (widget-agnostic, see LanLobbyData.h).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/LanLobbyData.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

class LANGameInfo;
class LANPlayer;

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlLanLobbyScreen : public RmlScreen
{
public:
	static RmlLanLobbyScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack (see LanLobbyMenu.cpp's GBM_SELECTED)
	virtual void update() override; // TheLAN->update() + socket-error box, see header comment

	// Hook targets (free functions in the .cpp forward into these); public so the free functions
	// can reach the singleton without befriending it.
	void onPlayerListChanged(LANPlayer *playerList);
	void onGameListChanged(LANGameInfo *gameList);
	void onChatLine(const Rml::String &line);

private:
	RmlLanLobbyScreen() {}

	void refreshSelectedGameDetails(); // rebuilds m_model.gameDetailSlots from the selected game
	void clearSelection();

	void onHostGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onJoinSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowActivated(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // double-click
	void onDirectConnect(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onNameChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onClearName(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Enter/blur, mirrors GEM_EDIT_DONE
	void onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Send button, mirrors ButtonEmote's actual (non-emote) behavior

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	UnicodeString m_defaultName;

	// One row of the player list (see LanLobbyData::buildPlayerRows()).
	struct PlayerRowModel
	{
		Rml::String name;
	};

	// One row of the game list (see LanLobbyData::buildGameRows()). index is this row's offset into
	// TheLAN's game list, passed back to LookupGameByListOffset()/joinGame() -- the same offset the
	// .wnd path already keys off of (GLM_SELECTED/GLM_DOUBLE_CLICKED's mData2), since a raw
	// LANGameInfo* isn't safe to keep in a data-bound row past this list's lifetime (see LanLobbyData.h).
	struct GameRowModel
	{
		int index = 0;
		Rml::String displayName;
		bool inProgress = false;
		bool isSelected = false;
	};

	// One slot of the selected game's details panel (see LanLobbyData::buildGameDetails()). Always
	// MAX_SLOTS entries, occupied/unoccupied toggled in place -- same fixed-size-array idiom as
	// RmlSkirmishSetupScreen::StartMarkerModel, not a resized array.
	struct GameDetailSlotModel
	{
		bool occupied = false;
		bool isHuman = false;
		bool isObserver = false;
		bool isRandomFaction = false;
		Rml::String label;
		Rml::String sideIconImage;
		bool showSideIcon = false;
	};

	struct Model
	{
		Rml::Vector<PlayerRowModel> players;
		Rml::Vector<GameRowModel> games;
		int selectedGameIndex = -1;

		bool detailsValid = false;
		Rml::String detailsGameName;
		Rml::String detailsMapDisplayName;
		Rml::Vector<GameDetailSlotModel> detailSlots;

		Rml::String playerName;
		Rml::String chatEntryText;
		Rml::Vector<Rml::String> chatLines;
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlLanLobbyScreen();
void CloseRmlLanLobbyScreen();
