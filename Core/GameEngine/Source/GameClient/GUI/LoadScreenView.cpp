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

// FILE: LoadScreenView.cpp ///////////////////////////////////////////////////
// Applies LoadScreenData to the gadgets of the .wnd load screens.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/NameKeyGenerator.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "Common/UnicodeString.h"
#include "GameClient/LoadScreenView.h"
#include "GameClient/MapUtil.h"

// Row colors are stored without alpha; the gadgets want it opaque.
static Color opaqueColor( UnsignedInt rgb )
{
	return 0xFF000000 | (rgb & 0xFFFFFF);
}

void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );

static const Image *findImage( const AsciiString &name )
{
	return name.isEmpty() ? nullptr : TheMappedImageCollection->findImageByName( name );
}

// House colored bar: image on Zero Hour, bar color on the original.
static void applyHouseBar( GameWindow *bar, const LoadScreenPlayerRow &row )
{
#if RTS_GENERALS
	GadgetProgressBarSetEnabledBarColor(bar, opaqueColor( row.m_color ) );
#else
	AsciiString imageName;
	imageName.format("LoadingBar_ProgressCenter%d", row.m_colorIndex);
	const Image *houseImage = TheMappedImageCollection->findImageByName(imageName);
	if (! houseImage)
		houseImage = TheMappedImageCollection->findImageByName("LoadingBar_Progress");
	bar->winSetEnabledImage( 6, houseImage );
#endif
}

// RosterLoadScreenView ///////////////////////////////////////////////////////
RosterLoadScreenView::RosterLoadScreenView( const char *wndName ) : m_wndName( wndName )
{
	m_mapPreview = nullptr;
	m_portraitLocalGeneral = nullptr;
	m_featuresLocalGeneral = nullptr;
	m_nameLocalGeneral = nullptr;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i] = nullptr;
		m_buttonMapStartPosition[i] = nullptr;
		m_appliedProgress[i] = 0;
	}
}

RosterLoadScreenView::~RosterLoadScreenView()
{
	if(m_mapPreview)
	{
		m_mapPreview->winSetUserData(nullptr);
	}
}

void RosterLoadScreenView::bindCommon( GameWindow *root )
{
	AsciiString winName;
	winName.format( "%s:WinMapPreview", m_wndName.str() );
	m_mapPreview = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
}

void RosterLoadScreenView::applyCommon( GameWindow *root, GameInfo *game, const LoadScreenData &data )
{
#if RTS_GENERALS
	if (const Image *loadScreenImage = findImage( data.m_backgroundImage ))
		root->winSetEnabledImage(0, loadScreenImage);
#else
	// portrait, features, and name of the local player's general
	AsciiString winName;
	winName.format( "%s:LocalGeneralPortrait", m_wndName.str() );
	m_portraitLocalGeneral = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
	m_portraitLocalGeneral->winSetEnabledImage( 0, findImage( data.m_localPortrait ));
	winName.format( "%s:LocalGeneralFeatures", m_wndName.str() );
	m_featuresLocalGeneral = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
	GadgetStaticTextSetText( m_featuresLocalGeneral, data.m_localFeatures );
	winName.format( "%s:LocalGeneralName", m_wndName.str() );
	m_nameLocalGeneral = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
	GadgetStaticTextSetText( m_nameLocalGeneral, data.m_localName );
#endif
}

void RosterLoadScreenView::applyMapPreview( GameInfo *game, const LoadScreenData &data )
{
	if(m_mapPreview)
	{
		const MapMetaData *mmd = TheMapCache->findMap(data.m_mapName);
		Image *image = getMapPreviewImage(data.m_mapName);
		m_mapPreview->winSetUserData((void *)mmd);

		positionStartSpots( game, m_buttonMapStartPosition, m_mapPreview);
		updateMapStartSpots( game, m_buttonMapStartPosition, TRUE );
		if(image)
		{
			m_mapPreview->winSetStatus(WIN_STATUS_IMAGE);
			m_mapPreview->winSetEnabledImage(0, image);
		}
		else
		{
			m_mapPreview->winClearStatus(WIN_STATUS_IMAGE);
		}
	}
}

