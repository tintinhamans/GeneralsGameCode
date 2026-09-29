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

#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"

#include <set>

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
			options.m_factionOptions.push_back( option );
		}

		if( allowObservers )
		{
			GameSetupFactionOption observer;
			observer.m_playerTemplate = PLAYERTEMPLATE_OBSERVER;
			observer.m_displayName = TheGameText->fetch( "GUI:Observer" );
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
