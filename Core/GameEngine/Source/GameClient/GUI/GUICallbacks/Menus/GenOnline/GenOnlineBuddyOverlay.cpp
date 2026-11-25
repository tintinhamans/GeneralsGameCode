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
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineDockChat.h"
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
#include "GameClient/GUI/ChatCompletionSets.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Image.h"
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
#include "GameClient/DisplayString.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GenOnline/GenOnlineRoomsInterface.h"
#include <algorithm>
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

static NameKeyType textEntryFindID = NAMEKEY_INVALID;
static GameWindow *textEntryFind = nullptr;

static void RunPlayerFind()
{
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr || textEntryFind == nullptr)
	{
		return;
	}
	pSocialInterface->FindPlayers(to_utf8(GadgetTextEntryGetText(textEntryFind).str()));
}

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

static ChatTabCycler s_buddyChatCycler;
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

static AsciiString rcWindowFile( WindowLayout *layout );
void GenOnlineRequestBuddyAdd(Int profileID, AsciiString nick);

// The communicator: the Friends, Requests and Blocked tabs, and the conversation with one friend beside the friends list.
static NameKeyType radioButtonRequestsID = NAMEKEY_INVALID;
static NameKeyType parentRequestsID = NAMEKEY_INVALID;
static NameKeyType listboxRequestsID = NAMEKEY_INVALID;
static NameKeyType buttonInviteID = NAMEKEY_INVALID;
static NameKeyType buttonProfileID = NAMEKEY_INVALID;
static NameKeyType buttonMoreID = NAMEKEY_INVALID;
static GameWindow *radioButtonRequests = nullptr;
static GameWindow *parentRequests = nullptr;
static GameWindow *listboxRequests = nullptr;
static GameWindow *staticTextPeerName = nullptr;
static GameWindow *staticTextPeerStatus = nullptr;
static GameWindow *windowPeerDot = nullptr;
static GameWindow *buttonInvite = nullptr;
static GameWindow *buttonProfile = nullptr;
static GameWindow *buttonMore = nullptr;

// The user of the open conversation, -1 while none is open.
static int64_t s_peerID = -1;
static std::string s_peerDot = "offline";
// The tab and conversation the next opening of the communicator shows.
static Int s_openTab = GENONLINE_COMMUNICATOR_FRIENDS;
static int64_t s_openPeer = -1;
// The conversation a click on the notification box opens, -1 for none.
static int64_t s_noticePeer = -1;
// The presence dot of each friends list row, empty for a row without one.
static std::vector<std::string> s_friendRowDots;

enum RequestRowKind { REQUEST_ROW_INCOMING = 1, REQUEST_ROW_SENT = 2 };
enum { FRIEND_COLUMN_DOT, FRIEND_COLUMN_NAME, FRIEND_COLUMN_INFO, FRIEND_COLUMN_UNREAD };
enum { CHAT_COLUMN_TEXT, CHAT_COLUMN_JOIN };
enum { REQUEST_COLUMN_NAME, REQUEST_COLUMN_FIRST, REQUEST_COLUMN_SECOND };
enum { BLOCKED_COLUMN_NAME, BLOCKED_COLUMN_UNBLOCK };

static Color CommunicatorMuted()
{
	return GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
}

static Color CommunicatorGold()
{
	return GameMakeColor(255, 194, 15, 255);
}

static Color CommunicatorWhite()
{
	return GameMakeColor(255, 255, 255, 255);
}

static std::wstring LowerText(std::wstring text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
	return text;
}

// The dot of a friend: offline, or the presence status of an online friend.
static std::string FriendDot(const FriendsEntry& entry)
{
	return entry.online ? (entry.status.empty() ? std::string("online") : entry.status) : std::string("offline");
}

static std::wstring FriendStatusText(const FriendsEntry& entry)
{
	if (!entry.online)
	{
		return L"Offline";
	}
	if (entry.status == "idle")
	{
		return L"Idle";
	}
	if (entry.status == "do_not_disturb")
	{
		return entry.presence.empty() ? std::wstring(L"Do not disturb") : L"Do not disturb · " + from_utf8(entry.presence);
	}
	return entry.presence.empty() ? std::wstring(L"Online") : from_utf8(entry.presence);
}

// The name of any user the communicator knows: friends, requests, conversations, search results and recent players.
static std::wstring CommunicatorPeerName(GenOnlineSocialInterface* pSocial, int64_t userID)
{
	for (const auto& list : { pSocial->GetCachedFriendsList(), pSocial->GetCachedRequestsList(), pSocial->GetCachedSentRequestsList(), pSocial->GetCachedBlockedList() })
	{
		auto it = list.find(userID);
		if (it != list.end())
		{
			return from_utf8(it->second.display_name);
		}
	}
	for (const auto& conversation : pSocial->GetNonFriendConversations())
	{
		if (conversation.first == userID)
		{
			return from_utf8(conversation.second);
		}
	}
	for (const GenOnlineRealtime::Member& member : pSocial->GetFoundPlayers())
	{
		if (member.id == userID)
		{
			return from_utf8(member.name);
		}
	}
	const std::unordered_map<int64_t, FriendsEntry>& recent = pSocial->GetRecentlyPlayedWithList();
	auto it = recent.find(userID);
	return it != recent.end() ? from_utf8(it->second.display_name) : std::wstring();
}

// The name, presence dot and status line the communicator shows for the other player of a conversation, also used by the dock tabs.
void GenOnlineGetConversationPeer(int64_t userID, UnicodeString& name, std::string& dot, UnicodeString& status)
{
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	name.clear();
	status.clear();
	dot = "offline";
	if (pSocial == nullptr)
	{
		return;
	}
	name = UnicodeString(CommunicatorPeerName(pSocial, userID).c_str());
	const std::unordered_map<int64_t, FriendsEntry> friends = pSocial->GetCachedFriendsList();
	auto it = friends.find(userID);
	if (it != friends.end())
	{
		dot = FriendDot(it->second);
		status = UnicodeString(FriendStatusText(it->second).c_str());
	}
	else
	{
		status = UnicodeString(pSocial->IsUserPendingRequest(userID) ? L"Wants to be your friend" : L"Not a friend");
	}
}

// Sends one line to the other player of a conversation, shared by the communicator and the dock. FALSE keeps the line in the entry, the failure text is set when there is a reason to show.
Bool GenOnlineSendConversationLine(int64_t peerID, UnicodeString text, UnicodeString* failure)
{
	// Block chatting with a buddy who is currently in the same game as us
	if (TheGenOnlineGame && TheGenOnlineGame->isGameInProgress())
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(i);
			if (slot && slot->isHuman() && slot->m_userID == (int64_t)peerID)
			{
				if (failure != nullptr)
				{
					*failure = UnicodeString(L"You cannot send messages to a friend who is currently in your game.");
				}
				return FALSE;
			}
		}
	}

	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS == nullptr)
	{
		return FALSE;
	}
	text.trim();
	if (!text.isEmpty())
	{
		pWS->SendData_FriendMessage(text, peerID);
	}
	return TRUE;
}

// A local line of the open conversation that the conversation cache does not keep.
static void AddCommunicatorNote(const std::wstring& text)
{
	if (buddyControls.listboxChat == nullptr)
	{
		return;
	}
	Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(text.c_str()), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
	GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
}

// Inviting needs a custom lobby in setup and a friend who is online.
static Bool CanInvitePeer(GenOnlineSocialInterface* pSocial, int64_t userID)
{
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pSocial == nullptr || pLobby == nullptr || userID < 0 || !pLobby->IsInLobby() || TheGenOnlineGame == nullptr
		|| TheGenOnlineGame->isGameInProgress() || TheGenOnlineGame->isQMGame())
	{
		return FALSE;
	}
	const std::unordered_map<int64_t, FriendsEntry> friends = pSocial->GetCachedFriendsList();
	auto it = friends.find(userID);
	return it != friends.end() && it->second.online;
}

