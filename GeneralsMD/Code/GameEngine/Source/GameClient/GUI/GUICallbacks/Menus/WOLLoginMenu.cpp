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
// FILE: WOLLoginMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/STLTypedefs.h"
#include "../GenOnlineTypes.h"

void GenOnline_WOLLoginMenu_LoginCallback(ELoginResult loginResult);

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/QuotedPrintable.h"
#include "Common/Registry.h"
#include "Common/OptionPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/Display.h"
#include "GameClient/Image.h"
#include "GameClient/MessageBox.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PingThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"

#include "GameNetwork/GameSpyOverlay.h"

#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"


#ifdef ALLOW_NON_PROFILED_LOGIN
Bool GameSpyUseProfiles = false;
#endif // ALLOW_NON_PROFILED_LOGIN

static Bool webBrowserActive = FALSE;
static Bool useWebBrowserForTOS = FALSE;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;

static const UnsignedInt loginTimeoutInMS = 10000;
static UnsignedInt loginAttemptTime = 0;

class GameSpyLoginPreferences : public UserPreferences
{
public:
	GameSpyLoginPreferences();
	virtual ~GameSpyLoginPreferences() override;

	Bool loadFromIniFile();

	virtual Bool load(AsciiString fname) override;
	virtual Bool write() override;

	AsciiString getPasswordForEmail( AsciiString email );
	AsciiString getDateForEmail( AsciiString email, AsciiString &month, AsciiString &date, AsciiString &year  );
	AsciiStringList getNicksForEmail( AsciiString email );
	void addLogin( AsciiString email, AsciiString nick, AsciiString password, AsciiString date );
	void forgetLogin( AsciiString email );
	AsciiStringList getEmails();

private:
	typedef std::map<AsciiString, AsciiString> PassMap;
	typedef std::map<AsciiString, AsciiString> DateMap;
	typedef std::map<AsciiString, AsciiStringList> NickMap;
	PassMap m_emailPasswordMap;
	NickMap m_emailNickMap;
	DateMap m_emailDateMap;
};

static AsciiString obfuscate( AsciiString in )
{
	char *buf = NEW char[in.getLength() + 1];
	strcpy(buf, in.str());
	static const char *xorWord = "1337Munkee";
	char *c = buf;
	const char *c2 = xorWord;
	while (*c)
	{
		if (!*c2)
			c2 = xorWord;
		if (*c != *c2)
			*c = *c++ ^ *c2++;
		else
			c++, c2++;
	}
	AsciiString out = buf;
	delete[] buf;
	return out;
}

GameSpyLoginPreferences::GameSpyLoginPreferences()
{
	loadFromIniFile();
}

GameSpyLoginPreferences::~GameSpyLoginPreferences()
{
}

Bool GameSpyLoginPreferences::loadFromIniFile()
{
	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		AsciiString fname;
		fname.format("GameSpyLogin_Instance%.2u.ini", rts::ClientInstance::getInstanceId());
		return load(fname);
	}

	return load("GameSpyLogin.ini");
}


Bool GameSpyLoginPreferences::load( AsciiString fname )
{
	if (!UserPreferences::load(fname))
		return false;

	UserPreferences::iterator upIt = begin();
	while (upIt != end())
	{
		AsciiString key = upIt->first;
		if (key.startsWith("pass_"))
		{
			AsciiString email, pass;
			email = key.str() + 5;
			pass = upIt->second;

			AsciiString quoPass = QuotedPrintableToAsciiString(pass);
			pass = obfuscate(quoPass);

			m_emailPasswordMap[email] = pass;
		}
		if (key.startsWith("date_"))
		{
			AsciiString email, date;
			email = key.str() + 5;
			date = upIt->second;

			date = QuotedPrintableToAsciiString(date);

			m_emailDateMap[email] = date;
		}
		else if (key.startsWith("nick_"))
		{
			AsciiString email, nick, nicks;
			email = key.str() + 5;
			nicks = upIt->second;
			while (nicks.nextToken(&nick, ","))
			{
				m_emailNickMap[email].push_back(nick);
			}
		}
		++upIt;
	}

	return true;
}

Bool GameSpyLoginPreferences::write()
{
	if (m_filename.isEmpty())
		return false;

	FILE *fp = fopen(m_filename.str(), "w");
	if (fp)
	{
		fprintf(fp, "lastEmail = %s\n",   ((*this)["lastEmail"].str()));
		fprintf(fp, "lastName = %s\n",    ((*this)["lastName"].str()));
		fprintf(fp, "useProfiles = %s\n", ((*this)["useProfiles"].str()));
		PassMap::iterator passIt = m_emailPasswordMap.begin();
		while (passIt != m_emailPasswordMap.end())
		{
			AsciiString pass = obfuscate(passIt->second);
			AsciiString quoPass = AsciiStringToQuotedPrintable(pass);

			fprintf(fp, "pass_%s = %s\n", passIt->first.str(), quoPass.str());
			++passIt;
		}

		PassMap::iterator dateIt = m_emailDateMap.begin();
		while (dateIt != m_emailDateMap.end())
		{
			AsciiString date = AsciiStringToQuotedPrintable(dateIt->second);

			fprintf(fp, "date_%s = %s\n", dateIt->first.str(), date.str());
			++dateIt;
		}

		NickMap::iterator nickIt = m_emailNickMap.begin();
		while (nickIt != m_emailNickMap.end())
		{
			AsciiString nicks;
			AsciiStringListIterator listIt = nickIt->second.begin();
			while (listIt != nickIt->second.end())
			{
				nicks.concat(*listIt);
				nicks.concat(',');
				++listIt;
			}
			fprintf(fp, "nick_%s = %s\n", nickIt->first.str(), nicks.str());
			++nickIt;
		}

		fclose(fp);
		return true;
	}
	return false;
}
AsciiString GameSpyLoginPreferences::getDateForEmail( AsciiString email, AsciiString &month, AsciiString &date, AsciiString &year )
{
	if ( m_emailDateMap.find(email) == m_emailDateMap.end() )
		return AsciiString::TheEmptyString;
	AsciiString fullDate = m_emailDateMap[email];
	if(fullDate.getLength() != 8)
		return AsciiString::TheEmptyString;
	month.format("%c%c", fullDate.getCharAt(0), fullDate.getCharAt(1));
	date.format("%c%c", fullDate.getCharAt(2), fullDate.getCharAt(3));
	year.format("%c%c%c%c", fullDate.getCharAt(4), fullDate.getCharAt(5), fullDate.getCharAt(6), fullDate.getCharAt(7));
	return m_emailDateMap[email];
}

AsciiString GameSpyLoginPreferences::getPasswordForEmail( AsciiString email )
{
	if ( m_emailPasswordMap.find(email) == m_emailPasswordMap.end() )
		return AsciiString::TheEmptyString;
	return m_emailPasswordMap[email];
}

AsciiStringList GameSpyLoginPreferences::getNicksForEmail( AsciiString email )
{
	if ( m_emailNickMap.find(email) == m_emailNickMap.end() )
	{
		AsciiStringList empty;
		return empty;
	}
	return m_emailNickMap[email];
}

void GameSpyLoginPreferences::addLogin( AsciiString email, AsciiString nick, AsciiString password, AsciiString date  )
{
	if ( std::find(m_emailNickMap[email].begin(), m_emailNickMap[email].end(), nick) == m_emailNickMap[email].end() )
		m_emailNickMap[email].push_back(nick);
	m_emailPasswordMap[email] = password;
	m_emailDateMap[email] = date;
}

