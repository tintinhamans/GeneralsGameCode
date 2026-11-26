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

// FILE: LanMapSelectMenu.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/NameKeyGenerator.h"
#include "Common/MessageStream.h"
#include "Common/UserPreferences.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GUI/GUICallbacks/Menus/LanGameSetupActions.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameClient/MapUtil.h"
#if defined(GENONLINE)
#include "GameClient/MapSelectList.h"
#endif
#include "GameNetwork/GUIUtil.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBack = NAMEKEY_INVALID;
static NameKeyType buttonOK = NAMEKEY_INVALID;
static NameKeyType listboxMap = NAMEKEY_INVALID;
static NameKeyType winMapPreviewID = NAMEKEY_INVALID;
static GameWindow *parent = nullptr;
static GameWindow *mapList = nullptr;
static GameWindow *winMapPreview = nullptr;
#if defined(GENONLINE)
// GenOnline @feature arcticdolphin 08/11/2026 Search, filters and favourites replace the system and user map tabs.
static MapSelectList mapSelectList;
static MapSelectFilter mapSelectFilter;
#else
static NameKeyType radioButtonSystemMapsID = NAMEKEY_INVALID;
static NameKeyType radioButtonUserMapsID = NAMEKEY_INVALID;
#endif

static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};
static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };


// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
static const char *layoutFilename = "LanGameOptionsMenu.wnd";
static const char *parentName = "LanGameOptionsMenuParent";
#if defined(GENONLINE)
// GenOnline @tweak arcticdolphin 08/11/2026 Rows stay visible behind the dialog and only the preview hides, as it shares the marker list.
#endif
static const char *gadgetsToHide[] =
{
	"MapWindow",
	//"StaticTextTitle",
#if !defined(GENONLINE)
	"StaticTextTeam",
	"StaticTextFaction",
	"StaticTextColor",
	"TextEntryMapDisplay",
	"ButtonSelectMap",
	"ButtonStart",
#endif
	"StaticTextMapPreview",

	nullptr
};
static const char *perPlayerGadgetsToHide[] =
{
#if !defined(GENONLINE)
	"ComboBoxTeam",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
#endif
	nullptr
};

static void showLANGameOptionsUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	GameWindow *win	= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey("LanGameOptionsMenu.wnd:ButtonBack") );
	if(win)
		win->winEnable( show );

}

static void NullifyControls()
{
	parent = nullptr;
	mapList = nullptr;
#if defined(GENONLINE)
	mapSelectList.shutdown();
#endif
	if (winMapPreview)
	{
		winMapPreview->winSetUserData(nullptr);
		winMapPreview = nullptr;
	}
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuInit( WindowLayout *layout, void *userData )
{
	showLANGameOptionsUnderlyingGUIElements(FALSE);

	// set keyboard focus to main parent
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:LanMapSelectMenuParent" );
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );

	TheWindowManager->winSetFocus( parent );

#if !defined(GENONLINE)
	LANPreferences pref;
	Bool usesSystemMapDir = pref.usesSystemMapDir();

	const MapMetaData *mmd = TheMapCache->findMap(TheLAN->GetMyGame()->getMap());
	if (mmd)
	{
		usesSystemMapDir = mmd->m_isOfficial;
	}


#endif
	buttonBack = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:ButtonBack" );
	buttonOK = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:ButtonOK" );
	listboxMap = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:ListboxMap" );
	winMapPreviewID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:WinMapPreview" );

#if !defined(GENONLINE)
	radioButtonSystemMapsID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:RadioButtonSystemMaps" );
	radioButtonUserMapsID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:RadioButtonUserMaps" );
	GameWindow *radioButtonSystemMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonSystemMapsID );
	GameWindow *radioButtonUserMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonUserMapsID );
#endif
	winMapPreview = TheWindowManager->winGetWindowFromId(parent, winMapPreviewID);
#if !defined(GENONLINE)
	if (usesSystemMapDir)
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
	else
		GadgetRadioSetSelection( radioButtonUserMaps, FALSE );
#endif

	AsciiString tmpString;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		tmpString.format("LanMapSelectMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( winMapPreview, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
		buttonMapStartPosition[i]->winHide(TRUE);
		buttonMapStartPosition[i]->winEnable(FALSE);
	}

	// get the listbox window
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:ListboxMap" );
	mapList = TheWindowManager->winGetWindowFromId( parent, mapListID );
	if( mapList )
	{
		if (TheMapCache)
			TheMapCache->updateCache();
#if defined(GENONLINE)
		mapSelectList.init( parent, "LanMapSelectMenu.wnd", MapSelectOptions(), &mapSelectFilter, TheLAN->GetMyGame()->getMap() );
#else
		populateMapListbox( mapList, usesSystemMapDir, TRUE, TheLAN->GetMyGame()->getMap() );
#endif
	}
}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuShutdown( WindowLayout *layout, void *userData )
{

	// hide menu
	layout->hide( TRUE );

	NullifyControls();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

}

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanMapSelectMenuInput( GameWindow *window, UnsignedInt msg,
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
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:ButtonBack" );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;

}

