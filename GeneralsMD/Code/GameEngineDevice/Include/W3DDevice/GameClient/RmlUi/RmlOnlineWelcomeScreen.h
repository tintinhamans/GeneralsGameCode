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

// FILE: RmlOnlineWelcomeScreen.h /////////////////////////////////////////////////
// RmlScreen for Data/UI/OnlineWelcome.rml. Registered for Menus/WOLWelcomeMenu.wnd
// -- the online hub reached after a successful Generals Online login (see
// RmlOnlineLoginScreen::onLoginSucceeded()).
//
// Unlike RmlOnlineLoginScreen this .wnd's own controls are NOT dead under
// GENERALS_ONLINE: WOLWelcomeMenuInit() shows a real, populated screen (title,
// MOTD listbox, num-players text, 12 per-faction win-percent checkboxes, a
// buddies-notification-count button) -- see WOLWelcomeMenu.cpp and
// OnlineWelcomeData.h/OnlineWelcomeActions.h for the widget-agnostic split this
// screen binds into its data model.
//
// Like RmlLanLobbyScreen this never gets a WOLWelcomeMenuInit()/Shutdown()/Update()
// callback (RmlUiScreenRegistry routes the whole placeholder layout), so:
//   - show() rebuilds the whole data model itself (title/MOTD/stats/notification
//     count) instead of WOLWelcomeMenuInit().
//   - hide() has nothing .wnd-equivalent to undo (WOLWelcomeMenuShutdown()'s own
//     body is all fade/animation bookkeeping the .wnd path still owns, plus
//     RaiseGSMessageBox(), which already runs regardless of front end via
//     TheShell's own update loop -- see Shell.cpp).
//   - update() polls OnlineWelcomeActions::consumePendingFullTeardown(), mirroring
//     WOLWelcomeMenuUpdate()'s pending-full-teardown branch exactly.
// The buddy notification count arrives via OnlineWelcomeSignals::notificationsChanged
// (OnlineWelcomeData.h), connected in show() and dropped in hide() -- same lifetime
// pattern as OnlineLoginSignals in OnlineLoginActions.h.
//
// Navigation targets not yet converted to RmlUi (Custom Match -> WOLCustomLobby.wnd,
// Ladder -> WOLLadderScreen.wnd, the Options/Buddies/My Info GameSpyOverlay popups)
// keep working exactly as the .wnd does them: TheShell->push()/GameSpyOpenOverlay()/
// GameSpyToggleOverlay() fall back to their own .wnd layouts, unaffected by this
// screen's registration.
//
// Back/logout replicates the .wnd's live GENERALS_ONLINE buttonBackID body exactly
// (OnlineWelcomeActions::requestLogout() + TheShell->pop()), but -- like
// RmlOnlineLoginScreen/RmlLanLobbyScreen -- does so directly instead of the .wnd's
// deferred fade-then-pop.
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
class RmlOnlineWelcomeScreen : public RmlScreen
{
public:
	static RmlOnlineWelcomeScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack, see header comment above
	virtual void update() override; // pending-full-teardown poll, see header comment above

private:
	RmlOnlineWelcomeScreen() : m_motdRows(m_model.motdLines), m_factionRows(m_model.factionStats) {}

	// Signal targets, connected in show().
	void onNotificationsChanged(int numNotifications);
	void onNumPlayersOnlineChanged(int numPlayersOnline);
	void onPlayerStatsUpdated(const PlayerStatsData &data);

	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQuickMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMyInfo(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCustomMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onLadder(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnection m_notificationsConnection;
	SignalConnection m_numPlayersConnection;
	SignalConnection m_playerStatsConnection;
	Rml::DataModelHandle m_modelHandle;

	// One MOTD listbox line (see OnlineWelcomeData::buildMotdLines()). colorHex is a
	// "rgba(r, g, b, a)" CSS string (0-255 alpha, same convention as RmlSkirmishSetupScreen.cpp's
	// rgbToHex()) bound via data-style-color.
	struct MotdLineModel
	{
		Rml::String text;
		Rml::String colorHex;
		bool isHeading = false;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
	};

	// One "PercentXxx" checkbox (see OnlineWelcomeData::requestFactionWinStats()). icon is the
	// mapped-image name for <mappedimage>; tooltipText is the resolved "SIDE:<side>" TOOLTIPTEXT,
	// bound via data-attr-data-tooltip-text (same dynamic-tooltip pattern as LanGameOptions.rml's
	// row.player_tooltip/row.faction_tooltip).
	struct FactionStatModel
	{
		Rml::String side;
		Rml::String icon;
		Rml::String tooltipText;
		Rml::String text;
		bool used = true; // grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
	};

	struct Model
	{
		Rml::String title;
		Rml::String numPlayersText;
		Rml::String buddiesButtonText;
		Rml::Vector<MotdLineModel> motdLines;
		Rml::Vector<FactionStatModel> factionStats;

		// Community rank panel (see PlayerStatsData.h / WOLWelcomeMenu.wnd's RankBorder cluster).
		// Only the rank-panel fields are shown on this screen, matching the .wnd.
		bool rankAtMax = false;
		Rml::String rankProgressWidthStyle = "0%"; // data-style-width can't bind a raw number
		Rml::String rankImageName;
		bool showFactionImage = false;
		Rml::String factionImageName;
		Rml::String rankText;
	} m_model;

	// Applies a BuildPlayerStatsData() result's rank-panel fields into m_model and dirties them
	// individually (never DirtyAllVariables()).
	void applyPlayerStatsToModel(const PlayerStatsData &data);

	RmlGrowOnlyList<MotdLineModel> m_motdRows;
	RmlGrowOnlyList<FactionStatModel> m_factionRows;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlOnlineWelcomeScreen();
void CloseRmlOnlineWelcomeScreen();
