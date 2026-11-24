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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell, July 2002
// Description: Player info right-click popup screen
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineMenus.h"

#include "Common/PlayerTemplate.h"
#include "Common/BattleHonors.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/FileSystem.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Display.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "WWDownload/Registry.h"

#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType listboxInfoID = NAMEKEY_INVALID;
static NameKeyType buttonCloseID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
//static NameKeyType buttonOptionsID = NAMEKEY_INVALID;
static NameKeyType buttonSetLocaleID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteAccountID = NAMEKEY_INVALID;
static NameKeyType checkBoxAsianFontID = NAMEKEY_INVALID;
static NameKeyType checkBoxNonAsianFontID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = NULL;
static GameWindow *listboxInfo = NULL;
static GameWindow *buttonClose = NULL;
static GameWindow *buttonBuddies = NULL;
//static GameWindow *buttonbuttonOptions = NULL;
static GameWindow *buttonSetLocale = NULL;
static GameWindow *buttonDeleteAccount = NULL;
static GameWindow *checkBoxAsianFont = NULL;
static GameWindow *checkBoxNonAsianFont = NULL;

static Bool isOverlayActive = false;
static Bool raiseMessageBox = false;
static int64_t lookAtPlayerID = 0;

static std::string lookAtPlayerName;


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


static Int getTotalDisconnectsFromFile(Int playerID)
{
	Int retval = 0;
	if (playerID == 0)
	{
		return 0;
	}

	UserPreferences pref;
	AsciiString userPrefFilename;
	userPrefFilename.format("GeneralsOnline\\MiscPref%d.ini", playerID);
	DEBUG_LOG(("getTotalDisconnectsFromFile - reading stats from file %s", userPrefFilename.str()));
	pref.load(userPrefFilename);

	// if there is a file override, use that data instead.
	if (pref.find("0") != pref.end()) {
		retval = atoi(pref.find("0")->second.str());
	}
	if (pref.find("1") != pref.end()) {
		retval += atoi(pref.find("1")->second.str());
	}
	if (pref.find("2") != pref.end()) {
		retval += atoi(pref.find("2")->second.str());
	}
	if (pref.find("3") != pref.end()) {
		retval += atoi(pref.find("3")->second.str());
	}
	if (pref.find("4") != pref.end()) {
		retval += atoi(pref.find("4")->second.str());
	}
	if (pref.find("5") != pref.end()) {
		retval += atoi(pref.find("5")->second.str());
	}

	return retval;	
}

Int GenOnlineGetAdditionalDisconnectsFromUserFile(Int playerID)
{
	Int retval = getTotalDisconnectsFromFile(playerID);

	if (playerID == 0) {
		return 0;
	}

	// TODO_GENONLINE_STATS:
	/*
	if (TheGameSpyInfo->getAdditionalDisconnects() > 0 && !retval)
	{
		DEBUG_LOG(("Clearing additional disconnects"));
		TheGameSpyInfo->clearAdditionalDisconnects();
	}

	if (TheGameSpyInfo->getAdditionalDisconnects() != -1)
	{
		return TheGameSpyInfo->getAdditionalDisconnects();
	}
	*/

	return retval;
}


void GenOnlineSetLookAtPlayer(int64_t id, UnicodeString nick)
{
	lookAtPlayerID = id;
	lookAtPlayerName = to_utf8(nick.str());
}

