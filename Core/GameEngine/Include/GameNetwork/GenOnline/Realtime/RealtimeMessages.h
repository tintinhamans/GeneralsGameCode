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

// FILE: RealtimeMessages.h ///////////////////////////////////////////////////
// Description: Typed arguments, results and events of the GenOnline realtime
//              hub, protocol version 1. Field names are the snake_case wire
//              names. Enum values stay lowercase strings, so values added by a
//              newer service still parse. Each request names its hub method in
//              METHOD and says in EXPECTS_RESULT whether it waits for a Result.
//              Each event names itself in EVENT. The contract fixtures of the
//              service pin every shape here.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/json.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace GenOnlineRealtime
{

constexpr int PROTOCOL_VERSION = 1;

// Converts a wire duration in whole seconds to milliseconds, saturating instead of overflowing.
inline int64_t SecondsToMilliseconds(int64_t seconds)
{
	constexpr int64_t LIMIT = INT64_MAX / 1000;
	return (std::max)((std::min)(seconds, LIMIT), -LIMIT) * 1000;
}

// Base of every object on the wire. Each derived type lists its fields in Fields().
struct WireObject
{
};

// A field that is always sent and can be null. An absent field reads as null.
template <class T>
struct Nullable
{
	bool isNull = true;
	T value = T();

	Nullable() {}
	Nullable(const T& v) : isNull(false), value(v) {}
};

// std::optional fields are left out when empty. An absent or null field reads as empty.
// std::optional<Nullable<T>> fields tell an absent field, null and a value apart.

namespace Detail
{

template <class T>
void WriteValue(nlohmann::json& node, const T& value);

template <class T>
void WriteValue(nlohmann::json& node, const Nullable<T>& value)
{
	if (value.isNull)
		node = nullptr;
	else
		WriteValue(node, value.value);
}

template <class T>
void WriteValue(nlohmann::json& node, const std::optional<T>& value)
{
	WriteValue(node, *value);
}

template <class T>
void WriteValue(nlohmann::json& node, const T& value)
{
	node = value;
}

template <class T>
bool IsLeftOut(const T&)
{
	return false;
}

template <class T>
bool IsLeftOut(const std::optional<T>& value)
{
	return !value.has_value();
}

template <class T>
void ReadValue(const nlohmann::json& node, T& value);

template <class T>
void ReadValue(const nlohmann::json& node, Nullable<T>& value)
{
	if (node.is_null())
	{
		value = Nullable<T>();
		return;
	}
	value.isNull = false;
	ReadValue(node, value.value);
}

template <class T>
void ReadValue(const nlohmann::json& node, std::optional<T>& value)
{
	value.emplace();
	ReadValue(node, *value);
}

template <class T>
void ReadValue(const nlohmann::json& node, T& value)
{
	node.get_to(value);
}

template <class T>
bool ReadAbsent(T&)
{
	return false;
}

template <class T>
bool ReadAbsent(Nullable<T>& value)
{
	value = Nullable<T>();
	return true;
}

template <class T>
bool ReadAbsent(std::optional<T>& value)
{
	value.reset();
	return true;
}

template <class T>
bool IsPlainOptional(const T&)
{
	return false;
}

template <class T>
bool IsPlainOptional(const std::optional<T>&)
{
	return true;
}

template <class T>
bool IsPlainOptional(const std::optional<Nullable<T> >&)
{
	return false;
}

class Writer
{
public:
	explicit Writer(nlohmann::json& j) : m_json(j) {}

	template <class T>
	void operator()(const char* name, const T& value)
	{
		if (IsLeftOut(value))
			return;
		WriteValue(m_json[name], value);
	}

private:
	nlohmann::json& m_json;
};

class Reader
{
public:
	explicit Reader(const nlohmann::json& j) : m_json(j) {}

	template <class T>
	void operator()(const char* name, T& value)
	{
		auto it = m_json.find(name);
		if (it == m_json.end() || (it->is_null() && IsPlainOptional(value)))
		{
			if (!ReadAbsent(value))
				throw std::runtime_error(std::string("missing field ") + name);
			return;
		}
		ReadValue(*it, value);
	}

private:
	const nlohmann::json& m_json;
};

} // namespace Detail

template <class T, typename std::enable_if<std::is_base_of<WireObject, T>::value, int>::type = 0>
void to_json(nlohmann::json& j, const T& object)
{
	j = nlohmann::json::object();
	Detail::Writer writer(j);
	const_cast<T&>(object).Fields(writer);
}

template <class T, typename std::enable_if<std::is_base_of<WireObject, T>::value, int>::type = 0>
void from_json(const nlohmann::json& j, T& object)
{
	if (!j.is_object())
		throw std::runtime_error("expected an object");
	Detail::Reader reader(j);
	object.Fields(reader);
}

// Shared objects ///////////////////////////////////////////////////////////////

struct EmptyObject : WireObject
{
	template <class V> void Fields(V&) {}
};

// A command answer. data is set when ok is true, code and params when it is false.
struct Result : WireObject
{
	bool ok = false;
	std::optional<nlohmann::json> data;
	std::optional<std::string> code;
	std::optional<nlohmann::json> params;

	template <class V> void Fields(V& v)
	{
		v("ok", ok);
		v("data", data);
		v("code", code);
		v("params", params);
	}
};

struct Member : WireObject
{
	int64_t id = 0;
	std::string name;
	std::vector<std::string> badges;
	// "game", "launcher" or "chat". Member lists carry it, chat messages leave it out.
	std::optional<std::string> client;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("badges", badges);
		v("client", client);
	}
};

