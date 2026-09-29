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

// FILE: RmlQuickMatchScreen.h /////////////////////////////////////////////////
// RmlScreen for Data/UI/QuickMatch.rml. Registered for Menus/WOLQuickMatchMenu.wnd
// -- see WOLQuickMatchMenu.cpp for the .wnd this reproduces and the quick match
// triage note (scratchpad) for the full control-by-control breakdown this binds:
//   - ComboBoxNumPlayers (repurposed as the playlist selector under GENERALS_ONLINE)
//     and ListBoxMapSelect (per-map toggle) drive QuickMatchActions::retrievePlaylists()/
//     getMapSelectOptions()/getPlaylistMapInfo(), same data QuickMatchActions already
//     exposes to WOLQuickMatchMenu.cpp.
//   - ButtonStart/ButtonStop/ButtonWiden/ButtonBuddies map straight onto
//     QuickMatchActions::startMatchmaking()/cancelMatchmaking()/widenSearch()/
//     toggleBuddiesOverlay(), the same widget-agnostic actions the .wnd calls.
//   - The async lobby lifecycle (status lines, Back/Stop/Widen enable state, the
//     Buddies notification badge, match-found/countdown/timeout) comes from
//     QuickMatchSession::enter()/update()/leave() via the QuickMatchSignals connected in the
//     .cpp -- no GameWindow involved, same session the .wnd shares.
//   - ButtonOptions' Setup/PlayerInfo toggle (WOLQuickMatchMenu.cpp's isInfoShown()/
//     hideInfoGadgets()/hideOptionsGadgets()) becomes show_stats/showsSetup panels
//     driven by one m_model.showStats bool instead of two GameWindow's winHide()
//     calls.
//   - The stats panel (ParentStats: streak/losses/disconnects/best streak/games
//     played/wins/win percent + the rank/faction-image cluster) binds the same
//     PlayerStatsData/RequestLocalPlayerStatsData()/PlayerStatsSignals::localPlayerUpdated pipeline
//     RmlOnlineWelcomeScreen already uses, same subset of fields the .wnd's
//     PopulatePlayerInfoWindows("WOLQuickMatchMenu.wnd") call fills.
//   - ComboBoxLadder/ComboBoxMaxPing/ComboBoxMaxDisconnects/ComboBoxSide/ComboBoxColor
//     are shown disabled, populated from QuickMatchActions::getLadderOptions()/
//     getMaxPingOptions()/getMaxDisconnectsOptions()/getSideOptions()/getColorOptions() --
//     the same values the .wnd's own population functions produce under GENERALS_ONLINE
//     (real, populated controls that are simply force-disabled there, not dead code).
// PopupLadderSelect/PopupLadderDetails are NOT ported (see the triage note's §1:
// unreachable and unpopulated for Quick Match under Generals Online).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "GameClient/GUI/GUICallbacks/Menus/PlayerStatsData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlQuickMatchScreen : public RmlScreen
{
public:
	static RmlQuickMatchScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack
	virtual void update() override; // QuickMatchSession::update(), same as WOLQuickMatchMenuUpdate()'s non-widget slice

	// QuickMatchSignals targets -- public so the free function connecting them in the .cpp
	// (which closes over a raw screen pointer, since std::function can't bind a member function the
	// way BindEventCallback can) can reach them without befriending the class. Same idiom as
	// RmlOnlineGameSetupScreen's onChatLine()/setBackButtonEnabled()/etc.
	void onStatusLine(const Rml::String &text, const Rml::String &color);
	void setBackButtonEnabled(bool enabled);
	void setStopButtonEnabled(bool enabled);
	void setWidenButtonEnabled(bool enabled);
	void setCommunicatorButtonEnabled(bool enabled);
	void onCommunicatorCountChanged(int numNotifications);

	// PlayerStatsSignals::localPlayerUpdated target (see PlayerStatsData.h).
	void onPlayerStatsUpdated(const PlayerStatsData &data);

private:
	RmlQuickMatchScreen() : m_mapRows(m_model.maps) {}

	void refreshPlaylists();     // QuickMatchActions::retrievePlaylists() -> m_model.playlists
	void refreshMapsForPlaylist(); // QuickMatchActions::getMapSelectOptions(selected_playlist) -> m_model.maps
	void applyPlayerStatsToModel(const PlayerStatsData &data);
	void scrollStatusFeedToBottom(); // called from update() when status_lines has grown, see RmlOnlineGameSetupScreen::scrollChatToBottom()
	void populateDisabledOptionCombos(); // QuickMatchActions::getLadderOptions()/etc -> m_model.ladderOptions/etc, called once from show()

	void onPlaylistChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapRowClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapRowHover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMapRowHoverClear(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onToggleOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStop(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onWiden(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	// Called each update() while the preview is visible -- mirrors RmlOnlineLobbyScreen::clampPlayerMenu():
	// RmlUi doesn't know the preview's laid-out size until after a layout pass, so the first frame positions
	// it at the raw cursor point (onMapRowHover()) and this re-derives a clamped, cursor-centered position
	// from m_mapPreviewRawX/Y once the real size is available, same on-screen clamp updateMapHoverPreview()
	// does against TheDisplay's width/height (WOLQuickMatchMenu.cpp:248-282), just deferred a frame.
	void clampMapPreview();

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnections m_connections; // QuickMatchSignals + PlayerStatsSignals, connected while showing
	Rml::DataModelHandle m_modelHandle;
	size_t m_lastStatusLineCount = 0;

	// One status/chat line (see QuickMatchSignals::statusLine). color is a pre-formatted
	// "rgba(r,g,b,a)" string, same idiom RmlOnlineGameSetupScreen.cpp's colorToCss() produces -- append
	// only, mirrors quickmatchTextWindow's listbox (which only ever grows while the screen is open).
	struct StatusLineModel
	{
		Rml::String text;
		Rml::String color;
	};

	// One ComboBoxNumPlayers (playlist) entry, see QuickMatchData::PlaylistOption.
	struct PlaylistOptionModel
	{
		int index = 0;
		Rml::String label;
	};

	// One ListBoxMapSelect row, see QuickMatchData::MapOption.
	struct MapRowModel
	{
		int index = 0;      // position in m_model.maps this update -- NOT QuickMatchData::MapOption's own index
		Rml::String path;   // QuickMatchData::MapOption::mapPath, kept for saveMapSelections()
		Rml::String label;
		bool selected = false;
		bool used = true;   // grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
	};

	// One disabled-combo entry, see QuickMatchData::ComboOption.
	struct ComboOptionModel
	{
		int value = 0;
		Rml::String label;
	};

	struct Model
	{
		Rml::String title; // StaticTextTitle, GUI:QuickMatchTitle formatted with the NGMP display name

		Rml::Vector<StatusLineModel> statusLines; // ListboxQuickMatch, append-only while the document is open

		Rml::Vector<PlaylistOptionModel> playlists; // ComboBoxNumPlayers (playlist-repurposed)
		int selectedPlaylist = 0;

		Rml::Vector<MapRowModel> maps; // ListBoxMapSelect

		bool showStats = true; // ParentStats (true) vs ParentOptions (false), see isInfoShown()
		Rml::String optionsButtonLabel; // GUI:Setup / GUI:PlayerInfo, formatted in C++ (see onToggleOptions())

		bool startVisible = true;
		bool startEnabled = true;
		bool stopVisible = false;
		bool stopEnabled = true;
		bool widenEnabled = false;
		bool backEnabled = true;

		bool communicatorEnabled = true;
		Rml::String communicatorLabel; // "GUI:Buddies" [+ " [n]"], same format as RmlOnlineGameSetupScreen's

		// ParentStats subset (see PlayerStatsData.h) -- same fields WOLQuickMatchMenu.wnd's ParentStats
		// shows, no total kills/deaths/built or battle honors (those are PopupPlayerInfo.wnd-only).
		Rml::String streakLabelText;
		Rml::String streakValueText;
		Rml::String lossesText;
		Rml::String disconnectsLabelText;
		Rml::String disconnectsValueText;
		Rml::String bestStreakText;
		Rml::String gamesPlayedText;
		Rml::String winsText;
		Rml::String winPercentText;

		bool rankAtMax = false;
		Rml::String rankProgressWidthStyle = "0%";
		Rml::String rankImageName;
		bool showFactionImage = false;
		Rml::String factionImageName;
		Rml::String rankText;

		// ComboBoxLadder/MaxPing/MaxDisconnects/Side/Color -- populated once in populateDisabledOptionCombos()
		// (see QuickMatchActions::getLadderOptions()/etc), shown disabled (QuickMatch.rml).
		Rml::Vector<ComboOptionModel> ladderOptions;
		int ladderSelected = 0;
		Rml::Vector<ComboOptionModel> maxPingOptions;
		int maxPingSelected = 0;
		Rml::Vector<ComboOptionModel> maxDisconnectsOptions;
		int maxDisconnectsSelected = 0;
		Rml::Vector<ComboOptionModel> sideOptions;
		int sideSelected = 0;
		Rml::Vector<ComboOptionModel> colorOptions;
		int colorSelected = 0;

		// Map hover preview (<mappreview>), ports updateMapHoverPreview() (WOLQuickMatchMenu.cpp:248-282).
		bool mapPreviewVisible = false;
		Rml::String mapPreviewXStyle = "0px";
		Rml::String mapPreviewYStyle = "0px";
		Rml::String mapPreviewMapPath; // QuickMatchData::MapOption::mapPath of the hovered row
	} m_model;

	RmlGrowOnlyList<MapRowModel> m_mapRows;

	// Unclamped cursor position from the triggering mouseover/mousemove event; clampMapPreview()
	// re-derives the clamped style strings from these once the preview's real size is known, same
	// idiom as RmlOnlineLobbyScreen's m_playerMenuRawX/Y.
	float m_mapPreviewRawX = 0.0f;
	float m_mapPreviewRawY = 0.0f;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlQuickMatchScreen();
void CloseRmlQuickMatchScreen();
