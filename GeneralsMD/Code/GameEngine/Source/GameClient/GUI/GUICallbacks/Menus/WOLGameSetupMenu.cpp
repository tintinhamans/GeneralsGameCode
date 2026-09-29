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
// FILE: WOLGameSetupMenu.cpp
// Author: Matt Campbell, December 2001
// Description: WOL Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MultiplayerSettings.h"
#include "Common/OptionPreferences.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "Common/PlayerTemplate.h"
#include "Common/CustomMatchPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Mouse.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/EstablishConnectionsMenu.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/LANAPICallbacks.h"

#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/GSConfig.h"

#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupData.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupSession.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineSessionExit.h"
#include <ws2ipdef.h>
#include <format>
#include <cmath>
#include "../OnlineServices_Init.h"
#include "GameLogic/GameLogic.h"
NGMPGame* TheNGMPGame = NULL;

void WOLDisplaySlotList( void );
static void WOLRefreshConnectionIndicators( void );


extern std::list<PeerResponse> TheLobbyQueuedUTMs;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);


#if defined(RTS_DEBUG)
extern Bool g_debugSlots;
void slotListDebugLog(const char *fmt, ...)
{
	static char buf[1024];
	va_list va;
	va_start( va, fmt );
	vsnprintf(buf, 1024, fmt, va );
	va_end( va );

	DEBUG_LOG(("%s", buf));
	if (g_debugSlots)
	{
		UnicodeString msg;
		msg.translate(buf);
		// TODO_NGMP: Impl again
		TheGameSpyInfo->addText(msg, GameSpyColor[GSCOLOR_DEFAULT], NULL);
	}
}
#define SLOTLIST_DEBUG_LOG(x) slotListDebugLog x
#else
#define SLOTLIST_DEBUG_LOG(x) DEBUG_LOG(x)
#endif

// TODO_NGMP: Remove this, make others get it from the service
void SendStatsToOtherPlayers(const GameInfo *game)
{
	PeerRequest req;
	req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
	req.UTM.isStagingRoom = TRUE;
	req.id = "STATS/";
	AsciiString fullStr;
	PSPlayerStats fullStats = TheGameSpyPSMessageQueue->findPlayerStatsByID(TheGameSpyInfo->getLocalProfileID());
	PSPlayerStats subStats;
	subStats.id = fullStats.id;
	subStats.wins = fullStats.wins;
	subStats.losses = fullStats.losses;
	subStats.discons = fullStats.discons;
	subStats.desyncs = fullStats.desyncs;
	subStats.games = fullStats.games;
	subStats.locale = fullStats.locale;
	subStats.gamesAsRandom = fullStats.gamesAsRandom;
	GetAdditionalDisconnectsFromUserFile(&subStats);
	fullStr.format("%d %s", TheGameSpyInfo->getLocalProfileID(), TheGameSpyPSMessageQueue->formatPlayerKVPairs( subStats ).c_str());
	req.options = fullStr.str();

	Int localIndex = game->getLocalSlotNum();
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot->isHuman() && i != localIndex)
		{
			AsciiString hostName;
			hostName.translate(slot->getName());
			req.nick = hostName.str();
			DEBUG_LOG(("SendStatsToOtherPlayers() - sending to '%s', data of\n\t'%s'", hostName.str(), req.options.c_str()));
			TheGameSpyPeerMessageQueue->addRequest(req);
		}
	}
}

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;
static Bool raiseMessageBoxes = false;
static Bool launchGameNext = FALSE;

// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLGameSetupID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType staticTextPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonAcceptID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxColorID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxPlayerTemplateID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxTeamID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
//static NameKeyType buttonStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
static NameKeyType genericPingWindowID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;

// Match-start countdown state now lives in OnlineGameSetupSession (owns update()'s countdown tick).

static NameKeyType windowMapSelectMapID = NAMEKEY_INVALID;
static NameKeyType checkBoxUseStatsID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitArmiesID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLGameSetup = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonStart = NULL;
static GameWindow *buttonSelectMap = NULL;
static GameWindow *buttonEmote = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *textEntryMapDisplay = NULL;
static GameWindow *windowMap = NULL;
static GameWindow *checkBoxUseStats = NULL;
static GameWindow *checkBoxLimitSuperweapons = NULL;
static GameWindow *comboBoxStartingCash = NULL;
static GameWindow *checkBoxLimitArmies = NULL;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																									 NULL,NULL,NULL,NULL };
static GameWindow *staticTextPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																									 NULL,NULL,NULL,NULL };
static GameWindow *buttonAccept[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxColor[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxTeam[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
//																								NULL,NULL,NULL,NULL };
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *genericPingWindow[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

// Connection indicator per slot: 0..4 is the signal level (1..5 bars), negative values are special states.
enum
{
	CONNECTION_INDICATOR_CONNECTING = -1,
	CONNECTION_INDICATOR_WARNING = -2,
};
static Int connectionIndicatorState[MAX_SLOTS];

static const Int CONNECTION_LEVEL_COUNT = 5;
static const Int connectionLevelThresholds[CONNECTION_LEVEL_COUNT - 1] = { 40, 60, 75, 90 }; ///< score needed for 2..5 bars
static const Int CONNECTION_LEVEL_HYSTERESIS = 3;

WindowLayout *WOLMapSelectLayout = NULL;

void PopBackToLobby()
{
	// Leave even without the screen up: NGMPGame calls this from outside the setup screen.
	OnlineGameSetupSession::leaveLobby();

	DEBUG_LOG(("PopBackToLobby() - parentWOLGameSetup is %X", parentWOLGameSetup));
	if (parentWOLGameSetup)
	{
		nextScreen = "Menus/WOLCustomLobby.wnd";
		TheShell->pop();
	}
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void positionStartSpots(AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void WOLPositionStartSpots()
{
	GameWindow *win = windowMap;
	if (WOLMapSelectLayout != NULL) {
		win = TheWindowManager->winGetWindowFromId(NULL, windowMapSelectMapID);

		// get the controls.
		NameKeyType listboxMapID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
		GameWindow *listboxMap = TheWindowManager->winGetWindowFromId( NULL, listboxMapID );

		if (listboxMap != NULL) {
			Int selected;
			UnicodeString map;

			// get the selected index
			GadgetListBoxGetSelected( listboxMap, &selected );

			if( selected != -1 )
			{

				// get text of the map to load
				map = GadgetListBoxGetText( listboxMap, selected, 0 );


				// set the map name in the global data map name
				AsciiString asciiMap;
				const char *mapFname = (const char *)GadgetListBoxGetItemData( listboxMap, selected );
				DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
				if (mapFname) {
					asciiMap = mapFname;
				} else {
					asciiMap.translate( map );
				}

				positionStartSpots(asciiMap, buttonMapStartPosition, win);
			}
		}

	} else {
		DEBUG_ASSERTCRASH(win != NULL, ("no map preview window"));

		AsciiString map = TheNGMPGame->getMap();
		positionStartSpots( map, buttonMapStartPosition, win);
	}
}
static void savePlayerInfo()
{
	if (TheNGMPGame)
	{
		Int slotNum = TheNGMPGame->getLocalSlotNum();
		if (slotNum >= 0)
		{
			NGMPGameSlot *slot = TheNGMPGame->getGameSpySlot(slotNum);
			if (slot)
			{
				// save off some prefs
				CustomMatchPreferences pref;
				pref.setPreferredColor(slot->getColor());
				pref.setPreferredFaction(slot->getPlayerTemplate());
				if (TheNGMPGame->amIHost())
				{
					pref.setPreferredMap(TheNGMPGame->getMap());
				}
				pref.write();
			}
		}
	}
}

// Tooltips -------------------------------------------------------------------------------

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int slotIdx = -1;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (window == comboBoxPlayer[i] || window == staticTextPlayer[i])
		{
			slotIdx = i;
			break;
		}
	}
	if (slotIdx < 0)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString, -1, NULL, 1.5f);
		return;
	}

	NGMPGame* game = pLobbyInterface->GetCurrentGame();
	if (!game)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	NGMPGameSlot *slot = game->getGameSpySlot(slotIdx);
	if (!slot || !slot->isHuman())
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	// for tooltip, we want:
	// * player name
	// * ping
	// * locale
	// * win/loss history
	// * discons/desyncs as one var
	// * favorite army
	// in that order.  got it?  good.

	UnicodeString uName = slot->getName();
// 
// 	AsciiString aName;
// 	aName.translate(uName);
// 	PlayerInfoMap::iterator pmIt = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
// 	if (pmIt == TheGameSpyInfo->getPlayerInfoMap()->end())
// 	{
// 		TheMouse->setCursorTooltip( uName, -1, NULL, 1.5f );
// 		return;
// 	}
	//Int profileID = pmIt->second.m_profileID;

	int64_t profileID = slot->m_userID;

	//TheMouse->setCursorTooltip(UnicodeString(L"Retrieving User Stats..."), -1, NULL, 1.5f);

	NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	if (pStatsInterface == nullptr)
	{
		return;
	}

	pStatsInterface->findPlayerStatsByID(profileID, [=](bool bSuccess, PSPlayerStats stats)
	{
			if (stats.id == 0)
			{
				TheMouse->setCursorTooltip(uName, -1, NULL, 1.5f);
				return;
			}

			Bool isLocalPlayer = slot == game->getGameSpySlot(game->getLocalSlotNum());

			//AsciiString localeIdentifier;
			//localeIdentifier.format("WOL:Locale%2.2d", stats.locale);
			UnicodeString	playerInfo;
			Int totalWins = 0, totalLosses = 0, totalDiscons = 0;
			PerGeneralMap::iterator it;

	for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		totalWins += it->second;
	}
	for (it = stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		totalLosses += it->second;
	}
	for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		totalDiscons += it->second;
	}
	for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		totalDiscons += it->second;
	}
	UnicodeString favoriteSide;
	Int numGames = 0;
	Int favorite = 0;
	for(it = stats.games.begin(); it != stats.games.end(); ++it)
	{
		if (it->first == PLAYERTEMPLATE_OBSERVER) // not a real faction
			continue;
		if(it->second >= numGames)
		{
			numGames = it->second;
			favorite = it->first;
		}
	}
	if(numGames == 0)
		favoriteSide = TheGameText->fetch("GUI:None");
	else if( stats.gamesAsRandom >= numGames )
		favoriteSide = TheGameText->fetch("GUI:Random");
	else
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);
		if (fac)
		{
			AsciiString side;
			side.format("SIDE:%s", fac->getSide().str());

					favoriteSide = TheGameText->fetch(side);
				}
			}