struct BadgeDefinition : WireObject
{
	std::string text;
	std::string color;

	template <class V> void Fields(V& v)
	{
		v("text", text);
		v("color", color);
	}
};

typedef std::map<std::string, BadgeDefinition> BadgeMap;

// A message deleted by staff is a tombstone with deleted set and no type, author or content.
struct ChatMessage : WireObject
{
	std::string id;
	std::string channel_id;
	// "chat", "action" or "announcement".
	std::string type;
	Member author;
	std::string content;
	std::string created_at;
	std::optional<bool> deleted;

	bool IsDeleted() const { return deleted.value_or(false); }

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("channel_id", channel_id);
		v("created_at", created_at);
		v("deleted", deleted);
		if (IsDeleted())
		{
			return;
		}
		v("type", type);
		v("author", author);
		v("content", content);
	}
};

struct Group : WireObject
{
	int64_t id = 0;
	// Null when the owner set no name, clients then show the member names.
	Nullable<std::string> name;
	int64_t owner_id = 0;
	// Every member, the owner included, in the order they joined.
	std::vector<Member> members;
	std::string created_at;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("owner_id", owner_id);
		v("members", members);
		v("created_at", created_at);
	}
};

struct Notice : WireObject
{
	static constexpr const char* EVENT = "Notice";

	std::string code;
	nlohmann::json params = nlohmann::json::object();

	template <class V> void Fields(V& v)
	{
		v("code", code);
		v("params", params);
	}
};

struct MapReference : WireObject
{
	std::string name;
	std::string path;
	bool official = false;

	template <class V> void Fields(V& v)
	{
		v("name", name);
		v("path", path);
		v("official", official);
	}
};

struct LobbySummaryMap : WireObject
{
	std::string name;
	bool official = false;

	template <class V> void Fields(V& v)
	{
		v("name", name);
		v("official", official);
	}
};

struct LobbySummarySettings : WireObject
{
	int starting_cash = 0;
	bool limit_superweapons = false;
	bool vanilla_teams = false;
	bool track_stats = false;
	bool allow_observers = false;

	template <class V> void Fields(V& v)
	{
		v("starting_cash", starting_cash);
		v("limit_superweapons", limit_superweapons);
		v("vanilla_teams", vanilla_teams);
		v("track_stats", track_stats);
		v("allow_observers", allow_observers);
	}
};

struct LobbySettings : LobbySummarySettings
{
	int max_camera_height = 0;

	template <class V> void Fields(V& v)
	{
		LobbySummarySettings::Fields(v);
		v("max_camera_height", max_camera_height);
	}
};

struct BuildIdentity : WireObject
{
	uint32_t exe_crc = 0;
	uint32_t ini_crc = 0;

	template <class V> void Fields(V& v)
	{
		v("exe_crc", exe_crc);
		v("ini_crc", ini_crc);
	}
};

struct LobbySummary : WireObject
{
	int64_t id = 0;
	std::string name;
	Member host;
	// "setup" or "countdown".
	std::string state;
	LobbySummaryMap map;
	int players = 0;
	int max_players = 0;
	bool has_password = false;
	std::string region;
	int latency_ms = 0;
	// "none", "go_integrated" or "easy_anticheat".
	std::string anticheat;
	LobbySummarySettings settings;
	BuildIdentity build;
	std::string created_at;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("host", host);
		v("state", state);
		v("map", map);
		v("players", players);
		v("max_players", max_players);
		v("has_password", has_password);
		v("region", region);
		v("latency_ms", latency_ms);
		v("anticheat", anticheat);
		v("settings", settings);
		v("build", build);
		v("created_at", created_at);
	}
};

struct LobbySlot : WireObject
{
	int index = 0;
	// "open", "closed", "player", "ai_easy", "ai_medium" or "ai_brutal".
	std::string state;
	std::optional<Member> member;
	bool ready = false;
	bool has_map = false;
	// Game ids, -1 is random.
	int side = -1;
	int color = -1;
	Nullable<int> team;
	int start_position = -1;

	template <class V> void Fields(V& v)
	{
		v("index", index);
		v("state", state);
		v("member", member);
		v("ready", ready);
		v("has_map", has_map);
		v("side", side);
		v("color", color);
		v("team", team);
		v("start_position", start_position);
	}
};

// The start data of GameStart, so a client that reconnects after the start still starts.
struct LobbyStart : WireObject
{
	int64_t match_id = 0;
	int64_t rng_seed = 0;
	Nullable<std::string> screenshot_url;

	template <class V> void Fields(V& v)
	{
		v("match_id", match_id);
		v("rng_seed", rng_seed);
		v("screenshot_url", screenshot_url);
	}
};

