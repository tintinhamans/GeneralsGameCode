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

#include "W3DDevice/GameClient/RmlUi/RmlSkirmishMapSelectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlMapSearch.h"

#include "Common/SkirmishBattleHonors.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/GUI/GUICallbacks/Menus/GameSetupData.h"
#include "GameClient/GUI/GUICallbacks/Menus/MapSelectActions.h"
#include "GameClient/MapUtil.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameNetwork/GameInfo.h"
#include "W3DDevice/GameClient/RmlUi/RmlSkirmishSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RmlSkirmishMapSelectScreen &RmlSkirmishMapSelectScreen::instance()
{
	static RmlSkirmishMapSelectScreen s_screen;
	return s_screen;
}

void RmlSkirmishMapSelectScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("skirmishmapselect");
	if (constructor)
	{
		Rml::StructHandle<MapEntryModel> entryHandle = constructor.RegisterStruct<MapEntryModel>();
		if (entryHandle)
		{
			entryHandle.RegisterMember("map_name", &MapEntryModel::mapName);
			entryHandle.RegisterMember("display_name", &MapEntryModel::displayName);
			entryHandle.RegisterMember("num_players", &MapEntryModel::numPlayers);
			entryHandle.RegisterMember("has_star_image", &MapEntryModel::hasStarImage);
			entryHandle.RegisterMember("star_image", &MapEntryModel::starImage);
			entryHandle.RegisterMember("is_selected", &MapEntryModel::isSelected);
			entryHandle.RegisterMember("used", &MapEntryModel::used);
			entryHandle.RegisterMember("visible", &MapEntryModel::visible);
		}
		Rml::StructHandle<StartMarkerModel> markerHandle = constructor.RegisterStruct<StartMarkerModel>();
		if (markerHandle)
		{
			markerHandle.RegisterMember("position", &StartMarkerModel::position);
			markerHandle.RegisterMember("x_style", &StartMarkerModel::xStyle);
			markerHandle.RegisterMember("y_style", &StartMarkerModel::yStyle);
			markerHandle.RegisterMember("used", &StartMarkerModel::used);
		}

		constructor.RegisterArray<Rml::Vector<MapEntryModel>>();
		constructor.RegisterArray<Rml::Vector<StartMarkerModel>>();

		constructor.Bind("maps", &m_model.maps);
		constructor.Bind("use_system_maps", &m_model.useSystemMaps);
		constructor.Bind("selected_map_name", &m_model.selectedMapName);
		constructor.Bind("selected_display_name", &m_model.selectedDisplayName);
		constructor.Bind("has_selection", &m_model.hasSelection);
		constructor.Bind("selected_num_players", &m_model.selectedNumPlayers);
		constructor.Bind("start_markers", &m_model.startMarkers);
		constructor.Bind("player_filter", &m_model.playerFilter);
		constructor.Bind("visible_count", &m_model.visibleCount);
		constructor.Bind("search_text", &m_model.searchText);
		constructor.Bind("sort_by_players", &m_model.sortByPlayers);
		constructor.Bind("map_count_text", &m_model.mapCountText);
		constructor.BindEventCallback("search_changed", &RmlSkirmishMapSelectScreen::onSearchChanged, this);
		constructor.BindEventCallback("sort_changed", &RmlSkirmishMapSelectScreen::onSortChanged, this);
		m_hq.bind(constructor);

		constructor.BindEventCallback("filter_changed", &RmlSkirmishMapSelectScreen::onFilterChanged, this);
		constructor.BindEventCallback("player_filter_changed", &RmlSkirmishMapSelectScreen::onPlayerFilterChanged, this);
		constructor.BindEventCallback("map_selected", &RmlSkirmishMapSelectScreen::onMapSelected, this);
		constructor.BindEventCallback("map_activated", &RmlSkirmishMapSelectScreen::onMapActivated, this);
		constructor.BindEventCallback("ok", &RmlSkirmishMapSelectScreen::onOk, this);
		constructor.BindEventCallback("back", &RmlSkirmishMapSelectScreen::onBack, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/SkirmishMapSelect.rml");
}

//-------------------------------------------------------------------------------------------------
// Rebuilds m_model.maps for the current filter, same two-pass shape SkirmishMapSelectMenu.cpp's
// RadioButtonUserMaps handler uses (custom maps = non-multiplayer + multiplayer, concatenated;
// system maps = multiplayer only), and the same star tiering as MapUtil.cpp's
// addMapEntryToMapListbox() (stars only apply to the multiplayer pass).
void RmlSkirmishMapSelectScreen::refreshMapList()
{
	m_mapRows.beginUpdate();
	m_model.hasSelection = false;
	m_model.selectedDisplayName.clear();
	m_model.selectedNumPlayers = 0;

	AsciiString mapToSelect(m_model.selectedMapName.c_str());
	SkirmishBattleHonors honors;

	auto appendEntries = [&](const MapEntryList &entries, Bool isMultiplayer)
	{
		for (const MapListEntry &src : entries)
		{
			MapEntryModel &entry = m_mapRows.next();
			entry.mapName = src.mapName.str();
			entry.displayName = unicodeToUtf8(src.displayName);
			entry.numPlayers = src.numPlayers;
			entry.isSelected = src.isSelected == TRUE;

			if (isMultiplayer)
			{
				Int numBrutal = honors.getEnduranceMedal(src.mapName, SLOT_BRUTAL_AI);
				Int numMedium = honors.getEnduranceMedal(src.mapName, SLOT_MED_AI);
				Int numEasy = honors.getEnduranceMedal(src.mapName, SLOT_EASY_AI);
				if (numBrutal)
				{
					entry.hasStarImage = true;
					entry.starImage = (numBrutal == src.numPlayers - 1) ? "RedYell_Star" : "Star-Gold";
				}
				else if (numMedium)
				{
					entry.hasStarImage = true;
					entry.starImage = "Star-Silver";
				}
				else if (numEasy)
				{
					entry.hasStarImage = true;
					entry.starImage = "Star-Bronze";
				}
			}

			if (entry.isSelected)
			{
				m_model.hasSelection = true;
				m_model.selectedDisplayName = entry.displayName;
				m_model.selectedNumPlayers = entry.numPlayers;
			}
		}
	};

	if (m_model.useSystemMaps)
	{
		appendEntries(buildFilteredMapList(TRUE, TRUE, mapToSelect), TRUE);
	}
	else
	{
		appendEntries(buildFilteredMapList(FALSE, FALSE, mapToSelect), FALSE);
		appendEntries(buildFilteredMapList(FALSE, TRUE, mapToSelect), TRUE);
	}

	m_mapRows.endUpdate();
	RmlMapSearchSort(m_model.maps, m_mapRows.liveCount(), m_model.sortByPlayers);

	if (!m_model.hasSelection)
		m_model.selectedMapName.clear();

	refreshVisibleCount();

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("maps");
		m_modelHandle.DirtyVariable("selected_map_name");
		m_modelHandle.DirtyVariable("selected_display_name");
		m_modelHandle.DirtyVariable("has_selection");
		m_modelHandle.DirtyVariable("selected_num_players");
	}
}

