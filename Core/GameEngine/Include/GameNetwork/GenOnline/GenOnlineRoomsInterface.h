#pragma once
#include "GameNetwork/GenOnline/Realtime/RealtimeChannelChat.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include "GenOnlineInclude.h"
#include "NetworkMesh.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GenOnlineInit.h"
#include "Common/MultiplayerSettings.h"
#include "Common/GOUIColors.h"

#include <chrono>
#include <optional>

extern GenOnlineGame* TheGenOnlineGame;

// Shows an ISO 8601 UTC time of the service as local date and time, or the text itself when it does not parse.
std::string GenOnlineLocalTimeText(const std::string& utcText);

enum class EChatMessageType
{
	CHAT_MESSAGE_TYPE_NETWORK_ROOM,
	CHAT_MESSAGE_TYPE_LOBBY
};
// GenOnline @feature arcticdolphin 09/11/2026 System and chat line colours come from the GO UI colour table, these colours without it.
static Color DetermineSystemNoticeColor(bool bWarning = false, bool bError = false)
{
	if (bError)
	{
		return GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 94, 94, 255));
	}
	if (bWarning)
	{
		return GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255));
	}
	return GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255));
}

// A chat line that mentions the local player takes the Mention role of the GO UI colour table, orange without it.
static Color DetermineMentionColor()
{
	return GOUIColor(GOUICOLOR_MENTION, GameMakeColor(255, 136, 0, 255));
}

// True when the realtime message mentions the signed in user and another user wrote it.
bool GenOnlineMentionsLocalUser(const GenOnlineRealtime::ChatMessage& message);

void GenOnlinePlayMentionSound();

static Color DetermineColorForChatMessage(EChatMessageType chatMessageType, Bool isPublic, bool bAction, bool bAdmin, bool bIsNameChange, int lobbySlot = -1)
{
	Color style = GOUIColor(GOUICOLOR_TEXT, GameMakeColor(255, 255, 255, 255));

	// TODO_GENONLINE: Support owner chat again
	Bool isOwner = false;

	if (isPublic && bAction)
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_OWNER_EMOTE] : GameSpyColor[GSCOLOR_CHAT_EMOTE];
	}
    else if (isPublic && bIsNameChange)
    {
        style = GOUIColor(GOUICOLOR_DISABLED, GameMakeColor(127, 127, 127, 255));
    }
	else if (isPublic)
	{
		// use lobby colors
		if (chatMessageType == EChatMessageType::CHAT_MESSAGE_TYPE_LOBBY)
		{
			if (lobbySlot == -1)
			{
				return GOUIColor(GOUICOLOR_TEXT, GameMakeColor(255, 255, 255, 255));
			}
			else
			{
				if (TheGenOnlineGame)
				{
					GameSlot* pSlot = TheGenOnlineGame->getSlot(lobbySlot);

					if (pSlot != nullptr)
					{
						int numColors = TheMultiplayerSettings->getNumColors();
						int color = pSlot->getColor();
						if (color > -1 && color < numColors)
						{
							MultiplayerColorDefinition* def = TheMultiplayerSettings->getColor(color);
							style = def->getColor();
						}
					}
				}
			}
		}
		else
		{
			if (bAdmin)
			{
				style = GOUIColor(GOUICOLOR_ADMIN, GameMakeColor(0, 162, 232, 255));
			}
			else
			{
				style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_OWNER] : GameSpyColor[GSCOLOR_CHAT_NORMAL];
			}
		}
	}
	else if (bAction)
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_PRIVATE_OWNER_EMOTE] : GameSpyColor[GSCOLOR_CHAT_PRIVATE_EMOTE];
	}
	else
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_PRIVATE_OWNER] : GameSpyColor[GSCOLOR_CHAT_PRIVATE];
	}

	// filters language
//  if( TheGlobalData->m_languageFilterPref )
//  {
	//TheLanguageFilter->filterLine(msg);
	//	}

	return style;
}

struct GenOnlineRoomInfo
{
	int numMembers;
	int maxMembers;
};

class NetworkRoomMember : public NetworkMemberBase
{
public:
	bool IsValid() const { return user_id != -1; }
};

class GenOnlineRoomsInterface
{
public:
	GenOnlineRoomsInterface();

	void GetRoomList(std::function<void(bool)> cb);

	void JoinRoom(int roomIndex);

	void LeaveRoom()
	{
		m_CurrentRoomIndex = -1;
		m_EffectiveRoomID.reset();
		m_PendingRoomChange.reset();
		m_ChannelChat.Clear();
		m_vecRooms.clear();

		std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
		if (pWS != nullptr)
		{
			pWS->SendData_LeaveNetworkRoom();
		}
	}

	std::function<void(UnicodeString strMessage, Color color)> m_OnChatCallback = nullptr;
	void RegisterForChatCallback(std::function<void(UnicodeString strMessage, Color color)> cb)
	{
		m_OnChatCallback = cb;
	}

	void DeregisterForChatCallback()
	{
		m_OnChatCallback = nullptr;
		m_OnChatMessageCallback = nullptr;
		m_OnChatMessageRemovedCallback = nullptr;
	}

