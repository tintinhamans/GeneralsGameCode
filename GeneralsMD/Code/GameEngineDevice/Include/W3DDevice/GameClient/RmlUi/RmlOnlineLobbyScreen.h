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

// FILE: RmlOnlineLobbyScreen.h /////////////////////////////////////////////////
// RmlScreen for Assets/UI/OnlineLobby.rml. Registered for Menus/WOLCustomLobby.wnd
// (OnlineWelcomeActions's Custom Match button pushes that path). Like
// RmlLanLobbyScreen this never gets a WOLLobbyMenuInit()/Update()/Shutdown()
// callback (RmlUiScreenRegistry routes the whole placeholder layout), so show()/
// hide() do the engine-state setup/teardown themselves and independently
// register the NGMP push callbacks WOLLobbyMenuInit() would otherwise install
// (RegisterForChatCallback/RegisterForRosterNeedsRefreshCallback/
// RegisterForRoomChangedCallback/RegisterForCreateLobbyCallback/
// RegisterForJoinLobbyCallback -- all single-slot std::function members on the
// NGMP interfaces, so registering our own here simply preempts WOLLobbyMenuInit's
// registrations for as long as this screen -- not the .wnd one -- is open).
//
// Game list rows are pushed from LobbyUtils.cpp's RefreshGameListBox() async
// SearchForLobbies() completion via g_onlineLobbyGameListHook (installed in
// show(), cleared in hide()) so this screen and the .wnd listbox share the same
// one network round trip. Player rows are polled each update() from
// OnlineLobbyData::collectPlayerRows() (cheap; same roster-signature-diff idiom
// as WOLLobbyMenu.cpp's PopulateLobbyPlayerListbox(), see .cpp).
//
// Not converted: GLM_RIGHT_CLICKED's moderation right-click menu (RC*Menu.wnd)
// and the per-row async rank-icon stat lookups WOLLobbyMenu.cpp's listbox
// viewport optimization drives -- both stay .wnd-only pending their own
// conversion pass (see WOLLobbyMenu.cpp's GLM_RIGHT_CLICKED case and
// ResolveRankIconForUser()). GSOVERLAY_GAMEOPTIONS/GSOVERLAY_GAMEPASSWORD/
// GSOVERLAY_BUDDY still open as the same .wnd overlays either way.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/Color.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlOnlineLobbyScreen : public RmlScreen
{
public:
	static RmlOnlineLobbyScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack (OnlineLobbyActions::back())
	virtual void update() override; // player-list poll + NGMP teardown check, see .cpp

	// Hook targets -------------------------------------------------------------------------
	void onGameListChanged(const std::vector<OnlineLobbyData::GameRow> &rows); // g_onlineLobbyGameListHook target
	void onChatLine(const UnicodeString &text, Color color); // g_onlineLobbyChatHook target
	void onRoomChanged(int roomIndex, bool effectiveRoomChanged); // g_onlineLobbyRoomChangedHook target
	void onLobbyJoinResult(int result); // g_onlineLobbyJoinResultHook target (EJoinLobbyResult)
	void refreshPlayersFromHook(); // g_onlineLobbyRosterRefreshHook target; forwards to refreshPlayers(false)

private:
	RmlOnlineLobbyScreen() : m_gameRows(m_model.games), m_playerRows(m_model.players), m_chatRows(m_model.chatLines) {}

	void refreshRoomCombo();
	void refreshFilterHighlight();
	void refreshSortHighlight();
	void refreshPlayers(bool force);

	void onHostGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onJoinSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowActivated(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // double-click
	void onRefresh(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBuddyOverlay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onJoinRoom(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSetFilter(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSortAge(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSortMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSortBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Enter/blur, mirrors GEM_EDIT_DONE
	void onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Send button, mirrors ButtonEmote

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	std::string m_rosterSignature; // see refreshPlayers()

	// One row of the game list (see OnlineLobbyData::GameRow, which this mirrors 1:1 for RmlUi binding).
	struct GameRowModel
	{
		int index = 0;
		int64_t lobbyID = -1;
		Rml::String displayName;
		Rml::String mapDisplayName;
		Rml::String playersText;
		bool playersAlmostFull = false;
		bool playersFull = false;
		bool hasPassword = false;
		bool allowObservers = false;
		bool trackStats = false;
		bool pingOk = false;
		bool pingBad = false;
		bool hasBuddy = false;
		bool crcMismatch = false;
		bool isSelected = false;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h
	};

	// One row of the player list (see OnlineLobbyData::PlayerRow).
	struct PlayerRowModel
	{
		Rml::String name;
		bool isAdmin = false;
		bool isFriend = false;
		bool isIgnored = false;
		bool isSelf = false;
		bool used = true;
	};

	// One line of chat (see OnlineLobbyActions/RegisterForChatCallback). color is a "rgba(r,g,b,a)"
	// CSS string bound via data-style-color, preserving the original per-message Color exactly
	// (unlike RmlLanLobbyScreen's plain-string chat_lines, this lobby's chat has always been colored --
	// system notices, rate-limit warnings, moderation text, room-join messages, player chat).
	struct ChatLineModel
	{
		Rml::String text;
		Rml::String color = "rgba(255,255,255,255)"; // never left empty, see colorToCss()
		bool used = true;
	};

	// One entry of the group-room combo (see PopulateLobbyFilterComboBox()'s room section; the filter
	// section below it is static markup in the .rml, highlighted via m_model.filterIsAll etc).
	struct RoomRowModel
	{
		int index = 0;
		Rml::String label;
		bool isCurrent = false;
		bool used = true;
	};

	struct Model
	{
		Rml::Vector<GameRowModel> games;
		int selectedGameIndex = -1;

		Rml::Vector<PlayerRowModel> players;

		Rml::Vector<ChatLineModel> chatLines;
		Rml::String chatEntryText;

		Rml::Vector<RoomRowModel> rooms;
		int currentRoomIndex = -1; // two-way bound to the room <select>, see onJoinRoom()

		bool filterIsAll = true, filterIs1v1 = false, filterIsTeam = false,
			filterIsFfa = false, filterIsAod = false, filterIsBuddies = false;

		bool sortByAge = false, sortAgeDescending = false;
		bool sortByMap = true, sortMapDescending = false;
		bool sortBuddiesFirst = true;
	} m_model;

	RmlGrowOnlyList<GameRowModel> m_gameRows;
	RmlGrowOnlyList<PlayerRowModel> m_playerRows;
	RmlGrowOnlyList<ChatLineModel> m_chatRows;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlOnlineLobbyScreen();
void CloseRmlOnlineLobbyScreen();
