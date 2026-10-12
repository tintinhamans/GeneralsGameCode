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


#pragma once

#include "Lib/BaseType.h"

class AsciiString;
class UnicodeString;
class GameInfo;
class DisplayString;

class IOnlineGameHooks
{
public:
	virtual ~IOnlineGameHooks() {}

	virtual void onGlobalDataLoaded() = 0;		///< Runs right after the global data loads, before the mods and the command line.
	virtual void loadSettings() = 0;			///< Loads the provider settings that the engine reads during init.
	virtual void onEngineInitialized() = 0;		///< Runs at the end of the engine init.
	virtual void onEngineUpdate() = 0;			///< Runs once per engine update after the network update.
	virtual void onEngineShutdown() = 0;		///< Runs while the engine shuts down its subsystems.
	virtual void onEngineExit() = 0;			///< Runs last when the engine is gone.

	// Returns TRUE and the frame rate limit when the provider sets the limit for the running match.
	virtual Bool getMatchFrameRateLimit( Int &outLimit ) = 0;

	virtual GameInfo *getGameInfo() = 0;		///< The game info of the internet match.
	virtual Bool isLoggedIn() = 0;				///< True while the provider has a signed in session.
	virtual void onCRCMismatch( const UnicodeString &details ) = 0;
	virtual Bool shouldLeaveGame() = 0;			///< True when the provider ends the match for the local player.
	virtual void onReplayWritten( const AsciiString &replayPath ) = 0;

	// The synced start holds every player on the load screen until all of them reach the given phase.
	virtual Bool beginSyncedStart( Int phase ) = 0;	///< Returns TRUE when the provider runs this phase.
	virtual Bool isSyncedStartDone( Int phase ) = 0;

	virtual Int getChatMessageLifeSeconds() = 0;	///< Seconds a chat line stays up, or -1 for the standard message time.
	virtual void drawNetworkOverlay( DisplayString *text, Int dropColor ) = 0;
	virtual void releaseNetworkOverlay() = 0;	///< Frees what the overlay keeps between frames.
};

// Returns the hooks of the selected provider. It never returns null.
IOnlineGameHooks *GetOnlineGameHooks();
