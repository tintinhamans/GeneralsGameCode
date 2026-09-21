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

#include "GameNetwork/Caster/CasterProtocol.h"
#include "GameNetwork/Caster/CasterBootstrap.h"
#include "GameNetwork/Caster/CasterLobby.h"

#include <string.h>

namespace CasterProtocol
{

void Writer::reset(UnsignedByte* buffer, UnsignedInt capacity)
{
	buf = buffer;
	cap = capacity;
	len = 0;
	ok = TRUE;
}

void Writer::u8(UnsignedByte value)
{
	if (!ok)
	{
		return;
	}
	if (len + 1 > cap)
	{
		ok = FALSE;
		return;
	}
	buf[len++] = value;
}

void Writer::u16(UnsignedShort value)
{
	u8((UnsignedByte)(value & 0xFF));
	u8((UnsignedByte)((value >> 8) & 0xFF));
}

void Writer::u32(UnsignedInt value)
{
	u8((UnsignedByte)(value & 0xFF));
	u8((UnsignedByte)((value >> 8) & 0xFF));
	u8((UnsignedByte)((value >> 16) & 0xFF));
	u8((UnsignedByte)((value >> 24) & 0xFF));
}

void Writer::raw(const void* data, UnsignedInt size)
{
	if (!ok)
	{
		return;
	}
	if (len + size > cap)
	{
		ok = FALSE;
		return;
	}
	if (size > 0)
	{
		memcpy(buf + len, data, size);
	}
	len += size;
}

void Reader::reset(const UnsignedByte* buffer, UnsignedInt size)
{
	buf = buffer;
	len = size;
	pos = 0;
	ok = TRUE;
}

UnsignedByte Reader::u8()
{
	if (!ok || pos + 1 > len)
	{
		ok = FALSE;
		return 0;
	}
	return buf[pos++];
}

UnsignedShort Reader::u16()
{
	UnsignedByte low = u8();
	UnsignedByte high = u8();
	if (!ok)
	{
		return 0;
	}
	return (UnsignedShort)(low | ((UnsignedShort)high << 8));
}

UnsignedInt Reader::u32()
{
	UnsignedByte b0 = u8();
	UnsignedByte b1 = u8();
	UnsignedByte b2 = u8();
	UnsignedByte b3 = u8();
	if (!ok)
	{
		return 0;
	}
	return (UnsignedInt)b0 | ((UnsignedInt)b1 << 8) | ((UnsignedInt)b2 << 16) | ((UnsignedInt)b3 << 24);
}

Bool Reader::rawView(const UnsignedByte*& view, UnsignedInt size)
{
	if (!ok || pos + size > len)
	{
		ok = FALSE;
		return FALSE;
	}
	view = buf + pos;
	pos += size;
	return TRUE;
}

Bool Reader::stringView(const char*& view, UnsignedInt& size)
{
	UnsignedInt stringLen = u16();
	const UnsignedByte* raw = NULL;
	if (!ok || stringLen > MAX_FRAME_BYTES)
	{
		ok = FALSE;
		return FALSE;
	}
	if (!rawView(raw, stringLen))
	{
		return FALSE;
	}
	view = (const char*)raw;
	size = stringLen;
	return TRUE;
}

static UnsignedInt readU32LE(const UnsignedByte* p)
{
	return (UnsignedInt)p[0] | ((UnsignedInt)p[1] << 8) | ((UnsignedInt)p[2] << 16) | ((UnsignedInt)p[3] << 24);
}

static Bool startFrame(Writer& w, UnsignedByte* out, UnsignedInt cap, UnsignedByte type)
{
	if (out == NULL || cap < FRAME_HEADER_BYTES + 1)
	{
		return FALSE;
	}
	if (cap > MAX_FRAME_BYTES)
	{
		cap = MAX_FRAME_BYTES;
	}
	w.reset(out, cap);
	w.len = 4;
	w.u8(type);
	return w.ok;
}

static UnsignedInt finishFrame(Writer& w)
{
	UnsignedInt following;
	if (!w.ok)
	{
		return 0;
	}

	following = w.len - 4;
	w.buf[0] = (UnsignedByte)(following & 0xFF);
	w.buf[1] = (UnsignedByte)((following >> 8) & 0xFF);
	w.buf[2] = (UnsignedByte)((following >> 16) & 0xFF);
	w.buf[3] = (UnsignedByte)((following >> 24) & 0xFF);
	return w.len;
}

static void writeString(Writer& w, const char* s)
{
	UnsignedInt n = (s != NULL) ? (UnsignedInt)strlen(s) : 0;
	if (n > 0xFFFF)
	{
		w.ok = FALSE;
		return;
	}
	w.u16((UnsignedShort)n);
	if (n > 0)
	{
		w.raw(s, n);
	}
}

UnsignedInt encodeHello(UnsignedByte* out, UnsignedInt cap)
{
	Writer w;
	if (!startFrame(w, out, cap, FRAME_HELLO))
	{
		return 0;
	}
	w.u8(PROTOCOL_VERSION);
	return finishFrame(w);
}

UnsignedInt encodeHelloAck(UnsignedByte* out, UnsignedInt cap, Bool matchRunning, const char* gameName)
{
	Writer w;
	if (!startFrame(w, out, cap, FRAME_HELLO_ACK))
	{
		return 0;
	}
	w.u8(PROTOCOL_VERSION);
	w.u8(matchRunning ? (UnsignedByte)1 : (UnsignedByte)0);
	writeString(w, gameName);
	return finishFrame(w);
}

UnsignedInt encodeSubscribe(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid)
{
	Writer w;
	if (!startFrame(w, out, cap, FRAME_SUBSCRIBE))
	{
		return 0;
	}
	w.u32(gameUid);
	w.u8(0);	// mode: live
	return finishFrame(w);
}

UnsignedInt encodeSubAck(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedByte state)
{
	Writer w;
	if (!startFrame(w, out, cap, FRAME_SUB_ACK))
	{
		return 0;
	}
	w.u32(gameUid);
	w.u8(state);
	return finishFrame(w);
}

static Bool boundedStringLength(const char* text, UnsignedInt maxLength, UnsignedInt& length)
{
	length = 0;
	if (text == NULL)
	{
		return FALSE;
	}
	while (length <= maxLength && text[length] != '\0')
	{
		++length;
	}
	return (length <= maxLength) ? TRUE : FALSE;
}

static void writeBoundedString(Writer& w, const char* text, UnsignedInt maxLength)
{
	UnsignedInt length;
	if (!boundedStringLength(text, maxLength, length))
	{
		w.ok = FALSE;
		return;
	}
	w.u16((UnsignedShort)length);
	w.raw(text, length);
}

static Bool readBoundedString(Reader& r, char* out, UnsignedInt capacity,
	UnsignedInt maxLength)
{
	const char* text = NULL;
	UnsignedInt length = 0;
	if (!r.stringView(text, length) || length > maxLength || length + 1 > capacity)
	{
		return FALSE;
	}
	if (length != 0)
	{
		memcpy(out, text, length);
	}
	out[length] = '\0';
	return TRUE;
}

static void writeSigned(Writer& w, Int value)
{
	w.u32((UnsignedInt)value);
}

static Int readSigned(Reader& r)
{
	return (Int)r.u32();
}

UnsignedInt encodeCommandFragment(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt simulationFrame, UnsignedInt totalBytes, UnsignedInt checksum, UnsignedInt fragmentOffset, const UnsignedByte* bytes, UnsignedInt fragmentLength)
{
	Writer w;
	if (totalBytes == 0 || totalBytes > MAX_COMMAND_FRAME_BYTES || fragmentLength == 0 || fragmentLength > MAX_COMMAND_FRAGMENT_BYTES || bytes == NULL || fragmentOffset >= totalBytes || fragmentLength > totalBytes - fragmentOffset)
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_COMMAND_FRAGMENT))
	{
		return 0;
	}
	w.u32(gameUid);
	w.u32(simulationFrame);
	w.u32(totalBytes);
	w.u32(checksum);
	w.u32(fragmentOffset);
	w.raw(bytes, fragmentLength);
	return finishFrame(w);
}

