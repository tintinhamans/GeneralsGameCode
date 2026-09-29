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
#include "W3DDevice/GameClient/RmlUi/RmlChatInput.h"

#include "Common/AsciiString.h"
#include "Common/Debug.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbySession.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

// No GeneralsOnline/NGMP or LobbyUtils.h include here on purpose: those pull winsock headers that
// conflict with <windows.h> below when both land in a GameEngineDevice translation unit (see design
// notes / RmlOnlineLobbyScreen.h). Every NGMP touch point goes through OnlineLobbyActions (plain
// types only), included above.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
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

// Chat lines arrive as one string: the rooms interface prefixes "[hh:mm] " and player chat is
// "name: message". Split those off for display only; anything else stays in body.
static void splitChatLine(const Rml::String &text, Rml::String &time, Rml::String &name, Rml::String &body)
{
	time.clear();
	name.clear();
	body = text;

	if (body.size() >= 8 && body[0] == '[' && body[3] == ':' && body[6] == ']' && body[7] == ' '
		&& isdigit((unsigned char)body[1]) && isdigit((unsigned char)body[2]) && isdigit((unsigned char)body[4]) && isdigit((unsigned char)body[5]))
	{
		time = body.substr(1, 5);
		body = body.substr(8);
	}

	const size_t colon = body.find(": ");
	if (colon != Rml::String::npos && colon > 0 && colon <= 32 && body.find(' ') >= colon)
	{
		name = body.substr(0, colon);
		body = body.substr(colon + 2);
	}
}

