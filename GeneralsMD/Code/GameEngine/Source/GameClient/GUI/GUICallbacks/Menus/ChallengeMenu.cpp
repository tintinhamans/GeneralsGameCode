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
// FILE: ChallengeMenu.cpp
// Author: Steve Copeland, May 2003
// Description: General's Challenge Mode Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/Image.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuData.h"


// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonPlayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType bioPortraitID = NAMEKEY_INVALID;
static NameKeyType bioNameEntryID = NAMEKEY_INVALID;
static NameKeyType bioDOBEntryID = NAMEKEY_INVALID;
static NameKeyType bioBirthplaceEntryID = NAMEKEY_INVALID;
static NameKeyType bioStrategyEntryID = NAMEKEY_INVALID;
static NameKeyType buttonGeneralPositionID[NUM_GENERALS] = {NAMEKEY_INVALID};
static NameKeyType backdropID = NAMEKEY_INVALID;
static NameKeyType bioParentID = NAMEKEY_INVALID;

// window pointers --------------------------------------------------------------------------------
static GameWindow *parentMenu = nullptr;
static GameWindow *buttonPlay = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *bioPortrait = nullptr;
static GameWindow *bioLine1Entry = nullptr;
static GameWindow *bioLine2Entry = nullptr;
static GameWindow *bioLine3Entry = nullptr;
static GameWindow *bioLine4Entry = nullptr;
static GameWindow *buttonGeneralPosition[NUM_GENERALS] = {nullptr};
static GameWindow *backdrop = nullptr;
static GameWindow *bioParent = nullptr;

static WindowVideoManager *wndVideoManager = nullptr;

//
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool isShuttingDown = FALSE;

static Bool isAutoSelecting = FALSE;
static UnsignedInt shownVersion = 0;

// for use by the intro animation
static Int buttonSequenceStep = 0;


//-------------------------------------------------------------------------------------------------
// returns the index of the General Position button selected, or -1 if not found
//-------------------------------------------------------------------------------------------------
static Int findPositionButton( Int controlID )
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		if (controlID == buttonGeneralPositionID[i])
			return i;
	}
	return -1;
}


//-------------------------------------------------------------------------------------------------
// enable the appropriate buttons, make sure they aren't hidden, and set the correct images
//-------------------------------------------------------------------------------------------------
static void setEnabledButtons()
{
	const ChallengeMenuData &data = ChallengeMenuData::instance();

	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		const ChallengeMenuData::General &general = data.m_generals[i];
		buttonGeneralPosition[i]->winEnable(general.m_enabled);
		buttonGeneralPosition[i]->winHide(! general.m_enabled);

		GadgetCheckBoxSetEnabledImage( buttonGeneralPosition[i], general.m_normal );
		if (general.m_normal)
			// image size keeps changing, so it'll drive the window size directly
			buttonGeneralPosition[i]->winSetSize( general.m_normal->getImageWidth(), general.m_normal->getImageWidth() );

		GadgetCheckBoxSetHiliteUncheckedBoxImage( buttonGeneralPosition[i], general.m_selected );
		GadgetCheckBoxSetDisabledUncheckedBoxImage( buttonGeneralPosition[i], general.m_selected );
		GadgetCheckBoxSetHiliteImage( buttonGeneralPosition[i], general.m_hilite );
	}
}


//-------------------------------------------------------------------------------------------------
// make the windows show the challenge data: the bio being typed, the Play button
//-------------------------------------------------------------------------------------------------
static void syncWindows()
{
	const ChallengeMenuData &data = ChallengeMenuData::instance();
	if( shownVersion == data.m_version )
		return;
	shownVersion = data.m_version;

	// the bio is hidden until one is set
	bioParent->winHide( !data.m_bioVisible );
	if( data.m_bioVisible )
	{
		bioPortrait->winSetEnabledImage( 0, data.m_portrait );
		bioPortrait->winSetStatus( WIN_STATUS_IMAGE );
	}

	GadgetStaticTextSetText(bioLine1Entry, data.m_bioShown[0]);
	GadgetStaticTextSetText(bioLine2Entry, data.m_bioShown[1]);
	GadgetStaticTextSetText(bioLine3Entry, data.m_bioShown[2]);
	GadgetStaticTextSetText(bioLine4Entry, data.m_bioShown[3]);

	buttonPlay->winHide( data.m_selected == -1 );
}