UnsignedInt encodeCommandHistoryReq(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt firstSimulationFrame, UnsignedInt frameCount)
{
	Writer w;
	if (frameCount == 0 || frameCount > MAX_COMMAND_HISTORY_FRAMES || firstSimulationFrame > 0xFFFFFFFFu - (frameCount - 1))
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_COMMAND_HISTORY_REQ))
	{
		return 0;
	}
	w.u32(gameUid);
	w.u32(firstSimulationFrame);
	w.u32(frameCount);
	return finishFrame(w);
}

UnsignedInt encodeCommandEnd(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, Bool hasFrames, UnsignedInt finalSimulationFrame)
{
	Writer w;
	if (!hasFrames && finalSimulationFrame != 0)
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_COMMAND_END))
	{
		return 0;
	}
	w.u32(gameUid);
	w.u8(hasFrames ? 1 : 0);
	w.u32(finalSimulationFrame);
	return finishFrame(w);
}

UnsignedInt encodeChat(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam, UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len, Bool isEmote)
{
	Writer w;
	if (direction > CHAT_LOBBY)
	{
		return 0;
	}
	if (senderNameLen > 0xFFFF || (senderNameLen > 0 && senderName == NULL)
		|| utf8Len > 0xFFFF || (utf8Len > 0 && utf8Text == NULL))
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_CHAT))
	{
		return 0;
	}
	w.u32(gameUid);

	w.u32(commandID);
	w.u8(direction);
	w.u8(senderSlot);
	w.u8(isSenderCaster);
	w.u8(senderTeam);
	w.u32(senderIdentity);
	w.u16((UnsignedShort)senderNameLen);
	if (senderNameLen > 0)
	{
		w.raw(senderName, senderNameLen);
	}
	w.u16((UnsignedShort)utf8Len);
	if (utf8Len > 0)
	{
		w.raw(utf8Text, utf8Len);
	}
	w.u8(isEmote ? 1 : 0);
	return finishFrame(w);
}

