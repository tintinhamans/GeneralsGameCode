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

#include "GameNetwork/GenOnline/Realtime/RealtimeHubSession.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeProtocol.h"
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

static std::map<std::string, RoundTripFn> BuildFixtureTable()
{
	std::map<std::string, RoundTripFn> t;

	t["Result.ok.json"] = RoundTrip<Result>;
	t["Result.error.json"] = RoundTrip<Result>;

	t["JoinRoom.request.json"] = RoundTrip<JoinRoomRequest>;
	t["LeaveRoom.request.json"] = RoundTrip<LeaveRoomRequest>;
	t["SendMessage.request.json"] = RoundTrip<SendMessageRequest>;
	t["SendMessage.result.json"] = ResultRoundTrip<SendMessageResult>;
	t["SetDisplayName.request.json"] = RoundTrip<SetDisplayNameRequest>;

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
	t["ReportMatchOutcome.request.json"] = RoundTrip<ReportMatchOutcomeRequest>;
	t["ReportMatchOutcome.result.json"] = ResultRoundTrip<ReportMatchOutcomeResult>;

	t["GetPlaylists.request.json"] = RoundTrip<GetPlaylistsRequest>;
	t["GetPlaylists.result.json"] = ResultRoundTrip<GetPlaylistsResult>;
	t["JoinQueue.request.json"] = RoundTrip<JoinQueueRequest>;
	t["LeaveQueue.request.json"] = RoundTrip<LeaveQueueRequest>;
	t["WidenSearch.request.json"] = RoundTrip<WidenSearchRequest>;

	t["SendFriendRequest.request.json"] = RoundTrip<SendFriendRequestRequest>;
	t["AcceptFriendRequest.request.json"] = RoundTrip<AcceptFriendRequestRequest>;
	t["DeclineFriendRequest.request.json"] = RoundTrip<DeclineFriendRequestRequest>;
	t["CancelFriendRequest.request.json"] = RoundTrip<CancelFriendRequestRequest>;
	t["RemoveFriend.request.json"] = RoundTrip<RemoveFriendRequest>;
	t["BlockUser.request.json"] = RoundTrip<BlockUserRequest>;
	t["UnblockUser.request.json"] = RoundTrip<UnblockUserRequest>;

	t["Kick.request.json"] = RoundTrip<KickRequest>;
	t["Mute.request.json"] = RoundTrip<MuteRequest>;
	t["Unmute.request.json"] = RoundTrip<UnmuteRequest>;
	t["Ban.request.json"] = RoundTrip<BanRequest>;
	t["Unban.request.json"] = RoundTrip<UnbanRequest>;

	t["RequestSignalling.request.json"] = RoundTrip<RequestSignallingRequest>;
	t["Signal.request.json"] = RoundTrip<SignalRequest>;
	t["BeginMeshCheck.request.json"] = RoundTrip<BeginMeshCheckRequest>;
	t["MeshCheckResponse.request.json"] = RoundTrip<MeshCheckResponseRequest>;
	t["AnticheatMessage.request.json"] = RoundTrip<AnticheatMessageRequest>;
	t["ProbeResponse.request.json"] = RoundTrip<ProbeResponseRequest>;
	t["ModuleScanResponse.request.json"] = RoundTrip<ModuleScanResponseRequest>;

	t["Ready.event.json"] = RoundTrip<ReadyEvent>;
	t["BadgesUpdate.event.json"] = RoundTrip<BadgesUpdateEvent>;
	t["Notice.event.json"] = RoundTrip<Notice>;
	t["ModerationNotice.event.json"] = RoundTrip<ModerationNoticeEvent>;

	t["RoomMembers.event.json"] = RoundTrip<RoomMembersEvent>;
	t["MemberAdd.event.json"] = RoundTrip<MemberAddEvent>;
	t["MemberRemove.event.json"] = RoundTrip<MemberRemoveEvent>;
	t["MemberUpdate.event.json"] = RoundTrip<MemberUpdateEvent>;
	t["MessageCreate.event.json"] = RoundTrip<MessageCreateEvent>;

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
	t["AnticheatMessage.event.json"] = RoundTrip<AnticheatMessageEvent>;
	t["ProbeRequest.event.json"] = RoundTrip<ProbeRequestEvent>;
	t["ModuleScanRequest.event.json"] = RoundTrip<ModuleScanRequestEvent>;

	return t;
}

