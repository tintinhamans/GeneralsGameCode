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

// FILE: RealtimeContractTest.cpp /////////////////////////////////////////////
// Description: Contract test of the GenOnline realtime protocol. Every fixture
//              of the service parses into its C++ type and serializes back to
//              the same JSON. The framing tests use the wire examples of the
//              protocol document.
///////////////////////////////////////////////////////////////////////////////

#include "GameNetwork/GenOnline/Realtime/RealtimeChannelChat.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeConnectionTimeline.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeDirectMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeFriendRoster.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeHubSession.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeLobbyStart.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeNoticeText.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeProtocol.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeSignIn.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace GenOnlineRealtime;
using nlohmann::json;

static int g_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			++g_failures; \
		} \
	} while (0)

typedef std::function<json(const json&)> RoundTripFn;

template <class T>
json RoundTrip(const json& in)
{
	T object = in.get<T>();
	return json(object);
}

// Result fixtures carry typed data, so the data round trips through its type.
template <class T>
json ResultRoundTrip(const json& in)
{
	Result result = in.get<Result>();
	if (result.ok && result.data.has_value())
		result.data = json(result.data->get<T>());
	return json(result);
}

// True when reading the json as T throws.
template <class T>
bool ParseThrows(const json& in)
{
	try
	{
		in.get<T>();
	}
	catch (const std::exception&)
	{
		return true;
	}
	return false;
}

static std::map<std::string, RoundTripFn> BuildFixtureTable()
{
	std::map<std::string, RoundTripFn> t;

	t["Result.ok.json"] = RoundTrip<Result>;
	t["Result.error.json"] = RoundTrip<Result>;

	t["JoinChannel.request.json"] = RoundTrip<JoinChannelRequest>;
	t["LeaveChannel.request.json"] = RoundTrip<LeaveChannelRequest>;
	t["SendMessage.request.json"] = RoundTrip<SendMessageRequest>;
	t["SendMessage.result.json"] = ResultRoundTrip<SendMessageResult>;
	t["GetHistory.request.json"] = RoundTrip<GetHistoryRequest>;
	t["GetHistory.result.json"] = ResultRoundTrip<GetHistoryResult>;
	t["MarkRead.request.json"] = RoundTrip<MarkReadRequest>;

	t["WatchLobbyList.request.json"] = RoundTrip<WatchLobbyListRequest>;
	t["WatchLobbyList.result.json"] = ResultRoundTrip<WatchLobbyListResult>;
	t["UnwatchLobbyList.request.json"] = RoundTrip<UnwatchLobbyListRequest>;
	t["CreateLobby.request.json"] = RoundTrip<CreateLobbyRequest>;
	t["CreateLobby.result.json"] = ResultRoundTrip<CreateLobbyResult>;
	t["JoinLobby.request.json"] = RoundTrip<JoinLobbyRequest>;
	t["JoinLobby.result.json"] = ResultRoundTrip<JoinLobbyResult>;
	t["LeaveLobby.request.json"] = RoundTrip<LeaveLobbyRequest>;
	t["UpdateLobby.request.json"] = RoundTrip<UpdateLobbyRequest>;
	t["SetLobbyPassword.request.json"] = RoundTrip<SetLobbyPasswordRequest>;
	t["UpdateMySlot.request.json"] = RoundTrip<UpdateMySlotRequest>;
	t["SetHasMap.request.json"] = RoundTrip<SetHasMapRequest>;
	t["SetReady.request.json"] = RoundTrip<SetReadyRequest>;
	t["SetSlotState.request.json"] = RoundTrip<SetSlotStateRequest>;
	t["UpdateSlots.request.json"] = RoundTrip<UpdateSlotsRequest>;
	t["KickFromLobby.request.json"] = RoundTrip<KickFromLobbyRequest>;
	t["ForceReady.request.json"] = RoundTrip<ForceReadyRequest>;
	t["StartGame.request.json"] = RoundTrip<StartGameRequest>;
	t["StartGame.error.json"] = RoundTrip<Result>;

	t["JoinQueue.request.json"] = RoundTrip<JoinQueueRequest>;
	t["LeaveQueue.request.json"] = RoundTrip<LeaveQueueRequest>;
	t["WidenSearch.request.json"] = RoundTrip<WidenSearchRequest>;

	t["RequestSignalling.request.json"] = RoundTrip<RequestSignallingRequest>;
	t["SetPeerTransports.request.json"] = RoundTrip<SetPeerTransportsRequest>;
	t["Signal.request.json"] = RoundTrip<SignalRequest>;
	t["BeginMeshCheck.request.json"] = RoundTrip<BeginMeshCheckRequest>;
	t["MeshCheckResponse.request.json"] = RoundTrip<MeshCheckResponseRequest>;
	t["ProbeResponse.request.json"] = RoundTrip<ProbeResponseRequest>;
	t["ModuleScanResponse.request.json"] = RoundTrip<ModuleScanResponseRequest>;

	t["Ready.event.json"] = RoundTrip<ReadyEvent>;
	t["BadgesUpdate.event.json"] = RoundTrip<BadgesUpdateEvent>;
	t["FeaturesUpdate.event.json"] = RoundTrip<FeaturesUpdateEvent>;
	t["Notice.event.json"] = RoundTrip<Notice>;
	t["ModerationNotice.event.json"] = RoundTrip<ModerationNoticeEvent>;
	t["ServerSwitching.event.json"] = RoundTrip<ServerSwitchingEvent>;

	t["ChannelMembers.event.json"] = RoundTrip<ChannelMembersEvent>;
	t["ChannelHistory.event.json"] = RoundTrip<ChannelHistoryEvent>;
	t["MemberAdd.event.json"] = RoundTrip<MemberAddEvent>;
	t["MemberRemove.event.json"] = RoundTrip<MemberRemoveEvent>;
	t["MemberUpdate.event.json"] = RoundTrip<MemberUpdateEvent>;
	t["MessageCreate.event.json"] = RoundTrip<MessageCreateEvent>;
	t["MessageDelete.event.json"] = RoundTrip<MessageDeleteEvent>;
	t["Conversations.event.json"] = RoundTrip<ConversationsEvent>;
	t["ReadMarker.event.json"] = RoundTrip<ReadMarkerEvent>;
	t["GroupCreate.event.json"] = RoundTrip<GroupCreateEvent>;
	t["GroupUpdate.event.json"] = RoundTrip<GroupUpdateEvent>;
	t["GroupRemoved.event.json"] = RoundTrip<GroupRemovedEvent>;

	t["LobbyListUpsert.event.json"] = RoundTrip<LobbyListUpsertEvent>;
	t["LobbyListRemove.event.json"] = RoundTrip<LobbyListRemoveEvent>;
	t["LobbyState.event.json"] = RoundTrip<LobbyStateEvent>;
	t["LobbyLeft.event.json"] = RoundTrip<LobbyLeftEvent>;
	t["CountdownStarted.event.json"] = RoundTrip<CountdownStartedEvent>;
	t["CountdownCancelled.event.json"] = RoundTrip<CountdownCancelledEvent>;
	t["GameStart.event.json"] = RoundTrip<GameStartEvent>;

	t["MatchmakingStatus.event.json"] = RoundTrip<MatchmakingStatusEvent>;
	t["MatchFound.event.json"] = RoundTrip<MatchFoundEvent>;
	t["MatchSetupProgress.event.json"] = RoundTrip<MatchSetupProgressEvent>;
	t["MatchStart.event.json"] = RoundTrip<MatchStartEvent>;
	t["MatchRequeue.event.json"] = RoundTrip<MatchRequeueEvent>;

	t["Friends.event.json"] = RoundTrip<FriendsEvent>;
	t["FriendRequestReceived.event.json"] = RoundTrip<FriendRequestReceivedEvent>;
	t["FriendRequestAccepted.event.json"] = RoundTrip<FriendRequestAcceptedEvent>;
	t["FriendRemoved.event.json"] = RoundTrip<FriendRemovedEvent>;
	t["FriendPresence.event.json"] = RoundTrip<FriendPresenceEvent>;

	t["StartSignalling.event.json"] = RoundTrip<StartSignallingEvent>;
	t["DisconnectPeer.event.json"] = RoundTrip<DisconnectPeerEvent>;
	t["Signal.event.json"] = RoundTrip<SignalEvent>;
	t["MeshCheckRequest.event.json"] = RoundTrip<MeshCheckRequestEvent>;
	t["MeshCheckResult.event.json"] = RoundTrip<MeshCheckResultEvent>;
	t["AnticheatRegister.event.json"] = RoundTrip<AnticheatRegisterEvent>;
	t["AnticheatDeregister.event.json"] = RoundTrip<AnticheatDeregisterEvent>;
	t["ProbeRequest.event.json"] = RoundTrip<ProbeRequestEvent>;
	t["ModuleScanRequest.event.json"] = RoundTrip<ModuleScanRequestEvent>;

	return t;
}

// Every JSON file of the folder needs an entry in the table, and every entry needs its file.
static void CheckFixtureFolder(const std::filesystem::path& folder, const std::map<std::string, RoundTripFn>& table, const char* label)
{
	std::set<std::string> seen;

	for (const auto& entry : std::filesystem::directory_iterator(folder))
	{
		if (entry.path().extension() != ".json")
			continue;
		std::string name = entry.path().filename().string();
		seen.insert(name);

		auto it = table.find(name);
		if (it == table.end())
		{
			std::printf("FAILED fixture %s has no C++ type\n", name.c_str());
			++g_failures;
			continue;
		}

		std::ifstream file(entry.path(), std::ios::binary);
		std::stringstream text;
		text << file.rdbuf();
		json in = json::parse(text.str(), nullptr, false);
		if (in.is_discarded())
		{
			std::printf("FAILED fixture %s is not valid JSON\n", name.c_str());
			++g_failures;
			continue;
		}

		try
		{
			json out = it->second(in);
			if (out != in)
			{
				std::printf("FAILED fixture %s round trip\n  in:  %s\n  out: %s\n", name.c_str(), in.dump().c_str(), out.dump().c_str());
				++g_failures;
			}
		}
		catch (const std::exception& e)
		{
			std::printf("FAILED fixture %s: %s\n", name.c_str(), e.what());
			++g_failures;
		}
	}

	for (const auto& entry : table)
	{
		if (seen.find(entry.first) == seen.end())
		{
			std::printf("FAILED fixture %s is mapped but missing\n", entry.first.c_str());
			++g_failures;
		}
	}
	std::printf("%s checked: %zu\n", label, seen.size());
}

static void TestFixtures(const std::filesystem::path& folder)
{
	CheckFixtureFolder(folder, BuildFixtureTable(), "fixtures");
}

static std::map<std::string, RoundTripFn> BuildApiFixtureTable()
{
	std::map<std::string, RoundTripFn> t;

	t["error.response.json"] = RoundTrip<ErrorBody>;
	t["post-lobbies-connection_reports.request.json"] = RoundTrip<ConnectionReportBody>;

	t["post-me-friend_requests.request.json"] = RoundTrip<FriendRequestBody>;
	t["patch-me.request.json"] = RoundTrip<ProfileUpdateBody>;

	t["put-matches-results-me.request.json"] = RoundTrip<MatchResultBody>;
	t["put-matches-results-me.response.json"] = RoundTrip<MatchResultResponse>;
	t["get-matchmaking-playlists.response.json"] = RoundTrip<PlaylistsResponse>;

	t["post-groups.request.json"] = RoundTrip<GroupCreateBody>;
	t["post-groups.response.json"] = RoundTrip<GroupCreateResponse>;
	t["post-groups-members.request.json"] = RoundTrip<GroupMembersBody>;
	t["patch-groups.request.json"] = RoundTrip<GroupRenameBody>;

	t["post-players-sanctions.request.json"] = RoundTrip<SanctionBody>;
	t["post-players-sanctions.response.json"] = RoundTrip<SanctionCreateResponse>;
	t["get-players-sanctions.response.json"] = RoundTrip<SanctionsResponse>;
	t["get-me-friends.response.json"] = RoundTrip<MyFriendsResponse>;
	t["get-me-blocks.response.json"] = RoundTrip<MyBlocksResponse>;
	t["get-reports.response.json"] = RoundTrip<StaffReportsResponse>;
	t["patch-reports.request.json"] = RoundTrip<ReportCloseBody>;
	t["delete-sanctions.request.json"] = RoundTrip<ReasonBody>;
	t["delete-messages.request.json"] = RoundTrip<ReasonBody>;
	t["post-reports.request.json"] = RoundTrip<ReportBody>;
	t["post-reports.response.json"] = RoundTrip<ReportCreateResponse>;

	t["get-compatibility.response.json"] = RoundTrip<CompatibilityResponse>;
	t["get-config.response.json"] = RoundTrip<ConfigResponse>;
	t["put-me-linked_accounts.request.json"] = RoundTrip<LinkAccountBody>;
	t["post-me-linked_accounts-sign_in_token.response.json"] = RoundTrip<SignInTokenResponse>;
	t["get-stats-global.response.json"] = RoundTrip<GlobalStatsResponse>;
	t["get-players-stats.response.json"] = RoundTrip<PlayerStatsBody>;
	t["get-players-stats-ids.response.json"] = RoundTrip<PlayerStatsBatchResponse>;

	return t;
}

static void TestApiFixtures(const std::filesystem::path& folder)
{
	CheckFixtureFolder(folder, BuildApiFixtureTable(), "api fixtures");
}

static void TestHandshake()
{
	std::string request = BuildHandshakeRequest();
	CHECK(request == std::string("{\"protocol\":\"json\",\"version\":1}\x1E"));

	std::string error;
	CHECK(ParseHandshakeResponse("{}", error));
	CHECK(error.empty());
	CHECK(!ParseHandshakeResponse("{\"error\":\"Requested protocol 'json' is not available.\"}", error));
	CHECK(error == "Requested protocol 'json' is not available.");
	CHECK(!ParseHandshakeResponse("not json", error));
}

static void TestParseRecord()
{
	Record record;
	std::string error;

	CHECK(ParseRecord("{\"type\":3,\"invocationId\":\"5\",\"result\":{\"ok\":true,\"data\":{\"message_id\":\"8812\"}}}", record, error));
	CHECK(record.type == 3);
	CHECK(record.invocationId == "5");
	CHECK(record.hasResult);
	CHECK(record.result["data"]["message_id"] == "8812");

	CHECK(ParseRecord("{\"type\":1,\"target\":\"MessageCreate\",\"arguments\":[{\"id\":\"8812\",\"channel_id\":\"channel:3\",\"type\":\"chat\",\"author\":{\"id\":42,\"name\":\"Bob\",\"badges\":[]},\"content\":\"hi\",\"created_at\":\"2026-11-14T20:00:00Z\"}]}", record, error));
	CHECK(record.type == 1);
	CHECK(record.invocationId.empty());
	CHECK(record.target == "MessageCreate");
	MessageCreateEvent message = record.argument.get<MessageCreateEvent>();
	CHECK(message.author.id == 42);
	CHECK(!message.author.client.has_value());
	CHECK(message.content == "hi");

	CHECK(ParseRecord("{\"type\":6}", record, error));
	CHECK(record.type == 6);

	CHECK(ParseRecord("{\"type\":7,\"error\":\"session.superseded\",\"allowReconnect\":false}", record, error));
	CHECK(record.type == 7);
	CHECK(record.error == "session.superseded");
	CHECK(!record.allowReconnect);

	CHECK(ParseRecord("{\"type\":7,\"error\":\"server.restarting\",\"allowReconnect\":true}", record, error));
	CHECK(record.allowReconnect);

	CHECK(!ParseRecord("{\"type\":1,\"target\":\"Ready\",\"arguments\":[]}", record, error));
	CHECK(!ParseRecord("{\"type\":1,\"target\":\"Ready\",\"arguments\":[1,2]}", record, error));
	CHECK(!ParseRecord("{\"type\":3,\"result\":{}}", record, error));
	CHECK(!ParseRecord("{\"target\":\"Ready\"}", record, error));
	CHECK(!ParseRecord("[1]", record, error));
}