//	BATTLE_HONOR_LADDER_CHAMP		= 0x0000001,
//	BATTLE_HONOR_STREAK					= 0x0000002,
//	BATTLE_HONOR_STREAK_5				= 0x0000004,
//	BATTLE_HONOR_STREAK_10			= 0x0000008,
//	BATTLE_HONOR_STREAK_20			= 0x0000010,
//	BATTLE_HONOR_LOYALTY_USA		= 0x0000020,
//	BATTLE_HONOR_LOYALTY_CHINA	= 0x0000040,
//	BATTLE_HONOR_LOYALTY_GLA		= 0x0000060,
//	BATTLE_HONOR_BATTLE_TANK		= 0x0000080,
//	BATTLE_HONOR_AIR_WING				= 0x0000100,
//	BATTLE_HONOR_SPECIAL_FORCES	= 0x0000200,
//	BATTLE_HONOR_ENDURANCE			= 0x0000400,
//	BATTLE_HONOR_CAMPAIGN_USA		= 0x0000800,
//	BATTLE_HONOR_CAMPAIGN_CHINA	= 0x0001000,
//	BATTLE_HONOR_CAMPAIGN_GLA  	= 0x0002000,
//	BATTLE_HONOR_BLITZ5					= 0x0004000,
//	BATTLE_HONOR_BLITZ10				= 0x0008000,
//	BATTLE_HONOR_DOMINATION			= 0x0010000,
//	BATTLE_HONOR_CHALLENGE			= 0x0020000,
//	BATTLE_HONOR_ULTIMATE				= 0x0040000,
//	BATTLE_HONOR_GLOBAL_GENERAL	= 0x0080000,
//	BATTLE_HONOR_DOMINATION_ONLINE = 0x0100000,
//	BATTLE_HONOR_SOLO_CHINA_G		= 0x0200000,
//	BATTLE_HONOR_SOLO_GLA_B			= 0x0400000,
//	BATTLE_HONOR_SOLO_GLA_S			= 0x0800000,
//	BATTLE_HONOR_SOLO_GLA_G			= 0x1000000,

