#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/RankPointValue.h"
#include "../GenOnlineInit.h"
#include "../HTTP/HTTPManager.h"
#include "GameClient/GameText.h"

GenOnlineSocialInterface::GenOnlineSocialInterface()
{

}

GenOnlineSocialInterface::~GenOnlineSocialInterface()
{

}

void GenOnlineSocialInterface::GetFriendsList(bool bUseCache, std::function<void()> cb)
{
	// The hub keeps the cache current, so there is nothing to fetch.
	if (cb != nullptr)
	{
		cb();
	}
}

void GenOnlineSocialInterface::GetBlockList(std::function<void(BlockedResult blockResult)> cb)
{
	BlockedResult blockedResult;
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		for (const auto& entry : m_mapBlocked)
		{
			blockedResult.vecBlocked.push_back(entry.second);
		}
	}
	if (cb != nullptr)
	{
		cb(blockedResult);
	}
}

void GenOnlineSocialInterface::AddFriend(int64_t target_user_id)
{
	GenOnlineRealtime::FriendRequestBody body;
	body.user_id = target_user_id;
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Post, GenOnlineRealtime::RestFriendRequestsPath(), nlohmann::json(body).dump(), nullptr);
}

void GenOnlineSocialInterface::RemoveFriend(int64_t target_user_id)
{
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Delete, GenOnlineRealtime::RestFriendPath(target_user_id), std::string(), [target_user_id](GenOnlineSocialInterface* pThis)
		{
			std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
			pThis->m_mapFriends.erase(target_user_id);
		});
}

void GenOnlineSocialInterface::IgnoreUser(int64_t target_user_id)
{
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Put, GenOnlineRealtime::RestBlockPath(target_user_id), std::string(), [target_user_id](GenOnlineSocialInterface* pThis)
		{
			FriendsEntry entry;
			entry.user_id = target_user_id;
			entry.display_name = pThis->FindDisplayName(target_user_id);
			entry.online = false;
			std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
			pThis->m_mapFriends.erase(target_user_id);
			pThis->m_mapPendingRequests.erase(target_user_id);
			pThis->m_mapBlocked.insert_or_assign(target_user_id, entry);
		});
}

void GenOnlineSocialInterface::UnignoreUser(int64_t target_user_id)
{
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Delete, GenOnlineRealtime::RestBlockPath(target_user_id), std::string(), [target_user_id](GenOnlineSocialInterface* pThis)
		{
			std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
			pThis->m_mapBlocked.erase(target_user_id);
		});
}

void GenOnlineSocialInterface::AcceptPendingRequest(int64_t target_user_id)
{
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Post, GenOnlineRealtime::RestFriendRequestAcceptPath(target_user_id), std::string(), [target_user_id](GenOnlineSocialInterface* pThis)
		{
			// The FriendPresence of the new friend usually arrived before this answer and is kept on the request.
			std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
			GenOnlineRealtime::MoveAcceptedRequest(pThis->m_mapFriends, pThis->m_mapPendingRequests, target_user_id);
		});
	if (m_numTotalNotifications > 0) --m_numTotalNotifications;
	TriggerCallback_OnNumberGlobalNotificationsChanged();
}

void GenOnlineSocialInterface::RejectPendingRequest(int64_t target_user_id)
{
	CallSocialRest(GenOnlineRealtimeBridge::RestMethod::Delete, GenOnlineRealtime::RestFriendRequestPath(target_user_id), std::string(), [target_user_id](GenOnlineSocialInterface* pThis)
		{
			std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
			pThis->m_mapPendingRequests.erase(target_user_id);
		});
	if (m_numTotalNotifications > 0) --m_numTotalNotifications;
	TriggerCallback_OnNumberGlobalNotificationsChanged();
}

void GenOnlineSocialInterface::OnChatMessage(int64_t source_user_id, int64_t target_user_id, UnicodeString unicodeStr)
{
	// also cache it incase UI isnt visible
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		int64_t user_id_to_store = -1;

		// was it me chatting?
		if (pAuthInterface->GetUserID() == source_user_id)
		{
			// cache under target user
			user_id_to_store = target_user_id;
		}
		else
		{
			// cache under source user
			user_id_to_store = source_user_id;
		}

		GetConversation(user_id_to_store).AddNote(std::wstring(unicodeStr.str()));
		OnConversationLine(user_id_to_store, pAuthInterface->GetUserID() == source_user_id, 0, unicodeStr);
	}
}

