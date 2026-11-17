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
// FILE: GenOnlineBuddyOverlay.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineMenus.h"

#include "Common/AudioEventRTS.h"
#include "Common/GOUIColors.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/GameText.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/Display.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GenOnline/GenOnlineSocialInterface.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessageActions.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"
#include <unordered_set>

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////


// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonHideID = NAMEKEY_INVALID;
static NameKeyType buttonAddBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteBuddyID = NAMEKEY_INVALID;
static NameKeyType textEntryID = NAMEKEY_INVALID;
static NameKeyType listboxBuddyID = NAMEKEY_INVALID;
static NameKeyType listboxChatID = NAMEKEY_INVALID;
static NameKeyType buttonAcceptBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDenyBuddyID = NAMEKEY_INVALID;
static NameKeyType radioButtonBuddiesID = NAMEKEY_INVALID;
static NameKeyType radioButtonIgnoreID = NAMEKEY_INVALID;
static NameKeyType parentBuddiesID = NAMEKEY_INVALID;
static NameKeyType parentIgnoreID = NAMEKEY_INVALID;
static NameKeyType listboxIgnoreID = NAMEKEY_INVALID;
static NameKeyType buttonNotificationID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = nullptr;
static GameWindow *buttonHide = nullptr;
static GameWindow *buttonAddBuddy = nullptr;
static GameWindow *buttonDeleteBuddy = nullptr;
static GameWindow *textEntry = nullptr;
static GameWindow *listboxBuddy = nullptr;
static GameWindow *listboxChat = nullptr;
static GameWindow *buttonAcceptBuddy = nullptr;
static GameWindow *buttonDenyBuddy = nullptr;
static GameWindow *radioButtonBuddies = nullptr;
static GameWindow *radioButtonIgnore = nullptr;
static GameWindow *parentBuddies = nullptr;
static GameWindow *parentIgnore = nullptr;
static GameWindow *listboxIgnore = nullptr;

static Bool isOverlayActive = false;
// RightClick pointers ---------------------------------------------------------------------
static GameWindow *rcMenu = nullptr;
static WindowLayout *noticeLayout = nullptr;
static UnsignedInt noticeExpires = 0;

	enum { NOTIFICATION_EXPIRES = 5000 };

void GenOnlineSetUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id);
void GenOnlineRefreshIgnoreList();
static void closeRightClickMenu(GameWindow *win);
static int64_t GetSelectedBuddyID();

void GenOnlineShowNotificationBox( AsciiString nick, UnicodeString message, bool bPlaySound);
void GenOnlineDeleteNotificationBox();
static Bool lastNotificationWasStatus = FALSE;
static Int numOnlineInNotification = 0;

class GenOnlineBuddyControls
{
public:
	GenOnlineBuddyControls();
	GameWindow *listboxChat;
	NameKeyType listboxChatID;

	GameWindow *listboxBuddies;
	NameKeyType listboxBuddiesID;

	GameWindow *textEntryEdit;
	NameKeyType textEntryEditID;
	Bool isInit;
};

static GenOnlineBuddyControls buddyControls;
GenOnlineBuddyControls::GenOnlineBuddyControls(	)
{
	listboxChat = nullptr;
	listboxChatID = NAMEKEY_INVALID;
	listboxBuddies = nullptr;
	listboxBuddiesID = NAMEKEY_INVALID;
	textEntryEdit = nullptr;
	textEntryEditID = NAMEKEY_INVALID;
	isInit = FALSE;
}
// At this point I don't give a damn about how good this way is.  I'm doing it anyway.
enum
{
	BUDDY_RESETALL_CRAP = -1,
	BUDDY_WINDOW_BUDDIES = 0,
	BUDDY_WINDOW_DIPLOMACY,
	BUDDY_WINDOW_WELCOME_SCREEN,
};

void GenOnlineInitBuddyControls(Int type)
{
	switch (type) {
	case BUDDY_RESETALL_CRAP:
		buddyControls.textEntryEditID = NAMEKEY_INVALID;
		buddyControls.textEntryEdit = nullptr;
		buddyControls.listboxBuddiesID = NAMEKEY_INVALID;
		buddyControls.listboxChatID = NAMEKEY_INVALID;
		buddyControls.listboxBuddies = nullptr;
		buddyControls.listboxChat = nullptr;
		buddyControls.isInit = FALSE;
	break;
	case BUDDY_WINDOW_BUDDIES:
		buddyControls.textEntryEditID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:TextEntryChat" );
		buddyControls.textEntryEdit = TheWindowManager->winGetWindowFromId(nullptr,  buddyControls.textEntryEditID);
		buddyControls.listboxBuddiesID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxBuddies" );
		buddyControls.listboxChatID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxBuddyChat" );
		buddyControls.listboxBuddies = TheWindowManager->winGetWindowFromId( nullptr,  buddyControls.listboxBuddiesID );
		buddyControls.listboxChat = TheWindowManager->winGetWindowFromId( nullptr,  buddyControls.listboxChatID);
		SetListBoxRowAnimMode(buddyControls.listboxChat, LIST_ROW_ANIM_SLOT);
		SetListBoxRowAnimMode(buddyControls.listboxBuddies, LIST_ROW_ANIM_ID);
		GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
		buddyControls.isInit = TRUE;

		{
			// clear current box contents
			GadgetListBoxReset(buddyControls.listboxChat);

			Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"Select a friend to start chatting"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
			GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
		}
		break;
	case BUDDY_WINDOW_DIPLOMACY:
		buddyControls.textEntryEditID = TheNameKeyGenerator->nameToKey( "Diplomacy.wnd:TextEntryChat" );
		buddyControls.textEntryEdit = TheWindowManager->winGetWindowFromId(nullptr,  buddyControls.textEntryEditID);
		buddyControls.listboxBuddiesID = TheNameKeyGenerator->nameToKey( "Diplomacy.wnd:ListboxBuddies" );
		buddyControls.listboxChatID = TheNameKeyGenerator->nameToKey( "Diplomacy.wnd:ListboxBuddyChat" );
		buddyControls.listboxBuddies = TheWindowManager->winGetWindowFromId( nullptr,  buddyControls.listboxBuddiesID );
		buddyControls.listboxChat = TheWindowManager->winGetWindowFromId( nullptr,  buddyControls.listboxChatID);
		GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
		buddyControls.isInit = TRUE;
		break;
	case BUDDY_WINDOW_WELCOME_SCREEN:
		break;
	default:
		DEBUG_CRASH(("Well, you really shouldn't have gotten here, if you really care about GUI Bugs, search for this string, you you don't care, call chris (who probably doesn't care either"));
	}

}

