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
	if (bUseCache || UsesRealtimeHub())
	{
		if (cb != nullptr)
		{
			cb();
		}

		return;
	}

	{
		std::scoped_lock<std::mutex> lock(m_friendsListCallbackMutex);
		m_cbOnGetFriendsList = cb;
	}

	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Social/Friends");
	std::map<std::string, std::string> mapHeaders;

	// Capture callback by local copy to avoid race condition if GetFriendsList is called again
	auto localCallback = cb;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [localCallback](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			FriendsResult friendsResult;

			try
			{
				GenOnlineSocialInterface* pThis = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
				if (pThis == nullptr)
					return;

				std::unordered_map<int64_t, FriendsEntry> mapFriends{};
				std::unordered_map<int64_t, FriendsEntry> mapPendingRequests{};

				nlohmann::json jsonObject = nlohmann::json::parse(strBody);

				// friends
				for (const auto& friendEntryIter : jsonObject["friends"])
				{
					FriendsEntry newFriend;

					friendEntryIter["user_id"].get_to(newFriend.user_id);
					friendEntryIter["display_name"].get_to(newFriend.display_name);
					friendEntryIter["online"].get_to(newFriend.online);
					friendEntryIter["presence"].get_to(newFriend.presence);


					friendsResult.vecFriends.push_back(newFriend);

					// cache
					mapFriends.insert_or_assign(newFriend.user_id, newFriend);
				}

				// pending requests
				for (const auto& friendEntryIter : jsonObject["pending_requests"])
				{
					FriendsEntry newEntry;

					friendEntryIter["user_id"].get_to(newEntry.user_id);
					friendEntryIter["display_name"].get_to(newEntry.display_name);


					friendsResult.vecPendingRequests.push_back(newEntry);

					// cache
					mapPendingRequests.insert_or_assign(newEntry.user_id, newEntry);
				}

				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				pThis->m_mapFriends.swap(mapFriends);
				pThis->m_mapPendingRequests.swap(mapPendingRequests);
			}
			catch (...)
			{

			}

			// Use local callback copy instead of potentially overwritten member
			if (localCallback != nullptr)
			{
				localCallback();
			}
		});
}

void GenOnlineSocialInterface::GetBlockList(std::function<void(BlockedResult blockResult)> cb)
{
	if (UsesRealtimeHub())
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
		return;
	}

	m_cbOnGetBlockList = cb;

	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Social/Blocked");
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			BlockedResult blockedResult;

			try
			{
				std::unordered_map<int64_t, FriendsEntry> mapBlocked{};
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);

				for (const auto& blockedEntryIter : jsonObject["blocked"])
				{
					FriendsEntry newEntry;

					blockedEntryIter["user_id"].get_to(newEntry.user_id);
					blockedEntryIter["display_name"].get_to(newEntry.display_name);


					blockedResult.vecBlocked.push_back(newEntry);

					// cache
					mapBlocked.insert_or_assign(newEntry.user_id, newEntry);
				}

				std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
				m_mapBlocked.swap(mapBlocked);
			}
			catch (...)
			{

			}

			if (m_cbOnGetBlockList != nullptr)
			{
				// TODO_SOCIAL: Clean this up on exit etc
				m_cbOnGetBlockList(blockedResult);
				m_cbOnGetBlockList = nullptr;
			}
		});
}

void GenOnlineSocialInterface::AddFriend(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::SendFriendRequestRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, nullptr);
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Friends/Requests"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPUTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});
}

void GenOnlineSocialInterface::RemoveFriend(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::RemoveFriendRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, [target_user_id](GenOnlineSocialInterface* pThis)
			{
				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				pThis->m_mapFriends.erase(target_user_id);
			});
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Friends"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});
}

void GenOnlineSocialInterface::IgnoreUser(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::BlockUserRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, [target_user_id](GenOnlineSocialInterface* pThis)
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
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Blocked"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPUTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});
}

void GenOnlineSocialInterface::UnignoreUser(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::UnblockUserRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, [target_user_id](GenOnlineSocialInterface* pThis)
			{
				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				pThis->m_mapBlocked.erase(target_user_id);
			});
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Blocked"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});
}

void GenOnlineSocialInterface::AcceptPendingRequest(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::AcceptFriendRequestRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, [target_user_id](GenOnlineSocialInterface* pThis)
			{
				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				auto it = pThis->m_mapPendingRequests.find(target_user_id);
				if (it != pThis->m_mapPendingRequests.end())
				{
					// Presence arrives as FriendPresence.
					FriendsEntry entry = it->second;
					entry.online = false;
					pThis->m_mapFriends.insert_or_assign(target_user_id, entry);
					pThis->m_mapPendingRequests.erase(it);
				}
			});
		if (m_numTotalNotifications > 0) --m_numTotalNotifications;
		TriggerCallback_OnNumberGlobalNotificationsChanged();
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Friends/Requests"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});

	// update notifications
	if (m_numTotalNotifications > 0) --m_numTotalNotifications;
	TriggerCallback_OnNumberGlobalNotificationsChanged();
}

