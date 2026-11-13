/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: GenOnlineMenus.h /////////////////////////////////////////////////////
// Description: Window callbacks and shared functions of the GenOnline menus.
//              The GenOnline layouts name these callbacks. The GameSpy menus
//              keep their own callbacks in GUICallbacks.h.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GameWindow.h"

class WindowLayout;

// GenOnlinePopupJoinGame
extern void GenOnlinePopupJoinGameInit( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePopupJoinGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupJoinGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlinePopupHostGame
extern void GenOnlinePopupHostGameInit( WindowLayout *layout, void *userData );
extern void GenOnlinePopupHostGameUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePopupHostGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupHostGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlineMapSelectMenu
extern void GenOnlineMapSelectMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineMapSelectMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineMapSelectMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineMapSelectMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineMapSelectMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlineDownloadMenu
extern void GenOnlineDownloadMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineDownloadMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineDownloadMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineDownloadMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineDownloadMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlineWelcomeMenu
extern void GenOnlineWelcomeMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineWelcomeMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineWelcomeMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineWelcomeMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineWelcomeMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// Shared functions of the GenOnline menus
extern void GenOnlineMapSelectMenuApplyMap( const AsciiString& map );
extern void GenOnlineHandleNumPlayersOnline( Int numPlayersOnline );
extern void GenOnlineHandleOverallStats( const char *szHTTPStats, unsigned len );
extern void GenOnlineUpdateLocalPlayerStats();