WindowMsgHandledType GenOnlineBuddyControlSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2)
{
	if(!buddyControls.isInit)
	{
		return MSG_IGNORED;
	}

	switch( msg )
	{
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if( controlID == buddyControls.listboxBuddiesID )
				{
					RightClickStruct *rc = (RightClickStruct *)mData2;
					WindowLayout *rcLayout;
					if(rc->pos < 0)
						break;

					GPProfile profileID = (GPProfile)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, 0);
					RCItemType itemType = (RCItemType)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, 1);
					UnicodeString nick = UnicodeString(GadgetListBoxGetText(control, rc->pos).str() + 2); // Skip the online/offline indicator

					GadgetListBoxSetSelected(control, rc->pos);
					if (itemType == ITEM_BUDDY)
						rcLayout = TheWindowManager->winCreateLayout("Menus/GenOnlineRCBuddiesMenu.wnd");
					else if (itemType == ITEM_REQUEST)
						rcLayout = TheWindowManager->winCreateLayout("Menus/GenOnlineRCBuddyRequestMenu.wnd");
					else
						rcLayout = TheWindowManager->winCreateLayout("Menus/GenOnlineRCNonBuddiesMenu.wnd");
					rcMenu = rcLayout->getFirstWindow();
					rcMenu->winGetLayout()->runInit();
					rcMenu->winBringToTop();
					rcMenu->winHide(FALSE);


					ICoord2D rcSize, rcPos;
					rcMenu->winGetSize(&rcSize.x, &rcSize.y);
					rcPos.x = rc->mouseX;
					rcPos.y = rc->mouseY;
					if(rc->mouseX + rcSize.x > TheDisplay->getWidth())
						rcPos.x = TheDisplay->getWidth() - rcSize.x;
					if(rc->mouseY + rcSize.y > TheDisplay->getHeight())
						rcPos.y = TheDisplay->getHeight() - rcSize.y;
					rcMenu->winSetPosition(rcPos.x, rcPos.y);


					GameSpyRCMenuData *rcData = NEW GameSpyRCMenuData;
					rcData->m_id = profileID;
					rcData->m_nick.translate(nick);
					rcData->m_itemType = itemType;
					GenOnlineSetUnignoreText(rcLayout, rcData->m_nick, rcData->m_id);
					rcMenu->winSetUserData((void *)rcData);
					TheWindowManager->winSetLoneWindow(rcMenu);
				}
				else if( controlID == buddyControls.listboxChatID )
				{
					// a line keeps the key of its message, the shown conversation is the selected buddy's
					RightClickStruct *rc = (RightClickStruct *)mData2;
					GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (rc->pos < 0 || pSocialInterface == nullptr)
						break;
					const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(control, rc->pos, 0)));
					const GenOnlineRealtime::ChatMessage* message = pSocialInterface->FindConversationMessage(GetSelectedBuddyID(), key);
					GenOnlineOpenMessageMenu(control, rc->pos, rc->mouseX, rc->mouseY, message != nullptr ? message->id.c_str() : "", message != nullptr ? message->author.id : 0);
				}
				else
					return MSG_IGNORED;
				break;
			}
			case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if(controlID != buddyControls.textEntryEditID)
					return MSG_IGNORED;

				// see if someone's selected
				Int selected = -1;
				GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
				if (selected >= 0)
				{
					int profileID = (int)(intptr_t)GadgetListBoxGetItemData(buddyControls.listboxBuddies, selected);

					// Block chatting with a buddy who is currently in the same game as us
					if (TheGenOnlineGame && TheGenOnlineGame->isGameInProgress())
					{
						for (Int i = 0; i < MAX_SLOTS; ++i)
						{
							GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(i);
							if (slot && slot->isHuman() && slot->m_userID == (int64_t)profileID)
							{
								if (buddyControls.listboxChat)
								{
									GadgetListBoxAddEntryText( buddyControls.listboxChat, UnicodeString(L"You cannot send messages to a buddy who is currently in your game."),
									GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
								}
								return MSG_HANDLED;
							}
						}
					}

					std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
					if (pWS != nullptr)
					{
						UnicodeString txtInput;
						txtInput.set(GadgetTextEntryGetText(buddyControls.textEntryEdit));
						GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
						txtInput.trim();
						if (!txtInput.isEmpty())
						{
							pWS->SendData_FriendMessage(txtInput, profileID);
						}
					}
						
				}
				else
				{
					// nobody selected.  Prompt the user.
					if (buddyControls.listboxChat)
					{
						GadgetListBoxAddEntryText( buddyControls.listboxChat, TheGameText->fetch("Buddy:SelectBuddyToChat"),
							GameSpyColor[GSCOLOR_DEFAULT], -1, -1 );
					}
				}
				break;
			}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

bool g_genOnlineIsProcessingRefresh = false;

