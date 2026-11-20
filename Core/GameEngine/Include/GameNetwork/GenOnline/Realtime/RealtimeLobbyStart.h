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

// FILE: RealtimeLobbyStart.h ////////////////////////////////////////////////
// Description: Match start of a lobby, GenOnline realtime protocol v1. A
//              client starts a match from GameStart or, after a reconnect
//              that missed GameStart, from LobbyState.start, and starts each
//              match once.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include <cstdint>

namespace GenOnlineRealtime
{

// Reads the start of a lobby as the GameStart it stands for. A lobby that has not started or is complete has none.
inline bool LobbyStateStart(const LobbyState& lobby, GameStartEvent& start)
{
	if (lobby.start.isNull || lobby.state == "complete")
	{
		return false;
	}
	start.lobby_id = lobby.id;
	start.match_id = lobby.start.value.match_id;
	start.rng_seed = lobby.start.value.rng_seed;
	start.screenshot_url = lobby.start.value.screenshot_url;
	return true;
}

// Remembers the match a client started, so GameStart and LobbyState.start start it once in either order.
class MatchStartOnce
{
public:
	// True for the first start of a match, false for a repeat of the match already started.
	bool Begin(int64_t matchID)
	{
		if (m_started && m_matchID == matchID)
		{
			return false;
		}
		m_started = true;
		m_matchID = matchID;
		return true;
	}

	// Forgets the started match.
	void Reset()
	{
		m_started = false;
		m_matchID = 0;
	}

private:
	bool m_started = false;
	int64_t m_matchID = 0;
};

} // namespace GenOnlineRealtime
