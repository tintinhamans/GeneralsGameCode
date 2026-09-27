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

// FILE: ScoreScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Jun 2002
//
//	Filename: 	ScoreScreen.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Gui callbacks for the Score Screen
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BattleHonors.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/ReplaySimulation.h"
#include "Common/ScoreKeeper.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/ThingFactory.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"
#include "GameClient/Display.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/VideoPlayer.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GUI/GUICallbacks/Menus/ScoreScreenActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ScoreScreenData.h"

#include "../NGMP_interfaces.h"
#include "../OnlineServices_Init.h"
#include "../OnlineServices_StatsInterface.h"


//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonOkID = NAMEKEY_INVALID;
///static NameKeyType buttonRehostID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType chatBoxBorderID = NAMEKEY_INVALID;
static NameKeyType buttonContinueID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
static NameKeyType buttonSaveReplayID = NAMEKEY_INVALID;
static NameKeyType backdropID = NAMEKEY_INVALID;

static GameWindow *parent = nullptr;
static GameWindow *buttonOk = nullptr;
//static GameWindow *buttonRehost = nullptr;
static GameWindow *buttonContinue = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *chatBoxBorder = nullptr;
static GameWindow *buttonBuddies = nullptr;
static GameWindow *staticTextGameSaved = nullptr;
static GameWindow *backdrop = nullptr;
static GameWindow *challengePortrait = nullptr;
static GameWindow *challengeRemarks = nullptr;
static GameWindow *challengeWinLossText = nullptr;
static GameWindow *gadgetParent = nullptr;

static Bool overidePlayerDisplayName = FALSE;

//External declarations
NameKeyType listboxChatWindowScoreScreenID = NAMEKEY_INVALID;
GameWindow *listboxChatWindowScoreScreen = nullptr;

NameKeyType listboxAcademyWindowScoreScreenID = NAMEKEY_INVALID;
GameWindow *listboxAcademyWindowScoreScreen = nullptr;
NameKeyType staticTextAcademyTitleID = NAMEKEY_INVALID;
GameWindow *staticTextAcademyTitle = nullptr;


std::string LastReplayFileName;
static Bool canSaveReplay = FALSE;
extern void PopupReplayUpdate(WindowLayout *layout, void *userData);

void initSinglePlayer( void );
void finishSinglePlayerInit( void );
static Bool s_needToFinishSinglePlayerInit = FALSE;
static Bool buttonIsFinishCampaign = FALSE;
static WindowLayout *s_blankLayout = nullptr;

void initSkirmish( void );
void initLANMultiPlayer(void);
void initInternetMultiPlayer(void);
void initReplayMultiPlayer(void);
void initReplaySinglePlayer(void);
void applyScoreScreenLayout( const ScoreScreenLayout &layout );
void grabMultiPlayerInfo( void );
void grabSinglePlayerInfo( void );
void hideWindows( Int pos );
void ScoreScreenEnableControls(Bool enable);
void displayChallengewinLoss( Image *imageGeneral, AsciiString strGeneral );
static ScoreScreenModeType screenType;
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