void GenOnlineUpdateBuddyInfo( bool bIsAutoRefresh, bool bUseCache)
{
	if (g_genOnlineIsProcessingRefresh)
	{
		return;
	}

    GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
    if (pSocialInterface == nullptr)
    {
        return;
    }

	if (!buddyControls.isInit)
	{
		// If the UI isn't visible, still get our friends list and block list because we want to update our cache, but dont process it here
		pSocialInterface->GetFriendsList(false, nullptr);
		pSocialInterface->GetBlockList(nullptr); // TODO_SOCIA: Consider combining friends and block, original game didn't but we could for efficiency
		return;
	}

	// Get friends

	// If it's the first time in, show loading, otherwise, on refreshes, we already have some list we can use
	if (!bIsAutoRefresh)
	{
		GadgetListBoxReset(buddyControls.listboxBuddies);
		GadgetListBoxAddEntryText(buddyControls.listboxBuddies, UnicodeString(L"Loading..."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1);
	}

	// refresh block list too (but no UI to process here)
    if (!bUseCache)
    {
        pSocialInterface->GetBlockList(nullptr);
    }

    pSocialInterface->GetFriendsList(bUseCache, []()
        {
            GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocialInterface == nullptr)
			{
				return;
			}

			g_genOnlineIsProcessingRefresh = true;

            int selected;
            GPProfile selectedProfile = 0;
            int visiblePos = GadgetListBoxGetTopVisibleEntry(buddyControls.listboxBuddies);

            GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
            if (selected >= 0)
                selectedProfile = (GPProfile)GadgetListBoxGetItemData(buddyControls.listboxBuddies, selected);

            selected = -1;
            GadgetListBoxReset(buddyControls.listboxBuddies);

            // TODO_SOCIAL: De-register callback when buddy list exits, otherwise half of these UI elements probably wont exist anymore

            GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();

            int64_t user_id = pAuthInterface != nullptr ? pAuthInterface->GetUserID() : -1;

            // CURRENT GAME
			std::unordered_set<int64_t> setCurrentGameMembers;
            if (TheGenOnlineGame != nullptr)
            {
                for (Int i = 0; i < MAX_SLOTS; ++i)
                {
                    GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(i);
                    if (slot && slot->isHuman())
                    {
                        int64_t profileID = slot->m_userID;

						// dont allow self
						if (profileID != user_id)
						{
							// dont show if already friends or if in QM lobby
							bool bIsInQMLobby = !TheGenOnlineGame->isGameInProgress() && TheGenOnlineGame->isQMGame();
							if (!pSocialInterface->IsUserFriend(profileID) && !bIsInQMLobby)
							{
								setCurrentGameMembers.insert(profileID);

                                UnicodeString strName = slot->getName();

                                // insert name into box
                                int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, strName, GameSpyColor[GSCOLOR_CHAT_EMOTE], -1, -1);
                                GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(profileID), index, 0);

                                // insert status into box
                                GadgetListBoxAddEntryText(buddyControls.listboxBuddies, UnicodeString(L"In Current Lobby"), GameSpyColor[GSCOLOR_CHAT_EMOTE], index, 1);
                                GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(ITEM_NONBUDDY), index, 1);

                                if (profileID == selectedProfile)
                                    selected = index;
							}
						}
                    }
                }
            }

			// RECENTLY PLAYED WITH
			std::unordered_set<int64_t> setListedNonFriends = setCurrentGameMembers;
			for (auto& kvPair : pSocialInterface->GetRecentlyPlayedWithList())
			{
                FriendsEntry friendsEntry = kvPair.second;
                int64_t profileID = friendsEntry.user_id;

				// dont add if they are in current lobby
				if (!setCurrentGameMembers.contains(profileID))
				{
					setListedNonFriends.insert(profileID);
                    // dont need to check self here as this is checked when populating the recently played list, but do need to check friend status as it could have changed since population

                // dont show if already friends, or if they sent a request: the request row below covers them
                    if (!pSocialInterface->IsUserFriend(profileID) && !pSocialInterface->IsUserPendingRequest(profileID))
                    {
                        UnicodeString strName;
                        strName.format(L"%s", from_utf8(friendsEntry.display_name).c_str());

                        // insert name into box
                        int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, strName, GameSpyColor[GSCOLOR_CHAT_EMOTE], -1, -1);
                        GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(profileID), index, 0);

                        // insert status into box
                        GadgetListBoxAddEntryText(buddyControls.listboxBuddies, UnicodeString(L"Recently Played With"), GameSpyColor[GSCOLOR_CHAT_OWNER_EMOTE], index, 1);
                        GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(ITEM_NONBUDDY), index, 1);

                        if (profileID == selectedProfile)
                            selected = index;
                    }
				}
			}

			// CONVERSATIONS with non-friends, which the service lists when a staff member wrote or the local user is staff
			for (const std::pair<int64_t, std::string>& conversation : pSocialInterface->GetNonFriendConversations())
			{
				const int64_t profileID = conversation.first;
				if (profileID == user_id || setListedNonFriends.contains(profileID))
				{
					continue;
				}

				UnicodeString strName(from_utf8(conversation.second).c_str());
				const int numUnread = pSocialInterface->GetNumberUnreadChatMessagesForUser(profileID);
				if (numUnread > 0)
				{
					strName.format(L"%s [%d]", strName.str(), numUnread);
				}

				int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, strName, GameSpyColor[GSCOLOR_CHAT_EMOTE], -1, -1);
				GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(profileID), index, 0);
				GadgetListBoxAddEntryText(buddyControls.listboxBuddies, UnicodeString(L"Conversation"), GameSpyColor[GSCOLOR_CHAT_OWNER_EMOTE], index, 1);
				GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(ITEM_NONBUDDY), index, 1);

				if (profileID == selectedProfile)
					selected = index;
			}

			// REQUESTS
			for (auto& kvPair : pSocialInterface->GetCachedRequestsList())
			{
				FriendsEntry friendsEntry = kvPair.second;
				int64_t profileID = friendsEntry.user_id;
				// insert name into box
				UnicodeString formatStr(from_utf8(friendsEntry.display_name).c_str());
				int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
				GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(profileID), index, 0);

				// insert status into box
				formatStr = TheGameText->fetch("GUI:BuddyAddReq");
				GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
				GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(ITEM_REQUEST), index, 1);

				if (profileID == selectedProfile)
					selected = index;
			}

			// FRIENDS
			int i = 0;
			auto friendsMap = pSocialInterface->GetCachedFriendsList();
			std::vector<std::pair<int64_t, FriendsEntry>> sortedFriends(friendsMap.begin(), friendsMap.end());
			std::stable_sort(sortedFriends.begin(), sortedFriends.end(),
				[&](auto& a, auto& b) {
					Int unreadA = pSocialInterface->GetNumberUnreadChatMessagesForUser(a.second.user_id);
					Int unreadB = pSocialInterface->GetNumberUnreadChatMessagesForUser(b.second.user_id);
					if (unreadA != unreadB)
						return unreadA > unreadB;
					return a.second.online > b.second.online;
				});

			for (auto& kvPair : sortedFriends)
            {
				FriendsEntry friendsEntry = kvPair.second;
                int64_t profileID = friendsEntry.user_id;

                //AsciiString strName;

                int numUnreadMessages = 0;
                if (pSocialInterface != nullptr)
                {
                    numUnreadMessages = pSocialInterface->GetNumberUnreadChatMessagesForUser(profileID);
                }

                UnicodeString strName;
                if (friendsEntry.online)
                {
                    strName.format(L"\u25CF %s", from_utf8(friendsEntry.display_name).c_str());
                }
                else
                {
                    strName.format(L"\u25CC %s", from_utf8(friendsEntry.display_name).c_str());
                }

                if (numUnreadMessages > 0)
                {
                    strName.format(L"%s [%d]", strName.str(), numUnreadMessages);
                }

                ++i;
                //AsciiString strName = AsciiString();
                UnicodeString strStatus = UnicodeString(L"Online");
                UnicodeString strLocation = UnicodeString(L"Location!");

                // insert name into box
                UnicodeString formatStr = strName;
                //formatStr.translate(strName.str());

                // NOTE: Gamespy let you be friends with someone AND have them ignored. We don't.
                bool isSavedIgnored = false;
                Color nameColor = (isSavedIgnored) ?
                    GameSpyColor[GSCOLOR_PLAYER_IGNORED] :
					friendsEntry.online ? GameSpyColor[GSCOLOR_PLAYER_BUDDY] : GOUIColor(GOUICOLOR_OFFLINE, GameMakeColor(100, 130, 150, 255));
                int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, nameColor, -1, -1);

                if (friendsEntry.online)
                {
                    UnicodeString strGameState = TheGameText->fetch("Buddy:Online");
                    formatStr.format(L"%s - %s", strGameState.str(), from_utf8(friendsEntry.presence).c_str());
                }
                else
                {
                    // TODO_SOCIAL: Show last online
                    UnicodeString strGameState = TheGameText->fetch("Buddy:Offline");
                    formatStr.format(L"%s", strGameState.str());
                }

                GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
                GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(profileID), index, 0);
                GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void*)(ITEM_BUDDY), index, 1);

                if (profileID == selectedProfile)
                    selected = index;
            }

            // select the same guy
            if (selected >= 0)
            {
                GadgetListBoxSetSelected(buddyControls.listboxBuddies, selected);
            }

            // view the same spot
            GadgetListBoxSetTopVisibleEntry(buddyControls.listboxBuddies, visiblePos);

			g_genOnlineIsProcessingRefresh = false;
        });
}

