#pragma once

#include "GenOnlineInclude.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeDirectMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeFriendRoster.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessageActions.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "NetworkMesh.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"

namespace GenOnlineRealtimeBridge
{
enum class RestMethod;
}

struct FriendsResult
{
	std::vector<FriendsEntry> vecFriends;
	std::vector<FriendsEntry> vecPendingRequests;
};

struct BlockedResult
{
	std::vector<FriendsEntry> vecBlocked;
};

class GenOnlineSocialInterface
{
public:
	GenOnlineSocialInterface();
	~GenOnlineSocialInterface();

	void GetFriendsList(bool bUseCache, std::function<void()> cb);

	void GetBlockList(std::function<void(BlockedResult blockResult)> cb);

	void AddFriend(int64_t target_user_id);
	void RemoveFriend(int64_t target_user_id);

	void IgnoreUser(int64_t target_user_id);
	void UnignoreUser(int64_t target_user_id);

	void AcceptPendingRequest(int64_t target_user_id);
	void RejectPendingRequest(int64_t target_user_id);
	// Withdraws a friend request the local player sent.
	void CancelSentRequest(int64_t target_user_id);

	// Invites a friend to the lobby the local player is in. The text is empty on success, else the reason to show.
	void InviteToLobby(int64_t target_user_id, std::function<void(std::wstring strFailure)> cb);
	void OnRealtimeLobbyInvite(const GenOnlineRealtime::LobbyInviteEvent& event);

	// The user of the conversation that changed last, -1 when there is none.
	int64_t GetLastConversationPeer() const { return m_lastConversationPeer; }

	void OnChatMessage(int64_t source_user_id, int64_t target_user_id, UnicodeString unicodeStr);
	void OnOnlineStatusChanged(std::string strDisplayName, bool bOnline);

	// Saves the local presence status, "online", "idle", "do_not_disturb" or "invisible", and sends it to the service.
	void SetLocalPresenceStatus(const std::string& status);
	// The service keeps the status in memory per user, so it is sent again after every connect.
	void SendLocalPresenceStatus();
	// No notification popup shows while the local status is do not disturb. The pill still counts them.
	static bool NotificationsMuted();

	void OnFriendRequestAccepted(std::string strDisplayName);

	bool IsUserIgnored(int64_t target_user_id);
	bool IsUserFriend(int64_t target_user_id);
	bool IsUserPendingRequest(int64_t target_user_id);

	// NOTE: If we aren't registered for indepth notifications (only when UI is visible), we will just get online/offline status changes.
	//	     This cuts down on server traffic, and we don't need the additional info like presence etc unless the UI is visible
	void RegisterForRealtimeServiceUpdates();
	void DeregisterForRealtimeServiceUpdates();

	// TODO_SOCIAL: Store unread messages in DB so data isnt lost if user logs out or crashes etc without reading them?
	std::vector<GenOnlineRealtime::DirectChatLine> GetChatMessagesForUser(int64_t target_user_id)
	{
		auto it = m_mapCachedMessages.find(target_user_id);
		if (it != m_mapCachedMessages.end())
		{
			return it->second.Lines();
		}

		return std::vector<GenOnlineRealtime::DirectChatLine>();
	}

	static UnicodeString GetChatLineText(const GenOnlineRealtime::DirectChatLine& line);

	void ClearUnreadChatMessagesForUser(int64_t target_user_id)
	{
		m_unreadConversations.Clear(target_user_id);
	}

	int GetNumberUnreadChatMessagesForUser(int64_t target_user_id)
	{
		return m_unreadConversations.Unread(target_user_id);
	}

	// The conversations with unread lines, each counted once.
	int GetNumUnreadConversations() const
	{
		int numUnread = 0;
		for (int64_t peer : m_unreadConversations.Peers())
		{
			numUnread += m_unreadConversations.Unread(peer) > 0 ? 1 : 0;
		}
		return numUnread;
	}

	// True when the communicator may open a chat with the user: a friend, anyone for staff holding
	// chat.message_anyone, or a non-friend whose conversation the service lists (a staff member who wrote).
	bool CanChatWith(int64_t target_user_id);

