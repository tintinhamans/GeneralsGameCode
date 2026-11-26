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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h"
#include "GameNetwork/GenOnline/Provider/GenOnlineNetworkOverlay.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"
#include "GameNetwork/GenOnline/GenOnlineCrashReport.h"

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
#include "GameNetwork/GenOnline/GenOnlineMainMenuUtils.h"
#include "GameClient/GenOnlineMenus.h"
#include <steam/isteamnetworkingsockets.h>

static bool g_bTearDownGenOnlineRequested = false;
// The Logging Out box shows until the teardown is done or this time has passed.
static constexpr int LOGOUT_BOX_MAX_MS = 15000;
static bool g_bLogoutBoxShown = false;
static int64_t g_logoutBoxDeadline = 0;
static UnicodeString g_logoutResultTitle;
static UnicodeString g_logoutResultBody;

static void CloseLogoutBox()
{
	if (!g_bLogoutBoxShown)
		return;
	g_bLogoutBoxShown = false;
	ClearGSMessageBoxes();
	if (!g_logoutResultTitle.isEmpty())
		GSMessageBoxOk(g_logoutResultTitle, g_logoutResultBody);
	g_logoutResultTitle.clear();
	g_logoutResultBody.clear();
}

void TearDownGenOnline()
{
	if (GenOnlineServicesManager::GetInstance() == nullptr)
		return;

	g_bTearDownGenOnlineRequested = true;

	EGOTearDownReason teardownReason = GenOnlineServicesManager::GetInstance()->GetTeardownReason();

	if (IsModerationTeardownReason(teardownReason))
		return;

	g_logoutResultTitle.clear();
	g_logoutResultBody.clear();
	if (teardownReason != EGOTearDownReason::USER_REQUESTED_SILENT)
	{
		UnicodeString title, body;

		if (teardownReason == EGOTearDownReason::USER_LOGOUT)
		{
			title = L"Logged Out";
			body = L"You are now logged out of Generals Online.";
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

		g_logoutResultTitle = title;
		g_logoutResultBody = body;
	}

	GameSpyCloseAllOverlays();
	GSMessageBoxNoButtons(UnicodeString(L"Logging Out"), UnicodeString(L"Logging out of Generals Online..."), false);
	g_bLogoutBoxShown = true;
	g_logoutBoxDeadline = NowMs() + LOGOUT_BOX_MAX_MS;
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
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Services: %s, chosen by %s", GenOnlineServicesManager::EnvironmentName(), GenOnlineServicesManager::EnvironmentReason());
}

void GenOnlineGameHooks::onDisplayCreated()
{
	GenOnlineServicesManager::LogDisplayAdapter();
}

void GenOnlineGameHooks::onEngineInitialized()
{
	// The anticheat protects the whole session from game start, so the plugin loads here and not at the first sign in.
	GenOnlinePluginHost::LoadPlugin(GenOnlineServicesManager::Settings.GetAnticheatPlugin(), GenOnlineServicesManager::Settings.GetTransportPlugins());

	m_discordRichPresence = new GenOnlineDiscordRPC();
	m_discordRichPresence->Initialize();
}

void GenOnlineGameHooks::onEngineUpdate()
{
	if (g_bTearDownGenOnlineRequested) // delayed tear down
	{
		g_bTearDownGenOnlineRequested = false;

		// The sockets and threads close on a worker thread so the game keeps drawing.
		GenOnlineServicesManager::BeginDestroyInstance();
	}

	if (GenOnlineServicesManager::PollDestroyInstance() || (g_bLogoutBoxShown && !GenOnlineServicesManager::IsDestroyPending()))
	{
		CloseLogoutBox();
	}
	else if (g_bLogoutBoxShown && NowMs() >= g_logoutBoxDeadline)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] The logout is still running, the Logging Out box closes");
		CloseLogoutBox();
	}

	// work handed back by worker threads, also while no online services instance exists
	ServiceConnection::PumpMainThreadTasks();

	GenOnlinePluginHost::Tick();

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
	GenOnlinePluginHost::UnloadPlugin();
}

