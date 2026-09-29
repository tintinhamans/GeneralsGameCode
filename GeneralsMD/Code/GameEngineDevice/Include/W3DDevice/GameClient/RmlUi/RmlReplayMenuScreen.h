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

// FILE: RmlReplayMenuScreen.h ////////////////////////////////////////////////
// RmlUi view of Menus/ReplayMenu.wnd (main menu > Load > Replay). All the rules live in
// ReplayMenuActions/ReplayMenuData; this only mirrors ReplayMenuData into a data model after
// every action and once per frame (a confirmed delete or copy runs from there, and message box
// callbacks change the list behind the view's back).
//
// Like the save/load screen it is not an RmlScreen swapped in by showScreen(): Shell::push's
// placeholder layout opens it through the registry. It is modal so Escape reaches it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlReplayMenuScreen
{
public:
	static RmlReplayMenuScreen &instance();

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as KEY_ESC in ReplayMenu.cpp

	// Per-frame pump while visible; see RmlUiManager::update().
	static void tick();

private:
	RmlReplayMenuScreen() {}

	void load(Rml::Context *context);
	void refresh();

	void onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onActivateRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCopy(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	struct RowModel
	{
		Rml::String name;
		Rml::String dateTime;
		Rml::String version;
		Rml::String map;
		Rml::String tooltip;
		Rml::String colorHex = "#FFFFFF";
		Rml::String mapColorHex = "#FFFFFF";
		int index = 0;
		bool selected = false;
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	Rml::Vector<RowModel> m_rows;
	unsigned int m_shownVersion = 0;
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlReplayMenuScreen();
void CloseRmlReplayMenuScreen();