static Rml::String formatCash(unsigned int amount)
{
	char digits[16];
	snprintf(digits, sizeof(digits), "%u", amount);
	Rml::String out = "$";
	const size_t len = strlen(digits);
	for (size_t i = 0; i < len; ++i)
	{
		if (i > 0 && (len - i) % 3 == 0)
			out += ',';
		out += digits[i];
	}
	return out;
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
			playerHandle.RegisterMember("index", &PlayerRowModel::index);
			playerHandle.RegisterMember("name", &PlayerRowModel::name);
			playerHandle.RegisterMember("is_admin", &PlayerRowModel::isAdmin);
			playerHandle.RegisterMember("is_friend", &PlayerRowModel::isFriend);
			playerHandle.RegisterMember("is_ignored", &PlayerRowModel::isIgnored);
			playerHandle.RegisterMember("is_self", &PlayerRowModel::isSelf);
			playerHandle.RegisterMember("rank_image", &PlayerRowModel::rankImage);
			playerHandle.RegisterMember("used", &PlayerRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<PlayerRowModel>>();

		Rml::StructHandle<PlayerMenuItemModel> playerMenuItemHandle = constructor.RegisterStruct<PlayerMenuItemModel>();
		if (playerMenuItemHandle)
		{
			playerMenuItemHandle.RegisterMember("label", &PlayerMenuItemModel::label);
			playerMenuItemHandle.RegisterMember("action", &PlayerMenuItemModel::action);
			playerMenuItemHandle.RegisterMember("used", &PlayerMenuItemModel::used);
		}
		constructor.RegisterArray<Rml::Vector<PlayerMenuItemModel>>();

		Rml::StructHandle<ChatLineModel> chatHandle = constructor.RegisterStruct<ChatLineModel>();
		if (chatHandle)
		{
			chatHandle.RegisterMember("text", &ChatLineModel::text);
			chatHandle.RegisterMember("color", &ChatLineModel::color);
			chatHandle.RegisterMember("time", &ChatLineModel::time);
			chatHandle.RegisterMember("name", &ChatLineModel::name);
			chatHandle.RegisterMember("body", &ChatLineModel::body);
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

		Rml::StructHandle<DetailMemberModel> memberHandle = constructor.RegisterStruct<DetailMemberModel>();
		if (memberHandle)
		{
			memberHandle.RegisterMember("name", &DetailMemberModel::name);
			memberHandle.RegisterMember("is_host", &DetailMemberModel::isHost);
			memberHandle.RegisterMember("used", &DetailMemberModel::used);
		}
		constructor.RegisterArray<Rml::Vector<DetailMemberModel>>();

		constructor.Bind("games", &m_model.games);
		constructor.Bind("selected_game_index", &m_model.selectedGameIndex);
		constructor.Bind("game_count", &m_model.gameCount);
		constructor.Bind("detail_visible", &m_model.detailVisible);
		constructor.Bind("detail_name", &m_model.detailName);
		constructor.Bind("detail_map", &m_model.detailMap);
		constructor.Bind("detail_map_path", &m_model.detailMapPath);
		constructor.Bind("detail_players", &m_model.detailPlayers);
		constructor.Bind("detail_ping", &m_model.detailPing);
		constructor.Bind("detail_cash", &m_model.detailCash);
		constructor.Bind("detail_ping_ok", &m_model.detailPingOk);
		constructor.Bind("detail_ping_bad", &m_model.detailPingBad);
		constructor.Bind("detail_password", &m_model.detailPassword);
		constructor.Bind("detail_observers", &m_model.detailObservers);
		constructor.Bind("detail_stats", &m_model.detailStats);
		constructor.Bind("detail_limit_superweapons", &m_model.detailLimitSuperweapons);
		constructor.Bind("detail_members", &m_model.detailMembers);
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
		constructor.Bind("player_menu_visible", &m_model.playerMenuVisible);
		constructor.Bind("player_menu_x_style", &m_model.playerMenuXStyle);
		constructor.Bind("player_menu_y_style", &m_model.playerMenuYStyle);
		constructor.Bind("player_menu_items", &m_model.playerMenuItems);

		constructor.BindEventCallback("host_game", &RmlOnlineLobbyScreen::onHostGame, this);
		constructor.BindEventCallback("join_selected", &RmlOnlineLobbyScreen::onJoinSelected, this);
		constructor.BindEventCallback("game_row_clicked", &RmlOnlineLobbyScreen::onGameRowClicked, this);
		constructor.BindEventCallback("game_row_activated", &RmlOnlineLobbyScreen::onGameRowActivated, this);
		constructor.BindEventCallback("game_row_hovered", &RmlOnlineLobbyScreen::onGameRowHovered, this);
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
		constructor.BindEventCallback("player_row_mousedown", &RmlOnlineLobbyScreen::onPlayerRowMouseDown, this);
		constructor.BindEventCallback("player_menu_item_clicked", &RmlOnlineLobbyScreen::onPlayerMenuItemClicked, this);
		constructor.BindEventCallback("player_menu_dismiss", &RmlOnlineLobbyScreen::onPlayerMenuDismiss, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/OnlineLobby.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineLobbyScreen::show()
{
	// Shell::doPush() opens a routed screen twice (runInit, then bringForward); entering the session a
	// second time would fetch the room list again and restart the lobby state.
	if (!m_document || m_document->IsVisible())
		return;

	m_lastRosterPoll = ::GetTickCount();

	m_gameRows.beginUpdate();
	m_gameRows.endUpdate();
	m_playerRows.beginUpdate();
	m_playerRows.endUpdate();
	m_chatRows.beginUpdate();
	m_chatRows.endUpdate();
	m_model.selectedGameIndex = -1;
	m_model.hoverGameIndex = -1;
	m_model.gameCount = 0;
	m_rawGameRows.clear();
	m_model.chatEntryText.clear();
	m_rosterSignature.clear();
	m_playerMenuItemRows.beginUpdate();
	m_playerMenuItemRows.endUpdate();
	m_model.playerMenuVisible = false;
	m_rawPlayerRows.clear();

	m_connections.disconnect();
	m_connections.add(OnlineLobbySignals::gameList().connect([this](const std::vector<OnlineLobbyData::GameRow> &rows) { onGameListChanged(rows); }));
	m_connections.add(OnlineLobbySignals::chatLine().connect([this](const UnicodeString &text, Color color) { onChatLine(text, color); }));
	m_connections.add(OnlineLobbySignals::rosterRefresh().connect([this]() { refreshPlayersFromSignal(); }));
	m_connections.add(OnlineLobbySignals::roomChanged().connect([this](int roomIndex, bool effectiveRoomChanged) { onRoomChanged(roomIndex, effectiveRoomChanged); }));
	m_connections.add(OnlineLobbySignals::joinResult().connect([this](int result) { onLobbyJoinResult(result); }));
	m_connections.add(OnlineLobbySignals::createResult().connect([](bool /*bSuccess*/)
		{
			// Mirrors NGMP_WOLLobbyMenu_CreateLobbyCallback(): always proceeds to game options on success path;
			// TODO_NGMP upstream has no error case either (see WOLLobbyMenu.cpp).
			TheShell->push("Menus/GameSpyGameOptionsMenu.wnd");
		}));
	m_connections.add(OnlineLobbySignals::roomListResult().connect([this](bool success) { onRoomListResult(success); }));

	// Leaves any lobby, registers the NGMP callbacks, fetches the room list and joins the first room.
	OnlineLobbySession::enter();
	OnlineLobbyData::resetPlayerStatsRequests();

	refreshRoomCombo();
	refreshFilterHighlight();
	refreshSortHighlight();
	refreshPlayers(true);
	refreshDetail();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_document->Show();
	OnlineLobbyActions::refresh();
}

void RmlOnlineLobbyScreen::hide()
{
	if (m_document)
		m_document->Hide();

	OnlineLobbySession::leave();
	m_connections.disconnect();
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
	// WOLLobbyMenuUpdate() never runs for a registry-routed screen (see header comment); its
	// pending-full-teardown exit and lobby-list-dirty poll are OnlineLobbySession's, and the periodic
	// player-list re-poll it drove via refreshPlayerList()'s time-gate is replaced here.
	if (OnlineLobbySession::handlePendingTeardown())
		return;

	OnlineLobbySession::update();

	// The roster signal covers roster changes; this only picks up friend/ignore changes, on the .wnd's
	// playerListRefreshInterval, since a poll every frame rebuilds the whole roster (hundreds of players).
	const DWORD now = ::GetTickCount();
	if (now - m_lastRosterPoll >= PLAYER_LIST_POLL_MS)
	{
		m_lastRosterPoll = now;
		refreshPlayers(false);
		refreshRankIcons();
	}

	if (m_model.playerMenuVisible)
		clampPlayerMenu();
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

	m_rawPlayerRows = rows;

	m_playerRows.beginUpdate();
	int playerIndex = 0;
	for (const OnlineLobbyData::PlayerRow &row : rows)
	{
		PlayerRowModel &player = m_playerRows.next();
		player.index = playerIndex++;
		player.name = row.displayName;
		player.isAdmin = row.isAdmin;
		player.isFriend = row.isFriend;
		player.isIgnored = row.isIgnored;
		player.isSelf = row.isSelf;
		player.rankImage = OnlineLobbyData::rankImageForUser(row.userID);
	}
	m_playerRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("players");

	// A stale row (e.g. the target left the room) would leave the menu pointing at nothing; same
	// "close on churn" behavior winSetLoneWindow() gets for free when its owning listbox rebuilds.
	closePlayerMenu();
}

//-------------------------------------------------------------------------------------------------
// The .wnd repaints the badges of the rows in view from the stats cache and fetches stats for them
// (plus a margin); this does the same from the player list's scroll position.
void RmlOnlineLobbyScreen::refreshRankIcons()
{
	const int rowCount = (int)m_rawPlayerRows.size();
	if (rowCount == 0 || !m_document)
		return;

	int first = 0;
	int last = rowCount - 1;
	Rml::Element *list = m_document->GetElementById("player-list");
	Rml::Element *firstRow = list ? list->QuerySelector(".player-row") : nullptr;
	if (list && firstRow && firstRow->GetOffsetHeight() > 0.0f)
	{
		const float rowHeight = firstRow->GetOffsetHeight();
		first = (int)(list->GetScrollTop() / rowHeight);
		last = first + (int)(list->GetClientHeight() / rowHeight) + 1;
	}
	static const int VISIBLE_STATS_BUFFER = 8; // same prefetch margin as WOLLobbyMenu.cpp
	first = max(0, first - VISIBLE_STATS_BUFFER);
	last = min(rowCount - 1, last + VISIBLE_STATS_BUFFER);

	std::vector<int64_t> visibleUsers;
	for (int i = first; i <= last; ++i)
		visibleUsers.push_back(m_rawPlayerRows[i].userID);
	OnlineLobbyData::requestPlayerStats(visibleUsers);

	bool changed = false;
	for (int i = 0; i < rowCount && i < (int)m_model.players.size(); ++i)
	{
		const Rml::String rank = OnlineLobbyData::rankImageForUser(m_rawPlayerRows[i].userID);
		if (rank != m_model.players[i].rankImage)
		{
			m_model.players[i].rankImage = rank;
			changed = true;
		}
	}
	if (changed && m_modelHandle)
		m_modelHandle.DirtyVariable("players");
}

void RmlOnlineLobbyScreen::refreshDetail()
{
	const int count = m_gameRows.liveCount();
	int index = m_model.hoverGameIndex;
	if (index < 0 || index >= count || index >= (int)m_rawGameRows.size())
		index = m_model.selectedGameIndex;

	m_detailMemberRows.beginUpdate();
	m_model.detailVisible = index >= 0 && index < count && index < (int)m_rawGameRows.size();
	if (m_model.detailVisible)
	{
		const OnlineLobbyData::GameRow &row = m_rawGameRows[index];
		m_model.detailName = row.displayName;
		m_model.detailMap = row.mapDisplayName;
		m_model.detailMapPath = row.mapPath;
		m_model.detailPlayers = row.playersText;
		char ping[32];
		snprintf(ping, sizeof(ping), "%d ms", row.latency);
		m_model.detailPing = ping;
		m_model.detailCash = formatCash(row.startingCash);
		m_model.detailPingOk = row.pingTier == OnlineLobbyData::PING_OK;
		m_model.detailPingBad = row.pingTier == OnlineLobbyData::PING_BAD;
		m_model.detailPassword = row.hasPassword;
		m_model.detailObservers = row.allowObservers;
		m_model.detailStats = row.trackStats;
		m_model.detailLimitSuperweapons = row.limitSuperweapons;
		for (size_t i = 0; i < row.memberNames.size(); ++i)
		{
			DetailMemberModel &member = m_detailMemberRows.next();
			member.name = row.memberNames[i];
			member.isHost = i == 0;
		}
	}
	m_detailMemberRows.endUpdate();

	if (m_modelHandle)
	{
		static const char *const vars[] = { "detail_visible", "detail_name", "detail_map", "detail_map_path", "detail_players",
			"detail_ping", "detail_cash", "detail_ping_ok", "detail_ping_bad", "detail_password", "detail_observers",
			"detail_stats", "detail_limit_superweapons", "detail_members" };
		for (const char *var : vars)
			m_modelHandle.DirtyVariable(var);
	}
}

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
	m_rawGameRows = rows;
	m_model.gameCount = m_gameRows.liveCount();

	if (m_model.selectedGameIndex >= m_gameRows.liveCount())
		m_model.selectedGameIndex = -1;
	if (m_model.hoverGameIndex >= m_gameRows.liveCount())
		m_model.hoverGameIndex = -1;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("games");
		m_modelHandle.DirtyVariable("selected_game_index");
		m_modelHandle.DirtyVariable("game_count");
	}
	refreshDetail();
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
	splitChatLine(line.text, line.time, line.name, line.body);

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

void RmlOnlineLobbyScreen::refreshPlayersFromSignal()
{
	refreshPlayers(false);
}

void RmlOnlineLobbyScreen::onRoomListResult(bool success)
{
	refreshRoomCombo();

	// Same error line WOLLobbyMenu.cpp adds to its chat listbox.
	if (!success)
		onChatLine(UnicodeString(L"	 ERROR: No rooms are available. Try logging in again."), GameMakeColor(255, 0, 0, 255));
}

void RmlOnlineLobbyScreen::onLobbyJoinResult(int result)
{
	// Mirrors NGMP_WOLLobbyMenu_JoinLobbyCallback()'s success path; the failure message boxes it
	// raises are unchanged (GSMessageBoxOk stays a .wnd overlay either way). 0 ==
	// EJoinLobbyResult::JoinLobbyResult_Success (OnlineServices_LobbyInterface.h's first, unvalued
	// enumerator); the OnlineLobbySignals::joinResult target passes the raw enum cast to int so this
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
	refreshDetail();
}

void RmlOnlineLobbyScreen::onGameRowHovered(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	const int index = args.empty() ? -1 : args[0].Get<int>();
	if (index == m_model.hoverGameIndex)
		return;
	m_model.hoverGameIndex = index;
	refreshDetail();
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

void RmlOnlineLobbyScreen::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	// Change fires on every edit; only Enter (linebreak) commits, like the .wnd's GEM_EDIT_DONE.
	if (!ev.GetParameter<bool>("linebreak", false))
		return;

	if (OnlineLobbyActions::sendChatEntry(utf8ToUnicode(m_model.chatEntryText)))
	{
		m_model.chatEntryText.clear();
		RmlClearChatInput(ev);
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("chat_entry_text");
	}
}

void RmlOnlineLobbyScreen::onSendChat(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	if (OnlineLobbyActions::sendChatButton(utf8ToUnicode(m_model.chatEntryText)))
	{
		m_model.chatEntryText.clear();
		RmlClearChatInput(ev);
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("chat_entry_text");
	}
}

//-------------------------------------------------------------------------------------------------
// Mirrors GLM_RIGHT_CLICKED: RmlUi only synthesizes "click" for the left button, so the
// right-click open has to be caught on the raw mousedown (button 1), same as
// RmlLanGameSetupScreen.cpp's onStartPositionMarkerMouseDown().
void RmlOnlineLobbyScreen::onPlayerRowMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || ev.GetParameter<int>("button", 0) != 1)
		return;

	const int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_rawPlayerRows.size())
		return;

	m_playerMenuTarget = m_rawPlayerRows[index];

	const std::vector<OnlineLobbyData::PlayerMenuItem> items = OnlineLobbyData::buildPlayerContextMenu(m_playerMenuTarget);
	m_playerMenuItemRows.beginUpdate();
	for (const OnlineLobbyData::PlayerMenuItem &item : items)
	{
		PlayerMenuItemModel &menuItem = m_playerMenuItemRows.next();
		menuItem.action = (int)item.action;
		if (item.action == OnlineLobbyData::PLAYERMENU_TOGGLE_IGNORE)
		{
			// WOLBuddyOverlay.cpp's setUnignoreText() overwrites ButtonIgnore's text with this exact
			// hardcoded (non-GUI:-key) literal under GENERALS_ONLINE; mirrored verbatim, not localized.
			menuItem.label = m_playerMenuTarget.isIgnored ? "Unblock" : "Block";
		}
		else
		{
			menuItem.label = unicodeToUtf8(TheGameText->fetch(item.labelKey.c_str()));
		}
	}
	m_playerMenuItemRows.endUpdate();

	m_playerMenuRawX = (float)ev.GetParameter<int>("mouse_x", 0);
	m_playerMenuRawY = (float)ev.GetParameter<int>("mouse_y", 0);
	char buf[32];
	snprintf(buf, sizeof(buf), "%dpx", (int)m_playerMenuRawX);
	m_model.playerMenuXStyle = buf;
	snprintf(buf, sizeof(buf), "%dpx", (int)m_playerMenuRawY);
	m_model.playerMenuYStyle = buf;
	m_model.playerMenuVisible = true;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("player_menu_items");
		m_modelHandle.DirtyVariable("player_menu_x_style");
		m_modelHandle.DirtyVariable("player_menu_y_style");
		m_modelHandle.DirtyVariable("player_menu_visible");
	}
}