struct LobbyState : WireObject
{
	int64_t id = 0;
	std::string name;
	int64_t host_id = 0;
	// "custom" or "quick_match".
	std::string type;
	// "setup", "countdown", "in_game" or "complete".
	std::string state;
	// "public" or "friends".
	std::string visibility;
	bool has_password = false;
	MapReference map;
	std::string region;
	std::string anticheat;
	LobbySettings settings;
	Nullable<int64_t> match_id;
	// Null until the game starts.
	Nullable<LobbyStart> start;
	std::string created_at;
	std::vector<LobbySlot> slots;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("host_id", host_id);
		v("type", type);
		v("state", state);
		v("visibility", visibility);
		v("has_password", has_password);
		v("map", map);
		v("region", region);
		v("anticheat", anticheat);
		v("settings", settings);
		v("match_id", match_id);
		v("start", start);
		v("created_at", created_at);
		v("slots", slots);
	}
};

struct PlaylistMap : WireObject
{
	int index = 0;
	std::string name;
	std::string path;
	bool custom = false;
	int max_players = 0;

	template <class V> void Fields(V& v)
	{
		v("index", index);
		v("name", name);
		v("path", path);
		v("custom", custom);
		v("max_players", max_players);
	}
};

struct Playlist : WireObject
{
	int id = 0;
	std::string name;
	int min_players = 0;
	int desired_players = 0;
	int min_selected_maps = 0;
	bool allow_teams = false;
	Nullable<int> team_size;
	bool allow_army_selection = false;
	int64_t grace_period_s = 0;
	std::vector<PlaylistMap> maps;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("min_players", min_players);
		v("desired_players", desired_players);
		v("min_selected_maps", min_selected_maps);
		v("allow_teams", allow_teams);
		v("team_size", team_size);
		v("allow_army_selection", allow_army_selection);
		v("grace_period_s", grace_period_s);
		v("maps", maps);
	}
};

struct TurnCredentials : WireObject
{
	std::string username;
	std::string token;

	template <class V> void Fields(V& v)
	{
		v("username", username);
		v("token", token);
	}
};

// Requests: channels and chat ///////////////////////////////////////////////////

// A parent channel resolves to its default child.
struct JoinChannelRequest : WireObject
{
	static constexpr const char* METHOD = "JoinChannel";
	static constexpr bool EXPECTS_RESULT = true;

	int64_t channel_id = 0;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
	}
};

struct LeaveChannelRequest : EmptyObject
{
	static constexpr const char* METHOD = "LeaveChannel";
	static constexpr bool EXPECTS_RESULT = true;
};

struct SendMessageRequest : WireObject
{
	static constexpr const char* METHOD = "SendMessage";
	static constexpr bool EXPECTS_RESULT = true;

	// "channel:<id>", "lobby:<id>", "dm:<user id>" or "group:<id>".
	std::string channel_id;
	std::string type;
	std::string content;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("type", type);
		v("content", content);
	}
};

struct SendMessageResult : WireObject
{
	std::string message_id;

	template <class V> void Fields(V& v)
	{
		v("message_id", message_id);
	}
};

// Without before, the newest messages. The messages come oldest first.
struct GetHistoryRequest : WireObject
{
	static constexpr const char* METHOD = "GetHistory";
	static constexpr bool EXPECTS_RESULT = true;

	std::string channel_id;
	std::optional<std::string> before;
	int limit = 0;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("before", before);
		v("limit", limit);
	}
};

struct GetHistoryResult : WireObject
{
	std::vector<ChatMessage> messages;
	bool has_more = false;

	template <class V> void Fields(V& v)
	{
		v("messages", messages);
		v("has_more", has_more);
	}
};

struct ChannelMessageArgument : WireObject
{
	std::string channel_id;
	std::string message_id;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("message_id", message_id);
	}
};

// A whole direct or group conversation.
struct ConversationArgument : WireObject
{
	std::string channel_id;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
	}
};

// Marks the whole conversation read, up to the newest message the service holds.
struct MarkReadRequest : ConversationArgument
{
	static constexpr const char* METHOD = "MarkRead";
	static constexpr bool EXPECTS_RESULT = true;
};

// Requests: lobbies /////////////////////////////////////////////////////////////

struct WatchLobbyListRequest : EmptyObject
{
	static constexpr const char* METHOD = "WatchLobbyList";
	static constexpr bool EXPECTS_RESULT = true;
};

struct WatchLobbyListResult : WireObject
{
	std::vector<LobbySummary> lobbies;

	template <class V> void Fields(V& v)
	{
		v("lobbies", lobbies);
	}
};

struct UnwatchLobbyListRequest : EmptyObject
{
	static constexpr const char* METHOD = "UnwatchLobbyList";
	static constexpr bool EXPECTS_RESULT = true;
};

struct CreateLobbyRequest : WireObject
{
	static constexpr const char* METHOD = "CreateLobby";
	static constexpr bool EXPECTS_RESULT = true;

	std::string name;
	MapReference map;
	int max_players = 0;
	std::string visibility;
	Nullable<std::string> password;
	int preferred_port = 0;
	LobbySettings settings;

