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

#include "GameClient/MapUtil.h"

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
	}
	else
	{
		options.m_mapFound = FALSE;
		options.m_mapDisplayName.translate( AsciiString( game->getMap().str() ) );
		options.m_mapIsMultiplayer = TRUE;
		options.m_mapNumPlayers = 0;
	}

	return data;
}