static void TestSerializeInvocation()
{
	SendMessageRequest request;
	request.channel_id = "channel:3";
	request.type = "chat";
	request.content = "hi";
	std::string withResult = SerializeInvocation(SendMessageRequest::METHOD, json(request), "5");
	CHECK(withResult.back() == RECORD_SEPARATOR);
	json parsed = json::parse(withResult.substr(0, withResult.size() - 1));
	CHECK(parsed == json::parse("{\"type\":1,\"invocationId\":\"5\",\"target\":\"SendMessage\",\"arguments\":[{\"channel_id\":\"channel:3\",\"type\":\"chat\",\"content\":\"hi\"}]}"));

	SignalRequest signal;
	signal.to = 7;
	signal.payload = "AAECAwQ=";
	std::string fire = SerializeInvocation(SignalRequest::METHOD, json(signal), std::string());
	parsed = json::parse(fire.substr(0, fire.size() - 1));
	CHECK(!parsed.contains("invocationId"));
	CHECK(parsed["target"] == "Signal");
	CHECK(!SignalRequest::EXPECTS_RESULT);
	CHECK(SendMessageRequest::EXPECTS_RESULT);

	CHECK(SerializePing() == std::string("{\"type\":6}\x1E"));
}

static void TestRecordSplitter()
{
	RecordSplitter splitter;
	std::string record;

	std::string two = std::string("{\"type\":6}\x1E{\"type\":3,") ;
	CHECK(splitter.Append(two.data(), two.size()));
	CHECK(splitter.Next(record));
	CHECK(record == "{\"type\":6}");
	CHECK(!splitter.Next(record));

	std::string rest = std::string("\"invocationId\":\"1\",\"result\":{}}\x1E");
	CHECK(splitter.Append(rest.data(), rest.size()));
	CHECK(splitter.Next(record));
	CHECK(record == "{\"type\":3,\"invocationId\":\"1\",\"result\":{}}");
	CHECK(!splitter.Next(record));

	std::string big(MAX_RECORD_BYTES, 'x');
	CHECK(splitter.Append(big.data(), big.size()));
	CHECK(!splitter.Append("x", 1));
	splitter.Reset();
	CHECK(!splitter.Next(record));
}

static void TestInvocationTracker()
{
	InvocationTracker tracker;
	int calls = 0;
	bool lastSucceeded = false;
	std::string lastError;
	json lastResult;
	auto fn = [&](bool succeeded, const json& result, const std::string& error)
	{
		++calls;
		lastSucceeded = succeeded;
		lastResult = result;
		lastError = error;
	};

	std::string first = tracker.Add(fn);
	std::string second = tracker.Add(fn);
	CHECK(first != second);
	CHECK(tracker.PendingCount() == 2);

	Record completion;
	std::string error;
	CHECK(ParseRecord("{\"type\":3,\"invocationId\":\"" + first + "\",\"result\":{\"ok\":false,\"code\":\"chat.muted\",\"params\":{}}}", completion, error));
	CHECK(tracker.Complete(completion));
	CHECK(calls == 1);
	CHECK(lastSucceeded);
	Result result = lastResult.get<Result>();
	CHECK(!result.ok);
	CHECK(result.code.has_value() && *result.code == "chat.muted");
	CHECK(!tracker.Complete(completion));

	CHECK(ParseRecord("{\"type\":3,\"invocationId\":\"" + second + "\",\"error\":\"Failed to invoke 'X'\"}", completion, error));
	CHECK(tracker.Complete(completion));
	CHECK(calls == 2);
	CHECK(!lastSucceeded);
	CHECK(lastError == "Failed to invoke 'X'");

	tracker.Add(fn);
	tracker.FailAll("connection lost");
	CHECK(calls == 3);
	CHECK(!lastSucceeded);
	CHECK(lastError == "connection lost");
	CHECK(tracker.PendingCount() == 0);
}

// Wire durations in whole seconds feed the millisecond consumers of the client.
static void TestSecondsToMilliseconds()
{
	CHECK(SecondsToMilliseconds(0) == 0);
	CHECK(SecondsToMilliseconds(5) == 5000);
	CHECK(SecondsToMilliseconds(-2) == -2000);
	CHECK(SecondsToMilliseconds(INT64_MAX) == INT64_MAX / 1000 * 1000);
	CHECK(SecondsToMilliseconds(INT64_MIN) == -(INT64_MAX / 1000 * 1000));

	CountdownStartedEvent countdown = json::parse("{\"lobby_id\":991,\"duration_s\":5}").get<CountdownStartedEvent>();
	CHECK(SecondsToMilliseconds(countdown.duration_s) == 5000);
}

static void TestFieldPresence()
{
	// Absent, null and a value stay distinct for patch fields.
	UpdateMySlotRequest patch = json::parse("{\"side\":2}").get<UpdateMySlotRequest>();
	CHECK(!patch.team.has_value());
	CHECK(json(patch) == json::parse("{\"side\":2}"));

	patch = json::parse("{\"team\":null}").get<UpdateMySlotRequest>();
	CHECK(patch.team.has_value() && patch.team->isNull);
	CHECK(json(patch) == json::parse("{\"team\":null}"));

	patch = json::parse("{\"team\":3}").get<UpdateMySlotRequest>();
	CHECK(patch.team.has_value() && !patch.team->isNull && patch.team->value == 3);

	// Nullable fields are always sent.
	SetLobbyPasswordRequest removePassword;
	CHECK(json(removePassword) == json::parse("{\"password\":null}"));

	// Unknown fields are ignored and missing required fields fail.
	MatchStartEvent start = json::parse("{\"lobby_id\":5,\"new_field\":true}").get<MatchStartEvent>();
	CHECK(start.lobby_id == 5);
	CHECK(ParseThrows<MatchStartEvent>(json::parse("{\"new_field\":true}")));

	// A null optional field reads as left out.
	ModerationNoticeEvent notice = json::parse("{\"action\":\"kick\",\"reason\":\"r\",\"expires_at\":null}").get<ModerationNoticeEvent>();
	CHECK(!notice.expires_at.has_value());
}

// A transport stand-in that records what the session sends and how the connection ended.
struct FakeHub
{
	std::vector<std::string> sent;
	int closes = 0;
	int readies = 0;
	int disconnects = 0;
	DisconnectAction lastAction = DisconnectAction::Reconnect;
	std::string lastReason;
	HubSession session;

	FakeHub() : session(MakeCallbacks()) {}

	HubSession::Callbacks MakeCallbacks()
	{
		HubSession::Callbacks c;
		c.sendText = [this](const std::string& text)
		{
			sent.push_back(text);
			return true;
		};
		c.closeTransport = [this]()
		{
			++closes;
		};
		c.onReady = [this](const ReadyEvent&)
		{
			++readies;
		};
		c.onDisconnected = [this](DisconnectAction action, const std::string& reason)
		{
			++disconnects;
			lastAction = action;
			lastReason = reason;
		};
		return c;
	}

	void Receive(const std::string& records, int64_t nowMs)
	{
		session.OnTransportText(records.data(), records.size(), nowMs);
	}

	// Sends the handshake answer and a valid Ready in one frame.
	void Connect(int64_t nowMs)
	{
		session.OnTransportOpen(nowMs);
		Receive(std::string("{}\x1E") + ReadyRecord(1), nowMs);
	}

	json LastSent() const
	{
		const std::string& last = sent.back();
		return json::parse(last.substr(0, last.size() - 1));
	}

	static std::string ReadyRecord(int protocolVersion)
	{
		json ready = json::parse("{\"protocol_version\":1,\"session_type\":\"game\",\"user\":{\"id\":42,\"name\":\"Bob\",\"badges\":[]},\"permissions\":[],\"badges\":{},\"channels\":[],\"limits\":{\"chat_max_length\":512,\"chat_rate\":{\"messages\":3,\"window_s\":9},\"name_min_length\":3,\"name_max_length\":16,\"group_max_members\":10}}");
		ready["protocol_version"] = protocolVersion;
		json record = { { "type", 1 }, { "target", "Ready" }, { "arguments", json::array({ ready }) } };
		return record.dump() + RECORD_SEPARATOR;
	}
};

static void TestHubSessionConnect()
{
	FakeHub hub;
	hub.session.OnTransportOpen(0);
	CHECK(hub.sent.size() == 1);
	CHECK(hub.sent[0] == BuildHandshakeRequest());
	CHECK(hub.session.GetState() == HubSession::State::Handshaking);

	hub.Receive(std::string("{}\x1E") + FakeHub::ReadyRecord(1), 10);
	CHECK(hub.session.GetState() == HubSession::State::Ready);
	CHECK(hub.readies == 1);
	CHECK(hub.disconnects == 0);

	FakeHub refused;
	refused.session.OnTransportOpen(0);
	refused.Receive("{\"error\":\"Requested protocol 'json' is not available.\"}\x1E", 1);
	CHECK(refused.disconnects == 1);
	CHECK(refused.lastAction == DisconnectAction::ReturnToLogin);
	CHECK(refused.closes == 1);

	FakeHub notReadyFirst;
	notReadyFirst.session.OnTransportOpen(0);
	notReadyFirst.Receive("{}\x1E{\"type\":1,\"target\":\"Friends\",\"arguments\":[{}]}\x1E", 1);
	CHECK(notReadyFirst.readies == 0);
	CHECK(notReadyFirst.lastAction == DisconnectAction::ReturnToLogin);

	FakeHub wrongVersion;
	wrongVersion.session.OnTransportOpen(0);
	wrongVersion.Receive(std::string("{}\x1E") + FakeHub::ReadyRecord(2), 1);
	CHECK(wrongVersion.readies == 0);
	CHECK(wrongVersion.lastAction == DisconnectAction::ReturnToLogin);

	FakeHub oversized;
	oversized.session.OnTransportOpen(0);
	std::string big(MAX_RECORD_BYTES + 1, ' ');
	oversized.Receive(big, 1);
	CHECK(oversized.lastAction == DisconnectAction::ReturnToLogin);
}

static void TestHubSessionInvoke()
{
	FakeHub hub;
	SendMessageRequest request;
	request.channel_id = "channel:3";
	request.type = "chat";
	request.content = "hi";

	int calls = 0;
	InvokeOutcome last;
	auto callback = [&](const InvokeOutcome& outcome)
	{
		++calls;
		last = outcome;
	};

	hub.session.Invoke(request, callback, 0);
	CHECK(calls == 1);
	CHECK(!last.delivered);
	CHECK(last.transportError == "not connected");

	hub.Connect(0);
	size_t before = hub.sent.size();
	hub.session.Invoke(request, callback, 100);
	CHECK(hub.sent.size() == before + 1);
	json sent = hub.LastSent();
	CHECK(sent["target"] == "SendMessage");
	std::string id = sent["invocationId"].get<std::string>();
	CHECK(!id.empty());

	hub.Receive("{\"type\":3,\"invocationId\":\"" + id + "\",\"result\":{\"ok\":true,\"data\":{\"message_id\":\"8812\"}}}\x1E", 200);
	CHECK(calls == 2);
	CHECK(last.Succeeded());
	SendMessageResult data;
	CHECK(last.Data(data));
	CHECK(data.message_id == "8812");

	hub.session.Invoke(request, callback, 300);
	id = hub.LastSent()["invocationId"].get<std::string>();
	hub.Receive("{\"type\":3,\"invocationId\":\"" + id + "\",\"result\":{\"ok\":false,\"code\":\"chat.rate_limited\",\"params\":{\"retry_after_ms\":4200}}}\x1E", 400);
	CHECK(calls == 3);
	CHECK(last.delivered);
	CHECK(!last.Succeeded());
	CHECK(last.Code() == "chat.rate_limited");
	CHECK(!last.Data(data));

	SignalRequest signal;
	signal.to = 7;
	signal.payload = "AAECAwQ=";
	CHECK(hub.session.Fire(signal, 500));
	CHECK(!hub.LastSent().contains("invocationId"));
	CHECK(hub.LastSent()["target"] == "Signal");

	// A dropped connection fails the pending invocation and reconnects.
	hub.session.Invoke(request, callback, 600);
	CHECK(hub.session.PendingInvocations() == 1);
	hub.session.OnTransportLost("recv failed");
	CHECK(calls == 4);
	CHECK(last.transportError == "connection lost");
	CHECK(hub.disconnects == 1);
	CHECK(hub.lastAction == DisconnectAction::Reconnect);
	CHECK(hub.closes == 0);
	CHECK(!hub.session.Fire(signal, 700));
}

static void TestHubSessionEvents()
{
	FakeHub hub;
	int friends = 0;
	int64_t lastFriendId = 0;
	hub.session.On<FriendsEvent>([&](const FriendsEvent& event)
	{
		++friends;
		lastFriendId = event.friends.empty() ? 0 : event.friends[0].id;
	});
	hub.Connect(0);

	hub.Receive("{\"type\":1,\"target\":\"Friends\",\"arguments\":[{\"friends\":[{\"id\":7,\"name\":\"Ann\",\"badges\":[],\"online\":true,\"activity\":\"menu\"}],\"incoming\":[],\"outgoing\":[],\"blocked\":[]}]}\x1E", 1);
	CHECK(friends == 1);
	CHECK(lastFriendId == 7);

	// Unknown and malformed events are dropped and the connection stays up.
	hub.Receive("{\"type\":1,\"target\":\"SomethingNew\",\"arguments\":[{}]}\x1E", 2);
	hub.Receive("{\"type\":1,\"target\":\"Friends\",\"arguments\":[{\"friends\":3}]}\x1E", 3);
	CHECK(friends == 1);
	CHECK(hub.session.GetState() == HubSession::State::Ready);
	CHECK(hub.disconnects == 0);
}

static void TestHubSessionKeepAlive()
{
	FakeHub hub;
	hub.Connect(0);
	size_t before = hub.sent.size();
	hub.session.Tick(PING_INTERVAL_MS - 1);
	CHECK(hub.sent.size() == before);
	hub.session.Tick(PING_INTERVAL_MS);
	CHECK(hub.sent.size() == before + 1);
	CHECK(hub.sent.back() == SerializePing());

	hub.Receive("{\"type\":6}\x1E", 20000);
	hub.session.Tick(20000 + SERVER_TIMEOUT_MS - 1);
	CHECK(hub.disconnects == 0);
	hub.session.Tick(20000 + SERVER_TIMEOUT_MS);
	CHECK(hub.disconnects == 1);
	CHECK(hub.lastAction == DisconnectAction::Reconnect);
	CHECK(hub.closes == 1);
}

static void TestHubSessionClose()
{
	struct Case
	{
		const char* record;
		DisconnectAction action;
	};
	const Case cases[] = {
		{ "{\"type\":7,\"error\":\"session.superseded\",\"allowReconnect\":false}", DisconnectAction::StopSignedInElsewhere },
		{ "{\"type\":7,\"error\":\"auth.revoked\",\"allowReconnect\":false}", DisconnectAction::RefreshAndReconnect },
		{ "{\"type\":7,\"error\":\"moderation.kicked\",\"allowReconnect\":true}", DisconnectAction::ReconnectAfterKick },
		{ "{\"type\":7,\"error\":\"moderation.banned\",\"allowReconnect\":false}", DisconnectAction::ReturnToLogin },
		{ "{\"type\":7,\"error\":\"server.restarting\",\"allowReconnect\":true}", DisconnectAction::Reconnect },
		{ "{\"type\":7,\"error\":\"server.switching\",\"allowReconnect\":true}", DisconnectAction::ReconnectAfterSwitch },
		{ "{\"type\":7,\"error\":\"protocol.error\",\"allowReconnect\":false}", DisconnectAction::ReturnToLogin },
		{ "{\"type\":7,\"error\":\"something.new\",\"allowReconnect\":true}", DisconnectAction::Reconnect },
		{ "{\"type\":7,\"error\":\"something.new\",\"allowReconnect\":false}", DisconnectAction::ReturnToLogin },
		{ "{\"type\":7}", DisconnectAction::ReturnToLogin },
	};
	for (const Case& c : cases)
	{
		FakeHub hub;
		hub.Connect(0);
		hub.Receive(std::string(c.record) + RECORD_SEPARATOR, 1);
		CHECK(hub.disconnects == 1);
		CHECK(hub.lastAction == c.action);
		CHECK(hub.session.GetState() == HubSession::State::Closed);
		// A lost transport after the close record does not report a second disconnect.
		hub.session.OnTransportLost("closed");
		CHECK(hub.disconnects == 1);
	}
}

static void TestReconnectPolicy()
{
	double random = 0.0;
	ReconnectPolicy policy([&]() { return random; });
	CHECK(policy.NextDelayMs() == 500);
	CHECK(policy.NextDelayMs() == 2000);
	CHECK(policy.NextDelayMs() == 4000);
	CHECK(policy.NextDelayMs() == 8000);
	CHECK(policy.NextDelayMs() == 16000);
	CHECK(policy.NextDelayMs() == 30000);
	CHECK(policy.NextDelayMs() == 30000);
	for (int i = 0; i < 100; ++i)
		policy.NextDelayMs();
	CHECK(policy.NextDelayMs() == 30000);

	policy.Reset();
	random = 0.999;
	int64_t first = policy.NextDelayMs();
	CHECK(first >= 2990 && first < 3000);
}