void GenOnlineSocialInterface::RejectPendingRequest(int64_t target_user_id)
{
	if (UsesRealtimeHub())
	{
		GenOnlineRealtime::DeclineFriendRequestRequest request;
		request.user_id = target_user_id;
		InvokeSocialRealtime(request, [target_user_id](GenOnlineSocialInterface* pThis)
			{
				std::scoped_lock<std::mutex> lock(pThis->m_friendsMapMutex);
				pThis->m_mapPendingRequests.erase(target_user_id);
			});
		if (m_numTotalNotifications > 0) --m_numTotalNotifications;
		TriggerCallback_OnNumberGlobalNotificationsChanged();
		return;
	}

	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("Social/Friends/Requests"), target_user_id);
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{

		});

    // update notifications
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

		// does it exist yet?
		if (!m_mapCachedMessages.contains(user_id_to_store))
		{
			m_mapCachedMessages[user_id_to_store] = std::vector<UnicodeString>();
		}

		// only if I am not the sender and overlay isnt active
		if (pAuthInterface->GetUserID() != source_user_id && !m_bOverlayActive)
		{
            if (!m_mapUnreadMessagesForUser.contains(user_id_to_store))
            {
                m_mapUnreadMessagesForUser[user_id_to_store] = 1;

                ++m_numTotalNotifications; // only increase this if we dont already have unread messages from the person
                TriggerCallback_OnNumberGlobalNotificationsChanged();
            }
            else
            {
                ++m_mapUnreadMessagesForUser[user_id_to_store];
            }
            // show popup for incoming message
            if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
            {
                if (GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Gameplay())
                {
#if defined(GENONLINE)
                    GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr, true /*bPlaySound*/);
#else
                    GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr);
#endif
                }
            }
            else
            {
                if (GenOnlineServicesManager::Settings.Social_Notifications_FriendComesOnline_Menus())
                {
#if defined(GENONLINE)
                    GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr, true /*bPlaySound*/);
#else
                    GenOnlineShowNotificationBox(AsciiString::TheEmptyString, unicodeStr);
#endif
                }
            }
        }
        m_mapCachedMessages[user_id_to_store].push_back(unicodeStr);
        if (m_cbOnChatMessage != nullptr)
        {
            m_cbOnChatMessage(source_user_id, target_user_id, unicodeStr);
        }
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

bool GenOnlineSocialInterface::UsesRealtimeHub()
{
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	return pWS != nullptr && pWS->IsRealtimeHub();
}

void GenOnlineSocialInterface::RegisterForRealtimeServiceUpdates()
{
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->SendData_SubscribeRealtimeUpdates();
	}

	m_bOverlayActive = true;
}

void GenOnlineSocialInterface::DeregisterForRealtimeServiceUpdates()
{
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->SendData_UnsubscribeRealtimeUpdates();
	}

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

template <class R>
void GenOnlineSocialInterface::InvokeSocialRealtime(const R& request, std::function<void(GenOnlineSocialInterface* pThis)> onSuccess)
{
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS == nullptr)
	{
		return;
	}
	pWS->InvokeHub(request, [onSuccess](const GenOnlineRealtime::InvokeOutcome& outcome)
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
	std::string strName;
	bool bOnlineChanged = false;
	bool bPresenceChanged = false;
	{
		std::scoped_lock<std::mutex> lock(m_friendsMapMutex);
		auto it = m_mapFriends.find(event.user_id);
		if (it == m_mapFriends.end())
		{
			return;
		}
		const std::string strPresence = GenOnlineRealtimeBridge::ActivityText(event.activity);
		bOnlineChanged = it->second.online != event.online;
		bPresenceChanged = it->second.presence != strPresence;
		it->second.online = event.online;
		it->second.presence = strPresence;
		strName = it->second.display_name;
	}
	if (bOnlineChanged)
	{
		OnOnlineStatusChanged(strName, event.online);
	}
	if (bOnlineChanged || bPresenceChanged)
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
void GenOnlineSocialInterface::OnRealtimeDirectMessage(const GenOnlineRealtime::ChatMessage& message)
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth == nullptr)
	{
		return;
	}
	int64_t peerID = -1;
	try
	{
		peerID = std::stoll(message.channel_id.substr(3));
	}
	catch (const std::exception&)
	{
		return;
	}
	const int64_t me = pAuth->GetUserID();
	const int64_t targetID = message.author.id == me ? peerID : me;
	const std::wstring text = from_utf8(message.author.name) + (message.type == "action" ? L" " : L": ") + from_utf8(message.content);
	OnChatMessage(message.author.id, targetID, UnicodeString(text.c_str()));
}
