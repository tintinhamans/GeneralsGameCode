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

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeProtocol.h"

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

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: %s <fixtures folder>\n", argv[0]);
		return 2;
	}
	TestFixtures(argv[1]);
	TestHandshake();
	TestParseRecord();
	TestSerializeInvocation();
	TestRecordSplitter();
	TestInvocationTracker();
	TestFieldPresence();

	if (g_failures != 0)
	{
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
