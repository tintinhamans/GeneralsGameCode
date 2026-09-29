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

#include "W3DDevice/GameClient/RmlUi/RmlBuddyOverlayScreen.h"

#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlaySession.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"
// TheSuperHackers @fix RmlBuddyOverlayScreen: same GameEngineDevice/NGMP <windows.h> ordering
// concern RmlPlayerInfoScreen.cpp documents -- BuddyOverlayData.h/BuddyOverlayActions.h/
// OnlineLobbyData.h/OnlineLobbyActions.h are deliberately winsock-free, so they're safe to
// include here alongside <windows.h> below (needed for WideCharToMultiByte); nothing in this
// file touches GameNetwork/NGMP headers directly.
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>
#include <windows.h>

namespace
{
	// Same conversion as RmlPlayerInfoScreen.cpp/RmlOnlineLobbyScreen.cpp.
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

	// Same conversion as RmlOnlineLobbyScreen.cpp's utf8ToUnicode().
	UnicodeString utf8ToUnicode(const Rml::String &utf8)
	{
		AsciiString ascii(utf8.c_str());
		UnicodeString text;
		text.translate(ascii);
		return text;
	}

	// Same "rgba(r,g,b,a)" packing RmlOnlineLobbyScreen.cpp's colorToCss() uses.
	Rml::String colorToCss(Color color)
	{
		const int a = (color >> 24) & 0xFF;
		const int r = (color >> 16) & 0xFF;
		const int g = (color >> 8) & 0xFF;
		const int b = color & 0xFF;
		char buf[48];
		snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%d)", r, g, b, a);
		return Rml::String(buf);
	}
}

//-------------------------------------------------------------------------------------------------
RmlBuddyOverlayScreen &RmlBuddyOverlayScreen::instance()
{
	static RmlBuddyOverlayScreen s_screen;
	return s_screen;
}

void RmlBuddyOverlayScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("buddyoverlay");
	if (constructor)
	{
		Rml::StructHandle<RosterRowModel> rosterHandle = constructor.RegisterStruct<RosterRowModel>();
		if (rosterHandle)
		{
			rosterHandle.RegisterMember("index", &RosterRowModel::index);
			rosterHandle.RegisterMember("name", &RosterRowModel::name);
			rosterHandle.RegisterMember("status_text", &RosterRowModel::statusText);
			rosterHandle.RegisterMember("name_color", &RosterRowModel::nameColor);
			rosterHandle.RegisterMember("is_lobby_member", &RosterRowModel::isLobbyMember);
			rosterHandle.RegisterMember("is_recent", &RosterRowModel::isRecent);
			rosterHandle.RegisterMember("is_request", &RosterRowModel::isRequest);
			rosterHandle.RegisterMember("is_friend", &RosterRowModel::isFriend);
			rosterHandle.RegisterMember("online", &RosterRowModel::online);
			rosterHandle.RegisterMember("has_unread", &RosterRowModel::hasUnread);
			rosterHandle.RegisterMember("unread_text", &RosterRowModel::unreadText);
			rosterHandle.RegisterMember("used", &RosterRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<RosterRowModel>>();

		Rml::StructHandle<BlockedRowModel> blockedHandle = constructor.RegisterStruct<BlockedRowModel>();
		if (blockedHandle)
		{
			blockedHandle.RegisterMember("index", &BlockedRowModel::index);
			blockedHandle.RegisterMember("name", &BlockedRowModel::name);
			blockedHandle.RegisterMember("used", &BlockedRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<BlockedRowModel>>();

		Rml::StructHandle<ChatLineModel> chatHandle = constructor.RegisterStruct<ChatLineModel>();
		if (chatHandle)
		{
			chatHandle.RegisterMember("text", &ChatLineModel::text);
			chatHandle.RegisterMember("color", &ChatLineModel::color);
			chatHandle.RegisterMember("used", &ChatLineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<ChatLineModel>>();

		Rml::StructHandle<PlayerMenuItemModel> menuItemHandle = constructor.RegisterStruct<PlayerMenuItemModel>();
		if (menuItemHandle)
		{
			menuItemHandle.RegisterMember("label", &PlayerMenuItemModel::label);
			menuItemHandle.RegisterMember("action", &PlayerMenuItemModel::action);
			menuItemHandle.RegisterMember("used", &PlayerMenuItemModel::used);
		}
		constructor.RegisterArray<Rml::Vector<PlayerMenuItemModel>>();

		constructor.Bind("show_block_tab", &m_model.showBlockTab);
		constructor.Bind("roster_rows", &m_model.rosterRows);
		constructor.Bind("blocked_rows", &m_model.blockedRows);

		constructor.Bind("has_chat_target", &m_model.hasChatTarget);
		constructor.Bind("chat_target_name", &m_model.chatTargetName);
		constructor.Bind("chat_lines", &m_model.chatLines);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);

		constructor.Bind("player_menu_visible", &m_model.playerMenuVisible);
		constructor.Bind("player_menu_x_style", &m_model.playerMenuXStyle);
		constructor.Bind("player_menu_y_style", &m_model.playerMenuYStyle);
		constructor.Bind("player_menu_items", &m_model.playerMenuItems);

		constructor.BindEventCallback("close", &RmlBuddyOverlayScreen::onClose, this);
		constructor.BindEventCallback("set_tab", &RmlBuddyOverlayScreen::onSetTab, this);
		constructor.BindEventCallback("row_clicked", &RmlBuddyOverlayScreen::onRowClicked, this);
		constructor.BindEventCallback("row_mousedown", &RmlBuddyOverlayScreen::onRowMouseDown, this);
		constructor.BindEventCallback("blocked_row_mousedown", &RmlBuddyOverlayScreen::onBlockedRowMouseDown, this);
		constructor.BindEventCallback("chat_entry_committed", &RmlBuddyOverlayScreen::onChatEntryCommitted, this);
		constructor.BindEventCallback("send_chat", &RmlBuddyOverlayScreen::onSendChat, this);
		constructor.BindEventCallback("player_menu_item_clicked", &RmlBuddyOverlayScreen::onMenuItemClicked, this);
		constructor.BindEventCallback("player_menu_dismiss", &RmlBuddyOverlayScreen::onMenuDismiss, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/BuddyOverlay.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyOverlayScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	m_model.showBlockTab = false;
	m_selectedUserID = 0;
	m_model.hasChatTarget = false;
	m_model.chatTargetName.clear();
	m_model.playerMenuVisible = false;

	// BuddyOverlaySignals targets, same shape as WOLBuddyOverlay.cpp's ConnectBuddyOverlaySignals().
	m_connections.disconnect();
	m_connections.add( BuddyOverlaySignals::chatMessage().connect( [this]( int64_t sourceUserID, int64_t targetUserID, const UnicodeString &text ) { onChatMessage( sourceUserID, targetUserID, text ); } ) );
	m_connections.add( BuddyOverlaySignals::rosterNeedsRefresh().connect( [this]( bool bIsAutoRefresh, bool bUseCache ) { onRosterNeedsRefresh( bIsAutoRefresh, bUseCache ); } ) );
	BuddyOverlaySession::enter();

	refreshRoster();
	refreshChat();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_document->Show();
}

void RmlBuddyOverlayScreen::close()
{
	BuddyOverlaySession::leave();
	m_connections.disconnect();
	if (m_document)
		m_document->Hide();
}

bool RmlBuddyOverlayScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
// Mirrors updateBuddyInfo()'s GENERALS_ONLINE branch via BuddyOverlayData::collectBuddyRows() --
// same four sections/sort/colour rules, see BuddyOverlayData.h.
void RmlBuddyOverlayScreen::refreshRoster()
{
	m_rawRosterRows = BuddyOverlayData::collectBuddyRows();

	m_rosterRows.beginUpdate();
	int index = 0;
	for (const BuddyOverlayData::BuddyRow &row : m_rawRosterRows)
	{
		RosterRowModel &m = m_rosterRows.next();
		m.index = index++;
		m.name = row.displayName.c_str();
		m.statusText = row.statusText.c_str();
		m.nameColor = colorToCss(row.nameColor);
		m.isLobbyMember = row.category == BuddyOverlayData::ROW_LOBBY_MEMBER;
		m.isRecent = row.category == BuddyOverlayData::ROW_RECENTLY_PLAYED;
		m.isRequest = row.category == BuddyOverlayData::ROW_REQUEST;
		m.isFriend = row.category == BuddyOverlayData::ROW_FRIEND;
		m.online = row.online;
		m.hasUnread = row.unreadCount > 0;
		if (m.hasUnread)
		{
			char buf[16];
			snprintf(buf, sizeof(buf), "%d", row.unreadCount);
			m.unreadText = buf;
		}
	}
	m_rosterRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("roster_rows");
}

// Mirrors refreshIgnoreList()'s GetBlockList() async rebuild of ListboxIgnore.
void RmlBuddyOverlayScreen::refreshBlockList()
{
	BuddyOverlayActions::refreshBlockList( []( std::vector<BuddyOverlayData::BlockedRow> rows )
		{
			RmlBuddyOverlayScreen &screen = RmlBuddyOverlayScreen::instance();
			screen.m_rawBlockedRows = rows;

			screen.m_blockedRows.beginUpdate();
			int index = 0;
			for (const BuddyOverlayData::BlockedRow &row : screen.m_rawBlockedRows)
			{
				RmlBuddyOverlayScreen::BlockedRowModel &m = screen.m_blockedRows.next();
				m.index = index++;
				m.name = row.displayName.c_str();
			}
			screen.m_blockedRows.endUpdate();

			if (screen.m_modelHandle)
				screen.m_modelHandle.DirtyVariable("blocked_rows");
		} );
}

// Mirrors GLM_SELECTED's chat-pane population via BuddyOverlayData::collectChatHistory().
void RmlBuddyOverlayScreen::refreshChat()
{
	m_chatRows.beginUpdate();
	for (const BuddyOverlayData::ChatLine &line : BuddyOverlayData::collectChatHistory( m_selectedUserID ))
	{
		ChatLineModel &m = m_chatRows.next();
		m.text = unicodeToUtf8( line.text );
		m.color = colorToCss( line.color );
	}
	m_chatRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_lines");
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyOverlayScreen::onChatMessage( int64_t sourceUserID, int64_t targetUserID, const UnicodeString & )
{
	// Mirrors ConnectBuddyOverlaySignals()'s chatMessage listener: only touch the open chat pane if this
	// message belongs to the currently-selected friend, otherwise rely on the roster refresh
	// (unread count) BuddyOverlaySession fires right after this.
	if (m_selectedUserID != 0 && (sourceUserID == m_selectedUserID || targetUserID == m_selectedUserID))
		refreshChat();
}

void RmlBuddyOverlayScreen::onRosterNeedsRefresh( bool, bool )
{
	refreshRoster();
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyOverlayScreen::onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	BuddyOverlayActions::close();
}

void RmlBuddyOverlayScreen::onSetTab(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const bool wantBlockTab = args[0].Get<int>() != 0;
	if (wantBlockTab == m_model.showBlockTab)
		return;

	m_model.showBlockTab = wantBlockTab;
	if (wantBlockTab)
		refreshBlockList(); // async, same as RadioButtonIgnore's tab switch calling refreshIgnoreList()

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("show_block_tab");
}

// Mirrors GLM_SELECTED: selecting any roster row clears its unread count (only meaningful for
// friends) and rebuilds the chat pane, which itself shows the right non-friend/pending-request/
// empty-chat placeholder for the other categories (see BuddyOverlayData::collectChatHistory()).
void RmlBuddyOverlayScreen::onRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_rawRosterRows.size())
		return;

	m_selectedUserID = m_rawRosterRows[index].userID;
	m_model.hasChatTarget = true;
	m_model.chatTargetName = m_rawRosterRows[index].displayName.c_str();
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("has_chat_target");
		m_modelHandle.DirtyVariable("chat_target_name");
	}

	BuddyOverlayActions::selectFriend( m_selectedUserID );
	refreshChat();
	if (m_model.showBlockTab)
		return; // clearing unread doesn't change the block list

	refreshRoster(); // unread badge just cleared, same as GLM_SELECTED's implicit listbox refresh
}

//-------------------------------------------------------------------------------------------------
// Mirrors GLM_RIGHT_CLICKED (roster listbox variant): builds an OnlineLobbyData::PlayerRow from
// this BuddyRow's category so the shared buildPlayerContextMenu()/performPlayerMenuAction() cover
// Stats/buddy-toggle/ignore-toggle/accept/deny exactly like RCBuddiesMenu.wnd/RCNonBuddiesMenu.wnd/
// RCBuddyRequestMenu.wnd did.
void RmlBuddyOverlayScreen::onRowMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || ev.GetParameter<int>("button", 0) != 1)
		return;

	const int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_rawRosterRows.size())
		return;

	const BuddyOverlayData::BuddyRow &row = m_rawRosterRows[index];

	OnlineLobbyData::PlayerRow target;
	target.userID = row.userID;
	target.displayName = row.displayName;
	target.isFriend = row.category == BuddyOverlayData::ROW_FRIEND;
	target.isPendingRequest = row.category == BuddyOverlayData::ROW_REQUEST;
	target.isIgnored = BuddyOverlayActions::isIgnored( row.userID );

	openContextMenu( target, ev );
}

// Mirrors GLM_RIGHT_CLICKED (ignore listbox variant, WOLBuddyOverlay.cpp:1356-1395): a blocked
// row is always non-friend/non-request here (BuddyOverlayData exposes no friend/request lookup
// for it), so this always resolves to the RCNonBuddiesMenu.wnd shape (Stats/Add/Unblock) -- a
// documented simplification versus the .wnd's live IsUserFriend()/IsUserPendingRequest() re-check,
// since a blocked user can't simultaneously be an active friend or pending request in practice.
void RmlBuddyOverlayScreen::onBlockedRowMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || ev.GetParameter<int>("button", 0) != 1)
		return;

	const int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_rawBlockedRows.size())
		return;

	const BuddyOverlayData::BlockedRow &row = m_rawBlockedRows[index];

	OnlineLobbyData::PlayerRow target;
	target.userID = row.userID;
	target.displayName = row.displayName;
	target.isFriend = false;
	target.isPendingRequest = false;
	target.isIgnored = true;

	openContextMenu( target, ev );
}

