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

// FILE: ReplayMenu.cpp /////////////////////////////////////////////////////////////////////
// Author: Chris The masta Huybregts, December 2001
// Description: Replay Menus
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Lib/BaseType.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuData.h"

// window ids -------------------------------------------------------------------------------------
static NameKeyType parentReplayMenuID = NAMEKEY_INVALID;
static NameKeyType buttonLoadID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType listboxReplayFilesID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteID = NAMEKEY_INVALID;
static NameKeyType buttonCopyID = NAMEKEY_INVALID;

static Bool isShuttingDown = false;

// window pointers --------------------------------------------------------------------------------
static GameWindow *parentReplayMenu = nullptr;
static GameWindow *buttonLoad = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *listboxReplayFiles = nullptr;
static GameWindow *buttonDelete = nullptr;
static GameWindow *buttonCopy = nullptr;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static UnsignedInt shownRowsVersion = 0;


#if defined(RTS_DEBUG)
static GameWindow *buttonAnalyzeReplay = nullptr;
#endif

static Color toColor( UnsignedInt rgb )
{
	return GameMakeColor( (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255 );
}

//-------------------------------------------------------------------------------------------------
/** Fill the listbox from the replay rows */
//-------------------------------------------------------------------------------------------------
static void fillListbox(GameWindow *listbox, const std::vector<ReplayRow> &rows)
{
	GadgetListBoxReset(listbox);
	const Int listboxLength = GadgetListBoxGetListLength(listbox);
	const Int columns = GadgetListBoxGetNumColumns(listbox);

	for (size_t i = 0; i < rows.size(); ++i)
	{
		const ReplayRow &row = rows[i];
		const Color color = toColor(row.m_color);
		const Color mapColor = toColor(row.m_mapColor);

		// columns are: name, date, version, map, extra
		const Int insertionIndex = GadgetListBoxAddEntryText(listbox, row.m_name, color, -1, 0);
		DEBUG_ASSERTCRASH(insertionIndex >= 0, ("Expects valid index"));

		// TheSuperHackers @info Caball009 09/02/2026 Original replay menu has 4 columns; the code now supports a future 5-column layout.
		// If there aren't two columns for time and date, concatenate them for a single column.
		if (columns == 4)
		{
			UnicodeString displayDateTimeBuffer;
			displayDateTimeBuffer.format(L"%s %s", row.m_time.str(), row.m_date.str());

			GadgetListBoxAddEntryText(listbox, displayDateTimeBuffer, color, insertionIndex, 1);
			GadgetListBoxAddEntryText(listbox, row.m_version, color, insertionIndex, 2);
			GadgetListBoxAddEntryText(listbox, row.m_map, mapColor, insertionIndex, 3);
		}
		else if (columns == 5)
		{
			GadgetListBoxAddEntryText(listbox, row.m_time, color, insertionIndex, 1);
			GadgetListBoxAddEntryText(listbox, row.m_date, color, insertionIndex, 2);
			GadgetListBoxAddEntryText(listbox, row.m_version, color, insertionIndex, 3);
			GadgetListBoxAddEntryText(listbox, row.m_map, mapColor, insertionIndex, 4);
		}
		else
		{
			DEBUG_CRASH(("Replay menu uses %d columns; expected either 4 or 5", columns));
		}

		if (insertionIndex == listboxLength - 1)
			break;
	}
	GadgetListBoxSetSelected(listbox, 0);
}

//-------------------------------------------------------------------------------------------------
/** Populate the listbox with the names of the available replay files. Also used by PopupReplay. */
//-------------------------------------------------------------------------------------------------
void PopulateReplayFileListbox(GameWindow *listbox)
{
	std::vector<ReplayRow> rows;
	if (!ReplayList::scan(rows))
		return;

	fillListbox(listbox, rows);
}

UnicodeString GetReplayFilenameFromListbox(GameWindow *listbox, Int index)
{
	UnicodeString fname = GadgetListBoxGetText(listbox, index);

	if (fname == TheGameText->fetch("GUI:LastReplay"))
	{
		fname.translate(TheRecorder->getLastReplayFileName());
	}

	UnicodeString ext;
	ext.translate(TheRecorder->getReplayExtention());
	fname.concat(ext);

	return fname;
}

//-------------------------------------------------------------------------------------------------
/** Make the listbox show the replay data: the rows and the selection */
//-------------------------------------------------------------------------------------------------
static void syncWindows()
{
	const ReplayMenuData &data = ReplayMenuData::instance();

	if( shownRowsVersion != data.m_rowsVersion )
	{
		fillListbox( listboxReplayFiles, data.m_rows );
		shownRowsVersion = data.m_rowsVersion;
	}

	Int selected;
	GadgetListBoxGetSelected( listboxReplayFiles, &selected );
	if( selected != data.m_selected && data.m_selected >= 0 )
		GadgetListBoxSetSelected( listboxReplayFiles, data.m_selected );
}

//-------------------------------------------------------------------------------------------------
// TheSuperHackers @feature Stubbjax 21/10/2025 Show extra info tooltip when hovering over a replay.

static void showReplayTooltip(GameWindow* window, WinInstanceData* instData, UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	const std::vector<ReplayRow> &rows = ReplayMenuData::instance().m_rows;
	if (row == -1 || col == -1 || row >= (Int)rows.size())
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
		return;
	}

	TheMouse->setCursorTooltip(rows[row].m_tooltip, -1, nullptr, 1.5f);
}

