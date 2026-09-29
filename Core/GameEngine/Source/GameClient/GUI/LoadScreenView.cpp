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

// Row colors are stored without alpha; the gadgets want it opaque.
static Color opaqueColor( UnsignedInt rgb )
{
	return 0xFF000000 | (rgb & 0xFFFFFF);
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