void GenOnlineBattleHonorTooltip(GameWindow *window,
												WinInstanceData *instData,
												UnsignedInt mouse)
{
	// Generals has its own battle honor bits, so only Zero Hour shows the GenOnline honors.
#if RTS_ZEROHOUR
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonors") );
		return;
	}

	Int battleHonor = (Int)(intptr_t)GadgetListBoxGetItemData( window, row, col );
	Int extraValue = (Int)(intptr_t)GadgetListBoxGetItemData( window, row - 1, col );
	if (battleHonor == 0)
	{
		//DEBUG_CRASH(("No Battle Honor in listbox row %d, col %d!", row, col));
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonors") );
		return;
	}
	Real tooltipWidth = 1.5f;
	if (BitIsSet(battleHonor, BATTLE_HONOR_NOT_GAINED))
	{
		if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSADisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChinaDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLADisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_BATTLE_TANK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBattleTankDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_AIR_WING))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorAirWingDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_ENDURANCE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorEnduranceDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSADisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChinaDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLADisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_BLITZ10))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitzDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_FAIR_PLAY))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorFairPlayDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_APOCALYPSE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorApocalypseDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallengeDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_ULTIMATE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorUltimateDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneralDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorChallengeDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_STREAK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, NULL, tooltipWidth );
	}
	else
	{
		if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSA"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChina"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLA"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_BATTLE_TANK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBattleTank"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_AIR_WING))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorAirWing"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_ENDURANCE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorEndurance"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSA"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChina"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLA"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_BLITZ5))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitz5"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_BLITZ10))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitz10"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_FAIR_PLAY))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorFairPlay"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_APOCALYPSE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorApocalypse"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_OFFICERSCLUB))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorOfficersClub"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallenge"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_ULTIMATE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorUltimate"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneral"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorChallenge"), -1, NULL, tooltipWidth );
		else if(BitIsSet(battleHonor, BATTLE_HONOR_STREAK))
		{
			if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak1000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak500"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak100"), -1, NULL, tooltipWidth );
			else if (extraValue >= 25)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak25"), -1, NULL, tooltipWidth );
			else if (extraValue >= 10)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak10"), -1, NULL, tooltipWidth );
			else if (extraValue >= 3)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak3"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitIsSet(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
		{
			if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak1000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak500Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak100Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 25)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak25Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 10)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak10Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 3)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak3Online"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION))
		{
			if (extraValue >= 10000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination10000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination1000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination500"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination100"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
		{
			if (extraValue >= 10000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination10000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination1000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination500Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination100Online"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, NULL, tooltipWidth );
		}
	}
	
#endif
}

static Int rowsToSkip = 0;
void GenOnlineResetBattleHonorInsertion()
{
	rowsToSkip = 0;
}
void GenOnlineInsertBattleHonor(GameWindow *list, const Image *image, Bool enabled, Int itemData, Int& row, Int& column, UnicodeString text = UnicodeString::TheEmptyString, Int extra = 0)
{
#if !defined(GENONLINE_WIDESCREEN)
	Int width = MAX_BATTLE_HONOR_IMAGE_WIDTH * (TheDisplay->getWidth() / 800.0f);
	Int height = MAX_BATTLE_HONOR_IMAGE_HEIGHT * (TheDisplay->getHeight() / 600.0f);
#else
	Int width = MAX_BATTLE_HONOR_IMAGE_WIDTH * (TheDisplay->getWidth() / GENONLINE_WIDESCREEN_X_SCALE);
	Int height = MAX_BATTLE_HONOR_IMAGE_HEIGHT * (TheDisplay->getHeight() / GENONLINE_WIDESCREEN_Y_SCALE);
#endif

	

	static Int enabledColor = 0xFFFFFFFF;
	static Int disabledColor = GameMakeColor(80, 80, 80, 255);
	Int color;
	if (enabled)
		color = enabledColor;
	else
		color = disabledColor;
	
	if (!enabled)
		itemData |= BATTLE_HONOR_NOT_GAINED;

	GadgetListBoxAddEntryImage(list, image, row, column, height, width, TRUE, color);
	GadgetListBoxSetItemData(list, (void *)itemData, row, column );
	GadgetListBoxSetItemData(list, (void *)extra, row - 1, column );

	/*
	** removing text, since every place that adds text has alternate displays of the same thing
	if (!text.isEmpty())
	{
		GadgetListBoxAddEntryText(list, text, GameSpyColor[GSCOLOR_DEFAULT], row+1, column, TRUE );
		GadgetListBoxSetItemData(list, (void *)itemData, row+1, column );
		rowsToSkip++;
	}
	*/

	if(++column >= GadgetListBoxGetNumColumns(list))
	{
		column = 0;
		row = row + 1 + rowsToSkip;
		rowsToSkip = max(rowsToSkip-1, 0);
	}
}

static void populateBattleHonors(const PSPlayerStats& stats, Int battleHonors, Int gamesInRow, Int lastGen, Int challenge, GameWindow *list)
{
	// Generals has its own battle honor bits, so only Zero Hour shows the GenOnline honors.
#if RTS_ZEROHOUR
	if( !list )
		return;

	list->winSetTooltipFunc(GenOnlineBattleHonorTooltip);
	GadgetListBoxReset( list );
	Int column = 0;
	Int row = 0;

	Bool isFairPlayer = FALSE;
	Int numGames = 0;
	Int numDiscons = 0;
	PerGeneralMap::const_iterator it;
	for(it = stats.games.begin(); it != stats.games.end(); ++it)
	{
		numGames += it->second;
	}
	for(it = stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numDiscons += it->second;
	}
	for(it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numDiscons += it->second;
	}
	if (numGames >= 10 && numDiscons * 10 < numGames)
	{
		isFairPlayer = TRUE;
	}

	GenOnlineResetBattleHonorInsertion();
	GadgetListBoxAddEntryImage(list, NULL, 0, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));
	row = 1;

	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("FairPlay"), isFairPlayer,
		BATTLE_HONOR_FAIR_PLAY, row, column);

	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorAirWing"), BitIsSet(battleHonors, BATTLE_HONOR_AIR_WING),
		BATTLE_HONOR_AIR_WING, row, column);
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBattleTank"), BitIsSet(battleHonors, BATTLE_HONOR_BATTLE_TANK),
		BATTLE_HONOR_BATTLE_TANK, row, column);
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Apocalypse"), BitIsSet(battleHonors, BATTLE_HONOR_APOCALYPSE),
		BATTLE_HONOR_APOCALYPSE, row, column);

	// create a spacer for row 2 and start the images on row 3
	GadgetListBoxAddEntryImage(list, NULL, 2, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));
	row = 3;

	if (BitIsSet(battleHonors, BATTLE_HONOR_BLITZ5))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz5"), TRUE,
			BATTLE_HONOR_BLITZ5, row, column);
	}
	else if (BitIsSet(battleHonors, BATTLE_HONOR_BLITZ10))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), TRUE,
			BATTLE_HONOR_BLITZ10, row, column);
	}
	else
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), FALSE,
			BATTLE_HONOR_BLITZ10, row, column);
	}

	// TEST FOR STREAK HONOR
	UnicodeString uStr;
	Int streak = stats.winsInARow;
	uStr.format(L"%10d", streak);
	if (streak >= 1000)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_1000"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 500)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_500"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 100)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_100"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 25)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_G"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 10)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_S"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 3)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), FALSE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}

	// TEST FOR DOMINATION HONOR
	Int totalWins = 0;
	PerGeneralMap::const_iterator pit;
	for(pit = stats.wins.begin(); pit != stats.wins.end(); ++pit)
	{
		totalWins += pit->second;
	}
	uStr.format(L"%10d", totalWins);
	if (totalWins >= 10000)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_10000"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 1000)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_1000"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 500)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_500"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 100)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), FALSE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}

	// TEST FOR GLOBAL GENERAL HONOR
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("GlobalGen"), BitIsSet(battleHonors, BATTLE_HONOR_GLOBAL_GENERAL),
		BATTLE_HONOR_GLOBAL_GENERAL, row, column);

	/*
	Bool isLoyal;
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "america") == 0 && gamesInRow >= 20;
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_USA"), isLoyal,
		BATTLE_HONOR_LOYALTY_USA, row, column);
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "china") == 0 && gamesInRow >= 20;
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_China"), isLoyal,
		BATTLE_HONOR_LOYALTY_CHINA, row, column);
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "gla") == 0 && gamesInRow >= 20;
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_GLA"), isLoyal,
		BATTLE_HONOR_LOYALTY_GLA, row, column);
		*/


	//insertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance"), BitIsSet(battleHonors, BATTLE_HONOR_ENDURANCE),
		//BATTLE_HONOR_ENDURANCE, row, column);

	/*
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_USA"), BitIsSet(battleHonors, BATTLE_HONOR_CAMPAIGN_USA),
		BATTLE_HONOR_CAMPAIGN_USA, row, column);
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_China"), BitIsSet(battleHonors, BATTLE_HONOR_CAMPAIGN_CHINA),
		BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_GLA"), BitIsSet(battleHonors, BATTLE_HONOR_CAMPAIGN_GLA),
		BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	*/

	/*
	if(BitIsSet(challenge, BH_CHALLENGE_MASK_7))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge7"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_6))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge6"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_5))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge5"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_4))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge4"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_3))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge3"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_2))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge2"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_1))
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), FALSE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	*/

	// TODO_GENONLINE_STATS
	bool bPreordered = true;
	//if (TheGameSpyInfo->didPlayerPreorder(stats.id))
	if (bPreordered)
	{
		GenOnlineInsertBattleHonor(list, TheMappedImageCollection->findImageByName("OfficersClub"), TRUE,
			BATTLE_HONOR_OFFICERSCLUB, row, column);
	}
