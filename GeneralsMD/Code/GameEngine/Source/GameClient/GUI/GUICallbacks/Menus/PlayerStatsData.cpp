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

// FILE: PlayerStatsData.cpp //////////////////////////////////////////////////////
// See PlayerStatsData.h. Extracted from PopupPlayerInfo.cpp's PopulatePlayerInfoWindows() --
// this is the ONE implementation now; PopupPlayerInfo.cpp's PopulatePlayerInfoWindows() calls
// BuildPlayerStatsData() and applies the result to whichever parent window it was given, instead of
// duplicating the per-widget computation. CalculateRank()/lookupRankImage()/rankNames also moved
// here from PopupPlayerInfo.cpp (CalculateRank is still declared in RankPointValue.h; the other two
// are file-local, only BuildPlayerStatsData() needs them).
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/PlayerStatsData.h"

#include "Common/BattleHonors.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h" // LOC_MIN/LOC_MAX
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/RankPointValue.h"

#if defined(GENERALS_ONLINE)
#include "GameNetwork/GeneralsOnline/NGMP_include.h" // from_utf8()
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h" // EGOTearDownReason
#endif

namespace PlayerStatsSignals
{
	Signal1<const PlayerStatsData &> &localPlayerUpdated() { static Signal1<const PlayerStatsData &> s; return s; }
	Signal1<const PlayerStatsData &> &lookAtPlayerUpdated() { static Signal1<const PlayerStatsData &> s; return s; }
}

// SetLookAtPlayer()'s own module statics (PopupPlayerInfo.cpp), no longer file-static there so the
// accessors below can read the same ONE source of truth instead of duplicating it.
extern int64_t g_lookAtPlayerID;
extern std::string g_lookAtPlayerName;

int64_t GetLookAtPlayerID()
{
	return g_lookAtPlayerID;
}

#if defined(GENERALS_ONLINE)
std::string GetLookAtPlayerNameUtf8()
{
	return g_lookAtPlayerName;
}
#endif

//-------------------------------------------------------------------------------------------------
void PerformPlayerLogout()
{
#if defined(GENERALS_ONLINE)
	// Same body as PopupPlayerInfo.cpp's messageBoxYes(), minus the popup-closing bits (callers
	// already share GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO) for that).
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr)
	{
		pAuthInterface->LogoutOfMyAccount();

		if (NGMP_OnlineServicesManager::GetInstance() != nullptr)
		{
			NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_LOGOUT);
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
Bool IsLookingAtLocalPlayer()
{
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface == nullptr)
		return FALSE;
	return g_lookAtPlayerID == pAuthInterface->GetUserID();
}

// ClosePlayerInfoOverlay() is defined in PopupPlayerInfo.cpp, not here: it needs
// GameNetwork/GameSpy/LobbyUtils.h (RefreshGameListBoxes()), which needs WinInstanceData
// (GameClient/GameWindow.h) -- this .cpp is intentionally GameWindow-free (see file header comment).