static void TestFixtures(const std::filesystem::path& folder)
{
	std::map<std::string, RoundTripFn> table = BuildFixtureTable();
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
	std::printf("fixtures checked: %zu\n", seen.size());
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

	CHECK(ParseRecord("{\"type\":1,\"target\":\"MessageCreate\",\"arguments\":[{\"id\":\"8812\",\"channel_id\":\"room:3\",\"type\":\"chat\",\"author\":{\"id\":42,\"name\":\"Bob\",\"badges\":[]},\"content\":\"hi\"}]}", record, error));
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
	request.channel_id = "room:3";
	request.type = "chat";
	request.content = "hi";
	std::string withResult = SerializeInvocation(SendMessageRequest::METHOD, json(request), "5");
	CHECK(withResult.back() == RECORD_SEPARATOR);
	json parsed = json::parse(withResult.substr(0, withResult.size() - 1));
	CHECK(parsed == json::parse("{\"type\":1,\"invocationId\":\"5\",\"target\":\"SendMessage\",\"arguments\":[{\"channel_id\":\"room:3\",\"type\":\"chat\",\"content\":\"hi\"}]}"));

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
	bool threw = false;
	try
	{
		json::parse("{\"new_field\":true}").get<MatchStartEvent>();
	}
	catch (const std::exception&)
	{
		threw = true;
	}
	CHECK(threw);

	// A null optional field reads as left out.
	ModerationNoticeEvent notice = json::parse("{\"action\":\"kick\",\"reason\":\"r\",\"until\":null}").get<ModerationNoticeEvent>();
	CHECK(!notice.until.has_value());
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
		json ready = json::parse("{\"protocol_version\":1,\"session_type\":\"game\",\"user\":{\"id\":42,\"name\":\"Bob\",\"badges\":[]},\"permissions\":[],\"badges\":{},\"rooms\":[],\"limits\":{\"chat_max_length\":512,\"chat_rate\":{\"messages\":3,\"window_ms\":9000},\"name_min_length\":3,\"name_max_length\":16}}");
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
	request.channel_id = "room:3";
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
	FormFields revoke = RevokeTokenForm("gen_online_60hz", "rt");
	CHECK(FormToJson(revoke) == json::parse("{\"client_id\":\"gen_online_60hz\",\"token\":\"rt\",\"token_type_hint\":\"refresh_token\"}"));
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
	CHECK(OriginOfBase("http://localhost/").empty());

	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/-/realtime", base) == "wss://api.playgenerals.online:2087/-/realtime");
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/ws", base) == "wss://api.playgenerals.online:2087/ws");

	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online/-/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087.evil.example/-/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:20870/-/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("ws://api.playgenerals.online:2087/-/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://evil.example/-/realtime", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/-/realtime?access_token=x", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/a/../b", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/%2e%2e/b", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087", base).empty());
	CHECK(AcceptRealtimeUrl("wss://api.playgenerals.online:2087/-/realtime", "http://api.playgenerals.online:2087/").empty());
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
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: %s <realtime fixtures folder> <sign-in fixtures folder>\n", argv[0]);
		return 2;
	}
	TestFixtures(argv[1]);
	TestSignInFixtures(argv[2]);
	TestPkce();
	TestPeerPayload();
	TestMatchmakingText();
	TestFormEncoding();
	TestLoopbackCallback();
	TestAcceptRealtimeUrl();
	TestDiscovery();
	TestHandshake();
	TestParseRecord();
	TestSerializeInvocation();
	TestRecordSplitter();
	TestInvocationTracker();
	TestFieldPresence();
	TestHubSessionConnect();
	TestHubSessionInvoke();
	TestHubSessionEvents();
	TestHubSessionKeepAlive();
	TestHubSessionClose();
	TestReconnectPolicy();

	if (g_failures != 0)
	{
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
