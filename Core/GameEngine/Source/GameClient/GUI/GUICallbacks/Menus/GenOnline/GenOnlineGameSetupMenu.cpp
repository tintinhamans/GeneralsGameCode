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
// Author: Matt Campbell, December 2001
// Description: WOL Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineChatWrap.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/GenOnlineUserSection.h"

#include "Common/GameEngine.h"
#include "Common/GOUIColors.h"
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
#include "GameClient/GUI/ChatCompletionSets.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/EstablishConnectionsMenu.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GUIUtil.h"
#include "Common/LocalApiLobby.h"
#include "GameNetwork/GameSpy/GSConfig.h"

#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include <ws2ipdef.h>
#include <format>
#include <cmath>
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineAnticheatKind.h"
#include "GameClient/Image.h"
#include "GameLogic/GameLogic.h"
GenOnlineGame* TheGenOnlineGame = NULL;

void GenOnlineDisplaySlotList( void );
static void WOLRefreshConnectionIndicators( void );


extern std::list<PeerResponse> TheGenOnlineLobbyQueuedUTMs;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);


#if defined(RTS_DEBUG)
extern Bool g_genOnlineDebugSlots;
void GenOnlineSlotListDebugLog(const char *fmt, ...)
{
	static char buf[1024];
	va_list va;
	va_start( va, fmt );
	vsnprintf(buf, 1024, fmt, va );
	va_end( va );

	DEBUG_LOG(("%s", buf));
}
#define SLOTLIST_DEBUG_LOG(x) GenOnlineSlotListDebugLog x
#else
#define SLOTLIST_DEBUG_LOG(x) DEBUG_LOG(x)
#endif

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
static NameKeyType buttonChatID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;

static NameKeyType windowMapSelectMapID = NAMEKEY_INVALID;
static NameKeyType checkBoxUseStatsID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitArmiesID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLGameSetup = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *buttonChat = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *windowMap = nullptr;

static ChatTabCycler s_gameSetupChatCycler;
static GameWindow *checkBoxUseStats = nullptr;
static GameWindow *checkBoxLimitSuperweapons = nullptr;
static GameWindow *comboBoxStartingCash = nullptr;
static GameWindow *checkBoxLimitArmies = nullptr;

// The Zero Hour string file has the use stats texts, the Generals one does not, so they fall back to English.
static UnicodeString GenOnlineUseStatsTooltip(Bool on)
{
	return on ? TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:UseStatsOn", L"Statistics from this game will be recorded")
		: TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:UseStatsOff", L"Statistics from this game will not be recorded");
}

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

static NameKeyType kickButtonID[MAX_SLOTS];
static NameKeyType banButtonID[MAX_SLOTS];
static GameWindow *kickButton[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };
static GameWindow *banButton[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };
static int64_t pendingBanUserID = 0;

// Connection indicator per slot: 0..4 is the signal level (1..5 bars), negative values are special states.
enum
{
	CONNECTION_INDICATOR_CONNECTING = -1,
	CONNECTION_INDICATOR_WARNING = -2,
	CONNECTION_INDICATOR_NONE = -3,
};
static Int connectionIndicatorState[MAX_SLOTS];

// GenOnline @feature arcticdolphin 22/11/2026 The ping window of a row is its icon cell, framed green when the player is ready.
struct IconCellState
{
	Bool human;
	Bool ready;
};
static IconCellState iconCellState[MAX_SLOTS];
static const Image *iconCellShield = nullptr;
static GameWindow *listboxInfo = nullptr;
static const Int INFO_ROW_READY = 1;
static const Int INFO_COLUMN_LABEL = 0;
static const Int INFO_COLUMN_ICON = 1;
static const Int INFO_COLUMN_VALUE = 2;
static const Int INFO_ICON_EXTENT = 14;

// GenOnline @feature arcticdolphin 22/11/2026 The Game panel info rows are a scrollable list that is rewritten in place.
static void setInfoCell(Int row, Int column, const UnicodeString &text, Color color)
{
	GadgetListBoxAddEntryText(listboxInfo, text, color, row, column);
}

WindowLayout *GenOnlineMapSelectLayout = NULL;

void GenOnlinePopBackToLobby()
{
	// delete TheNAT, its no good for us anymore.
	if (TheNAT != nullptr)
	{
		delete TheNAT;
		TheNAT = NULL;
	}

	if (TheGenOnlineGame) // this can be blown away by a disconnect on the map transfer screen
	{
		TheGenOnlineGame->reset();


	}

	DEBUG_LOG(("PopBackToLobby() - parentWOLGameSetup is %X", parentWOLGameSetup));
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->LeaveCurrentLobby();
	}

	if (parentWOLGameSetup)
	{
		nextScreen = "Menus/GenOnlineCustomLobby.wnd";
		TheShell->pop();


	}
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void positionStartSpots(AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void GenOnlinePositionStartSpots()
{
	GameWindow *win = windowMap;
	if (GenOnlineMapSelectLayout != NULL) {
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

		AsciiString map = TheGenOnlineGame->getMap();
		positionStartSpots( map, buttonMapStartPosition, win);
	}
}
static void savePlayerInfo()
{
	if (TheGenOnlineGame)
	{
		Int slotNum = TheGenOnlineGame->getLocalSlotNum();
		if (slotNum >= 0)
		{
			GenOnlineGameSlot *slot = TheGenOnlineGame->getGameSpySlot(slotNum);
			if (slot)
			{
				// save off some prefs
				CustomMatchPreferences pref;
				pref.setPreferredColor(slot->getColor());
				pref.setPreferredFaction(slot->getPlayerTemplate());
				if (TheGenOnlineGame->amIHost())
				{
					pref.setPreferredMap(TheGenOnlineGame->getMap());
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

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString, -1, NULL, 1.5f);
		return;
	}

	GenOnlineGame* game = pLobbyInterface->GetCurrentGame();
	if (!game)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	GenOnlineGameSlot *slot = game->getGameSpySlot(slotIdx);
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

	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
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

	int64_t localPlayerID = -1;
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		localPlayerID = pAuthInterface->GetUserID();
	}

	bool bIsConnected = false;
	int connectionBars = -1;
	int connectionLatency = -1;
	int connectionJitter = -1;
	int connectionLossPct = -1;
	int connectionDelayMs = -1;
	std::string strConnectionType = "";

	LobbyMemberEntry member = pLobbyInterface->GetRoomMemberFromID(slot->m_userID);

	if (localPlayerID != slot->m_userID)
	{
		if (GenOnlineServicesManager::GetNetworkMesh() != nullptr)
		{
			PlayerConnection* pConnection = GenOnlineServicesManager::GetNetworkMesh()->GetConnectionForUser(slot->m_userID);

			if (pConnection != nullptr)
			{
				strConnectionType = pConnection->GetRouteLabel();
				if (pConnection->GetState() == EConnectionState::CONNECTED_DIRECT)
				{
					bIsConnected = true;
				}
				else
				{
					bIsConnected = false;
				}
				connectionBars = pConnection->ComputeConnectionRating();
				connectionLatency = pConnection->GetSeedLatency();
				if (connectionLatency < 0)
				{
					connectionLatency = pConnection->GetLatency();
				}
				connectionJitter = pConnection->GetJitter();
				const float sustainedLoss = pConnection->GetSustainedLossPercent(60000);
				connectionLossPct = (sustainedLoss >= 0.0f) ? static_cast<int>(sustainedLoss + 0.5f) : -1;
				connectionDelayMs = (connectionBars > 0) ? pConnection->m_rating.delayMs : -1;
			}
		}
	}

	if (localPlayerID == slot->m_userID)
	{
		// local user wont have a connection
#if defined(WORLD_SERIES_CLIENT)
		playerInfo.format(L"\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#else
        playerInfo.format(L"\nOverall Elo Rating: %d (in %d matches)\nMonthly Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
            stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#endif
	}
	else if (bIsConnected)
	{
		UnicodeString latencyStr, jitterStr, qualityStr, delayStr;
		if (connectionLatency >= 0) latencyStr.format(L"%d ms", connectionLatency); else latencyStr = L"Unknown";
		if (connectionDelayMs >= 0)
		{
			delayStr.format(L"%d ms", connectionDelayMs);
		}
		else
		{
			delayStr = L"Unknown";
		}
		if (connectionJitter >= 0) jitterStr.format(L"%d ms", connectionJitter); else jitterStr = L"Unknown";
		if (connectionLossPct >= 0) qualityStr.format(L"%d%%", connectionLossPct); else qualityStr = L"Unknown";

#if defined(WORLD_SERIES_CLIENT)
		playerInfo.format(L"\nConnection State: Connected (%hs)\nPing (round trip): %s\nPacket loss: %s\nJitter: %s\nExpected input delay: %s\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			strConnectionType.c_str(), latencyStr.str(), qualityStr.str(), jitterStr.str(), delayStr.str(),
			member.region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#else
        playerInfo.format(L"\nConnection State: Connected (%hs)\nPing (round trip): %s\nPacket loss: %s\nJitter: %s\nExpected input delay: %s\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nMonthly Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
            strConnectionType.c_str(), latencyStr.str(), qualityStr.str(), jitterStr.str(), delayStr.str(),
            member.region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#endif
	}
	else
	{
#if defined(WORLD_SERIES_CLIENT)
		playerInfo.format(L"\nConnection State: Connecting...\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nWS Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			member.region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#else
        playerInfo.format(L"\nConnection State: Connecting...\nRegion: %hs\nOverall Elo Rating: %d (in %d matches)\nMonthly Elo Rating: %d\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
            member.region.c_str(), stats.elo_rating, stats.elo_num_matches, stats.monthly_elo_rating, totalWins, totalLosses, totalDiscons, favoriteSide.str());
#endif
	}

			UnicodeString tooltip = UnicodeString::TheEmptyString;
			if (isLocalPlayer)
			{
				tooltip.format(TheGameText->fetch("TOOLTIP:LocalPlayer"), uName.str());
			}
			else
			{
				// not us
				// TODO_GENONLINE: Impl friends again
				bool bIsFriend = false;
				if (bIsFriend)
				//if (TheGameSpyInfo->getBuddyMap()->find(profileID) != TheGameSpyInfo->getBuddyMap()->end())
				{
					// buddy
					tooltip.format(TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:GOFriendPlayer", L"Your friend %s"), uName.str());
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

			GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
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

void GenOnlineGameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
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

void GenOnlinePingTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);


	Int winPosX, winPosY, winWidth, winHeight;

	window->winGetScreenPosition(&winPosX, &winPosY);

	window->winGetSize(&winWidth, &winHeight);

	// GenOnline @feature arcticdolphin 22/11/2026 Cells without a connection to show give no tooltip.
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (genericPingWindow[i] == window && connectionIndicatorState[i] == CONNECTION_INDICATOR_NONE)
		{
			return;
		}
	}

	if ((x > winPosX && x < (winPosX + winWidth)) && (y > winPosY && y < (winPosY + winHeight)))
	{
		TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:ConnectionSpeed"), -1, NULL);
	}
}

//external declarations of the Gadgets the callbacks can use
GameWindow *GenOnlineListboxGameSetupChat = NULL;
NameKeyType GenOnlineListboxGameSetupChatID = NAMEKEY_INVALID;

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

	GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					GameSlot *checkSlot = myGame->getSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}

			// TODO_GENONLINE: Enforce this on the service too
			if(!colorAvailable)
				return;
		}

		// GenOnline: The lobby service sets this value, not the local slot.
		//slot->setColor(color);

		// GenOnline: Only a local change goes to the lobby, remote players arrive from the service.
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

		if (index == myGame->getLocalSlotNum())
		{
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->UpdateCurrentLobby_MyColor(color);
			}
		}
		else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
		{
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->UpdateCurrentLobby_AIColor(index, color);
			}
		}
	}
}

static void handlePlayerTemplateSelection(int index, bool bInitialSetup = false)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);

	if (!bInitialSetup)
	{
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

		if (myGame)
		{
			GameSlot* slot = myGame->getSlot(index);
			if (playerTemplate == slot->getPlayerTemplate())
				return;

			Int oldTemplate = slot->getPlayerTemplate();
			slot->setPlayerTemplate(playerTemplate);

			int updatedStartPos = slot->getStartPos();

			if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				// was observer, so populate color & team with all, and enable
				GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
				GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
				slot->setStartPos(-1);
				updatedStartPos = -1;
			}
			else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				// is becoming observer, so populate color & team with random only, and disable
				GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
				GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
				slot->setStartPos(-1);
				updatedStartPos = -1;
			}

			// GenOnline: Only a local change goes to the lobby, remote players arrive from the service.
			if (index == myGame->getLocalSlotNum())
			{
				pLobbyInterface->UpdateCurrentLobby_MySide(playerTemplate, updatedStartPos);
			}
			else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
			{
				pLobbyInterface->UpdateCurrentLobby_AISide(index, playerTemplate, updatedStartPos);
			}
		}
	}
}