	// The non-friend users with a conversation, without pending requests, with the name to show.
	std::vector<std::pair<int64_t, std::string>> GetNonFriendConversations();

	// Finds the players whose display name starts with the name and lists them in the communicator.
	// A name shorter than 2 characters clears the results.
	void FindPlayers(const std::string& strName);
	std::vector<GenOnlineRealtime::Member> GetFoundPlayers()
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		return m_vecFoundPlayers;
	}

	// The message of a line of the conversation with the user, for its context menu. Null for a note or an unknown line.
	const GenOnlineRealtime::ChatMessage* FindConversationMessage(int64_t target_user_id, uint32_t key) const
	{
		auto it = m_mapCachedMessages.find(target_user_id);
		return it != m_mapCachedMessages.end() ? it->second.MessageOf(key) : nullptr;
	}

	// The player views the conversation. It loads the history once and marks an unread conversation read as a whole.
	void ViewConversation(int64_t target_user_id);

	// Callbacks
	void InvokeCallback_NewFriendRequest(std::string strDisplayName);
	void RegisterForCallback_NewFriendRequest(std::function<void(std::string strDisplayName)> cbOnNewFriendRequest)
	{
		m_cbOnNewFriendRequest = cbOnNewFriendRequest;
	}

	// The key names the chat line of a message and is 0 for a local note.
	void RegisterForCallback_OnChatMessage(std::function<void(int64_t peer_user_id, uint32_t key, UnicodeString unicodeStr)> cbOnChatMessage)
	{
		m_cbOnChatMessage = cbOnChatMessage;
	}

	// The lines of a conversation changed as a whole. The user id is -1 for every conversation.
	void RegisterForCallback_OnConversationReloaded(std::function<void(int64_t peer_user_id)> cb)
	{
		m_cbOnConversationReloaded = cb;
	}

	void RegisterForCallback_OnChatLineRemoved(std::function<void(int64_t peer_user_id, uint32_t key)> cb)
	{
		m_cbOnChatLineRemoved = cb;
	}

	const std::unordered_map<int64_t, FriendsEntry>& GetRecentlyPlayedWithList()
    {
		// is it stale? clear it out
		const int64_t recentPlayersListLifespan = 600000; // 10 minutes
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
		if (currTime - m_RecentlyPlayedWithTimestamp >= recentPlayersListLifespan)
		{
			m_mapRecentlyPlayedWith.clear();
		}

        return m_mapRecentlyPlayedWith;
    }

	std::unordered_map<int64_t, FriendsEntry> GetCachedFriendsList()
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		return m_mapFriends;
	}

    std::unordered_map<int64_t, FriendsEntry> GetCachedRequestsList()
    {
        std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
        return m_mapPendingRequests;
    }

	// The friend requests the local player sent that wait for an answer.
	std::unordered_map<int64_t, FriendsEntry> GetCachedSentRequestsList()
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		return m_mapSentRequests;
	}

	std::unordered_map<int64_t, FriendsEntry> GetCachedBlockedList()
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		return m_mapBlocked;
	}

	void RegisterForCallback_OnNumberGlobalNotificationsChanged(std::function<void(int newNumNotifications)> cb)
	{
		m_cbOnNumberGlobalNotificationsChanged = cb;
	}

	void ClearGlobalNotificatations()
	{
		m_numTotalNotifications = 0;

		TriggerCallback_OnNumberGlobalNotificationsChanged();
	}

	void RegisterInitialPendingRequestsUponLogin(int num)
	{
		m_numTotalNotifications = num;

		if (num > 0)
		{
			TriggerCallback_OnNumberGlobalNotificationsChanged();
		}
	}

	int GetNumTotalNotifications() const
	{
		return std::max<int>(0, m_numTotalNotifications);
	}

	void CommitLobbyPlayerListToRecentlyPlayedWithList();

	// Realtime protocol v1: the hub sends a snapshot after every connect and the changes as events.
	void ExpectRealtimeWelcome() { m_bRealtimeWelcomePending = true; }
	void OnRealtimeFriends(const GenOnlineRealtime::FriendsEvent& event);
	void OnRealtimeFriendRequestReceived(const GenOnlineRealtime::Member& from);
	void OnRealtimeFriendRequestAccepted(const GenOnlineRealtime::Member& by);
	void OnRealtimeFriendRemoved(int64_t user_id);
	void OnRealtimeFriendPresence(const GenOnlineRealtime::FriendPresenceEvent& event);
	void OnRealtimeMemberUpdate(const GenOnlineRealtime::Member& member);
	void OnRealtimeDirectMessage(const GenOnlineRealtime::ChatMessage& message);
	void OnRealtimeConversations(const GenOnlineRealtime::ConversationsEvent& event);
	void OnRealtimeReadMarker(const GenOnlineRealtime::ReadMarkerEvent& event);
	void OnRealtimeDirectMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event);

