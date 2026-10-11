/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#include "GameNetwork/LobbyOptions.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
	const int SLOT_COUNT = 8;	// MAX_SLOTS
	const int MAX_TEXT = 512;

	enum SlotKind { SLOT_KIND_OPEN, SLOT_KIND_CLOSED, SLOT_KIND_HUMAN, SLOT_KIND_EASY, SLOT_KIND_MEDIUM, SLOT_KIND_BRUTAL };

	bool eat(const char *&p, const char *literal)
	{
		const size_t length = strlen(literal);
		if (strncmp(p, literal, length) != 0)
			return false;

		p += length;
		return true;
	}

	bool readInt(const char *&p, long *value)
	{
		char *end = 0;
		*value = strtol(p, &end, 10);
		if (end == p)
			return false;

		p = end;
		return true;
	}

	bool readHex(const char *&p, unsigned long *value)
	{
		char *end = 0;
		*value = strtoul(p, &end, 16);
		if (end == p)
			return false;

		p = end;
		return true;
	}

	bool readUnsigned(const char *&p, unsigned long *value)
	{
		char *end = 0;
		*value = strtoul(p, &end, 10);
		if (end == p)
			return false;

		p = end;
		return true;
	}

	bool readFlag(const char *&p, bool *value)
	{
		if (*p != 'T' && *p != 'F')
			return false;

		*value = *p == 'T';
		++p;
		return true;
	}

	bool readSlot(const char *&p, netproto_LanSlot *slot)
	{
		long number = 0;
		unsigned long unsignedNumber = 0;
		memset(slot, 0, sizeof(*slot));

		switch (*p)
		{
		case 'O':
			++p;
			slot->kind = SLOT_KIND_OPEN;
			return eat(p, ":");
		case 'X':
			++p;
			slot->kind = SLOT_KIND_CLOSED;
			return eat(p, ":");
		case 'H':
		{
			++p;
			slot->kind = SLOT_KIND_HUMAN;
			size_t length = 0;
			while (p[length] != ',' && p[length] != ':' && p[length] != ';' && p[length] != '\0')
				++length;
			if (p[length] != ',' || length >= sizeof(slot->name))
				return false;

			memcpy(slot->name, p, length);
			slot->name[length] = '\0';
			p += length;
			if (!eat(p, ",") || !readHex(p, &unsignedNumber))
				return false;

			slot->ip = (uint32_t)unsignedNumber;
			if (!eat(p, ",") || !readUnsigned(p, &unsignedNumber))
				return false;

			slot->port = (uint32_t)unsignedNumber;
			if (!eat(p, ",") || !readFlag(p, &slot->accepted) || !readFlag(p, &slot->has_map))
				return false;
			break;
		}
		case 'C':
			++p;
			if (*p == 'E')
				slot->kind = SLOT_KIND_EASY;
			else if (*p == 'M')
				slot->kind = SLOT_KIND_MEDIUM;
			else if (*p == 'H')
				slot->kind = SLOT_KIND_BRUTAL;
			else
				return false;
			++p;
			break;
		default:
			return false;
		}

		if (!eat(p, ",") || !readInt(p, &number))
			return false;
		slot->color = (int32_t)number;
		if (!eat(p, ",") || !readInt(p, &number))
			return false;
		slot->player_template = (int32_t)number;
		if (!eat(p, ",") || !readInt(p, &number))
			return false;
		slot->start_pos = (int32_t)number;
		if (!eat(p, ",") || !readInt(p, &number))
			return false;
		slot->team = (int32_t)number;
		if (slot->kind == SLOT_KIND_HUMAN)
		{
			if (!eat(p, ",") || !readInt(p, &number))
				return false;
			slot->nat = (int32_t)number;
		}
		return eat(p, ":");
	}

	struct Writer
	{
		Writer(char *buffer, int size) : buffer(buffer), size(size), length(0), ok(true) {}

		void put(const char *text)
		{
			const int n = (int)strlen(text);
			if (length + n >= size)
			{
				ok = false;
				return;
			}
			memcpy(buffer + length, text, n + 1);
			length += n;
		}

		char *buffer;
		int size;
		int length;
		bool ok;
	};
}

