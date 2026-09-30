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

// FILE: GameSetupData.cpp ////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/GameSetupData.h"

#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"

#include <algorithm>
#include <set>

UnsignedInt GetTeamUiColor(Int teamNumber);

GameSetupData GameSetupData::build( GameInfo *game, Bool allowObservers )
{
	GameSetupData data;

	if( !game )
		return data;

	Int localSlotNum = game->getLocalSlotNum();
	Bool amIHost = game->amIHost();

	data.m_slots.resize( MAX_SLOTS );
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( !slot )
			continue;

		GameSetupSlotRow &row = data.m_slots[i];
		row.m_state = slot->getState();
		row.m_name = slot->getName();
		row.m_color = slot->getColor();
		row.m_playerTemplate = slot->getPlayerTemplate();
		row.m_teamNumber = slot->getTeamNumber();
		row.m_startPosition = slot->getStartPos();
		row.m_isAI = slot->isAI();
		row.m_isLocalSlot = ( i == localSlotNum );
		row.m_canEdit = row.m_isLocalSlot || ( amIHost && slot->isAI() );
		row.m_canEditOccupant = amIHost && !row.m_isLocalSlot;

		// Same filter as PopulateColorComboBox().
		row.m_colorChoices.push_back( -1 );
		if( TheMultiplayerSettings && row.m_playerTemplate != PLAYERTEMPLATE_OBSERVER )
		{
			for( Int c = 0; c < TheMultiplayerSettings->getNumColors(); ++c )
			{
				Bool taken = FALSE;
				for( Int j = 0; j < MAX_SLOTS && !taken; ++j )
				{
					const GameSlot *other = game->getConstSlot( j );
					taken = ( j != i && other && other->getColor() == c );
				}
				if( !taken && TheMultiplayerSettings->getColor( c ) )
					row.m_colorChoices.push_back( c );
			}
		}
	}

	AsciiString lowerMap = game->getMap();
	lowerMap.toLower();
	GameSetupOptionsData &options = data.m_options;
	options.m_mapName = game->getMap();
	options.m_startingCash = game->getStartingCash();
	options.m_superweaponsRestricted = game->getSuperweaponRestriction() != 0;

	options.m_startPositionMarkers = computeStartPositionMarkers( game->getMap() );

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( game->getMap() ) : nullptr;

	// Same rule as updateMapStartSpots(): a spot shows the slot starting there, observers excepted,
	// tinted by team. A later slot on the same spot wins, as it overwrites the button text there.
	for( Int i = 0; md && i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( !slot )
			continue;

		const Int startPos = slot->getStartPos();
		if( startPos < 0 || startPos >= md->m_numPlayers || startPos >= MAX_SLOTS || slot->getPlayerTemplate() <= PLAYERTEMPLATE_MIN )
			continue;

		GameSetupStartPositionMarker &marker = options.m_startPositionMarkers[startPos];
		marker.m_occupantSlot = i;
		marker.m_occupantColor = ( slot->getTeamNumber() >= 0 ? GetTeamUiColor( slot->getTeamNumber() ) : 0xFFFFFF ) & 0xFFFFFF;
	}

	if( md )
	{
		options.m_mapFound = TRUE;
		options.m_mapDisplayName = md->m_displayName;
		options.m_mapIsMultiplayer = md->m_isMultiplayer;
		options.m_mapNumPlayers = md->m_numPlayers;
	}
	else
	{
		options.m_mapFound = FALSE;
		options.m_mapDisplayName.translate( AsciiString( game->getMap().str() ) );
		options.m_mapIsMultiplayer = TRUE;
		options.m_mapNumPlayers = 0;
	}

	// Same entries as PopulatePlayerTemplateComboBox().
	if( ThePlayerTemplateStore && TheGameText )
	{
		GameSetupFactionOption random;
		random.m_playerTemplate = PLAYERTEMPLATE_RANDOM;
		random.m_displayName = TheGameText->fetch( "GUI:Random" );
		options.m_factionOptions.push_back( random );

		std::set<AsciiString> seenSides;
		for( Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i )
		{
			const PlayerTemplate *tmpl = ThePlayerTemplateStore->getNthPlayerTemplate( i );
			if( !tmpl || tmpl->getStartingBuilding().isEmpty() )
				continue;

			if( game->oldFactionsOnly() && !tmpl->isOldFaction() )
				continue;

			const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName( tmpl->getName() ) : nullptr;
			if( general && !general->isStartingEnabled() )
				continue;

			AsciiString side;
			side.format( "SIDE:%s", tmpl->getSide().str() );
			if( !seenSides.insert( side ).second )
				continue;

			GameSetupFactionOption option;
			option.m_playerTemplate = i;
			option.m_displayName = TheGameText->fetch( side );
			const Image *icon = tmpl->getGeneralImage() ? tmpl->getGeneralImage() : tmpl->getSideIconImage();
			if( icon )
				option.m_iconImage = icon->getName();
			options.m_factionOptions.push_back( option );
		}

		if( allowObservers )
		{
			GameSetupFactionOption observer;
			observer.m_playerTemplate = PLAYERTEMPLATE_OBSERVER;
			observer.m_displayName = TheGameText->fetch( "GUI:Observer" );
			const PlayerTemplate *observerTemplate = ThePlayerTemplateStore->findPlayerTemplate( NAMEKEY( "FactionObserver" ) );
			if( observerTemplate && observerTemplate->getSideIconImage() )
				observer.m_iconImage = observerTemplate->getSideIconImage()->getName();
			options.m_factionOptions.push_back( observer );
		}
	}

	if( TheMultiplayerSettings && TheGameText )
	{
		GameSetupColorOption random;
		random.m_color = -1;
		MultiplayerColorDefinition *randomDef = TheMultiplayerSettings->getColor( PLAYERTEMPLATE_RANDOM );
		random.m_rgb = randomDef ? ( randomDef->getColor() & 0x00FFFFFF ) : 0x00FFFFFF;
		random.m_name = TheGameText->fetch( "GUI:Random" );
		options.m_colorOptions.push_back( random );

		for( Int i = 0; i < TheMultiplayerSettings->getNumColors(); ++i )
		{
			MultiplayerColorDefinition *colorDef = TheMultiplayerSettings->getColor( i );
			if( !colorDef )
				continue;

			GameSetupColorOption option;
			option.m_color = i;
			option.m_rgb = colorDef->getColor() & 0x00FFFFFF;

			// Same name lookup as PopulateColorComboBox().
			Bool found = FALSE;
			option.m_name = TheGameText->fetch( colorDef->getTooltipName().str(), &found );
			if( !found )
				option.m_name.format( L"%hs", colorDef->getTooltipName().str() );
			options.m_colorOptions.push_back( option );
		}
	}

	if( TheMultiplayerSettings )
	{
		const MultiplayerStartingMoneyList &moneyList = TheMultiplayerSettings->getStartingMoneyList();
		for( MultiplayerStartingMoneyList::const_iterator it = moneyList.begin(); it != moneyList.end(); ++it )
		{
			GameSetupStartingCashOption option;
			option.m_amount = it->countMoney();
			option.m_label = FormatStartingCashLabel( *it );
			options.m_startingCashOptions.push_back( option );
		}

		Bool listed = FALSE;
		for( size_t i = 0; i < options.m_startingCashOptions.size() && !listed; ++i )
			listed = options.m_startingCashOptions[i].m_amount == options.m_startingCash.countMoney();
		if( !listed && options.m_startingCash.countMoney() > 0 )
		{
			GameSetupStartingCashOption option;
			option.m_amount = options.m_startingCash.countMoney();
			option.m_label = FormatStartingCashLabel( options.m_startingCash );
			options.m_startingCashOptions.push_back( option );
		}
	}

	return data;
}

