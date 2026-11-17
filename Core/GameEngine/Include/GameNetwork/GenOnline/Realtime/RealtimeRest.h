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

#include <algorithm>
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

// Standby answers ////////////////////////////////////////////////////////////////

// The wait before the next try after a standby answer.
constexpr int64_t STANDBY_RETRY_DELAY_MS = 1000;
// Standby answers in a row for this long count as a normal failure.
constexpr int64_t STANDBY_RETRY_WINDOW_MS = 10000;

// True when a service copy that is not serving gave the answer, in the second after a failover switch.
// The body is {"error":"service.unavailable","params":{"what":"standby"}} with a 503. The refused realtime
// upgrade carries no body, so bUpgrade counts any 503 as a standby answer.
inline bool IsStandbyAnswer(const RestOutcome& outcome, bool bUpgrade)
{
	if (!outcome.delivered || outcome.status != 503)
	{
		return false;
	}
	if (bUpgrade)
	{
		return true;
	}
	if (outcome.error.code != "service.unavailable")
	{
		return false;
	}
	auto what = outcome.error.params.find("what");
	return what != outcome.error.params.end() && what->is_string() && what->get<std::string>() == "standby";
}

// Times the retries of standby answers in a row. A standby retry does not count as a send of the call.
struct StandbyRetry
{
	// The time of the first standby answer in a row, or -1.
	int64_t firstStandbyMs = -1;

	// Takes an answer at nowMs and returns the wait before the next try, or -1 when the answer is no
	// standby answer or the standby answers in a row lasted STANDBY_RETRY_WINDOW_MS.
	int64_t NextDelayMs(bool bStandby, int64_t nowMs)
	{
		if (!bStandby)
		{
			firstStandbyMs = -1;
			return -1;
		}
		if (firstStandbyMs < 0)
		{
			firstStandbyMs = nowMs;
		}
		return nowMs - firstStandbyMs < STANDBY_RETRY_WINDOW_MS ? STANDBY_RETRY_DELAY_MS : -1;
	}

	void Reset()
	{
		firstStandbyMs = -1;
	}
};

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

// POST reports a message or a player (ReportBody).
inline std::string RestReportsPath()
{
	return "/api/v1/reports";
}

// DELETE deletes a message, staff only (ReasonBody).
inline std::string RestMessagePath(const std::string& messageID)
{
	return "/api/v1/messages/" + messageID;
}

// PATCH changes the display name.
inline std::string RestMePath()
{
	return "/api/v1/me";
}

// POST reports the final connection result of one pair of lobby members.
inline std::string RestConnectionReportPath(int64_t lobbyID)
{
	return "/api/v1/lobbies/" + std::to_string(lobbyID) + "/connection_reports";
}

