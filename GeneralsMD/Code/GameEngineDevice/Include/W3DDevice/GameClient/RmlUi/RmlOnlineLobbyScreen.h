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
// hide() do the engine-state setup/teardown themselves through
// OnlineLobbySession::enter()/leave() -- the same NGMP callback registration,
// room list fetch/first room join and pending-teardown exit WOLLobbyMenu.cpp
// uses, so both front ends behave identically. Its results arrive through
// OnlineLobbySignals, connected in show() and dropped in hide().
//
// Game list rows are pushed from LobbyUtils.cpp's RefreshGameListBox() async
// SearchForLobbies() completion via OnlineLobbySignals::gameList (connected in
// show(), dropped in hide()) so this screen and the .wnd listbox share the same
// one network round trip. Player rows are polled each update() from
// OnlineLobbyData::collectPlayerRows() (cheap; same roster-signature-diff idiom
// as WOLLobbyMenu.cpp's PopulateLobbyPlayerListbox(), see .cpp).
//
// The player-row right-click menu (GLM_RIGHT_CLICKED's RCLocalPlayerMenu.wnd/
// RCNoProfileMenu.wnd/RCBuddiesMenu.wnd/RCNonBuddiesMenu.wnd, handled by
// WOLBuddyOverlayRCMenuSystem()) is reimplemented as a generic RmlUi popup (see
// common.rcss's .context-menu) driven by OnlineLobbyData::buildPlayerContextMenu()
// + OnlineLobbyActions::performPlayerMenuAction() -- see onPlayerRowMouseDown()/
// onPlayerMenuItemClicked() below.
//
// Not converted: the per-row async rank-icon stat lookups WOLLobbyMenu.cpp's
// listbox viewport optimization drives -- stays .wnd-only pending its own
// conversion pass (see ResolveRankIconForUser()). GSOVERLAY_GAMEOPTIONS/
// GSOVERLAY_GAMEPASSWORD/GSOVERLAY_BUDDY still open as the same .wnd overlays
// either way (GSOVERLAY_PLAYERINFO, opened by the new menu's Stats item, already
// routes through RmlPlayerInfoScreen once registered -- see RmlUiScreenRegistry).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "GameClient/Color.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"
#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"
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
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack (OnlineLobbyActions::back())
	virtual void update() override; // player-list poll + NGMP teardown check, see .cpp

	// OnlineLobbySignals targets, connected in show() ---------------------------------------
	void onGameListChanged(const std::vector<OnlineLobbyData::GameRow> &rows); // OnlineLobbySignals::gameList
	void onChatLine(const UnicodeString &text, Color color); // OnlineLobbySignals::chatLine
	void onRoomChanged(int roomIndex, bool effectiveRoomChanged); // OnlineLobbySignals::roomChanged
	void onLobbyJoinResult(int result); // OnlineLobbySignals::joinResult (EJoinLobbyResult)
	void refreshPlayersFromSignal(); // OnlineLobbySignals::rosterRefresh; forwards to refreshPlayers(false)
	void onRoomListResult(bool success); // OnlineLobbySignals::roomListResult

