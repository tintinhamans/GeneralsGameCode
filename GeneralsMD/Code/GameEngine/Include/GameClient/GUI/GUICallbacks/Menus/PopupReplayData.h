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

// FILE: PopupReplayData.h ///////////////////////////////////////////////////
// Widget-agnostic state of the save replay popup (PopupReplay.wnd and its RmlUi replacement): the
// list of existing replays, the replay name being typed and the "Replay Saved" notice.
// PopupReplayActions changes it; the screens only draw it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/ReplayListData.h"

#include <vector>

struct PopupReplayData
{
	static const Int MAX_NAME_LENGTH = 64; ///< the .wnd entry field's MAXLEN
	static const UnsignedInt SAVED_POPUP_DURATION = 1000; ///< milliseconds the "Replay Saved" notice stays up

	static PopupReplayData &instance()
	{
		static PopupReplayData s_data;
		return s_data;
	}

	void open(); ///< scan the replay directory, empty name entry
	void refresh(); ///< rescan, keeping the old list while the map cache is missing
	void select( Int row ); ///< the entry shows the name of the selected replay
	void setName( const UnicodeString &name );
	void touch() { ++m_version; } ///< bump after any change so the view refreshes

	Bool canSave() const { return !m_name.isEmpty(); }

	void (*m_closePopup)() = nullptr; ///< hides the popup, set by the screen showing it

	std::vector<ReplayRow> m_rows;
	Int m_selected = -1;
	UnicodeString m_name; ///< the name entry

	Bool m_showSaved = FALSE;
	UnsignedInt m_savedTime = 0; ///< timeGetTime() when the notice came up, 0 while none is pending

	UnsignedInt m_version = 0;
	UnsignedInt m_rowsVersion = 0; ///< changes whenever m_rows is rebuilt
};
