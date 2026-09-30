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

#include "W3DDevice/GameClient/RmlUi/RmlQuickMatchScreen.h"

#include "Common/AsciiString.h"
#include "Common/QuickmatchPreferences.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/Color.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/PlayerStatsData.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchData.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineSessionExit.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchSession.h"
#include "GameClient/Shell.h"
#include "GameClient/TransitionSounds.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// ARGB packing (Color.h's GameMakeColor()); RmlUi's rgba() takes 0-255 ints including alpha. Same
// helper RmlOnlineGameSetupScreen.cpp/RmlOnlineLobbyScreen.cpp each keep privately.
static Rml::String colorToCss(Color color)
{
	const int a = (color >> 24) & 0xFF;
	const int r = (color >> 16) & 0xFF;
	const int g = (color >> 8) & 0xFF;
	const int b = color & 0xFF;
	char buf[48];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "rgba(%d,%d,%d,%d)", r, g, b, a);
	return Rml::String(buf);
}

//-------------------------------------------------------------------------------------------------
RmlQuickMatchScreen &RmlQuickMatchScreen::instance()
{
	static RmlQuickMatchScreen s_screen;
	return s_screen;
}

void RmlQuickMatchScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("quickmatch");
	if (constructor)
	{
		m_hq.bind(constructor);
		Rml::StructHandle<StatusLineModel> statusHandle = constructor.RegisterStruct<StatusLineModel>();
		if (statusHandle)
		{
			statusHandle.RegisterMember("text", &StatusLineModel::text);
			statusHandle.RegisterMember("color", &StatusLineModel::color);
		}
		constructor.RegisterArray<Rml::Vector<StatusLineModel>>();

		Rml::StructHandle<PlaylistOptionModel> playlistHandle = constructor.RegisterStruct<PlaylistOptionModel>();
		if (playlistHandle)
		{
			playlistHandle.RegisterMember("index", &PlaylistOptionModel::index);
			playlistHandle.RegisterMember("label", &PlaylistOptionModel::label);
		}
		constructor.RegisterArray<Rml::Vector<PlaylistOptionModel>>();

		Rml::StructHandle<MapRowModel> mapHandle = constructor.RegisterStruct<MapRowModel>();
		if (mapHandle)
		{
			mapHandle.RegisterMember("index", &MapRowModel::index);
			mapHandle.RegisterMember("label", &MapRowModel::label);
			mapHandle.RegisterMember("path", &MapRowModel::path); // the tile's <mappreview>
			mapHandle.RegisterMember("selected", &MapRowModel::selected);
			mapHandle.RegisterMember("used", &MapRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<MapRowModel>>();

		Rml::StructHandle<ComboOptionModel> comboOptionHandle = constructor.RegisterStruct<ComboOptionModel>();
		if (comboOptionHandle)
		{
			comboOptionHandle.RegisterMember("value", &ComboOptionModel::value);
			comboOptionHandle.RegisterMember("label", &ComboOptionModel::label);
		}
		constructor.RegisterArray<Rml::Vector<ComboOptionModel>>();

		constructor.Bind("title", &m_model.title);
		constructor.Bind("status_lines", &m_model.statusLines);
		constructor.Bind("playlists", &m_model.playlists);
		constructor.Bind("selected_playlist", &m_model.selectedPlaylist);
		constructor.Bind("maps", &m_model.maps);
		constructor.Bind("show_stats", &m_model.showStats);
		constructor.Bind("options_button_label", &m_model.optionsButtonLabel);
		constructor.Bind("start_visible", &m_model.startVisible);
		constructor.Bind("start_enabled", &m_model.startEnabled);
		constructor.Bind("stop_visible", &m_model.stopVisible);
		constructor.Bind("stop_enabled", &m_model.stopEnabled);
		constructor.Bind("widen_enabled", &m_model.widenEnabled);
		constructor.Bind("back_enabled", &m_model.backEnabled);
		constructor.Bind("communicator_enabled", &m_model.communicatorEnabled);
		constructor.Bind("communicator_label", &m_model.communicatorLabel);

		constructor.Bind("streak_label_text", &m_model.streakLabelText);
		constructor.Bind("streak_value_text", &m_model.streakValueText);
		constructor.Bind("losses_text", &m_model.lossesText);
		constructor.Bind("disconnects_label_text", &m_model.disconnectsLabelText);
		constructor.Bind("disconnects_value_text", &m_model.disconnectsValueText);
		constructor.Bind("best_streak_text", &m_model.bestStreakText);
		constructor.Bind("games_played_text", &m_model.gamesPlayedText);
		constructor.Bind("wins_text", &m_model.winsText);
		constructor.Bind("win_percent_text", &m_model.winPercentText);

		constructor.Bind("rank_at_max", &m_model.rankAtMax);
		constructor.Bind("rank_progress_width_style", &m_model.rankProgressWidthStyle);
		constructor.Bind("rank_image_name", &m_model.rankImageName);
		constructor.Bind("show_faction_image", &m_model.showFactionImage);
		constructor.Bind("faction_image_name", &m_model.factionImageName);
		constructor.Bind("rank_text", &m_model.rankText);

		constructor.Bind("ladder_options", &m_model.ladderOptions);
		constructor.Bind("ladder_selected", &m_model.ladderSelected);
		constructor.Bind("max_ping_options", &m_model.maxPingOptions);
		constructor.Bind("max_ping_selected", &m_model.maxPingSelected);
		constructor.Bind("max_disconnects_options", &m_model.maxDisconnectsOptions);
		constructor.Bind("max_disconnects_selected", &m_model.maxDisconnectsSelected);
		constructor.Bind("side_options", &m_model.sideOptions);
		constructor.Bind("side_selected", &m_model.sideSelected);
		constructor.Bind("color_options", &m_model.colorOptions);
		constructor.Bind("color_selected", &m_model.colorSelected);

		constructor.Bind("map_preview_visible", &m_model.mapPreviewVisible);
		constructor.Bind("map_preview_x_style", &m_model.mapPreviewXStyle);
		constructor.Bind("map_preview_y_style", &m_model.mapPreviewYStyle);
		constructor.Bind("map_preview_map_path", &m_model.mapPreviewMapPath);
		constructor.Bind("search_elapsed_text", &m_model.searchElapsedText);

		constructor.BindEventCallback("playlist_changed", &RmlQuickMatchScreen::onPlaylistChanged, this);
		constructor.BindEventCallback("map_row_clicked", &RmlQuickMatchScreen::onMapRowClicked, this);
		constructor.BindEventCallback("map_row_hover", &RmlQuickMatchScreen::onMapRowHover, this);
		constructor.BindEventCallback("map_row_hover_clear", &RmlQuickMatchScreen::onMapRowHoverClear, this);
		constructor.BindEventCallback("toggle_options", &RmlQuickMatchScreen::onToggleOptions, this);
		constructor.BindEventCallback("start", &RmlQuickMatchScreen::onStart, this);
		constructor.BindEventCallback("stop", &RmlQuickMatchScreen::onStop, this);
		constructor.BindEventCallback("widen", &RmlQuickMatchScreen::onWiden, this);
		constructor.BindEventCallback("buddies", &RmlQuickMatchScreen::onBuddies, this);
		constructor.BindEventCallback("back", &RmlQuickMatchScreen::onBackPressed, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/QuickMatch.rml");
}

//-------------------------------------------------------------------------------------------------
// QuickMatchSignals listeners -- every write the async NGMP lobby callbacks and the per-frame
// update used to make straight to quickmatchTextWindow/buttonBack/buttonStop/buttonWiden/
// buttonBuddies (WOLQuickMatchMenu.cpp's connectQuickMatchSessionSignals()) lands on m_model +
// DirtyVariable() here instead.
static void connectSessionSignals(RmlQuickMatchScreen *screen, SignalConnections &connections)
{
	connections.add( QuickMatchSignals::statusLine().connect( [screen](const UnicodeString &text, Color color) { screen->onStatusLine(unicodeToUtf8(text), colorToCss(color)); } ) );
	connections.add( QuickMatchSignals::backButtonEnabled().connect( [screen](Bool enabled) { screen->setBackButtonEnabled(enabled == TRUE); } ) );
	connections.add( QuickMatchSignals::stopButtonEnabled().connect( [screen](Bool enabled) { screen->setStopButtonEnabled(enabled == TRUE); } ) );
	connections.add( QuickMatchSignals::widenButtonEnabled().connect( [screen](Bool enabled) { screen->setWidenButtonEnabled(enabled == TRUE); } ) );
	connections.add( QuickMatchSignals::communicatorButtonEnabled().connect( [screen](Bool enabled) { screen->setCommunicatorButtonEnabled(enabled == TRUE); } ) );
	connections.add( QuickMatchSignals::communicatorCount().connect( [screen](int numNotifications) { screen->onCommunicatorCountChanged(numNotifications); } ) );
}

//-------------------------------------------------------------------------------------------------
void RmlQuickMatchScreen::show()
{
	if (!m_document)
		return;

	m_model.statusLines.clear();
	m_model.showStats = true;
	m_model.optionsButtonLabel = unicodeToUtf8(TheGameText->fetch("GUI:Setup"));
	m_model.startVisible = true;
	m_model.startEnabled = true;
	m_model.stopVisible = false;
	m_model.stopEnabled = true;
	m_model.widenEnabled = false;
	m_model.backEnabled = true;
	m_model.communicatorEnabled = true;
	m_model.communicatorLabel = unicodeToUtf8(TheGameText->fetch("GUI:Buddies"));

	// StaticTextTitle: GUI:QuickMatchTitle formatted with the NGMP display name (WOLQuickMatchMenu.cpp:1008-1011).
	m_model.title = unicodeToUtf8(QuickMatchActions::buildTitle());

	// Welcome msg + instructions (WOLQuickMatchMenu.cpp:1132-1135, hardcoded English literals, not GameText keys).
	onStatusLine("Welcome to QuickMatch. Choose Setup to select playlists and maps.", colorToCss(GameMakeColor(255, 194, 25, 255)));
	onStatusLine("Special thanks to map makers Tanso, ReLaX, cncHD, Specovik, Mp3, Jundiyy & Bamovich for making quickmatch possible.", colorToCss(GameMakeColor(255, 194, 25, 255)));

	m_model.mapPreviewVisible = false;
	m_searchClockRunning = false;
	m_model.searchElapsedText = "0:00";
	populateDisabledOptionCombos();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	refreshPlaylists();

	m_connections.disconnect();
	m_connections.add(PlayerStatsSignals::localPlayerUpdated().connect([this](const PlayerStatsData &data) { onPlayerStatsUpdated(data); }));
	RequestLocalPlayerStatsData([this](const PlayerStatsData &data) { applyPlayerStatsToModel(data); });

	connectSessionSignals(this, m_connections);
	QuickMatchSession::enter();

	m_hq.refresh(m_modelHandle, true);
	m_document->Show();

	// WOLQuickMatchMenuInit()'s entrance group, and WOLQuickMatchMenuShutdown()'s reverse below.
	TransitionSounds::play("WOLQuickMatchMenuFade");
}

void RmlQuickMatchScreen::hide()
{
	if (m_document && m_document->IsVisible())
		TransitionSounds::play("WOLQuickMatchMenuFade", TRUE);
	if (m_document)
		m_document->Hide();

	// Mirrors WOLQuickMatchMenuShutdown() -> saveQuickMatchOptions()'s GENERALS_ONLINE branch: persist
	// each map's selected state (see QuickMatchActions::saveMapSelections()).
	std::vector<QuickMatchData::MapOption> maps;
	for (const MapRowModel &row : m_model.maps)
	{
		if (!row.used)
			continue;
		QuickMatchData::MapOption option;
		option.mapPath = AsciiString(row.path.c_str());
		option.initiallySelected = row.selected ? TRUE : FALSE;
		maps.push_back(option);
	}
	QuickMatchActions::saveMapSelections(maps);

	QuickMatchSession::leave();

	m_connections.disconnect();
}

bool RmlQuickMatchScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlQuickMatchScreen::onBack()
{
	hide();
	TheShell->pop();
}

void RmlQuickMatchScreen::update()
{
	m_hq.refresh(m_modelHandle);
	if (!m_document || !isVisible())
		return;

	// WOLQuickMatchMenuUpdate() never runs for a registry-routed screen; this is its pending-full-teardown exit.
	if (OnlineSessionExit::isTeardownReady())
	{
		hide();
		OnlineSessionExit::tearDownAndPop();
		return;
	}

	QuickMatchSession::update();
	updateSearchClock();

	if (m_model.mapPreviewVisible)
		clampMapPreview();
}

void RmlQuickMatchScreen::updateSearchClock()
{
	if (!m_model.stopVisible)
	{
		m_searchClockRunning = false;
		return;
	}

	const unsigned long long now = ::GetTickCount64();
	if (!m_searchClockRunning)
	{
		m_searchClockRunning = true;
		m_searchStartMs = now;
		m_searchElapsedSeconds = -1;
	}

	const int seconds = (int)((now - m_searchStartMs) / 1000ULL);
	if (seconds == m_searchElapsedSeconds)
		return;
	m_searchElapsedSeconds = seconds;

	char buf[32];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d:%02d", seconds / 60, seconds % 60);
	m_model.searchElapsedText = buf;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("search_elapsed_text");
}

// Called each update() while the preview is visible; see the declaration's comment.
void RmlQuickMatchScreen::clampMapPreview()
{
	if (!m_document || !m_context)
		return;

	Rml::Element *preview = m_document->GetElementById("map-hover-preview");
	if (!preview)
		return;

	const Rml::Vector2f size = preview->GetBox().GetSize();
	if (size.x <= 0.0f || size.y <= 0.0f)
		return;

	// updateMapHoverPreview() offsets the preview right of the cursor and vertically centers it
	// (WOLQuickMatchMenu.cpp:277-281): previewX = mouseX + offset, previewY = mouseY - previewSize/2.
	const float offset = 20.0f;
	float left = m_mapPreviewRawX + offset;
	float top = m_mapPreviewRawY - (size.y * 0.5f);

	const Rml::Vector2i contextSize = m_context->GetDimensions();
	if (left + size.x > (float)contextSize.x)
		left = m_mapPreviewRawX - offset - size.x; // flip to the left of the cursor if it doesn't fit
	if (left < 0.0f)
		left = 0.0f;
	if (top + size.y > (float)contextSize.y)
		top = (float)contextSize.y - size.y;
	if (top < 0.0f)
		top = 0.0f;

	char buf[32];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%dpx", (int)left);
	const Rml::String newX = buf;
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%dpx", (int)top);
	const Rml::String newY = buf;

	if (newX != m_model.mapPreviewXStyle || newY != m_model.mapPreviewYStyle)
	{
		m_model.mapPreviewXStyle = newX;
		m_model.mapPreviewYStyle = newY;
		if (m_modelHandle)
		{
			m_modelHandle.DirtyVariable("map_preview_x_style");
			m_modelHandle.DirtyVariable("map_preview_y_style");
		}
	}
}

//-------------------------------------------------------------------------------------------------
void RmlQuickMatchScreen::refreshPlaylists()
{
	QuickMatchActions::retrievePlaylists([this](std::vector<QuickMatchData::PlaylistOption> options)
		{
			m_model.playlists.clear();
			for (const QuickMatchData::PlaylistOption &option : options)
			{
				PlaylistOptionModel row;
				row.index = option.index;
				row.label = option.name.str();
				m_model.playlists.push_back(row);
			}
			m_model.selectedPlaylist = 0;

			if (m_modelHandle)
			{
				m_modelHandle.DirtyVariable("playlists");
				m_modelHandle.DirtyVariable("selected_playlist");
			}

			refreshMapsForPlaylist();
		});
}

void RmlQuickMatchScreen::refreshMapsForPlaylist()
{
	std::vector<QuickMatchData::MapOption> options = QuickMatchActions::getMapSelectOptions(m_model.selectedPlaylist);

	m_mapRows.beginUpdate();
	int index = 0;
	for (const QuickMatchData::MapOption &option : options)
	{
		MapRowModel &row = m_mapRows.next();
		row.index = index++;
		row.path = option.mapPath.str();
		row.label = unicodeToUtf8(option.displayName);
		row.selected = option.initiallySelected == TRUE;
	}
	m_mapRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("maps");
}

// Fills the five disabled combos with the same values the .wnd's own population functions produce
// (see QuickMatchActions.h's comment above getLadderOptions()). Called once from show() -- these
// never change while the screen is open (they're disabled, so nothing can drive a repopulate).
void RmlQuickMatchScreen::populateDisabledOptionCombos()
{
	QuickMatchPreferences pref;

	auto fillCombo = [](const std::vector<QuickMatchData::ComboOption> &options, Rml::Vector<ComboOptionModel> &dest, int &selected)
		{
			dest.clear();
			selected = 0;
			for (const QuickMatchData::ComboOption &option : options)
			{
				ComboOptionModel row;
				row.value = option.value;
				row.label = unicodeToUtf8(option.label);
				dest.push_back(row);
				if (option.initiallySelected == TRUE)
					selected = option.value;
			}
		};

	fillCombo(QuickMatchActions::getLadderOptions(), m_model.ladderOptions, m_model.ladderSelected);
	fillCombo(QuickMatchActions::getMaxPingOptions(), m_model.maxPingOptions, m_model.maxPingSelected);
	fillCombo(QuickMatchActions::getMaxDisconnectsOptions(pref.getMaxDisconnects()), m_model.maxDisconnectsOptions, m_model.maxDisconnectsSelected);
	fillCombo(QuickMatchActions::getSideOptions(pref.getSide()), m_model.sideOptions, m_model.sideSelected);
	fillCombo(QuickMatchActions::getColorOptions(pref.getColor()), m_model.colorOptions, m_model.colorSelected);
}

void RmlQuickMatchScreen::applyPlayerStatsToModel(const PlayerStatsData &data)
{
	m_model.streakLabelText = TheGameText ? unicodeToUtf8(TheGameText->fetch(data.streakLabelKey)) : Rml::String();
	m_model.streakValueText = unicodeToUtf8(data.streakValueText);
	m_model.lossesText = unicodeToUtf8(data.lossesText);
#if defined(GENERALS_ONLINE)
	m_model.disconnectsLabelText = unicodeToUtf8(data.disconnectsLabelText);
#endif
	m_model.disconnectsValueText = unicodeToUtf8(data.disconnectsValueText);
	m_model.bestStreakText = unicodeToUtf8(data.bestStreakText);
	m_model.gamesPlayedText = unicodeToUtf8(data.gamesPlayedText);
	m_model.winsText = unicodeToUtf8(data.winsText);
	m_model.winPercentText = unicodeToUtf8(data.winPercentText);

	m_model.rankAtMax = data.rankAtMax == TRUE;
	if (!data.rankAtMax)
	{
		char buf[16];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f%%", data.rankProgressPercent);
		m_model.rankProgressWidthStyle = buf;
	}
	m_model.rankImageName = data.rankImageName.str();
	m_model.showFactionImage = data.showFactionImage == TRUE;
	if (data.showFactionImage)
		m_model.factionImageName = data.factionImageName.str();
	m_model.rankText = unicodeToUtf8(data.rankText);

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

//-------------------------------------------------------------------------------------------------
void RmlQuickMatchScreen::onPlaylistChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	refreshMapsForPlaylist();
}

void RmlQuickMatchScreen::onMapRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_model.maps.size())
		return;

	m_model.maps[index].selected = !m_model.maps[index].selected;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("maps");
}

// Ports updateMapHoverPreview()'s hover-tracking (WOLQuickMatchMenu.cpp:248-282): bound to the map
// row's mouseover (first entry) and mousemove (repositioning while the cursor moves within the row).
void RmlQuickMatchScreen::onMapRowHover(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	int index = args[0].Get<int>();
	if (index < 0 || index >= (int)m_model.maps.size() || !m_model.maps[index].used)
		return;

	m_mapPreviewRawX = (float)ev.GetParameter<int>("mouse_x", 0);
	m_mapPreviewRawY = (float)ev.GetParameter<int>("mouse_y", 0);

	char buf[32];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%dpx", (int)m_mapPreviewRawX);
	m_model.mapPreviewXStyle = buf;
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%dpx", (int)m_mapPreviewRawY);
	m_model.mapPreviewYStyle = buf;
	m_model.mapPreviewMapPath = m_model.maps[index].path;
	m_model.mapPreviewVisible = true;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("map_preview_x_style");
		m_modelHandle.DirtyVariable("map_preview_y_style");
		m_modelHandle.DirtyVariable("map_preview_map_path");
		m_modelHandle.DirtyVariable("map_preview_visible");
	}
}