UnsignedInt encodeLobby(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, const char* text, UnsignedInt textLen)
{
	Writer w;
	if (gameUid == 0)
	{
		return 0;
	}
	if (textLen > MAX_LOBBY_TEXT_BYTES || (textLen > 0 && text == NULL))
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_LOBBY))
	{
		return 0;
	}
	w.u32(gameUid);

	w.u16((UnsignedShort)textLen);
	if (textLen > 0)
	{
		w.raw(text, textLen);
	}
	return finishFrame(w);
}

enum RoomSnapshotFlags
{
	ROOM_FLAG_ZERO_HOUR = 1,
	ROOM_FLAG_USE_STATS = 2,
	ROOM_FLAG_OLD_FACTIONS_ONLY = 4,
	ROOM_FLAG_ALL = ROOM_FLAG_ZERO_HOUR | ROOM_FLAG_USE_STATS | ROOM_FLAG_OLD_FACTIONS_ONLY,
};

enum RoomSlotFlags
{
	ROOM_SLOT_ACCEPTED = 1,
	ROOM_SLOT_HAS_MAP = 2,
	ROOM_SLOT_ALL = ROOM_SLOT_ACCEPTED | ROOM_SLOT_HAS_MAP,
};

UnsignedInt encodeRoomSnapshotPayload(UnsignedByte* out, UnsignedInt cap,
	const CasterLobby::RoomSnapshot& snapshot)
{
	Writer w;
	UnsignedInt i;
	UnsignedByte flags = 0;

	if (out == NULL || cap == 0 || snapshot.gameUid == 0
		|| snapshot.slotCount > CasterLobby::LOBBY_MAX_SLOTS)
	{
		return 0;
	}
	if (cap > MAX_FRAME_BYTES)
	{
		cap = MAX_FRAME_BYTES;
	}

	w.reset(out, cap);
	w.u8(ROOM_SNAPSHOT_SCHEMA_VERSION);
	w.u32(snapshot.gameUid);
	w.u32(snapshot.revision);
	writeSigned(w, snapshot.countdownSeconds);
	if (snapshot.zeroHour) flags |= ROOM_FLAG_ZERO_HOUR;
	if (snapshot.useStats) flags |= ROOM_FLAG_USE_STATS;
	if (snapshot.oldFactionsOnly) flags |= ROOM_FLAG_OLD_FACTIONS_ONLY;
	w.u8(flags);
	writeSigned(w, snapshot.mapContentsMask);
	w.u32(snapshot.mapCRC);
	writeSigned(w, snapshot.mapSize);
	writeSigned(w, snapshot.seed);
	writeSigned(w, snapshot.crcInterval);
	w.u32(snapshot.superweaponRestriction);
	w.u32(snapshot.startingCash);
	writeBoundedString(w, snapshot.mapName, CasterLobby::LOBBY_MAP_BYTES);
	w.u8((UnsignedByte)snapshot.slotCount);

	for (i = 0; i < snapshot.slotCount; ++i)
	{
		const CasterLobby::LobbySlot& slot = snapshot.slots[i];
		UnsignedByte slotFlags = 0;
		if (slot.kind > CasterLobby::LOBBY_SLOT_CLOSED)
		{
			return 0;
		}
		if (slot.accepted) slotFlags |= ROOM_SLOT_ACCEPTED;
		if (slot.hasMap) slotFlags |= ROOM_SLOT_HAS_MAP;
		w.u8(slot.kind);
		w.u8(slotFlags);
		w.u32(slot.ip);
		w.u32(slot.port);
		writeSigned(w, slot.color);
		writeSigned(w, slot.playerTemplate);
		writeSigned(w, slot.startPos);
		writeSigned(w, slot.team);
		writeSigned(w, slot.nat);
		w.u8((UnsignedByte)slot.aiDifficulty);
		writeBoundedString(w, slot.name, CasterLobby::LOBBY_NAME_BYTES);
	}

	return w.ok ? w.len : 0;
}

