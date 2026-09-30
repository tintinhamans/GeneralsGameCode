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

// FILE: RmlPlayerInfoScreen.h //////////////////////////////////////////////////
// RmlScreen for Data/UI/PlayerInfo.rml -- the player info right-click popup
// (PopupPlayerInfo.wnd / GSOVERLAY_PLAYERINFO). Like RmlQuitMenuScreen this is a
// popup, not a shell screen: it stays up OVER whatever's showing underneath
// (RmlOnlineWelcomeScreen or WOLQuickMatchMenu.wnd) instead of going through
// RmlUiManager::showScreen()'s single-current-screen swap, so it loads/shows/
// hides its own document directly.
//
// GameSpyOverlay.cpp's GameSpyOpenOverlay()/GameSpyCloseOverlay()/
// GameSpyIsOverlayOpen() route GSOVERLAY_PLAYERINFO through
// RmlUiScreenRegistry::open/close("Menus/PopupPlayerInfo.wnd") whenever it's
// registered (same isRegistered() gate QuitMenu.cpp's ToggleQuitMenu() uses),
// so every existing caller (WOLWelcomeMenu's My Info / right-click context menus)
// keeps working unchanged -- SetLookAtPlayer() is always called by the caller
// before the overlay opens, exactly like the .wnd path.
//
// This screen owns none of PopupPlayerInfo.cpp's GameWindow lookups; it reads the
// same shared, widget-agnostic data PlayerStatsData.h exposes (BuildPlayerStatsData,
// BuildBattleHonorRows, GetLookAtPlayerID, PerformPlayerLogout) instead. Since no
// GameWindow parent exists for the "PopupPlayerInfo.wnd" case when this screen (not
// the .wnd) owns the popup, open() calls PopulatePlayerInfoWindows() itself to kick
// off the async stats fetch (see PopupPlayerInfo.cpp's PopulatePlayerInfoWindows()
// comment) and connects to PlayerStatsSignals::lookAtPlayerUpdated for the reply.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "GameClient/GUI/GUICallbacks/Menus/PlayerStatsData.h"
#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlPlayerInfoScreen
{
public:
	static RmlPlayerInfoScreen &instance();
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as ButtonClose (KEY_ESC in PopupPlayerInfo.cpp)

private:
	RmlPlayerInfoScreen() : m_honorRows(m_model.battleHonors) {}

	// PlayerStatsSignals::lookAtPlayerUpdated target (see PlayerStatsData.h), connected in open().
	void onPlayerStatsUpdated(const PlayerStatsData &data);

	void load(Rml::Context *context);

	void onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onLogout(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnection m_playerStatsConnection;
	Rml::DataModelHandle m_modelHandle;

	// One battle-honor badge (see BattleHonorRow, PlayerStatsData.h). tooltipText is already
	// resolved (TheGameText->fetch(row.tooltipKey)), same "resolve in C++, bind the string" pattern
	// as LanLobby.rml's player.tooltip / OnlineWelcome.rml's stat.tooltip_text.
	struct BattleHonorRowModel
	{
		Rml::String imageName;
		Rml::String tooltipText;
		Rml::String countText;
		bool hasCount = false; // countText non-empty (streak/domination badges only)
		bool enabled = false;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
	};

	struct Model
	{
		Rml::String title; // player display name (or "Player Statistics" while loading)
		bool statsLoading = true; // StaticTextInProgress, see PlayerStatsData.h's weHaveStats
		bool showLogout = false; // true only when looking at the local player, same as the .wnd

		Rml::String gamesPlayedText;
		Rml::String winsText;
		Rml::String lossesText;
		Rml::String disconnectsLabelText;
		Rml::String disconnectsValueText;
		Rml::String bestStreakText;
		Rml::String streakLabelText;
		Rml::String streakValueText;
		Rml::String totalKillsText;
		Rml::String totalDeathsText;
		Rml::String totalBuiltText;
		Rml::String buildingsKilledText;
		Rml::String buildingsLostText;
		Rml::String buildingsBuiltText;
		Rml::String winPercentText;

		bool rankAtMax = false;
		Rml::String rankProgressWidthStyle = "0%";
		Rml::String rankImageName;
		bool showFactionImage = false;
		Rml::String factionImageName;
		Rml::String rankText;

		Rml::Vector<BattleHonorRowModel> battleHonors;
	} m_model;

	RmlGrowOnlyList<BattleHonorRowModel> m_honorRows;
};

// Registry entry point (see RmlUiManager::init() / GameSpyOverlay.cpp).
void OpenRmlPlayerInfoScreen();
void CloseRmlPlayerInfoScreen();
