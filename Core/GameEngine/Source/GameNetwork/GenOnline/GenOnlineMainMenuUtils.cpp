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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002
// Description: GameSpy version check, patch download, etc utils
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/UserPreferences.h"
#include "Common/version.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameLogic/ScriptEngine.h"

#include "GameClient/ShellHooks.h"

#include "gamespy/ghttp/ghttp.h"

#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GenOnline/GenOnlineMainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"

#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"


///////////////////////////////////////////////////////////////////////////////////////

static Int timeThroughOnline = 0; // used to avoid having old callbacks cause problems

GameWindow *genOnlineCancelWindow = nullptr;

///////////////////////////////////////////////////////////////////////////////////////

// GenOnline @bugfix arcticdolphin 09/11/2026 Tear down now like CancelLogin does, nothing on the main menu acts on a pending teardown, and the next attempt must not reuse this manager.
static void tearDownOnlineAttempt()
{
	GenOnlineServicesManager *manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		return;
	}

	manager->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);
	manager->ConsumePendingFullTeardown();
	TearDownGenOnline();
}

// GenOnline @feature arcticdolphin 09/11/2026 An error of the Online path shows inside the sign-in dialog when it is open, else in a message box.
static void showOnlineError(UnicodeString title, UnicodeString message, GameWinMsgBoxFunc closeFunc)
{
	if (GOSignInDialogIsOpen())
	{
		GOSignInDialogShowError(title, message, closeFunc);
	}
	else
	{
		MessageBoxOk(title, message, closeFunc);
	}
}

// An outdated client cannot go online. The message tells the player to start the game from the launcher, which updates it, and the button ends the game.
static void quitForUpdate()
{
	TheGameEngine->setQuitting(TRUE);
}

static void showUpdateRequired()
{
	showOnlineError(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUpdateRequiredTitle", L"Update Required"),
		TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUpdateRequired", L"This version of the game is out of date. Close the game and start it from the launcher to update it."),
		quitForUpdate);
}

///////////////////////////////////////////////////////////////////////////////////////

static void startOnline()
{
	if (genOnlineCancelWindow)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}

	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_SELECTED]);

	// GenOnline @feature arcticdolphin 09/11/2026 The sign-in runs inside the dialog when it is open, data without the dialog keeps the sign-in screen.
	if (GOSignInDialogIsOpen())
	{
		GOSignInDialogBeginSignIn();
	}
	else
	{
		TheShell->push(AsciiString("Menus/GenOnlineLoginProfile.wnd"));
	}
}

///////////////////////////////////////////////////////////////////////////////////////

void GenOnlineCancelPatchCheckCallbackAndReopenDropdown()
{
	HandleCanceledDownload();
	GenOnlineCancelPatchCheckCallback();

	// Patch was cancelled, tear us down
	tearDownOnlineAttempt();
}

void GenOnlineCancelPatchCheckCallback()
{
	// GenOnline @bugfix arcticdolphin 09/11/2026 End this attempt so its late replies never start online.
	timeThroughOnline++;
	HandleCanceledDownload(FALSE); // don't dropdown
	if (genOnlineCancelWindow)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}
	// GenOnline @feature arcticdolphin 09/11/2026 The sign-in dialog replaces the checking box, so it closes with it.
	GOSignInDialogDestroy();
}

// GameSpy's HTTP SDK has had at least 1 crash bug, so we're going to just bail and
// never try again if they crash us.  We won't be able to get back online again (we'll
// time out) but at least we'll live.
static Bool isHttpOk = TRUE;