	template <class V> void Fields(V& v)
	{
		v("name", name);
		v("map", map);
		v("max_players", max_players);
		v("visibility", visibility);
		v("password", password);
		v("preferred_port", preferred_port);
		v("settings", settings);
	}
};

struct CreateLobbyResult : WireObject
{
	int64_t lobby_id = 0;
	TurnCredentials turn;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("turn", turn);
	}
};

struct JoinLobbyRequest : WireObject
{
	static constexpr const char* METHOD = "JoinLobby";
	static constexpr bool EXPECTS_RESULT = true;

	int64_t lobby_id = 0;
	Nullable<std::string> password;
	int preferred_port = 0;
	bool has_map = false;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("password", password);
		v("preferred_port", preferred_port);
		v("has_map", has_map);
	}
};

struct JoinLobbyResult : WireObject
{
	TurnCredentials turn;

	template <class V> void Fields(V& v)
	{
		v("turn", turn);
	}
};

struct LeaveLobbyRequest : EmptyObject
{
	static constexpr const char* METHOD = "LeaveLobby";
	static constexpr bool EXPECTS_RESULT = true;
};

struct UpdateLobbyMap : WireObject
{
	std::string name;
	std::string path;
	bool official = false;
	int max_players = 0;

	template <class V> void Fields(V& v)
	{
		v("name", name);
		v("path", path);
		v("official", official);
		v("max_players", max_players);
	}
};

struct UpdateLobbySettings : WireObject
{
	std::optional<int> starting_cash;
	std::optional<bool> limit_superweapons;
	std::optional<int> max_camera_height;

	template <class V> void Fields(V& v)
	{
		v("starting_cash", starting_cash);
		v("limit_superweapons", limit_superweapons);
		v("max_camera_height", max_camera_height);
	}
};

// Only the fields that are present change.
struct UpdateLobbyRequest : WireObject
{
	static constexpr const char* METHOD = "UpdateLobby";
	static constexpr bool EXPECTS_RESULT = true;

	std::optional<UpdateLobbyMap> map;
	std::optional<UpdateLobbySettings> settings;
	std::optional<std::string> visibility;

	template <class V> void Fields(V& v)
	{
		v("map", map);
		v("settings", settings);
		v("visibility", visibility);
	}
};

// A null password removes it.
struct SetLobbyPasswordRequest : WireObject
{
	static constexpr const char* METHOD = "SetLobbyPassword";
	static constexpr bool EXPECTS_RESULT = true;

	Nullable<std::string> password;

	template <class V> void Fields(V& v)
	{
		v("password", password);
	}
};

// Only the fields that are present change. A null team removes the slot from its team.
struct UpdateMySlotRequest : WireObject
{
	static constexpr const char* METHOD = "UpdateMySlot";
	static constexpr bool EXPECTS_RESULT = true;

	std::optional<int> side;
	std::optional<int> color;
	std::optional<Nullable<int> > team;
	std::optional<int> start_position;

	template <class V> void Fields(V& v)
	{
		v("side", side);
		v("color", color);
		v("team", team);
		v("start_position", start_position);
	}
};

struct SetHasMapRequest : WireObject
{
	static constexpr const char* METHOD = "SetHasMap";
	static constexpr bool EXPECTS_RESULT = true;

	bool has_map = false;

	template <class V> void Fields(V& v)
	{
		v("has_map", has_map);
	}
};

struct SetReadyRequest : WireObject
{
	static constexpr const char* METHOD = "SetReady";
	static constexpr bool EXPECTS_RESULT = true;

	bool ready = false;

	template <class V> void Fields(V& v)
	{
		v("ready", ready);
	}
};

struct SetSlotStateRequest : WireObject
{
	static constexpr const char* METHOD = "SetSlotState";
	static constexpr bool EXPECTS_RESULT = true;

	int index = 0;
	std::string state;

	template <class V> void Fields(V& v)
	{
		v("index", index);
		v("state", state);
	}
};

struct SlotUpdate : WireObject
{
	int index = 0;
	std::optional<int> side;
	std::optional<int> color;
	std::optional<Nullable<int> > team;
	std::optional<int> start_position;

	template <class V> void Fields(V& v)
	{
		v("index", index);
		v("side", side);
		v("color", color);
		v("team", team);
		v("start_position", start_position);
	}
};

struct UpdateSlotsRequest : WireObject
{
	static constexpr const char* METHOD = "UpdateSlots";
	static constexpr bool EXPECTS_RESULT = true;

	std::vector<SlotUpdate> slots;

	template <class V> void Fields(V& v)
	{
		v("slots", slots);
	}
};

struct UserIdArgument : WireObject
{
	int64_t user_id = 0;

	template <class V> void Fields(V& v)
	{
		v("user_id", user_id);
	}
};

struct KickFromLobbyRequest : UserIdArgument
{
	static constexpr const char* METHOD = "KickFromLobby";
	static constexpr bool EXPECTS_RESULT = true;
};

struct ForceReadyRequest : EmptyObject
{
	static constexpr const char* METHOD = "ForceReady";
	static constexpr bool EXPECTS_RESULT = true;
};

struct StartGameRequest : EmptyObject
{
	static constexpr const char* METHOD = "StartGame";
	static constexpr bool EXPECTS_RESULT = true;
};

