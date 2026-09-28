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
#include "Common/UnicodeString.h"

#include <functional>
#include <vector>

// One line of the WOLWelcomeMenu.wnd MOTD listbox, in the .wnd's own display order (see
// updateNumPlayersOnline()'s live GENERALS_ONLINE body): the "MOTD:NumPlayersHeading" text first
// (isHeading = TRUE, split on '\n', blank lines shown as a single space), then the NGMP MOTD body
// split the same way. Per-line color (GSCOLOR_MOTD_HEADING/GSCOLOR_MOTD, plus the original's
// "\ffffffffText" hex-color-prefix parsing) is intentionally dropped: see OnlineWelcomeData.cpp.
struct OnlineWelcomeMotdLine
{
	UnicodeString text;
	Bool isHeading;
};

// One "PercentXxx" checkbox's text from updateOverallStats()'s live body, in
// g_mapServiceIndexToPlayerTemplateString order -- matches WOLWelcomeMenu.wnd's PercentUSA..
// PercentGLAStealthGeneral controls 1:1 (side is the "WOLWelcomeMenu.wnd:Percent%s" suffix).
struct OnlineWelcomeFactionStat
{
	AsciiString side;
	UnicodeString text;
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
	// registration exactly; forwards live updates into g_onlineWelcomeNotificationsChangedHook below.
	// No-op if the social interface isn't available yet (same guard as the original).
	void registerNotificationsHook();
}

// Set by the active front end around its own enter()/leave() (or show()/hide()), same lifetime
// pattern as g_onlineLogin*Hook in OnlineLoginActions.h -- exactly one front end owns
// WOLWelcomeMenu.wnd's path at a time via RmlUiScreenRegistry, so this is a plain function pointer,
// not an additive delivery hook like g_lanLobby*Hook. Null (the default) drops the update.
extern void (*g_onlineWelcomeNotificationsChangedHook)(int numNotifications);
