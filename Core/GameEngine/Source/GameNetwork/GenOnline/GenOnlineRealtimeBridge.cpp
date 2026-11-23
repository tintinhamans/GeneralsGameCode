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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeNoticeText.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "GameClient/GenOnlineMenus.h"

#include <chrono>
#include <format>
#include <map>
#include <algorithm>

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
std::vector<std::string> s_features;

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

bool HasClientFeature(const char* szFeature)
{
	return szFeature != nullptr && std::find(s_features.begin(), s_features.end(), szFeature) != s_features.end();
}

bool HasPermission(const char* szPermission)
{
	return szPermission != nullptr && std::find(s_permissions.begin(), s_permissions.end(), szPermission) != s_permissions.end();
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
	const nlohmann::json params = outcome.result.params.has_value() && outcome.result.params->is_object() ? *outcome.result.params : nlohmann::json::object();
	return FailureText(outcome.Code(), params);
}

std::wstring FailureText(const RestOutcome& outcome)
{
	if (!outcome.delivered)
	{
		return L"The online service is not reachable right now. Please try again.";
	}
	if (outcome.error.code.empty())
	{
		return std::format(L"The request failed (HTTP {}).", outcome.status);
	}
	return FailureText(outcome.error.code, outcome.error.params);
}

std::wstring FailureText(const std::string& code, const nlohmann::json& params)
{
	const std::string matchmakingText = MatchmakingFailureText(code, params);
	if (!matchmakingText.empty())
	{
		return Wide(matchmakingText);
	}
	const std::string ruleText = RuleFailureText(code, params, GenOnlineLocalTimeText);
	if (!ruleText.empty())
	{
		return Wide(ruleText);
	}
	if (code == "chat.rate_limited" || code == "rate_limited")
	{
		return L"You are sending messages too quickly. Please wait a moment.";
	}
	if (code == "lobby.army_selection_disabled")
	{
		return L"This playlist plays random armies, so the army cannot be changed.";
	}
	if (code == "chat.too_long")
	{
		return std::format(L"Your message is too long. Messages can have up to {} characters.", ParamInt(params, "max", 512));
	}
	if (code == "lobby.invite_pending")
	{
		return L"This player already has an invite to your game.";
	}
	if (code == "lobby.invite_offline")
	{
		return L"This player is not in the game right now.";
	}
	if (code == "lobby.not_in_setup")
	{
		return L"Friends can only be invited while the game is in setup.";
	}
	if (code == "lobby.already_member")
	{
		return L"This player is already in your game.";
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
	s_features = ready.features.has_value() ? *ready.features : std::vector<std::string>();
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
	session.On<FeaturesUpdateEvent>([](const FeaturesUpdateEvent& event)
		{
			s_features = event.features;
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Client features changed, %d active", (int)s_features.size());
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
			else if (event.channel_id.compare(0, 3, "dm:") == 0)
			{
				GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
				if (pSocial != nullptr)
				{
					pSocial->OnRealtimeDirectMessageDelete(event);
				}
			}
		});
	session.On<MentionCreateEvent>([](const MentionCreateEvent& event)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (pRooms != nullptr)
			{
				pRooms->OnRealtimeMentionCreate(event);
			}
		});
	session.On<ConversationsEvent>([](const ConversationsEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeConversations(event);
			}
		});
	session.On<ReadMarkerEvent>([](const ReadMarkerEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeReadMarker(event);
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
				OnlineBanDetails ban;
				ban.reason = notice.reason.value_or(std::string());
				if (HasNoticeText(notice.expires_at))
				{
					ban.expiresAt = notice.expires_at;
				}
				HandleModerationBan(ban);
			}
			else if (notice.action == "kick")
			{
				// The hub closes the connection next and the client reconnects after 10 seconds.
				GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(Wide(ModerationNoticeText(notice, GenOnlineLocalTimeText)).c_str()));
			}
			else if (notice.action == "mute")
			{
				ShowRoomNotice(Wide(ModerationNoticeText(notice, GenOnlineLocalTimeText)), false);
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
	session.On<LobbyInviteEvent>([](const LobbyInviteEvent& event)
		{
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			if (pSocial != nullptr)
			{
				pSocial->OnRealtimeLobbyInvite(event);
			}
		});
}