struct MatchStats : WireObject
{
	int buildings_built = 0;
	int buildings_killed = 0;
	int buildings_lost = 0;
	int units_built = 0;
	int units_killed = 0;
	int units_lost = 0;
	int64_t total_money = 0;

	template <class V> void Fields(V& v)
	{
		v("buildings_built", buildings_built);
		v("buildings_killed", buildings_killed);
		v("buildings_lost", buildings_lost);
		v("units_built", units_built);
		v("units_killed", units_killed);
		v("units_lost", units_lost);
		v("total_money", total_money);
	}
};

// Requests: matchmaking /////////////////////////////////////////////////////////

struct JoinQueueRequest : WireObject
{
	static constexpr const char* METHOD = "JoinQueue";
	static constexpr bool EXPECTS_RESULT = true;

	int playlist_id = 0;
	std::vector<int> map_indexes;

	template <class V> void Fields(V& v)
	{
		v("playlist_id", playlist_id);
		v("map_indexes", map_indexes);
	}
};

struct LeaveQueueRequest : EmptyObject
{
	static constexpr const char* METHOD = "LeaveQueue";
	static constexpr bool EXPECTS_RESULT = true;
};

struct WidenSearchRequest : EmptyObject
{
	static constexpr const char* METHOD = "WidenSearch";
	static constexpr bool EXPECTS_RESULT = true;
};

// Requests: peer networking /////////////////////////////////////////////////////

struct RequestSignallingRequest : WireObject
{
	static constexpr const char* METHOD = "RequestSignalling";
	static constexpr bool EXPECTS_RESULT = false;

	int64_t target_user_id = 0;

	template <class V> void Fields(V& v)
	{
		v("target_user_id", target_user_id);
	}
};

// Encodes a peer payload as standard padded base64.
inline std::string EncodePeerPayload(const std::vector<uint8_t>& data)
{
	static const char* const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (size_t i = 0; i < data.size(); i += 3)
	{
		const size_t remaining = data.size() - i;
		uint32_t n = uint32_t(data[i]) << 16;
		if (remaining > 1)
		{
			n |= uint32_t(data[i + 1]) << 8;
		}
		if (remaining > 2)
		{
			n |= uint32_t(data[i + 2]);
		}
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += remaining > 1 ? ALPHABET[(n >> 6) & 63] : '=';
		out += remaining > 2 ? ALPHABET[n & 63] : '=';
	}
	return out;
}

// Decodes standard padded base64. Returns false for any other text.
inline bool DecodePeerPayload(const std::string& text, std::vector<uint8_t>& data)
{
	data.clear();
	if (text.size() % 4 != 0)
	{
		return false;
	}
	data.reserve(text.size() / 4 * 3);
	for (size_t i = 0; i < text.size(); i += 4)
	{
		const bool bLast = i + 4 == text.size();
		uint32_t n = 0;
		int padding = 0;
		for (size_t j = 0; j < 4; ++j)
		{
			const char c = text[i + j];
			int value = -1;
			if (c >= 'A' && c <= 'Z')
			{
				value = c - 'A';
			}
			else if (c >= 'a' && c <= 'z')
			{
				value = c - 'a' + 26;
			}
			else if (c >= '0' && c <= '9')
			{
				value = c - '0' + 52;
			}
			else if (c == '+')
			{
				value = 62;
			}
			else if (c == '/')
			{
				value = 63;
			}
			else if (c == '=' && bLast && j >= 2)
			{
				value = 0;
				++padding;
			}
			if (value < 0 || (padding > 0 && c != '='))
			{
				data.clear();
				return false;
			}
			n = (n << 6) | uint32_t(value);
		}
		data.push_back(uint8_t(n >> 16));
		if (padding < 2)
		{
			data.push_back(uint8_t(n >> 8));
		}
		if (padding < 1)
		{
			data.push_back(uint8_t(n));
		}
	}
	return true;
}

// Payloads are base64 strings.
struct PeerPayloadRequest : WireObject
{
	int64_t to = 0;
	std::string payload;

	template <class V> void Fields(V& v)
	{
		v("to", to);
		v("payload", payload);
	}
};

struct SignalRequest : PeerPayloadRequest
{
	static constexpr const char* METHOD = "Signal";
	static constexpr bool EXPECTS_RESULT = false;
};

struct AnticheatMessageRequest : PeerPayloadRequest
{
	static constexpr const char* METHOD = "AnticheatMessage";
	static constexpr bool EXPECTS_RESULT = false;
};

struct BeginMeshCheckRequest : EmptyObject
{
	static constexpr const char* METHOD = "BeginMeshCheck";
	static constexpr bool EXPECTS_RESULT = true;
};

struct MeshCheckResponseRequest : WireObject
{
	static constexpr const char* METHOD = "MeshCheckResponse";
	static constexpr bool EXPECTS_RESULT = false;

	int64_t mesh_check_id = 0;
	int attempt = 0;
	std::vector<int64_t> connected;
	std::vector<int64_t> connecting;

	template <class V> void Fields(V& v)
	{
		v("mesh_check_id", mesh_check_id);
		v("attempt", attempt);
		v("connected", connected);
		v("connecting", connecting);
	}
};