void GenOnlineGameHooks::onEngineExit()
{
	GenOnlineServicesManager::ShutdownSentry();
}

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
	if (TheGenOnlineGame != nullptr && GenOnlineServicesManager::IsSentryActive())
	{
		// The user_id tag on the scope identifies the local player.
		const UnicodeString strReportDetails(GenOnlineCrashReport::WithoutPlayerNames(strMismatchDetails.str()).c_str());
		AsciiString sentryMsg;
		sentryMsg.translate(strReportDetails);

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

Bool GenOnlineGameHooks::beginSyncedStart( Int phase )
{
	NetworkMesh* pStartMesh = GenOnlineServicesManager::GetNetworkMesh();
	return pStartMesh != nullptr && pStartMesh->BeginSyncedStart(phase);
}

Bool GenOnlineGameHooks::isSyncedStartDone( Int phase )
{
	NetworkMesh* pStartMesh = GenOnlineServicesManager::GetNetworkMesh();
	return pStartMesh == nullptr || pStartMesh->SyncedStartDone(phase);
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

// The GenOnline menus open their own layouts for the overlays they show.
const char *GenOnlineGameHooks::getOverlayLayout( Int overlay, const char *gameSpyLayout )
{
	switch (overlay)
	{
		case GSOVERLAY_PLAYERINFO:
			return "Menus/GenOnlinePopupPlayerInfo.wnd";
		case GSOVERLAY_MAPSELECT:
			return "Menus/GenOnlineMapSelectMenu.wnd";
		case GSOVERLAY_BUDDY:
			return "Menus/GenOnlineBuddyOverlay.wnd";
		case GSOVERLAY_GAMEOPTIONS:
			return "Menus/GenOnlinePopupHostGame.wnd";
		case GSOVERLAY_GAMEPASSWORD:
			return "Menus/GenOnlinePopupJoinGame.wnd";
		default:
			return gameSpyLayout;
	}
}

void GenOnlineGameHooks::onShellScreenChanged()
{
	GenOnlineRaiseNotificationBox();
}

void GenOnlineGameHooks::onCloseAllOverlays()
{
	GenOnlineDeleteNotificationBox();
}

void GenOnlineGameHooks::startOnline()
{
	// Two instances must never share the mesh and the plugin.
	if (GenOnlineServicesManager::IsDestroyPending())
	{
		GSMessageBoxOk(UnicodeString(L"Logging Out"), UnicodeString(L"Generals Online is still logging out. Try again in a moment."));
		return;
	}
	GenOnlineStartPatchCheck();
}

// The sign-in dialog sits over the main menu, which stays the shell screen and ticks it.
void GenOnlineGameHooks::updateMainMenu()
{
	GenOnlineHTTPThinkWrapper();
	GOSignInDialogUpdate();
}

// The launcher updates the game, so the Online path has no patch download.
void GenOnlineGameHooks::updateDownloadMenu( WindowLayout *layout, void *userData )
{
}

void GenOnlineGameHooks::onDownloadCanceled()
{
}

void GenOnlineGameHooks::stopOnlineChecks()
{
}

void GenOnlineGameHooks::cancelPatchCheck()
{
}

void GenOnlineGameHooks::startDownloadingPatches()
{
}

void GenOnlineGameHooks::onOptionsMenuInit( Bool locked )
{
	GenOnlineOptionsMenuInit(locked);
}

Bool GenOnlineGameHooks::onOptionsMenuComboSelected( GameWindow *control )
{
	return GenOnlineOptionsMenuComboSelected(control);
}

Bool GenOnlineGameHooks::onOptionsMenuButtonSelected( GameWindow *control )
{
	return GenOnlineOptionsMenuButtonSelected(control);
}

Bool GenOnlineGameHooks::onOptionsMenuAccept()
{
	return GenOnlineOptionsMenuAccept();
}

void GenOnlineGameHooks::onOptionsMenuDefaults()
{
	GenOnlineOptionsMenuDefaults();
}
