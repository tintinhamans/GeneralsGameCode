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

// FILE: RmlSkirmishMapSelectScreen.h ///////////////////////////////////////////
// RmlScreen for Data/UI/SkirmishMapSelect.rml. Registered for
// Menus/SkirmishMapSelectMenu.wnd, opened by RmlSkirmishSetupScreen's "Select
// Map" button (RmlUiScreenRegistry::open()), same as SkirmishGameOptionsMenu.cpp's
// ButtonSelectMap -> winCreateLayout() flow. Not routed through RmlUiManager::
// showScreen()'s single-current-screen swap -- like RmlQuitMenuScreen it stays
// independent of the screen underneath, which it hides while open and re-shows
// (refreshed, not reset) on OK/Back via ReturnToRmlSkirmishSetupScreen(). The
// map list itself is widget-agnostic already (MapUtil.h's buildFilteredMapList());
// only the OK write-back into TheSkirmishGameInfo lives in MapSelectActions,
// shared with SkirmishMapSelectMenu.cpp's .wnd callbacks.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlSkirmishMapSelectScreen
{
public:
	static RmlSkirmishMapSelectScreen &instance();

	void open();
	void close();
	bool isVisible() const;

private:
	RmlSkirmishMapSelectScreen() {}

	void load(Rml::Context *context);
	void refreshMapList(); // rebuilds m_model.maps (buildFilteredMapList()) for the current filter
	void refreshStartMarkers(); // rebuilds m_model.startMarkers for m_model.selectedMapName
	void selectMap(const Rml::String &mapName); // OK/double-click: write back + return to setup

	void onFilterChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapActivated(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // double-click: select + OK
	void onOk(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	// One row in the map-list (see GameSetup.rcss's reusable "map-row" markup block).
	struct MapEntryModel
	{
		Rml::String mapName; // map cache key, also the click/select/OK value
		Rml::String displayName;
		int numPlayers = 0;
		bool hasStarImage = false;
		Rml::String starImage; // TheMappedImageCollection name, valid only if hasStarImage
		bool isSelected = false;
	};

	// One start-position marker on the preview (see GameSetupStartPositionMarker); no occupant/color
	// fields here -- positionStartSpotControls() draws the same generic marker image for every start
	// spot on the browse preview, unlike the setup screen's per-slot-colored markers.
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
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlSkirmishMapSelectScreen();
void CloseRmlSkirmishMapSelectScreen();
