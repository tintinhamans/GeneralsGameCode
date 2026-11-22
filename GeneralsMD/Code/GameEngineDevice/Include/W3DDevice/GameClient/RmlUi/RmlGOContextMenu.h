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

// Ronin @feature 22/11/2026 DX9: GO's right-click menus (GenOnlineRC*.wnd, the chat message menu) over the RmlUi Communicator. GO
// opens, fills, answers and closes its .wnd; this hides it, lists its shown buttons where GO placed it, and presses the one picked.
#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <vector>

namespace Rml { class ElementDocument; class Event; }
class GameWindow;

class RmlGOContextMenu
{
public:
	static RmlGOContextMenu &instance();
	static void tick();		// RmlUiManager::update(): follows GO's lone right-click menu while the RmlUi Communicator is up
	void releaseRml() { m_document = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back();			// Escape: the menu closes, as a press outside it

private:
	RmlGOContextMenu() : m_itemRows(m_model.items) {}

	void load();
	void read(GameWindow *menu);
	static void buttons(GameWindow *menu, std::vector<GameWindow *> &out);	// its shown push buttons, top to bottom

	void onPick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onInside(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onOutside(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	GameWindow *m_menu = nullptr;		// the .wnd menu shown; a new one is read afresh
	bool m_pressInside = false;			// the mousedown reached the menu before the layer behind it

	struct ItemModel
	{
		Rml::String label;
		int index = 0;
		bool used = true;
	};

	struct Model
	{
		Rml::Vector<ItemModel> items;
	} m_model;

	RmlGrowOnlyList<ItemModel> m_itemRows;
};