static void handleStartPositionSelection(Int player, int startPos)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		GenOnlineGameSlot * slot = myGame->getGameSpySlot(player);
		if (!slot)
			return;

		if (startPos == slot->getStartPos())
			return;
		Bool skip = FALSE;
		if (startPos < 0)
		{
			skip = TRUE;
		}

		if(!skip)
		{
			Bool isAvailable = TRUE;
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				if(i != player && myGame->getSlot(i)->getStartPos() == startPos)
				{
					isAvailable = FALSE;
					break;
				}
			}
			if( !isAvailable )
				return;
		}

		// GenOnline: The lobby service sets this value, not the local slot.
		//slot->setStartPos(startPos);

		// GenOnline: Only a local change goes to the lobby, remote players arrive from the service.
		//		 We also allow host to set AI here
		if (player == myGame->getLocalSlotNum())
		{
			pLobbyInterface->UpdateCurrentLobby_MyStartPos(startPos);
		}
		else if (myGame->amIHost() && slot->isAI()) // AI + Host
		{
			pLobbyInterface->UpdateCurrentLobby_AIStartPos(player, startPos);
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (team == slot->getTeamNumber())
			return;

		// GenOnline: The lobby service sets this value, not the local slot.
		//slot->setTeamNumber(team);

		// GenOnline: Only a local change goes to the lobby, remote players arrive from the service.
		if (index == myGame->getLocalSlotNum())
		{
			pLobbyInterface->UpdateCurrentLobby_MyTeam(team);
		}
		else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
		{
			pLobbyInterface->UpdateCurrentLobby_AITeam(index, team);
		}
	}
}

static void handleStartingCashSelection()
{
// update it on the service
	Int selIndex;
	GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

	UnsignedInt startingCashValue = (UnsignedInt)GadgetComboBoxGetItemData(comboBoxStartingCash, selIndex);

	Money startingCash;
	startingCash.deposit(startingCashValue, FALSE);

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->UpdateCurrentLobby_StartingCash(startingCashValue);
	}
}

static void handleLimitSuperweaponsClick()
{

	// update it on the service
	bool bLimitSuperweapons = GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons);

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->UpdateCurrentLobby_LimitSuperweapons(bLimitSuperweapons);
	}
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

// lobby humans (the view of the service) the game has no human slot for
static Int CountLobbyHumansMissingFromGame(GenOnlineGame* game)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr || game == nullptr)
	{
		return 0;
	}

	Int missing = 0;
	for (const LobbyMemberEntry& member : pLobbyInterface->GetCurrentLobby().members)
	{
		if (!member.IsHuman())
		{
			continue;
		}

		bool bInGame = false;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GenOnlineGameSlot* slot = static_cast<GenOnlineGameSlot*>(game->getSlot(i));
			if (slot != nullptr && slot->isHuman() && slot->m_userID == member.user_id)
			{
				bInGame = true;
				break;
			}
		}

		if (!bInGame)
		{
			++missing;
		}
	}

	return missing;
}

static Bool AreAllHumansSlottedAndConnected(GenOnlineLobbyInterface* lobby)
{
	if (lobby == nullptr || lobby->GetCurrentGame() == nullptr)
	{
		return FALSE;
	}

	return CountLobbyHumansMissingFromGame(lobby->GetCurrentGame()) == 0 && lobby->AreLocalLobbyPeersConnected();
}

static const Int MAX_ROSTER_REFRESHES_BEFORE_START = 3;

static void StartMatchWhenRosterMatches(Int refreshesDone)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (!TheGenOnlineGame)
	{
		return;
	}

	if (pLobbyInterface == nullptr || !myGame || !myGame->isInGame())
	{
		TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
		return;
	}

	const Int missingHumans = CountLobbyHumansMissingFromGame(myGame);
	if (missingHumans > 0)
	{
		if (refreshesDone < MAX_ROSTER_REFRESHES_BEFORE_START)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Start: the game is missing %d lobby player(s), refreshing the lobby (%d/%d)", missingHumans, refreshesDone + 1, MAX_ROSTER_REFRESHES_BEFORE_START);
			pLobbyInterface->UpdateRoomDataCache([refreshesDone](bool)
				{
					StartMatchWhenRosterMatches(refreshesDone + 1);
				});
			return;
		}

		// A client starts anyway because the service and the other players have already started.
		if (!pLobbyInterface->IsHost())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Start: the game is still missing %d lobby player(s), starting anyway", missingHumans);
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Start: the game is still missing %d lobby player(s), not starting", missingHumans);
			TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
			GSMessageBoxOk(TheGameText->fetch("GUI:Error"), UnicodeString(L"The player list was incomplete, so the match was not started. Please rejoin the lobby."));
			GenOnlinePopBackToLobby();
			return;
		}
	}

	// TODO_GENONLINE

	GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
	if (buttonBuddy)
		buttonBuddy->winEnable(FALSE);
	GameSpyCloseOverlay(GSOVERLAY_BUDDY);
	GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);

	*TheGenOnlineGame = *myGame;
	TheGenOnlineGame->startGame(0);
}

// A reason code with the GO: string key that shows it and the English text used when the key is missing.
struct ReasonText
{
	const char* code;
	const char* key;
	const WideChar* fallback;
};

static const ReasonText START_REJECTED_TEXTS[] =
{
	{ "not_owner", "GO:StartRefusedNotOwner", L"The start was refused because you are no longer the host." },
	{ "no_lobby", "GO:StartRefusedNoLobby", L"The start was refused because the lobby no longer exists." },
	{ "already_started", "GO:StartRefusedAlreadyStarted", L"The match has already started." },
	{ "not_ready", "GO:StartRefusedNotReady", L"The start was refused because not every player is ready." },
	{ "mesh_incomplete", "GO:StartRefusedMeshIncomplete", L"The start was refused because not every player is connected." },
	{ nullptr, "GO:StartRefused", L"The start was refused by the service." }
};

static const ReasonText COUNTDOWN_CANCELLED_TEXTS[] =
{
	{ "member_left", "GO:StartStoppedMemberLeft", L"The start stopped because a player left." },
	{ "settings_changed", "GO:StartStoppedSettingsChanged", L"The start stopped because the settings changed." },
	{ "host_left", "GO:StartStoppedHostLeft", L"The start stopped because the host left." },
	{ "mesh_incomplete", "GO:StartStoppedMeshIncomplete", L"The start stopped because not every player is connected." },
	{ nullptr, "GO:StartStopped", L"The start stopped." }
};

// The last table entry is the text for an unknown code.
static UnicodeString GetReasonText(const ReasonText* texts, const std::string& strReason)
{
	while (texts->code != nullptr && strReason != texts->code)
	{
		++texts;
	}
	return TheGameText->fetchOrSubstitute(texts->key, texts->fallback);
}

static void ShowMissingLinks(const std::vector<std::pair<int64_t, int64_t>>& missingLinks)
{
	auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr)
	{
		return;
	}

	auto displayName = [lobby](int64_t userID)
	{
		const LobbyMemberEntry member = lobby->GetRoomMemberFromID(userID);
		return UnicodeString(member.user_id != -1 ? from_utf8(member.display_name).c_str() : std::to_wstring(userID).c_str());
	};

	for (const auto& link : missingLinks)
	{
		UnicodeString text;
		text.format(TheGameText->fetchOrSubstitute("GO:PlayerNotConnectedTo", L"%s is not connected to %s."), displayName(link.first).str(), displayName(link.second).str());
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
	}
}

static Int countdownShownSecond = 0;

static void ShowCountdownSecond(Int secondsRemaining)
{
	countdownShownSecond = secondsRemaining;

	UnicodeString strInform;
	if (secondsRemaining == 1)
	{
		// Lock all host controlled lobby settings last second of the match start countdown
		// to prevent late local changes not propagating to remote clients in time
		WOLLockSettings();

		strInform.format(TheGameText->fetch("LAN:GameStartTimerSingular"), secondsRemaining);
	}
	else
	{
		strInform.format(TheGameText->fetch("LAN:GameStartTimerPlural"), secondsRemaining);
	}
	GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strInform, DetermineSystemNoticeColor(false, false), -1, -1);
}

static void OnLobbyEvent(const LobbyEvent& lobbyEvent)
{
	if (TheGenOnlineGame == nullptr || TheGenOnlineGame->isGameInProgress())
	{
		return;
	}

	if (lobbyEvent.event == "countdown_started")
	{
		if (TheGenOnlineGame->GetStartPhase() == EStartPhase::Starting)
		{
			return;
		}

		TheGenOnlineGame->StartCountdown(lobbyEvent.duration_ms);
		GameSpyCloseOverlay(GSOVERLAY_BUDDY);
		const Int seconds = lobbyEvent.duration_ms > 0 ? (Int)((lobbyEvent.duration_ms + 999) / 1000) : 0;
		if (seconds > 0)
		{
			ShowCountdownSecond(seconds);
		}
	}
	else if (lobbyEvent.event == "countdown_cancelled")
	{
		TheGenOnlineGame->AbortStart();
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, GetReasonText(COUNTDOWN_CANCELLED_TEXTS, lobbyEvent.reason), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
		ShowMissingLinks(lobbyEvent.missingLinks);
	}
	else if (lobbyEvent.event == "host_wants_to_start")
	{
		auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (lobby != nullptr && !lobby->IsHost())
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, TheGameText->fetch("GUI:HostWantsToStart"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
	}
	else if (lobbyEvent.event == "ready_check_started")
	{
		UnicodeString text;
		text.format(TheGameText->fetchOrSubstitute("GO:ReadyCheckStarted", L"Ready check: All players will be marked ready in %d seconds."), (Int)((lobbyEvent.duration_ms + 999) / 1000));
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, DetermineSystemNoticeColor(false, false), -1, -1);
	}
	else if (lobbyEvent.event == "ready_check_complete")
	{
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, TheGameText->fetchOrSubstitute("GO:ReadyCheckComplete", L"Ready check: All players are now marked ready."), DetermineSystemNoticeColor(false, false), -1, -1);
	}
	else if (lobbyEvent.event == "camera_height")
	{
		UnicodeString text;
		text.format(TheGameText->fetchOrSubstitute("GO:CameraHeightChanged", L"Camera height: The host set the limit to %d."), (Int)lobbyEvent.value);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, DetermineSystemNoticeColor(false, false), -1, -1);
	}
	else if (lobbyEvent.event == "lobby_access")
	{
		UnicodeString text = lobbyEvent.friends_only
			? TheGameText->fetchOrSubstitute("GO:LobbyAccessFriendsOnly", L"Lobby access: Friends only.")
			: TheGameText->fetchOrSubstitute("GO:LobbyAccessAnyone", L"Lobby access: Anyone can join.");
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, DetermineSystemNoticeColor(false, false), -1, -1);
	}
	else if (lobbyEvent.event == "member_removed")
	{
		UnicodeString text;
		text.format(TheGameText->fetchOrSubstitute("GO:MemberRemoved", L"Lobby: '%s' was removed by the host."), UnicodeString(from_utf8(lobbyEvent.display_name).c_str()).str());
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, DetermineSystemNoticeColor(false, false), -1, -1);
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Ignoring unknown lobby event %s", lobbyEvent.event.c_str());
	}
}

static Bool RetryMissingLobbyPeerLinks()
{
	auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || lobby->GetCurrentLobby().lobby_type != ELobbyType::CustomGame || lobby->AreLocalLobbyPeersConnected())
	{
		return FALSE;
	}

	lobby->RetryFailedLobbyPeers();
	GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Start blocked: waiting for all player connections."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
	return TRUE;
}

// The host does not start while a member is not connected or has not exchanged keys with everyone.
static Bool WOLLobbyReadyToStart(void)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pLobbyInterface == nullptr || pMesh == nullptr)
	{
		return TRUE;
	}

	std::vector<NetworkMesh::LobbyStartBlocker> blockers;
	pMesh->GetLobbyStartBlockers(blockers);
	for (const NetworkMesh::LobbyStartBlocker& blocker : blockers)
	{
		const LobbyMemberEntry member = pLobbyInterface->GetRoomMemberFromID(blocker.userID);
		UnicodeString text;
		text.format(blocker.notConnected ? L"Connections: Waiting for %s to connect." : L"Connections: Waiting for %s to exchange keys.", from_utf8(member.display_name).c_str());
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[START] Not starting, waiting for user %lld to %s", static_cast<long long>(blocker.userID), blocker.notConnected ? "connect" : "exchange keys");
	}
	return blockers.empty();
}