void startNextCampaignGame(void)
{
	TheShell->popImmediate();
	TheShell->hideShell();
	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	if (TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
	{
		DEBUG_ASSERTCRASH( TheChallengeGameInfo, ("TheChallengeGameInfo doesn't exist.") );
		TheChallengeGameInfo->init();  
		TheChallengeGameInfo->clearSlotList();
		TheChallengeGameInfo->reset();
		TheChallengeGameInfo->enterGame();
		TheChallengeGameInfo->setMap(TheCampaignManager->getCurrentMap());

		Int templateNum = TheChallengeGenerals->getCurrentPlayerTemplateNum();
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		GameSlot slot;
		slot.setState(SLOT_PLAYER, playerTemplate->getDisplayName());
		slot.setPlayerTemplate(templateNum);
		TheChallengeGameInfo->setSlot(0, slot);
		
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData();
	}

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	
	InitRandom(0);
}


void ScoreScreenEnableControls(Bool enable)
{
	// if we are using the button, do the enable thing.
	if ((buttonOk != nullptr) && (buttonOk->winIsHidden() == FALSE)) {
		buttonOk->winEnable(enable);
	}

	if ((buttonContinue != nullptr) && (buttonContinue->winIsHidden() == FALSE)) {
		buttonContinue->winEnable(enable);
	}

	if ((buttonBuddies != nullptr) && (buttonBuddies->winIsHidden() == FALSE)) {
		buttonBuddies->winEnable(enable);
	}

	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if ((buttonSaveReplay != nullptr) && (buttonSaveReplay->winIsHidden() == FALSE)) {
		if (!canSaveReplay)
			enable = FALSE;
		buttonSaveReplay->winEnable(enable);
	}
}

extern Bool DontShowMainMenu; //KRIS
Bool g_playMusic = FALSE;
Bool ReplayWasPressed = FALSE;

Bool g_bNeedToTakeDoneEOGScreenshot = FALSE;
int64_t g_TimeEnterState = -1;
/** Initialize the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenInit( WindowLayout *layout, void *userData )
{
	//Play music after subsystems get reset including the audio...
	g_playMusic = TRUE;
	g_bNeedToTakeDoneEOGScreenshot = FALSE;
	
	if (TheGameSpyInfo)
	{
		DEBUG_LOG(("ScoreScreenInit(): TheGameSpyInfo->stuff(%s/%s/%s)", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));
	}

	DontShowMainMenu = TRUE; //KRIS
	buttonIsFinishCampaign = FALSE;

	//Store the keys so we have them later
	parentID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ParentScoreScreen" );
	buttonOkID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonOk" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:TextEntryChat" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonEmote" );
	listboxChatWindowScoreScreenID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ListboxChatWindowScoreScreen" );
	listboxAcademyWindowScoreScreenID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ListboxWarschoolAdvice" );
	staticTextAcademyTitleID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:StaticTextWarSchool" );
//	buttonRehostID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonRehost" );
	chatBoxBorderID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ChatBoxBorder" );
	buttonBuddiesID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonBuddy" );
	buttonContinueID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonContinue" );
	buttonSaveReplayID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonSaveReplay" );
	backdropID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:MainBackdrop" );

	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	buttonOk = TheWindowManager->winGetWindowFromId( parent, buttonOkID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parent, textEntryChatID );
	buttonEmote = TheWindowManager->winGetWindowFromId( parent,buttonEmoteID  );
	listboxChatWindowScoreScreen = TheWindowManager->winGetWindowFromId( parent, listboxChatWindowScoreScreenID );
	listboxAcademyWindowScoreScreen = TheWindowManager->winGetWindowFromId( parent, listboxAcademyWindowScoreScreenID );
	staticTextAcademyTitle = TheWindowManager->winGetWindowFromId( parent, staticTextAcademyTitleID );

//	buttonRehost = TheWindowManager->winGetWindowFromId( parent, buttonRehostID );
	chatBoxBorder = TheWindowManager->winGetWindowFromId( parent, chatBoxBorderID );
	buttonContinue = TheWindowManager->winGetWindowFromId( parent, buttonContinueID );
	buttonBuddies = TheWindowManager->winGetWindowFromId( parent, buttonBuddiesID );
	staticTextGameSaved= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:StaticTextGameSaveComplete") );
	backdrop = TheWindowManager->winGetWindowFromId( parent, backdropID );
	challengePortrait = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:BigPortrait") );
	challengeWinLossText = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:ChallengeWinLossText") );
	challengeRemarks = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GeneralRemarks") );
	gadgetParent = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GadgetParent") );
	// get the replay filename for later (not full path)
	LastReplayFileName = TheRecorder->getLastReplayFileName().str();
	staticTextGameSaved->winHide(TRUE);
	overidePlayerDisplayName = FALSE;
	WindowLayout *replayLayout = TheShell->getPopupReplayLayout();
	if (replayLayout != nullptr) {
		replayLayout->hide(TRUE);
	}
	canSaveReplay = FALSE;
	if(TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		canSaveReplay = TRUE;
	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if (TheRecorder->getMode() == RECORDERMODETYPE_NONE && buttonSaveReplay)
		buttonSaveReplay->winEnable(FALSE);

	s_needToFinishSinglePlayerInit = FALSE;
	if (TheGameLogic->isInReplayGame())
	{
		if (buttonSaveReplay)
			buttonSaveReplay->winHide(TRUE);

		if (TheRecorder->isMultiplayer())
		{
			initReplayMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initReplaySinglePlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
	}
	else
	{
		if(TheGameLogic->isInInternetGame())
		{
			initInternetMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else if( TheGameLogic->isInLanGame())
		{
			initLANMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else if( TheGameLogic->isInSkirmishGame())
		{
			initSkirmish();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initSinglePlayer();
			buttonSaveReplay->winHide(TRUE);
		}
	}

	// challenge maps have no replay (design and technical issues)
	Bool isChallengeCampaign = TheCampaignManager->getCurrentCampaign() ? TheCampaignManager->getCurrentCampaign()->isChallengeCampaign() : FALSE;
	if (isChallengeCampaign)
	{
		buttonSaveReplay->winEnable(FALSE);
		buttonSaveReplay->winHide(TRUE);
	}


	// Make Sure the layout is visible
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );
	ReplayWasPressed = FALSE;
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}

#if defined(GENERALS_ONLINE)
    // Update the communicator button anytime we get notifications
    NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
    if (pSocialInterface != nullptr)
    {
        // notifiactions callback
        pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
            {
                // update communicator button
                GameWindow *currentButtonBuddies = TheWindowManager->winGetWindowFromId(nullptr, buttonBuddiesID);
                if (currentButtonBuddies != nullptr)
                {
                    UnicodeString buttonText;

					if (numNotifications > 0)
					{
						buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
					}
					else
					{
						buttonText.format(L"%s", TheGameText->fetch("GUI:Buddies").str());
					}
					currentButtonBuddies->winSetText(buttonText);
                }
            });

        // And also initialize it
        if (buttonBuddies != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
        {
            UnicodeString buttonText;
            buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
            buttonBuddies->winSetText(buttonText);
        }
    }
#endif
}

void FixupScoreScreenMovieWindow( void )
{
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}
}

/** Shutdown the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenShutdown( WindowLayout *layout, void *userData )
{
	DontShowMainMenu = FALSE; //KRIS

#if defined(GENERALS_ONLINE)
	NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface != nullptr)
		pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged(nullptr);
#endif
	buttonBuddies = nullptr;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );

}

/** Update the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenUpdate( WindowLayout * layout, void *userData)
{
	WindowLayout *popupReplayLayout = TheShell->getPopupReplayLayout();
	if (popupReplayLayout != nullptr) {
		if (popupReplayLayout->isHidden() == FALSE) {
			PopupReplayUpdate(popupReplayLayout, nullptr);
		}
	}

	// TODO_NGMP: Find a better way of doing this... before the user exists
	if (NGMP_OnlineServicesManager::GetInstance() != nullptr && g_bNeedToTakeDoneEOGScreenshot)
	{
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
		if (currTime - g_TimeEnterState >= 1000)
		{
			g_bNeedToTakeDoneEOGScreenshot = false;

			NGMP_OnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN, std::string()); // pass no URI here, wait until we have one received from server
		}
	}

	if (s_needToFinishSinglePlayerInit)
	{
		finishSinglePlayerInit();
		s_needToFinishSinglePlayerInit = FALSE;
	}
	
	//TheGameLogic->clearGameData() gets called after ScoreScreenInit and before the
	//first ScoreScreenUpdate(), so it was creatively moved here so we can actually
	//hear the music play.
	if( TheGameInfo && g_playMusic )
	{
		g_playMusic = FALSE;

		Int localSlotNum = TheGameInfo->getLocalSlotNum();

		if (localSlotNum != -1)
		{
			GameSlot* lSlot = TheGameInfo->getSlot(localSlotNum);
			const PlayerTemplate* pt;

			if (lSlot && lSlot->getPlayerTemplate() >= 0)
				pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
			else
				pt = ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionObserver"));
			AsciiString musicName = pt->getScoreScreenMusic();
			if (!musicName.isEmpty())
			{
				TheAudio->removeAudioEvent(AHSV_StopTheMusicFade);
				AudioEventRTS event(musicName);
				event.setShouldFade(TRUE);
				TheAudio->addAudioEvent(&event);
				TheAudio->update();//Since GameEngine::update() is suspended until after I am gone... 
			}
		}

		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			if (pLobbyInterface->IsInLobby())
			{
				// commit current lobby player list to 'recently played with'
				NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
				if (pSocialInterface != nullptr)
				{
					pSocialInterface->CommitLobbyPlayerListToRecentlyPlayedWithList();
				}

				pLobbyInterface->LeaveCurrentLobby();

			}
		}
	}
}

/** Input function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenInput( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

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

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)buttonOk, buttonOkID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;

}

/** System Function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg ) 
	{
		// --------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;
		}

		// --------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			TheTransitionHandler->remove("ScoreScreenShow", TRUE);
			ReplayWasPressed = FALSE;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			if( controlID == buttonOkID )
			{
				ScoreScreenActions::pressOk();
			}
			else if ( controlID == buttonContinueID )
			{
				ReplayWasPressed = ScoreScreenActions::pressContinue(screenType, buttonIsFinishCampaign);
			}
			else if ( controlID == buttonBuddiesID )
			{
				ScoreScreenActions::toggleBuddyOverlay();
			}
			else if ( controlID == buttonSaveReplayID )
			{
				ScoreScreenEnableControls(FALSE);
				ScoreScreenActions::startSaveReplayFlow();
			}

			else if ( controlID == buttonEmoteID )
			{
				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				ScoreScreenActions::sendChat(txtInput, TRUE);
			}
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				AsciiString name;
				name.format("ScoreScreen.wnd:ButtonAdd%d", i);
				if( controlID == TheNameKeyGenerator->nameToKey(name))
				{
					Bool notBuddy = TRUE;
					Int playerID = (Int)GadgetButtonGetData(TheWindowManager->winGetWindowFromId(nullptr,controlID));
											// request to add a buddy
					BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator bIt;
					if( playerID > 0)
					{
						bIt = buddies->find(playerID);
						if (bIt != buddies->end())
						{
							notBuddy = FALSE;
						}
					}
					if(notBuddy)
					{
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_ADDBUDDY;
						req.arg.addbuddy.id = playerID;
						UnicodeString buddyAddstr;
						buddyAddstr = TheGameText->fetch("GUI:BuddyAddReq");
						wcslcpy(req.arg.addbuddy.text, buddyAddstr.str(), MAX_BUDDY_CHAT_LEN);
						TheGameSpyBuddyMessageQueue->addRequest(req);
					}
				}
			}

			break;
		}

		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			// Take the user's input and echo it into the chat window as well as
			// send it to the other clients on the lan
			if ( controlID == textEntryChatID )
			{

				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				ScoreScreenActions::sendChat(txtInput, FALSE);

			}

			break;
		}
	}		
	return MSG_HANDLED;
}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

/** Apply a ScoreScreenLayout to the score screen gadgets. Moved from the init*() functions' */
/** inline winHide()/winSetText() calls; the gadget names and per-field rules are unchanged. */
//-------------------------------------------------------------------------------------------------
void applyScoreScreenLayout( const ScoreScreenLayout &layout )
{
	if (textEntryChat)
		textEntryChat->winHide(!layout.m_showChatEntry);
	if (buttonEmote)
		buttonEmote->winHide(!layout.m_showEmoteButton);
	if (chatBoxBorder)
		chatBoxBorder->winHide(!layout.m_showChatBoxBorder);
	if (buttonBuddies)
		buttonBuddies->winHide(!layout.m_showBuddiesButton);
	if (buttonContinue)
	{
		buttonContinue->winHide(!layout.m_showContinueButton);
		if (!layout.m_continueButtonCaption.isEmpty())
			buttonContinue->winSetText(layout.m_continueButtonCaption);
	}
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(!layout.m_showChatLog);
	if (layout.m_touchAcademyPanel)
	{
		if( listboxAcademyWindowScoreScreen )
			listboxAcademyWindowScoreScreen->winHide( !layout.m_showAcademyPanel );
		if( staticTextAcademyTitle )
			staticTextAcademyTitle->winHide( !layout.m_showAcademyPanel );
	}
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(!layout.m_showSaveGameText);
}

/** Special Init path for making this a single player Score Screen */
//-------------------------------------------------------------------------------------------------
void initSkirmish( void )
{
	screenType = SCORESCREENMODE_SKIRMISH;
	grabMultiPlayerInfo();
	applyScoreScreenLayout(ScoreScreenLayout::forMode(screenType));
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

void PlayMovieAndBlock(AsciiString movieTitle)
{
	VideoStreamInterface *videoStream = TheVideoPlayer->open( movieTitle );
	if ( videoStream == nullptr )
	{
		return;
	}

	// Create the new buffer
	VideoBuffer *videoBuffer = TheDisplay->createVideoBuffer();
	if (	videoBuffer == nullptr ||
				!videoBuffer->allocate(	videoStream->width(),
													videoStream->height())
		)
	{
		delete videoBuffer;
		videoBuffer = nullptr;

		if ( videoStream )
		{
			videoStream->close();
			videoStream = nullptr;
		}

		return;
	}

	// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
	// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

	GameWindow *movieWindow = s_blankLayout->getFirstWindow();
	TheWritableGlobalData->m_loadScreenRender = TRUE;
	while (videoStream->frameIndex() < videoStream->frameCount() - 1)
	{
		// TheSuperHackers @feature User can now skip video by pressing ESC
		if (TheKeyboard)
		{
			TheKeyboard->UPDATE();
			KeyboardIO *io = TheKeyboard->findKey(KEY_ESC, KeyboardIO::STATUS_UNUSED);
			if (io && BitIsSet(io->state, KEY_STATE_DOWN))
			{
				io->setUsed();
				break;
			}
		}

		TheGameEngine->serviceWindowsOS();

		if(!videoStream->isFrameReady())
		{
			Sleep(1);
			continue;
		}

		videoStream->frameDecompress();
		videoStream->frameRender(videoBuffer);
		videoStream->frameNext();

		if(videoBuffer)
			movieWindow->winGetInstanceData()->setVideoBuffer(videoBuffer);

		//TheWindowManager->update();

		TheDisplay->draw();
	}
	TheWritableGlobalData->m_loadScreenRender = FALSE;
	movieWindow->winGetInstanceData()->setVideoBuffer(nullptr);

	delete videoBuffer;
	videoBuffer = nullptr;

	if (videoStream)
	{
		videoStream->close();
		videoStream = nullptr;
	}

	setFPMode();
}

void initSinglePlayer( void )
{
	screenType = SCORESCREENMODE_SINGLEPLAYER;
	TheCampaignManager->setRankPoints(ThePlayerList->getLocalPlayer()->getSkillPoints());
	TheCampaignManager->setGameDifficulty(TheScriptEngine->getGlobalDifficulty());
	grabSinglePlayerInfo();
	s_needToFinishSinglePlayerInit = TRUE;
	s_blankLayout = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(s_blankLayout,("We Couldn't Load Menus/BlankWindow.wnd"));
	s_blankLayout->hide(FALSE);
	s_blankLayout->bringForward();
	s_blankLayout->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
}

void displayChallengeWinLoss( const Image *imageGeneral, const UnicodeString strHeader, const UnicodeString strRemarks )
{
	// format the display for challenge mode win/loss
	backdrop->winHide(TRUE);
	gadgetParent->winHide(TRUE);	
	challengeWinLossText->winHide(FALSE);
	challengeRemarks->winHide(FALSE);
	challengePortrait->winHide(FALSE);
	parent->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("GeneralsChallengeWinLoss"));

	// display the defeated enemy general
	challengePortrait->winSetEnabledImage(0, imageGeneral);
	GadgetStaticTextSetText(challengeWinLossText, strHeader);
	GadgetStaticTextSetText(challengeRemarks, strRemarks);
}

void finishSinglePlayerInit( void )
{
	if(TheCampaignManager->isVictorious())
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralDefeated = general->getImageDefeated();
			const UnicodeString strGeneralDefeated = TheGameText->fetch(general->getStringDefeated());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeWinText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralDefeated, strHeader, strGeneralDefeated);

			AudioEventRTS event( general->getWinSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		TheCampaignManager->gotoNextMission();

		if(TheCampaignManager->getCurrentMap().isEmpty())
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:EndCampaign"));
			buttonIsFinishCampaign = TRUE;
			// mark us as having completed the campaign
			Campaign* campaign = TheCampaignManager->getCurrentCampaign();
			if (campaign)
			{
				GameDifficulty difficulty = TheCampaignManager->getGameDifficulty();
				SkirmishBattleHonors stats;
				if (campaign->m_name.compareNoCase("USA") == 0)
				{
					stats.setUSACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_USA);
				}

				if (campaign->m_name.compareNoCase("China") == 0)
				{
					stats.setCHINACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_CHINA);
				}

				if (campaign->m_name.compareNoCase("GLA") == 0)
				{
					stats.setGLACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_GLA);
				}

				if (campaign->m_name.compareNoCase("GLA") == 0)
				{
					stats.setGLACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_GLA);
				}

				for (int i = 0; i < MAX_GLOBAL_GENERAL_TYPES; ++i)
				{
					char campaignName[128];
					sprintf(campaignName, "CHALLENGE_%d", i);
					if (campaign->m_name.compareNoCase(campaignName) == 0)
					{
						stats.setChallengeCampaignComplete(i, difficulty);
						stats.setHonors(BATTLE_HONOR_CHALLENGE_MODE);
					}
				}

				stats.write();

				if (buttonOk)
					buttonOk->winHide(TRUE);
				if (buttonContinue)
					buttonContinue->winHide(TRUE);
				if (textEntryChat)
					textEntryChat->winHide(TRUE);
				if (buttonEmote)
					buttonEmote->winHide(TRUE);
				if (listboxChatWindowScoreScreen)
					listboxChatWindowScoreScreen->winHide(TRUE);
				if( listboxAcademyWindowScoreScreen )
					listboxAcademyWindowScoreScreen->winHide( TRUE );
				if( staticTextAcademyTitle )
					staticTextAcademyTitle->winHide( TRUE );
				if (chatBoxBorder)
					chatBoxBorder->winHide(TRUE);
				if (buttonBuddies)
					buttonBuddies->winHide(TRUE);
				if (campaign->getFinalVictoryMovie().isNotEmpty())
				{
					AsciiString vidName;
					vidName = campaign->getFinalVictoryMovie();
					Bool useLowRes = FALSE;
					if (TheGameLODManager) {
						if (!TheGameLODManager->didMemPass()) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->getRecommendedStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->getStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
					}
					if(!useLowRes)
						PlayMovieAndBlock(vidName);
				}
			}
		}
		else 
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:SaveAndContinue"));
			
			// auto save game
			TheGameState->missionSave();
			if(staticTextGameSaved)
				staticTextGameSaved->winHide(FALSE);
		}
	}
	else
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralVictorious = general->getImageVictorious();
			const UnicodeString strGeneralVictorious = TheGameText->fetch(general->getStringVictorious());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeLossText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralVictorious, strHeader, strGeneralVictorious);

			AudioEventRTS event( general->getLossSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:Retry"));

	}

	TheInGameUI->freeMessageResources();

	if (s_blankLayout)
	{
		s_blankLayout->destroyWindows();
		deleteInstance(s_blankLayout);
		s_blankLayout = nullptr;
	}

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );

	if (buttonOk)
		buttonOk->winHide(FALSE);
	if (buttonContinue)
		buttonContinue->winHide(FALSE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);

	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( TRUE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( TRUE );

	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);

	// need to do this here
	if ( TheCampaignManager->getCurrentCampaign()
	 && !TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		TheTransitionHandler->setGroup("ScoreScreenShow");
}

