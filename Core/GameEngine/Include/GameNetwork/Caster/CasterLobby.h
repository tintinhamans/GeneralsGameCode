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

namespace CasterLobby
{

enum GestureResult
{
	GESTURE_NONE                = 0,
	GESTURE_JOIN                = 1,
	GESTURE_CAST             = 2,
	GESTURE_CAST_UNAVAILABLE = 3,
};

GestureResult classifyGesture(Bool gameSelected, Bool shiftHeld, Bool featureEnabled,
	Bool readOnlyLobby);

static const UnsignedInt MAX_LOBBY_LINES = 8;

static const UnsignedInt LOBBY_LINE_BYTES = 256;

static const UnsignedInt LOBBY_MAX_SLOTS = 8;

static const UnsignedInt LOBBY_NAME_BYTES = 32;

static const UnsignedInt LOBBY_MAP_BYTES = 128;

enum LobbySlotKind
{
	LOBBY_SLOT_EMPTY   = 0,
	LOBBY_SLOT_HUMAN   = 1,
	LOBBY_SLOT_AI      = 2,
	LOBBY_SLOT_OPEN    = 3,
	LOBBY_SLOT_CLOSED  = 4,
};

struct LobbySlot
{
	UnsignedByte kind;
	Bool accepted;
	Bool hasMap;
	UnsignedInt ip;
	UnsignedInt port;
	Int color;
	Int playerTemplate;
	Int startPos;
	Int team;
	Int nat;
	Char aiDifficulty;
	Char name[LOBBY_NAME_BYTES + 1];
};

/// The parsed local mirror of a LAN room. Deliberately kept separate from the
/// wire-carried RoomSnapshot below: the shared middle fields are identical, but
/// LobbyState adds parse/countdown bookkeeping (`valid`, `countdownRevision`)
/// that must never reach the wire. Keep the shared fields in step by hand.
struct LobbyState
{
	Bool valid;
	Int countdownSeconds;
	UnsignedInt countdownRevision;
	Bool zeroHour;
	Int useStats;
	Int mapContentsMask;
	UnsignedInt mapCRC;
	Int mapSize;
	Int seed;
	Int crcInterval;
	UnsignedInt superweaponRestriction;
	UnsignedInt startingCash;
	Bool oldFactionsOnly;
	Char mapName[LOBBY_MAP_BYTES + 1];
	UnsignedInt slotCount;
	LobbySlot slots[LOBBY_MAX_SLOTS];
};

/// The wire form of a room, carried inside the BOOTSTRAP frame. Mirrors the
/// shared middle of LobbyState above (see the note there) and replaces its
/// parse bookkeeping with the identity the receiver needs. The encoder writes
/// these fields in declaration order, so the layout is part of the protocol.
struct RoomSnapshot
{
	UnsignedInt gameUid;
	UnsignedInt revision;
	Int countdownSeconds;
	Bool zeroHour;
	Int useStats;
	Int mapContentsMask;
	UnsignedInt mapCRC;
	Int mapSize;
	Int seed;
	Int crcInterval;
	UnsignedInt superweaponRestriction;
	UnsignedInt startingCash;
	Bool oldFactionsOnly;
	Char mapName[LOBBY_MAP_BYTES + 1];
	UnsignedInt slotCount;
	LobbySlot slots[LOBBY_MAX_SLOTS];
};

Bool parseLobbyState(const char* text, UnsignedInt len, LobbyState& out);
Bool takeCountdown(const LobbyState& state, UnsignedInt& lastRevision);

enum LobbyStatus
{
	LOBBY_STATUS_OK                = 0,
	LOBBY_STATUS_ABSENT            = 1,
	LOBBY_STATUS_STALE             = 2,
	LOBBY_STATUS_UNPARSED          = 3,
	LOBBY_STATUS_NO_WATCH          = 4,
	LOBBY_STATUS_NO_SOURCE         = 5,
	LOBBY_STATUS_NO_SUBSCRIPTION   = 6,
	LOBBY_STATUS_SUBSCRIBE_PENDING = 7,
	LOBBY_STATUS_WAITING           = 8,
	LOBBY_STATUS_LOCAL_IP_UNKNOWN  = 9,
	LOBBY_STATUS_NOT_PLAYER        = 10,
	LOBBY_STATUS_NO_GAME_ANNOUNCED = 11,
	LOBBY_STATUS_NO_UID            = 12,
};

enum LobbySubscription
{
	LOBBY_SUB_NONE    = 0,
	LOBBY_SUB_UNSENT  = 1,
	LOBBY_SUB_PENDING = 2,
	LOBBY_SUB_ACTIVE  = 3,
};

/**
 * Classifies the mirrored lobby state. A snapshot older than `staleMs` is
 * reported stale (not silently rendered), and a frame whose blob did not parse
 * is reported unparsed. Wrap-safe on the millisecond clock.
 */
LobbyStatus classifyLobbyStatus(Bool watching, UnsignedInt watchedGameUid,
	Bool stateParsed, UnsignedInt stateGameUid, UnsignedInt nowMs, UnsignedInt stateMs,
	UnsignedInt staleMs);

LobbyStatus classifyLobbyFeed(Bool watching, LobbySubscription subscription, LobbyStatus snapshot);

enum ChatSurface
{
	CHAT_SURFACE_NONE             = 0,
	CHAT_SURFACE_LAN_LOBBY        = 1,
	CHAT_SURFACE_LAN_GAME_OPTIONS = 2,
	CHAT_SURFACE_READ_ONLY        = 3,
	CHAT_SURFACE_SCORE_SCREEN     = 4,
};

ChatSurface selectActiveChatSurface(Bool readOnlyBoxPresent, Bool lobbyBoxPresent,
	Bool gameBoxPresent, Bool scoreBoxPresent = FALSE);

Bool shouldForwardLobby(Bool subscribed, Bool haveLast, Bool changed, UnsignedInt nowMs,
	UnsignedInt lastMs, UnsignedInt intervalMs);

struct LobbyViewRow
{
	Bool visible;
	UnsignedByte kind;
	Bool accepted;
	Bool hasMap;
	Int color;
	Int playerTemplate;
	Int startPos;
	Int team;
	Int nat;
	Char aiDifficulty;
	Char name[LOBBY_NAME_BYTES + 1];
};

struct LobbyViewModel
{
	LobbyStatus status;
	Bool hasSnapshot;
	Bool optionsVisible;
	Bool mutatingEnabled;
	Int mapContentsMask;
	UnsignedInt superweaponRestriction;
	UnsignedInt startingCash;
	Char mapName[LOBBY_MAP_BYTES + 1];
	UnsignedInt rowCount;
	LobbyViewRow rows[LOBBY_MAX_SLOTS];
};

void buildLobbyView(const LobbyState& state, LobbyStatus status, LobbyViewModel& out);

/// Field-by-field equality (struct padding makes memcmp unsafe here).
Bool lobbyViewEquals(const LobbyViewModel& a, const LobbyViewModel& b);

enum ReadOnlyOpenStage
{
	READONLY_OPEN_OK             = 0,
	READONLY_OPEN_LAYOUT_FAILED  = 1,
	READONLY_OPEN_PARENT_MISSING = 2,
	READONLY_OPEN_GADGET_MISSING = 3,
};

UnsignedInt formatReadOnlyOpenFailure(ReadOnlyOpenStage stage, const char* firstMissingGadget,
	char* out, UnsignedInt cap);

class OpenFailureLatch
{
public:
	OpenFailureLatch();

	void clear();

	Bool shouldSkip(UnsignedInt gameUid) const;

	Bool latch(UnsignedInt gameUid);

private:
	UnsignedInt m_gameUid;
};

class LineQueue
{
public:
	LineQueue();

	void clear();

	Bool push(const char* utf8Text, UnsignedInt utf8Len, const char* senderName,
		UnsignedInt senderNameLen, UnsignedByte senderSlot, Bool senderIsCaster, Bool isEmote);

	struct Line
	{
		char text[LOBBY_LINE_BYTES + 1];
		UnsignedInt textLen;
		char senderName[65];
		UnsignedInt senderNameLen;
		UnsignedByte senderSlot;
		Bool senderIsCaster;
		Bool isEmote;
	};

	Bool pop(Line& out);

	UnsignedInt count() const;
	UnsignedInt dropped() const;

private:
	Line m_lines[MAX_LOBBY_LINES];
	UnsignedInt m_head;
	UnsignedInt m_count;
	UnsignedInt m_dropped;

	LineQueue(const LineQueue&);
	LineQueue& operator=(const LineQueue&);
};

}
