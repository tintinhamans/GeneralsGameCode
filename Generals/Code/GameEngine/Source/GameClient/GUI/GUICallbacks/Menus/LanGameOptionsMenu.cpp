/*
**	Command & Conquer Generals(tm)
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
// FILE: LanGameOptionsMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/PlayerTemplate.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/UserPreferences.h"
#include "Common/QuotedPrintable.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameInfoWindow.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/Caster/Caster.h"
#include "GameNetwork/Caster/CasterChatMessage.h"
#include "GameNetwork/Caster/CasterLobby.h"
#include "GameNetwork/Caster/CasterProtocol.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameClient/LanguageFilter.h"
#include "GameNetwork/GUIUtil.h"


extern char *LANnextScreen;
extern Bool LANisShuttingDown;
extern Bool LANbuttonPushed;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);
extern void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse);
Color white = GameMakeColor( 255, 255, 255, 255 );
static bool s_isIniting = FALSE;
// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanGameOptionsID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
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

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonChatID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanGameOptions = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *buttonChat = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *checkboxLimitSuperweapons = nullptr;
static GameWindow *comboBoxStartingCash = nullptr;
static GameWindow *windowMap = nullptr;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {0};
static GameWindow *buttonAccept[MAX_SLOTS] = {0};

static GameWindow *comboBoxColor[MAX_SLOTS] = {0};

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {0};

static GameWindow *comboBoxTeam[MAX_SLOTS] = {0};

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {0};
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxChatWindowLanGame = nullptr;
NameKeyType listboxChatWindowLanGameID = NAMEKEY_INVALID;
WindowLayout *mapSelectLayout = nullptr;

static Int getNextSelectablePlayer(Int start)
{
	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		LANGameSlot *slot = game->getLANSlot(j);
		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER)
			|| slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || (slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER))
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow );
void LanPositionStartSpots()
{

	positionStartSpots( TheLAN->GetMyGame(), buttonMapStartPosition, windowMap);
}
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	Int i=0;
	for (; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	LANGameSlot *slot = TheLAN->GetMyGame()->getLANSlot(i);
	if (!slot)
		return;

	LANPlayer *player = slot->getUser();
	if (!player)
	{
		DEBUG_ASSERTCRASH(TheLAN->GetMyGame()->getIP(i) == 0, ("No player info in listbox!"));
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}

	setLANPlayerTooltip(player);
}

void StartPressed()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	Bool isReady = true;
	Bool allHaveMap = true;
	Int playerCount = 0;
	if (!myGame)
	{
		return;
	}
	myGame->getLANSlot(0)->setAccept(); // cause we are, of course!

	int i;

	int numUsers = 0;
	int numHumans = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot && slot->isHuman())
				numHumans++;
			numUsers++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < numUsers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !numHumans)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few players
	if (numUsers < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams;
	for (i=0; i<MAX_SLOTS; ++i)
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
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
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
		mapDisplayName.format(L"%hs", myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( i = 0; i < MAX_SLOTS; i++ )
	{
		LANGameSlot *slot = myGame->getLANSlot(i);
		if( slot->isHuman() && !slot->isAccepted())
		{
			isReady = false;
			if (!willTransfer)
			{
				if (!slot->hasMap())
				{
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:PlayerNoMap"), slot->getName().str(), mapDisplayName.str());
					GadgetListBoxAddEntryText(listboxChatWindowLanGame, msg , chatSystemColor, -1, 0);
					allHaveMap = false;
				}
			}
		}
		if( slot->isHuman() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			playerCount++;
	}

	if(isReady)
	{
		for( i = 0; i < MAX_SLOTS; i++ )
		{
			LANGameSlot *slot = myGame->getLANSlot(i);
			if (slot && slot->isOpen())
			{
				slot->setState( SLOT_CLOSED );
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], SLOT_CLOSED);
			}
		}
		Int seconds = TheMultiplayerSettings->getStartCountdownTimerSeconds();
		if (seconds)
			TheLAN->RequestGameStartTimer(seconds);
		else
			TheLAN->RequestGameStart();
		LANEnableStartButton(false);
	}
	else
	{
		// Does everyone have the map?
		if (allHaveMap)
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, TheGameText->fetch("GUI:NotifiedStartIntent") , chatSystemColor, -1, 0);
			TheLAN->RequestAccept();
		}
	}

}

void LANEnableStartButton(Bool enabled)
{
	buttonStart->winEnable(enabled);
	buttonSelectMap->winEnable(enabled);
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					LANGameSlot *checkSlot = myGame->getLANSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}
			if(!colorAvailable)
				return;
		}

		slot->setColor(color);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (!slot->isLocalPlayer() || !AreSlotListUpdatesEnabled())
				return;

			AsciiString options;
			options.format("Color=%d", color);
			TheLAN->RequestGameOptions(options, true);
		}
	}
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		Int oldTemplate = slot->getPlayerTemplate();
		slot->setPlayerTemplate(playerTemplate);

		if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// was observer, so populate color & team with all, and enable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}
		else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// is becoming observer, so populate color & team with random only, and disable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}

		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the playerTemplate from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("PlayerTemplate=%d", playerTemplate);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartPositionSelection(Int player, int startPos)
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(player);
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
		slot->setStartPos(startPos);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("StartPos=%d", slot->getStartPos());
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the team from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("Team=%d", team);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

void lanUpdateSlotList()
{
	if(!AreSlotListUpdatesEnabled() || s_isIniting)
		return;
	UpdateSlotList( TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor,
		comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

	updateMapStartSpots(TheLAN->GetMyGame(), buttonMapStartPosition);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
static GameWindow *findLayoutWindow(WindowLayout *layout, NameKeyType key)
{
	GameWindow *window;
	GameWindow *found;

	for (window = layout != nullptr ? layout->getFirstWindow() : nullptr; window != nullptr;
		window = window->winGetNextInLayout())
	{
		if (window->winGetWindowId() == key)
			return window;
		found = TheWindowManager->winGetWindowFromId(window, key);
		if (found != nullptr)
			return found;
	}
	return TheWindowManager->winGetWindowFromId(nullptr, key);
}

static void BindLanGameGadgets(WindowLayout *layout)
{
	parentLanGameOptionsID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:LanGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonStart" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryMapDisplay" );
	listboxChatWindowLanGameID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame" );
	buttonChatID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonEmote" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonSelectMap" );
	windowMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:MapWindow" );

	parentLanGameOptions = findLayoutWindow(layout, parentLanGameOptionsID);
	buttonChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonChatID );
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonSelectMapID );
	buttonStart = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonStartID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonBackID );
	listboxChatWindowLanGame = TheWindowManager->winGetWindowFromId( parentLanGameOptions, listboxChatWindowLanGameID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryChatID );
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryMapDisplayID );
	checkboxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentLanGameOptions,
		TheNameKeyGenerator->nameToKey("LanGameOptionsMenu.wnd:CheckboxLimitSuperweapons") );
	comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentLanGameOptions,
		TheNameKeyGenerator->nameToKey("LanGameOptionsMenu.wnd:ComboBoxStartingCash") );
	windowMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions, windowMapID );

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString id;
		id.format("LanGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey(id);
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, comboBoxPlayerID[i]);
		id.format("LanGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey(id);
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, comboBoxColorID[i]);
		id.format("LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey(id);
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, comboBoxPlayerTemplateID[i]);
		id.format("LanGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey(id);
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, comboBoxTeamID[i]);
		id.format("LanGameOptionsMenu.wnd:ButtonAccept%d", i);
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey(id);
		buttonAccept[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, buttonAcceptID[i]);
		id.format("LanGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey(id);
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId(parentLanGameOptions, buttonMapStartPositionID[i]);
	}
}

void InitLanGameGadgets(WindowLayout *layout)
{
	BindLanGameGadgets(layout);
	// This screen hosts caster chat while the live LAN room is up.
	SetLanGameOptionsCasterChatWindow(listboxChatWindowLanGame);
	DEBUG_ASSERTCRASH(parentLanGameOptions, ("Could not find the parentLanGameOptions"));
	DEBUG_ASSERTCRASH(buttonChat, ("Could not find the buttonChat"));
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	DEBUG_ASSERTCRASH(buttonBack, ("Could not find the buttonBack"));
	DEBUG_ASSERTCRASH(listboxChatWindowLanGame, ("Could not find the listboxChatWindowLanGame"));
	DEBUG_ASSERTCRASH(textEntryChat, ("Could not find the textEntryChat"));
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the LanGameOptionsMenu.wnd:MapWindow"));

	Int localSlotNum = TheLAN->GetMyGame()->getLocalSlotNum();
	DEBUG_ASSERTCRASH(localSlotNum >= 0, ("Bad slot number!"));
	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		DEBUG_ASSERTCRASH(comboBoxPlayer[i], ("Could not find the comboBoxPlayer[%d]", i));
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);
		if (localSlotNum != i)
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Open"), white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Closed"), white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:EasyAI"), white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:MediumAI"), white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:HardAI"), white);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		}

		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]", i));
		PopulateColorComboBox(i, comboBoxColor, TheLAN->GetMyGame());
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]", i));
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheLAN->GetMyGame(), TRUE);
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]", i));
		PopulateTeamComboBox(i, comboBoxTeam, TheLAN->GetMyGame());
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]", i));
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]", i));
		if (i != 0)
			buttonAccept[i]->winHide(TRUE);
	}
	if (buttonAccept[0])
		GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor);
}

void DeinitLanGameGadgets()
{
	parentLanGameOptions = nullptr;
	buttonChat = nullptr;
	buttonSelectMap = nullptr;
	buttonStart = nullptr;
	buttonBack = nullptr;
	SetLanGameOptionsCasterChatWindow(nullptr);
	listboxChatWindowLanGame = nullptr;
	textEntryChat = nullptr;
	textEntryMapDisplay = nullptr;
	checkboxLimitSuperweapons = nullptr;
	comboBoxStartingCash = nullptr;
	if (windowMap)
	{
		windowMap->winSetUserData(nullptr);
		windowMap = nullptr;
	}
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = nullptr;
		comboBoxColor[i] = nullptr;
		comboBoxPlayerTemplate[i] = nullptr;
		comboBoxTeam[i] = nullptr;
		buttonAccept[i] = nullptr;
//		buttonStartPosition[i] = nullptr;
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
// Shared room presentation helpers.
//
// This screen hosts two rooms that drive the exact same gadgets: the normal LAN player room,
// which mutates the game through TheLAN, and the read-only caster room, which only renders a
// received snapshot.  Anything below is pure gadget manipulation with no room specific policy,
// so both room controllers may call it with their own already computed values.
//-------------------------------------------------------------------------------------------------

// Reads and clears the chat entry box.  Returns TRUE when the trimmed input is worth sending.
static Bool takeChatEntryInput(UnicodeString& input)
{
	if (textEntryChat == nullptr)
		return FALSE;
	// read the user's input
	input.set(GadgetTextEntryGetText( textEntryChat ));
	// Clear the text entry line
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	// Clean up the text (remove leading/trailing chars, etc)
	input.trim();
	return !input.isEmpty();
}

//-------------------------------------------------------------------------------------------------
// Normal LAN player room controller.
//-------------------------------------------------------------------------------------------------

// Echo the user's input to the chat window
static void playerRoomSendChat()
{
	UnicodeString txtInput;
	if (takeChatEntryInput(txtInput))
		TheLAN->RequestPlayerChat(txtInput);
}

//-------------------------------------------------------------------------------------------------
// Read-only caster room controller.
//-------------------------------------------------------------------------------------------------

static Bool s_readOnlyCasterMode = FALSE;
static WindowLayout *s_readOnlyLayout = nullptr;
static Bool s_readOnlyShellOwned = FALSE;
static Bool s_readOnlyShuttingDown = FALSE;
static CasterLobby::LobbyViewModel s_readOnlyLastView;
static Bool s_readOnlyHaveLastView = FALSE;
static UnsignedInt s_readOnlyCountdownRevision = 0;
static AsciiString s_readOnlyWarnedMap;

static AsciiString resolveReadOnlyMapPath(const char *mapName)
{
	AsciiString remaining(mapName);
	AsciiString token;
	AsciiString path;
	if (remaining.isEmpty())
		return AsciiString::TheEmptyString;

	remaining.nextToken(&token, "\\/");
	while (!remaining.isEmpty())
	{
		path.concat(token);
		path.concat('\\');
		remaining.nextToken(&token, "\\/");
	}
	path.concat(token);
	path.concat('\\');
	path.concat(token);
	path.concat('.');
	path.concat(TheMapCache->getMapExtension());
	path = TheGameState->portableMapPathToRealMapPath(path);
	path.toLower();
	return path;
}

// The read-only room renders its slot rows through the very same UpdateSlotList() that drives the
// player room, so the received snapshot is mirrored into this synthetic GameInfo.  The object is
// deliberately inert: it never enters a game, so GameInfo::amIHost() and GameInfo::getLocalSlotNum()
// bail out on m_inGame and answer FALSE and -1 for good.  Every branch in UpdateSlotList() and
// EnableAcceptControls() that can enable a control sits behind one of those two answers, so no
// enabling branch can ever fire here and the room cannot stop being read-only.  As a second and
// independent barrier the local IP is parked on a value no slot can carry, because slot IPs are
// never set and stay at zero.
static const UnsignedInt READ_ONLY_UNREACHABLE_IP = 0xFFFFFFFF;

class ReadOnlyGameInfo : public GameInfo
{
public:
	ReadOnlyGameInfo()
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
			setSlotPointer(i, &m_readOnlySlot[i]);
		setLocalIP(READ_ONLY_UNREACHABLE_IP);
	}

private:
	GameSlot m_readOnlySlot[MAX_SLOTS];
};

static ReadOnlyGameInfo *s_readOnlyGame = nullptr;

// Built on demand, because GameInfo::reset() reads TheGlobalData, and released with the room so
// that it never outlives the engine's allocators.
static ReadOnlyGameInfo *readOnlyGameInfo()
{
	if (s_readOnlyGame == nullptr)
		s_readOnlyGame = NEW ReadOnlyGameInfo;
	return s_readOnlyGame;
}

// Mirrors one snapshot row onto the matching synthetic slot.  The slot IP is never touched.
static void fillReadOnlySlot(const CasterLobby::LobbyViewRow& row, GameSlot& slot)
{
	UnicodeString name;

	switch (row.kind)
	{
	case CasterLobby::LOBBY_SLOT_HUMAN:
		name.translate(row.name);
		slot.setState(SLOT_PLAYER, name);
		break;
	case CasterLobby::LOBBY_SLOT_AI:
		if (row.aiDifficulty == 'E')
			slot.setState(SLOT_EASY_AI);
		else if (row.aiDifficulty == 'H')
			slot.setState(SLOT_BRUTAL_AI);
		else
			slot.setState(SLOT_MED_AI);
		break;
	case CasterLobby::LOBBY_SLOT_CLOSED:
		slot.setState(SLOT_CLOSED);
		break;
	default:
		slot.setState(SLOT_OPEN);
		break;
	}
	slot.setColor(row.color);
	slot.setPlayerTemplate(row.playerTemplate);
	slot.setTeamNumber(row.team);
	slot.setStartPos(row.startPos);
	slot.setMapAvailability(row.hasMap ? true : false);
	if (row.accepted)
		slot.setAccept();
}

// Draws the slot rows of one snapshot with the player room's own renderer.  Only called with at
// least one row present.
static void renderReadOnlySlots(const CasterLobby::LobbyViewModel& view)
{
	ReadOnlyGameInfo *game = readOnlyGameInfo();
	Int i;

	// Rebuild the lists UpdateSlotList() picks its selections from, the way InitLanGameGadgets()
	// does for the player room.  They are filled from the still empty game so that every color
	// remains on offer: these combo boxes stay disabled for good, so UpdateSlotList() never
	// repopulates them itself and a selection has to resolve against this list.
	game->reset();
	game->setLocalIP(READ_ONLY_UNREACHABLE_IP);
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Open"), white);
		GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Closed"), white);
		GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:EasyAI"), white);
		GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:MediumAI"), white);
		GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:HardAI"), white);
		GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		PopulateColorComboBox(i, comboBoxColor, game);
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, game, TRUE);
		PopulateTeamComboBox(i, comboBoxTeam, game);
	}

	game->setMap(resolveReadOnlyMapPath(view.mapName));
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot slot;
		if ((UnsignedInt)i < view.rowCount)
			fillReadOnlySlot(view.rows[i], slot);
		game->setSlot(i, slot);
	}

	UpdateSlotList(game, comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate, comboBoxTeam,
		buttonAccept, buttonStart, buttonMapStartPosition);

	// UpdateSlotList() leaves slot 0's accept light alone, because the host is always accepted.
	// Unhide it here, as clearReadOnlyRow() may have hidden it while we had no snapshot yet.
	if (view.rows[0].kind == CasterLobby::LOBBY_SLOT_HUMAN && buttonAccept[0] != nullptr)
		buttonAccept[0]->winHide(FALSE);
}

static void positionReadOnlyMap(const char *mapName)
{
	GameWindow *hiddenStartPositions[CasterLobby::LOBBY_MAX_SLOTS] = {0};
	if (windowMap != nullptr)
		positionStartSpots(resolveReadOnlyMapPath(mapName), hiddenStartPositions, windowMap);
}

static void setReadOnlyCombo(GameWindow *combo, const UnicodeString& text, Color color)
{
	if (combo == nullptr)
		return;
	GadgetComboBoxReset(combo);
	GadgetComboBoxAddEntry(combo, text, color);
	GadgetComboBoxSetSelectedPos(combo, 0, TRUE);
}

static void clearReadOnlyRow(Int index)
{
	if (comboBoxPlayer[index] != nullptr)
		GadgetComboBoxReset(comboBoxPlayer[index]);
	if (comboBoxColor[index] != nullptr)
		GadgetComboBoxReset(comboBoxColor[index]);
	if (comboBoxPlayerTemplate[index] != nullptr)
		GadgetComboBoxReset(comboBoxPlayerTemplate[index]);
	if (comboBoxTeam[index] != nullptr)
		GadgetComboBoxReset(comboBoxTeam[index]);
	if (buttonAccept[index] != nullptr)
		buttonAccept[index]->winHide(TRUE);
}

static UnicodeString readOnlyMapLabel(const char *mapName)
{
	AsciiString path(mapName);
	AsciiString lookup = resolveReadOnlyMapPath(mapName);
	const MapMetaData *mapData;
	UnicodeString label;

	lookup.toLower();
	mapData = TheMapCache->findMap(lookup);
	if (mapData != nullptr && !mapData->m_displayName.isEmpty())
		return mapData->m_displayName;
	if (path.reverseFind('/') != nullptr)
		path.set(path.reverseFind('/') + 1);
	if (path.reverseFind('\\') != nullptr)
		path.set(path.reverseFind('\\') + 1);
	label.translate(path.str());
	return label;
}

// Warns once per map when the watched game's map is not in the local map cache.
static void warnReadOnlyMissingMap(const char *mapName)
{
	AsciiString lookup = resolveReadOnlyMapPath(mapName);
	if (lookup.isEmpty() || listboxChatWindowLanGame == nullptr)
		return;
	if (TheMapCache->findMap(lookup) != nullptr)
	{
		s_readOnlyWarnedMap.clear();
		return;
	}
	if (lookup.compareNoCase(s_readOnlyWarnedMap) == 0)
		return;
	s_readOnlyWarnedMap = lookup;

	UnicodeString mapLabel = readOnlyMapLabel(mapName);
	UnicodeString text;
	text.format(TheGameText->fetch("GUI:LocalPlayerNoMap"), mapLabel.str());
	GadgetListBoxAddEntryText(listboxChatWindowLanGame, text, chatSystemColor, -1, 0);
}

static void setReadOnlyMapLabel(UnicodeString text)
{
	Int width, height;
	textEntryMapDisplay->winGetSize(&width, &height);
	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	measure->setFont(textEntryMapDisplay->winGetFont());
	measure->setText(text);
	if (measure->getWidth() > width - 4)
	{
		UnicodeString shortened;
		do
		{
			text.removeLastChar();
			shortened = text;
			shortened.concat(L"...");
			measure->setText(shortened);
		} while (!text.isEmpty() && measure->getWidth() > width - 4);
		text = shortened;
	}
	TheDisplayStringManager->freeDisplayString(measure);
	GadgetStaticTextSetText(textEntryMapDisplay, text);
}

static Bool readOnlyRosterHasName(const CasterLobby::LobbyViewModel& view, const char *name)
{
	for (UnsignedInt i = 0; i < view.rowCount && i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
	{
		if (view.rows[i].kind == CasterLobby::LOBBY_SLOT_HUMAN
			&& strcmp(view.rows[i].name, name) == 0)
			return TRUE;
	}
	return FALSE;
}

// Posts the leave line a player in the room would see, by diffing the human names of
// two consecutive snapshots. Joins are silent, as in the normal room.
static void announceReadOnlyRoster(const CasterLobby::LobbyViewModel& previous,
	const CasterLobby::LobbyViewModel& current)
{
	UnicodeString name;
	UnicodeString text;
	UnsignedInt i;

	if (listboxChatWindowLanGame == nullptr)
		return;

	for (i = 0; i < previous.rowCount && i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
	{
		const CasterLobby::LobbyViewRow& row = previous.rows[i];
		if (row.kind != CasterLobby::LOBBY_SLOT_HUMAN || row.name[0] == '\0'
			|| readOnlyRosterHasName(current, row.name))
			continue;
		name.translate(row.name);
		text.format(TheGameText->fetch("Network:PlayerLeftGame"), name.str());
		GadgetListBoxAddEntryText(listboxChatWindowLanGame, text, chatSystemColor, -1, 0);
	}
}

static UnicodeString readOnlyStatusText(CasterLobby::LobbyStatus status)
{
	switch (status)
	{
	case CasterLobby::LOBBY_STATUS_OK:
		return UnicodeString::TheEmptyString;
	case CasterLobby::LOBBY_STATUS_NO_WATCH:
		return TheGameText->fetch("LAN:ErrorNoGameSelected");
	case CasterLobby::LOBBY_STATUS_NO_SOURCE:
		return TheGameText->fetch("LAN:HostNotResponding");
	case CasterLobby::LOBBY_STATUS_NO_SUBSCRIPTION:
	case CasterLobby::LOBBY_STATUS_SUBSCRIBE_PENDING:
		return TheGameText->fetch("LAN:HostNotResponding");
	case CasterLobby::LOBBY_STATUS_STALE:
		return TheGameText->fetch("LAN:HostNotResponding");
	case CasterLobby::LOBBY_STATUS_UNPARSED:
		return TheGameText->fetch("LAN:ErrorCRCMismatch");
	case CasterLobby::LOBBY_STATUS_LOCAL_IP_UNKNOWN:
		return TheGameText->fetch("LAN:ErrorUnknown");
	case CasterLobby::LOBBY_STATUS_NOT_PLAYER:
		return TheGameText->fetch("LAN:ErrorUnknown");
	case CasterLobby::LOBBY_STATUS_NO_GAME_ANNOUNCED:
		return TheGameText->fetch("LAN:ErrorUnknown");
	case CasterLobby::LOBBY_STATUS_NO_UID:
		return TheGameText->fetch("LAN:ErrorUnknown");
	default:
		return TheGameText->fetch("LAN:HostNotResponding");
	}
}

static void populateReadOnlyLobby()
{
	CasterLobby::LobbyViewModel view;
	UnicodeString text;
	UnsignedInt i;

	if (!s_readOnlyCasterMode || TheCaster == nullptr)
		return;

	const CasterLobby::LobbyState& state = TheCaster->lobbyState();
	CasterLobby::buildLobbyView(state, TheCaster->lobbyStatus(), view);
	if (listboxChatWindowLanGame != nullptr && view.status == CasterLobby::LOBBY_STATUS_OK
		&& CasterLobby::takeCountdown(state, s_readOnlyCountdownRevision))
	{
		text.format(TheGameText->fetch(state.countdownSeconds == 1
			? "LAN:GameStartTimerSingular" : "LAN:GameStartTimerPlural"), state.countdownSeconds);
		GadgetListBoxAddEntryText(listboxChatWindowLanGame, text, chatSystemColor, -1, 0);
	}

	if (s_readOnlyHaveLastView && CasterLobby::lobbyViewEquals(s_readOnlyLastView, view))
		return;
	if (listboxChatWindowLanGame != nullptr && view.status != CasterLobby::LOBBY_STATUS_NO_WATCH
		&& (!s_readOnlyHaveLastView || s_readOnlyLastView.status != view.status))
	{
		UnicodeString message = readOnlyStatusText(view.status);
		UnicodeString previousMessage = readOnlyStatusText(s_readOnlyLastView.status);
		if (!message.isEmpty()
			&& (!s_readOnlyHaveLastView || message.compare(previousMessage) != 0))
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, message,
				GameMakeColor(128, 128, 128, 255), -1, 0);
		}
	}
	if (s_readOnlyHaveLastView && s_readOnlyLastView.status == CasterLobby::LOBBY_STATUS_OK
		&& view.status == CasterLobby::LOBBY_STATUS_OK)
		announceReadOnlyRoster(s_readOnlyLastView, view);
	s_readOnlyLastView = view;
	s_readOnlyHaveLastView = TRUE;

	text.clear();
	if (view.hasSnapshot)
	{
		text = readOnlyMapLabel(view.mapName);
		positionReadOnlyMap(view.mapName);
		warnReadOnlyMissingMap(view.mapName);
	}
	else
		positionReadOnlyMap("");
	if (textEntryMapDisplay != nullptr)
		setReadOnlyMapLabel(text);

	if (checkboxLimitSuperweapons != nullptr)
		GadgetCheckBoxSetChecked(checkboxLimitSuperweapons, view.superweaponRestriction != 0);
	if (comboBoxStartingCash != nullptr)
	{
		if (view.hasSnapshot)
		{
			text.format(TheGameText->fetch("GUI:StartingMoneyFormat"), (Int)view.startingCash);
			setReadOnlyCombo(comboBoxStartingCash, text,
				comboBoxStartingCash->winGetEnabledTextColor());
		}
		else
			GadgetComboBoxReset(comboBoxStartingCash);
	}

	if (view.rowCount > 0)
		renderReadOnlySlots(view);
	for (i = view.rowCount; i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
		clearReadOnlyRow((Int)i);
}

static void readOnlySendChat()
{
	UnicodeString input;
	if (TheCaster == nullptr || !TheCaster->isCaster() || TheLAN == nullptr)
		return;

	if (takeChatEntryInput(input))
	{
		// The read-only room renders its own feed, so it takes no local echo.
		SendCasterLobbyChatLine(input, TheLAN->GetMyName(), FALSE);
	}
}

static void updateReadOnlyLobby()
{
	CasterLobby::LineQueue::Line line;
	const CasterLobby::LobbyState *state;

	if (!s_readOnlyCasterMode || TheCaster == nullptr)
		return;

	while (TheCaster->popLobbyLine(line))
	{
		ChatMessage chat;
		UnicodeString rendered;
		Color chatColor = chatSystemColor;
		if (!MakeCasterChatMessage(chat, line))
			continue;
		if (!line.senderIsCaster)
		{
			state = &TheCaster->lobbyState();
			if (line.senderSlot < state->slotCount)
				chat.hasColor = ChatColorFromIndex(state->slots[line.senderSlot].color, chat.color);
		}
		RenderChatMessage(chat, chatSystemColor, rendered, chatColor);
		GadgetListBoxAddEntryText(listboxChatWindowLanGame, rendered, chatColor, -1, 0);
	}
	populateReadOnlyLobby();
}

static void resetReadOnlyState()
{
	SetReadOnlyCasterChatWindow(nullptr);
	s_readOnlyLayout = nullptr;
	s_readOnlyShellOwned = FALSE;
	s_readOnlyShuttingDown = FALSE;
	s_readOnlyHaveLastView = FALSE;
	s_readOnlyCasterMode = FALSE;
	EnableSlotListUpdates(FALSE);
	if (s_readOnlyGame != nullptr)
	{
		delete s_readOnlyGame;
		s_readOnlyGame = nullptr;
	}
	DeinitLanGameGadgets();
}

Bool IsReadOnlyLanGameOptionsOpen()
{
	return s_readOnlyCasterMode && s_readOnlyLayout != nullptr && !s_readOnlyShuttingDown;
}

void PostReadOnlyLanGameOptionsLine(const WideChar *text)
{
	if (!IsReadOnlyLanGameOptionsOpen() || listboxChatWindowLanGame == nullptr || text == nullptr)
		return;
	GadgetListBoxAddEntryText(listboxChatWindowLanGame, UnicodeString(text), chatSystemColor, -1, 0);
}

void CloseReadOnlyLanGameOptions()
{
	if (!IsReadOnlyLanGameOptionsOpen())
		return;
	TheShell->popReplacement();
	GameWindow *lobbyChat = TheWindowManager->winGetWindowFromId(nullptr,
		TheNameKeyGenerator->nameToKey("LanLobbyMenu.wnd:TextEntryChat"));
	if (lobbyChat != nullptr)
		TheWindowManager->winSetFocus(lobbyChat);
}

Bool OpenReadOnlyLanGameOptions(char *failure, UnsignedInt failureCapacity)
{
	static const char *layoutPaths[] =
	{
		"Menus/LanGameOptionsMenu.wnd",
		"LanGameOptionsMenu.wnd",
	};
	CasterLobby::ReadOnlyOpenStage failureStage = CasterLobby::READONLY_OPEN_LAYOUT_FAILED;
	const char *failedGadget = nullptr;
	Int row;	// hoisted: goto below must not skip a for-scoped initializer

	if (IsReadOnlyLanGameOptionsOpen())
		return TRUE;
	for (Int i = 0; i < 2 && s_readOnlyLayout == nullptr; ++i)
		s_readOnlyLayout = TheWindowManager->winCreateLayout(layoutPaths[i]);
	if (s_readOnlyLayout == nullptr)
		goto failure;

	s_readOnlyCasterMode = TRUE;
	BindLanGameGadgets(s_readOnlyLayout);
	if (parentLanGameOptions == nullptr)
	{
		failureStage = CasterLobby::READONLY_OPEN_PARENT_MISSING;
		goto failure;
	}
	if (textEntryMapDisplay == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:TextEntryMapDisplay";
	else if (windowMap == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:MapWindow";
	else if (listboxChatWindowLanGame == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame";
	else if (textEntryChat == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:TextEntryChat";
	else if (buttonBack == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:ButtonBack";
	else if (buttonStart == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:ButtonStart";
	else if (buttonSelectMap == nullptr)
		failedGadget = "LanGameOptionsMenu.wnd:ButtonSelectMap";
	// initReadOnlyLanGameOptions touches every slot row unconditionally.
	for (row = 0; row < CasterLobby::LOBBY_MAX_SLOTS && failedGadget == nullptr; ++row)
	{
		if (comboBoxPlayer[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ComboBoxPlayer";
		else if (comboBoxColor[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ComboBoxColor";
		else if (comboBoxPlayerTemplate[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate";
		else if (comboBoxTeam[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ComboBoxTeam";
		else if (buttonAccept[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ButtonAccept";
		else if (buttonMapStartPosition[row] == nullptr)
			failedGadget = "LanGameOptionsMenu.wnd:ButtonMapStartPosition";
	}
	if (failedGadget != nullptr)
	{
		failureStage = CasterLobby::READONLY_OPEN_GADGET_MISSING;
		goto failure;
	}

	TheMapCache->updateCache();
	if (!TheShell->pushReplacement(s_readOnlyLayout, LanGameOptionsMenuInit,
		LanGameOptionsMenuUpdate, LanGameOptionsMenuShutdown))
		goto failure;
	s_readOnlyShellOwned = TRUE;
	return TRUE;

failure:
	if (failure != nullptr && failureCapacity != 0)
		CasterLobby::formatReadOnlyOpenFailure(failureStage, failedGadget,
			failure, failureCapacity);
	WindowLayout *failedLayout = s_readOnlyLayout;
	Bool destroyFailedLayout = failedLayout != nullptr && !s_readOnlyShellOwned;
	resetReadOnlyState();
	if (destroyFailedLayout)
	{
		failedLayout->destroyWindows();
		deleteInstance(failedLayout);
	}
	return FALSE;
}

static void initReadOnlyLanGameOptions(WindowLayout *layout)
{
	if (TheCaster == nullptr || !TheCaster->isCaster()
		|| !TheCaster->hasSelectedGame() || TheCaster->playbackEntered())
	{
		// The watch is over (for example the match ended and we are returning from
		// the score screen), so there is no room left to show. Go back to the lobby.
		DEBUG_LOG(("Popping to lobby after a cast game!"));
		TheShell->popImmediate();
		return;
	}

	BindLanGameGadgets(layout);
	SetReadOnlyCasterChatWindow(listboxChatWindowLanGame);
	GadgetListBoxReset(listboxChatWindowLanGame);
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	s_readOnlyCountdownRevision = 0;
	s_readOnlyHaveLastView = FALSE;
	s_readOnlyWarnedMap.clear();

	buttonStart->winEnable(FALSE);
	buttonSelectMap->winEnable(FALSE);
	if (checkboxLimitSuperweapons != nullptr)
		checkboxLimitSuperweapons->winEnable(FALSE);
	if (comboBoxStartingCash != nullptr)
		comboBoxStartingCash->winEnable(FALSE);
	for (Int i = 0; i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
	{
		comboBoxPlayer[i]->winEnable(FALSE);
		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
		// The accept lights are indicators, not controls: both rooms render them by toggling the
		// enabled state, so UpdateSlotList() owns it here too.  Blocking their input is what keeps
		// them inert, and that holds whatever the enabled state says.
		buttonAccept[i]->winSetInputFunc(GameWinBlockInput);
		buttonMapStartPosition[i]->winHide(TRUE);
	}
	GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor);

	HideGameInfoWindow(TRUE);
	layout->hide(FALSE);
	layout->bringForward();
	TheWindowManager->winSetFocus(parentLanGameOptions);
	EnableSlotListUpdates(TRUE);
	populateReadOnlyLobby();
	TheTransitionHandler->setGroup("LanGameOptionsFade");
}

static void completeReadOnlyShutdown(WindowLayout *layout)
{
	resetReadOnlyState();
	layout->hide(TRUE);
	TheShell->shutdownComplete(layout);
}

static void shutdownReadOnlyLanGameOptions(WindowLayout *layout, Bool popImmediate)
{
	Bool matchEntered = TheCaster != nullptr && TheCaster->playbackEntered();
	if (TheCaster != nullptr && TheCaster->isCaster() && !matchEntered)
	{
		TheCaster->finishWatch(WATCH_EXIT_USER_BACKED_OUT);
		CasterEnable(CASTER_ROLE_PLAYER);
	}
	// Skip the animation for an immediate pop and when the match itself is starting.
	if (popImmediate || matchEntered)
	{
		completeReadOnlyShutdown(layout);
		return;
	}
	s_readOnlyShuttingDown = TRUE;
	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
}

static void updateReadOnlyLanGameOptions(WindowLayout *layout)
{
	if (s_readOnlyShuttingDown)
	{
		if (TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
			completeReadOnlyShutdown(layout);
		return;
	}
	if (TheCaster != nullptr && TheCaster->hasSelectedGame())
		updateReadOnlyLobby();
}

static WindowMsgHandledType readOnlyLanGameOptionsSystem(UnsignedInt msg,
	WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_INPUT_FOCUS:
		if (mData1 == TRUE)
			*(Bool *)mData2 = TRUE;
		return MSG_HANDLED;
	case GBM_SELECTED:
		if ((GameWindow *)mData1 == buttonBack)
		{
			CloseReadOnlyLanGameOptions();
			return MSG_HANDLED;
		}
		if ((GameWindow *)mData1 == buttonChat)
		{
			readOnlySendChat();
			return MSG_HANDLED;
		}
		return MSG_HANDLED;
	case GEM_EDIT_DONE:
		if ((GameWindow *)mData1 == textEntryChat)
		{
			readOnlySendChat();
			return MSG_HANDLED;
		}
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
/** Initialize the normal LAN player room */
//-------------------------------------------------------------------------------------------------
static void playerRoomInit( WindowLayout *layout )
{
	if (TheLAN->GetMyGame() && TheLAN->GetMyGame()->isGameInProgress())
	{
		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("Popping to lobby after a game!"));
		TheShell->popImmediate();
		return;
	}
	s_isIniting = TRUE;

	LANbuttonPushed = false;
	LANisShuttingDown = false;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitLanGameGadgets(layout);
	EnableSlotListUpdates(TRUE);
	Int start = 0;

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxChatWindowLanGame );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();
	if (TheLAN->AmIHost())
	{
		// read in some prefs
		LANGameInfo *game = TheLAN->GetMyGame();
		LANGameSlot *slot = game->getLANSlot(0);
		LANPreferences pref;
		slot->setColor( pref.getPreferredColor() );
		slot->setPlayerTemplate( pref.getPreferredFaction() );
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap( pref.getPreferredMap() );
		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			TheLAN->GetMyGame()->getSlot(0)->setMapAvailability(true);
			TheLAN->GetMyGame()->setMapCRC( it->second.m_CRC );
			TheLAN->GetMyGame()->setMapSize( it->second.m_filesize );

			TheLAN->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the selected map.
		}

		//GadgetTextEntrySetText(comboBoxPlayer[0], TheLAN->GetMyName());
		lanUpdateSlotList();
		updateGameOptions();
		start = 1; // leave my combo boxes usable

		// TheSuperHackers @tweak disable the combo box for the host's player name
		comboBoxPlayer[0]->winEnable(FALSE);
	}
	else
	{

		//DEBUG_LOG(("LanGameOptionsMenuInit(): map is %s", TheLAN->GetMyGame()->getMap().str()));
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonSelectMap->winEnable( FALSE );
		TheLAN->GetMyGame()->setMapCRC( TheLAN->GetMyGame()->getMapCRC() );		// force a recheck
		TheLAN->GetMyGame()->setMapSize( TheLAN->GetMyGame()->getMapSize() ); // of if we have the map
		TheLAN->RequestHasMap();
		lanUpdateSlotList();
		updateGameOptions();
	}
	for (Int i = start; i < MAX_SLOTS; ++i)
	{
		//I'm a client, disable the controls I can't touch.
		if (!TheLAN->AmIHost())
			comboBoxPlayer[i]->winEnable(FALSE);

		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
//		buttonStartPosition[i]->winEnable(FALSE);
	}

