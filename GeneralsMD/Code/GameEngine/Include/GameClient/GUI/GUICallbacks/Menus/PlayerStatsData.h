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

#include <functional>
#include <string>
#include <vector>

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

// Fetches PSPlayerStats for the local player (same NGMP_OnlineServices_AuthInterface /
// NGMP_OnlineServices_StatsInterface lookup PopulatePlayerInfoWindows() itself does) and invokes
// callback with BuildPlayerStatsData()'s result. No-op (callback never invoked) if the auth/stats
// interfaces aren't available yet, the reply comes back without stats, or TheRankPointValues isn't
// set -- same guards PopulatePlayerInfoWindows()'s reply lambda uses. lookAtPlayerName is passed as
// empty, since callers of this (the local-player-only path) never show playerStatisticsLabelText.
// Kept in this .cpp (rather than the caller) so callers don't need to pull in the NGMP headers.
void RequestLocalPlayerStatsData(std::function<void(const PlayerStatsData &)> callback);

// Set by the active front end around its own show()/hide(); fired from PopupPlayerInfo.cpp's
// findPlayerStatsByID() reply lambda whenever the looked-up player is the local player, so a
// registry-routed front end (RmlOnlineWelcomeScreen) can live-update its own rank panel the same way
// WOLWelcomeMenu.wnd's community panel does via UpdateLocalPlayerStats(). Null (the default) drops
// the update -- same lifetime pattern as g_onlineWelcomeNotificationsChangedHook in OnlineWelcomeData.h.
extern void (*g_playerStatsUpdatedHook)(const PlayerStatsData &data);

// FILE: PlayerStatsData.h (popup-info sharing) /////////////////////////////////
// Widget-agnostic split of PopupPlayerInfo.cpp/PopupPlayerInfo.wnd's own popup logic (look-at
// player, battle honors, logout), so RmlPlayerInfoScreen (RmlUi popup) can reuse it exactly instead
// of re-deriving it from the .wnd path. PopupPlayerInfo.cpp still owns the GameWindow-facing pieces
// (findWindow(), populateBattleHonors()'s listbox insertion, the mouse-hover BattleHonorTooltip()).
///////////////////////////////////////////////////////////////////////////////

// One battle-honor badge, in the exact order populateBattleHonors() inserts them (spacer rows
// aside). honorBit/extraValue are threaded straight into InsertBattleHonor()'s itemData/extra params
// by the .wnd path (BattleHonorTooltip()'s mouse-hover lookup still needs them); tooltipKey is the
// same key BattleHonorTooltip() would resolve for this exact badge/enabled/extraValue combination,
// precomputed here so a data-row front end can bind a static "TOOLTIP:..." key per row.
struct BattleHonorRow
{
	AsciiString imageName;
	AsciiString tooltipKey;
	UnicodeString countText; // empty unless the badge shows a number (streak/domination)
	Bool enabled;
	Int honorBit;
	Int extraValue; // 0 unless the badge's tooltip varies by count (streak/domination)
};

// Mirrors populateBattleHonors()'s per-badge selection exactly (see PopupPlayerInfo.cpp): same 9
// badges, same thresholds, same order. Doesn't touch a GameWindow -- the .wnd path still owns
// inserting these into the listbox (InsertBattleHonor()) and its own mouse-hover tooltip callback.
std::vector<BattleHonorRow> BuildBattleHonorRows(const PSPlayerStats &stats);

// Look-at player id/name, set by SetLookAtPlayer() (PopupPlayerInfo.cpp module statics -- unchanged,
// still the ONE source of truth PopulatePlayerInfoWindows()'s "PopupPlayerInfo.wnd" branch reads).
// Exposed here so a registry-routed front end can read the same target the .wnd popup would show
// without reaching into PopupPlayerInfo.cpp's private statics.
int64_t GetLookAtPlayerID();
#if defined(GENERALS_ONLINE)
std::string GetLookAtPlayerNameUtf8();
#endif

// Fired unconditionally from PopulatePlayerInfoWindows()'s "PopupPlayerInfo.wnd" branch (both the
// local player and another player's info), unlike g_playerStatsUpdatedHook above which only fires
// for the local player. Lets RmlPlayerInfoScreen live-update regardless of whose stats it's showing.
extern void (*g_lookAtPlayerStatsUpdatedHook)(const PlayerStatsData &data);

// Shared body of PopupPlayerInfo.cpp's messageBoxYes() Logout confirmation: logs out of the NGMP
// account and flags the pending full teardown. Callers still own closing their own popup afterwards
// (GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO) is already shared between both front ends).
void PerformPlayerLogout();

// True when GetLookAtPlayerID() is the local player's own id -- same NGMP auth GetUserID() check
// GameSpyPlayerInfoOverlayInit() uses to decide whether to show LOGOUT. Kept here (not
// GameEngineDevice) so RmlPlayerInfoScreen doesn't need its own NGMP_interfaces.h include.
Bool IsLookingAtLocalPlayer();

// RefreshGameListBoxes() + GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO), exactly ButtonClose's
// GBM_SELECTED body (PopupPlayerInfo.cpp). Kept here (not GameEngineDevice) so RmlPlayerInfoScreen
// doesn't need its own GameNetwork/GameSpyOverlay.h / GameNetwork/GameSpy/LobbyUtils.h includes.
void ClosePlayerInfoOverlay();