void GameSpyLoginPreferences::forgetLogin( AsciiString email )
{
	m_emailNickMap.erase(email);
	m_emailPasswordMap.erase(email);
	m_emailDateMap.erase(email);

}

AsciiStringList GameSpyLoginPreferences::getEmails()
{
	AsciiStringList theList;
	NickMap::iterator it = m_emailNickMap.begin();
	while (it != m_emailNickMap.end())
	{
		theList.push_back(it->first);
		++it;
	}
	return theList;
}

static GameSpyLoginPreferences *loginPref = NULL;

static void startPings()
{
	std::list<AsciiString> pingServers = TheGameSpyConfig->getPingServers();
	Int timeout = TheGameSpyConfig->getPingTimeoutInMs();
	Int reps = TheGameSpyConfig->getNumPingRepetitions();

	for (std::list<AsciiString>::const_iterator it = pingServers.begin(); it != pingServers.end(); ++it)
	{
		AsciiString pingServer = *it;
		PingRequest req;
		req.hostname = pingServer.str();
		req.repetitions = reps;
		req.timeout = timeout;
		ThePinger->addRequest(req);
	}
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		if (loginPref)
		{
			loginPref->write();
			delete loginPref;
			loginPref = NULL;
		}
		TheShell->push(nextScreen);
	}
	else
	{
		DEBUG_ASSERTCRASH(loginPref != NULL, ("loginPref == NULL"));
		if (loginPref)
		{
			loginPref->write();
			delete loginPref;
			loginPref = NULL;
		}
	}

	nextScreen = NULL;

}


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLLoginID =						NAMEKEY_INVALID;
static NameKeyType buttonBackID =								NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonLoginID =							NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonCreateAccountID =			NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonUseAccountID =					NAMEKEY_INVALID;	// quick
static NameKeyType buttonDontUseAccountID =			NAMEKEY_INVALID;	// profile
static NameKeyType buttonTOSID =								NAMEKEY_INVALID;	// TOS
static NameKeyType parentTOSID =								NAMEKEY_INVALID;	// TOS Parent
static NameKeyType buttonTOSOKID =							NAMEKEY_INVALID;	// TOS
static NameKeyType listboxTOSID =								NAMEKEY_INVALID;	// TOS
static NameKeyType comboBoxEmailID =						NAMEKEY_INVALID;	// profile
static NameKeyType comboBoxLoginNameID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryLoginNameID =				NAMEKEY_INVALID;	// quick
static NameKeyType textEntryPasswordID =				NAMEKEY_INVALID;	// profile
static NameKeyType checkBoxRememberPasswordID =	NAMEKEY_INVALID;	// checkbox to remember information or not
static NameKeyType textEntryMonthID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryDayID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryYearID =				NAMEKEY_INVALID;	// profile

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLLogin =						NULL;
static GameWindow *buttonBack =								NULL;
static GameWindow *buttonLogin =							NULL;
static GameWindow *buttonCreateAccount =			NULL;
static GameWindow *buttonUseAccount =					NULL;
static GameWindow *buttonDontUseAccount =			NULL;
static GameWindow *buttonTOS						=			NULL;
static GameWindow *parentTOS						=			NULL;
static GameWindow *buttonTOSOK					=			NULL;
static GameWindow *listboxTOS						=			NULL;
static GameWindow *comboBoxEmail =						NULL;
static GameWindow *comboBoxLoginName =				NULL;
static GameWindow *textEntryLoginName =				NULL;
static GameWindow *textEntryPassword =				NULL;
static GameWindow *checkBoxRememberPassword =	NULL;
static GameWindow *textEntryMonth =				NULL;
static GameWindow *textEntryDay =				NULL;
static GameWindow *textEntryYear =				NULL;

// TheSuperHackers @feature arcticdolphin 08/11/2026 GO sign-in screen; a layout without these windows falls back to the message box login.
static const Int SIGN_IN_PROVIDER_COUNT = 3;
static const char *const signInProviderNames[SIGN_IN_PROVIDER_COUNT] = {
	"GameSpyLoginProfile.wnd:ButtonProviderDiscord",
	"GameSpyLoginProfile.wnd:ButtonProviderSteam",
	"GameSpyLoginProfile.wnd:ButtonProviderGameReplays",
};
static const ELoginProvider signInProviders[SIGN_IN_PROVIDER_COUNT] = { ELoginProvider::Discord, ELoginProvider::Steam, ELoginProvider::GameReplays };
static Bool signInScreen = FALSE;
static GameWindow *staticTextSignInStatus =		NULL;
static GameWindow *buttonUseAnotherAccount =	NULL;
static GameWindow *buttonSignInCancel =				NULL;
static GameWindow *buttonSignInProviders[SIGN_IN_PROVIDER_COUNT] = { NULL, NULL, NULL };
static ELoginStage shownSignInStage = ELoginStage::None;
// TheSuperHackers @feature arcticdolphin 09/11/2026 Provider logos (scripts/go_logos.py), drawn by the button itself so no window over it blocks the click or the hover.
static const char *const signInProviderLogos[SIGN_IN_PROVIDER_COUNT] = { "GOLogoDiscord", "GOLogoSteam", "GOLogoGameReplays" };
static GameWinDrawFunc signInProviderDraws[SIGN_IN_PROVIDER_COUNT] = { NULL, NULL, NULL };

static void signInProviderDraw( GameWindow *window, WinInstanceData *instData )
{
	Int provider = -1;
	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
	{
		if (window == buttonSignInProviders[i])
			provider = i;
	}
	if (provider < 0)
		return;

	if (signInProviderDraws[provider] != NULL)
		signInProviderDraws[provider]( window, instData );

	const Image *logo = TheMappedImageCollection ? TheMappedImageCollection->findImageByName( signInProviderLogos[provider] ) : NULL;
	if (logo == NULL)
		return;

	// a square of 5/8 of the button height, centred down and twice as far from the left edge
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Int size = height * 5 / 8;
	const Int inset = (height - size) / 2;
	const Color color = BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) ? GameMakeColor( 255, 255, 255, 255 ) : GameMakeColor( 255, 255, 255, 96 );
	TheDisplay->drawImage( logo, x + 2 * inset, y + inset, x + 2 * inset + size, y + inset + size, color );
}

static void showSignInState( ELoginStage stage )
{
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
		return;

	UnicodeString status;
	if (stage == ELoginStage::Failed)
		status = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInFailed", L"Login failed.");
	else if (stage == ELoginStage::RequestingCode || stage == ELoginStage::WaitingForProvider)
		status = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInChoose", L"Choose how to sign in");
	else if (stage == ELoginStage::WaitingForBrowser)
		status = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInContinueInBrowser", L"Continue in your web browser");
	else if (pAuthInterface->HasDisplayName())
		status = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:GOSignInSigningInAs", L"Signing in as %ls...", pAuthInterface->GetDisplayNameW().c_str());
	else
		status = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInSigningIn", L"Signing in...");
	GadgetStaticTextSetText(staticTextSignInStatus, status);

	const Bool codeKnown = (stage == ELoginStage::WaitingForProvider || stage == ELoginStage::WaitingForBrowser) && !pAuthInterface->GetLoginCode().empty();
	const Bool choosing = codeKnown || stage == ELoginStage::RequestingCode;

	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
	{
		buttonSignInProviders[i]->winHide(!choosing);
		buttonSignInProviders[i]->winEnable(codeKnown);
	}
	buttonUseAnotherAccount->winHide(stage != ELoginStage::SavedLogin);
	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 Cancel stays up while the login finishes, so a websocket that never connects is no dead end.
	buttonSignInCancel->winHide(stage == ELoginStage::Failed);
}

