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

// FILE: ChallengeMenuActions.cpp ////////////////////////////////////////////
// See ChallengeMenuActions.h. Bodies moved out of ChallengeMenu.cpp's callbacks; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuActions.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/Shell.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuData.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameInfo.h"

SkirmishGameInfo *TheChallengeGameInfo = nullptr;

namespace ChallengeMenuActions
{

static const Int TELETYPE_SKIP = 2;

static ChallengeMenuData &data()
{
	return ChallengeMenuData::instance();
}

static AudioHandle lastPreviewSound = 0;
static Int introAudioMagicNumber = 0;
static Bool hasPlayedIntroAudio = FALSE;

// sets the appropriate campaign for the chosen general
static void setGeneralCampaign( Int general )
{
	if( general < 0 || general >= NUM_GENERALS )
		return;

	// determine which general and player template is selected and store it
	const GeneralPersona *generals = TheChallengeGenerals->getChallengeGenerals();
	TheCampaignManager->setCampaign( generals[general].getCampaign() );
	Int templateNum = ThePlayerTemplateStore->getTemplateNumByName( generals[general].getPlayerTemplateName() );
	TheChallengeGenerals->setCurrentPlayerTemplateNum( templateNum );

	// set up the skirmish games single player slot
	GameSlot slot;
	const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate( templateNum );
	slot.setState( SLOT_PLAYER, playerTemplate->getDisplayName() );
	slot.setPlayerTemplate( templateNum );
	TheChallengeGameInfo->setSlot( 0, slot );
}

void open()
{
	if( !TheChallengeGameInfo )
		TheChallengeGameInfo = NEW SkirmishGameInfo;

	TheChallengeGameInfo->init();
	TheChallengeGameInfo->clearSlotList();
	TheChallengeGameInfo->reset();
	TheChallengeGameInfo->enterGame();

	TheShell->showShellMap( TRUE );

	data().open();

	lastPreviewSound = 0;
	hasPlayedIntroAudio = FALSE;
}

void close( Bool popImmediate )
{
	data().m_selected = -1;
	data().touch();

	if( popImmediate )
		return;

	delete TheChallengeGameInfo;
	TheChallengeGameInfo = nullptr;

	TheAudio->removeAudioEvent( lastPreviewSound );
	lastPreviewSound = 0;
	introAudioMagicNumber = 0;
}

void hover( Int general )
{
	if( general < 0 || general >= NUM_GENERALS || general == data().m_selected )
		return;

	// preview the bio for this position
	data().showBio( general );

	// special sound for Harvard
	AudioEventRTS event( "GUILogoMouseOver" );
	TheAudio->addAudioEvent( &event );
}

void unhover( Int general )
{
	if( general < 0 || general >= NUM_GENERALS || general == data().m_selected )
		return;

	// set the bio back to the selected one, if there is one
	data().showBio( data().m_selected );
}

void select( Int general )
{
	if( general < 0 || general >= NUM_GENERALS )
		return;

	// play audio to indicate selection
	TheAudio->removeAudioEvent( lastPreviewSound );
	const GeneralPersona *generals = TheChallengeGenerals->getChallengeGenerals();
	AudioEventRTS event( generals[general].getPreviewSound() );
	lastPreviewSound = TheAudio->addAudioEvent( &event );

	data().m_selected = general;
	data().touch();
}

void play()
{
	if( TheChallengeGameInfo == nullptr )
	{
		// If this is null, then we must be on the way back out of this menu. Just eat the click.
		return;
	}

	const Int general = data().m_selected;
	if( general < 0 || general >= NUM_GENERALS )
		return;

	setGeneralCampaign( general );
	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	TheChallengeGameInfo->setMap( TheCampaignManager->getCurrentMap() );

	// turn off the last button so the screen will be pristine when the user returns
	data().m_selected = -1;
	data().m_gameStarting = TRUE;
	data().touch();

	if( TheGameLogic->isInGame() )
		TheGameLogic->clearGameData();

	// If the campaign has been reset, so has the campaign difficulty. Restore it, just in case.
	DEBUG_ASSERTCRASH( TheChallengeGenerals, ("TheChallengeGenerals are not initialized.") );
	if( TheChallengeGenerals )
	{
		TheCampaignManager->setGameDifficulty( TheChallengeGenerals->getCurrentDifficulty() );
		TheScriptEngine->setGlobalDifficulty( TheChallengeGenerals->getCurrentDifficulty() );
	}

	// put a request in the message stream for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SINGLE_PLAYER );
	msg->appendIntegerArgument( TheCampaignManager->getGameDifficulty() );
	msg->appendIntegerArgument( TheCampaignManager->getRankPoints() );

	// Added so that, even though a ChallengeGame is really a SkirmishGame in SinglePlayerGame's clothing,
	// GameEngine will still apply the default "FRAME CAP" as it does during "Solo Missions."
	msg->appendIntegerArgument( LOGICFRAMES_PER_SECOND ); // FPS limit

	InitRandom( 0 );
}

void back()
{
	TheShell->pop();
}

void update()
{
	// delay the voice for N updates after the transition is done
	if( !hasPlayedIntroAudio && TheTransitionHandler->isFinished() )
	{
		introAudioMagicNumber++;
		if( introAudioMagicNumber == 10 )
		{
			// "Choose your general."
			AudioEventRTS event( "Taunts_GCAnnouncer01" );
			TheAudio->addAudioEvent( &event );
			hasPlayedIntroAudio = TRUE;
		}
	}

	data().typeBio( TELETYPE_SKIP );
}

}