GenOnlineRealtime::DirectConversation& GenOnlineSocialInterface::GetConversation(int64_t peer_user_id)
{
	auto it = m_mapCachedMessages.find(peer_user_id);
	if (it == m_mapCachedMessages.end())
	{
		it = m_mapCachedMessages.emplace(peer_user_id, GenOnlineRealtime::DirectConversation(GenOnlineRealtime::DirectMessageChannel(peer_user_id))).first;
	}
	return it->second;
}

static bool ConversationNotificationsEnabled()
{
	if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
	{
		return GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Gameplay();
	}
	return GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Menus();
}

void GenOnlineSocialInterface::OnConversationLine(int64_t user_id_to_store, bool bFromMe, uint32_t key, const UnicodeString& unicodeStr)
{
	// only if I am not the sender and overlay isnt active
	if (!bFromMe && !m_bOverlayActive)
	{
		if (m_unreadConversations.AddUnread(user_id_to_store))
		{
			++m_numTotalNotifications; // only increase this if we dont already have unread messages from the person
			TriggerCallback_OnNumberGlobalNotificationsChanged();
		}
		// show popup for incoming message
		if (ConversationNotificationsEnabled())
		{
#if defined(GENONLINE)
			GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr, true /*bPlaySound*/);
#else
			GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr);
#endif
		}
	}
	if (m_cbOnChatMessage != nullptr)
	{
		m_cbOnChatMessage(user_id_to_store, key, unicodeStr);
	}
}

void GenOnlineSocialInterface::OnOnlineStatusChanged(std::string strDisplayName, bool bOnline)
{
	bool bShowNotification = false;
	if (bOnline)
	{
		if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
		{
			bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Gameplay();
		}
		else
		{
			bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Menus();
		}
	}
	else
	{
        if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
        {
            bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_FriendGoesOffline_Gameplay();
        }
        else
        {
            bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_FriendGoesOffline_Menus();
        }
	}

	// TODO_SOCIAL: Update communicator if its active
	if (bShowNotification)
	{
		GenOnlineShowNotificationBox(AsciiString(strDisplayName.c_str()), bOnline ? TheGameText->fetch("Buddy:OnlineNotification") : UnicodeString(L"%hs went offline"));
	}
}

void GenOnlineSocialInterface::OnFriendRequestAccepted(std::string strDisplayName)
{
	bool bShowNotification = true;
    if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
    {
        bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_PlayerAcceptsRequest_Gameplay();
    }
    else
    {
        bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_PlayerAcceptsRequest_Menus();
    }

	if (bShowNotification)
	{
		GenOnlineShowNotificationBox(AsciiString(strDisplayName.c_str()), UnicodeString(L"%hs accepted your friend request."));
	}
}

bool GenOnlineSocialInterface::IsUserIgnored(int64_t target_user_id)
{
	const std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
	return m_mapBlocked.contains(target_user_id);
}
bool GenOnlineSocialInterface::IsUserFriend(int64_t target_user_id)
{
	const std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
	return m_mapFriends.contains(target_user_id);
}
bool GenOnlineSocialInterface::IsUserPendingRequest(int64_t target_user_id)
{
	std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
    return m_mapPendingRequests.contains(target_user_id);
}

void GenOnlineSocialInterface::RegisterForRealtimeServiceUpdates()
{
	m_bOverlayActive = true;
}

void GenOnlineSocialInterface::DeregisterForRealtimeServiceUpdates()
{
	m_bOverlayActive = false;
}

void GenOnlineSocialInterface::InvokeCallback_NewFriendRequest(std::string strDisplayName)
{
	// only if overlay isnt active
	if (!m_bOverlayActive)
	{
        ++m_numTotalNotifications;
        TriggerCallback_OnNumberGlobalNotificationsChanged();
	}

	if (m_cbOnNewFriendRequest != nullptr)
	{
		m_cbOnNewFriendRequest(strDisplayName);
	}

    bool bShowNotification = true;
    if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
    {
        bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_PlayerSendsRequest_Gameplay();
    }
    else
    {
        bShowNotification = GenOnlineServicesManager::Settings.Social_Notifications_PlayerSendsRequest_Menus();
    }

	if (bShowNotification)
	{
		GenOnlineShowNotificationBox(AsciiString(strDisplayName.c_str()), TheGameText->fetch("Buddy:AddNotification"));
	}
}

void GenOnlineSocialInterface::CommitLobbyPlayerListToRecentlyPlayedWithList()
{
    GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
    int64_t user_id = pAuthInterface != nullptr ? pAuthInterface->GetUserID() : -1;

    m_mapRecentlyPlayedWith.clear();

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
                        // dont show if already friends
                        if (!IsUserFriend(profileID))
                        {
                            FriendsEntry newEntry;
                            newEntry.user_id = profileID;
                            newEntry.display_name = to_utf8(slot->getName().str());
                            m_mapRecentlyPlayedWith.emplace(profileID, newEntry);
                        }
                    }
                }
            }
        }

	m_RecentlyPlayedWithTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
}


