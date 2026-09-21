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

#include "GameNetwork/Caster/CasterBeacon.h"

#include <string.h>

namespace CasterBeacon
{

static const UnsignedInt MAX_IP_CHARS = 16;

static void copyBounded(char* dest, UnsignedInt cap, const char* source)
{
	UnsignedInt i = 0;

	if (dest == NULL || cap == 0 || source == NULL)
	{
		return;
	}
	while (source[i] != '\0' && i + 1 < cap)
	{
		dest[i] = source[i];
		++i;
	}
	dest[i] = '\0';
}

Bool buildMessage(const char* uid, UnsignedShort tcpPort, UnsignedInt gameUid,
	char* uidOut, UnsignedInt uidCap, UnsignedShort& tcpPortOut,
	UnsignedByte& versionOut, UnsignedInt& gameUidOut)
{
	char normalized[MAX_UID_CHARS + 1];

	tcpPortOut = 0;
	versionOut = 0;
	gameUidOut = 0;
	if (uidOut != NULL && uidCap != 0)
	{
		uidOut[0] = '\0';
	}

	if (uidOut == NULL || uidCap < MAX_UID_CHARS + 1)
	{
		return FALSE;
	}
	if (gameUid == 0 || tcpPort == 0)
	{
		return FALSE;
	}
	if (!normalizeUid(uid, normalized, sizeof(normalized)))
	{
		return FALSE;
	}

	copyBounded(uidOut, uidCap, normalized);
	tcpPortOut = tcpPort;
	versionOut = CasterProtocol::PROTOCOL_VERSION;
	gameUidOut = gameUid;
	return TRUE;
}

Bool parsePayload(const char* uid, UnsignedInt uidCap, UnsignedShort tcpPort,
	UnsignedInt gameUid, Payload& out)
{
	char normalized[MAX_UID_CHARS + 1];

	out.uid[0] = '\0';
	out.tcpPort = 0;
	out.gameUid = 0;

	if (uid == NULL || uidCap < MAX_UID_CHARS + 1)
	{
		return FALSE;
	}
	if (gameUid == 0 || tcpPort == 0)
	{
		return FALSE;
	}
	if (!normalizeUid(uid, normalized, sizeof(normalized)))
	{
		return FALSE;
	}

	copyBounded(out.uid, sizeof(out.uid), normalized);
	out.tcpPort = tcpPort;
	out.gameUid = gameUid;
	return TRUE;
}

Bool isProtocolCompatible(UnsignedByte protocolVersion)
{
	return (protocolVersion == CasterProtocol::PROTOCOL_VERSION) ? TRUE : FALSE;
}

Bool decodeRoomIdentity(const WideChar* name, UnsignedInt length, UnsignedInt& hostIP, UnsignedInt& seed)
{
	hostIP = 0;
	seed = 0;
	if (name == NULL || length < 16) return FALSE;
	UnsignedInt values[2] = { 0, 0 };
	for (UnsignedInt i = 0; i < 16; ++i)
	{
		WideChar ch = name[i];
		UnsignedInt digit;
		if (ch >= L'0' && ch <= L'9') digit = ch - L'0';
		else if (ch >= L'A' && ch <= L'F') digit = ch - L'A' + 10;
		else if (ch >= L'a' && ch <= L'f') digit = ch - L'a' + 10;
		else return FALSE;
		values[i / 8] = (values[i / 8] << 4) | digit;
	}
	hostIP = values[0];
	seed = values[1];
	return hostIP != 0;
}
UnsignedInt gameUid(UnsignedInt hostIP, UnsignedInt seed)
{
	return CasterProtocol::computeGameUid(hostIP, seed);
}

Bool normalizeUid(const char* source, char* out, UnsignedInt outCap)
{
	UnsignedInt i = 0;

	if (out == NULL || outCap == 0)
	{
		return FALSE;
	}
	out[0] = '\0';
	if (source == NULL)
	{
		return FALSE;
	}

	while (source[i] != '\0' && i < MAX_UID_CHARS)
	{
		char c = source[i];
		if (c < 0x21 || c > 0x7E || c == '|')
		{
			out[0] = '\0';
			return FALSE;
		}
		if (i + 1 >= outCap)
		{
			out[0] = '\0';
			return FALSE;
		}
		out[i] = c;
		++i;
	}

	if (i == 0 || source[i] != '\0')
	{
		out[0] = '\0';
		return FALSE;
	}
	out[i] = '\0';
	return TRUE;
}

SourceTable::SourceTable()
{
	m_nextSourceId = 0;
	reset();
}

void SourceTable::reset()
{
	UnsignedInt i;

	for (i = 0; i < MAX_SOURCES; ++i)
	{
		m_sources[i].gameUid = 0;
		m_sources[i].uid[0] = '\0';
		m_sources[i].ip[0] = '\0';
		m_sources[i].tcpPort = 0;
		m_sources[i].isHost = FALSE;
		m_sources[i].active = FALSE;
		m_sources[i].sourceId = 0;
	}
}

Source* SourceTable::findEntry(const char* uid, const char* ip)
{
	UnsignedInt i;

	for (i = 0; i < MAX_SOURCES; ++i)
	{
		if (m_sources[i].active
			&& strcmp(m_sources[i].uid, uid) == 0
			&& strcmp(m_sources[i].ip, ip) == 0)
		{
			return &m_sources[i];
		}
	}
	return NULL;
}

Source* SourceTable::findFree()
{
	UnsignedInt i;

	for (i = 0; i < MAX_SOURCES; ++i)
	{
		if (!m_sources[i].active)
		{
			return &m_sources[i];
		}
	}
	return NULL;
}

CastResult SourceTable::cast(UnsignedInt gameUid, const char* uid, const char* ip,
	UnsignedShort tcpPort, Bool isHost, UnsignedInt& sourceIdOut)
{
	Source* entry;
	UnsignedInt uidLen;

	sourceIdOut = 0;

	if (gameUid == 0 || uid == NULL || ip == NULL || tcpPort == 0)
	{
		return SOURCE_REJECTED;
	}
	uidLen = (UnsignedInt)strlen(uid);
	if (uidLen == 0 || uidLen > MAX_UID_CHARS || ip[0] == '\0' || strlen(ip) >= MAX_IP_CHARS)
	{
		return SOURCE_REJECTED;
	}

	entry = findEntry(uid, ip);
	if (entry != NULL)
	{

		entry->gameUid = gameUid;
		if (isHost)
		{
			entry->isHost = TRUE;
		}
		sourceIdOut = entry->sourceId;
		return SOURCE_REFRESHED;
	}

	entry = findFree();
	if (entry == NULL)
	{
		return SOURCE_TABLE_FULL;
	}

	copyBounded(entry->uid, sizeof(entry->uid), uid);
	copyBounded(entry->ip, sizeof(entry->ip), ip);
	entry->gameUid = gameUid;
	entry->tcpPort = tcpPort;
	entry->isHost = isHost;
	entry->active = TRUE;

	++m_nextSourceId;
	entry->sourceId = m_nextSourceId;
	sourceIdOut = entry->sourceId;
	return SOURCE_ADDED;
}

Bool SourceTable::hasIp(const char* ip) const
{
	UnsignedInt i;

	if (ip == NULL)
	{
		return FALSE;
	}
	for (i = 0; i < MAX_SOURCES; ++i)
	{
		if (m_sources[i].active && strcmp(m_sources[i].ip, ip) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

Bool SourceTable::getSourceAt(UnsignedInt index, Source& out) const
{
	UnsignedInt i;
	UnsignedInt seen = 0;

	for (i = 0; i < MAX_SOURCES; ++i)
	{
		if (!m_sources[i].active)
		{
			continue;
		}
		if (seen == index)
		{
			out = m_sources[i];
			return TRUE;
		}
		++seen;
	}
	return FALSE;
}

Bool shouldRequestDetails(Bool waiting, UnsignedInt nowMs,
 UnsignedInt lastRequestMs, UnsignedInt attempts)
{
 return waiting && attempts < 5
  && (attempts == 0 || nowMs - lastRequestMs >= 500);
}

UnsignedInt queryBackoffMs(UnsignedInt attempts)
{
	// 500ms doubling to a 4s ceiling.
	UnsignedInt delay = 500;
	while (attempts > 0 && delay < 4000)
	{
		delay *= 2;
		--attempts;
	}
	return (delay > 4000) ? 4000 : delay;
}

Bool shouldQueryPlayers(Bool pending, UnsignedInt nowMs,
	UnsignedInt lastQueryMs, UnsignedInt attempts)
{
	return pending
		&& (attempts == 0 || (UnsignedInt)(nowMs - lastQueryMs) >= queryBackoffMs(attempts - 1));
}

}
