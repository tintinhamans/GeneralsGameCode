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

// FILE: OnlineGameSetupActions.cpp ///////////////////////////////////////////
// See OnlineGameSetupActions.h. getQR2HostingStatus()/isThreadHosting are legacy GameSpy
// globals WOLGameSetupMenu.cpp/WOLLobbyMenu.cpp/InGameChat.cpp already forward-declare the
// same way (no shared header covers them) -- /host is dead behind #if !defined(GENERALS_ONLINE)
// in the .wnd's own copy, but the GENERALS_ONLINE branch (the one that ships) still prints this
// diagnostic line, so it's ported here too.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupActions.h"

#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NextGenMP_defines.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

#include <cctype>
#include <cstdlib>
#include <set>

// Legacy GameSpy globals (see file comment above). getQR2HostingStatus() has C linkage -- match
// WOLGameSetupMenu.cpp's own extern "C" forward declaration or the linker can't find it.
extern "C" {
int getQR2HostingStatus();
}
extern int isThreadHosting;
#if defined(RTS_DEBUG)
extern Bool g_debugSlots;
#endif

namespace OnlineGameSetupActions
{

//-------------------------------------------------------------------------------------------------
Int getNextSelectablePlayer( NGMPGame *game, Int start )
{
	if( !game || !game->amIHost() )
		return -1;

	for( Int j = start; j < MAX_SLOTS; ++j )
	{
		NGMPGameSlot *slot = game->getGameSpySlot( j );
		if( slot && slot->getStartPos() == -1 &&
			( ( j == game->getLocalSlotNum() && game->getConstSlot( j )->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			|| slot->isAI() ) )
		{
			return j;
		}
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
Int getFirstSelectablePlayer( const GameInfo *game )
{
	const GameSlot *slot = game->getConstSlot( game->getLocalSlotNum() );
	if( !game->amIHost() || ( slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER ) )
		return game->getLocalSlotNum();

	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		slot = game->getConstSlot( i );
		if( slot && slot->isAI() )
			return i;
	}

	return game->getLocalSlotNum();
}

//-------------------------------------------------------------------------------------------------
Bool selectSlotState( NGMPGame *game, Int slotIndex, SlotState state, Bool *outIsAIChanged, Bool *outWasAI )
{
	if( outIsAIChanged )
		*outIsAIChanged = FALSE;

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !game || !pLobbyInterface || !pLobbyInterface->IsHost() || slotIndex == game->getLocalSlotNum() )
		return FALSE;

	if( state == SLOT_PLAYER || state < 0 )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot || slot->getState() == state )
		return FALSE;

	if( outWasAI )
		*outWasAI = slot->isAI();

	if( slot->getState() == SLOT_PLAYER )
	{
		// Occupied by a human: changing this slot's combo box away from "Player" kicks them.
		UnicodeString name = slot->getName();
		NGMPGameSlot *ngmpSlot = (NGMPGameSlot *)slot;
		int64_t userBeingKicked = ngmpSlot->m_userID;

		pLobbyInterface->UpdateCurrentLobby_KickUser( userBeingKicked, name );
		pLobbyInterface->UpdateCurrentLobby_SetSlotState( slotIndex, SlotState( state ) );
		slot->setState( state );
		game->resetAccepted();

		if( TheNGMPGame && TheNGMPGame->IsCountdownStarted() )
			TheNGMPGame->StopCountdown();
		return TRUE;
	}

	Bool wasAI = slot->isAI();
	slot->setState( state );
	Bool isAI = slot->isAI();
	game->resetAccepted();

	if( TheNGMPGame && TheNGMPGame->IsCountdownStarted() )
		TheNGMPGame->StopCountdown();

	if( outIsAIChanged )
		*outIsAIChanged = ( wasAI != isAI );

	pLobbyInterface->UpdateCurrentLobby_SetSlotState( slotIndex, state );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void selectColor( NGMPGame *game, Int slotIndex, Int color )
{
	if( !game )
		return;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot || color == slot->getColor() )
		return;

	if( color < -1 || color >= TheMultiplayerSettings->getNumColors() )
		return;

	if( color != -1 )
	{
		for( Int i = 0; i < MAX_SLOTS; ++i )
		{
			GameSlot *checkSlot = game->getSlot( i );
			if( checkSlot != slot && color == checkSlot->getColor() )
				return; // TODO_NGMP: Enforce this on the service too
		}
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !pLobbyInterface )
		return;

	if( slotIndex == game->getLocalSlotNum() )
	{
		pLobbyInterface->UpdateCurrentLobby_MyColor( color );
	}
	else if( slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI )
	{
		pLobbyInterface->UpdateCurrentLobby_AIColor( slotIndex, color );
	}
}

//-------------------------------------------------------------------------------------------------
Bool selectPlayerTemplate( NGMPGame *game, Int slotIndex, Int playerTemplate )
{
	if( !game )
		return FALSE;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot || playerTemplate == slot->getPlayerTemplate() )
		return FALSE;