// The communicator read the cache before the service answered, so its lists are drawn again.
static void RefreshCommunicator()
{
	extern void GenOnlineUpdateBuddyInfo(bool bIsAutoRefresh, bool bUseCache);
	extern void GenOnlineRefreshIgnoreList();
	GenOnlineUpdateBuddyInfo(true, true);
	GenOnlineRefreshIgnoreList();
}

void GenOnlineSocialInterface::CallSocialRest(GenOnlineRealtimeBridge::RestMethod method, const std::string& path, const std::string& body, std::function<void(GenOnlineSocialInterface* pThis)> onSuccess)
{
	GenOnlineRealtimeBridge::CallRest(method, path, body, [onSuccess](const GenOnlineRealtime::RestOutcome& outcome)
		{
			GenOnlineSocialInterface* pThis = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pThis == nullptr)
			{
				return;
			}
			if (!outcome.Succeeded())
			{
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str()));
				return;
			}
			if (onSuccess)
			{
				onSuccess(pThis);
			}
			RefreshCommunicator();
		});
}

static FriendsEntry ToFriendsEntry(const GenOnlineRealtime::Member& member, bool bOnline)
{
	FriendsEntry entry;
	entry.user_id = member.id;
	entry.display_name = member.name;
	entry.online = bOnline;
	return entry;
}

std::string GenOnlineSocialInterface::FindDisplayName(int64_t user_id)
{
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		for (const auto* pMap : { &m_mapFriends, &m_mapPendingRequests, &m_mapBlocked })
		{
			auto it = pMap->find(user_id);
			if (it != pMap->end())
			{
				return it->second.display_name;
			}
		}
	}
	GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	NetworkRoomMember* pMember = pRooms != nullptr ? pRooms->GetRoomMemberFromID(user_id) : nullptr;
	return pMember != nullptr ? pMember->display_name : std::string();
}

void GenOnlineSocialInterface::OnRealtimeFriends(const GenOnlineRealtime::FriendsEvent& event)
{
	int numOnline = 0;
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		m_mapFriends.clear();
		m_mapPendingRequests.clear();
		m_mapBlocked.clear();
		for (const GenOnlineRealtime::FriendEntry& entry : event.friends)
		{
			FriendsEntry friendEntry = ToFriendsEntry(entry, entry.online);
			friendEntry.presence = GenOnlineRealtimeBridge::ActivityText(entry.activity);
			m_mapFriends.insert_or_assign(entry.id, friendEntry);
			numOnline += entry.online ? 1 : 0;
		}
		for (const GenOnlineRealtime::Member& member : event.incoming)
		{
			m_mapPendingRequests.insert_or_assign(member.id, ToFriendsEntry(member, false));
		}
		for (const GenOnlineRealtime::Member& member : event.blocked)
		{
			m_mapBlocked.insert_or_assign(member.id, ToFriendsEntry(member, false));
		}
	}

	// The first snapshot after a login greets the player, as the legacy status update did.
	if (m_bRealtimeWelcomePending)
	{
		m_bRealtimeWelcomePending = false;
		const int numPending = static_cast<int>(event.incoming.size());
		UnicodeString strText;
		if (numOnline > 0 && numPending > 0)
		{
			strText.format(L"You have %d friend(s) online and %d pending friend request(s)", numOnline, numPending);
		}
		else if (numOnline > 0)
		{
			strText.format(L"You have %d friend(s) online.", numOnline);
		}
		else if (numPending > 0)
		{
			strText.format(L"You have %d pending friend request(s)", numPending);
		}
		else
		{
			strText = UnicodeString(L"Press F5 or INSERT to bring up the communicator at any time (including in-game).");
		}
		if (numPending > 0)
		{
			RegisterInitialPendingRequestsUponLogin(numPending);
		}
		GenOnlineShowNotificationBox(AsciiString::TheEmptyString, strText);
	}
	RefreshCommunicator();
}

void GenOnlineSocialInterface::OnRealtimeFriendRequestReceived(const GenOnlineRealtime::Member& from)
{
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		m_mapPendingRequests.insert_or_assign(from.id, ToFriendsEntry(from, false));
	}
	InvokeCallback_NewFriendRequest(from.name);
}

