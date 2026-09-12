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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LANAPIHandlers.cpp
// Author: Matthew D. Campbell, October 2001
// Description: LAN callback handlers
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/Registry.h"
#include "Common/GlobalData.h"
#include "Common/QuotedPrintable.h"
#include "Common/UserPreferences.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameClient/MapUtil.h"

void LANAPI::handleRequestLocations( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANMessage reply;
		fillInLANMessage( &reply );
		reply.messageType = LANMessage::MSG_LOBBY_ANNOUNCE;

		sendMessage(&reply);
		m_lastResendTime = timeGetTime();
	}
	else
	{
		// In game - are we a game host?
		if (m_currentGame)
		{
			if (m_currentGame->getIP(0) == m_localIP)
			{
				LANMessage reply;
				fillInLANMessage( &reply );
				reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;
				AsciiString gameOpts = GenerateGameOptionsString();
				strlcpy(reply.GameInfo.options, gameOpts.str(), ARRAY_SIZE(reply.GameInfo.options));
				wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
				reply.GameInfo.inProgress = m_currentGame->isGameInProgress();
				reply.GameInfo.isDirectConnect = m_currentGame->getIsDirectConnect();

				sendMessage(&reply);
			}
			else
			{
				// We're a joiner
			}
		}
	}
	// Add the player to the lobby player list
	LANPlayer *player = LookupPlayer(senderIP);
	if (!player)
	{
		player = NEW LANPlayer;
		player->setIP(senderIP);
	}
	else
	{
		removePlayer(player);
	}
	player->setName(UnicodeString(msg->name));
	player->setHost(msg->hostName);
	player->setLogin(msg->userName);
	player->setLastHeard(timeGetTime());

	addPlayer(player);

	OnNameChange(player->getIP(), player->getName());
}

void LANAPI::handleGameAnnounce( LANMessage *msg, UnsignedInt senderIP )
{
	if (senderIP == m_localIP)
	{
		return; // Don't try to update own info
	}
	else if (m_currentGame && m_currentGame->isGameInProgress())
	{
		return; // Don't care about games if we're playing
	}
	else if (senderIP == m_directConnectRemoteIP)
	{

		if (m_currentGame == nullptr)
		{
			LANGameInfo *game = LookupGame(UnicodeString(msg->GameInfo.gameName));
			if (!game)
			{
				game = NEW LANGameInfo;
				game->setName(UnicodeString(msg->GameInfo.gameName));
				addGame(game);
			}
			Bool success = ParseGameOptionsString(game,AsciiString(msg->GameInfo.options));
			game->setGameInProgress(msg->GameInfo.inProgress);
			game->setIsDirectConnect(msg->GameInfo.isDirectConnect);
			game->setLastHeard(timeGetTime());
			if (!success)
			{
				// remove from list
				removeGame(game);
				delete game;
				return;
			}
			RequestGameJoin(game, m_directConnectRemoteIP);
		}
	}
	else
	{
		LANGameInfo *game = LookupGame(UnicodeString(msg->GameInfo.gameName));
		if (!game)
		{
			game = NEW LANGameInfo;
			game->setName(UnicodeString(msg->GameInfo.gameName));
			addGame(game);
		}
		Bool success = ParseGameOptionsString(game,AsciiString(msg->GameInfo.options));
		game->setGameInProgress(msg->GameInfo.inProgress);
		game->setIsDirectConnect(msg->GameInfo.isDirectConnect);
		game->setLastHeard(timeGetTime());
		if (!success)
		{
			// remove from list
			removeGame(game);
			delete game;
			game = nullptr;
		}

		OnGameList( m_games );
	//	if (game == m_currentGame && !m_inLobby)
	//		OnSlotList(RET_OK, game);
	}
}

void LANAPI::handleLobbyAnnounce( LANMessage *msg, UnsignedInt senderIP )
{
	LANPlayer *player = LookupPlayer(senderIP);
	if (!player)
	{
		player = NEW LANPlayer;
		player->setIP(senderIP);
	}
	else
	{
		removePlayer(player);
	}
	player->setName(UnicodeString(msg->name));
	player->setHost(msg->hostName);
	player->setLogin(msg->userName);
	player->setLastHeard(timeGetTime());

	addPlayer(player);

	OnNameChange(player->getIP(), player->getName());
}