static void StartPressed()
{
	Bool isReady = TRUE;
	Bool allHaveMap = TRUE;
	Int playerCount = 0;
	Int humanCount = 0;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (pLobbyInterface == nullptr || !myGame || pAuthInterface == nullptr || myGame->GetStartPhase() != EStartPhase::Idle)
		return;

	if (RetryMissingLobbyPeerLinks())
	{
		return;
	}

	if (pLobbyInterface->GetCurrentLobby().lobby_type == ELobbyType::CustomGame && !AreAllHumansSlottedAndConnected(pLobbyInterface))
	{
		pLobbyInterface->UpdateRoomDataCache();
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Start blocked: waiting for the player list."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
		return;
	}

	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh == nullptr)
	{
		return;
	}

	int numHumanPlayers = 0;
	for(LobbyMemberEntry & member : pLobbyInterface->GetCurrentLobby().members)
	{
		if (member.IsHuman())
		{
			++numHumanPlayers;
		}
	}

	if (pMesh->GetAllConnections().size() < numHumanPlayers - 1)
	{
		UnicodeString text(L"Connections: Some players are still connecting. Try again shortly:");
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);

		
		int64_t myUserID = pAuthInterface->GetUserID();
		auto vecLobbyMembers = pLobbyInterface->GetCurrentLobby().members;
		const auto& allConnections = pMesh->GetAllConnections();

		for (LobbyMemberEntry& lobbyMember : vecLobbyMembers)
		{
			if (lobbyMember.IsHuman() && lobbyMember.user_id != myUserID) // dont show AI or self
			{
				bool bFoundLobbyMemberForConnection = false;

				if (allConnections.find(lobbyMember.user_id) != allConnections.end())
				{
					// we have a connection for this lobby member
					bFoundLobbyMemberForConnection = true;
				}
				
				if (!bFoundLobbyMemberForConnection)
				{
					UnicodeString strDisplayName(from_utf8(lobbyMember.display_name).c_str());
					GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strDisplayName, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
				}
			}
		}
		
		return;
	}

	if (!WOLLobbyReadyToStart())
	{
		return;
	}

	// see if everyone's accepted and count the number of players in the game
	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		// GenOnline @bugfix arcticdolphin 09/11/2026 The map name hides the folders of the user.
		mapDisplayName = getMapNameFromPath(myGame->getMap());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( int i = 0; i < MAX_SLOTS; i++ )
	{
		bool bIsAccepted = myGame->getSlot(i)->isAccepted();
		bool bIsHuman = myGame->getSlot(i)->isHuman();
		if ((bIsAccepted == FALSE) && (bIsHuman == TRUE))
		{
			isReady = FALSE;
			if (!myGame->getSlot(i)->hasMap() && !willTransfer)
			{
				UnicodeString msg;
				msg.format(TheGameText->fetch("GUI:PlayerNoMap"), myGame->getSlot(i)->getName().str(), mapDisplayName.str());
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, msg, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
				allHaveMap = FALSE;
			}
		}
		if(myGame->getSlot(i)->isOccupied() && myGame->getSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (myGame->getSlot(i)->isHuman())
				humanCount++;
			playerCount++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < playerCount)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !humanCount)
	{
		if (myGame->amIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for too few players
	if (playerCount < TheGlobalData->m_netMinPlayers)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),playerCount);
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot->getTeamNumber() >= 0)
			{
				teams.insert(slot->getTeamNumber());
			}
			else
			{
				++numRandom;
			}
		}
	}
	if (numRandom + teams.size() < TheGlobalData->m_netMinPlayers)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
	}

	if(isReady)
	{
		// start full mesh connection check
		UnicodeString strInform = UnicodeString(L"Connections: Checking all players...");
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strInform, GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);

		std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();
		if (pWS != nullptr)
		{
			TheGenOnlineGame->SetStartPhase(EStartPhase::StartRequested);
			pWS->SendData_StartGame();
		}
	}
	else if (allHaveMap)
	{
		// local msg
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, TheGameText->fetch("GUI:NotifiedStartIntent"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);

		std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
		if (pWS != nullptr)
		{
			pWS->SendData_HostStartIntent();
		}
	}
}


// The Game panel options under the map scroll as whole rows. The engine does not clip child windows, so the rows outside the viewport are hidden.
struct OptionRow
{
	GameWindow *windows[2];
	GameWinInputFunc inputs[2];
	Int x[2];
	Int y;
	Int count;
	Int span;
};

static const Int OPTION_ROWS_VISIBLE = 5;
static const Int OPTION_ROWS_MAX = 8;
static OptionRow optionRows[OPTION_ROWS_MAX];
static Int optionRowCount = 0;
static Int optionScroll = 0;
static Int optionTop = 0;
static Int optionPitch = 0;
static Bool syncingOptions = FALSE;
static Bool passwordEdited = FALSE;
static const Int CAMERA_HEIGHT_PRESETS[] = { GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM, 400, 500, 600, 800, 1000 };

static GameWindow *lobbyAccessLabel = nullptr;
static GameWindow *comboBoxLobbyAccess = nullptr;
static GameWindow *passwordLabel = nullptr;
static GameWindow *textEntryPassword = nullptr;
static GameWindow *cameraHeightLabel = nullptr;
static GameWindow *comboBoxCameraHeight = nullptr;
static GameWindow *buttonOptionsUp = nullptr;
static GameWindow *buttonOptionsDown = nullptr;

// The slash commands and the options change a lobby setting through the same function.
static Bool applyLobbyAccess(Bool friendsOnly)
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || !lobby->IsInLobby() || !lobby->IsHost())
	{
		return FALSE;
	}

	lobby->SetJoinability(friendsOnly ? ELobbyJoinability::LobbyJoinability_FriendsOnly : ELobbyJoinability::LobbyJoinability_Public);
	return TRUE;
}

static Bool applyMaxCameraHeight(Int height)
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || !lobby->IsInLobby() || !lobby->IsHost() || height < GENONLINE_MIN_LOBBY_CAMERA_ZOOM || height > GENONLINE_MAX_LOBBY_CAMERA_ZOOM)
	{
		return FALSE;
	}

	GenOnlineServicesManager::Settings.Save_Camera_MaxHeight_WhenLobbyHost((float)height);
	lobby->UpdateCurrentLobbyMaxCameraHeight((uint16_t)height);
	return TRUE;
}

enum LobbyPasswordResult
{
	LOBBY_PASSWORD_APPLIED,
	LOBBY_PASSWORD_OFFLINE,
	LOBBY_PASSWORD_NOT_HOST,
	LOBBY_PASSWORD_BAD_LENGTH,
	LOBBY_PASSWORD_NONE_SET,
};

// A null password removes it. The service validates the owner too, so the client check only picks the message.
static LobbyPasswordResult changeLobbyPassword(const UnicodeString *newPassword)
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	std::shared_ptr<WebSocket> ws = GenOnlineServicesManager::GetWebSocket();
	if (lobby == nullptr || auth == nullptr || ws == nullptr)
	{
		return LOBBY_PASSWORD_OFFLINE;
	}

	LobbyEntry &entry = lobby->GetCurrentLobby();
	if (entry.owner != auth->GetUserID())
	{
		return LOBBY_PASSWORD_NOT_HOST;
	}

	if (newPassword == nullptr)
	{
		if (!entry.passworded)
		{
			return LOBBY_PASSWORD_NONE_SET;
		}

		ws->SendData_RemoveLobbyPassword();
		entry.password.clear();
		return LOBBY_PASSWORD_APPLIED;
	}

	if (newPassword->getLength() == 0 || newPassword->getLength() > GENONLINE_LOBBY_MAX_PASSWORD_LENGTH)
	{
		return LOBBY_PASSWORD_BAD_LENGTH;
	}

	UnicodeString password = *newPassword;
	ws->SendData_ChangeLobbyPassword(password);
	entry.password = to_utf8(password.str());
	return LOBBY_PASSWORD_APPLIED;
}

static Int optionSlotCount()
{
	Int slots = 0;
	for (Int i = 0; i < optionRowCount; ++i)
	{
		slots += optionRows[i].span;
	}
	return slots;
}

static void layoutOptionRows()
{
	Int slot = 0;
	for (Int i = 0; i < optionRowCount; ++i)
	{
		OptionRow &row = optionRows[i];
		const Bool shown = slot >= optionScroll && slot + row.span <= optionScroll + OPTION_ROWS_VISIBLE;
		for (Int w = 0; w < row.count; ++w)
		{
			row.windows[w]->winSetPosition(row.x[w], optionTop + (slot - optionScroll) * optionPitch);
			row.windows[w]->winHide(!shown);
		}
		slot += row.span;
	}

	if (buttonOptionsUp != nullptr)
	{
		buttonOptionsUp->winEnable(optionScroll > 0);
	}
	if (buttonOptionsDown != nullptr)
	{
		buttonOptionsDown->winEnable(optionScroll < optionSlotCount() - OPTION_ROWS_VISIBLE);
	}
}

static void scrollOptionRows(Int rows)
{
	const Int scroll = (std::clamp)(optionScroll + rows, 0, (std::max)(0, optionSlotCount() - OPTION_ROWS_VISIBLE));
	if (scroll != optionScroll)
	{
		optionScroll = scroll;
		layoutOptionRows();
	}
}

static Bool mouseInOptionViewport(Int mouseX, Int mouseY)
{
	if (optionRowCount == 0 || buttonOptionsUp == nullptr)
	{
		return FALSE;
	}

	Int left, top, arrowLeft, arrowTop, arrowWidth, arrowHeight;
	optionRows[0].windows[0]->winGetScreenPosition(&left, &top);
	buttonOptionsUp->winGetScreenPosition(&arrowLeft, &arrowTop);
	buttonOptionsUp->winGetSize(&arrowWidth, &arrowHeight);
	top -= optionScroll * optionPitch;
	return mouseX >= left && mouseX < arrowLeft + arrowWidth && mouseY >= top && mouseY < top + OPTION_ROWS_VISIBLE * optionPitch;
}

// A combo box and a list box keep the wheel for themselves, so each row window passes it on to the scroll first.
static WindowMsgHandledType optionRowInput(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	for (Int i = 0; i < optionRowCount; ++i)
	{
		for (Int w = 0; w < optionRows[i].count; ++w)
		{
			if (optionRows[i].windows[w] != window)
			{
				continue;
			}

			if (msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN)
			{
				scrollOptionRows(msg == GWM_WHEEL_UP ? -1 : 1);
				return MSG_HANDLED;
			}
			return optionRows[i].inputs[w] != nullptr ? optionRows[i].inputs[w](window, msg, mData1, mData2) : MSG_IGNORED;
		}
	}
	return MSG_IGNORED;
}

// The scroll arrows use the kit scroll bar art, so they match every list on the screen.
static void optionArrowDraw(GameWindow *window, WinInstanceData *instData)
{
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);

	const Bool enabled = BitIsSet(window->winGetStatus(), WIN_STATUS_ENABLED);
	const Bool hovered = enabled && BitIsSet(instData->getState(), WIN_STATE_HILITED);
	AsciiString name;
	name.format("VSlider%sButton%s", window == buttonOptionsUp ? "Up" : "Down", !enabled ? "Disabled" : hovered ? "Hilite" : "Enabled");
	const Image *image = TheMappedImageCollection->findImageByName(name);
	if (image != nullptr)
	{
		TheWindowManager->winDrawImage(image, x, y, x + width, y + height);
	}
}

static void addOptionRow(Int span, GameWindow *first, GameWindow *second = nullptr)
{
	if (first == nullptr || optionRowCount >= OPTION_ROWS_MAX)
	{
		return;
	}

	OptionRow &row = optionRows[optionRowCount++];
	row.count = 0;
	row.span = span;
	for (GameWindow *window : { first, second })
	{
		if (window != nullptr)
		{
			window->winGetPosition(&row.x[row.count], &row.y);
			row.windows[row.count] = window;
			row.inputs[row.count] = window->winGetInputFunc();
			window->winSetInputFunc(optionRowInput);
			++row.count;
		}
	}
}

static void populateCameraHeightCombo(Int current)
{
	GadgetComboBoxReset(comboBoxCameraHeight);
	Int selected = 0;
	Bool currentAdded = FALSE;
	auto add = [&](Int value)
	{
		UnicodeString text;
		text.format(L"%d", value);
		const Int index = GadgetComboBoxAddEntry(comboBoxCameraHeight, text, GameSpyColor[GSCOLOR_DEFAULT]);
		GadgetComboBoxSetItemData(comboBoxCameraHeight, index, (void *)(intptr_t)value);
		if (value == current)
		{
			selected = index;
			currentAdded = TRUE;
		}
	};
	for (Int preset : CAMERA_HEIGHT_PRESETS)
	{
		if (!currentAdded && current < preset)
		{
			add(current);
		}
		add(preset);
	}
	if (!currentAdded)
	{
		add(current);
	}
	GadgetComboBoxSetSelectedPos(comboBoxCameraHeight, selected, TRUE);
}

// Shows the lobby values in the options, for the host and for everyone else.
static void syncOptionRows()
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || comboBoxLobbyAccess == nullptr || textEntryPassword == nullptr || comboBoxCameraHeight == nullptr)
	{
		return;
	}

	const LobbyEntry &entry = lobby->GetCurrentLobby();
	syncingOptions = TRUE;

	Int selected = -1;
	const Int access = entry.friends_only ? 1 : 0;
	GadgetComboBoxGetSelectedPos(comboBoxLobbyAccess, &selected);
	if (selected != access)
	{
		GadgetComboBoxSetSelectedPos(comboBoxLobbyAccess, access, TRUE);
	}

	const Int height = entry.max_cam_height == 0 ? GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM : entry.max_cam_height;
	GadgetComboBoxGetSelectedPos(comboBoxCameraHeight, &selected);
	if (selected < 0 || (Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxCameraHeight, selected) != height)
	{
		populateCameraHeightCombo(height);
	}

	// The host sees the password and edits it, everyone else only sees whether one is set.
	UnicodeString password = GadgetTextEntryGetText(textEntryPassword);
	if (!lobby->IsHost())
	{
		password = UnicodeString(entry.passworded ? L"Set" : L"None");
	}
	else if (TheWindowManager->winGetFocus() != textEntryPassword)
	{
		password = UnicodeString(from_utf8(entry.password).c_str());
	}
	if (password.compare(GadgetTextEntryGetText(textEntryPassword)) != 0)
	{
		GadgetTextEntrySetText(textEntryPassword, password);
		passwordEdited = FALSE;
	}

	syncingOptions = FALSE;
}

static void updateOptionRowEnables(Bool canEdit)
{
	for (GameWindow *window : { lobbyAccessLabel, comboBoxLobbyAccess, passwordLabel, textEntryPassword, cameraHeightLabel, comboBoxCameraHeight })
	{
		if (window != nullptr)
		{
			window->winEnable(canEdit);
		}
	}
}

static void handleLobbyAccessSelection()
{
	Int selected = 0;
	GadgetComboBoxGetSelectedPos(comboBoxLobbyAccess, &selected);
	applyLobbyAccess(selected == 1);
}

static void handleCameraHeightSelection()
{
	Int selected = -1;
	GadgetComboBoxGetSelectedPos(comboBoxCameraHeight, &selected);
	if (selected >= 0)
	{
		applyMaxCameraHeight((Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxCameraHeight, selected));
	}
}