//-------------------------------------------------------------------------------------------------
PlayerStatsData::PlayerStatsData()
	: rankAtMax(FALSE)
	, rankProgressPercent(0.0f)
	, showFactionImage(FALSE)
	, weHaveStats(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
// Moved from PopupPlayerInfo.cpp verbatim (was a static local there).
static const char *const rankNames[] = {
	"Private",
	"Corporal",
	"Sergeant",
	"Lieutenant",
	"Captain",
	"Major",
	"Colonel",
	"General",
	"Brigadier",
	"Commander",
};
static_assert(ARRAY_SIZE(rankNames) == MAX_RANKS, "Incorrect array size");

//-------------------------------------------------------------------------------------------------
// Moved from PopupPlayerInfo.cpp verbatim (was a static local there).
static const Image* lookupRankImage(AsciiString side, Int rank)
{
	if (side.isEmpty())
		return TheMappedImageCollection->findImageByName("NewPlayer");

	if (rank < 0 || rank >= MAX_RANKS)
		return NULL;

	// dirty hack rather than try to get artists to follow a naming convention
	if (side == "USA")
		side = "_USA";
	else if (side == "China")
		side = "_China";
	else if (side == "GLA")
		side = "_GLA";
	else if (side == "Random")
		side = "Elite";

	AsciiString fullImageName;
	fullImageName.format("Rank_%s%s", rankNames[rank], side.str());
	if(strcmp(fullImageName.str(),"Rank_PrivateElite") == 0)
		fullImageName = "Rank";//_Private_Elite";
	const Image *img = TheMappedImageCollection->findImageByName(fullImageName);
	DEBUG_ASSERTCRASH( img, ("Could not load rank image: %s", fullImageName.str()));
	return img;
}

//-------------------------------------------------------------------------------------------------
// Moved from PopupPlayerInfo.cpp verbatim (was a free function there, declared in RankPointValue.h).
// TODO_NGMP: We should calculate this and store it on server side too so we can display on website
Int CalculateRank( const PSPlayerStats& stats )
{
	if(stats.id == 0 || !TheRankPointValues)
		return 0;
	PerGeneralMap::const_iterator it;
	Int rankPoints = 0;
	Int numGames = 0;

	for(it =stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints += (numGames * TheRankPointValues->m_winMultiplier);

	numGames = 0;
	for(it =stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints += (numGames * TheRankPointValues->m_lostMultiplier);

	numGames = 0;
	for(it =stats.duration.begin(); it != stats.duration.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints += (numGames / 60) * TheRankPointValues->m_hourSpentOnlineMultiplier;

	numGames = 0;
	for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numGames += it->second;
	}
	for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints += numGames * TheRankPointValues->m_disconnectMultiplier;

	if(BitIsSet(stats.battleHonors, BATTLE_HONOR_CAMPAIGN_USA | BATTLE_HONOR_CAMPAIGN_CHINA |BATTLE_HONOR_CAMPAIGN_GLA))
	{
		rankPoints += 1 * TheRankPointValues->m_completedSoloCampaigns;
	}

	rankPoints = max(0, rankPoints); // clip off negative values, since discons can push us below 0.

	return rankPoints;
}

//-------------------------------------------------------------------------------------------------
// See PlayerStatsData.h. Callers guarantee weHaveStats and TheRankPointValues are both valid before
// calling this, same two guards PopulatePlayerInfoWindows()'s reply lambda checked before doing any
// of this work in the original.
#if defined(GENERALS_ONLINE)
PlayerStatsData BuildPlayerStatsData(const PSPlayerStats &stats, int64_t lookupID, const std::string &lookAtPlayerName)
#else
PlayerStatsData BuildPlayerStatsData(const PSPlayerStats &stats, int64_t lookupID, const AsciiString &lookAtPlayerName)
#endif
{
	PlayerStatsData data;

	Int currentRank = 0;
	Int rankPoints = CalculateRank(stats);
	Int i = 0;
	while (i + 1 < MAX_RANKS && rankPoints >= TheRankPointValues->m_ranks[i + 1])
		++i;
	currentRank = i;

	PerGeneralMap::const_iterator it;
	Int numWins = 0;
	Int numLosses = 0;
	Int numDiscons = 0;
	Int numGames = 0;
	for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		numWins += it->second;
	}
	for (it = stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		numLosses += it->second;
	}
	for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numDiscons += it->second;
	}
	for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numDiscons += it->second;
	}

	numDiscons += GetAdditionalDisconnectsFromUserFile(lookupID);

	numGames = numWins + numLosses + numDiscons;

	UnicodeString uStr;
	{
		AsciiString localeID = "WOL:Locale00";
		if (stats.locale >= LOC_MIN && stats.locale <= LOC_MAX)
			localeID.format("WOL:Locale%2.2d", stats.locale);

		// NGMP: Dont show the "from <locale> anymore...
#if defined(GENERALS_ONLINE)
		uStr.format(L"%s", from_utf8(lookAtPlayerName).c_str());
#else
		uStr.format(TheGameText->fetch("GUI:PlayerStatistics"), lookAtPlayerName.c_str(), TheGameText->fetch(localeID).str());
#endif
	}
	data.playerStatisticsLabelText = uStr;

#if defined(GENERALS_ONLINE)
	data.gamesPlayedText.format(L"%d (%d QM)", numGames, stats.elo_num_matches);
#else
	data.gamesPlayedText.format(L"%d", numGames);
#endif

#if defined(GENERALS_ONLINE)
	data.winsText.format(L"%d (Elo: %d)", numWins, stats.elo_rating);
#else
	data.winsText.format(L"%d", numWins);
#endif

	data.lossesText.format(L"%d", numLosses);

#if defined(GENERALS_ONLINE)
	data.disconnectsLabelText.format(L"World Series Elo:");
#endif

#if defined(GENERALS_ONLINE)
	data.disconnectsValueText.format(L"%d", stats.monthly_elo_rating);
#else
	data.disconnectsValueText.format(L"%d", numDiscons);
#endif

	data.bestStreakText.format(L"%d", stats.maxWinsInARow);

	if (stats.lossesInARow > 0)
	{
		data.streakLabelKey = "GUI:CurrentLossStreak";
	}
	else
	{
		data.streakLabelKey = "GUI:CurrentWinStreak";
	}

	{
		Int streak = max(stats.lossesInARow, stats.winsInARow);
		data.streakValueText.format(L"%d", streak);
	}

	{
		Int killTotal = 0;
		for (it = stats.unitsKilled.begin(); it != stats.unitsKilled.end(); ++it)
			killTotal += it->second;
		data.totalKillsText.format(L"%d", killTotal);
	}
	{
		Int deathTotal = 0;
		for (it = stats.unitsLost.begin(); it != stats.unitsLost.end(); ++it)
			deathTotal += it->second;
		data.totalDeathsText.format(L"%d", deathTotal);
	}
	{
		Int builtTotal = 0;
		for (it = stats.unitsBuilt.begin(); it != stats.unitsBuilt.end(); ++it)
			builtTotal += it->second;
		data.totalBuiltText.format(L"%d", builtTotal);
	}
	{
		Int buildingsKilledTotal = 0;
		for (it = stats.buildingsKilled.begin(); it != stats.buildingsKilled.end(); ++it)
			buildingsKilledTotal += it->second;
		data.buildingsKilledText.format(L"%d", buildingsKilledTotal);
	}
	{
		Int buildingsLostTotal = 0;
		for (it = stats.buildingsLost.begin(); it != stats.buildingsLost.end(); ++it)
			buildingsLostTotal += it->second;
		data.buildingsLostText.format(L"%d", buildingsLostTotal);
	}
	{
		Int buildingsBuiltTotal = 0;
		for (it = stats.buildingsBuilt.begin(); it != stats.buildingsBuilt.end(); ++it)
			buildingsBuiltTotal += it->second;
		data.buildingsBuiltText.format(L"%d", buildingsBuiltTotal);
	}

	//GS  prevent divide by zero
	if (numGames > 0)
		data.winPercentText.format(TheGameText->fetch("GUI:WinPercent"), REAL_TO_INT(numWins / (Real)numGames * 100.0f));
	else
		data.winPercentText.format(TheGameText->fetch("GUI:WinPercent"), 0);

	if (currentRank == MAX_RANKS - 1)
	{
		// we've reached the max rank
		data.rankAtMax = TRUE;
	}
	else
	{
		data.rankAtMax = FALSE;
		data.rankProgressPercent = 100 * INT_TO_REAL(rankPoints - TheRankPointValues->m_ranks[currentRank]) / (TheRankPointValues->m_ranks[currentRank + 1] - TheRankPointValues->m_ranks[currentRank]);
	}

	//calculate favorite side and rank overlay image
	UnicodeString rankStr; //, sideStr, sideRankStr;
	const PlayerTemplate* pPlayerTemplate = NULL;  //NULL == newbie
	{	//search all stats for side favorite side (highest numGames)
		Int mostGames = 0;
		Int favorite = 0;
		for (it = stats.games.begin(); it != stats.games.end(); ++it)
		{
			if (it->second >= mostGames)
			{
				mostGames = it->second;
				favorite = it->first;
			}
		}
		if (mostGames > 0)
			pPlayerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);

		//rank (ex: Corporal)
		AsciiString rank;
		rank.format("GUI:GSRank%d", currentRank);
		rankStr = TheGameText->fetch(rank);
	}

	//rank image;  based on rank and primary faction (USA, China, GLA)
	if (rankPoints == 0 || pPlayerTemplate == NULL)
	{
		data.rankImageName = "NewPlayer";
	}
	else
	{
		const Image *img = lookupRankImage(pPlayerTemplate->getBaseSide(), currentRank);
		data.rankImageName = img ? img->getName() : AsciiString::TheEmptyString;
	}

	//sub-faction overlay icon  (ex: Tank General, Toxin General, etc.)
	data.showFactionImage = (pPlayerTemplate != NULL && rankPoints != 0);
	if (data.showFactionImage)
	{
		const Image *img = pPlayerTemplate->getGeneralImage();
		data.factionImageName = img ? img->getName() : AsciiString::TheEmptyString;
	}

	//favorite side and rank text (Ex: Tank Corporal)
	data.rankText = rankStr;  //just rank

	// PopulatePlayerInfoWindows()'s reply lambda already returned before this point whenever
	// weHaveStats was FALSE, so this is always TRUE here; kept as a field (rather than a hardcoded
	// TRUE at the call site) so the widget-side apply's original if/else -- including its dead
	// GUI:FetchingPlayerInfo else branch -- can stay exactly as it was.
	data.weHaveStats = TRUE;

	data.stats = stats;

	return data;
}