#if defined(GENERALS_ONLINE)
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	OnlineGameSetupConnectionInfo connectionInfo = OnlineGameSetupData::computeConnectionInfo(pLobbyInterface, game, slot);

	if (connectionInfo.m_isLocalPlayer)
	{
		// local user wont have a connection
		playerInfo.format(L"\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
	else if (connectionInfo.m_isConnected)
	{
		UnicodeString scoreStr, latencyStr, jitterStr, qualityStr;
		if (connectionInfo.m_score >= 0) scoreStr.format(L"%d%%", connectionInfo.m_score); else scoreStr = L"Unknown";
		if (connectionInfo.m_latencyMs >= 0) latencyStr.format(L"%d ms", connectionInfo.m_latencyMs); else latencyStr = L"Unknown";
		if (connectionInfo.m_jitterMs >= 0) jitterStr.format(L"%d ms", connectionInfo.m_jitterMs); else jitterStr = L"Unknown";
		if (connectionInfo.m_qualityPct >= 0) qualityStr.format(L"%d%%", connectionInfo.m_qualityPct); else qualityStr = L"Unknown";
		playerInfo.format(L"\nConnection State: Connected (%hs)\nConnection Score: %s\nLatency: %s\nJitter: %s\nReliability: %s\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			connectionInfo.m_connectionType.c_str(), scoreStr.str(), latencyStr.str(), jitterStr.str(), qualityStr.str(),
			connectionInfo.m_region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
	else
	{
		playerInfo.format(L"\nConnection State: Connecting...\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			connectionInfo.m_region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
#else
			playerInfo.format(L"\nLatency: %d ms\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
				slot->getPingAsInt(), totalWins, totalLosses, totalDiscons, favoriteSide.str());
#endif

			UnicodeString tooltip = UnicodeString::TheEmptyString;
			if (isLocalPlayer)
			{
				tooltip.format(TheGameText->fetch("TOOLTIP:LocalPlayer"), uName.str());
			}
			else
			{
				// not us
				// TODO_NGMP: Impl friends again
				bool bIsFriend = false;
				if (bIsFriend)
				//if (TheGameSpyInfo->getBuddyMap()->find(profileID) != TheGameSpyInfo->getBuddyMap()->end())
				{
					// buddy
					tooltip.format(TheGameText->fetch("TOOLTIP:BuddyPlayer"), uName.str());
				}
				else
				{
					if (profileID)
					{
						// non-buddy profiled player
						tooltip.format(TheGameText->fetch("TOOLTIP:ProfiledPlayer"), uName.str());
					}
					else
					{
						// non-profiled player
						tooltip.format(TheGameText->fetch("TOOLTIP:GenericPlayer"), uName.str());
					}
				}
			}

			tooltip.concat(playerInfo);

			NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
			if (pAuthInterface != nullptr && pRoomsInterface != nullptr)
			{
				NetworkRoomMember* localMember = pRoomsInterface->GetRoomMemberFromID(pAuthInterface->GetUserID());
				if (localMember != nullptr && localMember->m_bIsAdmin)
				{
					UnicodeString idLine;
					idLine.format(L"\n\nUser ID: %lld", slot->m_userID);
					tooltip.concat(idLine);
				}
			}

			TheMouse->setCursorTooltip(tooltip, -1, NULL, 1.5f); // the text and width are the only params used.  the others are the default values.

	}, EStatsRequestPolicy::CACHED_ONLY);
}

void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	Int winPosX, winPosY, winWidth, winHeight;

	window->winGetScreenPosition(&winPosX, &winPosY);

	window->winGetSize(&winWidth, &winHeight);

	if ((x > winPosX && x < (winPosX + winWidth)) && (y > winPosY && y < (winPosY + winHeight)))
	{
		TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:GameAcceptance"), -1, NULL);
	}
}

void pingTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);


	Int winPosX, winPosY, winWidth, winHeight;

	window->winGetScreenPosition(&winPosX, &winPosY);

	window->winGetSize(&winWidth, &winHeight);

	if ((x > winPosX && x < (winPosX + winWidth)) && (y > winPosY && y < (winPosY + winHeight)))
	{
		TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:ConnectionSpeed"), -1, NULL);
	}
}

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxGameSetupChat = NULL;
NameKeyType listboxGameSetupChatID = NAMEKEY_INVALID;

// Same GadgetListBoxAddEntryText(listboxGameSetupChat, ...) call every chat/system-notice line in
// this file used to make directly -- now also handed to OnlineGameSetupActions as a ChatLineFn, so
// StartPressed()/slash commands produce lines through this one call site whether they're
// widget-driven (this .wnd) or not (a future RmlOnlineGameSetupScreen would pass its own).
static void addGameSetupChatLine( const UnicodeString &text, Color color )
{
	GadgetListBoxAddEntryText(listboxGameSetupChat, text, color, -1, -1);
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	OnlineGameSetupActions::selectColor(myGame, index, color);
}

static void handlePlayerTemplateSelection(int index, bool bInitialSetup = false)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	if (!bInitialSetup)
	{
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

		if (myGame && OnlineGameSetupActions::selectPlayerTemplate(myGame, index, playerTemplate))
		{
			// became, or stopped being, an observer -- reset color/team to "random"/"all", same as
			// GadgetComboBoxSetSelectedPos(comboBoxColor[index]/comboBoxTeam[index], 0) always did.
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
		}
	}
}


static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	OnlineGameSetupActions::selectTeam(myGame, index, team);
}

static void handleStartingCashSelection()
{
#if defined(GENERALS_ONLINE)
// update it on the service
	Int selIndex;
	GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

	UnsignedInt startingCashValue = (UnsignedInt)GadgetComboBoxGetItemData(comboBoxStartingCash, selIndex);

	Money startingCash;
	startingCash.deposit(startingCashValue, FALSE);

	OnlineGameSetupActions::setStartingCash(startingCash);
#else
  GameInfo *myGame = TheGameSpyInfo->getCurrentStagingRoom();

  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( (UnsignedInt)GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ), FALSE, FALSE );
    myGame->setStartingCash( startingCash );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      // send around the new data
      TheGameSpyInfo->setGameOptions();
      WOLDisplaySlotList();// Update the accepted button UI
    }
  }
#endif
}

static void handleLimitSuperweaponsClick()
{

#if defined(GENERALS_ONLINE)
	// update it on the service
	bool bLimitSuperweapons = GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons);
	OnlineGameSetupActions::setSuperweaponRestriction(bLimitSuperweapons);
#else
  GameInfo *myGame = TheGameSpyInfo->getCurrentStagingRoom();

  if (myGame)
  {
    // At the moment, 1 and 0 are the only choices supported in the GUI, though the system could
    // support more.
    if ( GadgetCheckBoxIsChecked( checkBoxLimitSuperweapons ) )
    {
      myGame->setSuperweaponRestriction( 1 );
    }
    else
    {
      myGame->setSuperweaponRestriction( 0 );
    }
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      // send around a new slotlist
      TheGameSpyInfo->setGameOptions();
      WOLDisplaySlotList();// Update the accepted button UI
    }
  }
#endif
}

static void WOLLockSettings()
{
	buttonBack->winEnable(false);
	checkBoxLimitSuperweapons->winEnable(false);
	comboBoxStartingCash->winEnable(false);

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		comboBoxPlayer[i]->winEnable(false);
		comboBoxColor[i]->winEnable(false);
		comboBoxPlayerTemplate[i]->winEnable(false);
		comboBoxTeam[i]->winEnable(false);
		buttonMapStartPosition[i]->winEnable(false);
	}
}

static void StartPressed()
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (!myGame)
		return;

	OnlineGameSetupActions::StartPressCallbacks callbacks;
	callbacks.chatLine = addGameSetupChatLine;
	callbacks.setStartButtonEnabled = []( Bool enabled ) { if( buttonStart != nullptr ) buttonStart->winEnable( enabled ); };
	callbacks.setBackButtonEnabled = []( Bool enabled ) { if( buttonBack != nullptr ) buttonBack->winEnable( enabled ); };
	callbacks.setSelectMapButtonEnabled = []( Bool enabled ) { if( buttonSelectMap != nullptr ) buttonSelectMap->winEnable( enabled ); };

	OnlineGameSetupActions::pressStart( myGame, callbacks );
}


//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void WOLDisplayGameOptions()
{
	if (!parentWOLGameSetup)
		return;

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* theGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (pLobbyInterface == nullptr || theGame == nullptr)
	{
		return;
	}

#if defined(GENERALS_ONLINE)
	OnlineGameSetupData data = OnlineGameSetupData::build(theGame);
	GadgetStaticTextSetText(textEntryMapDisplay, data.m_mapDisplayText);
#else
	const GameSlot *localSlot = NULL;
	if (theGame->getLocalSlotNum() >= 0)
		localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());

	AsciiString map = theGame->getMap();
	const MapMetaData *md = TheMapCache->findMap(map);
	if (md && localSlot && localSlot->hasMap())
	{
		GadgetStaticTextSetText(textEntryMapDisplay, md->m_displayName);
	}
	else
	{
		AsciiString s = TheGameSpyInfo->getCurrentStagingRoom()->getMap();
		if (s.reverseFind('\\'))
		{
			s = s.reverseFind('\\') + 1;
		}
		UnicodeString mapDisplay;
		mapDisplay.translate(s);
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplay);
	}
#endif
	WOLPositionStartSpots();
	updateMapStartSpots(theGame, buttonMapStartPosition);

#if defined(GENERALS_ONLINE)
	Bool isUsingStats = data.m_useStats;
