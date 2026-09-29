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

// FILE: ReplayMenuData.h ////////////////////////////////////////////////////
// Widget-agnostic state of the replay menu (ReplayMenu.wnd and its RmlUi replacement): the
// replay list and the selection. ReplayMenuActions changes it; the screens only draw it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/ReplayListData.h"

#include <vector>

struct ReplayMenuData
{
	static ReplayMenuData &instance()
	{
		static ReplayMenuData s_data;
		return s_data;
	}

	void open(); ///< scan the replay directory, select the first entry
	void refresh(); ///< same, keeping the old list while the map cache is missing
	void select( Int row );
	void touch() { ++m_version; } ///< bump after any change so the view refreshes

	const ReplayRow *selectedRow() const;

	void (*m_closeScreen)() = nullptr; ///< hides the screen once a replay started, set by the screen showing it

	std::vector<ReplayRow> m_rows;
	Int m_selected = -1;

	// Set by the confirmation boxes and run by ReplayMenuActions::update(), once the box is gone.
	Bool m_deletePending = FALSE;
	Bool m_copyPending = FALSE;

	UnsignedInt m_version = 0;
	UnsignedInt m_rowsVersion = 0; ///< changes whenever m_rows is rebuilt
};