static void TestServerSwitching()
{
	// The session remembers the retry_after_ms of the last ServerSwitching for the close that follows.
	FakeHub hub;
	hub.Connect(0);
	CHECK(!hub.session.ServerSwitchingRetryAfterMs().has_value());
	hub.Receive("{\"type\":1,\"target\":\"ServerSwitching\",\"arguments\":[{\"retry_after_ms\":400}]}\x1E", 1);
	hub.Receive("{\"type\":1,\"target\":\"ServerSwitching\",\"arguments\":[{\"retry_after_ms\":750}]}\x1E", 2);
	CHECK(hub.session.GetState() == HubSession::State::Ready);
	hub.Receive("{\"type\":7,\"error\":\"server.switching\",\"allowReconnect\":true}\x1E", 3);
	CHECK(hub.disconnects == 1);
	CHECK(hub.lastAction == DisconnectAction::ReconnectAfterSwitch);
	CHECK(hub.session.ServerSwitchingRetryAfterMs().has_value() && *hub.session.ServerSwitchingRetryAfterMs() == 750);
	// A new connection forgets the value.
	hub.session.OnTransportOpen(10);
	CHECK(!hub.session.ServerSwitchingRetryAfterMs().has_value());

	// A server.switching close without a ServerSwitching before it still reconnects without backoff.
	FakeHub bare;
	bare.Connect(0);
	bare.Receive("{\"type\":7,\"error\":\"server.switching\",\"allowReconnect\":true}\x1E", 1);
	CHECK(bare.lastAction == DisconnectAction::ReconnectAfterSwitch);
	CHECK(!bare.session.ServerSwitchingRetryAfterMs().has_value());

	// The delay is the retry_after_ms, kept within 0 and the backoff cap.
	CHECK(SwitchReconnectDelayMs(std::optional<int64_t>(750), 0.9) == 750);
	CHECK(SwitchReconnectDelayMs(std::optional<int64_t>(0), 0.9) == 0);
	CHECK(SwitchReconnectDelayMs(std::optional<int64_t>(-5), 0.9) == 0);
	CHECK(SwitchReconnectDelayMs(std::optional<int64_t>(600000), 0.9) == ReconnectPolicy::STEP_CAP_MS);

	// For a while after a server switch the new copy may still be starting, so retries stay short.
	CHECK(SwitchWindowDelayMs(16000, 1000, 1000 + 10000) == SWITCH_FAST_RETRY_MAX_MS);
	CHECK(SwitchWindowDelayMs(1000, 1000, 1000 + 10000) == 1000);
	CHECK(SwitchWindowDelayMs(16000, 1000, 1000 + SWITCH_FAST_RETRY_WINDOW_MS) == 16000);
	CHECK(SwitchWindowDelayMs(16000, -1, 5000) == 16000);
	// Without a ServerSwitching the delay is a random 0 to 1 second.
	CHECK(SwitchReconnectDelayMs(std::nullopt, 0.0) == 0);
	CHECK(SwitchReconnectDelayMs(std::nullopt, 0.5) == 500);
	CHECK(SwitchReconnectDelayMs(std::nullopt, 0.999) == 999);

	// The reconnect stays hidden for 3 seconds after the close, then it is a normal reconnect.
	CHECK(SwitchReconnectHidden(1000, 1000));
	CHECK(SwitchReconnectHidden(1000, 3999));
	CHECK(!SwitchReconnectHidden(1000, 4000));
	CHECK(!SwitchReconnectHidden(1000, 60000));
}

static void TestConnectRefusals()
{
	// The service refuses a connection after the handshake with at most one event and a close record, and never sends Ready.
	struct Case
	{
		const char* records;
		const char* code;
		DisconnectAction action;
	};
	const Case cases[] = {
		{ "{\"type\":7,\"error\":\"unauthorized\",\"allowReconnect\":false}\x1E", "unauthorized", DisconnectAction::RefreshTokenAndReconnect },
		{ "{\"type\":7,\"error\":\"session.not_registered\",\"allowReconnect\":false}\x1E", "session.not_registered", DisconnectAction::RegisterAndReconnect },
		{ "{\"type\":1,\"target\":\"ModerationNotice\",\"arguments\":[{\"action\":\"ban\",\"reason\":\"cheating\",\"expires_at\":\"2026-12-01T00:00:00Z\"}]}\x1E"
			"{\"type\":7,\"error\":\"moderation.banned\",\"allowReconnect\":false}\x1E", "moderation.banned", DisconnectAction::ReturnToLogin },
		{ "{\"type\":1,\"target\":\"ServerSwitching\",\"arguments\":[{\"retry_after_ms\":1500}]}\x1E"
			"{\"type\":7,\"error\":\"service.unavailable\",\"allowReconnect\":true}\x1E", "service.unavailable", DisconnectAction::ReconnectAfterStandby },
		{ "{\"type\":7,\"error\":\"service.unavailable\",\"allowReconnect\":false}\x1E", "service.unavailable", DisconnectAction::ReturnToLogin },
		{ "{\"type\":7,\"error\":\"auth.revoked\",\"allowReconnect\":false}\x1E", "auth.revoked", DisconnectAction::RefreshAndReconnect },
	};
	for (const Case& c : cases)
	{
		FakeHub hub;
		int bans = 0;
		hub.session.On<ModerationNoticeEvent>([&](const ModerationNoticeEvent& notice)
		{
			if (notice.action == "ban")
			{
				++bans;
			}
		});
		hub.session.OnTransportOpen(0);
		hub.Receive(std::string("{}\x1E") + c.records, 1);
		CHECK(hub.readies == 0);
		CHECK(hub.disconnects == 1);
		CHECK(hub.lastAction == c.action);
		CHECK(hub.lastReason == c.code);
		CHECK(hub.closes == 1);
		CHECK(bans == (std::string(c.code) == "moderation.banned" ? 1 : 0));
	}

	// The ban notice before Ready carries the reason and expiry, both absent for a permanent ban without a reason.
	FakeHub banned;
	ModerationNoticeEvent ban;
	banned.session.On<ModerationNoticeEvent>([&](const ModerationNoticeEvent& notice)
	{
		ban = notice;
	});
	banned.session.OnTransportOpen(0);
	banned.Receive("{}\x1E{\"type\":1,\"target\":\"ModerationNotice\",\"arguments\":[{\"action\":\"ban\",\"reason\":\"cheating\",\"expires_at\":\"2026-12-01T00:00:00Z\"}]}\x1E", 1);
	CHECK(ban.action == "ban");
	CHECK(ban.reason.has_value() && *ban.reason == "cheating");
	CHECK(ban.expires_at.has_value() && *ban.expires_at == "2026-12-01T00:00:00Z");
	CHECK(banned.session.GetState() == HubSession::State::WaitingForReady);
	FakeHub permanent;
	ModerationNoticeEvent permanentBan;
	permanentBan.reason = std::string("stale");
	permanent.session.On<ModerationNoticeEvent>([&](const ModerationNoticeEvent& notice)
	{
		permanentBan = notice;
	});
	permanent.session.OnTransportOpen(0);
	permanent.Receive("{}\x1E{\"type\":1,\"target\":\"ModerationNotice\",\"arguments\":[{\"action\":\"ban\"}]}\x1E", 1);
	CHECK(permanentBan.action == "ban");
	CHECK(!permanentBan.reason.has_value());
	CHECK(!permanentBan.expires_at.has_value());

	// The standby refusal keeps the retry_after_ms of its ServerSwitching for the reconnect.
	FakeHub standby;
	standby.session.OnTransportOpen(0);
	standby.Receive("{}\x1E{\"type\":1,\"target\":\"ServerSwitching\",\"arguments\":[{\"retry_after_ms\":1500}]}\x1E{\"type\":7,\"error\":\"service.unavailable\",\"allowReconnect\":true}\x1E", 1);
	CHECK(standby.session.ServerSwitchingRetryAfterMs().has_value() && *standby.session.ServerSwitchingRetryAfterMs() == 1500);

	// Other events before Ready still end the connection.
	FakeHub chatFirst;
	chatFirst.session.OnTransportOpen(0);
	chatFirst.Receive("{}\x1E{\"type\":1,\"target\":\"Notice\",\"arguments\":[{\"code\":\"x\",\"params\":{}}]}\x1E", 1);
	CHECK(chatFirst.lastAction == DisconnectAction::ReturnToLogin);

	// unauthorized refreshes the token, session.not_registered registers the session, auth.revoked does both.
	CHECK(RecoveryForAction(DisconnectAction::RefreshTokenAndReconnect).refreshToken);
	CHECK(!RecoveryForAction(DisconnectAction::RefreshTokenAndReconnect).registerSession);
	CHECK(!RecoveryForAction(DisconnectAction::RegisterAndReconnect).refreshToken);
	CHECK(RecoveryForAction(DisconnectAction::RegisterAndReconnect).registerSession);
	CHECK(RecoveryForAction(DisconnectAction::RefreshAndReconnect).refreshToken);
	CHECK(RecoveryForAction(DisconnectAction::RefreshAndReconnect).registerSession);
	CHECK(!RecoveryForAction(DisconnectAction::Reconnect).refreshToken);
	CHECK(!RecoveryForAction(DisconnectAction::Reconnect).registerSession);
	CHECK(!RecoveryForAction(DisconnectAction::ReturnToLogin).refreshToken);
	CHECK(!RecoveryForAction(DisconnectAction::ReturnToLogin).registerSession);
}

static void TestStandbyRefusalDelay()
{
	// A standby refusal reconnects after its retry_after_ms until the refusals in a row lasted 10 seconds.
	StandbyRetry standby;
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(1500), 1000) == 1500);
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(800), 6000) == 800);
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(800), 1000 + STANDBY_RETRY_WINDOW_MS - 1) == 800);
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(800), 1000 + STANDBY_RETRY_WINDOW_MS) == -1);
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(800), 1000 + STANDBY_RETRY_WINDOW_MS + 5000) == -1);

	// A new row starts after a reset, which a Ready or any other ending of the connection does.
	standby.Reset();
	CHECK(StandbyRefusalDelayMs(standby, std::optional<int64_t>(800), 30000) == 800);

	// Without a ServerSwitching the wait is the standby retry delay, and the value stays within 0 and the backoff cap.
	StandbyRetry bare;
	CHECK(StandbyRefusalDelayMs(bare, std::nullopt, 0) == STANDBY_RETRY_DELAY_MS);
	CHECK(StandbyRefusalDelayMs(bare, std::optional<int64_t>(-5), 1) == 0);
	CHECK(StandbyRefusalDelayMs(bare, std::optional<int64_t>(600000), 2) == ReconnectPolicy::STEP_CAP_MS);
}

static void TestModerationText()
{
	const auto format = [](const std::string& utc) { return "<" + utc + ">"; };
	ModerationNoticeEvent notice;
	notice.action = "mute";
	CHECK(ModerationNoticeText(notice, format) == "A moderator muted you.");
	notice.reason = std::string("spam");
	CHECK(ModerationNoticeText(notice, format) == "A moderator muted you: spam");
	notice.expires_at = std::string("2026-11-15T14:00:00Z");
	CHECK(ModerationNoticeText(notice, format) == "A moderator muted you until <2026-11-15T14:00:00Z>: spam");
	notice.reason.reset();
	CHECK(ModerationNoticeText(notice, format) == "A moderator muted you until <2026-11-15T14:00:00Z>.");
	// An empty reason or expiry counts as absent.
	notice.reason = std::string();
	notice.expires_at = std::string();
	CHECK(ModerationNoticeText(notice, format) == "A moderator muted you.");

	ModerationNoticeEvent kick;
	kick.action = "kick";
	CHECK(ModerationNoticeText(kick, format) == "A moderator removed you from the online service. Reconnecting in 10 seconds.");
	kick.reason = std::string("flooding");
	CHECK(ModerationNoticeText(kick, format) == "A moderator removed you from the online service: flooding. Reconnecting in 10 seconds.");

	ModerationNoticeEvent ban;
	ban.action = "ban";
	CHECK(ModerationNoticeText(ban, format).empty());

	CHECK(RuleFailureText("chat.muted", json::object(), format) == "You are muted and cannot send messages.");
	CHECK(RuleFailureText("chat.muted", json::parse(R"({"expires_at":null})"), format) == "You are muted and cannot send messages.");
	CHECK(RuleFailureText("chat.muted", json::parse(R"({"expires_at":"2026-11-15T14:00:00Z"})"), format) == "You are muted and cannot send messages until <2026-11-15T14:00:00Z>.");
	CHECK(RuleFailureText("channel.unavailable", json::object(), format) == "That room is not available.");
	CHECK(RuleFailureText("moderation.target_outranks", json::object(), format) == "You cannot do that to a player of equal or higher rank.");
	CHECK(RuleFailureText("report.duplicate", json::object(), format) == "You already reported that.");
	CHECK(RuleFailureText("group.full", json::parse(R"({"max":10})"), format) == "The group is full. Groups can have up to 10 members.");
	CHECK(RuleFailureText("group.full", json::object(), format) == "The group is full.");
	CHECK(RuleFailureText("group.not_owner", json::object(), format) == "Only the group owner can do that.");
	CHECK(RuleFailureText("group.not_member", json::object(), format) == "You are not a member of that group.");
	// Codes without a text here fall through to the caller.
	CHECK(RuleFailureText("something.new", json::object(), format).empty());
	CHECK(RuleFailureText("chat.too_long", json::parse(R"({"max":512})"), format).empty());
}

static std::vector<uint8_t> Sha256(const std::string& data)
{
	std::vector<uint8_t> digest(32);
	BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest.data(), (ULONG)digest.size());
	return digest;
}

static json LoadJson(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	return json::parse(text.str());
}

static json FormToJson(const FormFields& fields)
{
	json j = json::object();
	for (const auto& field : fields)
		j[field.first] = field.second;
	return j;
}

static void TestSignInFixtures(const std::filesystem::path& folder)
{
	std::set<std::string> checked;
	auto load = [&](const char* name)
	{
		checked.insert(name);
		return LoadJson(folder / name);
	};

	// The authorize query and the token forms match the request fixtures field by field.
	json authorize = load("OAuthAuthorize.request.json");
	std::string url = BuildAuthorizeUrl("https://accounts.example.org/oauth/authorize", authorize["client_id"], authorize["redirect_uri"], authorize["state"], authorize["code_challenge"]);
	const std::string prefix = "https://accounts.example.org/oauth/authorize?";
	CHECK(url.compare(0, prefix.size(), prefix) == 0);
	std::map<std::string, std::string> query;
	CHECK(DecodeForm(url.substr(prefix.size()), query));
	CHECK(json(query) == authorize);

	// A sign-in for another account asks for the sign-in form even when the browser is still signed in.
	std::string loginUrl = BuildAuthorizeUrl("https://accounts.example.org/oauth/authorize", authorize["client_id"], authorize["redirect_uri"], authorize["state"], authorize["code_challenge"], true);
	CHECK(loginUrl.compare(0, prefix.size(), prefix) == 0);
	std::map<std::string, std::string> loginQuery;
	CHECK(DecodeForm(loginUrl.substr(prefix.size()), loginQuery));
	json loginAuthorize = authorize;
	loginAuthorize["prompt"] = "login";
	CHECK(json(loginQuery) == loginAuthorize);

	json codeRequest = load("OAuthToken.code.request.json");
	CHECK(FormToJson(CodeTokenForm(codeRequest["client_id"], codeRequest["code"], codeRequest["code_verifier"], codeRequest["redirect_uri"])) == codeRequest);
	// The fixture verifier belongs to the fixture challenge.
	CHECK(PkceChallenge(codeRequest["code_verifier"].get<std::string>(), Sha256) == authorize["code_challenge"].get<std::string>());

	json refreshRequest = load("OAuthToken.refresh.request.json");
	CHECK(FormToJson(RefreshTokenForm(refreshRequest["client_id"], refreshRequest["refresh_token"])) == refreshRequest);

	struct RoundTripCase
	{
		const char* name;
		RoundTripFn fn;
	};
	const RoundTripCase cases[] = {
		{ "OAuthToken.result.json", RoundTrip<TokenResponse> },
		{ "OAuthToken.error.json", RoundTrip<OAuthError> },
		{ "AuthSession.request.json", RoundTrip<AuthSessionRequest> },
		{ "AuthSession.result.json", RoundTrip<AuthSessionResult> },
		{ "AuthSession.error.json", RoundTrip<ApiError> },
	};
	for (const RoundTripCase& c : cases)
	{
		json in = load(c.name);
		try
		{
			json out = c.fn(in);
			if (out != in)
			{
				std::printf("FAILED fixture %s round trip\n  in:  %s\n  out: %s\n", c.name, in.dump().c_str(), out.dump().c_str());
				++g_failures;
			}
		}
		catch (const std::exception& e)
		{
			std::printf("FAILED fixture %s: %s\n", c.name, e.what());
			++g_failures;
		}
	}

	for (const auto& entry : std::filesystem::directory_iterator(folder))
	{
		std::string name = entry.path().filename().string();
		if (entry.path().extension() == ".json" && checked.find(name) == checked.end())
		{
			std::printf("FAILED sign-in fixture %s has no check\n", name.c_str());
			++g_failures;
		}
	}
	std::printf("sign-in fixtures checked: %zu\n", checked.size());
}