struct ProbeResponseRequest : WireObject
{
	static constexpr const char* METHOD = "ProbeResponse";
	static constexpr bool EXPECTS_RESULT = false;

	std::string probe_id;

	template <class V> void Fields(V& v)
	{
		v("probe_id", probe_id);
	}
};

struct ScannedModule : WireObject
{
	std::string path;
	int64_t size = 0;

	template <class V> void Fields(V& v)
	{
		v("path", path);
		v("size", size);
	}
};

struct ModuleScanResponseRequest : WireObject
{
	static constexpr const char* METHOD = "ModuleScanResponse";
	static constexpr bool EXPECTS_RESULT = false;

	std::string scan_id;
	std::vector<ScannedModule> modules;

	template <class V> void Fields(V& v)
	{
		v("scan_id", scan_id);
		v("modules", modules);
	}
};

// Events: session ///////////////////////////////////////////////////////////////

struct ChannelInfo : WireObject
{
	int64_t id = 0;
	std::string name;
	// Null for a top level channel.
	Nullable<int64_t> parent_id;
	bool show_all_matches = false;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("name", name);
		v("parent_id", parent_id);
		v("show_all_matches", show_all_matches);
	}
};

// Applies to channel chat only.
struct ChatRate : WireObject
{
	int messages = 0;
	int64_t window_s = 0;

	template <class V> void Fields(V& v)
	{
		v("messages", messages);
		v("window_s", window_s);
	}
};

struct SessionLimits : WireObject
{
	int chat_max_length = 0;
	ChatRate chat_rate;
	int name_min_length = 0;
	int name_max_length = 0;
	int group_max_members = 0;

	template <class V> void Fields(V& v)
	{
		v("chat_max_length", chat_max_length);
		v("chat_rate", chat_rate);
		v("name_min_length", name_min_length);
		v("name_max_length", name_max_length);
		v("group_max_members", group_max_members);
	}
};

// Always the first event of a connection.
struct ReadyEvent : WireObject
{
	static constexpr const char* EVENT = "Ready";

	int protocol_version = 0;
	// "game", "launcher" or "chat".
	std::string session_type;
	Member user;
	std::vector<std::string> permissions;
	BadgeMap badges;
	std::vector<ChannelInfo> channels;
	SessionLimits limits;

	template <class V> void Fields(V& v)
	{
		v("protocol_version", protocol_version);
		v("session_type", session_type);
		v("user", user);
		v("permissions", permissions);
		v("badges", badges);
		v("channels", channels);
		v("limits", limits);
	}
};

// Carries the full badge set.
struct BadgesUpdateEvent : WireObject
{
	static constexpr const char* EVENT = "BadgesUpdate";

	BadgeMap badges;

	template <class V> void Fields(V& v)
	{
		v("badges", badges);
	}
};

// A close record follows for kick and ban.
struct ModerationNoticeEvent : WireObject
{
	static constexpr const char* EVENT = "ModerationNotice";

	// "kick", "ban" or "mute".
	std::string action;
	// Absent when staff gave no reason.
	std::optional<std::string> reason;
	std::optional<std::string> expires_at;

	template <class V> void Fields(V& v)
	{
		v("action", action);
		v("reason", reason);
		v("expires_at", expires_at);
	}
};

// A planned handover to another service copy. A server.switching close follows.
struct ServerSwitchingEvent : WireObject
{
	static constexpr const char* EVENT = "ServerSwitching";

	int64_t retry_after_ms = 0;

	template <class V> void Fields(V& v)
	{
		v("retry_after_ms", retry_after_ms);
	}
};

// Events: channels and chat /////////////////////////////////////////////////////

// The full member list, on join and after every reconnect. channel_id is the channel actually joined.
struct ChannelMembersEvent : WireObject
{
	static constexpr const char* EVENT = "ChannelMembers";

	int64_t channel_id = 0;
	std::vector<Member> members;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("members", members);
	}
};

// Follows ChannelMembers with the last messages, oldest first.
struct ChannelHistoryEvent : WireObject
{
	static constexpr const char* EVENT = "ChannelHistory";

	std::string channel_id;
	std::vector<ChatMessage> messages;
	bool has_more = false;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("messages", messages);
		v("has_more", has_more);
	}
};

struct MemberAddEvent : WireObject
{
	static constexpr const char* EVENT = "MemberAdd";

	std::string channel_id;
	Member member;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("member", member);
	}
};

struct MemberRemoveEvent : WireObject
{
	static constexpr const char* EVENT = "MemberRemove";

	std::string channel_id;
	int64_t user_id = 0;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("user_id", user_id);
	}
};

struct MemberUpdateEvent : WireObject
{
	static constexpr const char* EVENT = "MemberUpdate";

	Member member;

	template <class V> void Fields(V& v)
	{
		v("member", member);
	}
};

struct MessageCreateEvent : ChatMessage
{
	static constexpr const char* EVENT = "MessageCreate";
};

// Staff deleted the message. Sent to everyone who received it.
struct MessageDeleteEvent : ChannelMessageArgument
{
	static constexpr const char* EVENT = "MessageDelete";
};

