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

#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayData.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

#include <algorithm>
#include <unordered_set>

namespace BuddyOverlayData
{

// Presence is free text from the server; look for the two states the social dock shows apart.
static Activity classifyPresence( const std::string &presence )
{
	std::string lower = presence;
	std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return (char)tolower( c ); } );
	if ( lower.find( "lobby" ) != std::string::npos )
		return ACTIVITY_IN_LOBBY;
	if ( lower.find( "game" ) != std::string::npos || lower.find( "playing" ) != std::string::npos || lower.find( "match" ) != std::string::npos )
		return ACTIVITY_IN_GAME;
	return ACTIVITY_ONLINE;
}

//-------------------------------------------------------------------------------------------------
// Mirrors updateBuddyInfo()'s GENERALS_ONLINE branch (WOLBuddyOverlay.cpp:469-698), minus the
// listbox writes: same four sections in the same order, same skip rules, same friends-list sort.
std::vector<BuddyRow> collectBuddyRows()
{
	std::vector<BuddyRow> rows;

	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return rows;

	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	const int64_t localUserID = (pAuthInterface != nullptr) ? pAuthInterface->GetUserID() : -1;

	// CURRENT LOBBY (521-556): non-friend members of the active game/QM lobby.
	std::unordered_set<int64_t> currentLobbyMembers;
	if ( TheNGMPGame != nullptr )
	{
		const bool bIsInQMLobby = !TheNGMPGame->isGameInProgress() && TheNGMPGame->isQMGame();
		for ( Int i = 0; i < MAX_SLOTS; ++i )
		{
			NGMPGameSlot *slot = TheNGMPGame->getGameSpySlot( i );
			if ( slot == nullptr || !slot->isHuman() )
				continue;

			const int64_t profileID = slot->m_userID;
			if ( profileID == localUserID )
				continue;
			if ( pSocialInterface->IsUserFriend( profileID ) || bIsInQMLobby )
				continue;

			currentLobbyMembers.insert( profileID );

			BuddyRow row;
			row.userID = profileID;
			row.displayName = unicodeToUtf8( slot->getName() );
			row.statusText = unicodeToUtf8( TheGameText->fetch( "GUI:GODockLobby" ) );
			row.nameColor = GameSpyColor[GSCOLOR_CHAT_EMOTE];
			row.category = ROW_LOBBY_MEMBER;
			rows.push_back( row );
		}
	}

	// RECENTLY PLAYED WITH (559-587): skip anyone already shown above, already a friend, or
	// already a pending request (the request row below covers them).
	for ( auto &kvPair : pSocialInterface->GetRecentlyPlayedWithList() )
	{
		const FriendsEntry &friendsEntry = kvPair.second;
		const int64_t profileID = friendsEntry.user_id;
		if ( currentLobbyMembers.contains( profileID ) )
			continue;
		if ( pSocialInterface->IsUserFriend( profileID ) || pSocialInterface->IsUserPendingRequest( profileID ) )
			continue;

		BuddyRow row;
		row.userID = profileID;
		row.displayName = friendsEntry.display_name;
		row.statusText = unicodeToUtf8( TheGameText->fetch( "GUI:GODockRecent" ) );
		row.nameColor = GameSpyColor[GSCOLOR_CHAT_EMOTE];
		row.category = ROW_RECENTLY_PLAYED;
		rows.push_back( row );
	}

	// REQUESTS (590-609).
	const UnicodeString requestStatus = TheGameText->fetch( "GUI:BuddyAddReq" );
	for ( auto &kvPair : pSocialInterface->GetCachedRequestsList() )
	{
		const FriendsEntry &friendsEntry = kvPair.second;

		BuddyRow row;
		row.userID = friendsEntry.user_id;
		row.displayName = friendsEntry.display_name;
		row.statusText = unicodeToUtf8( requestStatus );
		row.nameColor = GameSpyColor[GSCOLOR_DEFAULT];
		row.category = ROW_REQUEST;
		rows.push_back( row );
	}

	// FRIENDS (612-686), sorted unread-count desc then online-first, same as updateBuddyInfo().
	auto friendsMap = pSocialInterface->GetCachedFriendsList();
	std::vector<std::pair<int64_t, FriendsEntry>> sortedFriends( friendsMap.begin(), friendsMap.end() );
	std::stable_sort( sortedFriends.begin(), sortedFriends.end(),
		[&]( auto &a, auto &b )
		{
			const Int unreadA = pSocialInterface->GetNumberUnreadChatMessagesForUser( a.second.user_id );
			const Int unreadB = pSocialInterface->GetNumberUnreadChatMessagesForUser( b.second.user_id );
			if ( unreadA != unreadB )
				return unreadA > unreadB;
			return a.second.online > b.second.online;
		} );

	const UnicodeString onlineStatus = TheGameText->fetch( "Buddy:Online" );
	const UnicodeString offlineStatus = TheGameText->fetch( "Buddy:Offline" );
	for ( auto &kvPair : sortedFriends )
	{
		const FriendsEntry &friendsEntry = kvPair.second;

		BuddyRow row;
		row.userID = friendsEntry.user_id;
		row.displayName = friendsEntry.display_name;
		row.online = friendsEntry.online;
		row.unreadCount = pSocialInterface->GetNumberUnreadChatMessagesForUser( friendsEntry.user_id );
		row.category = ROW_FRIEND;
		row.nameColor = friendsEntry.online ? GameSpyColor[GSCOLOR_PLAYER_BUDDY] : GameMakeColor( 100, 130, 150, 255 );
		if ( friendsEntry.online )
		{
			row.presence = friendsEntry.presence;
			row.activity = classifyPresence( friendsEntry.presence );
		}

		if ( friendsEntry.online )
		{
			UnicodeString formatStr;
			formatStr.format( L"%s - %hs", onlineStatus.str(), friendsEntry.presence.c_str() );
			row.statusText = unicodeToUtf8( formatStr );
		}
		else
		{
			row.statusText = unicodeToUtf8( offlineStatus );
		}

		rows.push_back( row );
	}

	return rows;
}