//-------------------------------------------------------------------------------------------------
std::vector<GameSetupStartPositionMarker> GameSetupData::computeStartPositionMarkers( AsciiString mapName )
{
	std::vector<GameSetupStartPositionMarker> markers( MAX_SLOTS );
	for( Int i = 0; i < MAX_SLOTS; ++i )
		markers[i].m_position = i;

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( mapName ) : nullptr;
	if( !md || !md->m_isMultiplayer )
		return markers;

	// Same fractional math as positionStartSpotControls(), minus its overlap nudge (see header).
	Real extentW = md->m_extent.hi.x - md->m_extent.lo.x;
	Real extentH = md->m_extent.hi.y - md->m_extent.lo.y;
	for( Int i = 0; i < md->m_numPlayers && i < MAX_SLOTS; ++i )
	{
		AsciiString waypointName;
		waypointName.format( "Player_%d_Start", i + 1 ); // 1-based, matches positionStartSpots()
		WaypointMap::const_iterator wmIt = md->m_waypoints.find( waypointName );
		if( wmIt == md->m_waypoints.end() )
			continue;

		GameSetupStartPositionMarker &marker = markers[i];
		marker.m_xFraction = extentW != 0.0f ? ( wmIt->second.x - md->m_extent.lo.x ) / extentW : 0.0f;
		marker.m_yFraction = extentH != 0.0f ? 1.0f - ( wmIt->second.y - md->m_extent.lo.y ) / extentH : 0.0f;
		marker.m_used = TRUE;
	}

	return markers;
}

//-------------------------------------------------------------------------------------------------
UnicodeString GameSetupData::startPositionTooltip( const GameSetupStartPositionMarker &marker )
{
	UnicodeString tooltip;
	if( !TheGameText )
		return tooltip;

	if( marker.m_occupantSlot < 0 )
		return TheGameText->fetch( "TOOLTIP:StartPosition" );

	tooltip.format( TheGameText->fetch( "TOOLTIP:StartPositionN" ), marker.m_occupantSlot + 1 );
	return tooltip;
}

