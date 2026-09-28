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

// FILE: QuickMatchSession.cpp /////////////////////////////////////////////////
// See QuickMatchSession.h. Extracted from WOLQuickMatchMenu.cpp's live
// GENERALS_ONLINE code path.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchSession.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/NetworkMesh.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_SocialInterface.h"

extern NGMPGame *TheNGMPGame;

namespace QuickMatchSession
{

namespace
{
	EventSink s_sink;

	static const Int lobbyTimeoutMs = 10000;
	static const Int defaultMatchStartCountdownMs = 5000;

	Int s_matchFoundTimeoutStart = 0;
	Int s_matchFoundTimeoutDurationMs = lobbyTimeoutMs;
	Int s_matchStartCountdownDurationMs = defaultMatchStartCountdownMs;
	Int s_matchStartCountdownLastSecond = 0;

	void callStatusLine( const UnicodeString &text, Color color )
	{
		if( s_sink.statusLine )
			s_sink.statusLine( text, color );
	}
}

//-------------------------------------------------------------------------------------------------
void enter( const EventSink &sink )
{
	s_sink = sink;

	s_matchFoundTimeoutStart = 0;
	s_matchFoundTimeoutDurationMs = lobbyTimeoutMs;
	s_matchStartCountdownDurationMs = defaultMatchStartCountdownMs;
	s_matchStartCountdownLastSecond = 0;

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface == nullptr )
		return;

	// cannot connect to the lobby we joined
	pLobbyInterface->RegisterForCannotConnectToLobbyCallback( []( void )
		{
			callStatusLine( UnicodeString( L"Could not connect to a player, waiting for the matchmaker..." ), GameSpyColor[GSCOLOR_DEFAULT] );

			// don't cancel: racing a server-issued requeue could unregister us from its bucket
			s_matchFoundTimeoutStart = 0;
			s_matchStartCountdownLastSecond = 0;

			if( s_sink.setBackButtonEnabled )
				s_sink.setBackButtonEnabled( TRUE );
			if( s_sink.setStopButtonEnabled )
				s_sink.setStopButtonEnabled( TRUE );
		} );

	// TODO_QUICKMATCH: Deregister when leaving QM
	pLobbyInterface->RegisterForMatchmakingMessageCallback( []( std::string strMsg )
		{
			UnicodeString uMsg;
			uMsg = UnicodeString( from_utf8( strMsg ).c_str() );

			callStatusLine( uMsg, GameSpyColor[GSCOLOR_DEFAULT] );
		} );

	pLobbyInterface->RegisterForMatchmakingMatchFoundCallback( []()
		{
			if( s_sink.setBackButtonEnabled )
				s_sink.setBackButtonEnabled( FALSE );
			if( s_sink.setStopButtonEnabled )
				s_sink.setStopButtonEnabled( FALSE );
			s_matchFoundTimeoutDurationMs = lobbyTimeoutMs;
			s_matchFoundTimeoutStart = timeGetTime();
			s_matchStartCountdownLastSecond = 0;
			if( TheAudio )
			{
				AudioEventRTS evt( "GUICommunicatorOpen" );
				TheAudio->addAudioEvent( &evt );
			}
		} );

	pLobbyInterface->RegisterForMatchmakingRequeueCallback( []()
		{
			s_matchFoundTimeoutStart = 0;
			s_matchFoundTimeoutDurationMs = lobbyTimeoutMs;
			s_matchStartCountdownLastSecond = 0;
			if( s_sink.setBackButtonEnabled )
				s_sink.setBackButtonEnabled( TRUE );
			if( s_sink.setStopButtonEnabled )
				s_sink.setStopButtonEnabled( TRUE );
			if( s_sink.setWidenButtonEnabled )
				s_sink.setWidenButtonEnabled( TRUE );
		} );

