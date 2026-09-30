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

// FILE: OnlineWelcomeData.h /////////////////////////////////////////////////////
// Widget-agnostic data builders extracted from WOLWelcomeMenu.cpp's live
// GENERALS_ONLINE code path (updateNumPlayersOnline(), updateOverallStats(), the
// staticTextTitle body in WOLWelcomeMenuInit(), and the buddy-notifications
// callback also registered there). Same split as LanLobbyData.h/OnlineLoginActions.h:
// this header returns plain strings/structs, the .wnd path still owns its own
// GameWindow lookups and GadgetXxxSetText() calls, and RmlOnlineWelcomeScreen binds
// the same builders into its data model.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/Signal.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"

#include <functional>
#include <vector>

// One line of the WOLWelcomeMenu.wnd MOTD listbox, in the .wnd's own display order (see
// updateNumPlayersOnline()'s live GENERALS_ONLINE body): the "MOTD:NumPlayersHeading" text first
// (isHeading = TRUE, split on '\n', blank lines shown as a single space), then the NGMP MOTD body
// split the same way. color mirrors the original's per-line GameSpyColor[GSCOLOR_MOTD_HEADING] /
// GameSpyColor[GSCOLOR_MOTD], including the original's "\ffffffffText" hex-color-prefix override for
// body lines (see OnlineWelcomeData.cpp's buildMotdLines()).
struct OnlineWelcomeMotdLine
{
	UnicodeString text;
	Bool isHeading;
	Color color;
};

// One "PercentXxx" checkbox's text from updateOverallStats()'s live body, in
// g_mapServiceIndexToPlayerTemplateString order -- matches WOLWelcomeMenu.wnd's PercentUSA..
// PercentGLAStealthGeneral controls 1:1 (side is the "WOLWelcomeMenu.wnd:Percent%s" suffix).
// icon is that control's ENABLEDDRAWDATA mapped-image name (e.g. "USA_Logo", "USA_Superweapon"), from
// the .wnd's own per-control image, not derivable from side by a naming rule. tooltip is the fetched
// "SIDE:<side>" TOOLTIPTEXT (see buildFactionStatTooltipKey()).
struct OnlineWelcomeFactionStat
{
	AsciiString side;
	AsciiString icon;
	UnicodeString text;
	UnicodeString tooltip;
};

namespace OnlineWelcomeData
{
	// Mirrors WOLWelcomeMenuInit()'s GENERALS_ONLINE staticTextTitle body exactly ("Welcome to
	// Generals Online, %s"). Returns an empty string if the auth interface isn't available yet, same
	// condition the original guarded the GadgetStaticTextSetText() call with.
	UnicodeString buildWelcomeTitle();

	// Mirrors updateNumPlayersOnline()'s listboxInfo-building body exactly for the GENERALS_ONLINE MOTD
	// source (NGMP_OnlineServicesManager::GetInstance()->GetMOTD()).
	std::vector<OnlineWelcomeMotdLine> buildMotdLines();

	// Mirrors updateNumPlayersOnline()'s "GUI:NumPlayersOnline" StaticTextNumPlayersOnline body.
	UnicodeString buildNumPlayersOnlineText(Int numPlayersOnline);

	// Player count to seed the text with when the screen opens: the last HandleNumPlayersOnline()
	// delivery (it can arrive while the login screen is still up, before any welcome listener is
	// installed), else the count the server states in the MOTD, else 0.
	Int currentNumPlayersOnline();

	// Whether the client is logged in to Generals Online (news and the player count are only current then).
	Bool isLoggedIn();

	// The logged-in player's display name, or an empty string if the auth interface isn't available.
	UnicodeString localDisplayName();

	// Mirrors WOLWelcomeMenu.wnd's "SIDE:<side>" TOOLTIPTEXT for a PercentXxx checkbox, given the same
	// side string buildMotdLines()/requestFactionWinStats() key off. Index 0 ("USA") is the one
	// mismatch between the control-name suffix and the tooltip's side literal (control is PercentUSA,
	// tooltip is SIDE:America) -- see g_mapServiceIndexToPlayerTemplateString's own comment.
	AsciiString buildFactionStatTooltipKey(const AsciiString &side);

	// Mirrors updateOverallStats()'s live body exactly, including its divide-by-zero guards. Async:
	// invokes callback once NGMP_OnlineServices_StatsInterface::GetGlobalStats() replies, matching the
	// original's GadgetCheckBoxSetText() calls happening inside that same reply lambda. No-op (callback
	// never invoked) if the stats interface isn't available yet, matching the original's early return.
	void requestFactionWinStats(std::function<void(std::vector<OnlineWelcomeFactionStat>)> callback);

	// Mirrors the buddy-notifications callback's buttonText formatting exactly (both the "%s [%d]" and
	// plain "%s" cases).
	UnicodeString buildBuddiesButtonText(Int numNotifications);

	// Mirrors WOLWelcomeMenuInit()'s pSocialInterface->GetNumTotalNotifications() initial read. Returns
	// 0 if the social interface isn't available yet.
	Int getCurrentNotificationCount();

	// Mirrors WOLWelcomeMenuInit()'s pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged
	// registration exactly; forwards live updates into OnlineWelcomeSignals::notificationsChanged below.
	// No-op if the social interface isn't available yet (same guard as the original).
	void registerNotificationsCallback();
}

// Live updates for the active front end (WOLWelcomeMenu.wnd or RmlOnlineWelcomeScreen): each connects
// around its own init/show and drops the connection on shutdown/hide, same lifetime pattern as
// OnlineLoginSignals in OnlineLoginActions.h.
namespace OnlineWelcomeSignals
{
	// Buddy notification count, forwarded from OnlineWelcomeData::registerNotificationsCallback().
	Signal1<int> &notificationsChanged();

	// Server player count, fired from WOLWelcomeMenu.cpp's HandleNumPlayersOnline() (the live NGMP -> UI
	// delivery path) so a registry-routed front end can live-update its own "GUI:NumPlayersOnline" text
	// the same way the .wnd's StaticTextNumPlayersOnline does.
	Signal1<int> &numPlayersOnline();
}

// Last count HandleNumPlayersOnline() received (already floored at 1), 0 if none yet.
Int GetLastNumPlayersOnline();
