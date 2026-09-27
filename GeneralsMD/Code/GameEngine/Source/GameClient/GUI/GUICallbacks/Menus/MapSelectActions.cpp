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

// FILE: MapSelectActions.cpp //////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/MapSelectActions.h"

#include "Common/UserPreferences.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANAPICallbacks.h"

// See header. Mirrors SkirmishMapSelectMenuInit()'s usesSystemMapDir logic exactly.
Bool MapSelectActions::initialUsesSystemMaps( const AsciiString &currentMap )
{
	LANPreferences pref;
	Bool usesSystemMapDir = pref.usesSystemMapDir();

	const MapMetaData *mmd = TheMapCache ? TheMapCache->findMap( currentMap ) : nullptr;
	if( mmd )
		usesSystemMapDir = mmd->m_isOfficial;

	return usesSystemMapDir;
}

// See header. Mirrors SkirmishMapSelectMenu.cpp's ButtonOK handler exactly.
void MapSelectActions::applySelectedMap( GameInfo *game, const AsciiString &mapName )
{
	if( !game )
		return;

	game->setMap( mapName );

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( mapName ) : nullptr;
	if( !md )
	{
		game->setMapCRC( 0 );
		game->setMapSize( 0 );
	}
	else
	{
		game->setMapCRC( md->m_CRC );
		game->setMapSize( md->m_filesize );
	}

	for( Int i = 0; i < MAX_SLOTS; ++i )
		game->getSlot( i )->setStartPos( -1 );
}
