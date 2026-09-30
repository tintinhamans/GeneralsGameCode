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

#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlReplayMenuScreen
{
public:
	static RmlReplayMenuScreen &instance();
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close(bool reverseTransition = true); ///< false once a replay took over, like an immediate pop
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
		Rml::String time;
		Rml::String date;
		Rml::String mapPath;
		Rml::String duration;
		Rml::String playersText; ///< the players, comma separated
		bool hasMap = false;
		bool isCompatible = false;
		bool isMultiplayer = false;
		int index = 0;
		bool selected = false;
	};

	// One player of the selected replay, for its card.
	struct PlayerModel
	{
		Rml::String name;
		Rml::String colorHex; ///< empty when the slot has no colour
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status

	Rml::Vector<RowModel> m_rows;
	RowModel m_selected; ///< the selected row for the details card, valid when m_hasSelection
	bool m_hasSelection = false;
	Rml::Vector<PlayerModel> m_selectedPlayers;
	unsigned int m_shownVersion = 0;
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlReplayMenuScreen();
void CloseRmlReplayMenuScreen();
