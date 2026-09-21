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
	struct RoomSnapshot;
}

namespace CasterBootstrap
{
	struct MatchStart;
}

namespace CasterProtocol
{

// Frame: u32 byte length | u8 type | payload. Integers are little-endian; strings are u16 length + UTF-8.
enum FrameType
{
	FRAME_HELLO       = 0x01,	///< caster -> player: u8 protoVer
	FRAME_HELLO_ACK   = 0x02,	///< player -> caster: u8 protoVer, u8 matchRunning, string gameName
	FRAME_SUBSCRIBE   = 0x03,	///< caster -> player: u32 gameUid, u8 mode (0 = live)
	FRAME_UNSUBSCRIBE = 0x04,	///< caster -> player: u32 gameUid
	FRAME_SUB_ACK     = 0x05,	///< player -> caster: u32 gameUid, u8 state (0 = eligible, 1 = none)
	FRAME_CHAT        = 0x0A,	///< both: game/command/direction/slot/caster/team, u32 senderIdentity, sender name, UTF-8 text, u8 isEmote
	FRAME_LOBBY       = 0x0B,	///< player -> caster: u32 gameUid, u16 textLen, UTF-8 lobby-state text
	FRAME_COMMAND_FRAGMENT = 0x0D,	///< player -> caster: u32 gameUid, u32 simulationFrame, u32 totalBytes, u32 checksum, u32 fragmentOffset, bytes
	FRAME_COMMAND_HISTORY_REQ = 0x0E,	///< caster -> player: u32 gameUid, u32 firstSimulationFrame, u32 frameCount
	FRAME_COMMAND_END = 0x0F,	///< player -> caster: u32 gameUid, u8 hasFrames, u32 finalSimulationFrame
	FRAME_BOOTSTRAP   = 0x10,	///< player -> caster: u32 gameUid, typed match-start bootstrap body
};

enum ChatDirection
{
	CHAT_EVERYONE = 0,
	CHAT_ALLIES   = 1,
	CHAT_LOBBY    = 2,
};

static const UnsignedByte PROTOCOL_VERSION    = 9;	///< also carried by the caster-details reply (CasterBeacon)
static const UnsignedInt  FRAME_HEADER_BYTES  = 5;		///< u32 length + u8 type
static const UnsignedInt  MAX_FRAME_BYTES     = 1024;	///< hard bound on a single TCP message
static const UnsignedInt  MAX_LOBBY_TEXT_BYTES = 512;	///< bound on a FRAME_LOBBY lobby-state blob (max options length is 400)
static const UnsignedInt  MAX_COMMAND_FRAME_BYTES = 64 * 1024;	///< maximum complete native command frame
static const UnsignedInt  MAX_COMMAND_FRAGMENT_BYTES = 900;	///< payload bytes carried by one command fragment
static const UnsignedInt  MAX_COMMAND_HISTORY_FRAMES = 64;	///< maximum frames requested in one history request
static const UnsignedInt  BEACON_MESSAGE_TYPE = 17;		///< LANMessage::Type value, spelled out explicitly
static const UnsignedInt  BEACON_TOKEN_CHARS  = 12;		///< usable wide chars in LANMessage::name[13]
static const UnsignedByte ROOM_SNAPSHOT_SCHEMA_VERSION = 1;
static const UnsignedByte BOOTSTRAP_SCHEMA_VERSION = 1;

// Payload views borrow the caller's frame buffer.
struct Frame
{
	UnsignedByte type;
	UnsignedByte protoVer;			///< HELLO / HELLO_ACK
	UnsignedInt gameUid;
	UnsignedByte matchRunning;		///< HELLO_ACK: the announced match is under way
	const char* gameName;			///< HELLO_ACK
	UnsignedInt gameNameLen;
	UnsignedByte modeOrState;		///< SUBSCRIBE mode / SUB_ACK state
	UnsignedInt length;				///< BOOTSTRAP body byte count
	const UnsignedByte* bytes;		///< BOOTSTRAP body / COMMAND_FRAGMENT payload
	UnsignedInt simulationFrame;	///< COMMAND_FRAGMENT / COMMAND_END final frame
	UnsignedInt totalBytes;		///< COMMAND_FRAGMENT complete command-frame length
	UnsignedInt checksum;			///< COMMAND_FRAGMENT complete command-frame checksum
	UnsignedInt fragmentOffset;	///< COMMAND_FRAGMENT byte position within the command frame
	UnsignedInt fragmentLength;	///< COMMAND_FRAGMENT byte count
	UnsignedInt firstSimulationFrame;	///< COMMAND_HISTORY_REQ first requested frame
	UnsignedInt frameCount;		///< COMMAND_HISTORY_REQ requested frame count
	UnsignedByte hasFrames;		///< COMMAND_END: finalSimulationFrame is valid
	UnsignedInt commandID;
	UnsignedByte direction;			///< CHAT
	UnsignedByte senderSlot;		///< CHAT
	UnsignedByte isSenderCaster;	///< CHAT
	UnsignedByte senderTeam;		///< CHAT (0 = unknown)
	UnsignedInt senderIdentity;	///< CHAT: player slot, or stable caster-session identity
	const char* senderName;		///< CHAT sender name (UTF-8)
	UnsignedInt senderNameLen;
	const char* text;				///< CHAT (UTF-8) / LOBBY (lobby-state blob)
	UnsignedInt textLen;
	UnsignedByte isEmote;			///< CHAT: TRUE for a stock "/me " emote line
};

struct Writer
{
	UnsignedByte* buf;
	UnsignedInt cap;
	UnsignedInt len;
	Bool ok;