//-------------------------------------------------------------------------------------------------
std::vector<BlockedRow> buildBlockedRows( const BlockedResult &blockResult )
{
	std::vector<BlockedRow> rows;
	rows.reserve( blockResult.vecBlocked.size() );
	for ( const FriendsEntry &blockedEntry : blockResult.vecBlocked )
	{
		BlockedRow row;
		row.userID = blockedEntry.user_id;
		row.displayName = blockedEntry.display_name;
		rows.push_back( row );
	}
	return rows;
}

//-------------------------------------------------------------------------------------------------
// Mirrors GLM_SELECTED's chat-pane population (1276-1334): three hardcoded placeholders for a
// non-friend/pending-request/empty-chat selection, otherwise the cached message history.
std::vector<ChatLine> collectChatHistory( int64_t userID )
{
	std::vector<ChatLine> lines;

	if ( userID <= 0 )
	{
		lines.push_back( { TheGameText->fetch( "GUI:GOBuddySelectFriend" ), GameSpyColor[GSCOLOR_DEFAULT], true } );
		return lines;
	}

	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return lines;

	if ( !pSocialInterface->IsUserFriend( userID ) && !pSocialInterface->IsUserPendingRequest( userID ) )
	{
		lines.push_back( { TheGameText->fetch( "GUI:GOBuddyNotFriend" ), GameSpyColor[GSCOLOR_DEFAULT], true } );
		return lines;
	}

	if ( pSocialInterface->IsUserPendingRequest( userID ) )
	{
		lines.push_back( { TheGameText->fetch( "GUI:GOBuddyPending" ), GameSpyColor[GSCOLOR_DEFAULT], true } );
		return lines;
	}

	for ( const UnicodeString &line : pSocialInterface->GetChatMessagesForUser( userID ) )
		lines.push_back( { line, GameSpyColor[GSCOLOR_PLAYER_BUDDY], false } );

	if ( lines.empty() )
		lines.push_back( { TheGameText->fetch( "GUI:GOBuddyEmpty" ), GameSpyColor[GSCOLOR_DEFAULT], true } );

	return lines;
}

//-------------------------------------------------------------------------------------------------
int getNotificationBadgeCount()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	return (pSocialInterface != nullptr) ? pSocialInterface->GetNumTotalNotifications() : 0;
}

//-------------------------------------------------------------------------------------------------
bool isOnline()
{
	return NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>() != nullptr;
}

//-------------------------------------------------------------------------------------------------
int64_t getLocalUserID()
{
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	return (pAuthInterface != nullptr) ? pAuthInterface->GetUserID() : -1;
}

} // namespace BuddyOverlayData
