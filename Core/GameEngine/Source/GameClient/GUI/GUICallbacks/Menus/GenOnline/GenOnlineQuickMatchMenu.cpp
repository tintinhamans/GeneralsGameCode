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
// FILE: GenOnlineQuickMatchMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineMenus.h"

#include "Common/GameEngine.h"
#include "Common/GOUIColors.h"
#include "Common/QuickmatchPreferences.h"
#include "Common/LadderPreferences.h"
#include "Common/MultiplayerSettings.h"
#include "Common/OptionPreferences.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"

#include "GameLogic/GameLogic.h"

#include "GameNetwork/NAT.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/LadderDefs.h"

#ifdef DEBUG_LOGGING
#include "Common/MiniLog.h"
//#define PERF_TEST
static LogClass s_perfLog("QMPerf.txt");
static Bool s_inQM = FALSE;
#define PERF_LOG(x) s_perfLog.log x
#else // DEBUG_LOGGING
#define PERF_LOG(x)
#endif // DEBUG_LOGGING

#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/GenOnline/GenOnlineGame.h"
extern GenOnlineGame* TheGenOnlineGame;
#include "GameNetwork/GenOnline/GenOnlineMatchmakingInterface.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineSocialInterface.h"
#include "GameNetwork/GenOnline/GenOnlineStatsInterface.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLQuickMatchID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonStopID = NAMEKEY_INVALID;
static NameKeyType buttonWidenID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
static NameKeyType listboxQuickMatchID = NAMEKEY_INVALID;
static NameKeyType listboxMapSelectID = NAMEKEY_INVALID;
static NameKeyType buttonSelectAllMapsID = NAMEKEY_INVALID;
static NameKeyType buttonSelectNoMapsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMaxDisconnectsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMaxPointsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMinPointsID = NAMEKEY_INVALID;
static NameKeyType textEntryWaitTimeID = NAMEKEY_INVALID;
static NameKeyType comboBoxNumPlayersID = NAMEKEY_INVALID;
static NameKeyType comboBoxMaxPingID = NAMEKEY_INVALID;
static NameKeyType comboBoxLadderID = NAMEKEY_INVALID;
static NameKeyType comboBoxMaxDisconnectsID = NAMEKEY_INVALID;
static NameKeyType staticTextNumPlayersID = NAMEKEY_INVALID;
static NameKeyType comboBoxSideID = NAMEKEY_INVALID;
static NameKeyType comboBoxColorID = NAMEKEY_INVALID;


// Bumped on Init/Shutdown; async callbacks bail if this changed before they fire.
static uint64_t s_quickMatchMenuGeneration = 0;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLQuickMatch = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonStop = nullptr;
static GameWindow *buttonWiden = nullptr;
GameWindow *GenOnlineQuickMatchTextWindow = nullptr;
static GameWindow *listboxMapSelect = nullptr;
//static GameWindow *textEntryMaxDisconnects = nullptr;
//static GameWindow *textEntryMaxPoints = nullptr;
//static GameWindow *textEntryMinPoints = nullptr;
static GameWindow *textEntryWaitTime = nullptr;
static GameWindow *comboBoxNumPlayers = nullptr;
static GameWindow *comboBoxMaxPing = nullptr;
static GameWindow *comboBoxLadder = nullptr;
static GameWindow *comboBoxDisabledLadder = nullptr; // enable and disable this, but never use it.  it is a stand-in for comboBoxLadder for when there are no ladders
static GameWindow *comboBoxMaxDisconnects = nullptr;
static GameWindow *staticTextNumPlayers = nullptr;
static GameWindow *comboBoxSide = nullptr;
static GameWindow *comboBoxColor = nullptr;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = nullptr;
static Bool raiseMessageBoxes = false;
static Bool isInInit = FALSE;
static const Image *selectedImage = nullptr;
static const Image *unselectedImage = nullptr;
static const Image *mapHoverPreview = nullptr;
static GameWinDrawFunc mapListboxPreviewFunc = nullptr;

static bool isPopulatingLadderBox = false;
static Int maxPingEntries = 0;
static Int maxPoints= 100;
static Int minPoints = 0;

static Int matchFoundTimeoutStart = 0;
static const Int lobbyTimeoutMs = 10000;
static Int matchFoundTimeoutDurationMs = lobbyTimeoutMs;
static const Int defaultMatchStartCountdownMs = 5000;
static Int matchStartCountdownDurationMs = defaultMatchStartCountdownMs;
static Int matchStartCountdownLastSecond = 0;

static const LadderInfo * getLadderInfo();

static Bool isInfoShown()
{
	static NameKeyType parentStatsID = NAMEKEY("WOLQuickMatchMenu.wnd:ParentStats");
	GameWindow *parentStats = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, parentStatsID );
	if (parentStats)
		return !parentStats->winIsHidden();
	return FALSE;
}

static void hideInfoGadgets(Bool doIt)
{
	static NameKeyType parentStatsID = NAMEKEY("WOLQuickMatchMenu.wnd:ParentStats");
	GameWindow *parentStats = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, parentStatsID );
	if (parentStats)
	{
		parentStats->winHide(doIt);
	}
}

static void hideOptionsGadgets(Bool doIt)
{
	static NameKeyType parentOptionsID = NAMEKEY("WOLQuickMatchMenu.wnd:ParentOptions");
	GameWindow *parentOptions = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, parentOptionsID );
	if (parentOptions)
	{
		parentOptions->winHide(doIt);
		if (comboBoxSide)
			comboBoxSide->winHide(doIt);
		if (comboBoxColor)
			comboBoxColor->winHide(doIt);
		if (comboBoxNumPlayers)
			comboBoxNumPlayers->winHide(doIt);
		if (comboBoxLadder)
			comboBoxLadder->winHide(doIt);
		if (comboBoxDisabledLadder)
			comboBoxDisabledLadder->winHide(doIt);
		if (comboBoxMaxPing)
			comboBoxMaxPing->winHide(doIt);
		if (comboBoxMaxDisconnects)
			comboBoxMaxDisconnects->winHide(doIt);
	}
}

static void enableOptionsGadgets(Bool doIt)
{
#ifdef PERF_TEST
	s_inQM = !doIt;
#endif // PERF_TEST
	static NameKeyType parentOptionsID = NAMEKEY("WOLQuickMatchMenu.wnd:ParentOptions");
	GameWindow *parentOptions = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, parentOptionsID );
	const LadderInfo *li = getLadderInfo();
	if (parentOptions)
	{
		parentOptions->winEnable(doIt);
		if (comboBoxSide)
			comboBoxSide->winEnable(doIt && (!li || !li->randomFactions));
		if (comboBoxColor)
			comboBoxColor->winEnable(doIt);
		if (comboBoxNumPlayers)
			comboBoxNumPlayers->winEnable(doIt);

	}
}

enum
{
	MAX_DISCONNECTS_ANY = 0,
	MAX_DISCONNECTS_5 = 5,
	MAX_DISCONNECTS_10 = 10,
	MAX_DISCONNECTS_25 = 25,
	MAX_DISCONNECTS_50 = 50,
};
enum{ MAX_DISCONNECTS_COUNT = 5 };