void RmlBuddyOverlayScreen::openContextMenu( const OnlineLobbyData::PlayerRow &target, Rml::Event &ev )
{
	m_menuTarget = target;

	const std::vector<OnlineLobbyData::PlayerMenuItem> items = OnlineLobbyData::buildPlayerContextMenu( m_menuTarget );
	m_menuItemRows.beginUpdate();
	for (const OnlineLobbyData::PlayerMenuItem &item : items)
	{
		PlayerMenuItemModel &menuItem = m_menuItemRows.next();
		menuItem.action = (int)item.action;
		if (item.action == OnlineLobbyData::PLAYERMENU_TOGGLE_IGNORE)
		{
			// setUnignoreText()'s exact hardcoded (non-GUI:-key) literal, mirrored verbatim.
			menuItem.label = m_menuTarget.isIgnored ? "Unblock" : "Block";
		}
		else
		{
			menuItem.label = unicodeToUtf8(TheGameText->fetch(item.labelKey.c_str()));
		}
	}
	m_menuItemRows.endUpdate();

	// Synchronous on-screen clamp against an estimated menu box, same clamp GLM_RIGHT_CLICKED does
	// against TheDisplay's width/height -- this popup has no per-frame update() to defer to a
	// post-layout pass the way RmlOnlineLobbyScreen's clampPlayerMenu() does.
	const float rawX = (float)ev.GetParameter<int>("mouse_x", 0);
	const float rawY = (float)ev.GetParameter<int>("mouse_y", 0);
	const float dpRatio = m_context ? ((float)m_context->GetDimensions().y / 1080.0f) : 1.0f;
	const float estWidth = 150.0f * dpRatio;
	const float estHeight = (float)items.size() * 30.0f * dpRatio;
	const Rml::Vector2i contextSize = m_context ? m_context->GetDimensions() : Rml::Vector2i(rawX + estWidth, rawY + estHeight);

	float left = rawX;
	float top = rawY;
	if (left + estWidth > (float)contextSize.x)
		left = (float)contextSize.x - estWidth;
	if (top + estHeight > (float)contextSize.y)
		top = (float)contextSize.y - estHeight;
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

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("player_menu_items");
		m_modelHandle.DirtyVariable("player_menu_x_style");
		m_modelHandle.DirtyVariable("player_menu_y_style");
		m_modelHandle.DirtyVariable("player_menu_visible");
	}
}