static void TestPkce()
{
	// RFC 7636 appendix B.
	const uint8_t octets[] = { 116, 24, 223, 180, 151, 153, 224, 37, 79, 250, 96, 125, 216, 173, 187, 186,
		22, 212, 37, 77, 105, 214, 191, 240, 91, 88, 5, 88, 83, 132, 141, 121 };
	std::string verifier = PkceVerifier(std::vector<uint8_t>(octets, octets + sizeof(octets)));
	CHECK(verifier == "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
	CHECK(PkceChallenge(verifier, Sha256) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");

	const uint8_t one[] = { 0xFB };
	const uint8_t two[] = { 0xFB, 0xFF };
	CHECK(Base64UrlEncode(one, 1) == "-w");
	CHECK(Base64UrlEncode(two, 2) == "-_8");
	CHECK(Base64UrlEncode(nullptr, 0).empty());
}

static void TestFormEncoding()
{
	CHECK(UrlEncode("a b&c=d/é~") == "a%20b%26c%3Dd%2F%C3%A9~");
	std::string decoded;
	CHECK(UrlDecode("a%20b+c%2f", decoded));
	CHECK(decoded == "a b c/");
	CHECK(!UrlDecode("bad%2", decoded));
	CHECK(!UrlDecode("bad%zz", decoded));
	CHECK(!UrlDecode("bad%", decoded));

	FormFields fields;
	fields.push_back(std::make_pair("redirect_uri", "http://127.0.0.1:53817/callback"));
	fields.push_back(std::make_pair("scope", SIGN_IN_SCOPE));
	CHECK(EncodeForm(fields) == "redirect_uri=http%3A%2F%2F127.0.0.1%3A53817%2Fcallback&scope=openid%20offline_access%20game");

	std::map<std::string, std::string> decodedFields;
	CHECK(DecodeForm("a=1&a=2&b=&c", decodedFields));
	CHECK(decodedFields["a"] == "1");
	CHECK(decodedFields.count("b") == 1 && decodedFields["b"].empty());
	CHECK(decodedFields.count("c") == 1);

	CHECK(LoopbackRedirectUri(53817) == "http://127.0.0.1:53817/callback");
	FormFields revoke = RevokeTokenForm("genonline_zerohour", "rt");
	CHECK(FormToJson(revoke) == json::parse("{\"client_id\":\"genonline_zerohour\",\"token\":\"rt\",\"token_type_hint\":\"refresh_token\"}"));
}

static void TestLoopbackCallback()
{
	LoopbackCallback c = ParseLoopbackRequest("GET /callback?code=abc%2Bd&state=s1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Code);
	CHECK(c.code == "abc+d");

	c = ParseLoopbackRequest("GET /callback?error=access_denied&state=s1 HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Error);
	CHECK(c.error == "access_denied");

	c = ParseLoopbackRequest("GET /callback?code=abc&state=other HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Rejected);
	c = ParseLoopbackRequest("GET /callback?code=abc HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Rejected);
	c = ParseLoopbackRequest("GET /callback?code=abc&state= HTTP/1.1\r\n\r\n", "");
	CHECK(c.kind == LoopbackCallback::Kind::Rejected);
	c = ParseLoopbackRequest("GET /callback?state=s1 HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Rejected);
	c = ParseLoopbackRequest("GET /callback HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Rejected);

	c = ParseLoopbackRequest("GET /favicon.ico HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Ignore);
	c = ParseLoopbackRequest("POST /callback?code=abc&state=s1 HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Ignore);
	c = ParseLoopbackRequest("GET /callbackx?code=abc&state=s1 HTTP/1.1\r\n\r\n", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Ignore);
	c = ParseLoopbackRequest("garbage", "s1");
	CHECK(c.kind == LoopbackCallback::Kind::Ignore);
}

static void TestDiscovery()
{
	const std::string origin = "https://api.example.org:2087";
	OpenIdConfiguration config;
	std::string error;
	CHECK(ParseOpenIdConfiguration("{\"issuer\":\"https://api.example.org:2087/\",\"authorization_endpoint\":\"https://api.example.org:2087/oauth/authorize\",\"token_endpoint\":\"https://api.example.org:2087/oauth/token\",\"revocation_endpoint\":\"https://api.example.org:2087/oauth/revoke\",\"jwks_uri\":\"x\"}", origin, config, error));
	CHECK(config.token_endpoint == "https://api.example.org:2087/oauth/token");
	CHECK(config.revocation_endpoint == "https://api.example.org:2087/oauth/revoke");

	// A separate accounts host is allowed, a broken revocation endpoint is dropped.
	CHECK(ParseOpenIdConfiguration("{\"issuer\":\"https://api.example.org:2087\",\"authorization_endpoint\":\"https://accounts.example.org/oauth/authorize\",\"token_endpoint\":\"https://accounts.example.org/oauth/token\",\"revocation_endpoint\":\"http://accounts.example.org/oauth/revoke\"}", origin, config, error));
	CHECK(config.authorization_endpoint == "https://accounts.example.org/oauth/authorize");
	CHECK(config.revocation_endpoint.empty());

	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://other.example.org\",\"authorization_endpoint\":\"https://other.example.org/oauth/authorize\",\"token_endpoint\":\"https://other.example.org/oauth/token\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://api.example.org:2087\",\"authorization_endpoint\":\"http://api.example.org:2087/oauth/authorize\",\"token_endpoint\":\"https://api.example.org:2087/oauth/token\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://api.example.org:2087\",\"authorization_endpoint\":\"https://api.example.org:2087/oauth/authorize\",\"token_endpoint\":\"https://user@evil.example/oauth/token\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://api.example.org:2087\",\"authorization_endpoint\":\"https://api.example.org:2087/oauth/authorize\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("not json", origin, config, error));
	CHECK(!IsAcceptableEndpoint("https:///path"));
	CHECK(!IsAcceptableEndpoint("https://host/path?x=1"));
}

static void TestAcceptRealtimeUrl()
{
	const std::string base = "https://api.playgenerals.online:2087/env/test/contract/1/";
	CHECK(OriginOfBase(base) == "https://api.playgenerals.online:2087");
	CHECK(OriginOfBase("https://localhost") == "https://localhost");
	CHECK(OriginOfBase("http://api.playgenerals.online/").empty());

	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/realtime", base) == "wss://api.playgenerals.online:2087/realtime");
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/genonline/realtime", base) == "wss://api.playgenerals.online:2087/genonline/realtime");
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/ws", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/realtime/", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/xrealtime", base).empty());

	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087.evil.example/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:20870/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://api.playgenerals.online:2087/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://evil.example/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/realtime?access_token=x", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/a/../b", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/%2e%2e/b", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/realtime", "http://api.playgenerals.online:2087/").empty());
}

static void TestLoopbackHttp()
{
	// Plain http is allowed only for a loopback host, and the hub of such a route uses ws on the same origin.
	CHECK(OriginOfBase("http://localhost:9000/env/dev/contract/1/") == "http://localhost:9000");
	CHECK(OriginOfBase("http://localhost/") == "http://localhost");
	CHECK(OriginOfBase("http://LocalHost:9000/") == "http://LocalHost:9000");
	CHECK(OriginOfBase("http://127.0.0.1/") == "http://127.0.0.1");
	CHECK(OriginOfBase("http://[::1]:9000/") == "http://[::1]:9000");
	CHECK(OriginOfBase("http://localhost:9000") == "http://localhost:9000");
	CHECK(OriginOfBase("http://api.example.org/").empty());
	CHECK(OriginOfBase("http://localhost.evil.example/").empty());
	CHECK(OriginOfBase("http://127.0.0.1.nip.io/").empty());
	CHECK(OriginOfBase("http://127.0.0.2/").empty());
	CHECK(OriginOfBase("http://localhost@evil.example/").empty());
	CHECK(OriginOfBase("http://localhost:9000@evil.example/").empty());
	CHECK(OriginOfBase("http://user@localhost/").empty());
	CHECK(OriginOfBase("http://localhost:90x/").empty());
	CHECK(OriginOfBase("http://localhost:/").empty());
	CHECK(OriginOfBase("http://[::1]x/").empty());
	CHECK(OriginOfBase("http://localhost\\@evil.example/").empty());
	CHECK(OriginOfBase("HTTP://localhost/").empty());

	CHECK(IsLoopbackHost("localhost"));
	CHECK(IsLoopbackHost("LOCALHOST"));
	CHECK(IsLoopbackHost("127.0.0.1"));
	CHECK(IsLoopbackHost("[::1]"));
	CHECK(!IsLoopbackHost(""));
	CHECK(!IsLoopbackHost("localhost."));
	CHECK(!IsLoopbackHost("localhost.evil.example"));
	CHECK(!IsLoopbackHost("127.0.0.1.nip.io"));
	CHECK(!IsLoopbackHost("[::1].evil.example"));
	CHECK(IsLoopbackURL("ws://localhost:9000/realtime", "ws"));
	CHECK(!IsLoopbackURL("wss://localhost:9000/realtime", "ws"));
	CHECK(!IsLoopbackURL("http://localhost:9000:1/", "http"));
	CHECK(!IsLoopbackURL("http://[::1]:9000]/", "http"));
	CHECK(!IsLoopbackURL("http://local%68ost/", "http"));

	CHECK(IsAcceptableEndpoint("http://localhost:9000/oauth/token", "http://localhost:9000"));
	CHECK(IsAcceptableEndpoint("http://[::1]:5001/oauth/token", "http://127.0.0.1:9000"));
	CHECK(IsAcceptableEndpoint("https://accounts.example.org/oauth/token", "http://localhost:9000"));
	CHECK(!IsAcceptableEndpoint("http://localhost:9000/oauth/token"));
	CHECK(!IsAcceptableEndpoint("http://localhost:9000/oauth/token", "https://localhost:9000"));
	CHECK(!IsAcceptableEndpoint("http://accounts.example.org/oauth/token", "http://localhost:9000"));
	CHECK(!IsAcceptableEndpoint("http://localhost:9000/oauth/token?x=1", "http://localhost:9000"));
	CHECK(!IsAcceptableEndpoint("http://user@localhost:9000/oauth/token", "http://localhost:9000"));
	CHECK(!IsAcceptableEndpoint("http://localhost:9000/oauth/token", "http://evil.example"));

	const std::string base = "http://localhost:9000/env/dev/contract/1/";
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/realtime", base) == "ws://localhost:9000/realtime");
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/genonline/realtime", base) == "ws://localhost:9000/genonline/realtime");
	CHECK(AcceptRealtimeUrl("ws://[::1]:9000/realtime", "http://[::1]:9000/") == "ws://[::1]:9000/realtime");
	CHECK(AcceptRealtimeUrl("wss://localhost:9000/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://evil.example/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9001/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://127.0.0.1:9000/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9000.evil.example/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9000@evil.example/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/ws", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/realtime?access_token=x", base).empty());
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/a/../realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://evil.example/realtime", "http://evil.example/").empty());
	// An https route never takes ws, even on a loopback host.
	CHECK(AcceptRealtimeUrl("ws://localhost:9000/realtime", "https://localhost:9000/env/dev/contract/1/").empty());
	CHECK(AcceptRealtimeUrl("wss://localhost:9000/realtime", "https://localhost:9000/env/dev/contract/1/") == "wss://localhost:9000/realtime");

	// The discovery of a loopback http origin may name http endpoints on a loopback host, any other host stays https.
	const std::string origin = "http://localhost:9000";
	OpenIdConfiguration config;
	std::string error;
	CHECK(ParseOpenIdConfiguration("{\"issuer\":\"http://localhost:9000/\",\"authorization_endpoint\":\"http://localhost:9000/oauth/authorize\",\"token_endpoint\":\"http://127.0.0.1:5001/oauth/token\",\"revocation_endpoint\":\"https://accounts.example.org/oauth/revoke\"}", origin, config, error));
	CHECK(config.token_endpoint == "http://127.0.0.1:5001/oauth/token");
	CHECK(config.revocation_endpoint == "https://accounts.example.org/oauth/revoke");
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"http://localhost:9000\",\"authorization_endpoint\":\"http://accounts.example.org/oauth/authorize\",\"token_endpoint\":\"http://localhost:9000/oauth/token\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"http://localhost:9000\",\"authorization_endpoint\":\"http://localhost:9000/oauth/authorize\",\"token_endpoint\":\"http://localhost.evil.example/oauth/token\"}", origin, config, error));
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://localhost:9000\",\"authorization_endpoint\":\"http://localhost:9000/oauth/authorize\",\"token_endpoint\":\"http://localhost:9000/oauth/token\"}", origin, config, error));
	// An https origin never takes http endpoints, not even on a loopback host.
	CHECK(!ParseOpenIdConfiguration("{\"issuer\":\"https://localhost:9000\",\"authorization_endpoint\":\"http://localhost:9000/oauth/authorize\",\"token_endpoint\":\"https://localhost:9000/oauth/token\"}", "https://localhost:9000", config, error));
}

static void TestPeerPayload()
{
	// RFC 4648 section 10.
	const char* const vectors[][2] = { { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" } };
	for (const auto& vector : vectors)
	{
		std::string text = vector[0];
		std::vector<uint8_t> bytes(text.begin(), text.end());
		CHECK(EncodePeerPayload(bytes) == vector[1]);
		std::vector<uint8_t> decoded;
		CHECK(DecodePeerPayload(vector[1], decoded));
		CHECK(decoded == bytes);
	}

	std::vector<uint8_t> fixture;
	CHECK(DecodePeerPayload("AAECAwQ=", fixture));
	CHECK(fixture == std::vector<uint8_t>({ 0, 1, 2, 3, 4 }));
	const uint8_t high[] = { 0xFB, 0xFF };
	CHECK(EncodePeerPayload(std::vector<uint8_t>(high, high + 2)) == "+/8=");

	std::vector<uint8_t> rejected;
	CHECK(!DecodePeerPayload("Zg=", rejected));
	CHECK(!DecodePeerPayload("Z-8=", rejected));
	CHECK(!DecodePeerPayload("Zm9v!", rejected));
	CHECK(!DecodePeerPayload("Z===", rejected));
	CHECK(!DecodePeerPayload("Zg==Zg==", rejected));
}

static void TestMatchmakingText()
{
	CHECK(MatchmakingStatusText("matchmaking.queued", json::object()) == "Joined the queue.");
	CHECK(MatchmakingStatusText("matchmaking.searching", json::parse(R"({"players":1,"desired":2,"required":2})")) == "Searching for players: 1 of 2.");
	CHECK(MatchmakingStatusText("matchmaking.expanding", json::object()) == "Widening the search.");
	CHECK(MatchmakingStatusText("matchmaking.grace_countdown", json::parse(R"({"seconds":10})")) == "Starting with the players found in 10 seconds.");
	CHECK(MatchmakingStatusText("matchmaking.grace_cancelled", json::object()) == "Waiting for more players.");
	CHECK(MatchmakingStatusText("matchmaking.creating", json::parse(R"({"players":2,"map":"Tournament Desert"})")) == "Match found on Tournament Desert, creating the lobby.");
	CHECK(MatchmakingStatusText("matchmaking.creating", json::parse(R"({"players":2})")) == "Match found, creating the lobby.");
	CHECK(MatchmakingStatusText("matchmaking.preparing", json::object()) == "Preparing the match.");
	CHECK(MatchmakingStatusText("matchmaking.starting", json::parse(R"({"seconds":5})")) == "Starting in 5 seconds.");
	CHECK(MatchmakingStatusText("matchmaking.searching", json::parse(R"({"players":"x"})")) == "Searching for players: 0 of 0.");
	CHECK(MatchmakingStatusText("matchmaking.future_code", json::object()) == "Matchmaking: matchmaking.future_code");
	CHECK(MatchmakingStatusText("matchmaking.left", json::parse(R"({"reason":"join_timeout"})")) == "You did not join the match in time and left matchmaking.");
	CHECK(MatchmakingStatusText("matchmaking.left", json::parse(R"({"reason":"setup_failed"})")) == "The match could not be set up. You left matchmaking.");
	CHECK(MatchmakingStatusText("matchmaking.left", json::parse(R"({"reason":"stats_unavailable"})")) == "Your rating could not be loaded. You left matchmaking.");
	CHECK(MatchmakingStatusText("matchmaking.left", json::parse(R"({"reason":"invalid_playlist"})")) == "The playlist is no longer available. You left matchmaking.");
	CHECK(MatchmakingStatusText("matchmaking.left", json::parse(R"({"reason":"something_new"})")) == "You left matchmaking.");
	CHECK(MatchmakingStatusText("matchmaking.left", json::object()) == "You left matchmaking.");

	CHECK(MatchRequeueText("player_left") == "A player left. Searching again.");
	CHECK(MatchRequeueText("lobby_gone") == "The match lobby closed. Searching again.");
	CHECK(MatchRequeueText("mesh_incomplete") == "Not every player could connect. Searching again.");
	CHECK(MatchRequeueText("join_timeout") == "A player did not join in time. Searching again.");
	CHECK(MatchRequeueText("setup_failed") == "The match setup failed. Searching again.");
	CHECK(MatchRequeueText("other") == "Searching again.");

	std::vector<int> out;
	CHECK(TranslateMapIndexes({ 0, 2 }, { 4, 7, 9 }, out));
	CHECK(out == std::vector<int>({ 4, 9 }));
	CHECK(!TranslateMapIndexes({ 3 }, { 4, 7, 9 }, out));
	CHECK(out.empty());
	CHECK(!TranslateMapIndexes({ -1 }, { 4 }, out));

	CHECK(MatchmakingFailureText("matchmaking.invalid_playlist", json::parse(R"({"playlist_id":3})")) == "That playlist is not available.");
	CHECK(MatchmakingFailureText("matchmaking.too_few_maps", json::parse(R"({"min":3})")) == "Select at least 3 maps.");
	CHECK(MatchmakingFailureText("matchmaking.too_few_maps", json::object()) == "Select more maps.");
	CHECK(MatchmakingFailureText("matchmaking.stats_unavailable", json::object()) == "Your rating could not be loaded. Please try again later.");
	CHECK(MatchmakingFailureText("matchmaking.already_queued", json::object()) == "You are already searching for a match.");
	CHECK(MatchmakingFailureText("matchmaking.not_queued", json::object()) == "You are not searching for a match.");
	CHECK(MatchmakingFailureText("matchmaking.future_code", json::object()) == "Matchmaking failed (matchmaking.future_code).");
	CHECK(MatchmakingFailureText("chat.too_long", json::object()).empty());
}

static ChatMessage MakeChat(const std::string& id, const std::string& channel)
{
	ChatMessage message;
	message.id = id;
	message.channel_id = channel;
	message.type = "chat";
	message.author.id = 7;
	message.author.name = "Ann";
	message.content = "hello " + id;
	message.created_at = "2026-11-14T20:00:00Z";
	return message;
}

static ChatMessage MakeTombstone(const std::string& id, const std::string& channel)
{
	ChatMessage message;
	message.id = id;
	message.channel_id = channel;
	message.created_at = "2026-11-14T19:59:00Z";
	message.deleted = true;
	return message;
}

static ChannelHistoryEvent MakeHistory(const std::string& channel, const std::vector<ChatMessage>& messages)
{
	ChannelHistoryEvent history;
	history.channel_id = channel;
	history.messages = messages;
	return history;
}

static std::vector<std::string> LineIDs(const std::vector<ChannelChatLine>& lines)
{
	std::vector<std::string> ids;
	for (const ChannelChatLine& line : lines)
	{
		ids.push_back(line.message.id);
	}
	return ids;
}

static void TestChannelChatLog()
{
	typedef std::vector<std::string> Ids;
	const std::string channel = "channel:3";

	// A live message before the history waits and shows once, as the history copy.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(log.IsAwaitingHistory());
		CHECK(log.AddLive(MakeChat("12", channel)).empty());
		const ChannelChatUpdate update = log.AddHistory(MakeHistory(channel, { MakeChat("10", channel), MakeChat("11", channel), MakeChat("12", channel) }));
		CHECK(LineIDs(update.shown) == Ids({ "10", "11", "12" }));
		CHECK(update.shown.size() == 3 && !update.shown[0].live && !update.shown[1].live && !update.shown[2].live);
		CHECK(update.removed.empty());
		CHECK(!log.IsAwaitingHistory());
	}

	// A live message after the history that already held it is not shown again.
	{
		ChannelChatLog log;
		log.Start(channel);
		const ChannelChatUpdate update = log.AddHistory(MakeHistory(channel, { MakeChat("10", channel), MakeChat("11", channel) }));
		CHECK(LineIDs(update.shown) == Ids({ "10", "11" }));
		CHECK(log.AddLive(MakeChat("11", channel)).empty());
		CHECK(LineIDs(log.AddLive(MakeChat("13", channel))) == Ids({ "13" }));
		CHECK(log.AddLive(MakeChat("13", channel)).empty());
	}

	// A history split into several events keeps its order and skips what it already showed.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(LineIDs(log.AddHistory(MakeHistory(channel, { MakeChat("1", channel), MakeChat("2", channel) })).shown) == Ids({ "1", "2" }));
		CHECK(LineIDs(log.AddHistory(MakeHistory(channel, { MakeChat("2", channel), MakeChat("3", channel) })).shown) == Ids({ "3" }));
	}

	// A tombstone is never shown, and a tombstone of a shown message removes its line.
	{
		ChannelChatLog log;
		log.Start(channel);
		const ChannelChatUpdate first = log.AddHistory(MakeHistory(channel, { MakeChat("20", channel), MakeTombstone("21", channel) }));
		CHECK(LineIDs(first.shown) == Ids({ "20" }));
		CHECK(first.removed.empty());
		CHECK(log.AddLive(MakeTombstone("22", channel)).empty());
		log.Start(channel);
		const ChannelChatUpdate second = log.AddHistory(MakeHistory(channel, { MakeTombstone("20", channel) }));
		CHECK(second.shown.empty());
		CHECK(first.shown.size() == 1 && second.removed.size() == 1 && second.removed[0] == first.shown[0].key);
	}

	// A delete removes the line of a shown message once and ignores unknown ids and other channels.
	{
		ChannelChatLog log;
		log.Start(channel);
		const ChannelChatUpdate update = log.AddHistory(MakeHistory(channel, { MakeChat("30", channel), MakeChat("31", channel) }));
		CHECK(update.shown.size() == 2 && update.shown[0].key != 0 && update.shown[0].key != update.shown[1].key);
		CHECK(log.Remove("channel:4", "30") == 0);
		CHECK(log.Remove(channel, "99") == 0);
		CHECK(update.shown.size() == 2 && log.Remove(channel, "30") == update.shown[0].key);
		CHECK(log.Remove(channel, "30") == 0);
		CHECK(log.AddLive(MakeChat("30", channel)).empty());
	}

	// A delete of a live message that waits for the history drops it before it shows.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(log.AddLive(MakeChat("40", channel)).empty());
		CHECK(log.Remove(channel, "40") == 0);
		CHECK(log.AddHistory(MakeHistory(channel, {})).shown.empty());
	}

	// Releasing the wait shows the waiting live messages in order.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(log.AddLive(MakeChat("50", channel)).empty());
		CHECK(log.AddLive(MakeChat("51", channel)).empty());
		const std::vector<ChannelChatLine> released = log.ReleaseWaiting();
		CHECK(LineIDs(released) == Ids({ "50", "51" }));
		CHECK(released.size() == 2 && released[0].live && released[1].live);
		CHECK(!log.IsAwaitingHistory());
		CHECK(log.ReleaseWaiting().empty());
	}

	// Another channel is ignored, and a channel change forgets the shown messages.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(log.AddHistory(MakeHistory("channel:4", { MakeChat("60", "channel:4") })).shown.empty());
		CHECK(log.IsAwaitingHistory());
		CHECK(LineIDs(log.AddHistory(MakeHistory(channel, { MakeChat("60", channel) })).shown) == Ids({ "60" }));
		CHECK(log.AddLive(MakeChat("61", "channel:4")).empty());
		log.Start(channel);
		CHECK(log.AddHistory(MakeHistory(channel, { MakeChat("60", channel) })).shown.empty());
		log.Start("channel:4");
		CHECK(log.AddHistory(MakeHistory("channel:4", { MakeChat("60", "channel:4") })).shown.size() == 1);
		CHECK(log.Remove(channel, "60") == 0);
		log.Clear();
		CHECK(log.AddLive(MakeChat("62", "channel:4")).empty());
		CHECK(log.AddHistory(MakeHistory("channel:4", { MakeChat("62", "channel:4") })).shown.empty());
	}

	// The shown ids stay bounded, so the oldest id can show again.
	{
		ChannelChatLog log;
		log.Start(channel);
		CHECK(log.AddHistory(MakeHistory(channel, {})).shown.empty());
		for (size_t i = 0; i <= ChannelChatLog::MAX_SHOWN; ++i)
		{
			CHECK(log.AddLive(MakeChat(std::to_string(i), channel)).size() == 1);
		}
		CHECK(log.ShownCount() == ChannelChatLog::MAX_SHOWN);
		CHECK(log.AddLive(MakeChat(std::to_string(ChannelChatLog::MAX_SHOWN), channel)).empty());
		CHECK(log.AddLive(MakeChat("0", channel)).size() == 1);
	}
}

static void TestLobbyChatReplay()
{
	typedef std::vector<std::string> Ids;
	const std::string lobby = "lobby:991";

	// A joined lobby shows live chat at once, a rejoin shows the missed messages once.
	ChannelChatLog log;
	log.Start(lobby);
	CHECK(log.ReleaseWaiting().empty());
	CHECK(LineIDs(log.AddLive(MakeChat("1", lobby))) == Ids({ "1" }));
	log.Start(lobby);
	CHECK(log.AddLive(MakeChat("3", lobby)).empty());
	const ChannelChatUpdate update = log.AddHistory(MakeHistory(lobby, { MakeChat("1", lobby), MakeChat("2", lobby), MakeChat("3", lobby) }));
	CHECK(LineIDs(update.shown) == Ids({ "2", "3" }));
	CHECK(!update.shown[0].live);
	CHECK(log.AddHistory(MakeHistory(lobby, { MakeChat("1", lobby), MakeChat("2", lobby), MakeChat("3", lobby) })).shown.empty());
	CHECK(log.Remove(lobby, "2") == update.shown[0].key);
	CHECK(log.Remove("lobby:992", "3") == 0);
}

static ChatMessage MakeDirect(const std::string& id, int64_t peer, int64_t author)
{
	ChatMessage message = MakeChat(id, DirectMessageChannel(peer));
	message.author.id = author;
	return message;
}

static Conversation MakeConversation(const std::string& channel, int unread, const std::string& lastID)
{
	Conversation conversation;
	conversation.channel_id = channel;
	conversation.unread = unread;
	if (!lastID.empty())
	{
		conversation.last = Nullable<ChatMessage>(MakeChat(lastID, channel));
	}
	return conversation;
}

static std::vector<std::string> DirectLineIDs(const DirectConversation& conversation)
{
	std::vector<std::string> ids;
	for (const DirectChatLine& line : conversation.Lines())
	{
		ids.push_back(line.IsNote() ? std::string("note") : line.message.id);
	}
	return ids;
}

static GetHistoryResult MakeDirectHistory(const std::vector<ChatMessage>& messages, bool hasMore)
{
	GetHistoryResult result;
	result.messages = messages;
	result.has_more = hasMore;
	return result;
}

static void TestDirectMessagePeer()
{
	CHECK(DirectMessagePeer("dm:42") == 42);
	CHECK(DirectMessagePeer("dm:") == -1);
	CHECK(DirectMessagePeer("dm:4x") == -1);
	CHECK(DirectMessagePeer("dm:-4") == -1);
	CHECK(DirectMessagePeer("dm:99999999999999999999") == -1);
	CHECK(DirectMessagePeer("channel:42") == -1);
	CHECK(DirectMessagePeer("group:42") == -1);
	CHECK(DirectMessageChannel(42) == "dm:42");
}

static void TestDirectMessageUnread()
{
	// The snapshot sets the unread counts of direct conversations and ignores groups.
	DirectMessageUnread unread;
	ConversationsEvent snapshot;
	snapshot.conversations = { MakeConversation("dm:7", 2, "20"), MakeConversation("dm:8", 0, "30"), MakeConversation("group:5", 4, "40") };
	CHECK(unread.ApplySnapshot(snapshot) == 1);
	CHECK(unread.Unread(7) == 2);
	CHECK(unread.Unread(8) == 0);
	CHECK(unread.Unread(5) == 0);

	// A new message of the other user makes the conversation unread once.
	CHECK(unread.AddUnread(8));
	CHECK(!unread.AddUnread(8));
	CHECK(unread.Unread(8) == 2);
	unread.Clear(8);
	CHECK(unread.Unread(8) == 0);

	// A ReadMarker from another connection clears the unread count of its whole conversation.
	ReadMarkerEvent marker;
	marker.channel_id = "dm:7";
	CHECK(unread.ApplyReadMarker(marker));
	CHECK(unread.Unread(7) == 0);
	marker.channel_id = "group:5";
	CHECK(!unread.ApplyReadMarker(marker));

	// A later snapshot replaces the counts and forgets conversations it does not list.
	CHECK(unread.AddUnread(8));
	ConversationsEvent later;
	later.conversations = { MakeConversation("dm:7", 1, "22") };
	CHECK(unread.ApplySnapshot(later) == 1);
	CHECK(unread.Unread(7) == 1);
	CHECK(unread.Unread(8) == 0);
	CHECK(unread.ApplySnapshot(later) == 0);
}

static void TestDirectConversation()
{
	typedef std::vector<std::string> Ids;
	const int64_t peer = 7;
	const int64_t me = 1;

	// Live messages show at once and once per message id, own messages included.
	DirectConversation conversation(DirectMessageChannel(peer));
	DirectChatLine line;
	CHECK(conversation.NeedsHistory());
	CHECK(conversation.AddLive(MakeDirect("12", peer, peer), line));
	CHECK(line.key != 0 && line.message.id == "12");
	CHECK(!conversation.AddLive(MakeDirect("12", peer, peer), line));
	CHECK(!conversation.AddLive(MakeDirect("50", 8, 8), line));

	// The history shows before the live messages it shares, oldest first, and skips tombstones.
	conversation.BeginHistory();
	CHECK(!conversation.NeedsHistory());
	CHECK(conversation.AddLive(MakeDirect("13", peer, me), line));
	conversation.AddHistory(MakeDirectHistory({ MakeTombstone("9", "dm:7"), MakeDirect("10", peer, peer), MakeDirect("11", peer, me), MakeDirect("12", peer, peer), MakeDirect("13", peer, me) }, true));
	CHECK(DirectLineIDs(conversation) == Ids({ "10", "11", "12", "13" }));
	CHECK(!conversation.NeedsHistory());
	CHECK(!conversation.AddLive(MakeDirect("11", peer, me), line));
	CHECK(conversation.NewestMessage() != nullptr && conversation.NewestMessage()->id == "13");

	// A local note stays, and a reload after a reconnect adds the missed messages after the known ones.
	conversation.AddNote(L"Not delivered");
	conversation.ForgetHistory();
	CHECK(conversation.NeedsHistory());
	conversation.BeginHistory();
	CHECK(conversation.AddLive(MakeDirect("16", peer, peer), line));
	conversation.AddHistory(MakeDirectHistory({ MakeDirect("12", peer, peer), MakeDirect("13", peer, me), MakeDirect("14", peer, peer), MakeDirect("15", peer, peer), MakeDirect("16", peer, peer) }, false));
	CHECK(DirectLineIDs(conversation) == Ids({ "10", "11", "12", "13", "14", "15", "note", "16" }));
	CHECK(conversation.NewestMessage() != nullptr && conversation.NewestMessage()->id == "16");

	// A tombstone in the history removes the shown message.
	conversation.BeginHistory();
	conversation.AddHistory(MakeDirectHistory({ MakeTombstone("14", "dm:7"), MakeDirect("15", peer, peer) }, false));
	CHECK(DirectLineIDs(conversation) == Ids({ "10", "11", "12", "13", "15", "note", "16" }));

	// MessageDelete removes a shown line by its key and ignores unknown ids.
	uint32_t key11 = 0;
	for (const DirectChatLine& shown : conversation.Lines())
	{
		if (shown.message.id == "11")
		{
			key11 = shown.key;
		}
	}
	CHECK(key11 != 0);
	CHECK(conversation.Remove("11") == key11);
	CHECK(conversation.Remove("11") == 0);
	CHECK(conversation.Remove("99") == 0);
	CHECK(DirectLineIDs(conversation) == Ids({ "10", "12", "13", "15", "note", "16" }));

	// A failed history load is requested again.
	conversation.ForgetHistory();
	conversation.BeginHistory();
	conversation.HistoryFailed();
	CHECK(conversation.NeedsHistory());

	// A history that shares no shown message follows the shown lines.
	DirectConversation older(DirectMessageChannel(peer));
	CHECK(older.AddLive(MakeDirect("1", peer, peer), line));
	older.BeginHistory();
	older.AddHistory(MakeDirectHistory({ MakeDirect("5", peer, peer), MakeDirect("6", peer, peer) }, true));
	CHECK(DirectLineIDs(older) == Ids({ "1", "5", "6" }));
}

static LobbyState MakeLobbyState(const std::string& state, bool started)
{
	LobbyState lobby;
	lobby.id = 991;
	lobby.state = state;
	if (started)
	{
		LobbyStart start;
		start.match_id = 7001;
		start.rng_seed = 123456;
		start.screenshot_url = std::string("https://example.invalid/shot");
		lobby.match_id = int64_t(7001);
		lobby.start = start;
	}
	return lobby;
}

static FriendsEntry MakeFriendsEntry(int64_t userID, const std::string& name, bool online)
{
	FriendsEntry entry;
	entry.user_id = userID;
	entry.display_name = name;
	entry.online = online;
	return entry;
}

static FriendPresenceEvent MakePresence(int64_t userID, bool online, const std::string& activity)
{
	FriendPresenceEvent presence;
	presence.user_id = userID;
	presence.online = online;
	presence.activity = activity;
	return presence;
}

static void TestFriendRoster()
{
	const int64_t requester = 7;

	// The service sends the accepting user FriendPresence before the accept call answers: the new friend is online.
	{
		FriendsMap friends;
		FriendsMap pending;
		pending.insert_or_assign(requester, MakeFriendsEntry(requester, "Requester", false));
		FriendPresenceChange change = ApplyFriendPresence(friends, pending, MakePresence(requester, true, "in_game"), "In game");
		CHECK(!change.friendChanged);
		CHECK(!change.onlineChanged);
		CHECK(friends.empty());
		CHECK(MoveAcceptedRequest(friends, pending, requester));
		CHECK(pending.empty());
		CHECK(friends.size() == 1);
		CHECK(friends[requester].online);
		CHECK(friends[requester].presence == "In game");
		CHECK(friends[requester].display_name == "Requester");
	}

	// The accept call answers first: the friend is listed offline until FriendPresence arrives.
	{
		FriendsMap friends;
		FriendsMap pending;
		pending.insert_or_assign(requester, MakeFriendsEntry(requester, "Requester", false));
		CHECK(MoveAcceptedRequest(friends, pending, requester));
		CHECK(!friends[requester].online);
		FriendPresenceChange change = ApplyFriendPresence(friends, pending, MakePresence(requester, true, "menu"), "Menu");
		CHECK(change.friendChanged);
		CHECK(change.onlineChanged);
		CHECK(change.displayName == "Requester");
		CHECK(friends[requester].online);
		CHECK(friends[requester].presence == "Menu");
	}

	// A friend's presence changes as before; the same presence again changes nothing.
	{
		FriendsMap friends;
		FriendsMap pending;
		friends.insert_or_assign(requester, MakeFriendsEntry(requester, "Friend", true));
		friends[requester].presence = "Menu";
		FriendPresenceChange same = ApplyFriendPresence(friends, pending, MakePresence(requester, true, "menu"), "Menu");
		CHECK(!same.friendChanged);
		CHECK(!same.onlineChanged);
		FriendPresenceChange offline = ApplyFriendPresence(friends, pending, MakePresence(requester, false, "offline"), "Offline");
		CHECK(offline.friendChanged);
		CHECK(offline.onlineChanged);
		CHECK(!friends[requester].online);
	}

	// Presence of a user who is neither a friend nor pending, and an accept of no pending request, change nothing.
	{
		FriendsMap friends;
		FriendsMap pending;
		FriendPresenceChange change = ApplyFriendPresence(friends, pending, MakePresence(requester, true, "menu"), "Menu");
		CHECK(!change.friendChanged);
		CHECK(friends.empty());
		CHECK(pending.empty());
		CHECK(!MoveAcceptedRequest(friends, pending, requester));
		CHECK(friends.empty());
	}
}

static void TestLobbyStateStart()
{
	GameStartEvent start;
	CHECK(!LobbyStateStart(MakeLobbyState("setup", false), start));
	CHECK(!LobbyStateStart(MakeLobbyState("countdown", false), start));
	CHECK(!LobbyStateStart(MakeLobbyState("complete", true), start));
	CHECK(LobbyStateStart(MakeLobbyState("in_game", true), start));
	CHECK(start.lobby_id == 991);
	CHECK(start.match_id == 7001);
	CHECK(start.rng_seed == 123456);
	CHECK(!start.screenshot_url.isNull && start.screenshot_url.value == "https://example.invalid/shot");

	// GameStart and LobbyState.start start a match once, in either order.
	{
		MatchStartOnce once;
		CHECK(once.Begin(7001));
		CHECK(!once.Begin(7001));
		CHECK(!once.Begin(7001));
	}
	// A later match starts again, and a reset forgets the started match.
	{
		MatchStartOnce once;
		CHECK(once.Begin(7001));
		CHECK(once.Begin(7002));
		once.Reset();
		CHECK(once.Begin(7002));
	}
}

static void TestApiError()
{
	// The spec form.
	{
		const ApiErrorInfo info = ParseApiError(R"({"error":"moderation.banned","params":{"reason":"cheating","expires_at":"2026-12-01T00:00:00Z"}})");
		CHECK(info.code == "moderation.banned");
		CHECK(info.reason == std::optional<std::string>("cheating"));
		CHECK(info.expires_at == std::optional<std::string>("2026-12-01T00:00:00Z"));
		CHECK(info.params["reason"] == "cheating");
	}
	// A top level code with reason and expires_at at the top level, until the server is aligned.
	{
		const ApiErrorInfo info = ParseApiError(R"({"code":"chat.muted","reason":"spam","expires_at":"2026-12-01T00:00:00Z"})");
		CHECK(info.code == "chat.muted");
		CHECK(info.reason == std::optional<std::string>("spam"));
		CHECK(info.expires_at == std::optional<std::string>("2026-12-01T00:00:00Z"));
		// The copied expiry reaches the failure texts that read params.
		CHECK(info.params["expires_at"] == "2026-12-01T00:00:00Z");
	}
	// The older message and error body names the code in error.
	{
		const ApiErrorInfo info = ParseApiError(R"({"message":"A game session needs exe_crc, ini_crc and anticheat_id.","error":"invalid_argument"})");
		CHECK(info.code == "invalid_argument");
		CHECK(!info.reason.has_value());
		CHECK(!info.expires_at.has_value());
		CHECK(info.params.is_object() && info.params.empty());
	}
	// params wins over the top level, and a null or missing expiry is a permanent ban.
	{
		const ApiErrorInfo info = ParseApiError(R"({"error":"moderation.banned","params":{"reason":"a","expires_at":null},"reason":"b"})");
		CHECK(info.reason == std::optional<std::string>("a"));
		CHECK(!info.expires_at.has_value());
	}
	// A body that is no JSON object names no code.
	CHECK(ParseApiError("").code.empty());
	CHECK(ParseApiError("<html>bad gateway</html>").code.empty());
	CHECK(ParseApiError("[1,2]").code.empty());

	// The REST outcome parses failures only, and a bare 401 is unauthorized.
	{
		const RestOutcome ok = MakeRestOutcome(true, 204, "");
		CHECK(ok.Succeeded());
		CHECK(ok.error.code.empty());
		EmptyObject empty;
		CHECK(ok.Data(empty));
		const RestOutcome failed = MakeRestOutcome(true, 422, R"({"error":"social.friend_list_full","params":{"max":200}})");
		CHECK(!failed.Succeeded());
		CHECK(failed.error.code == "social.friend_list_full");
		CHECK(failed.error.params["max"] == 200);
		CHECK(MakeRestOutcome(true, 401, "").error.code == "unauthorized");
		const RestOutcome lost = MakeRestOutcome(false, 0, "");
		CHECK(!lost.delivered);
		CHECK(!lost.Succeeded());
		MatchResultResponse response;
		CHECK(MakeRestOutcome(true, 200, R"({"screenshot_url":"s","replay_url":"r"})").Data(response));
		CHECK(response.screenshot_url == "s" && response.replay_url == "r");
		CHECK(!MakeRestOutcome(true, 200, "not json").Data(response));
	}

	// Failure texts of the codes the sign-in and the upgrade return.
	const auto format = [](const std::string& utc) { return "<" + utc + ">"; };
	CHECK(RuleFailureText("unauthorized", json::object(), format) == "Your sign-in is no longer valid. Please sign in again.");
	CHECK(RuleFailureText("session.build_not_accepted", json::object(), format) == "This version of the game is not accepted by the online service. Please update the game.");
	CHECK(RuleFailureText("moderation.banned", json::object(), format) == "You are banned from Generals Online.");
	CHECK(RuleFailureText("moderation.banned", json::parse(R"({"expires_at":"2026-12-01T00:00:00Z"})"), format) == "You are banned from Generals Online until <2026-12-01T00:00:00Z>.");

	// The ban dialog shows the expiry, permanent without one, and nothing about the end without a ban body.
	CHECK(BanDialogText(std::string("2026-12-01T00:00:00Z"), true, format) == "You have been banned from Generals Online until <2026-12-01T00:00:00Z>.");
	CHECK(BanDialogText(std::nullopt, true, format) == "You have been permanently banned from Generals Online.");
	CHECK(BanDialogText(std::string(), true, format) == "You have been permanently banned from Generals Online.");
	CHECK(BanDialogText(std::nullopt, false, format) == "You have been banned from Generals Online.");
}

static void TestRestRetry()
{
	// Lost answers, timeouts, rate limits and server errors retry with a growing delay.
	CHECK(RestRetryDelayMs(MakeRestOutcome(false, 0, ""), 1) == 2000);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 503, ""), 2) == 5000);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 408, ""), 3) == 10000);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 429, R"({"error":"rate_limited","params":{"retry_after_ms":1500}})"), 1) == 1500);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 429, R"({"error":"rate_limited","params":{"retry_after_ms":900000}})"), 1) == 60000);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 429, ""), 1) == 2000);
	// The fourth send is the last one.
	CHECK(RestRetryDelayMs(MakeRestOutcome(false, 0, ""), REST_RETRY_MAX_SENDS) == -1);
	// A success, a rule failure or a rejected token never retries.
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 200, "{}"), 1) == -1);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 422, R"({"error":"invalid_argument","params":{"field":"side"}})"), 1) == -1);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 401, ""), 1) == -1);
	CHECK(RestRetryDelayMs(MakeRestOutcome(true, 404, ""), 1) == -1);
}