static Int MAX_DISCONNECTS[MAX_DISCONNECTS_COUNT] = {
	MAX_DISCONNECTS_ANY,
	MAX_DISCONNECTS_5,
	MAX_DISCONNECTS_10,
	MAX_DISCONNECTS_25,
	MAX_DISCONNECTS_50
};
void GenOnlineUpdateMapHoverPreview(GameWindow* window, WinInstanceData* instData)
{
	if (mapListboxPreviewFunc)
		mapListboxPreviewFunc(window, instData);
	if (listboxMapSelect == nullptr)
		return;

	const MouseIO* mouseStatus = TheMouse->getMouseStatus();
	Int mouseX = mouseStatus->pos.x;
	Int mouseY = mouseStatus->pos.y;
	Int listboxX, listboxY, listboxW, listboxH;
	listboxMapSelect->winGetScreenPosition(&listboxX, &listboxY);
	listboxMapSelect->winGetSize(&listboxW, &listboxH);

	// mouse is outside the listbox?
	if (mouseX < listboxX || mouseX > listboxX + listboxW || mouseY < listboxY || mouseY > listboxY + listboxH)
		return;

	Int hoveredRow, col;
	GadgetListBoxGetEntryBasedOnXY(listboxMapSelect, mouseX, mouseY, hoveredRow, col);
	const MapMetaData* mapData = (const MapMetaData*)GadgetListBoxGetItemData(listboxMapSelect, hoveredRow, 1);
	mapHoverPreview = mapData ? getMapPreviewImage(mapData->m_fileName) : nullptr;

	if (mapHoverPreview == nullptr)
		return;

	Real wScale = TheDisplay->getWidth() / (Real)DEFAULT_DISPLAY_WIDTH;
	Real hScale = TheDisplay->getHeight() / (Real)DEFAULT_DISPLAY_HEIGHT;
	Real scale = (wScale + hScale) * 0.5f;
	Int  previewSize = (Int)(50 * scale);
	Int  offset = (Int)(20 * scale);
	Int  previewX = mouseX + offset;
	Int  previewY = mouseY - (previewSize / 2);
	TheWindowManager->winDrawImage(mapHoverPreview, previewX, previewY, previewX + previewSize, previewY + previewSize);
}

void GenOnlineUpdateQuickMatchStartButton()
{
	if (!comboBoxLadder || !buttonStart || !listboxMapSelect)
		return;

	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	if (li)
	{
		buttonStart->winEnable(TRUE);
		return;
	}

	Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
	for ( Int i=0; i<numMaps; ++i )
	{
		if ((Bool)GadgetListBoxGetItemData(listboxMapSelect, i, 0))
		{
			buttonStart->winEnable(TRUE);
			return;
		}
	}
	buttonStart->winEnable(FALSE);
}

// -----------------------------------------------------------------------------

static void populateQMColorComboBox(QuickMatchPreferences& pref)
{
	Int numColors = TheMultiplayerSettings->getNumColors();
	UnicodeString colorName;

	GadgetComboBoxReset(comboBoxColor);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboBoxColor, TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboBoxColor, newIndex, (void *)-1);

	for (Int c=0; c<numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def)
			continue;

		colorName = TheGameText->fetch(def->getTooltipName().str());
		newIndex = GadgetComboBoxAddEntry(comboBoxColor, colorName, def->getColor());
		GadgetComboBoxSetItemData(comboBoxColor, newIndex, (void *)c);
	}
	GadgetComboBoxSetSelectedPos(comboBoxColor, pref.getColor());
}

// -----------------------------------------------------------------------------

static void populateQMSideComboBox(Int favSide, const LadderInfo *li = nullptr)
{
	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	UnicodeString playerTemplateName;

	GadgetComboBoxReset(comboBoxSide);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboBoxSide, TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboBoxSide, newIndex, (void *)PLAYERTEMPLATE_RANDOM);

	std::set<AsciiString> seenSides;

	Int entryToSelect = 0; // select Random by default

	for (Int c=0; c<numPlayerTemplates; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		if (fac->getStartingBuilding().isEmpty())
			continue;

		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		if (li)
		{
			if (std::find(li->validFactions.begin(), li->validFactions.end(), fac->getSide()) == li->validFactions.end())
				continue; // ladder doesn't allow it.
		}

		// Remove disallowed generals from the choice list.
		// This is also enforced at GUI setup (GUIUtil.cpp and UserPreferences.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboBoxSide, TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboBoxSide, newIndex, (void *)c);

		if (c == favSide)
			entryToSelect = newIndex;
	}
	seenSides.clear();

	GadgetComboBoxSetSelectedPos(comboBoxSide, entryToSelect);
	if (li && li->randomFactions)
		comboBoxSide->winEnable(FALSE);
	else
		comboBoxSide->winEnable(TRUE);
}

void GenOnlineHandleQMLadderSelection(Int ladderID)
{
	if (!parentWOLQuickMatch)
		return;

	QuickMatchPreferences pref;

	if (ladderID < 1)
	{
		pref.setLastLadder(AsciiString::TheEmptyString, 0);
		pref.write();
		return;
	}

	const LadderInfo *info = TheLadderList->findLadderByIndex(ladderID);
	if (!info)
	{
		pref.setLastLadder(AsciiString::TheEmptyString, 0);
	}
	else
	{
		pref.setLastLadder(info->address, info->port);
	}

	pref.write();
}