namespace LobbyOptions
{

bool pack(const char *text, netproto_LanOptions *out)
{
	memset(out, 0, sizeof(*out));
	const char *p = text;
	long number = 0;
	unsigned long unsignedNumber = 0;

	if (eat(p, "US="))
	{
		if (!readInt(p, &number) || !eat(p, ";"))
			return false;
		out->has_stats = true;
		out->use_stats = (int32_t)number;
	}

	if (!eat(p, "M=") || !isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]))
		return false;
	{
		char digits[3] = { p[0], p[1], '\0' };
		out->map_mask = (uint32_t)strtoul(digits, 0, 16);
		p += 2;
	}
	{
		size_t length = 0;
		while (p[length] != ';' && p[length] != '\0')
			++length;
		if (p[length] != ';' || length >= sizeof(out->map_path))
			return false;

		memcpy(out->map_path, p, length);
		out->map_path[length] = '\0';
		p += length + 1;
	}

	if (!eat(p, "MC=") || !readHex(p, &unsignedNumber) || !eat(p, ";"))
		return false;
	out->map_crc = (uint32_t)unsignedNumber;
	if (!eat(p, "MS=") || !readInt(p, &number) || !eat(p, ";"))
		return false;
	out->map_size = (int32_t)number;
	if (!eat(p, "SD=") || !readInt(p, &number) || !eat(p, ";"))
		return false;
	out->seed = (int32_t)number;
	if (!eat(p, "C=") || !readInt(p, &number) || !eat(p, ";"))
		return false;
	out->crc_interval = (int32_t)number;

	if (eat(p, "SR="))
	{
		if (!readUnsigned(p, &unsignedNumber) || !eat(p, ";"))
			return false;
		out->has_extras = true;
		out->superweapon_restriction = (uint32_t)unsignedNumber;
		if (!eat(p, "SC=") || !readUnsigned(p, &unsignedNumber) || !eat(p, ";"))
			return false;
		out->starting_cash = (uint32_t)unsignedNumber;
		if (!eat(p, "O=") || (*p != 'Y' && *p != 'N'))
			return false;
		out->old_factions = *p == 'Y';
		++p;
		if (!eat(p, ";"))
			return false;
	}

	if (!eat(p, "S="))
		return false;
	for (int i = 0; i < SLOT_COUNT; ++i)
	{
		if (!readSlot(p, &out->slots[i]))
			return false;
	}
	out->slots_count = SLOT_COUNT;
	if (!eat(p, ";") || *p != '\0')
		return false;

	char again[MAX_TEXT];
	return unpack(*out, again, sizeof(again)) && strcmp(again, text) == 0;
}

bool unpack(const netproto_LanOptions &options, char *buffer, int size)
{
	Writer w(buffer, size);
	char tmp[160];
	w.put("");

	if (options.has_stats)
	{
		sprintf(tmp, "US=%d;", (int)options.use_stats);
		w.put(tmp);
	}
	w.put("M=");
	sprintf(tmp, "%2.2x", (unsigned)options.map_mask);
	w.put(tmp);
	w.put(options.map_path);
	sprintf(tmp, ";MC=%X;MS=%d;SD=%d;C=%d;", (unsigned)options.map_crc, (int)options.map_size, (int)options.seed, (int)options.crc_interval);
	w.put(tmp);
	if (options.has_extras)
	{
		sprintf(tmp, "SR=%u;SC=%u;O=%c;", (unsigned)options.superweapon_restriction, (unsigned)options.starting_cash, options.old_factions ? 'Y' : 'N');
		w.put(tmp);
	}

	w.put("S=");
	for (int i = 0; i < (int)options.slots_count; ++i)
	{
		const netproto_LanSlot &slot = options.slots[i];
		switch (slot.kind)
		{
		case SLOT_KIND_HUMAN:
			w.put("H");
			w.put(slot.name);
			sprintf(tmp, ",%X,%d,%c%c,%d,%d,%d,%d,%d:", (unsigned)slot.ip, (int)slot.port, slot.accepted ? 'T' : 'F', slot.has_map ? 'T' : 'F',
				(int)slot.color, (int)slot.player_template, (int)slot.start_pos, (int)slot.team, (int)slot.nat);
			w.put(tmp);
			break;
		case SLOT_KIND_EASY:
		case SLOT_KIND_MEDIUM:
		case SLOT_KIND_BRUTAL:
			sprintf(tmp, "C%c,%d,%d,%d,%d:", slot.kind == SLOT_KIND_EASY ? 'E' : (slot.kind == SLOT_KIND_MEDIUM ? 'M' : 'H'),
				(int)slot.color, (int)slot.player_template, (int)slot.start_pos, (int)slot.team);
			w.put(tmp);
			break;
		case SLOT_KIND_OPEN:
			w.put("O:");
			break;
		default:
			w.put("X:");
			break;
		}
	}
	w.put(";");
	return w.ok;
}

}