void LANAPI::handleRequestGameInfo( LANMessage *msg, UnsignedInt senderIP )
{
	// In game - are we a game host?
	if (m_currentGame)
	{
		if (m_currentGame->getIP(0) == m_localIP || (m_currentGame->isGameInProgress() && TheNetwork && TheNetwork->isPacketRouter())) // if we're in game we should reply if we're the packet router
		{
			LANMessage reply;
			fillInLANMessage( &reply );
			reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;

#if !RETAIL_COMPATIBLE_NETWORKING
			// TheSuperHackers @info arcticdolphin 02/03/2026 Omit SD= from announces; seed is negotiated via commit-reveal.
			AsciiString gameOpts = GameInfoToAsciiString(m_currentGame, FALSE);
#else
			AsciiString gameOpts = GameInfoToAsciiString(m_currentGame);
#endif
			strlcpy(reply.GameInfo.options,gameOpts.str(), ARRAY_SIZE(reply.GameInfo.options));
			wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
			reply.GameInfo.inProgress = m_currentGame->isGameInProgress();
			reply.GameInfo.isDirectConnect = m_currentGame->getIsDirectConnect();

			sendMessage(&reply, senderIP);
		}
	}
}

static Bool IsInvalidCharForPlayerName(const WideChar c)
{
	return c < L' ' // C0 control chars
		|| c == L',' || c == L':' || c == L';' // chars used for strtok in ParseAsciiStringToGameInfo
		|| (c >= L'\x007f' && c <= L'\x009f') // DEL + C1 control chars
		|| c == L'\x2028' || c == L'\x2029' // line and paragraph separators
		|| (c >= L'\xdc00' && c <= L'\xdfff') // low surrogate, for chars beyond the Unicode Basic Multilingual Plane
		|| (c >= L'\xd800' && c <= L'\xdbff'); // high surrogate, for chars beyond the BMP
}

static Bool IsSpaceCharacter(const WideChar c)
{
	return c == L' ' // space
		|| c == L'\xA0' // no-break space
		|| c == L'\x1680' // ogham space mark
		|| (c >= L'\x2000' && c <= L'\x200A') // en/em spaces, figure, punctuation, thin, hair
		|| c == L'\x202F' // narrow no-break space
		|| c == L'\x205F' // medium mathematical space
		|| c == L'\x3000'; // ideographic space
}

static Bool ContainsInvalidChars(const WideChar* playerName)
{
	DEBUG_ASSERTCRASH(playerName != nullptr, ("playerName is null"));
	while (*playerName)
	{
		if (IsInvalidCharForPlayerName(*playerName++))
			return true;
	}

	return false;
}

static Bool ContainsAnyReadableChars(const WideChar* playerName)
{
	DEBUG_ASSERTCRASH(playerName != nullptr, ("playerName is null"));
	while (*playerName)
	{
		if (!IsSpaceCharacter(*playerName++))
			return true;
	}

	return false;
}

void LANAPI::handleRequestJoin( LANMessage *msg, UnsignedInt senderIP )
{
	UnsignedInt responseIP = senderIP;	// need this cause the player may or may not be
																			// in the player list at the sendMessage.

	if (msg->GameToJoin.gameIP != m_localIP)
	{
		return; // Not us.  Ignore it.
	}
	LANMessage reply;
	fillInLANMessage( &reply );
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == m_localIP)
	{
		if (m_currentGame->isGameInProgress())
		{
			reply.messageType = LANMessage::MSG_JOIN_DENY;
			reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_STARTED;
			reply.GameNotJoined.gameIP = m_localIP;
			reply.GameNotJoined.playerIP = senderIP;
			DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because game already started."));
		}
		else
		{
			int player;
			Bool canJoin = true;

			// see if the CRCs match
#if defined(RTS_DEBUG)
			if (TheGlobalData->m_netMinPlayers > 0) {
#endif
// TheSuperHackers @todo Enable CRC checks!
#if !RTS_ZEROHOUR
			if (msg->GameToJoin.iniCRC != TheGlobalData->m_iniCRC ||
					msg->GameToJoin.exeCRC != TheGlobalData->m_exeCRC)
			{
				DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of CRC mismatch. CRCs are them/us INI:%X/%X exe:%X/%X",
					msg->GameToJoin.iniCRC, TheGlobalData->m_iniCRC,
					msg->GameToJoin.exeCRC, TheGlobalData->m_exeCRC));
				reply.messageType = LANMessage::MSG_JOIN_DENY;
				reply.GameNotJoined.reason = LANAPIInterface::RET_CRC_MISMATCH;
				reply.GameNotJoined.gameIP = m_localIP;
				reply.GameNotJoined.playerIP = senderIP;
				canJoin = false;
			}
#endif
#if defined(RTS_DEBUG)
			}