/** Special Init path for making this a single player replay Score Screen */
//-------------------------------------------------------------------------------------------------
void initReplaySinglePlayer( void )
{
	screenType = SCORESCREENMODE_REPLAY;
	grabSinglePlayerInfo();
	applyScoreScreenLayout(ScoreScreenLayout::forMode(screenType));

//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

/** Special Init path for making this a Multiplayer Score Screen(LAN) */
//-------------------------------------------------------------------------------------------------
void initLANMultiPlayer(void)
{
	screenType = SCORESCREENMODE_LAN;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );
	applyScoreScreenLayout(ScoreScreenLayout::forMode(screenType));
}

/** Special Init path for making this a Multiplayer Score Screen(Internet) */
//-------------------------------------------------------------------------------------------------
void initInternetMultiPlayer(void)
{
	screenType = SCORESCREENMODE_INTERNET;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );

	ScoreScreenLayout layout = ScoreScreenLayout::forMode(screenType);

	// attempt to register our outcome
    NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
    if (pStatsInterface != nullptr && TheNGMPGame != nullptr)
    {
        Player* localPlayer = ThePlayerList->getLocalPlayer();

        if (localPlayer != nullptr)
        {
            if (!TheNGMPGame->HasCommittedOutcome())
            {
                TheNGMPGame->SetHasCommittedOutcome();
                pStatsInterface->CommitMyOutcome(localPlayer->getScoreKeeper(), TheVictoryConditions->isLocalAlliedVictory());
            }
        }
    }

	// Leave the lobby
