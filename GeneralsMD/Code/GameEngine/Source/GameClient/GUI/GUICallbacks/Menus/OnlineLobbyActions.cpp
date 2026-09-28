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

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"

#include "GameClient/WinInstanceData.h" // LobbyUtils.h's tooltip decls need this in scope
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

// Free functions with external linkage in WOLLobbyMenu.cpp (no shared header covers the whole
// .wnd menu callback file; same forward-declare precedent that file itself uses for e.g.
// refreshGameList()/refreshPlayerList()).
extern void refreshGameList( Bool forceRefresh );
extern void refreshPlayerList( Bool forceRefresh );
extern void ExitState();
extern Bool handleLobbySlashCommands( UnicodeString uText, Bool *wasRateLimited );
extern bool LobbyChatRateLimitAllowsSend();
extern void LobbyMenu_HostGamePressed();
extern void LobbyMenu_JoinLobbyByID( int64_t selectedID );

// LobbyUtils.cpp global (see its theLobbyFilter definition; WOLLobbyMenu.cpp forward-declares it
// the same way inside GCM_SELECTED).
extern LobbyGameModeFilter theLobbyFilter;

namespace OnlineLobbyActions
{

void hostGame()
{
	LobbyMenu_HostGamePressed();
}

void joinLobby( int64_t lobbyID )
{
	if ( lobbyID < 0 )
		return;
	LobbyMenu_JoinLobbyByID( lobbyID );
}

void refresh()
{
	refreshGameList( TRUE );
	refreshPlayerList( TRUE );
}

void back()
{
	ExitState();
}

void toggleBuddyOverlay()
{
	GameSpyToggleOverlay( GSOVERLAY_BUDDY );
}

void joinRoom( int roomIndex )
{
	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if ( pRoomsInterface == nullptr )
		return;

	const std::vector<NetworkRoom>& rooms = pRoomsInterface->GetGroupRooms();
	if ( roomIndex < 0 || roomIndex >= (int)rooms.size() || roomIndex == pRoomsInterface->GetCurrentRoomIndex() )
		return;

	theLobbyFilter = LOBBY_FILTER_ALL;
	pRoomsInterface->JoinRoom( roomIndex );
}

void setFilter( int filterValue )
{
	theLobbyFilter = (LobbyGameModeFilter)filterValue;
	refreshGameList( TRUE );
}

void toggleSortAge()
{
	SetGameSortType( GetGameSortType() == GAMESORT_AGE_ASCENDING ? GAMESORT_AGE_DESCENDING : GAMESORT_AGE_ASCENDING );
}

void toggleSortMap()
{
	SetGameSortType( GetGameSortType() == GAMESORT_MAP_ASCENDING ? GAMESORT_MAP_DESCENDING : GAMESORT_MAP_ASCENDING );
}

void toggleSortBuddies()
{
	SetSortByBuddies( !GetSortByBuddies() );
}

bool sendChatEntry( const UnicodeString &text )
{
	UnicodeString trimmed = text;
	trimmed.trim();
	if ( trimmed.isEmpty() )
		return true; // caller clears its field regardless, mirrors the .wnd's early-out

	Bool wasRateLimited = FALSE;
	if ( handleLobbySlashCommands( trimmed, &wasRateLimited ) )
	{
		return !wasRateLimited;
	}

	if ( !LobbyChatRateLimitAllowsSend() )
		return false;

	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if ( pWS != nullptr )
	{
		pWS->SendData_RoomChatMessage( trimmed, false );
	}
	return true;
}

bool sendChatButton( const UnicodeString &text )
{
	UnicodeString trimmed = text;
	trimmed.trim();
	if ( trimmed.isEmpty() )
		return true;

	if ( !LobbyChatRateLimitAllowsSend() )
		return false;

	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if ( pRoomsInterface != nullptr )
	{
		pRoomsInterface->SendChatMessageToCurrentRoom( trimmed, false );
	}
	return true;
}

} // namespace OnlineLobbyActions