// The outcome of a connection report. Only final results are reported.
inline std::string ConnectionReportOutcome(bool bConnected, bool bRelayed, bool bDisconnected)
{
	if (bConnected)
	{
		return bRelayed ? "connected_relay" : "connected_direct";
	}
	return bDisconnected ? "disconnected" : "failed";
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

// One state change of a connection attempt.
struct ConnectionReportEvent : WireObject
{
	// connecting, finding_route, connected_direct, connected_relay, failed or disconnected
	std::string state;
	// The milliseconds since the attempt started, never decreasing, at most 600000.
	int64_t t_ms = 0;

	template <class V> void Fields(V& v)
	{
		v("state", state);
		v("t_ms", t_ms);
	}
};

// POST /api/v1/lobbies/:id/connection_reports
struct ConnectionReportBody : WireObject
{
	int64_t target_user_id = 0;
	std::string outcome;
	// The transport of the attempt's StartSignalling: gns or a plugin transport name.
	std::string transport;
	int ip_version = 4;
	// The attempt of the StartSignalling of this pair in this lobby, 1 or more.
	int attempt = 0;
	// Every state change of the attempt, oldest first, at most 32.
	std::optional<std::vector<ConnectionReportEvent> > events;

	template <class V> void Fields(V& v)
	{
		v("target_user_id", target_user_id);
		v("outcome", outcome);
		v("transport", transport);
		v("ip_version", ip_version);
		v("attempt", attempt);
		v("events", events);
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
	Nullable<std::string> lifted_at;
	Member created_by;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("type", type);
		v("reason", reason);
		v("created_at", created_at);
		v("expires_at", expires_at);
		v("lifted_at", lifted_at);
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

// GET /api/v1/me/friends, the lists of the Friends event without the blocked users.
struct MyFriendsResponse : WireObject
{
	std::vector<FriendEntry> friends;
	std::vector<Member> incoming;
	std::vector<Member> outgoing;

	template <class V> void Fields(V& v)
	{
		v("friends", friends);
		v("incoming", incoming);
		v("outgoing", outgoing);
	}
};

// GET /api/v1/me/blocks
struct MyBlocksResponse : WireObject
{
	std::vector<Member> blocked;

	template <class V> void Fields(V& v)
	{
		v("blocked", blocked);
	}
};

// A report in the staff queue. message_id is null for a report of a player.
struct StaffReport : WireObject
{
	int64_t id = 0;
	Member reporter;
	Member reported;
	Nullable<std::string> message_id;
	Nullable<std::string> reason;
	std::string status;
	std::string created_at;
	Nullable<std::string> resolved_at;
	Nullable<std::string> resolution_note;

	template <class V> void Fields(V& v)
	{
		v("id", id);
		v("reporter", reporter);
		v("reported", reported);
		v("message_id", message_id);
		v("reason", reason);
		v("status", status);
		v("created_at", created_at);
		v("resolved_at", resolved_at);
		v("resolution_note", resolution_note);
	}
};

// GET /api/v1/reports
struct StaffReportsResponse : WireObject
{
	std::vector<StaffReport> reports;

	template <class V> void Fields(V& v)
	{
		v("reports", reports);
	}
};

// PATCH /api/v1/reports/:id closes a report as actioned or dismissed.
struct ReportCloseBody : WireObject
{
	std::string status;
	std::optional<std::string> resolution_note;

	template <class V> void Fields(V& v)
	{
		v("status", status);
		v("resolution_note", resolution_note);
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

// Meta: compatibility and config ////////////////////////////////////////////////////

// GET is anonymous and answers 200 for every known client, also an outdated one.
inline std::string RestCompatibilityPath(const std::string& clientID, const std::string& version)
{
	return "/api/v1/compatibility?client=" + clientID + "&version=" + version;
}

namespace Detail
{
// Reads the decimal number at pos and moves pos past it. False when no digit is there.
inline bool ReadVersionNumber(const std::string& text, size_t& pos, int& number)
{
	const size_t start = pos;
	number = 0;
	while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && pos - start < 6)
	{
		number = number * 10 + (text[pos] - '0');
		++pos;
	}
	return pos > start;
}
} // namespace Detail

// The YYYY.MM.PATCH version of a build. releaseTag is the git describe of the closest release tag, so a build
// past a release reports that release. A build without one reports the year and month of its commit with patch 0.
inline std::string CompatibilityVersion(const std::string& releaseTag, const std::string& commitDate)
{
	int year = 0;
	int month = 0;
	int patch = 0;
	size_t pos = 1;
	if (!releaseTag.empty() && releaseTag[0] == 'v' && Detail::ReadVersionNumber(releaseTag, pos, year) && pos - 1 == 4
		&& pos < releaseTag.size() && releaseTag[pos++] == '.' && Detail::ReadVersionNumber(releaseTag, pos, month)
		&& pos < releaseTag.size() && releaseTag[pos++] == '.' && Detail::ReadVersionNumber(releaseTag, pos, patch)
		&& (pos == releaseTag.size() || releaseTag[pos] == '-') && month >= 1 && month <= 12)
	{
		return std::to_string(year) + "." + std::to_string(month) + "." + std::to_string(patch);
	}
	pos = 0;
	if (Detail::ReadVersionNumber(commitDate, pos, year) && pos == 4 && pos < commitDate.size() && commitDate[pos++] == '-'
		&& Detail::ReadVersionNumber(commitDate, pos, month) && month >= 1 && month <= 12)
	{
		return std::to_string(year) + "." + std::to_string(month) + ".0";
	}
	return "1970.1.0";
}

struct CompatibilityUpdate : WireObject
{
	std::string patcher_name;
	std::string patcher_path;
	// Null when the service has no copy of the patcher.
	Nullable<int64_t> patcher_size;

	template <class V> void Fields(V& v)
	{
		v("patcher_name", patcher_name);
		v("patcher_path", patcher_path);
		v("patcher_size", patcher_size);
	}
};

// GET /api/v1/compatibility
struct CompatibilityResponse : WireObject
{
	bool compatible = false;
	Nullable<std::string> min_version;
	std::string service_version;
	Nullable<CompatibilityUpdate> update;

	template <class V> void Fields(V& v)
	{
		v("compatible", compatible);
		v("min_version", min_version);
		v("service_version", service_version);
		v("update", update);
	}
};

// True for a body of the compatibility answer, whatever its verdict.
inline bool IsCompatibilityAnswer(const std::string& body)
{
	CompatibilityResponse answer;
	return MakeRestOutcome(true, 200, body).Data(answer);
}

enum class ProbeAnswer
{
	None,
	Serving,
	Standby
};

// Classifies the answer of the compatibility probe. A standby answer also proves the route reaches the service.
inline ProbeAnswer ProbeAnswerOf(int status, const std::string& body)
{
	if (status == 200 && IsCompatibilityAnswer(body))
	{
		return ProbeAnswer::Serving;
	}
	if (IsStandbyAnswer(MakeRestOutcome(true, status, body), false))
	{
		return ProbeAnswer::Standby;
	}
	return ProbeAnswer::None;
}

enum class CompatibilityVerdict
{
	UpToDate,
	NeedsUpdate,
	Failed
};

// The verdict of a compatibility answer. An outdated build needs an update with a patcher size, which the
// downloaded patcher is checked against, and a bare file name, because the name becomes a local path.
// An outdated build without such an update fails.
inline CompatibilityVerdict CompatibilityVerdictOf(const RestOutcome& outcome, CompatibilityResponse& answer)
{
	if (!outcome.Data(answer))
	{
		return CompatibilityVerdict::Failed;
	}
	if (answer.compatible)
	{
		return CompatibilityVerdict::UpToDate;
	}
	if (answer.update.isNull || answer.update.value.patcher_size.isNull)
	{
		return CompatibilityVerdict::Failed;
	}
	const std::string& name = answer.update.value.patcher_name;
	if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos)
	{
		return CompatibilityVerdict::Failed;
	}
	return CompatibilityVerdict::NeedsUpdate;
}

// GET needs a session.
inline std::string RestConfigPath()
{
	return "/api/v1/config";
}

// GET /api/v1/config
struct ConfigResponse : WireObject
{
	// The settings the service config reader takes.
	nlohmann::json service_config = nlohmann::json::object();
	// The client does not read it.
	std::optional<int64_t> anticheat_level;
	std::string motd;

	template <class V> void Fields(V& v)
	{
		v("service_config", service_config);
		v("anticheat_level", anticheat_level);
		v("motd", motd);
	}
};

// Linked accounts /////////////////////////////////////////////////////////////////

// PUT links the caller to their account at the provider that the plugin reports and answers 204.
inline std::string RestLinkedAccountPath(const std::string& provider)
{
	return "/api/v1/me/linked_accounts/" + provider;
}

// PUT /api/v1/me/linked_accounts/:provider
struct LinkAccountBody : WireObject
{
	// The provider's own proof of the account.
	std::string proof;

	template <class V> void Fields(V& v)
	{
		v("proof", proof);
	}
};

// POST mints the short-lived token the plugin hands to its platform to sign the player in. It has no body.
inline std::string RestSignInTokenPath(const std::string& provider)
{
	return RestLinkedAccountPath(provider) + "/sign_in_token";
}

// POST /api/v1/me/linked_accounts/:provider/sign_in_token
struct SignInTokenResponse : WireObject
{
	std::string token;
	int expires_in = 0;

	template <class V> void Fields(V& v)
	{
		v("token", token);
		v("expires_in", expires_in);
	}
};

// Stats ///////////////////////////////////////////////////////////////////////////

// GET is anonymous.
inline std::string RestGlobalStatsPath()
{
	return "/api/v1/stats/global";
}

// The service index of a playable side, -1 for any other side. The playable sides are 2 to 13.
inline int GlobalStatsFactionIndex(int side)
{
	return side >= 2 && side <= 13 ? side - 2 : -1;
}

struct FactionStats : WireObject
{
	int side = 0;
	int64_t matches = 0;
	int64_t wins = 0;

	template <class V> void Fields(V& v)
	{
		v("side", side);
		v("matches", matches);
		v("wins", wins);
	}
};

// GET /api/v1/stats/global
struct GlobalStatsResponse : WireObject
{
	int64_t players = 0;
	int64_t lobbies = 0;
	std::vector<FactionStats> factions;

	template <class V> void Fields(V& v)
	{
		v("players", players);
		v("lobbies", lobbies);
		v("factions", factions);
	}
};

// GET needs a session.
inline std::string RestPlayerStatsPath(int64_t userID)
{
	return "/api/v1/players/" + std::to_string(userID) + "/stats";
}

// The most ids one batch request takes.
constexpr size_t REST_PLAYER_STATS_BATCH_MAX = 100;

// One GET path per batch of at most 100 ids, in the order of the ids.
inline std::vector<std::string> RestPlayerStatsBatchPaths(const std::vector<int64_t>& userIDs)
{
	std::vector<std::string> paths;
	for (size_t start = 0; start < userIDs.size(); start += REST_PLAYER_STATS_BATCH_MAX)
	{
		std::string path = "/api/v1/players/stats?ids=";
		const size_t end = (std::min)(userIDs.size(), start + REST_PLAYER_STATS_BATCH_MAX);
		for (size_t i = start; i < end; ++i)
		{
			if (i != start)
			{
				path += ",";
			}
			path += std::to_string(userIDs[i]);
		}
		paths.push_back(path);
	}
	return paths;
}

// The per general counters of a player, one entry per service index.
struct PlayerGeneralStats : WireObject
{
	std::vector<int64_t> wins;
	std::vector<int64_t> losses;
	std::vector<int64_t> games;
	std::vector<int64_t> duration;
	std::vector<int64_t> units_killed;
	std::vector<int64_t> units_lost;
	std::vector<int64_t> units_built;
	std::vector<int64_t> buildings_killed;
	std::vector<int64_t> buildings_lost;
	std::vector<int64_t> buildings_built;
	std::vector<int64_t> earnings;
	std::vector<int64_t> tech_captured;
	std::vector<int64_t> discons;
	std::vector<int64_t> desyncs;
	std::vector<int64_t> surrenders;
	std::vector<int64_t> games_of_2p;
	std::vector<int64_t> games_of_3p;
	std::vector<int64_t> games_of_4p;
	std::vector<int64_t> games_of_5p;
	std::vector<int64_t> games_of_6p;
	std::vector<int64_t> games_of_7p;
	std::vector<int64_t> games_of_8p;
	std::vector<int64_t> custom_games;
	std::vector<int64_t> quick_matches;

	template <class V> void Fields(V& v)
	{
		v("wins", wins);
		v("losses", losses);
		v("games", games);
		v("duration", duration);
		v("units_killed", units_killed);
		v("units_lost", units_lost);
		v("units_built", units_built);
		v("buildings_killed", buildings_killed);
		v("buildings_lost", buildings_lost);
		v("buildings_built", buildings_built);
		v("earnings", earnings);
		v("tech_captured", tech_captured);
		v("discons", discons);
		v("desyncs", desyncs);
		v("surrenders", surrenders);
		v("games_of_2p", games_of_2p);
		v("games_of_3p", games_of_3p);
		v("games_of_4p", games_of_4p);
		v("games_of_5p", games_of_5p);
		v("games_of_6p", games_of_6p);
		v("games_of_7p", games_of_7p);
		v("games_of_8p", games_of_8p);
		v("custom_games", custom_games);
		v("quick_matches", quick_matches);
	}
};

// GET /api/v1/players/:id/stats, also one entry of the batch.
struct PlayerStatsBody : WireObject
{
	int64_t user_id = 0;
	int64_t elo_rating = 0;
	int64_t monthly_elo_rating = 0;
	int64_t elo_matches = 0;
	PlayerGeneralStats generals;
	int64_t games_as_random = 0;
	int64_t last_general = 0;
	int64_t games_in_row_with_last_general = 0;
	int64_t challenge_medals = 0;
	int64_t battle_honors = 0;
	int64_t qm_wins_in_a_row = 0;
	int64_t max_qm_wins_in_a_row = 0;
	int64_t wins_in_a_row = 0;
	int64_t max_wins_in_a_row = 0;
	int64_t losses_in_a_row = 0;
	int64_t max_losses_in_a_row = 0;
	int64_t discons_in_a_row = 0;
	int64_t max_discons_in_a_row = 0;
	int64_t desyncs_in_a_row = 0;
	int64_t max_desyncs_in_a_row = 0;
	int64_t built_particle_cannon = 0;
	int64_t built_nuke = 0;
	int64_t built_scud = 0;

	template <class V> void Fields(V& v)
	{
		v("user_id", user_id);
		v("elo_rating", elo_rating);
		v("monthly_elo_rating", monthly_elo_rating);
		v("elo_matches", elo_matches);
		v("generals", generals);
		v("games_as_random", games_as_random);
		v("last_general", last_general);
		v("games_in_row_with_last_general", games_in_row_with_last_general);
		v("challenge_medals", challenge_medals);
		v("battle_honors", battle_honors);
		v("qm_wins_in_a_row", qm_wins_in_a_row);
		v("max_qm_wins_in_a_row", max_qm_wins_in_a_row);
		v("wins_in_a_row", wins_in_a_row);
		v("max_wins_in_a_row", max_wins_in_a_row);
		v("losses_in_a_row", losses_in_a_row);
		v("max_losses_in_a_row", max_losses_in_a_row);
		v("discons_in_a_row", discons_in_a_row);
		v("max_discons_in_a_row", max_discons_in_a_row);
		v("desyncs_in_a_row", desyncs_in_a_row);
		v("max_desyncs_in_a_row", max_desyncs_in_a_row);
		v("built_particle_cannon", built_particle_cannon);
		v("built_nuke", built_nuke);
		v("built_scud", built_scud);
	}
};

// GET /api/v1/players/stats?ids= answers in request order and leaves unknown ids out.
struct PlayerStatsBatchResponse : WireObject
{
	std::vector<PlayerStatsBody> stats;

	template <class V> void Fields(V& v)
	{
		v("stats", stats);
	}
};

// Log redaction ///////////////////////////////////////////////////////////////////

// A service path as it may be logged. Route names stay, and so does a provider name right after linked_accounts.
// Every other segment and the whole query become [redacted].
inline std::string RestPathForLog(const std::string& path)
{
	static const char* const ROUTE_NAMES[] = {
		"api", "v1", "me", "auth", "session", "compatibility", "config", "linked_accounts", "sign_in_token",
		"stats", "global", "players", "matches", "results", "lobbies", "connection_reports", "matchmaking",
		"playlists", "friend_requests", "accept", "friends", "blocks", "groups", "members", "sanctions",
		"messages", "reports", "roles", "leaderboards", "sessions", "realtime", "connect", "oauth", "token",
		"authorize", "revoke", ".well-known", "openid-configuration", "jwks.json"
	};
	const size_t queryStart = path.find_first_of("?#");
	const std::string route = path.substr(0, queryStart);
	std::string result;
	size_t pos = route.empty() || route[0] != '/' ? 0 : 1;
	bool afterLinkedAccounts = false;
	while (pos <= route.size())
	{
		size_t end = route.find('/', pos);
		if (end == std::string::npos)
		{
			end = route.size();
		}
		const std::string segment = route.substr(pos, end - pos);
		result += "/";
		if (!segment.empty())
		{
			bool known = false;
			for (const char* name : ROUTE_NAMES)
			{
				if (segment == name)
				{
					known = true;
					break;
				}
			}
			if (!known && afterLinkedAccounts && IsPluginTransportName(segment))
			{
				known = true;
			}
			result += known ? segment : std::string("[redacted]");
		}
		afterLinkedAccounts = segment == "linked_accounts";
		if (end == route.size())
		{
			break;
		}
		pos = end + 1;
	}
	if (queryStart != std::string::npos)
	{
		result += "?[redacted]";
	}
	return result;
}

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