#if defined(GENERALS_ONLINE)
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		// populate match info
		if (pLobbyInterface->IsInLobby())
		{
			LobbyEntry& lobby = pLobbyInterface->GetCurrentLobby();

 			UnicodeString strMatchID;
 			strMatchID.format(L"\nMatch ID: %" PRIu64, lobby.match_id);
 
 			UnicodeString strMatchURL;

			if (lobby.match_id == 0) // probably AI or < 2 humans
			{
				layout.m_showContinueButton = FALSE;

				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, UnicodeString(L"\nMatch data is not available online because the match had AI present OR less than 2 human players."), GameSpyColor[GSCOLOR_DEFAULT], -1);
			}
			else
			{
				layout.m_showContinueButton = TRUE;

#if defined(USE_TEST_ENV)
				strMatchURL.format(L"\nView match data, participants, replays, anti-cheat data: https://www.playgenerals.online/viewmatch?match=%" PRIu64 "&env=test", lobby.match_id);
#else
				strMatchURL.format(L"\nView match data, participants, replays, anti-cheat data: https://strata.gamereplays.org/zh/match/%" PRIu64, lobby.match_id);
#endif

				layout.m_continueButtonCaption = UnicodeString(L"VIEW MATCH ONLINE");

				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, strMatchID, GameSpyColor[GSCOLOR_DEFAULT], -1);
				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, strMatchURL, GameSpyColor[GSCOLOR_DEFAULT], -1);
			}
		}
	}