static void TestStandbyRetry()
{
	const std::string standbyBody = R"({"error":"service.unavailable","params":{"what":"standby"}})";

	// A standby answer is a 503 with service.unavailable and what standby.
	CHECK(IsStandbyAnswer(MakeRestOutcome(true, 503, standbyBody), false));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(true, 503, R"({"error":"service.unavailable","params":{"what":"database"}})"), false));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(true, 503, R"({"error":"server.overloaded","params":{"what":"standby"}})"), false));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(true, 503, ""), false));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(true, 500, standbyBody), false));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(false, 0, ""), false));

	// The refused realtime upgrade has no body, so any 503 there is a standby answer.
	CHECK(IsStandbyAnswer(MakeRestOutcome(true, 503, ""), true));
	CHECK(IsStandbyAnswer(MakeRestOutcome(true, 503, R"({"error":"server.overloaded"})"), true));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(true, 502, ""), true));
	CHECK(!IsStandbyAnswer(MakeRestOutcome(false, 0, ""), true));

	// Standby answers wait a fixed second until 10 seconds passed since the first one in a row.
	StandbyRetry standby;
	CHECK(STANDBY_RETRY_DELAY_MS == 1000);
	CHECK(standby.NextDelayMs(true, 50000) == 1000);
	CHECK(standby.NextDelayMs(true, 51000) == 1000);
	CHECK(standby.NextDelayMs(true, 59999) == 1000);
	CHECK(standby.NextDelayMs(true, 60000) == -1);
	CHECK(standby.NextDelayMs(true, 61000) == -1);

	// Any other answer ends the run, and the next standby answer starts a new window.
	CHECK(standby.NextDelayMs(false, 62000) == -1);
	CHECK(standby.NextDelayMs(true, 63000) == 1000);
	CHECK(standby.NextDelayMs(true, 72999) == 1000);
	CHECK(standby.NextDelayMs(true, 73000) == -1);
	standby.Reset();
	CHECK(standby.NextDelayMs(true, 74000) == 1000);
}

