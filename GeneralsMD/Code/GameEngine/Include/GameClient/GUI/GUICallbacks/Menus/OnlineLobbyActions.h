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

// FILE: OnlineLobbyActions.h ///////////////////////////////////////////////////
// Widget-agnostic Generals Online custom-lobby actions: host/join/refresh/back/
// chat, with the exact validation and popup-opening WOLLobbyMenu.cpp's GBM_SELECTED/
// GEM_EDIT_DONE cases have always used. Everything here is a thin wrapper around
// free functions WOLLobbyMenu.cpp already exposes with global linkage (refreshGameList()/
// refreshPlayerList()/ExitState()/handleLobbySlashCommands()/LobbyChatRateLimitAllowsSend(),
// plus the LobbyMenu_HostGamePressed()/LobbyMenu_JoinLobbyByID() extracted from the
// button handlers) -- same "call it from the shared layer" precedent as LanLobbyActions.h,
// used where the underlying logic is too entangled with GameSpy overlays/legacy globals
// to duplicate safely. Host/Join still open GSOVERLAY_GAMEOPTIONS/GSOVERLAY_GAMEPASSWORD,
// which stay .wnd overlays regardless of which lobby screen (WOLCustomLobby.wnd or
// RmlOnlineLobbyScreen) is currently open.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"

#include <cstdint>
#include <vector>

namespace OnlineLobbyActions
{
	// Button handlers -----------------------------------------------------------------------
	void hostGame(); ///< mirrors ButtonHost
	void joinLobby( int64_t lobbyID ); ///< mirrors ButtonJoin with a row already selected
	void refresh(); ///< mirrors ButtonRefresh (forces both game and player list refresh)
	void back(); ///< mirrors ButtonBack (ExitState())
	void toggleBuddyOverlay(); ///< mirrors ButtonBuddy (GSOVERLAY_BUDDY, stays a .wnd overlay)

	// NGMP session accessors/registration -----------------------------------------------------
	// Everything below exists so a GameEngineDevice caller (RmlOnlineLobbyScreen) never has to
	// include a GeneralsOnline/NGMP header directly -- those pull winsock in a way that conflicts
	// with <windows.h> when both land in the same translation unit (see design notes). All types
	// crossing this boundary are plain (int/bool/std::string/std::vector of a plain struct).

	std::vector<OnlineLobbyData::RoomInfo> getGroupRooms(); ///< mirrors PopulateLobbyFilterComboBox()'s room section
	bool isPendingFullTeardown(); ///< mirrors NGMP_OnlineServicesManager::GetInstance()->IsPendingFullTeardown()
	void leaveCurrentLobby(); ///< mirrors WOLLobbyMenuInit()'s pLobbyInterface->LeaveCurrentLobby()

	struct SortState
	{
		bool sortByAge = false, sortAgeDescending = false;
		bool sortByMap = true, sortMapDescending = false;
		bool sortBuddiesFirst = true;
	};
	SortState getSortState();

	// Registers the NGMP push callbacks WOLLobbyMenuInit() installs (RegisterForChatCallback/
	// RegisterForRosterNeedsRefreshCallback/RegisterForRoomChangedCallback/RegisterForCreateLobbyCallback/
	// RegisterForJoinLobbyCallback), all forwarding into the plain g_onlineLobby*Hook function
	// pointers in OnlineLobbyData.h. Call once from a lobby screen's show(); these are single-slot
	// std::function members on the NGMP interfaces, so this simply preempts WOLLobbyMenuInit's own
	// registrations for as long as this screen -- not the .wnd one -- is open.
	void installScreenHooks();

	// Room / filter combo ---------------------------------------------------------------------
	void joinRoom( int roomIndex ); ///< mirrors GCM_SELECTED's room-entry branch
	void setFilter( int filterValue ); ///< mirrors GCM_SELECTED's filter-entry branch (LobbyGameModeFilter)
	int getFilterValue(); ///< current LobbyGameModeFilter, as a plain int (0=All..5=Buddies)

	// Sorting -----------------------------------------------------------------------------------
	void toggleSortAge(); ///< mirrors ButtonSortAlpha (HandleSortButton)
	void toggleSortMap(); ///< mirrors ButtonSortPing (HandleSortButton)
	void toggleSortBuddies(); ///< mirrors ButtonSortBuddies (HandleSortButton)

	// Chat --------------------------------------------------------------------------------------
	// Mirrors GEM_EDIT_DONE exactly: slash commands first, then rate limit + pWS->SendData_RoomChatMessage().
	// Returns true if the caller should clear its chat entry field (mirrors the .wnd's
	// GadgetTextEntrySetText(textEntryChat, Empty) calls).
	bool sendChatEntry( const UnicodeString &text );

	// Mirrors ButtonEmote exactly: despite the name, no slash-command handling and sends via
	// pRoomsInterface->SendChatMessageToCurrentRoom() (a different path than sendChatEntry()) --
	// same intentional-inconsistency precedent as LanLobbyActions::sendChatButton().
	bool sendChatButton( const UnicodeString &text );
}