#endif

// TheSuperHackers @tweak Disables the duplicate serial check
#if 0
			// check for a duplicate serial
			AsciiString s;
			for (player = 0; canJoin && player<MAX_SLOTS; ++player)
			{
				LANGameSlot *slot = m_currentGame->getLANSlot(player);
				s.clear();
				if (player == 0)
				{
					GetStringFromRegistry("\\ergc", "", s);
				}
				else if (slot->isHuman())
				{
					s = slot->getSerial();
					if (s.isEmpty())
						s = "<Munkee>";
				}

				if (s.isNotEmpty())
				{
					DEBUG_LOG(("Checking serial '%s' in slot %d", s.str(), player));

					if (!strncmp(s.str(), msg->GameToJoin.serial, g_maxSerialLength))
					{
						// serials match!  kick the punk!
						reply.messageType = LANMessage::MSG_JOIN_DENY;
						reply.GameNotJoined.reason = LANAPIInterface::RET_SERIAL_DUPE;
						reply.GameNotJoined.gameIP = m_localIP;
						reply.GameNotJoined.playerIP = senderIP;
						canJoin = false;

						DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of duplicate serial # (%s).", s.str()));
						break;
					}
				}
			}
#endif

			// TheSuperHackers @bugfix slurmlord 18/09/2025 need to validate the name of the connecting player before
			// allowing them to join to prevent messing up the format of game state string. Commas, colons, semicolons etc.
			// should not be in a player name. It should also not consist of only space characters.
			if (canJoin)
			{
				if (ContainsInvalidChars(msg->name) || !ContainsAnyReadableChars(msg->name))
				{
					// Just deny with a duplicate name reason, for backwards compatibility with retail
					reply.messageType = LANMessage::MSG_JOIN_DENY;
					reply.GameNotJoined.reason = LANAPIInterface::RET_DUPLICATE_NAME;
					reply.GameNotJoined.gameIP = m_localIP;
					reply.GameNotJoined.playerIP = senderIP;
					canJoin = false;

					DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of illegal characters in the player name."));
				}
			}

			// Then see if the player has a duplicate name
			for (player = 0; canJoin && player<MAX_SLOTS; ++player)
			{
				LANGameSlot *slot = m_currentGame->getLANSlot(player);
				if (slot->isHuman() && slot->getName().compare(msg->name) == 0)
				{
					// just deny duplicates
					reply.messageType = LANMessage::MSG_JOIN_DENY;
					reply.GameNotJoined.reason = LANAPIInterface::RET_DUPLICATE_NAME;
					reply.GameNotJoined.gameIP = m_localIP;
					reply.GameNotJoined.playerIP = senderIP;
					canJoin = false;

					DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of duplicate names."));
					break;
				}
			}

			// TheSuperHackers @bugfix Stubbjax 26/09/2025 Players can now join open slots regardless of starting spots on the map.
			for (player = 0; canJoin && player<MAX_SLOTS; ++player)
			{
				if (m_currentGame->getLANSlot(player)->isOpen())
				{
					// OK, add him in.
					reply.messageType = LANMessage::MSG_JOIN_ACCEPT;
					wcslcpy(reply.GameJoined.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameJoined.gameName));
					reply.GameJoined.slotPosition = player;
					reply.GameJoined.gameIP = m_localIP;
					reply.GameJoined.playerIP = senderIP;

					LANGameSlot newSlot;
					newSlot.setState(SLOT_PLAYER, UnicodeString(msg->name));
					newSlot.setIP(senderIP);
					newSlot.setPort(NETWORK_BASE_PORT_NUMBER);
					newSlot.setLastHeard(timeGetTime());
					newSlot.setSerial(msg->GameToJoin.serial);
					m_currentGame->setSlot(player,newSlot);
					DEBUG_LOG(("LANAPI::handleRequestJoin - added player %ls at ip 0x%08x to the game", msg->name, senderIP));

					OnPlayerJoin(player, UnicodeString(msg->name));
					responseIP = 0;

					break;
				}
			}

			if (canJoin && player == MAX_SLOTS)
			{
				reply.messageType = LANMessage::MSG_JOIN_DENY;
				wcslcpy(reply.GameNotJoined.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameNotJoined.gameName));
				reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_FULL;
				reply.GameNotJoined.gameIP = m_localIP;
				reply.GameNotJoined.playerIP = senderIP;
				DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because game is full."));
			}
		}
	}
	else
	{
		reply.messageType = LANMessage::MSG_JOIN_DENY;
		reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_GONE;
		reply.GameNotJoined.gameIP = m_localIP;
		reply.GameNotJoined.playerIP = senderIP;
	}
	sendMessage(&reply, responseIP);
	RequestGameOptions(GenerateGameOptionsString(), true);
}