static void UpdatePeerHeader()
{
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	const Bool bShown = pSocial != nullptr && s_peerID >= 0;
	std::wstring name;
	std::wstring status;
	s_peerDot = "offline";
	if (bShown)
	{
		UnicodeString peerName, peerStatus;
		GenOnlineGetConversationPeer(s_peerID, peerName, s_peerDot, peerStatus);
		name = peerName.str();
		status = peerStatus.str();
	}
	else
	{
		name = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GONoConversation", L"No conversation open").str();
		status = TheGameText->FETCH_OR_SUBSTITUTE("Buddy:GOSelectFriendToChat", L"Select a friend to chat").str();
	}
	if (staticTextPeerName != nullptr)
	{
		GadgetStaticTextSetText(staticTextPeerName, UnicodeString(name.c_str()));
		staticTextPeerName->winSetEnabledTextColors(bShown ? CommunicatorGold() : CommunicatorWhite(), GameMakeColor(0, 0, 0, 255));
	}
	if (staticTextPeerStatus != nullptr)
	{
		GadgetStaticTextSetText(staticTextPeerStatus, UnicodeString(status.c_str()));
	}
	if (windowPeerDot != nullptr)
	{
		windowPeerDot->winHide(!bShown);
	}
	if (buttonProfile != nullptr)
	{
		buttonProfile->winHide(!bShown);
	}
	if (buttonMore != nullptr)
	{
		buttonMore->winHide(!bShown);
	}
	if (buttonInvite != nullptr)
	{
		buttonInvite->winHide(!CanInvitePeer(pSocial, s_peerID));
	}
	if (buttonAddBuddy != nullptr)
	{
		const Bool bPending = bShown && pSocial->IsUserPendingRequest(s_peerID);
		buttonAddBuddy->winHide(!bShown || pSocial->IsUserFriend(s_peerID) || pSocial->IsUserIgnored(s_peerID));
		GadgetButtonSetText(buttonAddBuddy, bPending ? TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOAccept", L"Accept")
			: TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOAddFriend", L"Add friend"));
	}
}

// The menu button draws the drop-down arrow of the combo boxes, as the user section does.
static void DrawMoreButton(GameWindow *window, WinInstanceData *instData)
{
	const Bool lit = instData != nullptr && BitIsSet(instData->getState(), WIN_STATE_HILITED);
	const Image *image = TheMappedImageCollection->findImageByName(lit ? "VSliderDownButtonHilite" : "VSliderDownButtonEnabled");
	if (image == nullptr)
	{
		return;
	}
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	TheWindowManager->winDrawImage(image, x, y, x + width, y + height);
}

struct EntryHint
{
	GameWindow *window;
	GameWinDrawFunc draw;
	DisplayString *text;
};
static EntryHint s_entryHints[2];

// An empty search or message entry shows a grey hint after the entry drew itself.
static void DrawHintEntry(GameWindow *window, WinInstanceData *instData)
{
	const EntryHint *hint = nullptr;
	for (const EntryHint& entry : s_entryHints)
	{
		if (entry.window == window)
		{
			hint = &entry;
		}
	}
	if (hint == nullptr)
	{
		return;
	}
	if (hint->draw != nullptr)
	{
		hint->draw(window, instData);
	}
	if (hint->text == nullptr || GadgetTextEntryGetText(window).getLength() > 0)
	{
		return;
	}
	if (hint->text->getFont() != window->winGetFont())
	{
		hint->text->setFont(window->winGetFont());
	}
	Int x, y, width, height, textWidth, textHeight;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	hint->text->getSize(&textWidth, &textHeight);
	const Int offset = GameWindowManager::winScaleLayoutSize(4);
	IRegion2D clip = { { x, y }, { x + width - offset, y + height } };
	hint->text->setClipRegion(&clip);
	hint->text->draw(x + offset, y + (height - textHeight) / 2, CommunicatorMuted(), GameMakeColor(0, 0, 0, 255));
}

static void HookEntryHint(Int slot, GameWindow *window, const UnicodeString& text)
{
	EntryHint& hint = s_entryHints[slot];
	if (hint.text == nullptr)
	{
		hint.text = TheDisplayStringManager->newDisplayString();
	}
	hint.text->setText(text);
	hint.window = window;
	if (window != nullptr && window->winGetDrawFunc() != DrawHintEntry)
	{
		hint.draw = window->winGetDrawFunc();
		window->winSetDrawFunc(DrawHintEntry);
	}
}

static void DrawPeerDot(GameWindow *window, WinInstanceData *)
{
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	const Int size = (std::min)(width, height);
	GenOnlineDrawPresenceDot(s_peerDot.c_str(), x + (width - size) / 2, y + (height - size) / 2, size);
}

// The friends list row of the user, -1 when it shows none.
static Int FindPeerRow(int64_t userID)
{
	if (buddyControls.listboxBuddies == nullptr || userID < 0)
	{
		return -1;
	}
	const Int numEntries = GadgetListBoxGetNumEntries(buddyControls.listboxBuddies);
	for (Int row = 0; row < numEntries; ++row)
	{
		if ((int64_t)(intptr_t)GadgetListBoxGetItemData(buddyControls.listboxBuddies, row, FRIEND_COLUMN_DOT) == userID)
		{
			return row;
		}
	}
	return -1;
}

static void ShowBuddyConversation(GenOnlineSocialInterface* pSocialInterface, int64_t profileID);

// Opens the conversation with the user, or the empty pane for -1.
static void ShowPeer(int64_t userID)
{
	s_peerID = userID;
	if (buddyControls.listboxChat != nullptr)
	{
		GadgetListBoxReset(buddyControls.listboxChat);
	}
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocial == nullptr || userID < 0)
	{
		// The conversation header asks to select a friend.
	}
	else if (!pSocial->CanChatWith(userID) && pSocial->IsUserPendingRequest(userID))
	{
		AddCommunicatorNote(L"This is a pending friend request. You cannot chat with the player until you accept it.");
	}
	else if (!pSocial->CanChatWith(userID))
	{
		AddCommunicatorNote(L"This player is not a friend yet. Use Add friend or the menu to add or block them.");
	}
	else
	{
		ShowBuddyConversation(pSocial, userID);
		pSocial->ViewConversation(userID);
		GenOnlineDockOpenConversation(userID, false);
		GenOnlineUpdateBuddyInfo(true, true);
		if (buddyControls.textEntryEdit != nullptr)
		{
			TheWindowManager->winSetFocus(buddyControls.textEntryEdit);
		}
	}
	UpdatePeerHeader();
}

static void SelectPeerRow(int64_t userID)
{
	if (buddyControls.listboxBuddies != nullptr)
	{
		GadgetListBoxSetSelected(buddyControls.listboxBuddies, FindPeerRow(userID));
	}
}

// A radio button reports its selection to the overlay before it marks itself selected, so the handler of that report
// must not select it again or the two call each other until the stack runs out.
static void ShowCommunicatorTab(Int tab, Bool selectRadio = TRUE)
{
	if (parentBuddies != nullptr)
	{
		parentBuddies->winHide(tab != GENONLINE_COMMUNICATOR_FRIENDS);
	}
	if (parentRequests != nullptr)
	{
		parentRequests->winHide(tab != GENONLINE_COMMUNICATOR_REQUESTS);
	}
	if (parentIgnore != nullptr)
	{
		parentIgnore->winHide(tab != GENONLINE_COMMUNICATOR_BLOCKED);
	}
	GameWindow *radio = tab == GENONLINE_COMMUNICATOR_REQUESTS ? radioButtonRequests : tab == GENONLINE_COMMUNICATOR_BLOCKED ? radioButtonIgnore : radioButtonBuddies;
	if (selectRadio && radio != nullptr)
	{
		GadgetRadioSetSelection(radio, FALSE);
	}
	if (tab == GENONLINE_COMMUNICATOR_BLOCKED)
	{
		GenOnlineRefreshIgnoreList();
	}
}