static InvokeOutcome FailedResult(const std::string& code, const json& params)
{
	InvokeOutcome outcome;
	outcome.delivered = true;
	outcome.result.ok = false;
	outcome.result.code = code;
	outcome.result.params = params;
	return outcome;
}

static void TestStandbyHubCalls()
{
	const InvokeOutcome standby = FailedResult("service.unavailable", json{ { "what", "standby" } });
	CHECK(IsStandbyResult(standby));
	CHECK(!IsStandbyResult(FailedResult("service.unavailable", json{ { "what", "database" } })));
	CHECK(!IsStandbyResult(FailedResult("lobby.full", json{ { "what", "standby" } })));
	InvokeOutcome lost;
	lost.transportError = "connection lost";
	CHECK(!IsStandbyResult(lost));

	auto makeCall = [&](const std::string& method, int& calls, InvokeOutcome& last)
	{
		ParkedHubCall call;
		call.method = method;
		call.request = json{ { "lobby_id", 7 } };
		call.callback = [&calls, &last](const InvokeOutcome& outcome)
		{
			++calls;
			last = outcome;
		};
		return call;
	};

	// A standby Result parks the call without its callback.
	StandbyHubCalls queue;
	int calls = 0;
	InvokeOutcome last;
	CHECK(queue.Complete(makeCall("SetReady", calls, last), standby, 1000));
	CHECK(calls == 0);
	CHECK(queue.Size() == 1);

	// Another failure code and a transport error run the callback at once.
	int otherCalls = 0;
	InvokeOutcome otherLast;
	CHECK(!queue.Complete(makeCall("SetReady", otherCalls, otherLast), FailedResult("lobby.full", json::object()), 1000));
	CHECK(otherCalls == 1);
	CHECK(otherLast.Code() == "lobby.full");
	CHECK(!queue.Complete(makeCall("SetReady", otherCalls, otherLast), lost, 1000));
	CHECK(otherCalls == 2);
	CHECK(otherLast.transportError == "connection lost");
	CHECK(queue.Size() == 1);

	// The call waits for a ready session and for one second after its standby answer.
	CHECK(queue.TakeDue(1999, true).empty());
	CHECK(queue.TakeDue(2000, false).empty());
	CHECK(queue.TakeDue(5000, false).empty());
	std::vector<ParkedHubCall> due = queue.TakeDue(5000, true);
	CHECK(due.size() == 1);
	CHECK(queue.Size() == 0);
	CHECK(calls == 0);
	CHECK(due.size() == 1 && due[0].method == "SetReady" && due[0].request["lobby_id"] == 7);

	// Standby answers in a row park the call again until 10 seconds passed since the first one.
	ParkedHubCall resent = due[0];
	CHECK(queue.Complete(resent, standby, 6000));
	due = queue.TakeDue(7000, true);
	CHECK(due.size() == 1);
	CHECK(queue.Complete(due[0], standby, 10999));
	due = queue.TakeDue(11999, true);
	CHECK(due.size() == 1);
	CHECK(!queue.Complete(due[0], standby, 12000));
	CHECK(calls == 1);
	CHECK(IsStandbyResult(last));
	CHECK(queue.Size() == 0);
	CHECK(queue.TakeDue(30000, true).empty());
	CHECK(calls == 1);

	// Parked calls come back in the order they were parked.
	int callsA = 0;
	int callsB = 0;
	int callsC = 0;
	InvokeOutcome lastA;
	InvokeOutcome lastB;
	InvokeOutcome lastC;
	CHECK(queue.Complete(makeCall("A", callsA, lastA), standby, 100));
	CHECK(queue.Complete(makeCall("B", callsB, lastB), standby, 200));
	CHECK(queue.Complete(makeCall("C", callsC, lastC), standby, 300));
	due = queue.TakeDue(1250, true);
	CHECK(due.size() == 2 && due[0].method == "A" && due[1].method == "B");
	due = queue.TakeDue(1300, true);
	CHECK(due.size() == 1 && due[0].method == "C");

	// A stale call is dropped without its callback, ready or not.
	bool wanted = true;
	int staleCalls = 0;
	InvokeOutcome staleLast;
	ParkedHubCall stale = makeCall("JoinLobby", staleCalls, staleLast);
	stale.fnWanted = [&wanted]()
	{
		return wanted;
	};
	CHECK(queue.Complete(stale, standby, 100));
	int dropped = 0;
	auto onDropped = [&dropped](const ParkedHubCall&)
	{
		++dropped;
	};
	CHECK(queue.TakeDue(500, false, onDropped).empty());
	CHECK(queue.Size() == 1);
	wanted = false;
	CHECK(queue.TakeDue(500, false, onDropped).empty());
	CHECK(queue.Size() == 0);
	CHECK(dropped == 1);
	CHECK(staleCalls == 0);

	// Clear drops every parked call without its callback.
	CHECK(queue.Complete(makeCall("A", callsA, lastA), standby, 100));
	queue.Clear();
	CHECK(queue.Size() == 0);
	CHECK(queue.TakeDue(5000, true).empty());
	CHECK(callsA == 0);

	// A call that waits for a ready session longer than the standby window fails with its last answer.
	int waitCalls = 0;
	InvokeOutcome waitLast;
	CHECK(queue.Complete(makeCall("SetReady", waitCalls, waitLast), standby, 1000));
	CHECK(queue.TakeDue(10999, false).empty());
	CHECK(waitCalls == 0 && queue.Size() == 1);
	CHECK(queue.TakeDue(11000, false).empty());
	CHECK(waitCalls == 1 && IsStandbyResult(waitLast));
	CHECK(queue.Size() == 0);

	// The wait for a ready session counts from the last standby answer, not the first one.
	int lateCalls = 0;
	InvokeOutcome lateLast;
	CHECK(queue.Complete(makeCall("SetReady", lateCalls, lateLast), standby, 1000));
	due = queue.TakeDue(2000, true);
	CHECK(due.size() == 1);
	CHECK(queue.Complete(due[0], standby, 9000));
	CHECK(queue.TakeDue(11000, false).empty());
	CHECK(lateCalls == 0 && queue.Size() == 1);
	CHECK(queue.TakeDue(18999, false).empty());
	CHECK(lateCalls == 0 && queue.Size() == 1);
	CHECK(queue.TakeDue(19000, false).empty());
	CHECK(lateCalls == 1 && IsStandbyResult(lateLast));
	CHECK(queue.Size() == 0);
}

