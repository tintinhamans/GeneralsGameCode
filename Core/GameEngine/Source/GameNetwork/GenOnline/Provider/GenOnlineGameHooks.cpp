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

// FILE: GenOnlineGameHooks.cpp ///////////////////////////////////////////////
// Description: GenOnline game hooks.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h"
#include "GameNetwork/GenOnline/Provider/GenOnlineNetworkOverlay.h"

#include "Common/FileSystem.h"
#include "GameClient/Color.h"
#include "GameClient/DisplayString.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineDefines.h"
#include "GameNetwork/GenOnline/GenOnlineGame.h"
#include "GameNetwork/GenOnline/DiscordRichPresence.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"
#include "GameNetwork/GenOnline/ProxyLogin.h"
#include <steam/isteamnetworkingsockets.h>

static bool g_bTearDownGeneralsOnlineRequested = false;
void TearDownGeneralsOnline()
{
	g_bTearDownGeneralsOnlineRequested = true;

	if (GenOnlineServicesManager::GetInstance() == nullptr)
		return;

	EGOTearDownReason teardownReason = GenOnlineServicesManager::GetInstance()->GetTeardownReason();

	if (teardownReason != EGOTearDownReason::USER_REQUESTED_SILENT && !IsModerationTeardownReason(teardownReason))
	{
		UnicodeString title, body;

		if (teardownReason == EGOTearDownReason::USER_LOGOUT)
		{
			title = L"Logged Out";
			body = L"You are now logged out of GeneralsOnline.";
		}
		else if (teardownReason == EGOTearDownReason::LOST_CONNECTION)
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"Your connection to the Generals Online servers was lost.";
		}
        else if (teardownReason == EGOTearDownReason::AUTH_FAILED)
        {
            title = TheGameText->fetch("GUI:GSErrorTitle");
            body = L"Authentication with the Generals Online servers failed.";
        }
		else if (teardownReason == EGOTearDownReason::MIDDLEWARE_LOGIN_FAILED)
		{
            title = TheGameText->fetch("GUI:GSErrorTitle");
            body = L"Middleware Authentication failed.";
		}
		else
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"An unknown error occurred.";
		}

		GenOnlineServicesManager::GetInstance()->ResetPendingFullTeardownReason();

		GameSpyCloseAllOverlays();
		GSMessageBoxOk(title, body);
	}
}

//-------------------------------------------------------------------------------------------------
GenOnlineGameHooks::GenOnlineGameHooks()
	: m_discordRichPresence(nullptr)
{
}

GenOnlineGameHooks::~GenOnlineGameHooks()
{
	delete m_discordRichPresence;
	m_discordRichPresence = nullptr;
}

void GenOnlineGameHooks::onGlobalDataLoaded()
{
	GenOnlineServicesManager::InitSentry();
}

void GenOnlineGameHooks::loadSettings()
{
	GenOnlineServicesManager::Settings.Initialize();
	TheFileSystem->setExcludedPaths(GenOnlineServicesManager::Settings.DataPacks_UserExcludePaths());
}

void GenOnlineGameHooks::onEngineInitialized()
{
	m_discordRichPresence = new GenOnlineDiscordRPC();
	m_discordRichPresence->Initialize();
}

void GenOnlineGameHooks::onEngineUpdate()
{
	if (g_bTearDownGeneralsOnlineRequested) // delayed tear down
	{
		g_bTearDownGeneralsOnlineRequested = false;

		GenOnlineServicesManager::DestroyInstance();

	}

	// work handed back by worker threads, also while no online services instance exists
	ServiceConnection::PumpMainThreadTasks();

	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}

	if (m_discordRichPresence != nullptr)
	{
		m_discordRichPresence->Tick(
			GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>());
	}
}

void GenOnlineGameHooks::onEngineShutdown()
{
	delete m_discordRichPresence;
	m_discordRichPresence = nullptr;

	GenOnlineServicesManager::DestroyInstance();
}

void GenOnlineGameHooks::onEngineExit()
{
	GenOnlineServicesManager::ShutdownSentry();
}

// A GenOnline match runs at the frame rate limit from the GenOnline settings.
Bool GenOnlineGameHooks::getMatchFrameRateLimit( Int &outLimit )
{
	if (TheGenOnlineGame == nullptr)
		return FALSE;

	outLimit = GenOnlineServicesManager::Settings.Graphics_GetFPSLimit();
	return TRUE;
}

GameInfo *GenOnlineGameHooks::getGameInfo()
{
	return TheGenOnlineGame;
}

Bool GenOnlineGameHooks::isLoggedIn()
{
	return GenOnlineServicesManager::GetInstance() != nullptr;
}

void GenOnlineGameHooks::onCRCMismatch( const UnicodeString &strMismatchDetails )
{
#if defined(GENONLINE_USE_SENTRY)
	if (TheGenOnlineGame != nullptr)
	{
		// local player info
		int64_t userID = -1;
		std::string strDisplayname = "Unknown";
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (pAuthInterface != nullptr)
		{
			userID = pAuthInterface->GetUserID();
			strDisplayname = pAuthInterface->GetDisplayName();
		}
		std::string strUserID = std::format("{}", userID);

		sentry_set_extra("user_id", sentry_value_new_int32(userID));
		sentry_set_extra("user_displayname", sentry_value_new_string(strDisplayname.c_str()));

		AsciiString sentryMsg;
		sentryMsg.translate(strMismatchDetails);

		// send event to sentry
		sentry_capture_event(sentry_value_new_message_event(
			SENTRY_LEVEL_ERROR,
			"CRC_MISMATCH",
			sentryMsg.str()
		));
	}
#endif
}

Bool GenOnlineGameHooks::shouldLeaveGame()
{
	return TheGenOnlineGame != nullptr && TheGenOnlineGame->canKickOnObserversDisabled();
}

void GenOnlineGameHooks::onReplayWritten( const AsciiString &absoluteReplayPath )
{
	if (TheGenOnlineGame != nullptr)
	{
		GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
		if (pOnlineServicesMgr != nullptr)
		{
			pOnlineServicesMgr->CommitReplay(absoluteReplayPath);
		}
	}
}

Int GenOnlineGameHooks::getChatMessageLifeSeconds()
{
	return GenOnlineServicesManager::Settings.GetChatLifeSeconds();
}

void GenOnlineGameHooks::drawNetworkOverlay( DisplayString *text, Int dropColor )
{
	if (GenOnlineServicesManager::GetNetworkOverlayMode() != 0)
	{
		GenOnlineDrawNetworkOverlay(GenOnlineServicesManager::GetNetworkOverlayMode(), text, dropColor);
	}
	else
	{
		GenOnlineReleaseNetworkOverlay();
	}
}

void GenOnlineGameHooks::releaseNetworkOverlay()
{
	GenOnlineReleaseNetworkOverlay();
}
