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

#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayActions.h"

#include "GameNetwork/GameSpy/PersistentStorageDefs.h" // SetLookAtPlayer()
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

// WOLBuddyOverlay.cpp free function: RequestBuddyAdd()'s GENERALS_ONLINE branch calls
// SocialInterface::AddFriend() plus the same "Invite Sent" notification sound/box the .wnd's
// ButtonAdd handler always has -- called through rather than duplicated so that stays intact,
// same reuse OnlineLobbyActions::performPlayerMenuAction's PLAYERMENU_TOGGLE_BUDDY case makes.
extern void RequestBuddyAdd( Int profileID, AsciiString nick );

namespace BuddyOverlayActions
{

void refreshFriendsList( bool bUseCache, std::function<void()> cb )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return;

	pSocialInterface->GetBlockList( nullptr ); // background cache refresh, no UI here
	pSocialInterface->GetFriendsList( bUseCache, cb );
}

void refreshBlockList( std::function<void( std::vector<BuddyOverlayData::BlockedRow> )> cb )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return;

	pSocialInterface->GetBlockList( [cb]( BlockedResult blockResult )
		{
			if ( cb )
				cb( BuddyOverlayData::buildBlockedRows( blockResult ) );
		} );
}

void selectFriend( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface != nullptr )
		pSocialInterface->ClearUnreadChatMessagesForUser( userID );
}

bool sendChatMessage( int64_t userID, const UnicodeString &text )
{
	// Block chatting with a buddy who is currently in the same game as us (GEM_EDIT_DONE, 309-325).
	if ( TheNGMPGame != nullptr && TheNGMPGame->isGameInProgress() )
	{
		for ( Int i = 0; i < MAX_SLOTS; ++i )
		{
			NGMPGameSlot *slot = TheNGMPGame->getGameSpySlot( i );
			if ( slot != nullptr && slot->isHuman() && slot->m_userID == userID )
				return false;
		}
	}

	UnicodeString trimmed = text;
	trimmed.trim();
	if ( trimmed.isEmpty() )
		return true;

	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if ( pWS != nullptr )
		pWS->SendData_FriendMessage( trimmed, userID );

	return true;
}

void addFriend( int64_t userID, const std::string &displayName )
{
	RequestBuddyAdd( (Int)userID, AsciiString( displayName.c_str() ) );
}

void removeFriend( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface != nullptr )
		pSocialInterface->RemoveFriend( userID );
}

void acceptRequest( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface != nullptr )
		pSocialInterface->AcceptPendingRequest( userID );
}

void rejectRequest( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface != nullptr )
		pSocialInterface->RejectPendingRequest( userID );
}

void toggleIgnore( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return;

	if ( pSocialInterface->IsUserIgnored( userID ) )
		pSocialInterface->UnignoreUser( userID );
	else
		pSocialInterface->IgnoreUser( userID );
}

bool isIgnored( int64_t userID )
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	return pSocialInterface != nullptr && pSocialInterface->IsUserIgnored( userID );
}

void openPlayerInfo( int64_t userID, const std::string &displayName )
{
	SetLookAtPlayer( userID, UnicodeString( from_utf8( displayName ).c_str() ) );
	GameSpyOpenOverlay( GSOVERLAY_PLAYERINFO );
}

void close()
{
	GameSpyCloseOverlay( GSOVERLAY_BUDDY );
}

void open()
{
	GameSpyOpenOverlay( GSOVERLAY_BUDDY );
}

} // namespace BuddyOverlayActions