void LANAPI::handleJoinAccept( LANMessage *msg, UnsignedInt senderIP )
{
	if (msg->GameJoined.playerIP == m_localIP) // Is it for us?
	{
		if (m_pendingAction == ACT_JOIN) // Are we trying to join?
		{
			m_currentGame = LookupGame(UnicodeString(msg->GameJoined.gameName));

			if (!m_currentGame)
			{
				DEBUG_CRASH(("Could not find game to join!"));
				OnGameJoin(RET_UNKNOWN, nullptr);
			}
			else
			{
				m_inLobby = false;
				AsciiString options = GameInfoToAsciiString(m_currentGame);
				m_currentGame->enterGame();
				ParseAsciiStringToGameInfo(m_currentGame, options);

				Int pos = msg->GameJoined.slotPosition;

				LANGameSlot slot;
				slot.setState(SLOT_PLAYER, m_name);
				slot.setIP(m_localIP);
				slot.setPort(NETWORK_BASE_PORT_NUMBER);
				slot.setLastHeard(0);
				slot.setLogin(m_userName);
				slot.setHost(m_hostName);
				m_currentGame->setSlot(pos, slot);

				m_currentGame->getLANSlot(0)->setHost(msg->hostName);
				m_currentGame->getLANSlot(0)->setLogin(msg->userName);

				LANPreferences prefs;
				AsciiString entry;
				entry.format("%d.%d.%d.%d:%s", PRINTF_IP_AS_4_INTS(senderIP), UnicodeStringToQuotedPrintable(m_currentGame->getSlot(0)->getName()).str());
				prefs["RemoteIP0"] = entry;
				prefs.write();

				OnGameJoin(RET_OK, m_currentGame);
				//DEBUG_CRASH(("setting host to %ls@%ls", m_currentGame->getLANSlot(0)->getUser()->getLogin().str(),
				//	m_currentGame->getLANSlot(0)->getUser()->getHost().str()));
			}
			m_pendingAction = ACT_NONE;
			m_expiration = 0;
		}
	}
}

void LANAPI::handleJoinDeny( LANMessage *msg, UnsignedInt senderIP )
{
	if (msg->GameJoined.playerIP == m_localIP) // Is it for us?
	{
		if (m_pendingAction == ACT_JOIN) // Are we trying to join?
		{
			OnGameJoin(msg->GameNotJoined.reason, LookupGame(UnicodeString(msg->GameNotJoined.gameName)));
			m_pendingAction = ACT_NONE;
			m_expiration = 0;
		}
	}
}

void LANAPI::handleRequestGameLeave( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				if (player == 0)
				{
					OnHostLeave();
					removeGame(m_currentGame);
					delete m_currentGame;
					m_currentGame = nullptr;

					/// @todo re-add myself to lobby?  Or just keep me there all the time?  If we send a LOBBY_ANNOUNCE things'll work out...
					LANPlayer *lanPlayer = LookupPlayer(m_localIP);
					if (!lanPlayer)
					{
						lanPlayer = NEW LANPlayer;
						lanPlayer->setIP(m_localIP);
					}
					else
					{
						removePlayer(lanPlayer);
					}
					lanPlayer->setName(UnicodeString(m_name));
					lanPlayer->setHost(m_hostName);
					lanPlayer->setLogin(m_userName);
					lanPlayer->setLastHeard(timeGetTime());
					addPlayer(lanPlayer);

				}
				else
				{
					if (AmIHost())
					{
						// remove the deadbeat
						LANGameSlot slot;
						slot.setState(SLOT_OPEN);
						m_currentGame->setSlot( player, slot );
					}
					OnPlayerLeave(UnicodeString(msg->name));
					m_currentGame->getLANSlot(player)->setState(SLOT_OPEN);
					m_currentGame->resetAccepted();
					RequestGameOptions(GenerateGameOptionsString(), false, senderIP);
					//m_currentGame->endGame();
				}
				break;
			}
			DEBUG_ASSERTCRASH(player < MAX_SLOTS, ("Didn't find player!"));
		}
	}
	else if (m_inLobby)
	{
		// Look for dissappearing games
		LANGameInfo *game = m_games;
		while (game)
		{
			if (game->getName().compare(msg->GameToLeave.gameName) == 0)
			{
				removeGame(game);
				delete game;
				OnGameList(m_games);
				break;
			}
			game = game->getNext();
		}
	}
}

