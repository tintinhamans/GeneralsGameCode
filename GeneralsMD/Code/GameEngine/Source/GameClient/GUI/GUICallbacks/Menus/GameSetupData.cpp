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
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"

GameSetupData GameSetupData::build( GameInfo *game )
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
		row.m_canEdit = amIHost || row.m_isLocalSlot;
	}

	AsciiString lowerMap = game->getMap();
	lowerMap.toLower();
	GameSetupOptionsData &options = data.m_options;
	options.m_mapName = game->getMap();
	options.m_startingCash = game->getStartingCash();
	options.m_superweaponsRestricted = game->getSuperweaponRestriction() != 0;

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( game->getMap() ) : nullptr;
	if( md )
	{
		options.m_mapFound = TRUE;
		options.m_mapDisplayName = md->m_displayName;
		options.m_mapIsMultiplayer = md->m_isMultiplayer;
		options.m_mapNumPlayers = md->m_numPlayers;

		// Same fractional math as positionStartSpotControls(), minus its overlap nudge (see header).
		if( md->m_isMultiplayer )
		{
			Real extentW = md->m_extent.hi.x - md->m_extent.lo.x;
			Real extentH = md->m_extent.hi.y - md->m_extent.lo.y;
			for( Int i = 0; i < md->m_numPlayers; ++i )
			{
				AsciiString waypointName;
				waypointName.format( "Player_%d_Start", i + 1 ); // 1-based, matches positionStartSpots()
				WaypointMap::const_iterator wmIt = md->m_waypoints.find( waypointName );
				if( wmIt == md->m_waypoints.end() )
					continue;

				GameSetupStartPositionMarker marker;
				marker.m_position = i;
				marker.m_xFraction = extentW != 0.0f ? ( wmIt->second.x - md->m_extent.lo.x ) / extentW : 0.0f;
				marker.m_yFraction = extentH != 0.0f ? 1.0f - ( wmIt->second.y - md->m_extent.lo.y ) / extentH : 0.0f;
				options.m_startPositionMarkers.push_back( marker );
			}
		}
	}
	else
	{
		options.m_mapFound = FALSE;
		options.m_mapDisplayName.translate( AsciiString( game->getMap().str() ) );
		options.m_mapIsMultiplayer = TRUE;
		options.m_mapNumPlayers = 0;
	}

	if( ThePlayerTemplateStore )
	{
		for( Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i )
		{
			const PlayerTemplate *tmpl = ThePlayerTemplateStore->getNthPlayerTemplate( i );
			if( !tmpl || !tmpl->isPlayableSide() )
				continue;

			GameSetupFactionOption option;
			option.m_playerTemplate = i;
			option.m_displayName = tmpl->getDisplayName();
			options.m_factionOptions.push_back( option );
		}
	}

	if( TheMultiplayerSettings )
	{
		for( Int i = 0; i < TheMultiplayerSettings->getNumColors(); ++i )
		{
			MultiplayerColorDefinition *colorDef = TheMultiplayerSettings->getColor( i );
			if( !colorDef )
				continue;

			GameSetupColorOption option;
			option.m_color = i;
			option.m_rgb = colorDef->getColor() & 0x00FFFFFF;
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