private:
	RmlOnlineLobbyScreen() : m_gameRows(m_model.games), m_playerRows(m_model.players), m_chatRows(m_model.chatLines),
		m_playerMenuItemRows(m_model.playerMenuItems), m_detailMemberRows(m_model.detailMembers) {}

	void refreshRoomCombo();
	void refreshFilterHighlight();
	void refreshSortHighlight();
	void refreshPlayers(bool force);
	void refreshRankIcons(); // badges from the stats cache, and a stats fetch for the rows in view
	void pushRoomToDock(); // the roster is the social dock's Lobby tab
	void refreshDetail(); // details panel: the hovered game, else the selected one

	void onHostGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onJoinSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameRowActivated(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // double-click
	void onGameRowHovered(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // (index), -1 when the pointer leaves
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
	void onPlayerRowMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors GLM_RIGHT_CLICKED
	void onPlayerMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors WOLBuddyOverlayRCMenuSystem's GBM_SELECTED
	void onPlayerMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // click-away close, mirrors winSetLoneWindow()'s auto-close

	void closePlayerMenu();
	void clampPlayerMenu(); // called from update() while visible; see .cpp

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnections m_connections; // OnlineLobbySignals, connected while showing
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status
	std::string m_rosterSignature; // see refreshPlayers()
	static const unsigned long PLAYER_LIST_POLL_MS = 4000; // WOLLobbyMenu.cpp's playerListRefreshInterval
	unsigned long m_lastRosterPoll = 0; // GetTickCount() of the last update() poll

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

	// One human player of the details panel's game.
	struct DetailMemberModel
	{
		Rml::String name;
		bool isHost = false;
		bool used = true;
	};

	// One row of the player list (see OnlineLobbyData::PlayerRow). index feeds
	// player_row_mousedown(player.index), which looks the row back up in m_rawPlayerRows for its
	// userID/displayName (not worth exposing those to RmlUi, only the context menu needs them).
	struct PlayerRowModel
	{
		int index = 0;
		Rml::String name;
		bool isAdmin = false;
		bool isFriend = false;
		bool isIgnored = false;
		bool isSelf = false;
		Rml::String rankImage; // small rank badge mapped image, empty when unranked/unknown
		bool used = true;
	};

	// One entry of the player-row right-click menu (see OnlineLobbyData::buildPlayerContextMenu()).
	struct PlayerMenuItemModel
	{
		Rml::String label;
		int action = 0; // OnlineLobbyData::PlayerMenuAction
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
		// text split for display (see splitChatLine()): the "[hh:mm] " prefix, the "name: " sender, the rest.
		Rml::String time;
		Rml::String name;
		Rml::String body;
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
		int gameCount = 0; // live rows, for the empty state
		int hoverGameIndex = -1;

		// Details panel (refreshDetail()).
		bool detailVisible = false;
		Rml::String detailName;
		Rml::String detailMap;
		Rml::String detailMapPath;
		Rml::String detailPlayers;
		Rml::String detailPing;
		Rml::String detailCash;
		bool detailPingOk = false;
		bool detailPingBad = false;
		bool detailPassword = false;
		bool detailObservers = false;
		bool detailStats = false;
		bool detailLimitSuperweapons = false;
		Rml::Vector<DetailMemberModel> detailMembers;

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

		bool playerMenuVisible = false;
		Rml::String playerMenuXStyle = "0px";
		Rml::String playerMenuYStyle = "0px";
		Rml::Vector<PlayerMenuItemModel> playerMenuItems;
	} m_model;

	RmlGrowOnlyList<GameRowModel> m_gameRows;
	RmlGrowOnlyList<PlayerRowModel> m_playerRows;
	RmlGrowOnlyList<ChatLineModel> m_chatRows;
	RmlGrowOnlyList<PlayerMenuItemModel> m_playerMenuItemRows;
	RmlGrowOnlyList<DetailMemberModel> m_detailMemberRows;
	std::vector<OnlineLobbyData::GameRow> m_rawGameRows; // last game list, same order/index as m_model.games

	// Snapshot of the last refreshPlayers() roster, same order/index as m_model.players, so
	// onPlayerRowMouseDown()/onPlayerMenuItemClicked() can reach a row's userID/displayName without
	// exposing those to RmlUi.
	std::vector<OnlineLobbyData::PlayerRow> m_rawPlayerRows;
	OnlineLobbyData::PlayerRow m_playerMenuTarget; // row the open context menu applies to
	float m_playerMenuRawX = 0.0f; // unclamped cursor position; clampPlayerMenu() re-derives the
	float m_playerMenuRawY = 0.0f; // clamped style strings from these once the menu's real size is known
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlOnlineLobbyScreen();
void CloseRmlOnlineLobbyScreen();