static void updateSignInScreen( Bool force )
{
	if (!signInScreen || buttonPushed)
		return;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
		return;

	const ELoginStage stage = pAuthInterface->GetLoginStage();
	if (force || stage != shownSignInStage)
	{
		shownSignInStage = stage;
		showSignInState(stage);
	}
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in screen and the sign-in dialog share these windows; only one of them is up at a time.
static Bool findSignInWindows( GameWindow *parent, const char *statusName, const char *useAnotherAccountName, const char *cancelName, const char *const providerNames[] )
{
	staticTextSignInStatus = TheWindowManager->winGetWindowFromId( parent, NAMEKEY(statusName) );
	buttonUseAnotherAccount = TheWindowManager->winGetWindowFromId( parent, NAMEKEY(useAnotherAccountName) );
	buttonSignInCancel = TheWindowManager->winGetWindowFromId( parent, NAMEKEY(cancelName) );
	Bool found = staticTextSignInStatus && buttonUseAnotherAccount && buttonSignInCancel;
	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
	{
		buttonSignInProviders[i] = TheWindowManager->winGetWindowFromId( parent, NAMEKEY(providerNames[i]) );
		found = found && buttonSignInProviders[i] != NULL;
	}
	if (!found)
		return FALSE;

	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
	{
		if (buttonSignInProviders[i]->winGetDrawFunc() != signInProviderDraw)
		{
			signInProviderDraws[i] = buttonSignInProviders[i]->winGetDrawFunc();
			buttonSignInProviders[i]->winSetDrawFunc( signInProviderDraw );
		}
	}

	GadgetButtonSetText(buttonSignInProviders[0], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInProviderDiscord", L"Discord"));
	GadgetButtonSetText(buttonSignInProviders[1], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInProviderSteam", L"Steam"));
	GadgetButtonSetText(buttonSignInProviders[2], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInProviderGameReplays", L"GameReplays"));
	GadgetButtonSetText(buttonUseAnotherAccount, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUseAnotherAccount", L"Use another account"));
	GadgetButtonSetText(buttonSignInCancel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:Cancel", L"Cancel"));
	GadgetStaticTextSetText(staticTextSignInStatus, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInSigningIn", L"Signing in..."));
	return TRUE;
}

static Bool findSignInScreen()
{
	return findSignInWindows( NULL, "GameSpyLoginProfile.wnd:StaticTextSignInStatus", "GameSpyLoginProfile.wnd:ButtonUseAnotherAccount",
		"GameSpyLoginProfile.wnd:ButtonCancel", signInProviderNames );
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 GO sign-in dialog: one modal card over the main menu for the update check, the update prompt, errors and the sign-in.
enum SignInDialogStep
{
	SIGN_IN_DIALOG_CHECKING,
	SIGN_IN_DIALOG_UPDATE,
	SIGN_IN_DIALOG_ERROR,
	SIGN_IN_DIALOG_SIGN_IN
};

static const char *const signInDialogProviderNames[SIGN_IN_PROVIDER_COUNT] = {
	"GOSignInDialog.wnd:ButtonProviderDiscord",
	"GOSignInDialog.wnd:ButtonProviderSteam",
	"GOSignInDialog.wnd:ButtonProviderGameReplays",
};
static WindowLayout *signInDialogLayout = NULL;
static GameWindow *signInDialogRoot = NULL;
static GameWindow *signInDialogMessage = NULL;
static GameWindow *signInDialogUpdate = NULL;
static SignInDialogStep signInDialogStep = SIGN_IN_DIALOG_CHECKING;
static GameWinMsgBoxFunc signInDialogCancelFunc = NULL;
static GameWinMsgBoxFunc signInDialogUpdateFunc = NULL;

// the step's status line and message; every button but Cancel hides, the sign-in step shows its own through showSignInState
static void showSignInDialogStep( SignInDialogStep step, const UnicodeString &status, const UnicodeString &message )
{
	signInDialogStep = step;
	GadgetStaticTextSetText(staticTextSignInStatus, status);
	GadgetStaticTextSetText(signInDialogMessage, message);
	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
		buttonSignInProviders[i]->winHide(TRUE);
	buttonUseAnotherAccount->winHide(TRUE);
	signInDialogUpdate->winHide(step != SIGN_IN_DIALOG_UPDATE);
	buttonSignInCancel->winHide(FALSE);
	if (step == SIGN_IN_DIALOG_ERROR)
		GadgetButtonSetText(buttonSignInCancel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:Ok", L"OK"));
	else
		GadgetButtonSetText(buttonSignInCancel, TheGameText->FETCH_OR_SUBSTITUTE("GUI:Cancel", L"Cancel"));
}

static void updateSignInDialog( Bool force )
{
	if (signInDialogLayout == NULL || signInDialogStep != SIGN_IN_DIALOG_SIGN_IN)
		return;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
		return;

	const ELoginStage stage = pAuthInterface->GetLoginStage();
	if (force || stage != shownSignInStage)
	{
		shownSignInStage = stage;
		showSignInState(stage);
	}
}

// the auth keeps this callback after the dialog closes, until the next login registers its own, so it only acts while the dialog is up
static void signInDialogLoginCallback( ELoginResult loginResult )
{
	if (signInDialogLayout == NULL || signInDialogStep != SIGN_IN_DIALOG_SIGN_IN)
		return;

	if (loginResult == ELoginResult::Success)
	{
		ClearGSMessageBoxes();
		GOSignInDialogDestroy();
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGIN);
		TheShell->push("Menus/WOLWelcomeMenu.wnd");
	}
	else if (loginResult == ELoginResult::Failed)
	{
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		UnicodeString title = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInFailed", L"Login failed.");
		UnicodeString text;
		if (pAuthInterface != nullptr && !pAuthInterface->GetLoginFailureTitle().empty())
		{
			title = UnicodeString(pAuthInterface->GetLoginFailureTitle().c_str());
			text = UnicodeString(pAuthInterface->GetLoginFailureText().c_str());
		}
		ClearGSMessageBoxes();
		GOSignInDialogShowError(title, text, CancelPatchCheckCallbackAndReopenDropdown);
	}
	// UserCancelled: the Cancel button that cancelled closes the dialog
}

static void signInDialogPressed( GameWindow *control )
{
	if (control == buttonSignInCancel)
	{
		if (signInDialogStep == SIGN_IN_DIALOG_SIGN_IN)
		{
			GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
			if (pAuthInterface != nullptr)
				pAuthInterface->CancelLogin();
			CancelPatchCheckCallbackAndReopenDropdown();
			return;
		}
		GameWinMsgBoxFunc cancelFunc = signInDialogCancelFunc;
		if (cancelFunc != NULL)
			cancelFunc();
		else
			CancelPatchCheckCallbackAndReopenDropdown();
		return;
	}

	if (control == signInDialogUpdate)
	{
		GameWinMsgBoxFunc updateFunc = signInDialogUpdateFunc;
		GOSignInDialogDestroy();
		if (updateFunc != NULL)
			updateFunc();
		return;
	}

	if (signInDialogStep != SIGN_IN_DIALOG_SIGN_IN)
		return;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
		return;

	if (control == buttonUseAnotherAccount)
	{
		pAuthInterface->UseAnotherAccount();
		updateSignInDialog(FALSE);
		return;
	}
	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
	{
		if (control == buttonSignInProviders[i])
		{
			pAuthInterface->OpenProviderLogin(signInProviders[i]);
			updateSignInDialog(FALSE);
			return;
		}
	}
}

static WindowMsgHandledType signInDialogSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch (msg)
	{
		case GWM_INPUT_FOCUS:
		{
			if (mData1 == TRUE)
				*(Bool *)mData2 = TRUE;
			return MSG_HANDLED;
		}
		case GBM_SELECTED:
		{
			signInDialogPressed( (GameWindow *)mData1 );
			return MSG_HANDLED;
		}
	}
	return MSG_IGNORED;
}

// Escape does what the Cancel button does (OK on an error) whenever it is shown
static WindowMsgHandledType signInDialogInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if (msg != GWM_CHAR || (UnsignedByte)mData1 != KEY_ESC)
		return MSG_IGNORED;

	if (BitIsSet( (UnsignedByte)mData2, KEY_STATE_UP ) && signInDialogLayout != NULL && !buttonSignInCancel->winIsHidden())
		signInDialogPressed( buttonSignInCancel );
	return MSG_HANDLED;
}

Bool GOSignInDialogOpen( void )
{
	if (signInDialogLayout != NULL)
		return TRUE;

	// a mod or older data without the dialog keeps the message boxes and the sign-in screen
	if (TheFileSystem == NULL || !TheFileSystem->doesFileExist("Window\\Menus\\GOSignInDialog.wnd"))
		return FALSE;

	WindowLayout *layout = TheWindowManager->winCreateLayout( "Menus/GOSignInDialog.wnd" );
	if (layout == NULL)
		return FALSE;

	GameWindow *root = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("GOSignInDialog.wnd:GOSignInDialogRoot") );
	GameWindow *title = TheWindowManager->winGetWindowFromId( root, NAMEKEY("GOSignInDialog.wnd:StaticTextTitle") );
	signInDialogMessage = TheWindowManager->winGetWindowFromId( root, NAMEKEY("GOSignInDialog.wnd:StaticTextMessage") );
	signInDialogUpdate = TheWindowManager->winGetWindowFromId( root, NAMEKEY("GOSignInDialog.wnd:ButtonUpdate") );
	if (root == NULL || title == NULL || signInDialogMessage == NULL || signInDialogUpdate == NULL
		|| !findSignInWindows( root, "GOSignInDialog.wnd:StaticTextSignInStatus", "GOSignInDialog.wnd:ButtonUseAnotherAccount",
			"GOSignInDialog.wnd:ButtonCancel", signInDialogProviderNames ))
	{
		layout->destroyWindows();
		deleteInstance(layout);
		signInDialogMessage = NULL;
		signInDialogUpdate = NULL;
		staticTextSignInStatus = NULL;
		buttonUseAnotherAccount = NULL;
		buttonSignInCancel = NULL;
		for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
			buttonSignInProviders[i] = NULL;
		return FALSE;
	}

	signInDialogLayout = layout;
	signInDialogRoot = root;
	signInDialogCancelFunc = NULL;
	signInDialogUpdateFunc = NULL;
	shownSignInStage = ELoginStage::None;
	GadgetStaticTextSetText(title, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInTitle", L"Generals Online"));
	GadgetButtonSetText(signInDialogUpdate, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInUpdate", L"Update"));
	showSignInDialogStep(SIGN_IN_DIALOG_CHECKING, TheGameText->fetch("GUI:CheckingForPatches"), UnicodeString::TheEmptyString);

	root->winSetSystemFunc(signInDialogSystem);
	root->winSetInputFunc(signInDialogInput);
	layout->hide(FALSE);
	layout->bringForward();
	root->winBringToTop();
	TheWindowManager->winSetModal(root);
	TheWindowManager->winSetFocus(root);
	return TRUE;
}

Bool GOSignInDialogIsOpen( void )
{
	return signInDialogLayout != NULL;
}

void GOSignInDialogShowChecking( GameWinMsgBoxFunc cancelFunc )
{
	if (signInDialogLayout == NULL)
		return;
	signInDialogCancelFunc = cancelFunc;
	signInDialogUpdateFunc = NULL;
	showSignInDialogStep(SIGN_IN_DIALOG_CHECKING, TheGameText->fetch("GUI:CheckingForPatches"), UnicodeString::TheEmptyString);
}

void GOSignInDialogShowUpdate( UnicodeString message, GameWinMsgBoxFunc updateFunc, GameWinMsgBoxFunc cancelFunc )
{
	if (signInDialogLayout == NULL)
		return;
	signInDialogCancelFunc = cancelFunc;
	signInDialogUpdateFunc = updateFunc;
	showSignInDialogStep(SIGN_IN_DIALOG_UPDATE, TheGameText->fetch("GUI:PatchAvailable"), message);
}

void GOSignInDialogShowError( UnicodeString title, UnicodeString message, GameWinMsgBoxFunc closeFunc )
{
	if (signInDialogLayout == NULL)
		return;
	signInDialogCancelFunc = closeFunc;
	signInDialogUpdateFunc = NULL;
	showSignInDialogStep(SIGN_IN_DIALOG_ERROR, title, message);
}

void GOSignInDialogBeginSignIn( void )
{
	if (signInDialogLayout == NULL)
		return;

	ClearGSMessageBoxes();
	signInDialogCancelFunc = NULL;
	signInDialogUpdateFunc = NULL;
	showSignInDialogStep(SIGN_IN_DIALOG_SIGN_IN, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInSigningIn", L"Signing in..."), UnicodeString::TheEmptyString);

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		GOSignInDialogShowError(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSignInFailed", L"Login failed."), UnicodeString::TheEmptyString,
			CancelPatchCheckCallbackAndReopenDropdown);
		return;
	}

	pAuthInterface->RegisterForLoginCallback(signInDialogLoginCallback);
	pAuthInterface->SetSignInScreen(true);
	pAuthInterface->SetSignInDialog(true);
	pAuthInterface->BeginLogin();
	updateSignInDialog(TRUE);
}

void GOSignInDialogUpdate( void )
{
	updateSignInDialog(FALSE);
}

void GOSignInDialogDestroy( void )
{
	if (signInDialogLayout == NULL)
		return;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		pAuthInterface->SetSignInScreen(false);
		pAuthInterface->SetSignInDialog(false);
	}

	WindowLayout *layout = signInDialogLayout;
	signInDialogLayout = NULL;
	TheWindowManager->winUnsetModal(signInDialogRoot);
	layout->destroyWindows();
	deleteInstance(layout);

	signInDialogRoot = NULL;
	signInDialogMessage = NULL;
	signInDialogUpdate = NULL;
	signInDialogCancelFunc = NULL;
	signInDialogUpdateFunc = NULL;
	staticTextSignInStatus = NULL;
	buttonUseAnotherAccount = NULL;
	buttonSignInCancel = NULL;
	for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
		buttonSignInProviders[i] = NULL;
	shownSignInStage = ELoginStage::None;

	GameWindow *mainWin = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("MainMenu.wnd:MainMenuParent") );
	if (mainWin)
		TheWindowManager->winSetFocus( mainWin );
}

void EnableLoginControls( Bool state )
{
	if (buttonLogin)
		buttonLogin->winEnable(state);
	if (buttonCreateAccount)
		buttonCreateAccount->winEnable(state);
	if (buttonUseAccount)
		buttonUseAccount->winEnable(state);
	if (buttonDontUseAccount)
		buttonDontUseAccount->winEnable(state);
	if (comboBoxEmail)
		comboBoxEmail->winEnable(state);
	if (comboBoxLoginName)
		comboBoxLoginName->winEnable(state);
	if (textEntryLoginName)
		textEntryLoginName->winEnable(state);
	if (textEntryPassword)
		textEntryPassword->winEnable(state);
	if (checkBoxRememberPassword)
		checkBoxRememberPassword->winEnable(state);
	if( buttonTOS )
		buttonTOS->winEnable(state);

	if (textEntryMonth)
		textEntryMonth->winEnable(state);
	if (textEntryDay)
		textEntryDay->winEnable(state);
	if( textEntryYear )
		textEntryYear->winEnable(state);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Login Menu */
//-------------------------------------------------------------------------------------------------
void WOLLoginMenuInit( WindowLayout *layout, void *userData )
{
	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	loginAttemptTime = 0;
	signInScreen = findSignInScreen();
	shownSignInStage = ELoginStage::None;

	// GenOnline
	ClearGSMessageBoxes();
	if (!signInScreen)
	{
		GSMessageBoxNoButtons(UnicodeString(L"Logging In"), UnicodeString(L"Please wait..."), true);
	}

	// GenOnline: Register for login callback
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return;
	}

	// Now we can begin login
	pAuthInterface->RegisterForLoginCallback(GenOnline_WOLLoginMenu_LoginCallback);
	pAuthInterface->SetSignInScreen(signInScreen);
	pAuthInterface->BeginLogin();
	updateSignInScreen(TRUE);


	/*
	if (!loginPref)
	{
		loginPref = NEW GameSpyLoginPreferences;
	}

	// if the ESRB warning is blank (other country) hide the box
	GameWindow *esrbTitle = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("GameSpyLoginProfile.wnd:StaticTextESRBTop") );
	GameWindow *esrbParent = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("GameSpyLoginProfile.wnd:ParentESRB") );
	if (esrbTitle && esrbParent)
	{
		if ( GadgetStaticTextGetText( esrbTitle ).getLength() < 2 )
		{
			esrbParent->winHide(TRUE);
		}
	}

	
	parentWOLLoginID =						TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:WOLLoginMenuParent" );
	buttonBackID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonBack" );
	buttonLoginID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonLogin" );
	buttonCreateAccountID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonCreateAccount" );
	buttonUseAccountID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonUseAccount" );
	buttonDontUseAccountID =			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonDontUseAccount" );
	buttonTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonTOS" );
	parentTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ParentTOS" );
	buttonTOSOKID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonTOSOK" );
	listboxTOSID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ListboxTOS" );
	comboBoxEmailID =							TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ComboBoxEmail" );
	comboBoxLoginNameID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ComboBoxLoginName" );
	textEntryLoginNameID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryLoginName" );
	textEntryPasswordID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryPassword" );
	checkBoxRememberPasswordID =	TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:CheckBoxRememberInfo" );
	textEntryMonthID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryMonth" );
	textEntryDayID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryDay" );
	textEntryYearID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryYear" );

	parentWOLLogin =							TheWindowManager->winGetWindowFromId( NULL,  parentWOLLoginID );
	buttonBack =									TheWindowManager->winGetWindowFromId( NULL,  buttonBackID);
	buttonLogin =									TheWindowManager->winGetWindowFromId( NULL,  buttonLoginID);
	buttonCreateAccount =					TheWindowManager->winGetWindowFromId( NULL,  buttonCreateAccountID);
	buttonUseAccount =						TheWindowManager->winGetWindowFromId( NULL,  buttonUseAccountID);
	buttonDontUseAccount =				TheWindowManager->winGetWindowFromId( NULL,  buttonDontUseAccountID);
	buttonTOS =										TheWindowManager->winGetWindowFromId( NULL,  buttonTOSID);
	parentTOS =										TheWindowManager->winGetWindowFromId( NULL,  parentTOSID);
	buttonTOSOK =									TheWindowManager->winGetWindowFromId( NULL,  buttonTOSOKID);
	listboxTOS =									TheWindowManager->winGetWindowFromId( NULL,  listboxTOSID);
	comboBoxEmail =								TheWindowManager->winGetWindowFromId( NULL,  comboBoxEmailID);
	comboBoxLoginName =						TheWindowManager->winGetWindowFromId( NULL,  comboBoxLoginNameID);
	textEntryLoginName =					TheWindowManager->winGetWindowFromId( NULL,  textEntryLoginNameID);
	textEntryPassword =						TheWindowManager->winGetWindowFromId( NULL,  textEntryPasswordID);
	checkBoxRememberPassword =		TheWindowManager->winGetWindowFromId( NULL,  checkBoxRememberPasswordID);
	textEntryMonth =					TheWindowManager->winGetWindowFromId( NULL,  textEntryMonthID);
	textEntryDay =					TheWindowManager->winGetWindowFromId( NULL,  textEntryDayID);
	textEntryYear =					TheWindowManager->winGetWindowFromId( NULL,  textEntryYearID);
	*/

	EnableLoginControls(TRUE);

	// Show Menu
	layout->hide( !signInScreen );

	// Set Keyboard to Main Parent

	RaiseGSMessageBox();

	OptionPreferences optionPref;
	if (!optionPref.getBool("SawTOS", TRUE))
	{
		TheWindowManager->winSendSystemMsg( parentWOLLogin, GBM_SELECTED,
																			(WindowMsgData)buttonTOS, buttonTOSID );
	}
	TheTransitionHandler->setGroup("GameSpyLoginProfileFade");

}

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu shutdown method */
//-------------------------------------------------------------------------------------------------
static Bool loggedInOK = false;
void WOLLoginMenuShutdown( WindowLayout *layout, void *userData )
{
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		pAuthInterface->DeregisterForLoginCallback();
		pAuthInterface->SetSignInScreen(false);
	}
	signInScreen = FALSE;


	isShuttingDown = true;
	loggedInOK = false;
	TheWindowManager->clearTabList();
	if (webBrowserActive)
	{
		if (TheWebBrowser != NULL)
		{
			TheWebBrowser->closeBrowserWindow(listboxTOS);
		}
		webBrowserActive = FALSE;
	}

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("GameSpyLoginProfileFade");

}


// this is used to check if we've got all the pings
static void checkLogin()
{
	if (loggedInOK)
	{
		buttonPushed = true;
		loggedInOK = false; // don't try this again

		loginAttemptTime = 0;

		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGIN);
		nextScreen = "Menus/WOLWelcomeMenu.wnd";
		TheShell->pop();
	}

	/*
	if (loggedInOK && ThePinger && !ThePinger->arePingsInProgress())
	{
		// save off our ping string, and end those threads
		AsciiString pingStr = ThePinger->getPingString( 1000 );
		DEBUG_LOG(("Ping string is %s", pingStr.str()));
		TheGameSpyInfo->setPingString(pingStr);
		//delete ThePinger;
		//ThePinger = NULL;

		buttonPushed = true;
		loggedInOK = false; // don't try this again

		loginAttemptTime = 0;

		// start looking for group rooms
		TheGameSpyInfo->clearGroupRoomList();

		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGIN);
		nextScreen = "Menus/WOLWelcomeMenu.wnd";
		TheShell->pop();

		// read in some cached data
		GameSpyMiscPreferences mPref;
		PSPlayerStats localPSStats = GameSpyPSMessageQueueInterface::parsePlayerKVPairs(mPref.getCachedStats().str());
		localPSStats.id = TheGameSpyInfo->getLocalProfileID();
		TheGameSpyInfo->setCachedLocalPlayerStats(localPSStats);
//		TheGameSpyPSMessageQueue->trackPlayerStats(localPSStats);

		// and push the info around to other players
//		PSResponse newResp;
//		newResp.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
//		newResp.player = localPSStats;
//		TheGameSpyPSMessageQueue->addResponse(newResp);
	}
	*/
}

void GenOnline_WOLLoginMenu_LoginCallback(ELoginResult loginResult)
{
	if (!buttonPushed)
	{
		// TODO_GENONLINE: Handle failure properly
		if (loginResult == ELoginResult::Success)
		{
			ClearGSMessageBoxes();
			loggedInOK = true;

			checkLogin();
		}
		else
		{
			if (loginResult == ELoginResult::Failed)
			{
				updateSignInScreen(FALSE);
                GSMessageBoxOk(UnicodeString(L"Logging In"), UnicodeString(L"Login failed."), []()
                    {
                        TheShell->pop();
                    });
			}
            else if (loginResult == ELoginResult::UserCancelled)
            {
                // User requested, nothing to do here
            }

			
		}

	}
}


//-------------------------------------------------------------------------------------------------
/** WOL Login Menu update method */
//-------------------------------------------------------------------------------------------------
void WOLLoginMenuUpdate( WindowLayout * layout, void *userData)
{

	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (!isShuttingDown)
		updateSignInScreen(FALSE);

	/*
	if (TheShell->isAnimFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		PingResponse pingResp;
		if (ThePinger && ThePinger->getResponse(pingResp))
		{
			checkLogin();
		}

		PeerResponse resp;
		if (!loggedInOK && TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_GROUPROOM:
				{
					GameSpyGroupRoom room;
					room.m_groupID = resp.groupRoom.id;
					room.m_maxWaiting = resp.groupRoom.maxWaiting;
					room.m_name = resp.groupRoomName.c_str();
					room.m_translatedName = L"TEST";
					room.m_numGames = resp.groupRoom.numGames;
					room.m_numPlaying = resp.groupRoom.numPlaying;
					room.m_numWaiting = resp.groupRoom.numWaiting;
					TheGameSpyInfo->addGroupRoom( room );
				}
				break;
			case PeerResponse::PEERRESPONSE_LOGIN:
				{
					loggedInOK = true;

					// fetch our player info
					TheGameSpyInfo->setLocalName( resp.nick.c_str() );
					TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
					TheGameSpyInfo->loadSavedIgnoreList();
					TheGameSpyInfo->setLocalIPs(resp.player.internalIP, resp.player.externalIP);
					TheGameSpyInfo->readAdditionalDisconnects();
					//TheGameSpyInfo->setLocalEmail( resp.player.email );
					//TheGameSpyInfo->setLocalPassword( resp)

					GameSpyMiscPreferences miscPref;
					TheGameSpyInfo->setMaxMessagesPerUpdate(miscPref.getMaxMessagesPerUpdate());
				}
				break;
			case PeerResponse::PEERRESPONSE_DISCONNECT:
				{
					loginAttemptTime = 0;
					UnicodeString title, body;
					AsciiString disconMunkee;
					disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
					title = TheGameText->fetch( "GUI:GSErrorTitle" );
					body = TheGameText->fetch( disconMunkee );
					GSMessageBoxOk( title, body );
					EnableLoginControls( TRUE );

					// kill & restart the threads
					AsciiString motd = TheGameSpyInfo->getMOTD();
					AsciiString config = TheGameSpyInfo->getConfig();
					DEBUG_LOG(("Tearing down GameSpy from WOLLoginMenuUpdate(PEERRESPONSE_DISCONNECT)"));
					TearDownGameSpy();
					SetUpGameSpy( motd.str(), config.str() );
				}
				break;
			}
		}

		checkLogin();
	}
	

	if (TheGameSpyInfo && !buttonPushed && loginAttemptTime && (loginAttemptTime + loginTimeoutInMS < timeGetTime()))
	{
		// timed out a login attempt, so say so
		loginAttemptTime = 0;
		UnicodeString title, body;
		AsciiString disconMunkee;
		disconMunkee.format("GUI:GSDisconReason4");	// ("could not connect to server")
		title = TheGameText->fetch( "GUI:GSErrorTitle" );
		body = TheGameText->fetch( disconMunkee );
		GSMessageBoxOk( title, body );
		EnableLoginControls( TRUE );

		// kill & restart the threads
		AsciiString motd = TheGameSpyInfo->getMOTD();
		AsciiString config = TheGameSpyInfo->getConfig();
		DEBUG_LOG(("Tearing down GameSpy from WOLLoginMenuUpdate(login timeout)"));
		TearDownGameSpy();
		SetUpGameSpy( motd.str(), config.str() );
	}
	*/
	// TODO_GENONLINE: Add login timeout again

}

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLoginMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{
						// TheSuperHackers @feature arcticdolphin 08/11/2026 Escape cancels the GO sign-in like its Cancel button.
						if (signInScreen)
						{
							if (!buttonSignInCancel->winIsHidden())
								TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)buttonSignInCancel, buttonSignInCancel->winGetWindowId() );
						}
						else
						{
							TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)buttonBack, buttonBackID );
						}

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}

