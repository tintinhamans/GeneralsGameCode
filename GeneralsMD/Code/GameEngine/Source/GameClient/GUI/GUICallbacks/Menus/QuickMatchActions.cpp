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

// FILE: QuickMatchActions.cpp /////////////////////////////////////////////////
// See QuickMatchActions.h. Extracted from WOLQuickMatchMenu.cpp's live
// GENERALS_ONLINE code path.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchActions.h"

#include "Common/GlobalData.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_MatchmakingInterface.h"

#include <format>

namespace QuickMatchActions
{

//-------------------------------------------------------------------------------------------------
QuickMatchData::PlaylistMapInfo getPlaylistMapInfo( Int playlistIndex )
{
	QuickMatchData::PlaylistMapInfo info;

	if( playlistIndex < 0 )
		return info;

	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface == nullptr )
		return info;

	PlaylistEntry plEntry = pMatchmakingInterface->GetCachedPlaylistFromIndex( playlistIndex );
	if( plEntry.PlaylistID == (uint16_t)-1 )
		return info;

	// take the min player count, we'll offer maps in that range, up to desired player count
	info.m_numPlayers = plEntry.MinPlayers;
	info.m_playlistID = plEntry.PlaylistID;
	info.m_minSelectedMaps = plEntry.MinSelectedMaps;

	for( const PlaylistMapEntry &mapEntry : plEntry.Maps )
	{
		// format into game format (Maps\\name\\name.map) for official EA maps or full path for custom maps
		std::string correctedMapPath;
		if( mapEntry.Custom )
		{
			correctedMapPath = std::format( "{}maps\\{}\\{}.map", TheGlobalData->getPath_UserData().str(), mapEntry.Path, mapEntry.Path );
		}
		else
		{
			correctedMapPath = std::format( "maps\\{}\\{}.map", mapEntry.Path, mapEntry.Path );
		}

		AsciiString mapPath = correctedMapPath.c_str();
		mapPath.toLower();
		info.m_mapPaths.push_back( mapPath );
	}

	info.m_valid = TRUE;
	return info;
}

//-------------------------------------------------------------------------------------------------
Bool startMatchmaking( UnsignedShort playlistID, const std::vector<Int> &selectedMapIndexes, Int minSelectedMaps, std::function<void( Bool )> onComplete )
{
	if( static_cast<Int>( selectedMapIndexes.size() ) < minSelectedMaps )
		return FALSE;

	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface == nullptr )
		return FALSE;

	std::vector<int> vecSelectedMapIndexes( selectedMapIndexes.begin(), selectedMapIndexes.end() );
	pMatchmakingInterface->StartMatchmaking( playlistID, vecSelectedMapIndexes, [onComplete]( bool bSuccess )
		{
			if( onComplete )
				onComplete( bSuccess ? TRUE : FALSE );
		} );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void cancelMatchmaking()
{
	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface != nullptr )
		pMatchmakingInterface->CancelMatchmaking();
}

//-------------------------------------------------------------------------------------------------
void widenSearch()
{
	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface != nullptr )
		pMatchmakingInterface->WidenSearch();
}

//-------------------------------------------------------------------------------------------------
void toggleBuddiesOverlay()
{
	GameSpyToggleOverlay( GSOVERLAY_BUDDY );
}

} // namespace QuickMatchActions
