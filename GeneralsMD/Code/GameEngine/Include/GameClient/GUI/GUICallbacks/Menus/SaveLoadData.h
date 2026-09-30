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

// FILE: SaveLoadData.h //////////////////////////////////////////////////////
// Widget-agnostic state of the save/load screens (SaveLoad.wnd, PopupSaveLoad.wnd and
// their RmlUi replacement): the save list, the selection, save vs load mode, which
// confirmation dialog is up and the save name being typed. SaveLoadActions changes it;
// the screens only draw it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameState.h"
#include "Common/UnicodeString.h"

#include <vector>

struct SaveLoadRow
{
	UnicodeString m_name;
	UnicodeString m_time;
	UnicodeString m_date;
	UnsignedInt m_color = 0xFFFFFF; ///< 0x00RRGGBB
	AvailableGameInfo *m_info = nullptr; ///< nullptr for the "new save game" entry
	UnicodeString m_mapLabel; ///< the map's display name
	UnicodeString m_campaign; ///< the campaign's display name, empty outside a campaign
	Int m_missionNumber = 0; ///< 1-based mission of the campaign, 0 outside one
	Bool m_isMissionSave = FALSE; ///< the automatic save at the start of a mission
};

struct SaveLoadData
{
	enum Dialog
	{
		DIALOG_NONE,
		DIALOG_LOAD_CONFIRM,
		DIALOG_OVERWRITE_CONFIRM,
		DIALOG_SAVE_DESC,
		DIALOG_DELETE_CONFIRM,
	};

	static const Int MAX_DESCRIPTION_LENGTH = 128; ///< the .wnd entry field's MAXLEN

	static SaveLoadData &instance()
	{
		static SaveLoadData s_data;
		return s_data;
	}

	void open( SaveLoadLayoutType layoutType, Bool isPopup );
	void refresh(); ///< rescan the save directory, select the first entry
	void select( Int row );
	void touch() { ++m_version; } ///< bump after any change so the view refreshes

	AvailableGameInfo *selectedInfo() const;
	Bool canSave() const { return m_layoutType != SLLT_LOAD_ONLY; }
	Bool canLoad() const { return selectedInfo() != nullptr; }
	Bool canDelete() const { return selectedInfo() != nullptr; }

	// The list is locked whenever a dialog is up; the buttons too, except under the save name
	// entry, which leaves them usable like the .wnd does.
	Bool isListEnabled() const { return m_dialog == DIALOG_NONE; }
	Bool areButtonsEnabled() const { return m_dialog == DIALOG_NONE || m_dialog == DIALOG_SAVE_DESC; }

	SaveLoadLayoutType m_layoutType = SLLT_INVALID;
	Bool m_isPopup = FALSE; ///< over the game (PopupSaveLoad) rather than the shell (SaveLoad)
	Bool m_open = FALSE; ///< set by the RmlUi screen while it is up
	void (*m_closePopup)() = nullptr; ///< hides the popup, set by the screen showing it

	std::vector<SaveLoadRow> m_rows;
	Int m_selected = -1;
	Dialog m_dialog = DIALOG_NONE;
	UnicodeString m_description; ///< the save name entry

	UnsignedInt m_version = 0;
	UnsignedInt m_rowsVersion = 0; ///< changes whenever m_rows is rebuilt
};