void RmlQuickMatchScreen::onMapRowHoverClear(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (!m_model.mapPreviewVisible)
		return;
	m_model.mapPreviewVisible = false;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("map_preview_visible");
}

void RmlQuickMatchScreen::onToggleOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonOptions' GBM_SELECTED body exactly (WOLQuickMatchMenu.cpp:1957-1972): flips the
	// mutually exclusive Setup/PlayerInfo panels and relabels the button to the OTHER panel's name.
	m_model.showStats = !m_model.showStats;
	m_model.optionsButtonLabel = unicodeToUtf8(TheGameText->fetch(m_model.showStats ? "GUI:Setup" : "GUI:PlayerInfo"));

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("show_stats");
		m_modelHandle.DirtyVariable("options_button_label");
	}
}

void RmlQuickMatchScreen::onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	QuickMatchData::PlaylistMapInfo plMapInfo = QuickMatchActions::getPlaylistMapInfo(m_model.selectedPlaylist);

	std::vector<Int> selectedMapIndexes;
	for (size_t i = 0; i < m_model.maps.size(); ++i)
	{
		if (m_model.maps[i].used && m_model.maps[i].selected)
			selectedMapIndexes.push_back((Int)i);
	}

	if (!plMapInfo.m_valid || (Int)selectedMapIndexes.size() < plMapInfo.m_minSelectedMaps)
	{
		UnicodeString msg;
		msg.format(L"You must select at least %d maps.", plMapInfo.m_minSelectedMaps);
		onStatusLine(unicodeToUtf8(msg), colorToCss(GameMakeColor(255, 255, 255, 255)));
		return;
	}

	m_model.widenEnabled = false;
	m_model.startEnabled = false;
	m_model.stopVisible = false;
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("widen_enabled");
		m_modelHandle.DirtyVariable("start_enabled");
		m_modelHandle.DirtyVariable("stop_visible");
	}

	QuickMatchActions::startMatchmaking(plMapInfo.m_playlistID, selectedMapIndexes, plMapInfo.m_minSelectedMaps, [this](Bool bSuccess)
		{
			if (bSuccess)
			{
				m_model.widenEnabled = true;
				m_model.startVisible = false;
				m_model.stopVisible = true;
			}
			else
			{
				onStatusLine("Failed to start matchmaking.", colorToCss(GameMakeColor(255, 255, 255, 255)));
				m_model.widenEnabled = false;
				m_model.startVisible = true;
				m_model.startEnabled = true;
				m_model.stopVisible = false;
			}

			if (m_modelHandle)
			{
				m_modelHandle.DirtyVariable("widen_enabled");
				m_modelHandle.DirtyVariable("start_visible");
				m_modelHandle.DirtyVariable("start_enabled");
				m_modelHandle.DirtyVariable("stop_visible");
			}
		});
}