	Int oldTemplate = slot->getPlayerTemplate();
	slot->setPlayerTemplate( playerTemplate );

	int updatedStartPos = slot->getStartPos();
	Bool observerChanged = FALSE;

	if( oldTemplate == PLAYERTEMPLATE_OBSERVER || playerTemplate == PLAYERTEMPLATE_OBSERVER )
	{
		slot->setStartPos( -1 );
		updatedStartPos = -1;
		observerChanged = TRUE;
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !pLobbyInterface )
		return observerChanged;

	if( slotIndex == game->getLocalSlotNum() )
	{
		pLobbyInterface->UpdateCurrentLobby_MySide( playerTemplate, updatedStartPos );
	}
	else if( slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI )
	{
		pLobbyInterface->UpdateCurrentLobby_AISide( slotIndex, playerTemplate, updatedStartPos );
	}

	return observerChanged;
}

//-------------------------------------------------------------------------------------------------
void selectTeam( NGMPGame *game, Int slotIndex, Int team )
{
	if( !game )
		return;

	GameSlot *slot = game->getSlot( slotIndex );
	if( !slot || team == slot->getTeamNumber() )
		return;

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !pLobbyInterface )
		return;

	if( slotIndex == game->getLocalSlotNum() )
	{
		pLobbyInterface->UpdateCurrentLobby_MyTeam( team );
	}
	else if( slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI )
	{
		pLobbyInterface->UpdateCurrentLobby_AITeam( slotIndex, team );
	}
}

//-------------------------------------------------------------------------------------------------
void selectStartPosition( NGMPGame *game, Int slotIndex, Int startPos )
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !game || !pLobbyInterface )
		return;

	NGMPGameSlot *slot = game->getGameSpySlot( slotIndex );
	if( !slot || startPos == slot->getStartPos() )
		return;

	if( startPos >= 0 )
	{
		for( Int i = 0; i < MAX_SLOTS; ++i )
		{
			if( i != slotIndex && game->getSlot( i )->getStartPos() == startPos )
				return;
		}
	}

	if( slotIndex == game->getLocalSlotNum() )
	{
		pLobbyInterface->UpdateCurrentLobby_MyStartPos( startPos );
	}
	else if( game->amIHost() && slot->isAI() )
	{
		pLobbyInterface->UpdateCurrentLobby_AIStartPos( slotIndex, startPos );
	}
}

