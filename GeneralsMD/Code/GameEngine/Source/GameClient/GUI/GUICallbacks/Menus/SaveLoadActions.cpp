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

// FILE: SaveLoadActions.cpp /////////////////////////////////////////////////
// See SaveLoadActions.h. Bodies moved out of PopupSaveLoad.cpp's callbacks; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadActions.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadData.h"

#include <windows.h>

namespace SaveLoadActions
{

static SaveLoadData &data()
{
	return SaveLoadData::instance();
}

// Closes the screen: the popup hides itself, the shell version hides the whole shell.
static void closeMenu()
{
	SaveLoadData &d = data();
	if( d.m_isPopup )
	{
		if( d.m_closePopup )
			d.m_closePopup();
	}
	else
		TheShell->hideShell();
}

static void doLoadGame()
{

	AvailableGameInfo *selectedGameInfo = data().selectedInfo();
	DEBUG_ASSERTCRASH( selectedGameInfo, ("doLoadGame: No selected game info found") );

	// when loading a game we also close the quit/esc menu for the user when in-game
	if( TheShell->isShellActive() == FALSE )
	{
		destroyQuitMenu();
	}
	else
	{
		TheTransitionHandler->remove("MainMenuLoadReplayMenu");
		TheTransitionHandler->remove("MainMenuLoadReplayMenuBack");
		TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );
	}

	//
	// load game, note the *copy* of the selected game info is passed here because we will
	// loose these allocated user data pointers attached as list items when the engine resets
	//
	if (TheGameState->loadGame( *selectedGameInfo ) != SC_OK)
	{
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );
		TheGameEngine->reset();
		TheShell->showShell(TRUE);
	}

}

// When the layout only saves between missions it is a mission save, else a normal one.
static SaveFileType saveFileType()
{
	return data().m_layoutType == SLLT_SAVE_AND_LOAD ? SAVE_FILE_TYPE_NORMAL : SAVE_FILE_TYPE_MISSION;
}

//
// if we have a campaign the default description names the location in the campaign, otherwise
// it is just the map name (which is really only used in debug)
//
static UnicodeString defaultDescription()
{
	UnicodeString defaultDesc;
	Campaign *campaign = TheCampaignManager->getCurrentCampaign();

	if( campaign )
		defaultDesc.format( L"%s %d",
												TheGameText->fetch( campaign->m_campaignNameLabel ).str(),
												TheCampaignManager->getCurrentMissionNumber() + 1 );
	else
	{
		const char *mapName = TheGlobalData->m_mapName.reverseFind( '\\' );

		if( mapName )
			defaultDesc.format( L"%S", mapName + 1 );
		else
			defaultDesc.format( L"%S", TheGlobalData->m_mapName.str() );

		//Keep the extension out of the descriptive name.
		if( (defaultDesc.getLength() >= 4)  &&  (defaultDesc.getCharAt(defaultDesc.getLength()-4) == '.') )
		{
			defaultDesc.truncateBy(4);
		}

	}

	return defaultDesc;
}

static void endDialog()
{
	SaveLoadData &d = data();
	d.m_dialog = SaveLoadData::DIALOG_NONE;
	d.touch();
}

void open( SaveLoadLayoutType layoutType, Bool isPopup, void (*closePopup)() )
{
	if( !isPopup )
		TheShell->showShellMap(TRUE);

	SaveLoadData &d = data();
	d.m_closePopup = closePopup;
	d.open( layoutType, isPopup );
}

void select( Int row )
{
	data().select( row );
}

void activate( Int row )
{
	select( row );
	load();
}

void load()
{
	SaveLoadData &d = data();
	if( d.selectedInfo() == nullptr )
		return;

	//
	// if we're in the shell we do not need a confirmation dialog that states we will
	// lose the current loaded game data cause we're not in a game
	//
	if( TheShell->isShellActive() == TRUE )
	{
		// just close the menu and do the load game logic
		closeMenu();
		doLoadGame();
	}
	else
	{
		d.m_dialog = SaveLoadData::DIALOG_LOAD_CONFIRM;
		d.touch();
	}
}

void save()
{
	SaveLoadData &d = data();

	// sanity
	DEBUG_ASSERTCRASH( d.m_layoutType == SLLT_SAVE_AND_LOAD ||
										 d.m_layoutType == SLLT_SAVE_ONLY,
										 ("SaveLoadActions::save - layout type '%d' does not allow saving",
										 d.m_layoutType) );

	// if there is no file info, this is a new game
	if( d.selectedInfo() == nullptr )
	{
		d.m_dialog = SaveLoadData::DIALOG_SAVE_DESC;
		d.m_description = defaultDescription();
	}
	else
		d.m_dialog = SaveLoadData::DIALOG_OVERWRITE_CONFIRM;

	d.touch();
}

void remove()
{
	SaveLoadData &d = data();
	if( d.selectedInfo() )
	{
		d.m_dialog = SaveLoadData::DIALOG_DELETE_CONFIRM;
		d.touch();
	}
}

void back()
{
	if( data().m_isPopup )
		closeMenu();
	else
		TheShell->pop();
}

void escape()
{
	endDialog();
	back();
}

void confirmLoad()
{
	endDialog();
	closeMenu();
	doLoadGame();
}

void cancelLoad()
{
	endDialog();
}

void confirmOverwrite()
{
	AvailableGameInfo *selectedGameInfo = data().selectedInfo();
	DEBUG_ASSERTCRASH( selectedGameInfo, ("SaveLoadActions::confirmOverwrite: Internal error, list entry to overwrite game has no game info") );

	endDialog();
	closeMenu();

	// save the game
	AsciiString filename;
	filename = selectedGameInfo->filename;
	TheGameState->saveGame( filename, selectedGameInfo->saveGameInfo.description, saveFileType() );
}

void cancelOverwrite()
{
	endDialog();
}

void confirmSaveDesc()
{
	SaveLoadData &d = data();
	UnicodeString desc = d.m_description;
	AvailableGameInfo *selectedGameInfo = d.selectedInfo();

	endDialog();
	closeMenu();

	// save the game
	AsciiString filename;
	if( selectedGameInfo )
		filename = selectedGameInfo->filename;
	TheGameState->saveGame( filename, desc, saveFileType() );
}

void cancelSaveDesc()
{
	endDialog();
}

void confirmDelete()
{
	SaveLoadData &d = data();

	AvailableGameInfo *selectedGameInfo = d.selectedInfo();
	if( selectedGameInfo )
	{
		// delete the file
		AsciiString filepath = TheGameState->getFilePathInSaveDirectory(selectedGameInfo->filename);
		DeleteFile( filepath.str() );

		// repopulate the list
		d.refresh();
	}

	endDialog();
}

void cancelDelete()
{
	endDialog();
}

}
