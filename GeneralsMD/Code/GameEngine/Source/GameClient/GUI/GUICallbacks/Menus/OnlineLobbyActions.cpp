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
#include "GameNetwork/GameSpy/PersistentStorageDefs.h" // SetLookAtPlayer()
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
extern UnicodeString FormatRoomLabel( const std::vector<NetworkRoom> &rooms, Int roomIndex ); // same tree-indent label the .wnd's PopulateLobbyFilterComboBox() uses

// WOLBuddyOverlay.cpp free function (RequestBuddyAdd()'s GENERALS_ONLINE branch calls
// SocialInterface::AddFriend() plus the same "Invite Sent" notification sound/box the .wnd's
// ButtonAdd handler always has -- called through rather than duplicated so that stays intact).
extern void RequestBuddyAdd( Int profileID, AsciiString nick );

// LobbyUtils.cpp global (see its theLobbyFilter definition; WOLLobbyMenu.cpp forward-declares it
// the same way inside GCM_SELECTED).
extern LobbyGameModeFilter theLobbyFilter;

// UnicodeString (UTF-16) -> UTF-8 std::string, same conversion RmlOnlineLobbyScreen.cpp's
// unicodeToUtf8() does; duplicated here rather than shared because that one lives in
// GameEngineDevice, which this GameEngine-layer file can't depend on.
static std::string unicodeToUtf8( const UnicodeString &str )
{
	const wchar_t *wide = (const wchar_t *)str.str();
	if ( wide == nullptr || wide[0] == 0 )
		return std::string();
	int len = ::WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
	if ( len <= 0 )
		return std::string();
	std::string utf8;
	utf8.resize( (size_t)len - 1 );
	::WideCharToMultiByte( CP_UTF8, 0, wide, -1, &utf8[0], len, nullptr, nullptr );
	return utf8;
}

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

int getFilterValue()
{
	return (int)theLobbyFilter;
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

std::vector<OnlineLobbyData::RoomInfo> getGroupRooms()
{
	std::vector<OnlineLobbyData::RoomInfo> result;
	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if ( pRoomsInterface == nullptr )
		return result;

	const std::vector<NetworkRoom>& rooms = pRoomsInterface->GetGroupRooms();
	const int currentIndex = pRoomsInterface->GetCurrentRoomIndex();
	result.reserve( rooms.size() );
	for ( int i = 0; i < (int)rooms.size(); ++i )
	{
		OnlineLobbyData::RoomInfo info;
		info.index = i;
		info.label = unicodeToUtf8( FormatRoomLabel( rooms, i ) );
		info.isCurrent = (i == currentIndex);
		result.push_back( info );
	}
	return result;
}

bool isPendingFullTeardown()
{
	auto pManager = NGMP_OnlineServicesManager::GetInstance();
	return pManager != nullptr && pManager->IsPendingFullTeardown();
}

void leaveCurrentLobby()
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if ( pLobbyInterface != nullptr )
		pLobbyInterface->LeaveCurrentLobby();
}

void performPlayerMenuAction( OnlineLobbyData::PlayerMenuAction action, const OnlineLobbyData::PlayerRow &player )
{
	AsciiString nick( player.displayName.c_str() );

	switch ( action )
	{
		case OnlineLobbyData::PLAYERMENU_STATS:
		{
			SetLookAtPlayer( player.userID, UnicodeString( from_utf8( player.displayName ).c_str() ) );
			GameSpyOpenOverlay( GSOVERLAY_PLAYERINFO );
			break;
		}
		case OnlineLobbyData::PLAYERMENU_TOGGLE_BUDDY:
		{
			NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
			if ( pSocialInterface == nullptr )
				break;
			if ( player.isFriend )
				pSocialInterface->RemoveFriend( player.userID );
			else
				RequestBuddyAdd( (Int)player.userID, nick );
			break;
		}
		case OnlineLobbyData::PLAYERMENU_TOGGLE_IGNORE:
		{
			NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
			if ( pSocialInterface == nullptr )
				break;
			if ( pSocialInterface->IsUserIgnored( player.userID ) )
				pSocialInterface->UnignoreUser( player.userID );
			else
				pSocialInterface->IgnoreUser( player.userID );
			break;
		}
	}
}

SortState getSortState()
{
	const GameSortType sortType = GetGameSortType();
	SortState state;
	state.sortByAge = (sortType == GAMESORT_AGE_ASCENDING || sortType == GAMESORT_AGE_DESCENDING);
	state.sortAgeDescending = (sortType == GAMESORT_AGE_DESCENDING);
	state.sortByMap = (sortType == GAMESORT_MAP_ASCENDING || sortType == GAMESORT_MAP_DESCENDING);
	state.sortMapDescending = (sortType == GAMESORT_MAP_DESCENDING);
	state.sortBuddiesFirst = GetSortByBuddies() != FALSE;
	return state;
}

void installScreenHooks()
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if ( pLobbyInterface == nullptr || pRoomsInterface == nullptr )
		return;

	pLobbyInterface->RegisterForCreateLobbyCallback( []( bool bSuccess )
		{
			if ( g_onlineLobbyCreateResultHook )
				g_onlineLobbyCreateResultHook( bSuccess );
		} );
	pLobbyInterface->RegisterForJoinLobbyCallback( []( EJoinLobbyResult result )
		{
			if ( g_onlineLobbyJoinResultHook )
				g_onlineLobbyJoinResultHook( (int)result );
		} );
	pRoomsInterface->RegisterForChatCallback( []( UnicodeString strMessage, Color color )
		{
			if ( g_onlineLobbyChatHook )
				g_onlineLobbyChatHook( strMessage, color );
		} );
	pRoomsInterface->RegisterForRosterNeedsRefreshCallback( []()
		{
			if ( g_onlineLobbyRosterRefreshHook )
				g_onlineLobbyRosterRefreshHook();
		} );
	pRoomsInterface->RegisterForRoomChangedCallback( []( int roomIndex, bool effectiveRoomChanged )
		{
			if ( g_onlineLobbyRoomChangedHook )
				g_onlineLobbyRoomChangedHook( roomIndex, effectiveRoomChanged );
		} );
}

} // namespace OnlineLobbyActions