void RmlOnlineLobbyScreen::onPlayerMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const OnlineLobbyData::PlayerMenuAction action = (OnlineLobbyData::PlayerMenuAction)args[0].Get<int>();
	OnlineLobbyActions::performPlayerMenuAction(action, m_playerMenuTarget);
	closePlayerMenu();
	refreshPlayers(true); // reflect the new friend/ignored state immediately, same as
	                       // WOLBuddyOverlayRCMenuSystem's PopulateLobbyPlayerListbox() call
}

void RmlOnlineLobbyScreen::onPlayerMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	closePlayerMenu();
}

void RmlOnlineLobbyScreen::closePlayerMenu()
{
	if (!m_model.playerMenuVisible)
		return;
	m_model.playerMenuVisible = false;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("player_menu_visible");
}

// Called each update() while the menu is visible. RmlUi doesn't know the menu's laid-out size until
// after a layout pass, so the first frame positions it at the raw cursor point (onPlayerRowMouseDown)
// and this re-derives a clamped position from m_playerMenuRawX/Y once the real size is available --
// same on-screen clamp GLM_RIGHT_CLICKED does against TheDisplay's width/height, just deferred a
// frame instead of using winGetSize() synchronously.
void RmlOnlineLobbyScreen::clampPlayerMenu()
{
	if (!m_document || !m_context)
		return;

	Rml::Element *menu = m_document->GetElementById("player-context-menu");
	if (!menu)
		return;

	const Rml::Vector2f size = menu->GetBox().GetSize();
	if (size.x <= 0.0f || size.y <= 0.0f)
		return;

	const Rml::Vector2i contextSize = m_context->GetDimensions();
	float left = m_playerMenuRawX;
	float top = m_playerMenuRawY;
	if (left + size.x > (float)contextSize.x)
		left = (float)contextSize.x - size.x;
	if (top + size.y > (float)contextSize.y)
		top = (float)contextSize.y - size.y;
	if (left < 0.0f)
		left = 0.0f;
	if (top < 0.0f)
		top = 0.0f;

	char buf[32];
	snprintf(buf, sizeof(buf), "%dpx", (int)left);
	const Rml::String newX = buf;
	snprintf(buf, sizeof(buf), "%dpx", (int)top);
	const Rml::String newY = buf;

	if (newX != m_model.playerMenuXStyle || newY != m_model.playerMenuYStyle)
	{
		m_model.playerMenuXStyle = newX;
		m_model.playerMenuYStyle = newY;
		if (m_modelHandle)
		{
			m_modelHandle.DirtyVariable("player_menu_x_style");
			m_modelHandle.DirtyVariable("player_menu_y_style");
		}
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