#else
  //If our display does not match the current state of game settings, update the checkbox.
  Bool isUsingStats = TheGameSpyInfo->getCurrentStagingRoom()->getUseStats() ? TRUE : FALSE;
#endif
  if (GadgetCheckBoxIsChecked(checkBoxUseStats) != isUsingStats)
  {
  	GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats);
    checkBoxUseStats->winSetTooltip( TheGameText->fetch( isUsingStats ? "TOOLTIP:UseStatsOn" : "TOOLTIP:UseStatsOff" ) );
  }

#if defined(GENERALS_ONLINE)
  Bool oldFactionsOnly = data.m_limitArmies;
#else
  Bool oldFactionsOnly = theGame->oldFactionsOnly();
#endif
  if (GadgetCheckBoxIsChecked(checkBoxLimitArmies) != oldFactionsOnly)
  {
    GadgetCheckBoxSetChecked(checkBoxLimitArmies, oldFactionsOnly);
    // Repopulate the lists of available armies, since the old list is now wrong
    for (Int i = 0; i < MAX_SLOTS; i++)
    {
      PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, theGame, theGame->getAllowObservers() );

      // Make sure selections are up to date on all machines
      handlePlayerTemplateSelection(i) ;
    }
  }

#if defined(GENERALS_ONLINE)
  // Note: must check if checkbox is already correct to avoid infinite recursion
  Bool limitSuperweapons = data.m_options.m_superweaponsRestricted;
#else
  // Note: must check if checkbox is already correct to avoid infinite recursion
  Bool limitSuperweapons = (theGame->getSuperweaponRestriction() != 0);
#endif
  if ( limitSuperweapons != GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons))
    GadgetCheckBoxSetChecked( checkBoxLimitSuperweapons, limitSuperweapons );

  Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
  Int index = 0;
  for ( ; index < itemCount; index++ )
  {
    Int value  = (Int)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
    if ( value == theGame->getStartingCash().countMoney() )
    {
      // Note: must check if combobox is already correct to avoid infinite recursion
      Int selectedIndex;
      GadgetComboBoxGetSelectedPos( comboBoxStartingCash, &selectedIndex );
      if ( index != selectedIndex )
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);

      break;
    }
  }

  DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", theGame->getStartingCash().countMoney() ) );
}


//  -----------------------------------------------------------------------------------------
// The Bad munkee slot list displaying function
//-------------------------------------------------------------------------------------------------
static Int WOLUpdateConnectionLevel(Int level, Int score)
{
    if (level < 0)
    {
        level = 0;
        while (level < CONNECTION_LEVEL_COUNT - 1 && score >= connectionLevelThresholds[level])
            ++level;
        return level;
    }

    while (level < CONNECTION_LEVEL_COUNT - 1 && score >= connectionLevelThresholds[level] + CONNECTION_LEVEL_HYSTERESIS)
        ++level;
    while (level > 0 && score < connectionLevelThresholds[level - 1] - CONNECTION_LEVEL_HYSTERESIS)
        --level;
    return level;
}

static void WOLRefreshConnectionIndicators(void)
{
    NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
    NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        NGMPGameSlot* slot = game->getGameSpySlot(i);
        if (slot == nullptr || !slot->isHuman() || i == game->getLocalSlotNum())
        {
            if (genericPingWindow[i])
                genericPingWindow[i]->winHide(TRUE);
            connectionIndicatorState[i] = CONNECTION_INDICATOR_CONNECTING;
            continue;
        }

        if (genericPingWindow[i] == nullptr)
            continue;

        genericPingWindow[i]->winHide(FALSE);

        EConnectionState connectionState = EConnectionState::NOT_CONNECTED;
        int connectionScore = -1;

        if (pMesh != nullptr)
        {
            PlayerConnection* pConnection = pMesh->GetConnectionForUser(slot->m_userID);
            if (pConnection != nullptr)
            {
                connectionState = pConnection->GetState();
                connectionScore = pConnection->ComputeConnectionScore();
            }
        }

        Int& state = connectionIndicatorState[i];
        if (connectionState == EConnectionState::CONNECTION_FAILED || connectionState == EConnectionState::CONNECTION_DISCONNECTED)
            state = CONNECTION_INDICATOR_WARNING;
        else if (connectionState != EConnectionState::CONNECTED_DIRECT || connectionScore < 0)
            state = CONNECTION_INDICATOR_CONNECTING;
        else
            state = WOLUpdateConnectionLevel(state, connectionScore);
    }
}

//-------------------------------------------------------------------------------------------------
/** Draws the connection indicator: a spinner while connecting, a warning triangle on failure, else signal bars. */
//-------------------------------------------------------------------------------------------------
static void WOLConnectionIndicatorDraw(GameWindow *window, WinInstanceData *instData)
{
    Int slot = -1;
    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        if (genericPingWindow[i] == window)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    Int x, y, width, height;
    window->winGetScreenPosition(&x, &y);
    window->winGetSize(&width, &height);

    const Int size = (Int)(min(width, height) * 0.7f);
    const Int left = x + (width - size) / 2;
    const Int top = y + (height - size) / 2;
    const Real centerX = left + size * 0.5f;
    const Real centerY = top + size * 0.5f;
    const Int state = connectionIndicatorState[slot];

    if (state == CONNECTION_INDICATOR_CONNECTING)
    {
        // Faint full ring with a solid arc rotating around it, drawn as short segments.
        const Int segmentCount = 24;
        const Int arcSegments = 7;
        const Real radius = size * 0.4f;
        const Real rotation = (timeGetTime() % 900) * 2.0f * PI / 900.0f;
        const Color ringColor = GameMakeColor(47, 55, 168, 70); // Zero Hour UI blue
        const Color arcColor = GameMakeColor(47, 55, 168, 255);
        for (Int segment = 0; segment < segmentCount; ++segment)
        {
            const Real startAngle = rotation + segment * 2.0f * PI / segmentCount;
            const Real endAngle = startAngle + 2.0f * PI / segmentCount;
            TheDisplay->drawLine((Int)(centerX + std::cos(startAngle) * radius), (Int)(centerY + std::sin(startAngle) * radius),
                (Int)(centerX + std::cos(endAngle) * radius), (Int)(centerY + std::sin(endAngle) * radius),
                3.0f, segment < arcSegments ? arcColor : ringColor);
        }
    }
    else if (state == CONNECTION_INDICATOR_WARNING)
    {
        const Color amber = GameMakeColor(255, 190, 0, 255);
        const Int apexY = top + (Int)(size * 0.1f);
        const Int baseY = top + (Int)(size * 0.9f);
        const Int baseLeft = left + (Int)(size * 0.05f);
        const Int baseRight = left + (Int)(size * 0.95f);
        TheDisplay->drawLine((Int)centerX, apexY, baseLeft, baseY, 1.5f, amber);
        TheDisplay->drawLine((Int)centerX, apexY, baseRight, baseY, 1.5f, amber);
        TheDisplay->drawLine(baseLeft, baseY, baseRight, baseY, 1.5f, amber);
        TheDisplay->drawLine((Int)centerX, top + (Int)(size * 0.38f), (Int)centerX, top + (Int)(size * 0.66f), 2.0f, amber);
        TheDisplay->drawFillRect((Int)centerX - 1, top + (Int)(size * 0.73f), 2, 2, amber);
    }
    else
    {
        Color levelColor = GameMakeColor(220, 40, 40, 255);
        if (state >= 3)
            levelColor = GameMakeColor(0, 200, 0, 255);
        else if (state == 2)
            levelColor = GameMakeColor(255, 190, 0, 255);
        const Color emptyColor = GameMakeColor(90, 90, 90, 200);

        const Int gap = 1;
        const Int barWidth = max(1, (size - gap * (CONNECTION_LEVEL_COUNT - 1)) / CONNECTION_LEVEL_COUNT);
        for (Int bar = 0; bar < CONNECTION_LEVEL_COUNT; ++bar)
        {
            const Int barHeight = max(1, size * (bar + 1) / CONNECTION_LEVEL_COUNT);
            TheDisplay->drawFillRect(left + bar * (barWidth + gap), top + size - barHeight, barWidth, barHeight,
                bar <= state ? levelColor : emptyColor);
        }
    }
}

void WOLDisplaySlotList(void)
{
    // TODO_NGMP
    //if (!parentWOLGameSetup || !TheGameSpyInfo->getCurrentStagingRoom())
    //	return;

    NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
    NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    DEBUG_ASSERTCRASH(!game->getConstSlot(0)->isOpen(), ("Open host!"));

    UpdateSlotList(game, comboBoxPlayer, comboBoxColor,
        comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

    WOLDisplayGameOptions();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        NGMPGameSlot* slot = game->getGameSpySlot(i);
        if (slot && slot->isHuman())
        {
            // Determine friends and blocked players in lobby setup and highlight them
            Color nameColor = GameSpyColor[GSCOLOR_PLAYER_NORMAL];
            NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();

            if (pSocialInterface != nullptr && pSocialInterface->IsUserFriend(slot->m_userID))
            {
                nameColor = GameSpyColor[GSCOLOR_PLAYER_BUDDY];
            }

            else if (pSocialInterface != nullptr && pSocialInterface->IsUserIgnored(slot->m_userID))
            {
                nameColor = GameSpyColor[GSCOLOR_PLAYER_IGNORED];
            }

            if (comboBoxPlayer[i])
            {
                GadgetTextEntrySetTextColor(GadgetComboBoxGetEditBox(comboBoxPlayer[i]), nameColor);
            }
        }
    }

    WOLRefreshConnectionIndicators();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitWOLGameGadgets()
{
	ClearGSMessageBoxes();

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* theGameInfo = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (theGameInfo == nullptr || pLobbyInterface == nullptr)
	{
		return;
	}

#if defined(GENERALS_ONLINE)
	OnlineGameSetupData initSetupData = OnlineGameSetupData::build(theGameInfo);
#endif

	for (Int i = 0; i < MAX_SLOTS; ++i)
		connectionIndicatorState[i] = CONNECTION_INDICATOR_CONNECTING;

	//Initialize the gadget IDs
	parentWOLGameSetupID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:GameSpyGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonStart" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryMapDisplay" );
	listboxGameSetupChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ListboxChatWindowGameSpyGameSetup" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonEmote" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonSelectMap" );
	checkBoxUseStatsID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:CheckBoxUseStats" );
	windowMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:MapWindow" );
  checkBoxLimitSuperweaponsID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons");
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash");
  checkBoxLimitArmiesID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies");
	windowMapSelectMapID = TheNameKeyGenerator->nameToKey("WOLMapSelectMenu.wnd:WinMapPreview");

	NameKeyType staticTextTitleID = NAMEKEY("GameSpyGameOptionsMenu.wnd:StaticTextGameName");

	// Initialize the pointers to our gadgets
	parentWOLGameSetup = TheWindowManager->winGetWindowFromId( NULL, parentWOLGameSetupID );
	buttonEmote = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonEmoteID  );
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonSelectMapID  );
	checkBoxUseStats = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxUseStatsID );
	buttonStart = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonStartID  );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,  buttonBackID);
	listboxGameSetupChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, listboxGameSetupChatID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryChatID );
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryMapDisplayID );
	windowMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,windowMapID  );
	SetListBoxRowAnimMode(listboxGameSetupChat, LIST_ROW_ANIM_SLOT);
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the parentWOLGameSetup.wnd:MapWindow" ));

  checkBoxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitSuperweaponsID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons" ));
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash" ));

