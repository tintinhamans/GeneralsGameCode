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
// FILE: GenOnlineMainMenuUtils.cpp
// Author: Matthew D. Campbell, Sept 2002
// Description: GameSpy version check, patch download, etc utils
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <fcntl.h>

//#include "Common/Registry.h"
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

#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GenOnline/GenOnlineMainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"

#include "WWDownload/Registry.h"
#include "WWDownload/urlBuilder.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"


///////////////////////////////////////////////////////////////////////////////////////

static Bool checkingForPatchBeforeGameSpy = FALSE;
static Int checksLeftBeforeOnline = 0;
static Int timeThroughOnline = 0; // used to avoid having old callbacks cause problems
static Bool mustDownloadPatch = FALSE;
static Bool cantConnectBeforeOnline = FALSE;
static std::list<QueuedDownload> queuedDownloads;

static char *MOTDBuffer = nullptr;
static char *configBuffer = nullptr;
GameWindow *genOnlineCancelWindow = nullptr;

static Bool s_asyncDNSThreadDone = TRUE;
static Bool s_asyncDNSThreadSucceeded = FALSE;
static Bool s_asyncDNSLookupInProgress = FALSE;
static HANDLE s_asyncDNSThreadHandle = nullptr;
enum {
	LOOKUP_INPROGRESS,
	LOOKUP_FAILED,
	LOOKUP_SUCCEEDED,
};

///////////////////////////////////////////////////////////////////////////////////////

static void startOnline();
static void reallyStartPatchCheck();

// TheSuperHackers @bugfix arcticdolphin 09/11/2026 Tear down now like CancelLogin does; nothing on the main menu acts on a pending teardown, and the next attempt must not reuse this manager.
static void tearDownOnlineAttempt()
{
	GenOnlineServicesManager *manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		return;
	}

	manager->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);
	manager->ConsumePendingFullTeardown();
	TearDownGeneralsOnline();
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 An error of the Online path shows inside the sign-in dialog when it is open, else in a message box.
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

// TheSuperHackers @feature arcticdolphin 09/11/2026 A failed update inside the sign-in dialog ends the game like the Update Failed message box does.
static void quitAfterFailedUpdate()
{
	TheGameEngine->setQuitting(TRUE);
}

