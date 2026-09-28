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

// FILE: OnlineGameSetupSession.cpp ///////////////////////////////////////////
// See OnlineGameSetupSession.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupSession.h"

#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GeneralsOnline/NextGenMP_defines.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/PluginInterfaces.h"

namespace OnlineGameSetupSession
{

namespace
{
	EventSink s_sink;

#if defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
	bool s_matchStartCountdownWasRunning = false;
#endif

	void callChatLine( const UnicodeString &text, Color color )
	{
		if( s_sink.chatLine )
			s_sink.chatLine( text, color );
	}
}

//-------------------------------------------------------------------------------------------------
void enter( const EventSink &sink )
{
	s_sink = sink;

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface == nullptr )
		return;

	// register for chat events
	pLobbyInterface->RegisterForChatCallback( []( UnicodeString strMessage, Color color )
		{
			callChatLine( strMessage, color );
		} );

	// cannot connect to the lobby we joined
	pLobbyInterface->RegisterForCannotConnectToLobbyCallback( []( void )
		{
			if( TheNetwork != NULL )
			{
				delete TheNetwork;
				TheNetwork = NULL;
			}
			GSMessageBoxOk( TheGameText->fetch( "GUI:Error" ), UnicodeString( L"Could not connect to all players in the lobby" ) );

			backToLobby();
		} );

	// connection events (for debug really)
	NetworkMesh *pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if( pMesh != nullptr )
	{
		pMesh->RegisterForConnectionEvents( []( int64_t userID, std::wstring strDisplayName, PlayerConnection *connection )
			{
				std::string strState = "Unknown";

				EConnectionState connState = connection->GetState();

				switch( connState )
				{
				case EConnectionState::NOT_CONNECTED:
					strState = "Not Connected";
					break;

				case EConnectionState::CONNECTING_DIRECT:
					strState = "Connecting";
					break;
				case EConnectionState::FINDING_ROUTE:
					strState = "Connecting (Finding Route)";
					break;

				case EConnectionState::CONNECTED_DIRECT:
					strState = "Connected";
					break;

				case EConnectionState::CONNECTION_FAILED:
					strState = "Connection Failed";
					break;

				case EConnectionState::CONNECTION_DISCONNECTED:
					strState = "Disconnected (Was Connected Previously)";
					break;

				default:
					strState = "Unknown";
					break;
				}

				UnicodeString strConnectionMessage;
				if( connState == EConnectionState::CONNECTED_DIRECT || connState == EConnectionState::CONNECTION_DISCONNECTED )
				{
					// (kept silent, same as the .wnd path)
				}
				else
				{
#if !defined(_DEBUG)
					if( connState == EConnectionState::CONNECTION_FAILED )
					{
#endif
						strConnectionMessage.format( L"Connection: %s is now %hs.", strDisplayName.c_str(), strState.c_str() );
						const Color connectionColor = connState == EConnectionState::CONNECTION_FAILED
							? GameMakeColor( 255, 0, 0, 255 )
							: GameMakeColor( 192, 192, 192, 255 );
						callChatLine( strConnectionMessage, connectionColor );

#if !defined(_DEBUG)
					}
#endif
				}

				// update UI
				if( s_sink.slotsChanged )
					s_sink.slotsChanged();
			} );
	}

	// player doesnt have map events
	pLobbyInterface->RegisterForPlayerDoesntHaveMapCallback( []( LobbyMemberEntry lobbyMember )
		{
			// tell the host the user doesn't have the map
			UnicodeString mapDisplayName;
			const MapMetaData *mapData = TheMapCache->findMap( TheNGMPGame->getMap() );
			Bool willTransfer = TRUE;
			if( mapData )
			{
				mapDisplayName.format( L"%ls", mapData->m_displayName.str() );
				willTransfer = !mapData->m_isOfficial;
			}
			else
			{
				mapDisplayName.translate( TheNGMPGame->getMap().str() );
				willTransfer = WouldMapTransfer( TheNGMPGame->getMap() );
			}

			UnicodeString strDisplayName( from_utf8( lobbyMember.display_name ).c_str() );

			UnicodeString text;
			if( willTransfer )
				text.format( TheGameText->fetch( "GUI:PlayerNoMapWillTransfer" ), strDisplayName.str(), mapDisplayName.str() );
			else
				text.format( TheGameText->fetch( "GUI:PlayerNoMap" ), strDisplayName.str(), mapDisplayName.str() );
			callChatLine( text, GameSpyColor[GSCOLOR_DEFAULT] );
		} );

	// register for roster events
	pLobbyInterface->RegisterForRosterNeedsRefreshCallback( []()
		{
			if( s_sink.slotsChanged )
				s_sink.slotsChanged();
			if( s_sink.optionsChanged )
				s_sink.optionsChanged();
		} );

