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

// FILE: BuddyOverlayActions.h //////////////////////////////////////////////////
// Widget-agnostic Generals Online buddy overlay actions: select friend, send/
// receive chat, add/remove friend, accept/reject a pending request, block/
// unblock, open player info, close -- the exact validation and calls
// WOLBuddyOverlay.cpp's GBM_SELECTED/GEM_EDIT_DONE/RC-menu handlers have always
// used. Same "thin wrapper" precedent as OnlineLobbyActions.h.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayData.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace BuddyOverlayActions
{
	// Roster refresh ------------------------------------------------------------------------
	// Mirrors updateBuddyInfo()'s network trigger: a background GetBlockList(nullptr) cache
	// refresh plus GetFriendsList(bUseCache, cb). cb fires once the friends list is in; the
	// caller re-collects rows via BuddyOverlayData::collectBuddyRows() afterward -- same
	// "rebuild from cache" pattern updateBuddyInfo()'s lambda uses. No-op with no social interface.
	void refreshFriendsList( bool bUseCache, std::function<void()> cb );

	// Mirrors refreshIgnoreList()'s GetBlockList(cb) call, converting the result through
	// BuddyOverlayData::buildBlockedRows() before handing it back.
	void refreshBlockList( std::function<void( std::vector<BuddyOverlayData::BlockedRow> )> cb );

	// Selecting a friend row marks their chat read (mirrors GLM_SELECTED's
	// ClearUnreadChatMessagesForUser call after showing their history, 1327-1332).
	void selectFriend( int64_t userID );

	// TextEntryChat/GEM_EDIT_DONE: returns false (sends nothing) if userID is currently in the
	// local player's own game -- same block the .wnd's "You cannot send messages to a buddy who
	// is currently in your game" case applies (309-325) -- otherwise sends via
	// WebSocket::SendData_FriendMessage(). Empty/whitespace-only text is a no-op (returns true).
	bool sendChatMessage( int64_t userID, const UnicodeString &text );

	void addFriend( int64_t userID, const std::string &displayName ); ///< RequestBuddyAdd()
	void removeFriend( int64_t userID ); ///< RC ButtonDelete on a friend row
	void acceptRequest( int64_t userID ); ///< RCBuddyRequestMenu.wnd ButtonAdd ("ACCEPT")
	void rejectRequest( int64_t userID ); ///< RCBuddyRequestMenu.wnd ButtonDelete ("Reject")

	void toggleIgnore( int64_t userID ); ///< RC ButtonIgnore
	bool isIgnored( int64_t userID ); ///< drives the RC ButtonIgnore Block/Unblock label

	void openPlayerInfo( int64_t userID, const std::string &displayName ); ///< RC ButtonStats

	void close(); ///< ButtonHide -- mirrors GameSpyCloseOverlay(GSOVERLAY_BUDDY)
}
