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

// FILE: PopupReplay.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   Generals
//
// File name: PopupReplay.cpp
//
// Created:   Matthew D. Campbell, November 2002
//
// Desc:      the Replay Save window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayData.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBackKey					= NAMEKEY_INVALID;
static NameKeyType buttonSaveKey					= NAMEKEY_INVALID;
static NameKeyType listboxGamesKey				= NAMEKEY_INVALID;
static NameKeyType textEntryReplayNameKey = NAMEKEY_INVALID;

static GameWindow *parent = nullptr;
static GameWindow *replaySavedParent = nullptr;
static GameWindow *listboxGames = nullptr;
static GameWindow *textEntryReplayName = nullptr;
static UnsignedInt shownRowsVersion = 0;

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
extern void FillReplayListbox(GameWindow *listbox, const std::vector<ReplayRow> &rows);
extern void ScoreScreenEnableControls(Bool enable);

//-------------------------------------------------------------------------------------------------
/** Show or hide the "Replay Saved" popup */
//-------------------------------------------------------------------------------------------------
void ShowReplaySavedPopup(Bool show)
{
	if (replaySavedParent != nullptr) {
		if (show) {
			replaySavedParent->winHide(FALSE);
		} else {
			replaySavedParent->winHide(TRUE);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Close the save/load menu */
// ------------------------------------------------------------------------------------------------
static void closeSaveMenu( GameWindow *window )
{
	WindowLayout *layout = window->winGetLayout();

	if( layout )
		layout->hide( TRUE );

}

// ------------------------------------------------------------------------------------------------
/** Hide the popup and give the score screen its buttons back */
// ------------------------------------------------------------------------------------------------
static void closePopup()
{
	if (parent != nullptr)
	{
		closeSaveMenu( parent );
	}
	ScoreScreenEnableControls(TRUE);
}

// ------------------------------------------------------------------------------------------------
/** Make the windows show the popup data: the list and its selection, the name entry, the save
	* button and the "Replay Saved" notice */
// ------------------------------------------------------------------------------------------------
static void syncWindows()
{
	const PopupReplayData &data = PopupReplayData::instance();

	if( parent == nullptr || listboxGames == nullptr || textEntryReplayName == nullptr )
		return;

	// the fill selects a row, which comes back through GLM_SELECTED and syncs again
	if( shownRowsVersion != data.m_rowsVersion )
	{
		shownRowsVersion = data.m_rowsVersion;
		FillReplayListbox( listboxGames, data.m_rows );
	}

	Int selected;
	GadgetListBoxGetSelected( listboxGames, &selected );
	if( selected != data.m_selected && data.m_selected >= 0 )
		GadgetListBoxSetSelected( listboxGames, data.m_selected );

	if( GadgetTextEntryGetText( textEntryReplayName ) != data.m_name )
		GadgetTextEntrySetText( textEntryReplayName, data.m_name );

	//Kris:
	//Enable or disable the save button -- disabled when empty.
	GameWindow *control = TheWindowManager->winGetWindowFromId( parent, buttonSaveKey );
	if( control )
		control->winEnable( data.canSave() );

	ShowReplaySavedPopup( data.m_showSaved );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void PopupReplayInit( WindowLayout *layout, void *userData )
{

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "PopupReplay.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "PopupReplay.wnd:ButtonSave" );
	listboxGamesKey				 = NAMEKEY( "PopupReplay.wnd:ListboxGames" );
	textEntryReplayNameKey = NAMEKEY( "PopupReplay.wnd:TextEntryReplayName" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("PopupReplay.wnd:PopupReplayMenu");
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );

	NameKeyType replaySavedParentID = TheNameKeyGenerator->nameToKey("PopupReplay.wnd:PopupReplaySaved");
	replaySavedParent = TheWindowManager->winGetWindowFromId( nullptr, replaySavedParentID);
	if (replaySavedParent == nullptr) {
		DEBUG_CRASH(("replaySavedParent == nullptr"));
	}

	// enable the menu action buttons
	GameWindow *buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupReplay.wnd:MenuButtonFrame" ) );
	buttonFrame->winEnable( TRUE );

	// get the listbox that will have the save games in it
	listboxGames = TheWindowManager->winGetWindowFromId( nullptr, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != nullptr, ("PopupReplayInit - Unable to find games listbox") );

	textEntryReplayName = TheWindowManager->winGetWindowFromId( parent, textEntryReplayNameKey );

	// populate the listbox with the save games on disk
	PopupReplayActions::open( &closePopup );
	shownRowsVersion = 0;
	syncWindows();

	// selecting the first entry named the replay, start with an empty name instead
	PopupReplayActions::setName( UnicodeString::TheEmptyString );
	syncWindows();

	TheWindowManager->winSetFocus( textEntryReplayName );

}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu shutdown method */
//-------------------------------------------------------------------------------------------------
void PopupReplayShutdown( WindowLayout *layout, void *userData )
{
	parent = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu update method */
//-------------------------------------------------------------------------------------------------
void PopupReplayUpdate( WindowLayout *layout, void *userData )
{
	PopupReplayActions::update();

	if (parent != nullptr)
		syncWindows();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupReplayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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
						GameWindow *button = TheWindowManager->winGetWindowFromId( parent, buttonBackKey );
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonBackKey );

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
WindowMsgHandledType PopupReplaySystem( GameWindow *window, UnsignedInt msg,
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

		// --------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;

			//
			// handle games listbox, when certain items are selected in the listbox only some
			// commands are available
			//
			if( control == listboxGames )
			{
				int rowSelected = mData2;
				if (rowSelected >= 0)
				{
					PopupReplayActions::select( rowSelected );
					syncWindows();
				}
			}

			break;

		}

    //---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == textEntryReplayNameKey )
      {
				PopupReplayActions::setName( GadgetTextEntryGetText( control ) );
				PopupReplayActions::save();
      }

			break;

		}
    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == buttonSaveKey )
      {
				PopupReplayActions::setName( GadgetTextEntryGetText( textEntryReplayName ) );
				PopupReplayActions::save();
      }
			else if( controlID == buttonBackKey )
			{

				PopupReplayActions::back();

			}

			break;

		}

		case GEM_UPDATE_TEXT:
		{
			if( textEntryReplayName != nullptr )
			{
				PopupReplayActions::setName( GadgetTextEntryGetText( textEntryReplayName ) );
				syncWindows();
			}

			break;
		}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