static inline Bool isValidLadder( const LadderInfo *lad )
{
	if (lad && lad->index > 0 && lad->validQM)
	{
		// The local wins come from the GenOnline stats cache.
		PSPlayerStats stats;
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
		if (pAuthInterface != nullptr && pStatsInterface != nullptr)
		{
			pStatsInterface->getPlayerStatsFromCache(pAuthInterface->GetUserID(), &stats);
		}
		Int numWins = 0;
		PerGeneralMap::iterator it;
		for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
		{
			numWins += it->second;
		}
		if (!lad->maxWins || lad->maxWins >=numWins)
		{
			if (!lad->minWins || lad->minWins<=numWins)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

void GenOnlinePopulateQMLadderListBox( GameWindow *win )
{
	if (!parentWOLQuickMatch || !comboBoxLadder)
		return;

	isPopulatingLadderBox = true;

	QuickMatchPreferences pref;
	AsciiString userPrefFilename;

	
	Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Int index;
	GadgetListBoxReset( win );

	std::set<const LadderInfo *> usedLadders;

	// start with "No Ladder"
	index = GadgetListBoxAddEntryText(win, UnicodeString(L"Automatic Ladder"), normalColor, -1);
	GadgetListBoxSetItemData(win, 0, index);
	isPopulatingLadderBox = false;
}

static const LadderInfo * getLadderInfo()
{
	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	return li;
}

void GenOnlinePopulateQMLadderComboBox()
{
	if (!parentWOLQuickMatch || !comboBoxLadder)
		return;

	isPopulatingLadderBox = true;

	QuickMatchPreferences pref;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();

	int64_t localProfile = pAuthInterface != nullptr ? pAuthInterface->GetUserID() : -1;

	Color specialColor = GameSpyColor[GSCOLOR_MAP_SELECTED];
	Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Int index;
	GadgetComboBoxReset( comboBoxLadder );
	index = GadgetComboBoxAddEntry( comboBoxLadder, TheGameText->fetch("GUI:NoLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadder, index, nullptr );

	std::set<const LadderInfo *> usedLadders;

	Int selectedPos = 0;
	AsciiString lastLadderAddr = pref.getLastLadderAddr();
	UnsignedShort lastLadderPort = pref.getLastLadderPort();
	const LadderInfo *info = TheLadderList->findLadder( lastLadderAddr, lastLadderPort );
	if (isValidLadder(info))
	{
		usedLadders.insert(info);
		index = GadgetComboBoxAddEntry( comboBoxLadder, info->name, specialColor );
		GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)(info->index) );
		selectedPos = index;

		// we selected a ladder?  No game size choice for us...
		GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, info->playersPerTeam-1);
		comboBoxNumPlayers->winEnable( FALSE );
	}
	else
	{
		comboBoxNumPlayers->winEnable( TRUE );
	}

	LadderPreferences ladPref;
	ladPref.loadProfile( localProfile );
	const LadderPrefMap recentLadders = ladPref.getRecentLadders();
	for (LadderPrefMap::const_iterator cit = recentLadders.begin(); cit != recentLadders.end(); ++cit)
	{
		AsciiString addr = cit->second.address;
		UnsignedShort port = cit->second.port;
		if (addr == lastLadderAddr && port == lastLadderPort)
			continue;
		const LadderInfo *info = TheLadderList->findLadder( addr, port );
		if (isValidLadder(info) && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetComboBoxAddEntry( comboBoxLadder, info->name, normalColor );
			GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)(info->index) );
		}
	}

	index = GadgetComboBoxAddEntry( comboBoxLadder, TheGameText->fetch("GUI:ChooseLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)-1 );

	GadgetComboBoxSetSelectedPos( comboBoxLadder, selectedPos );
	isPopulatingLadderBox = false;

	populateQMSideComboBox(pref.getSide(), getLadderInfo()); // this will set side to random and disable if necessary
}

static void populateQuickMatchMapSelectListbox( QuickMatchPreferences& pref )
{
	NetworkLog(ELogVerbosity::LOG_DEBUG, "Begin Map Dump");
	for (auto it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		if (it->second.m_isOfficial && it->second.m_isMultiplayer)
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "%ls - %s", it->second.m_displayName.str(), it->second.m_fileName.str());
		}
	}
	NetworkLog(ELogVerbosity::LOG_DEBUG, "End Map Dump");

	// TODO_QUICKMATCH
	std::list<AsciiString> maps;
	Int numPlayers = 0;
	Int playlistIndex = -1;
	GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &playlistIndex);

	if (playlistIndex != -1)
	{
		GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
		if (pMatchmakingInterface != nullptr)
		{
			PlaylistEntry plEntry = pMatchmakingInterface->GetCachedPlaylistFromIndex(playlistIndex);
			if (plEntry.PlaylistID != -1)
			{
				// take the min player count, we'll offer maps in that range, up to desired player count
				numPlayers = plEntry.MinPlayers;

				// maps
				for (PlaylistMapEntry& mapEntry : plEntry.Maps)
				{
					// format into game format (Maps\\name\\name.map) for official EA maps or full path for custom maps
					std::string correctedMapPath;
					
					// custom maps need the full path
					if (mapEntry.Custom)
					{
						// TODO_QUICKMATCH: How did the original gamespy service handle this?
						correctedMapPath = std::format("{}maps\\{}\\{}.map", TheGlobalData->getPath_UserData().str(), mapEntry.Path, mapEntry.Path);
					}
					else
					{
						correctedMapPath = std::format("maps\\{}\\{}.map", mapEntry.Path, mapEntry.Path);
					}

					AsciiString mapPath = correctedMapPath.c_str();
					mapPath.toLower();
					maps.push_back(mapPath);
				}
			}
			else
			{
				// TODO_QUICKMATCH: Error?
			}
		}
	}
	else
	{
		// TODO_QUICKMATCH: Error?
	}
	

	// enable/disable box based on ladder status
	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	//listboxMapSelect->winEnable( li == nullptr || li->randomMaps == FALSE );

	// GO does this differently (and does it above)


	GadgetListBoxReset(listboxMapSelect);
	for (std::list<AsciiString>::const_iterator it = maps.begin(); it != maps.end(); ++it)
	{
		const AsciiString &theMap = *it;
		const MapMetaData *md = TheMapCache->findMap(theMap);
		if (md && md->m_numPlayers >= numPlayers)
		{
			UnicodeString displayName;
			displayName = md->m_displayName;
			Bool isSelected = pref.isMapSelected(theMap);
			if (li && li->randomMaps)
				isSelected = TRUE;
			Int width = 10;
			Int height = 10;
			const Image *img = (isSelected)?selectedImage:unselectedImage;
			if ( img )
			{
				width = min(GadgetListBoxGetColumnWidth(listboxMapSelect, 0), img->getImageWidth());
				height = width;
			}
			Int index = GadgetListBoxAddEntryImage(listboxMapSelect, img, -1, 0, height, width);
			GadgetListBoxAddEntryText(listboxMapSelect, displayName, GameSpyColor[(isSelected)?GSCOLOR_MAP_SELECTED:GSCOLOR_MAP_UNSELECTED], index, 1);
			GadgetListBoxSetItemData(listboxMapSelect, (void *)isSelected, index);
			GadgetListBoxSetItemData(listboxMapSelect, (void *)md, index, 1);
		}
	}
}

static void saveQuickMatchOptions()
{
	if(isInInit)
		return;
	QuickMatchPreferences pref;

	Int numPlayers = 0;
	std::list<AsciiString> maps;
	Int selected = -1;
	GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &selected);

	if (selected != -1)
	{
		GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
		if (pMatchmakingInterface != nullptr)
		{
			PlaylistEntry plEntry = pMatchmakingInterface->GetCachedPlaylistFromIndex(selected);
			if (plEntry.PlaylistID != -1)
			{
				// take the min player count, we'll offer maps in that range, up to desired player count
				numPlayers = plEntry.MinPlayers;

				// maps
				for (PlaylistMapEntry& mapEntry : plEntry.Maps)
				{
					// format into game format (Maps\\name\\name.map)
					std::string correctedMapPath = std::format("maps\\{}\\{}.map", mapEntry.Path, mapEntry.Path);
					maps.push_back(AsciiString(correctedMapPath.c_str()));
				}

				// ladder
				pref.setLastLadder(AsciiString::TheEmptyString, 0);

				// map
				Int row = 0;
				Int entries = GadgetListBoxGetNumEntries(listboxMapSelect);
				while (row < entries)
				{
					const MapMetaData* md = (const MapMetaData*)GadgetListBoxGetItemData(listboxMapSelect, row, 1);
					if (md)
						pref.setMapSelected(md->m_fileName, (Bool)GadgetListBoxGetItemData(listboxMapSelect, row));
					row++;
				}
			}
			else
			{
				// TODO_QUICKMATCH: Error?
			}
		}
	}
	else
	{
		// TODO_QUICKMATCH: Error?
	}

	UnicodeString u;
	AsciiString a;