	pLobbyInterface->RegisterForMatchmakingSetupProgressCallback( []( int timeoutMs, int countdownMs )
		{
			s_matchFoundTimeoutDurationMs = timeoutMs;
			s_matchFoundTimeoutStart = timeGetTime();

			// Mirror the service-owned countdown for UI feedback. Older services don't send its length, so a short
			// timeout is taken to mean the countdown.
			if( countdownMs < 0 )
			{
				countdownMs = timeoutMs < lobbyTimeoutMs ? defaultMatchStartCountdownMs : 0;
			}

			s_matchStartCountdownDurationMs = countdownMs;
			s_matchStartCountdownLastSecond = countdownMs > 0 ? ( countdownMs + 999 ) / 1000 : 0;
		} );

	pLobbyInterface->RegisterForMatchmakingStartGameCallback( []()
		{
			s_matchFoundTimeoutStart = 0;
			s_matchStartCountdownLastSecond = 0;
			NetworkLog( ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] GOT START GAME EVENT" );

			// Check if TheNGMPGame is initialized before dereferencing it
			if( !TheNGMPGame )
			{
				NetworkLog( ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] NO NGMP GAME INSTANCE" );
				return;
			}

			// mark everyone as having the map, we dont allow user provided custom maps or map transfers in QM
			// TODO_QUICKMATCH: Do this automatically for game type quickmatch, or better yet, do it on the service
			for( int i = 0; i < MAX_SLOTS; i++ )
			{
				GameSlot *slot = TheNGMPGame->getSlot( i );
				if( slot != nullptr )
				{
					slot->setMapAvailability( TRUE );
				}
			}

			// start
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			NGMPGame *myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

			if( pLobbyInterface == nullptr || !myGame || !myGame->isInGame() )
			{
				NetworkLog( ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] Checks failed, %d, %d, %d", pLobbyInterface == nullptr, !myGame, !myGame->isInGame() );
				return;
			}

			// TODO_NGMP
			//SendStatsToOtherPlayers(TheNGMPGame);

			// NOTE: the original inline code looked this button up by the wrong window name
			// (GameSpyGameOptionsMenu.wnd:ButtonCommunicator, not this screen's ButtonBuddies) and so
			// this winEnable(FALSE) was always a silent no-op -- see the commit message.
			if( s_sink.setCommunicatorButtonEnabled )
				s_sink.setCommunicatorButtonEnabled( FALSE );
			GameSpyCloseOverlay( GSOVERLAY_BUDDY );
			GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );

			*TheNGMPGame = *myGame;
			TheNGMPGame->startGame( 0 );
		} );

	pLobbyInterface->RegisterForJoinLobbyCallback( []( EJoinLobbyResult result )
		{
			NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

			if( !pLobbyInterface->IsInLobby() )
			{
				return;
			}

			if( TheNGMPGame == nullptr )
			{
				TheNGMPGame = new NGMPGame();
				TheNGMPGame->markGameAsQM();
			}
			pLobbyInterface->UpdateRoomDataCache( []( bool bSuccess )
				{

				} );

			// connection events (for debug really)
			NetworkMesh *pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
			if( pMesh != nullptr )
			{
				pMesh->RegisterForConnectionEvents( []( int64_t userID, std::wstring strDisplayName, PlayerConnection *connection )
					{
#if _DEBUG // not enabled in quickmatch because then people see their opponent during matchmaking
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
						if( connState == EConnectionState::CONNECTING_DIRECT || connState == EConnectionState::FINDING_ROUTE )
						{
							strConnectionMessage.format( L"Connecting to %s", strDisplayName.c_str() );
							callStatusLine( strConnectionMessage, GameMakeColor( 255, 194, 15, 255 ) );
						}
						else if( connState == EConnectionState::CONNECTED_DIRECT )
						{
							strConnectionMessage.format( L"Connected to %s", strDisplayName.c_str() );
							callStatusLine( strConnectionMessage, GameMakeColor( 255, 194, 15, 255 ) );
						}
						else
						{
							if( connState == EConnectionState::CONNECTION_FAILED || connState == EConnectionState::CONNECTION_DISCONNECTED )
							{
								strConnectionMessage.format( L"Connection failed to %s", strDisplayName.c_str() );
								callStatusLine( strConnectionMessage, GameMakeColor( 255, 194, 15, 255 ) );
							}
						}
#endif
					} );
			}

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

	// And also initialize it
	if( pSocialInterface != nullptr && s_sink.communicatorCountChanged && pSocialInterface->GetNumTotalNotifications() > 0 )
	{
		s_sink.communicatorCountChanged( pSocialInterface->GetNumTotalNotifications() );
	}
}

//-------------------------------------------------------------------------------------------------
void leave()
{
	NGMP_OnlineServices_MatchmakingInterface *pMatchmakingInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_MatchmakingInterface>();
	if( pMatchmakingInterface != nullptr )
	{
		// Shutdown is invoked for UI exit but also for going to game... so don't tear down lobby on service in the latter case
		if( TheNGMPGame == nullptr || !TheNGMPGame->isGameInProgress() )
		{
			pMatchmakingInterface->CancelMatchmaking();
		}
	}

	NGMP_OnlineServices_LobbyInterface *pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if( pLobbyInterface != nullptr )
	{
		pLobbyInterface->DeregisterForMatchmakingMessageCallback();
		pLobbyInterface->DeRegisterForMatchmakingMatchFoundCallback();
		pLobbyInterface->DeregisterForMatchmakingRequeueCallback();
		pLobbyInterface->DeregisterForMatchmakingSetupProgressCallback();
		pLobbyInterface->DeregisterForMatchmakingStartGameCallback();

		pLobbyInterface->DeregisterForJoinLobbyCallback();
		pLobbyInterface->DeregisterForCannotConnectToLobbyCallback();
	}

	s_sink = EventSink();
	s_matchFoundTimeoutStart = 0;
	s_matchStartCountdownLastSecond = 0;
}

//-------------------------------------------------------------------------------------------------
void update( const EventSink &sink )
{
	s_sink = sink;

	if( s_matchStartCountdownLastSecond > 0 )
	{
		Int elapsedMs = timeGetTime() - s_matchFoundTimeoutStart;
		Int remainingMs = s_matchStartCountdownDurationMs - elapsedMs;
		Int secondsRemaining = remainingMs > 0 ? ( remainingMs + 999 ) / 1000 : 0;

		if( secondsRemaining > 0 && secondsRemaining < s_matchStartCountdownLastSecond )
		{
			UnicodeString countdownMessage;
			if( secondsRemaining == 1 )
			{
				countdownMessage.format( TheGameText->fetch( "LAN:GameStartTimerSingular" ), secondsRemaining );
			}
			else
			{
				countdownMessage.format( TheGameText->fetch( "LAN:GameStartTimerPlural" ), secondsRemaining );
			}

			callStatusLine( countdownMessage, GameMakeColor( 192, 192, 192, 255 ) );
			s_matchStartCountdownLastSecond = secondsRemaining;
		}
	}

	// Leave time for the server's START_GAME event to arrive.
	Int effectiveTimeoutMs = s_matchFoundTimeoutDurationMs < lobbyTimeoutMs ? lobbyTimeoutMs : s_matchFoundTimeoutDurationMs;
	if( s_matchFoundTimeoutStart != 0 && timeGetTime() - s_matchFoundTimeoutStart >= effectiveTimeoutMs )
	{
		s_matchFoundTimeoutStart = 0;
		if( s_sink.setBackButtonEnabled )
			s_sink.setBackButtonEnabled( TRUE );
		if( s_sink.setStopButtonEnabled )
			s_sink.setStopButtonEnabled( TRUE );
		callStatusLine( UnicodeString( L"Match setup timed out. You may cancel or continue waiting." ), GameMakeColor( 255, 194, 25, 255 ) );
	}
}

} // namespace QuickMatchSession
