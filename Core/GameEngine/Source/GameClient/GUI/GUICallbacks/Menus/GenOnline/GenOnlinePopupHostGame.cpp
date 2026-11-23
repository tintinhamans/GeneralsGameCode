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

//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
//	created:	Jul 2002
//
//	Filename: 	PopupHostGame.cpp
//
//	author:		Chris Huybregts
//
//	purpose:	Contains the Callbacks for the Host Game Popus
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineMenus.h"

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/version.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpyOverlay.h"

#include "GameNetwork/GameSpy/LadderDefs.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/LadderPreferences.h"

// GENERALS ONLINE:
#include "GameNetwork/GenOnline/GenOnlineDefines.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineAnticheatKind.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindow.h"
#include "Common/QuotedPrintable.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

static NameKeyType parentPopupID = NAMEKEY_INVALID;
static NameKeyType textEntryGameNameID = NAMEKEY_INVALID;
static NameKeyType buttonCreateGameID = NAMEKEY_INVALID;
static NameKeyType checkBoxAllowObserversID = NAMEKEY_INVALID;
static NameKeyType textEntryGameDescriptionID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;
static NameKeyType textEntryLadderPasswordID = NAMEKEY_INVALID;
static NameKeyType comboBoxLadderNameID = NAMEKEY_INVALID;
static NameKeyType textEntryGamePasswordID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitArmiesID = NAMEKEY_INVALID;
static NameKeyType checkBoxUseStatsID = NAMEKEY_INVALID;
static NameKeyType checkBoxRequireEacID = NAMEKEY_INVALID;

static GameWindow *parentPopup = nullptr;
static GameWindow *textEntryGameName = nullptr;
static GameWindow *buttonCreateGame = nullptr;
static GameWindow *checkBoxAllowObservers = nullptr;
static GameWindow *textEntryGameDescription = nullptr;
static GameWindow *buttonCancel = nullptr;
static GameWindow *comboBoxLadderName = nullptr;
static GameWindow *textEntryLadderPassword = nullptr;
static GameWindow *textEntryGamePassword = nullptr;
static GameWindow *checkBoxLimitArmies = nullptr;
static GameWindow *checkBoxUseStats = nullptr;
static GameWindow *checkBoxRequireEac = nullptr;


void GenOnlineCreateGame();

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

// Ladders --------------------------------------------------------------------------------

static bool isPopulatingLadderBox = false;