void GenOnlineHandleBuddyResponses()
{

	if(noticeLayout && timeGetTime() > noticeExpires)
	{
		GenOnlineDeleteNotificationBox();
	}
}

void GenOnlineShowNotificationBox(AsciiString nick, UnicodeString message, bool bPlaySound)
{
//	if(!GameSpyIsOverlayOpen(GSOVERLAY_BUDDY))
//		return;
	if( !noticeLayout )
		noticeLayout = TheWindowManager->winCreateLayout( "Menus/GenOnlinePopupBuddyListNotification.wnd" );
	noticeLayout->hide( FALSE );
	if (buttonNotificationID == NAMEKEY_INVALID)
	{
		buttonNotificationID = TheNameKeyGenerator->nameToKey("PopupBuddyListNotification.wnd:ButtonNotification");
	}
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr,buttonNotificationID);
	if(!win)
	{
		GenOnlineDeleteNotificationBox();
		return;
	}

	if (lastNotificationWasStatus && numOnlineInNotification > 1)
	{
		message = TheGameText->fetch("Buddy:MultipleOnlineNotification");
	}

	if (nick.isNotEmpty())
	{
		// Generals Online nicks are UTF-8 display names
		message.format(WidenFormatSpecifiers(message.str()).c_str(), from_utf8(nick.str()).c_str());
	}
	GadgetButtonSetText(win, message);
	//GadgetStaticTextSetText(win, message);
	noticeExpires = timeGetTime() + NOTIFICATION_EXPIRES;
	noticeLayout->bringForward();

	AudioEventRTS buttonClick("GUICommunicatorIncoming");

	if (TheAudio && bPlaySound)
	{
		TheAudio->addAudioEvent( &buttonClick );
	}

}

// TheSuperHackers @bugfix arcticdolphin 07/11/2026 A notification was raised only when it appeared, so the next shell screen
// and its ruler were drawn over it. The shell raises it again after every screen change.
void GenOnlineRaiseNotificationBox()
{
	if (noticeLayout != nullptr && !noticeLayout->isHidden())
	{
		noticeLayout->bringForward();
	}
}

void GenOnlineDeleteNotificationBox()
{
	lastNotificationWasStatus = FALSE;
	numOnlineInNotification = 0;
	if(noticeLayout)
	{
		noticeLayout->destroyWindows();
		deleteInstance(noticeLayout);
		noticeLayout = nullptr;
	}
}

// A chat line keeps the key of its message as item data, so a MessageDelete finds the row again.
static void AddBuddyChatLine(const UnicodeString& text, uint32_t key)
{
	Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, text, GameSpyColor[GSCOLOR_PLAYER_BUDDY], -1, -1);
	GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_PLAYER_BUDDY], index, 1);
	if (index >= 0 && key != 0)
	{
		GadgetListBoxSetItemData(buddyControls.listboxChat, reinterpret_cast<void*>(static_cast<uintptr_t>(key)), index, 0);
	}
}

static void RemoveBuddyChatLine(uint32_t key)
{
	if (buddyControls.listboxChat == nullptr)
	{
		return;
	}
	const Int numEntries = GadgetListBoxGetNumEntries(buddyControls.listboxChat);
	for (Int row = 0; row < numEntries; ++row)
	{
		if (reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(buddyControls.listboxChat, row, 0)) == key)
		{
			TheWindowManager->winSendSystemMsg(buddyControls.listboxChat, GLM_DEL_ENTRY, static_cast<WindowMsgData>(row), 0);
			return;
		}
	}
}

// The user id of the selected buddy, or -1.
static int64_t GetSelectedBuddyID()
{
	if (buddyControls.listboxBuddies == nullptr)
	{
		return -1;
	}
	Int selected = -1;
	GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
	if (selected < 0)
	{
		return -1;
	}
	return (GPProfile)GadgetListBoxGetItemData(buddyControls.listboxBuddies, selected);
}

// Shows the cached conversation with a friend in the chat box.
static void ShowBuddyConversation(GenOnlineSocialInterface* pSocialInterface, int64_t profileID)
{
	GadgetListBoxReset(buddyControls.listboxChat);
	std::vector<GenOnlineRealtime::DirectChatLine> vecMessages = pSocialInterface->GetChatMessagesForUser(profileID);

	for (const GenOnlineRealtime::DirectChatLine& line : vecMessages)
	{
		AddBuddyChatLine(GenOnlineSocialInterface::GetChatLineText(line), line.key);
	}

	if (vecMessages.empty())
	{
		Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"This chat is empty. Send a message to start a conversation"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
	}
}