static void TestRestPaths()
{
	CHECK(RestFriendRequestsPath() == "/api/v1/me/friend_requests");
	CHECK(RestConnectionReportPath(991) == "/api/v1/lobbies/991/connection_reports");
	CHECK(ConnectionReportOutcome(true, false, false) == "connected_direct");
	CHECK(ConnectionReportOutcome(true, true, false) == "connected_relay");
	CHECK(ConnectionReportOutcome(false, false, true) == "disconnected");
	CHECK(ConnectionReportOutcome(false, false, false) == "failed");
	CHECK(RestFriendRequestAcceptPath(7) == "/api/v1/me/friend_requests/7/accept");
	CHECK(RestFriendRequestPath(7) == "/api/v1/me/friend_requests/7");
	CHECK(RestFriendPath(7) == "/api/v1/me/friends/7");
	CHECK(RestBlockPath(88) == "/api/v1/me/blocks/88");
	CHECK(RestMePath() == "/api/v1/me");
	CHECK(RestMatchResultPath(551234) == "/api/v1/matches/551234/results/me");
	CHECK(RestPlaylistsPath() == "/api/v1/matchmaking/playlists");

	// The friend request body is the user id the hub took.
	FriendRequestBody body;
	body.user_id = 7;
	CHECK(json(body) == json::parse(R"({"user_id":7})"));
}

static std::string ReadApiFixture(const std::filesystem::path& folder, const char* name)
{
	std::ifstream file(folder / name, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

static void TestLegacyRouteReplacements(const std::filesystem::path& folder)
{
	// The compatibility check names the client and a plain YYYY.MM.PATCH version.
	CHECK(RestCompatibilityPath("genonline_zerohour", "2026.11.3") == "/api/v1/compatibility?client=genonline_zerohour&version=2026.11.3");
	CHECK(CompatibilityVersion("v2026.11.3", "2026-11-10 12:00:00 +0000") == "2026.11.3");
	CHECK(CompatibilityVersion("v2026.11.3-5-gabc1234", "2026-11-14 12:00:00 +0000") == "2026.11.3");
	CHECK(CompatibilityVersion("v2026.1.12", "2026-01-20 12:00:00 +0000") == "2026.1.12");
	CHECK(CompatibilityVersion("", "2026-11-14 10:00:00 +0100") == "2026.11.0");
	CHECK(CompatibilityVersion("v1.2", "2026-01-05 10:00:00 +0100") == "2026.1.0");
	CHECK(CompatibilityVersion("v2026.13.1", "2026-02-05 10:00:00 +0100") == "2026.2.0");
	CHECK(CompatibilityVersion("v2026.11.3rc", "2026-11-05 10:00:00 +0100") == "2026.11.0");
	CHECK(CompatibilityVersion("", "") == "1970.1.0");

	// The route check takes any compatibility answer, also an incompatible one.
	const std::string compatibility = ReadApiFixture(folder, "get-compatibility.response.json");
	CHECK(IsCompatibilityAnswer(compatibility));
	CHECK(!IsCompatibilityAnswer("{}"));
	CHECK(!IsCompatibilityAnswer(R"({"execrc_30":1,"execrc_60":2})"));
	CHECK(!IsCompatibilityAnswer("not json"));

	// The connection test counts a standby answer as a reached service.
	const std::string standbyBody = R"({"error":"service.unavailable","params":{"what":"standby"}})";
	CHECK(ProbeAnswerOf(200, compatibility) == ProbeAnswer::Serving);
	CHECK(ProbeAnswerOf(503, standbyBody) == ProbeAnswer::Standby);
	CHECK(ProbeAnswerOf(503, R"({"error":"service.unavailable","params":{"what":"database"}})") == ProbeAnswer::None);
	CHECK(ProbeAnswerOf(503, "") == ProbeAnswer::None);
	CHECK(ProbeAnswerOf(200, "{}") == ProbeAnswer::None);
	CHECK(ProbeAnswerOf(502, compatibility) == ProbeAnswer::None);

	{
		CompatibilityResponse answer;
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 200, compatibility), answer) == CompatibilityVerdict::NeedsUpdate);
		CHECK(!answer.update.isNull && answer.update.value.patcher_name == "GeneralsOnline_update_111426.exe");
		CHECK(!answer.update.value.patcher_size.isNull && answer.update.value.patcher_size.value == 48211968);
	}
	{
		CompatibilityResponse answer;
		const std::string body = R"({"compatible":true,"min_version":"2026.11.2","service_version":"2026.11.5","update":null})";
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 200, body), answer) == CompatibilityVerdict::UpToDate);
	}
	{
		CompatibilityResponse answer;
		const std::string body = R"({"compatible":false,"min_version":"2026.11.2","service_version":"2026.11.5","update":null})";
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 200, body), answer) == CompatibilityVerdict::Failed);
	}
	{
		CompatibilityResponse answer;
		json body = json::parse(compatibility);
		body["update"]["patcher_size"] = nullptr;
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 200, body.dump()), answer) == CompatibilityVerdict::Failed);
	}
	for (const char* name : { "", "../update.exe", "dir/update.exe", "C:update.exe", "a\\b.exe" })
	{
		CompatibilityResponse answer;
		json body = json::parse(compatibility);
		body["update"]["patcher_name"] = name;
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 200, body.dump()), answer) == CompatibilityVerdict::Failed);
	}
	{
		CompatibilityResponse answer;
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(true, 400, R"({"error":"invalid_argument","params":{"field":"version"}})"), answer) == CompatibilityVerdict::Failed);
		CHECK(CompatibilityVerdictOf(MakeRestOutcome(false, 0, ""), answer) == CompatibilityVerdict::Failed);
	}

	// The config carries the service config in its grouped sections and the MOTD.
	CHECK(RestConfigPath() == "/api/v1/config");
	{
		ConfigResponse config;
		CHECK(MakeRestOutcome(true, 200, ReadApiFixture(folder, "get-config.response.json")).Data(config));
		CHECK(config.service_config.is_object() && config.service_config["connection"]["retry_signalling"] == true);
		CHECK(config.service_config["input_delay"]["slack_percent"] == 30);
		CHECK(config.motd == "Thank you for playing Generals Online.\nThere are currently 312 player(s) online.");
		ConfigResponse withoutLevel;
		CHECK(MakeRestOutcome(true, 200, R"({"service_config":{},"motd":"hi"})").Data(withoutLevel));
		CHECK(withoutLevel.motd == "hi" && !withoutLevel.anticheat_level.has_value());
	}

	// The link sends the provider's own proof of the account to the route of the provider the plugin reports.
	CHECK(RestLinkedAccountPath("eos") == "/api/v1/me/linked_accounts/eos");
	CHECK(RestLinkedAccountPath("steam") == "/api/v1/me/linked_accounts/steam");
	{
		LinkAccountBody link;
		link.proof = "abc";
		CHECK(json(link) == json::parse(R"({"proof":"abc"})"));
	}

	// The plugin signs in to its platform with the short-lived token of its provider, not with the access token.
	CHECK(RestSignInTokenPath("eos") == "/api/v1/me/linked_accounts/eos/sign_in_token");
	CHECK(RestSignInTokenPath("steam") == "/api/v1/me/linked_accounts/steam/sign_in_token");
	{
		SignInTokenResponse token;
		CHECK(MakeRestOutcome(true, 200, R"({"token":"a.b.c","expires_in":300})").Data(token));
		CHECK(token.token == "a.b.c" && token.expires_in == 300);
		SignInTokenResponse missing;
		CHECK(!MakeRestOutcome(true, 200, R"({"expires_in":300})").Data(missing));
	}

	// The global stats map each playable side onto its service index.
	CHECK(RestGlobalStatsPath() == "/api/v1/stats/global");
	CHECK(GlobalStatsFactionIndex(2) == 0);
	CHECK(GlobalStatsFactionIndex(13) == 11);
	CHECK(GlobalStatsFactionIndex(1) == -1);
	CHECK(GlobalStatsFactionIndex(14) == -1);
	CHECK(GlobalStatsFactionIndex(-1) == -1);
	{
		GlobalStatsResponse global;
		CHECK(MakeRestOutcome(true, 200, ReadApiFixture(folder, "get-stats-global.response.json")).Data(global));
		CHECK(global.players == 312 && global.lobbies == 27);
		CHECK(global.factions.size() == 12 && global.factions[0].side == 2 && global.factions[0].matches == 41 && global.factions[0].wins == 20);
	}

	// Player stats come one at a time or in batches of at most 100 ids.
	CHECK(RestPlayerStatsPath(42) == "/api/v1/players/42/stats");
	CHECK(RestPlayerStatsBatchPaths({}).empty());
	CHECK(RestPlayerStatsBatchPaths({ 1, 2, 3 }) == std::vector<std::string>({ "/api/v1/players/stats?ids=1,2,3" }));
	{
		std::vector<int64_t> ids;
		for (int64_t id = 1; id <= 250; ++id)
		{
			ids.push_back(id);
		}
		const std::vector<std::string> paths = RestPlayerStatsBatchPaths(ids);
		CHECK(paths.size() == 3);
		if (paths.size() == 3)
		{
			CHECK(paths[0].compare(0, 33, "/api/v1/players/stats?ids=1,2,3,4") == 0);
			CHECK(paths[0].size() >= 4 && paths[0].compare(paths[0].size() - 4, 4, ",100") == 0);
			CHECK(paths[1].compare(0, 30, "/api/v1/players/stats?ids=101,") == 0);
			CHECK(paths[1].size() >= 4 && paths[1].compare(paths[1].size() - 4, 4, ",200") == 0);
			CHECK(paths[2].compare(0, 30, "/api/v1/players/stats?ids=201,") == 0);
			CHECK(paths[2].size() >= 4 && paths[2].compare(paths[2].size() - 4, 4, ",250") == 0);
		}
	}
	{
		PlayerStatsBatchResponse batch;
		CHECK(MakeRestOutcome(true, 200, ReadApiFixture(folder, "get-players-stats-ids.response.json")).Data(batch));
		CHECK(batch.stats.size() == 2 && batch.stats[0].user_id == 42 && batch.stats[1].user_id == 7);
		CHECK(!batch.stats.empty() && batch.stats[0].generals.wins.size() == 15 && batch.stats[0].generals.wins[2] == 5);
		PlayerStatsBody single;
		CHECK(MakeRestOutcome(true, 200, ReadApiFixture(folder, "get-players-stats.response.json")).Data(single));
		CHECK(single.user_id == 42 && single.elo_rating == 1184 && single.built_scud == 1);
	}

	// Paths reach the log with every id and query replaced.
	CHECK(RestPathForLog("/api/v1/players/42/stats") == "/api/v1/players/[redacted]/stats");
	CHECK(RestPathForLog("/api/v1/players/stats?ids=1,2,3") == "/api/v1/players/stats?[redacted]");
	CHECK(RestPathForLog("/api/v1/compatibility?client=genonline_zerohour&version=2026.11.3") == "/api/v1/compatibility?[redacted]");
	// The provider right after linked_accounts stays when it has the syntax of a provider name.
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/eos") == "/api/v1/me/linked_accounts/eos");
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/eos/sign_in_token") == "/api/v1/me/linked_accounts/eos/sign_in_token");
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/steam/sign_in_token") == "/api/v1/me/linked_accounts/steam/sign_in_token");
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/Secret-Token/sign_in_token") == "/api/v1/me/linked_accounts/[redacted]/sign_in_token");
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/gns") == "/api/v1/me/linked_accounts/[redacted]");
	CHECK(RestPathForLog("/api/v1/me/linked_accounts/eos/abc") == "/api/v1/me/linked_accounts/eos/[redacted]");
	CHECK(RestPathForLog("/api/v1/me/friend_requests/7/accept") == "/api/v1/me/friend_requests/[redacted]/accept");
	CHECK(RestPathForLog("/api/v1/messages/9b2f0c/unknownword") == "/api/v1/messages/[redacted]/[redacted]");
	CHECK(RestPathForLog("/api/v1/secret_token_abc") == "/api/v1/[redacted]");
	CHECK(RestPathForLog("/api/v1/config") == "/api/v1/config");
	CHECK(RestPathForLog("/.well-known/openid-configuration") == "/.well-known/openid-configuration");
	CHECK(RestPathForLog("/") == "/");
	CHECK(RestPathForLog("") == "/");
	CHECK(RestPathForLog("/api/v1/players/") == "/api/v1/players/");
}

