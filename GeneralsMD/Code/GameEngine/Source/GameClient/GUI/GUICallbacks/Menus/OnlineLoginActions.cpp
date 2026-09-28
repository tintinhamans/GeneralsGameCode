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

// FILE: OnlineLoginActions.cpp //////////////////////////////////////////////////
// See OnlineLoginActions.h. Extracted verbatim from WOLLoginMenu.cpp's live
// GENERALS_ONLINE code path (the only path this fork ships).
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineLoginActions.h"

#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

Bool (*g_onlineLoginIsAlreadyLeavingHook)() = nullptr;
void (*g_onlineLoginSucceededHook)() = nullptr;
void (*g_onlineLoginFailedHook)() = nullptr;
void (*g_onlineLoginCancelledHook)() = nullptr;

namespace OnlineLoginActions
{

//-------------------------------------------------------------------------------------------------
Bool beginLogin()
{
	// NGMP
	ClearGSMessageBoxes();
	GSMessageBoxNoButtons(UnicodeString(L"Logging In"), UnicodeString(L"Please wait..."), true);

	// NGMP: Register for login callback
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return FALSE;
	}

	// Now we can begin login
	pAuthInterface->RegisterForLoginCallback(&OnlineLoginActions::onLoginResult);
	pAuthInterface->BeginLogin();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void endLogin()
{
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr)
	{
		pAuthInterface->DeregisterForLoginCallback();
	}
}

//-------------------------------------------------------------------------------------------------
void onLoginResult(ELoginResult loginResult)
{
	if (g_onlineLoginIsAlreadyLeavingHook && g_onlineLoginIsAlreadyLeavingHook())
		return;

	if (loginResult == ELoginResult::Success)
	{
		ClearGSMessageBoxes();

		if (g_onlineLoginSucceededHook)
			g_onlineLoginSucceededHook();
	}
	else if (loginResult == ELoginResult::Failed)
	{
		GSMessageBoxOk(UnicodeString(L"Logging In"), UnicodeString(L"Login failed."), []()
			{
				if (g_onlineLoginFailedHook)
					g_onlineLoginFailedHook();
			});
	}
	else if (loginResult == ELoginResult::UserCancelled)
	{
		// User requested, nothing to do here (mirrors NGMP_WOLLoginMenu_LoginCallback() exactly)
		if (g_onlineLoginCancelledHook)
			g_onlineLoginCancelledHook();
	}
}

} // namespace OnlineLoginActions