struct Conversation : WireObject
{
	// "dm:<user id>" or "group:<id>".
	std::string channel_id;
	int unread = 0;
	// Null for a group with no messages yet.
	Nullable<ChatMessage> last;

	template <class V> void Fields(V& v)
	{
		v("channel_id", channel_id);
		v("unread", unread);
		v("last", last);
	}
};

// The snapshot after Friends, newest activity first.
struct ConversationsEvent : WireObject
{
	static constexpr const char* EVENT = "Conversations";

	std::vector<Conversation> conversations;
	std::vector<Group> groups;

	template <class V> void Fields(V& v)
	{
		v("conversations", conversations);
		v("groups", groups);
	}
};

// The conversation was read on another connection of the user.
struct ReadMarkerEvent : ConversationArgument
{
	static constexpr const char* EVENT = "ReadMarker";
};

struct GroupArgument : WireObject
{
	Group group;

	template <class V> void Fields(V& v)
	{
		v("group", group);
	}
};

struct GroupCreateEvent : GroupArgument
{
	static constexpr const char* EVENT = "GroupCreate";
};

struct GroupUpdateEvent : GroupArgument
{
	static constexpr const char* EVENT = "GroupUpdate";
};

struct GroupRemovedEvent : WireObject
{
	static constexpr const char* EVENT = "GroupRemoved";

	int64_t group_id = 0;
	// "left" or "removed".
	std::string reason;

	template <class V> void Fields(V& v)
	{
		v("group_id", group_id);
		v("reason", reason);
	}
};

// Events: lobbies ///////////////////////////////////////////////////////////////

struct LobbyListUpsertEvent : WireObject
{
	static constexpr const char* EVENT = "LobbyListUpsert";

	LobbySummary lobby;

	template <class V> void Fields(V& v)
	{
		v("lobby", lobby);
	}
};

struct LobbyListRemoveEvent : WireObject
{
	static constexpr const char* EVENT = "LobbyListRemove";

	int64_t lobby_id = 0;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
	}
};

// The full lobby state after every change.
struct LobbyStateEvent : WireObject
{
	static constexpr const char* EVENT = "LobbyState";

	LobbyState lobby;

	template <class V> void Fields(V& v)
	{
		v("lobby", lobby);
	}
};

struct LobbyLeftEvent : WireObject
{
	static constexpr const char* EVENT = "LobbyLeft";

	int64_t lobby_id = 0;
	// "left", "kicked" or "closed".
	std::string reason;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("reason", reason);
	}
};

struct CountdownStartedEvent : WireObject
{
	static constexpr const char* EVENT = "CountdownStarted";

	int64_t lobby_id = 0;
	int64_t duration_s = 0;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("duration_s", duration_s);
	}
};

struct CountdownCancelledEvent : WireObject
{
	static constexpr const char* EVENT = "CountdownCancelled";

	int64_t lobby_id = 0;
	// "player_not_ready", "mesh_incomplete", "player_left" or "settings_changed".
	std::string reason;
	std::optional<std::vector<int64_t> > missing;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("reason", reason);
		v("missing", missing);
	}
};

struct GameStartEvent : WireObject
{
	static constexpr const char* EVENT = "GameStart";

	int64_t lobby_id = 0;
	int64_t match_id = 0;
	int64_t rng_seed = 0;
	Nullable<std::string> screenshot_url;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("match_id", match_id);
		v("rng_seed", rng_seed);
		v("screenshot_url", screenshot_url);
	}
};

// Events: matchmaking ///////////////////////////////////////////////////////////

struct MatchmakingStatusEvent : WireObject
{
	static constexpr const char* EVENT = "MatchmakingStatus";

	std::string code;
	nlohmann::json params = nlohmann::json::object();

	template <class V> void Fields(V& v)
	{
		v("code", code);
		v("params", params);
	}
};

// The client joins the lobby with JoinLobby within 45 seconds.
struct MatchFoundEvent : WireObject
{
	static constexpr const char* EVENT = "MatchFound";

	int64_t lobby_id = 0;
	MapReference map;
	int players = 0;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("map", map);
		v("players", players);
	}
};

// The mesh check has countdown_s 0, the start countdown sets both.
struct MatchSetupProgressEvent : WireObject
{
	static constexpr const char* EVENT = "MatchSetupProgress";

	int64_t timeout_s = 0;
	int64_t countdown_s = 0;

	template <class V> void Fields(V& v)
	{
		v("timeout_s", timeout_s);
		v("countdown_s", countdown_s);
	}
};

struct MatchStartEvent : WireObject
{
	static constexpr const char* EVENT = "MatchStart";

	int64_t lobby_id = 0;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
	}
};

struct MatchRequeueEvent : WireObject
{
	static constexpr const char* EVENT = "MatchRequeue";

	// "player_left", "lobby_gone", "mesh_incomplete", "join_timeout" or "setup_failed".
	std::string reason;

	template <class V> void Fields(V& v)
	{
		v("reason", reason);
	}
};

// Events: social ////////////////////////////////////////////////////////////////

