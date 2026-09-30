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

// FILE: ControlBarActions.cpp ////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/ControlBarActions.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarData.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"

namespace
{
	GameWindow *buttonWindow( const ControlBarButtonId &id )
	{
		return TheControlBar ? TheControlBar->getButtonWindow( id ) : nullptr;
	}

	// what the window manager sends along: the pointer's screen position
	WindowMsgData mousePosition()
	{
		const MouseIO *io = TheMouse ? TheMouse->getMouseStatus() : nullptr;
		return io ? SHORTTOLONG( io->pos.x, io->pos.y ) : 0;
	}

	// the window manager only gives input to a shown, enabled button (GameWindow::winPointInChild())
	Bool takesInput( GameWindow *win )
	{
		return !win->winIsHidden() && BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED );
	}
}

//-------------------------------------------------------------------------------------------------
void ControlBarActions::enter( const ControlBarButtonId &id )
{
	GameWindow *win = buttonWindow( id );
	if( win && takesInput( win ) )
		TheWindowManager->winSendInputMsg( win, GWM_MOUSE_ENTERING, mousePosition(), 0 );
}

void ControlBarActions::leave( const ControlBarButtonId &id )
{
	GameWindow *win = buttonWindow( id );
	if( win )
		TheWindowManager->winSendInputMsg( win, GWM_MOUSE_LEAVING, mousePosition(), 0 );
}

void ControlBarActions::press( const ControlBarButtonId &id, Bool right )
{
	GameWindow *win = buttonWindow( id );
	if( win && takesInput( win ) )
		TheWindowManager->winSendInputMsg( win, right ? GWM_RIGHT_DOWN : GWM_LEFT_DOWN, mousePosition(), 0 );
}

void ControlBarActions::release( const ControlBarButtonId &id, Bool right )
{
	GameWindow *win = buttonWindow( id );
	if( win == nullptr || win->winIsHidden() )
		return;

	if( !takesInput( win ) )
	{
		if( !right && TheAudio )
		{
			AudioEventRTS disabledClick( "GUIClickDisabled" );
			TheAudio->addAudioEvent( &disabledClick );
		}
		return;
	}

	TheWindowManager->winSendInputMsg( win, right ? GWM_RIGHT_UP : GWM_LEFT_UP, mousePosition(), 0 );
}

void ControlBarActions::panelRelease()
{
	GameWinBlockInput( nullptr, GWM_LEFT_UP, mousePosition(), 0 );
}
