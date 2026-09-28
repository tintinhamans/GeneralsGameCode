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

// FILE: HostGameData.h ///////////////////////////////////////////////////////
// Widget-agnostic snapshot of PopupHostGame.wnd's initial state (GENERALS_ONLINE
// build only -- the ladder combo/ladder-password controls PopupHostGame.cpp still
// carries are absent from the .wnd under this fork and stay unreachable, see
// HostGameActions.h). Mirrors PopupHostGameInit()'s GENERALS_ONLINE branch exactly:
// remembered lobby name (or the "Generals Online Lobby" fallback), Allow Observers/
// Record Stats/Limit Armies checkbox defaults from CustomMatchPreferences, and the
// password field's max length. Limit Armies is always independently togglable in
// this fork (GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES is permanently
// defined, see NextGenMP_defines.h), so unlike the upstream .wnd it is never
// disabled/forced by Record Stats.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

namespace HostGameData
{
	struct InitialState
	{
		UnicodeString gameName; // last remembered lobby name, or "Generals Online Lobby"
		bool allowObservers = false;
		bool useStats = false;
		bool limitArmies = false;
		int passwordMaxLength = 0; // GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH
	};

	InitialState getInitialState();
}