//-------------------------------------------------------------------------------------------------
// Rebuilds m_model.startMarkers for m_model.selectedMapName, matching positionStartSpots(AsciiString,
// ...)'s browse-preview markers (SkirmishMapSelectMenu.cpp's ButtonMapStartPosition0..7 -- plain,
// unhighlighted position markers, not colored by occupant like the setup screen's).
void RmlSkirmishMapSelectScreen::refreshStartMarkers()
{
	AsciiString mapName( m_model.hasSelection ? m_model.selectedMapName.c_str() : "" );
	std::vector<GameSetupStartPositionMarker> markers = GameSetupData::computeStartPositionMarkers( mapName );

	m_model.startMarkers.clear();
	for (const GameSetupStartPositionMarker &marker : markers)
	{
		StartMarkerModel markerModel;
		markerModel.position = marker.m_position;
		char xBuf[16], yBuf[16];
		_snprintf_s(xBuf, sizeof(xBuf), _TRUNCATE, "%.3f%%", marker.m_xFraction * 100.0f);
		_snprintf_s(yBuf, sizeof(yBuf), _TRUNCATE, "%.3f%%", marker.m_yFraction * 100.0f);
		markerModel.xStyle = xBuf;
		markerModel.yStyle = yBuf;
		markerModel.used = marker.m_used == TRUE;
		m_model.startMarkers.push_back(markerModel);
	}

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("start_markers");
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishMapSelectScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	AsciiString currentMap = TheSkirmishGameInfo ? TheSkirmishGameInfo->getMap() : AsciiString::TheEmptyString;
	m_model.useSystemMaps = MapSelectActions::initialUsesSystemMaps(currentMap) == TRUE;
	m_model.selectedMapName = currentMap.str();

	if (TheMapCache)
		TheMapCache->updateCache();

	refreshMapList();
	refreshStartMarkers();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("use_system_maps");

	m_model.searchText.clear();
	refreshVisibleCount();
	m_hq.refresh(m_modelHandle, true);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("search_text");

	m_document->Show();
}

void RmlSkirmishMapSelectScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlSkirmishMapSelectScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishMapSelectScreen::onFilterChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	bool useSystem = args[0].Get<int>() != 0;
	if (useSystem == m_model.useSystemMaps)
		return;

	m_model.useSystemMaps = useSystem;
	if (TheMapCache)
		TheMapCache->updateCache();
	refreshMapList();
	refreshStartMarkers();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("use_system_maps");
}

void RmlSkirmishMapSelectScreen::onPlayerFilterChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	m_model.playerFilter = args[0].Get<int>();
	refreshVisibleCount();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("player_filter");
}

void RmlSkirmishMapSelectScreen::refreshVisibleCount()
{
	m_model.visibleCount = 0;
	for (MapEntryModel &entry : m_model.maps)
	{
		entry.visible = (m_model.playerFilter == 0 || entry.numPlayers == m_model.playerFilter)
			&& RmlMapSearchMatches(entry.displayName, m_model.searchText);
		if (entry.used && entry.visible)
			++m_model.visibleCount;
	}
	m_model.mapCountText = RmlMapSearchCountText(m_model.visibleCount, m_mapRows.liveCount());
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("maps");
		m_modelHandle.DirtyVariable("visible_count");
		m_modelHandle.DirtyVariable("map_count_text");
	}
}

// Filtering on every keystroke is fine here: nothing is sent. The value comes off the event, which
// can run before the input's own data-value controller writes search_text.
void RmlSkirmishMapSelectScreen::onSearchChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	m_model.searchText = ev.GetParameter<Rml::String>("value", m_model.searchText);
	refreshVisibleCount();
}

void RmlSkirmishMapSelectScreen::onSortChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	const bool byPlayers = args[0].Get<int>() != 0;
	if (byPlayers == m_model.sortByPlayers)
		return;
	m_model.sortByPlayers = byPlayers;
	RmlMapSearchSort(m_model.maps, m_mapRows.liveCount(), m_model.sortByPlayers);
	refreshVisibleCount();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("sort_by_players");
}

void RmlSkirmishMapSelectScreen::onMapSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	Rml::String mapName = args[0].Get<Rml::String>();

	m_model.selectedMapName = mapName;
	m_model.hasSelection = false;
	m_model.selectedDisplayName.clear();
	m_model.selectedNumPlayers = 0;

	for (MapEntryModel &entry : m_model.maps)
	{
		entry.isSelected = (entry.mapName == mapName);
		if (entry.isSelected)
		{
			m_model.hasSelection = true;
			m_model.selectedDisplayName = entry.displayName;
			m_model.selectedNumPlayers = entry.numPlayers;
		}
	}

	refreshStartMarkers();

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("maps");
		m_modelHandle.DirtyVariable("selected_map_name");
		m_modelHandle.DirtyVariable("selected_display_name");
		m_modelHandle.DirtyVariable("has_selection");
		m_modelHandle.DirtyVariable("selected_num_players");
	}
}

void RmlSkirmishMapSelectScreen::onMapActivated(Rml::DataModelHandle handle, Rml::Event &ev, const Rml::VariantList &args)
{
	// Same as SkirmishMapSelectMenu.cpp's GLM_DOUBLE_CLICKED: select then simulate ButtonOK.
	onMapSelected(handle, ev, args);
	if (m_model.hasSelection)
		selectMap(m_model.selectedMapName);
}

void RmlSkirmishMapSelectScreen::onOk(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (!m_model.hasSelection)
		return;
	selectMap(m_model.selectedMapName);
}

void RmlSkirmishMapSelectScreen::onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	close();
	ReturnToRmlSkirmishSetupScreen();
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishMapSelectScreen::selectMap(const Rml::String &mapName)
{
	AsciiString asciiMap(mapName.c_str());
	MapSelectActions::applySelectedMap(TheSkirmishGameInfo, asciiMap);

	close();
	ReturnToRmlSkirmishSetupScreen();
}

void RmlSkirmishMapSelectScreen::back()
{
	close();
	ReturnToRmlSkirmishSetupScreen();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlSkirmishMapSelectScreen()
{
	RmlSkirmishMapSelectScreen::instance().open();
}

void CloseRmlSkirmishMapSelectScreen()
{
	RmlSkirmishMapSelectScreen::instance().close();
}