// The text of a conversation line, a message with the local time it was written.
static UnicodeString TimedChatLineText(const GenOnlineRealtime::ChatMessage* pMessage, const UnicodeString& text)
{
	if (pMessage == nullptr)
	{
		return text;
	}
	const std::string local = GenOnlineLocalTimeText(pMessage->created_at);
	// GenOnlineLocalTimeText gives "YYYY-MM-DD HH:MM", or the service text when it does not parse.
	if (local.size() != 16 || local == pMessage->created_at)
	{
		return text;
	}
	UnicodeString timed;
	timed.format(L"[%hs] %s", local.substr(11).c_str(), text.str());
	return timed;
}

static void AddInviteChatLine(const std::wstring& text, int64_t lobbyID)
{
	if (buddyControls.listboxChat == nullptr)
	{
		return;
	}
	const Color gold = CommunicatorGold();
	Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(text.c_str()), gold, -1, CHAT_COLUMN_TEXT);
	GadgetListBoxAddEntryText(buddyControls.listboxChat, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOJoin", L"Join"), gold, index, CHAT_COLUMN_JOIN);
	// shortcut: the lobby id rides in a pointer, which is 32 bits wide, so an id past 2^31 cannot join from the line. Widen it once ids grow that large.
	GadgetListBoxSetItemData(buddyControls.listboxChat, reinterpret_cast<void*>(static_cast<intptr_t>(lobbyID)), index, CHAT_COLUMN_JOIN);
}

static void JoinInvite(int64_t lobbyID)
{
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobby == nullptr || lobbyID <= 0)
	{
		return;
	}
	if (pLobby->IsInLobby() || (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress()))
	{
		AddCommunicatorNote(L"Leave your current game to join this one.");
		return;
	}
	if (GenOnlineLobbyMenuIsActive() || GenOnlineWelcomeMenuOpenLobby())
	{
		GenOnlineLobbyMenuJoinInvite(lobbyID);
		GameSpyCloseOverlay(GSOVERLAY_BUDDY);
		return;
	}
	AddCommunicatorNote(L"Open the game list to join this game.");
}

static void InvitePeer()
{
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocial == nullptr || !CanInvitePeer(pSocial, s_peerID))
	{
		return;
	}
	const int64_t peerID = s_peerID;
	const std::wstring name = CommunicatorPeerName(pSocial, peerID);
	pSocial->InviteToLobby(peerID, [peerID, name](std::wstring failure)
		{
			const std::wstring text = failure.empty() ? L"Invite sent to " + name + L"." : failure;
			if (isOverlayActive && s_peerID == peerID)
			{
				AddCommunicatorNote(text);
			}
			else
			{
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(text.c_str()), false);
			}
		});
}

static void OpenPeerMenu()
{
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocial == nullptr || s_peerID < 0 || buttonMore == nullptr)
	{
		return;
	}
	GenOnlineCloseRightClickMenus();
	const Bool bFriend = pSocial->IsUserFriend(s_peerID);
	WindowLayout *rcLayout = TheWindowManager->winCreateLayout(bFriend ? "Menus/GenOnlineRCBuddiesMenu.wnd" : "Menus/GenOnlineRCNonBuddiesMenu.wnd");
	if (rcLayout == nullptr)
	{
		return;
	}
	rcMenu = rcLayout->getFirstWindow();
	rcLayout->runInit();

	// The menu of a friend holds Remove and Block, the profile has its own button.
	AsciiString controlName;
	controlName.format("%s:ButtonStats", rcWindowFile(rcLayout).str());
	GameWindow *buttonStats = TheWindowManager->winGetWindowFromId(rcMenu, TheNameKeyGenerator->nameToKey(controlName));
	controlName.format("%s:ButtonDelete", rcWindowFile(rcLayout).str());
	GameWindow *buttonDelete = TheWindowManager->winGetWindowFromId(rcMenu, TheNameKeyGenerator->nameToKey(controlName));
	if (bFriend && buttonStats != nullptr && buttonDelete != nullptr)
	{
		Int statsX, statsY, deleteX, deleteY;
		buttonStats->winGetPosition(&statsX, &statsY);
		buttonDelete->winGetPosition(&deleteX, &deleteY);
		const Int step = deleteY - statsY;
		buttonStats->winHide(TRUE);
		for (GameWindow *child = rcMenu->winGetChild(); child != nullptr; child = child->winGetNext())
		{
			Int childX, childY;
			child->winGetPosition(&childX, &childY);
			if (child != buttonStats && childY > statsY)
			{
				child->winSetPosition(childX, childY - step);
			}
		}
		Int width, height;
		rcMenu->winGetSize(&width, &height);
		rcMenu->winSetSize(width, height - step);
		GadgetButtonSetText(buttonDelete, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GORemoveFriend", L"Remove"));
	}

	Int buttonX, buttonY, buttonWidth, buttonHeight, menuWidth, menuHeight;
	buttonMore->winGetScreenPosition(&buttonX, &buttonY);
	buttonMore->winGetSize(&buttonWidth, &buttonHeight);
	rcMenu->winGetSize(&menuWidth, &menuHeight);
	rcMenu->winSetPosition((std::max)(0, buttonX + buttonWidth - menuWidth), buttonY + buttonHeight);
	rcMenu->winBringToTop();
	rcMenu->winHide(FALSE);

	GameSpyRCMenuData *rcData = NEW GameSpyRCMenuData;
	rcData->m_id = (GPProfile)s_peerID;
	rcData->m_nick = AsciiString(to_utf8(CommunicatorPeerName(pSocial, s_peerID)).c_str());
	rcData->m_itemType = bFriend ? ITEM_BUDDY : ITEM_NONBUDDY;
	GenOnlineSetUnignoreText(rcLayout, rcData->m_nick, rcData->m_id);
	rcMenu->winSetUserData((void *)rcData);
	TheWindowManager->winSetLoneWindow(rcMenu);
}

static void AddFriendRow(GameWindow *listbox, const std::string& dot, const std::wstring& name, Color nameColor, const std::wstring& info, Int unread, int64_t userID, RCItemType itemType)
{
	const Int row = GadgetListBoxAddEntryText(listbox, UnicodeString::TheEmptyString, nameColor, -1, FRIEND_COLUMN_DOT);
	if (row < 0)
	{
		return;
	}
	GadgetListBoxAddEntryText(listbox, UnicodeString(name.c_str()), nameColor, row, FRIEND_COLUMN_NAME);
	GadgetListBoxAddEntryText(listbox, UnicodeString(info.c_str()), CommunicatorMuted(), row, FRIEND_COLUMN_INFO);
	UnicodeString count;
	if (unread > 0)
	{
		count.format(L"%d", unread);
	}
	GadgetListBoxAddEntryText(listbox, count, CommunicatorGold(), row, FRIEND_COLUMN_UNREAD);
	GadgetListBoxSetItemData(listbox, reinterpret_cast<void*>(static_cast<intptr_t>(userID)), row, FRIEND_COLUMN_DOT);
	GadgetListBoxSetItemData(listbox, reinterpret_cast<void*>(static_cast<intptr_t>(itemType)), row, FRIEND_COLUMN_NAME);
	if ((Int)s_friendRowDots.size() <= row)
	{
		s_friendRowDots.resize(row + 1);
	}
	s_friendRowDots[row] = dot;
}

static void AddFriendHeaderRow(GameWindow *listbox, const WideChar *label, Int count)
{
	const Int row = GadgetListBoxAddEntryText(listbox, UnicodeString::TheEmptyString, CommunicatorMuted(), -1, FRIEND_COLUMN_DOT);
	if (row < 0)
	{
		return;
	}
	GadgetListBoxAddEntryText(listbox, UnicodeString(label), CommunicatorMuted(), row, FRIEND_COLUMN_NAME);
	UnicodeString text;
	text.format(L"%d", count);
	GadgetListBoxAddEntryText(listbox, text, CommunicatorMuted(), row, FRIEND_COLUMN_UNREAD);
	GadgetListBoxSetItemData(listbox, reinterpret_cast<void*>(static_cast<intptr_t>(-1)), row, FRIEND_COLUMN_DOT);
	if ((Int)s_friendRowDots.size() <= row)
	{
		s_friendRowDots.resize(row + 1);
	}
	s_friendRowDots[row].clear();
}