namespace
{

struct PendingRestRetry
{
	RestMethod method = RestMethod::Get;
	std::string path;
	std::string body;
	int sends = 0;
	int64_t sendAtMs = 0;
	int64_t userID = -1;
	RestOutcome lastOutcome;
	RestCallback callback;
	bool bRetry = true;
	StandbyRetry standby;
};

// Touched on the main thread only, where the HTTP callbacks and the services tick run.
std::vector<PendingRestRetry> s_restRetries;

struct PendingStandbyRetry
{
	std::string label;
	int64_t sendAtMs = 0;
	StandbyRetry standby;
	HTTPSendFunction fnSend;
	HTTPDoneCallback fnDone;
	std::function<bool()> fnWanted;
};

// Touched on the main thread only, like s_restRetries.
std::vector<PendingStandbyRetry> s_standbyRetries;

int64_t RestNowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const char* RestMethodName(RestMethod method)
{
	switch (method)
	{
	case RestMethod::Get:
		return "GET";
	case RestMethod::Post:
		return "POST";
	case RestMethod::Put:
		return "PUT";
	case RestMethod::Delete:
		return "DELETE";
	case RestMethod::Patch:
		return "PATCH";
	}
	return "?";
}

int64_t SignedInUserID()
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	return pAuth != nullptr && pAuth->IsLoggedIn() ? pAuth->GetUserID() : -1;
}

void SendRest(RestMethod method, const std::string& path, const std::string& body, bool bRetry, int sendsBefore, const StandbyRetry& standby, RestCallback callback)
{
	GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
	const std::string origin = GenOnlineAuthInterface::ServiceOriginV1();
	if (pManager == nullptr || pManager->GetHTTPManager() == nullptr || origin.empty() || SignedInUserID() < 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s %s not sent, no session or service route", RestMethodName(method), RestPathForLog(path).c_str());
		if (callback)
		{
			callback(MakeRestOutcome(false, 0, std::string()));
		}
		return;
	}

	const int64_t userID = SignedInUserID();
	auto onDone = [method, path, body, bRetry, sendsBefore, standby, callback, userID](bool bSuccess, int statusCode, std::string strBody, HTTPRequest*)
		{
			const RestOutcome outcome = MakeRestOutcome(bSuccess, statusCode, strBody);
			// A standby answer sends the same call again without a failure and without counting the send.
			StandbyRetry nextStandby = standby;
			const int64_t standbyDelayMs = nextStandby.NextDelayMs(IsStandbyAnswer(outcome, false), RestNowMs());
			if (standbyDelayMs >= 0)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s %s answered standby, sends again in %lld ms", RestMethodName(method), RestPathForLog(path).c_str(), static_cast<long long>(standbyDelayMs));
				PendingRestRetry retry;
				retry.method = method;
				retry.path = path;
				retry.body = body;
				retry.sends = sendsBefore;
				retry.sendAtMs = RestNowMs() + standbyDelayMs;
				retry.userID = userID;
				retry.lastOutcome = outcome;
				retry.callback = callback;
				retry.bRetry = bRetry;
				retry.standby = nextStandby;
				s_restRetries.push_back(retry);
				return;
			}
			const int sends = sendsBefore + 1;
			if (!outcome.Succeeded())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s %s failed: status %d code %s", RestMethodName(method), RestPathForLog(path).c_str(), statusCode, outcome.error.code.c_str());
			}
			const int64_t delayMs = bRetry ? RestRetryDelayMs(outcome, sends) : -1;
			if (delayMs >= 0)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s %s sends again in %lld ms", RestMethodName(method), RestPathForLog(path).c_str(), static_cast<long long>(delayMs));
				PendingRestRetry retry;
				retry.method = method;
				retry.path = path;
				retry.body = body;
				retry.sends = sends;
				retry.sendAtMs = RestNowMs() + delayMs;
				retry.userID = userID;
				retry.lastOutcome = outcome;
				retry.callback = callback;
				retry.standby = nextStandby;
				s_restRetries.push_back(retry);
				return;
			}
			if (callback)
			{
				callback(outcome);
			}
		};

	std::map<std::string, std::string> mapHeaders;
	mapHeaders["Content-Type"] = "application/json";
	const std::string uri = origin + path;
	HTTPManager* pHTTP = pManager->GetHTTPManager();
	switch (method)
	{
	case RestMethod::Get:
		pHTTP->SendGETRequest(uri.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, onDone);
		break;
	case RestMethod::Post:
		pHTTP->SendPOSTRequest(uri.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, body.c_str(), onDone);
		break;
	case RestMethod::Put:
		pHTTP->SendPUTRequest(uri.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, body.c_str(), onDone);
		break;
	case RestMethod::Delete:
		pHTTP->SendDELETERequest(uri.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, body.c_str(), onDone);
		break;
	case RestMethod::Patch:
		pHTTP->SendPATCHRequest(uri.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, body.c_str(), onDone);
		break;
	}
}