static void showUpdateFailedInSignInDialog(const UnicodeString &message)
{
	GOSignInDialogShowError(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUpdateFailed", L"Update Failed"), message, quitAfterFailedUpdate);
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 Restart in the sign-in dialog launches the downloaded patcher, which quits the game.
static void launchPatcherFromSignInDialog()
{
	const UnicodeString failed = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUpdateRunFailed", L"Could not run the updater. Press below to exit.");
	GenOnlineServicesManager *manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		showUpdateFailedInSignInDialog(failed);
		return;
	}

	manager->LaunchPatcher([failed]()
		{
			showUpdateFailedInSignInDialog(failed);
		});
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The update downloads inside the sign-in dialog; its Cancel stops it through the close owner, which tears the online services down.
static void downloadUpdateInSignInDialog()
{
	const UnicodeString failed = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUpdateDownloadFailed", L"Could not download the updater. Press below to exit.");
	GenOnlineServicesManager *manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		showUpdateFailedInSignInDialog(failed);
		return;
	}

	const Int updateAttempt = timeThroughOnline;
	GOSignInDialogShowDownloading(GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
	manager->StartDownloadUpdate([updateAttempt]()
		{
			if (updateAttempt != timeThroughOnline)
				return;
			GOSignInDialogShowReady(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUpdateReadyMessage", L"Press Restart to begin installing the patch."),
				launchPatcherFromSignInDialog, GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
		},
		[updateAttempt, failed]()
		{
			if (updateAttempt != timeThroughOnline)
				return;
			showUpdateFailedInSignInDialog(failed);
		});
}

///////////////////////////////////////////////////////////////////////////////////////

// someone has hit a button allowing downloads to start
void GenOnlineStartDownloadingPatches()
{
	if (queuedDownloads.empty())
	{
		HandleCanceledDownload();
		return;
	}

	WindowLayout *layout;
	layout = TheWindowManager->winCreateLayout( "Menus/GenOnlineDownloadMenu.wnd" );
	layout->runInit();
	layout->hide( FALSE );
	layout->bringForward();
	HandleCanceledDownload(FALSE);
	DEBUG_ASSERTCRASH(TheDownloadManager, ("No download manager!"));
	if (TheDownloadManager)
	{
		std::list<QueuedDownload>::iterator it = queuedDownloads.begin();
		while (it != queuedDownloads.end())
		{
			QueuedDownload q = *it;
			TheDownloadManager->queueFileForDownload(q.server, q.userName, q.password,
				q.file, q.localFile, q.regKey, q.tryResume);
			queuedDownloads.pop_front();
			it = queuedDownloads.begin();
		}
		TheDownloadManager->downloadNextQueuedFile();
	}
}

///////////////////////////////////////////////////////////////////////////////////////

// user agrees to patch before going online
static void patchBeforeOnlineCallback()
{
	// TheSuperHackers @feature arcticdolphin 09/11/2026 These patches download in the download menu, so the sign-in dialog closes first.
	GOSignInDialogDestroy();
	GenOnlineStartDownloadingPatches();
}

// user doesn't want to patch before going online
static void noPatchBeforeOnlineCallback()
{
	queuedDownloads.clear();
	if (mustDownloadPatch || cantConnectBeforeOnline)
	{
		// Patch was cancelled and critical, tear us down; the close owner also returns the menu to normal
		GenOnlineCancelPatchCheckCallbackAndReopenDropdown();
	}
	else
	{
		// clear out unneeded downloads and go on
		startOnline();
	}
}

///////////////////////////////////////////////////////////////////////////////////////

static Bool hasWriteAccess(bool bFileAccessOnly = false)
{
	const char* filename = "PatchAccessTest.txt";

	remove(filename);

	int handle = _open( filename, _O_CREAT | _O_RDWR, _S_IREAD | _S_IWRITE);
	if (handle == -1)
	{
		return false;
	}

	_close(handle);
	remove(filename);

	// NGMP: We don't care about registry anymore... just disk access
	if (!bFileAccessOnly)
	{
		unsigned int val;
		if (!GetUnsignedIntFromRegistry("", "Version", val))
		{
			return false;
		}

		if (!SetUnsignedIntInRegistry("", "Version", val))
		{
			return false;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////

static void startOnline()
{
	checkingForPatchBeforeGameSpy = FALSE;

	DEBUG_ASSERTCRASH(checksLeftBeforeOnline==0, ("starting online with pending callbacks"));
	if (genOnlineCancelWindow)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}

	if (cantConnectBeforeOnline)
	{
		showOnlineError(TheGameText->fetch("GUI:CannotConnectToServservTitle"),
			TheGameText->fetch("GUI:CannotConnectToServserv"),
			noPatchBeforeOnlineCallback);
		return;
	}
	if (!queuedDownloads.empty())
	{
		if (!hasWriteAccess())
		{
			showOnlineError(TheGameText->fetch("GUI:Error"),
				TheGameText->fetch("GUI:MustHaveAdminRights"),
				noPatchBeforeOnlineCallback);
		}
		// TheSuperHackers @feature arcticdolphin 09/11/2026 The patch prompt shows inside the sign-in dialog when it is open.
		else if (GOSignInDialogIsOpen())
		{
			GOSignInDialogShowUpdate(TheGameText->fetch(mustDownloadPatch ? "GUI:MustPatchForOnline" : "GUI:CanPatchForOnline"),
				patchBeforeOnlineCallback, noPatchBeforeOnlineCallback);
		}
		else if (mustDownloadPatch)
		{
			MessageBoxOkCancel(TheGameText->fetch("GUI:PatchAvailable"),
				TheGameText->fetch("GUI:MustPatchForOnline"),
				patchBeforeOnlineCallback, noPatchBeforeOnlineCallback);
		}
		else
		{
			MessageBoxYesNo(TheGameText->fetch("GUI:PatchAvailable"),
				TheGameText->fetch("GUI:CanPatchForOnline"),
				patchBeforeOnlineCallback, noPatchBeforeOnlineCallback);
		}
		return;
	}

	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_SELECTED]);

	// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in runs inside the dialog when it is open; data without the dialog keeps the sign-in screen.
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

static void queuePatch(Bool mandatory, AsciiString downloadURL)
{
	QueuedDownload q;
	Bool success = TRUE;

	AsciiString connectionType;
	success &= downloadURL.nextToken(&connectionType, ":");

	AsciiString server;
	success &= downloadURL.nextToken(&server, ":/");

	AsciiString user;
	success &= downloadURL.nextToken(&user, ":@");

	AsciiString pass;
	success &= downloadURL.nextToken(&pass, "@/");

	AsciiString filePath;
	success &= downloadURL.nextToken(&filePath, "");

	if (!success && user.isNotEmpty())
	{
		// no user/pass combo - move the file into it's proper place
		filePath = user;
		user = ""; // LFeenanEA - Credentials removed as per Security requirements
		pass = "";
		success = TRUE;
	}

	AsciiString fileStr = filePath;
	const char *s = filePath.reverseFind('/');
	if (s)
		fileStr = s+1;
	AsciiString fileName = "patches\\";
	fileName.concat(fileStr);

	DEBUG_LOG(("download URL split: %d [%s] [%s] [%s] [%s] [%s] [%s]",
		success, connectionType.str(), server.str(), user.str(), pass.str(),
		filePath.str(), fileName.str()));

	if (!success)
		return;

	q.file = filePath;
	q.localFile = fileName;
	q.password = pass;
	q.regKey = "";
	q.server = server;
	q.tryResume = TRUE;
	q.userName = user;

	std::list<QueuedDownload>::iterator it = queuedDownloads.begin();
	while (it != queuedDownloads.end())
	{
		if (it->localFile == q.localFile)
			return; // don't add it if it exists already (because we can check multiple times)
		++it;
	}

	queuedDownloads.push_back(q);
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool motdCallback( GHTTPRequest request, GHTTPResult result,
															char * buffer, GHTTPByteCount bufferLen, void * param )
{
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	delete[] MOTDBuffer;
	MOTDBuffer = NEW char[bufferLen];
	memcpy(MOTDBuffer, buffer, bufferLen);
	MOTDBuffer[bufferLen-1] = 0;

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
	if (genOnlineCancelWindow && !checksLeftBeforeOnline)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}

	DEBUG_LOG(("------- Got MOTD before going online -------"));
	DEBUG_LOG(("%s", (MOTDBuffer)?MOTDBuffer:""));
	DEBUG_LOG(("--------------------------------------------"));

	if (!checksLeftBeforeOnline)
		startOnline();

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool configCallback( GHTTPRequest request, GHTTPResult result,
																char * buffer, GHTTPByteCount bufferLen, void * param )
{
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	delete[] configBuffer;
	configBuffer = nullptr;

	if (result != GHTTPSuccess || bufferLen < 100)
	{
		if (!checkingForPatchBeforeGameSpy)
			return GHTTPTrue;
		--checksLeftBeforeOnline;
		if (genOnlineCancelWindow && !checksLeftBeforeOnline)
		{
			TheWindowManager->winDestroy(genOnlineCancelWindow);
			genOnlineCancelWindow = nullptr;
		}
		cantConnectBeforeOnline = TRUE;
		if (!checksLeftBeforeOnline)
		{
			startOnline();
		}
		return GHTTPTrue;
	}

	configBuffer = NEW char[bufferLen];
	memcpy(configBuffer, buffer, bufferLen);
	configBuffer[bufferLen-1] = 0;

	AsciiString fname;
	fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
	FILE *fp = fopen(fname.str(), "wb");
	if (fp)
	{
		fwrite(configBuffer, bufferLen, 1, fp);
		fclose(fp);
	}

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
	if (genOnlineCancelWindow && !checksLeftBeforeOnline)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}

	DEBUG_LOG(("Got Config before going online"));

	if (!checksLeftBeforeOnline)
		startOnline();

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool configHeadCallback( GHTTPRequest request, GHTTPResult result,
																		char * buffer, GHTTPByteCount bufferLen, void * param )
{
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	DEBUG_LOG(("HTTP head resp: res=%d, len=%d, buf=[%s]", result, bufferLen, buffer));

	if (result == GHTTPSuccess)
	{
		DEBUG_LOG(("Headers are [%s]", ghttpGetHeaders( request )));

		AsciiString headers(ghttpGetHeaders( request ));
		AsciiString line;
		while (headers.nextToken(&line, "\n\r"))
		{
			AsciiString key, val;
			line.nextToken(&key, ": ");
			line.nextToken(&val, ": \r\n");

			if (key.compare("Content-Length") == 0 && val.isNotEmpty())
			{
				Int serverLen = atoi(val.str());
				Int fileLen = 0;
				AsciiString fname;
				fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
				FILE *fp = fopen(fname.str(), "rb");
				if (fp)
				{
					fseek(fp, 0, SEEK_END);
					fileLen = ftell(fp);
					fclose(fp);
				}

				if (serverLen == fileLen)
				{
					// we don't need to download the MOTD again
					--checksLeftBeforeOnline;
					DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
					if (genOnlineCancelWindow && !checksLeftBeforeOnline)
					{
						TheWindowManager->winDestroy(genOnlineCancelWindow);
						genOnlineCancelWindow = nullptr;
					}

					delete[] configBuffer;
					configBuffer = nullptr;

					AsciiString fname;
					fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
					FILE *fp = fopen(fname.str(), "rb");
					if (fp)
					{
						configBuffer = NEW char[fileLen];
						fread(configBuffer, fileLen, 1, fp);
						configBuffer[fileLen-1] = 0;
						fclose(fp);

						DEBUG_LOG(("Got Config before going online"));

						if (!checksLeftBeforeOnline)
							startOnline();

						return GHTTPTrue;
					}
				}
			}
		}
	}

	// we need to download the MOTD again
	std::string gameURL, mapURL;
	std::string configURL, motdURL;
	FormatURLFromRegistry(gameURL, mapURL, configURL, motdURL);
	ghttpGet( configURL.c_str(), GHTTPFalse, configCallback, param );

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool gamePatchCheckCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen, void * param )
{
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));

	DEBUG_LOG(("Result=%d, buffer=[%s], len=%d", result, buffer, bufferLen));
	if (result != GHTTPSuccess)
	{
		if (!checkingForPatchBeforeGameSpy)
			return GHTTPTrue;
		cantConnectBeforeOnline = TRUE;
		if (!checksLeftBeforeOnline)
		{
			startOnline();
		}
		return GHTTPTrue;
	}

	AsciiString message = buffer;
	AsciiString line;
	while (message.nextToken(&line, "\r\n"))
	{
		AsciiString type, req, url;
		Bool ok = TRUE;
		ok &= line.nextToken(&type, " ");
		ok &= line.nextToken(&req, " ");
		ok &= line.nextToken(&url, " ");
		if (ok && type == "patch")
		{
			DEBUG_LOG(("Saw a patch: %d/[%s]", atoi(req.str()), url.str()));
			queuePatch( atoi(req.str()), url );
			if (atoi(req.str()))
			{
				mustDownloadPatch = TRUE;
			}
		}
		else if (ok && type == "server")
		{
		}
	}

	if (!checksLeftBeforeOnline)
	{
		startOnline();
	}

	return GHTTPTrue;
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
	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 End this attempt so its late replies never start online.
	timeThroughOnline++;
	s_asyncDNSLookupInProgress = FALSE;
	HandleCanceledDownload(FALSE); // don't dropdown
	checkingForPatchBeforeGameSpy = FALSE;
	checksLeftBeforeOnline = 0;
	if (genOnlineCancelWindow)
	{
		TheWindowManager->winDestroy(genOnlineCancelWindow);
		genOnlineCancelWindow = nullptr;
	}
	// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in dialog replaces the checking box, so it closes with it.
	GOSignInDialogDestroy();
	queuedDownloads.clear();

	delete[] MOTDBuffer;
	MOTDBuffer = nullptr;

	delete[] configBuffer;
	configBuffer = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool overallStatsCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen, void * param )
{
	DEBUG_LOG(("overallStatsCallback() - Result=%d, len=%d", result, bufferLen));
	if (result != GHTTPSuccess)
	{
		return GHTTPTrue;
	}

	// The GenOnline welcome menu reads the stats text of both games.
	GenOnlineHandleOverallStats( buffer, bufferLen );

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool numPlayersOnlineCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen, void * param )
{
	DEBUG_LOG(("numPlayersOnlineCallback() - Result=%d, buffer=[%s], len=%d", result, buffer, bufferLen));
	if (result != GHTTPSuccess)
	{
		return GHTTPTrue;
	}

	AsciiString message = buffer;
	message.trim();
	const char *s = message.reverseFind('\\');
	if (!s)
	{
		return GHTTPTrue;
	}

	if (*s == '\\')
		++s;

	DEBUG_LOG(("Message was '%s', trimmed to '%s'=%d", buffer, s, atoi(s)));
	GenOnlineHandleNumPlayersOnline(atoi(s));

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

void GenOnlineCheckOverallStats()
{
#if RTS_GENERALS
	const char *const url = "http://gamestats.gamespy.com/ccgenerals/display.html";
#elif RTS_ZEROHOUR
	const char *const url = "http://gamestats.gamespy.com/ccgenzh/display.html";
#endif
	ghttpGet(url, GHTTPFalse, overallStatsCallback, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////

void GenOnlineCheckNumPlayersOnline()
{
#if RTS_GENERALS
	const char *const url = "http://launch.gamespyarcade.com/software/launch/arcadecount2.dll?svcname=ccgenerals";
#elif RTS_ZEROHOUR
	const char *const url = "http://launch.gamespyarcade.com/software/launch/arcadecount2.dll?svcname=ccgenzh";
#endif
	ghttpGet(url, GHTTPFalse, numPlayersOnlineCallback, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////

DWORD WINAPI GenOnlineAsyncGethostbynameThreadFunc( void * szName )
{
	HOSTENT *he = gethostbyname( (const char *)szName );

	if (he)
	{
		s_asyncDNSThreadSucceeded = TRUE;
	}
	else
	{
		s_asyncDNSThreadSucceeded = FALSE;
	}

	s_asyncDNSThreadDone = TRUE;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////

int GenOnlineAsyncGethostbyname(char * szName)
{
	static int            stat = 0;
	static unsigned long  threadid;

	if( stat == 0 )
	{
		/* Kick off gethostname thread */
		s_asyncDNSThreadDone = FALSE;
		s_asyncDNSThreadHandle = CreateThread( nullptr, 0, GenOnlineAsyncGethostbynameThreadFunc, szName, 0, &threadid );

		if( s_asyncDNSThreadHandle == nullptr )
		{
			return( LOOKUP_FAILED );
		}
		stat = 1;
	}
	if( stat == 1 )
	{
		if( s_asyncDNSThreadDone )
		{
			/* Thread finished */
			stat = 0;
			s_asyncDNSLookupInProgress = FALSE;
			CloseHandle(s_asyncDNSThreadHandle);
			s_asyncDNSThreadHandle = nullptr;
			return( (s_asyncDNSThreadSucceeded)?LOOKUP_SUCCEEDED:LOOKUP_FAILED );
		}
	}

	return( LOOKUP_INPROGRESS );
}

///////////////////////////////////////////////////////////////////////////////////////

// GameSpy's HTTP SDK has had at least 1 crash bug, so we're going to just bail and
// never try again if they crash us.  We won't be able to get back online again (we'll
// time out) but at least we'll live.
static Bool isHttpOk = TRUE;

void GenOnlineHTTPThinkWrapper()
{
	if (s_asyncDNSLookupInProgress)
	{
		Char hostname[] = "servserv.generals.ea.com";
		Int ret = GenOnlineAsyncGethostbyname(hostname);
		switch(ret)
		{
		case LOOKUP_FAILED:
			cantConnectBeforeOnline = TRUE;
			startOnline();
			break;
		case LOOKUP_SUCCEEDED:
			reallyStartPatchCheck();
			break;
		}
	}

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

void GenOnlineStopAsyncDNSCheck()
{
	if (s_asyncDNSThreadHandle)
	{
		MAYBE_UNUSED Int res = TerminateThread(s_asyncDNSThreadHandle, 0);
		(void)res;
		DEBUG_ASSERTCRASH(res, ("Could not terminate the Async DNS Lookup thread!"));	// Thread still not killed!
		CloseHandle(s_asyncDNSThreadHandle);
	}
	s_asyncDNSThreadHandle = nullptr;
	s_asyncDNSLookupInProgress = FALSE;
}

///////////////////////////////////////////////////////////////////////////////////////

void GenOnlineStartPatchCheck()
{

    checkingForPatchBeforeGameSpy = TRUE;
    cantConnectBeforeOnline = FALSE;
    timeThroughOnline++;
    checksLeftBeforeOnline = 0;

    // TheSuperHackers @feature arcticdolphin 09/11/2026 One sign-in dialog shows every step of the Online path; without its layout the message boxes and the sign-in screen stay.
    GOSignInDialogOpen();

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);

	bool bIsARMArchitecture = SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM || SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 || SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64;
	if (bIsARMArchitecture)
	{
        showOnlineError(TheGameText->fetchOrSubstitute("GUI:NoARMSupportHeader", L"Unsupported System"),
            TheGameText->fetchOrSubstitute("GUI:NoARMSupport", L"GeneralsOnline does not support ARM processors"),
            GenOnlineCancelPatchCheckCallbackAndReopenDropdown);

		return;
	}

	// GENERALS ONLINE
	GenOnlineServicesManager::CreateInstance();

	// online services must be initialized
	// TODO_NGMP: Uninit this when leaving MP, waste of resources and cycles
	GenOnlineServicesManager::GetInstance()->Init();

	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 A teardown left by an earlier cancelled or failed attempt must not pop the next login off the welcome screen.
	GenOnlineServicesManager::GetInstance()->ClearPendingFullTeardown();

    // if we have an AC plugin loaded but the AC external process isnt running, show an error message
    if (GenOnlinePluginHost::IsPluginLoaded())
    {
        if (!GenOnlinePluginHost::IsExternalProcessRunning())
        {
            showOnlineError(TheGameText->fetchOrSubstitute("GUI:ACErrorHeader", L"AntiCheat Error"),
                TheGameText->fetchOrSubstitute("GUI:ACExternalProcessNotRunning", L"The AntiCheat external process is not running"),
                GenOnlineCancelPatchCheckCallbackAndReopenDropdown);

            return;
        }
    }
	else if (GenOnlinePluginHost::DidPluginFailToLoad()) // Did we have something to load but it failed?
	{
        std::string strPlugin = GenOnlineServicesManager::Settings.GetAnticheatPlugin();
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

	const Int patchCheckAttempt = timeThroughOnline;
	GenOnlineServicesManager::GetInstance()->StartVersionCheck([patchCheckAttempt](bool bSuccess, bool bNeedsUpdate)
		{
			// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The player cancelled this check or started another one before the reply came.
			if (patchCheckAttempt != timeThroughOnline)
			{
				return;
			}

#if defined(USE_TEST_ENV) || defined(USE_DEBUG_ON_LIVE_SERVER)
			bNeedsUpdate = false;
#endif
			cantConnectBeforeOnline = !bSuccess;
			mustDownloadPatch = bNeedsUpdate;

			if (!bSuccess)
			{
				if (genOnlineCancelWindow)
				{
					TheWindowManager->winDestroy(genOnlineCancelWindow);
					genOnlineCancelWindow = NULL;
				}

				// TODO_NGMP: do this everywhere teardowngamespy was called
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);

				showOnlineError(TheGameText->fetch("GUI:CannotConnectToServservTitle"),
					TheGameText->fetch("GUI:CannotConnectToServserv"),
					noPatchBeforeOnlineCallback);
			}
			else
			{
				if (!bNeedsUpdate)
				{
					startOnline();
				}
				else
				{
					// TODO_NGMP: Later we should allow in-game updates
					if (genOnlineCancelWindow)
					{
						TheWindowManager->winDestroy(genOnlineCancelWindow);
						genOnlineCancelWindow = NULL;
					}

					// NGMP_NOTE: This checks you can write to the local dir, we actually write to my docs data dir now, because it's safer, so we don't really need this chekc
					/*
					if (!hasWriteAccess(true))
					{
						MessageBoxOk(TheGameText->fetch("GUI:Error"),
							TheGameText->fetch("GUI:MustHaveAdminRights"),
							GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
					}
					else*/ if (mustDownloadPatch)
					{
						// NOTE: we treat all patches as mandatory currently
						// TheSuperHackers @feature arcticdolphin 09/11/2026 The update prompt and the download show inside the sign-in dialog when it is open; without it the download menu opens.
						const GameWinMsgBoxFunc startUpdate = []()
							{
								if (GOSignInDialogIsOpen())
								{
									downloadUpdateInSignInDialog();
									return;
								}

								WindowLayout* layout;
								layout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineDownloadMenu.wnd"));
								layout->runInit();
								layout->hide(FALSE);
								layout->bringForward();

								GenOnlineServicesManager::GetInstance()->StartDownloadUpdate([]()
									{
										MessageBoxOk(UnicodeString(L"Update Ready"), UnicodeString(L"Press OK to begin installing the patch"), []()
											{
												GenOnlineServicesManager::GetInstance()->LaunchPatcher();
											});
									});

							};
						if (GOSignInDialogIsOpen())
						{
							GOSignInDialogShowUpdate(UnicodeString(L"Press Update to begin updating.\nOtherwise, you can visit www.playgenerals.online to download the latest update manually."),
								startUpdate, GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
						}
						else
						{
							genOnlineCancelWindow = MessageBoxOkCancel(TheGameText->fetch("GUI:PatchAvailable"),
								UnicodeString(L"Press OK to begin updating.\n\nOtherwise, you can visit www.playgenerals.online to download the latest update manually."), startUpdate, GenOnlineCancelPatchCheckCallbackAndReopenDropdown);
						}
					}
				}
			}
		});
	

	// TODO_NGMP: Impl patch checks again

	/*
	s_asyncDNSLookupInProgress = TRUE;
	Char hostname[] = "servserv.generals.ea.com";
	Int ret = GenOnlineAsyncGethostbyname(hostname);
	switch(ret)
	{
	case LOOKUP_FAILED:
		cantConnectBeforeOnline = TRUE;
		startOnline();
		break;
	case LOOKUP_SUCCEEDED:
		reallyStartPatchCheck();
		break;
	}
	*/
}

///////////////////////////////////////////////////////////////////////////////////////

static void reallyStartPatchCheck()
{
	checksLeftBeforeOnline = 4;

	std::string gameURL, mapURL;
	std::string configURL, motdURL;

	FormatURLFromRegistry(gameURL, mapURL, configURL, motdURL);

	// check for a patch first
	DEBUG_LOG(("Game patch check: [%s]", gameURL.c_str()));
	DEBUG_LOG(("Map patch check: [%s]", mapURL.c_str()));
	DEBUG_LOG(("Config: [%s]", configURL.c_str()));
	DEBUG_LOG(("MOTD: [%s]", motdURL.c_str()));
	ghttpGet(gameURL.c_str(), GHTTPFalse, gamePatchCheckCallback, (void *)timeThroughOnline);
	ghttpGet(mapURL.c_str(), GHTTPFalse, gamePatchCheckCallback, (void *)timeThroughOnline);
	ghttpHead(configURL.c_str(), GHTTPFalse, configHeadCallback, (void *)timeThroughOnline);
	ghttpGet(motdURL.c_str(), GHTTPFalse, motdCallback, (void *)timeThroughOnline);

	// check total game stats
	GenOnlineCheckOverallStats();

	// check the users online
	GenOnlineCheckNumPlayersOnline();
}

///////////////////////////////////////////////////////////////////////////////////////