private:
	// Calls a social REST endpoint and refreshes the communicator once the service answered.
	// The hub pushes the resulting events as well.
	void CallSocialRest(GenOnlineRealtimeBridge::RestMethod method, const std::string& path, const std::string& body, std::function<void(GenOnlineSocialInterface* pThis)> onSuccess);
	std::string FindDisplayName(int64_t user_id);
	bool m_bRealtimeWelcomePending = false;
	GenOnlineRealtime::DirectConversation& GetConversation(int64_t peer_user_id);
	// Adds a shown line to the unread count, the notification and the open chat.
	void OnConversationLine(int64_t peer_user_id, bool bFromMe, uint32_t key, const UnicodeString& unicodeStr);

	void TriggerCallback_OnNumberGlobalNotificationsChanged()
	{
		if (m_cbOnNumberGlobalNotificationsChanged)
		{
			m_cbOnNumberGlobalNotificationsChanged(m_numTotalNotifications);
		}
	}

    // This includes:
    // - one count per player who has sent us unread messages (we dont show number of total messages)
    // - number of pending friend requests
	int m_numTotalNotifications = 0;

	// NOTE: We cache messages here, because the UI isn't always present, but we dont want to miss messages
	// TODO_SOCIAL: Limit this
	std::map<int64_t, GenOnlineRealtime::DirectConversation> m_mapCachedMessages; // user id here is who we are talking to / target user
	GenOnlineRealtime::DirectMessageUnread m_unreadConversations; // Keyed by the other user of each conversation.
	std::map<int64_t, std::string> m_mapConversationNames; // The other user's name, from their last message seen.
	std::vector<GenOnlineRealtime::Member> m_vecFoundPlayers; // The last player search, guarded by m_friendsMapMutex.

	std::function<void(int newNumNotifications)> m_cbOnNumberGlobalNotificationsChanged = nullptr;

	std::function<void(std::string strDisplayName)> m_cbOnNewFriendRequest = nullptr;

	std::function<void(int64_t peer_user_id, uint32_t key, UnicodeString unicodeStr)> m_cbOnChatMessage = nullptr;
	std::function<void(int64_t peer_user_id)> m_cbOnConversationReloaded = nullptr;
	std::function<void(int64_t peer_user_id, uint32_t key)> m_cbOnChatLineRemoved = nullptr;

	// Cached, may be out of date if friends UI isnt active, optimized for lookup
	// Rebuilt on the HTTP thread and read from the main thread, so all access goes through m_friendsMapMutex.
	mutable std::mutex m_friendsMapMutex;
	std::unordered_map<int64_t, FriendsEntry> m_mapFriends;
	std::unordered_map<int64_t, FriendsEntry> m_mapPendingRequests;
	std::unordered_map<int64_t, FriendsEntry> m_mapSentRequests;
	std::unordered_map<int64_t, FriendsEntry> m_mapBlocked;
	int64_t m_lastConversationPeer = -1;

	// managed on client / locally
	std::unordered_map<int64_t, FriendsEntry> m_mapRecentlyPlayedWith;
	int64_t m_RecentlyPlayedWithTimestamp = -1;

	bool m_bOverlayActive = false;
};
