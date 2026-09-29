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

// FILE: MainMenuActions.cpp //////////////////////////////////////////////////
// See MainMenuActions.h. Bodies moved verbatim out of MainMenu.cpp's
// GBM_SELECTED handler (or its file-local quitCallback()); no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/MainMenuActions.h"

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "Common/RandomValue.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/WindowLayout.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"

#include <process.h>

namespace MainMenuActions
{

//-------------------------------------------------------------------------------------------------
void quit()
{
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_SELECTED]);
	TheShell->pop();
	TheGameEngine->setQuitting(TRUE);

	if (TheGameLogic->isInGame())
		TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
}

//-------------------------------------------------------------------------------------------------
void startSkirmishOptions()
{
	TheShell->push("Menus/SkirmishGameOptionsMenu.wnd");
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_SKIRMISH_SELECTED]);
}

//-------------------------------------------------------------------------------------------------
void openNetworkLobby()
{
	TheShell->push("Menus/LanLobbyMenu.wnd");
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
}

//-------------------------------------------------------------------------------------------------
void startOnlinePatchCheck()
{
	StartPatchCheck();
}

//-------------------------------------------------------------------------------------------------
void openOptions()
{
	// TheSuperHackers @feature RmlUi screen registry: route to the RmlUi options screen
	// unless -wnd was given or RmlUi isn't linked/initialized (see RmlUiScreenRegistry.h).
	if (RmlUiScreenRegistry::routesToRmlUi("Menus/OptionsMenu.wnd"))
	{
		RmlUiScreenRegistry::open("Menus/OptionsMenu.wnd");
	}
	else
	{
		WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
		DEBUG_ASSERTCRASH(optLayout != nullptr, ("unable to get options menu layout"));
		optLayout->runInit();
		optLayout->hide(FALSE);
		optLayout->bringForward();
	}
}

//-------------------------------------------------------------------------------------------------
void launchWorldBuilder()
{
#if defined RTS_DEBUG
	if (_spawnl(_P_NOWAIT, "WorldBuilderD.exe", "WorldBuilderD.exe", nullptr) < 0)
		MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"), nullptr);
#else
	if (_spawnl(_P_NOWAIT, "WorldBuilder.exe", "WorldBuilder.exe", nullptr) < 0)
		MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"), nullptr);
#endif
}

//-------------------------------------------------------------------------------------------------
void startPatchDownload()
{
	StartDownloadingPatches();
}

//-------------------------------------------------------------------------------------------------
void openLoadGame()
{
	TheShell->push("Menus/SaveLoad.wnd");
}

//-------------------------------------------------------------------------------------------------
void openReplayMenu()
{
	TheShell->push("Menus/ReplayMenu.wnd");
}

//-------------------------------------------------------------------------------------------------
void openCreditsMenu()
{
	TheShell->push("Menus/CreditsMenu.wnd");
}

//-------------------------------------------------------------------------------------------------
void selectCampaign(const AsciiString &campaignName)
{
	TheCampaignManager->setCampaign(campaignName);
}

//-------------------------------------------------------------------------------------------------
void startCampaignAtDifficulty(GameDifficulty diff, bool challenge)
{
	// prepareCampaignGame() game-state lines (transition/dontAllowTransitions lines dropped).
	OptionPreferences pref;
	pref.setCampaignDifficulty(diff);
	pref.write();
	TheScriptEngine->setGlobalDifficulty(diff);

	// setupGameStart() game-state lines (transition/reverseAnimatewindow lines dropped).
	TheCampaignManager->setGameDifficulty(diff);

	if (challenge)
	{
		if (TheChallengeGenerals)
			TheChallengeGenerals->setCurrentDifficulty(diff);
		TheShell->push("Menus/ChallengeMenu.wnd");
		return;
	}

	// doGameStart() game-state lines (isShuttingDown is .wnd shell-animation bookkeeping, dropped;
	// the RmlUi screen pops itself immediately instead of waiting for an animation to finish).
	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();

	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	InitRandom(0);
}

} // namespace MainMenuActions
