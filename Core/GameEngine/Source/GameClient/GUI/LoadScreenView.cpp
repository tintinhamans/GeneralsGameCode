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
#include "GameClient/LoadScreenView.h"

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
