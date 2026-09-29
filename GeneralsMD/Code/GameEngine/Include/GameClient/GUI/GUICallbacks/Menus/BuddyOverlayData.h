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

// FILE: BuddyOverlayData.h ////////////////////////////////////////////////////
// Widget-agnostic Generals Online buddy overlay content (Menus/WOLBuddyOverlay.wnd):
// roster rows (current-lobby/recently-played/requests/friends), the selected
// friend's chat history, and the notification badge count. No GameWindow/gadget
// coupling, same role for this overlay as OnlineLobbyData.h has for the custom
// lobby.
//
// collectBuddyRows() reproduces updateBuddyInfo()'s GENERALS_ONLINE branch
// (WOLBuddyOverlay.cpp) as a pure function over the social interface's already-
// cached state: same four sections in the same order, same friends-list sort
// (unread-desc, then online-first), same name/status text and colour rules.
// collectChatHistory() reproduces GLM_SELECTED's chat-pane population, including
// its three hardcoded placeholder messages for a non-friend/pending-request/
// empty-chat selection.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameClient/Color.h"

#include <cstdint>
#include <string>
#include <vector>

struct BlockedResult; // GameNetwork/GeneralsOnline/OnlineServices_SocialInterface.h

namespace BuddyOverlayData
{
	// Which section of the roster a row came from (see updateBuddyInfo()'s GENERALS_ONLINE
	// branch, WOLBuddyOverlay.cpp:493-698, which rebuilds these four sections in this fixed
	// order every refresh).
	enum RowCategory
	{
		ROW_LOBBY_MEMBER = 0, // current-lobby non-friend (ITEM_NONBUDDY)
		ROW_RECENTLY_PLAYED,  // recently-played-with non-friend (ITEM_NONBUDDY)
		ROW_REQUEST,          // pending incoming friend request (ITEM_REQUEST)
		ROW_FRIEND,           // accepted friend (ITEM_BUDDY)
	};

	// What an online friend is doing, read from their presence text: the server sends free text
	// ("In Menus", "In Lobby ...", "In Game ..."), so anything unrecognised is plain online.
	enum Activity
	{
		ACTIVITY_OFFLINE = 0,
		ACTIVITY_ONLINE,
		ACTIVITY_IN_LOBBY,
		ACTIVITY_IN_GAME,
	};

	// One row of the roster listbox, covering all four sections.
	struct BuddyRow
	{
		int64_t userID = 0;
		std::string displayName; // utf8, no online/offline marker or unread-count suffix
		std::string statusText;  // "In Current Lobby" / "Recently Played With" / GUI:BuddyAddReq /
		                          // "Buddy:Online - <presence>" / "Buddy:Offline"
		Color nameColor = 0;
		RowCategory category = ROW_LOBBY_MEMBER;
		bool online = false;     // only meaningful for ROW_FRIEND
		int unreadCount = 0;     // only meaningful for ROW_FRIEND
		std::string presence;    // ROW_FRIEND: the server's presence text, empty while offline
		Activity activity = ACTIVITY_OFFLINE; // ROW_FRIEND: presence classified, see Activity
	};

	// Rebuilds all four sections from the social interface's cached/local state, same sort as
	// updateBuddyInfo(). Cheap/synchronous: reads GetRecentlyPlayedWithList()/GetCachedRequestsList()/
	// GetCachedFriendsList(), which are themselves populated by a prior GetFriendsList()/GetBlockList()
	// call (see BuddyOverlayActions::refreshFriendsList()). Empty if there is no social interface.
	std::vector<BuddyRow> collectBuddyRows();

	// One row of the block-list tab (ListboxIgnore equivalent, refreshIgnoreList()'s GO branch).
	struct BlockedRow
	{
		int64_t userID = 0;
		std::string displayName;
	};

	// Converts the async GetBlockList() result into rows for ListboxIgnore (refreshIgnoreList()'s
	// GO branch, 2033-2059). A pure function here so BuddyOverlayActions::refreshBlockList()'s
	// callback wrapper doesn't have to touch the NGMP type directly at the call site.
	std::vector<BlockedRow> buildBlockedRows( const BlockedResult &blockResult );

	// One chat line in the selected friend's history pane (ListboxBuddyChat equivalent).
	struct ChatLine
	{
		UnicodeString text;
		Color color = 0;
		bool isNote = false; // one of the placeholders below rather than a message
	};

	// Chat history for the given friend, or one placeholder line describing why chat isn't
	// available yet (not a friend, pending request, or empty history) -- same three placeholders
	// WOLBuddyOverlay.cpp's GLM_SELECTED handler shows (1300-1324). userID <= 0 (nothing selected)
	// returns the "Select a friend to start chatting" placeholder (202/342/1342).
	std::vector<ChatLine> collectChatHistory( int64_t userID );

	// Total notification badge (unread-chat senders + pending friend requests), same count
	// NGMP_OnlineServices_SocialInterface::GetNumTotalNotifications() already tracks.
	int getNotificationBadgeCount();

	// Logged in to Generals Online (the social interface exists).
	bool isOnline();

	// The local player's user ID, -1 when not logged in. Tells a received chat message from one
	// this client sent.
	int64_t getLocalUserID();
}
