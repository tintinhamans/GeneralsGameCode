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

// FILE: PopupSaveLoad.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PopupSaveLoad.cpp
//
// Created:   Chris Brue, June 2002
//
// Desc:      the Save/Load window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Color.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadData.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBackKey					= NAMEKEY_INVALID;
static NameKeyType buttonSaveKey					= NAMEKEY_INVALID;
static NameKeyType buttonLoadKey					= NAMEKEY_INVALID;
static NameKeyType buttonDeleteKey				= NAMEKEY_INVALID;
static NameKeyType listboxGamesKey				= NAMEKEY_INVALID;
static NameKeyType buttonOverwriteCancel	= NAMEKEY_INVALID;
static NameKeyType buttonOverwriteConfirm = NAMEKEY_INVALID;
static NameKeyType buttonLoadCancel				= NAMEKEY_INVALID;
static NameKeyType buttonLoadConfirm			= NAMEKEY_INVALID;
static NameKeyType buttonSaveDescCancel		= NAMEKEY_INVALID;
static NameKeyType buttonSaveDescConfirm	= NAMEKEY_INVALID;
static NameKeyType buttonDeleteConfirm		= NAMEKEY_INVALID;
static NameKeyType buttonDeleteCancel			= NAMEKEY_INVALID;

static GameWindow *buttonFrame = nullptr;
static GameWindow *overwriteConfirm = nullptr;
static GameWindow *loadConfirm = nullptr;
static GameWindow *saveDesc = nullptr;
static GameWindow *listboxGames = nullptr;
static GameWindow *editDesc = nullptr;
static GameWindow *deleteConfirm = nullptr;

static GameWindow *parent = nullptr;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool isShuttingDown = false;
static UnsignedInt shownRowsVersion = 0;

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
extern Bool DontShowMainMenu; //KRIS
extern Bool ReplayWasPressed;

// ------------------------------------------------------------------------------------------------
/** Fill the games listbox from the save list */
// ------------------------------------------------------------------------------------------------
static void populateListbox()
{
	const SaveLoadData &data = SaveLoadData::instance();

	GadgetListBoxReset( listboxGames );

	for( Int i = 0; i < (Int)data.m_rows.size(); ++i )
	{
		const SaveLoadRow &row = data.m_rows[i];
		Color color = GameMakeColor( (row.m_color >> 16) & 0xFF, (row.m_color >> 8) & 0xFF, row.m_color & 0xFF, 255 );

		Int index;
		if( row.m_info == nullptr )
		{
			// the new save game entry
			index = GadgetListBoxAddEntryText( listboxGames, row.m_name, color, -1 );
		}
		else
		{
			index = GadgetListBoxAddEntryText( listboxGames, row.m_name, color, -1, 0 );
			GadgetListBoxAddEntryText( listboxGames, row.m_time, color, index, 1 );
			GadgetListBoxAddEntryText( listboxGames, row.m_date, color, index, 2 );
		}
		GadgetListBoxSetItemData( listboxGames, row.m_info, index );
	}

	shownRowsVersion = data.m_rowsVersion;
}

// ------------------------------------------------------------------------------------------------
/** Make the windows show the save/load data: the list and its selection, which dialog is up and
	* which controls are enabled */
// ------------------------------------------------------------------------------------------------
static void syncWindows()
{
	const SaveLoadData &data = SaveLoadData::instance();

	if( shownRowsVersion != data.m_rowsVersion )
		populateListbox();

	Int selected;
	GadgetListBoxGetSelected( listboxGames, &selected );
	if( selected != data.m_selected && data.m_selected >= 0 )
		GadgetListBoxSetSelected( listboxGames, data.m_selected );

	overwriteConfirm->winHide( data.m_dialog != SaveLoadData::DIALOG_OVERWRITE_CONFIRM );
	loadConfirm->winHide( data.m_dialog != SaveLoadData::DIALOG_LOAD_CONFIRM );
	saveDesc->winHide( data.m_dialog != SaveLoadData::DIALOG_SAVE_DESC );
	deleteConfirm->winHide( data.m_dialog != SaveLoadData::DIALOG_DELETE_CONFIRM );

	listboxGames->winEnable( data.isListEnabled() );
	buttonFrame->winEnable( data.areButtonsEnabled() );

	// for loading only, disable the save button, otherwise enable it
	GameWindow *saveButton = TheWindowManager->winGetWindowFromId( nullptr, buttonSaveKey );
	DEBUG_ASSERTCRASH( saveButton, ("SaveLoadMenuInit: Unable to find save button") );
	saveButton->winEnable( data.canSave() );

	// if something with a game file is selected we can use load and delete
	GameWindow *buttonLoad = TheWindowManager->winGetWindowFromId( nullptr, buttonLoadKey );
	buttonLoad->winEnable( data.canLoad() );
	GameWindow *buttonDelete = TheWindowManager->winGetWindowFromId( nullptr, buttonDeleteKey );
	buttonDelete->winEnable( data.canDelete() );
}