void GenOnlinePopulateOldBuddyMessages()
{
	// TODO_SOCIAL
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Buddy Overlay */
//-------------------------------------------------------------------------------------------------
void GenOnlineBuddyOverlayInit( WindowLayout *layout, void *userData )
{
	parentID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:BuddyMenuParent" );
	buttonHideID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonHide" );
	buttonAddBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonAdd" );
	buttonDeleteBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonDelete" );
	//textEntryID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:TextEntryChat" );
	//listboxBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxBuddies" );
	//listboxChatID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxBuddyChat" );
	buttonAcceptBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonYes" );
	buttonDenyBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonNo" );
	radioButtonBuddiesID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RadioButtonBuddies" );
	radioButtonIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RadioButtonIgnore" );
	parentBuddiesID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:BuddiesParent" );
	parentIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:IgnoreParent" );
	listboxIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxIgnore" );

// TODO_SOCIAL: Lobby sort list by member
	//
	// NOTE Init is only called when the UI is visible, so don't register for callbacks that you want to occur anytime
	// GO: register for callbacks
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->RegisterForRealtimeServiceUpdates();

		// TODO_SOCIAL: Maybe later dont clear chat messaages until they open the chat?
		// we opened things, clear it
		pSocialInterface->ClearGlobalNotificatations();

		pSocialInterface->RegisterForCallback_NewFriendRequest([](std::string strDisplayName)
			{
				GenOnlineUpdateBuddyInfo();
			});

		pSocialInterface->RegisterForCallback_OnChatMessage([](int64_t peer_user_id, uint32_t key, UnicodeString unicodeStr)
			{
				// Only add if the user is currently selected, otherwise show notification and just rely on the cache
				const int64_t profileID = GetSelectedBuddyID();
				if (profileID >= 0 && buddyControls.listboxChat != nullptr)
				{
					// sending to them, or getting from them, is valid
					if (profileID == peer_user_id)
					{
						AddBuddyChatLine(unicodeStr, key);

						// we read the message, so clear it
                        GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
                        if (pSocialInterface != nullptr)
                        {
                            pSocialInterface->ViewConversation(profileID);
                        }
					}
				}

                // always update this (from cache)
                GenOnlineUpdateBuddyInfo(true, true);
			});

		// The open chat shows the conversation again after its history loaded or after a reconnect.
		pSocialInterface->RegisterForCallback_OnConversationReloaded([](int64_t peer_user_id)
			{
				GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
				const int64_t profileID = GetSelectedBuddyID();
				if (pSocialInterface == nullptr || profileID < 0 || buddyControls.listboxChat == nullptr)
				{
					return;
				}
				if ((peer_user_id == -1 || peer_user_id == profileID) && pSocialInterface->CanChatWith(profileID))
				{
					ShowBuddyConversation(pSocialInterface, profileID);
					pSocialInterface->ViewConversation(profileID);
					GenOnlineUpdateBuddyInfo(true, true);
				}
			});

		pSocialInterface->RegisterForCallback_OnChatLineRemoved([](int64_t peer_user_id, uint32_t key)
			{
				if (GetSelectedBuddyID() == peer_user_id)
				{
					RemoveBuddyChatLine(key);
				}
			});
	}


	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonHide = TheWindowManager->winGetWindowFromId( parent,  buttonHideID);
	buttonAddBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonAddBuddyID);
	buttonDeleteBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonDeleteBuddyID);
	//	textEntry = TheWindowManager->winGetWindowFromId( parent,  textEntryID);
	//listboxBuddy = TheWindowManager->winGetWindowFromId( parent,  listboxBuddyID);
	//listboxChat = TheWindowManager->winGetWindowFromId( parent,  listboxChatID);
	buttonAcceptBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonAcceptBuddyID);
	buttonDenyBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonDenyBuddyID);
	radioButtonBuddies = TheWindowManager->winGetWindowFromId( parent,  radioButtonBuddiesID);
	radioButtonIgnore = TheWindowManager->winGetWindowFromId( parent,  radioButtonIgnoreID);
	parentBuddies = TheWindowManager->winGetWindowFromId( parent,  parentBuddiesID);
	parentIgnore = TheWindowManager->winGetWindowFromId( parent,  parentIgnoreID);
	listboxIgnore = TheWindowManager->winGetWindowFromId( parent,  listboxIgnoreID);

	if (radioButtonIgnore)
		GadgetRadioSetText(radioButtonIgnore, UnicodeString(L"Blocked"));

	GenOnlineInitBuddyControls(BUDDY_WINDOW_BUDDIES);

	GadgetRadioSetSelection(radioButtonBuddies,FALSE);
	parentBuddies->winHide(FALSE);
	parentIgnore->winHide(TRUE);

	//GadgetTextEntrySetText(textEntry, UnicodeString::TheEmptyString);

	GenOnlinePopulateOldBuddyMessages();

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parent );

	isOverlayActive = true;
	GenOnlineUpdateBuddyInfo();

}

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlineBuddyOverlayShutdown( WindowLayout *layout, void *userData )
{
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->DeregisterForRealtimeServiceUpdates();
	}

	listboxIgnore = NULL;

	// a right-click menu of the Communicator closes with it
	if (rcMenu != nullptr)
	{
		closeRightClickMenu(rcMenu);
		rcMenu = nullptr;
	}

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	//TheShell->shutdownComplete( layout );

	isOverlayActive = false;

	GenOnlineInitBuddyControls(BUDDY_RESETALL_CRAP);

}