// "offline", "menu", "channel", "lobby", "matchmaking" or "in_game", in this environment only.
// channel_id is the channel the user is in, null when in none.
struct FriendEntry : Member
{
	bool online = false;
	std::string activity;
	Nullable<int64_t> channel_id;

	template <class V> void Fields(V& v)
	{
		Member::Fields(v);
		v("online", online);
		v("activity", activity);
		v("channel_id", channel_id);
	}
};

// The snapshot after Ready.
struct FriendsEvent : WireObject
{
	static constexpr const char* EVENT = "Friends";

	std::vector<FriendEntry> friends;
	std::vector<Member> incoming;
	std::vector<Member> outgoing;
	std::vector<Member> blocked;

	template <class V> void Fields(V& v)
	{
		v("friends", friends);
		v("incoming", incoming);
		v("outgoing", outgoing);
		v("blocked", blocked);
	}
};

struct FriendRequestReceivedEvent : WireObject
{
	static constexpr const char* EVENT = "FriendRequestReceived";

	Member from;

	template <class V> void Fields(V& v)
	{
		v("from", from);
	}
};

struct FriendRequestAcceptedEvent : WireObject
{
	static constexpr const char* EVENT = "FriendRequestAccepted";

	Member by;

	template <class V> void Fields(V& v)
	{
		v("by", by);
	}
};

struct FriendRemovedEvent : UserIdArgument
{
	static constexpr const char* EVENT = "FriendRemoved";
};

struct FriendPresenceEvent : WireObject
{
	static constexpr const char* EVENT = "FriendPresence";

	int64_t user_id = 0;
	bool online = false;
	std::vector<std::string> clients;
	std::string activity;
	Nullable<int64_t> channel_id;

	template <class V> void Fields(V& v)
	{
		v("user_id", user_id);
		v("online", online);
		v("clients", clients);
		v("activity", activity);
		v("channel_id", channel_id);
	}
};

// Events: peer networking ///////////////////////////////////////////////////////

struct StartSignallingEvent : WireObject
{
	static constexpr const char* EVENT = "StartSignalling";

	int64_t lobby_id = 0;
	int64_t user_id = 0;
	int preferred_port = 0;
	std::string middleware_id;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("user_id", user_id);
		v("preferred_port", preferred_port);
		v("middleware_id", middleware_id);
	}
};

struct DisconnectPeerEvent : WireObject
{
	static constexpr const char* EVENT = "DisconnectPeer";

	int64_t lobby_id = 0;
	int64_t user_id = 0;

	template <class V> void Fields(V& v)
	{
		v("lobby_id", lobby_id);
		v("user_id", user_id);
	}
};

// The service sets from to the authenticated sender.
struct PeerPayloadEvent : WireObject
{
	int64_t from = 0;
	std::string payload;

	template <class V> void Fields(V& v)
	{
		v("from", from);
		v("payload", payload);
	}
};

struct SignalEvent : PeerPayloadEvent
{
	static constexpr const char* EVENT = "Signal";
};

struct AnticheatMessageEvent : PeerPayloadEvent
{
	static constexpr const char* EVENT = "AnticheatMessage";
};

struct MeshCheckRequestEvent : WireObject
{
	static constexpr const char* EVENT = "MeshCheckRequest";

	int64_t mesh_check_id = 0;
	int attempt = 0;

	template <class V> void Fields(V& v)
	{
		v("mesh_check_id", mesh_check_id);
		v("attempt", attempt);
	}
};

struct MeshLink : WireObject
{
	int64_t source_user_id = 0;
	int64_t target_user_id = 0;

	template <class V> void Fields(V& v)
	{
		v("source_user_id", source_user_id);
		v("target_user_id", target_user_id);
	}
};

struct MeshCheckResultEvent : WireObject
{
	static constexpr const char* EVENT = "MeshCheckResult";

	int64_t mesh_check_id = 0;
	int attempt = 0;
	bool complete = false;
	std::string reason;
	std::vector<MeshLink> missing;

	template <class V> void Fields(V& v)
	{
		v("mesh_check_id", mesh_check_id);
		v("attempt", attempt);
		v("complete", complete);
		v("reason", reason);
		v("missing", missing);
	}
};

struct AnticheatPeerArgument : WireObject
{
	int64_t user_id = 0;
	std::string middleware_id;

	template <class V> void Fields(V& v)
	{
		v("user_id", user_id);
		v("middleware_id", middleware_id);
	}
};

struct AnticheatRegisterEvent : AnticheatPeerArgument
{
	static constexpr const char* EVENT = "AnticheatRegister";
};

struct AnticheatDeregisterEvent : AnticheatPeerArgument
{
	static constexpr const char* EVENT = "AnticheatDeregister";
};

struct ProbeRequestEvent : WireObject
{
	static constexpr const char* EVENT = "ProbeRequest";

	std::string probe_id;
	std::string url;

	template <class V> void Fields(V& v)
	{
		v("probe_id", probe_id);
		v("url", url);
	}
};

struct ModuleScanRequestEvent : WireObject
{
	static constexpr const char* EVENT = "ModuleScanRequest";

	std::string scan_id;

	template <class V> void Fields(V& v)
	{
		v("scan_id", scan_id);
	}
};

} // namespace GenOnlineRealtime
