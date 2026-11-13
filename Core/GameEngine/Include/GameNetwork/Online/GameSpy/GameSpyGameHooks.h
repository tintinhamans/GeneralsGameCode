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

// FILE: GameSpyGameHooks.h ///////////////////////////////////////////////////
// Description: GameSpy game hooks. GameSpy plays its matches from the staging
//              room and adds nothing to the engine, logic, recorder or UI.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/Online/IOnlineGameHooks.h"

class GameSpyGameHooks : public IOnlineGameHooks
{
public:
	virtual ~GameSpyGameHooks() override {}

	virtual void onGlobalDataLoaded() override {}
	virtual void loadSettings() override {}
	virtual void onDisplayCreated() override {}
	virtual void onEngineInitialized() override {}
	virtual void onEngineUpdate() override {}
	virtual void onEngineShutdown() override {}
	virtual void onEngineExit() override {}

	virtual Bool getMatchFrameRateLimit( Int &outLimit ) override;

	virtual GameInfo *getGameInfo() override;
	virtual Bool isLoggedIn() override;
	virtual void onCRCMismatch( const UnicodeString &details ) override {}
	virtual Bool shouldLeaveGame() override { return FALSE; }
	virtual void onReplayWritten( const AsciiString &replayPath ) override {}
	virtual Bool beginSyncedStart( Int phase ) override { return FALSE; }
	virtual Bool isSyncedStartDone( Int phase ) override { return TRUE; }

	virtual Int getChatMessageLifeSeconds() override { return -1; }
	virtual void drawNetworkOverlay( DisplayString *text, Int dropColor ) override {}
	virtual void releaseNetworkOverlay() override {}

	virtual const char *getOverlayLayout( Int overlay, const char *gameSpyLayout ) override { return gameSpyLayout; }
	virtual void onShellScreenChanged() override;
	virtual void onCloseAllOverlays() override;

	virtual void startOnline() override;
	virtual void updateMainMenu() override;
	virtual void updateDownloadMenu( WindowLayout *layout, void *userData ) override;
	virtual void onDownloadCanceled() override {}
	virtual void stopOnlineChecks() override;
	virtual void cancelPatchCheck() override;
	virtual void startDownloadingPatches() override;
};
