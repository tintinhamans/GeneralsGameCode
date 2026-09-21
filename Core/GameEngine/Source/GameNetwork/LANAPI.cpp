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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define WIN32_LEAN_AND_MEAN  // only bare bones windows stuff wanted

#include <stddef.h>

#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/Registry.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameClient/GUICallbacks.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/Caster/Caster.h"
#include "GameNetwork/Caster/CasterBeacon.h"
#include "GameNetwork/Caster/CasterChatMessage.h"
#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/RandomValue.h"
#include "GameClient/GameText.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/MapUtil.h"

#include "GameClient/GameWindow.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "Common/UserPreferences.h"
#include "GameLogic/GameLogic.h"


namespace
{
// VC6 disables static_assert; negative array sizes enforce the wire contract.
typedef char CasterBeaconType[(LANMessage::MSG_CASTER_BEACON == 17 && CasterBeacon::MESSAGE_TYPE == 17) ? 1 : -1];
typedef char CasterBeaconUid[(sizeof(((LANMessage*)0)->Caster.uid) == CasterBeacon::MAX_UID_CHARS + 1) ? 1 : -1];
typedef char CasterPacketBound[(sizeof(LANMessage) <= MAX_LANAPI_PACKET_SIZE) ? 1 : -1];
}

static const UnsignedShort lobbyPort = 8086; ///< This is the UDP port used by all LANAPI communication

AsciiString GetMessageTypeString(UnsignedInt type);

const UnsignedInt LANAPI::s_resendDelta = 10 * 1000;	///< This is how often we announce ourselves to the world
/*
LANGame::LANGame()
{
	m_gameName = L"";

	int player;
	for (player = 0; player < MAX_SLOTS; ++player)
	{
		m_playerName[player] = L"";
		m_playerIP[player]= 0;
		m_playerAccepted[player] = false;
	}
	m_lastHeard = 0;
	m_inProgress = false;
	m_next = nullptr;
}
*/




LANAPI::LANAPI() : m_transport(nullptr)
{
	DEBUG_LOG(("LANAPI::LANAPI() - max game option size is %d, sizeof(LANMessage)=%d, MAX_LANAPI_PACKET_SIZE=%d",
		m_lanMaxOptionsLength, sizeof(LANMessage), MAX_LANAPI_PACKET_SIZE));

	m_lastResendTime = 0;
	//
	m_lobbyPlayers = nullptr;
	m_games = nullptr;
	m_name = L""; // safe default?
	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_localIP = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;
	m_broadcastAddr = INADDR_BROADCAST;
	m_directConnectRemoteIP = 0;
	m_actionTimeout = 5000; // ms
	m_lastUpdate = 0;
	m_transport = new Transport;
	m_isActive = TRUE;
}

LANAPI::~LANAPI()
{
	reset();
	delete m_transport;
}

void LANAPI::init()
{
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;
	m_gameStartRevision = 0;
	m_transport->reset();
	m_transport->init(m_localIP, lobbyPort);
	m_transport->allowBroadcasts(true);

	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;
	m_directConnectRemoteIP = 0;

	m_lastGameopt = "";

#if TELL_COMPUTER_IDENTITY_IN_LAN_LOBBY
	char userName[UNLEN + 1];
	DWORD bufSize = ARRAY_SIZE(userName);
	if (GetUserNameA(userName, &bufSize))
	{
		m_userName.set(userName, bufSize - 1);
	}
	else
	{
		m_userName = "unknown";
	}

	char computerName[MAX_COMPUTERNAME_LENGTH + 1];
	bufSize = ARRAY_SIZE(computerName);
	if (GetComputerNameA(computerName, &bufSize))
	{
		m_hostName.set(computerName, bufSize - 1);
	}
	else
	{
		m_hostName = "unknown";
	}
#endif


	CasterEnable(CASTER_ROLE_PLAYER);
}

void LANAPI::reset()
{
	if (m_inLobby)
	{
		LANMessage msg;
		fillInLANMessage( &msg );
		msg.messageType = LANMessage::MSG_REQUEST_LOBBY_LEAVE;
		sendMessage(&msg);
	}
	m_transport->update();

	LANGameInfo *theGame = m_games;
	LANGameInfo *deletableGame = nullptr;

	while (theGame)
	{
		deletableGame = theGame;
		theGame = theGame->getNext();
		delete deletableGame;
	}

	LANPlayer *thePlayer = m_lobbyPlayers;
	LANPlayer *deletablePlayer = nullptr;

	while (thePlayer)
	{
		deletablePlayer = thePlayer;
		thePlayer = thePlayer->getNext();
		delete deletablePlayer;
	}

	m_games = nullptr;
	m_lobbyPlayers = nullptr;
	m_directConnectRemoteIP = 0;
	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;

}

void LANAPI::sendMessage(LANMessage *msg, UnsignedInt ip /* = 0 */)
{
	if (ip != 0)
	{
		m_transport->queueSend(ip, lobbyPort, (unsigned char *)msg, sizeof(LANMessage) /*, 0, 0 */);
	}
	else if ((m_currentGame != nullptr) && (m_currentGame->getIsDirectConnect()))
	{
		Int localSlot = m_currentGame->getLocalSlotNum();
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (i != localSlot) {
				GameSlot *slot = m_currentGame->getSlot(i);
				if ((slot != nullptr) && (slot->isHuman())) {
					m_transport->queueSend(slot->getIP(), lobbyPort, (unsigned char *)msg, sizeof(LANMessage) /*, 0, 0 */);
				}
			}
		}
	}
	else
	{
		m_transport->queueSend(m_broadcastAddr, lobbyPort, (unsigned char *)msg, sizeof(LANMessage) /*, 0, 0 */);
	}
}