void GenOnlinePopulateCustomLadderComboBox()
{
	if (!parentPopup || !comboBoxLadderName)
		return;

	isPopulatingLadderBox = true;

	CustomMatchPreferences pref;
	AsciiString userPrefFilename;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	int64_t localProfile = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();
	userPrefFilename.format("GeneralsOnline\\CustomPref%lld.ini", localProfile);
	pref.load(userPrefFilename);

	std::set<const LadderInfo *> usedLadders;

	Color specialColor = GameSpyColor[GSCOLOR_MAP_SELECTED];
	Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Int index;
	GadgetComboBoxReset( comboBoxLadderName );
	index = GadgetComboBoxAddEntry( comboBoxLadderName, TheGameText->fetch("GUI:NoLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadderName, index, nullptr );

	Int selectedPos = 0;
	AsciiString lastLadderAddr = pref.getLastLadderAddr();
	UnsignedShort lastLadderPort = pref.getLastLadderPort();
	const LadderInfo *info = TheLadderList->findLadder( lastLadderAddr, lastLadderPort );
	if (info && info->validCustom)
	{
		usedLadders.insert(info);
		index = GadgetComboBoxAddEntry( comboBoxLadderName, info->name, specialColor );
		GadgetComboBoxSetItemData( comboBoxLadderName, index, (void *)(info->index) );
		selectedPos = index;
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
		if (info && info->validCustom && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetComboBoxAddEntry( comboBoxLadderName, info->name, normalColor );
			GadgetComboBoxSetItemData( comboBoxLadderName, index, (void *)(info->index) );
		}
	}

	index = GadgetComboBoxAddEntry( comboBoxLadderName, TheGameText->fetch("GUI:ChooseLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadderName, index, (void *)-1 );

	GadgetComboBoxSetSelectedPos( comboBoxLadderName, selectedPos );
	isPopulatingLadderBox = false;
}

// Window Functions -----------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenOnlinePopupHostGameInit( WindowLayout *layout, void *userData )
{
	parentPopupID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:ParentHostPopUp");
	parentPopup = TheWindowManager->winGetWindowFromId(nullptr, parentPopupID);

	textEntryGameNameID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:TextEntryGameName");
	textEntryGameName = TheWindowManager->winGetWindowFromId(parentPopup, textEntryGameNameID);
	UnicodeString name;

	{
		CustomMatchPreferences pref;
		name = pref.getLastLobbyName();
		if (name.isEmpty())
		{
			name.translate("Generals Online Lobby");
		}
	}

	GadgetTextEntrySetText(textEntryGameName, name);

	textEntryGameDescriptionID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:TextEntryGameDescription");
	textEntryGameDescription = TheWindowManager->winGetWindowFromId(parentPopup, textEntryGameDescriptionID);
	GadgetTextEntrySetText(textEntryGameDescription, UnicodeString::TheEmptyString);

	textEntryLadderPasswordID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:TextEntryLadderPassword");
	textEntryLadderPassword = TheWindowManager->winGetWindowFromId(parentPopup, textEntryLadderPasswordID);
	GadgetTextEntrySetText(textEntryLadderPassword, UnicodeString::TheEmptyString);

	textEntryGamePasswordID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:TextEntryGamePassword");
	textEntryGamePassword = TheWindowManager->winGetWindowFromId(parentPopup, textEntryGamePasswordID);
	GadgetTextEntrySetText(textEntryGamePassword, UnicodeString::TheEmptyString);

	buttonCreateGameID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:ButtonCreateGame");
	buttonCreateGame = TheWindowManager->winGetWindowFromId(parentPopup, buttonCreateGameID);

	buttonCancelID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:ButtonCancel");
	buttonCancel = TheWindowManager->winGetWindowFromId(parentPopup, buttonCancelID);

	checkBoxAllowObserversID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:CheckBoxAllowObservers");
	checkBoxAllowObservers = TheWindowManager->winGetWindowFromId(parentPopup, checkBoxAllowObserversID);
	CustomMatchPreferences customPref;
	GadgetCheckBoxSetChecked(checkBoxAllowObservers, customPref.allowsObservers());

	comboBoxLadderNameID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:ComboBoxLadderName");
	comboBoxLadderName = TheWindowManager->winGetWindowFromId(parentPopup, comboBoxLadderNameID);
	if (comboBoxLadderName)
		GadgetComboBoxReset(comboBoxLadderName);
	GenOnlinePopulateCustomLadderComboBox();

  checkBoxUseStatsID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:CheckBoxUseStats");
  checkBoxUseStats = TheWindowManager->winGetWindowFromId(parentPopup, checkBoxUseStatsID);
	Bool usingStats = customPref.getUseStats();
  GadgetCheckBoxSetChecked( checkBoxUseStats, usingStats );
  // The Zero Hour string file has the use stats texts, the Generals one does not, so they fall back to English.
  GadgetCheckBoxSetText( checkBoxUseStats, TheGameText->FETCH_OR_SUBSTITUTE("GUI:UseStats", L"Record Stats") );
  checkBoxUseStats->winSetTooltip( TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:UseStats", L"Select whether the statistics of this game will be recorded") );

  checkBoxLimitArmiesID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:CheckBoxLimitArmies");
  checkBoxLimitArmies = TheWindowManager->winGetWindowFromId(parentPopup, checkBoxLimitArmiesID);
	
#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
  // limit armies is disallowed in "use stats" games
	checkBoxLimitArmies->winEnable(! usingStats );
  GadgetCheckBoxSetChecked( checkBoxLimitArmies, usingStats? FALSE : customPref.getFactionsLimited() );
#else
  GadgetCheckBoxSetChecked( checkBoxLimitArmies, customPref.getFactionsLimited() );
#endif

#if !RTS_ZEROHOUR
	// Generals has no armies to limit, so the box stays hidden and unchecked.
	GadgetCheckBoxSetChecked( checkBoxLimitArmies, FALSE );
	checkBoxLimitArmies->winHide( TRUE );
#endif

	// GenOnline @feature arcticdolphin 22/11/2026 Requiring Easy Anti-Cheat needs the host to run it.
	checkBoxRequireEacID = TheNameKeyGenerator->nameToKey("PopupHostGame.wnd:CheckBoxRequireEac");
	checkBoxRequireEac = TheWindowManager->winGetWindowFromId(parentPopup, checkBoxRequireEacID);
	const Bool hasEac = LocalAnticheatKind() == EAnticheatKind::Eac;
	GadgetCheckBoxSetChecked( checkBoxRequireEac, FALSE );
	GadgetCheckBoxSetText( checkBoxRequireEac, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GORequireEac", L"Require Easy Anti-Cheat") );
	checkBoxRequireEac->winEnable( hasEac );
	checkBoxRequireEac->winSetTooltip( hasEac
		? TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:GORequireEac", L"Only players running Easy Anti-Cheat can join this lobby")
		: TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:GORequireEacOff", L"Easy Anti-Cheat must be running to require it") );

	TheWindowManager->winSetFocus(textEntryGameName);
	TheWindowManager->winSetModal( parentPopup );

	// GO Menus/PopupHostGame.wnd places the observers box. Retail ships it hidden
	checkBoxAllowObservers->winHide(false);

	// hide password for streams
	EntryData* e = (EntryData*)textEntryGamePassword->winGetUserData();
	e->secretText = true;
	e->maxTextLen = GENONLINE_LOBBY_MAX_PASSWORD_LENGTH;

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenOnlinePopupHostGameUpdate( WindowLayout * layout, void *userData)
{
#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	if (GadgetCheckBoxIsChecked( checkBoxUseStats ))
	{
		checkBoxLimitArmies->winEnable( FALSE );
		GadgetCheckBoxSetChecked( checkBoxLimitArmies, FALSE );
	}
	else
	{
		checkBoxLimitArmies->winEnable( TRUE );
	}
#endif
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlinePopupHostGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
//			if (buttonPushed)
//				break;

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
																							(WindowMsgData)buttonCancel, buttonCancelID );

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
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlinePopupHostGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
  switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			break;

		}
    //---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			parentPopup = nullptr;

			break;

		}

    //----------------------------------------------------------------------------------------------
    case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}

    //----------------------------------------------------------------------------------------------
		case GEM_UPDATE_TEXT:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryGameNameID )
				{
					UnicodeString txtInput;

					// grab the game's name
					txtInput.set(GadgetTextEntryGetText( textEntryGameName ));

					// Clean up the text (remove leading/trailing chars, etc)
					const WideChar *c = txtInput.str();
					while (c && (iswspace(*c)))
						c++;

					if (c)
						txtInput = UnicodeString(c);
					else
						txtInput = UnicodeString::TheEmptyString;

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryGameName, txtInput );

				}
				break;
			}
    //---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				Int pos = -1;
				GadgetComboBoxGetSelectedPos(control, &pos);

				if (controlID == comboBoxLadderNameID && !isPopulatingLadderBox)
				{
					if (pos >= 0)
					{
						Int ladderID = (Int)(intptr_t)GadgetComboBoxGetItemData(control, pos);
						if (ladderID < 0)
						{
							// "Choose a ladder" selected - open overlay
							GenOnlinePopulateCustomLadderComboBox(); // this restores the non-"Choose a ladder" selection
							GameSpyOpenOverlay( GSOVERLAY_LADDERSELECT );
						}
					}
				}
				break;
			}

    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == buttonCancelID )
			{
				parentPopup = nullptr;
				GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
				GenOnlineSetLobbyAttemptHostJoin( FALSE );
			}
			else if( controlID == buttonCreateGameID)
			{
				UnicodeString name;
				name = GadgetTextEntryGetText(textEntryGameName);
				name.trim();
				if(name.isEmpty())
				{
					GenOnlineSetLobbyAttemptHostJoin(FALSE);
					GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
					GSMessageBoxOk(TheGameText->fetch("GUI:Error"), UnicodeString(L"Please enter a lobby name."), nullptr);
					break;
				}
				// save last used lobby name to CustomPref.ini
				{
					CustomMatchPreferences pref;
					pref.setLastLobbyName(name);
					pref.write();
				}
				GenOnlineCreateGame();
				parentPopup = nullptr;
				GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
			}
			break;
		}
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