Bool decodeRoomSnapshotPayload(const UnsignedByte* data, UnsignedInt length,
	CasterLobby::RoomSnapshot& snapshot)
{
	Reader r;
	CasterLobby::RoomSnapshot decoded;
	UnsignedByte schemaVersion;
	UnsignedByte flags;
	UnsignedInt i;

	memset(&decoded, 0, sizeof(decoded));
	if (data == NULL || length == 0 || length > MAX_FRAME_BYTES)
	{
		return FALSE;
	}

	r.reset(data, length);
	schemaVersion = r.u8();
	decoded.gameUid = r.u32();
	decoded.revision = r.u32();
	decoded.countdownSeconds = readSigned(r);
	flags = r.u8();
	if (!r.ok || schemaVersion != ROOM_SNAPSHOT_SCHEMA_VERSION || decoded.gameUid == 0
		|| (flags & ~ROOM_FLAG_ALL) != 0)
	{
		return FALSE;
	}
	decoded.zeroHour = (flags & ROOM_FLAG_ZERO_HOUR) ? TRUE : FALSE;
	decoded.useStats = (flags & ROOM_FLAG_USE_STATS) ? TRUE : FALSE;
	decoded.oldFactionsOnly = (flags & ROOM_FLAG_OLD_FACTIONS_ONLY) ? TRUE : FALSE;
	decoded.mapContentsMask = readSigned(r);
	decoded.mapCRC = r.u32();
	decoded.mapSize = readSigned(r);
	decoded.seed = readSigned(r);
	decoded.crcInterval = readSigned(r);
	decoded.superweaponRestriction = r.u32();
	decoded.startingCash = r.u32();
	if (!readBoundedString(r, decoded.mapName, sizeof(decoded.mapName),
			CasterLobby::LOBBY_MAP_BYTES))
	{
		return FALSE;
	}
	decoded.slotCount = r.u8();
	if (!r.ok || decoded.slotCount > CasterLobby::LOBBY_MAX_SLOTS)
	{
		return FALSE;
	}

	for (i = 0; i < decoded.slotCount; ++i)
	{
		CasterLobby::LobbySlot& slot = decoded.slots[i];
		UnsignedByte slotFlags;
		slot.kind = r.u8();
		slotFlags = r.u8();
		if (!r.ok || slot.kind > CasterLobby::LOBBY_SLOT_CLOSED
			|| (slotFlags & ~ROOM_SLOT_ALL) != 0)
		{
			return FALSE;
		}
		slot.accepted = (slotFlags & ROOM_SLOT_ACCEPTED) ? TRUE : FALSE;
		slot.hasMap = (slotFlags & ROOM_SLOT_HAS_MAP) ? TRUE : FALSE;
		slot.ip = r.u32();
		slot.port = r.u32();
		slot.color = readSigned(r);
		slot.playerTemplate = readSigned(r);
		slot.startPos = readSigned(r);
		slot.team = readSigned(r);
		slot.nat = readSigned(r);
		slot.aiDifficulty = (Char)r.u8();
		if (!readBoundedString(r, slot.name, sizeof(slot.name),
				CasterLobby::LOBBY_NAME_BYTES))
		{
			return FALSE;
		}
	}

	if (!r.ok || r.pos != r.len)
	{
		return FALSE;
	}
	snapshot = decoded;
	return TRUE;
}

