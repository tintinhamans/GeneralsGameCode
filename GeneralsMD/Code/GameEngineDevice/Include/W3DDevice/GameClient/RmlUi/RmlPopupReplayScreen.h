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

// FILE: RmlPopupReplayScreen.h ///////////////////////////////////////////////
// RmlUi view of Menus/PopupReplay.wnd (the score screen's Save Replay popup). All the rules live in
// PopupReplayActions/PopupReplayData; this only mirrors PopupReplayData into a data model after
// every action and once per frame (the "Replay Saved" notice closes the popup from there, and
// the overwrite box's callback changes the data behind the view's back).
//
// It is not an RmlScreen swapped in by showScreen(): ScoreScreenActions::startSaveReplayFlow()
// opens it through the registry over the score screen. It is modal so Escape reaches it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlPopupReplayScreen
{
public:
	static RmlPopupReplayScreen &instance();

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as KEY_ESC in PopupReplay.cpp

	// Per-frame pump while visible; see RmlUiManager::update().
	static void tick();

private:
	RmlPopupReplayScreen() {}

	void load(Rml::Context *context);
	void refresh();

	void onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onNameChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSave(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	struct RowModel
	{
		Rml::String name;
		Rml::String dateTime;
		Rml::String version;
		Rml::String map;
		Rml::String colorHex = "#FFFFFF";
		Rml::String mapColorHex = "#FFFFFF";
		int index = 0;
		bool selected = false;
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	Rml::Vector<RowModel> m_rows;
	Rml::String m_name; ///< the replay name entry, two-way bound
	int m_nameMax = 0;
	bool m_saveDisabled = true;
	bool m_showSaved = false;
	unsigned int m_shownVersion = 0;
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlPopupReplayScreen();
void CloseRmlPopupReplayScreen();