	pLobbyInterface->RegisterForGameStartPacket( []()
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			NGMPGame *myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

			if( pLobbyInterface == nullptr || !myGame || !myGame->isInGame() )
				return;

			if( !TheNGMPGame )
				return;

			if( s_sink.setCommunicatorButtonEnabled )
				s_sink.setCommunicatorButtonEnabled( FALSE );
			GameSpyCloseOverlay( GSOVERLAY_BUDDY );
			GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );

			*TheNGMPGame = *myGame;
			TheNGMPGame->startGame( 0 );
		} );

	// Update the communicator button anytime we get notifications
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if( pSocialInterface != nullptr )
	{
		pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged( []( int numNotifications )
			{
				if( s_sink.communicatorCountChanged )
					s_sink.communicatorCountChanged( numNotifications );
			} );
	}

	// Did we just enter a lobby with a modified camera height?
	LobbyEntry &theLobby = pLobbyInterface->GetCurrentLobby();
	if( theLobby.max_cam_height != GENERALS_ONLINE_DEFAULT_LOBBY_CAMERA_ZOOM )
	{
		if( !pLobbyInterface->IsHost() )
		{
			UnicodeString strInform;
			strInform.format( L"Camera height: The host set the limit to %lu.", theLobby.max_cam_height );
			callChatLine( strInform, GameMakeColor( 192, 192, 192, 255 ) );
		}
		else
		{
			UnicodeString strInform;
			strInform.format( L"Camera height: Your limit is %lu. Use /maxcameraheight <value> to change it. Default: 310.", theLobby.max_cam_height );
			callChatLine( strInform, GameMakeColor( 192, 192, 192, 255 ) );
		}
	}

	if( pLobbyInterface->IsHost() )
	{
		callChatLine( UnicodeString( L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends." ), GameMakeColor( 192, 192, 192, 255 ) );
	}

	if( TheNGMPGame != nullptr && !TheNGMPGame->getAllowObservers() )
	{
		callChatLine( UnicodeString( L"Observers: Disabled by the host." ), GameMakeColor( 192, 192, 192, 255 ) );
	}

	// Initialize the Communicator badge for whatever notification count is already pending.
	if( pSocialInterface != nullptr && s_sink.communicatorCountChanged && pSocialInterface->GetNumTotalNotifications() > 0 )
	{
		s_sink.communicatorCountChanged( pSocialInterface->GetNumTotalNotifications() );
	}
}

//-------------------------------------------------------------------------------------------------
void leave()
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->DeregisterForChatCallback();
		pLobbyInterface->DeregisterForCannotConnectToLobbyCallback();
		pLobbyInterface->DeregisterForPlayerDoesntHaveMapCallback();
		pLobbyInterface->DeregisterForRosterNeedsRefreshCallback();
		pLobbyInterface->DeregisterForGameStartPacket();
	}

	NetworkMesh *pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if( pMesh != nullptr )
	{
		pMesh->DeregisterForConnectionEvents();
	}

	s_sink = EventSink();
}

