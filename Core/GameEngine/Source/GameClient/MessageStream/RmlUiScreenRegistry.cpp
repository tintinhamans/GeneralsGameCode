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
#include "GameClient/RmlUiScreenRegistry.h"

#include "Common/AsciiString.h"

#include <vector>

namespace
{
	struct Entry
	{
		AsciiString wndPath;
		RmlUiScreenFunc open;
		RmlUiScreenFunc close;
	};

	// Small and linearly scanned: at most a few dozen screens, looked up on menu navigation only.
	std::vector<Entry> &entries()
	{
		static std::vector<Entry> s_entries;
		return s_entries;
	}

	Entry *find(const AsciiString &wndPath)
	{
		std::vector<Entry> &e = entries();
		for (size_t i = 0; i < e.size(); ++i)
			if (e[i].wndPath == wndPath)
				return &e[i];
		return nullptr;
	}
}

void RmlUiScreenRegistry::registerScreen(const char *wndPath, RmlUiScreenFunc open, RmlUiScreenFunc close)
{
	AsciiString path(wndPath);
	Entry *existing = find(path);
	if (existing)
	{
		existing->open = open;
		existing->close = close;
		return;
	}
	Entry e;
	e.wndPath = path;
	e.open = open;
	e.close = close;
	entries().push_back(e);
}

void RmlUiScreenRegistry::unregisterScreen(const char *wndPath)
{
	AsciiString path(wndPath);
	std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
	{
		if (e[i].wndPath == path)
		{
			e.erase(e.begin() + i);
			return;
		}
	}
}

void RmlUiScreenRegistry::unregisterAll()
{
	entries().clear();
}

bool RmlUiScreenRegistry::isRegistered(const AsciiString &wndPath)
{
	return find(wndPath) != nullptr;
}

bool RmlUiScreenRegistry::open(const AsciiString &wndPath)
{
	Entry *e = find(wndPath);
	if (!e || !e->open)
		return false;
	e->open();
	return true;
}

bool RmlUiScreenRegistry::close(const AsciiString &wndPath)
{
	Entry *e = find(wndPath);
	if (!e || !e->close)
		return false;
	e->close();
	return true;
}