void GenOnlineSocialInterface::OnRealtimeFriendRequestAccepted(const GenOnlineRealtime::Member& by)
{
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		m_mapPendingRequests.erase(by.id);
		m_mapFriends.insert_or_assign(by.id, ToFriendsEntry(by, true));
	}
	OnFriendRequestAccepted(by.name);
	RefreshCommunicator();
}

void GenOnlineSocialInterface::OnRealtimeFriendRemoved(int64_t user_id)
{
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		m_mapFriends.erase(user_id);
		m_mapPendingRequests.erase(user_id);
	}
	RefreshCommunicator();
}

void GenOnlineSocialInterface::OnRealtimeFriendPresence(const GenOnlineRealtime::FriendPresenceEvent& event)
{
	const std::string strPresence = GenOnlineRealtimeBridge::ActivityText(event.activity);
	GenOnlineRealtime::FriendPresenceChange change;
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		change = GenOnlineRealtime::ApplyFriendPresence(m_mapFriends, m_mapPendingRequests, event, strPresence);
	}
	if (change.onlineChanged)
	{
		OnOnlineStatusChanged(change.displayName, event.online);
	}
	if (change.friendChanged)
	{
		RefreshCommunicator();
	}
}

void GenOnlineSocialInterface::OnRealtimeMemberUpdate(const GenOnlineRealtime::Member& member)
{
	bool bChanged = false;
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		for (auto* pMap : { &m_mapFriends, &m_mapPendingRequests, &m_mapBlocked })
		{
			auto it = pMap->find(member.id);
			if (it != pMap->end() && it->second.display_name != member.name)
			{
				it->second.display_name = member.name;
				bChanged = true;
			}
		}
	}
	if (bChanged)
	{
		RefreshCommunicator();
	}
}

// A direct message is cached under the other player of the conversation, as the legacy friend chat was.
// The channel dm:<id> names the other player, also for an own message sent from another connection.
void GenOnlineSocialInterface::OnRealtimeDirectMessage(const GenOnlineRealtime::ChatMessage& message)
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth == nullptr)
	{
		return;
	}
	const int64_t peerID = GenOnlineRealtime::DirectMessagePeer(message.channel_id);
	if (peerID < 0)
	{
		return;
	}
	GenOnlineRealtime::DirectChatLine line;
	if (!GetConversation(peerID).AddLive(message, line))
	{
		return;
	}
	// a staff member who is no friend can write, and the communicator then lists the conversation
	m_unreadConversations.NoteConversation(peerID);
	if (message.author.id == peerID && !message.author.name.empty())
	{
		m_mapConversationNames[peerID] = message.author.name;
	}
	const bool bFromMe = message.author.id == pAuth->GetUserID();
	// The notification box plays its own sound, so a mention it shows plays no second sound.
	const bool bNoticeSound = !bFromMe && !m_bOverlayActive && ConversationNotificationsEnabled();
	OnConversationLine(peerID, bFromMe, line.key, GetChatLineText(line));
	if (!bNoticeSound && GenOnlineRealtime::MentionsUser(message, pAuth->GetUserID()))
	{
		GenOnlinePlayMentionSound();
	}
}

bool GenOnlineSocialInterface::CanChatWith(int64_t target_user_id)
{
	return GenOnlineRealtime::CanChatWith(IsUserFriend(target_user_id), GenOnlineRealtimeBridge::HasPermission(GenOnlineRealtime::PERMISSION_MESSAGE_ANYONE), m_unreadConversations.HasConversation(target_user_id));
}

std::vector<std::pair<int64_t, std::string>> GenOnlineSocialInterface::GetNonFriendConversations()
{
	std::vector<std::pair<int64_t, std::string>> conversations;
	for (int64_t peer : m_unreadConversations.Peers())
	{
		if (IsUserFriend(peer) || IsUserPendingRequest(peer))
		{
			continue;
		}
		auto it = m_mapConversationNames.find(peer);
		std::string name = it != m_mapConversationNames.end() ? it->second : FindDisplayName(peer);
		if (name.empty())
		{
			name = "Player " + std::to_string(peer);
		}
		conversations.emplace_back(peer, name);
	}
	return conversations;
}

void GenOnlineSocialInterface::FindPlayers(const std::string& strName)
{
	const std::string term = GenOnlineRealtime::PlayerSearchTerm(strName);
	if (term.empty())
	{
		{
			std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
			m_vecFoundPlayers.clear();
		}
		RefreshCommunicator();
		return;
	}
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Get, GenOnlineRealtime::RestPlayerSearchPath(term), std::string(), [](const GenOnlineRealtime::RestOutcome& outcome)
		{
			GenOnlineSocialInterface* pThis = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pThis == nullptr)
			{
				return;
			}
			GenOnlineRealtime::PlayerListResponse found;
			if (!outcome.Succeeded() || !outcome.Data(found))
			{
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str()), false);
				return;
			}
			if (found.players.empty())
			{
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GONoPlayerFound", L"No player has a name that starts with that."));
			}
			{
				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				pThis->m_vecFoundPlayers = found.players;
			}
			RefreshCommunicator();
		});
}

