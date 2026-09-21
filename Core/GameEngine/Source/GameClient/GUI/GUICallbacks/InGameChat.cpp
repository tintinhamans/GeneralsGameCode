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

// FILE: InGameChat.cpp ///////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell - June 2002
// Desc: GUI callbacks for the in-game chat entry
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LanguageFilter.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Caster/Caster.h"
#include "GameNetwork/Caster/CasterProtocol.h"

static Bool isLocalCaster()
{
	return (TheCaster != nullptr && TheCaster->isCaster()) ? TRUE : FALSE;
}

static GameWindow *chatWindow = nullptr;
static GameWindow *chatTextEntry = nullptr;
static GameWindow *chatTypeStaticText = nullptr;
static UnicodeString s_savedChat;
static InGameChatType inGameChatType;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShowInGameChat( Bool immediate )
{
	if (TheGameLogic->isInReplayGame() && !isLocalCaster())
		return;

	if (TheInGameUI->isQuitMenuVisible())
		return;

	if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
		return;

	if (chatWindow)
	{
		chatWindow->winHide(FALSE);
		chatWindow->winEnable(TRUE);
		chatTextEntry->winHide(FALSE);
		chatTextEntry->winEnable(TRUE);
		GadgetTextEntrySetText( chatTextEntry, s_savedChat );
		s_savedChat.clear();
	}
	else
	{
		chatWindow = TheWindowManager->winCreateFromScript( "InGameChat.wnd" );

		static NameKeyType textEntryChatID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:TextEntryChat" );
		chatTextEntry = TheWindowManager->winGetWindowFromId( nullptr, textEntryChatID );
		GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );

		static NameKeyType chatTypeStaticTextID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:StaticTextChatType" );
		chatTypeStaticText = TheWindowManager->winGetWindowFromId( nullptr, chatTypeStaticTextID );
	}
	TheWindowManager->winSetFocus( chatTextEntry );
	SetInGameChatType( INGAME_CHAT_EVERYONE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ResetInGameChat()
{
	if(chatWindow)
		TheWindowManager->winDestroy( chatWindow );
	chatWindow = nullptr;
	chatTextEntry = nullptr;
	chatTypeStaticText = nullptr;
	s_savedChat.clear();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void HideInGameChat( Bool immediate )
{
	if (chatWindow)
	{
		s_savedChat = GadgetTextEntryGetText( chatTextEntry );
		chatWindow->winHide(TRUE);
		chatWindow->winEnable(FALSE);
		chatTextEntry->winHide(TRUE);
		chatTextEntry->winEnable(FALSE);
		TheWindowManager->winSetFocus( nullptr );
	}
	TheWindowManager->winSetFocus( nullptr );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SetInGameChatType( InGameChatType chatType )
{
	inGameChatType = chatType;
	if (chatTypeStaticText)
	{
		// A caster uses the legacy chat hotkeys as audience selectors: the
		// "allies" key reaches casters only, while "everyone" reaches both
		// players and casters over the caster link.  Do not expose the
		// player-team labels for those two caster routes.
		if (isLocalCaster())
		{
			if (inGameChatType == INGAME_CHAT_ALLIES)
			{
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Observers") );
			}
			else if (inGameChatType == INGAME_CHAT_EVERYONE)
			{
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Everyone") );
			}
			return;
		}

		switch (inGameChatType)
		{
		case INGAME_CHAT_EVERYONE:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Everyone") );
			break;
		case INGAME_CHAT_ALLIES:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Allies") );
			break;
		case INGAME_CHAT_PLAYERS:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Players") );
			break;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool IsInGameChatActive() {
	if (chatWindow != nullptr) {
		if (chatWindow->winIsHidden() == FALSE) {
			return TRUE;
		}
	}
	return FALSE;
}

// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus();
}
extern int isThreadHosting;

Bool handleInGameSlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		UnicodeString s;
		s.format(L"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting);
		TheInGameUI->message(s);
		return TRUE; // was a slash command
	}

	return FALSE; // not a slash command
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ToggleInGameChat( Bool immediate )
{
	static Bool justHid = false;
	if (justHid)
	{
		justHid = false;
		return;
	}

	if (TheGameLogic->isInReplayGame() && !isLocalCaster())
		return;

	if (!TheGameInfo->isMultiPlayer() && TheGlobalData->m_netMinPlayers)
		return;

	if (chatWindow)
	{
		Bool show = chatWindow->winIsHidden();
		if (show)
			ShowInGameChat( immediate );
		else
		{
			if (chatTextEntry)
			{
				// Send what is there, clear it out, and hide the window
				UnicodeString msg = GadgetTextEntryGetText( chatTextEntry );
				msg.trim();
				if (!msg.isEmpty() && !handleInGameSlashCommands(msg))
				{
					const Player *localPlayer = ThePlayerList->getLocalPlayer();
					Int playerMask = 0;

					for (Int i=0; i<MAX_SLOTS; ++i)
					{
						const Player *player = ThePlayerList->getPlayerFromSlotIndex(i);
						if (player && localPlayer)
						{
							switch (inGameChatType)
							{
							case INGAME_CHAT_EVERYONE:
								if (!TheGameInfo->getConstSlot(i)->isMuted())
									playerMask |= (1<<i);
								break;
							case INGAME_CHAT_ALLIES:
								if ( (player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES &&
									localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) || player==localPlayer )
									playerMask |= (1<<i);
								break;
							case INGAME_CHAT_PLAYERS:
								if ( player == localPlayer )
									playerMask |= (1<<i);
								break;
							}
						}
					}
					TheLanguageFilter->filterLine(msg);
					if (isLocalCaster())
					{
						// Caster chat rides the caster link. TheNetwork is null
						// during replay, so it is never dereferenced here.
						const WideChar *chatText = msg.str();
						Int chatLen = msg.getLength();
						char utf8[CasterProtocol::MAX_FRAME_BYTES];
						UnsignedInt utf8Len = 0;
						UnsignedByte direction;
						Int localSlot = 0;
						char senderName[65];
						UnsignedInt senderNameLen = 0;
						UnicodeString profileName;

						utf8[0] = '\0';
						senderName[0] = '\0';
						if (chatText != nullptr && chatLen > 0)
						{
							utf8Len = CasterProtocol::wideToUtf8(chatText, (UnsignedInt)chatLen, utf8, sizeof(utf8) - 1);
						}
						utf8[utf8Len] = '\0';
						direction = (inGameChatType == INGAME_CHAT_ALLIES)
							? (UnsignedByte)CasterProtocol::CHAT_ALLIES
							: (UnsignedByte)CasterProtocol::CHAT_EVERYONE;
						if (TheLAN != nullptr)
						{
							// wideToUtf8 does not terminate, and setCasterChatName reads
							// a C string: keep one byte for the terminator and write it.
							profileName = TheLAN->GetMyName();
							senderNameLen = CasterProtocol::wideToUtf8(profileName.str(),
								(UnsignedInt)profileName.getLength(), senderName, sizeof(senderName) - 1);
							senderName[senderNameLen] = '\0';
							TheCaster->setCasterChatName(senderName);
						}
						// No stock in-game chat surface has an emote convention.
						TheCaster->sendCasterChat(direction, (UnsignedByte)localSlot, 0,
							senderName, senderNameLen, utf8, utf8Len, FALSE);
					}
					else
					{
						TheNetwork->sendChat(msg, playerMask);
					}
				}
				GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				HideInGameChat( immediate );
				justHid = true;
			}
		}
	}
	else
	{
		ShowInGameChat( immediate );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
//			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					HideInGameChat();
					return MSG_HANDLED;
					//return MSG_IGNORED;
				}

			}

			return MSG_HANDLED;

		}

	}

	return MSG_IGNORED;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatSystem( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		//---------------------------------------------------------------------------------------------
		case GGM_FOCUS_CHANGE:
		{
//			Bool focus = (Bool) mData1;
			//if (focus)
				//TheWindowManager->winSetGrabWindow( chatTextEntry );
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{
			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			ToggleInGameChat();
			//HideInGameChat();

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			static NameKeyType buttonClearID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:ButtonClear" );
			if (control && control->winGetWindowId() == buttonClearID)
			{
				if (chatTextEntry)
					GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				s_savedChat.clear();
			}
			break;

		}

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}

