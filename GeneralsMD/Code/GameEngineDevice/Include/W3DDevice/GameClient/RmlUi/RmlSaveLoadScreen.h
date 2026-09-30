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

// FILE: RmlSaveLoadScreen.h //////////////////////////////////////////////////
// RmlUi view of Menus/SaveLoad.wnd (the main menu's Load Game, full screen) and
// Menus/PopupSaveLoad.wnd (the in-game Quit menu's Save/Load, over the game): one
// document and one instance serve both paths, told apart by the registry entry point
// that opened it. All the rules live in SaveLoadActions/SaveLoadData; this only
// mirrors SaveLoadData into a data model after every action.
//
// Like the quit menu it is not an RmlScreen swapped in by showScreen(): the shell
// version is opened by Shell::push's placeholder layout, the popup by
// openQuitMenuSaveLoad(). Both are modal so Escape reaches the document.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlSaveLoadScreen
{
public:
	static RmlSaveLoadScreen &instance();

	void open(bool isPopup);
	void close(bool reverseTransition = true); ///< false once a load took over, like an immediate pop
	bool isVisible() const;
	void back(); ///< Escape: same as KEY_ESC in PopupSaveLoad.cpp

private:
	RmlSaveLoadScreen() {}

	void load(Rml::Context *context);
	void refresh();

	void onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onActivateRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSave(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onKeyDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onConfirmLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancelLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onConfirmOverwrite(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancelOverwrite(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onConfirmSaveDesc(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancelSaveDesc(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onConfirmDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancelDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	struct RowModel
	{
		Rml::String name;
		Rml::String time;
		Rml::String date;
		Rml::String colorHex = "#FFFFFF";
		Rml::String map;
		Rml::String campaign; ///< empty outside a campaign
		int mission = 0; ///< 1-based, 0 outside a campaign
		bool isMissionSave = false;
		bool isNew = false; ///< the "new save game" entry
		int index = 0;
		bool selected = false;
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	Rml::Vector<RowModel> m_rows;
	RowModel m_selected; ///< the selected row for the details card, valid when m_hasSelection
	bool m_hasSelection = false;
	Rml::String m_description; ///< the save name entry, two-way bound
	int m_descriptionMax = 0;
	bool m_isPopup = false;
	bool m_listLocked = false;
	bool m_saveDisabled = false;
	bool m_loadDisabled = false;
	bool m_deleteDisabled = false;
	bool m_backDisabled = false;
	bool m_showLoadConfirm = false;
	bool m_showOverwriteConfirm = false;
	bool m_showSaveDesc = false;
	bool m_showDeleteConfirm = false;
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlSaveLoadScreen();
void CloseRmlSaveLoadScreen();
void OpenRmlPopupSaveLoadScreen();
void CloseRmlPopupSaveLoadScreen();
