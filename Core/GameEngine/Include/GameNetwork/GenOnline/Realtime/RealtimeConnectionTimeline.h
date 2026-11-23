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

#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace GenOnlineRealtime
{

// The report state of a client connection state, nullptr for a state that is not recorded.
// A connection reports CONNECTED_DIRECT for a relayed route too, so the relayed flag picks the state.
inline const char* ConnectionEventState(EConnectionState state, bool bRelayed)
{
	switch (state)
	{
	case EConnectionState::CONNECTING_DIRECT:
		return "connecting";
	case EConnectionState::FINDING_ROUTE:
		return "finding_route";
	case EConnectionState::CONNECTED_DIRECT:
		return bRelayed ? "connected_relay" : "connected_direct";
	case EConnectionState::CONNECTION_FAILED:
		return "failed";
	case EConnectionState::CONNECTION_DISCONNECTED:
		return "disconnected";
	default:
		return nullptr;
	}
}

class ConnectionTimeline
{
public:
	static constexpr size_t k_maxEvents = 32;
	static constexpr int64_t k_maxTimeMs = 600000;

	// Records a state change at tMs since the attempt started.
	// A repeat of the last state is ignored. The time never decreases and never exceeds 600000 ms.
	// A full timeline keeps its first 31 entries and replaces the last one, so the newest state is always last.
	void Add(const char* state, int64_t tMs)
	{
		if (state == nullptr || state[0] == '\0')
		{
			return;
		}
		if (!m_events.empty() && m_events.back().state == state)
		{
			return;
		}

		int64_t clampedMs = (std::min)((std::max)(tMs, static_cast<int64_t>(0)), k_maxTimeMs);
		if (!m_events.empty())
		{
			clampedMs = (std::max)(clampedMs, m_events.back().t_ms);
		}

		if (m_events.size() >= k_maxEvents)
		{
			m_events.pop_back();
			// The kept entry before the replaced one can already hold this state.
			if (m_events.back().state == state)
			{
				return;
			}
		}

		ConnectionReportEvent entry;
		entry.state = state;
		entry.t_ms = clampedMs;
		m_events.push_back(entry);
	}

	void Add(EConnectionState state, bool bRelayed, int64_t tMs)
	{
		Add(ConnectionEventState(state, bRelayed), tMs);
	}

	const std::vector<ConnectionReportEvent>& Events() const
	{
		return m_events;
	}

private:
	std::vector<ConnectionReportEvent> m_events;
};

} // namespace GenOnlineRealtime