//-------------------------------------------------------------------------------------------------
Bool update( const EventSink &sink )
{
	s_sink = sink;

	if( AnticheatPlugInterface::g_bPendingExitLobby )
	{
		AnticheatPlugInterface::g_bPendingExitLobby = false;

		GSMessageBoxOk( TheGameText->fetchOrSubstitute( "GUI:ACErrorHeader", L"AntiCheat Error" ), TheGameText->fetchOrSubstitute( "GUI:ACLobbyIntegrityError", L"Lobby integrity could not be validated. Leaving Lobby." ) );

		backToLobby();
	}

	if( NGMP_OnlineServicesManager::GetInstance() != nullptr )
	{
		NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if( pLobbyInterface != nullptr )
		{
			ServiceConfig &serviceConf = NGMP_OnlineServicesManager::GetInstance()->GetServiceConfig();
			bool bHostMigrationEnabledOnService = serviceConf.enable_host_migration;

			if( bHostMigrationEnabledOnService )
			{
				if( pLobbyInterface->m_bHostMigrated )
				{
					pLobbyInterface->m_bHostMigrated = false;

					// If we are in-game, nothing to do here, the game handles it for us
					if( !TheNGMPGame->isGameInProgress() ) // in progress is in game, ingame is just in lobby
					{
						// did we become the host?
						bool bIsHost = pLobbyInterface->IsHost();

						if( bIsHost )
						{
							if( s_sink.becameHost )
								s_sink.becameHost();

							NetworkLog( ELogVerbosity::LOG_RELEASE, "Host left and server migrated the host to us..." );

							callChatLine( UnicodeString( L"Host: The previous host left. You are now the host." ), GameMakeColor( 192, 192, 192, 255 ) );

							// NOTE: don't need to mark ourselves ready, the service did it for us upon migration
						}
						else
						{
							callChatLine( UnicodeString( L"Host: The previous host left. A new host was selected." ), GameMakeColor( 192, 192, 192, 255 ) );
						}

						// re-enable critical buttons for everyone
						if( s_sink.setBackButtonEnabled )
							s_sink.setBackButtonEnabled( TRUE );
						if( s_sink.setStartButtonEnabled )
							s_sink.setStartButtonEnabled( TRUE );
						if( s_sink.setCommunicatorButtonEnabled )
							s_sink.setCommunicatorButtonEnabled( FALSE );
					}

					TheNGMPGame->UpdateSlotsFromCurrentLobby();

					if( s_sink.slotsChanged )
						s_sink.slotsChanged();

					// Force a refresh to get latest lobby data
					NGMP_OnlineServices_LobbyInterface *pLobbyInterfaceRefresh = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if( pLobbyInterfaceRefresh != nullptr )
					{
						pLobbyInterfaceRefresh->UpdateRoomDataCache( []( bool bSuccess )
							{
							} );
					}
				}
			}

			if( pLobbyInterface->m_bPendingHostHasLeft || pLobbyInterface->m_bHostMigrated )
			{
				pLobbyInterface->m_bHostMigrated = false;
				pLobbyInterface->m_bPendingHostHasLeft = false;

				DEBUG_LOG( ( "Host left lobby\n" ) );
				if( TheNGMPGame )
					TheNGMPGame->reset();

				GSMessageBoxOk( TheGameText->fetch( "GUI:HostLeftTitle" ), TheGameText->fetch( "GUI:HostLeft" ) );

				backToLobby();

				return TRUE;
			}
		}
	}

#if defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
	// is there a countdown in progress?
	if( TheNGMPGame != nullptr )
	{
		if( TheNGMPGame->IsCountdownStarted() )
		{
			s_matchStartCountdownWasRunning = true;
			const int64_t timeBetweenChecks = 1000;
			int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::utc_clock::now().time_since_epoch() ).count();

			if( currTime - TheNGMPGame->GetCountdownLastCheckTime() >= timeBetweenChecks )
			{
				int secondsSinceCountdownStart = ( currTime - TheNGMPGame->GetCountdownStartTime() ) / 1000;
				int secondsRemaining = TheNGMPGame->GetTotalCountdownDuration() - secondsSinceCountdownStart;

				TheNGMPGame->UpdateCountdownLastCheckTime();

				// remote msg
				UnicodeString strInform;
				if( secondsRemaining == 1 )
				{
					// Lock all host controlled lobby settings last second of the match start countdown
					// to prevent late local changes not propagating to remote clients in time
					if( s_sink.lockSettings )
						s_sink.lockSettings();

					strInform.format( TheGameText->fetch( "LAN:GameStartTimerSingular" ), secondsRemaining );
				}
				else
				{
					strInform.format( TheGameText->fetch( "LAN:GameStartTimerPlural" ), secondsRemaining );
				}

				NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
				if( pLobbyInterface != nullptr )
				{
					pLobbyInterface->SendAnnouncementMessageToCurrentLobby( strInform, true );
				}

				// are we done?
				if( secondsRemaining <= 0 )
				{
					s_matchStartCountdownWasRunning = false;
					// stop countdown
					TheNGMPGame->StopCountdown();

					// send start game packet
					std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if( pWS != nullptr )
					{
						pWS->SendData_StartGame();
					}
				}
			}
		}
		else
		{
			// countdown is currently NOT running.
			// If it was running before, it just got cancelled or finished.
			if( s_matchStartCountdownWasRunning )
			{
				s_matchStartCountdownWasRunning = false;

				// Re-enable Back and Start buttons when countdown stops
				if( s_sink.setBackButtonEnabled )
					s_sink.setBackButtonEnabled( TRUE );
				if( s_sink.setStartButtonEnabled )
					s_sink.setStartButtonEnabled( TRUE );
			}
		}
	}
#endif

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void backToLobby()
{
	// delete TheNAT, its no good for us anymore.
	if( TheNAT != nullptr )
	{
		delete TheNAT;
		TheNAT = NULL;
	}

	if( TheNGMPGame ) // this can be blown away by a disconnect on the map transfer screen
	{
		TheNGMPGame->reset();
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->LeaveCurrentLobby();
	}

	TheShell->pop();
}

} // namespace OnlineGameSetupSession
