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

// FILE: RmlBuddyOverlayScreen.h /////////////////////////////////////////////////
// RmlScreen for Assets/UI/BuddyOverlay.rml -- the Generals Online buddy overlay
// (Menus/WOLBuddyOverlay.wnd / GSOVERLAY_BUDDY). Like RmlPlayerInfoScreen this is
// a popup, not a shell screen: it stays up OVER whatever's showing underneath
// instead of going through RmlUiManager::showScreen()'s single-current-screen
// swap, so it loads/shows/hides its own document directly.
//
// GameSpyOverlay.cpp's GameSpyOpenOverlay()/GameSpyCloseOverlay()/
// GameSpyIsOverlayOpen() route GSOVERLAY_BUDDY through
// RmlUiScreenRegistry::open/close("Menus/WOLBuddyOverlay.wnd") whenever it's
// registered, same isRegistered() gate GSOVERLAY_PLAYERINFO uses.
//
// This screen owns none of WOLBuddyOverlay.cpp's GameWindow lookups; it reads the
// same shared, widget-agnostic data BuddyOverlayData.h exposes (collectBuddyRows(),
// buildBlockedRows(), collectChatHistory()) and drives it through BuddyOverlayActions
// (send/select/add/remove/accept/reject/ignore/player-info/close) exactly like
// WOLBuddyOverlay.cpp's handlers did. Lifecycle (NGMP push-callback registration)
// goes through BuddyOverlaySession::enter()/leave(), same as WOLBuddyOverlayInit()/
// Shutdown() but without any GameWindow.
//
// The per-row right-click menu reuses the shared context-menu component (see
// RmlOnlineLobbyScreen.cpp) plus OnlineLobbyData::buildPlayerContextMenu()/
// OnlineLobbyActions::performPlayerMenuAction(), which already cover Stats/buddy
// toggle/ignore toggle/accept-request/deny-request -- everything RCBuddiesMenu.wnd/
// RCNonBuddiesMenu.wnd/RCBuddyRequestMenu.wnd did.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayData.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <vector>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlBuddyOverlayScreen
{
public:
	static RmlBuddyOverlayScreen &instance();

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as ButtonHide (KEY_ESC in WOLBuddyOverlay.cpp)

private:
	RmlBuddyOverlayScreen() : m_rosterRows(m_model.rosterRows), m_blockedRows(m_model.blockedRows),
		m_chatRows(m_model.chatLines), m_menuItemRows(m_model.playerMenuItems) {}

	// BuddyOverlaySignals targets, connected in open().
	void onChatMessage( int64_t sourceUserID, int64_t targetUserID, const UnicodeString &text );
	void onRosterNeedsRefresh( bool bIsAutoRefresh, bool bUseCache );

	void load(Rml::Context *context);

	void refreshRoster();
	void refreshBlockList();
	void refreshChat();

	void onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSetTab(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // args[0]: 0 = friends, 1 = block list
	void onRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors GLM_SELECTED
	void onRowMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors GLM_RIGHT_CLICKED (roster)
	void onBlockedRowMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // same, block list
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors GEM_EDIT_DONE
	void onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	void openContextMenu( const OnlineLobbyData::PlayerRow &target, Rml::Event &ev );
	void closeContextMenu();

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnections m_connections; // BuddyOverlaySignals, connected while open
	Rml::DataModelHandle m_modelHandle;

	// One row of the roster (see BuddyOverlayData::BuddyRow, which this mirrors 1:1 for RmlUi binding).
	struct RosterRowModel
	{
		int index = 0;
		Rml::String name;
		Rml::String statusText;
		Rml::String nameColor = "rgba(255,255,255,255)"; // "rgba(r,g,b,a)" CSS string, see colorToCss()
		bool isLobbyMember = false;
		bool isRecent = false;
		bool isRequest = false;
		bool isFriend = false;
		bool online = false;
		bool hasUnread = false;
		Rml::String unreadText;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h
	};

	// One row of the block-list tab (see BuddyOverlayData::BlockedRow).
	struct BlockedRowModel
	{
		int index = 0;
		Rml::String name;
		bool used = true;
	};

	// One line of the chat pane (see BuddyOverlayData::ChatLine).
	struct ChatLineModel
	{
		Rml::String text;
		Rml::String color = "rgba(255,255,255,255)";
		bool used = true;
	};

	// One entry of the roster row right-click menu (see OnlineLobbyData::buildPlayerContextMenu()).
	struct PlayerMenuItemModel
	{
		Rml::String label;
		int action = 0; // OnlineLobbyData::PlayerMenuAction
		bool used = true;
	};

	struct Model
	{
		bool showBlockTab = false;

		Rml::Vector<RosterRowModel> rosterRows;
		Rml::Vector<BlockedRowModel> blockedRows;

		bool hasChatTarget = false;
		Rml::String chatTargetName;
		Rml::Vector<ChatLineModel> chatLines;
		Rml::String chatEntryText;

		bool playerMenuVisible = false;
		Rml::String playerMenuXStyle = "0px";
		Rml::String playerMenuYStyle = "0px";
		Rml::Vector<PlayerMenuItemModel> playerMenuItems;
	} m_model;

	RmlGrowOnlyList<RosterRowModel> m_rosterRows;
	RmlGrowOnlyList<BlockedRowModel> m_blockedRows;
	RmlGrowOnlyList<ChatLineModel> m_chatRows;
	RmlGrowOnlyList<PlayerMenuItemModel> m_menuItemRows;

	// Snapshots of the last refreshRoster()/refreshBlockList() results, same order/index as
	// m_model.rosterRows/blockedRows, so onRowMouseDown()/onMenuItemClicked() can reach a row's
	// userID/displayName without exposing those to RmlUi (same pattern as RmlOnlineLobbyScreen's
	// m_rawPlayerRows).
	std::vector<BuddyOverlayData::BuddyRow> m_rawRosterRows;
	std::vector<BuddyOverlayData::BlockedRow> m_rawBlockedRows;

	int64_t m_selectedUserID = 0; // ListboxBuddies' selection equivalent; 0 = nothing selected
	OnlineLobbyData::PlayerRow m_menuTarget; // row the open context menu applies to
};

// Registry entry point (see RmlUiManager::init() / GameSpyOverlay.cpp).
void OpenRmlBuddyOverlayScreen();
void CloseRmlBuddyOverlayScreen();