	void reset(UnsignedByte* buffer, UnsignedInt capacity);
	void u8(UnsignedByte value);
	void u16(UnsignedShort value);
	void u32(UnsignedInt value);
	void raw(const void* data, UnsignedInt size);
};

/** Bounds-checked little-endian reader, symmetric to Writer. */
struct Reader
{
	const UnsignedByte* buf;
	UnsignedInt len;
	UnsignedInt pos;
	Bool ok;

	void reset(const UnsignedByte* buffer, UnsignedInt size);
	UnsignedByte u8();
	UnsignedShort u16();
	UnsignedInt u32();
	Bool rawView(const UnsignedByte*& view, UnsignedInt size);
	Bool stringView(const char*& view, UnsignedInt& size);
};

// Encoders. Each returns the total frame size in bytes, or 0 if the frame does
// not fit `cap` or the input is out of range.
UnsignedInt encodeHello(UnsignedByte* out, UnsignedInt cap);
UnsignedInt encodeHelloAck(UnsignedByte* out, UnsignedInt cap, Bool matchRunning, const char* gameName);
/// Always requests the live mode (wire byte 0); no other mode is defined.
UnsignedInt encodeSubscribe(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid);
UnsignedInt encodeSubAck(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedByte state);
UnsignedInt encodeCommandFragment(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt simulationFrame, UnsignedInt totalBytes, UnsignedInt checksum, UnsignedInt fragmentOffset, const UnsignedByte* bytes, UnsignedInt fragmentLength);
UnsignedInt encodeCommandHistoryReq(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt firstSimulationFrame, UnsignedInt frameCount);
UnsignedInt encodeCommandEnd(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, Bool hasFrames, UnsignedInt finalSimulationFrame);
UnsignedInt encodeChat(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction, UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam, UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len, Bool isEmote);

/// Encodes one LOBBY frame: `u32 gameUid | u16 textLen | text bytes` (the
/// serialized lobby-state blob). 0 when it does not fit `cap` or `textLen`
/// exceeds MAX_LOBBY_TEXT_BYTES.
UnsignedInt encodeLobby(UnsignedByte* out, UnsignedInt cap, UnsignedInt gameUid, const char* text, UnsignedInt textLen);

UnsignedInt encodeRoomSnapshotPayload(UnsignedByte* out, UnsignedInt cap,
	const CasterLobby::RoomSnapshot& snapshot);
Bool decodeRoomSnapshotPayload(const UnsignedByte* data, UnsignedInt length,
	CasterLobby::RoomSnapshot& snapshot);

/// Encodes one BOOTSTRAP frame: `u32 gameUid | typed match-start body`. 0 when
/// the bootstrap is invalid or the frame does not fit `cap`.
UnsignedInt encodeBootstrap(UnsignedByte* out, UnsignedInt cap,
	const CasterBootstrap::MatchStart& start);
/// Decodes one BOOTSTRAP body (the `bytes`/`length` view of a decoded frame).
Bool decodeBootstrapBody(UnsignedInt gameUid, const UnsignedByte* data, UnsignedInt length,
	CasterBootstrap::MatchStart& start);

/// Decodes one already-complete frame payload. FALSE on malformed input.
Bool decodeFrame(UnsignedByte type, const UnsignedByte* payload, UnsignedInt payloadLen, Frame& out);

UnsignedInt computeGameUid(UnsignedInt hostIP, UnsignedInt seed);

UnsignedInt utf8ToWide(const char* utf8, UnsignedInt utf8Len, WideChar* out, UnsignedInt cap);
UnsignedInt wideToUtf8(const WideChar* wide, UnsignedInt wideLen, char* out, UnsignedInt cap);

}