#endif
}

Int GenOnlineGetFavoriteSide( const PSPlayerStats& stats )
{
	PerGeneralMap::const_iterator it;
	Int numGames = 0;
	Int favorite = 0;
	for(it =stats.games.begin(); it != stats.games.end(); ++it)
	{
		if(it->second >= numGames)
		{
			numGames = it->second;
			favorite = it->first;
		}
	}
	if(numGames == 0)
		return -1;
	else if( stats.gamesAsRandom >= numGames )
		return 0;

	return favorite;
}

// TODO_GENONLINE: Calculate and store this on the server too, so the website can show it.
Int GenOnlineCalculateRank( const PSPlayerStats& stats )
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

static GameWindow* findWindow(GameWindow *parent, AsciiString baseWindow, AsciiString gadgetName)
{
	AsciiString fullPath;
	fullPath.format("%s:%s", baseWindow.str(), gadgetName.str());
	GameWindow *res = TheWindowManager->winGetWindowFromId(parent, NAMEKEY(fullPath));
	DEBUG_ASSERTLOG(res, ("Cannot find window %s", fullPath.str()));
	return res;
}

static GameWindow *findPlayerInfoParent(const AsciiString &parentWindowName, int64_t lookupID)
{
	if(parentWindowName == "PopupPlayerInfo.wnd")
	{
		if (lookAtPlayerID != lookupID)
			return NULL;
		return parent;
	}
	if(parentWindowName == "WOLWelcomeMenu.wnd")
		return TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("WOLWelcomeMenu.wnd:WOLWelcomeMenuParent"));
	if(parentWindowName == "WOLQuickMatchMenu.wnd")
		return TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("WOLQuickMatchMenu.wnd:WOLQuickMatchMenuParent"));
	return NULL;
}