void RosterLoadScreenView::update( const LoadScreenData &data )
{
	for (Int i = 0; i < data.m_rowCount; ++i)
	{
		if (m_appliedProgress[i] != data.m_rows[i].m_progress)
		{
			m_appliedProgress[i] = data.m_rows[i].m_progress;
			if (m_progressBars[i])
				GadgetProgressBarSetProgress(m_progressBars[i], data.m_rows[i].m_progress );
		}
	}
}

void RosterLoadScreenView::reset()
{
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i] = nullptr;
	}
}

// MultiPlayerLoadScreenView //////////////////////////////////////////////////
MultiPlayerLoadScreenView::MultiPlayerLoadScreenView() : RosterLoadScreenView( "MultiplayerLoadScreen.wnd" )
{
}

void MultiPlayerLoadScreenView::init( GameWindow *root, GameInfo *game, const LoadScreenData &data )
{
	root->winHide(FALSE);
	root->winBringToTop();
	bindCommon( root );
	applyCommon( root, game, data );

	GameWindow *teamWin[MAX_SLOTS];
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString winName;
		winName.format( "MultiplayerLoadScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the Multiplayer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		winName.format( "MultiplayerLoadScreen.wnd:ButtonMapStartPosition%d",i);
		m_buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_buttonMapStartPosition[i], ("Can't initialize the MapStart Positions for the MultiplayerLoadScreen loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the Multiplayer loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextSide%d",i);
		m_playerSide[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerSide[i], ("Can't initialize the Sides for the Multiplayer loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextTeam%d",i);
		teamWin[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));

		m_appliedProgress[i] = 0;

		if (i >= data.m_rowCount)
		{
			m_progressBars[i]->winHide(TRUE);
			m_playerNames[i]->winHide(TRUE);
			m_playerSide[i]->winHide(TRUE);
			if (teamWin[i])
				teamWin[i]->winHide(TRUE);
			continue;
		}

		const LoadScreenPlayerRow &row = data.m_rows[i];
		Color houseColor = opaqueColor( row.m_color );
		applyHouseBar( m_progressBars[i], row );

		GadgetStaticTextSetText(m_playerNames[i], row.m_name );
		m_playerNames[i]->winSetEnabledTextColors(houseColor, m_playerNames[i]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_playerSide[i], row.m_side );
		m_playerSide[i]->winSetEnabledTextColors(houseColor, m_playerSide[i]->winGetEnabledTextBorderColor());

		if (!row.m_showProgress)
			m_progressBars[i]->winHide(TRUE);

		if (teamWin[i])
		{
			GadgetStaticTextSetText(teamWin[i], row.m_team);
			teamWin[i]->winSetEnabledTextColors(houseColor, m_playerNames[i]->winGetEnabledTextBorderColor());
		}
	}

	applyMapPreview( game, data );
}

// GameSpyLoadScreenView //////////////////////////////////////////////////////
GameSpyLoadScreenView::GameSpyLoadScreenView() : RosterLoadScreenView( "GameSpyLoadScreen.wnd" )
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerWin[i] = nullptr;
		m_playerTotalDisconnects[i] = nullptr;
		m_playerWinLosses[i] = nullptr;
		m_playerRank[i] = nullptr;
		m_playerOfficerMedal[i] = nullptr;
	}
}

