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

#include "PreRTS.h"

#include <string.h>

#include "GameNetwork/Caster/CasterCore.h"

CasterCore::CasterCore()
	: m_role(CASTER_ROLE_NONE),
		m_broadcaster(nullptr),
		m_gameAnnounced(FALSE),
		m_gameUid(0),
		m_matchRunning(FALSE),
		m_watching(FALSE),
		m_watchedGameUid(0),
		m_lobbyStateGameUid(0),
		m_lobbyStateMs(0),
		m_lobbyStateParsed(FALSE),
		m_privacyFilter(TRUE),
		m_inboundHead(0),
		m_inboundCount(0),
		m_inboundDropped(0)
{
	m_gameName[0] = '\0';
	m_watchedGameName[0] = '\0';
	m_lobbyState.valid = FALSE;
}

CasterCore::~CasterCore()
{
}

void CasterCore::setRole(CasterRole role)
{
	m_role = role;
}

CasterRole CasterCore::role() const
{
	return m_role;
}

Bool CasterCore::isPlayer() const
{
	return (m_role == CASTER_ROLE_PLAYER) ? TRUE : FALSE;
}

Bool CasterCore::isCaster() const
{
	return (m_role == CASTER_ROLE_CASTER) ? TRUE : FALSE;
}

void CasterCore::resetRole()
{
	m_role = CASTER_ROLE_NONE;
}

void CasterCore::setBroadcaster(CasterBroadcaster* broadcaster)
{
	m_broadcaster = broadcaster;
}

CasterBroadcaster* CasterCore::broadcaster() const
{
	return m_broadcaster;
}

void CasterCore::copyGameName(const char* source)
{
	UnsignedInt i = 0;

	if (source == nullptr)
	{
		m_gameName[0] = '\0';
		return;
	}

	while (source[i] != '\0' && i < CASTER_GAME_NAME_BYTES)
	{
		m_gameName[i] = source[i];
		++i;
	}
	m_gameName[i] = '\0';
}

Bool CasterCore::announceGame(UnsignedInt gameUid, const char* gameName, Bool matchRunning)
{
	if (gameUid == 0)
	{

		return FALSE;
	}

	m_gameAnnounced = TRUE;
	m_gameUid = gameUid;
	m_matchRunning = matchRunning;
	copyGameName(gameName);
	return TRUE;
}

Bool CasterCore::isGameAnnounced() const
{
	return m_gameAnnounced;
}

void CasterCore::clearAnnouncedGame()
{
	m_gameAnnounced = FALSE;
	m_gameUid = 0;
	m_matchRunning = FALSE;
	m_gameName[0] = '\0';
}

Bool CasterCore::beginWatching(UnsignedInt gameUid, const char* gameName)
{
	if (gameUid == 0)
	{
		return FALSE;
	}

	m_watching = TRUE;
	m_watchedGameUid = gameUid;

	if (gameName != nullptr)
	{
		UnsignedInt i = 0;
		while (gameName[i] != '\0' && i < CASTER_GAME_NAME_BYTES)
		{
			m_watchedGameName[i] = gameName[i];
			++i;
		}
		m_watchedGameName[i] = '\0';
	}
	else
	{
		m_watchedGameName[0] = '\0';
	}

	clearLobbyState();
	return TRUE;
}

void CasterCore::stopWatching()
{
	m_watching = FALSE;
	m_watchedGameUid = 0;
	m_watchedGameName[0] = '\0';
	clearLobbyState();
}

void CasterCore::clearLobbyState()
{
	m_lobbyStateGameUid = 0;
	m_lobbyStateMs = 0;
	m_lobbyStateParsed = FALSE;
	m_lobbyState.valid = FALSE;
}

void CasterCore::receiveLobbyState(UnsignedInt gameUid, const char* text, UnsignedInt textLen,
	UnsignedInt nowMs)
{
	CasterLobby::LobbyState parsed;

	if (!m_watching || gameUid == 0 || gameUid != m_watchedGameUid)
	{
		return;
	}

	m_lobbyStateGameUid = gameUid;
	m_lobbyStateMs = nowMs;
	m_lobbyStateParsed = CasterLobby::parseLobbyState(text, textLen, parsed);
	if (m_lobbyStateParsed)
	{
		m_lobbyState = parsed;
	}
	else
	{
		m_lobbyState.valid = FALSE;
	}
}

CasterLobby::LobbyStatus CasterCore::lobbyStatus(UnsignedInt nowMs, UnsignedInt staleMs) const
{
	return CasterLobby::classifyLobbyStatus(m_watching, m_watchedGameUid, m_lobbyStateParsed,
		m_lobbyStateGameUid, nowMs, m_lobbyStateMs, staleMs);
}

const CasterLobby::LobbyState& CasterCore::lobbyState() const
{
	return m_lobbyState;
}

Bool CasterCore::hasLobbyState() const
{
	return (m_lobbyStateParsed && m_lobbyState.valid) ? TRUE : FALSE;
}

Bool CasterCore::isWatching() const
{
	return m_watching;
}

UnsignedInt CasterCore::watchedGameUid() const
{
	return m_watchedGameUid;
}


//-----------------------------------------------------------------------------
// Inbound queue
//-----------------------------------------------------------------------------

Bool CasterCore::pushInbound(const CasterInbound& event)
{
	UnsignedInt index;

	if (m_inboundCount >= CASTER_INBOUND_CAPACITY)
	{
		++m_inboundDropped;
		return FALSE;
	}

	index = (m_inboundHead + m_inboundCount) % CASTER_INBOUND_CAPACITY;
	m_inbound[index] = event;
	++m_inboundCount;
	return TRUE;
}

