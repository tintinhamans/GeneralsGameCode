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

// FILE: SaveLoadActions.h ///////////////////////////////////////////////////
// Widget-agnostic save/load screen logic: selecting, loading, saving, deleting and the
// confirmations around them, changing SaveLoadData and driving the game. PopupSaveLoad.cpp's
// .wnd callbacks and RmlSaveLoadScreen both call these instead of duplicating the rules.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameState.h"

namespace SaveLoadActions
{
	// Start a screen: SaveLoad.wnd is the shell version (isPopup FALSE, also brings up the shell
	// map), PopupSaveLoad.wnd the one over the game. closePopup hides a popup and may be null.
	void open( SaveLoadLayoutType layoutType, Bool isPopup, void (*closePopup)() );

	void select( Int row );
	void activate( Int row ); ///< double click: select and load

	// Main buttons.
	void load();
	void save();
	void remove();
	void back(); ///< closes the popup, or pops the shell screen
	void escape(); ///< Escape: drops any dialog, then back()

	// Confirmations.
	void confirmLoad();
	void cancelLoad();
	void confirmOverwrite();
	void cancelOverwrite();
	void confirmSaveDesc(); ///< saves under SaveLoadData::m_description
	void cancelSaveDesc();
	void confirmDelete();
	void cancelDelete();
}
