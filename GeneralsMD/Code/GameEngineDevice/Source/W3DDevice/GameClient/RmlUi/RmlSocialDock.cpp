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

#include "W3DDevice/GameClient/RmlUi/RmlSocialDock.h"

#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlaySession.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupSession.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchSession.h"
#include "GameClient/RmlUiScreenRegistry.h"
// Same winsock-free header set as RmlBuddyOverlayScreen.cpp, so <windows.h> below is safe.
#include "W3DDevice/GameClient/RmlUi/RmlBuddyOverlayScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>
#include <windows.h>

namespace
{
	const char *const kCommunicatorPath = "Menus/WOLBuddyOverlay.wnd";

	// The online shell screens the dock sits beside. The score screen is not one: it is a fixed
	// centred box with no room beside it on 4:3 and 5:4, so there the communicator stays the popup.
	const char *const kHostPaths[] =
	{
		"Menus/WOLWelcomeMenu.wnd",
		"Menus/WOLCustomLobby.wnd",
		"Menus/WOLQuickMatchMenu.wnd",
		"Menus/GameSpyGameOptionsMenu.wnd",
	};

	bool isHostPath(const AsciiString &path)
	{
		for (size_t i = 0; i < sizeof(kHostPaths) / sizeof(kHostPaths[0]); ++i)
			if (path == kHostPaths[i])
				return true;
		return false;
	}

	// Same conversions as RmlBuddyOverlayScreen.cpp.
	Rml::String unicodeToUtf8(const UnicodeString &str)
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

	UnicodeString utf8ToUnicode(const Rml::String &utf8)
	{
		UnicodeString text;
		int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		if (len <= 1)
			return text;

		std::vector<wchar_t> wide((size_t)len);
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
		text.set((const WideChar *)&wide[0]);
		return text;
	}

	Rml::String fetchUtf8(const char *key)
	{
		return TheGameText ? unicodeToUtf8(TheGameText->fetch(key)) : Rml::String();
	}

	// First character of a UTF-8 name for the avatar, upper-cased when it is ASCII.
	Rml::String initialOf(const std::string &name)
	{
		if (name.empty())
			return "?";
		const unsigned char c = (unsigned char)name[0];
		if (c < 0x80)
			return Rml::String(1, (char)toupper(c));
		size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
		return name.substr(0, len < name.size() ? len : name.size());
	}

	// "[name] text" -> name, text; anything else is all text.
	void splitChatLine(const Rml::String &line, Rml::String &name, Rml::String &body)
	{
		const size_t close = line.find(']');
		if (line.size() > 2 && line[0] == '[' && close != Rml::String::npos && close > 1 && close <= 40)
		{
			name = line.substr(1, close - 1);
			const size_t start = line.find_first_not_of(' ', close + 1);
			body = start == Rml::String::npos ? Rml::String() : line.substr(start);
			return;
		}
		name.clear();
		body = line;
	}

	int argIndex(const Rml::VariantList &args)
	{
		return args.empty() ? -1 : args[0].Get<int>();
	}
}

//-------------------------------------------------------------------------------------------------
RmlSocialDock &RmlSocialDock::instance()
{
	static RmlSocialDock s_dock;
	return s_dock;
}

void RmlSocialDock::dirty(const char *name)
{
	if (m_modelHandle)
		m_modelHandle.DirtyVariable(name);
}

