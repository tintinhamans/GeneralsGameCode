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

// FILE: JoinGameData.h ////////////////////////////////////////////////////////
// Widget-agnostic snapshot of PopupJoinGame.wnd's initial state. Mirrors
// PopupJoinGameInit() exactly: the display name of the lobby the player is
// trying to join, straight off NGMP_OnlineServices_LobbyInterface::GetLobbyTryingToJoin().
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

namespace JoinGameData
{
	// Empty if no lobby interface is available (mirrors PopupJoinGameInit's early-out).
	UnicodeString getLobbyName();
}