// Enter or a lost focus applies what the host typed, an empty entry removes the password.
static void applyPasswordEntry()
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (!passwordEdited || lobby == nullptr || textEntryPassword == nullptr || !lobby->IsHost())
	{
		return;
	}

	passwordEdited = FALSE;
	const UnicodeString text = GadgetTextEntryGetText(textEntryPassword);
	if (text.isEmpty())
	{
		changeLobbyPassword(nullptr);
	}
	else if (text.compare(UnicodeString(from_utf8(lobby->GetCurrentLobby().password).c_str())) != 0)
	{
		changeLobbyPassword(&text);
	}
}

static void initOptionRows()
{
	auto find = [](const char *name)
	{
		AsciiString full;
		full.format("GameSpyGameOptionsMenu.wnd:%s", name);
		return TheWindowManager->winGetWindowFromId(parentWOLGameSetup, TheNameKeyGenerator->nameToKey(full));
	};
	lobbyAccessLabel = find("LobbyAccessLabel");
	comboBoxLobbyAccess = find("ComboBoxLobbyAccess");
	passwordLabel = find("PasswordLabel");
	textEntryPassword = find("TextEntryPassword");
	cameraHeightLabel = find("MaxCameraHeightLabel");
	comboBoxCameraHeight = find("ComboBoxMaxCameraHeight");
	buttonOptionsUp = find("ButtonOptionsUp");
	buttonOptionsDown = find("ButtonOptionsDown");

	// Layout text is a text label key, so plain label text is set here.
	if (lobbyAccessLabel != nullptr)
	{
		GadgetStaticTextSetText(lobbyAccessLabel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineLobbyAccess", L"Lobby access"));
	}
	if (passwordLabel != nullptr)
	{
		GadgetStaticTextSetText(passwordLabel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlinePassword", L"Password"));
	}
	if (cameraHeightLabel != nullptr)
	{
		GadgetStaticTextSetText(cameraHeightLabel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineCameraHeight", L"Camera height"));
	}

	if (comboBoxLobbyAccess != nullptr)
	{
		GadgetComboBoxReset(comboBoxLobbyAccess);
		GadgetComboBoxAddEntry(comboBoxLobbyAccess, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineLobbyAccessAnyone", L"Anyone"), GameSpyColor[GSCOLOR_DEFAULT]);
		GadgetComboBoxAddEntry(comboBoxLobbyAccess, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineLobbyAccessFriends", L"Friends only"), GameSpyColor[GSCOLOR_DEFAULT]);
	}
	if (comboBoxCameraHeight != nullptr)
	{
		populateCameraHeightCombo(GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM);
	}
	if (textEntryPassword != nullptr)
	{
		GadgetTextEntrySetMaxLen(textEntryPassword, GENONLINE_LOBBY_MAX_PASSWORD_LENGTH);
		GadgetTextEntrySetText(textEntryPassword, UnicodeString::TheEmptyString);
	}
	for (GameWindow *arrow : { buttonOptionsUp, buttonOptionsDown })
	{
		if (arrow != nullptr)
		{
			arrow->winSetDrawFunc(optionArrowDraw);
		}
	}

	optionRowCount = 0;
	optionScroll = 0;
#if RTS_ZEROHOUR
	addOptionRow(1, find("StartingCashLabel"), comboBoxStartingCash);
#endif
	addOptionRow(1, lobbyAccessLabel, comboBoxLobbyAccess);
	addOptionRow(1, passwordLabel, textEntryPassword);
	addOptionRow(1, cameraHeightLabel, comboBoxCameraHeight);
#if RTS_ZEROHOUR
	addOptionRow(1, checkBoxLimitSuperweapons);
#endif
	addOptionRow(1, checkBoxUseStats);
#if RTS_ZEROHOUR
	addOptionRow(1, checkBoxLimitArmies);
#endif
	addOptionRow(2, listboxInfo);
	if (optionRowCount >= 2)
	{
		optionTop = optionRows[0].y;
		optionPitch = optionRows[1].y - optionRows[0].y;
	}
	layoutOptionRows();
	syncOptionRows();
}

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void GenOnlineDisplayGameOptions()
{
	if (!parentWOLGameSetup)
		return;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame* theGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (pLobbyInterface == nullptr || theGame == nullptr)
	{
		return;
	}

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
		AsciiString s = theGame->getMap();
		UnicodeString mapDisplay;
		mapDisplay.translate(AsciiString(getFileName(s.str())));
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplay);
	}
	GenOnlinePositionStartSpots();
	updateMapStartSpots(theGame, buttonMapStartPosition);

	Bool isUsingStats = TheGenOnlineGame->getUseStats();
  if (GadgetCheckBoxIsChecked(checkBoxUseStats) != isUsingStats)
  {
  	GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats);
    checkBoxUseStats->winSetTooltip( GenOnlineUseStatsTooltip( isUsingStats ) );
  }

  Bool oldFactionsOnly = theGame->oldFactionsOnly();
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

  // Note: must check if checkbox is already correct to avoid infinite recursion
  Bool limitSuperweapons = (theGame->getSuperweaponRestriction() != 0);
  if ( limitSuperweapons != GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons))
    GadgetCheckBoxSetChecked( checkBoxLimitSuperweapons, limitSuperweapons );

  Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
  Int index = 0;
  for ( ; index < itemCount; index++ )
  {
    Int value  = (Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
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

  syncOptionRows();
}


static void WOLRefreshConnectionIndicators(void)
{
    GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
    GenOnlineGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        GenOnlineGameSlot* slot = game->getGameSpySlot(i);
        const Bool human = slot != nullptr && slot->isHuman();
        iconCellState[i].human = human;
        iconCellState[i].ready = human && slot->isAccepted();
        if (!human || i == game->getLocalSlotNum())
        {
            connectionIndicatorState[i] = CONNECTION_INDICATOR_NONE;
            continue;
        }

        if (genericPingWindow[i] == nullptr)
            continue;

        EConnectionState connectionState = EConnectionState::NOT_CONNECTED;
        int connectionBars = -1;

        if (pMesh != nullptr)
        {
            PlayerConnection* pConnection = pMesh->GetConnectionForUser(slot->m_userID);
            if (pConnection != nullptr)
            {
                connectionState = pConnection->GetState();
                connectionBars = pConnection->ComputeConnectionRating();
            }
        }

        Int& state = connectionIndicatorState[i];
        if (connectionState == EConnectionState::CONNECTION_FAILED || connectionState == EConnectionState::CONNECTION_DISCONNECTED)
            state = CONNECTION_INDICATOR_WARNING;
        else if (connectionState != EConnectionState::CONNECTED_DIRECT || connectionBars < 0)
            state = CONNECTION_INDICATOR_CONNECTING;
        else
            state = connectionBars - 1; // the rating is 1..5 bars, the state is the level 0..4
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

    ScopedDisplayBatch batch;

    Int x, y, width, height;
    window->winGetScreenPosition(&x, &y);
    window->winGetSize(&width, &height);

    // GenOnline @feature arcticdolphin 22/11/2026 The cell frame is green and thicker when the player is ready.
    const IconCellState &cell = iconCellState[slot];
    const Int frameWidth = cell.ready ? (std::max)(2, GameWindowManager::winScaleLayoutSize(2)) : (std::max)(1, GameWindowManager::winScaleLayoutSize(1));
    Color frameColor = GameMakeColor(74, 78, 110, 255);
    if (cell.ready)
        frameColor = GOUIColor(GOUICOLOR_SUCCESS, GameMakeColor(56, 224, 122, 255));
    else if (cell.human)
        frameColor = GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
    TheWindowManager->winFillRect(frameColor, 1.0f, x, y, x + width, y + frameWidth);
    TheWindowManager->winFillRect(frameColor, 1.0f, x, y + height - frameWidth, x + width, y + height);
    TheWindowManager->winFillRect(frameColor, 1.0f, x, y + frameWidth, x + frameWidth, y + height - frameWidth);
    TheWindowManager->winFillRect(frameColor, 1.0f, x + width - frameWidth, y + frameWidth, x + width, y + height - frameWidth);

    if (!cell.human)
        return;

    // The shield and the bars sit at fixed spots, so nothing moves when the state of a player changes.
    const Int size = (Int)(height * 0.6f);
    const Int gap = (width - 2 * size) / 3;
    const Int left = x + 2 * gap + size;
    const Int top = y + (height - size) / 2;
    const Real centerX = left + size * 0.5f;
    const Int state = connectionIndicatorState[slot];

    if (iconCellShield != nullptr)
        TheDisplay->drawImage(iconCellShield, x + gap, top, x + gap + size, top + size);

    if (state == CONNECTION_INDICATOR_NONE)
        return;

    if (state == CONNECTION_INDICATOR_CONNECTING)
    {
        ConnectionRating::DrawSpinner(left, top, size);
    }
    else if (state == CONNECTION_INDICATOR_WARNING)
    {
        const Color amber = GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 190, 0, 255));
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
        ConnectionRating::DrawBars(left, top, size, state);
    }
}

static void RowButtonTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (kickButton[i] == window)
		{
			TheMouse->setCursorTooltip(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSetupKickTooltip", L"Kick: opens the slot"), -1, NULL);
			return;
		}
		if (banButton[i] == window)
		{
			TheMouse->setCursorTooltip(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSetupBanTooltip", L"Ban: removes the player and blocks them from rejoining this game"), -1, NULL);
			return;
		}
	}
}

static void RowButtonDraw(GameWindow *window, WinInstanceData *instData)
{
	Bool isBan = FALSE;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (banButton[i] == window)
			isBan = TRUE;
	}

	ScopedDisplayBatch batch;

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);

	const Bool hovered = BitIsSet(instData->getState(), WIN_STATE_HILITED);
	const Color color = hovered ? GameMakeColor(255, 255, 255, 255) : GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	const Int frameWidth = (std::max)(1, GameWindowManager::winScaleLayoutSize(1));
	TheWindowManager->winFillRect(color, 1.0f, x, y, x + width, y + frameWidth);
	TheWindowManager->winFillRect(color, 1.0f, x, y + height - frameWidth, x + width, y + height);
	TheWindowManager->winFillRect(color, 1.0f, x, y + frameWidth, x + frameWidth, y + height - frameWidth);
	TheWindowManager->winFillRect(color, 1.0f, x + width - frameWidth, y + frameWidth, x + width, y + height - frameWidth);

	const Real centerX = x + width * 0.5f;
	const Real centerY = y + height * 0.5f;
	const Real radius = (std::min)(width, height) * 0.28f;
	const Real thickness = 1.5f;
	if (!isBan)
	{
		TheDisplay->drawLine((Int)(centerX - radius), (Int)(centerY - radius), (Int)(centerX + radius), (Int)(centerY + radius), thickness, color);
		TheDisplay->drawLine((Int)(centerX - radius), (Int)(centerY + radius), (Int)(centerX + radius), (Int)(centerY - radius), thickness, color);
		return;
	}

	const Int segments = 16;
	for (Int s = 0; s < segments; ++s)
	{
		const Real a0 = 6.2831853f * s / segments;
		const Real a1 = 6.2831853f * (s + 1) / segments;
		TheDisplay->drawLine((Int)(centerX + radius * cosf(a0)), (Int)(centerY + radius * sinf(a0)),
			(Int)(centerX + radius * cosf(a1)), (Int)(centerY + radius * sinf(a1)), thickness, color);
	}
	const Real slash = radius * 0.7071f;
	TheDisplay->drawLine((Int)(centerX - slash), (Int)(centerY + slash), (Int)(centerX + slash), (Int)(centerY - slash), thickness, color);
}

static void updateRowButtons(GenOnlineGame *game, Bool isHost)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GenOnlineGameSlot *slot = game->getGameSpySlot(i);
		const Bool show = isHost && i != game->getLocalSlotNum() && slot != nullptr && slot->isHuman();
		if (kickButton[i] != nullptr)
			kickButton[i]->winHide(!show);
		if (banButton[i] != nullptr)
			banButton[i]->winHide(!show);
	}
}

static void removeSlotPlayer(Int slotIndex, Bool ban)
{
	GenOnlineLobbyInterface *pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (game == nullptr || !pLobbyInterface->IsHost() || slotIndex == game->getLocalSlotNum())
		return;

	GenOnlineGameSlot *slot = game->getGameSpySlot(slotIndex);
	if (slot == nullptr || !slot->isHuman())
		return;

	if (ban)
		pLobbyInterface->UpdateCurrentLobby_BanUser(slot->m_userID, slot->getName());
	else
		pLobbyInterface->UpdateCurrentLobby_KickUser(slot->m_userID, slot->getName());
	pLobbyInterface->UpdateCurrentLobby_SetSlotState(slotIndex, SLOT_OPEN);
	slot->setState(SLOT_OPEN);
	game->resetAccepted();
	GenOnlineDisplaySlotList();
}

static void banConfirmed()
{
	GenOnlineLobbyInterface *pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (game == nullptr)
		return;

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GenOnlineGameSlot *slot = game->getGameSpySlot(i);
		if (slot != nullptr && slot->isHuman() && slot->m_userID == pendingBanUserID)
		{
			removeSlotPlayer(i, TRUE);
			return;
		}
	}
}

static Bool handleRowButton(Int controlID)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (controlID == kickButtonID[i])
		{
			removeSlotPlayer(i, FALSE);
			return TRUE;
		}
		if (controlID == banButtonID[i])
		{
			GenOnlineLobbyInterface *pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			GenOnlineGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
			GenOnlineGameSlot *slot = game == nullptr ? nullptr : game->getGameSpySlot(i);
			if (slot == nullptr || !slot->isHuman())
				return TRUE;

			pendingBanUserID = slot->m_userID;
			UnicodeString body = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:GOSetupBanConfirm", L"Ban %ls from this game?", slot->getName().str());
			MessageBoxYesNo(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSetupBanTitle", L"Ban Player"), body, banConfirmed, nullptr);
			return TRUE;
		}
	}
	return FALSE;
}

