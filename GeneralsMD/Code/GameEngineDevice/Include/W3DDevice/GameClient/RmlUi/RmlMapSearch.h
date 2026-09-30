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

// FILE: RmlMapSearch.h //////////////////////////////////////////////////////
// The map select screens' search box and sort toggle: a live, case-insensitive name match and a
// name or player-count order over the live rows of a grow-only map list (RmlGrowOnlyList.h).
// Presentation only: nothing is sent, the map cache and the selection are untouched.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/GameText.h"

#include <RmlUi/Core/Types.h>

#include <algorithm>
#include <cctype>

inline Rml::String RmlMapSearchFold(const Rml::String &text)
{
	Rml::String folded(text);
	for (char &c : folded)
		c = (char)std::tolower((unsigned char)c);
	return folded;
}

// True when name contains needle, ignoring ASCII case; an empty needle matches everything.
inline bool RmlMapSearchMatches(const Rml::String &name, const Rml::String &needle)
{
	return needle.empty() || RmlMapSearchFold(name).find(RmlMapSearchFold(needle)) != Rml::String::npos;
}

// Orders the first liveCount rows by name, or by player count then name.
template <typename RowT>
void RmlMapSearchSort(Rml::Vector<RowT> &rows, int liveCount, bool byPlayers)
{
	std::stable_sort(rows.begin(), rows.begin() + liveCount, [byPlayers](const RowT &a, const RowT &b)
	{
		if (byPlayers && a.numPlayers != b.numPlayers)
			return a.numPlayers < b.numPlayers;
		return RmlMapSearchFold(a.displayName) < RmlMapSearchFold(b.displayName);
	});
}

// "n of N maps".
inline Rml::String RmlMapSearchCountText(int shown, int total)
{
	if (!TheGameText)
		return Rml::String();
	UnicodeString text;
	text.format(TheGameText->fetch("GUI:GOMapCount"), shown, total);
	return unicodeToUtf8(text);
}