#if defined(GENERALS_ONLINE)
  PopulateStartingCashComboBox(comboBoxStartingCash, theGameInfo);
#else
  PopulateStartingCashComboBox( comboBoxStartingCash, TheGameSpyGame );
#endif
  checkBoxLimitArmies = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitArmiesID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies" ));

  // Limit Armies can ONLY be set in the Host Game window (PopupHostGame.wnd)
  checkBoxLimitArmies->winEnable( false );
  // Ditto use stats
  checkBoxUseStats->winEnable( false );

#if defined(GENERALS_ONLINE)
  Int isUsingStats = initSetupData.m_useStats;
#else
  Int isUsingStats = TheGameSpyGame->getUseStats();
#endif

	
  GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats );
  checkBoxUseStats->winSetTooltip( TheGameText->fetch( isUsingStats ? "TOOLTIP:UseStatsOn" : "TOOLTIP:UseStatsOff" ) );

#if defined(GENERALS_ONLINE)
  if (!initSetupData.m_isHost)
#else
  if ( !TheGameSpyGame->amIHost() )
#endif
  {
    checkBoxLimitSuperweapons->winEnable( false );
    comboBoxStartingCash->winEnable( false );
		NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
		TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID)->winEnable( FALSE );
  }
#if defined(GENERALS_ONLINE)
  else
  {
	  checkBoxLimitSuperweapons->winEnable(true);
	  comboBoxStartingCash->winEnable(true);
  }
#endif


#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	if (isUsingStats)
	{
		// Recorded stats games can never limit superweapons, limit armies, or have inflated starting cash.
		// This should probably be enforced at the gamespy level as well, to prevent expoits.
		checkBoxLimitSuperweapons->winEnable( FALSE );
		comboBoxStartingCash->winEnable( FALSE );
		checkBoxLimitArmies->winEnable( FALSE );
		NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
		TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID)->winEnable( FALSE );
	}
#endif

	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, staticTextTitleID );
	if (staticTextTitle)
	{
#if defined(GENERALS_ONLINE)
		GadgetStaticTextSetText(staticTextTitle, initSetupData.m_gameName);
#else
		GadgetStaticTextSetText(staticTextTitle, TheGameSpyGame->getGameName());
#endif
	}

	if (!theGameInfo)
	{
		DEBUG_CRASH(("No staging room!"));
		return;
	}

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		comboBoxPlayer[i]->winSetTooltipFunc(playerTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:StaticTextPlayer%d", i);
		staticTextPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		staticTextPlayer[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, staticTextPlayerID[i] );
		staticTextPlayer[i]->winSetTooltipFunc(playerTooltip);
		
		bool bIsHost = pLobbyInterface->IsHost();
		if (bIsHost)
			staticTextPlayer[i]->winHide(TRUE);

		if (theGameInfo->getLocalSlotNum() != i)
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Open"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Closed"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:EasyAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:MediumAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:HardAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		}
		else
		{
			// Local player, so add the local player name
			NGMPGameSlot* slot = theGameInfo->getGameSpySlot(i);
			if (slot)
			{
				GadgetComboBoxAddEntry(comboBoxPlayer[i], slot->getName(), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
			}
		}

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));

		PopulateColorComboBox(i, comboBoxColor, theGameInfo);
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));

		PopulateTeamComboBox(i, comboBoxTeam, theGameInfo);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonAccept%d", i);
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:GenericPing%d", i);
		genericPingWindowID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		genericPingWindow[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, genericPingWindowID[i] );
		DEBUG_ASSERTCRASH(genericPingWindow[i], ("Could not find the genericPingWindow[%d]",i ));
		genericPingWindow[i]->winSetTooltipFunc(pingTooltip);
		genericPingWindow[i]->winSetDrawFunc(WOLConnectionIndicatorDraw);

//		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE);

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}

	if( buttonAccept[0] )
		buttonAccept[0]->winEnable(TRUE);

	if (buttonBack != NULL)
	{
		buttonBack->winEnable(TRUE);
	}
		//GadgetButtonSetEnabledColor(buttonAccept[0], GameSpyColor[GSCOLOR_ACCEPT_TRUE]);

		// TODO_NGMP: Where does this happen in the normal game?
#if defined(GENERALS_ONLINE)
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, theGameInfo, TRUE);

		// Make sure selections are up to date on all machines
		handlePlayerTemplateSelection(i, true);
	}
#endif
}

void DeinitWOLGameGadgets()
{
	parentWOLGameSetup = NULL;
	buttonEmote = NULL;
	buttonSelectMap = NULL;
	buttonStart = NULL;
	buttonBack = NULL;
	listboxGameSetupChat = NULL;
	textEntryChat = NULL;
	textEntryMapDisplay = NULL;
	if (windowMap)
	{
		windowMap->winSetUserData(NULL);
		windowMap = NULL;
	}
	checkBoxUseStats = NULL;
  checkBoxLimitSuperweapons = NULL;
  comboBoxStartingCash = NULL;

//	GameWindow *staticTextTitle = NULL;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = NULL;
		staticTextPlayer[i] = NULL;
		comboBoxColor[i] = NULL;
		comboBoxPlayerTemplate[i] = NULL;
		comboBoxTeam[i] = NULL;
		buttonAccept[i] = NULL;
//		buttonStartPosition[i] = NULL;
		buttonMapStartPosition[i] = NULL;
		genericPingWindow[i] = NULL;
	}
}

static Bool initDone = false;
UnsignedInt lastSlotlistTime = 0;
UnsignedInt enterTime = 0;
Bool initialAcceptEnable = FALSE;

//-------------------------------------------------------------------------------------------------
/** Widget-side listeners of OnlineGameSetupSignals: every async NGMP event and per-frame
	update this screen used to handle by touching its own GameWindows directly now goes through
	these lambdas instead, so OnlineGameSetupSession stays GameWindow-free. Connected in Init
	and dropped in shutdownComplete() (the per-frame update keeps running through the exit
	animation), since they close over this .wnd's own (file-static) widget pointers. */
//-------------------------------------------------------------------------------------------------
static SignalConnections s_setupConnections;

static void ConnectGameSetupSignals()
{
	s_setupConnections.disconnect();

	s_setupConnections.add( OnlineGameSetupSignals::chatLine().connect( []( const UnicodeString &text, Color color )
	{
		if( listboxGameSetupChat )
		{
			GadgetListBoxAddEntryText( listboxGameSetupChat, text, color, -1, -1 );
		}
		else if( TheNGMPGame && TheNGMPGame->isGameInProgress() && ScoreScreenSignals::chatLine().hasListeners() )
		{
			// Setup .wnd is torn down once the match starts; forward score-screen chat
			// (internet games) the same way WOLGameSetupMenu's disconnect notice does.
			ScoreScreenSignals::chatLine().emit( text, color );
		}
	} ) );

	s_setupConnections.add( OnlineGameSetupSignals::slotsChanged().connect( []() { WOLDisplaySlotList(); } ) );
	s_setupConnections.add( OnlineGameSetupSignals::optionsChanged().connect( []() { WOLDisplayGameOptions(); } ) );

	s_setupConnections.add( OnlineGameSetupSignals::becameHost().connect( []()
	{
		if( buttonStart != nullptr )
		{
			buttonStart->winSetText( TheGameText->fetch( "GUI:Start" ) );
			buttonStart->winEnable( TRUE );
		}
		if( buttonSelectMap != nullptr )
			buttonSelectMap->winEnable( TRUE );
		initialAcceptEnable = TRUE;
		if( comboBoxStartingCash != nullptr )
			comboBoxStartingCash->winEnable( TRUE );
		if( checkBoxLimitSuperweapons != nullptr )
			checkBoxLimitSuperweapons->winEnable( TRUE );
	} ) );

	s_setupConnections.add( OnlineGameSetupSignals::backButtonEnabled().connect( []( Bool enabled ) { if( buttonBack != nullptr ) buttonBack->winEnable( enabled ); } ) );
	s_setupConnections.add( OnlineGameSetupSignals::startButtonEnabled().connect( []( Bool enabled ) { if( buttonStart != nullptr ) buttonStart->winEnable( enabled ); } ) );

	s_setupConnections.add( OnlineGameSetupSignals::communicatorButtonEnabled().connect( []( Bool enabled )
	{
		GameWindow *buttonBuddy = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "GameSpyGameOptionsMenu.wnd:ButtonCommunicator" ) );
		if( buttonBuddy != nullptr )
			buttonBuddy->winEnable( enabled );
	} ) );

	s_setupConnections.add( OnlineGameSetupSignals::lockSettings().connect( []() { WOLLockSettings(); } ) );

	s_setupConnections.add( OnlineGameSetupSignals::communicatorCount().connect( []( int numNotifications )
	{
		GameWindow *buttonBuddy = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "GameSpyGameOptionsMenu.wnd:ButtonCommunicator" ) );
		if( buttonBuddy != nullptr )
		{
			UnicodeString buttonText;
			if( numNotifications > 0 )
				buttonText.format( L"%s [%d]", TheGameText->fetch( "GUI:Buddies" ).str(), numNotifications );
			else
				buttonText.format( L"%s", TheGameText->fetch( "GUI:Buddies" ).str() );
			buttonBuddy->winSetText( buttonText );
		}
	} ) );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuInit( WindowLayout *layout, void *userData )
{
	ConnectGameSetupSignals();

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	// NGMP async-callback registration + entry chat notices + Communicator badge init all move to
	// OnlineGameSetupSession::enter() (called below, after this screen's widgets exist) so a future
	// RmlUi front end drives the same room without any GameWindow. See ConnectGameSetupSignals().

	if (TheNGMPGame == nullptr || (TheNGMPGame && TheNGMPGame->isGameInProgress()))
	{
		if (TheNGMPGame != nullptr)
		{
			TheNGMPGame->setGameInProgress(FALSE);
		}

		pLobbyInterface->LeaveCurrentLobby();

		// check if we were disconnected

		// TODO_NGMP: Handle disconnected
		/*
		Int disconReason;
		if (TheGameSpyInfo->isDisconnectedAfterGameStart(&disconReason))
		{
			AsciiString disconMunkee;
			disconMunkee.format("GUI:GSDisconReason%d", disconReason);
			UnicodeString title, body;
			title = TheGameText->fetch( "GUI:GSErrorTitle" );
			body = TheGameText->fetch( disconMunkee );
			GameSpyCloseAllOverlays();
			GSMessageBoxOk( title, body );
			TheGameSpyInfo->reset();
			DEBUG_LOG(("WOLGameSetupMenuInit() - game was in progress, and we were disconnected, so pop immediate back to main menu"));
			TheShell->popImmediate();
			return;
		}
		*/

		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("WOLGameSetupMenuInit() - game was in progress, so pop immediate back to lobby"));
		TheShell->popImmediate();

		// TODO_NGMP: Only do this if still connected to service
		//if (TheGameSpyPeerMessageQueue && TheGameSpyPeerMessageQueue->isConnected())
		{
			DEBUG_LOG(("We're still connected, so pushing back on the lobby"));
			TheShell->push("Menus/WOLCustomLobby.wnd", TRUE);
		}

		return;
	}

