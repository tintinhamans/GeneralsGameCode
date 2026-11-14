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

// FILE: RealtimeRest.h ///////////////////////////////////////////////////////
// Description: Bodies, paths and error parsing of the GenOnline REST endpoints
//              that replace earlier hub methods, section 10 of the realtime
//              protocol. Field names match the hub. The REST fixtures of the
//              service pin every body here. Failures carry the section 7 codes
//              as {"error": code, "params": {...}} with an HTTP status.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "GameNetwork/GenOnline/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

// Error bodies //////////////////////////////////////////////////////////////////

// A failed REST call, a refused realtime upgrade or a refused session registration.
struct ApiErrorInfo
{
	// Empty when the body names no code.
	std::string code;
	// Always an object. A reason or expiry of the top level is copied in when params lacks it.
	nlohmann::json params = nlohmann::json::object();
	std::optional<std::string> reason;
	// Absent for a ban without an end.
	std::optional<std::string> expires_at;
};

// Reads an error body. The spec form is {"error": code, "params": {...}}. Older services name the
// code in a top level "code" or add a "message" next to "error", and put reason and expires_at at the
// top level. A body that is not a JSON object gives an empty code.
inline ApiErrorInfo ParseApiError(const std::string& body)
{
	ApiErrorInfo info;
	const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return info;
	}
	auto error = root.find("error");
	auto code = root.find("code");
	if (error != root.end() && error->is_string())
	{
		info.code = error->get<std::string>();
	}
	else if (code != root.end() && code->is_string())
	{
		info.code = code->get<std::string>();
	}
	auto params = root.find("params");
	if (params != root.end() && params->is_object())
	{
		info.params = *params;
	}
	for (const char* key : { "reason", "expires_at" })
	{
		auto inParams = info.params.find(key);
		if (inParams != info.params.end() && inParams->is_string())
		{
			continue;
		}
		auto top = root.find(key);
		if (top != root.end() && top->is_string())
		{
			info.params[key] = *top;
		}
	}
	auto reason = info.params.find("reason");
	if (reason != info.params.end() && reason->is_string())
	{
		info.reason = reason->get<std::string>();
	}
	auto expiresAt = info.params.find("expires_at");
	if (expiresAt != info.params.end() && expiresAt->is_string() && !expiresAt->get<std::string>().empty())
	{
		info.expires_at = expiresAt->get<std::string>();
	}
	return info;
}

// The answer of a REST call.
struct RestOutcome
{
	// False when no HTTP answer arrived.
	bool delivered = false;
	int status = 0;
	std::string body;
	// Set for a delivered failure.
	ApiErrorInfo error;

	bool Succeeded() const
	{
		return delivered && status >= 200 && status < 300;
	}

	// Parses the body of a success. A 204 without a body reads as an empty object.
	template <class T>
	bool Data(T& out) const
	{
		if (!Succeeded())
		{
			return false;
		}
		const nlohmann::json data = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body, nullptr, false);
		if (data.is_discarded())
		{
			return false;
		}
		try
		{
			out = data.get<T>();
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
};

// Builds the outcome of an HTTP answer. delivered is false when the transport failed.
inline RestOutcome MakeRestOutcome(bool delivered, int status, const std::string& body)
{
	RestOutcome outcome;
	outcome.delivered = delivered && status != 0;
	outcome.status = status;
	outcome.body = body;
	if (outcome.delivered && !outcome.Succeeded())
	{
		outcome.error = ParseApiError(body);
		// A 401 means the same for every caller, also without a body.
		if (outcome.error.code.empty() && status == 401)
		{
			outcome.error.code = "unauthorized";
		}
	}
	return outcome;
}

// Retries of calls that are safe to repeat ////////////////////////////////////////

// Sends of a call that is safe to repeat, the first one included.
constexpr int REST_RETRY_MAX_SENDS = 4;

// The delay before the next send of a failed call that is safe to repeat, or -1 to give up.
// sendsSoFar counts the sends already made. A lost answer, a timeout, a rate limit and a server
// error retry. A rule failure or a rejected token does not, a new send gets the same answer.
inline int64_t RestRetryDelayMs(const RestOutcome& outcome, int sendsSoFar)
{
	if (outcome.Succeeded() || sendsSoFar >= REST_RETRY_MAX_SENDS)
	{
		return -1;
	}
	if (outcome.delivered && outcome.status != 408 && outcome.status != 429 && outcome.status < 500)
	{
		return -1;
	}
	if (outcome.status == 429)
	{
		auto it = outcome.error.params.find("retry_after_ms");
		if (it != outcome.error.params.end() && it->is_number_integer() && it->get<int64_t>() > 0)
		{
			return (std::min)(it->get<int64_t>(), (int64_t)60000);
		}
	}
	static const int64_t DELAYS_MS[] = { 2000, 5000, 10000 };
	const int index = (std::max)(0, (std::min)(sendsSoFar - 1, 2));
	return DELAYS_MS[index];
}

// Paths ///////////////////////////////////////////////////////////////////////////

inline std::string RestFriendRequestsPath()
{
	return "/api/v1/me/friend_requests";
}

// POST accepts the incoming request of the user.
inline std::string RestFriendRequestAcceptPath(int64_t userID)
{
	return RestFriendRequestsPath() + "/" + std::to_string(userID) + "/accept";
}

// DELETE declines an incoming or cancels an outgoing request.
inline std::string RestFriendRequestPath(int64_t userID)
{
	return RestFriendRequestsPath() + "/" + std::to_string(userID);
}

// DELETE ends a friendship.
inline std::string RestFriendPath(int64_t userID)
{
	return "/api/v1/me/friends/" + std::to_string(userID);
}

// PUT blocks, DELETE unblocks.
inline std::string RestBlockPath(int64_t userID)
{
	return "/api/v1/me/blocks/" + std::to_string(userID);
}

// PATCH changes the display name.
inline std::string RestMePath()
{
	return "/api/v1/me";
}

// PUT stores the result of the caller in that match. Safe to repeat.
inline std::string RestMatchResultPath(int64_t matchID)
{
	return "/api/v1/matches/" + std::to_string(matchID) + "/results/me";
}

inline std::string RestPlaylistsPath()
{
	return "/api/v1/matchmaking/playlists";
}

// Bodies: social and profile //////////////////////////////////////////////////////

// POST /api/v1/me/friend_requests
struct FriendRequestBody : UserIdArgument
{
};

// PATCH /api/v1/me
struct ProfileUpdateBody : WireObject
{
	std::string name;

