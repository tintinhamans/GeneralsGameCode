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
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/PlayerStatsData.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchData.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchSession.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same private per-file helper every RmlScreen keeps (see e.g. RmlOnlineGameSetupScreen.cpp).
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1);
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

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
			mapHandle.RegisterMember("selected", &MapRowModel::selected);
			mapHandle.RegisterMember("used", &MapRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<MapRowModel>>();

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

		constructor.BindEventCallback("playlist_changed", &RmlQuickMatchScreen::onPlaylistChanged, this);
		constructor.BindEventCallback("map_row_clicked", &RmlQuickMatchScreen::onMapRowClicked, this);
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
// QuickMatchSession::EventSink -- every write the async NGMP lobby callbacks and the per-frame
// update used to make straight to quickmatchTextWindow/buttonBack/buttonStop/buttonWiden/
// buttonBuddies (WOLQuickMatchMenu.cpp's buildQuickMatchSessionSink()) lands on m_model +
// DirtyVariable() here instead.
static QuickMatchSession::EventSink buildEventSink(RmlQuickMatchScreen *screen)
{
	QuickMatchSession::EventSink sink;

	sink.statusLine = [screen](const UnicodeString &text, Color color) { screen->onStatusLine(unicodeToUtf8(text), colorToCss(color)); };
	sink.setBackButtonEnabled = [screen](Bool enabled) { screen->setBackButtonEnabled(enabled == TRUE); };
	sink.setStopButtonEnabled = [screen](Bool enabled) { screen->setStopButtonEnabled(enabled == TRUE); };
	sink.setWidenButtonEnabled = [screen](Bool enabled) { screen->setWidenButtonEnabled(enabled == TRUE); };
	sink.setCommunicatorButtonEnabled = [screen](Bool enabled) { screen->setCommunicatorButtonEnabled(enabled == TRUE); };
	sink.communicatorCountChanged = [screen](int numNotifications) { screen->onCommunicatorCountChanged(numNotifications); };

	return sink;
}

// g_playerStatsUpdatedHook target (see PlayerStatsData.h).
static void onQuickMatchPlayerStatsDelivered(const PlayerStatsData &data)
{
	RmlQuickMatchScreen::instance().onPlayerStatsUpdated(data);
}

//-------------------------------------------------------------------------------------------------
void RmlQuickMatchScreen::show()
{
	if (!m_document)
		return;

	m_model.statusLines.clear();
	m_lastStatusLineCount = 0;
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

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	refreshPlaylists();

	g_playerStatsUpdatedHook = &onQuickMatchPlayerStatsDelivered;
	RequestLocalPlayerStatsData([this](const PlayerStatsData &data) { applyPlayerStatsToModel(data); });

	QuickMatchSession::enter(buildEventSink(this));

	m_document->Show();
}

void RmlQuickMatchScreen::hide()
{
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

	if (g_playerStatsUpdatedHook == &onQuickMatchPlayerStatsDelivered)
		g_playerStatsUpdatedHook = nullptr;
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
	if (!m_document || !isVisible())
		return;

	QuickMatchSession::update(buildEventSink(this));

	// See RmlOnlineGameSetupScreen::scrollChatToBottom(): the DirtyVariable() in onStatusLine() lands
	// before RmlUi's next layout pass, so the post-layout GetScrollHeight() has to happen here instead.
	if (m_model.statusLines.size() != m_lastStatusLineCount)
	{
		m_lastStatusLineCount = m_model.statusLines.size();
		scrollStatusFeedToBottom();
	}
}

void RmlQuickMatchScreen::scrollStatusFeedToBottom()
{
	if (!m_document)
		return;
	Rml::Element *feed = m_document->GetElementById("status-feed");
	if (feed)
		feed->SetScrollTop(feed->GetScrollHeight());
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