#if !defined(GENERALS_ONLINE)
	TheGameSpyInfo->setCurrentGroupRoom(0);
#endif

	delete TheNAT;
	TheNAT = NULL;

	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	launchGameNext = FALSE;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitWOLGameGadgets();
	EnableSlotListUpdates(TRUE);
	// TODO_NGMP
	//TheGameSpyInfo->registerTextWindow(listboxGameSetupChat);

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();

	// Host/client game-state setup (accept flag, color/template/ping, starting cash, superweapon
	// restriction, old-factions-only, map CRC/size, slot states) moved to
	// OnlineGameSetupSession::prepareGameState() so a widget-free screen gets the same initial state.
	if (pLobbyInterface == nullptr || pLobbyInterface->GetCurrentGame() == nullptr)
	{
		return;
	}

	OnlineGameSetupSession::prepareGameState();

	bool bIsHost = pLobbyInterface->IsHost();

	if (bIsHost)
	{
		WOLDisplaySlotList();
		WOLDisplayGameOptions();

		// TheSuperHackers @tweak disable the combo box for the host's player name
		comboBoxPlayer[0]->winEnable(FALSE);
	}
	else
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			//I'm a client, disable the controls I can't touch.
			comboBoxPlayer[i]->winEnable(FALSE);

			comboBoxColor[i]->winEnable(FALSE);
			comboBoxPlayerTemplate[i]->winEnable(FALSE);
			comboBoxTeam[i]->winEnable(FALSE);
//			buttonStartPosition[i]->winEnable(FALSE);
			buttonMapStartPosition[i]->winEnable(FALSE);

		}
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonStart->winEnable( FALSE );
		buttonSelectMap->winEnable( FALSE );
		initialAcceptEnable = FALSE;

		WOLDisplaySlotList();
		WOLDisplayGameOptions();
	}

	// Show the Menu
	layout->hide( FALSE );

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxGameSetupChat );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	initDone = true;

	// TODO_NGMP
	//TheGameSpyInfo->setGameOptions();
	//TheShell->registerWithAnimateManager(parentWOLGameSetup, WIN_ANIMATION_SLIDE_TOP, TRUE);
	WOLPositionStartSpots();

	lastSlotlistTime = 0;
	enterTime = timeGetTime();

	// Set Keyboard to chat entry
	raiseMessageBoxes = true;
	TheTransitionHandler->setGroup("GameSpyGameOptionsMenuFade");
	TheWindowManager->winSetFocus(textEntryChat);

	// NGMP async-callback registration, the entry chat notices (camera height, join policy,
	// observers), and the Communicator badge's initial state -- all routed through the same signals
	// WOLGameSetupMenuUpdate() uses (see ConnectGameSetupSignals()).
	OnlineGameSetupSession::enter();
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;
	s_setupConnections.disconnect();

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		// TODO_NGMP: Handle disconnect again
		if (false)
		//if (!TheGameSpyPeerMessageQueue || !TheGameSpyPeerMessageQueue->isConnected())
		{
			DEBUG_LOG(("GameSetup shutdownComplete() - skipping push because we're disconnected"));
		}
		else
		{
			TheShell->push(nextScreen);
		}
	}

	/*
	if (launchGameNext)
	{
		TheNGMPGame->launchGame();
		TheGameSpyInfo->leaveStagingRoom();
	}
	*/

	nextScreen = NULL;

}