//	for (i = 0; i < MAX_SLOTS; ++i)
//	{
//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not picking start spots this way any more
//	}
//
	// Show the Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentLanGameOptions );

	s_isIniting = FALSE;

	if (TheLAN->AmIHost())
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(),true);
		TheLAN->RequestGameAnnounce();
	}
	lanUpdateSlotList();
	LanPositionStartSpots();
	TheTransitionHandler->setGroup("LanGameOptionsFade");

	// animate controls
	//TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (s_readOnlyCasterMode)
		initReadOnlyLanGameOptions(layout);
	else
		playerRoomInit(layout);
}

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateGameOptions()
{
	LANGameInfo *theGame = TheLAN->GetMyGame();
	UnicodeString mapDisplayName;
	if (theGame && AreSlotListUpdatesEnabled())
	{
		const GameSlot *localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());
		const MapMetaData *mapData = TheMapCache->findMap( TheLAN->GetMyGame()->getMap() );
		if (mapData && localSlot && localSlot->hasMap())
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		}
		else
		{
			AsciiString s = TheLAN->GetMyGame()->getMap();
			if (s.reverseFind('\\'))
			{
				s = s.reverseFind('\\') + 1;
			}
			mapDisplayName.format(L"%hs", s.str());
		}
		UnicodeString old = GadgetStaticTextGetText(textEntryMapDisplay);
		if(old.compare(mapDisplayName) != 0)
			LanPositionStartSpots();
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplayName);
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void setLANPlayerTooltip(LANPlayer* player)
{
	UnicodeString tooltip;

	if (!player->getLogin().isEmpty() || !player->getHost().isEmpty())
	{
		tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getLogin().str(), player->getHost().str());
	}