#if defined(GENONLINE)
// The effect of the OK path on the lobby: the map, its checksum and size, and the start spots and slots adjusting to it.
static void applySelectedMap( AsciiString asciiMap )
{
	TheLAN->GetMyGame()->setMap( asciiMap );
	asciiMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
	if (it != TheMapCache->end())
	{
		TheLAN->GetMyGame()->getSlot(0)->setMapAvailability(true);
		TheLAN->GetMyGame()->setMapCRC( it->second.m_CRC );
		TheLAN->GetMyGame()->setMapSize( it->second.m_filesize );

		TheLAN->GetMyGame()->resetStartSpots();
		TheLAN->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
	}
}

// The same without the selector window, for the stats pipe.
void LanMapSelectMenuApplyMap( const AsciiString& map )
{
	applySelectedMap( map );
	PostToLanGameOptions( SEND_GAME_OPTS );
}

#endif
//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanMapSelectMenuSystem( GameWindow *window, UnsignedInt msg,
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	GameWindow *mapWindow = nullptr;
	if (listboxMap != NAMEKEY_INVALID)
	{
		mapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );
	}

#if defined(GENONLINE)
	const MapSelectMsgResult filterResult = mapSelectList.handleSystemMessage( msg, mData1, mData2 );
	if (filterResult == MAP_SELECT_MSG_SOURCE_CHANGED)
	{
		// the single player map menu shares this preference
		const MapSelectSource source = mapSelectFilter.m_source;
		if (source == MAP_SOURCE_OFFICIAL || source == MAP_SOURCE_USER)
		{
			LANPreferences pref;
			pref["UseSystemMapDir"] = (source == MAP_SOURCE_OFFICIAL) ? "yes" : "no";
			pref.write();
		}
	}
	if (filterResult != MAP_SELECT_MSG_IGNORED)
		return MSG_HANDLED;

#endif
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
			NullifyControls();

			break;

		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}

		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap )
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonOK );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonOK );
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

#if defined(GENONLINE)
			if ( controlID == buttonBack )
#else
			if ( controlID == radioButtonSystemMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, TRUE, TRUE, TheLAN->GetMyGame()->getMap() );
				LANPreferences pref;
				pref["UseSystemMapDir"] = "yes";
				pref.write();
			}
			else if ( controlID == radioButtonUserMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, FALSE, TRUE, TheLAN->GetMyGame()->getMap() );
				LANPreferences pref;
				pref["UseSystemMapDir"] = "no";
				pref.write();
			}
			else if ( controlID == buttonBack )
#endif
			{

				if (mapSelectLayout)
				{
					mapSelectLayout->destroyWindows();
					deleteInstance(mapSelectLayout);
					mapSelectLayout = nullptr;
				}

				// set the controls to nullptr since they've been destroyed.
				NullifyControls();
				showLANGameOptionsUnderlyingGUIElements(TRUE);
				PostToLanGameOptions( MAP_BACK );
			}
			else if ( controlID == buttonOK )
			{
				Int selected = -1;
				UnicodeString map;

				// get the selected index
				if (mapWindow != nullptr)
				{
					GadgetListBoxGetSelected( mapWindow, &selected );
				}

				if( selected != -1 )
				{
					// get text of the map to load
					map = GadgetListBoxGetText( mapWindow, selected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( mapWindow, selected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					LanGameSetupActions::applySelectedMap( TheLAN->GetMyGame(), asciiMap );

					if (mapSelectLayout)
					{
						mapSelectLayout->destroyWindows();
						deleteInstance(mapSelectLayout);
						mapSelectLayout = nullptr;
					}

					// set the controls to nullptr since they've been destroyed.
					NullifyControls();

					showLANGameOptionsUnderlyingGUIElements(TRUE);

				}
			}

			break;

		}

		case GLM_SELECTED:
			{

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap )
				{
					int rowSelected = mData2;
					if( rowSelected < 0 )
					{
						positionStartSpots( AsciiString::TheEmptyString, buttonMapStartPosition, winMapPreview);
#if defined(GENONLINE)
						mapSelectList.refreshLegend( AsciiString::TheEmptyString );
#endif
//						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						break;
					}
					winMapPreview->winSetStatus(WIN_STATUS_IMAGE);
					UnicodeString map;
					// get text of the map to load
					map = GadgetListBoxGetText( mapWindow, rowSelected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( mapWindow, rowSelected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					asciiMap.toLower();
					Image *image = getMapPreviewImage(asciiMap);
					winMapPreview->winSetUserData((void *)TheMapCache->findMap(asciiMap));
					if(image)
					{
						winMapPreview->winSetEnabledImage(0, image);
					}
					else
					{
						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
					}
					positionStartSpots( asciiMap, buttonMapStartPosition, winMapPreview);
#if defined(GENONLINE)
					mapSelectList.refreshLegend( asciiMap );
#endif
				}
				break;
			}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