//-------------------------------------------------------------------------------------------------
/** GameSpy Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuShutdown( WindowLayout *layout, void *userData )
{
	OnlineGameSetupSession::leave();

	// drop any in-flight mesh connectivity check so a late reply never fires into this now-dead menu
	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->ClearConnectivityCheckCallback();
	}

	//TheGameSpyInfo->unregisterTextWindow(listboxGameSetupChat);

	if( WOLMapSelectLayout )
	{
		WOLMapSelectLayout->destroyWindows();
		deleteInstance(WOLMapSelectLayout);
		WOLMapSelectLayout = NULL;
	}
	parentWOLGameSetup = NULL;
	EnableSlotListUpdates(FALSE);
	DeinitWOLGameGadgets();
	if (TheEstablishConnectionsMenu != NULL)
	{
		TheEstablishConnectionsMenu->endMenu();
	}
	initDone = false;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();

	RaiseGSMessageBox();
	TheTransitionHandler->reverse("GameSpyGameOptionsMenuFade");
}

static void fillPlayerInfo(const PeerResponse *resp, PlayerInfo *info)
{
	info->m_name			= resp->nick.c_str();
	info->m_profileID	= resp->player.profileID;
	info->m_flags			= resp->player.flags;
	info->m_wins			= resp->player.wins;
	info->m_losses		= resp->player.losses;
	info->m_locale		= resp->locale.c_str();
	info->m_rankPoints= resp->player.rankPoints;
	info->m_side			= resp->player.side;
	info->m_preorder	= resp->player.preorder;
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuUpdate( WindowLayout * layout, void *userData)
{
	// Refresh only the fast-changing connection indicators each frame.
	WOLRefreshConnectionIndicators();

	// need to exit?
	if (OnlineSessionExit::isTeardownReady())
	{
		bool bForceShutdown = true;
		WOLGameSetupMenuShutdown(layout, (void*)&bForceShutdown); // userdata is 'force shutdown'
		OnlineSessionExit::tearDownAndPop();
		return;
	}
	
	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
		return;
	}

	// Anticheat teardown, host migration, host-left, and the match-start countdown tick all live in
	// OnlineGameSetupSession::update() now -- same order, same guards, routed through the signals
	// instead of touching listboxGameSetupChat/buttonStart/etc. directly. TRUE mirrors the host-left
	// early `return;` the .wnd path always had.
	if (OnlineGameSetupSession::update())
	{
		buttonPushed = true;
		return;
	}

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

#if defined(GENERALS_ONLINE) // GO needs to tick this, so notifications disappear etc
	HandleBuddyResponses();
#endif

	if (TheShell->isAnimFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		HandleBuddyResponses();
		HandlePersistentStorageResponses();

		if (TheNGMPGame && TheNGMPGame->isGameInProgress())
		{
			if (TheGameSpyInfo->isDisconnectedAfterGameStart(NULL))
			{
				return; // already been disconnected, so don't worry.
			}

			Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
			Bool sawImportantMessage = FALSE;
			PeerResponse resp;
			while (allowedMessages-- && !sawImportantMessage && TheGameSpyPeerMessageQueue->getResponse( resp ))
			{
				switch (resp.peerResponseType)
				{
				case PeerResponse::PEERRESPONSE_DISCONNECT:
					{
					// TODO_NGMP: hook this up again
						sawImportantMessage = TRUE;
						AsciiString disconMunkee;
						disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);

						// check for scorescreen
						NameKeyType listboxChatWindowScoreScreenID = NAMEKEY("ScoreScreen.wnd:ListboxChatWindowScoreScreen");
						GameWindow *listboxChatWindowScoreScreen = TheWindowManager->winGetWindowFromId( NULL, listboxChatWindowScoreScreenID );
						if (listboxChatWindowScoreScreen)
						{
							GadgetListBoxAddEntryText(listboxChatWindowScoreScreen, TheGameText->fetch(disconMunkee),
								GameSpyColor[GSCOLOR_DEFAULT], -1);
						}
						else if (ScoreScreenSignals::chatLine().hasListeners())
						{
							// non-.wnd score screen (e.g. RmlUi): no listbox to check for
							ScoreScreenSignals::chatLine().emit(TheGameText->fetch(disconMunkee), GameSpyColor[GSCOLOR_DEFAULT]);
						}
						else
						{
							// still ingame
							TheInGameUI->message(disconMunkee);
						}
						TheGameSpyInfo->markAsDisconnectedAfterGameStart(resp.discon.reason);
					}
				}
			}

			return; // if we're in game, all we care about is if we've been disconnected from the chat server
		}

		Bool isHosting = TheGameSpyInfo->amIHost(); // only while in game setup screen
		isHosting = isHosting || (TheNGMPGame && TheNGMPGame->isInGame() && TheNGMPGame->amIHost()); // while in game
		if (!isHosting && !lastSlotlistTime && timeGetTime() > enterTime + 10000)
		{
			// don't do this if we're disconnected
			if (TheGameSpyPeerMessageQueue->isConnected())
			{
				// haven't seen ourselves
				buttonPushed = true;
				DEBUG_LOG(("Haven't seen ourselves in slotlist"));
				if (TheNGMPGame)
					TheNGMPGame->reset();
				TheGameSpyInfo->leaveStagingRoom();
				//TheGameSpyInfo->joinBestGroupRoom();
				GSMessageBoxOk(TheGameText->fetch("GUI:HostLeftTitle"), TheGameText->fetch("GUI:HostLeft"));
				nextScreen = "Menus/WOLCustomLobby.wnd";
				TheShell->pop();
			}
			return;
		}

		if (TheNAT != NULL) {
			NATStateType NATState = TheNAT->update();
			if (NATState == NATSTATE_DONE)
			{
				//launchGameNext = TRUE;
				//TheShell->pop();
				TheNGMPGame->launchGame();
				if (TheGameSpyInfo) // this can be blown away by a disconnect on the map transfer screen
					TheGameSpyInfo->leaveStagingRoom();
				return;
			}
			else if (NATState == NATSTATE_FAILED)
			{
				// Just back out.  This cleans up some slot list problems
				buttonPushed = true;

				// delete TheNAT, its no good for us anymore.
				delete TheNAT;
				TheNAT = NULL;

				NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
				if (pLobbyInterface != nullptr)
				{
					NGMPGame* myGame = pLobbyInterface->GetCurrentGame();
					if (myGame != nullptr)
					{
						myGame->reset();
					}
				}
				
				TheGameSpyInfo->leaveStagingRoom();
				//TheGameSpyInfo->joinBestGroupRoom();
				GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:NATNegotiationFailed"));
				nextScreen = "Menus/WOLCustomLobby.wnd";
				TheShell->pop();
				return;
			}
		}

		PeerResponse resp;

		Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
		Bool sawImportantMessage = FALSE;
		while (allowedMessages-- && !sawImportantMessage)
		{

		if (!TheLobbyQueuedUTMs.empty())
		{
			DEBUG_LOG(("Got response from queued lobby UTM list"));
			resp = TheLobbyQueuedUTMs.front();
			TheLobbyQueuedUTMs.pop_front();
		}
		else if (TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
			DEBUG_LOG(("Got response from message queue"));
		}
			else
		{
				break;
			}
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_FAILEDTOHOST:
				{
					// oops - we've not heard from the qr server.  bail.
					GadgetListBoxAddEntryText(listboxGameSetupChat, TheGameText->fetch("GUI:GSFailedToHost"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
				}
				break;
			case PeerResponse::PEERRESPONSE_GAMESTART:
				{
					sawImportantMessage = TRUE;
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

					NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
					if (pLobbyInterface == nullptr || !myGame || !myGame->isInGame())
						break;

					if (!TheNGMPGame)
						break;

					SendStatsToOtherPlayers(TheNGMPGame);

					GameWindow *buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
					if (buttonBuddy)
						buttonBuddy->winEnable(FALSE);
					GameSpyCloseOverlay(GSOVERLAY_BUDDY);

					*TheNGMPGame = *myGame;
					TheNGMPGame->startGame(0);
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);
					WOLDisplaySlotList();
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERINFO:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);
					WOLDisplaySlotList();
					// send out new slotlist if I'm host
					TheGameSpyInfo->setGameOptions();
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERJOIN:
				{
					if (resp.player.roomType != StagingRoom)
					{
						break;
					}
					sawImportantMessage = TRUE;
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);

					if (p.m_profileID)
					{
						if (TheGameSpyPSMessageQueue->findPlayerStatsByID(p.m_profileID).id == 0)
						{
							PSRequest req;
							req.requestType = PSRequest::PSREQUEST_READPLAYERSTATS;
							req.player.id = p.m_profileID;
							TheGameSpyPSMessageQueue->addRequest(req);
						}
					}

					// check if we have room for the dude
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

					NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
					if (TheGameSpyInfo->amIHost() && game)
					{
						if (TheNAT)
						{
							// ditch him
							PeerRequest req;
							req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
							req.UTM.isStagingRoom = TRUE;
							req.id = "KICK/";
							req.nick = p.m_name.str();
							req.options = "GameStarted";
							TheGameSpyPeerMessageQueue->addRequest(req);
						}
						else
						{
							// look for room for him
							// See if there's room
							// First get the number of players currently in the room.
							Int numPlayers = 0;
							for (Int player = 0; player < MAX_SLOTS; ++player)
							{
								if (game->getSlot(player)->isOccupied() &&
									game->getSlot(player)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
								{
									++numPlayers;
								}
							}

							// now get the number of starting spots on the map.
							Int numStartingSpots = MAX_SLOTS;
							const MapMetaData *md = TheMapCache->findMap(game->getMap());
							if (md != NULL)
							{
								numStartingSpots = md->m_numPlayers;
							}

							Int openSlotIndex = -1;
							for (Int i=0; i<MAX_SLOTS; ++i)
							{
								const GameSlot *slot = game->getConstSlot(i);
								if (slot && slot->isOpen())
								{
									openSlotIndex = i;
									break;
								}
							}

							if (openSlotIndex >= 0)
							{
								// add him
								GameSlot newSlot;
								UnicodeString uName;
								uName.translate(p.m_name);
								newSlot.setState(SLOT_PLAYER, uName);
								newSlot.setIP(ntohl(resp.player.IP));
								game->setSlot( openSlotIndex, newSlot );
								game->resetAccepted(); // BGC - need to unaccept everyone if someone joins the game.
							}
							else
							{
								// ditch him
								PeerRequest req;
								req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
								req.UTM.isStagingRoom = TRUE;
								req.id = "KICK/";
								req.nick = p.m_name.str();
								req.options = "GameFull";
								TheGameSpyPeerMessageQueue->addRequest(req);
							}

							// send out new slotlist if I'm host
							TheGameSpyInfo->setGameOptions();
						}
					}
					WOLDisplaySlotList();
				}
				break;

			case PeerResponse::PEERRESPONSE_PLAYERLEFT:
				{
					sawImportantMessage = TRUE;
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->playerLeftGroupRoom(resp.nick.c_str());

					if (TheNGMPGame && TheNGMPGame->isGameInProgress())
					{
						break;
					}

					if (TheNAT == NULL) // don't update slot list if we're trying to start a game
					{

						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

						NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
						if (game && TheGameSpyInfo->amIHost())
						{
							Int idx = game->getSlotNum(resp.nick.c_str());
							if (idx >= 0)
							{
								game->getSlot(idx)->setState(SLOT_OPEN);
								game->resetAccepted(); // BGC - need to unaccept everyone if someone leaves the game.
							}
						}

						// send out new slotlist if I'm host
						TheGameSpyInfo->setGameOptions();
						WOLDisplaySlotList();

						if (game && !TheGameSpyInfo->amIHost())
						{
							Int idx = game->getSlotNum(resp.nick.c_str());
							if (idx == 0)
							{
								// host left
								buttonPushed = true;
								game->reset();
								TheGameSpyInfo->leaveStagingRoom();
								//TheGameSpyInfo->joinBestGroupRoom();
								GSMessageBoxOk(TheGameText->fetch("GUI:HostLeftTitle"), TheGameText->fetch("GUI:HostLeft"));
								nextScreen = "Menus/WOLCustomLobby.wnd";
								TheShell->pop();
							}
						}

					}
				}
				break;

			case PeerResponse::PEERRESPONSE_MESSAGE:
				{
					TheGameSpyInfo->addChat(resp.nick.c_str(), resp.message.profileID,
						UnicodeString(resp.text.c_str()), !resp.message.isPrivate, resp.message.isAction, listboxGameSetupChat);
				}
				break;

			case PeerResponse::PEERRESPONSE_DISCONNECT:
				{
					sawImportantMessage = TRUE;
					UnicodeString title, body;
					AsciiString disconMunkee;
					disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
					title = TheGameText->fetch( "GUI:GSErrorTitle" );
					body = TheGameText->fetch( disconMunkee );
					GameSpyCloseAllOverlays();
					GSMessageBoxOk( title, body );
					TheGameSpyInfo->reset();
					TheShell->pop();
				}
				break;

			case PeerResponse::PEERRESPONSE_ROOMUTM:
				{
					sawImportantMessage = TRUE;
#if defined(RTS_DEBUG)
					if (g_debugSlots)
					{
						DEBUG_LOG(("About to process a room UTM.  Command is '%s', command options is '%s'",
							resp.command.c_str(), resp.commandOptions.c_str()));
					}
#endif
					if (strcmp(resp.command.c_str(), "SL") == 0)
					{
						// slotlist
						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

						NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
						Bool isValidSlotList = game && game->getSlot(0) && game->getSlot(0)->isPlayer( resp.nick.c_str() ) && !TheGameSpyInfo->amIHost();
						if (!isValidSlotList)
						{
							SLOTLIST_DEBUG_LOG(("Not a valid slotlist"));
							if (!game)
							{
								SLOTLIST_DEBUG_LOG(("No game!"));
							}
							else
							{
								if (!game->getSlot(0))
								{
									SLOTLIST_DEBUG_LOG(("No slot 0!"));
								}
								else
								{
									if (TheGameSpyInfo->amIHost())
									{
										SLOTLIST_DEBUG_LOG(("I'm the host!"));
									}
									else
									{
										SLOTLIST_DEBUG_LOG(("Not from the host!  isHuman:%d, name:'%ls', sender:'%s'",
											game->getSlot(0)->isHuman(), game->getSlot(0)->getName().str(),
											resp.nick.c_str()));
									}
								}
							}
						}
						else // isValidSlotList
						{
							Int oldLocalSlotNum = (game->isInGame()) ? game->getLocalSlotNum() : -1;
							Bool wasInGame = oldLocalSlotNum >= 0;
							AsciiString oldMap = game->getMap();
							UnsignedInt oldMapCRC, newMapCRC;
							oldMapCRC = game->getMapCRC();

							AsciiString options = resp.commandOptions.c_str();
							options.trim();
							UnsignedShort ports[MAX_SLOTS];
							UnsignedInt ips[MAX_SLOTS];
							Int i;
							for (i=0; i<MAX_SLOTS; ++i)
							{
								if (game && game->getConstSlot(i))
								{
									ips[i] = game->getConstSlot(i)->getIP();
									ports[i] = game->getConstSlot(i)->getPort();
								}
								else
								{
									ips[i] = 0;
									ports[i] = 0;
								}
							}
							Bool optionsOK = ParseAsciiStringToGameInfo(game, options.str());
							if (TheNAT)
							{
								for (i=0; i<MAX_SLOTS; ++i)
								{
									if (game && game->getSlot(i))
									{
#ifdef DEBUG_LOGGING
										UnsignedShort newPort = game->getConstSlot(i)->getPort();
										UnsignedInt newIP = game->getConstSlot(i)->getIP();
										DEBUG_ASSERTLOG(newIP == ips[i], ("IP was different for player %d (%X --> %X)",
											i, ips[i], newIP));
										DEBUG_ASSERTLOG(newPort == ports[i], ("Port was different for player %d (%d --> %d)",
											i, ports[i], newPort));
#endif
										game->getSlot(i)->setPort(ports[i]);
										game->getSlot(i)->setIP(ips[i]);
									}
								}
							}
							Int newLocalSlotNum = (game->isInGame()) ? game->getLocalSlotNum() : -1;
							Bool isInGame = newLocalSlotNum >= 0;
							if (!optionsOK)
							{
								SLOTLIST_DEBUG_LOG(("Options are bad!  bailing!"));
								break;
							}
							else
							{
								SLOTLIST_DEBUG_LOG(("Options are good, local slot is %d", newLocalSlotNum));
								if (!isInGame)
								{
									SLOTLIST_DEBUG_LOG(("Not in game; players are:"));
									for (Int i=0; i<MAX_SLOTS; ++i)
									{
										const NGMPGameSlot *slot = game->getGameSpySlot(i);
										if (slot && slot->isHuman())
										{
											UnicodeString munkee;
											munkee.format(L"\t%d: %ls", i, slot->getName().str());
											SLOTLIST_DEBUG_LOG(("%ls", munkee.str()));
										}
									}
								}
							}
							WOLDisplaySlotList();

							// if I changed map availability, send it across
							newMapCRC = game->getMapCRC();
							if (isInGame)
							{
								lastSlotlistTime = timeGetTime();
								if ( (oldMapCRC ^ newMapCRC) || (!wasInGame && isInGame) )
								{
									// it changed.  send it
									UnicodeString hostName = game->getSlot(0)->getName();
									AsciiString asciiName;
									asciiName.translate(hostName);
									PeerRequest req;
									req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
									req.UTM.isStagingRoom = TRUE;
									req.id = "MAP";
									req.nick = asciiName.str();
									req.options = (game->getSlot(newLocalSlotNum)->hasMap())?"1":"0";
									TheGameSpyPeerMessageQueue->addRequest(req);
									if (!game->getSlot(newLocalSlotNum)->hasMap())
									{
										UnicodeString text;
										UnicodeString mapDisplayName;
										const MapMetaData *mapData = TheMapCache->findMap( game->getMap() );
										Bool willTransfer = TRUE;
										if (mapData)
										{
											mapDisplayName.format(L"%ls", mapData->m_displayName.str());
											willTransfer = !mapData->m_isOfficial;
										}
										else
										{
											mapDisplayName.translate(TheGameState->getMapLeafName(game->getMap()).str());
											willTransfer = WouldMapTransfer(game->getMap());
										}
										if (willTransfer)
											text.format(TheGameText->fetch("GUI:LocalPlayerNoMapWillTransfer"), mapDisplayName.str());
										else
											text.format(TheGameText->fetch("GUI:LocalPlayerNoMap"), mapDisplayName.str());
										GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
									}
								}
								if (!initialAcceptEnable)
								{
									buttonStart->winEnable( TRUE );
									initialAcceptEnable = TRUE;
								}
							}
							else
							{
								if (lastSlotlistTime)
								{
									// can't see ourselves
									buttonPushed = true;
									DEBUG_LOG(("Can't see ourselves in slotlist %s", options.str()));
									game->reset();
									TheGameSpyInfo->leaveStagingRoom();
									//TheGameSpyInfo->joinBestGroupRoom();
									GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSKicked"));
									nextScreen = "Menus/WOLCustomLobby.wnd";
									TheShell->pop();
								}
							}
						}
					}
					else if (strcmp(resp.command.c_str(), "HWS") == 0)
					{
						// host wants to start
						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

						NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
						if (game && game->isInGame() && game->getSlot(0) && game->getSlot(0)->isPlayer(resp.nick.c_str()))
						{
							Int slotNum = game->getLocalSlotNum();
							GameSlot* slot = game->getSlot(slotNum);
							if (slot && (slot->isAccepted() == false))
							{
								GadgetListBoxAddEntryText(listboxGameSetupChat, TheGameText->fetch("GUI:HostWantsToStart"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
							}
						}
					}
					else if (stricmp(resp.command.c_str(), "NAT") == 0)
					{
						if (TheNAT != NULL) {
							TheNAT->processGlobalMessage(-1, resp.commandOptions.c_str());
						}
					}
// TODO_NGMP: Probably don't care about this anymore
					/*
					else if (stricmp(resp.command.c_str(), "Pings") == 0)
					{
						if (!TheGameSpyInfo->amIHost())
						{
							AsciiString pings = resp.commandOptions.c_str();
							AsciiString token;
							for (Int i=0; i<MAX_SLOTS; ++i)
							{
								NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

						NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
								NGMPGameSlot *slot = game->getGameSpySlot(i);
								if (pings.nextToken(&token, ","))
								{
									token.trim();
									slot->setPingString(token);
								}
								else
								{
									slot->setPingString("");
								}
							}
						}
					}
					*/
				}
				break;

			case PeerResponse::PEERRESPONSE_PLAYERUTM:
				{
					sawImportantMessage = TRUE;
					if (strcmp(resp.command.c_str(), "STATS") == 0)
					{
						PSPlayerStats stats = TheGameSpyPSMessageQueue->parsePlayerKVPairs(resp.commandOptions.c_str());
						if (stats.id && (TheGameSpyPSMessageQueue->findPlayerStatsByID(stats.id).id == 0))
							TheGameSpyPSMessageQueue->trackPlayerStats(stats);
						break;
					}
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

					NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
					if (game)
					{
						Int slotNum = game->getSlotNum(resp.nick.c_str());
						if ((slotNum >= 0) && (slotNum < MAX_SLOTS) && (stricmp(resp.command.c_str(), "NAT") == 0)) {
							// this is a command for NAT negotiations, pass if off to TheNAT
							if (TheNAT != NULL) {
								TheNAT->processGlobalMessage(slotNum, resp.commandOptions.c_str());
							}
						}
						if (slotNum == 0 && !TheGameSpyInfo->amIHost())
						{
							if (strcmp(resp.command.c_str(), "KICK") == 0)
							{
								// oops - we've been kicked.  bail.
								buttonPushed = true;
								game->reset();
								TheGameSpyInfo->leaveStagingRoom();
								//TheGameSpyInfo->joinBestGroupRoom();
								UnicodeString message = TheGameText->fetch("GUI:GSKicked");
								AsciiString commandMessage = resp.commandOptions.c_str();
								commandMessage.trim();
								DEBUG_LOG(("We were kicked: reason was '%s'", resp.commandOptions.c_str()));
								if (commandMessage == "GameStarted")
								{
									message = TheGameText->fetch("GUI:GSKickedGameStarted");
								}
								else if (commandMessage == "GameFull")
								{
									message = TheGameText->fetch("GUI:GSKickedGameFull");
								}
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), message);
								nextScreen = "Menus/WOLCustomLobby.wnd";
								TheShell->pop();
							}
						}
						else if (slotNum > 0 && TheGameSpyInfo->amIHost())
						{
							if (strcmp(resp.command.c_str(), "accept") == 0)
							{
								game->getSlot(slotNum)->setAccept();
								TheGameSpyInfo->setGameOptions();
								WOLDisplaySlotList();
							}
							else if (strcmp(resp.command.c_str(), "MAP") == 0)
							{
								Bool hasMap = atoi(resp.commandOptions.c_str());
								game->getSlot(slotNum)->setMapAvailability(hasMap);
								if (!hasMap)
								{
									// tell the host the user doesn't have the map
									UnicodeString mapDisplayName;
									const MapMetaData *mapData = TheMapCache->findMap( game->getMap() );
									Bool willTransfer = TRUE;
									if (mapData)
									{
										mapDisplayName.format(L"%ls", mapData->m_displayName.str());
										willTransfer = !mapData->m_isOfficial;
									}
									else
									{
										mapDisplayName.translate(game->getMap().str());
										willTransfer = WouldMapTransfer(game->getMap());
									}
									UnicodeString text;
									if (willTransfer)
										text.format(TheGameText->fetch("GUI:PlayerNoMapWillTransfer"), game->getSlot(slotNum)->getName().str(), mapDisplayName.str());
									else
										text.format(TheGameText->fetch("GUI:PlayerNoMap"), game->getSlot(slotNum)->getName().str(), mapDisplayName.str());
									GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
								}
								WOLDisplaySlotList();
							}
							else if (strcmp(resp.command.c_str(), "REQ") == 0)
							{
								AsciiString options = resp.commandOptions.c_str();
								options.trim();

								Bool change = false;
								Bool shouldUnaccept = false;
								AsciiString key;
								options.nextToken(&key, "=");
								Int val = atoi(options.str()+1);
								UnsignedInt uVal = atoi(options.str()+1);
								DEBUG_LOG(("GameOpt request: key=%s, val=%s from player %d", key.str(), options.str()+1, slotNum));

								NGMPGameSlot *slot = game->getGameSpySlot(slotNum);
								if (!slot)
									break;

								if (key == "Color")
								{
									if (val >= -1 && val < TheMultiplayerSettings->getNumColors() && val != slot->getColor() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
									{
										Bool colorAvailable = TRUE;
										if(val != -1 )
										{
											for(Int i=0; i <MAX_SLOTS; i++)
											{
												GameSlot *checkSlot = game->getSlot(i);
												if(val == checkSlot->getColor() && slot != checkSlot)
												{
													colorAvailable = FALSE;
													break;
												}
											}
										}
										if(colorAvailable)
											slot->setColor(val);
										change = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid color %d", val));
									}
								}
								else if (key == "PlayerTemplate")
								{
									if (val >= PLAYERTEMPLATE_MIN && val < ThePlayerTemplateStore->getPlayerTemplateCount() && val != slot->getPlayerTemplate())
									{
                    // Validate for LimitArmies checkbox
                    if ( game->oldFactionsOnly() )
                    {
                      const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(val);
                      if ( fac != NULL && !fac->isOldFaction())
                      {
                        val = PLAYERTEMPLATE_RANDOM;
                      }
                    }

										slot->setPlayerTemplate(val);
										if (val == PLAYERTEMPLATE_OBSERVER)
										{
											slot->setColor(-1);
											slot->setStartPos(-1);
											slot->setTeamNumber(-1);
										}
										change = true;
										shouldUnaccept = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid PlayerTemplate %d", val));
									}
								}
								else if (key == "StartPos")
								{
									if (val >= -1 && val < MAX_SLOTS && val != slot->getStartPos() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
									{
										Bool startPosAvailable = TRUE;
										if(val != -1)
										{
											for(Int i=0; i <MAX_SLOTS; i++)
											{
												GameSlot *checkSlot = game->getSlot(i);
												if(val == checkSlot->getStartPos() && slot != checkSlot)
												{
													startPosAvailable = FALSE;
													break;
												}
											}
										}
										if(startPosAvailable)
											slot->setStartPos(val);
										change = true;
										shouldUnaccept = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid startPos %d", val));
									}
								}
								else if (key == "Team")
								{
									if (val >= -1 && val < MAX_SLOTS/2 && val != slot->getTeamNumber() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
									{
										slot->setTeamNumber(val);
										change = true;
										shouldUnaccept = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid team %d", val));
									}
								}
								else if (key == "IP")
								{
									if (uVal != slot->getIP())
									{
										DEBUG_LOG(("setting IP of player %ls from 0x%08x to be 0x%08x", slot->getName().str(), slot->getIP(), uVal));
										slot->setIP(uVal);
										change = true;
										shouldUnaccept = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid IP %d", uVal));
									}
								}
								else if (key == "NAT")
								{
									if ((val >= FirewallHelperClass::FIREWALL_MIN) &&
											(val <= FirewallHelperClass::FIREWALL_MAX))
									{
										slot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)val);
										DEBUG_LOG(("Setting NAT behavior to %d for player %d", val, slotNum));
										change = true;
									}
									else
									{
										DEBUG_LOG(("Rejecting invalid NAT behavior %d from player %d", val, slotNum));
									}
								}
								else if (key == "Ping")
								{
									// TODO_NGMP
									//slot->setPingString(options.str()+1);
									TheGameSpyInfo->setGameOptions();
									DEBUG_LOG(("Setting ping string to %s for player %d", options.str()+1, slotNum));
								}

								if (change)
								{
									if (shouldUnaccept)
										game->resetAccepted();

									TheGameSpyInfo->setGameOptions();

									WOLDisplaySlotList();
									DEBUG_LOG(("Slot value is color=%d, PlayerTemplate=%d, startPos=%d, team=%d, IP=0x%8.8X",
										slot->getColor(), slot->getPlayerTemplate(), slot->getStartPos(), slot->getTeamNumber(), slot->getIP()));
									DEBUG_LOG(("Slot list updated to %s", GameInfoToAsciiString(game).str()));
								}
							}
						}
					}
				}
				break;

			}
		}


	}
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLGameSetupMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	/*
	switch( msg )
	{

		//-------------------------------------------------------------------------------------------------
		case GWM_RIGHT_UP:
		{
			if (buttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			NameKeyType controlID = (NameKeyType)control->winGetWindowId();
			DEBUG_LOG(("GWM_RIGHT_UP for control %d(%s)", controlID, TheNameKeyGenerator->keyToName(controlID).str()));
			break;
		}

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
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																							(WindowMsgData)buttonBack, buttonBackID );
					}
					// don't let key fall through anywhere else
					return MSG_HANDLED;
				}
			}
		}
	}
	*/
	return MSG_IGNORED;
}


