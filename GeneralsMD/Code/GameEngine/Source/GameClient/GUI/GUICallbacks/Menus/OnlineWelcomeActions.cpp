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

// FILE: OnlineWelcomeActions.cpp /////////////////////////////////////////////////
// See OnlineWelcomeActions.h. Extracted verbatim from WOLWelcomeMenu.cpp's live
// GENERALS_ONLINE code path.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeActions.h"

#include "Common/GameEngine.h"
#include "Common/GameSpyMiscPreferences.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

namespace OnlineWelcomeActions
{

//-------------------------------------------------------------------------------------------------
Bool canStartQuickMatch()
{
	GameSpyMiscPreferences mPref;
	if ((TheDisplay->getWidth() != 800 || TheDisplay->getHeight() != 600) && mPref.getQuickMatchResLocked())
	{
		GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:QuickMatch800x600"));
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void requestLogout()
{
	// NGMP: Don't need to logout here, just kill the WS connection, that triggers a log out
	NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);

	DEBUG_LOG(("Tearing down GeneralsOnline from OnlineWelcomeActions::requestLogout()\n"));
}

//-------------------------------------------------------------------------------------------------
void openMyInfo()
{
	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr)
	{
		SetLookAtPlayer(pAuthInterface->GetUserID(), UnicodeString(pAuthInterface->GetDisplayNameW().c_str()));
		GameSpyToggleOverlay(GSOVERLAY_PLAYERINFO);
	}
}

//-------------------------------------------------------------------------------------------------
void openOptions()
{
	GameSpyOpenOverlay(GSOVERLAY_OPTIONS);
}

//-------------------------------------------------------------------------------------------------
void toggleBuddiesOverlay()
{
	GameSpyToggleOverlay(GSOVERLAY_BUDDY);
}

//-------------------------------------------------------------------------------------------------
Bool consumePendingFullTeardown()
{
	if (NGMP_OnlineServicesManager::GetInstance() != nullptr && NGMP_OnlineServicesManager::GetInstance()->IsPendingFullTeardown())
	{
		NGMP_OnlineServicesManager::GetInstance()->ConsumePendingFullTeardown();
		return TRUE;
	}
	return FALSE;
}

} // namespace OnlineWelcomeActions
