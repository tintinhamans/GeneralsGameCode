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

// FILE: RmlSocialDock.h ////////////////////////////////////////////////////////
// The Generals Online social dock (Assets/UI/SocialDock.rml): the communicator as a
// persistent, collapsible side panel beside every online shell screen, where the
// .wnd has the WOLBuddyOverlay.wnd popup. Friends with their presence, incoming
// requests, a conversation per friend, unread badges, and the popup's actions
// (add, remove, accept/deny, block/unblock, player info).
//
// Where it shows: over the online screens that host it (kHostPaths in the .cpp:
// welcome, custom lobby, quick match, game setup) while one of them is the topmost
// RmlUi layer. A popup, a message box or any other screen on top hides it; a host
// that fell back to its .wnd never shows it (it is not an open RmlUi layer), so the
// communicator there stays the popup. Collapsed it is a tab in the ruler band;
// expanded, the host (body.dock-host) gets .dock-open and makes room for it.
// Expanded/collapsed, the view and the open conversation live here, so they carry
// across the online screens for the rest of the login.
//
// Lifetime: the data is BuddyOverlayData/BuddyOverlayActions, the events are
// BuddyOverlaySignals, same as RmlBuddyOverlayScreen. The first time it shows in a
// login it calls BuddyOverlaySession::attach(), which keeps the push callbacks up
// until TearDownGeneralsOnline's delayed teardown detaches them; it notices that
// and resets. While expanded over a host it is "the visible UI" for the social
// interface (BuddyOverlaySession::setVisible): realtime presence on, unread counting
// and message toasts off, exactly as for the open popup.
//
// Communicator routing: the "Menus/WOLBuddyOverlay.wnd" registry entry (what
// GameSpyOpenOverlay/CloseOverlay/IsOverlayOpen(GSOVERLAY_BUDDY) reach) goes to the
// dock while it is available and to RmlBuddyOverlayScreen otherwise (in game, over a
// .wnd host). So every communicator button, F5/Insert and the toast expand it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/Signal.h"
#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayData.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <map>
#include <vector>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlSocialDock
{
public:
	static RmlSocialDock &instance();

	void init(Rml::Context *context); ///< RmlUiManager::init(), RmlUi shell only
	void shutdown();
	void tick(); ///< every frame from RmlUiManager::update(): placement, session, host classes

	bool isAvailable() const { return m_shown; } ///< shown over a host screen right now
	bool isExpanded() const { return m_shown && m_expanded; }
	void expand();
	void collapse();
	void back(); ///< Escape: closes the context menu, else collapses

	void noteToastClicked(); ///< the next expand opens the conversation the last toast was about

private:
	RmlSocialDock() : m_rowList(m_model.rows), m_blockedList(m_model.blockedRows),
		m_lineList(m_model.chatLines), m_menuList(m_model.playerMenuItems) {}

	void load(Rml::Context *context);
	void attachSession();
	void resetForLogout();
	void applyHostClasses(bool reserve);
	void setSessionVisible(bool visible);

	void refreshRoster();
	void refreshBlockList();
	void refreshChat();
	void openThread(int64_t userID);
	void dirty(const char *name);

	// BuddyOverlaySignals/BuddyToastSignals targets.
	void onChatMessage(int64_t sourceUserID, int64_t targetUserID, const UnicodeString &text);
	void onToastShown();

	void onToggle(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCollapse(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onShowList(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onShowBlocked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onOpenThread(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onRowMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onAccept(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onDeny(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onAdd(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBlockedRowMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onUnblock(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); ///< Enter only
	void onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMenuItemClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMenuDismiss(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	void sendChat(Rml::Event &ev);
	const BuddyOverlayData::BuddyRow *rowAt(const Rml::VariantList &args) const;
	void openContextMenu(const OnlineLobbyData::PlayerRow &target, Rml::Event &ev);
	void closeContextMenu();

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	SignalConnections m_connections; // BuddyOverlaySignals and the hosts' communicator locks, init() to shutdown()

	struct RowModel
	{
		int index = 0;
		Rml::String name;
		Rml::String initial;
		Rml::String sub;
		int activity = 0; // BuddyOverlayData::Activity
		int unread = 0;
		bool isRequest = false;
		bool isFriend = false;
		bool isLobby = false;
		bool isRecent = false;
		bool isOpen = false;
		bool used = true;
	};

	struct BlockedModel
	{
		int index = 0;
		Rml::String name;
		Rml::String initial;
		bool used = true;
	};

	// One line of the conversation. A "[name] text" line is split so the rml can show whose it is;
	// anything else is all body.
	struct LineModel
	{
		Rml::String name;
		Rml::String body;
		bool mine = false;   // named, and not the friend this conversation is with
		bool cont = false;   // same sender as the line before
		bool isNote = false; // a BuddyOverlayData placeholder, not a message
		bool used = true;
	};

	struct MenuItemModel
	{
		Rml::String label;
		int action = 0; // OnlineLobbyData::PlayerMenuAction
		bool used = true;
	};

	struct Model
	{
		bool expanded = false;
		bool locked = false;
		bool hostTemplate = true;
		Rml::String view = "list"; // list / blocked / thread

		int onlineCount = 0;
		int badgeCount = 0;
		Rml::String onlineText;
		int requestCount = 0;
		int friendCount = 0;
		int lobbyCount = 0;
		int recentCount = 0;
		int blockedCount = 0;
		Rml::Vector<RowModel> rows;
		Rml::Vector<BlockedModel> blockedRows;

		Rml::String threadName;
		Rml::String threadInitial;
		Rml::String threadSub;
		int threadActivity = 0;
		int othersUnread = 0;
		Rml::Vector<LineModel> chatLines;
		Rml::String chatEntryText;

		bool playerMenuVisible = false;
		Rml::String playerMenuXStyle = "0px";
		Rml::String playerMenuYStyle = "0px";
		Rml::Vector<MenuItemModel> playerMenuItems;
	} m_model;

	RmlGrowOnlyList<RowModel> m_rowList;
	RmlGrowOnlyList<BlockedModel> m_blockedList;
	RmlGrowOnlyList<LineModel> m_lineList;
	RmlGrowOnlyList<MenuItemModel> m_menuList;

	std::vector<BuddyOverlayData::BuddyRow> m_rawRows;
	std::vector<BuddyOverlayData::BlockedRow> m_rawBlocked;
	OnlineLobbyData::PlayerRow m_menuTarget;

	// Messages that came in while the dock was the visible UI but not showing their conversation: the
	// social interface does not count those (see BuddyOverlaySession::setVisible), so the badges do.
	std::map<int64_t, int> m_localUnread;

	bool m_expanded = false;   // wanted state, kept while hidden (in game, under a popup)
	bool m_shown = false;      // document up over a host
	bool m_attached = false;   // BuddyOverlaySession::attach() done for this login
	bool m_sessionVisible = false;
	bool m_rosterDirty = false;
	bool m_openLatest = false; // noteToastClicked()
	bool m_reserved = false;
	int64_t m_threadUserID = 0;
	// The toast on screen was a message from this friend (0: a request, a presence change, ...). The
	// social interface shows the toast and then reports the message, in one call, so a message in the
	// same frame as a toast is what that toast is about.
	int64_t m_toastUserID = 0;
	bool m_toastJustShown = false;
	AsciiString m_host;        // host path the dock was last shown over
};

// "Menus/WOLBuddyOverlay.wnd" registry entry (RmlUiManager::init()): the dock where it is available,
// the RmlBuddyOverlayScreen popup everywhere else.
void OpenRmlCommunicator();
void CloseRmlCommunicator();
bool RmlCommunicatorVisible();
void RmlCommunicatorBack();