void GameSpyLoadScreenView::init( GameWindow *root, GameInfo *game, const LoadScreenData &data )
{
	root->winHide(FALSE);
	root->winBringToTop();
	bindCommon( root );
	applyCommon( root, game, data );

	GameWindow *teamWin[MAX_SLOTS];
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString winName;
		winName.format( "GameSpyLoadScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the GameSpyLoadScreen loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		winName.format( "GameSpyLoadScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:ButtonMapStartPosition%d",i);
		m_buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_buttonMapStartPosition[i], ("Can't initialize the MapStart Positions for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:StaticTextSide%d",i);
		m_playerSide[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerSide[i], ("Can't initialize the Sides for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:WinPlayer%d",i);
		m_playerWin[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerWin[i], ("Can't initialize the WinPlayer for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:StaticTextTotalDisconnects%d",i);
		m_playerTotalDisconnects[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerTotalDisconnects[i], ("Can't initialize the m_playerTotalDisconnects for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:StaticTextWinLoss%d",i);
		m_playerWinLosses[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerWinLosses[i], ("Can't initialize the m_playerWinLosses for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:WinRank%d",i);
		m_playerRank[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerRank[i], ("Can't initialize the m_playerRank for the GameSpyLoadScreen loadscreen"));

		winName.format( "GameSpyLoadScreen.wnd:WinOfficer%d",i);
		m_playerOfficerMedal[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerOfficerMedal[i], ("Can't initialize the m_playerOfficerMedal for the GameSpyLoadScreen loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextTeam%d",i);
		teamWin[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));

		m_appliedProgress[i] = 0;

		if (i >= data.m_rowCount)
		{
			m_playerWin[i]->winHide(TRUE);
			continue;
		}

		const LoadScreenPlayerRow &row = data.m_rows[i];
		Color houseColor = opaqueColor( row.m_color );
		applyHouseBar( m_progressBars[i], row );

		GadgetStaticTextSetText(m_playerNames[i], row.m_name );
		m_playerNames[i]->winSetEnabledTextColors(houseColor, m_playerNames[i]->winGetEnabledTextBorderColor());

		m_playerOfficerMedal[i]->winSetEnabledImage(0, findImage( row.m_medalImage ));
		m_playerRank[i]->winSetEnabledImage(0, findImage( row.m_rankImage ));

		GadgetStaticTextSetText(m_playerWinLosses[i], row.m_winLoss);
		m_playerWinLosses[i]->winSetEnabledTextColors(houseColor, m_playerWinLosses[i]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_playerTotalDisconnects[i], row.m_disconnects);
		m_playerTotalDisconnects[i]->winSetEnabledTextColors(houseColor, m_playerTotalDisconnects[i]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_playerSide[i], row.m_side);
		m_playerSide[i]->winSetEnabledTextColors(houseColor, m_playerSide[i]->winGetEnabledTextBorderColor());

		if (!row.m_showProgress)
			m_progressBars[i]->winHide(TRUE);
		if (!row.m_showStats)
		{
			m_playerTotalDisconnects[i]->winHide(TRUE);
			m_playerWinLosses[i]->winHide(TRUE);
			m_playerRank[i]->winHide(TRUE);
			m_playerOfficerMedal[i]->winHide(TRUE);
		}

		if (teamWin[i])
		{
			GadgetStaticTextSetText(teamWin[i], row.m_team);
			teamWin[i]->winSetEnabledTextColors(houseColor, m_playerNames[i]->winGetEnabledTextBorderColor());
		}
	}

	applyMapPreview( game, data );
}

// MapTransferLoadScreenView //////////////////////////////////////////////////
MapTransferLoadScreenView::MapTransferLoadScreenView()
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_progressText[i] = nullptr;
		m_appliedProgress[i] = 0;
	}
	m_fileNameText = nullptr;
	m_timeoutText = nullptr;
}

void MapTransferLoadScreenView::init( GameWindow *root, GameInfo *game, const LoadScreenData &data )
{
	root->winHide(FALSE);
	root->winBringToTop();

	AsciiString winName;

	// Load the Filename Text
	m_fileNameText = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( "MapTransferScreen.wnd:StaticTextCurrentFile" ));
	DEBUG_ASSERTCRASH(m_fileNameText, ("Can't initialize the filename for the map transfer loadscreen"));

	// Load the Timeout Text
	m_timeoutText = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( "MapTransferScreen.wnd:StaticTextTimeout" ));
	DEBUG_ASSERTCRASH(m_timeoutText, ("Can't initialize the timeout for the map transfer loadscreen"));

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		winName.format( "MapTransferScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the map transfer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load the Player's name
		winName.format( "MapTransferScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the map transfer loadscreen"));

		// Load the Progress Text
		winName.format( "MapTransferScreen.wnd:StaticTextProgress%d",i);
		m_progressText[i] = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressText[i], ("Can't initialize the progress text for the map transfer loadscreen"));

		m_appliedProgress[i] = 0;

		if (i >= data.m_rowCount)
		{
			m_progressBars[i]->winHide(TRUE);
			m_playerNames[i]->winHide(TRUE);
			m_progressText[i]->winHide(TRUE);
			continue;
		}

		const LoadScreenPlayerRow &row = data.m_rows[i];
		Color houseColor = opaqueColor( row.m_color );
		GadgetProgressBarSetEnabledBarColor(m_progressBars[i], houseColor );

		GadgetStaticTextSetText(m_playerNames[i], row.m_name );
		m_playerNames[i]->winSetEnabledTextColors(houseColor, m_playerNames[i]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_progressText[i], UnicodeString::TheEmptyString );
		m_progressText[i]->winSetEnabledTextColors(houseColor, m_progressText[i]->winGetEnabledTextBorderColor());

		if (!row.m_showProgress)
			m_progressBars[i]->winHide(TRUE);
	}
}

