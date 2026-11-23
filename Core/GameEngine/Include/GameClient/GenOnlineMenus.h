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

///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GameWindow.h"

class WindowLayout;
class PSPlayerStats;
class Image;
struct StatsLobbyScreen;

extern void GenOnlinePopupJoinGameInit( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePopupJoinGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupJoinGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlinePopupHostGameInit( WindowLayout *layout, void *userData );
extern void GenOnlinePopupHostGameUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePopupHostGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupHostGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineMapSelectMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineMapSelectMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineMapSelectMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineMapSelectMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineMapSelectMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineDownloadMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineDownloadMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineDownloadMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineDownloadMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineDownloadMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineWelcomeMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineWelcomeMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineWelcomeMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineWelcomeMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineWelcomeMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineLoginMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineLoginMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineLoginMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineLoginMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineLoginMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineBuddyOverlayInit( WindowLayout *layout, void *userData );
extern void GenOnlineBuddyOverlayShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineBuddyOverlayUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineBuddyOverlayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineBuddyOverlaySystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePopupBuddyNotificationSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern void GenOnlineBuddyOverlayRCMenuInit( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineBuddyOverlayRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern void GenOnlineMessageRCMenuInit( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineMessageRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
/// Selects the row of a chat message and opens its context menu at the mouse. Nothing opens for a line without a stored message.
extern void GenOnlineOpenMessageMenu( GameWindow *listbox, Int row, Int mouseX, Int mouseY, const char *messageID, int64_t authorID, const char *authorName );
extern void GenOnlineCloseRightClickMenus();

extern void GenOnlinePlayerInfoOverlayInit( WindowLayout *layout, void *userData );
extern void GenOnlinePlayerInfoOverlayShutdown( WindowLayout *layout, void *userData );
extern void GenOnlinePlayerInfoOverlayUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlinePlayerInfoOverlayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlinePlayerInfoOverlaySystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineQuickMatchMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineQuickMatchMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineQuickMatchMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineQuickMatchMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineQuickMatchMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineLobbyMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineLobbyMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineLobbyMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineLobbyMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineLobbyMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

extern void GenOnlineGameSetupMenuInit( WindowLayout *layout, void *userData );
extern void GenOnlineGameSetupMenuShutdown( WindowLayout *layout, void *userData );
extern void GenOnlineGameSetupMenuUpdate( WindowLayout *layout, void *userData );
extern WindowMsgHandledType GenOnlineGameSetupMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern WindowMsgHandledType GenOnlineGameSetupMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

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

extern void GenOnlineOptionsMenuInit( Bool locked );
extern Bool GenOnlineOptionsMenuComboSelected( GameWindow *control );
extern Bool GenOnlineOptionsMenuButtonSelected( GameWindow *control );
extern Bool GenOnlineOptionsMenuAccept();
extern void GenOnlineOptionsMenuDefaults();

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
extern void GenOnlineRefreshIgnoreList();
// The tabs of the communicator. The peer is the friend whose conversation opens, -1 opens none.
enum GenOnlineCommunicatorTab { GENONLINE_COMMUNICATOR_FRIENDS, GENONLINE_COMMUNICATOR_REQUESTS, GENONLINE_COMMUNICATOR_BLOCKED };
extern void GenOnlineOpenCommunicator( Int tab, int64_t peerUserID );
// Draws the presence dot of the user section: online, idle, do_not_disturb, invisible or offline.
extern void GenOnlineDrawPresenceDot( const char *status, Int x, Int y, Int size );
extern Bool GenOnlineLobbyMenuIsActive();
// Joins the game of a lobby invite once the game list shows it, without its password.
extern void GenOnlineLobbyMenuJoinInvite( int64_t lobbyID );
extern Bool GenOnlineWelcomeMenuOpenLobby();
extern void GenOnlineRaiseNotificationBox();
extern void GenOnlineDeleteNotificationBox();
extern void GenOnlineInitBuddyControls( Int type );
extern void GenOnlinePopulateOldBuddyMessages();
extern WindowMsgHandledType GenOnlineBuddyControlSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );
extern void GenOnlinePopBackToLobby();
extern void GenOnlinePositionStartSpots();
extern void GenOnlineDisplayGameOptions();
extern void GenOnlineDisplaySlotList();
extern void GenOnlineSetLobbyAttemptHostJoin( Bool start );
extern void GenOnlinePopulateLobbyPlayerListbox();
extern const Image *GenOnlineLookupSmallRankImage( Int side, Int rankPoints );
extern void GenOnlineHandleNetSlashCommand( AsciiString subcommand, AsciiString value, GameWindow *listbox );
extern void OnKickedFromLobby();

extern Bool GenOnlineGameSetupMenuSubmitChatLine( UnicodeString text );
extern Bool GenOnlineLobbyMenuSubmitChatLine( UnicodeString text );
/// Describes the game setup controls for the stats readers lobby control. FALSE when the screen is not open.
extern Bool GenOnlineGameSetupMenuStatsLobby( StatsLobbyScreen& out );

extern GameWindow *GenOnlineQuickMatchTextWindow;	///< Text box on the quick match screen
extern GameWindow *GenOnlineListboxGameSetupChat;	///< Chat box on the game setup screen
extern WindowLayout *GenOnlineMapSelectLayout;		///< Map selection overlay
