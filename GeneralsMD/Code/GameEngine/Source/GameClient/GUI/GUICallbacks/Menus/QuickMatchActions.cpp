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
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuickmatchPreferences.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Auth.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_MatchmakingInterface.h"

#include <format>
#include <set>

namespace QuickMatchActions
{

//-------------------------------------------------------------------------------------------------
UnicodeString buildTitle()
{
	UnicodeString title;

	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if( pAuthInterface != nullptr )
		title.format( TheGameText->fetch( "GUI:QuickMatchTitle" ), pAuthInterface->GetDisplayName().c_str() );

	return title;
}

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

//-------------------------------------------------------------------------------------------------
void retrievePlaylists( std::function<void( std::vector<QuickMatchData::PlaylistOption> )> onComplete )
{
	if( !onComplete )
		return;

	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface == nullptr )
		return;

	pMatchmakingInterface->RetrievePlaylists( [onComplete]( std::vector<PlaylistEntry> vecPlaylists )
		{
			std::vector<QuickMatchData::PlaylistOption> options;
			options.reserve( vecPlaylists.size() );
			Int index = 0;
			for( PlaylistEntry &playlist : vecPlaylists )
			{
				QuickMatchData::PlaylistOption option;
				option.index = index++;
				option.name.format( "%s", playlist.Name.c_str() );
				options.push_back( option );
			}
			onComplete( options );
		} );
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::MapOption> getMapSelectOptions( Int playlistIndex )
{
	std::vector<QuickMatchData::MapOption> maps;

	QuickMatchData::PlaylistMapInfo plMapInfo = getPlaylistMapInfo( playlistIndex );
	if( !plMapInfo.m_valid )
		return maps;

	QuickMatchPreferences pref;
	for( const AsciiString &mapPath : plMapInfo.m_mapPaths )
	{
		const MapMetaData *md = TheMapCache->findMap( mapPath );
		if( md == nullptr || md->m_numPlayers < plMapInfo.m_numPlayers )
			continue;

		QuickMatchData::MapOption option;
		option.mapPath = mapPath;
		option.displayName = md->m_displayName;
		option.initiallySelected = pref.isMapSelected( mapPath );
		maps.push_back( option );
	}

	return maps;
}

//-------------------------------------------------------------------------------------------------
void saveMapSelections( const std::vector<QuickMatchData::MapOption> &maps )
{
	QuickMatchPreferences pref;

	// Quick Match under GO never resolves a ladder (see getLadderInfo()/§1 of the quick match
	// triage note) -- clearing this matches saveQuickMatchOptions()'s GENERALS_ONLINE branch exactly.
	pref.setLastLadder( AsciiString::TheEmptyString, 0 );

	for( const QuickMatchData::MapOption &map : maps )
		pref.setMapSelected( map.mapPath, map.initiallySelected );

	pref.write();
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::ComboOption> getLadderOptions()
{
	// Mirrors WOLQuickMatchMenu.cpp:957-994's no-ladders-found branch -- the only one reachable under
	// GENERALS_ONLINE, since TheLadderList is always empty there.
	std::vector<QuickMatchData::ComboOption> options;

	QuickMatchData::ComboOption option;
	option.label = UnicodeString( L"Automatic Ladder" );
	option.value = 0;
	option.initiallySelected = TRUE;
	options.push_back( option );

	return options;
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::ComboOption> getMaxPingOptions()
{
	// Mirrors WOLQuickMatchMenu.cpp:1071-1091: maxPingEntries is 0 under GENERALS_ONLINE (ping
	// filtering isn't supported there), so the ms-step loop never runs and GUI:ANY is the only entry.
	std::vector<QuickMatchData::ComboOption> options;

	QuickMatchData::ComboOption option;
	option.label = TheGameText->fetch( "GUI:ANY" );
	option.value = 0;
	option.initiallySelected = TRUE;
	options.push_back( option );

	return options;
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::ComboOption> getMaxDisconnectsOptions( Int favMaxDisconnects )
{
	// Mirrors WOLQuickMatchMenu.cpp:1061-1069 exactly (this list isn't GO-specific).
	enum { MAX_DISCONNECTS_COUNT = 5 };
	static const Int MAX_DISCONNECTS[MAX_DISCONNECTS_COUNT] = { 0, 5, 10, 25, 50 };

	std::vector<QuickMatchData::ComboOption> options;

	QuickMatchData::ComboOption any;
	any.label = TheGameText->fetch( "GUI:Any" );
	any.value = MAX_DISCONNECTS[0];
	options.push_back( any );

	for( Int i = 1; i < MAX_DISCONNECTS_COUNT; ++i )
	{
		QuickMatchData::ComboOption option;
		option.label.format( L"%d", MAX_DISCONNECTS[i] );
		option.value = MAX_DISCONNECTS[i];
		options.push_back( option );
	}

	Int selectedIndex = max( 0, favMaxDisconnects );
	if( selectedIndex >= (Int)options.size() )
		selectedIndex = 0;
	options[selectedIndex].initiallySelected = TRUE;

	return options;
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::ComboOption> getSideOptions( Int favSide )
{
	// Mirrors populateQMSideComboBox() exactly (WOLQuickMatchMenu.cpp:340-399) with li == nullptr.
	std::vector<QuickMatchData::ComboOption> options;

	QuickMatchData::ComboOption random;
	random.label = TheGameText->fetch( "GUI:Random" );
	random.value = PLAYERTEMPLATE_RANDOM;
	options.push_back( random );

	std::set<AsciiString> seenSides;
	Int selectedIndex = 0; // select Random by default

	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	for( Int c = 0; c < numPlayerTemplates; ++c )
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate( c );
		if( !fac )
			continue;

		if( fac->getStartingBuilding().isEmpty() )
			continue;

		AsciiString side;
		side.format( "SIDE:%s", fac->getSide().str() );
		if( seenSides.find( side ) != seenSides.end() )
			continue;

		// Remove disallowed generals from the choice list, same as populateQMSideComboBox().
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName( fac->getName() );
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if( disallowLockedGenerals && startsLocked )
			continue;

		seenSides.insert( side );

		QuickMatchData::ComboOption option;
		option.label = TheGameText->fetch( side );
		option.value = c;
		options.push_back( option );

		if( c == favSide )
			selectedIndex = (Int)options.size() - 1;
	}

	options[selectedIndex].initiallySelected = TRUE;
	return options;
}

//-------------------------------------------------------------------------------------------------
std::vector<QuickMatchData::ComboOption> getColorOptions( Int favColor )
{
	// Mirrors populateQMColorComboBox() exactly (WOLQuickMatchMenu.cpp:314-336).
	std::vector<QuickMatchData::ComboOption> options;

	QuickMatchData::ComboOption random;
	random.label = TheGameText->fetch( "GUI:???" );
	random.value = -1;
	options.push_back( random );

	Int numColors = TheMultiplayerSettings->getNumColors();
	for( Int c = 0; c < numColors; ++c )
	{
		MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor( c );
		if( !def )
			continue;

		QuickMatchData::ComboOption option;
		option.label = TheGameText->fetch( def->getTooltipName().str() );
		option.value = c;
		options.push_back( option );
	}

	// populateQMColorComboBox() selects by combo POSITION (GadgetComboBoxSetSelectedPos(comboBoxColor,
	// pref.getColor())), not by itemData -- position 0 is "???"/random, so favColor==0 (the
	// QuickmatchPreferences default) shows Random, matching the .wnd exactly.
	Int selectedIndex = favColor;
	if( selectedIndex < 0 || selectedIndex >= (Int)options.size() )
		selectedIndex = 0;
	options[selectedIndex].initiallySelected = TRUE;
	return options;
}

} // namespace QuickMatchActions