//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay update method */
//-------------------------------------------------------------------------------------------------
void GenOnlineBuddyOverlayUpdate( WindowLayout * layout, void *userData)
{
	GenOnlineServicesManager* pOnlineServicesManager = GenOnlineServicesManager::GetInstance();
	if (pOnlineServicesManager == nullptr || pOnlineServicesManager->IsPendingFullTeardown())
	{
		GameSpyCloseOverlay(GSOVERLAY_BUDDY);
	}
}

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineBuddyOverlayInput( GameWindow *window, UnsignedInt msg,
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
																							(WindowMsgData)buttonHide, buttonHideID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineBuddyOverlaySystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	if(GenOnlineBuddyControlSystem(window, msg, mData1, mData2) == MSG_HANDLED)
	{
		return MSG_HANDLED;
	}
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
		case GLM_SELECTED:
		{
			GameWindow* control = (GameWindow*)mData1;
			Int controlID = control->winGetWindowId();

			if (controlID == buddyControls.listboxBuddiesID)
			{
				// restore any cached chat messages

				// clear current box contents
				GadgetListBoxReset(buddyControls.listboxChat);

				// get selection
				Int selected = -1;
				GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
				if (selected >= 0)
				{
					GPProfile profileID = (GPProfile)GadgetListBoxGetItemData(control, selected);
					UnicodeString nick = GadgetListBoxGetText(control, selected);

					GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pSocialInterface != nullptr)
					{
						// friends, a staff conversation, and anyone for staff holding chat.message_anyone
						if (!pSocialInterface->CanChatWith(profileID) && pSocialInterface->IsUserPendingRequest(profileID))
						{
                            Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"This is a pending friend request. You cannot chat with the player until you accept it."), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
                            GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
						}
						else if (!pSocialInterface->CanChatWith(profileID))
						{
                            Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"This person is in your lobby or recently played with you but is not a friend yet and cannot be chatted with. You can right click them to add or block them."), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
                            GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
						}
						else
						{
                            ShowBuddyConversation(pSocialInterface, profileID);

                            // we read the message, so clear it
                            GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
                            if (pSocialInterface != nullptr)
                            {
                                pSocialInterface->ViewConversation(profileID);
								GenOnlineUpdateBuddyInfo(true, true); // use cache
                            }
							TheWindowManager->winSetFocus(buddyControls.textEntryEdit);
						}
					}
				}
				else
				{
					// clear current box contents
					GadgetListBoxReset(buddyControls.listboxChat);

					Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"Select a friend to start chatting"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
					GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
				}
			}
			break;
		}
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if( controlID == listboxIgnoreID )
				{

					RightClickStruct* rc = (RightClickStruct*)mData2;
					WindowLayout* rcLayout;
					if (rc->pos < 0)
						break;

					GPProfile profileID = (GPProfile)GadgetListBoxGetItemData(control, rc->pos);
					UnicodeString nick = GadgetListBoxGetText(control, rc->pos);

                    GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pSocialInterface == nullptr)
					{
						break;
					}

					bool isBuddy = pSocialInterface->IsUserFriend(profileID);
					bool isRequest = pSocialInterface->IsUserPendingRequest(profileID);

					GadgetListBoxSetSelected(control, rc->pos);
					if (isBuddy)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCBuddiesMenu.wnd"));
					else if (isRequest)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCBuddyRequestMenu.wnd"));
					else
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCNonBuddiesMenu.wnd"));
					rcMenu = rcLayout->getFirstWindow();
					rcMenu->winGetLayout()->runInit();
					rcMenu->winBringToTop();
					rcMenu->winHide(FALSE);



					rcMenu->winSetPosition(rc->mouseX, rc->mouseY);
					GameSpyRCMenuData* rcData = NEW GameSpyRCMenuData;
					rcData->m_id = profileID;
					rcData->m_nick.translate(nick);
					rcData->m_itemType = (isBuddy) ? ITEM_BUDDY : ((isRequest) ? ITEM_REQUEST : ITEM_NONBUDDY);
					GenOnlineSetUnignoreText(rcLayout, rcData->m_nick, rcData->m_id);
					rcMenu->winSetUserData((void*)rcData);
					TheWindowManager->winSetLoneWindow(rcMenu);
				}
				break;
			}
		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonHideID)
				{
					GameSpyCloseOverlay( GSOVERLAY_BUDDY );
				}
				else if (controlID == radioButtonBuddiesID)
				{
					parentBuddies->winHide(FALSE);
					parentIgnore->winHide(TRUE);
				}
				else if (controlID == radioButtonIgnoreID)
				{
					parentBuddies->winHide(TRUE);
					parentIgnore->winHide(FALSE);
					GenOnlineRefreshIgnoreList();
				}
				else if (controlID == buttonAddBuddyID)
				{
					/*
					UnicodeString uName = GadgetTextEntryGetText(textEntry);
					AsciiString aName;
					aName.translate(uName);
					if (!aName.isEmpty())
					{
						TheWOLBuddyList->requestBuddyAdd(aName);
					}
					GadgetTextEntrySetText(textEntry, UnicodeString::TheEmptyString);
					*/
				}
				else if (controlID == buttonDeleteBuddyID)
				{
					/*
					int selected;
					AsciiString selectedName = AsciiString::TheEmptyString;

					GadgetListBoxGetSelected(listbox, &selected);
					if (selected >= 0)
						selectedName = TheNameKeyGenerator->keyToName((NameKeyType)(int)GadgetListBoxGetItemData(listbox, selected));

					if (!selectedName.isEmpty())
					{
						TheWOLBuddyList->requestBuddyDelete(selectedName);
					}
					*/
				}
				break;
			}
		case GLM_DOUBLE_CLICKED:
			{
				/*
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxBuddyID )
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						UnicodeString buddyName;
						GameWindow *listboxWindow = TheWindowManager->winGetWindowFromId( parent, listboxBuddyID );

							// get text of buddy name
						buddyName = GadgetListBoxGetText( listboxWindow, rowSelected,0 );
						GPProfile buddyID = (GPProfile)GadgetListBoxGetItemData( listboxWindow, rowSelected, 0 );

						Int index = -1;
						gpGetBuddyIndex(TheGPConnection, buddyID, &index);
						if (index >= 0)
						{
							GPBuddyStatus status;
							gpGetBuddyStatus(TheGPConnection, rowSelected, &status);

							UnicodeString string;
							string.format(L"To join %s in %hs:", buddyName.str(), status.locationString);
							GameSpyAddText(string, GSCOLOR_DEFAULT);

							if (status.status == GP_CHATTING)
							{
								AsciiString location = status.locationString;
								AsciiString val;
								location.nextToken(&val, "/");
								location.nextToken(&val, "/");
								location.nextToken(&val, "/");

								string.format(L"  ???");
								if (!val.isEmpty())
								{
									Int groupRoom = atoi(val.str());
									if (TheGameSpyChat->getCurrentGroupRoomID() == groupRoom)
									{
										// already there
										string.format(L"  nothing");
										GameSpyAddText(string, GSCOLOR_DEFAULT);
									}
									else
									{
										GroupRoomMap *rooms = TheGameSpyChat->getGroupRooms();
										if (rooms)
										{
											Bool needToJoin = true;
											GroupRoomMap::iterator it = rooms->find(groupRoom);
											if (it != rooms->end())
											{
												// he's in a different room
												if (TheGameSpyChat->getCurrentGroupRoomID())
												{
													string.format(L"  leave group room");
													GameSpyAddText(string, GSCOLOR_DEFAULT);

													TheGameSpyChat->leaveRoom(GroupRoom);
												}
												else if (TheGameSpyGame->isInGame())
												{
													if (TheGameSpyGame->isGameInProgress())
													{
														string.format(L"  can't leave game in progress");
														GameSpyAddText(string, GSCOLOR_DEFAULT);
														needToJoin = false;
													}
													else
													{
														string.format(L"  leave game setup");
														GameSpyAddText(string, GSCOLOR_DEFAULT);

														TheGameSpyChat->leaveRoom(StagingRoom);
														TheGameSpyGame->leaveGame();
													}
												}
												if (needToJoin)
												{
													string.format(L"  join lobby %d", groupRoom);
													TheGameSpyChat->joinGroupRoom(groupRoom);
													GameSpyAddText(string, GSCOLOR_DEFAULT);
												}
											}
										}
									}
								}
							}
						}
						else
						{
							DEBUG_CRASH(("No buddy associated with that ProfileID"));
							GameSpyUpdateBuddyOverlay();
						}
					}
				}
				*/
				break;
			}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}

WindowMsgHandledType GenOnlinePopupBuddyNotificationSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
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

		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonNotificationID)
				{
					GameSpyOpenOverlay( GSOVERLAY_BUDDY );
				}
				break;
			}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}

/*
static NameKeyType buttonAcceptBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDenyBuddyID = NAMEKEY_INVALID;
*/
static NameKeyType buttonAddID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteID = NAMEKEY_INVALID;
static NameKeyType buttonPlayID = NAMEKEY_INVALID;
static NameKeyType buttonIgnoreID = NAMEKEY_INVALID;
static NameKeyType buttonStatsID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
//static GameWindow *rCparent = nullptr;


