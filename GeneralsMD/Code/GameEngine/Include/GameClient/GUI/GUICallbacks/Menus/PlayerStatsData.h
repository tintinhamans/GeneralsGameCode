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

// FILE: PlayerStatsData.h ////////////////////////////////////////////////////////
// Widget-agnostic data builder extracted from PopupPlayerInfo.cpp's
// PopulatePlayerInfoWindows() -- same split as OnlineWelcomeData.h: this header
// returns a plain data struct, the .wnd path (PopupPlayerInfo.cpp) still owns its
// own GameWindow lookups / GadgetXxxSetText() calls (findWindow()) plus
// populateBattleHonors(), and RmlOnlineWelcomeScreen binds the same struct into its
// own data model instead.
//
// PopulatePlayerInfoWindows( AsciiString parentWindowName ) is unchanged (still
// declared in PersistentStorageDefs.h) and is still the entry point all three .wnd
// callers (PopupPlayerInfo.wnd, WOLWelcomeMenu.wnd, WOLQuickMatchMenu.wnd) use; it
// now just calls BuildPlayerStatsData() once the async stats reply lands and applies
// the result to whichever parent window it was given.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h" // PSPlayerStats

#include <string>

// Mirrors PopulatePlayerInfoWindows()'s per-widget computation exactly (see PopupPlayerInfo.cpp),
// one field per GameWindow that function used to populate directly. The `stats` copy is kept
// alongside so the widget-side apply can still call populateBattleHonors() the same way (battle
// honors / lastGeneral / gamesInRowWithLastGeneral / challengeMedals all live on PSPlayerStats
// already, no separate fields needed here).
struct PlayerStatsData
{
	// StaticTextPlayerStatisticsLabel
	UnicodeString playerStatisticsLabelText;
	// StaticTextGamesPlayedValue
	UnicodeString gamesPlayedText;
	// StaticTextWinsValue
	UnicodeString winsText;
	// StaticTextLossesValue
	UnicodeString lossesText;
#if defined(GENERALS_ONLINE)
	// StaticTextDisconnects -- GO repurposes this label to "World Series Elo:" (only set under
	// GENERALS_ONLINE in the original; the widget-side apply keeps the same #if).
	UnicodeString disconnectsLabelText;
#endif
	// StaticTextDisconnectsValue
	UnicodeString disconnectsValueText;
	// StaticTextBestStreakValue
	UnicodeString bestStreakText;
	// StaticTextStreak -- which fetch key to display (GadgetStaticTextSetText'd verbatim by the
	// original: "GUI:CurrentLossStreak" or "GUI:CurrentWinStreak")
	AsciiString streakLabelKey;
	// StaticTextStreakValue
	UnicodeString streakValueText;
	// StaticTextTotalKillsValue / StaticTextTotalDeathsValue / StaticTextTotalBuiltValue
	UnicodeString totalKillsText;
	UnicodeString totalDeathsText;
	UnicodeString totalBuiltText;
	// StaticTextBuildingsKilledValue / StaticTextBuildingsLostValue / StaticTextBuildingsBuiltValue
	UnicodeString buildingsKilledText;
	UnicodeString buildingsLostText;
	UnicodeString buildingsBuiltText;
	// StaticTextWinPercentValue (div-by-zero guarded, same as the original)
	UnicodeString winPercentText;

	// ProgressBarRank -- hidden entirely at max rank, same as the original
	Bool rankAtMax;
	Real rankProgressPercent; // valid only when !rankAtMax

	// WinRank -- mapped-image name (NewPlayer, or Rank_<Name><Side> via the moved lookupRankImage())
	AsciiString rankImageName;

	// FactionImage -- shown only when pPlayerTemplate && rankPoints, same as the original
	Bool showFactionImage;
	AsciiString factionImageName; // valid only when showFactionImage

	// StaticTextRank (e.g. "Corporal")
	UnicodeString rankText;

	// StaticTextInProgress -- always TRUE (hide) today, since PopulatePlayerInfoWindows()'s reply
	// lambda already returns before this point whenever weHaveStats is FALSE; the original's else
	// branch (GUI:FetchingPlayerInfo) is dead code and stays exactly as dead in the widget-side apply.
	Bool weHaveStats;

	// ListboxInfo (populateBattleHonors), kept as a full copy for the widget-side apply
	PSPlayerStats stats;

	PlayerStatsData();
};

// Mirrors PopulatePlayerInfoWindows()'s per-widget computation exactly, given the already-fetched
// PSPlayerStats. lookupID is the same int64_t PopulatePlayerInfoWindows() itself resolves before
// calling findPlayerStatsByID() (either the local player's id, or PopupPlayerInfo.cpp's own
// lookAtPlayerID module static for the "PopupPlayerInfo.wnd" case) -- it's threaded through here
// only for GetAdditionalDisconnectsFromUserFile( lookupID ), same as the original. lookAtPlayerName
// is PopupPlayerInfo.cpp's own module static (set by SetLookAtPlayer()), used for the
// "PlayerStatistics" label.
#if defined(GENERALS_ONLINE)
PlayerStatsData BuildPlayerStatsData(const PSPlayerStats &stats, int64_t lookupID, const std::string &lookAtPlayerName);
#else
PlayerStatsData BuildPlayerStatsData(const PSPlayerStats &stats, int64_t lookupID, const AsciiString &lookAtPlayerName);
#endif

// Set by the active front end around its own show()/hide(); fired from PopupPlayerInfo.cpp's
// findPlayerStatsByID() reply lambda whenever the looked-up player is the local player, so a
// registry-routed front end (RmlOnlineWelcomeScreen) can live-update its own rank panel the same way
// WOLWelcomeMenu.wnd's community panel does via UpdateLocalPlayerStats(). Null (the default) drops
// the update -- same lifetime pattern as g_onlineWelcomeNotificationsChangedHook in OnlineWelcomeData.h.
extern void (*g_playerStatsUpdatedHook)(const PlayerStatsData &data);