static void AddRequestHeaderRow(const WideChar *label, Int count)
{
	UnicodeString text;
	text.format(L"%s  %d", label, count);
	const Int row = GadgetListBoxAddEntryText(listboxRequests, text, CommunicatorMuted(), -1, REQUEST_COLUMN_NAME);
	GadgetListBoxSetItemData(listboxRequests, reinterpret_cast<void*>(static_cast<intptr_t>(-1)), row, REQUEST_COLUMN_NAME);
}

static void AddRequestRow(const FriendsEntry& entry, RequestRowKind kind)
{
	const Int row = GadgetListBoxAddEntryText(listboxRequests, UnicodeString(from_utf8(entry.display_name).c_str()), CommunicatorWhite(), -1, REQUEST_COLUMN_NAME);
	if (kind == REQUEST_ROW_INCOMING)
	{
		GadgetListBoxAddEntryText(listboxRequests, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserAccept", L"Accept"), CommunicatorGold(), row, REQUEST_COLUMN_FIRST);
		GadgetListBoxAddEntryText(listboxRequests, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserIgnore", L"Ignore"), CommunicatorWhite(), row, REQUEST_COLUMN_SECOND);
	}
	else
	{
		GadgetListBoxAddEntryText(listboxRequests, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GORequestWaiting", L"Waiting"), CommunicatorMuted(), row, REQUEST_COLUMN_FIRST);
		GadgetListBoxAddEntryText(listboxRequests, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOCancelRequest", L"Cancel"), CommunicatorWhite(), row, REQUEST_COLUMN_SECOND);
	}
	GadgetListBoxSetItemData(listboxRequests, reinterpret_cast<void*>(static_cast<intptr_t>(entry.user_id)), row, REQUEST_COLUMN_NAME);
	GadgetListBoxSetItemData(listboxRequests, reinterpret_cast<void*>(static_cast<intptr_t>(kind)), row, REQUEST_COLUMN_FIRST);
}

static void RefreshRequestsList(GenOnlineSocialInterface* pSocial)
{
	const std::unordered_map<int64_t, FriendsEntry> incoming = pSocial->GetCachedRequestsList();
	if (radioButtonRequests != nullptr)
	{
		UnicodeString label = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GORequestsTab", L"Requests");
		if (!incoming.empty())
		{
			UnicodeString counted;
			counted.format(L"%s (%d)", label.str(), (Int)incoming.size());
			label = counted;
		}
		GadgetRadioSetText(radioButtonRequests, label);
	}
	if (listboxRequests == nullptr)
	{
		return;
	}
	const std::unordered_map<int64_t, FriendsEntry> sent = pSocial->GetCachedSentRequestsList();
	GadgetListBoxReset(listboxRequests);
	if (incoming.empty() && sent.empty())
	{
		AddRequestHeaderRow(L"NO FRIEND REQUESTS", 0);
		return;
	}
	AddRequestHeaderRow(L"INCOMING", (Int)incoming.size());
	for (const auto& entry : incoming)
	{
		AddRequestRow(entry.second, REQUEST_ROW_INCOMING);
	}
	AddRequestHeaderRow(L"SENT", (Int)sent.size());
	for (const auto& entry : sent)
	{
		AddRequestRow(entry.second, REQUEST_ROW_SENT);
	}
}

// A right click selects its row too, which must not press the button cell under the mouse.
static Bool s_menuSelecting = FALSE;

// The column of the cell under the mouse in the list, -1 when the mouse is over no cell.
static Int ClickedColumn(GameWindow *listbox)
{
	if (s_menuSelecting)
	{
		return -1;
	}
	const MouseIO *mouse = TheMouse->getMouseStatus();
	Int row = -1, column = -1;
	GadgetListBoxGetEntryBasedOnXY(listbox, mouse->pos.x, mouse->pos.y, row, column);
	return column;
}

// Each list draws itself first, then the friends list adds its presence dots, the button cells a frame and an invite line a gold band.
struct CommunicatorListDraw
{
	GameWindow *window;
	GameWinDrawFunc draw;
	UnsignedInt buttonColumns;
	Bool dots;
	Bool invites;
};
static CommunicatorListDraw s_listDraws[4];

static void DrawCommunicatorList(GameWindow *window, WinInstanceData *instData)
{
	const CommunicatorListDraw *spec = nullptr;
	for (const CommunicatorListDraw& entry : s_listDraws)
	{
		if (entry.window == window)
		{
			spec = &entry;
		}
	}
	if (spec == nullptr)
	{
		return;
	}
	if (spec->draw != nullptr)
	{
		spec->draw(window, instData);
	}
	ListboxData *list = (ListboxData *)window->winGetUserData();
	if (list == nullptr || list->listData == nullptr)
	{
		return;
	}

	// The rows follow the W3DListBox geometry, as the ping bars of the game list do.
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	const Int top = y + 4;
	const Int bottom = y + height - 1;
	const Int textOffset = GameWindowManager::winScaleLayoutSize(TEXT_X_OFFSET);
	const Int pad = GameWindowManager::winScaleLayoutSize(3);
	const Color muted = CommunicatorMuted();
	Int rowY = top - list->displayPos;
	for (Int i = 0; i < list->endPos; ++i)
	{
		const Int rowHeight = list->listData[i].height + 1;
		const Int rowTop = rowY;
		rowY += rowHeight;
		if (rowTop < top || rowTop + rowHeight > bottom || list->listData[i].cell == nullptr)
		{
			continue;
		}
		if (spec->dots && i < (Int)s_friendRowDots.size() && !s_friendRowDots[i].empty() && list->columns > FRIEND_COLUMN_DOT)
		{
			const Int size = GameWindowManager::winScaleLayoutSize(7);
			GenOnlineDrawPresenceDot(s_friendRowDots[i].c_str(), x + (list->columnWidth[FRIEND_COLUMN_DOT] - size) / 2 + pad, rowTop + (rowHeight - size) / 2, size);
		}
		if (spec->invites && list->columns > CHAT_COLUMN_JOIN && list->listData[i].cell[CHAT_COLUMN_JOIN].userData != nullptr)
		{
			TheWindowManager->winFillRect(GameMakeColor(255, 194, 15, 20), 1.0f, x + 1, rowTop, x + width - 1, rowTop + rowHeight);
			GadgetListBoxDrawKitFrame(GameMakeColor(107, 90, 26, 255), x + 1, rowTop, width - 2, rowHeight);
		}
		Int columnX = x;
		for (Int column = 0; column < list->columns; ++column)
		{
			const ListEntryCell& cell = list->listData[i].cell[column];
			if ((spec->buttonColumns & (1u << column)) != 0 && cell.cellType == LISTBOX_TEXT && cell.data != nullptr && cell.color != muted)
			{
				// The list stores an empty cell as a space, which must not get a button frame.
				DisplayString *cellText = (DisplayString *)cell.data;
				UnicodeString label = cellText->getText();
				label.trim();
				Int textWidth = 0, textHeight = 0;
				cellText->getSize(&textWidth, &textHeight);
				if (textWidth > 0 && !label.isEmpty())
				{
					const Int frameWidth = (std::min)(textWidth + 2 * pad, list->columnWidth[column] - textOffset + pad);
					GadgetListBoxDrawKitFrame(cell.color, columnX + textOffset - pad, rowTop + 1, frameWidth, rowHeight - 2);
				}
			}
			columnX += list->columnWidth[column];
		}
	}
}

static void HookCommunicatorList(Int slot, GameWindow *window, UnsignedInt buttonColumns, Bool dots, Bool invites)
{
	s_listDraws[slot].window = window;
	s_listDraws[slot].draw = nullptr;
	s_listDraws[slot].buttonColumns = buttonColumns;
	s_listDraws[slot].dots = dots;
	s_listDraws[slot].invites = invites;
	if (window != nullptr && window->winGetDrawFunc() != DrawCommunicatorList)
	{
		s_listDraws[slot].draw = window->winGetDrawFunc();
		window->winSetDrawFunc(DrawCommunicatorList);
	}
}

void GenOnlineOpenCommunicator(Int tab, int64_t peerUserID)
{
	if (isOverlayActive)
	{
		ShowCommunicatorTab(tab);
		if (tab == GENONLINE_COMMUNICATOR_FRIENDS)
		{
			ShowPeer(peerUserID);
			SelectPeerRow(peerUserID);
		}
		return;
	}
	s_openTab = tab;
	s_openPeer = peerUserID;
	GameSpyOpenOverlay(GSOVERLAY_BUDDY);
}

void GenOnlineShowCommunicatorNotice(long long peerUserID, UnicodeString message)
{
	GenOnlineShowNotificationBox(AsciiString::TheEmptyString, message, true);
	s_noticePeer = peerUserID;
}

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
		GadgetTextEntrySetCompletionEnabled(buddyControls.textEntryEdit, TRUE);
		s_buddyChatCycler.Reset();
		buddyControls.isInit = TRUE;

		// The conversation header asks to select a friend, so the empty chat list stays blank.
		GadgetListBoxReset(buddyControls.listboxChat);
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

// The direct message box completes friends and the other conversation partners. It has no slash commands.
static void BuildBuddyChatCompletionContext(ChatCompletionContext& context)
{
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr)
		return;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const std::wstring self = (pAuthInterface != nullptr) ? pAuthInterface->GetDisplayNameW() : std::wstring();

	// The cached friends list takes the friends map mutex itself.
	const std::unordered_map<int64_t, FriendsEntry> friends = pSocialInterface->GetCachedFriendsList();
	for (const auto& kvPair : friends)
	{
		AppendChatName(context.names, from_utf8(kvPair.second.display_name), self);
	}

	// A conversation partner without a known name shows as Player and the user id, which is not a name to complete.
	const std::vector<std::pair<int64_t, std::string>> conversations = pSocialInterface->GetNonFriendConversations();
	for (const auto& conversation : conversations)
	{
		if (conversation.second != "Player " + std::to_string(conversation.first))
			AppendChatName(context.names, from_utf8(conversation.second), self);
	}
}

static void UpdateBuddyChatCompletionHint(GameWindow *textEntry)
{
	s_buddyChatCycler.Reset();
	ChatCompletionContext context;
	BuildBuddyChatCompletionContext(context);
	const std::wstring text = GadgetTextEntryGetText(textEntry).str();
	GadgetTextEntrySetCompletionHint(textEntry, UnicodeString(ChatCompletionHint(text, context).c_str()));
}

static void CompleteBuddyChat(GameWindow *textEntry, Bool reverse)
{
	ChatCompletionContext context;
	BuildBuddyChatCompletionContext(context);
	std::wstring text = GadgetTextEntryGetText(textEntry).str();
	if (!s_buddyChatCycler.Complete(text, context, reverse != FALSE))
		return;

	GadgetTextEntrySetText(textEntry, UnicodeString(text.c_str()));
	GadgetTextEntrySetCompletionHint(textEntry, UnicodeString(ChatCompletionHint(text, context).c_str()));
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

					GPProfile profileID = (GPProfile)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, FRIEND_COLUMN_DOT);
					// a group heading has no user
					if (profileID < 0)
						break;
					RCItemType itemType = (RCItemType)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, FRIEND_COLUMN_NAME);
					UnicodeString nick = GadgetListBoxGetText(control, rc->pos, FRIEND_COLUMN_NAME);

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
					RightClickStruct *rc = (RightClickStruct *)mData2;
					GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (rc->pos < 0 || pSocialInterface == nullptr)
						break;
					const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(control, rc->pos, 0)));
					const GenOnlineRealtime::ChatMessage* message = pSocialInterface->FindConversationMessage(GetSelectedBuddyID(), key);
					s_menuSelecting = TRUE;
					GenOnlineOpenMessageMenu(control, rc->pos, rc->mouseX, rc->mouseY, message != nullptr ? message->id.c_str() : "", message != nullptr ? message->author.id : 0, message != nullptr ? message->author.name.c_str() : "");
					s_menuSelecting = FALSE;
				}
				else
					return MSG_IGNORED;
				break;
			}
			case GEM_UPDATE_TEXT:
			{
				GameWindow *control = (GameWindow *)mData1;
				if (control == nullptr || control != buddyControls.textEntryEdit)
					return MSG_IGNORED;

				UpdateBuddyChatCompletionHint(control);
				break;
			}
			case GEM_TAB_COMPLETE:
			{
				GameWindow *control = (GameWindow *)mData1;
				if (control == nullptr || control != buddyControls.textEntryEdit)
					return MSG_IGNORED;

				CompleteBuddyChat(control, mData2 != 0);
				break;
			}
			case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if(controlID != buddyControls.textEntryEditID)
					return MSG_IGNORED;

				s_buddyChatCycler.Reset();

				if (s_peerID >= 0)
				{
					const int64_t profileID = s_peerID;

					UnicodeString txtInput;
					txtInput.set(GadgetTextEntryGetText(buddyControls.textEntryEdit));
					UnicodeString failure;
					if (GenOnlineSendConversationLine(profileID, txtInput, &failure))
					{
						GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
					}
					else if (!failure.isEmpty() && buddyControls.listboxChat)
					{
						GadgetListBoxAddEntryText( buddyControls.listboxChat, failure, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
						return MSG_HANDLED;
					}
						
				}
				else
				{
					// nobody selected.  Prompt the user.
					if (buddyControls.listboxChat)
					{
						GadgetListBoxAddEntryText( buddyControls.listboxChat, TheGameText->FETCH_OR_SUBSTITUTE("Buddy:GOSelectFriendToChat", L"Select a friend to chat"),
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

// The friends list: search results, the friends grouped online, idle and offline, then the other players the
// local player met or talks to. The service keeps the cache current, so the list is drawn from it at once.
void GenOnlineUpdateBuddyInfo( bool bIsAutoRefresh, bool bUseCache)
{
	if (g_genOnlineIsProcessingRefresh)
	{
		return;
	}

	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr || !buddyControls.isInit || buddyControls.listboxBuddies == nullptr)
	{
		return;
	}

	g_genOnlineIsProcessingRefresh = true;
	GameWindow *listbox = buddyControls.listboxBuddies;
	const Int visiblePos = GadgetListBoxGetTopVisibleEntry(listbox);
	GadgetListBoxReset(listbox);
	s_friendRowDots.clear();

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const int64_t localUserID = pAuthInterface != nullptr ? pAuthInterface->GetUserID() : -1;
	const std::wstring filter = textEntryFind != nullptr ? LowerText(GadgetTextEntryGetText(textEntryFind).str()) : std::wstring();
	auto matches = [&filter](const std::wstring& name)
		{
			return filter.empty() || LowerText(name).find(filter) != std::wstring::npos;
		};
	std::unordered_set<int64_t> listed;
	listed.insert(localUserID);

	struct Row
	{
		int64_t id;
		std::wstring name;
		std::wstring info;
		std::string dot;
		Int unread;
	};
	auto sortRows = [](std::vector<Row>& rows)
		{
			std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
				{
					if (a.unread != b.unread)
						return a.unread > b.unread;
					return LowerText(a.name) < LowerText(b.name);
				});
		};

	std::vector<Row> found;
	for (const GenOnlineRealtime::Member& member : pSocialInterface->GetFoundPlayers())
	{
		if (listed.contains(member.id) || pSocialInterface->IsUserFriend(member.id))
			continue;
		listed.insert(member.id);
		found.push_back({ member.id, from_utf8(member.name), L"Player", std::string(), 0 });
	}
	if (!found.empty())
	{
		AddFriendHeaderRow(listbox, L"SEARCH RESULTS", (Int)found.size());
		for (const Row& row : found)
			AddFriendRow(listbox, row.dot, row.name, CommunicatorWhite(), row.info, row.unread, row.id, ITEM_NONBUDDY);
	}

	std::vector<Row> online, idle, offline;
	for (const auto& kvPair : pSocialInterface->GetCachedFriendsList())
	{
		const FriendsEntry& entry = kvPair.second;
		const std::wstring name = from_utf8(entry.display_name);
		listed.insert(entry.user_id);
		if (!matches(name))
			continue;
		std::wstring info = L"Offline";
		if (entry.online)
		{
			info = entry.status == "idle" ? std::wstring(L"Idle") : entry.status == "do_not_disturb" ? std::wstring(L"Do not disturb")
				: entry.presence.empty() ? std::wstring(L"Online") : from_utf8(entry.presence);
		}
		Row row = { entry.user_id, name, info, FriendDot(entry), pSocialInterface->GetNumberUnreadChatMessagesForUser(entry.user_id) };
		(!entry.online ? offline : entry.status == "idle" ? idle : online).push_back(row);
	}
	const struct { const WideChar *label; std::vector<Row> *rows; } groups[] = {
		{ L"ONLINE", &online }, { L"IDLE", &idle }, { L"OFFLINE", &offline } };
	for (const auto& group : groups)
	{
		if (group.rows->empty())
			continue;
		sortRows(*group.rows);
		AddFriendHeaderRow(listbox, group.label, (Int)group.rows->size());
		for (const Row& row : *group.rows)
			AddFriendRow(listbox, row.dot, row.name, group.rows == &offline ? CommunicatorMuted() : CommunicatorWhite(), row.info, row.unread, row.id, ITEM_BUDDY);
	}

	// The other players: the current lobby outside quick match, the recent players and the conversations of staff who wrote.
	std::vector<Row> others;
	auto addOther = [&](int64_t id, const std::wstring& name, const WideChar *info)
		{
			if (listed.contains(id) || pSocialInterface->IsUserPendingRequest(id) || !matches(name))
				return;
			listed.insert(id);
			others.push_back({ id, name, info, std::string(), pSocialInterface->GetNumberUnreadChatMessagesForUser(id) });
		};
	if (TheGenOnlineGame != nullptr && !(!TheGenOnlineGame->isGameInProgress() && TheGenOnlineGame->isQMGame()))
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(i);
			if (slot != nullptr && slot->isHuman())
				addOther(slot->m_userID, slot->getName().str(), L"In your game");
		}
	}
	for (const auto& kvPair : pSocialInterface->GetRecentlyPlayedWithList())
		addOther(kvPair.second.user_id, from_utf8(kvPair.second.display_name), L"Recently played with");
	for (const std::pair<int64_t, std::string>& conversation : pSocialInterface->GetNonFriendConversations())
		addOther(conversation.first, from_utf8(conversation.second), L"Conversation");
	if (!others.empty())
	{
		AddFriendHeaderRow(listbox, L"OTHER PLAYERS", (Int)others.size());
		for (const Row& row : others)
			AddFriendRow(listbox, row.dot, row.name, CommunicatorWhite(), row.info, row.unread, row.id, ITEM_NONBUDDY);
	}

	SelectPeerRow(s_peerID);
	GadgetListBoxSetTopVisibleEntry(listbox, visiblePos);
	RefreshRequestsList(pSocialInterface);
	UpdatePeerHeader();
	g_genOnlineIsProcessingRefresh = false;
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
	s_noticePeer = -1;
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
		message = TheGameText->FETCH_OR_SUBSTITUTE("Buddy:GOMultipleOnlineNotification", L"You have friends online");
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

// GenOnline @bugfix arcticdolphin 07/11/2026 A notification was raised only when it appeared, so the next shell screen
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
static void AddBuddyChatLine(int64_t peerID, const UnicodeString& text, uint32_t key)
{
	Color color = GameSpyColor[GSCOLOR_PLAYER_BUDDY];
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	const GenOnlineRealtime::ChatMessage* pMessage = pSocialInterface != nullptr ? pSocialInterface->FindConversationMessage(peerID, key) : nullptr;
	if (pMessage != nullptr && GenOnlineMentionsLocalUser(*pMessage))
	{
		color = DetermineMentionColor();
	}
	Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, TimedChatLineText(pMessage, text), color, -1, -1);
	GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, color, index, 1);
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