//-------------------------------------------------------------------------------------------------
// Mirrors BattleHonorTooltip()'s (PopupPlayerInfo.cpp) key selection for exactly the 9 badges
// populateBattleHonors() inserts -- the badges the original's commented-out branches (Loyalty,
// Endurance, Campaign, Challenge) never reach aren't needed here.
static AsciiString getBattleHonorTooltipKey(Int honorBit, Bool enabled, Int extraValue)
{
	if (!enabled)
	{
		if (BitIsSet(honorBit, BATTLE_HONOR_FAIR_PLAY))
			return "TOOLTIP:BattleHonorFairPlayDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_AIR_WING))
			return "TOOLTIP:BattleHonorAirWingDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_BATTLE_TANK))
			return "TOOLTIP:BattleHonorBattleTankDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_APOCALYPSE))
			return "TOOLTIP:BattleHonorApocalypseDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_BLITZ10))
			return "TOOLTIP:BattleHonorBlitzDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_STREAK_ONLINE))
			return "TOOLTIP:BattleHonorStreakOnlineDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_DOMINATION_ONLINE))
			return "TOOLTIP:BattleHonorDominationOnlineDisabled";
		if (BitIsSet(honorBit, BATTLE_HONOR_GLOBAL_GENERAL))
			return "TOOLTIP:BattleHonorGlobalGeneralDisabled";
	}
	else
	{
		if (BitIsSet(honorBit, BATTLE_HONOR_FAIR_PLAY))
			return "TOOLTIP:BattleHonorFairPlay";
		if (BitIsSet(honorBit, BATTLE_HONOR_AIR_WING))
			return "TOOLTIP:BattleHonorAirWing";
		if (BitIsSet(honorBit, BATTLE_HONOR_BATTLE_TANK))
			return "TOOLTIP:BattleHonorBattleTank";
		if (BitIsSet(honorBit, BATTLE_HONOR_APOCALYPSE))
			return "TOOLTIP:BattleHonorApocalypse";
		if (BitIsSet(honorBit, BATTLE_HONOR_BLITZ5))
			return "TOOLTIP:BattleHonorBlitz5";
		if (BitIsSet(honorBit, BATTLE_HONOR_BLITZ10))
			return "TOOLTIP:BattleHonorBlitz10";
		if (BitIsSet(honorBit, BATTLE_HONOR_OFFICERSCLUB))
			return "TOOLTIP:BattleHonorOfficersClub";
		if (BitIsSet(honorBit, BATTLE_HONOR_GLOBAL_GENERAL))
			return "TOOLTIP:BattleHonorGlobalGeneral";
		if (BitIsSet(honorBit, BATTLE_HONOR_STREAK_ONLINE))
		{
			if (extraValue >= 1000) return "TOOLTIP:BattleHonorStreak1000Online";
			if (extraValue >= 500) return "TOOLTIP:BattleHonorStreak500Online";
			if (extraValue >= 100) return "TOOLTIP:BattleHonorStreak100Online";
			if (extraValue >= 25) return "TOOLTIP:BattleHonorStreak25Online";
			if (extraValue >= 10) return "TOOLTIP:BattleHonorStreak10Online";
			if (extraValue >= 3) return "TOOLTIP:BattleHonorStreak3Online";
			return "TOOLTIP:BattleHonorStreakOnlineDisabled";
		}
		if (BitIsSet(honorBit, BATTLE_HONOR_DOMINATION_ONLINE))
		{
			if (extraValue >= 10000) return "TOOLTIP:BattleHonorDomination10000Online";
			if (extraValue >= 1000) return "TOOLTIP:BattleHonorDomination1000Online";
			if (extraValue >= 500) return "TOOLTIP:BattleHonorDomination500Online";
			if (extraValue >= 100) return "TOOLTIP:BattleHonorDomination100Online";
			return "TOOLTIP:BattleHonorDominationOnlineDisabled";
		}
	}
	return "TOOLTIP:BattleHonors";
}