#if defined(RTS_DEBUG)
	UnicodeString ip;
	ip.format(L" - %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(player->getIP()));
	tooltip.concat(ip);
#endif

	if (!tooltip.isEmpty())
	{
		TheMouse->setCursorTooltip( tooltip );
	}
}


//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{
	DeinitLanGameGadgets();
	textEntryMapDisplay = nullptr;
	LANisShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != nullptr) );

	if (LANnextScreen != nullptr)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Normal LAN player room shutdown */
//-------------------------------------------------------------------------------------------------
static void playerRoomShutdown( WindowLayout *layout, Bool popImmediate )
{
	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,nullptr,nullptr);
	EnableSlotListUpdates(FALSE);
	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
	if (TheLAN)
		TheLAN->ResetGameStartTimer();

	/*
	// hide menu
	layout->hide( TRUE );

	// Reset the LAN singleton
//	TheLAN->reset();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
	*/
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	if (s_readOnlyCasterMode)
		shutdownReadOnlyLanGameOptions(layout, *(Bool *)userData);
	else
		playerRoomShutdown(layout, *(Bool *)userData);
}

//-------------------------------------------------------------------------------------------------
/** Normal LAN player room update */
//-------------------------------------------------------------------------------------------------
static void playerRoomUpdate( WindowLayout *layout )
{
	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
	//TheLAN->update(); // this is handled in the lobby
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if (s_readOnlyCasterMode)
		updateReadOnlyLanGameOptions(layout);
	else
		playerRoomUpdate(layout);
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (LANbuttonPushed)
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
	return MSG_IGNORED;
}