Bool CasterCore::popInbound(CasterInbound& out)
{
	if (m_inboundCount == 0)
	{
		return FALSE;
	}

	out = m_inbound[m_inboundHead];
	m_inboundHead = (m_inboundHead + 1) % CASTER_INBOUND_CAPACITY;
	--m_inboundCount;
	return TRUE;
}

void CasterCore::clearInbound()
{
	m_inboundHead = 0;
	m_inboundCount = 0;
	m_inboundDropped = 0;
}

static void copyChatText(CasterInbound& out, const char* utf8Text, UnsignedInt utf8Len)
{
	UnsignedInt i;

	if (utf8Text == nullptr)
	{
		out.textLen = 0;
		out.text[0] = '\0';
		return;
	}

	if (utf8Len > CASTER_CHAT_TEXT_BYTES)
	{
		utf8Len = CASTER_CHAT_TEXT_BYTES;
	}

	for (i = 0; i < utf8Len; ++i)
	{
		out.text[i] = utf8Text[i];
	}
	out.text[utf8Len] = '\0';
	out.textLen = utf8Len;
}

void CasterCore::makeChatEvent(CasterInbound& out, UnsignedInt gameUid, UnsignedInt commandID,
	UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte isSenderCaster,
	UnsignedByte senderTeam, UnsignedInt senderIdentity, const char* senderName,
	UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len, UnsignedByte isEmote)
{
	out.gameUid = gameUid;
	out.commandID = commandID;
	out.direction = direction;
	out.senderSlot = senderSlot;
	out.isSenderCaster = isSenderCaster;
	out.senderTeam = senderTeam;
	out.isEmote = isEmote;
	out.senderIdentity = senderIdentity;
	if (senderNameLen > sizeof(out.senderName) - 1)
	{
		senderNameLen = sizeof(out.senderName) - 1;
	}
	if (senderName != nullptr && senderNameLen > 0)
	{
		memcpy(out.senderName, senderName, senderNameLen);
	}
	out.senderName[senderNameLen] = '\0';
	out.senderNameLen = senderNameLen;
	out.senderLinkId = 0;
	copyChatText(out, utf8Text, utf8Len);
}

UnsignedInt CasterCore::encodeCasterChat(UnsignedByte* out, UnsignedInt cap, UnsignedInt commandID,
	UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte senderTeam,
	UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen, const char* utf8Text,
	UnsignedInt utf8Len, Bool isEmote) const
{
	if (!m_watching || m_watchedGameUid == 0)
	{
		return 0;
	}

	return CasterProtocol::encodeChat(out, cap, m_watchedGameUid, commandID, direction,
		senderSlot, (UnsignedByte)1, senderTeam, senderIdentity, senderName, senderNameLen, utf8Text, utf8Len,
		isEmote);
}

void CasterCore::setPrivacyFilter(Bool enabled)
{
	m_privacyFilter = enabled;
}

Bool CasterCore::privacyFilter() const
{
	return m_privacyFilter;
}

Bool CasterCore::capturePlayerChat(UnsignedInt playerID, UnsignedInt commandID,
	UnsignedInt playerMask, UnsignedInt activePlayerMask, UnsignedByte senderTeam,
	const char* senderName, UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len)
{
	UnsignedByte direction;
	UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt frameLen;

	if (!isPlayer() || m_broadcaster == nullptr || !m_gameAnnounced || m_gameUid == 0)
	{
		return FALSE;
	}

	if (!m_chatDedup.accept(playerID, (UnsignedByte)0, commandID))
	{
		return FALSE;
	}

	direction = CasterChat::classifyDirection(playerMask, activePlayerMask);
	// No stock in-game chat surface has an emote convention (only the LAN lobby
	// "/me " does), so a captured in-game player chat line is never an emote.
	frameLen = CasterProtocol::encodeChat(frame, sizeof(frame), m_gameUid, commandID,
		direction, (UnsignedByte)playerID, (UnsignedByte)0, senderTeam, playerID, senderName,
		senderNameLen, utf8Text, utf8Len, FALSE);
	if (frameLen == 0)
	{
		return FALSE;
	}
	m_broadcaster->broadcastChat(frame, frameLen);
	return TRUE;
}

Bool CasterCore::receiveChat(UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction,
	UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam,
	UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen,
	const char* utf8Text, UnsignedInt utf8Len, UnsignedInt senderLinkId, UnsignedByte isEmote)
{
	CasterInbound event;

	if (!m_recvDedup.accept(senderIdentity, isSenderCaster, commandID))
	{
		return FALSE;
	}

	makeChatEvent(event, gameUid, commandID, direction, senderSlot, isSenderCaster, senderTeam,
		senderIdentity, senderName, senderNameLen, utf8Text, utf8Len, isEmote);
	event.senderLinkId = senderLinkId;
	return pushInbound(event);
}

UnsignedInt CasterCore::currentGameUid() const
{
	return m_gameAnnounced ? m_gameUid : 0;
}

Bool CasterCore::matchRunning(UnsignedInt gameUid) const
{
	return (m_gameAnnounced && gameUid == m_gameUid) ? m_matchRunning : FALSE;
}

const char* CasterCore::gameName(UnsignedInt gameUid) const
{
	return (m_gameAnnounced && gameUid == m_gameUid) ? m_gameName : "";
}