// ------------------------------------------------------------------------------------------------
/** Drop the dialogs from the windows before an action closes the menu */
// ------------------------------------------------------------------------------------------------
static void resetDialogWindows()
{
	overwriteConfirm->winHide( TRUE );
	loadConfirm->winHide( TRUE );
	saveDesc->winHide( TRUE );
	deleteConfirm->winHide( TRUE );
	listboxGames->winEnable( TRUE );
	buttonFrame->winEnable( TRUE );
}

// ------------------------------------------------------------------------------------------------
/** Hide the popup layout */
// ------------------------------------------------------------------------------------------------
static void closePopup()
{
	WindowLayout *saveLoadMenuLayout = parent ? parent->winGetLayout() : nullptr;
	if( saveLoadMenuLayout )
		saveLoadMenuLayout->hide( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuInit( WindowLayout *layout, void *userData )
{

	// set default behavior for this menu
	SaveLoadLayoutType layoutType = SLLT_SAVE_AND_LOAD;
	// get layout type if present
	if( userData )
		layoutType = *((SaveLoadLayoutType *)userData);

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonSave" );
	buttonLoadKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoad" );
	buttonDeleteKey        = NAMEKEY( "PopupSaveLoad.wnd:ButtonDelete" );
	listboxGamesKey				 = NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" );
	buttonOverwriteCancel	 = NAMEKEY( "PopupSaveLoad.wnd:ButtonOverwriteCancel" );
	buttonOverwriteConfirm = NAMEKEY( "PopupSaveLoad.wnd:ButtonOverwriteConfirm" );
	buttonLoadCancel			 = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoadCancel" );
	buttonLoadConfirm      = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoadConfirm" );
	buttonSaveDescCancel   = NAMEKEY( "PopupSaveLoad.wnd:ButtonSaveDescCancel" );
	buttonSaveDescConfirm  = NAMEKEY( "PopupSaveLoad.wnd:ButtonSaveDescConfirm" );
	buttonDeleteConfirm		 = NAMEKEY( "PopupSaveLoad.wnd:ButtonDeleteConfirm" );
	buttonDeleteCancel		 = NAMEKEY( "PopupSaveLoad.wnd:ButtonDeleteCancel" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("PopupSaveLoad.wnd:SaveLoadMenu");
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );
	TheWindowManager->winSetModal( parent );

	// enable the menu action buttons
	buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );

	// get confirmation windows
	overwriteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:OverwriteConfirmParent" ) );
	loadConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:LoadConfirmParent" ) );
	saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );
	deleteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:DeleteConfirmParent" ) );
	editDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:EntryDesc" ) );
	// get the listbox that will have the save games in it
	listboxGames = TheWindowManager->winGetWindowFromId( nullptr, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != nullptr, ("SaveLoadMenuInit - Unable to find games listbox") );

	// list the save games on disk and show them
	SaveLoadActions::open( layoutType, TRUE, &closePopup );
	populateListbox();
	GadgetListBoxSetSelected( listboxGames, 0 );

	// update the dialogs and the availability of the menu buttons
	syncWindows();

}

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuFullScreenInit( WindowLayout *layout, void *userData )
{

	// set default behavior for this menu
	SaveLoadLayoutType layoutType = SLLT_LOAD_ONLY;

	// get layout type if present
	if( userData )
		layoutType = *((SaveLoadLayoutType *)userData);

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "SaveLoad.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "SaveLoad.wnd:ButtonSave" );
	buttonLoadKey					 = NAMEKEY( "SaveLoad.wnd:ButtonLoad" );
	buttonDeleteKey        = NAMEKEY( "SaveLoad.wnd:ButtonDelete" );
	listboxGamesKey				 = NAMEKEY( "SaveLoad.wnd:ListboxGames" );
	buttonOverwriteCancel	 = NAMEKEY( "SaveLoad.wnd:ButtonOverwriteCancel" );
	buttonOverwriteConfirm = NAMEKEY( "SaveLoad.wnd:ButtonOverwriteConfirm" );
	buttonLoadCancel			 = NAMEKEY( "SaveLoad.wnd:ButtonLoadCancel" );
	buttonLoadConfirm      = NAMEKEY( "SaveLoad.wnd:ButtonLoadConfirm" );
	buttonSaveDescCancel   = NAMEKEY( "SaveLoad.wnd:ButtonSaveDescCancel" );
	buttonSaveDescConfirm  = NAMEKEY( "SaveLoad.wnd:ButtonSaveDescConfirm" );
	buttonDeleteConfirm		 = NAMEKEY( "SaveLoad.wnd:ButtonDeleteConfirm" );
	buttonDeleteCancel		 = NAMEKEY( "SaveLoad.wnd:ButtonDeleteCancel" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("SaveLoad.wnd:SaveLoadMenu");
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );
//	TheWindowManager->winSetModal( parent );

	// enable the menu action buttons
	buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:MenuButtonFrame" ) );

	// get confirmation windows
	overwriteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:OverwriteConfirmParent" ) );
	loadConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:LoadConfirmParent" ) );
	saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:SaveDescParent" ) );

	editDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:EntryDesc" ) );
	deleteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:DeleteConfirmParent" ) );
	// get the listbox that will have the save games in it
	listboxGames = TheWindowManager->winGetWindowFromId( nullptr, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != nullptr, ("SaveLoadMenuInit - Unable to find games listbox") );

	// list the save games on disk and show them
	SaveLoadActions::open( layoutType, FALSE, nullptr );
	populateListbox();
	GadgetListBoxSetSelected( listboxGames, 0 );

	// update the dialogs and the availability of the menu buttons
	syncWindows();

	layout->hide(FALSE);
	justEntered = TRUE;
	initialGadgetDelay = 2;
	if(parent)
		parent->winHide(TRUE);
	isShuttingDown = false;
}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu shutdown method */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuShutdown( WindowLayout *layout, void *userData )
{

	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;

	}

	// our shutdown is complete
	TheTransitionHandler->reverse("SaveLoadMenuFade");
	isShuttingDown = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu update method */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuUpdate( WindowLayout *layout, void *userData )
{

	if(DontShowMainMenu && justEntered)
		justEntered = FALSE;
	if(ReplayWasPressed && justEntered)
	{
		justEntered = FALSE;
		ReplayWasPressed = FALSE;
	}
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("SaveLoadMenuFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		TheShell->shutdownComplete( layout );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowMsgHandledType SaveLoadMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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
					// same as the back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{

						//Kris: Patch 1.01 - November 12, 2003
						//If you are in the game, then bring up the popup save menu, select a save game, click delete,
						//hit ESC (brings you back to menu), then hit save/load again, the delete confirmation is still up
						//and clicking on yes causes it to crash. So whenever we hit esc to leave this interface, kill
						//the confirmation, and re-enable the listbox and buttonFrame.
						resetDialogWindows();

						SaveLoadActions::escape();

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
/** SaveLoad menu system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SaveLoadMenuSystem( GameWindow *window, UnsignedInt msg,
																				 WindowMsgData mData1, WindowMsgData mData2 )
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
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
				DEBUG_ASSERTCRASH( listboxGames != nullptr, ("SaveLoadMenuInit - Unable to find games listbox") );

				if (listboxGames != nullptr) {
					int rowSelected = mData2;
					GadgetListBoxSetSelected(listboxGames, rowSelected);

					if (control == listboxGames)
					{
						SaveLoadActions::activate(rowSelected);

						// loading in game asks first, the rest closes the menu
						if( SaveLoadData::instance().m_dialog != SaveLoadData::DIALOG_NONE )
							syncWindows();
					}
				}
				break;
			}

		// --------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;

			GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
			DEBUG_ASSERTCRASH( listboxGames != nullptr, ("SaveLoadMenuInit - Unable to find games listbox") );

			//
			// handle games listbox, when certain items are selected in the listbox only some
			// commands are available
			//
			if( control == listboxGames )
			{
				Int selected;
				GadgetListBoxGetSelected( listboxGames, &selected );
				SaveLoadActions::select( selected );
				syncWindows();
			}

			break;

		}

    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == buttonLoadKey )
      {
				SaveLoadActions::load();

				// loading in game asks first, the rest closes the menu
				if( SaveLoadData::instance().m_dialog != SaveLoadData::DIALOG_NONE )
					syncWindows();
      }
      else if( controlID == buttonSaveKey )
      {

				SaveLoadActions::save();

				if( SaveLoadData::instance().m_dialog == SaveLoadData::DIALOG_SAVE_DESC )
				{
					// set the description text entry field to default value
					GadgetTextEntrySetText( editDesc, SaveLoadData::instance().m_description );
					TheWindowManager->winSetFocus(editDesc);
				}
				syncWindows();

      }
			else if( controlID == buttonDeleteKey )
			{

				SaveLoadActions::remove();
				syncWindows();

			}
			else if( controlID == buttonBackKey )
			{

				SaveLoadActions::back();

			}
			else if( controlID == buttonDeleteConfirm )
			{
				SaveLoadActions::confirmDelete();
				syncWindows();
			}
			else if( controlID == buttonDeleteCancel )
			{
				SaveLoadActions::cancelDelete();
				syncWindows();
			}
			else if( controlID == buttonOverwriteConfirm )
			{
				resetDialogWindows();
				SaveLoadActions::confirmOverwrite();
			}
			else if( controlID == buttonOverwriteCancel )
			{
				SaveLoadActions::cancelOverwrite();
				syncWindows();
			}
			else if( controlID == buttonSaveDescConfirm )
			{

				SaveLoadData::instance().m_description = GadgetTextEntryGetText( editDesc );

				resetDialogWindows();
				SaveLoadActions::confirmSaveDesc();

			}
			else if( controlID == buttonSaveDescCancel )
			{

				SaveLoadActions::cancelSaveDesc();
				syncWindows();

			}
			else if( controlID == buttonLoadConfirm )
			{

				resetDialogWindows();
				SaveLoadActions::confirmLoad();

			}
			else if( controlID == buttonLoadCancel )
			{

				SaveLoadActions::cancelLoad();
				syncWindows();

			}

			break;

		}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