//-------------------------------------------------------------------------------------------------
void handleStartPositionMarkerClick( NGMPGame *game, Int position )
{
	if( !game )
		return;

	Int playerIdxInPos = -1;
	for( Int j = 0; j < MAX_SLOTS; ++j )
	{
		NGMPGameSlot *slot = game->getGameSpySlot( j );
		if( slot && slot->getStartPos() == position )
		{
			playerIdxInPos = j;
			break;
		}
	}

	if( playerIdxInPos >= 0 )
	{
		NGMPGameSlot *slot = game->getGameSpySlot( playerIdxInPos );
		if( playerIdxInPos == game->getLocalSlotNum() || ( game->amIHost() && slot && slot->isAI() ) )
		{
			Int nextPlayer = getNextSelectablePlayer( game, playerIdxInPos + 1 );
			selectStartPosition( game, playerIdxInPos, -1 );
			if( nextPlayer >= 0 )
				selectStartPosition( game, nextPlayer, position );
		}
	}
	else
	{
		Int nextPlayer = getNextSelectablePlayer( game, 0 );
		if( nextPlayer < 0 )
			nextPlayer = getFirstSelectablePlayer( game );
		selectStartPosition( game, nextPlayer, position );
	}
}

//-------------------------------------------------------------------------------------------------
void handleStartPositionMarkerRightClick( NGMPGame *game, Int position )
{
	if( !game )
		return;

	Int playerIdxInPos = -1;
	for( Int j = 0; j < MAX_SLOTS; ++j )
	{
		NGMPGameSlot *slot = game->getGameSpySlot( j );
		if( slot && slot->getStartPos() == position )
		{
			playerIdxInPos = j;
			break;
		}
	}

	if( playerIdxInPos >= 0 )
	{
		NGMPGameSlot *slot = game->getGameSpySlot( playerIdxInPos );
		if( playerIdxInPos == game->getLocalSlotNum() || ( game->amIHost() && slot && slot->isAI() ) )
			selectStartPosition( game, playerIdxInPos, -1 );
	}
}

//-------------------------------------------------------------------------------------------------
void setStartingCash( const Money &startingCash )
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->UpdateCurrentLobby_StartingCash( startingCash.countMoney() );
	}
}

