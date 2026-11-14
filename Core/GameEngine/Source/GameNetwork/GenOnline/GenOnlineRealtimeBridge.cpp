/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: GenOnlineRealtimeBridge.cpp //////////////////////////////////////////
// Description: Connects the events of the GenOnline realtime hub to the rooms,
//              social and moderation interfaces.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameClient/GenOnlineMenus.h"

#include <format>

using namespace GenOnlineRealtime;

namespace GenOnlineRealtimeBridge
{

namespace
{

std::wstring Wide(const std::string& text)
{
	return from_utf8(text);
}

int64_t ParamInt(const nlohmann::json& params, const char* key, int64_t fallback)
{
	auto it = params.find(key);
	return it != params.end() && it->is_number_integer() ? it->get<int64_t>() : fallback;
}

std::string ParamText(const nlohmann::json& params, const char* key)
{
	auto it = params.find(key);
	return it != params.end() && it->is_string() ? it->get<std::string>() : std::string();
}

BadgeMap s_badges;
std::vector<std::string> s_permissions;

int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Reads a #rrggbb colour.
bool ParseHexColor(const std::string& text, int& color)
{
	if (text.size() != 7 || text[0] != '#')
	{
		return false;
	}
	int channels[3];
	for (int i = 0; i < 3; ++i)
	{
		const int high = HexDigit(text[1 + i * 2]);
		const int low = HexDigit(text[2 + i * 2]);
		if (high < 0 || low < 0)
		{
			return false;
		}
		channels[i] = high * 16 + low;
	}
	color = GameMakeColor(channels[0], channels[1], channels[2], 255);
	return true;
}

} // namespace

bool BadgeColor(const std::vector<std::string>& badges, int& color)
{
	for (const std::string& badge : badges)
	{
		auto it = s_badges.find(badge);
		if (it != s_badges.end() && ParseHexColor(it->second.color, color))
		{
			return true;
		}
	}
	return false;
}

bool HasModerationPermission()
{
	for (const std::string& permission : s_permissions)
	{
		if (permission.compare(0, 11, "moderation.") == 0)
		{
			return true;
		}
	}
	return false;
}

std::string ActivityText(const std::string& activity)
{
	if (activity == "menu")
		return "In menus";
	if (activity == "channel")
		return "In a room";
	if (activity == "lobby")
		return "In a lobby";
	if (activity == "matchmaking")
		return "Searching for a match";
	if (activity == "in_game")
		return "In game";
	return std::string();
}

std::wstring FailureText(const InvokeOutcome& outcome)
{
	if (!outcome.delivered)
	{
		return L"The online service is not reachable right now. Please try again.";
	}
	const std::string code = outcome.Code();
	const nlohmann::json params = outcome.result.params.has_value() && outcome.result.params->is_object() ? *outcome.result.params : nlohmann::json::object();
	const std::string matchmakingText = MatchmakingFailureText(code, params);
	if (!matchmakingText.empty())
	{
		return Wide(matchmakingText);
	}
	if (code == "chat.rate_limited" || code == "rate_limited")
	{
		return L"You are sending messages too quickly. Please wait a moment.";
	}
	if (code == "chat.too_long")
	{
		return std::format(L"Your message is too long. Messages can have up to {} characters.", ParamInt(params, "max", 512));
	}
	if (code == "chat.muted")
	{
		const std::string expiresAt = ParamText(params, "expires_at");
		return expiresAt.empty() ? std::wstring(L"You are muted and cannot send messages.") : L"You are muted and cannot send messages until " + Wide(expiresAt) + L".";
	}
	if (code == "chat.not_friends")
	{
		return L"You can only send messages to your friends.";
	}
	if (code == "chat.recipient_offline")
	{
		return L"That player is offline.";
	}
	if (code == "name.length")
	{
		return std::format(L"Your new name must be between {} and {} characters.", ParamInt(params, "min", 3), ParamInt(params, "max", 16));
	}
	if (code == "name.invalid_characters")
	{
		return L"Your new name contains characters that are not allowed.";
	}
	if (code == "name.whitespace")
	{
		return L"Your new name cannot start or end with a space or contain double spaces.";
	}
	if (code == "name.reserved")
	{
		return L"Your new name contains a reserved word.";
	}
	if (code == "name.taken")
	{
		return L"That name is already taken.";
	}
	if (code == "channel.unavailable")
	{
		return L"That room is not available.";
	}
	if (code == "social.friend_list_full")
	{
		return L"Cannot send the friend request. A friends list is full.";
	}
	if (code == "social.blocked")
	{
		return L"That player cannot be added.";
	}
	if (code == "not_found")
	{
		return L"That player could not be found.";
	}
	if (code == "forbidden")
	{
		return L"You are not allowed to do that.";
	}
	return L"The request failed (" + Wide(code) + L").";
}

void ShowRoomNotice(const std::wstring& text, bool bError)
{
	GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRooms != nullptr && pRooms->m_OnChatCallback != nullptr)
	{
		pRooms->m_OnChatCallback(UnicodeString(text.c_str()), DetermineSystemNoticeColor(!bError, bError));
	}
}