static Bool isNickOkay(UnicodeString nick)
{
	static const WideChar * legalIRCChars = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]`_^{|}-";

	Int len = nick.getLength();
	if (len == 0)
		return TRUE;

	if (len == 1 && nick.getCharAt(0) == L'-')
		return FALSE;

	WideChar newChar = nick.getCharAt(len-1);
	if (wcschr(legalIRCChars, newChar) == NULL)
		return FALSE;

	return TRUE;
}

static Bool isAgeOkay(AsciiString &month, AsciiString &day, AsciiString year)
{
	if(month.isEmpty() || day.isEmpty() || year.isEmpty() || year.getLength() != 4)
		return FALSE;

	Int monthInt = atoi(month.str());
	Int dayInt = atoi(day.str());

	if(monthInt > 12 || dayInt > 31)
		return FALSE;
		// setup date buffer for local region date format
	month.format("%02.2d",monthInt);
	day.format("%02.2d",dayInt);

	// test the year first
	#define DATE_BUFFER_SIZE 256
	char dateBuffer[ DATE_BUFFER_SIZE ];
	GetDateFormat( LOCALE_SYSTEM_DEFAULT,
								 0, NULL,
								 "yyyy",
								 dateBuffer, DATE_BUFFER_SIZE );
	Int sysVal = atoi(dateBuffer);
	Int userVal = atoi(year.str());
	if(sysVal - userVal >= 14)
		return TRUE;
	else if( sysVal - userVal <= 12)
		return FALSE;

	GetDateFormat( LOCALE_SYSTEM_DEFAULT,
								 0, NULL,
								 "MM",
								 dateBuffer, DATE_BUFFER_SIZE );
	sysVal = atoi(dateBuffer);
	userVal = atoi(month.str());
	if(sysVal - userVal >0 )
		return TRUE;
	else if( sysVal -userVal < 0 )
		return FALSE;
//	month.format("%02.2d",userVal);
	GetDateFormat( LOCALE_SYSTEM_DEFAULT,
								 0, NULL,
								 "dd",
								 dateBuffer, DATE_BUFFER_SIZE );
	sysVal = atoi(dateBuffer);
	userVal = atoi(day.str());
	if(sysVal - userVal< 0)
		return FALSE;
//	day.format("%02.2d",userVal);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLoginMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{


		case GWM_CREATE:
			{
				break;
			}

		case GWM_DESTROY:
			{
				break;
			}

		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}

		// someone typed in a combo box.  Clear password (or fill it in if the typed name matches a known login name)
		case GCM_UPDATE_TEXT:
			{
				UnicodeString uNick = GadgetComboBoxGetText(comboBoxLoginName);
				UnicodeString uEmail = GadgetComboBoxGetText(comboBoxEmail);
				AsciiString nick, email;
				nick.translate(uNick);
				email.translate(uEmail);
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				UnicodeString trimmedNick = uNick, trimmedEmail = uEmail;
				trimmedNick.trim();
				trimmedEmail.trim();
				if (!trimmedNick.isEmpty())
				{
					trimmedNick.trimEnd(L'\\');
					trimmedNick.trimEnd(L'/');
				}
				if (!trimmedEmail.isEmpty())
				{
					trimmedEmail.trimEnd(L'\\');
					trimmedEmail.trimEnd(L'/');
				}
				if (trimmedEmail.getLength() != uEmail.getLength())
				{
					// we just trimmed a space.  set the text back and bail
					GadgetComboBoxSetText(comboBoxEmail, trimmedEmail);
					break;
				}
				if (trimmedNick.getLength() != nick.getLength())
				{
					// we just trimmed a space.  set the text back and bail
					GadgetComboBoxSetText(comboBoxLoginName, trimmedNick);
					break;
				}

				if (controlID == comboBoxEmailID)
				{
					// email changed.  look up password, and choose new login names

					// fill in the password for the selected email
					UnicodeString pass;
					pass.translate(loginPref->getPasswordForEmail(email));
					GadgetTextEntrySetText(textEntryPassword, pass);

					// fill in list of nicks for selected email, selecting the first
					AsciiStringList cachedNicks = loginPref->getNicksForEmail(email);
					AsciiStringListIterator nIt = cachedNicks.begin();
					Int selectedPos = -1;
					GadgetComboBoxReset(comboBoxLoginName);
					while (nIt != cachedNicks.end())
					{
						UnicodeString uniNick;
						uniNick.translate(*nIt);
						GadgetComboBoxAddEntry(comboBoxLoginName, uniNick, GameSpyColor[GSCOLOR_DEFAULT]);
						selectedPos = 0;
						++nIt;
					}
					if (selectedPos >= 0)
					{
						GadgetComboBoxSetSelectedPos(comboBoxLoginName, selectedPos);
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, true);
						AsciiString month,day,year;
						loginPref->getDateForEmail(email, month, day, year);
						pass.translate(month);
						GadgetTextEntrySetText(textEntryMonth, pass);
						pass.translate(day);
						GadgetTextEntrySetText(textEntryDay, pass);
						pass.translate(year);
						GadgetTextEntrySetText(textEntryYear, pass);

					}
					else
					{
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, false);
						GadgetTextEntrySetText(textEntryMonth, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryDay, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryYear, UnicodeString::TheEmptyString);

					}
				}
				else if (controlID == comboBoxLoginNameID)
				{
					// they typed a new login name.  Email & pass shouldn't change
				}

				break;
			}

		case GCM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == comboBoxEmailID)
				{
					// email changed.  look up password, and choose new login names
					UnicodeString uEmail = GadgetComboBoxGetText(comboBoxEmail);
					AsciiString email;
					email.translate(uEmail);

					// fill in the password for the selected email
					UnicodeString pass;
					pass.translate(loginPref->getPasswordForEmail(email));
					GadgetTextEntrySetText(textEntryPassword, pass);

					// fill in list of nicks for selected email, selecting the first
					AsciiStringList cachedNicks = loginPref->getNicksForEmail(email);
					AsciiStringListIterator nIt = cachedNicks.begin();
					Int selectedPos = -1;
					GadgetComboBoxReset(comboBoxLoginName);
					while (nIt != cachedNicks.end())
					{
						UnicodeString uniNick;
						uniNick.translate(*nIt);
						GadgetComboBoxAddEntry(comboBoxLoginName, uniNick, GameSpyColor[GSCOLOR_DEFAULT]);
						selectedPos = 0;
						++nIt;
					}
					if (selectedPos >= 0)
					{
						GadgetComboBoxSetSelectedPos(comboBoxLoginName, selectedPos);
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, true);
						AsciiString month,day,year;
						loginPref->getDateForEmail(email, month, day, year);
						pass.translate(month);
						GadgetTextEntrySetText(textEntryMonth, pass);
						pass.translate(day);
						GadgetTextEntrySetText(textEntryDay, pass);
						pass.translate(year);
						GadgetTextEntrySetText(textEntryYear, pass);

					}
					else
					{
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, false);
						GadgetTextEntrySetText(textEntryMonth, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryDay, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryYear, UnicodeString::TheEmptyString);
					}

				}
				else if (controlID == comboBoxLoginNameID)
				{
					// they typed a new login name.  Email & pass shouldn't change
				}
				break;
			}

		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// TheSuperHackers @feature arcticdolphin 08/11/2026 GO sign-in screen buttons.
				if (signInScreen)
				{
					GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
					if (pAuthInterface == nullptr)
						break;

					if (control == buttonSignInCancel)
					{
						buttonPushed = true;
						pAuthInterface->CancelLogin();
						break;
					}
					if (control == buttonUseAnotherAccount)
					{
						pAuthInterface->UseAnotherAccount();
						updateSignInScreen(FALSE);
						break;
					}
					Int provider = -1;
					for (Int i = 0; i < SIGN_IN_PROVIDER_COUNT; ++i)
					{
						if (control == buttonSignInProviders[i])
							provider = i;
					}
					if (provider >= 0)
					{
						pAuthInterface->OpenProviderLogin(signInProviders[provider]);
						updateSignInScreen(FALSE);
						break;
					}
				}

				// If we back out, just bail - we haven't gotten far enough to need to log out
				if ( controlID == buttonBackID )
				{
					buttonPushed = true;
					TearDownGameSpy();
					TheShell->pop();
				}
#ifdef ALLOW_NON_PROFILED_LOGIN
				else if ( controlID == buttonUseAccountID )
				{
					buttonPushed = true;
					nextScreen = "Menus/GameSpyLoginProfile.wnd";
					TheShell->pop();
					//TheShell->push( "Menus/GameSpyLoginProfile.wnd" );
				}
				else if ( controlID == buttonDontUseAccountID )
				{
					buttonPushed = true;
					nextScreen = "Menus/GameSpyLoginQuick.wnd";
					TheShell->pop();
					//TheShell->push( "Menus/GameSpyLoginQuick.wnd" );
				}
#endif // ALLOW_NON_PROFILED_LOGIN
				else if ( controlID == buttonCreateAccountID )
				{
#ifdef ALLOW_NON_PROFILED_LOGIN
					if (GameSpyUseProfiles)
					{
#endif // ALLOW_NON_PROFILED_LOGIN
						// actually attempt to create an account based on info entered
						AsciiString month, day, year;
						month.translate( GadgetTextEntryGetText(textEntryMonth) );
						day.translate( GadgetTextEntryGetText(textEntryDay) );
						year.translate( GadgetTextEntryGetText(textEntryYear) );

						if(!isAgeOkay(month, day, year))
						{
							GSMessageBoxOk(TheGameText->fetch("GUI:AgeFailedTitle"), TheGameText->fetch("GUI:AgeFailed"));
							break;
						}

						AsciiString login, password, email;
						email.translate( GadgetComboBoxGetText(comboBoxEmail) );
						login.translate( GadgetComboBoxGetText(comboBoxLoginName) );
						password.translate( GadgetTextEntryGetText(textEntryPassword) );

						if ( !email.isEmpty() && !login.isEmpty() && !password.isEmpty() )
						{
							loginAttemptTime = timeGetTime();
							BuddyRequest req;
							req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGINNEW;
							strlcpy(req.arg.login.nick, login.str(), ARRAY_SIZE(req.arg.login.nick));
							strlcpy(req.arg.login.email, email.str(), ARRAY_SIZE(req.arg.login.email));
							strlcpy(req.arg.login.password, password.str(), ARRAY_SIZE(req.arg.login.password));
							req.arg.login.hasFirewall = TRUE;

							TheGameSpyInfo->setLocalBaseName( login );
							//TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
							TheGameSpyInfo->setLocalEmail( email );
							TheGameSpyInfo->setLocalPassword( password );
							DEBUG_LOG(("before create: TheGameSpyInfo->stuff(%s/%s/%s)", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));

							TheGameSpyBuddyMessageQueue->addRequest( req );
							if(checkBoxRememberPassword && GadgetCheckBoxIsChecked(checkBoxRememberPassword))
							{
								(*loginPref)["lastName"] = login;
								(*loginPref)["lastEmail"] = email;
								(*loginPref)["useProfiles"] = "yes";
								AsciiString date;
								date = month;
								date.concat(day);
								date.concat(year);

								loginPref->addLogin(email, login, password, date);
							}

							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
						else
						{
							// user didn't fill in all info.  prompt him.
							if(email.isEmpty() && login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
							else if( email.isEmpty() && login.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmailNickname"));
							else if( email.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmailPassword"));
							else if( login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoNicknamePassword"));
							else if( email.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmail"));
							else if( password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoPassword"));
							else if( login.isEmpty() )
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoNickname"));
							else
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
						}
#ifdef ALLOW_NON_PROFILED_LOGIN
					}
					else
					{
						// not the profile screen - switch to it
						buttonPushed = TRUE;
						nextScreen = "Menus/GameSpyLoginProfile.wnd";
						TheShell->pop();
					}
#endif // ALLOW_NON_PROFILED_LOGIN
				}
				else if ( controlID == buttonLoginID )
				{
					AsciiString login, password, email;

#ifdef ALLOW_NON_PROFILED_LOGIN
					if (GameSpyUseProfiles)
					{
#endif // ALLOW_NON_PROFILED_LOGIN
						AsciiString month, day, year;
						month.translate( GadgetTextEntryGetText(textEntryMonth) );
						day.translate( GadgetTextEntryGetText(textEntryDay) );
						year.translate( GadgetTextEntryGetText(textEntryYear) );

						if(!isAgeOkay(month, day, year))
						{
							GSMessageBoxOk(TheGameText->fetch("GUI:AgeFailedTitle"), TheGameText->fetch("GUI:AgeFailed"));
							break;
						}

						email.translate( GadgetComboBoxGetText(comboBoxEmail) );
						login.translate( GadgetComboBoxGetText(comboBoxLoginName) );
						password.translate( GadgetTextEntryGetText(textEntryPassword) );

						if ( !email.isEmpty() && !login.isEmpty() && !password.isEmpty() )
						{
							loginAttemptTime = timeGetTime();
							BuddyRequest req;
							req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
							strlcpy(req.arg.login.nick, login.str(), ARRAY_SIZE(req.arg.login.nick));
							strlcpy(req.arg.login.email, email.str(), ARRAY_SIZE(req.arg.login.email));
							strlcpy(req.arg.login.password, password.str(), ARRAY_SIZE(req.arg.login.password));
							req.arg.login.hasFirewall = true;

							TheGameSpyInfo->setLocalBaseName( login );
							//TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
							TheGameSpyInfo->setLocalEmail( email );
							TheGameSpyInfo->setLocalPassword( password );
							DEBUG_LOG(("before login: TheGameSpyInfo->stuff(%s/%s/%s)", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));

							TheGameSpyBuddyMessageQueue->addRequest( req );
							if(checkBoxRememberPassword && GadgetCheckBoxIsChecked(checkBoxRememberPassword))
							{
								(*loginPref)["lastName"] = login;
								(*loginPref)["lastEmail"] = email;
								(*loginPref)["useProfiles"] = "yes";
								AsciiString date;
								date = month;
								date.concat(day);
								date.concat(year);

								loginPref->addLogin(email, login, password,date);
							}
							else
							{
								loginPref->forgetLogin(email);
							}
							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
						else
						{
							// user didn't fill in all info.  prompt him.
							if(email.isEmpty() && login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
							else if( email.isEmpty() && login.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmailNickname"));
							else if( email.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmailPassword"));
							else if( login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoNicknamePassword"));
							else if( email.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmail"));
							else if( password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoPassword"));
							else if( login.isEmpty() )
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoNickname"));
							else
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
						}
#ifdef ALLOW_NON_PROFILED_LOGIN
					}
					else
					{
						login.translate( GadgetTextEntryGetText(textEntryLoginName) );

						if ( !login.isEmpty() )
						{
							loginAttemptTime = timeGetTime();
							PeerRequest req;
							req.peerRequestType = PeerRequest::PEERREQUEST_LOGIN;
							req.nick = login.str();
							req.login.profileID = 0;
							TheGameSpyPeerMessageQueue->addRequest( req );

							(*loginPref)["lastName"] = login;
							loginPref->erase("lastEmail");
							(*loginPref)["useProfiles"] = "no";
							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
					}
#endif // ALLOW_NON_PROFILED_LOGIN

				}
				else if ( controlID == buttonTOSID )
				{
					parentTOS->winHide(FALSE);
					useWebBrowserForTOS = FALSE;//loginPref->getBool("UseTOSBrowser", TRUE);
					if (useWebBrowserForTOS && (TheWebBrowser != NULL))
					{
						TheWebBrowser->createBrowserWindow("TermsOfService", listboxTOS);
						webBrowserActive = TRUE;
					}
					else
					{
						// Okay, no web browser.  This means we're looking at a UTF-8 text file.
						GadgetListBoxReset(listboxTOS);
						AsciiString fileName;
						fileName.format("Data\\%s\\TOS.txt", GetRegistryLanguage().str());
						File *theFile = TheFileSystem->openFile(fileName.str(), File::READ);
						if (theFile)
						{
							Int size = theFile->size();

							char *fileBuf = new char[size];
							Color tosColor = GameMakeColor(255, 255, 255, 255);

							Int bytesRead = theFile->read(fileBuf, size);
							if (bytesRead == size && size > 2)
							{
								fileBuf[size-1] = 0; // just to be safe
								AsciiString asciiBuf = fileBuf+2;
								AsciiString asciiLine;
								while (asciiBuf.nextToken(&asciiLine, "\r\n"))
								{
									UnicodeString uniLine;
									uniLine = UnicodeString(MultiByteToWideCharSingleLine(asciiLine.str()).c_str());
									uniLine.trimEnd();
									DEBUG_LOG(("adding TOS line: [%ls]", uniLine.str()));
									GadgetListBoxAddEntryText(listboxTOS, uniLine, tosColor, -1);
								}

							}

							delete[] fileBuf;
							fileBuf = NULL;

							theFile->close();
							theFile = NULL;
						}
					}
					EnableLoginControls( FALSE );
					buttonBack->winEnable(FALSE);

				}
				else if ( controlID == buttonTOSOKID )
				{
					EnableLoginControls( TRUE );

					parentTOS->winHide(TRUE);
					if (useWebBrowserForTOS && (TheWebBrowser != NULL))
					{
						if (listboxTOS != NULL)
						{
							TheWebBrowser->closeBrowserWindow(listboxTOS);
						}
					}

					OptionPreferences optionPref;
					optionPref["SawTOS"] = "yes";
					optionPref.write();
					webBrowserActive = FALSE;
					buttonBack->winEnable(TRUE);
				}
				break;
			}

		case GEM_EDIT_DONE:
			{
				break;
			}
			/*
		case GEM_UPDATE_TEXT:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryLoginNameID )
				{
					UnicodeString munkee = GadgetTextEntryGetText( textEntryLoginName );
					if ( !isNickOkay( munkee ) )
					{
						munkee.removeLastChar();
						GadgetTextEntrySetText( textEntryLoginName, munkee );
					}
				}
				break;
			}
			*/
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}


