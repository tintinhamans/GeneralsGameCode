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

// FILE: LoadScreenData.h /////////////////////////////////////////////////////
// Widget-agnostic content of the network load screens (map transfer so far). The LoadScreen classes fill this instead of GameWindows when RmlUi
// draws the screen; the RmlUi load screen reads it back every frame.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameNetwork/GameInfo.h"

struct LoadScreenPlayerRow
{
	UnicodeString m_name;
	UnicodeString m_status; ///< map transfer state text
	UnsignedInt m_color = 0xFFFFFF; ///< 0x00RRGGBB
	Int m_progress = 0; ///< 0..100
	Bool m_showProgress = TRUE;
};

struct LoadScreenData
{
	static LoadScreenData &instance()
	{
		static LoadScreenData s_data;
		return s_data;
	}

	void reset()
	{
		UnsignedInt version = m_version;
		*this = LoadScreenData();
		m_version = version + 1;
	}
	void touch() { ++m_version; } ///< bump after any change so the view refreshes

	Int m_rowCount = 0;
	LoadScreenPlayerRow m_rows[MAX_SLOTS];

	UnicodeString m_currentFile; ///< map transfer only
	UnicodeString m_timeout;

	UnsignedInt m_version = 0;
};
