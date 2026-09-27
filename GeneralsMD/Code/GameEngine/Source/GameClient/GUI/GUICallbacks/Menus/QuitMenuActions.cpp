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

// FILE: QuitMenuActions.cpp ///////////////////////////////////////////////////
// See QuitMenuActions.h. Bodies moved out of QuitMenu.cpp's ToggleQuitMenu()
// and its restartMissionMenu()/surrenderQuitMenu() statics; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/QuitMenuActions.h"

#include "Common/FramePacer.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"

namespace QuitMenuActions
{

bool useNoSaveVariant()
{
	return TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInReplayGame();
}

const char *restartLabelKey()
{
	if (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSkirmishGame())
		return TheGameLogic->isInSkirmishGame() ? "GUI:RestartGame" : "GUI:Surrender";
	if (TheGameLogic->isInReplayGame())
		return "GUI:RestartGame";
	return "GUI:RestartMission";
}

bool isRestartEnabled()
{
	if (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSkirmishGame())
	{
		// can't surrender when you're dead, but skirmish restart is always allowed
		if (!TheGameLogic->isInSkirmishGame() &&
			(!ThePlayerList->getLocalPlayer()->isPlayerActive() || TheVictoryConditions->isLocalAlliedVictory()))
			return false;
		return true;
	}
	return true; // campaign / single map / replay
}

const char *exitLabelKey()
{
	if (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSkirmishGame() || TheGameLogic->isInReplayGame())
		return "GUI:Exit";
	return "GUI:ExitMission";
}

bool isCinematicInputDisabled()
{
	return TheInGameUI->getInputEnabled() == FALSE;
}

void pauseForOpen()
{
	if (TheGameLogic->isInSkirmishGame())
		TheGameLogic->setGamePaused(TRUE);
	else if (!TheGameLogic->isInMultiplayerGame())
		TheGameLogic->setGamePaused(TRUE);
}

void exit()
{
	TheGameLogic->quit(FALSE);
}

//-------------------------------------------------------------------------------------------------
static void surrender()
{
	if (TheVictoryConditions->isLocalAlliedVictory())
		return;

	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
	msg->appendBooleanArgument(TRUE);

	TheInGameUI->setClientQuiet(TRUE);
}

//-------------------------------------------------------------------------------------------------
static void restartMission()
{
	Int gameMode = TheGameLogic->getGameMode();
	AsciiString mapName = TheGlobalData->m_mapName;

	// TheSuperHackers @bugfix Caball009 07/02/2026 Reuse the previous seed value for the new skirmish match to prevent mismatches.
	// Campaign, challenge, and skirmish single-player scenarios all use GAME_SINGLE_PLAYER and are expected to use 0 as seed value.
	DEBUG_ASSERTCRASH((TheSkirmishGameInfo != nullptr) == (gameMode == GAME_SKIRMISH), ("Unexpected game mode on map / mission restart"));
	const Int seed = TheSkirmishGameInfo ? TheSkirmishGameInfo->getSeed() : 0;

	//
	// if the map name was from a save game it will have "Save/" at the front of it,
	// we want to go back to the original pristine map string for the map name when restarting
	//
	if (TheGameState->isInSaveDirectory(mapName))
		mapName = TheGameState->getPristineMapName();

	// End the current game
	AsciiString replayFile = TheRecorder->getCurrentReplayFilename();
	if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
	{
		TheRecorder->stopRecording();
	}

	Int rankPointsStartedWith = TheGameLogic->getRankPointsToAddAtGameStart();// must write down before reset
	GameDifficulty diff = TheScriptEngine->getGlobalDifficulty();
	Int fps = TheFramePacer->getFramesPerSecondLimit();

	TheGameLogic->clearGameData(FALSE);
	TheGameEngine->setQuitting(FALSE);

	if (replayFile.isNotEmpty())
	{
		TheRecorder->playbackFile(replayFile);
	}
	else
	{
		// send a message to the logic for a new game
		TheWritableGlobalData->m_pendingFile = mapName;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(gameMode);
		msg->appendIntegerArgument(diff);
		msg->appendIntegerArgument(rankPointsStartedWith);
		msg->appendIntegerArgument(fps);
		DEBUG_LOG(("Restarting game mode %d, Diff=%d, RankPoints=%d", gameMode,
																																		TheScriptEngine->getGlobalDifficulty(),
																																		rankPointsStartedWith)
							);

		InitRandom(seed);
	}
	TheInGameUI->setClientQuiet( TRUE );
}

void confirmRestartOrSurrender()
{
	if (TheGameLogic->isInMultiplayerGame())
		surrender();
	else
		restartMission();
}

} // namespace QuitMenuActions