// The GenOnline right click layouts keep the retail window names, so the names leave out the GenOnline file prefix.
static AsciiString rcWindowFile( WindowLayout *layout )
{
	AsciiString name = layout->getFilename().str() + 6;
	if (name.startsWith("GenOnline"))
	{
		name = name.str() + 9;
	}
	return name;
}

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay Right Click menu callbacks */
//-------------------------------------------------------------------------------------------------
void GenOnlineBuddyOverlayRCMenuInit( WindowLayout *layout, void *userData )
{
	AsciiString controlName;
	controlName.format("%s:ButtonAdd",rcWindowFile(layout).str());
	buttonAddID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonDelete",rcWindowFile(layout).str());
	buttonDeleteID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonPlay",rcWindowFile(layout).str());
	buttonPlayID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonIgnore",rcWindowFile(layout).str());
	buttonIgnoreID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonStats",rcWindowFile(layout).str());
	buttonStatsID =  TheNameKeyGenerator->nameToKey( controlName );
}
static void closeRightClickMenu(GameWindow *win)
{

	if(win)
	{
		WindowLayout *winLay = win->winGetLayout();
		if(!winLay)
			return;
		winLay->destroyWindows();
		deleteInstance(winLay);
		winLay = nullptr;

	}
}

// The context menu of a chat message --------------------------------------------------------------------
static GameWindow *messageMenu = nullptr;
static std::string messageMenuID;
static NameKeyType buttonReportMessageID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteMessageID = NAMEKEY_INVALID;

static void ReportChatMessage(const std::string& messageID)
{
	GenOnlineRealtime::ReportBody body;
	body.message_id = messageID;
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Post, GenOnlineRealtime::RestReportsPath(), nlohmann::json(body).dump(), [](const GenOnlineRealtime::RestOutcome& outcome)
		{
			const UnicodeString text = outcome.Succeeded()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMessageReported", L"Message reported. Staff will review it.")
				: UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str());
			GenOnlineShowNotificationBox(AsciiString::TheEmptyString, text, false);
		});
}

// Every receiver gets MessageDelete from the service, which removes the line in each chat
static void DeleteChatMessage(const std::string& messageID)
{
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Delete, GenOnlineRealtime::RestMessagePath(messageID), nlohmann::json(GenOnlineRealtime::ReasonBody()).dump(), [](const GenOnlineRealtime::RestOutcome& outcome)
		{
			if (!outcome.Succeeded())
			{
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str()), false);
			}
		});
}

void GenOnlineMessageRCMenuInit( WindowLayout *layout, void *userData )
{
	buttonReportMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonReport");
	buttonDeleteMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonDelete");
}

void GenOnlineOpenMessageMenu( GameWindow *listbox, Int row, Int mouseX, Int mouseY, const char *messageID, int64_t authorID )
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const int64_t localUserID = pAuth != nullptr ? pAuth->GetUserID() : -1;
	const std::string id = messageID != nullptr ? messageID : "";
	const GenOnlineRealtime::MessageMenu menu = GenOnlineRealtime::MessageMenuFor(id, authorID, localUserID, GenOnlineRealtimeBridge::HasPermission(GenOnlineRealtime::PERMISSION_DELETE_ANY));
	if (listbox != nullptr && row >= 0)
	{
		GadgetListBoxSetSelected(listbox, row);
	}
	if (!menu.Any())
	{
		return;
	}

	WindowLayout *layout = TheWindowManager->winCreateLayout("Menus/GenOnlineRCMessageMenu.wnd");
	if (layout == nullptr)
	{
		return;
	}
	GameWindow *menuWindow = layout->getFirstWindow();
	layout->runInit();

	GameWindow *buttonReport = TheWindowManager->winGetWindowFromId(menuWindow, buttonReportMessageID);
	GameWindow *buttonDelete = TheWindowManager->winGetWindowFromId(menuWindow, buttonDeleteMessageID);
	if (buttonReport != nullptr)
	{
		GadgetButtonSetText(buttonReport, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOReportMessage", L"Report"));
	}
	// one entry: it takes the first row and the menu loses the second
	if (buttonReport != nullptr && buttonDelete != nullptr && menu.report != menu.remove)
	{
		Int reportX, reportY, deleteX, deleteY, width, height;
		buttonReport->winGetPosition(&reportX, &reportY);
		buttonDelete->winGetPosition(&deleteX, &deleteY);
		if (!menu.report)
		{
			buttonReport->winHide(TRUE);
			buttonDelete->winSetPosition(reportX, reportY);
		}
		else
		{
			buttonDelete->winHide(TRUE);
		}
		menuWindow->winGetSize(&width, &height);
		menuWindow->winSetSize(width, height - (deleteY - reportY));
	}

	ICoord2D size, pos;
	menuWindow->winGetSize(&size.x, &size.y);
	pos.x = mouseX;
	pos.y = mouseY;
	const Int displayWidth = (Int)TheDisplay->getWidth();
	const Int displayHeight = (Int)TheDisplay->getHeight();
	if (pos.x + size.x > displayWidth)
		pos.x = displayWidth - size.x;
	if (pos.y + size.y > displayHeight)
		pos.y = displayHeight - size.y;
	menuWindow->winSetPosition(pos.x, pos.y);
	menuWindow->winBringToTop();
	menuWindow->winHide(FALSE);

	messageMenu = menuWindow;
	messageMenuID = id;
	TheWindowManager->winSetLoneWindow(menuWindow);
}

