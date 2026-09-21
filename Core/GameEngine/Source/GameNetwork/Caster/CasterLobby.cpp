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

#include "GameNetwork/Caster/CasterLobby.h"

#include <stdio.h>
#include <string.h>

namespace CasterLobby
{

static const UnsignedInt LOBBY_PARSE_BYTES = 512;

static UnsignedInt findChar(const char* s, UnsignedInt len, UnsignedInt start, char c)
{
	UnsignedInt i;
	for (i = start; i < len; ++i)
	{
		if (s[i] == c)
		{
			return i;
		}
	}
	return len;
}

static void copyBounded(char* dest, UnsignedInt destCap, const char* src, UnsignedInt srcLen)
{
	UnsignedInt i;
	if (destCap == 0)
	{
		return;
	}
	for (i = 0; i < srcLen && i + 1 < destCap; ++i)
	{
		dest[i] = src[i];
	}
	dest[i] = '\0';
}

static Int parseDecimal(const char* s, UnsignedInt len, Int fallback)
{
	UnsignedInt i = 0;
	Bool negative = FALSE;
	Int value = 0;
	Bool any = FALSE;

	while (i < len && (s[i] == ' ' || s[i] == '\t'))
	{
		++i;
	}
	if (i < len && (s[i] == '-' || s[i] == '+'))
	{
		negative = (s[i] == '-') ? TRUE : FALSE;
		++i;
	}
	while (i < len && s[i] >= '0' && s[i] <= '9')
	{
		value = value * 10 + (s[i] - '0');
		any = TRUE;
		++i;
	}
	if (!any)
	{
		return fallback;
	}
	return negative ? -value : value;
}

static UnsignedInt parseHex(const char* s, UnsignedInt len)
{
	UnsignedInt i;
	UnsignedInt value = 0;

	for (i = 0; i < len; ++i)
	{
		Char c = s[i];
		UnsignedInt digit;
		if (c >= '0' && c <= '9') digit = (UnsignedInt)(c - '0');
		else if (c >= 'a' && c <= 'f') digit = (UnsignedInt)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') digit = (UnsignedInt)(c - 'A' + 10);
		else break;
		value = (value << 4) | digit;
	}
	return value;
}

static void clearLobbySlot(LobbySlot& slot)
{
	memset(&slot, 0, sizeof(slot));
	slot.kind = LOBBY_SLOT_EMPTY;
	slot.color = -1;
	slot.playerTemplate = -1;
	slot.startPos = -1;
	slot.team = -1;
	slot.nat = 0;
}

static void clearLobbyState(LobbyState& state)
{
	UnsignedInt i;
	memset(&state, 0, sizeof(state));
	state.valid = FALSE;
	state.zeroHour = FALSE;
	state.mapContentsMask = 0;
	state.mapSize = 0;
	state.seed = 0;
	state.crcInterval = 0;
	state.superweaponRestriction = 0;
	state.startingCash = 0;
	state.oldFactionsOnly = FALSE;
	state.useStats = TRUE;
	state.slotCount = 0;
	for (i = 0; i < LOBBY_MAX_SLOTS; ++i)
	{
		clearLobbySlot(state.slots[i]);
	}
}

static void parseHumanSlot(const char* s, UnsignedInt len, LobbySlot& slot)
{
	UnsignedInt pos = 0;
	UnsignedInt fieldNo;

	slot.kind = LOBBY_SLOT_HUMAN;
	for (fieldNo = 0; fieldNo < 9 && pos <= len; ++fieldNo)
	{
		UnsignedInt end = findChar(s, len, pos, ',');
		const char* field = s + pos;
		UnsignedInt fieldLen = end - pos;

		switch (fieldNo)
		{
		case 0: copyBounded(slot.name, LOBBY_NAME_BYTES, field, fieldLen); break;
		case 1: slot.ip = parseHex(field, fieldLen); break;
		case 2: slot.port = (UnsignedInt)parseDecimal(field, fieldLen, 0); break;
		case 3:
			if (fieldLen >= 1) slot.accepted = (field[0] == 'T') ? TRUE : FALSE;
			if (fieldLen >= 2) slot.hasMap = (field[1] == 'T') ? TRUE : FALSE;
			break;
		case 4: slot.color = parseDecimal(field, fieldLen, -1); break;
		case 5: slot.playerTemplate = parseDecimal(field, fieldLen, -1); break;
		case 6: slot.startPos = parseDecimal(field, fieldLen, -1); break;
		case 7: slot.team = parseDecimal(field, fieldLen, -1); break;
		case 8: slot.nat = parseDecimal(field, fieldLen, 0); break;
		}
		if (end >= len)
		{
			break;
		}
		pos = end + 1;
	}
}

static void parseAiSlot(const char* s, UnsignedInt len, LobbySlot& slot)
{
	UnsignedInt pos = 0;
	UnsignedInt fieldNo;

	slot.kind = LOBBY_SLOT_AI;
	for (fieldNo = 0; fieldNo < 5 && pos <= len; ++fieldNo)
	{
		UnsignedInt end = findChar(s, len, pos, ',');
		const char* field = s + pos;
		UnsignedInt fieldLen = end - pos;

		switch (fieldNo)
		{
		case 0: if (fieldLen >= 1) slot.aiDifficulty = field[0]; break;
		case 1: slot.color = parseDecimal(field, fieldLen, -1); break;
		case 2: slot.playerTemplate = parseDecimal(field, fieldLen, -1); break;
		case 3: slot.startPos = parseDecimal(field, fieldLen, -1); break;
		case 4: slot.team = parseDecimal(field, fieldLen, -1); break;
		}
		if (end >= len)
		{
			break;
		}
		pos = end + 1;
	}
}

static void parseSlot(const char* token, UnsignedInt len, LobbySlot& slot)
{
	clearLobbySlot(slot);
	if (len == 0)
	{
		return;
	}
	switch (token[0])
	{
	case 'H': parseHumanSlot(token + 1, len - 1, slot); break;
	case 'C': parseAiSlot(token + 1, len - 1, slot); break;
	case 'O': slot.kind = LOBBY_SLOT_OPEN; break;
	case 'X': slot.kind = LOBBY_SLOT_CLOSED; break;
	default: slot.kind = LOBBY_SLOT_EMPTY; break;
	}
}

static void parseSlotList(const char* val, UnsignedInt len, LobbyState& out)
{
	UnsignedInt pos = 0;
	UnsignedInt index = 0;

	while (index < LOBBY_MAX_SLOTS && pos < len)
	{
		UnsignedInt end = findChar(val, len, pos, ':');
		parseSlot(val + pos, end - pos, out.slots[index]);
		++index;
		if (end >= len)
		{
			break;
		}
		pos = end + 1;
	}
	out.slotCount = index;
}

static void parseKeyValue(const char* kv, UnsignedInt len, LobbyState& out, Bool& sawSlotList)
{
	UnsignedInt eq;
	UnsignedInt keyLen;
	UnsignedInt valLen;
	const char* key;
	const char* val;

	eq = findChar(kv, len, 0, '=');
	if (eq >= len)
	{
		return;
	}
	key = kv;
	keyLen = eq;
	val = kv + eq + 1;
	valLen = len - eq - 1;
	if (valLen == 0)
	{
		return;
	}

	if (keyLen == 2 && key[0] == 'D' && key[1] == 'S')
	{
		out.countdownSeconds = parseDecimal(val, valLen, 0);
	}
	else if (keyLen == 2 && key[0] == 'D' && key[1] == 'R')
	{
		out.countdownRevision = parseHex(val, valLen);
	}
	else if (keyLen == 2 && key[0] == 'U' && key[1] == 'S')
	{
		out.useStats = parseDecimal(val, valLen, out.useStats);
	}
	else if (keyLen == 2 && key[0] == 'M' && key[1] == 'C')
	{
		out.mapCRC = parseHex(val, valLen);
	}
	else if (keyLen == 2 && key[0] == 'M' && key[1] == 'S')
	{
		out.mapSize = parseDecimal(val, valLen, 0);
	}
	else if (keyLen == 2 && key[0] == 'S' && key[1] == 'D')
	{
		out.seed = parseDecimal(val, valLen, 0);
	}
	else if (keyLen == 2 && key[0] == 'S' && key[1] == 'R')
	{
		out.superweaponRestriction = (UnsignedInt)parseDecimal(val, valLen, 0);
		out.zeroHour = TRUE;
	}
	else if (keyLen == 2 && key[0] == 'S' && key[1] == 'C')
	{
		out.startingCash = (UnsignedInt)parseDecimal(val, valLen, 0);
		out.zeroHour = TRUE;
	}
	else if (keyLen == 1 && key[0] == 'M')
	{

		if (valLen < 3)
		{
			return;
		}
		out.mapContentsMask = (Int)parseHex(val, 2);
		copyBounded(out.mapName, LOBBY_MAP_BYTES, val + 2, valLen - 2);
	}
	else if (keyLen == 1 && key[0] == 'C')
	{
		out.crcInterval = parseDecimal(val, valLen, 0);
	}
	else if (keyLen == 1 && key[0] == 'O')
	{
		out.oldFactionsOnly = (valLen >= 1 && (val[0] == 'Y' || val[0] == 'y')) ? TRUE : FALSE;
	}
	else if (keyLen == 1 && key[0] == 'S')
	{
		parseSlotList(val, valLen, out);
		sawSlotList = TRUE;
	}
}

Bool parseLobbyState(const char* text, UnsignedInt len, LobbyState& out)
{
	char buf[LOBBY_PARSE_BYTES + 1];
	UnsignedInt i;
	UnsignedInt pos;
	Bool sawSlotList = FALSE;

	clearLobbyState(out);
	if (text == NULL || len == 0)
	{
		return FALSE;
	}
	if (len > LOBBY_PARSE_BYTES)
	{
		len = LOBBY_PARSE_BYTES;
	}
	for (i = 0; i < len; ++i)
	{
		buf[i] = text[i];
	}
	buf[len] = '\0';

	pos = 0;
	while (pos < len)
	{
		UnsignedInt end = findChar(buf, len, pos, ';');
		parseKeyValue(buf + pos, end - pos, out, sawSlotList);
		if (end >= len)
		{
			break;
		}
		pos = end + 1;
	}

	out.valid = sawSlotList;
	return out.valid;
}

Bool takeCountdown(const LobbyState& state, UnsignedInt& lastRevision)
{
	if (!state.valid || state.countdownRevision == lastRevision)
		return FALSE;
	lastRevision = state.countdownRevision;
	return state.countdownSeconds > 0 ? TRUE : FALSE;
}

LobbyStatus classifyLobbyStatus(Bool watching, UnsignedInt watchedGameUid,
	Bool stateParsed, UnsignedInt stateGameUid, UnsignedInt nowMs, UnsignedInt stateMs,
	UnsignedInt staleMs)
{
	Int age;

	if (!watching || watchedGameUid == 0)
	{
		return LOBBY_STATUS_ABSENT;
	}
	if (stateGameUid == 0 || stateGameUid != watchedGameUid)
	{
		return LOBBY_STATUS_ABSENT;
	}
	if (!stateParsed)
	{
		return LOBBY_STATUS_UNPARSED;
	}
	age = (Int)(nowMs - stateMs);
	if (age < 0)
	{
		age = 0;
	}
	if (age > (Int)staleMs)
	{
		return LOBBY_STATUS_STALE;
	}
	return LOBBY_STATUS_OK;
}

LobbyStatus classifyLobbyFeed(Bool watching, LobbySubscription subscription, LobbyStatus snapshot)
{

	if (snapshot != LOBBY_STATUS_ABSENT)
	{
		return snapshot;
	}
	if (!watching)
	{
		return LOBBY_STATUS_NO_WATCH;
	}

	switch (subscription)
	{
	case LOBBY_SUB_ACTIVE:

		return LOBBY_STATUS_WAITING;
	case LOBBY_SUB_PENDING:
		return LOBBY_STATUS_SUBSCRIBE_PENDING;
	case LOBBY_SUB_UNSENT:
		return LOBBY_STATUS_NO_SUBSCRIPTION;
	default:
		return LOBBY_STATUS_NO_SOURCE;
	}
}

Bool shouldForwardLobby(Bool subscribed, Bool haveLast, Bool changed, UnsignedInt nowMs,
	UnsignedInt lastMs, UnsignedInt intervalMs)
{
	if (!subscribed)
	{
		return FALSE;
	}
	if (!haveLast)
	{
		return TRUE;
	}
	if (changed)
	{
		return TRUE;
	}
	return ((Int)(nowMs - lastMs) >= (Int)intervalMs) ? TRUE : FALSE;
}

void buildLobbyView(const LobbyState& state, LobbyStatus status, LobbyViewModel& out)
{
	UnsignedInt i;
	Bool haveData;

	memset(&out, 0, sizeof(out));
	out.status = status;

	out.mutatingEnabled = FALSE;
	out.rowCount = 0;

	haveData = (state.valid && (status == LOBBY_STATUS_OK || status == LOBBY_STATUS_STALE))
		? TRUE : FALSE;
	if (!haveData)
	{
		return;
	}

	out.hasSnapshot = TRUE;
	out.optionsVisible = TRUE;
	out.mapContentsMask = state.mapContentsMask;
	out.superweaponRestriction = state.superweaponRestriction;
	out.startingCash = state.startingCash;
	copyBounded(out.mapName, LOBBY_MAP_BYTES + 1, state.mapName,
		(UnsignedInt)strlen(state.mapName));

	out.rowCount = (state.slotCount > LOBBY_MAX_SLOTS) ? LOBBY_MAX_SLOTS : state.slotCount;
	for (i = 0; i < out.rowCount; ++i)
	{
		out.rows[i].visible = TRUE;
		out.rows[i].kind = state.slots[i].kind;
		out.rows[i].accepted = state.slots[i].accepted || (i == 0 && state.slots[i].kind == LOBBY_SLOT_HUMAN);
		out.rows[i].hasMap = state.slots[i].hasMap;
		out.rows[i].color = state.slots[i].color;
		out.rows[i].playerTemplate = state.slots[i].playerTemplate;
		out.rows[i].startPos = state.slots[i].startPos;
		out.rows[i].team = state.slots[i].team;
		out.rows[i].nat = state.slots[i].nat;
		out.rows[i].aiDifficulty = state.slots[i].aiDifficulty;
		copyBounded(out.rows[i].name, LOBBY_NAME_BYTES + 1, state.slots[i].name,
			(UnsignedInt)strlen(state.slots[i].name));
	}
}

static Bool lobbyViewRowEquals(const LobbyViewRow& a, const LobbyViewRow& b)
{
	return a.visible == b.visible && a.kind == b.kind && a.accepted == b.accepted
		&& a.hasMap == b.hasMap && a.color == b.color && a.playerTemplate == b.playerTemplate
		&& a.startPos == b.startPos && a.team == b.team && a.nat == b.nat
		&& a.aiDifficulty == b.aiDifficulty
		&& strncmp(a.name, b.name, LOBBY_NAME_BYTES + 1) == 0;
}

Bool lobbyViewEquals(const LobbyViewModel& a, const LobbyViewModel& b)
{
	UnsignedInt i;

	if (a.status != b.status || a.hasSnapshot != b.hasSnapshot
		|| a.optionsVisible != b.optionsVisible || a.mutatingEnabled != b.mutatingEnabled
		|| a.mapContentsMask != b.mapContentsMask
		|| a.superweaponRestriction != b.superweaponRestriction
		|| a.startingCash != b.startingCash || a.rowCount != b.rowCount
		|| strncmp(a.mapName, b.mapName, LOBBY_MAP_BYTES + 1) != 0)
	{
		return FALSE;
	}
	for (i = 0; i < a.rowCount; ++i)
	{
		if (!lobbyViewRowEquals(a.rows[i], b.rows[i]))
		{
			return FALSE;
		}
	}
	return TRUE;
}

static void appendBounded(char* out, UnsignedInt cap, UnsignedInt& used, const char* text)
{
	UnsignedInt i = 0;

	if (text == NULL)
	{
		return;
	}
	while (text[i] != '\0' && used + 1 < cap)
	{
		out[used] = text[i];
		++used;
		++i;
	}
	out[used] = '\0';
}

UnsignedInt formatReadOnlyOpenFailure(ReadOnlyOpenStage stage, const char* firstMissingGadget,
	char* out, UnsignedInt cap)
{
	UnsignedInt used = 0;

	if (out == NULL || cap == 0)
	{
		return 0;
	}
	out[0] = '\0';
	if (stage == READONLY_OPEN_OK)
	{
		return 0;
	}

	switch (stage)
	{
	case READONLY_OPEN_LAYOUT_FAILED:
		appendBounded(out, cap, used,
			"Cannot cast: the read-only lobby layout could not be loaded "
			"(Menus/LanGameOptionsMenu.wnd).");
		break;
	case READONLY_OPEN_PARENT_MISSING:
		appendBounded(out, cap, used,
			"Cannot cast: the read-only lobby layout loaded but its top window "
			"LanGameOptionsMenu.wnd:LanGameOptionsMenuParent was not found.");
		break;
	case READONLY_OPEN_GADGET_MISSING:
		appendBounded(out, cap, used,
			"Cannot cast: the read-only lobby is missing the gadget ");
		if (firstMissingGadget != NULL && firstMissingGadget[0] != '\0')
		{
			appendBounded(out, cap, used, firstMissingGadget);
		}
		else
		{
			appendBounded(out, cap, used, "(unknown)");
		}
		appendBounded(out, cap, used, ".");
		break;
	default:
		break;
	}
	return used;
}

OpenFailureLatch::OpenFailureLatch()
{
	clear();
}

void OpenFailureLatch::clear()
{
	m_gameUid = 0;
}

Bool OpenFailureLatch::shouldSkip(UnsignedInt gameUid) const
{
	if (gameUid == 0)
	{
		return FALSE;
	}
	return (m_gameUid == gameUid) ? TRUE : FALSE;
}

Bool OpenFailureLatch::latch(UnsignedInt gameUid)
{
	if (gameUid == 0 || m_gameUid == gameUid)
	{
		return FALSE;
	}
	m_gameUid = gameUid;
	return TRUE;
}

GestureResult classifyGesture(Bool gameSelected, Bool shiftHeld, Bool featureEnabled,
	Bool readOnlyLobby)
{
	if (!gameSelected)
	{
		return GESTURE_NONE;
	}

	if (!shiftHeld)
	{
		return GESTURE_JOIN;
	}

	if (!featureEnabled)
	{
		return GESTURE_JOIN;
	}

	if (!readOnlyLobby)
	{
		return GESTURE_CAST_UNAVAILABLE;
	}

	return GESTURE_CAST;
}

ChatSurface selectActiveChatSurface(Bool readOnlyBoxPresent, Bool lobbyBoxPresent,
	Bool gameBoxPresent, Bool scoreBoxPresent)
{

	if (readOnlyBoxPresent)
	{
		return CHAT_SURFACE_READ_ONLY;
	}

	if (lobbyBoxPresent)
	{
		return CHAT_SURFACE_LAN_LOBBY;
	}
	if (gameBoxPresent)
	{
		return CHAT_SURFACE_LAN_GAME_OPTIONS;
	}
	if (scoreBoxPresent)
	{
		return CHAT_SURFACE_SCORE_SCREEN;
	}
	return CHAT_SURFACE_NONE;
}

LineQueue::LineQueue()
{
	clear();
}

void LineQueue::clear()
{
	UnsignedInt i;

	for (i = 0; i < MAX_LOBBY_LINES; ++i)
	{
		memset(&m_lines[i], 0, sizeof(m_lines[i]));
	}
	m_head = 0;
	m_count = 0;
	m_dropped = 0;
}

Bool LineQueue::push(const char* utf8Text, UnsignedInt utf8Len, const char* senderName,
	UnsignedInt senderNameLen, UnsignedByte senderSlot, Bool senderIsCaster, Bool isEmote)
{
	UnsignedInt slot;
	UnsignedInt i;
	UnsignedInt length;

	if (utf8Text == NULL || utf8Len == 0)
	{
		return FALSE;
	}
	if (m_count >= MAX_LOBBY_LINES)
	{

		++m_dropped;
		return FALSE;
	}

	length = utf8Len;
	if (length > LOBBY_LINE_BYTES)
	{
		length = LOBBY_LINE_BYTES;
	}

	slot = (m_head + m_count) % MAX_LOBBY_LINES;
	for (i = 0; i < length; ++i)
	{
		m_lines[slot].text[i] = utf8Text[i];
	}
	m_lines[slot].text[length] = '\0';
	m_lines[slot].textLen = length;
	m_lines[slot].senderNameLen = (senderName != NULL) ? senderNameLen : 0;
	if (m_lines[slot].senderNameLen > sizeof(m_lines[slot].senderName) - 1)
		m_lines[slot].senderNameLen = sizeof(m_lines[slot].senderName) - 1;
	if (senderName != NULL && m_lines[slot].senderNameLen != 0)
		memcpy(m_lines[slot].senderName, senderName, m_lines[slot].senderNameLen);
	m_lines[slot].senderName[m_lines[slot].senderNameLen] = '\0';
	m_lines[slot].senderSlot = senderSlot;
	m_lines[slot].senderIsCaster = senderIsCaster;
	m_lines[slot].isEmote = isEmote;
	++m_count;
	return TRUE;
}

Bool LineQueue::pop(Line& out)
{
	if (m_count == 0)
	{
		return FALSE;
	}

	out = m_lines[m_head];
	memset(&m_lines[m_head], 0, sizeof(m_lines[m_head]));
	m_head = (m_head + 1) % MAX_LOBBY_LINES;
	--m_count;
	return TRUE;
}

UnsignedInt LineQueue::count() const
{
	return m_count;
}

UnsignedInt LineQueue::dropped() const
{
	return m_dropped;
}

}