//-------------------------------------------------------------------------------------------------
// update the intro button sequence UNFINISHED
//-------------------------------------------------------------------------------------------------
void updateButtonSequence(Int stepsPerUpdate)
{
	const static Int cleanupStates = 2;
	if (buttonSequenceStep > NUM_GENERALS + cleanupStates)
		return;

	const ChallengeMenuData &data = ChallengeMenuData::instance();

	for (Int i = 0; i < stepsPerUpdate; i++)
	{
		// selected look
		Int pos = buttonSequenceStep;
		if (pos < NUM_GENERALS && !buttonGeneralPosition[pos]->winIsHidden())
			GadgetCheckBoxSetEnabledImage( buttonGeneralPosition[pos], data.m_generals[pos].m_selected );

		// mouseover look
		if (--pos > 0 && pos < NUM_GENERALS && !buttonGeneralPosition[pos]->winIsHidden())
			GadgetCheckBoxSetEnabledImage( buttonGeneralPosition[pos], data.m_generals[pos].m_hilite );

		// regular look
		if (--pos > 0 && pos < NUM_GENERALS && !buttonGeneralPosition[pos]->winIsHidden())
			GadgetCheckBoxSetEnabledImage( buttonGeneralPosition[pos], data.m_generals[pos].m_normal );

		buttonSequenceStep++;
	}
}


