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

// Author: Matt Campbell, December 2001
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineMenus.h"

#include "Common/CustomMatchPreferences.h"
#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MapSelectList.h"
#include "GameNetwork/GUIUtil.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBack = NAMEKEY_INVALID;
static NameKeyType buttonOK = NAMEKEY_INVALID;
static NameKeyType listboxMap = NAMEKEY_INVALID;
static GameWindow *parent = nullptr;
static Bool raiseMessageBoxes = FALSE;
static GameWindow *winMapPreview = nullptr;
static NameKeyType winMapPreviewID = NAMEKEY_INVALID;

// GenOnline @feature arcticdolphin 08/11/2026 Search, filters and favourites replace the system and user map tabs.
static MapSelectList mapSelectList;
static MapSelectFilter mapSelectFilter;

extern WindowLayout *GenOnlineMapSelectLayout;			///< Map selection overlay
static GameWindow *mapList = nullptr;

static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};
static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static GameWindow *winMapWindow = nullptr;

#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineGame.h"
extern GenOnlineGame* TheGenOnlineGame;

static void NullifyControls(void)
{
	parent = nullptr;
	mapList = nullptr;
	mapSelectList.shutdown();
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

static const char *layoutFilename = "GameSpyGameOptionsMenu.wnd";
static const char *parentName = "GameSpyGameOptionsMenuParent";
// GenOnline @tweak arcticdolphin 08/11/2026 Rows stay visible behind the dialog. Only the preview hides, because it shares the marker list.
static const char *gadgetsToHide[] =
{
	"MapWindow",
	//"StaticTextGameName",
	"StaticTextMapPreview",

	nullptr
};
static const char *perPlayerGadgetsToHide[] =
{
	//"ButtonStartPosition",
	nullptr
};
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);

static void showGameSpyGameOptionsUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	GameWindow *win	= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:ButtonBack") );
	if(win)
		win->winEnable( show );
}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////


//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void GenOnlineMapSelectMenuInit( WindowLayout *layout, void *userData )
{

	// set keyboard focus to main parent
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:WOLMapSelectMenuParent" );
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );

	TheWindowManager->winSetFocus( parent );

	winMapPreviewID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:WinMapPreview" );
	winMapPreview = TheWindowManager->winGetWindowFromId(parent, winMapPreviewID);

	MapSelectOptions options;
#if !defined(GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	bool bUseStats = TheGenOnlineGame->getUseStats();
	//if stats are enabled, only official maps can be used
	if(bUseStats)
		options.m_officialOnly = TRUE;
#endif

	buttonBack = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonBack" );
	buttonOK = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonOK" );
	listboxMap = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
	winMapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );
	AsciiString tmpString;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		tmpString.format("WOLMapSelectMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( winMapPreview, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
		buttonMapStartPosition[i]->winHide(TRUE);
		buttonMapStartPosition[i]->winEnable(FALSE);
	}

	raiseMessageBoxes = TRUE;
	showGameSpyGameOptionsUnderlyingGUIElements( FALSE );

	// get the listbox window
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
	mapList = TheWindowManager->winGetWindowFromId( parent, mapListID );

	// GENONLINE_CHANGE: Show all the maps in the folder, not just the first 1200. This change could be made in the .WND instead, but code is cleaner.
	// GENONLINE_BACKPORT: Port this to SH once verified
	GadgetListBoxSetListLength(mapList, TheMapCache->size());
	
	if( mapList )
	{
		if (TheMapCache)
			TheMapCache->updateCache();
		mapSelectList.init( parent, "WOLMapSelectMenu.wnd", options, &mapSelectFilter, TheGenOnlineGame->getMap() );
	}

}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlineMapSelectMenuShutdown( WindowLayout *layout, void *userData )
{
	NullifyControls();

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void GenOnlineMapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

	// No update because the game setup screen is up at the same
	// time and it does the update for us...
}

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineMapSelectMenuInput( GameWindow *window, UnsignedInt msg,
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
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonBack" );
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
void GenOnlinePositionStartSpots();

static void applySelectedMap( AsciiString asciiMap )
{
	int newMaxPlayers = -1;
	UnicodeString strMapName;
	bool bOfficialMap = false;

	TheGenOnlineGame->setMap(asciiMap);
	asciiMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
	if (it != TheMapCache->end())
	{
		TheGenOnlineGame->getGameSpySlot(0)->setMapAvailability(TRUE);
		TheGenOnlineGame->setMapCRC( it->second.m_CRC );
		TheGenOnlineGame->setMapSize( it->second.m_filesize );

		newMaxPlayers = it->second.m_numPlayers;
		strMapName = it->second.m_displayName;

		bOfficialMap = it->second.m_isOfficial;
	}

	TheGenOnlineGame->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
	TheGenOnlineGame->resetAccepted();
	TheGenOnlineGame->resetStartSpots();

	// TODO_GENONLINE:
	//TheGameSpyInfo->setGameOptions();

	GenOnlineDisplaySlotList();
	GenOnlineDisplayGameOptions();

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->UpdateCurrentLobby_Map(strMapName, TheGenOnlineGame->getMap(), bOfficialMap, newMaxPlayers);
	}
}

// The same without the selector window, for the stats pipe.
void GenOnlineMapSelectMenuApplyMap( const AsciiString& map )
{
	applySelectedMap( map );
	GenOnlinePositionStartSpots();
}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineMapSelectMenuSystem( GameWindow *window, UnsignedInt msg,
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	const MapSelectMsgResult filterResult = mapSelectList.handleSystemMessage( msg, mData1, mData2 );
	if (filterResult == MAP_SELECT_MSG_SOURCE_CHANGED)
	{
		const MapSelectSource source = mapSelectFilter.m_source;
		if (source == MAP_SOURCE_OFFICIAL || source == MAP_SOURCE_USER)
		{
			CustomMatchPreferences pref;
			pref.setUsesSystemMapDir(source == MAP_SOURCE_OFFICIAL);
			pref.write();
		}
	}
	if (filterResult != MAP_SELECT_MSG_IGNORED)
		return MSG_HANDLED;

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
						mapSelectList.refreshLegend( AsciiString::TheEmptyString );
//						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						break;
					}
					winMapPreview->winSetStatus(WIN_STATUS_IMAGE);
					UnicodeString map;
					// get text of the map to load
					map = GadgetListBoxGetText( winMapWindow, rowSelected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( winMapWindow, rowSelected );
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
					mapSelectList.refreshLegend( asciiMap );
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == buttonBack )
			{
				showGameSpyGameOptionsUnderlyingGUIElements( TRUE );

				if (GenOnlineMapSelectLayout)
				{
					GenOnlineMapSelectLayout->destroyWindows();
					deleteInstance(GenOnlineMapSelectLayout);
					GenOnlineMapSelectLayout = nullptr;
				}

				GenOnlinePositionStartSpots();
			}
			else if( controlID == buttonOK )
			{
				Int selected;
				UnicodeString map;

				// get the selected index
				GadgetListBoxGetSelected( winMapWindow, &selected );

				if( selected != -1 )
				{

					// get text of the map to load
					map = GadgetListBoxGetText( winMapWindow, selected, 0 );


					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( winMapWindow, selected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );

					applySelectedMap( asciiMap );

					if (GenOnlineMapSelectLayout)
					{
						GenOnlineMapSelectLayout->destroyWindows();
						deleteInstance(GenOnlineMapSelectLayout);
						GenOnlineMapSelectLayout = nullptr;
					}

					showGameSpyGameOptionsUnderlyingGUIElements( TRUE );

					GenOnlinePositionStartSpots();

				}

			}

			break;

		}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
