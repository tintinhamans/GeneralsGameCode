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

#include "GameNetwork/Caster/CasterBootstrap.h"

#include "Common/Recorder.h"
#include "GameNetwork/Caster/CasterProtocol.h"

#include <string.h>

namespace CasterBootstrap
{

// --- bounded text writer -----------------------------------------------------
// No sprintf: every append is explicitly bounded and leaves the buffer
// NUL-terminated, so a truncated build is reported rather than written.

struct TextWriter
{
	char* buf;
	UnsignedInt cap;		///< including the terminator
	UnsignedInt len;
	Bool ok;
};

static void textReset(TextWriter& w, char* buffer, UnsignedInt capacity)
{
	w.buf = buffer;
	w.cap = capacity;
	w.len = 0;
	w.ok = (buffer != NULL && capacity != 0) ? TRUE : FALSE;
	if (w.ok)
	{
		w.buf[0] = '\0';
	}
}

static void textChar(TextWriter& w, char c)
{
	if (!w.ok)
	{
		return;
	}
	if (w.len + 1 >= w.cap)
	{
		w.ok = FALSE;
		return;
	}
	w.buf[w.len++] = c;
	w.buf[w.len] = '\0';
}

static void textString(TextWriter& w, const char* text)
{
	UnsignedInt i;
	if (!w.ok || text == NULL)
	{
		return;
	}
	for (i = 0; text[i] != '\0'; ++i)
	{
		textChar(w, text[i]);
		if (!w.ok)
		{
			return;
		}
	}
}

static void textUnsigned(TextWriter& w, UnsignedInt value)
{
	char digits[12];
	UnsignedInt count = 0;

	if (!w.ok)
	{
		return;
	}
	do
	{
		digits[count++] = (char)('0' + (value % 10));
		value /= 10;
	}
	while (value != 0 && count < sizeof(digits));

	while (count > 0)
	{
		textChar(w, digits[--count]);
	}
}

static void textSigned(TextWriter& w, Int value)
{
	UnsignedInt magnitude;

	if (!w.ok)
	{
		return;
	}
	if (value < 0)
	{
		textChar(w, '-');
		// Negating INT_MIN is undefined; take the magnitude unsigned instead.
		magnitude = (UnsignedInt)(0u - (UnsignedInt)value);
	}
	else
	{
		magnitude = (UnsignedInt)value;
	}
	textUnsigned(w, magnitude);
}

/// Uppercase hex without leading zeros, matching "%X".
static void textHexUpper(TextWriter& w, UnsignedInt value)
{
	static const char DIGITS[17] = "0123456789ABCDEF";
	char out[8];
	UnsignedInt count = 0;

	if (!w.ok)
	{
		return;
	}
	do
	{
		out[count++] = DIGITS[value & 0xF];
		value >>= 4;
	}
	while (value != 0 && count < sizeof(out));

	while (count > 0)
	{
		textChar(w, out[--count]);
	}
}

/// Exactly two lowercase hex digits, matching "%2.2x" for a byte-wide mask.
static void textHexByte(TextWriter& w, UnsignedInt value)
{
	static const char DIGITS[17] = "0123456789abcdef";

	textChar(w, DIGITS[(value >> 4) & 0xF]);
	textChar(w, DIGITS[value & 0xF]);
}

// --- public surface ----------------------------------------------------------

void clear(MatchStart& start)
{
	memset(&start, 0, sizeof(start));
	start.localPlayerIndex = -1;
}

Bool isValid(const MatchStart& start)
{
	if (start.gameUid == 0 || start.room.gameUid != start.gameUid)
	{
		return FALSE;
	}
	if (start.room.slotCount == 0 || start.room.slotCount > CasterLobby::LOBBY_MAX_SLOTS)
	{
		return FALSE;
	}
	if (start.room.mapName[0] == '\0')
	{
		return FALSE;
	}
	// The recorder reads the local slot unconditionally, so a live bootstrap
	// must name a real slot inside the published table.
	if (start.localPlayerIndex < 0 || (UnsignedInt)start.localPlayerIndex >= start.room.slotCount)
	{
		return FALSE;
	}
	if (start.room.slots[start.localPlayerIndex].kind != CasterLobby::LOBBY_SLOT_HUMAN)
	{
		return FALSE;
	}
	return TRUE;
}

static void appendSlot(TextWriter& w, const CasterLobby::LobbySlot& slot)
{
	switch (slot.kind)
	{
	case CasterLobby::LOBBY_SLOT_HUMAN:
		textChar(w, 'H');
		textString(w, slot.name);
		textChar(w, ',');
		textHexUpper(w, slot.ip);
		textChar(w, ',');
		textUnsigned(w, slot.port);
		textChar(w, ',');
		textChar(w, slot.accepted ? 'T' : 'F');
		textChar(w, slot.hasMap ? 'T' : 'F');
		textChar(w, ',');
		textSigned(w, slot.color);
		textChar(w, ',');
		textSigned(w, slot.playerTemplate);
		textChar(w, ',');
		textSigned(w, slot.startPos);
		textChar(w, ',');
		textSigned(w, slot.team);
		textChar(w, ',');
		textSigned(w, slot.nat);
		textChar(w, ':');
		break;

	case CasterLobby::LOBBY_SLOT_AI:
		textChar(w, 'C');
		// Only 'E', 'M' and 'H' are accepted by the parser; anything else would
		// abort the caster's playback, so fall back to the medium AI.
		textChar(w, (slot.aiDifficulty == 'E' || slot.aiDifficulty == 'M'
			|| slot.aiDifficulty == 'H') ? slot.aiDifficulty : 'M');
		textChar(w, ',');
		textSigned(w, slot.color);
		textChar(w, ',');
		textSigned(w, slot.playerTemplate);
		textChar(w, ',');
		textSigned(w, slot.startPos);
		textChar(w, ',');
		textSigned(w, slot.team);
		textChar(w, ':');
		break;

	case CasterLobby::LOBBY_SLOT_OPEN:
		textString(w, "O:");
		break;

	case CasterLobby::LOBBY_SLOT_CLOSED:
	default:
		// The recorder writes a closed slot for anything it cannot describe.
		textString(w, "X:");
		break;
	}
}

UnsignedInt buildGameOptions(const CasterLobby::RoomSnapshot& room, char* out, UnsignedInt cap)
{
	TextWriter w;
	UnsignedInt i;

	textReset(w, out, cap);
	if (!w.ok)
	{
		return 0;
	}
	if (room.slotCount > CasterLobby::LOBBY_MAX_SLOTS)
	{
		return 0;
	}

#if RTS_GENERALS
	textString(w, "M=");
	textHexByte(w, (UnsignedInt)room.mapContentsMask & 0xFF);
	textString(w, room.mapName);
	textString(w, ";MC=");
	textHexUpper(w, room.mapCRC);
	textString(w, ";MS=");
	textSigned(w, room.mapSize);
	textString(w, ";SD=");
	textSigned(w, room.seed);
	textString(w, ";C=");
	textSigned(w, room.crcInterval);
	textChar(w, ';');
#else
	textString(w, "US=");
	textSigned(w, room.useStats);
	textString(w, ";M=");
	textHexByte(w, (UnsignedInt)room.mapContentsMask & 0xFF);
	textString(w, room.mapName);
	textString(w, ";MC=");
	textHexUpper(w, room.mapCRC);
	textString(w, ";MS=");
	textSigned(w, room.mapSize);
	textString(w, ";SD=");
	textSigned(w, room.seed);
	textString(w, ";C=");
	textSigned(w, room.crcInterval);
	textString(w, ";SR=");
	textUnsigned(w, room.superweaponRestriction);
	textString(w, ";SC=");
	textUnsigned(w, room.startingCash);
	textString(w, ";O=");
	textChar(w, room.oldFactionsOnly ? 'Y' : 'N');
	textChar(w, ';');
#endif

	textString(w, "S=");
	for (i = 0; i < REPLAY_SLOTS; ++i)
	{
		if (i < room.slotCount)
		{
			appendSlot(w, room.slots[i]);
		}
		else
		{
			// The recorder always describes every slot; pad a short table.
			textString(w, "X:");
		}
	}
	textChar(w, ';');

	return w.ok ? w.len : 0;
}

/// One carried UTF-8 string as the engine's unicode string.
static void setUnicode(UnicodeString& out, const char* utf8)
{
	WideChar wide[MAX_VERSION_BYTES + 1];
	UnsignedInt utf8Len;
	UnsignedInt count;

	out.clear();
	utf8Len = (utf8 != NULL) ? (UnsignedInt)strlen(utf8) : 0;
	if (utf8Len == 0)
	{
		return;
	}
	count = CasterProtocol::utf8ToWide(utf8, utf8Len, wide, sizeof(wide) / sizeof(wide[0]) - 1);
	if (count == 0 || count >= sizeof(wide) / sizeof(wide[0]))
	{
		return;
	}
	wide[count] = 0;
	out.set(wide);
}

Bool fillReplayStart(const MatchStart& start, ReplayStartData& out)
{
	char options[MAX_OPTIONS_BYTES];
	Int i;

	if (!isValid(start))
	{
		return FALSE;
	}
	if (buildGameOptions(start.room, options, sizeof(options)) == 0)
	{
		return FALSE;
	}

	// The same fields RecorderClass::readReplayHeader would have parsed, minus the
	// ones no reader consumes (replay name, timestamps, per-slot disconnect flags).
	out.header.filename = "LiveCaster";
	out.header.replayName.clear();
	memset(&out.header.timeVal, 0, sizeof(out.header.timeVal));
	setUnicode(out.header.versionString, start.versionString);
	setUnicode(out.header.versionTimeString, start.versionTimeString);
	out.header.versionNumber = start.versionNumber;
	out.header.exeCRC = start.exeCRC;
	out.header.iniCRC = start.iniCRC;
	out.header.startTime = 0;
	out.header.endTime = 0;
	out.header.frameCount = start.frameCount;
	out.header.quitEarly = FALSE;
	out.header.desyncGame = FALSE;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		out.header.playerDiscons[i] = FALSE;
	}
	out.header.gameOptions = options;
	out.header.localPlayerIndex = start.localPlayerIndex;

	out.playbackFilename = "LiveCaster";
	out.difficulty = start.difficulty;
	out.originalGameMode = start.originalGameMode;
	out.rankPoints = start.rankPoints;
	out.maxFPS = start.maxFPS;
	out.liveCast = TRUE;
	return TRUE;
}

}