void RmlQuickMatchScreen::onStop(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonStop's GENERALS_ONLINE body exactly (WOLQuickMatchMenu.cpp:1933-1955).
	m_model.widenEnabled = false;
	m_model.startVisible = true;
	m_model.startEnabled = true;
	m_model.stopVisible = false;

	onStatusLine(unicodeToUtf8(TheGameText->fetch("GUI:QMAborted")), colorToCss(GameMakeColor(255, 255, 255, 255)));

	QuickMatchActions::cancelMatchmaking();

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("widen_enabled");
		m_modelHandle.DirtyVariable("start_visible");
		m_modelHandle.DirtyVariable("start_enabled");
		m_modelHandle.DirtyVariable("stop_visible");
	}
}

void RmlQuickMatchScreen::onWiden(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonWiden's GENERALS_ONLINE body exactly (WOLQuickMatchMenu.cpp:1973-1987).
	onStatusLine(unicodeToUtf8(TheGameText->fetch("QM:WIDENINGSEARCH")), colorToCss(GameMakeColor(255, 255, 255, 255)));

	QuickMatchActions::widenSearch();

	m_model.widenEnabled = false;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("widen_enabled");
}

void RmlQuickMatchScreen::onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	QuickMatchActions::toggleBuddiesOverlay();
}

void RmlQuickMatchScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

//-------------------------------------------------------------------------------------------------
void RmlQuickMatchScreen::onStatusLine(const Rml::String &text, const Rml::String &color)
{
	StatusLineModel line;
	line.text = text;
	line.color = color;
	m_model.statusLines.push_back(line);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("status_lines");
}

void RmlQuickMatchScreen::setBackButtonEnabled(bool enabled)
{
	m_model.backEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("back_enabled");
}

void RmlQuickMatchScreen::setStopButtonEnabled(bool enabled)
{
	m_model.stopEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("stop_enabled");
}

void RmlQuickMatchScreen::setWidenButtonEnabled(bool enabled)
{
	m_model.widenEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("widen_enabled");
}

void RmlQuickMatchScreen::setCommunicatorButtonEnabled(bool enabled)
{
	m_model.communicatorEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("communicator_enabled");
}

void RmlQuickMatchScreen::onCommunicatorCountChanged(int numNotifications)
{
	UnicodeString buttonText;
	if (numNotifications > 0)
		buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
	else
		buttonText = TheGameText->fetch("GUI:Buddies");
	m_model.communicatorLabel = unicodeToUtf8(buttonText);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("communicator_label");
}

void RmlQuickMatchScreen::onPlayerStatsUpdated(const PlayerStatsData &data)
{
	applyPlayerStatsToModel(data);
}

//-------------------------------------------------------------------------------------------------
void OpenRmlQuickMatchScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlQuickMatchScreen::instance());
}

void CloseRmlQuickMatchScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlQuickMatchScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