//-------------------------------------------------------------------------------------------------
/** init the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuInit( WindowLayout *layout, void *userData )
{
	ChallengeMenuActions::open();

	// init window ids and pointers
	parentID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:ParentChallengeMenu" );
	parentMenu = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	buttonPlayID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:ButtonPlay" );
	buttonPlay = TheWindowManager->winGetWindowFromId( parentMenu, buttonPlayID );
	buttonBackID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:ButtonBack" );
	buttonBack = TheWindowManager->winGetWindowFromId( parentMenu, buttonBackID );
	bioPortraitID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:BioPortrait" );
	bioPortrait = TheWindowManager->winGetWindowFromId( parentMenu, bioPortraitID );
	bioNameEntryID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:BioNameEntry" );
	bioLine1Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioNameEntryID ); // this window has been repurposed
	bioDOBEntryID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:BioDOBEntry" );
	bioLine2Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioDOBEntryID ); // this window has been repurposed
	bioBirthplaceEntryID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:BioBirthplaceEntry" );
	bioLine3Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioBirthplaceEntryID ); // this window has been repurposed
	bioStrategyEntryID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:BioStrategyEntry" );
	bioLine4Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioStrategyEntryID ); // this window has been repurposed
	backdropID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:MainBackdrop" );
	backdrop = TheWindowManager->winGetWindowFromId( parentMenu, backdropID);
	bioParentID = TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:GeneralsBioParent" );
	bioParent = TheWindowManager->winGetWindowFromId( parentMenu, bioParentID);

	AsciiString strButtonName;
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		strButtonName.format("ChallengeMenu.wnd:GeneralPosition%d", i);
		buttonGeneralPositionID[i] = TheNameKeyGenerator->nameToKey( strButtonName );
		buttonGeneralPosition[i] = TheWindowManager->winGetWindowFromId( parentMenu, buttonGeneralPositionID[i] );
		DEBUG_ASSERTCRASH(buttonGeneralPosition[i], ("Could not find the ButtonGeneralPosition[%d]",i ));

		// start all buttons hidden, then expose them later if there is a general for this spot
		buttonGeneralPosition[i]->winHide( TRUE );
	}

	// set defaults
	isAutoSelecting = FALSE;
	buttonSequenceStep = 0;
	setEnabledButtons();
	shownVersion = 0;
	syncWindows(); // hides the bio and the Play button until a general is chosen

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentMenu );
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *winGadgetParent = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("ChallengeMenu.wnd:GadgetParent"));
	if(winGadgetParent)
		winGadgetParent->winHide(TRUE);
	isShuttingDown = FALSE;

	if(!wndVideoManager)
		wndVideoManager = NEW WindowVideoManager;
	wndVideoManager->init();
}


//-------------------------------------------------------------------------------------------------
/** update the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuUpdate( WindowLayout *layout, void *userData )
{
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("ChallengeMenuFade");
//			TheTransitionHandler->setGroup("ChallengeButtonsIntro");


			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

//	if (TheTransitionHandler->isFinished())
//		updateButtonSequence( 1 );

	ChallengeMenuActions::update();
	syncWindows();

	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		TheShell->shutdownComplete( layout );
	}

	if(wndVideoManager)
		wndVideoManager->update();
}


//-------------------------------------------------------------------------------------------------
/** shutdown the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuShutdown( WindowLayout *layout, void *userData )
{
	delete wndVideoManager;
	wndVideoManager = nullptr;

	buttonSequenceStep = 0;

	Bool popImmediate = *(Bool *)userData;
	ChallengeMenuActions::close( popImmediate );
	if( popImmediate )
	{
		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;
	}

	TheTransitionHandler->reverse("ChallengeMenuFade");
	isShuttingDown = TRUE;
}


//-------------------------------------------------------------------------------------------------
/** challenge mode menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ChallengeMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)buttonBack, buttonBackID );

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
/** challenge mode menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ChallengeMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE: break;

		case GWM_DESTROY: break;

		case GWM_INPUT_FOCUS:
		{
			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}

		case GBM_MOUSE_ENTERING:
		{
			GameWindow *control = (GameWindow *)mData1;
			ChallengeMenuActions::hover( findPositionButton( control->winGetWindowId() ) );
			break;
		}

		case GBM_MOUSE_LEAVING:
		{
			GameWindow *control = (GameWindow *)mData1;
			ChallengeMenuActions::unhover( findPositionButton( control->winGetWindowId() ) );
			break;
		}

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

 			// we don't need to handle these
 			if ( isAutoSelecting )
 			{
 				isAutoSelecting = FALSE;
 				break;
 			}

			ChallengeMenuData &data = ChallengeMenuData::instance();
			Int buttonIndex = findPositionButton(controlID);
			if( buttonIndex != -1)
			{
				// partial radio button behavior (I'm not using actual radio buttons
				// because it limits options on how the interface can work, which is a
				// bad thing when the design is constantly in flux.)
				// ...basically this just makes you have exactly one button selected
				// once the first choice has been made.
				if (data.m_selected != -1)
				{
					isAutoSelecting = TRUE;
					GameWindow *lastControl = TheWindowManager->winGetWindowFromId( nullptr, buttonGeneralPositionID[data.m_selected]);
					GadgetCheckBoxToggle(lastControl);
				}

				ChallengeMenuActions::select( buttonIndex );
			}
			else if( controlID == buttonPlayID )
 			{
				const Int chosen = data.m_selected;
				ChallengeMenuActions::play();

				// turn off the last button so the screen will be pristine when the user returns
				if( chosen != -1 && data.m_selected == -1 )
				{
					isAutoSelecting = TRUE;
					GameWindow *lastControl = TheWindowManager->winGetWindowFromId( nullptr, buttonGeneralPositionID[chosen]);
					GadgetCheckBoxSetChecked(lastControl, FALSE);
				}

				buttonSequenceStep = 0;
			}
			else if( controlID == buttonBackID )
			{
				ChallengeMenuActions::back();
			}
			break;
		}

		default: return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