void GenOnlinePopulatePlayerInfoWindows( AsciiString parentWindowName )
{
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (pAuthInterface == nullptr || pStatsInterface == nullptr)
	{
		return;
	}

	int64_t localID = pAuthInterface->GetUserID();
	int64_t lookupID = localID;
	if(parentWindowName == "PopupPlayerInfo.wnd")
	{
		lookupID = lookAtPlayerID;
		if (lookAtPlayerID == -1)
			return;
	}
	if (findPlayerInfoParent(parentWindowName, lookupID) == NULL)
		return;

	pStatsInterface->findPlayerStatsByID(lookupID, [=](bool bSuccess, PSPlayerStats stats)
		{
			// The screen can close while the request runs, so the window is looked up again when the answer arrives.
			GameWindow *parentWindow = findPlayerInfoParent(parentWindowName, lookupID);
			if (parentWindow == NULL)
				return;

			Bool weHaveStats = bSuccess;

			// if we don't have the stats from the server, see if we have cached stats
			if (!weHaveStats)
			{
				return;
			}

			if (!TheRankPointValues)
				return;

			Int currentRank = 0;
			Int rankPoints = GenOnlineCalculateRank(stats);
			Int i = 0;
			while (i + 1 < MAX_RANKS && rankPoints >= TheRankPointValues->m_ranks[i + 1])
				++i;
			currentRank = i;

			PerGeneralMap::iterator it;
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

			numDiscons += GenOnlineGetAdditionalDisconnectsFromUserFile(lookupID);

			numGames = numWins + numLosses + numDiscons;

			GameWindow* win = NULL;
			UnicodeString uStr;
			win = findWindow(parentWindow, parentWindowName, "StaticTextPlayerStatisticsLabel");
			if (win)
			{
				AsciiString localeID = "WOL:Locale00";
				if (stats.locale >= LOC_MIN && stats.locale <= LOC_MAX)
					localeID.format("WOL:Locale%2.2d", stats.locale);

				// GenOnline: The name shows without its locale.
				uStr.format(L"%s", from_utf8(lookAtPlayerName).c_str());
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextGamesPlayedValue");
			if (win)
			{
				uStr.format(L"%d (%d QM)", numGames, stats.elo_num_matches);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextWinsValue");
			if (win)
			{
				uStr.format(L"%d (Elo: %d)", numWins, stats.elo_rating);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextLossesValue");
			if (win)
			{
				uStr.format(L"%d", numLosses);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextDisconnects");
			if (win)
			{
#if defined(WORLD_SERIES_CLIENT)
				uStr.format(L"World Series Elo:");
#else
				uStr.format(L"Monthly Elo:");
#endif
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextDisconnectsValue");
			if (win)
			{
				uStr.format(L"%d", stats.monthly_elo_rating);
				GadgetStaticTextSetText(win, uStr);			
			}

			win = findWindow(parentWindow, parentWindowName, "StaticTextBestStreakValue");
			if (win)
			{
				uStr.format(L"%d", stats.maxWinsInARow);
				GadgetStaticTextSetText(win, uStr);
			}

			win = findWindow(parentWindow, parentWindowName, "StaticTextStreak");
			if (win)
			{
				if (stats.lossesInARow > 0)
				{
					GadgetStaticTextSetText(win, TheGameText->fetch("GUI:CurrentLossStreak"));
				}
				else
				{
					GadgetStaticTextSetText(win, TheGameText->fetch("GUI:CurrentWinStreak"));
				}
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextStreakValue");
			if (win)
			{
				Int streak = max(stats.lossesInARow, stats.winsInARow);
				uStr.format(L"%d", streak);
				GadgetStaticTextSetText(win, uStr);
			}

			win = findWindow(parentWindow, parentWindowName, "StaticTextTotalKillsValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.unitsKilled.begin(); it != stats.unitsKilled.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextTotalDeathsValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.unitsLost.begin(); it != stats.unitsLost.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextTotalBuiltValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.unitsBuilt.begin(); it != stats.unitsBuilt.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextBuildingsKilledValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.buildingsKilled.begin(); it != stats.buildingsKilled.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextBuildingsLostValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.buildingsLost.begin(); it != stats.buildingsLost.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}
			win = findWindow(parentWindow, parentWindowName, "StaticTextBuildingsBuiltValue");
			if (win)
			{
				Int numGames = 0;
				for (it = stats.buildingsBuilt.begin(); it != stats.buildingsBuilt.end(); ++it)
				{
					numGames += it->second;
				}
				uStr.format(L"%d", numGames);
				GadgetStaticTextSetText(win, uStr);
			}

			win = findWindow(parentWindow, parentWindowName, "StaticTextWinPercentValue");
			if (win)
			{
				//GS  prevent divide by zero
				if (numGames > 0)
					uStr.format(TheGameText->fetch("GUI:WinPercent"), REAL_TO_INT(numWins / (Real)numGames * 100.0f));
				else
					uStr.format(TheGameText->fetch("GUI:WinPercent"), 0);
				GadgetStaticTextSetText(win, uStr);
			}

			win = findWindow(parentWindow, parentWindowName, "ProgressBarRank");
			if (win && TheRankPointValues)
			{
				if (currentRank == MAX_RANKS - 1)
				{
					// we've reached the max rank
					win->winHide(TRUE);
				}
				else
				{
					GadgetProgressBarSetProgress(win, 100 * INT_TO_REAL(rankPoints - TheRankPointValues->m_ranks[currentRank]) / (TheRankPointValues->m_ranks[currentRank + 1] - TheRankPointValues->m_ranks[currentRank]));
				}
			}

			//calculate favorite side and rank overlay image
			UnicodeString rankStr; //, sideStr, sideRankStr;
			const PlayerTemplate* pPlayerTemplate = NULL;  //NULL == newbie
			{
				//search all stats for side favorite side (highest numGames)
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

				//		//favorite side  (ex: Toxin, Tank, Stealth, etc.)
				//		AsciiString side;
				//		if( mostGames > 0  &&  pPlayerTemplate != NULL )
				//		{
				//			if( stats.gamesAsRandom >= mostGames )
				//				side = "GUI:Random";
				//			else
				//				side.format("SIDE:%s", pPlayerTemplate->getSide().str());
				//		}
				//
				//		//combined text (Ex: Toxin Corporal)
				//		sideStr = TheGameText->fetch(side);
				//		sideRankStr.format(L"%s - %s", sideStr.str(), rankStr.str() );
			}

			//rank image;  based on rank and primary faction (USA, China, GLA)
			win = findWindow(parentWindow, parentWindowName, "WinRank");
			if (win && TheRankPointValues)
			{
				if (rankPoints == 0 || pPlayerTemplate == NULL)
					win->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("NewPlayer"));
				else
					win->winSetEnabledImage(0, lookupRankImage(pPlayerTemplate->getBaseSide(), currentRank));
				//x		win->setTooltipText(rankStr);  //ex: Corporal
			}

			//sub-faction overlay icon  (ex: Tank General, Toxin General, etc.)
			win = findWindow(parentWindow, parentWindowName, "FactionImage");
			if (win && pPlayerTemplate && TheRankPointValues && rankPoints)
			{
				win->winSetEnabledImage(0, pPlayerTemplate->getGeneralImage());
				//x		win->setTooltipText( sideStr );  //ex: Toxin General
			}

			//favorite side and rank text (Ex: Tank Corporal)
			win = findWindow(parentWindow, parentWindowName, "StaticTextRank");
			if (win)
			{
				GadgetStaticTextSetText(win, rankStr);  //just rank
				//x		win->setTooltipText(sideRankStr);  //ex: Toxin General - Corporal
			}

			win = findWindow(parentWindow, parentWindowName, "StaticTextInProgress");
			if (win)
			{
				if (weHaveStats)
				{
					win->winHide(TRUE);
				}
				else
				{
					win->winHide(FALSE);
					GadgetStaticTextSetText(win, TheGameText->fetch("GUI:FetchingPlayerInfo"));
				}
			}

			win = findWindow(parentWindow, parentWindowName, "ListboxInfo");
			if (win)
			{
				populateBattleHonors(stats, stats.battleHonors, stats.gamesInRowWithLastGeneral, stats.lastGeneral, stats.challengeMedals, win);
			}
		}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
}



//-------------------------------------------------------------------------------------------------
/** Initialize the Overlay */
//-------------------------------------------------------------------------------------------------
void GenOnlinePlayerInfoOverlayInit( WindowLayout *layout, void *userData )
{
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return;
	}

	parentID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:PopupParent" );
	buttonCloseID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonClose" );
	buttonBuddiesID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonCommunicator" );
	listboxInfoID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ListboxInfo" );
	//buttonOptionsID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonOptions" );
	buttonSetLocaleID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonSetLocale" );
	buttonDeleteAccountID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonDeleteAccount" );
	checkBoxAsianFontID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:CheckBoxAsianText" );
	checkBoxNonAsianFontID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:CheckBoxNonAsianText" );

	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonClose = TheWindowManager->winGetWindowFromId( parent,  buttonCloseID);
	buttonBuddies = TheWindowManager->winGetWindowFromId( parent,  buttonBuddiesID);
	listboxInfo = TheWindowManager->winGetWindowFromId( parent,  listboxInfoID);
	//buttonbuttonOptions = TheWindowManager->winGetWindowFromId( parent,  buttonOptionsID);
	buttonSetLocale = TheWindowManager->winGetWindowFromId( parent,  buttonSetLocaleID);
	buttonDeleteAccount = TheWindowManager->winGetWindowFromId( parent,  buttonDeleteAccountID);
	checkBoxAsianFont = TheWindowManager->winGetWindowFromId( parent,  checkBoxAsianFontID);
	checkBoxNonAsianFont = TheWindowManager->winGetWindowFromId( parent,  checkBoxNonAsianFontID);

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parent );

	isOverlayActive = true;

	//GadgetListBoxAddEntryText(listboxInfo, L"Working", GameSpyColor[GSCOLOR_DEFAULT], -1);

	raiseMessageBox = true;
	GenOnlinePopulatePlayerInfoWindows("PopupPlayerInfo.wnd");

	// we're on the myinfo screen
	if(lookAtPlayerID == pAuthInterface->GetUserID())
	{
		//buttonbuttonOptions->winHide(FALSE);
		buttonSetLocale->winHide(TRUE);
		buttonDeleteAccount->winHide(FALSE);
		buttonDeleteAccount->winSetText(UnicodeString(L"LOGOUT"));
		checkBoxAsianFont->winHide(TRUE);
		checkBoxNonAsianFont->winHide(TRUE);
	}
	else
	{
		//buttonbuttonOptions->winHide(TRUE);
		buttonSetLocale->winHide(TRUE);
		buttonDeleteAccount->winHide(TRUE);
		checkBoxAsianFont->winHide(TRUE);
		checkBoxNonAsianFont->winHide(TRUE);
	}

	// TODO_GENONLINE: re-enable social
	buttonBuddies->winHide(true);

	// set the asian check boxes
	CustomMatchPreferences pref;
	GadgetCheckBoxSetChecked(checkBoxAsianFont,!pref.getDisallowAsianText());
	GadgetCheckBoxSetChecked(checkBoxNonAsianFont,!pref.getDisallowNonAsianText());

	OSVERSIONINFO	osvi;
	osvi.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
	if (GetVersionEx(&osvi))
	{
		//check if we're running Win9x variant since they may need different fonts
		if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		{
			if (checkBoxAsianFont)
				checkBoxAsianFont->winEnable(FALSE);
			if (checkBoxNonAsianFont)
				checkBoxNonAsianFont->winEnable(FALSE);
		}
	}

	//TheWindowManager->winSetModal(parent);
}

//-------------------------------------------------------------------------------------------------
/** Overlay shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlinePlayerInfoOverlayShutdown( WindowLayout *layout, void *userData )
{
	// hide menu
	layout->hide( TRUE );

	parent = NULL;

	// our shutdown is complete
	isOverlayActive = false;
}


//-------------------------------------------------------------------------------------------------
/** Overlay update method */
//-------------------------------------------------------------------------------------------------
void GenOnlinePlayerInfoOverlayUpdate( WindowLayout * layout, void *userData)
{
	if (raiseMessageBox)
	{
		RaiseGSMessageBox();
		layout->bringForward();
	}
	raiseMessageBox = false;
}

//-------------------------------------------------------------------------------------------------
/** Overlay input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlinePlayerInfoOverlayInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							(WindowMsgData)buttonClose, buttonCloseID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}
static void messageBoxYes();
//-------------------------------------------------------------------------------------------------
/** Overlay window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlinePlayerInfoOverlaySystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				
				break;
			}

		case GWM_DESTROY:
			{
				break;
			}

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}
		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonCloseID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
				}
				else if (controlID == buttonBuddiesID)
				{
					RefreshGameListBoxes();
					//GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
					GameSpyOpenOverlay( GSOVERLAY_BUDDY );
				}
//				else if (controlID == buttonOptionsID)
//				{
//					RefreshGameListBoxes();
//					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
//					GameSpyOpenOverlay( GSOVERLAY_OPTIONS );
//				}
				else if (controlID == buttonSetLocaleID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
					if (!GameSpyIsOverlayOpen(GSOVERLAY_LOCALESELECT))
						GameSpyOpenOverlay( GSOVERLAY_LOCALESELECT );
					ReOpenPlayerInfo();
				}
				else if (controlID == buttonDeleteAccountID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );

					MessageBoxYesNo(UnicodeString(L"Log Out"), UnicodeString(L"Are you sure you want to log out?"), messageBoxYes, NULL);
				}
				else if (controlID == checkBoxAsianFontID)
				{
					Bool isChecked = !GadgetCheckBoxIsChecked(control);
					CustomMatchPreferences pref;
					pref.setDisallowAsianText(isChecked);
					pref.write();
					if(isChecked && !GadgetCheckBoxIsChecked(checkBoxNonAsianFont))
					{
						GadgetCheckBoxSetChecked(checkBoxNonAsianFont, TRUE);
						CustomMatchPreferences pref;
						pref.setDisallowNonAsianText(FALSE);
						pref.write();
					}

				}
				else if (controlID == checkBoxNonAsianFontID)
				{
					Bool isChecked = !GadgetCheckBoxIsChecked(control);
					CustomMatchPreferences pref;
					pref.setDisallowNonAsianText(isChecked);
					pref.write();				
					if(isChecked && !GadgetCheckBoxIsChecked(checkBoxAsianFont))
					{
						GadgetCheckBoxSetChecked(checkBoxAsianFont, TRUE);
						CustomMatchPreferences pref;
						pref.setDisallowAsianText(FALSE);
						pref.write();
					}
				}

				break;
			}
	
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}

static void messageBoxYes()
{
	// log out of account
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		// The teardown waits for the logout, because it cancels the requests that end the session.
		pAuthInterface->LogoutOfMyAccount([]()
			{
				if (GenOnlineServicesManager::GetInstance() != nullptr)
				{
					GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_LOGOUT);
				}
			});
	}

	

	// and go back
	RefreshGameListBoxes();
	GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);
	
}