UnsignedInt encodeBootstrap(UnsignedByte* out, UnsignedInt cap,
	const CasterBootstrap::MatchStart& start)
{
	Writer w;
	UnsignedByte room[MAX_FRAME_BYTES];
	UnsignedInt roomLen;

	if (!CasterBootstrap::isValid(start))
	{
		return 0;
	}
	roomLen = encodeRoomSnapshotPayload(room, sizeof(room), start.room);
	if (roomLen == 0)
	{
		return 0;
	}
	if (!startFrame(w, out, cap, FRAME_BOOTSTRAP))
	{
		return 0;
	}
	w.u32(start.gameUid);
	w.u8(BOOTSTRAP_SCHEMA_VERSION);
	w.u32(start.frameCount);
	w.u32(start.versionNumber);
	w.u32(start.exeCRC);
	w.u32(start.iniCRC);
	writeSigned(w, start.localPlayerIndex);
	writeSigned(w, start.difficulty);
	writeSigned(w, start.originalGameMode);
	writeSigned(w, start.rankPoints);
	writeSigned(w, start.maxFPS);
	writeBoundedString(w, start.versionString, CasterBootstrap::MAX_VERSION_BYTES);
	writeBoundedString(w, start.versionTimeString, CasterBootstrap::MAX_VERSION_BYTES);
	w.raw(room, roomLen);
	return finishFrame(w);
}

Bool decodeBootstrapBody(UnsignedInt gameUid, const UnsignedByte* data, UnsignedInt length,
	CasterBootstrap::MatchStart& start)
{
	Reader r;
	CasterBootstrap::MatchStart decoded;
	UnsignedByte schemaVersion;

	CasterBootstrap::clear(decoded);
	if (gameUid == 0 || data == NULL || length == 0 || length > MAX_FRAME_BYTES)
	{
		return FALSE;
	}

	r.reset(data, length);
	decoded.gameUid = gameUid;
	schemaVersion = r.u8();
	decoded.frameCount = r.u32();
	decoded.versionNumber = r.u32();
	decoded.exeCRC = r.u32();
	decoded.iniCRC = r.u32();
	decoded.localPlayerIndex = readSigned(r);
	decoded.difficulty = readSigned(r);
	decoded.originalGameMode = readSigned(r);
	decoded.rankPoints = readSigned(r);
	decoded.maxFPS = readSigned(r);
	if (!r.ok || schemaVersion != BOOTSTRAP_SCHEMA_VERSION)
	{
		return FALSE;
	}
	if (!readBoundedString(r, decoded.versionString, sizeof(decoded.versionString),
			CasterBootstrap::MAX_VERSION_BYTES))
	{
		return FALSE;
	}
	if (!readBoundedString(r, decoded.versionTimeString, sizeof(decoded.versionTimeString),
			CasterBootstrap::MAX_VERSION_BYTES))
	{
		return FALSE;
	}
	if (!r.ok || r.pos >= r.len)
	{
		return FALSE;
	}
	if (!decodeRoomSnapshotPayload(data + r.pos, r.len - r.pos, decoded.room))
	{
		return FALSE;
	}
	if (!CasterBootstrap::isValid(decoded))
	{
		return FALSE;
	}
	start = decoded;
	return TRUE;
}