extern GlobalData* TheWritableGlobalData;
void GenOnlineCreateGame()
{
	// TODO_GENONLINE: exe and ini crc, verison etc
	// TODO_GENONLINE: passworded lobbies

	// TODO_GENONLINE: Support 'favorite map' again
	AsciiString defaultMap = getDefaultMap(true);
    CustomMatchPreferences pref;
	AsciiString storedMap = pref.getAsciiString("Map", AsciiString::TheEmptyString);
	if (!storedMap.isEmpty())
	{
		AsciiString decoded = QuotedPrintableToAsciiString(storedMap);
		decoded.trim();
		if (!decoded.isEmpty() && isValidMap(decoded, TRUE))
		{
			defaultMap = decoded;
		}
	}
	const MapMetaData* md = TheMapCache->findMap(defaultMap);

	Bool limitArmies = GadgetCheckBoxIsChecked(checkBoxLimitArmies);
	Bool useStats = GadgetCheckBoxIsChecked(checkBoxUseStats);
	Bool bAllowObservers = GadgetCheckBoxIsChecked(checkBoxAllowObservers);
	Bool bRequireEac = GadgetCheckBoxIsChecked(checkBoxRequireEac);

	{
		CustomMatchPreferences pref;
		pref.setAllowsObserver(bAllowObservers);
		pref.setFactionsLimited(limitArmies);
		pref.setUseStats(useStats);
		pref.write();
	}

	UnicodeString gameName = GadgetTextEntryGetText(textEntryGameName);

	AsciiString passwd;
	passwd.translate(GadgetTextEntryGetText(textEntryGamePassword));


	// GenOnline:NOTE: Mods can change the starting money, so the lobby takes the amount this client reports instead of a fixed value.
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (!pLobbyInterface)
	{
		parentPopup = nullptr;
		GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
		GenOnlineSetLobbyAttemptHostJoin(FALSE);
		GSMessageBoxOk(UnicodeString(L"Error"), UnicodeString(L"Failed to get Online Services Lobby Interface!"));
		return;
	}

	pLobbyInterface->CreateLobby(gameName, md->m_displayName, md->m_fileName, md->m_isOfficial, md->m_numPlayers, limitArmies, useStats, TheGlobalData->m_defaultStartingCash.countMoney(), passwd.isNotEmpty(), std::string(passwd.str()), bAllowObservers, bRequireEac);

	GSMessageBoxCancel(UnicodeString(L"Creating Lobby"), UnicodeString(L"Lobby Creation is in progress..."), nullptr);

	return;
}