UnicodeString GenOnlineSocialInterface::GetChatLineText(const GenOnlineRealtime::DirectChatLine& line)
{
	if (line.IsNote())
	{
		return UnicodeString(line.note.c_str());
	}
	const std::wstring text = from_utf8(line.message.author.name) + (line.message.type == "action" ? L" " : L": ") + from_utf8(line.message.content);
	return UnicodeString(text.c_str());
}

// Every connect sends the snapshot, so each conversation loads its history again when it is viewed.
void GenOnlineSocialInterface::OnRealtimeConversations(const GenOnlineRealtime::ConversationsEvent& event)
{
	const int numBecameUnread = m_unreadConversations.ApplySnapshot(event);
	for (const GenOnlineRealtime::Conversation& conversation : event.conversations)
	{
		const int64_t peer = GenOnlineRealtime::DirectMessagePeer(conversation.channel_id);
		// The peer of the snapshot names the other user even when the last message is the local player's own.
		if (peer >= 0 && conversation.peer.has_value() && conversation.peer->id == peer && !conversation.peer->name.empty())
		{
			m_mapConversationNames[peer] = conversation.peer->name;
		}
		else if (peer >= 0 && !conversation.last.isNull && conversation.last.value.author.id == peer && !conversation.last.value.author.name.empty())
		{
			m_mapConversationNames[peer] = conversation.last.value.author.name;
		}
	}
	for (auto& kvPair : m_mapCachedMessages)
	{
		kvPair.second.ForgetHistory();
	}
	if (numBecameUnread > 0)
	{
		m_numTotalNotifications += numBecameUnread;
		TriggerCallback_OnNumberGlobalNotificationsChanged();
	}
	if (m_cbOnConversationReloaded != nullptr)
	{
		m_cbOnConversationReloaded(-1);
	}
	RefreshCommunicator();
}

void GenOnlineSocialInterface::OnRealtimeReadMarker(const GenOnlineRealtime::ReadMarkerEvent& event)
{
	if (m_unreadConversations.ApplyReadMarker(event))
	{
		RefreshCommunicator();
	}
}

void GenOnlineSocialInterface::OnRealtimeDirectMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event)
{
	const int64_t peerID = GenOnlineRealtime::DirectMessagePeer(event.channel_id);
	auto it = m_mapCachedMessages.find(peerID);
	if (it == m_mapCachedMessages.end())
	{
		return;
	}
	const uint32_t key = it->second.Remove(event.message_id);
	if (key != 0 && m_cbOnChatLineRemoved != nullptr)
	{
		m_cbOnChatLineRemoved(peerID, key);
	}
}

void GenOnlineSocialInterface::ViewConversation(int64_t target_user_id)
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pAuth == nullptr || pWS == nullptr)
	{
		m_unreadConversations.Clear(target_user_id);
		return;
	}
	GenOnlineRealtime::DirectConversation& conversation = GetConversation(target_user_id);
	if (conversation.NeedsHistory())
	{
		conversation.BeginHistory();
		GenOnlineRealtime::GetHistoryRequest request;
		request.channel_id = conversation.ChannelID();
		request.limit = GenOnlineRealtime::DIRECT_HISTORY_LIMIT;
		pWS->InvokeHub(request, [target_user_id](const GenOnlineRealtime::InvokeOutcome& outcome)
			{
				GenOnlineSocialInterface* pThis = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
				if (pThis == nullptr)
				{
					return;
				}
				GenOnlineRealtime::DirectConversation& conversation = pThis->GetConversation(target_user_id);
				GenOnlineRealtime::GetHistoryResult result;
				if (!outcome.Data(result))
				{
					conversation.HistoryFailed();
					return;
				}
				conversation.AddHistory(result);
				if (pThis->m_cbOnConversationReloaded != nullptr)
				{
					pThis->m_cbOnConversationReloaded(target_user_id);
				}
			});
	}

	// Viewing an unread conversation marks the whole conversation read.
	if (m_unreadConversations.Unread(target_user_id) == 0)
	{
		return;
	}
	m_unreadConversations.Clear(target_user_id);
	GenOnlineRealtime::MarkReadRequest request;
	request.channel_id = conversation.ChannelID();
	pWS->InvokeHub(request, nullptr);
}