void RmlSocialDock::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("socialdock");
	if (constructor)
	{
		if (Rml::StructHandle<RowModel> h = constructor.RegisterStruct<RowModel>())
		{
			h.RegisterMember("index", &RowModel::index);
			h.RegisterMember("name", &RowModel::name);
			h.RegisterMember("initial", &RowModel::initial);
			h.RegisterMember("sub", &RowModel::sub);
			h.RegisterMember("activity", &RowModel::activity);
			h.RegisterMember("unread", &RowModel::unread);
			h.RegisterMember("is_request", &RowModel::isRequest);
			h.RegisterMember("is_friend", &RowModel::isFriend);
			h.RegisterMember("is_lobby", &RowModel::isLobby);
			h.RegisterMember("is_recent", &RowModel::isRecent);
			h.RegisterMember("is_open", &RowModel::isOpen);
			h.RegisterMember("used", &RowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();

		if (Rml::StructHandle<BlockedModel> h = constructor.RegisterStruct<BlockedModel>())
		{
			h.RegisterMember("index", &BlockedModel::index);
			h.RegisterMember("name", &BlockedModel::name);
			h.RegisterMember("initial", &BlockedModel::initial);
			h.RegisterMember("used", &BlockedModel::used);
		}
		constructor.RegisterArray<Rml::Vector<BlockedModel>>();

		if (Rml::StructHandle<LineModel> h = constructor.RegisterStruct<LineModel>())
		{
			h.RegisterMember("name", &LineModel::name);
			h.RegisterMember("body", &LineModel::body);
			h.RegisterMember("mine", &LineModel::mine);
			h.RegisterMember("cont", &LineModel::cont);
			h.RegisterMember("is_note", &LineModel::isNote);
			h.RegisterMember("used", &LineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<LineModel>>();

		if (Rml::StructHandle<MenuItemModel> h = constructor.RegisterStruct<MenuItemModel>())
		{
			h.RegisterMember("label", &MenuItemModel::label);
			h.RegisterMember("action", &MenuItemModel::action);
			h.RegisterMember("used", &MenuItemModel::used);
		}
		constructor.RegisterArray<Rml::Vector<MenuItemModel>>();

		constructor.Bind("expanded", &m_model.expanded);
		constructor.Bind("locked", &m_model.locked);
		constructor.Bind("host_template", &m_model.hostTemplate);
		constructor.Bind("view", &m_model.view);
		constructor.Bind("online_count", &m_model.onlineCount);
		constructor.Bind("badge_count", &m_model.badgeCount);
		constructor.Bind("online_text", &m_model.onlineText);
		constructor.Bind("request_count", &m_model.requestCount);
		constructor.Bind("friend_count", &m_model.friendCount);
		constructor.Bind("lobby_count", &m_model.lobbyCount);
		constructor.Bind("recent_count", &m_model.recentCount);
		constructor.Bind("blocked_count", &m_model.blockedCount);
		constructor.Bind("rows", &m_model.rows);
		constructor.Bind("blocked_rows", &m_model.blockedRows);
		constructor.Bind("thread_name", &m_model.threadName);
		constructor.Bind("thread_initial", &m_model.threadInitial);
		constructor.Bind("thread_sub", &m_model.threadSub);
		constructor.Bind("thread_activity", &m_model.threadActivity);
		constructor.Bind("others_unread", &m_model.othersUnread);
		constructor.Bind("chat_lines", &m_model.chatLines);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);
		constructor.Bind("player_menu_visible", &m_model.playerMenuVisible);
		constructor.Bind("player_menu_x_style", &m_model.playerMenuXStyle);
		constructor.Bind("player_menu_y_style", &m_model.playerMenuYStyle);
		constructor.Bind("player_menu_items", &m_model.playerMenuItems);

		constructor.BindEventCallback("toggle", &RmlSocialDock::onToggle, this);
		constructor.BindEventCallback("collapse", &RmlSocialDock::onCollapse, this);
		constructor.BindEventCallback("show_list", &RmlSocialDock::onShowList, this);
		constructor.BindEventCallback("show_blocked", &RmlSocialDock::onShowBlocked, this);
		constructor.BindEventCallback("open_thread", &RmlSocialDock::onOpenThread, this);
		constructor.BindEventCallback("row_mousedown", &RmlSocialDock::onRowMouseDown, this);
		constructor.BindEventCallback("accept", &RmlSocialDock::onAccept, this);
		constructor.BindEventCallback("deny", &RmlSocialDock::onDeny, this);
		constructor.BindEventCallback("add", &RmlSocialDock::onAdd, this);
		constructor.BindEventCallback("blocked_row_mousedown", &RmlSocialDock::onBlockedRowMouseDown, this);
		constructor.BindEventCallback("unblock", &RmlSocialDock::onUnblock, this);
		constructor.BindEventCallback("chat_entry_committed", &RmlSocialDock::onChatEntryCommitted, this);
		constructor.BindEventCallback("send_chat", &RmlSocialDock::onSendChat, this);
		constructor.BindEventCallback("player_menu_item_clicked", &RmlSocialDock::onMenuItemClicked, this);
		constructor.BindEventCallback("player_menu_dismiss", &RmlSocialDock::onMenuDismiss, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/SocialDock.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::init(Rml::Context *context)
{
	load(context);

	m_connections.disconnect();
	m_connections.add( BuddyOverlaySignals::chatMessage().connect( [this]( int64_t sourceUserID, int64_t targetUserID, const UnicodeString &text ) { onChatMessage( sourceUserID, targetUserID, text ); } ) );
	m_connections.add( BuddyToastSignals::shown().connect( [this]( const UnicodeString & ) { onToastShown(); } ) );
	m_connections.add( BuddyOverlaySignals::rosterNeedsRefresh().connect( [this]( bool, bool ) { m_rosterDirty = true; } ) );

	// Quick match and game setup turn their communicator button off as the game starts; the dock's tab
	// follows them while it is over that screen, like the button it stands in for.
	auto lock = [this]( Bool enabled )
	{
		m_model.locked = enabled == FALSE;
		dirty("locked");
	};
	m_connections.add( QuickMatchSignals::communicatorButtonEnabled().connect( lock ) );
	m_connections.add( OnlineGameSetupSignals::communicatorButtonEnabled().connect( lock ) );
}

void RmlSocialDock::shutdown()
{
	m_connections.disconnect();
	m_document = nullptr; // Rml::Shutdown() destroys the document
	m_context = nullptr;
	m_modelHandle = Rml::DataModelHandle();
	m_shown = false;
	m_reserved = false;
}

//-------------------------------------------------------------------------------------------------
// The login the dock attached to ended (TearDownGeneralsOnline): forget everything about it.
void RmlSocialDock::resetForLogout()
{
	m_attached = false;
	m_sessionVisible = false; // the social interface went with the login
	m_expanded = false;
	m_threadUserID = 0;
	m_toastUserID = 0;
	m_openLatest = false;
	m_localUnread.clear();
	m_rawRows.clear();
	m_rawBlocked.clear();
	m_model.expanded = false;
	m_model.locked = false;
	m_model.view = "list";
	m_model.chatEntryText.clear();
	m_rowList.beginUpdate();
	m_rowList.endUpdate();
	m_lineList.beginUpdate();
	m_lineList.endUpdate();
	closeContextMenu();
	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlSocialDock::attachSession()
{
	BuddyOverlaySession::attach();
	m_attached = BuddyOverlaySession::isAttached();
	if (!m_attached)
		return;

	refreshRoster();
	BuddyOverlayActions::refreshFriendsList( false, []() { RmlSocialDock::instance().m_rosterDirty = true; } );
}

void RmlSocialDock::setSessionVisible(bool visible)
{
	if (visible == m_sessionVisible || !m_attached)
		return;
	m_sessionVisible = visible;
	BuddyOverlaySession::setVisible(visible);
}

// .dock-open on every host document while the panel is out, whether or not that host is the one on
// screen, so a host shown later already has room for it.
void RmlSocialDock::applyHostClasses(bool reserve)
{
	if (!m_context)
		return;

	for (int i = 0; i < m_context->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument *doc = m_context->GetDocument(i);
		if (!doc || doc == m_document || !doc->IsClassSet("dock-host"))
			continue;
		if (doc->IsClassSet("dock-open") != reserve)
			doc->SetClass("dock-open", reserve);
		if (doc->IsVisible())
		{
			const bool isTemplate = doc->IsClassSet("screen");
			if (isTemplate != m_model.hostTemplate)
			{
				m_model.hostTemplate = isTemplate;
				dirty("host_template");
			}
		}
	}
	m_reserved = reserve;
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::tick()
{
	if (!m_document)
		return;

	m_toastJustShown = false; // see m_toastUserID

	if (m_attached && !BuddyOverlaySession::isAttached())
		resetForLogout();

	const bool online = BuddyOverlayData::isOnline();
	const AsciiString top = RmlUiScreenRegistry::topLayer(AsciiString(kCommunicatorPath));
	const bool onHost = online && isHostPath(top);

	// A host still open under a popup keeps the session and its room for the panel.
	bool hostOpen = false;
	if (online)
		for (size_t i = 0; i < sizeof(kHostPaths) / sizeof(kHostPaths[0]) && !hostOpen; ++i)
			hostOpen = RmlUiScreenRegistry::isOpen(AsciiString(kHostPaths[i]));

	if (onHost && !m_attached)
		attachSession();

	if (onHost != m_shown)
	{
		m_shown = onHost;
		if (m_shown)
		{
			m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		}
		else
		{
			closeContextMenu();
			m_document->Hide();
		}
	}

	if (m_shown && top != m_host)
	{
		// The screen changed under the dock: the new one starts with its communicator enabled, and an
		// open panel goes back on top of it for Escape.
		m_host = top;
		if (m_model.locked)
		{
			m_model.locked = false;
			dirty("locked");
		}
		if (m_expanded)
			RmlUiScreenRegistry::raise(AsciiString(kCommunicatorPath));
	}

	setSessionVisible(hostOpen && m_expanded);
	if ((hostOpen && m_expanded) != m_reserved || m_shown)
		applyHostClasses(hostOpen && m_expanded);

	if (m_rosterDirty && m_attached)
	{
		m_rosterDirty = false;
		refreshRoster();
	}
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::expand()
{
	if (!m_shown || m_model.locked)
		return;

	m_expanded = true;
	m_model.expanded = true;
	dirty("expanded");

	setSessionVisible(true);
	applyHostClasses(true);

	if (m_openLatest && m_toastUserID != 0)
		openThread(m_toastUserID);
	else if (m_openLatest)
		m_model.view = "list";
	m_openLatest = false;

	if (m_model.view == "thread" && m_threadUserID != 0)
		openThread(m_threadUserID); // reading it now: clears what came in while collapsed
	else if (m_model.view == "blocked")
		refreshBlockList();
	refreshRoster();
	BuddyOverlayActions::refreshFriendsList( false, []() { RmlSocialDock::instance().m_rosterDirty = true; } );
}

void RmlSocialDock::collapse()
{
	closeContextMenu();
	m_expanded = false;
	m_model.expanded = false;
	dirty("expanded");

	if (m_context)
	{
		Rml::Element *focus = m_context->GetFocusElement();
		if (focus && focus->GetOwnerDocument() == m_document)
			focus->Blur();
	}

	setSessionVisible(false);
	applyHostClasses(false);
	refreshRoster(); // unread from now on is counted by the social interface again
}

void RmlSocialDock::back()
{
	if (m_model.playerMenuVisible)
	{
		closeContextMenu();
		return;
	}
	BuddyOverlayActions::close();
}

void RmlSocialDock::noteToastClicked()
{
	m_openLatest = m_shown; // only an expand that follows right away may use it
}

//-------------------------------------------------------------------------------------------------
// Same four sections and sort as the popup (BuddyOverlayData::collectBuddyRows()), grouped by the rml.
void RmlSocialDock::refreshRoster()
{
	m_rawRows = BuddyOverlayData::collectBuddyRows();

	const Rml::String onlineLabel = fetchUtf8("Buddy:Online");
	const Rml::String offlineLabel = fetchUtf8("Buddy:Offline");
	const bool threadOpen = m_model.view == "thread";

	int requests = 0, friends = 0, lobby = 0, recent = 0, online = 0, unreadThreads = 0, othersUnread = 0;
	m_rowList.beginUpdate();
	for (size_t i = 0; i < m_rawRows.size(); ++i)
	{
		const BuddyOverlayData::BuddyRow &raw = m_rawRows[i];
		RowModel &m = m_rowList.next();
		m.index = (int)i;
		m.name = raw.displayName;
		m.initial = initialOf(raw.displayName);
		m.isRequest = raw.category == BuddyOverlayData::ROW_REQUEST;
		m.isFriend = raw.category == BuddyOverlayData::ROW_FRIEND;
		m.isLobby = raw.category == BuddyOverlayData::ROW_LOBBY_MEMBER;
		m.isRecent = raw.category == BuddyOverlayData::ROW_RECENTLY_PLAYED;

		if (m.isFriend)
		{
			++friends;
			m.activity = raw.online ? (int)raw.activity : (int)BuddyOverlayData::ACTIVITY_OFFLINE;
			if (raw.online)
			{
				++online;
				m.sub = raw.presence.empty() ? onlineLabel : Rml::String(raw.presence);
			}
			else
			{
				m.sub = offlineLabel;
			}

			std::map<int64_t, int>::const_iterator local = m_localUnread.find(raw.userID);
			m.unread = raw.unreadCount + (local != m_localUnread.end() ? local->second : 0);
			m.isOpen = threadOpen && raw.userID == m_threadUserID;
			if (m.unread > 0)
			{
				++unreadThreads;
				if (raw.userID != m_threadUserID)
					++othersUnread;
			}
			if (raw.userID == m_threadUserID)
			{
				m_model.threadName = m.name;
				m_model.threadInitial = m.initial;
				m_model.threadSub = m.sub;
				m_model.threadActivity = m.activity;
			}
		}
		else if (m.isRequest)
		{
			++requests;
			m.sub = raw.statusText;
		}
		else if (m.isLobby)
		{
			++lobby;
		}
		else
		{
			++recent;
		}
	}
	m_rowList.endUpdate();

	m_model.requestCount = requests;
	m_model.friendCount = friends;
	m_model.lobbyCount = lobby;
	m_model.recentCount = recent;
	m_model.onlineCount = online;
	m_model.badgeCount = requests + unreadThreads;
	m_model.othersUnread = othersUnread;

	char buf[64];
	snprintf(buf, sizeof(buf), "%d %s", online, onlineLabel.c_str());
	m_model.onlineText = buf;

	static const char *const kRosterVariables[] =
	{
		"view", "rows", "request_count", "friend_count", "lobby_count", "recent_count", "online_count", "badge_count",
		"online_text", "others_unread", "thread_name", "thread_initial", "thread_sub", "thread_activity", "chat_entry_text",
	};
	for (const char *name : kRosterVariables)
		dirty(name);
}

void RmlSocialDock::refreshBlockList()
{
	BuddyOverlayActions::refreshBlockList( []( std::vector<BuddyOverlayData::BlockedRow> rows )
		{
			RmlSocialDock &dock = RmlSocialDock::instance();
			dock.m_rawBlocked = rows;

			dock.m_blockedList.beginUpdate();
			for (size_t i = 0; i < dock.m_rawBlocked.size(); ++i)
			{
				BlockedModel &m = dock.m_blockedList.next();
				m.index = (int)i;
				m.name = dock.m_rawBlocked[i].displayName;
				m.initial = initialOf(dock.m_rawBlocked[i].displayName);
			}
			dock.m_blockedList.endUpdate();
			dock.m_model.blockedCount = (int)dock.m_rawBlocked.size();

			dock.dirty("blocked_rows");
			dock.dirty("blocked_count");
		} );
}

void RmlSocialDock::refreshChat()
{
	m_lineList.beginUpdate();
	Rml::String previous;
	for (const BuddyOverlayData::ChatLine &line : BuddyOverlayData::collectChatHistory( m_threadUserID ))
	{
		LineModel &m = m_lineList.next();
		m.isNote = line.isNote;
		if (line.isNote)
			m.body = unicodeToUtf8( line.text );
		else
			splitChatLine( unicodeToUtf8( line.text ), m.name, m.body );
		m.mine = !m.name.empty() && m.name != m_model.threadName;
		m.cont = !m.name.empty() && m.name == previous;
		previous = m.name;
	}
	m_lineList.endUpdate();
	dirty("chat_lines");
}

void RmlSocialDock::openThread(int64_t userID)
{
	m_threadUserID = userID;
	m_localUnread.erase(userID);
	m_model.view = "thread";
	m_model.chatEntryText.clear();
	BuddyOverlayActions::selectFriend( userID ); // reading it clears the unread count, as selecting in the popup does
	refreshChat();
	refreshRoster();
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::onChatMessage( int64_t sourceUserID, int64_t targetUserID, const UnicodeString & )
{
	const int64_t localUserID = BuddyOverlayData::getLocalUserID();
	const bool incoming = sourceUserID != localUserID;
	const int64_t other = incoming ? sourceUserID : targetUserID;
	if (incoming && m_toastJustShown)
		m_toastUserID = sourceUserID;

	if (other == m_threadUserID)
	{
		refreshChat();
		if (isExpanded() && m_model.view == "thread")
		{
			BuddyOverlayActions::selectFriend( other );
			return;
		}
	}

	// While the dock is the visible UI the social interface does not count this one; the badge does.
	if (incoming && m_sessionVisible)
		++m_localUnread[other];
}

void RmlSocialDock::onToastShown()
{
	m_toastUserID = 0;
	m_toastJustShown = true;
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::onToggle(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_model.locked)
		return;
	BuddyOverlayActions::open(); // GameSpyOpenOverlay(GSOVERLAY_BUDDY): the open sound, then expand() via the registry
}

void RmlSocialDock::onCollapse(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	BuddyOverlayActions::close();
}

void RmlSocialDock::onShowList(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	m_model.view = "list";
	refreshRoster();
}

void RmlSocialDock::onShowBlocked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	m_model.view = "blocked";
	dirty("view");
	refreshBlockList(); // async, like the popup's block tab
}

const BuddyOverlayData::BuddyRow *RmlSocialDock::rowAt(const Rml::VariantList &args) const
{
	const int index = argIndex(args);
	return (index >= 0 && index < (int)m_rawRows.size()) ? &m_rawRows[index] : nullptr;
}

void RmlSocialDock::onOpenThread(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	const BuddyOverlayData::BuddyRow *row = rowAt(args);
	if (row && row->category == BuddyOverlayData::ROW_FRIEND)
		openThread(row->userID);
}

// Right click: the popup's per-row menu (Persona, add/remove, block/unblock, accept/deny).
void RmlSocialDock::onRowMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (ev.GetParameter<int>("button", 0) != 1)
		return;
	const BuddyOverlayData::BuddyRow *row = rowAt(args);
	if (!row)
		return;

	OnlineLobbyData::PlayerRow target;
	target.userID = row->userID;
	target.displayName = row->displayName;
	target.isFriend = row->category == BuddyOverlayData::ROW_FRIEND;
	target.isPendingRequest = row->category == BuddyOverlayData::ROW_REQUEST;
	target.isIgnored = BuddyOverlayActions::isIgnored( row->userID );
	openContextMenu(target, ev);
}

void RmlSocialDock::onAccept(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (const BuddyOverlayData::BuddyRow *row = rowAt(args))
	{
		BuddyOverlayActions::acceptRequest( row->userID );
		BuddyOverlayActions::refreshFriendsList( false, []() { RmlSocialDock::instance().m_rosterDirty = true; } );
	}
}

void RmlSocialDock::onDeny(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (const BuddyOverlayData::BuddyRow *row = rowAt(args))
	{
		BuddyOverlayActions::rejectRequest( row->userID );
		BuddyOverlayActions::refreshFriendsList( false, []() { RmlSocialDock::instance().m_rosterDirty = true; } );
	}
}

void RmlSocialDock::onAdd(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (const BuddyOverlayData::BuddyRow *row = rowAt(args))
		BuddyOverlayActions::addFriend( row->userID, row->displayName );
}

void RmlSocialDock::onBlockedRowMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (ev.GetParameter<int>("button", 0) != 1)
		return;
	const int index = argIndex(args);
	if (index < 0 || index >= (int)m_rawBlocked.size())
		return;

	// Same RCNonBuddiesMenu.wnd shape the popup's block list uses (see RmlBuddyOverlayScreen.cpp).
	OnlineLobbyData::PlayerRow target;
	target.userID = m_rawBlocked[index].userID;
	target.displayName = m_rawBlocked[index].displayName;
	target.isIgnored = true;
	openContextMenu(target, ev);
}

void RmlSocialDock::onUnblock(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	const int index = argIndex(args);
	if (index < 0 || index >= (int)m_rawBlocked.size())
		return;
	BuddyOverlayActions::toggleIgnore( m_rawBlocked[index].userID );
	refreshBlockList();
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::sendChat()
{
	if (m_threadUserID == 0)
		return;
	if (BuddyOverlayActions::sendChatMessage( m_threadUserID, utf8ToUnicode( m_model.chatEntryText ) ))
	{
		m_model.chatEntryText.clear();
		dirty("chat_entry_text");
		refreshChat();
	}
}

void RmlSocialDock::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	// change fires on every edit; only Enter (linebreak) sends
	if (ev.GetParameter<bool>("linebreak", false))
		sendChat();
}

void RmlSocialDock::onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	sendChat();
}

//-------------------------------------------------------------------------------------------------
void RmlSocialDock::openContextMenu(const OnlineLobbyData::PlayerRow &target, Rml::Event &ev)
{
	m_menuTarget = target;

	const std::vector<OnlineLobbyData::PlayerMenuItem> items = OnlineLobbyData::buildPlayerContextMenu( m_menuTarget );
	m_menuList.beginUpdate();
	for (const OnlineLobbyData::PlayerMenuItem &item : items)
	{
		MenuItemModel &menuItem = m_menuList.next();
		menuItem.action = (int)item.action;
		if (item.action == OnlineLobbyData::PLAYERMENU_TOGGLE_IGNORE)
			menuItem.label = m_menuTarget.isIgnored ? "Unblock" : "Block"; // the popup's own literals
		else
			menuItem.label = fetchUtf8(item.labelKey.c_str());
	}
	m_menuList.endUpdate();

	// Clamped on screen against an estimated box, as in RmlBuddyOverlayScreen::openContextMenu().
	const float dpRatio = m_context ? ((float)m_context->GetDimensions().y / 1080.0f) : 1.0f;
	const float estWidth = 150.0f * dpRatio;
	const float estHeight = (float)items.size() * 32.0f * dpRatio + 10.0f * dpRatio;
	float left = (float)ev.GetParameter<int>("mouse_x", 0);
	float top = (float)ev.GetParameter<int>("mouse_y", 0);
	if (m_context)
	{
		const Rml::Vector2i size = m_context->GetDimensions();
		if (left + estWidth > (float)size.x)
			left = (float)size.x - estWidth;
		if (top + estHeight > (float)size.y)
			top = (float)size.y - estHeight;
	}
	if (left < 0.0f)
		left = 0.0f;
	if (top < 0.0f)
		top = 0.0f;

	char buf[32];
	snprintf(buf, sizeof(buf), "%dpx", (int)left);
	m_model.playerMenuXStyle = buf;
	snprintf(buf, sizeof(buf), "%dpx", (int)top);
	m_model.playerMenuYStyle = buf;
	m_model.playerMenuVisible = true;

	dirty("player_menu_items");
	dirty("player_menu_x_style");
	dirty("player_menu_y_style");
	dirty("player_menu_visible");
}

void RmlSocialDock::closeContextMenu()
{
	if (!m_model.playerMenuVisible)
		return;
	m_model.playerMenuVisible = false;
	dirty("player_menu_visible");
}

void RmlSocialDock::onMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const OnlineLobbyData::PlayerMenuAction action = (OnlineLobbyData::PlayerMenuAction)args[0].Get<int>();
	closeContextMenu();
	OnlineLobbyActions::performPlayerMenuAction( action, m_menuTarget );
	if (action != OnlineLobbyData::PLAYERMENU_STATS)
		BuddyOverlayActions::refreshFriendsList( false, []() { RmlSocialDock::instance().m_rosterDirty = true; } );
	if (m_model.view == "blocked")
		refreshBlockList();
}

void RmlSocialDock::onMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	closeContextMenu();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlCommunicator()
{
	RmlSocialDock &dock = RmlSocialDock::instance();
	if (dock.isAvailable())
		dock.expand();
	else
		OpenRmlBuddyOverlayScreen();
}

void CloseRmlCommunicator()
{
	RmlSocialDock &dock = RmlSocialDock::instance();
	if (dock.isExpanded())
		dock.collapse();
	if (RmlBuddyOverlayScreen::instance().isVisible())
		CloseRmlBuddyOverlayScreen();
}

bool RmlCommunicatorVisible()
{
	return RmlSocialDock::instance().isExpanded() || RmlBuddyOverlayScreen::instance().isVisible();
}

void RmlCommunicatorBack()
{
	RmlSocialDock &dock = RmlSocialDock::instance();
	if (dock.isExpanded())
		dock.back();
	else
		RmlBuddyOverlayScreen::instance().back();
}