Bool decodeFrame(UnsignedByte type, const UnsignedByte* payload, UnsignedInt payloadLen, Frame& out)
{
	Reader r;
	memset(&out, 0, sizeof(out));
	out.type = type;
	r.reset(payload, payloadLen);

	switch (type)
	{
		case FRAME_HELLO:
			out.protoVer = r.u8();
			break;

		case FRAME_HELLO_ACK:
			out.protoVer = r.u8();
			out.matchRunning = r.u8();
			r.stringView(out.gameName, out.gameNameLen);
			if (out.matchRunning > 1)
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_SUBSCRIBE:
			out.gameUid = r.u32();
			out.modeOrState = r.u8();
			break;

		case FRAME_UNSUBSCRIBE:
			out.gameUid = r.u32();
			break;

		case FRAME_SUB_ACK:
			out.gameUid = r.u32();
			out.modeOrState = r.u8();
			break;

		case FRAME_COMMAND_FRAGMENT:
			out.gameUid = r.u32();
			out.simulationFrame = r.u32();
			out.totalBytes = r.u32();
			out.checksum = r.u32();
			out.fragmentOffset = r.u32();
			out.fragmentLength = r.len - r.pos;
			r.rawView(out.bytes, out.fragmentLength);
			if (out.totalBytes == 0 || out.totalBytes > MAX_COMMAND_FRAME_BYTES || out.fragmentLength == 0 || out.fragmentLength > MAX_COMMAND_FRAGMENT_BYTES || out.fragmentOffset >= out.totalBytes || out.fragmentLength > out.totalBytes - out.fragmentOffset)
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_COMMAND_HISTORY_REQ:
			out.gameUid = r.u32();
			out.firstSimulationFrame = r.u32();
			out.frameCount = r.u32();
			if (out.frameCount == 0 || out.frameCount > MAX_COMMAND_HISTORY_FRAMES || out.firstSimulationFrame > 0xFFFFFFFFu - (out.frameCount - 1))
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_COMMAND_END:
			out.gameUid = r.u32();
			out.hasFrames = r.u8();
			out.simulationFrame = r.u32();
			if (out.hasFrames > 1 || (!out.hasFrames && out.simulationFrame != 0))
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_CHAT:
			out.gameUid = r.u32();
			out.commandID = r.u32();
			out.direction = r.u8();
			out.senderSlot = r.u8();
			out.isSenderCaster = r.u8();
			out.senderTeam = r.u8();
			out.senderIdentity = r.u32();
			r.stringView(out.senderName, out.senderNameLen);
			r.stringView(out.text, out.textLen);
			out.isEmote = r.u8();
			if (out.direction > CHAT_LOBBY || out.isEmote > 1)
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_LOBBY:
			out.gameUid = r.u32();
			r.stringView(out.text, out.textLen);
			if (out.textLen > MAX_LOBBY_TEXT_BYTES)
			{
				r.ok = FALSE;
			}
			break;

		case FRAME_BOOTSTRAP:
			// The typed body stays a borrowed view; the source set decodes it.
			out.gameUid = r.u32();
			out.length = (r.len > r.pos) ? (r.len - r.pos) : 0;
			if (out.gameUid == 0 || out.length == 0)
			{
				r.ok = FALSE;
				break;
			}
			r.rawView(out.bytes, out.length);
			break;

		default:
			return FALSE;
	}

	// Every frame has an exact payload shape: reject short reads and trailing bytes.
	return (r.ok && r.pos == r.len) ? TRUE : FALSE;
}

