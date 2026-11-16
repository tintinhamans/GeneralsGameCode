/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding changes, one switch each (GameLogic/PathTuning.h).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/PathTuning.h"

#include <stdio.h>
#include <string.h>

namespace PathTuning
{
	Bool CoarseFix = TRUE;
	Bool CoarseFootprint = TRUE;
	Bool Octile = TRUE;

	// bit: the change's bit in Mask(), as the measured builds numbered it, so the CRC words are theirs
	struct Name { const char * name; Bool * flag; Int bit; };
	static const Name Names[] =
	{
		{ "coarse", &CoarseFix, 0 },
		{ "crossings", &CoarseFootprint, 1 },
		{ "octile", &Octile, 3 },
		{ nullptr, nullptr, 0 },	// the end (so the table is never empty)
	};
	static const Int NAME_COUNT = sizeof(Names) / sizeof(Names[0]) - 1;

	static void Set_All(Bool on)
	{
		for (Int i = 0; i < NAME_COUNT; ++i)
			*Names[i].flag = on;
	}

	Bool Set(const char * list)
	{
		Bool known = TRUE;
		char word[32];
		const char * p = list;
		while (p != nullptr && *p != 0)
		{
			Int n = 0;
			while (*p != 0 && *p != ',' && n < (Int)sizeof(word) - 1)
				word[n++] = *p++;
			word[n] = 0;
			if (*p == ',')
				++p;
			if (n == 0)
				continue;
			if (_stricmp(word, "none") == 0)
			{
				Set_All(FALSE);
				continue;
			}
			if (_stricmp(word, "default") == 0)
			{
				Set_All(TRUE);
				continue;
			}
			const Bool on = word[0] != '-';
			const char * name = on ? word : word + 1;
			Bool found = FALSE;
			for (Int i = 0; i < NAME_COUNT; ++i)
				if (_stricmp(name, Names[i].name) == 0)
				{
					*Names[i].flag = on;
					found = TRUE;
				}
			if (!found)
			{
				printf("-pathFix: unknown change \"%s\"\n", name);
				known = FALSE;
			}
		}
		return known;
	}

	Bool Any()
	{
		for (Int i = 0; i < NAME_COUNT; ++i)
			if (*Names[i].flag)
				return TRUE;
		return FALSE;
	}

	UnsignedInt Mask()
	{
		UnsignedInt mask = 0;
		for (Int i = 0; i < NAME_COUNT; ++i)
			if (*Names[i].flag)
				mask |= 1u << Names[i].bit;
		return mask;
	}

	void Describe(char * out, Int size)
	{
		if (size <= 0)
			return;
		out[0] = 0;
		Int used = 0;
		for (Int i = 0; i < NAME_COUNT; ++i)
			if (*Names[i].flag)
				used += snprintf(out + used, used < size ? size - used : 0, "%s%s", used > 0 ? "," : "", Names[i].name);
		if (used == 0)
			snprintf(out, size, "none");
	}
}
