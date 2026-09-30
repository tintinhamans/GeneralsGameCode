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

// FILE: RmlOnlineMapSelectScreen.h /////////////////////////////////////////////
// RmlScreen for Data/UI/OnlineMapSelect.rml. Registered for Menus/WOLMapSelectMenu.wnd,
// opened by RmlOnlineGameSetupScreen's "Select Map" button (RmlUiScreenRegistry::open()),
// same shape as RmlLanGameSetupScreen <-> RmlLanMapSelectScreen. Modeled directly on
// RmlLanMapSelectScreen, with two differences that mirror WOLMapSelectMenu.cpp exactly:
//   - No start-position markers: per this change's report, WOLMapSelectMenu.wnd's own
//     ButtonMapStartPosition0..7 stay hidden in the live .wnd (WOLMapSelectMenuInit() hides+
//     disables them, and nothing observed in this menu's flow un-hides them again), so the
//     RmlUi port doesn't draw them either -- unlike RmlLanMapSelectScreen, which does.
//   - Single-pass map list only (buildFilteredMapList(useSystemMaps, TRUE, ...)): every
//     WOLMapSelectMenu.cpp populateMapListbox() call passes isMultiplayer=TRUE, unlike
//     LanMapSelectMenu.cpp's user-maps filter, which concatenates a non-multiplayer pass
//     first. Generals Online only ever lists multiplayer maps.
// The map list itself is still widget-agnostic (MapUtil.h's buildFilteredMapList()); the
// OK write-back into TheNGMPGame lives in OnlineGameSetupActions::applySelectedMap(),
// shared with WOLMapSelectMenu.cpp's .wnd ButtonOK handler (see OnlineGameSetupActions.h).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"
#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlOnlineMapSelectScreen
{
public:
	static RmlOnlineMapSelectScreen &instance();
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as ButtonBack (KEY_ESC in WOLMapSelectMenu.cpp)

private:
	RmlOnlineMapSelectScreen() : m_mapRows(m_model.maps) {}

	void load(Rml::Context *context);
	void refreshMapList(); // rebuilds m_model.maps (buildFilteredMapList()) for the current filter
	void refreshStartMarkers(); // rebuilds m_model.startMarkers for m_model.selectedMapName
	void selectMap(const Rml::String &mapName); // OK/double-click: write back + return to setup

	void onFilterChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onPlayerFilterChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // (players), 0 = any
	void refreshVisibleCount(); // applies the player filter and the search, and counts what is left
	void onSearchChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // search box, every keystroke
	void onSortChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // (1: by players, 0: by name)
	void onMapSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapActivated(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // double-click: select + OK
	void onOk(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status

	// One row in the map-list (see GameSetup.rcss's reusable "map-row" markup block).
	struct MapEntryModel
	{
		Rml::String mapName; // map cache key, also the click/select/OK value
		Rml::String displayName;
		int numPlayers = 0;
		bool hasStarImage = false;
		Rml::String starImage; // TheMappedImageCollection name, valid only if hasStarImage
		bool isSelected = false;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
		bool visible = true; // passes the player filter and the search
	};

	// One start-position marker on the preview (see GameSetupStartPositionMarker): the same plain
	// numbered marker for every start spot, like the LAN and skirmish map selects.
	struct StartMarkerModel
	{
		int position = 0;
		Rml::String xStyle; // e.g. "12.500%", bound via data-style-left
		Rml::String yStyle; // e.g. "34.200%", bound via data-style-top
		bool used = false; // MAX_SLOTS entries always; see GameSetupStartPositionMarker::m_used
	};

	struct Model
	{
		Rml::Vector<MapEntryModel> maps;
		bool useSystemMaps = true;

		Rml::String selectedMapName;
		Rml::String selectedDisplayName;
		bool hasSelection = false;
		int selectedNumPlayers = 0;

		Rml::Vector<StartMarkerModel> startMarkers;
		int playerFilter = 0; // show only maps for this many players; 0 = any
		int visibleCount = 0; // live maps that pass playerFilter and the search, for the empty state
		Rml::String searchText; // the search box: a case-insensitive part of the name
		bool sortByPlayers = false; // order by player count instead of name
		Rml::String mapCountText; // "n of N maps"
	} m_model;

	// Grow-only wrapper around m_model.maps (see RmlGrowOnlyList.h): the filter toggle can rebuild
	// this list with a different count while the document stays open, so its storage never shrinks.
	RmlGrowOnlyList<MapEntryModel> m_mapRows;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlOnlineMapSelectScreen();
void CloseRmlOnlineMapSelectScreen();