//	u = GadgetTextEntryGetText(textEntryMaxDisconnects);
//	a.translate(u);
//	pref.setMaxDisconnects(atoi(a.str()));
//	u = GadgetTextEntryGetText(textEntryMaxPoints);
//	a.translate(u);
//	pref.setMaxPoints(max(100, atoi(a.str())));
//	u = GadgetTextEntryGetText(textEntryMinPoints);
//	a.translate(u);
//	pref.setMinPoints(min(100, atoi(a.str())));
	//u = GadgetTextEntryGetText(textEntryWaitTime);
	//a.translate(u);
	//pref.setWaitTime(atoi(a.str()));

	GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &selected);
	pref.setNumPlayers(selected);
	GadgetComboBoxGetSelectedPos(comboBoxMaxPing, &selected);
	pref.setMaxPing(selected);

	Int item;
	GadgetComboBoxGetSelectedPos(comboBoxSide, &selected);
	item = (Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxSide, selected);
	pref.setSide(max(0, item));
	GadgetComboBoxGetSelectedPos(comboBoxColor, &selected);
	pref.setColor(max(0, selected));

	GadgetComboBoxGetSelectedPos(comboBoxMaxDisconnects, &selected);
	pref.setMaxDisconnects(selected);


	pref.write();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Quick Match Menu */
//-------------------------------------------------------------------------------------------------
void GenOnlineQuickMatchMenuInit( WindowLayout *layout, void *userData )
{
	++s_quickMatchMenuGeneration;

	isInInit = TRUE;

	nextScreen = nullptr;
	buttonPushed = false;
	isShuttingDown = false;
	raiseMessageBoxes = true;
	matchFoundTimeoutStart = 0;
	matchFoundTimeoutDurationMs = lobbyTimeoutMs;
	matchStartCountdownLastSecond = 0;

	delete TheNAT;
	TheNAT = nullptr;

	parentWOLQuickMatchID = NAMEKEY( "WOLQuickMatchMenu.wnd:WOLQuickMatchMenuParent" );
	buttonBackID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonBack" );
	buttonBuddiesID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonBuddies" );
	buttonStartID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonStart" );
	buttonStopID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonStop" );
	buttonWidenID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonWiden" );
	listboxQuickMatchID = NAMEKEY( "WOLQuickMatchMenu.wnd:ListboxQuickMatch" );
	listboxMapSelectID = NAMEKEY( "WOLQuickMatchMenu.wnd:ListBoxMapSelect" );
	buttonSelectAllMapsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonSelectAllMaps" );
	buttonSelectNoMapsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonSelectNoMaps" );
	//textEntryMaxDisconnectsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMaxDisconnects" );
	//textEntryMaxPointsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMaxPointPercent" );
	//textEntryMinPointsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMinPointPercent" );
	textEntryWaitTimeID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryWaitTime" );
	comboBoxMaxPingID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxMaxPing" );
	comboBoxNumPlayersID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxNumPlayers" );
	comboBoxLadderID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxLadder" );
	comboBoxMaxDisconnectsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxMaxDisconnects" );
	staticTextNumPlayersID = NAMEKEY( "WOLQuickMatchMenu.wnd:StaticTextNumPlayers" );
	comboBoxSideID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxSide" );
	comboBoxColorID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxColor" );

	parentWOLQuickMatch = TheWindowManager->winGetWindowFromId( nullptr, parentWOLQuickMatchID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonBackID);
	buttonStart = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonStartID);
	buttonStop = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonStopID);
	buttonWiden = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonWidenID);
	GenOnlineQuickMatchTextWindow = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  listboxQuickMatchID);
	listboxMapSelect = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  listboxMapSelectID);
	if (listboxMapSelect)
	{
		mapListboxPreviewFunc = listboxMapSelect->winGetDrawFunc();
		listboxMapSelect->winSetDrawFunc(GenOnlineUpdateMapHoverPreview);
	}
	SetListBoxRowAnimMode(GenOnlineQuickMatchTextWindow, LIST_ROW_ANIM_SLOT);
	SetListBoxRowAnimMode(listboxMapSelect, LIST_ROW_ANIM_SLOT);
	//textEntryMaxDisconnects = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMaxDisconnectsID );
	//textEntryMaxPoints = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMaxPointsID );
	//textEntryMinPoints = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMinPointsID );
	textEntryWaitTime = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryWaitTimeID );
	comboBoxMaxPing = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxMaxPingID );
	comboBoxNumPlayers = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxNumPlayersID );
	comboBoxLadder = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxLadderID );
	comboBoxMaxDisconnects = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxMaxDisconnectsID );

	staticTextNumPlayers = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, staticTextNumPlayersID );
	comboBoxSide = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxSideID );
	comboBoxColor = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxColorID );

	if (TheLadderList->getStandardLadders()->empty() &&
		TheLadderList->getSpecialLadders()->empty() &&
		TheLadderList->getLocalLadders()->empty())
	{
		// no ladders, so just disable them
		comboBoxDisabledLadder = comboBoxLadder;
		comboBoxLadder = nullptr;

		isPopulatingLadderBox = TRUE;

		Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
		Int index;
		GadgetComboBoxReset( comboBoxDisabledLadder );

		index = GadgetComboBoxAddEntry(comboBoxDisabledLadder, UnicodeString(L"Automatic Ladder"), normalColor);
		GadgetComboBoxSetItemData( comboBoxDisabledLadder, index, 0 );
		GadgetComboBoxSetSelectedPos( comboBoxDisabledLadder, index );

		isPopulatingLadderBox = FALSE;

		/** This code would actually *hide* the combo box, but it doesn't look as good.  Left here since someone will want to
		 ** see it at some point.  :P
		if (comboBoxLadder)
		{
			comboBoxLadder->winHide(TRUE);
			comboBoxLadder->winEnable(FALSE);
		}
		comboBoxLadder = nullptr;
		GameWindow *staticTextLadder = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,
			NAMEKEY("WOLQuickMatchMenu.wnd:StaticTextLadder") );
		if (staticTextLadder)
			staticTextLadder->winHide(TRUE);
		*/
	}

	GameWindow *buttonBuddies = TheWindowManager->winGetWindowFromId(nullptr, buttonBuddiesID);
	if (buttonBuddies)
		buttonBuddies->winEnable(TRUE);

	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,
		NAMEKEY("WOLQuickMatchMenu.wnd:StaticTextTitle") );
	if (staticTextTitle)
	{
		UnicodeString tmp;
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (pAuthInterface != nullptr)
		{
			tmp.format(WidenFormatSpecifiers(TheGameText->fetch("GUI:QuickMatchTitle").str()).c_str(), pAuthInterface->GetDisplayNameW().c_str());
		}
		GadgetStaticTextSetText(staticTextTitle, tmp);
	}

	// QM is not going yet, so disable the Widen Search button
	if (buttonWiden) buttonWiden->winEnable( FALSE );
	if (buttonStop)  { buttonStop->winHide( TRUE ); buttonStop->winEnable(TRUE); }
	if (buttonStart) buttonStart->winHide( FALSE );
	if (GenOnlineQuickMatchTextWindow) GadgetListBoxReset(GenOnlineQuickMatchTextWindow);
	enableOptionsGadgets(TRUE);

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentWOLQuickMatch );

	// fill in preferences
	selectedImage = TheMappedImageCollection->findImageByName("CustomMatch_selected");
	unselectedImage = TheMappedImageCollection->findImageByName("CustomMatch_deselected");
	QuickMatchPreferences pref;


	UnicodeString s;
