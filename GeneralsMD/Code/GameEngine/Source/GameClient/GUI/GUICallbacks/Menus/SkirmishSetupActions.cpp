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

// FILE: SkirmishSetupActions.cpp //////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/SkirmishSetupActions.h"

#include "Common/GameEngine.h"
#include "Common/Money.h"
#include "Common/MultiplayerSettings.h"
#include "Common/RandomValue.h"
#include "Common/SkirmishBattleHonors.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"

Int SkirmishSetupActions::getNextSelectablePlayer( GameInfo *game, Int start )
{
	if( !game || !game->amIHost() )
		return -1;

	for( Int j = start; j < MAX_SLOTS; ++j )
	{
		GameSlot *slot = game->getSlot( j );
		if( slot && slot->getStartPos() == -1 && ( j == game->getLocalSlotNum() || slot->isAI() ) )
			return j;
	}
	return -1;
}

void SkirmishSetupActions::selectPlayerState( GameInfo *game, Int slotIndex, SlotState state, const UnicodeString &name )
{
	if( !game )
		return;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot )
		return;

	slot->setState( state, name );
}

Bool SkirmishSetupActions::selectColor( GameInfo *game, Int slotIndex, Int color )
{
	if( !game )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot )
		return FALSE;

	if( color == slot->getColor() )
		return FALSE;

	if( color < -1 || color >= TheMultiplayerSettings->getNumColors() )
		return FALSE;

	if( color != -1 )
	{
		for( Int i = 0; i < MAX_SLOTS; ++i )
		{
			GameSlot *checkSlot = game->getSlot( i );
			if( checkSlot && color == checkSlot->getColor() && slot != checkSlot )
				return FALSE;
		}
	}

	slot->setColor( color );
	return TRUE;
}

Bool SkirmishSetupActions::selectPlayerTemplate( GameInfo *game, Int slotIndex, Int playerTemplate )
{
	if( !game )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot )
		return FALSE;

	if( playerTemplate == slot->getPlayerTemplate() )
		return FALSE;

	slot->setPlayerTemplate( playerTemplate );
	return TRUE;
}

Bool SkirmishSetupActions::selectTeam( GameInfo *game, Int slotIndex, Int team )
{
	if( !game )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot )
		return FALSE;

	if( team == slot->getTeamNumber() )
		return FALSE;

	slot->setTeamNumber( team );
	return TRUE;
}

Bool SkirmishSetupActions::selectStartPosition( GameInfo *game, Int slotIndex, Int position )
{
	if( !game )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot )
		return FALSE;

	if( position == slot->getStartPos() )
		return FALSE;

	if( position < 0 )
	{
		slot->setStartPos( position );
		return TRUE;
	}

	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		if( i != slotIndex && game->getSlot( i )->getStartPos() == position )
			return FALSE;
	}

	slot->setStartPos( position );
	return TRUE;
}

void SkirmishSetupActions::handleStartPositionMarkerClick( GameInfo *game, Int position )
{
	if( !game )
		return;

	Int playerIdxInPos = -1;
	for( Int j = 0; j < MAX_SLOTS; ++j )
	{
		GameSlot *slot = game->getSlot( j );
		if( slot && slot->getStartPos() == position )
		{
			playerIdxInPos = j;
			break;
		}
	}

	if( playerIdxInPos >= 0 )
	{
		GameSlot *slot = game->getSlot( playerIdxInPos );
		if( playerIdxInPos == game->getLocalSlotNum() || ( game->amIHost() && slot && slot->isAI() ) )
		{
			// it's one of my type. Try to change it.
			Int nextPlayer = getNextSelectablePlayer( game, playerIdxInPos + 1 );
			selectStartPosition( game, playerIdxInPos, -1 );
			if( nextPlayer >= 0 )
				selectStartPosition( game, nextPlayer, position );
		}
	}
	else
	{
		// nobody in the slot - put us in
		Int nextPlayer = getNextSelectablePlayer( game, 0 );
		if( nextPlayer < 0 )
			nextPlayer = game->getLocalSlotNum();
		selectStartPosition( game, nextPlayer, position );
	}
}

void SkirmishSetupActions::setStartingCash( GameInfo *game, const Money &startingCash )
{
	if( game )
		game->setStartingCash( startingCash );
}

void SkirmishSetupActions::setSuperweaponRestriction( GameInfo *game, Bool restricted )
{
	if( game )
		game->setSuperweaponRestriction( restricted ? 1 : 0 );
}

SkirmishSetupActions::StartValidationResult SkirmishSetupActions::validateStart( GameInfo *game )
{
	if( !game )
		return STARTVALIDATION_MAP_NOT_FOUND;

	Int playerCount = game->getNumPlayers();
	AsciiString lowerMap = game->getMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find( lowerMap );
	if( it == TheMapCache->end() )
		return STARTVALIDATION_MAP_NOT_FOUND;

	if( playerCount > it->second.m_numPlayers )
		return STARTVALIDATION_TOO_MANY_PLAYERS;

	return STARTVALIDATION_READY;
}

void SkirmishSetupActions::startGame( GameInfo *game, Int maxFPS )
{
	if( !game )
		return;

	if( TheGameLogic->isInGame() )
		TheGameLogic->clearGameData( FALSE );

	TheWritableGlobalData->m_mapName = game->getMap();
	game->startGame( 0 );

	Bool isSkirmish = TRUE;
	const MapMetaData *md = TheMapCache->findMap( game->getMap() );
	if( md )
		isSkirmish = md->m_isMultiplayer; // we can now select solo campaign maps in Skirmish.

	if( isSkirmish )
	{
		InitRandom( game->getSeed() );

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument( GAME_SKIRMISH );
		msg->appendIntegerArgument( DIFFICULTY_NORMAL );	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument( 0 );									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument( maxFPS );							// FPS limit
	}
	else
	{
		InitRandom( 0 );

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument( GAME_SINGLE_PLAYER );
		msg->appendIntegerArgument( DIFFICULTY_NORMAL );	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument( 0 );									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument( maxFPS );							// FPS limit
	}
}

void SkirmishSetupActions::resetBattleHonors()
{
	SkirmishBattleHonors stats;
	stats.clear();
	stats.write();
}