//-------------------------------------------------------------------------------------------------
// Mirrors populateBattleHonors() (PopupPlayerInfo.cpp) exactly: same 9 badges, same order, same
// thresholds. See PlayerStatsData.h.
std::vector<BattleHonorRow> BuildBattleHonorRows(const PSPlayerStats &stats)
{
	std::vector<BattleHonorRow> rows;
	rows.reserve(9);

	PerGeneralMap::const_iterator it;

	Bool isFairPlayer = FALSE;
	{
		Int numGames = 0, numDiscons = 0;
		for (it = stats.games.begin(); it != stats.games.end(); ++it)
			numGames += it->second;
		for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
			numDiscons += it->second;
		for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
			numDiscons += it->second;
		if (numGames >= 10 && numDiscons * 10 < numGames)
			isFairPlayer = TRUE;
	}

	auto addRow = [&rows](const char *image, Bool enabled, Int honorBit, UnicodeString countText, Int extraValue)
	{
		BattleHonorRow row;
		row.imageName = image;
		row.enabled = enabled;
		row.honorBit = honorBit;
		row.countText = countText;
		row.extraValue = extraValue;
		row.tooltipKey = getBattleHonorTooltipKey(honorBit, enabled, extraValue);
		rows.push_back(row);
	};

	addRow("FairPlay", isFairPlayer, BATTLE_HONOR_FAIR_PLAY, UnicodeString::TheEmptyString, 0);
	addRow("HonorAirWing", BitIsSet(stats.battleHonors, BATTLE_HONOR_AIR_WING), BATTLE_HONOR_AIR_WING, UnicodeString::TheEmptyString, 0);
	addRow("HonorBattleTank", BitIsSet(stats.battleHonors, BATTLE_HONOR_BATTLE_TANK), BATTLE_HONOR_BATTLE_TANK, UnicodeString::TheEmptyString, 0);
	addRow("Apocalypse", BitIsSet(stats.battleHonors, BATTLE_HONOR_APOCALYPSE), BATTLE_HONOR_APOCALYPSE, UnicodeString::TheEmptyString, 0);

	if (BitIsSet(stats.battleHonors, BATTLE_HONOR_BLITZ5))
		addRow("HonorBlitz5", TRUE, BATTLE_HONOR_BLITZ5, UnicodeString::TheEmptyString, 0);
	else if (BitIsSet(stats.battleHonors, BATTLE_HONOR_BLITZ10))
		addRow("HonorBlitz10", TRUE, BATTLE_HONOR_BLITZ10, UnicodeString::TheEmptyString, 0);
	else
		addRow("HonorBlitz10", FALSE, BATTLE_HONOR_BLITZ10, UnicodeString::TheEmptyString, 0);

	{
		Int streak = stats.winsInARow;
		UnicodeString uStr;
		uStr.format(L"%10d", streak);
		if (streak >= 1000)
			addRow("HonorStreak_1000", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else if (streak >= 500)
			addRow("HonorStreak_500", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else if (streak >= 100)
			addRow("HonorStreak_100", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else if (streak >= 25)
			addRow("HonorStreak_G", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else if (streak >= 10)
			addRow("HonorStreak_S", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else if (streak >= 3)
			addRow("HonorStreak_B", TRUE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
		else
			addRow("HonorStreak_B", FALSE, BATTLE_HONOR_STREAK_ONLINE, uStr, streak);
	}

	{
		Int totalWins = 0;
		for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
			totalWins += it->second;
		UnicodeString uStr;
		uStr.format(L"%10d", totalWins);
		if (totalWins >= 10000)
			addRow("Domination_10000", TRUE, BATTLE_HONOR_DOMINATION_ONLINE, uStr, totalWins);
		else if (totalWins >= 1000)
			addRow("Domination_1000", TRUE, BATTLE_HONOR_DOMINATION_ONLINE, uStr, totalWins);
		else if (totalWins >= 500)
			addRow("Domination_500", TRUE, BATTLE_HONOR_DOMINATION_ONLINE, uStr, totalWins);
		else if (totalWins >= 100)
			addRow("Domination_100", TRUE, BATTLE_HONOR_DOMINATION_ONLINE, uStr, totalWins);
		else
			addRow("Domination_100", FALSE, BATTLE_HONOR_DOMINATION_ONLINE, uStr, totalWins);
	}

	addRow("GlobalGen", BitIsSet(stats.battleHonors, BATTLE_HONOR_GLOBAL_GENERAL), BATTLE_HONOR_GLOBAL_GENERAL, UnicodeString::TheEmptyString, 0);

	// TODO_NGMP_STATS: always TRUE today, same as populateBattleHonors()'s bPreordered local.
	addRow("OfficersClub", TRUE, BATTLE_HONOR_OFFICERSCLUB, UnicodeString::TheEmptyString, 0);

	return rows;
}

//-------------------------------------------------------------------------------------------------
void RequestLocalPlayerStatsData(std::function<void(const PlayerStatsData &)> callback)
{
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	NGMP_OnlineServices_StatsInterface *pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	if (pAuthInterface == nullptr || pStatsInterface == nullptr)
		return;

	int64_t localID = pAuthInterface->GetUserID();
	pStatsInterface->findPlayerStatsByID(localID, [callback, localID](bool bSuccess, PSPlayerStats stats)
		{
			if (!bSuccess || !TheRankPointValues)
				return;

#if defined(GENERALS_ONLINE)
			PlayerStatsData data = BuildPlayerStatsData(stats, localID, std::string());
#else
			PlayerStatsData data = BuildPlayerStatsData(stats, localID, AsciiString::TheEmptyString);
#endif
			callback(data);
		}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
}