//	s.format(L"%d", pref.getMaxDisconnects());
//	GadgetTextEntrySetText(textEntryMaxDisconnects, s);
//	s.format(L"%d", pref.getMaxPoints());
//	GadgetTextEntrySetText(textEntryMaxPoints, s);
//	s.format(L"%d", pref.getMinPoints());
//	GadgetTextEntrySetText(textEntryMinPoints, s);
	//s.format(L"%d", pref.getWaitTime());
	//GadgetTextEntrySetText(textEntryWaitTime, s);
	maxPoints= pref.getMaxPoints();
	minPoints = pref.getMinPoints();

	// NOTE: On GO, this comes from the service
	Int i;
	Color c = GameSpyColor[GSCOLOR_DEFAULT];

	GadgetComboBoxReset(comboBoxMaxDisconnects);
	GadgetComboBoxAddEntry( comboBoxMaxDisconnects, TheGameText->fetch("GUI:Any"), c);
	for( i = 1; i < MAX_DISCONNECTS_COUNT; ++i )
	{
		s.format(L"%d", MAX_DISCONNECTS[i]);
		GadgetComboBoxAddEntry( comboBoxMaxDisconnects, s, c );
	}
	Int maxDisconIndex = max(0, pref.getMaxDisconnects());
	GadgetComboBoxSetSelectedPos(comboBoxMaxDisconnects, maxDisconIndex);

	GadgetComboBoxReset( comboBoxMaxPing );
	// not supported in GO
	maxPingEntries = 0;

	maxPingEntries++; // need to add the entry for the actual timeout
	for (i=1; i <maxPingEntries; ++i)
	{
		s.format(TheGameText->fetch("GUI:TimeInMilliseconds"), i*100);
		GadgetComboBoxAddEntry( comboBoxMaxPing, s, c );
	}
	GadgetComboBoxAddEntry( comboBoxMaxPing, TheGameText->fetch("GUI:ANY"), c );
	i = pref.getMaxPing();
	if( i < 0 )
		i = 0;
	if( i >= maxPingEntries )
		i = maxPingEntries - 1;
	GadgetComboBoxSetSelectedPos( comboBoxMaxPing, i );

	populateQMColorComboBox(pref);
	populateQMSideComboBox(pref.getSide(), getLadderInfo());

	GenOnlinePopulateQMLadderComboBox();
	TheShell->showShellMap(TRUE);

	if (TheGenOnlineGame != nullptr)
		TheGenOnlineGame->reset();

	GadgetListBoxReset(listboxMapSelect);
	populateQuickMatchMapSelectListbox(pref);

	GenOnlineUpdateLocalPlayerStats();
	GenOnlineUpdateQuickMatchStartButton();
	TheTransitionHandler->setGroup("WOLQuickMatchMenuFade");
	isInInit= FALSE;

	buttonStart->winEnable(TRUE);
	buttonStart->winSetText(UnicodeString(L"START MATCHMAKING"));
	buttonStop->winSetText(UnicodeString(L"CANCEL MATCHMAKING"));
	buttonBack->winEnable(TRUE);

	// not supported in GO
	if (comboBoxLadder)
		comboBoxLadder->winEnable(FALSE);
	if (comboBoxDisabledLadder)
		comboBoxDisabledLadder->winEnable(FALSE);
	if (comboBoxMaxPing)
		comboBoxMaxPing->winEnable(FALSE);
	if (comboBoxMaxDisconnects)
		comboBoxMaxDisconnects->winEnable(FALSE);
	if (comboBoxSide)
		comboBoxSide->winEnable(FALSE);
	if (comboBoxColor)
		comboBoxColor->winEnable(FALSE);

	// welcome msg + instructions
	GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Welcome to QuickMatch. Choose Setup to select playlists and maps."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 25, 255)), -1, -1);
	GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Special thanks to map makers Tanso, ReLaX, cncHD, Specovik, Mp3, Jundiyy & Bamovich for making quickmatch possible."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 25, 255)), -1, -1);


	// refresh out stats, needed for ELO data
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
    GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
    if (pAuthInterface != nullptr && pStatsInterface != nullptr)
    {
		const uint64_t generationForStats = s_quickMatchMenuGeneration;
		pStatsInterface->findPlayerStatsByID(pAuthInterface->GetUserID(), [=](bool bSuccess, PSPlayerStats stats)
			{
				if (generationForStats != s_quickMatchMenuGeneration)
				{
					return;
				}

				if (bSuccess)
				{
					UnicodeString eloStr;
#if defined(WORLD_SERIES_CLIENT)
					eloStr.format(L"Your current Elo rating is %d (World Series: %d) after %d match(es)", stats.elo_rating, stats.monthly_elo_rating, stats.elo_num_matches);
#else
					eloStr.format(L"Your current Elo rating is %d (monthly: %d) after %d match(es)", stats.elo_rating, stats.monthly_elo_rating, stats.elo_num_matches);
#endif
					GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, eloStr, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 25, 255)), -1, -1);
				}
			}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
    }


	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

	// cannot connect to the lobby we joined
	if (pLobbyInterface != nullptr)
	{

	pLobbyInterface->RegisterForCannotConnectToLobbyCallback([](void)
		{
			Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Could not connect to a player, waiting for the matchmaker..."), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
			GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);

			// don't cancel: racing a server-issued requeue could unregister us from its bucket
			matchFoundTimeoutStart = 0;
			matchStartCountdownLastSecond = 0;

			buttonBack->winEnable(TRUE);
			buttonStop->winEnable(TRUE);
		});
	}


	// get playlist list
	GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (pMatchmakingInterface != nullptr)
	{
		const uint64_t generationForPlaylists = s_quickMatchMenuGeneration;
		pMatchmakingInterface->RetrievePlaylists([generationForPlaylists](std::vector<PlaylistEntry> vecPlaylists)
			{
				if (generationForPlaylists != s_quickMatchMenuGeneration)
				{
					return;
				}

				// add playlists
				UnicodeString s;

				// clear ui element
				GadgetComboBoxReset(comboBoxNumPlayers);


				for (PlaylistEntry& playlist : vecPlaylists)
				{
					s = UnicodeString(from_utf8(playlist.Name).c_str());
					GadgetComboBoxAddEntry(comboBoxNumPlayers, s, GameSpyColor[GSCOLOR_DEFAULT]);
				}

				GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, 0);
			});
	}


	if (pLobbyInterface != nullptr)
	{
		// TODO_QUICKMATCH: Deregister when leaving QM
		pLobbyInterface->RegisterForMatchmakingMessageCallback([](std::string strMsg)
			{
				UnicodeString uMsg;
				uMsg = UnicodeString(from_utf8(strMsg).c_str());

				Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, uMsg, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
				GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
			});

		pLobbyInterface->RegisterForMatchmakingMatchFoundCallback([]()
			{
				buttonBack->winEnable(FALSE);
				buttonStop->winEnable(FALSE);
				matchFoundTimeoutDurationMs = lobbyTimeoutMs;
				matchFoundTimeoutStart = timeGetTime();
				matchStartCountdownLastSecond = 0;
                if (TheAudio)
				{
					AudioEventRTS evt("GUICommunicatorOpen");
					TheAudio->addAudioEvent(&evt);
				}
			});

		pLobbyInterface->RegisterForMatchmakingRequeueCallback([]()
			{
				matchFoundTimeoutStart = 0;
				matchFoundTimeoutDurationMs = lobbyTimeoutMs;
				matchStartCountdownLastSecond = 0;
				buttonBack->winEnable(TRUE);
				buttonStop->winEnable(TRUE);
				buttonWiden->winEnable(TRUE);
			});

		pLobbyInterface->RegisterForMatchmakingSetupProgressCallback([](int timeoutMs, int countdownMs)
			{
				matchFoundTimeoutDurationMs = timeoutMs;
				matchFoundTimeoutStart = timeGetTime();

				// Mirror the service-owned countdown for UI feedback. Older services don't send its length, so a short
				// timeout is taken to mean the countdown.
				if (countdownMs < 0)
				{
					countdownMs = timeoutMs < lobbyTimeoutMs ? defaultMatchStartCountdownMs : 0;
				}

				matchStartCountdownDurationMs = countdownMs;
				matchStartCountdownLastSecond = countdownMs > 0 ? (countdownMs + 999) / 1000 : 0;
			});

		pLobbyInterface->RegisterForMatchmakingStartGameCallback([]()
			{
				matchFoundTimeoutStart = 0;
				matchStartCountdownLastSecond = 0;
				NetworkLog(ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] GOT START GAME EVENT");

				// Check if TheGenOnlineGame is initialized before dereferencing it
				if (!TheGenOnlineGame)
				{
					NetworkLog(ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] NO NGMP GAME INSTANCE");
					return;
				}

				// mark everyone as having the map, we dont allow user provided custom maps or map transfers in QM
				// TODO_QUICKMATCH: Do this automatically for game type quickmatch, or better yet, do it on the service
				for (int i = 0; i < MAX_SLOTS; i++)
				{
					GameSlot* slot = TheGenOnlineGame->getSlot(i);
					if (slot != nullptr)
					{
						slot->setMapAvailability(TRUE);
					}
				}

				// start
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

				if (pLobbyInterface == nullptr || !myGame || !myGame->isInGame())
				{
					NetworkLog(ELogVerbosity::LOG_DEBUG, "[QUICKMATCH] Checks failed, %d, %d, %d", pLobbyInterface == nullptr, !myGame, !myGame->isInGame());
					return;
				}


				// TODO_NGMP
				//GenOnlineSendStatsToOtherPlayers(TheGenOnlineGame);

				GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
				if (buttonBuddy)
					buttonBuddy->winEnable(FALSE);
				GameSpyCloseOverlay(GSOVERLAY_BUDDY);
				GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);

				*TheGenOnlineGame = *myGame;
				TheGenOnlineGame->startGame(0);
			});

		pLobbyInterface->RegisterForJoinLobbyCallback([](EJoinLobbyResult result)
			{
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

				if (result != EJoinLobbyResult::JoinLobbyResult_Success || !pLobbyInterface->IsInLobby())
				{
					Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Could not join the match lobby, waiting for the matchmaker..."), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
					GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);

					matchFoundTimeoutStart = 0;
					matchStartCountdownLastSecond = 0;

					buttonBack->winEnable(TRUE);
					buttonStop->winEnable(TRUE);
					return;
				}

				if (TheGenOnlineGame == nullptr)
				{
					TheGenOnlineGame = new GenOnlineGame();
					TheGenOnlineGame->markGameAsQM();
				}
				pLobbyInterface->UpdateRoomDataCache([](bool bSuccess)
					{

					});

				// connection events (for debug really)
				NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
				if (pMesh != nullptr)
				{
					pMesh->RegisterForConnectionEvents([](int64_t userID, std::wstring strDisplayName, PlayerConnection* connection)
						{
#if _DEBUG // not enabled in quickmatch because then people see their opponent during matchmaking
							std::string strState = "Unknown";

							EConnectionState connState = connection->GetState();

							switch (connState)
							{
							case EConnectionState::NOT_CONNECTED:
								strState = "Not Connected";
								break;

							case EConnectionState::CONNECTING_DIRECT:
								strState = "Connecting";
								break;
							case EConnectionState::FINDING_ROUTE:
								strState = "Connecting (Finding Route)";
								break;

							case EConnectionState::CONNECTED_DIRECT:
								strState = "Connected";
								break;

							case EConnectionState::CONNECTION_FAILED:
								strState = "Connection Failed";
								break;

							case EConnectionState::CONNECTION_DISCONNECTED:
								strState = "Disconnected (Was Connected Previously)";
								break;

							default:
								strState = "Unknown";
								break;
							}

							UnicodeString strConnectionMessage;
							if (connState == EConnectionState::CONNECTING_DIRECT || connState == EConnectionState::FINDING_ROUTE)
							{
								strConnectionMessage.format(L"Connecting to %s", strDisplayName.c_str());

								Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, strConnectionMessage, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
								GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
							}
							else if (connState == EConnectionState::CONNECTED_DIRECT)
							{
								strConnectionMessage.format(L"Connected to %s", strDisplayName.c_str());

								Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, strConnectionMessage, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
								GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
							}
							else
							{
								if (connState == EConnectionState::CONNECTION_FAILED || connState == EConnectionState::CONNECTION_DISCONNECTED)
								{
									strConnectionMessage.format(L"Connection failed to %s", strDisplayName.c_str());
									Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, strConnectionMessage, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
									GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
								}
							}
#endif
						});
				}

			});
	}

    // Update the communicator button anytime we get notifications
    GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
    if (pSocialInterface != nullptr)
    {
        // notifiactions callback
        pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([=](int numNotifications)
            {
                // update communicator button
                if (buttonBuddies != nullptr)
                {
                    UnicodeString buttonText;
                    if (numNotifications > 0)
                    {
                        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
                    }
                    else
                    {
                        buttonText.format(L"%s", TheGameText->fetch("GUI:Buddies").str());
                    }
					buttonBuddies->winSetText(buttonText);


                }
            });
    }

    // And also initialize it
    if (buttonBuddies != nullptr && pSocialInterface != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
    {
        UnicodeString buttonText;
        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
        buttonBuddies->winSetText(buttonText);
    }
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != nullptr) );

	if (nextScreen != nullptr)
	{
		TheShell->push(nextScreen);
	}

	nextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlineQuickMatchMenuShutdown( WindowLayout *layout, void *userData )
{
	++s_quickMatchMenuGeneration;


	GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (pMatchmakingInterface != nullptr)
	{
		// Shutdown is invokved for UI exit but also for going to game... so don't tear down lobby on service in the latter case
		if (TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress())
		{
			pMatchmakingInterface->CancelMatchmaking();
		}
	}

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->DeregisterForMatchmakingMessageCallback();
		pLobbyInterface->DeRegisterForMatchmakingMatchFoundCallback();
		pLobbyInterface->DeregisterForMatchmakingRequeueCallback();
		pLobbyInterface->DeregisterForMatchmakingSetupProgressCallback();
		pLobbyInterface->DeregisterForMatchmakingStartGameCallback();

		pLobbyInterface->DeregisterForJoinLobbyCallback();
		pLobbyInterface->DeregisterForCannotConnectToLobbyCallback();
	}

	if (!TheGameEngine->getQuitting())
		saveQuickMatchOptions();

	if (listboxMapSelect && mapListboxPreviewFunc)
		listboxMapSelect->winSetDrawFunc(mapListboxPreviewFunc);

	mapListboxPreviewFunc = nullptr;
	mapHoverPreview = nullptr;
	listboxMapSelect = nullptr;

	parentWOLQuickMatch = nullptr;
	buttonBack = nullptr;
	buttonStart = nullptr;
	buttonStop = nullptr;
	buttonWiden = nullptr;
	comboBoxNumPlayers = nullptr;
	GenOnlineQuickMatchTextWindow = nullptr;
	selectedImage = unselectedImage = nullptr;
	matchFoundTimeoutStart = 0;
	matchStartCountdownLastSecond = 0;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("WOLQuickMatchMenuFade");

	RaiseGSMessageBox();
}