void OnReady(const ReadyEvent& ready, bool bFirstReady)
{
	s_badges = ready.badges;
	s_permissions = ready.permissions;
	GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRooms != nullptr)
	{
		pRooms->SetRealtimeRooms(ready.channels);
	}
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth != nullptr)
	{
		pAuth->OnDisplayNameChanged(ready.user.name);
	}
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocial != nullptr && bFirstReady)
	{
		pSocial->ExpectRealtimeWelcome();
	}
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobby != nullptr)
	{
		pLobby->OnRealtimeReady();
	}
}

void RegisterHandlers(HubSession& session)
{
	session.On<LobbyListUpsertEvent>([](const LobbyListUpsertEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeLobbyListUpsert(event.lobby);
			}
		});
	session.On<LobbyListRemoveEvent>([](const LobbyListRemoveEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeLobbyListRemove(event.lobby_id);
			}
		});
	session.On<LobbyStateEvent>([](const LobbyStateEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeLobbyState(event.lobby);
			}
		});
	session.On<LobbyLeftEvent>([](const LobbyLeftEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeLobbyLeft(event);
			}
		});
	session.On<CountdownStartedEvent>([](const CountdownStartedEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeCountdownStarted(event);
			}
		});
	session.On<CountdownCancelledEvent>([](const CountdownCancelledEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeCountdownCancelled(event);
			}
		});
	session.On<GameStartEvent>([](const GameStartEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeGameStart(event);
			}
		});
	session.On<MatchmakingStatusEvent>([](const MatchmakingStatusEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeMatchmakingStatus(event);
			}
		});
	session.On<MatchFoundEvent>([](const MatchFoundEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->HandleMatchFound(event.lobby_id, utf8_to_local(event.map.path));
			}
		});
	session.On<MatchSetupProgressEvent>([](const MatchSetupProgressEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->HandleMatchSetupProgress(SecondsToMilliseconds(event.timeout_s), SecondsToMilliseconds(event.countdown_s));
			}
		});
	session.On<MatchStartEvent>([](const MatchStartEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeMatchStart(event);
			}
		});
	session.On<MatchRequeueEvent>([](const MatchRequeueEvent& event)
		{
			GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobby != nullptr)
			{
				pLobby->OnRealtimeMatchRequeue(event);
			}
		});
	session.On<BadgesUpdateEvent>([](const BadgesUpdateEvent& event)
		{
			s_badges = event.badges;
		});
	session.On<ChannelMembersEvent>([](const ChannelMembersEvent& event)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (pRooms != nullptr)
			{
				pRooms->OnRealtimeChannelMembers(event);
			}
		});
	session.On<MemberAddEvent>([](const MemberAddEvent& event)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (pRooms != nullptr)
			{
				pRooms->OnRealtimeMemberAdd(event);
			}
		});
	session.On<MemberRemoveEvent>([](const MemberRemoveEvent& event)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (pRooms != nullptr)
			{
				pRooms->OnRealtimeMemberRemove(event);
			}
		});
	session.On<MemberUpdateEvent>([](const MemberUpdateEvent& event)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (pRooms != nullptr)
			{
				pRooms->OnRealtimeMemberUpdate(event.member);
			}
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeMemberUpdate(event.member);
			}
			GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
			if (pAuth != nullptr && pAuth->GetUserID() == event.member.id)
			{
				pAuth->OnDisplayNameChanged(event.member.name);
			}
		});
	session.On<MessageCreateEvent>([](const MessageCreateEvent& event)
		{
			if (event.channel_id.compare(0, 8, "channel:") == 0)
			{
				GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
				if (pRooms != nullptr)
				{
					pRooms->OnRealtimeRoomMessage(event);
				}
			}
			else if (event.channel_id.compare(0, 6, "lobby:") == 0)
			{
				GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobby != nullptr)
				{
					pLobby->OnRealtimeLobbyMessageCreate(event);
				}
			}
			else if (event.channel_id.compare(0, 3, "dm:") == 0)
			{
				GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
				if (pSocial != nullptr)
				{
					pSocial->OnRealtimeDirectMessage(event);
				}
			}
		});
	session.On<ChannelHistoryEvent>([](const ChannelHistoryEvent& event)
		{
			if (event.channel_id.compare(0, 8, "channel:") == 0)
			{
				GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
				if (pRooms != nullptr)
				{
					pRooms->OnRealtimeChannelHistory(event);
				}
			}
			else if (event.channel_id.compare(0, 6, "lobby:") == 0)
			{
				GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobby != nullptr)
				{
					pLobby->OnRealtimeLobbyHistory(event);
				}
			}
		});
	session.On<MessageDeleteEvent>([](const MessageDeleteEvent& event)
		{
			if (event.channel_id.compare(0, 8, "channel:") == 0)
			{
				GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
				if (pRooms != nullptr)
				{
					pRooms->OnRealtimeMessageDelete(event);
				}
			}
			else if (event.channel_id.compare(0, 6, "lobby:") == 0)
			{
				GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobby != nullptr)
				{
					pLobby->OnRealtimeLobbyMessageDelete(event);
				}
			}
		});
	session.On<Notice>([](const Notice& notice)
		{
			if (notice.code == "name.changed")
			{
				GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
				if (pRooms != nullptr)
				{
					pRooms->OnRealtimeNameChanged(ParamText(notice.params, "old_name"), ParamText(notice.params, "new_name"));
				}
				return;
			}
			if (notice.code == "server.announcement")
			{
				// An operator announcement shows in the room chat and, inside a lobby, in the lobby chat.
				const std::wstring text = L"Announcement: " + Wide(ParamText(notice.params, "message"));
				ShowRoomNotice(text, false);
				GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobby != nullptr && pLobby->IsInLobby() && pLobby->m_OnChatCallback != nullptr)
				{
					pLobby->m_OnChatCallback(UnicodeString(text.c_str()), DetermineSystemNoticeColor(true, false));
				}
				return;
			}
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Unhandled notice %s", notice.code.c_str());
		});
	session.On<ModerationNoticeEvent>([](const ModerationNoticeEvent& notice)
		{
			if (notice.action == "ban")
			{
				HandleModerationDisconnect(EOnlineModerationAction::BAN, notice.reason.value_or(std::string()));
			}
			else if (notice.action == "kick")
			{
				// The hub closes the connection next and the client reconnects after 10 seconds.
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString((L"A moderator removed you from the online service: " + Wide(notice.reason.value_or(std::string())) + L". Reconnecting in 10 seconds.").c_str()));
			}
			else if (notice.action == "mute")
			{
				std::wstring text = L"A moderator muted you";
				if (notice.expires_at.has_value())
				{
					text += L" until " + Wide(*notice.expires_at);
				}
				text += L": " + Wide(notice.reason.value_or(std::string()));
				ShowRoomNotice(text, false);
			}
		});
	session.On<FriendsEvent>([](const FriendsEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeFriends(event);
			}
		});
	session.On<FriendRequestReceivedEvent>([](const FriendRequestReceivedEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeFriendRequestReceived(event.from);
			}
		});
	session.On<FriendRequestAcceptedEvent>([](const FriendRequestAcceptedEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeFriendRequestAccepted(event.by);
			}
		});
	session.On<FriendRemovedEvent>([](const FriendRemovedEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeFriendRemoved(event.user_id);
			}
		});
	session.On<FriendPresenceEvent>([](const FriendPresenceEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeFriendPresence(event);
			}
		});
}

} // namespace GenOnlineRealtimeBridge