WindowMsgHandledType GenOnlineMessageRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch (msg)
	{
		case GWM_CREATE:
			break;

		case GWM_DESTROY:
			if (messageMenu == window)
			{
				messageMenu = nullptr;
				messageMenuID.clear();
			}
			break;

		case GGM_CLOSE:
			closeRightClickMenu(window);
			break;

		case GBM_SELECTED:
		{
			const Int controlID = ((GameWindow *)mData1)->winGetWindowId();
			const std::string id = messageMenuID;
			closeRightClickMenu(window);
			if (id.empty())
			{
				break;
			}
			if (controlID == buttonReportMessageID)
			{
				ReportChatMessage(id);
			}
			else if (controlID == buttonDeleteMessageID)
			{
				DeleteChatMessage(id);
			}
			break;
		}

		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

void GenOnlineRequestBuddyAdd(Int profileID, AsciiString nick)
{

	// request to add a buddy
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr)
	{
		return;
	}
	pSocialInterface->AddFriend(profileID);

	// NOTE: The original Buddy:InviteSent string didn't make its way into the final game...
	// TODO_NGMP: Use the SH localization system for strings instead
	//UnicodeString s;
	//s.format(L, nick.str());


// TODO_SOCIAL
// 	// save message for future incarnations of the buddy window
// 	BuddyMessageList* messages = TheGameSpyInfo->getBuddyMessages();
// 	BuddyMessage message;
// 	message.m_timestamp = time(NULL);
// 	message.m_senderID = 0;
// 	message.m_senderNick = "";
// 	message.m_recipientID = TheGameSpyInfo->getLocalProfileID();
// 	message.m_recipientNick = TheGameSpyInfo->getLocalBaseName();
// 	message.m_message.format(TheGameText->fetch("Buddy:InviteSentToPlayer"), nick.str());

	// TODO_SOCIAL
	// insert status into box
	//messages->push_back(message);

	DEBUG_LOG(("Inserting buddy add request"));

	// TODO_SOCIAL
	// put message on screen
	//GenOnlineInsertBuddyChat(message);

	// play audio notification
	AudioEventRTS buddyMsgAudio("GUIMessageReceived");
	if (TheAudio)
	{
		TheAudio->addAudioEvent(&buddyMsgAudio);
	}

	lastNotificationWasStatus = FALSE;
	numOnlineInNotification = 0;
	GenOnlineShowNotificationBox(nick, UnicodeString(L"Invite Sent to %hs"));
}

WindowMsgHandledType GenOnlineBuddyOverlayRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		case GWM_CREATE:
			{

				break;
			}

		case GWM_DESTROY:
			{
				// a menu closed without picking an entry still holds its data
				delete (GameSpyRCMenuData*)window->winGetUserData();
				window->winSetUserData(nullptr);
				if (rcMenu == window)
					rcMenu = nullptr;
				break;
			}

		case GGM_CLOSE:
			{
				closeRightClickMenu(window);
				break;
			}


		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				GameSpyRCMenuData *rcData = (GameSpyRCMenuData*)window->winGetUserData();
				if(!rcData)
					break;
				DEBUG_ASSERTCRASH(rcData, ("WOLBuddyOverlayRCMenuSystem GBM_SELECTED:: we're attempting to read the GameSpyRCMenuData from the window, but the data's not there"));
				GPProfile profileID = rcData->m_id;
				AsciiString nick = rcData->m_nick;

				Bool isBuddy = false, isRequest = false;
				Bool isGameSpyUser = profileID > 0;
				if (rcData->m_itemType == ITEM_BUDDY)
					isBuddy = TRUE;
				else if (rcData->m_itemType == ITEM_REQUEST)
					isRequest = TRUE;

				delete rcData;
				rcData = nullptr;

				window->winSetUserData(nullptr);
				//DEBUG_ASSERTCRASH(profileID > 0, ("Bad profile ID in user data!"));

				if( controlID == buttonAddID )
				{
					if(!isGameSpyUser)
						break;
					DEBUG_LOG(("ButtonAdd was pushed"));
					if (isRequest)
					{
						GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
						if (pSocialInterface != nullptr)
						{
							pSocialInterface->AcceptPendingRequest(profileID);
						}
					}
					else if (!isBuddy)
					{
						GenOnlineRequestBuddyAdd(profileID, nick);
					}
				}
				else if( controlID == buttonDeleteID )
				{
					if(!isGameSpyUser)
						break;
					if (isBuddy)
					{
						// delete the buddy

						GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
						if (pSocialInterface != nullptr)
						{
							pSocialInterface->RemoveFriend(profileID);
						}
					}
					else
					{
						GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
						if (pSocialInterface != nullptr)
						{
							pSocialInterface->RejectPendingRequest(profileID);
						}
					}

					// TODO_SOCIAL
				}
				else if( controlID == buttonPlayID )
				{
					DEBUG_LOG(("buttonPlayID was pushed"));
				}
				else if( controlID == buttonIgnoreID )
				{
					GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pSocialInterface != nullptr)
					{
						if (pSocialInterface->IsUserIgnored(profileID))
						{
							pSocialInterface->UnignoreUser(profileID);
						}
						else
						{
							pSocialInterface->IgnoreUser(profileID);
						}
					}

					// TODO_SOCIAL: do this in a callback above
					GenOnlineUpdateBuddyInfo();
					GenOnlineRefreshIgnoreList();
					// repopulate our player listboxes now
					GenOnlinePopulateLobbyPlayerListbox();
				}
				else if( controlID == buttonStatsID )
				{
					DEBUG_LOG(("buttonStatsID was pushed"));

					GenOnlineSetLookAtPlayer(profileID, UnicodeString(from_utf8(nick.str()).c_str()));
					GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
				}
				closeRightClickMenu(window);
				break;
			}
		default:
			return MSG_IGNORED;

	}
	return MSG_HANDLED;
}


void GenOnlineSetUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id)
{
	AsciiString controlName;
	controlName.format("%s:ButtonIgnore",rcWindowFile(layout).str());
	NameKeyType ID =  TheNameKeyGenerator->nameToKey( controlName );
	GameWindow *win = TheWindowManager->winGetWindowFromId(layout->getFirstWindow(), ID);
	if(win)
	{
		GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
		if (pSocialInterface == nullptr)
		{
			return;
		}

		GadgetButtonSetText(win, UnicodeString(pSocialInterface->IsUserIgnored(id) ? L"Unblock" : L"Block"));
	}
}

void GenOnlineRefreshIgnoreList()
{
	// Get friends
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr)
	{
		return;
	}

	// The realtime hub refreshes this list after a block changed, also while the communicator is closed.
	if (listboxIgnore == nullptr)
	{
		return;
	}

	GadgetListBoxReset(listboxIgnore);
	GadgetListBoxAddEntryText(listboxIgnore, UnicodeString(L"Loading..."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1);

	pSocialInterface->GetBlockList([](BlockedResult blockResult)
		{
			GadgetListBoxReset(listboxIgnore);

			// TODO_SOCIAL: De-register callback when buddy list exits, otherwise half of these UI elements probably wont exist anymore

			for (FriendsEntry& blockedEntry : blockResult.vecBlocked)
			{
				UnicodeString name(from_utf8(blockedEntry.display_name).c_str());
				Int index = GadgetListBoxAddEntryText(listboxIgnore, name, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 100, 100, 255)), -1);
				GadgetListBoxSetItemData(listboxIgnore, (void*)(blockedEntry.user_id), index, 0);
			}
		});

//
//	GPProfile profileID = 0;
//	PlayerInfoMap::iterator it = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
//	if (it != TheGameSpyInfo->getPlayerInfoMap()->end())
//		profileID = it->second.m_profileID;

}
