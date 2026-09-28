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

#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

#include <algorithm>
#include <cctype>

namespace OnlineLobbyData
{

//-------------------------------------------------------------------------------------------------
GameRow buildGameRow( const LobbyEntry &lobby, bool hasBuddy, bool crcMismatch )
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
	row.mapDisplayName = lobby.map_name; // caller/view resolves TheMapCache display name if desired
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

	return row;
}

//-------------------------------------------------------------------------------------------------
// Moved verbatim from WOLLobbyMenu.cpp's (formerly static) CollectLobbyPlayerRows().
std::vector<PlayerRow> collectPlayerRows()
{
	std::vector<PlayerRow> outRows;

	NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pRoomsInterface == nullptr )
		return outRows;

	auto membersMap = pRoomsInterface->GetMembersListForCurrentRoom();
	outRows.reserve( membersMap.size() );

	for ( auto& [id, member] : membersMap )
	{
		PlayerRow row;
		row.userID = member.user_id;
		row.displayName = member.display_name;
		row.isAdmin = member.m_bIsAdmin ? true : false;
		row.isFriend = (pSocialInterface != nullptr && pSocialInterface->IsUserFriend(member.user_id));
		row.isIgnored = (pSocialInterface != nullptr && pSocialInterface->IsUserIgnored(member.user_id));

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

} // namespace OnlineLobbyData