static int64_t GetSelectedBuddyID()
{
	return s_peerID;
}

static void ShowBuddyConversation(GenOnlineSocialInterface* pSocialInterface, int64_t profileID)
{
	GadgetListBoxReset(buddyControls.listboxChat);
	std::vector<GenOnlineRealtime::DirectChatLine> vecMessages = pSocialInterface->GetChatMessagesForUser(profileID);

	for (const GenOnlineRealtime::DirectChatLine& line : vecMessages)
	{
		if (line.inviteLobbyID != 0)
		{
			AddInviteChatLine(line.note, line.inviteLobbyID);
			continue;
		}
		AddBuddyChatLine(profileID, GenOnlineSocialInterface::GetChatLineText(line), line.key);
	}

	if (vecMessages.empty())
	{
		Int index = GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString(L"This chat is empty. Send a message to start a conversation"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		GadgetListBoxAddEntryText(buddyControls.listboxChat, UnicodeString::TheEmptyString, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
	}
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
	buttonAcceptBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonYes" );
	buttonDenyBuddyID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonNo" );
	radioButtonBuddiesID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RadioButtonBuddies" );
	radioButtonIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RadioButtonIgnore" );
	parentBuddiesID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:BuddiesParent" );
	parentIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:IgnoreParent" );
	listboxIgnoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxIgnore" );
	radioButtonRequestsID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RadioButtonRequests" );
	parentRequestsID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:RequestsParent" );
	listboxRequestsID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ListboxRequests" );
	buttonInviteID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonInvite" );
	buttonProfileID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonProfile" );
	buttonMoreID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:ButtonMore" );
	s_peerID = -1;

	//
	// NOTE Init is only called when the UI is visible, so don't register for callbacks that you want to occur anytime
	// GO: register for callbacks
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->RegisterForRealtimeServiceUpdates();

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
						// a local line such as a lobby invite is drawn from the conversation cache
						GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
						if (key == 0 && pSocial != nullptr)
						{
							ShowBuddyConversation(pSocial, peer_user_id);
						}
						else
						{
							AddBuddyChatLine(peer_user_id, unicodeStr, key);
						}

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
	buttonAcceptBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonAcceptBuddyID);
	buttonDenyBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonDenyBuddyID);
	radioButtonBuddies = TheWindowManager->winGetWindowFromId( parent,  radioButtonBuddiesID);
	radioButtonIgnore = TheWindowManager->winGetWindowFromId( parent,  radioButtonIgnoreID);
	parentBuddies = TheWindowManager->winGetWindowFromId( parent,  parentBuddiesID);
	parentIgnore = TheWindowManager->winGetWindowFromId( parent,  parentIgnoreID);
	listboxIgnore = TheWindowManager->winGetWindowFromId( parent,  listboxIgnoreID);

	textEntryFindID = TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:TextEntryFind" );
	textEntryFind = TheWindowManager->winGetWindowFromId( parent, textEntryFindID );
	if (textEntryFind != nullptr)
	{
		GadgetTextEntrySetText(textEntryFind, UnicodeString::TheEmptyString);
	}

	if (radioButtonBuddies)
		GadgetRadioSetText(radioButtonBuddies, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOFriendsTab", L"Friends"));
	if (radioButtonIgnore)
		GadgetRadioSetText(radioButtonIgnore, UnicodeString(L"Blocked"));

	radioButtonRequests = TheWindowManager->winGetWindowFromId( parent, radioButtonRequestsID );
	parentRequests = TheWindowManager->winGetWindowFromId( parent, parentRequestsID );
	listboxRequests = TheWindowManager->winGetWindowFromId( parent, listboxRequestsID );
	buttonInvite = TheWindowManager->winGetWindowFromId( parent, buttonInviteID );
	buttonProfile = TheWindowManager->winGetWindowFromId( parent, buttonProfileID );
	buttonMore = TheWindowManager->winGetWindowFromId( parent, buttonMoreID );
	staticTextPeerName = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:StaticTextPeerName" ) );
	staticTextPeerStatus = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:StaticTextPeerStatus" ) );
	windowPeerDot = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:WindowPeerDot" ) );
	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "WOLBuddyOverlay.wnd:StaticTextTitle" ) );
	if (staticTextTitle != nullptr)
		GadgetStaticTextSetText(staticTextTitle, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOCommunicator", L"Communicator"));
	if (staticTextPeerName != nullptr)
		staticTextPeerName->winSetEnabledTextColors(CommunicatorGold(), GameMakeColor(0, 0, 0, 255));
	if (staticTextPeerStatus != nullptr)
		staticTextPeerStatus->winSetEnabledTextColors(CommunicatorMuted(), GameMakeColor(0, 0, 0, 255));
	if (windowPeerDot != nullptr)
		windowPeerDot->winSetDrawFunc(DrawPeerDot);
	if (buttonHide != nullptr)
		GadgetButtonSetText(buttonHide, UnicodeString(L"X"));
	if (buttonInvite != nullptr)
		GadgetButtonSetText(buttonInvite, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOInviteToGame", L"Invite"));
	if (buttonProfile != nullptr)
		GadgetButtonSetText(buttonProfile, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOProfile", L"Profile"));
	if (buttonMore != nullptr)
		buttonMore->winSetDrawFunc(DrawMoreButton);

	GenOnlineInitBuddyControls(BUDDY_WINDOW_BUDDIES);
	HookEntryHint(0, buddyControls.textEntryEdit, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOTypeMessage", L"Type a message..."));
	HookEntryHint(1, textEntryFind, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSearchPlayers", L"Search players..."));
	HookCommunicatorList(0, buddyControls.listboxBuddies, 0, TRUE, FALSE);
	HookCommunicatorList(1, buddyControls.listboxChat, 1u << CHAT_COLUMN_JOIN, FALSE, TRUE);
	HookCommunicatorList(2, listboxRequests, (1u << REQUEST_COLUMN_FIRST) | (1u << REQUEST_COLUMN_SECOND), FALSE, FALSE);
	HookCommunicatorList(3, listboxIgnore, 1u << BLOCKED_COLUMN_UNBLOCK, FALSE, FALSE);

	//GadgetTextEntrySetText(textEntry, UnicodeString::TheEmptyString);


	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parent );

	isOverlayActive = true;
	GenOnlineUpdateBuddyInfo();

	// Messages opens the latest conversation, All friends and the other ways in open none.
	const Int openTab = s_openTab;
	const int64_t openPeer = s_openPeer;
	s_openTab = GENONLINE_COMMUNICATOR_FRIENDS;
	s_openPeer = -1;
	ShowCommunicatorTab(openTab);
	ShowPeer(openPeer);
	SelectPeerRow(openPeer);
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
	textEntryFind = nullptr;
	radioButtonRequests = nullptr;
	parentRequests = nullptr;
	listboxRequests = nullptr;
	staticTextPeerName = nullptr;
	staticTextPeerStatus = nullptr;
	windowPeerDot = nullptr;
	buttonInvite = nullptr;
	buttonProfile = nullptr;
	buttonMore = nullptr;
	buttonAddBuddy = nullptr;
	for (EntryHint& hint : s_entryHints)
	{
		if (hint.text != nullptr)
		{
			TheDisplayStringManager->freeDisplayString(hint.text);
		}
		hint = EntryHint();
	}
	s_peerID = -1;
	s_friendRowDots.clear();
	for (CommunicatorListDraw& entry : s_listDraws)
		entry.window = nullptr;

	GenOnlineCloseRightClickMenus();

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
		return;
	}

	// Joining or leaving a lobby shows or hides the invite button while the communicator is open.
	static UnsignedInt nextInviteCheck = 0;
	if (buttonInvite != nullptr && timeGetTime() >= nextInviteCheck)
	{
		nextInviteCheck = timeGetTime() + 500;
		buttonInvite->winHide(!CanInvitePeer(GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>(), s_peerID));
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
			GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			Int selected = -1;
			GadgetListBoxGetSelected(control, &selected);

			if (controlID == buddyControls.listboxBuddiesID)
			{
				const int64_t profileID = selected >= 0 ? (int64_t)(intptr_t)GadgetListBoxGetItemData(control, selected, FRIEND_COLUMN_DOT) : -1;
				// a group heading keeps the open conversation selected
				if (profileID < 0)
				{
					if (selected >= 0)
					{
						SelectPeerRow(s_peerID);
					}
				}
				else if (profileID != s_peerID)
				{
					ShowPeer(profileID);
				}
			}
			// The Accept, Ignore, Cancel, Unblock and Join cells act on a click and leave nothing selected.
			else if (controlID == listboxRequestsID && selected >= 0 && pSocialInterface != nullptr)
			{
				const int64_t userID = (int64_t)(intptr_t)GadgetListBoxGetItemData(control, selected, REQUEST_COLUMN_NAME);
				const Int kind = (Int)(intptr_t)GadgetListBoxGetItemData(control, selected, REQUEST_COLUMN_FIRST);
				const Int column = ClickedColumn(control);
				GadgetListBoxSetSelected(control, -1);
				if (userID < 0)
				{
					break;
				}
				if (kind == REQUEST_ROW_INCOMING && column == REQUEST_COLUMN_FIRST)
				{
					pSocialInterface->AcceptPendingRequest(userID);
				}
				else if (kind == REQUEST_ROW_INCOMING && column == REQUEST_COLUMN_SECOND)
				{
					pSocialInterface->RejectPendingRequest(userID);
				}
				else if (kind == REQUEST_ROW_SENT && column == REQUEST_COLUMN_SECOND)
				{
					pSocialInterface->CancelSentRequest(userID);
				}
			}
			else if (controlID == listboxIgnoreID && selected >= 0 && pSocialInterface != nullptr)
			{
				const int64_t userID = (int64_t)(intptr_t)GadgetListBoxGetItemData(control, selected, BLOCKED_COLUMN_NAME);
				const Int column = ClickedColumn(control);
				GadgetListBoxSetSelected(control, -1);
				if (userID > 0 && column == BLOCKED_COLUMN_UNBLOCK)
				{
					pSocialInterface->UnignoreUser(userID);
				}
			}
			else if (controlID == buddyControls.listboxChatID && selected >= 0)
			{
				const int64_t lobbyID = (int64_t)(intptr_t)GadgetListBoxGetItemData(control, selected, CHAT_COLUMN_JOIN);
				if (lobbyID > 0 && ClickedColumn(control) == CHAT_COLUMN_JOIN)
				{
					GadgetListBoxSetSelected(control, -1);
					JoinInvite(lobbyID);
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

					s_menuSelecting = TRUE;
					GadgetListBoxSetSelected(control, rc->pos);
					s_menuSelecting = FALSE;
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
		case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				if (control == nullptr || control->winGetWindowId() != textEntryFindID)
					return MSG_IGNORED;
				RunPlayerFind();
				break;
			}
		case GEM_UPDATE_TEXT:
			{
				// the search field filters the friends list as it is typed
				GameWindow *control = (GameWindow *)mData1;
				if (control == nullptr || control->winGetWindowId() != textEntryFindID)
					return MSG_IGNORED;
				GenOnlineUpdateBuddyInfo(true, true);
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
					ShowCommunicatorTab(GENONLINE_COMMUNICATOR_FRIENDS, FALSE);
				}
				else if (controlID == radioButtonRequestsID)
				{
					ShowCommunicatorTab(GENONLINE_COMMUNICATOR_REQUESTS, FALSE);
				}
				else if (controlID == radioButtonIgnoreID)
				{
					ShowCommunicatorTab(GENONLINE_COMMUNICATOR_BLOCKED, FALSE);
				}
				else if (controlID == buttonInviteID)
				{
					InvitePeer();
				}
				else if (controlID == buttonProfileID)
				{
					GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pSocial != nullptr && s_peerID >= 0)
					{
						GenOnlineSetLookAtPlayer(s_peerID, UnicodeString(CommunicatorPeerName(pSocial, s_peerID).c_str()));
						GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
					}
				}
				else if (controlID == buttonMoreID)
				{
					OpenPeerMenu();
				}
				else if (controlID == buttonAddBuddyID)
				{
					GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pSocial != nullptr && s_peerID >= 0 && pSocial->IsUserPendingRequest(s_peerID))
					{
						pSocial->AcceptPendingRequest(s_peerID);
					}
					else if (pSocial != nullptr && s_peerID >= 0 && !pSocial->IsUserFriend(s_peerID) && !pSocial->IsUserIgnored(s_peerID))
					{
						GenOnlineRequestBuddyAdd((Int)s_peerID, AsciiString(to_utf8(CommunicatorPeerName(pSocial, s_peerID)).c_str()));
					}
				}
				else if (controlID == buttonDeleteBuddyID)
				{
				}
				break;
			}
		case GLM_DOUBLE_CLICKED:
			{
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
					GenOnlineOpenCommunicator(GENONLINE_COMMUNICATOR_FRIENDS, s_noticePeer);
				}
				break;
			}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}

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

