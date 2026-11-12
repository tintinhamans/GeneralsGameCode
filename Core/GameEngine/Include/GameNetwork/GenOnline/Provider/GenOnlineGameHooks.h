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

#include "GameNetwork/Online/IOnlineGameHooks.h"

class GenOnlineDiscordRPC;

// Ends the GenOnline services on the next engine update and tells the player why.
void TearDownGeneralsOnline();

class GenOnlineGameHooks : public IOnlineGameHooks
{
public:
	GenOnlineGameHooks();
	virtual ~GenOnlineGameHooks() override;

	virtual void onGlobalDataLoaded() override;
	virtual void loadSettings() override;
	virtual void onEngineInitialized() override;
	virtual void onEngineUpdate() override;
	virtual void onEngineShutdown() override;
	virtual void onEngineExit() override;

	virtual Bool getMatchFrameRateLimit( Int &outLimit ) override;

	virtual GameInfo *getGameInfo() override;
	virtual Bool isLoggedIn() override;
	virtual void onCRCMismatch( const UnicodeString &details ) override;
	virtual Bool shouldLeaveGame() override;
	virtual void onReplayWritten( const AsciiString &replayPath ) override;

	virtual Int getChatMessageLifeSeconds() override;
	virtual Bool isStatsOverlayEnabled() override;
	virtual void drawNetworkOverlay( DisplayString *text, Int dropColor ) override;

private:
	GenOnlineDiscordRPC *m_discordRichPresence;
};