void LANAPI::handleRequestLobbyLeave( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANPlayer *player = m_lobbyPlayers;
		while (player)
		{
			if (player->getIP() == senderIP)
			{
				removePlayer(player);
				OnPlayerList(m_lobbyPlayers);
				break;
			}
			player = player->getNext();
		}
	}
}

void LANAPI::handleSetAccept( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnAccept(senderIP, msg->Accept.isAccepted);
				break;
			}
		}
	}
}

void LANAPI::handleHasMap( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame)
	{
		CRC mapNameCRC;
//	mapNameCRC.computeCRC(m_currentGame->getMap().str(), m_currentGame->getMap().getLength());
		AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(m_currentGame->getMap());
		mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
		if (mapNameCRC.get() != msg->MapStatus.mapCRC)
		{
			return;
		}

		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnHasMap(senderIP, msg->MapStatus.hasMap);
				break;
			}
		}
	}
}

void LANAPI::handleChat( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANPlayer *player;
		if((player=LookupPlayer(senderIP)) != nullptr)
		{
			OnChat(UnicodeString(player->getName()), player->getIP(), UnicodeString(msg->Chat.message), msg->Chat.chatType);
			player->setLastHeard(timeGetTime());
		}
	}
	else
	{
		if (LookupGame(UnicodeString(msg->Chat.gameName)) != m_currentGame)
		{
			DEBUG_LOG(("Game '%ls' is not my game", msg->Chat.gameName));
			if (m_currentGame)
			{
				DEBUG_LOG(("Current game is '%ls'", m_currentGame->getName().str()));
			}
			return;
		}

		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame && m_currentGame->getIP(player) == senderIP)
			{
				OnChat(UnicodeString(msg->name), m_currentGame->getIP(player), UnicodeString(msg->Chat.message), msg->Chat.chatType);
				break;
			}
		}
	}
}

void LANAPI::handleGameStart( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == senderIP && !m_currentGame->isGameInProgress())
	{
		OnGameStart();
	}
}

void LANAPI::handleGameStartTimer( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == senderIP && !m_currentGame->isGameInProgress())
	{
		OnGameStartTimer(msg->StartTimer.seconds);
	}
}

void LANAPI::handleGameOptions( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnGameOptions(senderIP, player, AsciiString(msg->GameOptions.options));
				break;
			}
		}
	}
}

void LANAPI::handleInActive(LANMessage *msg, UnsignedInt senderIP) {
	if (m_inLobby || (m_currentGame == nullptr) || (m_currentGame->isGameInProgress())) {
		return;
	}

	// check to see if we are the host of this game.
	if (m_currentGame->amIHost() == FALSE) {
		return;
	}

	UnicodeString playerName;
	playerName = msg->name;

	Int slotNum = m_currentGame->getSlotNum(playerName);
	if (slotNum < 0)
		return;
	GameSlot *slot = m_currentGame->getSlot(slotNum);
	if (slot == nullptr) {
		return;
	}

	if (senderIP != slot->getIP()) {
		return;
	}

	// don't want to unaccept the host, that's silly.  They can't hit start alt-tabbed anyways.
	if (senderIP == GetLocalIP()) {
		return;
	}

	// only unaccept if the timer hasn't started yet.
	if (m_gameStartTime != 0) {
		return;
	}

	slot->unAccept();
	AsciiString options = GenerateGameOptionsString();
	RequestGameOptions(options, FALSE);
	lanUpdateSlotList();
}

#if !RETAIL_COMPATIBLE_NETWORKING