void GenOnlineHTTPThinkWrapper()
{
	if (isHttpOk)
	{
		try
		{
			ghttpThink();
		}
		catch (...)
		{
			isHttpOk = FALSE; // we can't abort the login, since we might be done with the
												// required checks and are fetching extras.  If it is a required
												// check, we'll time out normally.
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////

void GenOnlineStartPatchCheck()
{
	timeThroughOnline++;

    // GenOnline @feature arcticdolphin 09/11/2026 One sign-in dialog shows every step of the Online path, without its layout the message boxes and the sign-in screen stay.
    GOSignInDialogOpen();

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);

	bool bIsARMArchitecture = SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM || SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 || SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64;
	if (bIsARMArchitecture)
	{
        showOnlineError(TheGameText->fetchOrSubstitute("GUI:NoARMSupportHeader", L"Unsupported System"),
            TheGameText->fetchOrSubstitute("GUI:NoARMSupport", L"Generals Online does not support ARM processors"),
            GenOnlineCancelPatchCheckCallbackAndReopenDropdown);

		return;
	}

	// GENERALS ONLINE
	GenOnlineServicesManager::CreateInstance();

	// online services must be initialized
	// TODO_GENONLINE: Uninit this when leaving MP, waste of resources and cycles
	GenOnlineServicesManager::GetInstance()->Init();

	// GenOnline @bugfix arcticdolphin 09/11/2026 A teardown left by an earlier cancelled or failed attempt must not pop the next login off the welcome screen.
	GenOnlineServicesManager::GetInstance()->ClearPendingFullTeardown();

    // A loaded plugin that is the anticheat needs its external process, else an error message shows.
    if (GenOnlinePluginHost::IsPluginLoaded())
    {
        if (GenOnlinePluginHost::IsPluginAnticheat() && !GenOnlinePluginHost::IsExternalProcessRunning())
        {
            showOnlineError(TheGameText->fetchOrSubstitute("GUI:ACErrorHeader", L"AntiCheat Error"),
                TheGameText->fetchOrSubstitute("GUI:ACExternalProcessNotRunning", L"The AntiCheat external process is not running"),
                GenOnlineCancelPatchCheckCallbackAndReopenDropdown);

            return;
        }
    }
	else if (GenOnlinePluginHost::DidPluginFailToLoad()) // Did we have something to load but it failed?
	{
        std::string strPlugin = GenOnlinePluginHost::GetPluginName();
        std::string pluginPath = std::format("plugins/{}/{}.dll", strPlugin.c_str(), strPlugin.c_str());

		UnicodeString strErrorMssage;
        strErrorMssage.format(L"Failed to load the AntiCheat plugin from path: %hs. Please make sure the plugin is installed correctly.", pluginPath.c_str());

        showOnlineError(TheGameText->fetchOrSubstitute("GUI:ACErrorHeader", L"AntiCheat Error"),
			strErrorMssage,
            GenOnlineCancelPatchCheckCallbackAndReopenDropdown);

        return;
	}

	if (GOSignInDialogIsOpen())
	{
		GOSignInDialogShowChecking(GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
	}
	else
	{
		genOnlineCancelWindow = MessageBoxCancel(TheGameText->fetch("GUI:CheckingForPatches"),
			TheGameText->fetch("GUI:CheckingForPatches"), GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
	}

	// A transport plugin that failed to load does not block online play. The notice shows once per run and the sign in continues behind it.
	static Bool s_shownTransportPluginNotice = FALSE;
	if (GenOnlinePluginHost::DidTransportPluginFailToLoad() && !s_shownTransportPluginNotice)
	{
		s_shownTransportPluginNotice = TRUE;
		MessageBoxOk(TheGameText->fetchOrSubstitute("GUI:TransportPluginLoadFailedHeader", L"Peer-to-peer plugin"),
			TheGameText->fetchOrSubstitute("GUI:TransportPluginLoadFailed", L"The peer-to-peer plugin could not be loaded. You can still play online over the standard connection."),
			nullptr);
	}

	const Int patchCheckAttempt = timeThroughOnline;
	GenOnlineServicesManager::GetInstance()->StartVersionCheck([patchCheckAttempt](bool bSuccess, bool bNeedsUpdate)
		{
			// GenOnline @bugfix arcticdolphin 09/11/2026 The player cancelled this check or started another one before the reply came.
			if (patchCheckAttempt != timeThroughOnline)
			{
				return;
			}

#if defined(GENONLINE_BUILD_ENVIRONMENT)
			bNeedsUpdate = false;
#endif
			if (genOnlineCancelWindow)
			{
				TheWindowManager->winDestroy(genOnlineCancelWindow);
				genOnlineCancelWindow = nullptr;
			}

			if (!bSuccess)
			{
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);

				showOnlineError(TheGameText->fetch("GUI:CannotConnectToServservTitle"),
					TheGameText->fetch("GUI:CannotConnectToServserv"),
					GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
				return;
			}

			if (bNeedsUpdate)
			{
				showUpdateRequired();
				return;
			}

			startOnline();
		});
}

///////////////////////////////////////////////////////////////////////////////////////