//-------------------------------------------------------------------------------------------------
/** Hide the menu once a replay started playing */
//-------------------------------------------------------------------------------------------------
static void closeMenu()
{
	if(parentReplayMenu != nullptr)
	{
		parentReplayMenu->winHide(TRUE);
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the single player menu */
//-------------------------------------------------------------------------------------------------
void ReplayMenuInit( WindowLayout *layout, void *userData )
{
	// get ids for our children controls
	parentReplayMenuID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ParentReplayMenu" );
	buttonLoadID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ButtonLoadReplay" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ButtonBack" );
	listboxReplayFilesID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ListboxReplayFiles" );
	buttonDeleteID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ButtonDeleteReplay" );
	buttonCopyID = TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:ButtonCopyReplay" );

	parentReplayMenu = TheWindowManager->winGetWindowFromId( nullptr, parentReplayMenuID );
	buttonLoad = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonLoadID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonBackID );
	listboxReplayFiles = TheWindowManager->winGetWindowFromId( parentReplayMenu, listboxReplayFilesID );
	listboxReplayFiles->winSetTooltipFunc(showReplayTooltip);
	buttonDelete = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonDeleteID );
	buttonCopy = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonCopyID );

#if ENABLE_GUI_HACKS
	// TheSuperHackers @tweak Caball009 07/02/2026 The version column is wider than the time / date column.
	// Switch them so that there's enough space to show both time and date without a line break.
	ListboxData* list = static_cast<ListboxData*>(listboxReplayFiles->winGetUserData());

	if (list->columns == 4 && list->columnWidth[1] < list->columnWidth[2])
		std::swap(list->columnWidth[1], list->columnWidth[2]);
#endif

	//Load the listbox shiznit
	ReplayMenuActions::open( &closeMenu );
	GadgetListBoxReset(listboxReplayFiles);
	shownRowsVersion = 0;
	syncWindows();

#if defined(RTS_DEBUG)
	WinInstanceData instData;
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Analyze Replay";
	instData.setTooltipText(L"Only Used in Debug and Internal!");
	buttonAnalyzeReplay = TheWindowManager->gogoGadgetPushButton( parentReplayMenu,
																									 WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
																									 4, 4,
																									 180, 26,
																									 &instData, nullptr, TRUE );
#endif

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentReplayMenu );
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("ReplayMenu.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);
	isShuttingDown = FALSE;

}

//-------------------------------------------------------------------------------------------------
/** single player menu shutdown method */
//-------------------------------------------------------------------------------------------------
void ReplayMenuShutdown( WindowLayout *layout, void *userData )
{

	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;

	}

	// our shutdown is complete
	TheTransitionHandler->reverse("ReplayMenuFade");
	isShuttingDown = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** single player menu update method */
//-------------------------------------------------------------------------------------------------
void ReplayMenuUpdate( WindowLayout *layout, void *userData )
{
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("ReplayMenuFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	ReplayMenuActions::update();
	syncWindows();

	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		TheShell->shutdownComplete( layout );

}

//-------------------------------------------------------------------------------------------------
/** Replay menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayMenuInput( GameWindow *window, UnsignedInt msg,
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
/** single player menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayMenuSystem( GameWindow *window, UnsignedInt msg,
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

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're given the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}
		//---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				if( control == listboxReplayFiles )
				{
					Int selected;
					GadgetListBoxGetSelected( listboxReplayFiles, &selected );
					ReplayMenuActions::select( selected );
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxReplayFilesID )
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						ReplayMenuActions::activate( rowSelected );
						syncWindows();
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

#if defined(RTS_DEBUG)
			if( controlID == buttonAnalyzeReplay->winGetWindowId() )
			{
				const ReplayRow *row = ReplayMenuData::instance().selectedRow();
				if(row == nullptr)
				{
					MessageBoxOk(L"Blah Blah",L"Please select something munkee boy", nullptr);
					break;
				}

				if (TheRecorder->analyzeReplay(row->m_fileName))
				{
					do
					{
						TheRecorder->update();
					} while (TheRecorder->isPlaybackInProgress());
				}
			}
			else
#endif
			if( controlID == buttonLoadID )
			{
				ReplayMenuActions::load();
				syncWindows();
			}
			else if( controlID == buttonBackID )
			{
				ReplayMenuActions::back();
			}
			else if( controlID == buttonDeleteID )
			{
				ReplayMenuActions::remove();
			}
			else if( controlID == buttonCopyID )
			{
				ReplayMenuActions::copy();
			}
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