static void TestConnectionTimeline()
{
	// Each client state maps to its report state, a relayed connection reports connected_relay.
	CHECK(std::string(ConnectionEventState(EConnectionState::CONNECTING_DIRECT, false)) == "connecting");
	CHECK(std::string(ConnectionEventState(EConnectionState::FINDING_ROUTE, false)) == "finding_route");
	CHECK(std::string(ConnectionEventState(EConnectionState::CONNECTED_DIRECT, false)) == "connected_direct");
	CHECK(std::string(ConnectionEventState(EConnectionState::CONNECTED_DIRECT, true)) == "connected_relay");
	CHECK(std::string(ConnectionEventState(EConnectionState::CONNECTION_FAILED, false)) == "failed");
	CHECK(std::string(ConnectionEventState(EConnectionState::CONNECTION_DISCONNECTED, true)) == "disconnected");
	CHECK(ConnectionEventState(EConnectionState::NOT_CONNECTED, false) == nullptr);

	// NOT_CONNECTED is not recorded, a repeated state is ignored.
	{
		ConnectionTimeline timeline;
		timeline.Add(EConnectionState::NOT_CONNECTED, false, 0);
		timeline.Add(EConnectionState::CONNECTING_DIRECT, false, 0);
		timeline.Add(EConnectionState::CONNECTING_DIRECT, false, 100);
		timeline.Add(EConnectionState::FINDING_ROUTE, false, 2400);
		timeline.Add(EConnectionState::NOT_CONNECTED, false, 2500);
		timeline.Add(EConnectionState::CONNECTED_DIRECT, true, 5100);
		ConnectionReportBody body;
		body.target_user_id = 7;
		body.outcome = "connected_relay";
		body.transport = "gns";
		body.ip_version = 4;
		body.attempt = 2;
		body.events = timeline.Events();
		CHECK(json(body) == json::parse(R"({"target_user_id":7,"outcome":"connected_relay","transport":"gns","ip_version":4,"attempt":2,"events":[{"state":"connecting","t_ms":0},{"state":"finding_route","t_ms":2400},{"state":"connected_relay","t_ms":5100}]})"));

		// The transport of a plugin attempt is the name its StartSignalling carried, and the attempt is always sent.
		ConnectionReportBody plugin;
		plugin.target_user_id = 7;
		plugin.outcome = "failed";
		plugin.transport = "steam";
		plugin.ip_version = 6;
		plugin.attempt = 2;
		CHECK(json(plugin) == json::parse(R"({"target_user_id":7,"outcome":"failed","transport":"steam","ip_version":6,"attempt":2})"));
		CHECK(ParseThrows<ConnectionReportBody>(json::parse(R"({"target_user_id":7,"outcome":"failed","transport":"gns","ip_version":4})")));
	}

	// The time never decreases and never exceeds 600000 ms.
	{
		ConnectionTimeline timeline;
		timeline.Add(EConnectionState::CONNECTING_DIRECT, false, -5);
		timeline.Add(EConnectionState::FINDING_ROUTE, false, 3000);
		timeline.Add(EConnectionState::CONNECTED_DIRECT, false, 1000);
		timeline.Add(EConnectionState::CONNECTION_DISCONNECTED, false, 900000);
		const std::vector<ConnectionReportEvent>& events = timeline.Events();
		CHECK(events.size() == 4);
		CHECK(events[0].t_ms == 0);
		CHECK(events[1].t_ms == 3000);
		CHECK(events[2].t_ms == 3000);
		CHECK(events[3].t_ms == 600000);
	}

	// A full timeline keeps the first 31 entries and the newest state last.
	{
		ConnectionTimeline timeline;
		for (int i = 0; i < 40; ++i)
		{
			timeline.Add(i % 2 == 0 ? EConnectionState::CONNECTING_DIRECT : EConnectionState::FINDING_ROUTE, false, i * 10);
		}
		timeline.Add(EConnectionState::CONNECTION_FAILED, false, 500);
		const std::vector<ConnectionReportEvent>& events = timeline.Events();
		CHECK(events.size() == 32);
		CHECK(events[30].state == "connecting");
		CHECK(events[30].t_ms == 300);
		CHECK(events[31].state == "failed");
		CHECK(events[31].t_ms == 500);

		// A newest state equal to the kept entry before the last leaves that entry last.
		timeline.Add(EConnectionState::CONNECTING_DIRECT, false, 600);
		CHECK(timeline.Events().size() == 31);
		CHECK(timeline.Events().back().state == "connecting");
		CHECK(timeline.Events().back().t_ms == 300);
	}
}

static void TestLobbyJoinHistory()
{
	typedef std::vector<std::string> Ids;
	const std::string lobby = "lobby:991";

	// The history of a join arrives before the JoinLobby Result, so it is held until the chat starts.
	PendingChannelHistory pending;
	pending.Hold(MakeHistory(lobby, { MakeChat("1", lobby), MakeChat("2", lobby) }));
	CHECK(pending.IsHolding(lobby));
	CHECK(!pending.IsHolding("lobby:992"));
	CHECK(!pending.IsHolding(""));

	// The chat starts with the Result. Live lines that come before the held history shows wait for it.
	ChannelChatLog log;
	log.Start(lobby);
	CHECK(log.AddLive(MakeChat("3", lobby)).empty());
	CHECK(log.AddLive(MakeChat("2", lobby)).empty());
	ChannelHistoryEvent history;
	CHECK(pending.Take(lobby, history));
	CHECK(!pending.IsHolding(lobby));
	const ChannelChatUpdate update = log.AddHistory(history);
	CHECK(LineIDs(update.shown) == Ids({ "1", "2", "3" }));
	CHECK(!update.shown[0].live && !update.shown[1].live && update.shown[2].live);
	// Each message shows once, also when the same history comes again.
	CHECK(log.AddHistory(MakeHistory(lobby, { MakeChat("1", lobby), MakeChat("2", lobby) })).shown.empty());
	CHECK(log.AddLive(MakeChat("3", lobby)).empty());
	CHECK(LineIDs(log.AddLive(MakeChat("4", lobby))) == Ids({ "4" }));

	// A held history of another lobby is dropped, and nothing is held twice.
	pending.Hold(MakeHistory("lobby:992", { MakeChat("9", "lobby:992") }));
	CHECK(!pending.Take(lobby, history));
	CHECK(!pending.IsHolding("lobby:992"));
	CHECK(!pending.Take("lobby:992", history));
	pending.Hold(MakeHistory(lobby, {}));
	pending.Clear();
	CHECK(!pending.IsHolding(lobby));
}

// The step fields of StartSignalling choose the transport of the pair, see section 6.6 of the protocol.
static void TestConnectionStep()
{
	const json full = json::parse(R"({"lobby_id":991,"user_id":7,"preferred_port":8088,"middleware_id":"0002a1b2c3d4e5f6","attempt":1,"transport":"gns","relay":false,"timeout_s":6})");
	StartSignallingEvent direct = full.get<StartSignallingEvent>();
	CHECK(json(direct) == full);
	ConnectionStep step = ConnectionStepOf(direct);
	CHECK(step.IsValid());
	CHECK(!step.IsPlugin());
	CHECK(step.attempt == 1);
	CHECK(step.transport == "gns");
	CHECK(!step.relay);
	CHECK(step.timeoutS == 6);

	// Every StartSignalling carries attempt, transport, relay and timeout_s, an event without one is malformed.
	for (const char* field : { "attempt", "transport", "relay", "timeout_s" })
	{
		json missing = full;
		missing.erase(field);
		CHECK(ParseThrows<StartSignallingEvent>(missing));
	}

	// A plugin step names the provider that both peers listed, the client matches it as an opaque name.
	StartSignallingEvent plugin = direct;
	plugin.attempt = 2;
	plugin.transport = "eos";
	plugin.timeout_s = 10;
	step = ConnectionStepOf(plugin);
	CHECK(step.IsValid());
	CHECK(step.IsPlugin());
	CHECK(step.attempt == 2);
	CHECK(step.transport == "eos");
	CHECK(step.timeoutS == 10);

	plugin.transport = "steam";
	step = ConnectionStepOf(plugin);
	CHECK(step.IsPlugin());
	CHECK(step.transport == "steam");

	StartSignallingEvent turn = direct;
	turn.attempt = 3;
	turn.relay = true;
	turn.timeout_s = 0;
	step = ConnectionStepOf(turn);
	CHECK(step.IsValid());
	CHECK(!step.IsPlugin());
	CHECK(step.relay);
	CHECK(step.timeoutS == 0);

	// A transport that is neither gns nor a plugin transport name, or an attempt below 1, gives an invalid step.
	for (const char* transport : { "", "EOS", "web-rtc", "eos ", "a23456789012345678901234567890123" })
	{
		StartSignallingEvent bad = direct;
		bad.transport = transport;
		CHECK(!ConnectionStepOf(bad).IsValid());
		CHECK(!ConnectionStepOf(bad).IsPlugin());
	}
	StartSignallingEvent zeroAttempt = direct;
	zeroAttempt.attempt = 0;
	CHECK(!ConnectionStepOf(zeroAttempt).IsValid());
	StartSignallingEvent negativeAttempt = direct;
	negativeAttempt.attempt = -1;
	CHECK(!ConnectionStepOf(negativeAttempt).IsValid());

	StartSignallingEvent negativeTimeout = direct;
	negativeTimeout.timeout_s = -4;
	CHECK(ConnectionStepOf(negativeTimeout).timeoutS == 0);

	RequestSignallingRequest restart;
	restart.target_user_id = 7;
	CHECK(json(restart).dump() == "{\"target_user_id\":7}");
	restart.failed_attempt = 1;
	CHECK(json(restart).dump() == "{\"failed_attempt\":1,\"target_user_id\":7}");

	// SetPeerTransports sends the name the plugin reports, or an empty list without a plugin transport.
	SetPeerTransportsRequest none;
	CHECK(json(none).dump() == "{\"transports\":[]}");
	SetPeerTransportsRequest one;
	one.transports.push_back("steam");
	CHECK(json(one).dump() == "{\"transports\":[\"steam\"]}");
}

// A provider name is 1 to 32 characters of a-z, 0-9 and _, and gns is reserved.
// The lobby list may carry the map path. A service without it still parses, with the path left out.
static void TestLobbySummaryMapPath()
{
	const json withPath = json::parse(R"j({"name":"Alpine Assault (2)","official":true,"path":"alpine assault.map"})j");
	LobbySummaryMap map = withPath.get<LobbySummaryMap>();
	CHECK(map.path.has_value() && *map.path == "alpine assault.map");
	CHECK(json(map) == withPath);

	const json withoutPath = json::parse(R"j({"name":"Alpine Assault (2)","official":true})j");
	LobbySummaryMap legacy = withoutPath.get<LobbySummaryMap>();
	CHECK(!legacy.path.has_value());
	CHECK(json(legacy) == withoutPath);
}

static void TestPluginTransportName()
{
	CHECK(IsPluginTransportName("eos"));
	CHECK(IsPluginTransportName("steam"));
	CHECK(IsPluginTransportName("a"));
	CHECK(IsPluginTransportName("p2p_2"));
	CHECK(IsPluginTransportName("a2345678901234567890123456789012"));
	CHECK(!IsPluginTransportName("a23456789012345678901234567890123"));
	CHECK(!IsPluginTransportName(""));
	CHECK(!IsPluginTransportName("gns"));
	CHECK(!IsPluginTransportName("Eos"));
	CHECK(!IsPluginTransportName("e-os"));
	CHECK(!IsPluginTransportName("e os"));
	CHECK(!IsPluginTransportName("../eos"));
	CHECK(!IsPluginTransportName(std::string("eos\0x", 5)));
}

// A timed step reports its attempt failed once, when it ends or runs out of time before it connects.
static void TestStepAttemptWatch()
{
	ConnectionStep direct;
	direct.attempt = 1;
	direct.relay = false;
	direct.timeoutS = 6;

	StepAttemptWatch watch;
	CHECK(!watch.IsOpen());
	CHECK(!watch.TakeTimeout(1000000));
	CHECK(!watch.TakeFailure());

	watch.Begin(direct, 1000);
	CHECK(watch.IsOpen());
	CHECK(watch.Attempt() == 1);
	CHECK(!watch.TakeTimeout(6999));
	CHECK(watch.TakeTimeout(7000));
	CHECK(watch.IsReported());
	CHECK(!watch.TakeTimeout(8000));
	CHECK(!watch.TakeFailure());
	CHECK(watch.IsOpen());

	watch.Begin(direct, 1000);
	CHECK(!watch.IsReported());
	CHECK(watch.TakeFailure());
	CHECK(!watch.TakeFailure());
	CHECK(!watch.TakeTimeout(100000));

	watch.Begin(direct, 1000);
	watch.MarkConnected();
	CHECK(!watch.IsOpen());
	CHECK(!watch.TakeTimeout(100000));
	CHECK(!watch.TakeFailure());

	ConnectionStep turn;
	turn.attempt = 3;
	turn.relay = true;
	turn.timeoutS = 0;
	watch.Begin(turn, 1000);
	CHECK(!watch.IsOpen());
	CHECK(!watch.TakeTimeout(1000000000));
	CHECK(!watch.TakeFailure());

	watch.Begin(ConnectionStep(), 1000);
	CHECK(!watch.IsOpen());
	CHECK(watch.Attempt() == 0);
}

static std::vector<uint8_t> HeldMessage(int value)
{
	return std::vector<uint8_t>(1, static_cast<uint8_t>(value));
}

static void TestAnticheatHoldQueue()
{
	AnticheatHoldQueue queue;
	CHECK(queue.Count(7) == 0);
	CHECK(queue.Take(7, 0).empty());

	// The messages of a peer leave in the order they arrived and only for that peer.
	CHECK(!queue.Push(7, HeldMessage(1), 1000));
	CHECK(!queue.Push(7, HeldMessage(2), 1001));
	CHECK(!queue.Push(8, HeldMessage(9), 1002));
	CHECK(queue.Count(7) == 2);
	std::vector<std::vector<uint8_t>> taken = queue.Take(7, 2000);
	CHECK(taken.size() == 2);
	CHECK(taken.size() == 2 && taken[0] == HeldMessage(1) && taken[1] == HeldMessage(2));
	CHECK(queue.Count(7) == 0);
	CHECK(queue.Count(8) == 1);
	CHECK(queue.Take(7, 2000).empty());

	// A full queue keeps its first messages, they hold the anticheat handshake, and drops the new one.
	for (int i = 0; i < 64; ++i)
	{
		CHECK(!queue.Push(7, HeldMessage(i), 3000));
	}
	CHECK(queue.Count(7) == 64);
	CHECK(queue.Push(7, HeldMessage(200), 3000));
	CHECK(queue.Count(7) == 64);
	taken = queue.Take(7, 3000);
	CHECK(taken.size() == 64);
	CHECK(!taken.empty() && taken.front() == HeldMessage(0) && taken.back() == HeldMessage(63));

	// A message older than 120 seconds is dropped, one exactly 120 seconds old is kept.
	CHECK(!queue.Push(7, HeldMessage(1), 10000));
	CHECK(!queue.Push(7, HeldMessage(2), 10001));
	CHECK(!queue.Push(7, HeldMessage(3), 50000));
	taken = queue.Take(7, 130001);
	CHECK(taken.size() == 2);
	CHECK(taken.size() == 2 && taken[0] == HeldMessage(2) && taken[1] == HeldMessage(3));

	// Old messages do not count toward the cap of a later push.
	for (int i = 0; i < 64; ++i)
	{
		CHECK(!queue.Push(7, HeldMessage(i), 0));
	}
	CHECK(!queue.Push(7, HeldMessage(100), 120001));
	CHECK(queue.Count(7) == 1);

	// Clear forgets one peer, ClearAll forgets every peer.
	CHECK(!queue.Push(9, HeldMessage(5), 120001));
	queue.Clear(7);
	CHECK(queue.Count(7) == 0);
	CHECK(queue.Count(8) == 1);
	CHECK(queue.Count(9) == 1);
	queue.ClearAll();
	CHECK(queue.Count(8) == 0);
	CHECK(queue.Count(9) == 0);
	CHECK(queue.Take(8, 120001).empty());
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		std::printf("usage: %s <realtime fixtures folder> <sign-in fixtures folder> <api fixtures folder>\n", argv[0]);
		return 2;
	}
	TestFixtures(argv[1]);
	TestSignInFixtures(argv[2]);
	TestApiFixtures(argv[3]);
	TestApiError();
	TestRestRetry();
	TestStandbyRetry();
	TestStandbyHubCalls();
	TestRestPaths();
	TestLegacyRouteReplacements(argv[3]);
	TestConnectionTimeline();
	TestLobbyJoinHistory();
	TestPkce();
	TestPeerPayload();
	TestMatchmakingText();
	TestChannelChatLog();
	TestLobbyChatReplay();
	TestDirectMessagePeer();
	TestDirectMessageUnread();
	TestDirectConversation();
	TestFriendRoster();
	TestLobbyStateStart();
	TestFormEncoding();
	TestLoopbackCallback();
	TestAcceptRealtimeUrl();
	TestLoopbackHttp();
	TestDiscovery();
	TestHandshake();
	TestParseRecord();
	TestSerializeInvocation();
	TestRecordSplitter();
	TestInvocationTracker();
	TestSecondsToMilliseconds();
	TestFieldPresence();
	TestHubSessionConnect();
	TestHubSessionInvoke();
	TestHubSessionEvents();
	TestHubSessionKeepAlive();
	TestHubSessionClose();
	TestReconnectPolicy();
	TestServerSwitching();
	TestConnectRefusals();
	TestStandbyRefusalDelay();
	TestModerationText();
	TestConnectionStep();
	TestPluginTransportName();
	TestLobbySummaryMapPath();
	TestStepAttemptWatch();
	TestAnticheatHoldQueue();

	if (g_failures != 0)
	{
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