static void updateReadyInfo(GenOnlineGame *game)
{
	Int humans = 0;
	Int ready = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GenOnlineGameSlot *slot = game->getGameSpySlot(i);
		if (slot != nullptr && slot->isHuman())
		{
			++humans;
			if (slot->isAccepted())
			{
				++ready;
			}
		}
	}

	if (listboxInfo != nullptr && GadgetListBoxGetNumEntries(listboxInfo) > INFO_ROW_READY)
	{
		UnicodeString text;
		text.format(L"%d / %d", ready, humans);
		setInfoCell(INFO_ROW_READY, INFO_COLUMN_VALUE, text, GameMakeColor(254, 254, 254, 255));
	}
}

void GenOnlineDisplaySlotList(void)
{
    // TODO_GENONLINE
    //if (!parentWOLGameSetup || !TheGameSpyInfo->getCurrentStagingRoom())
    //	return;

    GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
    GenOnlineGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    DEBUG_ASSERTCRASH(!game->getConstSlot(0)->isOpen(), ("Open host!"));

    UpdateSlotList(game, comboBoxPlayer, comboBoxColor,
        comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

    // GenOnline @feature arcticdolphin 22/11/2026 The frame of the icon cell shows who is ready, so the accept markers stay hidden.
    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        if (buttonAccept[i] != nullptr)
        {
            buttonAccept[i]->winHide(TRUE);
        }
    }
    updateReadyInfo(game);
    updateRowButtons(game, pLobbyInterface->IsHost());

    GenOnlineDisplayGameOptions();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        GenOnlineGameSlot* slot = game->getGameSpySlot(i);
        if (slot && slot->isHuman())
        {
            // Determine friends and blocked players in lobby setup and highlight them
            Color nameColor = GameSpyColor[GSCOLOR_PLAYER_NORMAL];
            GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();

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
void GenOnlineInitGameGadgets()
{
	ClearGSMessageBoxes();

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	GenOnlineGame* theGameInfo = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (theGameInfo == nullptr || pLobbyInterface == nullptr)
	{
		return;
	}

	for (Int i = 0; i < MAX_SLOTS; ++i)
		connectionIndicatorState[i] = CONNECTION_INDICATOR_CONNECTING;

	//Initialize the gadget IDs
	parentWOLGameSetupID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:GameSpyGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonStart" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryMapDisplay" );
	GenOnlineListboxGameSetupChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ListboxChatWindowGameSpyGameSetup" );
	buttonChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonEmote" ); // TODO Rename ButtonEmote to ButtonChat in .wnd file
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonSelectMap" );
	checkBoxUseStatsID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:CheckBoxUseStats" );
	windowMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:MapWindow" );
  checkBoxLimitSuperweaponsID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons");
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash");
  checkBoxLimitArmiesID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies");
	windowMapSelectMapID = TheNameKeyGenerator->nameToKey("WOLMapSelectMenu.wnd:WinMapPreview");

	NameKeyType staticTextTitleID = NAMEKEY("GameSpyGameOptionsMenu.wnd:StaticTextGameName");

	// Initialize the pointers to our gadgets
	parentWOLGameSetup = TheWindowManager->winGetWindowFromId( nullptr, parentWOLGameSetupID );
	buttonChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonChatID  );
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonSelectMapID  );
	checkBoxUseStats = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxUseStatsID );
	buttonStart = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonStartID  );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,  buttonBackID);
	GenOnlineListboxGameSetupChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, GenOnlineListboxGameSetupChatID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryChatID );
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryMapDisplayID );
	windowMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,windowMapID  );
	SetListBoxRowAnimMode(GenOnlineListboxGameSetupChat, LIST_ROW_ANIM_SLOT);
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the parentWOLGameSetup.wnd:MapWindow" ));

  checkBoxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitSuperweaponsID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons" ));
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash" ));

  checkBoxLimitArmies = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitArmiesID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies" ));
#if RTS_ZEROHOUR
  PopulateStartingCashComboBox(comboBoxStartingCash, theGameInfo);
#else
  // Generals data has no starting cash choices, superweapon limits or armies to limit, so these controls stay hidden.
  comboBoxStartingCash->winHide( TRUE );
  checkBoxLimitSuperweapons->winHide( TRUE );
  checkBoxLimitArmies->winHide( TRUE );
  {
    NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
    GameWindow *label = TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID);
    if (label != nullptr)
    {
      label->winHide( TRUE );
    }
  }
#endif

  // Limit Armies can ONLY be set in the Host Game window (PopupHostGame.wnd)
  checkBoxLimitArmies->winEnable( false );
  // Ditto use stats
  checkBoxUseStats->winEnable( false );

  Int isUsingStats = theGameInfo->getUseStats();

	
  GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats );
  GadgetCheckBoxSetText(checkBoxUseStats, TheGameText->FETCH_OR_SUBSTITUTE("GUI:UseStats", L"Record Stats"));
  checkBoxUseStats->winSetTooltip( GenOnlineUseStatsTooltip( isUsingStats ) );

  if (!pLobbyInterface->CanChangeLobbySettings())
  {
    checkBoxLimitSuperweapons->winEnable( false );
    comboBoxStartingCash->winEnable( false );
		NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
		TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID)->winEnable( FALSE );
  }
  else
  {
	  checkBoxLimitSuperweapons->winEnable(true);
	  comboBoxStartingCash->winEnable(true);
  }


#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
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
		GadgetStaticTextSetText(staticTextTitle, theGameInfo->getGameName());
	}

	// GenOnline @feature arcticdolphin 22/11/2026 The Game panel header and its info block replace the title shield.
	{
		const EAnticheatKind kind = ParseAnticheatKind(pLobbyInterface->GetCurrentLobby().anticheat);
		const Image *shieldImage = kind == EAnticheatKind::Eac ? TheMappedImageCollection->findImageByName(AnticheatKindImageName(kind, false)) : nullptr;
		iconCellShield = shieldImage;
		auto findInfo = [](const char *name)
		{
			AsciiString full;
			full.format("GameSpyGameOptionsMenu.wnd:%s", name);
			return TheWindowManager->winGetWindowFromId(parentWOLGameSetup, TheNameKeyGenerator->nameToKey(full));
		};
		auto setInfo = [](GameWindow *window, const UnicodeString &text)
		{
			if (window != nullptr)
			{
				GadgetStaticTextSetText(window, text);
			}
		};
		setInfo(findInfo("StaticTextMapPreview"), TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineSetupGame", L"Game"));
		listboxInfo = findInfo("ListboxInfo");
		if (listboxInfo != nullptr && GadgetListBoxGetNumEntries(listboxInfo) == 0)
		{
			const Color labelColor = GameMakeColor(170, 178, 205, 255);
			const Color valueColor = GameMakeColor(254, 254, 254, 255);
			const Int anticheatRow = GadgetListBoxAddEntryText(listboxInfo, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineSetupAnticheat", L"Anti-cheat"), labelColor, -1, INFO_COLUMN_LABEL);
			if (shieldImage != nullptr)
			{
				GadgetListBoxAddEntryImage(listboxInfo, shieldImage, anticheatRow, INFO_COLUMN_ICON, INFO_ICON_EXTENT, INFO_ICON_EXTENT);
			}
			setInfoCell(anticheatRow, INFO_COLUMN_VALUE, kind == EAnticheatKind::Eac ? AnticheatKindText(kind) : UnicodeString(L"Not required"), valueColor);
			const Int readyRow = GadgetListBoxAddEntryText(listboxInfo, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GenOnlineSetupReady", L"Ready"), labelColor, -1, INFO_COLUMN_LABEL);
			setInfoCell(readyRow, INFO_COLUMN_VALUE, UnicodeString(L" "), valueColor);
		}
	}

	initOptionRows();

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
			GenOnlineGameSlot* slot = theGameInfo->getGameSpySlot(i);
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
		buttonAccept[i]->winSetTooltipFunc(GenOnlineGameAcceptTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:GenericPing%d", i);
		genericPingWindowID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		genericPingWindow[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, genericPingWindowID[i] );
		DEBUG_ASSERTCRASH(genericPingWindow[i], ("Could not find the genericPingWindow[%d]",i ));
		genericPingWindow[i]->winSetTooltipFunc(GenOnlinePingTooltip);
		genericPingWindow[i]->winSetDrawFunc(WOLConnectionIndicatorDraw);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonKick%d", i);
		kickButtonID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		kickButton[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, kickButtonID[i] );
		DEBUG_ASSERTCRASH(kickButton[i], ("Could not find the kickButton[%d]",i ));
		kickButton[i]->winSetTooltipFunc(RowButtonTooltip);
		kickButton[i]->winSetDrawFunc(RowButtonDraw);
		kickButton[i]->winHide(TRUE);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonBan%d", i);
		banButtonID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		banButton[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, banButtonID[i] );
		DEBUG_ASSERTCRASH(banButton[i], ("Could not find the banButton[%d]",i ));
		banButton[i]->winSetTooltipFunc(RowButtonTooltip);
		banButton[i]->winSetDrawFunc(RowButtonDraw);
		banButton[i]->winHide(TRUE);

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

		// TODO_GENONLINE: Where does this happen in the normal game?
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, theGameInfo, TRUE);

		// Make sure selections are up to date on all machines
		handlePlayerTemplateSelection(i, true);
	}
}

void GenOnlineDeinitGameGadgets()
{
	// The layout destroys these windows.
	listboxInfo = nullptr;
	iconCellShield = nullptr;
	optionRowCount = 0;
	optionScroll = 0;
	lobbyAccessLabel = nullptr;
	comboBoxLobbyAccess = nullptr;
	passwordLabel = nullptr;
	textEntryPassword = nullptr;
	cameraHeightLabel = nullptr;
	comboBoxCameraHeight = nullptr;
	buttonOptionsUp = nullptr;
	buttonOptionsDown = nullptr;
	parentWOLGameSetup = nullptr;
	buttonChat = nullptr;
	buttonSelectMap = nullptr;
	buttonStart = nullptr;
	buttonBack = nullptr;
	GenOnlineListboxGameSetupChat = nullptr;
	textEntryChat = nullptr;
	textEntryMapDisplay = nullptr;
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
		kickButton[i] = NULL;
		banButton[i] = NULL;
	}
}

static Bool initDone = false;
UnsignedInt GenOnlineLastSlotlistTime = 0;
UnsignedInt GenOnlineGameSetupEnterTime = 0;
Bool GenOnlineInitialAcceptEnable = FALSE;

// A lobby chat line keeps its key as item data, so a MessageDelete can find and remove it.
static void AddLobbyChatMessageLine(UnicodeString strMessage, Color color, uint32_t key)
{
	// GenOnline @bugfix arcticdolphin 22/11/2026 A word wider than the chat list is broken so the line wraps fully.
	const Int row = GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, GenOnlineBreakLongWords(GenOnlineListboxGameSetupChat, strMessage), color, -1, -1);
	if (row >= 0)
	{
		GadgetListBoxSetItemData(GenOnlineListboxGameSetupChat, reinterpret_cast<void*>(static_cast<uintptr_t>(key)), row, 0);
	}
}

static void RemoveLobbyChatMessageLine(uint32_t key)
{
	if (GenOnlineListboxGameSetupChat == nullptr)
	{
		return;
	}
	const Int numEntries = GadgetListBoxGetNumEntries(GenOnlineListboxGameSetupChat);
	for (Int row = 0; row < numEntries; ++row)
	{
		if (reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(GenOnlineListboxGameSetupChat, row, 0)) == key)
		{
			TheWindowManager->winSendSystemMsg(GenOnlineListboxGameSetupChat, GLM_DEL_ENTRY, static_cast<WindowMsgData>(row), 0);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
static Bool s_gameSetupMenuOpen = FALSE;

void GenOnlineGameSetupMenuInit( WindowLayout *layout, void *userData )
{
	s_gameSetupMenuOpen = TRUE;
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	if (TheGenOnlineGame != nullptr)
	{
		TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
	}

	// register for chat events
	pLobbyInterface->RegisterForChatCallback([](UnicodeString strMessage, Color color)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strMessage, color, -1, -1);
		});
	pLobbyInterface->RegisterForChatMessageCallbacks(AddLobbyChatMessageLine, RemoveLobbyChatMessageLine);

	// cannot connect to the lobby we joined
	pLobbyInterface->RegisterForCannotConnectToLobbyCallback([](void)
		{
			auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (lobby != nullptr && lobby->GetCurrentLobby().lobby_type == ELobbyType::CustomGame)
			{
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Connection failed. Stay in the lobby and use Retry connections."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
				return;
			}

			if (TheNetwork != NULL) {
				delete TheNetwork;
				TheNetwork = NULL;
			}
			GSMessageBoxOk(TheGameText->fetch("GUI:Error"), UnicodeString(L"Could not connect to all players in the lobby"));

			GenOnlinePopBackToLobby();
		});

	// connection events (for debug really)
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->RegisterForConnectionEvents([](int64_t userID, std::wstring strDisplayName, PlayerConnection* connection)
			{
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
				if (connState == EConnectionState::CONNECTED_DIRECT || connState == EConnectionState::CONNECTION_DISCONNECTED)
				{
					//strConnectionMessage.format(L"Connection state to %s changed to: %hs (mechanism: %hs | protocol: %hs)", strDisplayName.c_str(), strState.c_str(), strConnectionType.c_str(), connection->IsIPV4() ? "IPv4" : "IPv6");
				}
				else
				{
#if !defined(_DEBUG)
					if (connState == EConnectionState::CONNECTION_FAILED)
					{
#endif
						strConnectionMessage.format(L"Connection: %ls is now %hs.%ls", strDisplayName.c_str(), strState.c_str(), connState == EConnectionState::CONNECTION_FAILED ? L" Use Retry connections." : L"");
						const Color connectionColor = connState == EConnectionState::CONNECTION_FAILED
							? GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255))
							: GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255));
						GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strConnectionMessage, connectionColor, -1, -1);

