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

#include "GameClient/GUI/GUICallbacks/Menus/OnlineLobbyData.h"

#include "GameClient/Image.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/RankPointValue.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace OnlineLobbySignals
{
	Signal1<const std::vector<OnlineLobbyData::GameRow> &> &gameList() { static Signal1<const std::vector<OnlineLobbyData::GameRow> &> s; return s; }
	Signal2<const UnicodeString &, Color> &chatLine() { static Signal2<const UnicodeString &, Color> s; return s; }
	Signal0 &rosterRefresh() { static Signal0 s; return s; }
	Signal2<int, bool> &roomChanged() { static Signal2<int, bool> s; return s; }
	Signal1<int> &joinResult() { static Signal1<int> s; return s; }
	Signal1<bool> &createResult() { static Signal1<bool> s; return s; }
	Signal1<bool> &roomListResult() { static Signal1<bool> s; return s; }
}

namespace OnlineLobbyData
{

//-------------------------------------------------------------------------------------------------
GameRow buildGameRow( const LobbyEntry &lobby, const std::string &mapDisplayName, bool hasBuddy, bool crcMismatch )
{
	GameRow row;
	row.lobbyID = lobby.lobbyID;

	std::string ownerName;
	for ( const LobbyMemberEntry &member : lobby.members )
	{
		if ( member.user_id == lobby.owner )
			ownerName = member.display_name;
	}

	row.displayName = lobby.name + " (" + ownerName + ")";
	row.mapDisplayName = mapDisplayName;
	row.hasPassword = lobby.passworded;
	row.allowObservers = lobby.allow_observers;
	row.trackStats = lobby.track_stats;
	row.hasBuddy = hasBuddy;
	row.crcMismatch = crcMismatch;

	char playersText[32];
	snprintf( playersText, sizeof(playersText), "%d/%d", lobby.current_players, lobby.max_players );
	row.playersText = playersText;

	const bool isFull = (lobby.current_players == lobby.max_players) || (lobby.current_players >= MAX_SLOTS);
	const bool isAlmostFull = !isFull && (lobby.max_players > 0)
		&& ((float)lobby.current_players / (float)lobby.max_players >= 0.6f);
	row.playersTier = isFull ? PLAYERCOUNT_FULL : isAlmostFull ? PLAYERCOUNT_ALMOST_FULL : PLAYERCOUNT_NORMAL;

	// Same buckets as insertGame()'s pingImages[0..2] selection.
	row.pingTier = (lobby.latency < 250) ? PING_GOOD : (lobby.latency < 500) ? PING_OK : PING_BAD;

	row.mapPath = lobby.map_path;
	row.latency = lobby.latency;
	row.startingCash = lobby.starting_cash;
	row.limitSuperweapons = lobby.limit_superweapons;
	if ( !ownerName.empty() )
		row.memberNames.push_back( ownerName );
	for ( const LobbyMemberEntry &member : lobby.members )
	{
		if ( member.IsHuman() && member.user_id != lobby.owner )
			row.memberNames.push_back( member.display_name );
	}

	return row;
}

//-------------------------------------------------------------------------------------------------
std::string rankImageForUser( int64_t userID )
{
	// Last resolved (rank points, favorite side) per user, so a cache miss doesn't blank the badge.
	static std::unordered_map<int64_t, std::pair<Int, Int>> s_lastKnown;

	Int rankPoints = 0;
	Int favoriteSide = 0;
	NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	PSPlayerStats stats = PSPlayerStats();
	if ( pStatsInterface != nullptr && pStatsInterface->getPlayerStatsFromCache( userID, &stats ) && stats.id != 0 )
	{
		rankPoints = CalculateRank( stats );
		favoriteSide = GetFavoriteSide( stats );
		s_lastKnown[userID] = std::make_pair( rankPoints, favoriteSide );
	}
	else
	{
		auto it = s_lastKnown.find( userID );
		if ( it != s_lastKnown.end() )
		{
			rankPoints = it->second.first;
			favoriteSide = it->second.second;
		}
	}

	const Image *image = LookupSmallRankImage( favoriteSide, rankPoints );
	return image ? std::string( image->getName().str() ) : std::string();
}

//-------------------------------------------------------------------------------------------------
static bool s_statsBatchInFlight = false;
static UnsignedInt s_statsBatchStartTime = 0;
static UnsignedInt s_statsBatchGeneration = 0; // bumped per lobby visit; stale responses are ignored
static std::unordered_set<int64_t> s_statsRequestedUserIDs;
static const UnsignedInt STATS_BATCH_WATCHDOG_MS = 30000; // recover from a lost response

void requestPlayerStats( const std::vector<int64_t> &userIDs )
{
	NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	if ( pStatsInterface == nullptr )
		return;

	const UnsignedInt now = timeGetTime();
	if ( s_statsBatchInFlight && (now - s_statsBatchStartTime) < STATS_BATCH_WATCHDOG_MS )
		return;

	std::vector<int64_t> toRequest;
	for ( int64_t userID : userIDs )
	{
		if ( userID <= 0 || s_statsRequestedUserIDs.count( userID ) != 0 || pStatsInterface->HasFreshPlayerStats( userID ) )
			continue;
		toRequest.push_back( userID );
	}
	if ( toRequest.empty() )
		return;

	s_statsRequestedUserIDs.insert( toRequest.begin(), toRequest.end() );
	s_statsBatchInFlight = true;
	s_statsBatchStartTime = now;
	const UnsignedInt generation = s_statsBatchGeneration;
	pStatsInterface->findPlayerStatsByBatch( toRequest, [generation]( bool /*bSuccess*/ )
		{
			if ( generation == s_statsBatchGeneration )
				s_statsBatchInFlight = false;
		} );
}

void resetPlayerStatsRequests()
{
	++s_statsBatchGeneration;
	s_statsBatchInFlight = false;
	s_statsRequestedUserIDs.clear();
}

//-------------------------------------------------------------------------------------------------
// Moved verbatim from WOLLobbyMenu.cpp's (formerly static) CollectLobbyPlayerRows().
std::vector<PlayerRow> collectPlayerRows()
{
	std::vector<PlayerRow> outRows;

	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	const int64_t localUserID = (pAuthInterface != nullptr) ? pAuthInterface->GetUserID() : 0;
	if ( pRoomsInterface == nullptr )
		return outRows;

	const auto& membersMap = pRoomsInterface->GetMembersListForCurrentRoom();
	outRows.reserve( membersMap.size() );

	for ( auto& [id, member] : membersMap )
	{
		PlayerRow row;
		row.userID = member.user_id;
		row.displayName = member.display_name;
		row.isAdmin = member.m_bIsAdmin ? true : false;
		row.isFriend = (pSocialInterface != nullptr && pSocialInterface->IsUserFriend(member.user_id));
		row.isIgnored = (pSocialInterface != nullptr && pSocialInterface->IsUserIgnored(member.user_id));
		row.isSelf = (member.user_id == localUserID);

		row.sortKey.resize(row.displayName.size());
		std::transform(row.displayName.begin(), row.displayName.end(), row.sortKey.begin(),
			[](unsigned char c) { return std::tolower(c); });

		outRows.emplace_back(std::move(row));
	}

	std::sort(outRows.begin(), outRows.end(),
		[](const PlayerRow& a, const PlayerRow& b) { return a.sortKey < b.sortKey; });

	auto afterAdmins = std::stable_partition(outRows.begin(), outRows.end(),
		[](const PlayerRow& x) { return x.isAdmin; });

	std::stable_partition(afterAdmins, outRows.end(),
		[](const PlayerRow& x) { return x.isFriend; });

	return outRows;
}

//-------------------------------------------------------------------------------------------------
// Moved verbatim from WOLLobbyMenu.cpp's (formerly static) BuildLobbyRosterSignature().
std::string buildRosterSignature( const std::vector<PlayerRow> &rows )
{
	std::string sig;
	sig.reserve(rows.size() * 24);
	for ( const PlayerRow &row : rows )
	{
		const int flags = (row.isAdmin ? 1 : 0) | (row.isFriend ? 2 : 0) | (row.isIgnored ? 4 : 0);
		sig += std::to_string(row.userID);
		sig += '/';
		sig += std::to_string(flags);
		sig += '/';
		sig += row.displayName;
		sig += ';';
	}
	return sig;
}

//-------------------------------------------------------------------------------------------------
std::vector<PlayerMenuItem> buildPlayerContextMenu( const PlayerRow &player )
{
	std::vector<PlayerMenuItem> items;

	if ( player.isSelf )
	{
		// RCLocalPlayerMenu.wnd: Persona only.
		items.push_back( { PLAYERMENU_STATS, "GUI:Stats" } );
		return items;
	}

	if ( player.userID <= 0 )
	{
		// RCNoProfileMenu.wnd: Ignore only.
		items.push_back( { PLAYERMENU_TOGGLE_IGNORE, "" } );
		return items;
	}

	if ( player.isPendingRequest )
	{
		// RCBuddyRequestMenu.wnd: Accept/Deny only (WOLBuddyOverlay.cpp's ITEM_REQUEST branch).
		items.push_back( { PLAYERMENU_ACCEPT_REQUEST, "GUI:AcceptSm" } );
		items.push_back( { PLAYERMENU_DENY_REQUEST, "GUI:Deny" } );
		return items;
	}

	// RCBuddiesMenu.wnd / RCNonBuddiesMenu.wnd: Persona, buddy toggle, Ignore, in that order.
	items.push_back( { PLAYERMENU_STATS, "GUI:Stats" } );
	items.push_back( { PLAYERMENU_TOGGLE_BUDDY, player.isFriend ? "GUI:Delete" : "GUI:Add" } );
	items.push_back( { PLAYERMENU_TOGGLE_IGNORE, "" } );
	return items;
}

} // namespace OnlineLobbyData