// TheSuperHackers @feature arcticdolphin 02/03/2026 Handle incoming seed commit message from another player.
void LANAPI::handleSeedCommit(LANMessage *msg, UnsignedInt senderIP)
{
	if (!m_currentGame || m_currentGame->isGameInProgress())
		return;

	// Identify sender by explicit slot ID; validate IP matches the known slot address.
	const Int slot = static_cast<Int>(msg->SeedCommit.senderSlot);
	if (slot < 0 || slot >= MAX_SLOTS || m_currentGame->getIP(slot) != senderIP)
	{
		DEBUG_LOG(("LANAPI: ignoring seed commit: slot %d IP mismatch or out of range", slot));
		return;
	}

	// If the host sends a fresh commit while per-round state remains, reset for the new round.
	// A resent commit with identical bytes is treated as a duplicate.
	if (!m_currentGame->amIHost() && slot == 0 && (m_seedPhase != SEED_PHASE_NONE || m_seedReady))
	{
		if (m_slotCommitReceived[slot] && memcmp(m_slotSeedCommit[slot], msg->SeedCommit.commit, 32) == 0)
		{
			DEBUG_LOG(("LANAPI: ignoring resent host commit (matches current round)"));
			return;
		}
		DEBUG_LOG(("LANAPI: (client) new host commit (phase=%d seedReady=%d) - resetting stale seed state", m_seedPhase, m_seedReady));
		resetSeedProtocolState();
	}

	// Non-host commits carry the round nonce derived from the host commit, so the host commit
	// must be known before a non-host commit can be validated. Drop early arrivals; the sender
	// will resend within s_seedResendIntervalMs and the commit will be accepted then.
	if (slot != 0 && !m_slotCommitReceived[0])
	{
		DEBUG_LOG(("LANAPI: dropping seed commit from slot %d: host commit not yet received", slot));
		return;
	}

	// Validate round nonce for non-host commits to filter stale packets from prior rounds.
	if (slot != 0 &&
		memcmp(msg->SeedCommit.roundNonce, m_slotSeedCommit[0], sizeof(msg->SeedCommit.roundNonce)) != 0)
	{
		DEBUG_LOG(("LANAPI: ignoring seed commit from slot %d: round nonce mismatch", slot));
		return;
	}

	// Ignore duplicate commits and commits arriving after the commit phase.
	if (m_slotCommitReceived[slot])
	{
		DEBUG_LOG(("LANAPI: ignoring duplicate commit from slot %d", slot));
		return;
	}
	if (m_seedPhase != SEED_PHASE_NONE && m_seedPhase != SEED_PHASE_AWAITING_COMMITS)
	{
		DEBUG_LOG(("LANAPI: ignoring commit from slot %d in phase %d", slot, m_seedPhase));
		return;
	}

	memcpy(m_slotSeedCommit[slot], msg->SeedCommit.commit, 32);
	m_slotCommitReceived[slot] = TRUE;
	UnsignedInt dbgCommit;
	memcpy(&dbgCommit, msg->SeedCommit.commit, sizeof(dbgCommit));
	DEBUG_LOG(("LANAPI: stored seed commit from slot %d: commit[0..3]=0x%08X", slot, dbgCommit));

	// Host commit triggers client response; reply exactly once.
	if (!m_currentGame->amIHost() && slot == 0 && m_seedPhase == SEED_PHASE_NONE)
	{
		const Int localSlot = m_currentGame->getLocalSlotNum();
		if (localSlot < 0 || localSlot >= MAX_SLOTS)
		{
			abortSeedProtocol(L"Could not start the game: invalid local slot. Please try again.");
			return;
		}
		if (!generateLocalSecret(m_localSeedSecret))
		{
			abortSeedProtocol(L"Could not start the game: failed to generate a secure random secret. Please try again.");
			return;
		}
		if (!computeSeedCommitment(m_localSeedSecret, static_cast<BYTE>(localSlot), m_localSeedCommit))
		{
			abortSeedProtocol(L"Could not start the game: failed to compute seed commitment. Please try again.");
			return;
		}
		m_seedPhase = SEED_PHASE_AWAITING_COMMITS;
		m_seedPhaseDeadline = timeGetTime() + s_seedPhaseTimeoutMs;

		// Pre-fill own slot
		{
			memcpy(m_slotSeedCommit[localSlot], m_localSeedCommit, 32);
			m_slotCommitReceived[localSlot] = TRUE;
		}

		LANMessage reply = {};
		fillInLANMessage(&reply);
		reply.messageType = LANMessage::MSG_SEED_COMMIT;
		memcpy(reply.SeedCommit.commit, m_localSeedCommit, sizeof(reply.SeedCommit.commit));
		reply.SeedCommit.senderSlot = static_cast<BYTE>(localSlot);
		memcpy(reply.SeedCommit.roundNonce, m_slotSeedCommit[0], sizeof(reply.SeedCommit.roundNonce));
		sendMessage(&reply);
		m_seedResendTime = timeGetTime() + s_seedResendIntervalMs;
		UnsignedInt dbgCommit;
		memcpy(&dbgCommit, m_localSeedCommit, sizeof(dbgCommit));
		DEBUG_LOG(("LANAPI: sent seed commit commit[0..3]=0x%08X", dbgCommit));
	}

	// Flush any reveal that arrived before this commit (or before the host commit established the nonce).
	flushPendingReveal(slot);
	// If this was the host commit, all other slots' pending reveals can now be flushed too
	// because the round nonce (first 4 bytes of host commit) is now known.
	if (slot == 0)
	{
		for (Int i = 1; i < MAX_SLOTS; ++i)
			flushPendingReveal(i);
	}

	// Host advances to reveals once all commits received.
	if (m_currentGame->amIHost() && m_seedPhase == SEED_PHASE_AWAITING_COMMITS && allSeedCommitsReceived())
	{
		beginSeedRevealPhase();
	}
}