#ifdef PERF_TEST
static const char* getMessageString(Int t)
{
	switch(t)
	{
		case PeerResponse::PEERRESPONSE_LOGIN:
			return "login";
		case PeerResponse::PEERRESPONSE_DISCONNECT:
			return "disconnect";
		case PeerResponse::PEERRESPONSE_MESSAGE:
			return "message";
		case PeerResponse::PEERRESPONSE_GROUPROOM:
			return "group room";
		case PeerResponse::PEERRESPONSE_STAGINGROOM:
			return "staging room";
		case PeerResponse::PEERRESPONSE_STAGINGROOMPLAYERINFO:
			return "staging room player info";
		case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
			return "group room join";
		case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
			return "staging room create";
		case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
			return "staging room join";
		case PeerResponse::PEERRESPONSE_PLAYERJOIN:
			return "player join";
		case PeerResponse::PEERRESPONSE_PLAYERLEFT:
			return "player part";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDNICK:
			return "player nick";
		case PeerResponse::PEERRESPONSE_PLAYERINFO:
			return "player info";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
			return "player flags";
		case PeerResponse::PEERRESPONSE_ROOMUTM:
			return "room UTM";
		case PeerResponse::PEERRESPONSE_PLAYERUTM:
			return "player UTM";
		case PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS:
			return "QM status";
		case PeerResponse::PEERRESPONSE_GAMESTART:
			return "game start";
		case PeerResponse::PEERRESPONSE_FAILEDTOHOST:
			return "host failure";
	}
	return "unknown";
}
#endif // PERF_TEST

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu update method */
//-------------------------------------------------------------------------------------------------
void GenOnlineQuickMatchMenuUpdate( WindowLayout * layout, void *userData)
{
    // need to exit?
    if (GenOnlineServicesManager::GetInstance() != nullptr && GenOnlineServicesManager::GetInstance()->IsPendingFullTeardown())
    {
        // Only if not in game and not in anim
        if ((TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress()) && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
        {
            bool bForceShutdown = true;
            GenOnlineQuickMatchMenuShutdown(layout, (void*)&bForceShutdown); // userdata is 'force shutdown'
            TearDownGeneralsOnline();

            TheShell->pop();
            return;
        }
    }

	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_ENTERED_FROM_GAME);
	}

	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

	GenOnlineHandleBuddyResponses();

	if (matchStartCountdownLastSecond > 0)
	{
		Int elapsedMs = timeGetTime() - matchFoundTimeoutStart;
		Int remainingMs = matchStartCountdownDurationMs - elapsedMs;
		Int secondsRemaining = remainingMs > 0 ? (remainingMs + 999) / 1000 : 0;

		if (secondsRemaining > 0 && secondsRemaining < matchStartCountdownLastSecond)
		{
			UnicodeString countdownMessage;
			if (secondsRemaining == 1)
			{
				countdownMessage.format(TheGameText->fetch("LAN:GameStartTimerSingular"), secondsRemaining);
			}
			else
			{
				countdownMessage.format(TheGameText->fetch("LAN:GameStartTimerPlural"), secondsRemaining);
			}

			Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, countdownMessage, GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
			GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
			matchStartCountdownLastSecond = secondsRemaining;
		}
	}

	// Leave time for the server's START_GAME event to arrive.
	Int effectiveTimeoutMs = matchFoundTimeoutDurationMs < lobbyTimeoutMs ? lobbyTimeoutMs : matchFoundTimeoutDurationMs;
	if (matchFoundTimeoutStart != 0 && timeGetTime() - matchFoundTimeoutStart >= effectiveTimeoutMs)
	{
		matchFoundTimeoutStart = 0;
		buttonBack->winEnable(TRUE);
		buttonStop->winEnable(TRUE);
		Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Match setup timed out. You may cancel or continue waiting."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 25, 255)), -1, -1);
		GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
	}

	/// @todo: MDC handle disconnects in-game the same way as Custom Match!

}

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineQuickMatchMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

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
						// ESC must not do what the disabled back button can't, e.g. leave during match setup
						if(!buttonBack->winIsHidden() && buttonBack->winGetEnabled())
							TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																							(WindowMsgData)buttonBack, buttonBackID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineQuickMatchMenuSystem( GameWindow *window, UnsignedInt msg,
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

		case GCM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				Int pos = -1;
				GadgetComboBoxGetSelectedPos(control, &pos);

				saveQuickMatchOptions();
				if (controlID == comboBoxLadderID && !isPopulatingLadderBox)
				{
					if (pos >= 0)
					{
						QuickMatchPreferences pref;
						Int ladderID = (Int)(intptr_t)GadgetComboBoxGetItemData(control, pos);
						if (ladderID == 0)
						{
							// no ladder selected - enable buttons
							GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, max(0, pref.getNumPlayers()/2-1));
							comboBoxNumPlayers->winEnable( TRUE );
							populateQMSideComboBox(pref.getSide()); // this will set side to random and disable if necessary
						}
						else if (ladderID > 0)
						{
							// ladder selected - disable buttons
							const LadderInfo *li = TheLadderList->findLadderByIndex(ladderID);
							if (li)
								GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, li->playersPerTeam-1);
							else
								GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, 0);
							comboBoxNumPlayers->winEnable( FALSE );

							populateQMSideComboBox(pref.getSide(), li); // this will set side to random and disable if necessary
						}
						else
						{
							// "Choose a ladder" selected - open overlay
							GenOnlinePopulateQMLadderComboBox(); // this restores the non-"Choose a ladder" selection
							GameSpyOpenOverlay( GSOVERLAY_LADDERSELECT );
						}
					}
				}
				if (!isInInit)
				{
					QuickMatchPreferences pref;
					populateQuickMatchMapSelectListbox(pref);
					GenOnlineUpdateQuickMatchStartButton();
				}
				break;
			}

		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				static NameKeyType buttonOptionsID = NAMEKEY("WOLQuickMatchMenu.wnd:ButtonOptions");

				if ( controlID == buttonStopID )
				{
					buttonWiden->winEnable(FALSE);
					buttonStart->winEnable(TRUE);
					buttonStart->winHide(FALSE);
					buttonStop->winHide(TRUE);
					enableOptionsGadgets(TRUE);

					Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, TheGameText->fetch("GUI:QMAborted"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
					GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);

					GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
					if (pMatchmakingInterface != nullptr)
					{
						pMatchmakingInterface->CancelMatchmaking();
					}
				}
				else if ( controlID == buttonOptionsID )
				{
					GameWindow *win =TheWindowManager->winGetWindowFromId(parentWOLQuickMatch,buttonOptionsID);
					if (isInfoShown())
					{
						hideInfoGadgets(TRUE);
						hideOptionsGadgets(FALSE);
						GadgetButtonSetText(win, TheGameText->fetch("GUI:PlayerInfo"));
					}
					else
					{
						hideInfoGadgets(FALSE);
						hideOptionsGadgets(TRUE);
						GadgetButtonSetText(win, TheGameText->fetch("GUI:Setup"));
					}
				}
				else if ( controlID == buttonWidenID )
				{
					GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, TheGameText->fetch("QM:WIDENINGSEARCH"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);

					GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
					if (pMatchmakingInterface != nullptr)
					{
						pMatchmakingInterface->WidenSearch();

						// disable widen button
						buttonWiden->winEnable(FALSE);
					}
				}
				else if ( controlID == buttonStartID )
				{

					std::vector<int> vecSelectedMapIndexes;
					uint16_t playlistID = 0;
					int minSelectedMaps = 0;

					// get maps and playlist ID
					std::list<AsciiString> maps;
					Int playlistIndex = -1;
					GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &playlistIndex);

					if (playlistIndex != -1)
					{
						GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
						if (pMatchmakingInterface != nullptr)
						{
							PlaylistEntry plEntry = pMatchmakingInterface->GetCachedPlaylistFromIndex(playlistIndex);
							if (plEntry.PlaylistID != -1)
							{
								playlistID = plEntry.PlaylistID;
								minSelectedMaps = plEntry.MinSelectedMaps;

								// maps
								Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
								for (Int i = 0; i < numMaps; ++i)
								{
									bool bMapSelected = GadgetListBoxGetItemData(listboxMapSelect, i, 0);
									if (bMapSelected)
									{
										vecSelectedMapIndexes.push_back(i);
									}
								}
							}
							else
							{
								// TODO_QUICKMATCH: Error?
							}
						}
					}
					else
					{
						// TODO_QUICKMATCH: Error?
					}
					
					if (static_cast<int>(vecSelectedMapIndexes.size()) < minSelectedMaps)
					{
						UnicodeString msg;
						msg.format(L"You must select at least %d maps.", minSelectedMaps);
						Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, msg, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
						GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);
						
						// buttons
						buttonWiden->winEnable(FALSE);
						buttonStart->winHide(FALSE);
						buttonStart->winEnable(TRUE);
						buttonStop->winHide(TRUE);
						
						break;
					}

					// buttons
					buttonWiden->winEnable(FALSE);
					buttonStart->winEnable(FALSE);
					buttonStop->winHide(TRUE);

					GenOnlineMatchmakingInterface* pMatchmakingInterface = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
					if (pMatchmakingInterface != nullptr)
					{
						const uint64_t generationForStart = s_quickMatchMenuGeneration;
						pMatchmakingInterface->StartMatchmaking(playlistID, vecSelectedMapIndexes, [generationForStart](bool bSuccess)
							{
								if (generationForStart != s_quickMatchMenuGeneration)
								{
					// menu closed; static window pointers may be stale
									return;
								}

								// TODO_QUICKMATCH: Chat has a sound effect in TheGameSpyInfo, re-eanble it
								if (bSuccess)
								{

									// buttons
									buttonWiden->winEnable(TRUE);
									buttonStart->winHide(TRUE);
									buttonStop->winHide(FALSE);
								}
								else
								{
									Int index = GadgetListBoxAddEntryText(GenOnlineQuickMatchTextWindow, UnicodeString(L"Failed to start matchmaking."), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
									GadgetListBoxSetItemData(GenOnlineQuickMatchTextWindow, (void*)-1, index);

									// buttons
									buttonWiden->winEnable(FALSE);
									buttonStart->winHide(FALSE);
									buttonStart->winEnable(TRUE);
									buttonStop->winHide(TRUE);
								}
							});
					}
				}
				else if ( controlID == buttonBuddiesID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );
				}
				else if ( controlID == buttonBackID )
				{
					buttonPushed = true;			
					nextScreen = "Menus/WOLWelcomeMenu.wnd";
					TheShell->pop();
				}
				else if ( controlID == buttonSelectAllMapsID )
				{
					Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
					for ( Int i=0; i<numMaps; ++i )
					{
						GadgetListBoxAddEntryImage(listboxMapSelect, selectedImage, i, 0);
						GadgetListBoxSetItemData(listboxMapSelect, (void *)1, i);
						GadgetListBoxAddEntryText(listboxMapSelect, GadgetListBoxGetText(listboxMapSelect, i, 1), GameSpyColor[GSCOLOR_MAP_SELECTED], i, 1);
					}
				}
				else if ( controlID == buttonSelectNoMapsID )
				{
					Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
					for ( Int i=0; i<numMaps; ++i )
					{
						GadgetListBoxAddEntryImage(listboxMapSelect, unselectedImage, i, 0);
						GadgetListBoxSetItemData(listboxMapSelect, (void *)nullptr, i);
						GadgetListBoxAddEntryText(listboxMapSelect, GadgetListBoxGetText(listboxMapSelect, i, 1), GameSpyColor[GSCOLOR_MAP_UNSELECTED], i, 1);
					}
				}
				break;
			}

		case GLM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				Int selected = (Int)mData2;

				if ( controlID == listboxMapSelectID )
				{
					const LadderInfo *li = getLadderInfo();
					if (selected >= 0 && (!li || !li->randomMaps))
					{
						Bool wasSelected = (Bool)GadgetListBoxGetItemData(control, selected, 0);
						GadgetListBoxSetItemData(control, (void *)(!wasSelected), selected, 0);
						Int width = 10;
						Int height = 10;
						const Image *img = (!wasSelected)?selectedImage:unselectedImage;
						if ( img )
						{
							width = min(GadgetListBoxGetColumnWidth(control, 0), img->getImageWidth());
							height = width;
						}
						GadgetListBoxAddEntryImage(control, img, selected, 0, height, width);
						GadgetListBoxAddEntryText(control, GadgetListBoxGetText(control, selected, 1), GameSpyColor[(wasSelected)?GSCOLOR_MAP_UNSELECTED:GSCOLOR_MAP_SELECTED], selected, 1);
					}
					if (selected >= 0)
						GadgetListBoxSetSelected(control, -1);
				}
				GenOnlineUpdateQuickMatchStartButton();
				break;
			}

		case GEM_EDIT_DONE:
			{
				break;
			}
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}


