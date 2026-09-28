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

#include <cstdint>

namespace OnlineLobbyActions
{
	// Button handlers -----------------------------------------------------------------------
	void hostGame(); ///< mirrors ButtonHost
	void joinLobby( int64_t lobbyID ); ///< mirrors ButtonJoin with a row already selected
	void refresh(); ///< mirrors ButtonRefresh (forces both game and player list refresh)
	void back(); ///< mirrors ButtonBack (ExitState())
	void toggleBuddyOverlay(); ///< mirrors ButtonBuddy (GSOVERLAY_BUDDY, stays a .wnd overlay)

	// Room / filter combo ---------------------------------------------------------------------
	void joinRoom( int roomIndex ); ///< mirrors GCM_SELECTED's room-entry branch
	void setFilter( int filterValue ); ///< mirrors GCM_SELECTED's filter-entry branch (LobbyGameModeFilter)

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