	// A channel chat message line carries a key, so a staff delete can remove that line later.
	void RegisterForChatMessageCallbacks(std::function<void(UnicodeString strMessage, Color color, uint32_t key)> cbAdd, std::function<void(uint32_t key)> cbRemove)
	{
		m_OnChatMessageCallback = std::move(cbAdd);
		m_OnChatMessageRemovedCallback = std::move(cbRemove);
	}

	// The message id and author of a shown channel chat line, for its context menu.
	bool FindChatMessage(uint32_t key, std::string& messageID, int64_t& authorID) const
	{
		return m_ChannelChat.MessageOf(key, messageID, authorID);
	}

	std::function<void()> m_RosterNeedsRefreshCallback = nullptr;
	mutable std::mutex m_rosterCallbackMutex;
	void RegisterForRosterNeedsRefreshCallback(std::function<void()> cb)
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = cb;
	}

	void DeregisterForRosterNeedsRefreshCallback()
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = nullptr;
	}

	void RegisterForRoomChangedCallback(std::function<void(int, bool)> cb)
	{
		m_RoomChangedCallback = std::move(cb);
	}

	void DeregisterForRoomChangedCallback()
	{
		m_RoomChangedCallback = nullptr;
	}

	NetworkRoomMember* GetRoomMemberFromID(int64_t puid)
	{
		if (m_mapMembers.contains(puid))
		{
			return &m_mapMembers[puid];
		}

		return nullptr;
	}

	std::unordered_map<uint64_t, NetworkRoomMember>& GetMembersListForCurrentRoom();

	// Chat
	void SendChatMessageToCurrentRoom(UnicodeString& strChatMsg, bool bIsAction);

	void ResetCachedRoomData()
	{
		m_mapMembers.clear();
	
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		if (m_RosterNeedsRefreshCallback != nullptr)
		{
			m_RosterNeedsRefreshCallback();
		}
	}

	void Tick();

	const std::vector<NetworkRoom>& GetGroupRooms() const { return m_vecRooms; }

	int GetCurrentRoomIndex() const { return m_CurrentRoomIndex; }
	std::optional<int> GetEffectiveRoomID() const { return m_EffectiveRoomID; }

	// The realtime hub sends the rooms with Ready and the members as events.
	void SetRealtimeRooms(const std::vector<GenOnlineRealtime::ChannelInfo>& rooms);
	void OnRealtimeChannelMembers(const GenOnlineRealtime::ChannelMembersEvent& event);
	void OnRealtimeMemberAdd(const GenOnlineRealtime::MemberAddEvent& event);
	void OnRealtimeMemberRemove(const GenOnlineRealtime::MemberRemoveEvent& event);
	void OnRealtimeMemberUpdate(const GenOnlineRealtime::Member& member);
	void OnRealtimeRoomMessage(const GenOnlineRealtime::ChatMessage& message);
	void OnRealtimeChannelHistory(const GenOnlineRealtime::ChannelHistoryEvent& event);
	void OnRealtimeMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event);
	// A message in a channel the session is not in mentioned the user. The notice goes to the chat the player looks at.
	void OnRealtimeMentionCreate(const GenOnlineRealtime::MentionCreateEvent& event);
	void OnRealtimeNameChanged(const std::string& strOldName, const std::string& strNewName);
	void OnRealtimeRoomJoinFailed(int roomID, const std::wstring& strReason);

private:
	void RefreshRoster();
	void ShowRoomChatLine(const GenOnlineRealtime::ChannelChatLine& line);
	void RemoveRoomChatLine(uint32_t key);
	std::function<void(UnicodeString, Color, uint32_t)> m_OnChatMessageCallback = nullptr;
	std::function<void(uint32_t)> m_OnChatMessageRemovedCallback = nullptr;
	GenOnlineRealtime::ChannelChatLog m_ChannelChat;
	std::chrono::steady_clock::time_point m_ChannelHistoryDeadline;
	std::vector<NetworkRoom> m_vecRealtimeRooms;
	// The channel list of the last Ready, which names the channel of a mention notice.
	std::vector<GenOnlineRealtime::ChannelInfo> m_vecRealtimeChannels;
	GenOnlineRealtime::MentionNoticeLog m_MentionNotices;

	struct PendingRoomChange
	{
		int roomIndex;
		int roomID;
		uint64_t requestID;
		std::chrono::steady_clock::time_point deadline;
	};

	int m_CurrentRoomIndex = -1;
	std::optional<int> m_EffectiveRoomID;
	std::optional<PendingRoomChange> m_PendingRoomChange;
	uint64_t m_NextRoomChangeRequestID = 1;
	std::function<void(int, bool)> m_RoomChangedCallback = nullptr;
	void ReportRoomJoinFailure(const std::string& error);

	std::vector<NetworkRoom> m_vecRooms;

	std::unordered_map<uint64_t, NetworkRoomMember> m_mapMembers = std::unordered_map<uint64_t, NetworkRoomMember>();
};