void GenOnlineRequestBuddyAdd(Int profileID, AsciiString nick);

static GameWindow *messageMenu = nullptr;
static std::string messageMenuID;
static int64_t messageMenuAuthorID = 0;
static AsciiString messageMenuAuthorName;
static NameKeyType buttonReportMessageID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteMessageID = NAMEKEY_INVALID;
static NameKeyType buttonProfileMessageID = NAMEKEY_INVALID;
static NameKeyType buttonFriendMessageID = NAMEKEY_INVALID;
static NameKeyType buttonBlockMessageID = NAMEKEY_INVALID;

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

void GenOnlineCloseRightClickMenus()
{
	if (rcMenu != nullptr)
	{
		closeRightClickMenu(rcMenu);
		rcMenu = nullptr;
	}
	if (messageMenu != nullptr)
	{
		closeRightClickMenu(messageMenu);
		messageMenu = nullptr;
		messageMenuID.clear();
	}
	// the player menu of the lobby is held by the lobby screen alone and shares the system callback
	GameWindow *lone = TheWindowManager->winGetLoneWindow();
	if (lone != nullptr && lone->winGetSystemFunc() == GenOnlineBuddyOverlayRCMenuSystem)
	{
		closeRightClickMenu(lone);
	}
}

void GenOnlineMessageRCMenuInit( WindowLayout *layout, void *userData )
{
	buttonReportMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonReport");
	buttonDeleteMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonDelete");
	buttonProfileMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonProfile");
	buttonFriendMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonFriend");
	buttonBlockMessageID = TheNameKeyGenerator->nameToKey("RCMessageMenu.wnd:ButtonBlock");
}