UnsignedInt computeGameUid(UnsignedInt hostIP, UnsignedInt seed)
{
	UnsignedInt hash = 2166136261u;
	UnsignedInt words[2];
	UnsignedInt w;
	UnsignedInt i;

	// Host IP then game seed, most significant byte first, so the result is
	// byte-order independent and identical on every participant of the game.
	words[0] = hostIP;
	words[1] = seed;
	for (w = 0; w < 2; ++w)
	{
		for (i = 0; i < 4; ++i)
		{
			hash ^= (UnsignedByte)((words[w] >> (24 - i * 8)) & 0xFF);
			hash *= 16777619u;
		}
	}
	return hash;
}

UnsignedInt utf8ToWide(const char* utf8, UnsignedInt utf8Len, WideChar* out, UnsignedInt cap)
{
	UnsignedInt i;
	UnsignedInt n;
	if (utf8 == NULL || out == NULL)
	{
		return 0;
	}
	i = 0;
	n = 0;
	while (i < utf8Len)
	{
		UnsignedInt lead = (UnsignedByte)utf8[i];
		UnsignedInt extra;
		UnsignedInt cp;
		UnsignedInt k;

		if (lead < 0x80)
		{
			cp = lead;
			extra = 0;
		}
		else if ((lead & 0xE0) == 0xC0)
		{
			cp = lead & 0x1F;
			extra = 1;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			cp = lead & 0x0F;
			extra = 2;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			cp = lead & 0x07;
			extra = 3;
		}
		else
		{
			return 0;	// continuation byte or invalid lead byte
		}

		if (i + 1 + extra > utf8Len)
		{
			return 0;
		}
		k = 0;
		while (k < extra)
		{
			UnsignedInt cont = (UnsignedByte)utf8[i + 1 + k];
			if ((cont & 0xC0) != 0x80)
			{
				return 0;
			}
			cp = (cp << 6) | (cont & 0x3F);
			++k;
		}
		i += 1 + extra;

		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			return 0;
		}
		if (cp <= 0xFFFF)
		{
			if (n + 1 > cap)
			{
				return 0;
			}
			out[n++] = (WideChar)cp;
		}
		else
		{
			UnsignedInt v = cp - 0x10000;
			if (n + 2 > cap)
			{
				return 0;
			}
			out[n++] = (WideChar)(0xD800 + (v >> 10));
			out[n++] = (WideChar)(0xDC00 + (v & 0x3FF));
		}
	}
	return n;
}

UnsignedInt wideToUtf8(const WideChar* wide, UnsignedInt wideLen, char* out, UnsignedInt cap)
{
	UnsignedInt i;
	UnsignedInt n;
	if (wide == NULL || out == NULL)
	{
		return 0;
	}
	i = 0;
	n = 0;
	while (i < wideLen)
	{
		UnsignedInt cp = (UnsignedInt)(UnsignedShort)wide[i++];

		if (cp >= 0xD800 && cp <= 0xDBFF)
		{
			UnsignedInt low;
			if (i >= wideLen)
			{
				return 0;
			}
			low = (UnsignedInt)(UnsignedShort)wide[i];
			if (low < 0xDC00 || low > 0xDFFF)
			{
				return 0;
			}
			++i;
			cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
		}
		else if (cp >= 0xDC00 && cp <= 0xDFFF)
		{
			return 0;
		}

		if (cp < 0x80)
		{
			if (n + 1 > cap) { return 0; }
			out[n++] = (char)cp;
		}
		else if (cp < 0x800)
		{
			if (n + 2 > cap) { return 0; }
			out[n++] = (char)(0xC0 | (cp >> 6));
			out[n++] = (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			if (n + 3 > cap) { return 0; }
			out[n++] = (char)(0xE0 | (cp >> 12));
			out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[n++] = (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			if (n + 4 > cap) { return 0; }
			out[n++] = (char)(0xF0 | (cp >> 18));
			out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
			out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[n++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	return n;
}

}
