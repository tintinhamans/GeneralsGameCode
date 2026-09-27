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

// FILE: ScoreScreenActions.cpp ////////////////////////////////////////////////
// See ScoreScreenActions.h. Bodies moved verbatim out of ScoreScreen.cpp's
// GBM_SELECTED/GEM_EDIT_DONE handlers; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ScoreScreenActions.h"

#include "Common/GameEngine.h"
#include "Common/ReplaySimulation.h"
#include "Common/UnicodeString.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"

namespace ScoreScreenActions
{

//-------------------------------------------------------------------------------------------------
void pressOk()
{
	GameSpyCloseOverlay(GSOVERLAY_BUDDY);
	TheShell->pop();
	TheCampaignManager->setCampaign(AsciiString::TheEmptyString);

	if ( ReplaySimulation::getReplayCount() > 0 )
	{
		ReplaySimulation::stop();
		TheGameEngine->setQuitting(TRUE);
	}
}

//-------------------------------------------------------------------------------------------------
Bool pressContinue(ScoreScreenModeType mode, Bool buttonIsFinishCampaign)
{
	Bool replayWasPressed = FALSE;

	if( ReplaySimulation::getReplayCount() > 0 )
	{
		TheGameEngine->setQuitting(TRUE);
	}
	else
	{
		if(!buttonIsFinishCampaign)
			replayWasPressed = TRUE;
		if( mode == SCORESCREENMODE_SINGLEPLAYER)
		{
			AsciiString mapName = TheCampaignManager->getCurrentMap();

			if( mapName.isEmpty() )
			{
				replayWasPressed = FALSE;
				TheShell->pop();
			}
			else
			{

			}
		}
		else if (mode == SCORESCREENMODE_INTERNET)
		{
			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if (pLobbyInterface != nullptr)
			{
				uint64_t currentMatchID = pLobbyInterface->GetCurrentMatchID();

				if (currentMatchID != 0)
				{
					AsciiString strMatchURL;
#if defined(USE_TEST_ENV)
					strMatchURL.format("https://www.playgenerals.online/viewmatch?match=%" PRIu64 "&env=test", currentMatchID);
#else
					strMatchURL.format("https://strata.gamereplays.org/zh/match/%" PRIu64, currentMatchID);
#endif
					ShellExecuteA(NULL, "open", strMatchURL.str(), NULL, NULL, SW_SHOWNORMAL);
				}
			}
		}
	}

	return replayWasPressed;
}

//-------------------------------------------------------------------------------------------------
void toggleBuddyOverlay()
{
	GameSpyToggleOverlay( GSOVERLAY_BUDDY );
}

//-------------------------------------------------------------------------------------------------
void startSaveReplayFlow()
{
	WindowLayout *saveReplayLayout = TheShell->getPopupReplayLayout();
	DEBUG_ASSERTCRASH( saveReplayLayout, ("Unable to get save replay menu layout.") );
	saveReplayLayout->runInit();
	saveReplayLayout->hide( FALSE );
	saveReplayLayout->bringForward();
}

//-------------------------------------------------------------------------------------------------
void sendChat(const UnicodeString &text, Bool isEmote)
{
	if (!text.isEmpty())
		if (TheLAN)
			TheLAN->RequestChat(text, isEmote ? LANAPIInterface::LANCHAT_EMOTE : LANAPIInterface::LANCHAT_NORMAL);
			//add the gamespy chat request here
}

}