//-------------------------------------------------------------------------------------------------
void setSuperweaponRestriction( Bool restricted )
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->UpdateCurrentLobby_LimitSuperweapons( restricted != FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
static void callChatLine( const StartPressCallbacks &callbacks, const UnicodeString &text, Color color )
{
	if( callbacks.chatLine )
		callbacks.chatLine( text, color );
}

//-------------------------------------------------------------------------------------------------
void pressStart( NGMPGame *game, const StartPressCallbacks &callbacks )
{
	Bool isReady = TRUE;
	Bool allHaveMap = TRUE;
	Int playerCount = 0;
	Int humanCount = 0;

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	NGMPGame *myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if( pLobbyInterface == nullptr || !myGame || pAuthInterface == nullptr || myGame != game )
		return;

	NetworkMesh *pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if( pMesh == nullptr )
		return;

	int numHumanPlayers = 0;
	for( LobbyMemberEntry &member : pLobbyInterface->GetCurrentLobby().members )
	{
		if( member.IsHuman() )
			++numHumanPlayers;
	}

	if( pMesh->GetAllConnections().size() < numHumanPlayers - 1 )
	{
		callChatLine( callbacks, UnicodeString( L"Connections: Some players are still connecting. Try again shortly:" ), GameMakeColor( 255, 194, 15, 255 ) );

		int64_t myUserID = pAuthInterface->GetUserID();
		auto vecLobbyMembers = pLobbyInterface->GetCurrentLobby().members;
		auto allConnections = pMesh->GetAllConnections();

		for( LobbyMemberEntry &lobbyMember : vecLobbyMembers )
		{
			if( lobbyMember.IsHuman() && lobbyMember.user_id != myUserID )
			{
				if( allConnections.find( lobbyMember.user_id ) == allConnections.end() )
				{
					UnicodeString strDisplayName( from_utf8( lobbyMember.display_name ).c_str() );
					callChatLine( callbacks, strDisplayName, GameMakeColor( 255, 194, 15, 255 ) );
				}
			}
		}

		return;
	}

	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if( mapData )
	{
		mapDisplayName.format( L"%ls", mapData->m_displayName.str() );
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		mapDisplayName.translate( myGame->getMap().str() );
		willTransfer = WouldMapTransfer( myGame->getMap() );
	}
	for( int i = 0; i < MAX_SLOTS; i++ )
	{
		bool bIsAccepted = myGame->getSlot( i )->isAccepted();
		bool bIsHuman = myGame->getSlot( i )->isHuman();
		if( ( bIsAccepted == FALSE ) && ( bIsHuman == TRUE ) )
		{
			isReady = FALSE;
			if( !myGame->getSlot( i )->hasMap() && !willTransfer )
			{
				UnicodeString msg;
				msg.format( TheGameText->fetch( "GUI:PlayerNoMap" ), myGame->getSlot( i )->getName().str(), mapDisplayName.str() );
				callChatLine( callbacks, msg, GameSpyColor[GSCOLOR_DEFAULT] );
				allHaveMap = FALSE;
			}
		}
		if( myGame->getSlot( i )->isOccupied() && myGame->getSlot( i )->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
		{
			if( myGame->getSlot( i )->isHuman() )
				humanCount++;
			playerCount++;
		}
	}

	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if( !md || md->m_numPlayers < playerCount )
	{
		if( myGame->amIHost() )
		{
			UnicodeString text;
			text.format( TheGameText->fetch( "LAN:TooManyPlayers" ), ( md ) ? md->m_numPlayers : 0 );
			callChatLine( callbacks, text, GameSpyColor[GSCOLOR_DEFAULT] );
		}
		return;
	}

	if( TheGlobalData->m_netMinPlayers && !humanCount )
	{
		if( myGame->amIHost() )
			callChatLine( callbacks, TheGameText->fetch( "GUI:NeedHumanPlayers" ), GameSpyColor[GSCOLOR_DEFAULT] );
		return;
	}

	if( playerCount < TheGlobalData->m_netMinPlayers )
	{
		if( myGame->amIHost() )
		{
			UnicodeString text;
			text.format( TheGameText->fetch( "LAN:NeedMorePlayers" ), playerCount );
			callChatLine( callbacks, text, GameSpyColor[GSCOLOR_DEFAULT] );
		}
		return;
	}

	int numRandom = 0;
	std::set<Int> teams;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		GameSlot *slot = myGame->getSlot( i );
		if( slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
		{
			if( slot->getTeamNumber() >= 0 )
				teams.insert( slot->getTeamNumber() );
			else
				++numRandom;
		}
	}
	if( numRandom + (int)teams.size() < TheGlobalData->m_netMinPlayers )
	{
		if( myGame->amIHost() )
		{
			UnicodeString text;
			text.format( TheGameText->fetch( "LAN:NeedMoreTeams" ) );
			callChatLine( callbacks, text, GameSpyColor[GSCOLOR_DEFAULT] );
		}
		return;
	}

	if( numRandom + (int)teams.size() < 2 )
	{
		UnicodeString text;
		text.format( TheGameText->fetch( "GUI:SandboxMode" ) );
		callChatLine( callbacks, text, GameSpyColor[GSCOLOR_DEFAULT] );
	}

	if( isReady )
	{
		callChatLine( callbacks, UnicodeString( L"Connections: Checking all players..." ), GameMakeColor( 255, 194, 15, 255 ) );

		std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
		if( pWS != nullptr )
		{
			if( callbacks.setStartButtonEnabled )
				callbacks.setStartButtonEnabled( FALSE );

			StartPressCallbacks callbacksCopy = callbacks; // captured by value for the async lambda below
			pWS->SendData_StartFullMeshConnectivityCheck( [callbacksCopy]( bool bMeshFullyConnected, std::list<std::pair<int64_t, int64_t>> missingConnections, std::string strFailureReason )
				{
					NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

					if( bMeshFullyConnected )
					{
						callChatLine( callbacksCopy, UnicodeString( L"Connections: All players are connected." ), GameMakeColor( 0, 255, 0, 255 ) );

#if !defined(GENERALS_ONLINE_DISABLE_AUTO_ACCEPT)
						if( pLobbyInterface != nullptr )
							pLobbyInterface->ClearAutoReadyCountdown();
#endif

#if !defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
						Lobby_StartGamePacket startGamePacket;
						if( pLobbyInterface != nullptr )
							pLobbyInterface->SendToMesh( startGamePacket );

						NetworkMesh *pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
						if( pMesh != nullptr )
						{
							Lobby_StartGamePacket startGamePacket2;
							pMesh->ProcessGameStart( startGamePacket2 );
						}
#else
						if( TheNGMPGame != nullptr )
						{
							if( !TheNGMPGame->IsCountdownStarted() )
							{
								UnicodeString strInform;
								strInform.format( TheGameText->fetch( "LAN:GameStartTimerPlural" ), TheNGMPGame->GetTotalCountdownDuration() );
								if( pLobbyInterface != nullptr )
									pLobbyInterface->SendAnnouncementMessageToCurrentLobby( strInform, true );

								TheNGMPGame->StartCountdown();
								if( callbacksCopy.setSelectMapButtonEnabled )
									callbacksCopy.setSelectMapButtonEnabled( FALSE );
							}
						}
#endif

						GameSpyCloseOverlay( GSOVERLAY_BUDDY );
					}
					else
					{
						callChatLine( callbacksCopy, UnicodeString( L"Connections: The player network is not ready. Try again shortly." ), GameMakeColor( 255, 194, 15, 255 ) );

						if( !strFailureReason.empty() )
						{
							UnicodeString strReasonLine;
							strReasonLine.format( L"Connections: Reason: %s", from_utf8( strFailureReason ).c_str() );
							callChatLine( callbacksCopy, strReasonLine, GameMakeColor( 255, 194, 15, 255 ) );
						}

						callChatLine( callbacksCopy, UnicodeString( L"Connections: Missing links:" ), GameMakeColor( 255, 194, 15, 255 ) );
						for( auto &missingPair : missingConnections )
						{
							bool bFoundPlayer = false;
							if( pLobbyInterface != nullptr )
							{
								LobbyMemberEntry lobbyMemberSource = pLobbyInterface->GetRoomMemberFromID( missingPair.first );
								LobbyMemberEntry lobbyMemberTarget = pLobbyInterface->GetRoomMemberFromID( missingPair.second );
								if( lobbyMemberSource.user_id != -1 && lobbyMemberTarget.user_id != -1 )
								{
									bFoundPlayer = true;

									UnicodeString strMissingConnection;
									strMissingConnection.format( L"%s is not connected to %s.", from_utf8( lobbyMemberSource.display_name ).c_str(), from_utf8( lobbyMemberTarget.display_name ).c_str() );
									callChatLine( callbacksCopy, strMissingConnection, GameMakeColor( 255, 194, 15, 255 ) );
								}
							}

							if( !bFoundPlayer )
							{
								UnicodeString strMissingConnection;
								strMissingConnection.format( L"Player %lld is not connected to player %lld.", missingPair.first, missingPair.second );
								callChatLine( callbacksCopy, strMissingConnection, GameMakeColor( 255, 194, 15, 255 ) );
							}
						}

						if( callbacksCopy.setBackButtonEnabled )
							callbacksCopy.setBackButtonEnabled( TRUE );
						if( callbacksCopy.setStartButtonEnabled )
							callbacksCopy.setStartButtonEnabled( TRUE );
					}
				} );
		}
	}
	else if( allHaveMap )
	{
#if defined(GENERALS_ONLINE_DISABLE_AUTO_ACCEPT)
		callChatLine( callbacks, TheGameText->fetch( "GUI:NotifiedStartIntent" ), GameSpyColor[GSCOLOR_DEFAULT] );

		UnicodeString strInform = TheGameText->fetch( "GUI:HostWantsToStart" );
		pLobbyInterface->SendAnnouncementMessageToCurrentLobby( strInform, false );
#else
		if( !pLobbyInterface->HasAutoReadyCountdown() )
		{
			callChatLine( callbacks, TheGameText->fetch( "GUI:NotifiedStartIntent" ), GameSpyColor[GSCOLOR_DEFAULT] );

			UnicodeString strInform = TheGameText->fetch( "GUI:HostWantsToStart" );
			UnicodeString strInform2 = UnicodeString( L"Ready check: All players will be marked ready in 30 seconds." );
			pLobbyInterface->SendAnnouncementMessageToCurrentLobby( strInform, false );
			pLobbyInterface->SendAnnouncementMessageToCurrentLobby( strInform2, true );

			pLobbyInterface->StartAutoReadyCountdown();
		}
		else
		{
			callChatLine( callbacks, UnicodeString( L"Ready check: A countdown is already running. Players will be marked ready when it ends." ), GameMakeColor( 192, 192, 192, 255 ) );
		}
#endif
	}
}

//-------------------------------------------------------------------------------------------------
void requestAccept( NGMPGame *game )
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( !pLobbyInterface || !game )
		return;

	GameSlot *localSlot = game->getSlot( game->getLocalSlotNum() );
	if( localSlot )
		localSlot->setAccept();

	pLobbyInterface->ApplyLocalUserPropertiesToCurrentNetworkRoom();
}

//-------------------------------------------------------------------------------------------------
Bool canOpenMapSelect( const NGMPGame *game )
{
	return game != nullptr && game->amIHost();
}

//-------------------------------------------------------------------------------------------------
void applySelectedMap( NGMPGame *game, const AsciiString &mapName )
{
	if( game == nullptr )
		return;

	game->setMap( mapName );

	AsciiString lowerMap = mapName;
	lowerMap.toLower();

	int newMaxPlayers = -1;
	UnicodeString strMapName;
	bool bOfficialMap = false;

	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( lowerMap ) : nullptr;
	if( md != nullptr )
	{
		game->getGameSpySlot( 0 )->setMapAvailability( TRUE ); // the host always has its own selected map
		game->setMapCRC( md->m_CRC );
		game->setMapSize( md->m_filesize );

		newMaxPlayers = md->m_numPlayers;
		strMapName = md->m_displayName;
		bOfficialMap = md->m_isOfficial;
	}
	else
	{
		game->setMapCRC( 0 );
		game->setMapSize( 0 );
	}

	game->adjustSlotsForMap();
	game->resetAccepted();
	game->resetStartSpots();

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->UpdateCurrentLobby_Map( strMapName, game->getMap(), bOfficialMap, newMaxPlayers );
	}
}