#endif

	// TODO_NGMP: Enable this once friends works
#if !defined(GENERALS_ONLINE)
	layout.m_showBuddiesButton = !(TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() == 0);
#else
	layout.m_showBuddiesButton = TRUE;
#endif

	applyScoreScreenLayout(layout);

	g_bNeedToTakeDoneEOGScreenshot = true;
	g_TimeEnterState = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

	if (!TheGameSpyBuddyMessageQueue)
		return;
	BuddyRequest req;
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_SETSTATUS;
	req.arg.status.status = GP_ONLINE;
	strcpy(req.arg.status.statusString, "Online");
	strcpy(req.arg.status.locationString, "");
	TheGameSpyBuddyMessageQueue->addRequest(req);
}

/** Special Init path for making this a Multiplayer Score Screen(Replay) */
//-------------------------------------------------------------------------------------------------
void initReplayMultiPlayer(void)
{
	screenType = SCORESCREENMODE_REPLAY;
	grabMultiPlayerInfo();
	applyScoreScreenLayout(ScoreScreenLayout::forMode(screenType));
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

/** Fill one player list row's gadgets from pre-computed data. Moved (and consolidated) from
		populatePlayerInfo(), populateSideInfo() and setObserverWindows(); the .wnd gadget names
		and per-field hide/show rules are unchanged, only the data source (ScoreScreenPlayerRow
		instead of Player, ScoreKeeper and ScoreGather pointers) differs. */
//-------------------------------------------------------------------------------------------------
void fillRowWindows( const ScoreScreenPlayerRow &row, Int pos )
{
	if(pos < 0 || pos >= MAX_SLOTS)
		return;

	AsciiString winName;
	UnicodeString winValue;
	GameWindow *win;

	// set the player/side name
	winName.format("ScoreScreen.wnd:StaticTextPlayer%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	GadgetStaticTextSetText(win, row.m_displayName);
	win->winHide(FALSE);
	win->winSetEnabledTextColors(row.m_textColor, win->winGetEnabledTextBorderColor());

	// observer marker
	winName.format("ScoreScreen.wnd:StaticTextObserver%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide( row.m_isObserver ? FALSE : TRUE );

	// the six numeric stat fields; observers show none of them
	struct StatField { const char *m_suffix; Int m_value; };
	StatField fields[] =
	{
		{ "UnitsBuilt", row.m_stats.m_totalUnitsBuilt },
		{ "UnitsLost", row.m_stats.m_totalUnitsLost },
		{ "UnitsDestroyed", row.m_stats.m_totalUnitsDestroyed },
		{ "BuildingsBuilt", row.m_stats.m_totalBuildingsBuilt },
		{ "BuildingsLost", row.m_stats.m_totalBuildingsLost },
		{ "BuildingsDestroyed", row.m_stats.m_totalBuildingsDestroyed },
		{ "Resources", row.m_stats.m_totalMoneyEarned },
	};
	for (Int i = 0; i < (Int)(sizeof(fields)/sizeof(fields[0])); ++i)
	{
		winName.format("ScoreScreen.wnd:StaticText%s%d", fields[i].m_suffix, pos);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		if (row.m_isObserver)
		{
			win->winHide(TRUE);
		}
		else
		{
			winValue.format(L"%d", fields[i].m_value);
			GadgetStaticTextSetText(win, winValue);
			win->winSetEnabledTextColors(row.m_textColor, win->winGetEnabledTextBorderColor());
			win->winHide(FALSE);
		}
	}

	// academy advice listbox: unhidden for the local player's row even with no tips
	// (matches populatePlayerInfo()'s original nesting)
	if (row.m_isLocalPlayerRow && listboxAcademyWindowScoreScreen)
	{
		listboxAcademyWindowScoreScreen->winHide( FALSE );
		if( staticTextAcademyTitle )
			staticTextAcademyTitle->winHide( FALSE );
		for (UnsignedInt i = 0; i < row.m_academyAdvice.size(); ++i)
			GadgetListBoxAddEntryText( listboxAcademyWindowScoreScreen, row.m_academyAdvice[i], GameSpyColor[GSCOLOR_DEFAULT], -1 );
	}

	// winner/side icon marker. m_touchSideIcon false (populateSideInfo's no-image case) leaves
	// this gadget at its .wnd default, matching the original's missing else branch.
	winName.format("ScoreScreen.wnd:GameWindowWinner%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	if (row.m_touchSideIcon)
	{
		win->winHide(FALSE);
		if (row.m_sideIconImage)
			win->winSetEnabledImage(0, row.m_sideIconImage);
	}
}

/** We Grab information about the players differently in Multiplayer.  We only want the players
		listed in the slots */
//-------------------------------------------------------------------------------------------------
void grabMultiPlayerInfo( void )
{
	ScoreScreenData data = ScoreScreenData::buildForMultiPlayer(screenType);

	if (parent && data.m_hasBackgroundImage)
	{
		parent->winSetEnabledImage(0, data.m_backgroundImage);
		parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
	}

	hideWindows((Int)data.m_rows.size());
	for (Int i = 0; i < (Int)data.m_rows.size(); ++i)
		fillRowWindows(data.m_rows[i], i);
}

/**	Grab the single player info */
//-------------------------------------------------------------------------------------------------
void grabSinglePlayerInfo( void )
{
	ScoreScreenData data = ScoreScreenData::buildForSinglePlayer();

	if (parent && data.m_hasBackgroundImage)
	{
		parent->winSetEnabledImage(0, data.m_backgroundImage);
		parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
	}

	for (Int i = 0; i < (Int)data.m_rows.size(); ++i)
		fillRowWindows(data.m_rows[i], i);
	hideWindows((Int)data.m_rows.size());
}

/** Hide the windows we're not using */
//-------------------------------------------------------------------------------------------------
void hideWindows( Int pos )
{
	if(pos < 0 || pos >= MAX_SLOTS)
		return;
	AsciiString winName;
	GameWindow *win;
	for( Int i = pos; i < MAX_SLOTS; ++i)
	{

		// set the player name
		winName.format("ScoreScreen.wnd:StaticTextPlayer%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the player name
		winName.format("ScoreScreen.wnd:StaticTextObserver%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units built
		winName.format("ScoreScreen.wnd:StaticTextUnitsBuilt%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units Lost
		winName.format("ScoreScreen.wnd:StaticTextUnitsLost%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units Destroyed
		winName.format("ScoreScreen.wnd:StaticTextUnitsDestroyed%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsBuilt
		winName.format("ScoreScreen.wnd:StaticTextBuildingsBuilt%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsLost
		winName.format("ScoreScreen.wnd:StaticTextBuildingsLost%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsDestroyed
		winName.format("ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total Resources
		winName.format("ScoreScreen.wnd:StaticTextResources%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total score
		/*
winName.format("ScoreScreen.wnd:StaticTextScore%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);
*/

		// Set the Game Winner marker
		winName.format("ScoreScreen.wnd:GameWindowWinner%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);


//		// Set the Game Add Buttons
//		winName.format("ScoreScreen.wnd:ButtonAdd%d", i);
//		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
//		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
//		win->winHide(TRUE);
	}
}