#if !defined(_DEBUG)
					}
#endif
				}

				// update UI
				GenOnlineDisplaySlotList();
			});
	}

	// player doesnt have map events
	pLobbyInterface->RegisterForPlayerDoesntHaveMapCallback([](LobbyMemberEntry lobbyMember)
		{
			// tell the host the user doesn't have the map
			UnicodeString mapDisplayName;
			const MapMetaData* mapData = TheMapCache->findMap(TheGenOnlineGame->getMap());
			Bool willTransfer = TRUE;
			if (mapData)
			{
				mapDisplayName.format(L"%ls", mapData->m_displayName.str());
				willTransfer = !mapData->m_isOfficial;
			}
			else
			{
				// GenOnline @bugfix arcticdolphin 09/11/2026 The map name hides the folders of the user.
				mapDisplayName = getMapNameFromPath(TheGenOnlineGame->getMap());
				willTransfer = WouldMapTransfer(TheGenOnlineGame->getMap());
			}

			UnicodeString strDisplayName(from_utf8(lobbyMember.display_name).c_str());

			UnicodeString text;
			if (willTransfer)
				text.format(TheGameText->fetch("GUI:PlayerNoMapWillTransfer"), strDisplayName.str(), mapDisplayName.str());
			else
				text.format(TheGameText->fetch("GUI:PlayerNoMap"), strDisplayName.str(), mapDisplayName.str());
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		});

	

	// register for roster events
	pLobbyInterface->RegisterForRosterNeedsRefreshCallback([]()
		{
			GenOnlineDisplaySlotList();
			GenOnlineDisplayGameOptions();
		});

	pLobbyInterface->RegisterForGameStartPacket([]()
		{
			// A repeated start packet does not launch the match twice.
			if (TheGenOnlineGame == nullptr || TheGenOnlineGame->GetStartPhase() == EStartPhase::Starting)
			{
				return;
			}

			TheGenOnlineGame->SetStartPhase(EStartPhase::Starting);
			StartMatchWhenRosterMatches(0);
		});

	pLobbyInterface->RegisterForStartGameRejected([](const std::string& strReason, const std::vector<std::pair<int64_t, int64_t>>& missingLinks)
		{
			if (TheGenOnlineGame != nullptr)
			{
				TheGenOnlineGame->AbortStart();
			}

			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, GetReasonText(START_REJECTED_TEXTS, strReason), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
			ShowMissingLinks(missingLinks);
		});

	pLobbyInterface->RegisterForLobbyEvent(OnLobbyEvent);


	if (TheGenOnlineGame == nullptr || (TheGenOnlineGame && TheGenOnlineGame->isGameInProgress()))
	{
		if (TheGenOnlineGame != nullptr)
		{
			TheGenOnlineGame->setGameInProgress(FALSE);
		}

		pLobbyInterface->LeaveCurrentLobby();

		// check if we were disconnected

		// TODO_GENONLINE: Handle disconnected
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

		// TODO_GENONLINE: Only do this if still connected to service
		//if (TheGameSpyPeerMessageQueue && TheGameSpyPeerMessageQueue->isConnected())
		{
			DEBUG_LOG(("We're still connected, so pushing back on the lobby"));
			TheShell->push("Menus/GenOnlineCustomLobby.wnd", TRUE);
		}

		return;
	}


	delete TheNAT;
	TheNAT = NULL;

	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	launchGameNext = FALSE;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	GenOnlineInitGameGadgets();
	EnableSlotListUpdates(TRUE);
	// TODO_GENONLINE

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();

	// TODO_GENONLINE
	GenOnlineGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (game == nullptr)
	{
		return;
	}

	GenOnlineGameSlot* hostSlot = game->getGameSpySlot(0);
	hostSlot->setAccept();

	bool bIsHost = pLobbyInterface->IsHost();

	if (bIsHost)
	{
		// TODO_GENONLINE
		/*
		OptionPreferences natPref;
		CustomMatchPreferences customPref;
		game->setMap(customPref.getPreferredMap());
		*/

		// TODO_GENONLINE: Preferred color & factionsupport
		hostSlot->setColor(0);
		hostSlot->setPlayerTemplate(PLAYERTEMPLATE_RANDOM);
		//hostSlot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)natPref.getFirewallBehavior());
		hostSlot->setPingString(UnicodeString(L"TODO_GENONLINE"));

		CustomMatchPreferences customPref;


	// Recorded stats games can never limit superweapons, limit armies, or have inflated starting cash.
		// This should probably be enforced at the gamespy level as well, to prevent expoits.
#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		Int isUsingStats = game->getUseStats();
#endif

#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		game->setStartingCash( isUsingStats? TheMultiplayerSettings->getDefaultStartingMoney() : customPref.getStartingCash() );
		game->setSuperweaponRestriction( isUsingStats? 0 : customPref.getSuperweaponRestricted() ? 1 : 0 );
		if (isUsingStats)
			game->setOldFactionsOnly( 0 );
#else
		game->setStartingCash(customPref.getStartingCash());
		game->setSuperweaponRestriction(customPref.getSuperweaponRestricted() ? 1 : 0);
#endif

		//game->setOldFactionsOnly( customPref.getFactionsLimited() );
    if ( game->oldFactionsOnly() )
    {
      // Make sure host follows the old factions only restrictions!
      const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(hostSlot->getPlayerTemplate());

      if ( fac != NULL && !fac->isOldFaction() )
      {
        hostSlot->setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
      }
    }


		for (Int i=1; i<MAX_SLOTS; ++i)
		{
			GenOnlineGameSlot *slot = game->getGameSpySlot(i);
			slot->setState( SLOT_OPEN );
		}

		// TODO_GENONLINE: preferred map support
		//AsciiString lowerMap = getDefaultOfficialMap();
		AsciiString lowerMap = game->getMap();
		//AsciiString lowerMap = customPref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			hostSlot->setMapAvailability(TRUE);
			game->setMapCRC( it->second.m_CRC );
			game->setMapSize( it->second.m_filesize );

			game->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
		}


		GenOnlineDisplaySlotList();
		GenOnlineDisplayGameOptions();

		// TheSuperHackers @tweak disable the combo box for the host's player name
		comboBoxPlayer[0]->winEnable(FALSE);

		// A quick match host keeps the map the service chose.
		buttonSelectMap->winEnable(pLobbyInterface->CanChangeLobbySettings());
	}
	else
	{
		OptionPreferences natPref;
		CustomMatchPreferences customPref;
		AsciiString options;
		//PeerRequest req;
		UnicodeString uName = hostSlot->getName();
		AsciiString aName;
		aName.translate(uName);

		// TODO_GENONLINE: Do this on join? and map change
		AsciiString asciiMap = game->getMap();
		asciiMap.toLower();

		/*
		req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
		req.UTM.isStagingRoom = TRUE;
		req.id = "REQ/";
		req.nick = aName.str();
		options.format("PlayerTemplate=%d", customPref.getPreferredFaction());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("Color=%d", customPref.getPreferredColor());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("NAT=%d", natPref.getFirewallBehavior());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("Ping=%s", TheGameSpyInfo->getPingString().str());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		*/
		game->setMapCRC( game->getMapCRC() );		// force a recheck
		game->setMapSize( game->getMapSize() ); // of if we have the map

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
		GenOnlineInitialAcceptEnable = FALSE;

		GenOnlineDisplaySlotList();
		GenOnlineDisplayGameOptions();
	}

	// Show the Menu
	layout->hide( FALSE );

	// Make sure the text fields are clear
	GadgetListBoxReset( GenOnlineListboxGameSetupChat );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	GadgetTextEntrySetCompletionEnabled(textEntryChat, TRUE);
	s_gameSetupChatCycler.Reset();

	initDone = true;

	// TODO_GENONLINE
	//TheGameSpyInfo->setGameOptions();
	//TheShell->registerWithAnimateManager(parentWOLGameSetup, WIN_ANIMATION_SLIDE_TOP, TRUE);
	GenOnlinePositionStartSpots();

	GenOnlineLastSlotlistTime = 0;
	GenOnlineGameSetupEnterTime = timeGetTime();

	// Set Keyboard to chat entry
	raiseMessageBoxes = true;
	TheTransitionHandler->setGroup("GameSpyGameOptionsMenuFade");
	TheWindowManager->winSetFocus(textEntryChat);

	// if (pLobbyInterface->IsInLobby())
	{
		LobbyEntry& theLobby = pLobbyInterface->GetCurrentLobby();

		if (theLobby.max_cam_height != GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM)
		{


			if (!pLobbyInterface->IsHost())
			{
				UnicodeString strInform;
				strInform.format(L"Camera height: The host set the limit to %lu.", theLobby.max_cam_height);
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strInform, GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
			}
			else
			{
				UnicodeString strInform;
				strInform.format(L"Camera height: Your limit is %lu. Use /maxcameraheight <value> to change it. Default: 310.", theLobby.max_cam_height);
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, strInform, GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
			}

		}
	}

    if (pLobbyInterface != nullptr)
    {
        if (pLobbyInterface->IsHost())
        {
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
        }
    }
    
    if (TheGenOnlineGame != nullptr)
	{
		if (!TheGenOnlineGame->getAllowObservers())
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Observers: Disabled by the host."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
		}
	}

	// A player running Easy Anti-Cheat learns when the joined game lets in players without it.
	if (pLobbyInterface != nullptr && !pLobbyInterface->IsHost() && LocalAnticheatKind() == EAnticheatKind::Eac
		&& ParseAnticheatKind(pLobbyInterface->GetCurrentLobby().anticheat) != EAnticheatKind::Eac)
	{
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat,
			TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOEacNotRequired", L"Easy Anti-Cheat is not required in this game. Other players may not be running it."),
			GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
	}

	GenOnlineUserSectionShow(parentWOLGameSetup);

    // Update the communicator button anytime we get notifications
    GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
    if (pSocialInterface != nullptr)
    {
        // notifiactions callback
        pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
            {
                // update communicator button
                GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
                if (buttonBuddy != nullptr)
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
                    buttonBuddy->winSetText(buttonText);
                }
            });
    }

    // And also initialize it
    GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
    if (buttonBuddy != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
    {
        UnicodeString buttonText;
        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
        buttonBuddy->winSetText(buttonText);
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
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		// TODO_GENONLINE: Handle disconnect again
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
		TheGenOnlineGame->launchGame();
		TheGameSpyInfo->leaveStagingRoom();
	}
	*/

	nextScreen = NULL;

}