// TheSuperHackers @feature arcticdolphin 02/03/2026 Handle incoming seed reveal message and verify commitment.
void LANAPI::handleSeedReveal(LANMessage *msg, UnsignedInt senderIP)
{
	if (!m_currentGame || m_currentGame->isGameInProgress())
		return;

	// Identify sender by explicit slot ID; validate IP matches the known slot address.
	const Int slot = static_cast<Int>(msg->SeedReveal.senderSlot);
	if (slot < 0 || slot >= MAX_SLOTS || m_currentGame->getIP(slot) != senderIP)
	{
		DEBUG_LOG(("LANAPI: ignoring seed reveal: slot %d IP mismatch or out of range", slot));
		return;
	}

	// If the host commit has already arrived the round nonce is known; drop stale reveals early
	// so we never buffer garbage from a previous round. Skip this guard when the host commit is
	// not yet in hand — we cannot know the nonce yet and will validate at flush time instead.
	if (m_slotCommitReceived[0] &&
		memcmp(msg->SeedReveal.roundNonce, m_slotSeedCommit[0], sizeof(msg->SeedReveal.roundNonce)) != 0)
	{
		DEBUG_LOG(("LANAPI: ignoring seed reveal from slot %d: round nonce mismatch", slot));
		return;
	}

	// Ignore duplicate reveals.
	if (m_slotRevealReceived[slot])
	{
		DEBUG_LOG(("LANAPI: ignoring duplicate reveal from slot %d", slot));
		return;
	}

	// Buffer the reveal if either the sender's commit or the host commit has not arrived yet.
	// Both are required before processVerifiedReveal can verify the nonce and commitment.
	// Nonce and commitment checks are deferred to processVerifiedReveal via flushPendingReveal.
	if (!m_slotCommitReceived[slot] || !m_slotCommitReceived[0])
	{
		if (!m_slotPendingRevealValid[slot])
		{
			memcpy(m_slotPendingRevealSecret[slot], msg->SeedReveal.secret, 16);
			memcpy(m_slotPendingRevealNonce[slot],  msg->SeedReveal.roundNonce, sizeof(msg->SeedReveal.roundNonce));
			m_slotPendingRevealValid[slot] = TRUE;
			DEBUG_LOG(("LANAPI: buffered early reveal from slot %d (commit[slot]=%d commit[0]=%d)",
				slot, m_slotCommitReceived[slot], m_slotCommitReceived[0]));
		}
		return;
	}

	processVerifiedReveal(slot, msg->SeedReveal.secret, msg->SeedReveal.roundNonce);
}