AsciiString GetMessageTypeString(UnsignedInt type)
{
	AsciiString returnString;

	switch (type)
	{
		case LANMessage::MSG_REQUEST_LOCATIONS:
			returnString.format( "Request Locations (%d)",type);
			break;
		case LANMessage::MSG_GAME_ANNOUNCE:
			returnString.format("Game Announce (%d)",type);
			break;
		case LANMessage::MSG_LOBBY_ANNOUNCE:
			returnString.format("Lobby Announce (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_JOIN:
			returnString.format("Request Join (%d)",type);
			break;
		case LANMessage::MSG_JOIN_ACCEPT:
			returnString.format("Join Accept (%d)",type);
			break;
		case LANMessage::MSG_JOIN_DENY:
			returnString.format("Join Deny (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_GAME_LEAVE:
			returnString.format("Request Game Leave (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_LOBBY_LEAVE:
			returnString.format("Request Lobby Leave (%d)",type);
			break;
		case LANMessage::MSG_SET_ACCEPT:
			returnString.format("Set Accept(%d)",type);
			break;
		case LANMessage::MSG_CHAT:
			returnString.format("Chat (%d)",type);
			break;
		case LANMessage::MSG_GAME_START:
			returnString.format("Game Start (%d)",type);
			break;
		case LANMessage::MSG_GAME_START_TIMER:
			returnString.format("Game Start Timer (%d)",type);
			break;
		case LANMessage::MSG_GAME_OPTIONS:
			returnString.format("Game Options (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_GAME_INFO:
			returnString.format("Request GameInfo (%d)", type);
			break;
		case LANMessage::MSG_INACTIVE:
			returnString.format("Inactive (%d)", type);
			break;
		default:
			returnString.format("Unknown Message (%d)",type);
	}
	return returnString;
}


void LANAPI::checkMOTD()
{
#if defined(RTS_DEBUG)
	if (TheGlobalData->m_useLocalMOTD)
	{
		// for a playtest, let's log some play statistics, eh?
		if (TheGlobalData->m_playStats <= 0)
			TheWritableGlobalData->m_playStats = 30;

		static UnsignedInt oldMOTDCRC = 0;
		UnsignedInt newMOTDCRC = 0;
		AsciiString asciiMOTD;
		char buf[4096];
		FILE *fp = fopen(TheGlobalData->m_MOTDPath.str(), "r");
		Int len;
		if (fp)
		{
			while( (len = fread(buf, 1, 4096, fp)) > 0 )
			{
				buf[len] = 0;
				asciiMOTD.concat(buf);
			}
			fclose(fp);
			CRC crcObj;
			crcObj.computeCRC(asciiMOTD.str(), asciiMOTD.getLength());
			newMOTDCRC = crcObj.get();
		}

		if (oldMOTDCRC != newMOTDCRC)
		{
			// different MOTD... display it
			oldMOTDCRC = newMOTDCRC;
			AsciiString line;
			while (asciiMOTD.nextToken(&line, "\n"))
			{
				if (line.getCharAt(line.getLength()-1) == '\r')
					line.removeLastChar();	// there is a trailing '\r'

				if (line.isEmpty())
				{
					line = " ";
				}

				UnicodeString uniLine;
				uniLine.translate(line);
				OnChat( L"MOTD", 0, uniLine, LANCHAT_SYSTEM );
			}
		}
	}
#endif
}

extern Bool LANbuttonPushed;
extern Bool LANSocketErrorDetected;
void LANAPI::update()
{
	if(LANbuttonPushed)
		return;
	static const UnsignedInt LANAPIUpdateDelay = 200;
	UnsignedInt now = timeGetTime();

	if( now > m_lastUpdate + LANAPIUpdateDelay)
	{
		m_lastUpdate = now;
	}
	else
	{
		return;
	}

	// Let the UDP socket breathe
	if ((m_transport->update() == FALSE) && (LANSocketErrorDetected == FALSE)) {
		if (m_isInLANMenu == TRUE) {
			LANSocketErrorDetected = TRUE;
		}
	}

	// Handle any new messages
	for (size_t i = 0; i < ARRAY_SIZE(m_transport->m_inBuffer) && !LANbuttonPushed; ++i)
	{
		if (m_transport->m_inBuffer[i].length > 0)
		{
			// Process the new message
			UnsignedInt senderIP = m_transport->m_inBuffer[i].addr;
			if (senderIP == m_localIP)
			{

				m_transport->m_inBuffer[i].length = 0;
				continue;
			}

			LANMessage *msg = (LANMessage *)(m_transport->m_inBuffer[i].data);
			//DEBUG_LOG(("LAN message type %s from %ls (%s@%s)", GetMessageTypeString(msg->messageType).str(),
			//	msg->name, msg->userName, msg->hostName));
			switch (msg->messageType)
			{
				// Location specification
			case LANMessage::MSG_REQUEST_LOCATIONS:		// Hey, where is everybody?
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_LOCATIONS from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestLocations( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_ANNOUNCE:				// Here someone is, and here's his game info!
				DEBUG_LOG(("LANAPI::update - got a MSG_GAME_ANNOUNCE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleGameAnnounce( msg, senderIP );
				break;
			case LANMessage::MSG_LOBBY_ANNOUNCE:			// Hey, I'm in the lobby!
				DEBUG_LOG(("LANAPI::update - got a MSG_LOBBY_ANNOUNCE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleLobbyAnnounce( msg, senderIP );
				break;
			case LANMessage::MSG_REQUEST_GAME_INFO:
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_GAME_INFO from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestGameInfo( msg, senderIP );
				ReplyCasterDetails(senderIP);
				break;

				// Joining games
			case LANMessage::MSG_REQUEST_JOIN:				// Let me in!  Let me in!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_JOIN from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestJoin( msg, senderIP );
				break;
			case LANMessage::MSG_JOIN_ACCEPT:					// Okay, you can join.
				DEBUG_LOG(("LANAPI::update - got a MSG_JOIN_ACCEPT from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleJoinAccept( msg, senderIP );
				break;
			case LANMessage::MSG_JOIN_DENY:						// Go away!  We don't want any!
				DEBUG_LOG(("LANAPI::update - got a MSG_JOIN_DENY from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleJoinDeny( msg, senderIP );
				break;

				// Leaving games, lobby
			case LANMessage::MSG_REQUEST_GAME_LEAVE:				// I'm outa here!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_GAME_LEAVE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestGameLeave( msg, senderIP );
				break;
			case LANMessage::MSG_REQUEST_LOBBY_LEAVE:				// I'm outa here!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_LOBBY_LEAVE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestLobbyLeave( msg, senderIP );
				break;

				// Game options, chat, etc
			case LANMessage::MSG_SET_ACCEPT:					// I'm cool with everything as is.
				handleSetAccept( msg, senderIP );
				break;
			case LANMessage::MSG_MAP_AVAILABILITY:		// Map status
				handleHasMap( msg, senderIP );
				break;
			case LANMessage::MSG_CHAT:								// Just spouting my mouth off.
				handleChat( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_START:					// Hold on; we're starting!
				handleGameStart( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_START_TIMER:
				handleGameStartTimer( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_OPTIONS:				// Here's some info about the game.
				DEBUG_LOG(("LANAPI::update - got a MSG_GAME_OPTIONS from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleGameOptions( msg, senderIP );
				break;
			case LANMessage::MSG_INACTIVE:		// someone is telling us that we're inactive.
				handleInActive( msg, senderIP );
				break;
			case LANMessage::MSG_CASTER_BEACON:	// a live game is available to cast.
				handleCasterBeacon( msg, senderIP, (UnsignedInt)m_transport->m_inBuffer[i].length );
				break;

			default:
				DEBUG_LOG(("Unknown LAN message type %d", msg->messageType));
			}

			// Mark it as read
			m_transport->m_inBuffer[i].length = 0;
		}
		else
		{
			break;
		}
	}
	if(LANbuttonPushed)
		return;



	// Send out periodic I'm Here messages
	if (now > s_resendDelta + m_lastResendTime)
	{
		m_lastResendTime = now;

		if (m_inLobby)
		{
			RequestSetName(m_name);
		}
		else if (m_currentGame && !m_currentGame->isGameInProgress())
		{
			if (AmIHost())
			{
				RequestGameOptions( GenerateGameOptionsString(), true );
				RequestGameAnnounce();
			}
			else
			{
#if TELL_COMPUTER_IDENTITY_IN_LAN_LOBBY
				AsciiString text;
				text.format("User=%s", m_userName.str());
				RequestGameOptions( text, true );
				text.format("Host=%s", m_hostName.str());
				RequestGameOptions( text, true );
#endif
				RequestGameOptions( "HELLO", false );
			}
		}
		else if (m_currentGame)
		{
			// game is in progress - RequestGameAnnounce will check if we should send it
			RequestGameAnnounce();
		}
	}

	Bool playerListChanged = false;
	Bool gameListChanged = false;

	// Weed out people we haven't heard from in a while
	LANPlayer *player = m_lobbyPlayers;
	while (player)
	{
		if (player->getLastHeard() + s_resendDelta*2 < now)
		{
			// He's gone!
			removePlayer(player);
			LANPlayer *nextPlayer = player->getNext();
			delete player;
			player = nextPlayer;
			playerListChanged = true;
		}
		else
		{
			player = player->getNext();
		}
	}

	// Weed out people we haven't heard from in a while
	LANGameInfo *game = m_games;
	while (game)
	{
		if (game != m_currentGame && game->getLastHeard() + s_resendDelta*2 < now)
		{
			// He's gone!
			removeGame(game);
			LANGameInfo *nextGame = game->getNext();
			delete game;
			game = nextGame;
			gameListChanged = true;
		}
		else
		{
			game = game->getNext();
		}
	}
	if ( m_currentGame && !m_currentGame->isGameInProgress() )
	{
		if ( !AmIHost() && (m_currentGame->getLastHeard() + s_resendDelta*16 < now) )
		{
			// We haven't heard from the host in a while.  Bail.
			// Actually, fake a host leaving message. :)
			LANMessage msg;
			fillInLANMessage( &msg );
			msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
			wcslcpy(msg.name, m_currentGame->getPlayerName(0).str(), ARRAY_SIZE(msg.name));
			handleRequestGameLeave(&msg, m_currentGame->getIP(0));
			UnicodeString text;
			text = TheGameText->fetch("LAN:HostNotResponding");
			OnChat(UnicodeString::TheEmptyString, m_localIP, text, LANCHAT_SYSTEM);
		}
		else if ( AmIHost() )
		{
			// Check each player for timeouts
			for (int p=1; p<MAX_SLOTS; ++p)
			{
				if (m_currentGame->getIP(p) && m_currentGame->getPlayerLastHeard(p) + s_resendDelta*8 < now)
				{
					LANMessage msg;
					fillInLANMessage( &msg );
					UnicodeString theStr;
					theStr.format(TheGameText->fetch("LAN:PlayerDropped"), m_currentGame->getPlayerName(p).str());
					msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
					wcslcpy(msg.name, m_currentGame->getPlayerName(p).str(), ARRAY_SIZE(msg.name));
					handleRequestGameLeave(&msg, m_currentGame->getIP(p));
					OnChat(UnicodeString::TheEmptyString, m_localIP, theStr, LANCHAT_SYSTEM);
				}
			}
		}
	}

	if (playerListChanged)
	{
		OnPlayerList(m_lobbyPlayers);
	}

	if (gameListChanged)
	{
		OnGameList(m_games);
	}

	// Resolve a pending cast once the host's game details have arrived
	if (m_pendingAction == ACT_CAST)
	{
		if (TheCaster == nullptr || !TheCaster->isCaster()
			|| !TheCaster->isSelectedGame(m_pendingCastKey)
			|| IsReadOnlyLanGameOptionsOpen())
		{
			m_pendingAction = ACT_NONE;
			m_pendingCastKey = LiveCasterGameKey();
		}
		else if (TheCaster->lobbyStatus() == CasterLobby::LOBBY_STATUS_OK)
		{
			LiveCasterGameKey key = m_pendingCastKey;
			m_pendingAction = ACT_NONE;
			m_pendingCastKey = LiveCasterGameKey();
			OnCastGame(RET_OK, key);
		}
	}

	// Time out old actions
	if (m_pendingAction != ACT_NONE && now > m_expiration)
	{
		switch (m_pendingAction)
		{
		case ACT_JOIN:
			OnGameJoin(RET_TIMEOUT, nullptr);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		case ACT_LEAVE:
			OnPlayerLeave(m_name);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		case ACT_JOINDIRECTCONNECT:
			OnGameJoin(RET_TIMEOUT, nullptr);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		case ACT_CAST:
			{
				LiveCasterGameKey key = m_pendingCastKey;
				m_pendingAction = ACT_NONE;
				m_pendingCastKey = LiveCasterGameKey();
				OnCastGame(RET_TIMEOUT, key);
			}
			break;
		default:
			m_pendingAction = ACT_NONE;
		}
	}

	// send out "game starting" messages
	if ( m_gameStartTime && m_gameStartSeconds && m_gameStartTime <= now )
	{
		// m_gameStartTime is when the next message goes out
		// m_gameStartSeconds is how many seconds remain in the message

		RequestGameStartTimer( m_gameStartSeconds );
	}
	else if (m_gameStartTime && m_gameStartTime <= now)
	{
//		DEBUG_LOG(("m_gameStartTime=%d, now=%d, m_gameStartSeconds=%d", m_gameStartTime, now, m_gameStartSeconds));
		ResetGameStartTimer();
		RequestGameStart();
	}

	// Check for an MOTD every few seconds
	static UnsignedInt lastMOTDCheck = 0;
	static const UnsignedInt motdInterval = 30000;
	if (now > lastMOTDCheck + motdInterval)
	{
		checkMOTD();
		lastMOTDCheck = now;
	}
}

// Request functions generate network traffic
void LANAPI::RequestLocations()
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_LOCATIONS;
	fillInLANMessage( &msg );
	sendMessage(&msg);
}

void LANAPI::RequestGameJoin( LANGameInfo *game, UnsignedInt ip /* = 0 */ )
{
	if (m_pendingAction == ACT_CAST)
	{
		m_pendingAction = ACT_NONE;
		m_pendingCastKey = LiveCasterGameKey();
	}

	if ((m_pendingAction != ACT_NONE) && (m_pendingAction != ACT_JOINDIRECTCONNECT))
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}

	if (!game)
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}

	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_JOIN;
	fillInLANMessage( &msg );
	msg.GameToJoin.gameIP = game->getSlot(0)->getIP();
	msg.GameToJoin.exeCRC = TheGlobalData->m_exeCRC;
	msg.GameToJoin.iniCRC = TheGlobalData->m_iniCRC;

	AsciiString s;
	GetStringFromRegistry("\\ergc", "", s);
	strlcpy(msg.GameToJoin.serial, s.str(), ARRAY_SIZE(msg.GameToJoin.serial));

	sendMessage(&msg, ip);

	m_pendingAction = ACT_JOIN;
	m_expiration = timeGetTime() + m_actionTimeout;
}

void LANAPI::RequestCastGame( LANGameInfo *game )
{
	if (game == nullptr)
		return;

	LiveCasterGameKey key(
		CasterProtocol::computeGameUid(game->getHostIP(), (UnsignedInt)game->getSeed()),
		game->getHostIP(), (UnsignedInt)game->getSeed());
	if (m_pendingAction != ACT_NONE)
	{
		OnCastGame( RET_BUSY, key );
		return;
	}

	m_pendingCastKey = key;
	m_pendingAction = ACT_CAST;
	m_expiration = timeGetTime() + m_actionTimeout;
}

void LANAPI::RequestGameJoinDirectConnect(UnsignedInt ipaddress)
{
	if (m_pendingAction != ACT_NONE)
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}

	if (ipaddress == 0)
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}

	m_directConnectRemoteIP = ipaddress;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_GAME_INFO;
	fillInLANMessage(&msg);
	msg.PlayerInfo.ip = GetLocalIP();
	wcslcpy(msg.PlayerInfo.playerName, m_name.str(), ARRAY_SIZE(msg.PlayerInfo.playerName));

	sendMessage(&msg, ipaddress);

	m_pendingAction = ACT_JOINDIRECTCONNECT;
	m_expiration = timeGetTime() + m_actionTimeout;
}

void LANAPI::RequestGameLeave()
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
	fillInLANMessage( &msg );
	wcslcpy(msg.PlayerInfo.playerName, m_name.str(), ARRAY_SIZE(msg.PlayerInfo.playerName));
	sendMessage(&msg);
	m_transport->update();  // Send immediately, before OnPlayerLeave below resets everything.

	if (m_currentGame && m_currentGame->getIP(0) == m_localIP)
	{
		// Exit out immediately if we're hosting
		OnPlayerLeave(m_name);
		removeGame(m_currentGame);
		m_currentGame = nullptr;
		m_inLobby = true;
	}
	else
	{
		m_pendingAction = ACT_LEAVE;
		m_expiration = timeGetTime() + m_actionTimeout;
	}
}

void LANAPI::RequestGameAnnounce()
{
	// In game - are we a game host?
	if (m_currentGame && !(m_currentGame->getIsDirectConnect()))
	{
		if (m_currentGame->getIP(0) == m_localIP || (m_currentGame->isGameInProgress() && TheNetwork && TheNetwork->isPacketRouter())) // if we're in game we should reply if we're the packet router
		{
			LANMessage reply;
			fillInLANMessage( &reply );
			reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;

			AsciiString gameOpts = GameInfoToAsciiString(m_currentGame);
			strlcpy(reply.GameInfo.options,gameOpts.str(), ARRAY_SIZE(reply.GameInfo.options));
			wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
			reply.GameInfo.inProgress = m_currentGame->isGameInProgress();
			reply.GameInfo.isDirectConnect = m_currentGame->getIsDirectConnect();

			sendMessage(&reply);
		}
	}
}

void LANAPI::RequestAccept()
{
	if (m_inLobby || !m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_SET_ACCEPT;
	msg.Accept.isAccepted = true;
	wcslcpy(msg.Accept.gameName, m_currentGame->getName().str(), ARRAY_SIZE(msg.Accept.gameName));
	sendMessage(&msg);
}

void LANAPI::RequestHasMap()
{
	if (m_inLobby || !m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_MAP_AVAILABILITY;
	msg.MapStatus.hasMap = m_currentGame->getSlot(m_currentGame->getLocalSlotNum())->hasMap();
	wcslcpy(msg.MapStatus.gameName, m_currentGame->getName().str(), ARRAY_SIZE(msg.MapStatus.gameName));
	CRC mapNameCRC;
//mapNameCRC.computeCRC(m_currentGame->getMap().str(), m_currentGame->getMap().getLength());
	AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(m_currentGame->getMap());
	mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
	msg.MapStatus.mapCRC = mapNameCRC.get();
	sendMessage(&msg);

	if (!msg.MapStatus.hasMap)
	{
		UnicodeString text;
		UnicodeString mapDisplayName;
		const MapMetaData *mapData = TheMapCache->findMap( m_currentGame->getMap() );
		Bool willTransfer = TRUE;
		if (mapData)
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
			if (mapData->m_isOfficial)
				willTransfer = FALSE;
		}
		else
		{
			mapDisplayName.format(L"%hs", TheGameState->getMapLeafName(m_currentGame->getMap()).str());
			willTransfer = WouldMapTransfer(m_currentGame->getMap());
		}
		if (willTransfer)
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMapWillTransfer"), mapDisplayName.str());
		else
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMap"), mapDisplayName.str());
		OnChat(L"SYSTEM", m_localIP, text, LANCHAT_SYSTEM);
	}
}

void LANAPI::RequestChat( UnicodeString message, ChatType format )
{
	LANMessage msg;
	fillInLANMessage( &msg );
	wcslcpy(msg.Chat.gameName, (m_currentGame) ? m_currentGame->getName().str() : L"", ARRAY_SIZE(msg.Chat.gameName));
	msg.messageType = LANMessage::MSG_CHAT;
	msg.Chat.chatType = format;
	wcslcpy(msg.Chat.message, message.str(), ARRAY_SIZE(msg.Chat.message));
	sendMessage(&msg);


	if (TheCaster != nullptr && TheCaster->isPlayer())
	{
		char utf8[CasterProtocol::MAX_FRAME_BYTES];
		char senderUtf8[65];
		UnsignedInt senderUtf8Len;
		Int localSlot = -1;
		UnsignedByte senderSlot = 0xFF;
		UnsignedByte senderTeam = 0;
		UnsignedInt senderIdentity;
		UnsignedInt utf8Len = CasterProtocol::wideToUtf8(message.str(),
			(UnsignedInt)message.getLength(), utf8, sizeof(utf8));
		senderUtf8Len = CasterProtocol::wideToUtf8(m_name.str(),
			(UnsignedInt)m_name.getLength(), senderUtf8, sizeof(senderUtf8));
		if (m_currentGame != nullptr)
		{
			GameSlot *slot;
			localSlot = m_currentGame->getLocalSlotNum();
			if (localSlot >= 0 && localSlot < MAX_SLOTS)
			{
				slot = m_currentGame->getSlot(localSlot);
				senderSlot = (UnsignedByte)localSlot;
				if (slot != nullptr)
					senderTeam = (UnsignedByte)slot->getTeamNumber();
			}
		}
		// The LAN address keeps concurrent player command IDs distinct. If the
		// lobby has no address yet, preserve a non-host slot identity instead.
		senderIdentity = (m_localIP != 0) ? m_localIP : (UnsignedInt)(senderSlot + 1);
		if (utf8Len != 0)
		{
			// TheSuperHackers @feature arcticdolphin 08/08/2026 Carries the stock
			// "/me " emote flag through to caster observers of this LAN lobby chat.
			TheCaster->sendLobbyChat(utf8, utf8Len, senderUtf8, senderUtf8Len,
				senderSlot, senderTeam, senderIdentity, (format == LANCHAT_EMOTE));
		}
	}

	OnChat(m_name, m_localIP, message, format);
}
extern LANAPI *TheLAN;

// Main-thread UI registrations; clear each one before destroying its window.
// Every screen that can host caster chat registers its own listbox, so the
// router picks a surface from the registry instead of sniffing layout names.
static GameWindow* s_readOnlyCasterChatWindow = nullptr;
static GameWindow* s_lanLobbyCasterChatWindow = nullptr;
static GameWindow* s_lanGameOptionsCasterChatWindow = nullptr;
static GameWindow* s_scoreScreenCasterChatWindow = nullptr;

void SetReadOnlyCasterChatWindow(GameWindow* chatWindow)
{
	s_readOnlyCasterChatWindow = chatWindow;
}

void SetLanLobbyCasterChatWindow(GameWindow* chatWindow)
{
	s_lanLobbyCasterChatWindow = chatWindow;
}

void SetLanGameOptionsCasterChatWindow(GameWindow* chatWindow)
{
	s_lanGameOptionsCasterChatWindow = chatWindow;
}

void SetScoreScreenCasterChatWindow(GameWindow* chatWindow)
{
	s_scoreScreenCasterChatWindow = chatWindow;
}


// A shell screen only hosts chat while its listbox is still shown. This is a
// window property check, not a screen-name match.
static Bool casterChatWindowShown(GameWindow* chatWindow)
{
	if (chatWindow == nullptr)
	{
		return FALSE;
	}
	return chatWindow->winIsHidden() ? FALSE : TRUE;
}


static CasterLobby::ChatSurface findCasterChatSurface(GameWindow** out)
{
	CasterLobby::ChatSurface surface = CasterLobby::CHAT_SURFACE_NONE;
	GameWindow *chatWindow = nullptr;
	GameWindow *readOnlyBox = s_readOnlyCasterChatWindow;
	GameWindow *lobbyBox = s_lanLobbyCasterChatWindow;
	GameWindow *gameBox = s_lanGameOptionsCasterChatWindow;
	GameWindow *scoreBox = s_scoreScreenCasterChatWindow;
	Bool readOnlyBoxPresent = FALSE;
	Bool lobbyBoxPresent = FALSE;
	Bool gameBoxPresent = FALSE;
	Bool scoreBoxPresent = FALSE;

	if (out != nullptr)
	{
		*out = nullptr;
	}

	// The read-only room owns the layout it registers, so registration alone
	// makes it active. The score screen listbox is the post-match LAN chat
	// (casters and players); a player's stock LAN chat never posts caster
	// lines, so caster lines routed there are not duplicated.
	readOnlyBoxPresent = (readOnlyBox != nullptr) ? TRUE : FALSE;
	lobbyBoxPresent = casterChatWindowShown(lobbyBox);
	gameBoxPresent = casterChatWindowShown(gameBox);
	scoreBoxPresent = casterChatWindowShown(scoreBox);

	surface = CasterLobby::selectActiveChatSurface(readOnlyBoxPresent, lobbyBoxPresent,
		gameBoxPresent, scoreBoxPresent);
	if (surface == CasterLobby::CHAT_SURFACE_READ_ONLY)
	{
		chatWindow = readOnlyBox;
	}
	else if (surface == CasterLobby::CHAT_SURFACE_LAN_LOBBY)
	{
		chatWindow = lobbyBox;
	}
	else if (surface == CasterLobby::CHAT_SURFACE_LAN_GAME_OPTIONS)
	{
		chatWindow = gameBox;
	}
	else if (surface == CasterLobby::CHAT_SURFACE_SCORE_SCREEN)
	{
		chatWindow = scoreBox;
	}


	if (out != nullptr)
	{
		*out = chatWindow;
	}
	return surface;
}

void LANAPI::postLocalCasterLine(const char* ascii, const char* senderName,
	UnsignedByte senderSlot, Bool senderIsCaster, Bool isEmote)
{
	GameWindow *chatWindow = nullptr;
	ChatMessage chat;
	UnicodeString rendered;
	Color chatColor = chatSystemColor;
	GameSlot *slot;

	if (ascii == NULL || ascii[0] == '\0')
	{
		return;
	}


	if (findCasterChatSurface(&chatWindow) == CasterLobby::CHAT_SURFACE_NONE
		|| chatWindow == nullptr)
	{

		DEBUG_LOG(("Caster: %s", ascii));
		return;
	}

	if (!MakeCasterChatMessage(chat, (UnsignedByte)CasterProtocol::CHAT_LOBBY, senderSlot,
			senderIsCaster, 0, 0, 0, senderName,
			(senderName != NULL) ? (UnsignedInt)strlen(senderName) : 0,
			ascii, (UnsignedInt)strlen(ascii), isEmote))
	{
		return;
	}
	if (!senderIsCaster && m_currentGame != nullptr && senderSlot < MAX_SLOTS)
	{
		slot = m_currentGame->getSlot(senderSlot);
		if (slot != nullptr)
		{
			chat.hasColor = ChatColorFromIndex(slot->getColor(), chat.color);
		}
	}
	else if (!senderIsCaster && TheCaster != nullptr)
	{
		// Casters have no current game: use the watched room's slot colours.
		TheCaster->applyCasterChatColor(chat);
	}
	RenderChatMessage(chat, chatSystemColor, rendered, chatColor);

	GadgetListBoxAddEntryText(chatWindow, rendered, chatColor, -1, -1);


	DEBUG_LOG(("Caster: %s", ascii));
}

CasterLobby::ChatSurface PostCasterLocalLine(const char* ascii, const char* senderName,
	UnsignedByte senderSlot, Bool senderIsCaster, Bool isEmote)
{
	CasterLobby::ChatSurface surface;


	if (TheLAN == nullptr)
	{
		return CasterLobby::CHAT_SURFACE_NONE;
	}
	if (ascii != NULL && ascii[0] != '\0')
	{
		TheLAN->postLocalCasterLine(ascii, senderName, senderSlot, senderIsCaster, isEmote);
	}

	surface = findCasterChatSurface(nullptr);
	return surface;
}

void LANAPI::RequestGameStart()
{
	if (m_inLobby || !m_currentGame || m_currentGame->getIP(0) != m_localIP)
		return;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_GAME_START;
	fillInLANMessage( &msg );
	sendMessage(&msg);
	m_transport->update(); // force a send

	OnGameStart();
}

void LANAPI::ResetGameStartTimer()
{
	if (m_gameStartTime)
		++m_gameStartRevision;
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;
}

void LANAPI::RequestGameStartTimer( Int seconds )
{
	if (m_inLobby || !m_currentGame || m_currentGame->getIP(0) != m_localIP)
		return;

	UnsignedInt now = timeGetTime();
	++m_gameStartRevision;
	m_gameStartTime = now + 1000;
	m_gameStartSeconds = (seconds) ? seconds - 1 : 0;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_GAME_START_TIMER;
	msg.StartTimer.seconds = seconds;
	fillInLANMessage( &msg );
	sendMessage(&msg);
	m_transport->update(); // force a send

	OnGameStartTimer(seconds);
	if (TheCaster != nullptr)
		TheCaster->forwardLobbyState();
}

void LANAPI::RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip /* = 0 */ )
{
	DEBUG_ASSERTCRASH(gameOptions.getLength() < m_lanMaxOptionsLength, ("Game options string is too long!"));

	if (!m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_GAME_OPTIONS;
	strlcpy(msg.GameOptions.options, gameOptions.str(), ARRAY_SIZE(msg.GameOptions.options));
	sendMessage(&msg, ip);

	m_lastGameopt = gameOptions;

	int player;
	for (player = 0; player<MAX_SLOTS; ++player)
	{
		if (m_currentGame->getIP(player) == m_localIP)
		{
			OnGameOptions(m_localIP, player, AsciiString(msg.GameOptions.options));
			break;
		}
	}

	// We can request game options (side, color, etc) while we don't have a slot yet.  Of course, we don't need to
	// call OnGameOptions for those, so it's okay to silently fail.
	//DEBUG_ASSERTCRASH(player != MAX_SLOTS, ("Requested game options, but we're not in slot list!");
}

void LANAPI::RequestGameCreate( UnicodeString gameName, Bool isDirectConnect )
{
	// No games of the same name should exist...  Ignore that for now.
	/// @todo: make sure LAN games with identical names don't crash things like in RA2.

	if ((!m_inLobby || m_currentGame) && !isDirectConnect)
	{
		DEBUG_ASSERTCRASH(m_inLobby && m_currentGame, ("Can't create a game while in one!"));
		OnGameCreate(LANAPIInterface::RET_BUSY);
		return;
	}

	if (m_pendingAction != ACT_NONE)
	{
		OnGameCreate(LANAPIInterface::RET_BUSY);
		return;
	}

	// Create the local game object
	m_inLobby = false;
	LANGameInfo *myGame = NEW LANGameInfo;

	myGame->setSeed(GetTickCount());

//	myGame->setInProgress(false);
	myGame->enterGame();
	UnicodeString s;
	s.format(L"%8.8X%8.8X", m_localIP, myGame->getSeed());
	if (gameName.isEmpty())
		s.concat(m_name);
	else
		s.concat(gameName);

	s.truncateTo(g_lanGameNameLength);

	DEBUG_LOG(("Setting local game name to '%ls'", s.str()));

	myGame->setName(s);

	LANGameSlot newSlot;
	newSlot.setState(SLOT_PLAYER, m_name);
	newSlot.setIP(m_localIP);
	newSlot.setPort(NETWORK_BASE_PORT_NUMBER); // LAN game, everyone has a unique IP, so it's ok to use the same port.
	newSlot.setLastHeard(0);
	newSlot.setLogin(m_userName);
	newSlot.setHost(m_hostName);

	myGame->setSlot(0,newSlot);
	myGame->setNext(nullptr);
	LANPreferences pref;

	AsciiString mapName = pref.getPreferredMap();

	myGame->setMap(mapName);
	myGame->setIsDirectConnect(isDirectConnect);

	myGame->setLastHeard(timeGetTime());
	m_currentGame = myGame;

/// @todo: Need to initialize the players elsewere.
/*	for (int player = 1; player < MAX_SLOTS; ++player)
	{
		myGame->setPlayerName(player, L"");
		myGame->setIP(player, 0);
		myGame->setAccepted(player, false);
	}*/

	// Add the game to the local game list
	addGame(myGame);

	// Send an announcement
	//RequestSlotList();
/*
	LANMessage msg;
	wcslcpy(msg.name, m_name.str(), ARRAY_SIZE(msg.name));
	wcscpy(msg.GameInfo.gameName, myGame->getName().str());
	for (player=0; player<MAX_SLOTS; ++player)
	{
		wcscpy(msg.GameInfo.name[player], myGame->getPlayerName(player).str());
		msg.GameInfo.ip[player] = myGame->getIP(player);
		msg.GameInfo.playerAccepted[player] = myGame->getAccepted(player);
	}
	msg.messageType = LANMessage::MSG_GAME_ANNOUNCE;
*/
	OnGameCreate(LANAPIInterface::RET_OK);
}


/*static const char slotListID		= 'S';
static const char gameOptionsID	= 'G';
static const char acceptID			= 'A';
static const char wannaStartID	= 'W';

AsciiString LANAPI::createSlotString()
{
	AsciiString slotList;
	slotList.concat(slotListID);
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		LANGameSlot *slot = GetMyGame()->getLANSlot(i);
		AsciiString str;
		if (slot->isHuman())
		{
			str = "H";
			LANPlayer *user = slot->getUser();
			DEBUG_ASSERTCRASH(user, ("Human player has no User*!"));
			AsciiString name;
			name.translate(user->getName());
			str.concat(name);
			str.concat(',');
		}
		else if (slot->isAI())
		{
			if (slot->getState() == SLOT_EASY_AI)
				str = "CE,";
			if (slot->getState() == SLOT_MED_AI)
				str = "CM,";
			else
				str = "CB,";
		}
		else if (slot->getState() == SLOT_OPEN)
		{
			str = "O,";
		}
		else if (slot->getState() == SLOT_CLOSED)
		{
			str = "X,";
		}
		else
		{
			DEBUG_CRASH(("Bad slot type"));
			str = "X,";
		}

		slotList.concat(str);
	}
	return slotList;
}
*/
/*
void LANAPI::RequestSlotList()
{

	LANMessage reply;
	reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;
	wcslcpy(reply.name, m_name.str(), ARRAY_SIZE(reply.name));
	int player;
	for (player = 0; player < MAX_SLOTS; ++player)
	{
		wcslcpy(reply.GameInfo.name[player], m_currentGame->getPlayerName(player).str(), ARRAY_SIZE(reply.GameInfo.name[player]));
		reply.GameInfo.ip[player] = m_currentGame->getIP(player);
		reply.GameInfo.playerAccepted[player] = m_currentGame->getSlot(player)->isAccepted();
	}
	wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
	reply.GameInfo.inProgress = m_currentGame->isGameInProgress();

	sendMessage(&reply);

	OnSlotList(LANAPIInterface::RET_OK, m_currentGame);
}
*/
void LANAPI::RequestSetName( UnicodeString newName )
{
	newName.trim();
	if (m_pendingAction != ACT_NONE)
	{
		// Can't change name while joining games
		OnNameChange(m_localIP, newName);
		return;
	}

	// Set up timer
	m_lastResendTime = timeGetTime();

	if (m_inLobby && m_pendingAction == ACT_NONE)
	{
		m_name = newName;
		LANMessage msg;
		fillInLANMessage( &msg );
		msg.messageType = LANMessage::MSG_LOBBY_ANNOUNCE;
		sendMessage(&msg);

		// Update the interface
		LANPlayer *player = LookupPlayer(m_localIP);
		if (!player)
		{
			player = NEW LANPlayer;
			player->setIP(m_localIP);
		}
		else
		{
			removePlayer(player);
		}
		player->setName(m_name);
		player->setHost(m_hostName);
		player->setLogin(m_userName);
		player->setLastHeard(timeGetTime());

		addPlayer(player);

		OnNameChange(player->getIP(), player->getName());
	}
}

void LANAPI::fillInLANMessage( LANMessage *msg )
{
	if (!msg)
		return;

	wcslcpy(msg->name, m_name.str(), ARRAY_SIZE(msg->name));
	strlcpy(msg->userName, m_userName.str(), ARRAY_SIZE(msg->userName));
	strlcpy(msg->hostName, m_hostName.str(), ARRAY_SIZE(msg->hostName));
}

void LANAPI::RequestLobbyLeave( Bool forced )
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_LOBBY_LEAVE;
	fillInLANMessage( &msg );
	sendMessage(&msg);

	if (forced)
		m_transport->update();
}

// Misc utility functions
LANGameInfo * LANAPI::LookupGame( UnicodeString gameName )
{
	LANGameInfo *theGame = m_games;

	while (theGame && theGame->getName() != gameName)
	{
		theGame = theGame->getNext();
	}

	return theGame; // null means we didn't find anything.
}

LANGameInfo * LANAPI::LookupGameByListOffset( Int offset )
{
	LANGameInfo *theGame = m_games;

	if (offset < 0)
		return nullptr;

	while (offset-- && theGame)
	{
		theGame = theGame->getNext();
	}

	return theGame; // null means we didn't find anything.
}

LANGameInfo* LANAPI::LookupGameByHost(UnsignedInt hostIP)
{
	LANGameInfo* lastGame = nullptr;
	UnsignedInt lastHeard = 0;

	for (LANGameInfo* game = m_games; game; game = game->getNext())
	{
		if (game->getHostIP() == hostIP && game->getLastHeard() >= lastHeard)
		{
			lastGame = game;
			lastHeard = game->getLastHeard();
		}
	}

	return lastGame;
}

LANGameInfo* LANAPI::LookupGameBySenderIP(UnsignedInt senderIP)
{
	LANGameInfo* lastGame = nullptr;
	UnsignedInt lastHeard = 0;

	for (LANGameInfo* game = m_games; game; game = game->getNext())
	{
		Bool match = (game->getHostIP() == senderIP);
		Int slot;


		if (!match)
		{
			for (slot = 0; slot < MAX_SLOTS; ++slot)
			{
				if (game->getIP(slot) == senderIP)
				{
					match = TRUE;
					break;
				}
			}
		}

		if (match && game->getLastHeard() >= lastHeard)
		{
			lastGame = game;
			lastHeard = game->getLastHeard();
		}
	}

	return lastGame;
}



Bool GetCasterGameName(char* out, UnsignedInt cap)
{
	LANGameInfo* game;
	UnicodeString name;
	UnsignedInt nameLen;

	if (out == NULL || cap == 0)
	{
		return FALSE;
	}
	out[0] = '\0';


	if (TheLAN == NULL)
	{
		return FALSE;
	}
	game = TheLAN->GetMyGame();
	if (game == NULL)
	{
		return FALSE;
	}

	name = game->getName();
	nameLen = CasterProtocol::wideToUtf8(name.str(), (UnsignedInt)name.getLength(), out,
		cap - 1);
	if (nameLen == 0)
	{
		out[0] = '\0';
		return FALSE;
	}
	out[nameLen] = '\0';
	return TRUE;
}

Bool GetCasterGameSeed(UnsignedInt& hostIP, UnsignedInt& seed)
{
	LANGameInfo* game;

	hostIP = 0;
	seed = 0;

	if (TheLAN == NULL)
	{
		return FALSE;
	}
	game = TheLAN->GetMyGame();
	if (game == NULL)
	{
		return FALSE;
	}

	// The room name retains its creation identity while the simulation seed changes.
	UnicodeString name = game->getName();
	return CasterBeacon::decodeRoomIdentity(name.str(), (UnsignedInt)name.getLength(), hostIP, seed);
}

Bool GetCasterLobbyState(char* out, UnsignedInt cap, UnsignedInt& lenOut)
{
	LANGameInfo* game;
	AsciiString options;

	lenOut = 0;
	if (out == NULL || cap == 0)
	{
		return FALSE;
	}
	out[0] = '\0';

	if (TheLAN == NULL)
	{
		return FALSE;
	}
	game = TheLAN->GetMyGame();
	if (game == NULL || !game->amIHost())
	{
		return FALSE;
	}

	AsciiString gameOptions = GameInfoToAsciiString(game);
	if (gameOptions.isEmpty())
		return FALSE;
	options.format("DS=%d;DR=%08X;", TheLAN->GetGameStartSeconds(), TheLAN->GetGameStartRevision());
	options.concat(gameOptions);
	if ((UnsignedInt)options.getLength() >= cap)
	{

		return FALSE;
	}
	strcpy(out, options.str());
	lenOut = (UnsignedInt)options.getLength();
	return TRUE;
}

Bool GetCasterMatchOptions(char* out, UnsignedInt cap, UnsignedInt& lenOut)
{
	LANGameInfo* game;
	AsciiString options;

	lenOut = 0;
	if (out == NULL || cap == 0)
	{
		return FALSE;
	}
	out[0] = '\0';

	if (TheLAN == NULL)
	{
		return FALSE;
	}
	// Every player in the game mirrors the same slot list, so any of them can
	// publish the match start; the host gate belongs to the lobby mirror only.
	game = TheLAN->GetMyGame();
	if (game == NULL)
	{
		return FALSE;
	}

	options = GameInfoToAsciiString(game);
	if (options.isEmpty())
	{
		return FALSE;
	}
	if ((UnsignedInt)options.getLength() >= cap)
	{
		return FALSE;
	}
	strcpy(out, options.str());
	lenOut = (UnsignedInt)options.getLength();
	return TRUE;
}

void LANAPI::handleCasterBeacon( LANMessage *msg, UnsignedInt senderIP, UnsignedInt received )
{
	LANGameInfo* game;
	CasterBeacon::Payload payload;
	AsciiString ipText;
	char gameName[CASTER_GAME_NAME_BYTES + 1];
	UnicodeString name;
	UnsignedInt nameLen;

	if (TheCaster == nullptr || !TheCaster->isCaster())
	{
		return;
	}
	if (msg == nullptr)
	{
		return;
	}

	ipText.format("%d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP));
	// A short datagram is an older build's packed reply: incompatible, not malformed.
	if (received < sizeof(LANMessage)
		|| !CasterBeacon::isProtocolCompatible(msg->Caster.protocolVersion))
	{
		DEBUG_LOG(("LANAPI::handleCasterBeacon - incompatible caster protocol from %s", ipText.str()));
		TheCaster->onIncompatibleSource(ipText.str());
		return;
	}
	if (!CasterBeacon::parsePayload(msg->Caster.uid, ARRAY_SIZE(msg->Caster.uid),
			msg->Caster.tcpPort, msg->Caster.gameUid, payload))
	{

		return;
	}
	game = LookupGameBySenderIP(senderIP);
	if (game == nullptr)
	{
		DEBUG_LOG(("LANAPI::handleCasterBeacon - no game for sender %d.%d.%d.%d",
			PRINTF_IP_AS_4_INTS(senderIP)));
		return;
	}

	gameName[0] = '\0';
	name = game->getName();
	nameLen = CasterProtocol::wideToUtf8(name.str(), (UnsignedInt)name.getLength(),
		gameName, sizeof(gameName) - 1);
	if (nameLen == 0)
	{

		return;
	}
	gameName[nameLen] = '\0';

	DEBUG_LOG(("LANAPI::handleCasterBeacon - uid %s from %s (%s) game %8.8X", payload.uid,
		ipText.str(), gameName, payload.gameUid));

	TheCaster->onBeacon(payload.gameUid, payload.uid, ipText.str(), payload.tcpPort, gameName,
		(senderIP == game->getHostIP()) ? TRUE : FALSE);
}

void LANAPI::removeGame( LANGameInfo *game )
{
	if (!game)
	{
		return;
	}
	LiveCasterGameKey key(
		CasterProtocol::computeGameUid(game->getHostIP(), (UnsignedInt)game->getSeed()),
		game->getHostIP(), (UnsignedInt)game->getSeed());
	OnReadOnlyCasterGameRemoved(key, game->isGameInProgress());
	LANGameInfo *g = m_games;
	if (m_games == game)
	{
		m_games = m_games->getNext();
	}
	else
	{
		while (g->getNext() && g->getNext() != game)
		{
			g = g->getNext();
		}
		if (g->getNext() == game)
		{
			g->setNext(game->getNext());
		}
		else
		{
			// Odd.  We went the whole way without finding it in the list.
			DEBUG_CRASH(("LANGameInfo wasn't in the list"));
		}
	}
}

LANPlayer * LANAPI::LookupPlayer( UnsignedInt playerIP )
{
	LANPlayer *thePlayer = m_lobbyPlayers;

	while (thePlayer && thePlayer->getIP() != playerIP)
	{
		thePlayer = thePlayer->getNext();
	}

	return thePlayer; // null means we didn't find anything.
}

void LANAPI::removePlayer( LANPlayer *player )
{
	LANPlayer *p = m_lobbyPlayers;
	if (!player)
	{
		return;
	}
	else if (m_lobbyPlayers == player)
	{
		m_lobbyPlayers = m_lobbyPlayers->getNext();
	}
	else
	{
		while (p->getNext() && p->getNext() != player)
		{
			p = p->getNext();
		}
		if (p->getNext() == player)
		{
			p->setNext(player->getNext());
		}
		else
		{
			// Odd.  We went the whole way without finding it in the list.
			DEBUG_CRASH(("LANPlayer wasn't in the list"));
		}
	}
}

void LANAPI::addGame( LANGameInfo *game )
{
	if (!m_games)
	{
		m_games = game;
		game->setNext(nullptr);
		return;
	}
	else
	{
		if (game->getName().compareNoCase(m_games->getName()) < 0)
		{
			game->setNext(m_games);
			m_games = game;
			return;
		}
		else
		{
			LANGameInfo *g = m_games;
			while (g->getNext() && g->getNext()->getName().compareNoCase(game->getName()) > 0)
			{
				g = g->getNext();
			}
			game->setNext(g->getNext());
			g->setNext(game);
			return;
		}
	}
}

void LANAPI::addPlayer( LANPlayer *player )
{
	if (!m_lobbyPlayers)
	{
		m_lobbyPlayers = player;
		player->setNext(nullptr);
		return;
	}
	else
	{
		if (player->getName().compareNoCase(m_lobbyPlayers->getName()) < 0)
		{
			player->setNext(m_lobbyPlayers);
			m_lobbyPlayers = player;
			return;
		}
		else
		{
			LANPlayer *p = m_lobbyPlayers;
			while (p->getNext() && p->getNext()->getName().compareNoCase(player->getName()) > 0)
			{
				p = p->getNext();
			}
			player->setNext(p->getNext());
			p->setNext(player);
			return;
		}
	}
}

Bool LANAPI::SetLocalIP( UnsignedInt localIP )
{
	Bool retval = TRUE;
	m_localIP = localIP;

	m_transport->reset();
	retval = m_transport->init(m_localIP, lobbyPort);
	m_transport->allowBroadcasts(true);

	return retval;
}

void LANAPI::SetLocalIP( AsciiString localIP )
{
	UnsignedInt resolvedIP = ResolveIP(localIP);
	SetLocalIP(resolvedIP);
}

Bool RequestCasterHostDetails(const char* hostIp)
{
	return TheLAN != NULL && TheLAN->RequestCasterDetails(hostIp);
}

Bool GetCasterGamePlayerIPs(const char* hostIp, UnsignedInt* out, UnsignedInt cap, UnsignedInt& count)
{
	LANGameInfo* game;

	count = 0;
	if (TheLAN == NULL || hostIp == NULL || out == NULL)
	{
		return FALSE;
	}
	game = TheLAN->LookupGameByHost(ResolveIP(AsciiString(hostIp)));
	if (game == NULL)
	{
		return FALSE;
	}
	for (Int i = 0; i < MAX_SLOTS && count < cap; ++i)
	{
		LANGameSlot* slot = game->getLANSlot(i);
		if (slot != NULL && slot->isHuman() && slot->getIP() != 0 && slot->getIP() != TheLAN->GetLocalIP())
		{
			out[count++] = slot->getIP();
		}
	}
	return count > 0;
}

Bool GetCasterGameSlotColors(UnsignedInt hostIP, UnsignedInt seed, Int* out, UnsignedInt cap,
	UnsignedInt& count)
{
	LANGameInfo* game;
	Int i;

	count = 0;
	if (TheLAN == NULL || out == NULL || cap == 0 || hostIP == 0)
	{
		return FALSE;
	}
	game = TheLAN->LookupGameByHost(hostIP);
	if (game == NULL || (UnsignedInt)game->getSeed() != seed)
	{
		// A different game now lives on that host: its colours are not ours.
		return FALSE;
	}
	for (i = 0; i < MAX_SLOTS && count < cap; ++i)
	{
		const GameSlot* slot = game->getConstSlot(i);
		out[count++] = (slot != NULL && slot->isOccupied()) ? slot->getColor() : -1;
	}
	return (count != 0) ? TRUE : FALSE;
}

Bool LANAPI::RequestCasterDetails(const char* hostIp)
{
	if (m_transport == NULL || hostIp == NULL || hostIp[0] == '\0')
	{
		return FALSE;
	}
	UnsignedInt ip = ResolveIP(AsciiString(hostIp));
	if (ip == 0 || ip == 0xFFFFFFFFu || ip == m_localIP)
	{
		return FALSE;
	}
	LANMessage request;
	fillInLANMessage(&request);
	request.messageType = LANMessage::MSG_REQUEST_GAME_INFO;

	return m_transport->queueSend(ip, lobbyPort, (const UnsignedByte*)&request, sizeof(request));
}

void LANAPI::ReplyCasterDetails(UnsignedInt targetIP)
{
	// Any authenticated player in the active game may answer a details probe.
	// Casters still isolate sources by the announced game UID, so this does
	// not turn a generic lobby peer into a source for the selected game.
	if (m_transport == NULL || TheCaster == NULL || !TheCaster->isPlayer()
		|| TheCaster->announcedGameUid() == 0 || targetIP == 0 || targetIP == m_localIP)
	{
		return;
	}
	LANMessage reply;
	// The reply travels as a full-size LAN message: zero the unused union bytes
	// so no stack contents reach the wire.
	memset(&reply, 0, sizeof(reply));
	fillInLANMessage(&reply);
	reply.messageType = LANMessage::MSG_CASTER_BEACON;
	if (CasterBeacon::buildMessage(TheCaster->casterUid(), TheCaster->serverPort(),
			TheCaster->announcedGameUid(), reply.Caster.uid, ARRAY_SIZE(reply.Caster.uid),
			reply.Caster.tcpPort, reply.Caster.protocolVersion, reply.Caster.gameUid))
	{
		m_transport->queueSend(targetIP, lobbyPort, (const UnsignedByte*)&reply, sizeof(reply));
	}
}

Bool LANAPI::AmIHost()
{
	return m_currentGame && m_currentGame->getIP(0) == m_localIP;
}

void LANAPI::setIsActive(Bool isActive) {
	DEBUG_LOG(("LANAPI::setIsActive - entering"));
	if (isActive != m_isActive) {
		DEBUG_LOG(("LANAPI::setIsActive - m_isActive changed to %s", isActive ? "TRUE" : "FALSE"));
		if (isActive == FALSE) {
			if ((m_inLobby == FALSE) && (m_currentGame != nullptr)) {
				LANMessage msg;
				fillInLANMessage( &msg );
				msg.messageType = LANMessage::MSG_INACTIVE;
				sendMessage(&msg);
				DEBUG_LOG(("LANAPI::setIsActive - sent an IsActive message"));
			}
		}
	}
	m_isActive = isActive;
}