void SendStandbyTry(const PendingStandbyRetry& pending)
{
	const HTTPSendFunction fnSend = pending.fnSend;
	fnSend([pending](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pRequest)
		{
			PendingStandbyRetry next = pending;
			const bool bStandby = IsStandbyAnswer(MakeRestOutcome(bSuccess, statusCode, strBody), false);
			const int64_t delayMs = next.standby.NextDelayMs(bStandby, RestNowMs());
			if (delayMs >= 0)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s answered standby, sends again in %lld ms", next.label.c_str(), static_cast<long long>(delayMs));
				next.sendAtMs = RestNowMs() + delayMs;
				s_standbyRetries.push_back(next);
				return;
			}
			if (next.fnDone)
			{
				next.fnDone(bSuccess, statusCode, strBody, pRequest);
			}
		});
}

void TickStandbyRetries()
{
	if (s_standbyRetries.empty())
	{
		return;
	}
	const int64_t now = RestNowMs();
	std::vector<PendingStandbyRetry> due;
	for (auto it = s_standbyRetries.begin(); it != s_standbyRetries.end();)
	{
		if (it->sendAtMs <= now)
		{
			due.push_back(*it);
			it = s_standbyRetries.erase(it);
		}
		else
		{
			++it;
		}
	}
	for (const PendingStandbyRetry& retry : due)
	{
		if (retry.fnWanted && !retry.fnWanted())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s dropped while it waited for a standby retry", retry.label.c_str());
			continue;
		}
		SendStandbyTry(retry);
	}
}

} // namespace

void CallRest(RestMethod method, const std::string& path, const std::string& body, RestCallback callback)
{
	SendRest(method, path, body, false, 0, StandbyRetry(), callback);
}

void CallRestWithRetry(RestMethod method, const std::string& path, const std::string& body, RestCallback callback)
{
	SendRest(method, path, body, true, 0, StandbyRetry(), callback);
}

void SendWithStandbyRetry(const std::string& label, HTTPSendFunction fnSend, HTTPDoneCallback fnDone, std::function<bool()> fnWanted)
{
	PendingStandbyRetry pending;
	pending.label = label;
	pending.fnSend = fnSend;
	pending.fnDone = fnDone;
	pending.fnWanted = fnWanted;
	SendStandbyTry(pending);
}

void ShutdownRest()
{
	s_restRetries.clear();
	s_standbyRetries.clear();
}

void TickRest()
{
	TickStandbyRetries();
	if (s_restRetries.empty())
	{
		return;
	}
	const int64_t now = RestNowMs();
	const int64_t userID = SignedInUserID();
	std::vector<PendingRestRetry> due;
	std::vector<PendingRestRetry> dropped;
	for (auto it = s_restRetries.begin(); it != s_restRetries.end();)
	{
		if (it->userID != userID)
		{
			dropped.push_back(*it);
			it = s_restRetries.erase(it);
		}
		else if (it->sendAtMs <= now)
		{
			due.push_back(*it);
			it = s_restRetries.erase(it);
		}
		else
		{
			++it;
		}
	}
	for (const PendingRestRetry& retry : dropped)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] %s %s dropped, the signed in user changed", RestMethodName(retry.method), RestPathForLog(retry.path).c_str());
		if (retry.callback)
		{
			retry.callback(retry.lastOutcome);
		}
	}
	for (const PendingRestRetry& retry : due)
	{
		SendRest(retry.method, retry.path, retry.body, retry.bRetry, retry.sends, retry.standby, retry.callback);
	}
}

} // namespace GenOnlineRealtimeBridge