//-------------------------------------------------------------------------------------------------
/** GameSpy Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlineGameSetupMenuShutdown( WindowLayout *layout, void *userData )
{
	GenOnlineUserSectionHide();
	s_gameSetupMenuOpen = FALSE;
	GenOnlineCloseRightClickMenus();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->DeregisterForChatCallback();
		pLobbyInterface->DeregisterForCannotConnectToLobbyCallback();
		pLobbyInterface->DeregisterForPlayerDoesntHaveMapCallback();
		pLobbyInterface->DeregisterForRosterNeedsRefreshCallback();
		pLobbyInterface->DeregisterForGameStartPacket();
		pLobbyInterface->DeregisterForStartGameRejected();
		pLobbyInterface->DeregisterForLobbyEvent();
	}
	
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->DeregisterForConnectionEvents();
	}


	if( GenOnlineMapSelectLayout )
	{
		GenOnlineMapSelectLayout->destroyWindows();
		deleteInstance(GenOnlineMapSelectLayout);
		GenOnlineMapSelectLayout = NULL;
	}
	parentWOLGameSetup = NULL;
	EnableSlotListUpdates(FALSE);
	GenOnlineDeinitGameGadgets();
	if (TheEstablishConnectionsMenu != NULL)
	{
		TheEstablishConnectionsMenu->endMenu();
	}
	initDone = false;
	if (TheGenOnlineGame != nullptr)
	{
		TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
	}

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

static const int64_t START_REQUEST_TIMEOUT_MS = 30000;

static void UpdateStartPhase()
{
	auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || TheGenOnlineGame == nullptr || TheGenOnlineGame->isGameInProgress())
	{
		return;
	}

	const EStartPhase phase = TheGenOnlineGame->GetStartPhase();
	if (phase == EStartPhase::Idle || phase == EStartPhase::Starting)
	{
		return;
	}

	if (phase == EStartPhase::StartRequested && !lobby->IsHost())
	{
		TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
		return;
	}

	// A start request outlives a socket drop and ends only on its own timeout.
	auto ws = GenOnlineServicesManager::GetWebSocket();
	const bool socketLost = ws == nullptr || !ws->IsConnected();
	const bool timedOut = NowMs() - TheGenOnlineGame->GetStartPhaseTime() > START_REQUEST_TIMEOUT_MS;
	if (phase == EStartPhase::StartRequested ? timedOut : socketLost)
	{
		TheGenOnlineGame->SetStartPhase(EStartPhase::Idle);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"The start did not go through."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
	}
}

static void UpdateStartControls()
{
	auto* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr || TheGenOnlineGame == nullptr || TheGenOnlineGame->isGameInProgress()
		|| buttonStart == nullptr || buttonBack == nullptr || buttonSelectMap == nullptr
		|| comboBoxStartingCash == nullptr || checkBoxLimitSuperweapons == nullptr)
	{
		return;
	}

	const EStartPhase phase = TheGenOnlineGame->GetStartPhase();
	static EStartPhase s_shownPhase = EStartPhase::Idle;
	if (phase != s_shownPhase)
	{
		s_shownPhase = phase;
		if (phase == EStartPhase::Idle)
		{
			GenOnlineDisplaySlotList();
		}
	}

	const bool isHost = lobby->IsHost();
	const bool peersConnected = lobby->AreLocalLobbyPeersConnected();
	const bool connected = AreAllHumansSlottedAndConnected(lobby);
	const bool failed = lobby->HasFailedLobbyPeers();

	// A client may accept once it sees itself slotted and connected.
	if (!isHost && connected)
	{
		GenOnlineInitialAcceptEnable = TRUE;
	}

	bool settingsOpen = phase == EStartPhase::Idle;
	if (phase == EStartPhase::Countdown)
	{
		settingsOpen = TheGenOnlineGame->GetCountdownRemainingMs() > 1000;
	}

	buttonStart->winSetText(failed ? UnicodeString(L"Retry connections")
		: !peersConnected ? UnicodeString(L"Connecting...")
		: !connected ? UnicodeString(L"Updating players...") : TheGameText->fetch(isHost ? "GUI:Start" : "GUI:Accept"));
	if (phase != EStartPhase::Idle)
	{
		buttonStart->winEnable(FALSE);
	}
	else if (!connected)
	{
		buttonStart->winEnable(failed);
	}
	else
	{
		buttonStart->winEnable(isHost || GenOnlineInitialAcceptEnable);
	}

	buttonBack->winEnable(settingsOpen);
	const bool canChangeSettings = lobby->CanChangeLobbySettings();
	buttonSelectMap->winEnable(canChangeSettings && phase == EStartPhase::Idle);
	comboBoxStartingCash->winEnable(canChangeSettings && settingsOpen);
	checkBoxLimitSuperweapons->winEnable(canChangeSettings && settingsOpen);
	updateOptionRowEnables(canChangeSettings && settingsOpen);
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void GenOnlineGameSetupMenuUpdate( WindowLayout * layout, void *userData)
{
	// Refresh only the fast-changing connection indicators each frame.
	WOLRefreshConnectionIndicators();

	// need to exit?
	if (GenOnlineServicesManager::GetInstance() != nullptr && GenOnlineServicesManager::GetInstance()->IsPendingFullTeardown())
	{
		// Only if not in game and not in anim
		if ((TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress()) && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		{
			bool bForceShutdown = true;
			GenOnlineGameSetupMenuShutdown(layout, (void*)&bForceShutdown); // userdata is 'force shutdown'
			TearDownGeneralsOnline();

			TheShell->pop();
			return;
		}
	}
	
	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
		return;
	}

	if (GenOnlinePluginHost::g_bPendingExitLobby)
	{
		GenOnlinePluginHost::g_bPendingExitLobby = false;

        GSMessageBoxOk(TheGameText->fetchOrSubstitute("GUI:ACErrorHeader", L"AntiCheat Error"), TheGameText->fetchOrSubstitute("GUI:ACLobbyIntegrityError", L"Lobby integrity could not be validated. Leaving Lobby."));

        GenOnlinePopBackToLobby();
	}

	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
			bool bHostMigrationEnabledOnService = serviceConf.lobby.host_migration;

			if (bHostMigrationEnabledOnService)
			{
				if (pLobbyInterface->m_bHostMigrated)
				{
					pLobbyInterface->m_bHostMigrated = false;

					// If we are in-game, nothing to do here, the game handles it for us
					if (!TheGenOnlineGame->isGameInProgress()) // in progress is in game, ingame is just in lobby
					{
						// TODO_GENONLINE: Make sure a lobby get ran first.
						// did we become the host?
						bool bIsHost = pLobbyInterface->IsHost();

						if (bIsHost)
						{
							GenOnlineInitialAcceptEnable = TRUE;

							NetworkLog(ELogVerbosity::LOG_RELEASE, "Host left and server migrated the host to us...");

							GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Host: The previous host left. You are now the host."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);

							// NOTE: don't need to mark ourselves ready, the service did it for us upon migration
						}
						else
						{
							GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Host: The previous host left. A new host was selected."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
						}

						GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
						if (buttonBuddy != nullptr)
						{
							buttonBuddy->winEnable(FALSE);
						}
					}

					TheGenOnlineGame->UpdateSlotsFromCurrentLobby();

					GenOnlineDisplaySlotList();

					// Force a refresh to get latest lobby data
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
					if (pLobbyInterface != nullptr)
					{
						pLobbyInterface->UpdateRoomDataCache([](bool bSuccess)
							{

							});
					}
				}
			}

			if (pLobbyInterface->m_bPendingHostHasLeft || pLobbyInterface->m_bHostMigrated)
			{
				pLobbyInterface->m_bHostMigrated = false;
				pLobbyInterface->m_bPendingHostHasLeft = false;

				buttonPushed = true;
				DEBUG_LOG(("Host left lobby\n"));
				if (TheGenOnlineGame)
					TheGenOnlineGame->reset();

				GSMessageBoxOk(TheGameText->fetch("GUI:HostLeftTitle"), TheGameText->fetch("GUI:HostLeft"));

				GenOnlinePopBackToLobby();

				return;
			}
		}
	}

	auto* connectionLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (connectionLobby != nullptr && connectionLobby->GetCurrentLobby().lobby_type == ELobbyType::CustomGame
		&& TheGenOnlineGame != nullptr && !TheGenOnlineGame->isGameInProgress())
	{
		const bool peersConnected = connectionLobby->AreLocalLobbyPeersConnected();
		const bool connected = AreAllHumansSlottedAndConnected(connectionLobby);
		static UnsignedInt s_lastSlotRefreshTime = 0;
		if (peersConnected && !connected && timeGetTime() > s_lastSlotRefreshTime + 3000)
		{
			// peers are linked but the game lacks a slot for one, ask the service for the roster
			s_lastSlotRefreshTime = timeGetTime();
			connectionLobby->UpdateRoomDataCache();
		}
	}

	UpdateStartPhase();
	UpdateStartControls();

	// is there a countdown in progress?
	if (TheGenOnlineGame != nullptr && TheGenOnlineGame->GetStartPhase() == EStartPhase::Countdown)
	{
		const int64_t remainingMs = TheGenOnlineGame->GetCountdownRemainingMs();
		const Int secondsRemaining = remainingMs > 0 ? (Int)((remainingMs + 999) / 1000) : 0;
		if (secondsRemaining > 0 && secondsRemaining < countdownShownSecond)
		{
			ShowCountdownSecond(secondsRemaining);
		}
	}

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

	GenOnlineHandleBuddyResponses();

}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineGameSetupMenuInput( GameWindow *window, UnsignedInt msg,
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
	if ((msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN) && mouseInOptionViewport((Int)LOLONGTOSHORT(mData1), (Int)HILONGTOSHORT(mData1)))
	{
		scrollOptionRows(msg == GWM_WHEEL_UP ? -1 : 1);
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}


// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus();
}
extern int isThreadHosting;

Bool GenOnlineHandleGameSetupSlashCommands(UnicodeString uText);

Bool GenOnlineGameSetupMenuSubmitChatLine( UnicodeString text )
{
	text.trim();
	if (!s_gameSetupMenuOpen || text.isEmpty())
		return FALSE;

	if (GenOnlineHandleGameSetupSlashCommands(text))
		return TRUE;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
		return FALSE;

	pLobbyInterface->SendChatMessageToCurrentLobby(text, false);
	return TRUE;
}

Bool GenOnlineHandleGameSetupSlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		UnicodeString s;
		s.format(L"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, s, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		return TRUE; // was a slash command
	}
	else if (token == "me" && uText.getLength()>4)
	{
		UnicodeString msg = UnicodeString(uText.str() + 4); // skip the /me
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

		if (pLobbyInterface != nullptr)
		{
			pLobbyInterface->SendChatMessageToCurrentLobby(msg, true);
		}
		return TRUE; // was a slash command
	}

	else if (token == "help" || token == "commands")
	{
		const Color helpColor = GOUIColor(GOUICOLOR_HELP, GameMakeColor(127, 127, 127, 255));
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/me <message> - Send an emote."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/friendsonly - Let only friends join (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/public - Let anyone join (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/setpassword <password> - Set a lobby password (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/removepassword - Remove the lobby password (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/maxcameraheight <value> - Set the camera height limit (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/net - Show how you reach each player. /net transport, /net relay, /net gns ice and /net reset change your settings."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/support - Open the GeneralsOnline Discord."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"/help - Show these commands. You can also use /commands."), helpColor, -1, -1);
		return TRUE; // was a slash command
	}
	else if (token == "support")
	{
		ShellExecuteA(NULL, "open", "https://discord.generals.online", NULL, NULL, SW_SHOWNORMAL);
		return TRUE;
	}
	else if (token == "net")
	{
		AsciiString subcommand;
		AsciiString value;
		remainder.nextToken(&subcommand);
		// The value is the rest of the line because /net gns ice takes two words.
		value = remainder;
		value.trim();
		subcommand.toLower();
		value.toLower();
		GenOnlineHandleNetSlashCommand(subcommand, value, GenOnlineListboxGameSetupChat);
		return TRUE; // was a slash command
	}
	else if (token == "friendsonly")
	{
		if (applyLobbyAccess(TRUE))
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby access: Friends only. Use /public to let anyone join."), GOUIColor(GOUICOLOR_SUCCESS, GameMakeColor(0, 255, 0, 255)), -1, -1);
		}
		return TRUE; // was a slash command
	}
	else if (token == "public")
	{
		if (applyLobbyAccess(FALSE))
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends."), GOUIColor(GOUICOLOR_SUCCESS, GameMakeColor(0, 255, 0, 255)), -1, -1);
		}
		return TRUE; // was a slash command
	}
	else if (token == "maxcameraheight" && uText.getLength() > 17)
	{
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (pLobbyInterface != nullptr && pLobbyInterface->IsInLobby())
		{
			if (!pLobbyInterface->IsHost())
			{
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Camera height: Only the host can change it."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
				return TRUE; // was a slash command
			}

			AsciiString asciiVal;
			asciiVal.translate(UnicodeString(uText.str() + 17)); // skip the command

			// The value stops growing past the maximum so a long digit string cannot overflow.
			Int newCameraHeight = 0;
			bool bIsNumber = true;
			for (int i = 0; i < asciiVal.getLength(); ++i)
			{
				const char thisChar = asciiVal.getCharAt(i);
				if (!std::isdigit((unsigned char)thisChar))
				{
					bIsNumber = false;
					break;
				}
				newCameraHeight = (std::min)(newCameraHeight * 10 + (thisChar - '0'), GENONLINE_MAX_LOBBY_CAMERA_ZOOM + 1);
			}

			if (!bIsNumber)
			{
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Camera height: Enter a number."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
			}
			else if (!applyMaxCameraHeight(newCameraHeight))
			{
				UnicodeString msg;
				msg.format(L"Camera height: Enter a value from %d to %d.", GENONLINE_MIN_LOBBY_CAMERA_ZOOM, GENONLINE_MAX_LOBBY_CAMERA_ZOOM);
				GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, msg, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
			}
		}

		return TRUE; // was a slash command
	}
	// else if (token == "leave")
	// {
	// 	return TRUE;
	// }
	// else if (token == "quit")
	// {
	// 	TheGameEngine->setQuitting(TRUE);
	// 	return TRUE;
	// }
	else if (token == "steam" || token == "advnet")
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Writing advanced networking stats:");

		std::map<int64_t, PlayerConnection>& connections = GenOnlineServicesManager::GetNetworkMesh()->GetAllConnections();
		for (auto& kvPair : connections)
		{
			PlayerConnection& conn = kvPair.second;

			NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Connection to user %lld: %s", kvPair.first, conn.GetStats().c_str());
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Advanced networking stats dumped");
		GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Network debug: Statistics were written to the log file."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);

		return TRUE;
	}
	else if (token == "setpassword" && uText.getLength() > 13)
	{
		UnicodeString newPassword(uText.str() + 13); // skip the /setpassword and space
		const LobbyPasswordResult result = changeLobbyPassword(&newPassword);
		if (result == LOBBY_PASSWORD_BAD_LENGTH)
		{
			UnicodeString errorMsg;
			errorMsg.format(L"Lobby password: Use 1 to %d characters.", GENONLINE_LOBBY_MAX_PASSWORD_LENGTH);
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, errorMsg, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
		}
		else if (result == LOBBY_PASSWORD_NOT_HOST)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby password: Only the host can change it."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
		}
		else if (result == LOBBY_PASSWORD_APPLIED)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby password: Updated. Use /removepassword to remove it."), GOUIColor(GOUICOLOR_SUCCESS, GameMakeColor(0, 255, 0, 255)), -1, -1);
		}

		return TRUE; // was a slash command
	}
	else if (token == "removepassword" || token == "clearpassword" || token == "resetpassword")
	{
		const LobbyPasswordResult result = changeLobbyPassword(nullptr);
		if (result == LOBBY_PASSWORD_NOT_HOST)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby password: Only the host can change it."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
		}
		else if (result == LOBBY_PASSWORD_NONE_SET)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby password: No password is set. Use /setpassword <password> to add one."), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)), -1, -1);
		}
		else if (result == LOBBY_PASSWORD_APPLIED)
		{
			GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Lobby password: Removed. Use /setpassword <password> to add one."), GOUIColor(GOUICOLOR_SUCCESS, GameMakeColor(0, 255, 0, 255)), -1, -1);
		}

		return TRUE; // was a slash command
	}
#if defined(RTS_DEBUG)
	else if (token == "slots")
	{
		g_genOnlineDebugSlots = !g_genOnlineDebugSlots;
		return TRUE; // was a slash command
	}
#endif // defined(RTS_DEBUG)

	GadgetListBoxAddEntryText(GenOnlineListboxGameSetupChat, UnicodeString(L"Unknown command: Use /help to see all commands."), GameSpyColor[GSCOLOR_CHAT_NORMAL], -1, -1);
	return TRUE;
}

static Int getNextSelectablePlayer(Int start)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return -1;
	}

	GenOnlineGame* game = pLobbyInterface->GetCurrentGame();
	if (game == nullptr || !game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		GenOnlineGameSlot *slot = game->getGameSpySlot(j);

		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER) ||
			slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

static void BuildGameSetupChatCompletionContext(ChatCompletionContext& context)
{
	static ChatCommandRegistry s_commands;
	static Bool s_commandsBuilt = FALSE;
	if (!s_commandsBuilt)
	{
		BuildGameSetupChatCommands(s_commands);
		s_commandsBuilt = TRUE;
	}
	context.commands = &s_commands;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
		return;

	const std::wstring self = (pAuthInterface != nullptr) ? pAuthInterface->GetDisplayNameW() : std::wstring();
	for (const LobbyMemberEntry& member : pLobbyInterface->GetCurrentLobby().members)
	{
		if (member.user_id != -1)
			AppendChatName(context.names, from_utf8(member.display_name), self);
	}
}

static void UpdateGameSetupChatCompletionHint()
{
	s_gameSetupChatCycler.Reset();
	ChatCompletionContext context;
	BuildGameSetupChatCompletionContext(context);
	const std::wstring text = GadgetTextEntryGetText(textEntryChat).str();
	GadgetTextEntrySetCompletionHint(textEntryChat, UnicodeString(ChatCompletionHint(text, context).c_str()));
}

static void CompleteGameSetupChat(Bool reverse)
{
	ChatCompletionContext context;
	BuildGameSetupChatCompletionContext(context);
	std::wstring text = GadgetTextEntryGetText(textEntryChat).str();
	if (!s_gameSetupChatCycler.Complete(text, context, reverse != FALSE))
		return;

	GadgetTextEntrySetText(textEntryChat, UnicodeString(text.c_str()));
	GadgetTextEntrySetCompletionHint(textEntryChat, UnicodeString(ChatCompletionHint(text, context).c_str()));
}

//-------------------------------------------------------------------------------------------------
/** WOL Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineGameSetupMenuSystem( GameWindow *window, UnsignedInt msg,
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
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				if (control == nullptr || control != GenOnlineListboxGameSetupChat)
					return MSG_IGNORED;
				RightClickStruct *rc = (RightClickStruct *)mData2;
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (rc->pos < 0 || pLobbyInterface == nullptr)
					break;
				const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(control, rc->pos, 0)));
				std::string messageID;
				int64_t authorID = 0;
				pLobbyInterface->FindChatMessage(key, messageID, authorID);
				const LobbyMemberEntry author = pLobbyInterface->GetRoomMemberFromID(authorID);
				GenOnlineOpenMessageMenu(control, rc->pos, rc->mouseX, rc->mouseY, messageID.c_str(), authorID, author.display_name.c_str());
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

			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface == nullptr)
			{
				break;
			}
			
			GenOnlineGame* myGame = pLobbyInterface->GetCurrentGame();

			if (myGame == nullptr)
			{
				break;
			}

			if (syncingOptions)
			{
				break;
			}

			if (controlID == comboBoxStartingCashID)
			{
				handleStartingCashSelection();
			}
			else if (control == comboBoxLobbyAccess)
			{
				handleLobbyAccessSelection();
			}
			else if (control == comboBoxCameraHeight)
			{
				handleCameraHeightSelection();
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
						// Get
						Int pos = -1;
						GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						if (pos != SLOT_PLAYER && pos >= 0)
						{
							if (myGame->getSlot(i)->getState() == SLOT_PLAYER)
							{
								// TODO_GENONLINE: Support kick again
								/*
								PeerRequest req;
								req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
								req.UTM.isStagingRoom = TRUE;
								AsciiString aName;
								aName.translate(myGame->getSlot(i)->getName());
								req.nick = aName.str();
								req.id = "KICK/";
								req.options = "true";
								TheGameSpyPeerMessageQueue->addRequest(req);
								*/

								UnicodeString name = myGame->getSlot(i)->getName();

								// NOTE: No host check here, service enforces it
								GenOnlineGameSlot* pSlot = (GenOnlineGameSlot*)myGame->getSlot(i);
								int64_t userBeingKicked = pSlot->m_userID;

								pLobbyInterface->UpdateCurrentLobby_KickUser(userBeingKicked, name);
								pLobbyInterface->UpdateCurrentLobby_SetSlotState(i, SlotState(pos));  // use what host selected
								myGame->getSlot(i)->setState(SlotState(pos));
								myGame->resetAccepted();

								// // TODO_GENONLINE
								//TheGameSpyInfo->setGameOptions();
								GenOnlineDisplaySlotList();
								//TheLAN->OnPlayerLeave(name);
							}
							else if (myGame->getSlot(i)->getState() != pos)
							{
								Bool wasAI = (myGame->getSlot(i)->isAI());
								myGame->getSlot(i)->setState(SlotState(pos));
								Bool isAI = (myGame->getSlot(i)->isAI());
								myGame->resetAccepted();
								if (wasAI ^ isAI)
									PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI && myGame->getAllowObservers());

								// inform service
								GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
								if (pLobbyInterface != nullptr)
								{
									pLobbyInterface->UpdateCurrentLobby_SetSlotState(i, pos);
								}

								GenOnlineDisplaySlotList();
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

				if (control == buttonOptionsUp || control == buttonOptionsDown)
				{
					scrollOptionRows(control == buttonOptionsUp ? -1 : 1);
					break;
				}

				if (handleRowButton(controlID))
					break;

				if ( controlID == buttonBackID )
				{
					savePlayerInfo();
					if( GenOnlineMapSelectLayout )
					{
						GenOnlineMapSelectLayout->destroyWindows();
						deleteInstance(GenOnlineMapSelectLayout);
						GenOnlineMapSelectLayout = NULL;
					}

					
					GenOnlinePopBackToLobby();

				}
				else if ( controlID == buttonCommunicatorID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );

				}
				else if ( controlID == buttonChatID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					s_gameSetupChatCycler.Reset();
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
					{
						GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
						if (pLobbyInterface != nullptr)
						{
							pLobbyInterface->SendChatMessageToCurrentLobby(txtInput, false);
						}
					}
				}
				else if ( controlID == buttonSelectMapID )
				{
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
					if (pLobbyInterface == nullptr || !pLobbyInterface->CanChangeLobbySettings())
					{
						break;
					}

					GenOnlineMapSelectLayout = TheWindowManager->winCreateLayout( "Menus/GenOnlineMapSelectMenu.wnd" );
					GenOnlineMapSelectLayout->runInit();
					GenOnlineMapSelectLayout->hide( FALSE );
					GenOnlineMapSelectLayout->bringForward();
				}
				else if ( controlID == buttonStartID )
				{
					if (RetryMissingLobbyPeerLinks())
					{
						break;
					}
					savePlayerInfo();
					
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
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
						auto game = pLobbyInterface->GetCurrentGame();
						if (game == nullptr)
						{
							break;
						}

						GameSlot *localSlot = game->getSlot(game->getLocalSlotNum());
						if (localSlot)
						{
							localSlot->setAccept();
						}

						// force a refresh of our local lobby properties to sync to remote players
						pLobbyInterface->ApplyLocalUserPropertiesToCurrentNetworkRoom();

						/*
						UnicodeString hostName = game->getSlot(0)->getName();
						AsciiString asciiName;
						asciiName.translate(hostName);
						PeerRequest req;
						req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
						req.UTM.isStagingRoom = TRUE;
						req.id = "accept";
						req.nick = asciiName.str();
						req.options = "true";
						TheGameSpyPeerMessageQueue->addRequest(req);
						//peerSetReady( PEER, PEERTrue );
						GenOnlineDisplaySlotList();
						*/
					}
				}
        else if ( controlID == checkBoxLimitSuperweaponsID )
        {
          handleLimitSuperweaponsClick();
        }
				else
				{
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					GenOnlineGame* game = pLobbyInterface->GetCurrentGame();
					if (game == nullptr)
					{
						break;
					}

					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								GenOnlineGameSlot *slot = game->getGameSpySlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								GenOnlineGameSlot *slot = game->getGameSpySlot(playerIdxInPos);
								if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
								{
									// it's one of my type.  Try to change it.
									Int nextPlayer = getNextSelectablePlayer(playerIdxInPos+1);
									handleStartPositionSelection(playerIdxInPos, -1);
									if (nextPlayer >= 0)
									{
										handleStartPositionSelection(nextPlayer, i);
									}
								}
							}
							else
							{
								// nobody in the slot - put us in
								Int nextPlayer = getNextSelectablePlayer(0);
								if (nextPlayer < 0)
									nextPlayer = getFirstSelectablePlayer(game);
								handleStartPositionSelection(nextPlayer, i);
							}
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

				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobbyInterface == nullptr)
				{
					break;
				}

				GenOnlineGame* game = pLobbyInterface->GetCurrentGame();
				if (game == nullptr)
				{
					break;
				}

				for (Int i = 0; i < MAX_SLOTS; i++)
				{
					if (controlID == buttonMapStartPositionID[i])
					{
						Int playerIdxInPos = -1;
						for (Int j=0; j<MAX_SLOTS; ++j)
						{
							GenOnlineGameSlot *slot = game->getGameSpySlot(j);
							if (slot && slot->getStartPos() == i)
							{
								playerIdxInPos = j;
								break;
							}
						}
						if (playerIdxInPos >= 0)
						{
							GenOnlineGameSlot *slot = game->getGameSpySlot(playerIdxInPos);
							if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
							{
								// it's one of my type.  Remove it.
								handleStartPositionSelection(playerIdxInPos, -1);
							}
						}
					}
				}
				break;
			}

		//-------------------------------------------------------------------------------------------------
		case GGM_FOCUS_CHANGE:
			{
				if (initDone && mData1 == FALSE && textEntryPassword != nullptr && (Int)mData2 == textEntryPassword->winGetWindowId())
				{
					applyPasswordEntry();
				}
				return MSG_IGNORED;
			}
		//-------------------------------------------------------------------------------------------------
		case GEM_UPDATE_TEXT:
			{
				if ((GameWindow *)mData1 == textEntryPassword)
				{
					passwordEdited = passwordEdited || !syncingOptions;
					break;
				}
				if ((GameWindow *)mData1 != textEntryChat)
					return MSG_IGNORED;

				UpdateGameSetupChatCompletionHint();
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GEM_TAB_COMPLETE:
			{
				if ((GameWindow *)mData1 != textEntryChat)
					return MSG_IGNORED;

				CompleteGameSetupChat(mData2 != 0);
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if (control == textEntryPassword)
				{
					applyPasswordEntry();
					break;
				}
				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{

					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					s_gameSetupChatCycler.Reset();
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
					{
						if (!GenOnlineHandleGameSetupSlashCommands(txtInput))
						{
							GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
							if (pLobbyInterface != nullptr)
							{
								pLobbyInterface->SendChatMessageToCurrentLobby(txtInput, false);
							}
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

	if (TheGenOnlineGame != nullptr)
	{
		TheGenOnlineGame->reset();
	}

	GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSKicked"));
	nextScreen = "Menus/GenOnlineCustomLobby.wnd";
	TheShell->pop();
}

static void statsMoveStartPosition(Int slot, Int position)
{
	handleStartPositionSelection(slot, position);
}

Bool GenOnlineGameSetupMenuStatsLobby( StatsLobbyScreen& out )
{
	if (!s_gameSetupMenuOpen || !parentWOLGameSetup || !buttonStart)
		return FALSE;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr || !pLobbyInterface->IsInLobby())
		return FALSE;

	out.game = pLobbyInterface->GetCurrentGame();
	if (out.game == nullptr)
		return FALSE;

	out.isHost = pLobbyInterface->IsHost();
	out.localSlot = out.game->getLocalSlotNum();
	if (buttonPushed)
		out.blocked = "the screen is leaving or starting the game";
	else if (GenOnlineMapSelectLayout)
		out.blocked = "the map selector is open";
	else if (!initDone)
		out.blocked = "the screen is still opening";
	out.comboPlayer = comboBoxPlayer;
	out.comboColor = comboBoxColor;
	out.comboTemplate = comboBoxPlayerTemplate;
	out.comboTeam = comboBoxTeam;
	out.buttonStartPosition = buttonMapStartPosition;
	out.comboStartingCash = comboBoxStartingCash;
	out.checkLimitSuperweapons = checkBoxLimitSuperweapons;
	out.buttonSelectMap = buttonSelectMap;
	out.buttonStart = buttonStart;
	out.moveStartPosition = statsMoveStartPosition;
	out.applyMap = GenOnlineMapSelectMenuApplyMap;
	return TRUE;
}