// Slash commands and getNextSelectablePlayer()/getFirstSelectablePlayer() moved to
// OnlineGameSetupActions::handleSlashCommand()/getNextSelectablePlayer()/getFirstSelectablePlayer().

//-------------------------------------------------------------------------------------------------
/** WOL Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLGameSetupMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	static int buttonCommunicatorID = NAMEKEY_INVALID;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				buttonCommunicatorID = NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator");
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				if (windowMap)
					windowMap->winSetUserData(NULL);

				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}
		//-------------------------------------------------------------------------------------------------
		case GCM_SELECTED:
		{
			if (!initDone)
				break;
			if (buttonPushed)
				break;
			GameWindow* control = (GameWindow*)mData1;
			Int controlID = control->winGetWindowId();

			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if (pLobbyInterface == nullptr)
			{
				break;
			}
			
			NGMPGame* myGame = pLobbyInterface->GetCurrentGame();

			if (myGame == nullptr)
			{
				break;
			}

			if (controlID == comboBoxStartingCashID)
			{
				handleStartingCashSelection();
			}
			else
			{
				for (Int i = 0; i < MAX_SLOTS; i++)
				{
					if (controlID == comboBoxColorID[i])
					{
						handleColorSelection(i);
					}
					else if (controlID == comboBoxPlayerTemplateID[i])
					{
						handlePlayerTemplateSelection(i);
					}
					else if (controlID == comboBoxTeamID[i])
					{
						handleTeamSelection(i);
					}
					else if (controlID == comboBoxPlayerID[i] && pLobbyInterface->IsHost())
					{
						// We don't have anything that'll happen if we click on ourselves
						if (i == myGame->getLocalSlotNum())
							break;
						Int pos = -1;
						GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						if (pos != SLOT_PLAYER && pos >= 0)
						{
							Bool isAIChanged = FALSE, wasAI = FALSE;
							if (OnlineGameSetupActions::selectSlotState(myGame, i, SlotState(pos), &isAIChanged, &wasAI))
							{
								if (isAIChanged)
									PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI && myGame->getAllowObservers());
								WOLDisplaySlotList();
							}
						}
						break;
					}
				}
			}
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				static int buttonCommunicatorID = NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator");

				if ( controlID == buttonBackID )
				{
					savePlayerInfo();
					if( WOLMapSelectLayout )
					{
						WOLMapSelectLayout->destroyWindows();
						deleteInstance(WOLMapSelectLayout);
						WOLMapSelectLayout = NULL;
					}

					
					PopBackToLobby();

				}
				else if ( controlID == buttonCommunicatorID )
				{
					OnlineGameSetupActions::toggleCommunicatorOverlay();

				}
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
					{
						OnlineGameSetupActions::sendChat(txtInput);
					}
				}
				else if ( controlID == buttonSelectMapID )
				{
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if ( pLobbyInterface != nullptr && OnlineGameSetupActions::canOpenMapSelect( pLobbyInterface->GetCurrentGame() ) )
					{
						WOLMapSelectLayout = TheWindowManager->winCreateLayout( "Menus/WOLMapSelectMenu.wnd" );
						WOLMapSelectLayout->runInit();
						WOLMapSelectLayout->hide( FALSE );
						WOLMapSelectLayout->bringForward();
					}
				}
				else if ( controlID == buttonStartID )
				{
					savePlayerInfo();

					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					bool bIsHost = pLobbyInterface->IsHost();
					if (bIsHost)
					{
						StartPressed();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						OnlineGameSetupActions::requestAccept(pLobbyInterface->GetCurrentGame());
					}
				}
        else if ( controlID == checkBoxLimitSuperweaponsID )
        {
          handleLimitSuperweaponsClick();
        }
				else
				{
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					NGMPGame* game = pLobbyInterface->GetCurrentGame();
					if (game == nullptr)
					{
						break;
					}

					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							OnlineGameSetupActions::handleStartPositionMarkerClick(game, i);
						}
					}
				}


				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
   		{
   			if (buttonPushed)
   				break;

   			GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
				if (pLobbyInterface == nullptr)
				{
					break;
				}

				NGMPGame* game = pLobbyInterface->GetCurrentGame();
				if (game == nullptr)
				{
					break;
				}

				for (Int i = 0; i < MAX_SLOTS; i++)
				{
					if (controlID == buttonMapStartPositionID[i])
					{
						OnlineGameSetupActions::handleStartPositionMarkerRightClick(game, i);
					}
				}
				break;
			}

		//-------------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{

					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
					{
						if (!OnlineGameSetupActions::handleSlashCommand(txtInput, addGameSetupChatLine))
						{
							OnlineGameSetupActions::sendChat(txtInput);
						}
					}

				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}


void OnKickedFromLobby()
{
	// can't see ourselves
	buttonPushed = true;

	if (TheNGMPGame != nullptr)
	{
		TheNGMPGame->reset();
	}

	GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSKicked"));
	nextScreen = "Menus/WOLCustomLobby.wnd";
	TheShell->pop();
}
