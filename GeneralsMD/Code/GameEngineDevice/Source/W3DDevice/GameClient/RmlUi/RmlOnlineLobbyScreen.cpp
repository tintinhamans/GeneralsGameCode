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

#include "W3DDevice/GameClient/RmlUi/RmlOnlineLobbyScreen.h"

#include "Common/AsciiString.h"
#include "Common/Debug.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

// No GeneralsOnline/NGMP or LobbyUtils.h include here on purpose: those pull winsock headers that
// conflict with <windows.h> below when both land in a GameEngineDevice translation unit (see design
// notes / RmlOnlineLobbyScreen.h). Every NGMP touch point goes through OnlineLobbyActions (plain
// types only), included above.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same conversion RmlLanLobbyScreen.cpp/etc. each keep as a private helper.
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1);
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

static UnicodeString utf8ToUnicode(const Rml::String &utf8)
{
	AsciiString ascii(utf8.c_str());
	UnicodeString text;
	text.translate(ascii);
	return text;
}

static Rml::String colorToCss(Color color)
{
	// ARGB packing, see Color.h's GameMakeColor(); RmlUi's rgba() takes 0-255 ints for every
	// channel including alpha (see e.g. common.rcss's "rgba(2, 2, 2, 140)"), not a 0-1 float.
	const int a = (color >> 24) & 0xFF;
	const int r = (color >> 16) & 0xFF;
	const int g = (color >> 8) & 0xFF;
	const int b = color & 0xFF;
	char buf[48];
	snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%d)", r, g, b, a);
	return Rml::String(buf);
}

//-------------------------------------------------------------------------------------------------
RmlOnlineLobbyScreen &RmlOnlineLobbyScreen::instance()
{
	static RmlOnlineLobbyScreen s_screen;
	return s_screen;
}

void RmlOnlineLobbyScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("onlinelobby");
	if (constructor)
	{
		Rml::StructHandle<GameRowModel> gameHandle = constructor.RegisterStruct<GameRowModel>();
		if (gameHandle)
		{
			gameHandle.RegisterMember("index", &GameRowModel::index);
			gameHandle.RegisterMember("display_name", &GameRowModel::displayName);
			gameHandle.RegisterMember("map_display_name", &GameRowModel::mapDisplayName);
			gameHandle.RegisterMember("players_text", &GameRowModel::playersText);
			gameHandle.RegisterMember("players_almost_full", &GameRowModel::playersAlmostFull);
			gameHandle.RegisterMember("players_full", &GameRowModel::playersFull);
			gameHandle.RegisterMember("has_password", &GameRowModel::hasPassword);
			gameHandle.RegisterMember("allow_observers", &GameRowModel::allowObservers);
			gameHandle.RegisterMember("track_stats", &GameRowModel::trackStats);
			gameHandle.RegisterMember("ping_ok", &GameRowModel::pingOk);
			gameHandle.RegisterMember("ping_bad", &GameRowModel::pingBad);
			gameHandle.RegisterMember("has_buddy", &GameRowModel::hasBuddy);
			gameHandle.RegisterMember("crc_mismatch", &GameRowModel::crcMismatch);
			gameHandle.RegisterMember("is_selected", &GameRowModel::isSelected);
			gameHandle.RegisterMember("used", &GameRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<GameRowModel>>();

		Rml::StructHandle<PlayerRowModel> playerHandle = constructor.RegisterStruct<PlayerRowModel>();
		if (playerHandle)
		{
			playerHandle.RegisterMember("name", &PlayerRowModel::name);
			playerHandle.RegisterMember("is_admin", &PlayerRowModel::isAdmin);
			playerHandle.RegisterMember("is_friend", &PlayerRowModel::isFriend);
			playerHandle.RegisterMember("is_ignored", &PlayerRowModel::isIgnored);
			playerHandle.RegisterMember("is_self", &PlayerRowModel::isSelf);
			playerHandle.RegisterMember("used", &PlayerRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<PlayerRowModel>>();

		Rml::StructHandle<ChatLineModel> chatHandle = constructor.RegisterStruct<ChatLineModel>();
		if (chatHandle)
		{
			chatHandle.RegisterMember("text", &ChatLineModel::text);
			chatHandle.RegisterMember("color", &ChatLineModel::color);
			chatHandle.RegisterMember("used", &ChatLineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<ChatLineModel>>();

		Rml::StructHandle<RoomRowModel> roomHandle = constructor.RegisterStruct<RoomRowModel>();
		if (roomHandle)
		{
			roomHandle.RegisterMember("index", &RoomRowModel::index);
			roomHandle.RegisterMember("label", &RoomRowModel::label);
			roomHandle.RegisterMember("is_current", &RoomRowModel::isCurrent);
			roomHandle.RegisterMember("used", &RoomRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<RoomRowModel>>();

		constructor.Bind("games", &m_model.games);
		constructor.Bind("selected_game_index", &m_model.selectedGameIndex);
		constructor.Bind("players", &m_model.players);
		constructor.Bind("chat_lines", &m_model.chatLines);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);
		constructor.Bind("rooms", &m_model.rooms);
		constructor.Bind("current_room_index", &m_model.currentRoomIndex);
		constructor.Bind("filter_is_all", &m_model.filterIsAll);
		constructor.Bind("filter_is_1v1", &m_model.filterIs1v1);
		constructor.Bind("filter_is_team", &m_model.filterIsTeam);
		constructor.Bind("filter_is_ffa", &m_model.filterIsFfa);
		constructor.Bind("filter_is_aod", &m_model.filterIsAod);
		constructor.Bind("filter_is_buddies", &m_model.filterIsBuddies);
		constructor.Bind("sort_by_age", &m_model.sortByAge);
		constructor.Bind("sort_age_descending", &m_model.sortAgeDescending);
		constructor.Bind("sort_by_map", &m_model.sortByMap);
		constructor.Bind("sort_map_descending", &m_model.sortMapDescending);
		constructor.Bind("sort_buddies_first", &m_model.sortBuddiesFirst);

		constructor.BindEventCallback("host_game", &RmlOnlineLobbyScreen::onHostGame, this);
		constructor.BindEventCallback("join_selected", &RmlOnlineLobbyScreen::onJoinSelected, this);
		constructor.BindEventCallback("game_row_clicked", &RmlOnlineLobbyScreen::onGameRowClicked, this);
		constructor.BindEventCallback("game_row_activated", &RmlOnlineLobbyScreen::onGameRowActivated, this);
		constructor.BindEventCallback("refresh", &RmlOnlineLobbyScreen::onRefresh, this);
		constructor.BindEventCallback("back", &RmlOnlineLobbyScreen::onBackPressed, this);
		constructor.BindEventCallback("buddy_overlay", &RmlOnlineLobbyScreen::onBuddyOverlay, this);
		constructor.BindEventCallback("join_room", &RmlOnlineLobbyScreen::onJoinRoom, this);
		constructor.BindEventCallback("set_filter", &RmlOnlineLobbyScreen::onSetFilter, this);
		constructor.BindEventCallback("sort_age", &RmlOnlineLobbyScreen::onSortAge, this);
		constructor.BindEventCallback("sort_map", &RmlOnlineLobbyScreen::onSortMap, this);
		constructor.BindEventCallback("sort_buddies", &RmlOnlineLobbyScreen::onSortBuddies, this);
		constructor.BindEventCallback("chat_entry_committed", &RmlOnlineLobbyScreen::onChatEntryCommitted, this);
		constructor.BindEventCallback("send_chat", &RmlOnlineLobbyScreen::onSendChat, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/OnlineLobby.rml");
}

//-------------------------------------------------------------------------------------------------
// Hook targets: forward into the singleton, same pattern as RmlLanLobbyScreen.cpp's free functions.
static void onOnlineLobbyGameListDelivered(const std::vector<OnlineLobbyData::GameRow> &rows)
{
	RmlOnlineLobbyScreen::instance().onGameListChanged(rows);
}

static void onOnlineLobbyChatDelivered(const UnicodeString &text, Color color)
{
	RmlOnlineLobbyScreen::instance().onChatLine(text, color);
}

static void onOnlineLobbyRosterRefreshDelivered()
{
	RmlOnlineLobbyScreen::instance().refreshPlayersFromHook();
}

static void onOnlineLobbyRoomChanged(int roomIndex, bool effectiveRoomChanged)
{
	RmlOnlineLobbyScreen::instance().onRoomChanged(roomIndex, effectiveRoomChanged);
}

static void onOnlineLobbyJoinResult(int result)
{
	RmlOnlineLobbyScreen::instance().onLobbyJoinResult(result);
}

static void onOnlineLobbyCreateResult(bool /*bSuccess*/)
{
	// Mirrors NGMP_WOLLobbyMenu_CreateLobbyCallback(): always proceeds to game options on success path;
	// TODO_NGMP upstream has no error case either (see WOLLobbyMenu.cpp).
	TheShell->push("Menus/GameSpyGameOptionsMenu.wnd");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLobbyScreen::show()
{
	if (!m_document)
		return;

	m_gameRows.beginUpdate();
	m_gameRows.endUpdate();
	m_playerRows.beginUpdate();
	m_playerRows.endUpdate();
	m_chatRows.beginUpdate();
	m_chatRows.endUpdate();
	m_model.selectedGameIndex = -1;
	m_model.chatEntryText.clear();
	m_rosterSignature.clear();

	OnlineLobbyActions::leaveCurrentLobby();

	refreshRoomCombo();
	refreshFilterHighlight();
	refreshSortHighlight();
	refreshPlayers(true);

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	g_onlineLobbyGameListHook = &onOnlineLobbyGameListDelivered;
	g_onlineLobbyChatHook = &onOnlineLobbyChatDelivered;
	g_onlineLobbyRosterRefreshHook = &onOnlineLobbyRosterRefreshDelivered;
	g_onlineLobbyRoomChangedHook = &onOnlineLobbyRoomChanged;
	g_onlineLobbyJoinResultHook = &onOnlineLobbyJoinResult;
	g_onlineLobbyCreateResultHook = &onOnlineLobbyCreateResult;
	OnlineLobbyActions::installScreenHooks();

	m_document->Show();
	OnlineLobbyActions::refresh();
}

void RmlOnlineLobbyScreen::hide()
{
	if (m_document)
		m_document->Hide();

	if (g_onlineLobbyGameListHook == &onOnlineLobbyGameListDelivered)
		g_onlineLobbyGameListHook = nullptr;
	if (g_onlineLobbyChatHook == &onOnlineLobbyChatDelivered)
		g_onlineLobbyChatHook = nullptr;
	if (g_onlineLobbyRosterRefreshHook == &onOnlineLobbyRosterRefreshDelivered)
		g_onlineLobbyRosterRefreshHook = nullptr;
	if (g_onlineLobbyRoomChangedHook == &onOnlineLobbyRoomChanged)
		g_onlineLobbyRoomChangedHook = nullptr;
	if (g_onlineLobbyJoinResultHook == &onOnlineLobbyJoinResult)
		g_onlineLobbyJoinResultHook = nullptr;
	if (g_onlineLobbyCreateResultHook == &onOnlineLobbyCreateResult)
		g_onlineLobbyCreateResultHook = nullptr;
}

bool RmlOnlineLobbyScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOnlineLobbyScreen::onBack()
{
	OnlineLobbyActions::back();
}

void RmlOnlineLobbyScreen::update()
{
	// WOLLobbyMenuUpdate() never runs for a registry-routed screen (see header comment); the periodic
	// player-list re-poll it drove via refreshPlayerList()'s time-gate is replaced here.
	refreshPlayers(false);

	if (OnlineLobbyActions::isPendingFullTeardown())
	{
		onBack();
	}
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLobbyScreen::refreshRoomCombo()
{
	m_model.rooms.clear();
	m_model.currentRoomIndex = -1;

	std::vector<OnlineLobbyData::RoomInfo> rooms = OnlineLobbyActions::getGroupRooms();
	for (const OnlineLobbyData::RoomInfo &info : rooms)
	{
		RoomRowModel room;
		room.index = info.index;
		room.label = info.label;
		room.isCurrent = info.isCurrent;
		m_model.rooms.push_back(room);
		if (info.isCurrent)
			m_model.currentRoomIndex = info.index;
	}

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("rooms");
		m_modelHandle.DirtyVariable("current_room_index");
	}
}

void RmlOnlineLobbyScreen::refreshFilterHighlight()
{
	// 0=All, 1=1v1, 2=Team, 3=FFA, 4=AOD, 5=Buddies -- see LobbyGameModeFilter (LobbyUtils.h),
	// mirrored as a plain int by OnlineLobbyActions::getFilterValue()/setFilter().
	const int filter = OnlineLobbyActions::getFilterValue();
	m_model.filterIsAll = (filter == 0);
	m_model.filterIs1v1 = (filter == 1);
	m_model.filterIsTeam = (filter == 2);
	m_model.filterIsFfa = (filter == 3);
	m_model.filterIsAod = (filter == 4);
	m_model.filterIsBuddies = (filter == 5);

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("filter_is_all");
		m_modelHandle.DirtyVariable("filter_is_1v1");
		m_modelHandle.DirtyVariable("filter_is_team");
		m_modelHandle.DirtyVariable("filter_is_ffa");
		m_modelHandle.DirtyVariable("filter_is_aod");
		m_modelHandle.DirtyVariable("filter_is_buddies");
	}
}

void RmlOnlineLobbyScreen::refreshSortHighlight()
{
	const OnlineLobbyActions::SortState sortState = OnlineLobbyActions::getSortState();
	m_model.sortByAge = sortState.sortByAge;
	m_model.sortAgeDescending = sortState.sortAgeDescending;
	m_model.sortByMap = sortState.sortByMap;
	m_model.sortMapDescending = sortState.sortMapDescending;
	m_model.sortBuddiesFirst = sortState.sortBuddiesFirst;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("sort_by_age");
		m_modelHandle.DirtyVariable("sort_age_descending");
		m_modelHandle.DirtyVariable("sort_by_map");
		m_modelHandle.DirtyVariable("sort_map_descending");
		m_modelHandle.DirtyVariable("sort_buddies_first");
	}
}

// Polled from update() and RegisterForRosterNeedsRefreshCallback(), same roster-signature-diff idiom
// as WOLLobbyMenu.cpp's PopulateLobbyPlayerListbox() (minus its listbox-scroll/rank-icon-viewport
// bookkeeping, which has no RmlUi equivalent -- see header comment).
void RmlOnlineLobbyScreen::refreshPlayers(bool force)
{
	std::vector<OnlineLobbyData::PlayerRow> rows = OnlineLobbyData::collectPlayerRows();
	const std::string signature = OnlineLobbyData::buildRosterSignature(rows);
	if (!force && signature == m_rosterSignature)
		return;
	m_rosterSignature = signature;

	m_playerRows.beginUpdate();
	for (const OnlineLobbyData::PlayerRow &row : rows)
	{
		PlayerRowModel &player = m_playerRows.next();
		player.name = row.displayName;
		player.isAdmin = row.isAdmin;
		player.isFriend = row.isFriend;
		player.isIgnored = row.isIgnored;
		player.isSelf = row.isSelf;
	}
	m_playerRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("players");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLobbyScreen::onGameListChanged(const std::vector<OnlineLobbyData::GameRow> &rows)
{
	m_gameRows.beginUpdate();
	int i = 0;
	for (const OnlineLobbyData::GameRow &row : rows)
	{
		GameRowModel &game = m_gameRows.next();
		game.index = i;
		game.lobbyID = row.lobbyID;
		game.displayName = row.displayName;
		game.mapDisplayName = row.mapDisplayName;
		game.playersText = row.playersText;
		game.playersAlmostFull = (row.playersTier == OnlineLobbyData::PLAYERCOUNT_ALMOST_FULL);
		game.playersFull = (row.playersTier == OnlineLobbyData::PLAYERCOUNT_FULL);
		game.hasPassword = row.hasPassword;
		game.allowObservers = row.allowObservers;
		game.trackStats = row.trackStats;
		game.pingOk = (row.pingTier == OnlineLobbyData::PING_OK);
		game.pingBad = (row.pingTier == OnlineLobbyData::PING_BAD);
		game.hasBuddy = row.hasBuddy;
		game.crcMismatch = row.crcMismatch;
		game.isSelected = (i == m_model.selectedGameIndex);
		++i;
	}
	m_gameRows.endUpdate();

	if (m_model.selectedGameIndex >= m_gameRows.liveCount())
		m_model.selectedGameIndex = -1;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("games");
		m_modelHandle.DirtyVariable("selected_game_index");
	}
}

// Chat is append-only (never diffed/rebuilt like games/players): next() alone grows liveCount by one
// each call, with no surrounding beginUpdate()/endUpdate() needed for that -- only show()/
// onRoomChanged() call beginUpdate()+endUpdate() back-to-back, to reset liveCount to 0 and clear the
// log (mirrors GadgetListBoxReset(listboxLobbyChat)).
void RmlOnlineLobbyScreen::onChatLine(const UnicodeString &text, Color color)
{
	ChatLineModel &line = m_chatRows.next();
	line.text = unicodeToUtf8(text);
	line.color = colorToCss(color);

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_lines");
}

void RmlOnlineLobbyScreen::onRoomChanged(int roomIndex, bool effectiveRoomChanged)
{
	std::vector<OnlineLobbyData::RoomInfo> rooms = OnlineLobbyActions::getGroupRooms();
	if (roomIndex < 0 || roomIndex >= (int)rooms.size())
		return;

	if (effectiveRoomChanged)
	{
		m_chatRows.beginUpdate();
		m_chatRows.endUpdate();
		refreshPlayers(true);
	}

	UnicodeString msg;
	msg.format(TheGameText->fetch("GUI:LobbyJoined"), utf8ToUnicode(rooms[roomIndex].label).str());
	onChatLine(msg, GameMakeColor(255, 255, 255, 255));

	OnlineLobbyActions::refresh();
	refreshRoomCombo();
}

void RmlOnlineLobbyScreen::refreshPlayersFromHook()
{
	refreshPlayers(false);
}

void RmlOnlineLobbyScreen::onLobbyJoinResult(int result)
{
	// Mirrors NGMP_WOLLobbyMenu_JoinLobbyCallback()'s success path; the failure message boxes it
	// raises are unchanged (GSMessageBoxOk stays a .wnd overlay either way). 0 ==
	// EJoinLobbyResult::JoinLobbyResult_Success (OnlineServices_LobbyInterface.h's first, unvalued
	// enumerator); the g_onlineLobbyJoinResultHook target passes the raw enum cast to int so this
	// file never has to include the NGMP header that declares it (see .h comment).
	if (result == 0)
	{
		TheShell->push("Menus/GameSpyGameOptionsMenu.wnd");
	}
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLobbyScreen::onHostGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::hostGame();
}

void RmlOnlineLobbyScreen::onJoinSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_model.selectedGameIndex >= 0 && m_model.selectedGameIndex < (int)m_model.games.size())
	{
		OnlineLobbyActions::joinLobby(m_model.games[m_model.selectedGameIndex].lobbyID);
	}
	else
	{
		onChatLine(TheGameText->fetch("GUI:NoGameSelected"), GameMakeColor(255, 80, 80, 255));
	}
}

void RmlOnlineLobbyScreen::onGameRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	int index = args[0].Get<int>();

	for (GameRowModel &game : m_model.games)
		game.isSelected = game.used && (game.index == index);

	m_model.selectedGameIndex = (index >= 0 && index < m_gameRows.liveCount()) ? index : -1;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("games");
		m_modelHandle.DirtyVariable("selected_game_index");
	}
}

void RmlOnlineLobbyScreen::onGameRowActivated(Rml::DataModelHandle constructorHandle, Rml::Event &event, const Rml::VariantList &args)
{
	onGameRowClicked(constructorHandle, event, args);
	if (m_model.selectedGameIndex >= 0 && m_model.selectedGameIndex < (int)m_model.games.size())
	{
		OnlineLobbyActions::joinLobby(m_model.games[m_model.selectedGameIndex].lobbyID);
	}
}

void RmlOnlineLobbyScreen::onRefresh(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::refresh();
}

void RmlOnlineLobbyScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

void RmlOnlineLobbyScreen::onBuddyOverlay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::toggleBuddyOverlay();
}

void RmlOnlineLobbyScreen::onJoinRoom(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// m_model.currentRoomIndex is already updated by the <select>'s two-way data-value binding by
	// the time this fires (RmlUi applies the DOM change before dispatching data-event-change).
	OnlineLobbyActions::joinRoom(m_model.currentRoomIndex);
}

void RmlOnlineLobbyScreen::onSetFilter(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	OnlineLobbyActions::setFilter(args[0].Get<int>());
	refreshFilterHighlight();
}

void RmlOnlineLobbyScreen::onSortAge(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::toggleSortAge();
	refreshSortHighlight();
}

void RmlOnlineLobbyScreen::onSortMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::toggleSortMap();
	refreshSortHighlight();
}

void RmlOnlineLobbyScreen::onSortBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineLobbyActions::toggleSortBuddies();
	refreshSortHighlight();
}

void RmlOnlineLobbyScreen::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (OnlineLobbyActions::sendChatEntry(utf8ToUnicode(m_model.chatEntryText)))
	{
		m_model.chatEntryText.clear();
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("chat_entry_text");
	}
}

void RmlOnlineLobbyScreen::onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (OnlineLobbyActions::sendChatButton(utf8ToUnicode(m_model.chatEntryText)))
	{
		m_model.chatEntryText.clear();
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("chat_entry_text");
	}
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOnlineLobbyScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlOnlineLobbyScreen::instance());
}

void CloseRmlOnlineLobbyScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlOnlineLobbyScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