// TheSuperHackers @feature arcticdolphin 02/03/2026 Verify nonce+commitment for a reveal, store it, and advance the protocol.
void LANAPI::processVerifiedReveal(Int slot, const BYTE secret[16], const BYTE roundNonce[4])
{
	// Validate round nonce to filter stale reveals from prior rounds.
	if (memcmp(roundNonce, m_slotSeedCommit[0], 4) != 0)
	{
		DEBUG_LOG(("LANAPI: ignoring seed reveal from slot %d: round nonce mismatch", slot));
		return;
	}

	// Verify commitment before accepting the reveal.
	BYTE expectedCommit[32];
	if (!computeSeedCommitment(secret, static_cast<BYTE>(slot), expectedCommit))
	{
		DEBUG_LOG(("LANAPI: computeSeedCommitment failed during reveal verification for slot %d", slot));
		abortSeedProtocol(L"Could not start the game: failed to verify seed commitment. Please try again.");
		return;
	}
	if (memcmp(expectedCommit, m_slotSeedCommit[slot], 32) != 0)
	{
		DEBUG_LOG(("LANAPI: seed reveal FAILED for slot %d - commitment mismatch, aborting", slot));
		UnicodeString abortMsg;
		abortMsg.format(L"Could not start the game: %ls's random seed did not match the agreed value. Please try again.",
			m_currentGame->getSlot(slot)->getName().str());
		abortSeedProtocol(abortMsg.str());
		return;
	}

	// Non-host: host's reveal signals commit phase done -- enter reveal phase and send our own reveal.
	const Bool isHostReveal = !m_currentGame->amIHost() && slot == 0;
	if (isHostReveal && (m_seedPhase == SEED_PHASE_AWAITING_COMMITS || m_seedPhase == SEED_PHASE_NONE))
	{
		const Int localSlot = m_currentGame->getLocalSlotNum();
		if (localSlot < 0 || localSlot >= MAX_SLOTS)
		{
			abortSeedProtocol(L"Could not start the game: invalid local slot. Please try again.");
			return;
		}
		{
			memcpy(m_slotSeedReveal[localSlot], m_localSeedSecret, 16);
			m_slotRevealReceived[localSlot] = TRUE;
		}

		m_seedPhase = SEED_PHASE_AWAITING_REVEALS;
		m_seedPhaseDeadline = timeGetTime() + s_seedPhaseTimeoutMs;

		LANMessage reply = {};
		fillInLANMessage(&reply);
		reply.messageType = LANMessage::MSG_SEED_REVEAL;
		memcpy(reply.SeedReveal.secret, m_localSeedSecret, 16);
		reply.SeedReveal.senderSlot = static_cast<BYTE>(localSlot);
		memcpy(reply.SeedReveal.roundNonce, m_slotSeedCommit[0], sizeof(reply.SeedReveal.roundNonce));
		sendMessage(&reply);
		m_seedResendTime = timeGetTime() + s_seedResendIntervalMs;
		UnsignedInt dbgCommit;
		memcpy(&dbgCommit, m_localSeedCommit, sizeof(dbgCommit));
		DEBUG_LOG(("LANAPI: (client) entered reveal phase, sent reveal for commit[0..3]=0x%08X", dbgCommit));
	}
	// Store the verified reveal.
	memcpy(m_slotSeedReveal[slot], secret, 16);
	m_slotRevealReceived[slot] = TRUE;
	UnsignedInt dbgCommit;
	memcpy(&dbgCommit, m_slotSeedCommit[slot], sizeof(dbgCommit));
	DEBUG_LOG(("LANAPI: verified seed reveal slot %d: commit[0..3]=0x%08X", slot, dbgCommit));

	if (allSeedRevealsReceived())
		finalizeSeed();
}

// TheSuperHackers @feature arcticdolphin 02/03/2026 Drain a buffered early reveal for a slot once both its commit and the host commit are available.
void LANAPI::flushPendingReveal(Int slot)
{
	if (!m_slotPendingRevealValid[slot])
		return;
	if (!m_slotCommitReceived[slot] || !m_slotCommitReceived[0])
		return;

	DEBUG_LOG(("LANAPI: flushing buffered early reveal for slot %d", slot));
	m_slotPendingRevealValid[slot] = FALSE;
	processVerifiedReveal(slot, m_slotPendingRevealSecret[slot], m_slotPendingRevealNonce[slot]);
}

// TheSuperHackers @feature arcticdolphin 03/03/2026 Host receives seed-ready acknowledgments from clients.
void LANAPI::handleSeedReady(LANMessage *msg, UnsignedInt senderIP)
{
	if (!m_currentGame || m_currentGame->isGameInProgress())
		return;

	// Only the host processes seed-ready acknowledgments.
	if (!m_currentGame->amIHost())
		return;

	// Identify sender by explicit slot ID; validate IP matches the known slot address.
	const Int slot = static_cast<Int>(msg->SeedReady.senderSlot);
	if (slot < 0 || slot >= MAX_SLOTS || m_currentGame->getIP(slot) != senderIP)
	{
		DEBUG_LOG(("LANAPI: ignoring seed-ready: slot %d IP mismatch or out of range", slot));
		return;
	}

	// Validate the round nonce: must match the first 4 bytes of our own commit.
	// Drops stale acks from a previous round that arrived late.
	if (memcmp(msg->SeedReady.roundNonce, m_localSeedCommit, sizeof(msg->SeedReady.roundNonce)) != 0)
	{
		DEBUG_LOG(("LANAPI: ignoring seed-ready from slot %d: round nonce mismatch", slot));
		return;
	}

	// Safe to accept early (before host finalizeSeed): the round nonce ensures this ack is
	// for the current round, and RequestGameStart() independently checks m_seedReady before
	// starting the game.
	m_slotSeedReady[slot] = TRUE;
	DEBUG_LOG(("LANAPI: slot %d reported seed ready", slot));
}

#endif // !RETAIL_COMPATIBLE_NETWORKING