//-------------------------------------------------------------------------------------------------
std::vector<GameSetupDisplayRow> GameSetupData::displayOrder( const std::vector<GameSetupSlotRow> &slots, const GameSetupOptionsData &options,
	Bool *teamMode, Int *unusedCount )
{
	const Int capacity = ( options.m_mapFound && options.m_mapNumPlayers > 0 ) ? options.m_mapNumPlayers : MAX_SLOTS;

	std::vector<GameSetupDisplayRow> rows;
	Bool teams = FALSE;
	for( Int i = 0; i < (Int)slots.size(); ++i )
	{
		const GameSetupSlotRow &slot = slots[i];
		const Bool occupied = slot.m_state != SLOT_OPEN && slot.m_state != SLOT_CLOSED;
		GameSetupDisplayRow row;
		row.m_slot = i;
		row.m_unused = !occupied && i >= capacity;
		row.m_groupTeam = occupied ? slot.m_teamNumber : -1;
		if( occupied && slot.m_teamNumber >= 0 )
			teams = TRUE;
		rows.push_back( row );
	}

	// Stable: slot order inside a group, like the .wnd list.
	auto rank = [teams]( const GameSetupDisplayRow &row ) -> Int
	{
		if( row.m_unused )
			return 100;
		if( !teams )
			return 0;
		return row.m_groupTeam >= 0 ? row.m_groupTeam : 50;
	};
	std::stable_sort( rows.begin(), rows.end(), [&rank]( const GameSetupDisplayRow &a, const GameSetupDisplayRow &b ) { return rank( a ) < rank( b ); } );

	Int unused = 0;
	for( size_t i = 0; i < rows.size(); ++i )
	{
		const Bool first = i == 0 || rank( rows[i] ) != rank( rows[i - 1] );
		if( rows[i].m_unused )
		{
			rows[i].m_foldHead = first;
			++unused;
		}
		else if( teams )
			rows[i].m_groupHead = first;
	}

	if( teamMode )
		*teamMode = teams;
	if( unusedCount )
		*unusedCount = unused;
	return rows;
}

//-------------------------------------------------------------------------------------------------
std::vector<UnicodeString> GameSetupData::startBlockers( GameInfo *game, Bool isNetwork )
{
	std::vector<UnicodeString> blockers;
	if( !game )
		return blockers;

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( game->getMap() ) : nullptr;

	if( !isNetwork )
	{
		if( !md )
			blockers.push_back( TheGameText->fetch( "GUI:CantFindMap" ) );
		else if( game->getNumPlayers() > md->m_numPlayers )
		{
			UnicodeString text;
			text.format( TheGameText->fetch( "GUI:TooManyPlayers" ), md->m_numPlayers );
			blockers.push_back( text );
		}
		return blockers;
	}

	Int numUsers = 0, numHumans = 0, numRandom = 0;
	std::set<Int> teams;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
		{
			if( slot->isHuman() )
				++numHumans;
			++numUsers;
			if( slot->getTeamNumber() >= 0 )
				teams.insert( slot->getTeamNumber() );
			else
				++numRandom;
		}
	}

	if( !md || md->m_numPlayers < numUsers )
	{
		UnicodeString text;
		text.format( TheGameText->fetch( "LAN:TooManyPlayers" ), md ? md->m_numPlayers : 0 );
		blockers.push_back( text );
	}
	if( TheGlobalData->m_netMinPlayers && !numHumans )
		blockers.push_back( TheGameText->fetch( "GUI:NeedHumanPlayers" ) );
	if( numUsers < TheGlobalData->m_netMinPlayers )
	{
		UnicodeString text;
		text.format( TheGameText->fetch( "LAN:NeedMorePlayers" ), numUsers );
		blockers.push_back( text );
	}
	else if( numRandom + (Int)teams.size() < TheGlobalData->m_netMinPlayers )
		blockers.push_back( TheGameText->fetch( "LAN:NeedMoreTeams" ) );

	UnicodeString mapDisplayName;
	Bool willTransfer = TRUE;
	if( md )
	{
		mapDisplayName.format( L"%ls", md->m_displayName.str() );
		willTransfer = !md->m_isOfficial;
	}
	else
	{
		mapDisplayName.translate( game->getMap().str() );
		willTransfer = WouldMapTransfer( game->getMap() );
	}

	Bool waiting = FALSE;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( !slot || !slot->isHuman() || slot->isAccepted() || i == game->getLocalSlotNum() )
			continue;
		waiting = TRUE;
		if( !slot->hasMap() && !willTransfer )
		{
			UnicodeString text;
			text.format( TheGameText->fetch( "GUI:PlayerNoMap" ), slot->getName().str(), mapDisplayName.str() );
			blockers.push_back( text );
		}
	}
	if( waiting )
		blockers.push_back( TheGameText->fetch( "GO:GUI:WaitingForAccepts" ) );

	return blockers;
}
