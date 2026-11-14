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

#pragma once

#include "GameNetwork/GenOnline/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

inline int64_t MatchmakingParamInt(const nlohmann::json& params, const char* key)
{
	if (params.is_object() && params.contains(key) && params[key].is_number_integer())
	{
		return params[key].get<int64_t>();
	}
	return 0;
}

inline std::string MatchmakingParamText(const nlohmann::json& params, const char* key)
{
	if (params.is_object() && params.contains(key) && params[key].is_string())
	{
		return params[key].get<std::string>();
	}
	return std::string();
}

// The quick match menu line of a MatchmakingStatus code. An unknown code shows the code itself.
inline std::string MatchmakingStatusText(const std::string& code, const nlohmann::json& params)
{
	if (code == "matchmaking.queued")
	{
		return "Joined the queue.";
	}
	if (code == "matchmaking.searching")
	{
		return "Searching for players: " + std::to_string(MatchmakingParamInt(params, "players")) + " of " + std::to_string(MatchmakingParamInt(params, "desired")) + ".";
	}
	if (code == "matchmaking.expanding")
	{
		return "Widening the search.";
	}
	if (code == "matchmaking.grace_countdown")
	{
		return "Starting with the players found in " + std::to_string(MatchmakingParamInt(params, "seconds")) + " seconds.";
	}
	if (code == "matchmaking.grace_cancelled")
	{
		return "Waiting for more players.";
	}
	if (code == "matchmaking.creating")
	{
		const std::string map = MatchmakingParamText(params, "map");
		return map.empty() ? std::string("Match found, creating the lobby.") : "Match found on " + map + ", creating the lobby.";
	}
	if (code == "matchmaking.preparing")
	{
		return "Preparing the match.";
	}
	if (code == "matchmaking.starting")
	{
		return "Starting in " + std::to_string(MatchmakingParamInt(params, "seconds")) + " seconds.";
	}
	return "Matchmaking: " + code;
}

// The text of a refused matchmaking method. Returns an empty string for a code outside matchmaking.
inline std::string MatchmakingFailureText(const std::string& code, const nlohmann::json& params)
{
	if (code.compare(0, 12, "matchmaking.") != 0)
	{
		return std::string();
	}
	if (code == "matchmaking.invalid_playlist")
	{
		return "That playlist is not available.";
	}
	if (code == "matchmaking.too_few_maps")
	{
		const int64_t minimum = MatchmakingParamInt(params, "min");
		return minimum > 0 ? "Select at least " + std::to_string(minimum) + " maps." : std::string("Select more maps.");
	}
	if (code == "matchmaking.stats_unavailable")
	{
		return "Your rating could not be loaded. Please try again later.";
	}
	if (code == "matchmaking.already_queued")
	{
		return "You are already searching for a match.";
	}
	if (code == "matchmaking.not_queued")
	{
		return "You are not searching for a match.";
	}
	return "Matchmaking failed (" + code + ").";
}

inline std::string MatchRequeueText(const std::string& reason)
{
	if (reason == "player_left")
	{
		return "A player left. Searching again.";
	}
	if (reason == "lobby_gone")
	{
		return "The match lobby closed. Searching again.";
	}
	if (reason == "mesh_incomplete")
	{
		return "Not every player could connect. Searching again.";
	}
	if (reason == "join_timeout")
	{
		return "A player did not join in time. Searching again.";
	}
	if (reason == "setup_failed")
	{
		return "The match setup failed. Searching again.";
	}
	return "Searching again.";
}

// Turns positions in the playlist map list into the map indexes of the playlist. Returns false and an
// empty list when a position is outside the list.
inline bool TranslateMapIndexes(const std::vector<int>& positions, const std::vector<int>& mapIndexes, std::vector<int>& out)
{
	out.clear();
	for (int position : positions)
	{
		if (position < 0 || position >= (int)mapIndexes.size())
		{
			out.clear();
			return false;
		}
		out.push_back(mapIndexes[position]);
	}
	return true;
}

} // namespace GenOnlineRealtime
