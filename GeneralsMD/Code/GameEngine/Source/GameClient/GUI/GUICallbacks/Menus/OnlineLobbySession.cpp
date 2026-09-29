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

// FILE: OnlineLobbySession.cpp ///////////////////////////////////////////////
// See OnlineLobbySession.h. Extracted from WOLLobbyMenu.cpp's live GENERALS_ONLINE
// code path.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbySession.h"

#include "Common/GameEngine.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

extern NGMPGame *TheNGMPGame;

// WOLLobbyMenu.cpp free functions with external linkage (same forward-declare precedent as
// OnlineLobbyActions.cpp).
extern void SetLobbyAttemptHostJoin( Bool start );
extern Bool IsLobbyAttemptHostJoin();
extern void ResetLobbyRefreshGates();
extern void refreshGameList( Bool forceRefresh );
extern void ExitState();

namespace OnlineLobbySession
{

namespace
{
	UnsignedInt s_generation = 0;
	Bool s_leaving = FALSE;
}

//-------------------------------------------------------------------------------------------------
void enter()
{
	const UnsignedInt generation = ++s_generation;
	s_leaving = FALSE;

	// for safety (and sanity)
	OnlineLobbyActions::leaveCurrentLobby();

	SetLobbyAttemptHostJoin( FALSE ); // not trying to host or join
	ResetLobbyRefreshGates();

	if( TheNGMPGame != nullptr )
	{
		TheNGMPGame->reset();
	}

	OnlineLobbyActions::registerNetworkCallbacks();

	// upon entry, retrieve room list
	NGMP_OnlineServices_RoomsInterface *pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if( pRoomsInterface != nullptr )
	{
		pRoomsInterface->GetRoomList( [=]( bool success )
			{
				if( generation != s_generation || s_leaving )
				{
					return;
				}

				NGMP_OnlineServices_RoomsInterface *pRooms = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
				if( pRooms == nullptr )
				{
					return;
				}

				if( !success || pRooms->GetGroupRooms().empty() )
				{
					OnlineLobbySignals::roomListResult().emit( false );
					return;
				}

				pRooms->JoinRoom( 0 );
				OnlineLobbySignals::roomListResult().emit( true );
			} );
	}
}

//-------------------------------------------------------------------------------------------------
void leave()
{
	++s_generation;
	s_leaving = TRUE;

	NGMP_OnlineServices_RoomsInterface *pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if( pRoomsInterface != nullptr )
	{
		pRoomsInterface->DeregisterForChatCallback();
		pRoomsInterface->DeregisterForRosterNeedsRefreshCallback();
		pRoomsInterface->DeregisterForRoomChangedCallback();
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->DeregisterForCreateLobbyCallback();
		pLobbyInterface->DeregisterForJoinLobbyCallback();
		pLobbyInterface->DeregisterForSearchForLobbiesCallback();
	}
}

//-------------------------------------------------------------------------------------------------
void markLeaving()
{
	++s_generation;
	s_leaving = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool handlePendingTeardown()
{
	NGMP_OnlineServicesManager *pManager = NGMP_OnlineServicesManager::GetInstance();
	if( pManager == nullptr || !pManager->IsPendingFullTeardown() )
	{
		return FALSE;
	}

	if( !IsLobbyAttemptHostJoin() )
	{
		ExitState();
		TearDownGeneralsOnline();
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void update()
{
	if( s_leaving )
	{
		return;
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr && pLobbyInterface->IsLobbyListDirty() && !pLobbyInterface->IsInLobby() && pLobbyInterface->GetLobbyTryingToJoin().lobbyID == -1 )
	{
		refreshGameList( FALSE );
	}
}

} // namespace OnlineLobbySession
