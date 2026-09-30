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

// FILE: RmlHqStatus.h ///////////////////////////////////////////////////////
// The .hq-header strip's status (common.rcss): the online pill and the clock. A screen binds it into
// its own data model once, refreshes it on show, and ticks it from update().
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeUtf8.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeData.h"
#include "GameClient/GameText.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <ctime>

class RmlHqStatus
{
public:
	// logged_in, online_count_text, player_name and clock_text.
	void bind(Rml::DataModelConstructor &constructor)
	{
		constructor.Bind("logged_in", &m_loggedIn);
		constructor.Bind("online_count_text", &m_onlineCountText);
		constructor.Bind("player_name", &m_playerName);
		constructor.Bind("clock_text", &m_clockText);
		m_lastTick = 0;
	}

	// At most once a second; force on show.
	void refresh(Rml::DataModelHandle &handle, bool force = false)
	{
		const std::time_t now = std::time(nullptr);
		if (!force && now == m_lastTick)
			return;
		m_lastTick = now;

		const bool loggedIn = OnlineWelcomeData::isLoggedIn();
		Rml::String name, count;
		if (loggedIn)
		{
			name = unicodeToUtf8(OnlineWelcomeData::localDisplayName());
			const Int playersOnline = OnlineWelcomeData::currentNumPlayersOnline();
			if (playersOnline > 0 && TheGameText)
			{
				UnicodeString text;
				text.format(TheGameText->fetch("GO:GUI:MenuPlayersOnline"), playersOnline);
				count = unicodeToUtf8(text);
			}
		}

		std::tm local = {};
		localtime_s(&local, &now);
		const char clock[] = { char('0' + local.tm_hour / 10), char('0' + local.tm_hour % 10), ':',
			char('0' + local.tm_min / 10), char('0' + local.tm_min % 10), '\0' };

		set(handle, "logged_in", m_loggedIn, loggedIn, force);
		set(handle, "player_name", m_playerName, name, force);
		set(handle, "online_count_text", m_onlineCountText, count, force);
		set(handle, "clock_text", m_clockText, Rml::String(clock), force);
	}

private:
	template <typename T>
	static void set(Rml::DataModelHandle &handle, const char *name, T &field, const T &value, bool force)
	{
		if (field == value && !force)
			return;
		field = value;
		if (handle)
			handle.DirtyVariable(name);
	}

	bool m_loggedIn = false;
	Rml::String m_onlineCountText;
	Rml::String m_playerName;
	Rml::String m_clockText;
	std::time_t m_lastTick = 0;
};