//-------------------------------------------------------------------------------------------------
/** Normal LAN player room window system callback */
//-------------------------------------------------------------------------------------------------
static WindowMsgHandledType playerRoomSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				if (windowMap)
					windowMap->winSetUserData(nullptr);

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

				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				LANGameInfo *myGame = TheLAN->GetMyGame();
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
					else if( controlID == comboBoxPlayerID[i] && myGame->amIHost() )
					{
						// We don't have anything that'll happen if we click on ourselves
						if(i == myGame->getLocalSlotNum())
						 break;
						// Get
						Int pos = -1;
						GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						if( pos != SLOT_PLAYER && pos >= 0)
						{
							if( myGame->getLANSlot(i)->getState() == SLOT_PLAYER )
							{
								UnicodeString name = myGame->getPlayerName(i);
								myGame->getLANSlot(i)->setState(SlotState(pos));
								myGame->resetAccepted();
								TheLAN->OnPlayerLeave(name);
							}
							else if( myGame->getLANSlot(i)->getState() != pos )
							{
								Bool wasAI = (myGame->getLANSlot(i)->isAI());
								myGame->getLANSlot(i)->setState(SlotState(pos));
								Bool isAI = (myGame->getLANSlot(i)->isAI());
								if (wasAI || isAI)
									myGame->resetAccepted();
								if (wasAI ^ isAI)
									PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI);
								if (!s_isIniting)
								{
									TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
									lanUpdateSlotList();
								}
							}
						}
						break;
					}
				}
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					if( mapSelectLayout )
						{
							mapSelectLayout->destroyWindows();
							deleteInstance(mapSelectLayout);
							mapSelectLayout = nullptr;
						}
					TheLAN->RequestGameLeave();
					//TheShell->pop();

				}
				else if ( controlID == buttonChatID )
				{
					playerRoomSendChat();
				}
				else if ( controlID == buttonSelectMapID )
				{
					//buttonBack->winEnable( false );

					mapSelectLayout = TheWindowManager->winCreateLayout( "Menus/LanMapSelectMenu.wnd" );
					mapSelectLayout->runInit();
					mapSelectLayout->hide( FALSE );
					mapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					if (TheLAN->AmIHost())
					{
						StartPressed();
						//TheLAN->RequestGameStart();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						TheLAN->RequestAccept();

						// Disable the accept button
						EnableAcceptControls(TRUE, TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate,
							comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

					}
				}
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							LANGameInfo *game = TheLAN->GetMyGame();
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								LANGameSlot *slot = game->getLANSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
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
			if (LANbuttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					LANGameInfo *game = TheLAN->GetMyGame();
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						LANGameSlot *slot = game->getLANSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
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
		case GEM_EDIT_DONE:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{
					playerRoomSendChat();
				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	if (s_readOnlyCasterMode)
		return readOnlyLanGameOptionsSystem(msg, mData1, mData2);
	return playerRoomSystem(window, msg, mData1, mData2);
}

//-------------------------------------------------------------------------------------------------
/** Utility FUnction used as a bridge from other windows to this one */
//-------------------------------------------------------------------------------------------------
void PostToLanGameOptions( PostToLanGameType post )
{
	if (post >= POST_TO_LAN_GAME_TYPE_COUNT)
		return;
	LanPositionStartSpots();
	switch (post)
	{
			//-------------------------------------------------------------------------------------------------
		case SEND_GAME_OPTS:
		{
			LANGameInfo *game = TheLAN->GetMyGame();
			game->resetAccepted();
			updateGameOptions();
			lanUpdateSlotList();

			//buttonBack->winEnable( true );

			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				game->getSlot(i)->setStartPos(-1);
			}

			TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
			break;
		}
		case MAP_BACK:
		{
				//buttonBack->winEnable( true );
		}
		default:
			return;
	}
}
