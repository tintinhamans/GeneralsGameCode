/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#pragma once

#include "Lib/BaseType.h"

struct LiveCasterGameKey
{
	LiveCasterGameKey() : uid(0), hostIP(0), seed(0) {}
	LiveCasterGameKey(UnsignedInt gameUid, UnsignedInt gameHostIP, UnsignedInt gameSeed)
		: uid(gameUid), hostIP(gameHostIP), seed(gameSeed) {}

	Bool isValid() const { return uid != 0 && hostIP != 0; }
	Bool matches(const LiveCasterGameKey& other) const
	{
		return isValid() && other.isValid() && uid == other.uid
			&& hostIP == other.hostIP && seed == other.seed;
	}

	UnsignedInt uid;
	UnsignedInt hostIP;
	UnsignedInt seed;
};

enum LiveCasterSessionState
{
	LIVE_CASTER_IDLE = 0,
	LIVE_CASTER_DISCOVERING,
	LIVE_CASTER_ROOM,
	LIVE_CASTER_STARTING,
	LIVE_CASTER_LOADING,
	LIVE_CASTER_PLAYING,
	LIVE_CASTER_ENDING,
};

enum WatchExitReason
{
	WATCH_EXIT_USER_BACKED_OUT = 0,
	WATCH_EXIT_HOST_CLOSED_ROOM,
	WATCH_EXIT_MATCH_STARTED_WITHOUT_BOOTSTRAP,
	WATCH_EXIT_MAP_UNAVAILABLE,
	WATCH_EXIT_ALL_SOURCES_LOST,
	WATCH_EXIT_PROTOCOL_INCOMPATIBLE,
	WATCH_EXIT_MATCH_COMPLETED,
	WATCH_EXIT_TRANSPORT_DISAGREEMENT,
};

/// What the UI must do once a watch ends. The session teardown itself is always
/// performed by Caster::finishWatch.
enum WatchExitAction
{
	WATCH_ACTION_STAY = 0,			///< keep the current screen
	WATCH_ACTION_LOBBY,				///< leave the read-only room for the LAN lobby
	WATCH_ACTION_POPUP_LOBBY,		///< tell the user why, then return to the LAN lobby
	WATCH_ACTION_POSTGAME,			///< the running match ends through the normal game exit
};

/// The single policy table for watch exits. `matchEntered` is TRUE once the
/// caster game itself has been entered (no longer the read-only room).
inline WatchExitAction decideWatchExit(WatchExitReason reason, Bool matchEntered)
{
	switch (reason)
	{
	case WATCH_EXIT_USER_BACKED_OUT:
	case WATCH_EXIT_HOST_CLOSED_ROOM:
		return matchEntered ? WATCH_ACTION_STAY : WATCH_ACTION_LOBBY;
	case WATCH_EXIT_PROTOCOL_INCOMPATIBLE:
	case WATCH_EXIT_MATCH_STARTED_WITHOUT_BOOTSTRAP:
		return matchEntered ? WATCH_ACTION_STAY : WATCH_ACTION_POPUP_LOBBY;
	case WATCH_EXIT_ALL_SOURCES_LOST:
	case WATCH_EXIT_TRANSPORT_DISAGREEMENT:
		return matchEntered ? WATCH_ACTION_POSTGAME : WATCH_ACTION_POPUP_LOBBY;
	case WATCH_EXIT_MATCH_COMPLETED:
		return matchEntered ? WATCH_ACTION_POSTGAME : WATCH_ACTION_STAY;
	case WATCH_EXIT_MAP_UNAVAILABLE:
	default:
		return WATCH_ACTION_STAY;
	}
}

class LiveCasterSession
{
public:
	LiveCasterSession() : m_state(LIVE_CASTER_IDLE) {}

	Bool select(const LiveCasterGameKey& key)
	{
		if (!key.isValid())
		{
			return FALSE;
		}
		m_key = key;
		m_state = LIVE_CASTER_DISCOVERING;
		return TRUE;
	}

	void enterRoom()
	{
		if (m_state == LIVE_CASTER_DISCOVERING)
		{
			m_state = LIVE_CASTER_ROOM;
		}
	}

	void beginStarting()
	{
		if (m_state == LIVE_CASTER_DISCOVERING || m_state == LIVE_CASTER_ROOM)
		{
			m_state = LIVE_CASTER_STARTING;
		}
	}

	void beginLoading()
	{
		if (m_state == LIVE_CASTER_STARTING)
		{
			m_state = LIVE_CASTER_LOADING;
		}
	}

	void beginPlaying()
	{
		if (m_state == LIVE_CASTER_STARTING || m_state == LIVE_CASTER_LOADING)
		{
			m_state = LIVE_CASTER_PLAYING;
		}
	}

	void returnToRoom()
	{
		if (m_state == LIVE_CASTER_STARTING || m_state == LIVE_CASTER_LOADING)
		{
			m_state = LIVE_CASTER_ROOM;
		}
	}

	void beginEnding()
	{
		if (isActive())
		{
			m_state = LIVE_CASTER_ENDING;
		}
	}

	void reset()
	{
		m_key = LiveCasterGameKey();
		m_state = LIVE_CASTER_IDLE;
	}

	Bool isActive() const { return m_state != LIVE_CASTER_IDLE; }
	Bool hasGame() const { return m_key.isValid(); }
	Bool isGame(const LiveCasterGameKey& key) const { return m_key.matches(key); }
	const LiveCasterGameKey& gameKey() const { return m_key; }
	LiveCasterSessionState state() const { return m_state; }

private:
	LiveCasterGameKey m_key;
	LiveCasterSessionState m_state;
};
