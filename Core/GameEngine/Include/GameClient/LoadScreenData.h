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
// Widget-agnostic content of the network and shell load screens (map transfer, multiplayer,
// online, shell). The LoadScreen classes fill this instead of GameWindows when RmlUi
// draws the screen; the RmlUi load screen reads it back every frame.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "GameNetwork/GameInfo.h"

struct LoadScreenPlayerRow
{
	UnicodeString m_name;
	UnicodeString m_side;
	UnicodeString m_team;
	UnicodeString m_winLoss;
	UnicodeString m_disconnects;
	UnicodeString m_status; ///< map transfer state text
	AsciiString m_rankImage; ///< mapped image names, empty for none
	AsciiString m_medalImage;
	UnsignedInt m_color = 0xFFFFFF; ///< 0x00RRGGBB
	Int m_colorIndex = 0; ///< slot's apparent color index, for the legacy house-colored bar
	Int m_progress = 0; ///< 0..100
	Bool m_showProgress = TRUE;
	Bool m_showStats = TRUE; ///< FALSE for AI: no rank, win/loss or disconnects
};

// Start position marker over the map preview, laid out as fractions of the preview.
struct LoadScreenStartMarker
{
	Real m_x = 0.0f;
	Real m_y = 0.0f;
	Int m_slotNumber = 0; ///< 1-based number of the slot starting here
	UnsignedInt m_color = 0xFFFFFF;
	Bool m_used = FALSE;
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

	UnicodeString m_localName; ///< local player's general or faction
	UnicodeString m_localFeatures;
	AsciiString m_localPortrait; ///< mapped image name

	AsciiString m_backgroundImage; ///< mapped image behind the screen, empty for none (original Generals)

	AsciiString m_mapName;
	LoadScreenStartMarker m_markers[MAX_SLOTS];

	UnicodeString m_currentFile; ///< map transfer only
	UnicodeString m_timeout;

	Int m_progress = 0; ///< shell load bar, 0..100
	Bool m_titleScreen = FALSE; ///< first shell load: title art and copyright line

	UnsignedInt m_version = 0;
};
