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

#include "Common/CustomMatchPreferences.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GeneralsOnline/NextGenMP_defines.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/PluginInterfaces.h"

namespace OnlineGameSetupSignals
{
	Signal2<const UnicodeString &, Color> &chatLine() { static Signal2<const UnicodeString &, Color> s; return s; }
	Signal0 &slotsChanged() { static Signal0 s; return s; }
	Signal0 &optionsChanged() { static Signal0 s; return s; }
	Signal0 &becameHost() { static Signal0 s; return s; }
	Signal1<Bool> &backButtonEnabled() { static Signal1<Bool> s; return s; }
	Signal1<Bool> &startButtonEnabled() { static Signal1<Bool> s; return s; }
	Signal1<Bool> &communicatorButtonEnabled() { static Signal1<Bool> s; return s; }
	Signal0 &lockSettings() { static Signal0 s; return s; }
	Signal1<int> &communicatorCount() { static Signal1<int> s; return s; }
}

namespace OnlineGameSetupSession
{

namespace
{
#if defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
	bool s_matchStartCountdownWasRunning = false;
#endif

	void callChatLine( const UnicodeString &text, Color color )
	{
		OnlineGameSetupSignals::chatLine().emit( text, color );
	}
}

//-------------------------------------------------------------------------------------------------
NGMPGame *getCurrentGame()
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	return pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
}

//-------------------------------------------------------------------------------------------------
Bool isHost()
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	return pLobbyInterface != nullptr && pLobbyInterface->IsHost();
}

//-------------------------------------------------------------------------------------------------
void prepareGameState()
{
	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface == nullptr )
		return;

	NGMPGame *game = pLobbyInterface->GetCurrentGame();
	if( game == nullptr )
		return;

	NGMPGameSlot *hostSlot = game->getGameSpySlot( 0 );
	hostSlot->setAccept();

	if( pLobbyInterface->IsHost() )
	{
		// TODO_NGMP: Preferred color & factionsupport
		hostSlot->setColor( 0 );
		hostSlot->setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		hostSlot->setPingString( UnicodeString( L"TODO_NGMP" ) );

		CustomMatchPreferences customPref;

		// Recorded stats games can never limit superweapons, limit armies, or have inflated starting cash.
		// This should probably be enforced at the gamespy level as well, to prevent expoits.
#if !defined(GENERALS_ONLINE)
		Int isUsingStats = TheGameSpyGame->getUseStats();
#else
#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		Int isUsingStats = game->getUseStats();
#endif
#endif

#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		game->setStartingCash( isUsingStats ? TheMultiplayerSettings->getDefaultStartingMoney() : customPref.getStartingCash() );
		game->setSuperweaponRestriction( isUsingStats ? 0 : customPref.getSuperweaponRestricted() ? 1 : 0 );
		if( isUsingStats )
			game->setOldFactionsOnly( 0 );
#else
		game->setStartingCash( customPref.getStartingCash() );
		game->setSuperweaponRestriction( customPref.getSuperweaponRestricted() ? 1 : 0 );
#endif

		if( game->oldFactionsOnly() )
		{
			// Make sure host follows the old factions only restrictions!
			const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate( hostSlot->getPlayerTemplate() );

			if( fac != NULL && !fac->isOldFaction() )
			{
				hostSlot->setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
			}
		}

		for( Int i = 1; i < MAX_SLOTS; ++i )
		{
			NGMPGameSlot *slot = game->getGameSpySlot( i );
			slot->setState( SLOT_OPEN );
		}

		// TODO_NGMP: preferred map support
		AsciiString lowerMap = game->getMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find( lowerMap );
		if( it != TheMapCache->end() )
		{
			hostSlot->setMapAvailability( TRUE );
			game->setMapCRC( it->second.m_CRC );
			game->setMapSize( it->second.m_filesize );

			game->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
		}
	}
	else
	{
		// TODO_NGMP: Do this on join? and map change
		game->setMapCRC( game->getMapCRC() );		// force a recheck
		game->setMapSize( game->getMapSize() ); // of if we have the map
	}
}

//-------------------------------------------------------------------------------------------------
void enter()
{

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
				OnlineGameSetupSignals::slotsChanged().emit();
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
			OnlineGameSetupSignals::slotsChanged().emit();
			OnlineGameSetupSignals::optionsChanged().emit();
		} );

	pLobbyInterface->RegisterForGameStartPacket( []()
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			NGMPGame *myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

			if( pLobbyInterface == nullptr || !myGame || !myGame->isInGame() )
				return;

			if( !TheNGMPGame )
				return;

			OnlineGameSetupSignals::communicatorButtonEnabled().emit( FALSE );
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
				OnlineGameSetupSignals::communicatorCount().emit( numNotifications );
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
	if( pSocialInterface != nullptr && OnlineGameSetupSignals::communicatorCount().hasListeners() && pSocialInterface->GetNumTotalNotifications() > 0 )
	{
		OnlineGameSetupSignals::communicatorCount().emit( pSocialInterface->GetNumTotalNotifications() );
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

	// drop any in-flight mesh connectivity check so a late reply never fires into a dead screen
	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if( pWS != nullptr )
	{
		pWS->ClearConnectivityCheckCallback();
	}
}

//-------------------------------------------------------------------------------------------------
Bool update()
{
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
							OnlineGameSetupSignals::becameHost().emit();

							NetworkLog( ELogVerbosity::LOG_RELEASE, "Host left and server migrated the host to us..." );

							callChatLine( UnicodeString( L"Host: The previous host left. You are now the host." ), GameMakeColor( 192, 192, 192, 255 ) );

							// NOTE: don't need to mark ourselves ready, the service did it for us upon migration
						}
						else
						{
							callChatLine( UnicodeString( L"Host: The previous host left. A new host was selected." ), GameMakeColor( 192, 192, 192, 255 ) );
						}

						// re-enable critical buttons for everyone
						OnlineGameSetupSignals::backButtonEnabled().emit( TRUE );
						OnlineGameSetupSignals::startButtonEnabled().emit( TRUE );
						OnlineGameSetupSignals::communicatorButtonEnabled().emit( FALSE );
					}

					TheNGMPGame->UpdateSlotsFromCurrentLobby();

					OnlineGameSetupSignals::slotsChanged().emit();

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
					OnlineGameSetupSignals::lockSettings().emit();

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
				OnlineGameSetupSignals::backButtonEnabled().emit( TRUE );
				OnlineGameSetupSignals::startButtonEnabled().emit( TRUE );
			}
		}
	}
#endif

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void leaveLobby()
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
}

//-------------------------------------------------------------------------------------------------
void backToLobby()
{
	leaveLobby();
	TheShell->pop();
}

} // namespace OnlineGameSetupSession