void RmlBuddyOverlayScreen::closeContextMenu()
{
	if (!m_model.playerMenuVisible)
		return;
	m_model.playerMenuVisible = false;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("player_menu_visible");
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyOverlayScreen::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_selectedUserID == 0)
	{
		// Mirrors GEM_EDIT_DONE's nothing-selected case (Buddy:SelectBuddyToChat) -- no send target.
		return;
	}
	if (BuddyOverlayActions::sendChatMessage( m_selectedUserID, utf8ToUnicode( m_model.chatEntryText ) ))
	{
		m_model.chatEntryText.clear();
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("chat_entry_text");
		refreshChat();
	}
}

void RmlBuddyOverlayScreen::onSendChat(Rml::DataModelHandle handle, Rml::Event &ev, const Rml::VariantList &args)
{
	onChatEntryCommitted(handle, ev, args);
}

void RmlBuddyOverlayScreen::onMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const OnlineLobbyData::PlayerMenuAction action = (OnlineLobbyData::PlayerMenuAction)args[0].Get<int>();
	OnlineLobbyActions::performPlayerMenuAction( action, m_menuTarget );
	closeContextMenu();
	refreshRoster(); // reflect the new friend/ignored state immediately
	if (m_model.showBlockTab)
		refreshBlockList();
}

void RmlBuddyOverlayScreen::onMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	closeContextMenu();
}

void RmlBuddyOverlayScreen::back()
{
	BuddyOverlayActions::close();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlBuddyOverlayScreen()
{
	RmlBuddyOverlayScreen::instance().open();
}

void CloseRmlBuddyOverlayScreen()
{
	RmlBuddyOverlayScreen::instance().close();
}