//-------------------------------------------------------------------------------------------------
void toggleCommunicatorOverlay()
{
	GameSpyToggleOverlay( GSOVERLAY_BUDDY );
}

//-------------------------------------------------------------------------------------------------
Bool handleSlashCommand( const UnicodeString &text, const ChatLineFn &chatLine )
{
	AsciiString message;
	message.translate( text );

	if( message.getCharAt( 0 ) != '/' )
		return FALSE;

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken( &token );
	token.toLower();

	if( token == "host" )
	{
		UnicodeString s;
		s.format( L"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting );
		if( chatLine )
			chatLine( s, GameSpyColor[GSCOLOR_DEFAULT] );
		return TRUE;
	}
	else if( token == "me" && text.getLength() > 4 )
	{
		UnicodeString msg = UnicodeString( text.str() + 4 );
		NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if( pLobbyInterface != nullptr )
			pLobbyInterface->SendChatMessageToCurrentLobby( msg, true );
		return TRUE;
	}
	else if( token == "help" || token == "commands" )
	{
		const Color helpColor = GameMakeColor( 127, 127, 127, 255 );
		if( chatLine )
		{
			chatLine( UnicodeString( L"/me <message> - Send an emote." ), helpColor );
			chatLine( UnicodeString( L"/friendsonly - Let only friends join (host only)." ), helpColor );
			chatLine( UnicodeString( L"/public - Let anyone join (host only)." ), helpColor );
			chatLine( UnicodeString( L"/setpassword <password> - Set a lobby password (host only)." ), helpColor );
			chatLine( UnicodeString( L"/removepassword - Remove the lobby password (host only)." ), helpColor );
			chatLine( UnicodeString( L"/maxcameraheight <value> - Set the camera height limit (host only)." ), helpColor );
			chatLine( UnicodeString( L"/support - Open the GeneralsOnline Discord." ), helpColor );
			chatLine( UnicodeString( L"/help - Show these commands. You can also use /commands." ), helpColor );
		}
		return TRUE;
	}
	else if( token == "support" )
	{
		ShellExecuteA( NULL, "open", "https://discord.playgenerals.online", NULL, NULL, SW_SHOWNORMAL );
		return TRUE;
	}
	else if( token == "friendsonly" )
	{
		NGMP_OnlineServicesManager *pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
		if( pOnlineServicesMgr != nullptr )
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if( pLobbyInterface != nullptr && pLobbyInterface->IsInLobby() && pLobbyInterface->IsHost() )
			{
				if( chatLine )
					chatLine( UnicodeString( L"Lobby access: Friends only. Use /public to let anyone join." ), GameMakeColor( 0, 255, 0, 255 ) );
				pLobbyInterface->SetJoinability( ELobbyJoinability::LobbyJoinability_FriendsOnly );
			}
		}
		return TRUE;
	}
	else if( token == "public" )
	{
		NGMP_OnlineServicesManager *pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
		if( pOnlineServicesMgr != nullptr )
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if( pLobbyInterface != nullptr && pLobbyInterface->IsInLobby() && pLobbyInterface->IsHost() )
			{
				if( chatLine )
					chatLine( UnicodeString( L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends." ), GameMakeColor( 0, 255, 0, 255 ) );
				pLobbyInterface->SetJoinability( ELobbyJoinability::LobbyJoinability_Public );
			}
		}
		return TRUE;
	}
	else if( token == "maxcameraheight" && text.getLength() > 17 )
	{
		NGMP_OnlineServicesManager *pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
		if( pOnlineServicesMgr != nullptr )
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if( pLobbyInterface != nullptr && pLobbyInterface->IsInLobby() )
			{
				if( pLobbyInterface->IsHost() )
				{
					UnicodeString val = UnicodeString( text.str() + 17 );

					AsciiString asciiVal;
					asciiVal.translate( val );

					bool bIsNumber = true;
					for( int i = 0; i < asciiVal.getLength(); ++i )
					{
						char thisChar = asciiVal.getCharAt( i );
						if( !std::isdigit( (unsigned char)thisChar ) )
						{
							bIsNumber = false;
							break;
						}
					}

					if( bIsNumber )
					{
						int newCameraHeight = atoi( asciiVal.str() );

						if( newCameraHeight < GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM || newCameraHeight > GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM )
						{
							UnicodeString msg;
							msg.format( L"Camera height: Enter a value from %d to %d.", GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM, GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM );
							if( chatLine )
								chatLine( msg, GameMakeColor( 255, 0, 0, 255 ) );
							return TRUE;
						}

						NGMP_OnlineServicesManager::Settings.Save_Camera_MaxHeight_WhenLobbyHost( (float)newCameraHeight );
						pLobbyInterface->UpdateCurrentLobbyMaxCameraHeight( (uint16_t)newCameraHeight );
					}
					else
					{
						if( chatLine )
							chatLine( UnicodeString( L"Camera height: Enter a number." ), GameMakeColor( 255, 0, 0, 255 ) );
						return TRUE;
					}
				}
				else
				{
					if( chatLine )
						chatLine( UnicodeString( L"Camera height: Only the host can change it." ), GameMakeColor( 255, 0, 0, 255 ) );
					return TRUE;
				}
			}
		}
		return TRUE;
	}
	else if( token == "steam" || token == "advnet" )
	{
		NetworkLog( ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Writing advanced networking stats:" );

		std::map<int64_t, PlayerConnection> &connections = NGMP_OnlineServicesManager::GetNetworkMesh()->GetAllConnections();
		for( auto &kvPair : connections )
		{
			PlayerConnection &conn = kvPair.second;
			NetworkLog( ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Connection to user %lld: %s", kvPair.first, conn.GetStats().c_str() );
		}
		NetworkLog( ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Advanced networking stats dumped" );
		if( chatLine )
			chatLine( UnicodeString( L"Network debug: Statistics were written to the log file." ), GameMakeColor( 192, 192, 192, 255 ) );

		return TRUE;
	}
	else if( token == "setpassword" && text.getLength() > 13 )
	{
		UnicodeString newPassword( text.str() + 13 );

		NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		if( pLobbyInterface != nullptr && pAuthInterface != nullptr )
		{
			LobbyEntry &theLobby = pLobbyInterface->GetCurrentLobby();

			if( theLobby.owner == pAuthInterface->GetUserID() )
			{
				if( newPassword.getLength() == 0 || newPassword.getLength() > GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH )
				{
					UnicodeString errorMsg;
					errorMsg.format( L"Lobby password: Use 1 to %d characters.", GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH );
					if( chatLine )
						chatLine( errorMsg, GameMakeColor( 255, 0, 0, 255 ) );
				}
				else
				{
					std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if( pWS != nullptr )
					{
						pWS->SendData_ChangeLobbyPassword( newPassword );
						if( chatLine )
							chatLine( UnicodeString( L"Lobby password: Updated. Use /removepassword to remove it." ), GameMakeColor( 0, 255, 0, 255 ) );
					}
				}
			}
			else
			{
				if( chatLine )
					chatLine( UnicodeString( L"Lobby password: Only the host can change it." ), GameMakeColor( 255, 0, 0, 255 ) );
			}
		}
		return TRUE;
	}
	else if( token == "removepassword" || token == "clearpassword" || token == "resetpassword" )
	{
		NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		if( pLobbyInterface != nullptr && pAuthInterface != nullptr )
		{
			LobbyEntry &theLobby = pLobbyInterface->GetCurrentLobby();

			if( theLobby.owner == pAuthInterface->GetUserID() )
			{
				if( theLobby.passworded )
				{
					std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if( pWS != nullptr )
					{
						pWS->SendData_RemoveLobbyPassword();
						if( chatLine )
							chatLine( UnicodeString( L"Lobby password: Removed. Use /setpassword <password> to add one." ), GameMakeColor( 0, 255, 0, 255 ) );
					}
				}
				else
				{
					if( chatLine )
						chatLine( UnicodeString( L"Lobby password: No password is set. Use /setpassword <password> to add one." ), GameMakeColor( 192, 192, 192, 255 ) );
				}
			}
			else
			{
				if( chatLine )
					chatLine( UnicodeString( L"Lobby password: Only the host can change it." ), GameMakeColor( 255, 0, 0, 255 ) );
			}
		}
		return TRUE;
	}
#if defined(RTS_DEBUG)
	else if( token == "slots" )
	{
		g_debugSlots = !g_debugSlots;
		return TRUE;
	}
	else if( token == "discon" )
	{
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_LOGOUT;
		TheGameSpyPeerMessageQueue->addRequest( req );
		return TRUE;
	}
#endif

	if( chatLine )
		chatLine( UnicodeString( L"Unknown command: Use /help to see all commands." ), GameSpyColor[GSCOLOR_CHAT_NORMAL] );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void sendChat( const UnicodeString &text )
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		UnicodeString mutableText = text;
		pLobbyInterface->SendChatMessageToCurrentLobby( mutableText, false );
	}
}

} // namespace OnlineGameSetupActions
