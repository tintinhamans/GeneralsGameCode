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
class PSPlayerStats;

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

// GenOnlineLoginMenu
extern void GenOnlineLoginMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineLoginMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineLoginMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineLoginMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineLoginMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlineBuddyOverlay
extern void GenOnlineBuddyOverlayInit( WindowLayout *layout, void *userData );
extern void GenOnlineBuddyOverlayShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineBuddyOverlayUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineBuddyOverlayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineBuddyOverlaySystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupBuddyNotificationSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern void GenOnlineBuddyOverlayRCMenuInit( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineBuddyOverlayRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// GenOnlinePlayerInfoOverlay
extern void GenOnlinePlayerInfoOverlayInit( WindowLayout *layout, void *userData );
extern void GenOnlinePlayerInfoOverlayShutdown( WindowLayout *layout, void *userData );
extern void GenOnlinePlayerInfoOverlayUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePlayerInfoOverlayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePlayerInfoOverlaySystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

// Sign in dialog over the main menu for the whole Online path. Open returns FALSE when its layout is missing.
extern Bool GOSignInDialogOpen( void );
extern Bool GOSignInDialogIsOpen( void );
extern void GOSignInDialogShowChecking( GameWinMsgBoxFunc cancelFunc );
extern void GOSignInDialogShowUpdate( UnicodeString message, GameWinMsgBoxFunc updateFunc, GameWinMsgBoxFunc cancelFunc );
extern void GOSignInDialogShowError( UnicodeString title, UnicodeString message, GameWinMsgBoxFunc closeFunc );
extern void GOSignInDialogShowDownloading( GameWinMsgBoxFunc cancelFunc );
extern void GOSignInDialogShowReady( UnicodeString message, GameWinMsgBoxFunc restartFunc, GameWinMsgBoxFunc cancelFunc );
extern void GOSignInDialogBeginSignIn( void );
extern void GOSignInDialogUpdate( void );
extern void GOSignInDialogDestroy( void );

// Shared functions of the GenOnline menus
extern void GenOnlineMapSelectMenuApplyMap( const AsciiString& map );
extern void GenOnlineHandleNumPlayersOnline( Int numPlayersOnline );
extern void GenOnlineHandleOverallStats( const char *szHTTPStats, unsigned len );
extern void GenOnlineUpdateLocalPlayerStats();
extern void GenOnlineSetLookAtPlayer( int64_t id, UnicodeString nick );
extern void GenOnlinePopulatePlayerInfoWindows( AsciiString parentWindowName );
extern Int GenOnlineGetAdditionalDisconnectsFromUserFile( Int playerID );
extern Int GenOnlineCalculateRank( const PSPlayerStats& stats );
extern Int GenOnlineGetFavoriteSide( const PSPlayerStats& stats );
extern void GenOnlineHandleBuddyResponses();
extern void GenOnlineUpdateBuddyInfo( bool bIsAutoRefresh = false, bool bUseCache = false );
extern void GenOnlineRaiseNotificationBox();
extern void GenOnlineDeleteNotificationBox();
extern void GenOnlineInitBuddyControls( Int type );
extern void GenOnlinePopulateOldBuddyMessages();
extern WindowMsgHandledType GenOnlineBuddyControlSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
