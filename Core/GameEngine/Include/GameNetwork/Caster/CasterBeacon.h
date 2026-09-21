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
#include "GameNetwork/Caster/CasterProtocol.h"

namespace CasterBeacon
{

static const UnsignedInt MESSAGE_TYPE = CasterProtocol::BEACON_MESSAGE_TYPE;

static const UnsignedInt MAX_UID_CHARS = CasterProtocol::BEACON_TOKEN_CHARS - 6;

static const UnsignedInt MAX_SOURCES = 16;

Bool shouldRequestDetails(Bool waiting, UnsignedInt nowMs,
 UnsignedInt lastRequestMs, UnsignedInt attempts);

// Per-player capability queries reuse the details request; retried with a
// growing delay (no attempt cap) until every occupied slot has answered or the
// caller stops asking.

UnsignedInt queryBackoffMs(UnsignedInt attempts);
Bool shouldQueryPlayers(Bool pending, UnsignedInt nowMs,
	UnsignedInt lastQueryMs, UnsignedInt attempts);

struct Payload
{
	char uid[MAX_UID_CHARS + 1];
	UnsignedShort tcpPort;
	UnsignedInt gameUid;
};

/**
 * Fills the typed caster fields of an outgoing capability reply. On FALSE the
 * outputs are zeroed and the reply must not be sent.
 */
Bool buildMessage(const char* uid, UnsignedShort tcpPort, UnsignedInt gameUid,
	char* uidOut, UnsignedInt uidCap, UnsignedShort& tcpPortOut,
	UnsignedByte& versionOut, UnsignedInt& gameUidOut);

/// Reads the typed caster fields of a received capability reply.
Bool parsePayload(const char* uid, UnsignedInt uidCap, UnsignedShort tcpPort,
	UnsignedInt gameUid, Payload& out);

/// Strict: the reply must carry exactly this build's protocol version.
Bool isProtocolCompatible(UnsignedByte protocolVersion);

Bool normalizeUid(const char* source, char* out, UnsignedInt outCap);

UnsignedInt gameUid(UnsignedInt hostIP, UnsignedInt seed);
Bool decodeRoomIdentity(const WideChar* name, UnsignedInt length, UnsignedInt& hostIP, UnsignedInt& seed);

enum CastResult
{
	SOURCE_REJECTED   = 0,
	SOURCE_ADDED      = 1,
	SOURCE_REFRESHED  = 2,
	SOURCE_TABLE_FULL = 3,
};

struct Source
{
	UnsignedInt gameUid;
	char uid[MAX_UID_CHARS + 1];
	char ip[16];
	UnsignedShort tcpPort;
	Bool isHost;
	Bool active;
	UnsignedInt sourceId;
};

class SourceTable
{
public:
	SourceTable();

	void reset();

	/**
	 * Records one capability reply. On SOURCE_ADDED and SOURCE_REFRESHED `sourceIdOut` is the source's id.
	 * `gameUid` 0 is malformed (a reply only exists for an announced game).
	 */
	CastResult cast(UnsignedInt gameUid, const char* uid, const char* ip,
		UnsignedShort tcpPort, Bool isHost,
		UnsignedInt& sourceIdOut);

	Bool hasIp(const char* ip) const;

	Bool getSourceAt(UnsignedInt index, Source& out) const;


private:
	Source* findEntry(const char* uid, const char* ip);
	Source* findFree();

	Source m_sources[MAX_SOURCES];
	UnsignedInt m_nextSourceId;

	SourceTable(const SourceTable&);
	SourceTable& operator=(const SourceTable&);
};

}