void MapTransferLoadScreenView::update( const LoadScreenData &data )
{
	for (Int i = 0; i < data.m_rowCount; ++i)
	{
		const LoadScreenPlayerRow &row = data.m_rows[i];
		if (m_appliedProgress[i] != row.m_progress)
		{
			m_appliedProgress[i] = row.m_progress;
			if (m_progressBars[i])
				GadgetProgressBarSetProgress(m_progressBars[i], row.m_progress );
		}
		if (m_appliedStatus[i] != row.m_status)
		{
			m_appliedStatus[i] = row.m_status;
			if (m_progressText[i])
				GadgetStaticTextSetText(m_progressText[i], row.m_status);
		}
	}
	if (m_appliedFile != data.m_currentFile)
	{
		m_appliedFile = data.m_currentFile;
		if (m_fileNameText)
			GadgetStaticTextSetText(m_fileNameText, data.m_currentFile);
	}
	if (m_appliedTimeout != data.m_timeout)
	{
		m_appliedTimeout = data.m_timeout;
		if (m_timeoutText)
			GadgetStaticTextSetText(m_timeoutText, data.m_timeout);
	}
}

void MapTransferLoadScreenView::reset()
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_progressText[i] = nullptr;
	}
	m_fileNameText = nullptr;
	m_timeoutText = nullptr;
}

// ShellLoadScreenView ////////////////////////////////////////////////////////
ShellLoadScreenView::ShellLoadScreenView()
{
	m_progressBar = nullptr;
}

void ShellLoadScreenView::init( GameWindow *root, GameInfo *game, const LoadScreenData &data )
{
	root->winHide(FALSE);
	root->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( "ShellGameLoadScreen.wnd:ProgressLoad" ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );
	m_progressBar->winHide(TRUE);

	if (data.m_titleScreen)
	{
		root->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("TitleScreen"));

		GameWindow *win = TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( "ShellGameLoadScreen.wnd:StaticTextLegal" ));
		if(win)
			win->winHide(FALSE);
	}
	m_progressBar->winHide(FALSE);
}

void ShellLoadScreenView::update( const LoadScreenData &data )
{
	if (m_progressBar)
		GadgetProgressBarSetProgress(m_progressBar, data.m_progress);
}

void ShellLoadScreenView::reset()
{
	m_progressBar = nullptr;
}