	template <class V> void Fields(V& v)
	{
		v("name", name);
	}
};

// Bodies: matches and playlists ///////////////////////////////////////////////////

// PUT /api/v1/matches/:id/results/me
struct MatchResultBody : WireObject
{
	bool won = false;
	bool desynced = false;
	int side = 0;
	int64_t duration_s = 0;
	MatchStats stats;

	template <class V> void Fields(V& v)
	{
		v("won", won);
		v("desynced", desynced);
		v("side", side);
		v("duration_s", duration_s);
		v("stats", stats);
	}
};

// The URLs are presigned upload targets.
struct MatchResultResponse : WireObject
{
	std::string screenshot_url;
	std::string replay_url;

	template <class V> void Fields(V& v)
	{
		v("screenshot_url", screenshot_url);
		v("replay_url", replay_url);
	}
};

// GET /api/v1/matchmaking/playlists
struct PlaylistsResponse : WireObject
{
	std::vector<Playlist> playlists;

	template <class V> void Fields(V& v)
	{
		v("playlists", playlists);
	}
};

// Bodies: groups //////////////////////////////////////////////////////////////////

// POST /api/v1/groups
struct GroupCreateBody : WireObject
{
	std::vector<int64_t> user_ids;
	std::optional<std::string> name;

	template <class V> void Fields(V& v)
	{
		v("user_ids", user_ids);
		v("name", name);
	}
};

struct GroupCreateResponse : WireObject
{
	int64_t group_id = 0;

	template <class V> void Fields(V& v)
	{
		v("group_id", group_id);
	}
};

// POST /api/v1/groups/:id/members
struct GroupMembersBody : WireObject
{
	std::vector<int64_t> user_ids;

	template <class V> void Fields(V& v)
	{
		v("user_ids", user_ids);
	}
};

// PATCH /api/v1/groups/:id. A null name clears it.
struct GroupRenameBody : WireObject
{
	Nullable<std::string> name;

	template <class V> void Fields(V& v)
	{
		v("name", name);
	}
};

// Bodies: moderation and reports //////////////////////////////////////////////////

// The optional reason of DELETE /api/v1/sanctions/:id and DELETE /api/v1/messages/:id.
struct ReasonBody : WireObject
{
	std::optional<std::string> reason;

	template <class V> void Fields(V& v)
	{
		v("reason", reason);
	}
};

// POST /api/v1/players/:id/sanctions. type is "kick", "mute" or "ban". A ban without duration_s is permanent.
struct SanctionBody : WireObject
{
	std::string type;
	std::optional<int64_t> duration_s;
	std::optional<std::string> reason;

	template <class V> void Fields(V& v)
	{
		v("type", type);
		v("duration_s", duration_s);
		v("reason", reason);
	}
};

struct SanctionCreateResponse : WireObject
{
	int64_t sanction_id = 0;

	template <class V> void Fields(V& v)
	{
		v("sanction_id", sanction_id);
	}
};

struct Sanction : WireObject
{
	int64_t id = 0;
	std::string type;
	Nullable<std::string> reason;
	std::string created_at;
	Nullable<std::string> expires_at;
	Nullable<std::string> revoked_at;
	Member created_by;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("type", type);
		v("reason", reason);
		v("created_at", created_at);
		v("expires_at", expires_at);
		v("revoked_at", revoked_at);
		v("created_by", created_by);
	}
};

// GET /api/v1/players/:id/sanctions
struct SanctionsResponse : WireObject
{
	std::vector<Sanction> sanctions;

	template <class V> void Fields(V& v)
	{
		v("sanctions", sanctions);
	}
};

// POST /api/v1/reports. Exactly one of message_id and user_id.
struct ReportBody : WireObject
{
	std::optional<std::string> message_id;
	std::optional<int64_t> user_id;
	std::optional<std::string> reason;

	template <class V> void Fields(V& v)
	{
		v("message_id", message_id);
		v("user_id", user_id);
		v("reason", reason);
	}
};

struct ReportCreateResponse : WireObject
{
	int64_t report_id = 0;

	template <class V> void Fields(V& v)
	{
		v("report_id", report_id);
	}
};

// The failure body of the spec form.
struct ErrorBody : WireObject
{
	std::string error;
	nlohmann::json params = nlohmann::json::object();

	template <class V> void Fields(V& v)
	{
		v("error", error);
		v("params", params);
	}
};

} // namespace GenOnlineRealtime
