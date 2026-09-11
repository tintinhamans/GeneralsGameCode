/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// FILE: GameSpyProviderFactory.cpp ///////////////////////////////////////////
// GenMP online provider abstraction.
// Description: GameSpy implementation of OnlineProviderFactory.
//
// createServices() and destroyServices() follow the SetUpGameSpy and TearDownGameSpy
// bodies of PeerDefs.cpp in the same order. The leading TearDownGameSpy() call of
// SetUpGameSpy runs in SetUpOnline() instead, which calls TearDownOnline() before
// creating anything.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/GameSpy/GameSpyProviderFactory.h"
#include "GameNetwork/Online/GameSpy/GameSpySession.h"
#include "GameNetwork/Online/GameSpy/GameSpyMatchmaking.h"

#include "Common/GlobalData.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/GameSpyMiscPreferences.h"
#include "GameClient/ShellHooks.h"
#include "GameNetwork/Online/IOnlinePresence.h"
#include "GameNetwork/Online/IOnlineChat.h"
#include "GameNetwork/Online/IOnlineStats.h"
#include "GameNetwork/Online/IOnlineLobbyState.h"
#include "GameNetwork/Online/IOnlineLadder.h"
#include "GameNetwork/Online/GameSpy/GameSpyLadder.h"
#include "GameNetwork/Online/IOnlineResults.h"
#include "GameNetwork/Online/IOnlineConfig.h"
#include "GameNetwork/GameSpy/PingThread.h"
#include "GameNetwork/RankPointValue.h"

// Defined in GameSpyOverlay.cpp; forward-declared here exactly as PeerDefs.cpp does.
void deleteNotificationBox();

// --------------------------------------------------------------
IOnlineConfig *GameSpyProviderFactory::createConfig( const char *configBuffer )
{
	return IOnlineConfig::create( configBuffer ? configBuffer : "" );
}

IOnlineSession *GameSpyProviderFactory::createSession()
{
	return GameSpySession::createNewSession();
}

IOnlineChat *GameSpyProviderFactory::createChat()
{
	return IOnlineChat::createNewMessageQueue();
}

IOnlineLobbyState *GameSpyProviderFactory::createLobbyState()
{
	return IOnlineLobbyState::createNewGameSpyInfoInterface();
}

IOnlinePresence *GameSpyProviderFactory::createPresence()
{
	return IOnlinePresence::createNewMessageQueue();
}

IOnlineStats *GameSpyProviderFactory::createStats()
{
	return IOnlineStats::createNewMessageQueue();
}

IOnlineLadder *GameSpyProviderFactory::createLadder()
{
	return NEW GameSpyLadder;
}

IOnlineResults *GameSpyProviderFactory::createResults()
{
	return IOnlineResults::createNewGameResultsInterface();
}

IOnlineMatchmaking *GameSpyProviderFactory::createMatchmaking()
{
	return GameSpyMatchmaking::createNewMatchmaking();
}

// --------------------------------------------------------------
void GameSpyProviderFactory::createServices( const char *motdBuffer, const char *configBuffer )
{
	if (!motdBuffer)
		motdBuffer = "";
	if (!configBuffer)
		configBuffer = "";
	// SetUpOnline() runs the TearDownGameSpy() that SetUpGameSpy calls first.

	AsciiString dir = TheGlobalData->getPath_UserData();
	CreateDirectory(dir.str(), nullptr);
	dir.format("%sGeneralsOnline", TheGlobalData->getPath_UserData().str());
	CreateDirectory(dir.str(), nullptr);
	dir.format("%sGeneralsOnline\\Ladders", TheGlobalData->getPath_UserData().str());
	CreateDirectory(dir.str(), nullptr);

	TheGameSpyBuddyMessageQueue = createPresence();
	TheGameSpyBuddyMessageQueue->startThread();

	TheGameSpyPeerMessageQueue = createChat();
	TheGameSpyPeerMessageQueue->startThread();

	TheGameSpyPSMessageQueue = createStats();
	TheGameSpyPSMessageQueue->startThread();

	/*
	TheGameSpyGame = NEW GameSpyStagingRoom;
	*/

	TheGameSpyInfo = createLobbyState();
	TheGameSpyInfo->setMOTD(motdBuffer);
	TheGameSpyInfo->setConfig(configBuffer);

	CustomMatchPreferences pref;
	TheGameSpyInfo->setDisallowAsianText(pref.getDisallowAsianText());
	TheGameSpyInfo->setDisallowNonAsianText( pref.getDisallowNonAsianText());


	TheGameSpyConfig = createConfig(configBuffer);

	TheLadderList = createLadder();

	ThePinger = PingerInterface::createNewPingerInterface();
	ThePinger->startThreads();

	TheRankPointValues = NEW RankPoints;

	TheOnlineSession = createSession();
	TheOnlineMatchmaking = createMatchmaking();
}

// --------------------------------------------------------------
void GameSpyProviderFactory::destroyServices()
{
	delete TheOnlineMatchmaking;
	TheOnlineMatchmaking = nullptr;

	delete TheOnlineSession;
	TheOnlineSession = nullptr;

	// save off cached stats
	if (TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID())
	{
//		/* This was done on the score screen, so there is no need to do it now.
//		 *
		PSPlayerStats localPSStats = TheGameSpyPSMessageQueue->findPlayerStatsByID(TheGameSpyInfo->getLocalProfileID());
		if (localPSStats.id != 0)
		{
			GameSpyMiscPreferences mPref;
			mPref.setCachedStats(IOnlineStats::formatPlayerKVPairs(localPSStats).c_str());
			mPref.write();
		}
//		*/
	}

	// End our threads before we kill off the singletons they reference.  No crashy-crash for you!
	if (TheGameSpyPSMessageQueue)
		TheGameSpyPSMessageQueue->endThread();
	if (TheGameSpyBuddyMessageQueue)
		TheGameSpyBuddyMessageQueue->endThread();
	if (TheGameSpyPeerMessageQueue)
		TheGameSpyPeerMessageQueue->endThread();
	if (ThePinger)
		ThePinger->endThreads();

	delete TheRankPointValues;
	TheRankPointValues = nullptr;

	delete TheGameSpyPSMessageQueue;
	TheGameSpyPSMessageQueue = nullptr;

	delete TheGameSpyBuddyMessageQueue;
	TheGameSpyBuddyMessageQueue = nullptr;

	delete TheGameSpyPeerMessageQueue;
	TheGameSpyPeerMessageQueue = nullptr;

	if (TheGameSpyInfo)
	{
		if (TheGameSpyInfo->getInternalIP())
		{
			// we've logged in before.  mark us as logging out.
			SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGOUT);
		}
		delete TheGameSpyInfo;
		TheGameSpyInfo = nullptr;
	}

	delete ThePinger;
	ThePinger = nullptr;

	delete TheLadderList;
	TheLadderList = nullptr;

	delete TheGameSpyConfig;
	TheGameSpyConfig = nullptr;

	// make sure the notification box doesn't exist
	deleteNotificationBox();
}