void GenOnlineOpenMessageMenu( GameWindow *listbox, Int row, Int mouseX, Int mouseY, const char *messageID, int64_t authorID, const char *authorName )
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	const int64_t localUserID = pAuth != nullptr ? pAuth->GetUserID() : -1;
	const std::string id = messageID != nullptr ? messageID : "";
	const GenOnlineRealtime::MessageMenu menu = GenOnlineRealtime::MessageMenuFor(id, authorID, localUserID, GenOnlineRealtimeBridge::HasPermission(GenOnlineRealtime::PERMISSION_DELETE_ANY));
	// The author entries need a real other player, a system line or the local player only gets the message entries.
	const Bool authorEntries = authorID > 0 && authorID != localUserID && pSocial != nullptr;
	if (listbox != nullptr && row >= 0)
	{
		GadgetListBoxSetSelected(listbox, row);
	}
	if (!menu.Any() && !authorEntries)
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

	struct MenuEntry
	{
		NameKeyType id;
		Bool shown;
	};
	const MenuEntry entries[] =
	{
		{ buttonProfileMessageID, authorEntries },
		{ buttonFriendMessageID, authorEntries },
		{ buttonBlockMessageID, authorEntries },
		{ buttonReportMessageID, menu.report },
		{ buttonDeleteMessageID, menu.remove },
	};

	GameWindow *buttonProfile = TheWindowManager->winGetWindowFromId(menuWindow, buttonProfileMessageID);
	GameWindow *buttonFriend = TheWindowManager->winGetWindowFromId(menuWindow, buttonFriendMessageID);
	GameWindow *buttonBlock = TheWindowManager->winGetWindowFromId(menuWindow, buttonBlockMessageID);
	GameWindow *buttonReport = TheWindowManager->winGetWindowFromId(menuWindow, buttonReportMessageID);
	if (buttonProfile != nullptr)
		GadgetButtonSetText(buttonProfile, UnicodeString(L"Profile"));
	if (buttonFriend != nullptr && authorEntries)
		GadgetButtonSetText(buttonFriend, UnicodeString(pSocial->IsUserFriend(authorID) ? L"Remove friend" : L"Add friend"));
	if (buttonBlock != nullptr && authorEntries)
		GadgetButtonSetText(buttonBlock, UnicodeString(pSocial->IsUserIgnored(authorID) ? L"Unblock" : L"Block"));
	if (buttonReport != nullptr)
		GadgetButtonSetText(buttonReport, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOReportMessage", L"Report"));

	// The shown entries stack from the first row in layout order, and the menu shrinks by the rows left over.
	Int firstX = 0, firstY = 0, rowStep = 0, shownCount = 0, entryCount = 0;
	for (const MenuEntry& entry : entries)
	{
		GameWindow *button = TheWindowManager->winGetWindowFromId(menuWindow, entry.id);
		if (button == nullptr)
			continue;
		Int x, y;
		button->winGetPosition(&x, &y);
		if (entryCount == 0)
		{
			firstX = x;
			firstY = y;
		}
		else if (entryCount == 1)
		{
			rowStep = y - firstY;
		}
		++entryCount;
	}
	for (const MenuEntry& entry : entries)
	{
		GameWindow *button = TheWindowManager->winGetWindowFromId(menuWindow, entry.id);
		if (button == nullptr)
			continue;
		button->winHide(!entry.shown);
		if (entry.shown)
		{
			button->winSetPosition(firstX, firstY + shownCount * rowStep);
			++shownCount;
		}
	}
	Int width, height;
	menuWindow->winGetSize(&width, &height);
	menuWindow->winSetSize(width, height - (entryCount - shownCount) * rowStep);

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
	messageMenuAuthorID = authorEntries ? authorID : 0;
	messageMenuAuthorName = authorName != nullptr ? authorName : "";
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
			const int64_t authorID = messageMenuAuthorID;
			const AsciiString authorName = messageMenuAuthorName;
			closeRightClickMenu(window);
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (authorID > 0 && pSocial != nullptr)
			{
				if (controlID == buttonProfileMessageID)
				{
					GenOnlineSetLookAtPlayer(authorID, UnicodeString(from_utf8(authorName.str()).c_str()));
					GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
					break;
				}
				if (controlID == buttonFriendMessageID)
				{
					if (pSocial->IsUserFriend(authorID))
						pSocial->RemoveFriend(authorID);
					else
						GenOnlineRequestBuddyAdd((Int)authorID, authorName);
					break;
				}
				if (controlID == buttonBlockMessageID)
				{
					if (pSocial->IsUserIgnored(authorID))
						pSocial->UnignoreUser(authorID);
					else
						pSocial->IgnoreUser(authorID);
					GenOnlineUpdateBuddyInfo();
					GenOnlineRefreshIgnoreList();
					GenOnlinePopulateLobbyPlayerListbox();
					break;
				}
			}
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

	// TODO_GENONLINE: Use the SH localization system for strings instead

	DEBUG_LOG(("Inserting buddy add request"));

	// put message on screen

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
				GadgetListBoxAddEntryText(listboxIgnore, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUnblock", L"Unblock"), CommunicatorWhite(), index, BLOCKED_COLUMN_UNBLOCK);
				GadgetListBoxSetItemData(listboxIgnore, (void*)(blockedEntry.user_id), index, 0);
			}
		});

}
