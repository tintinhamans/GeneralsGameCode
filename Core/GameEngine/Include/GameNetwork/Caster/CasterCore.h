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

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Caster/CasterChat.h"
#include "GameNetwork/Caster/CasterLobby.h"
#include "GameNetwork/Caster/CasterTransportCore.h"

enum CasterRole
{
	CASTER_ROLE_NONE     = 0,
	CASTER_ROLE_PLAYER   = 1,
	CASTER_ROLE_CASTER = 2,
};

static const UnsignedInt CASTER_INBOUND_CAPACITY = 64;

/// Bound on one displayed chat line (bytes, UTF-8). Longer lines are truncated.
static const UnsignedInt CASTER_CHAT_TEXT_BYTES = 512;

static const UnsignedInt CASTER_GAME_NAME_BYTES = 64;

struct CasterInbound
{
	UnsignedInt gameUid;
	UnsignedInt commandID;
	UnsignedByte direction;
	UnsignedByte senderSlot;
	UnsignedByte isSenderCaster;
	UnsignedByte senderTeam;
	UnsignedInt senderIdentity;
	UnsignedInt senderNameLen;
	Char senderName[65];
	UnsignedInt senderLinkId;
	UnsignedInt textLen;
	Char text[CASTER_CHAT_TEXT_BYTES + 1];
	UnsignedByte isEmote;		///< TRUE for a stock "/me " emote line
};

class CasterBroadcaster
{
public:
	virtual ~CasterBroadcaster() {}

	virtual void broadcastChat(const UnsignedByte* frame, UnsignedInt length) = 0;
};

class CasterCore : public CasterTransportCore::HostServices
{
public:
	CasterCore();
	virtual ~CasterCore();

	// --- role (main thread) ------------------------------------------------

	void setRole(CasterRole role);
	CasterRole role() const;
	Bool isPlayer() const;
	Bool isCaster() const;
	void resetRole();

	// --- wiring (main thread) ----------------------------------------------

	void setBroadcaster(CasterBroadcaster* broadcaster);
	CasterBroadcaster* broadcaster() const;

	// --- player announcement (main thread) ---------------------------------

	/// `matchRunning` is the typed streaming gate: FALSE for a lobby-only
	/// announcement, TRUE once the match itself is under way.
	Bool announceGame(UnsignedInt gameUid, const char* gameName, Bool matchRunning);
	Bool isGameAnnounced() const;
	void clearAnnouncedGame();

	// --- watched game (caster, main thread) ------------------------------

	Bool beginWatching(UnsignedInt gameUid, const char* gameName);
	void stopWatching();
	Bool isWatching() const;
	UnsignedInt watchedGameUid() const;

	// --- watched lobby state (caster, main thread) -----------------------

	void receiveLobbyState(UnsignedInt gameUid, const char* text, UnsignedInt textLen,
		UnsignedInt nowMs);

	CasterLobby::LobbyStatus lobbyStatus(UnsignedInt nowMs, UnsignedInt staleMs) const;

	const CasterLobby::LobbyState& lobbyState() const;
	Bool hasLobbyState() const;

	void clearLobbyState();

	// --- inbound queue (any thread under the shell's lock) ------------------

	/// Appends one event. FALSE and a drop count when the queue is full.
	Bool pushInbound(const CasterInbound& event);

	/// Removes the oldest event into `out`. FALSE when empty (main thread).
	Bool popInbound(CasterInbound& out);

	void clearInbound();

	static void makeChatEvent(CasterInbound& out, UnsignedInt gameUid, UnsignedInt commandID,
		UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte isSenderCaster,
		UnsignedByte senderTeam, UnsignedInt senderIdentity, const char* senderName,
		UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len, UnsignedByte isEmote);

	UnsignedInt encodeCasterChat(UnsignedByte* out, UnsignedInt cap, UnsignedInt commandID,
		UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte senderTeam,
		UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen, const char* utf8Text,
		UnsignedInt utf8Len, Bool isEmote) const;

	void setPrivacyFilter(Bool enabled);
	Bool privacyFilter() const;

	Bool capturePlayerChat(UnsignedInt playerID, UnsignedInt commandID, UnsignedInt playerMask,
		UnsignedInt activePlayerMask, UnsignedByte senderTeam, const char* senderName,
		UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len);

	Bool receiveChat(UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction,
		UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam,
		UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen,
		const char* utf8Text, UnsignedInt utf8Len, UnsignedInt senderLinkId, UnsignedByte isEmote);

	virtual UnsignedInt currentGameUid() const;
	virtual Bool matchRunning(UnsignedInt gameUid) const;
	virtual const char* gameName(UnsignedInt gameUid) const;

private:
	void copyGameName(const char* source);

	CasterRole m_role;
	CasterBroadcaster* m_broadcaster;

	Bool m_gameAnnounced;
	UnsignedInt m_gameUid;
	Bool m_matchRunning;
	Char m_gameName[CASTER_GAME_NAME_BYTES + 1];

	Bool m_watching;
	UnsignedInt m_watchedGameUid;
	Char m_watchedGameName[CASTER_GAME_NAME_BYTES + 1];

	CasterLobby::LobbyState m_lobbyState;
	UnsignedInt m_lobbyStateGameUid;
	UnsignedInt m_lobbyStateMs;
	Bool m_lobbyStateParsed;

	Bool m_privacyFilter;

	CasterChat::Dedup m_chatDedup;
	CasterChat::Dedup m_recvDedup;

	CasterInbound m_inbound[CASTER_INBOUND_CAPACITY];
	UnsignedInt m_inboundHead;
	UnsignedInt m_inboundCount;
	UnsignedInt m_inboundDropped;

	CasterCore(const CasterCore&);
	CasterCore& operator=(const CasterCore&);
};
