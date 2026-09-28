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

// FILE: JoinGameActions.h /////////////////////////////////////////////////////
// Widget-agnostic Generals Online join-game-popup actions, extracted from
// PopupJoinGame.cpp's static joinGame()/ButtonCancel+ESC bodies so both
// PopupJoinGame.wnd and the RmlUi popup call the same functions.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

namespace JoinGameActions
{
	// Mirrors PopupJoinGame.cpp's static joinGame(AsciiString) exactly: bails out (closing the
	// overlay) if there's no pending lobby or no lobby interface, otherwise calls
	// NGMP_OnlineServices_LobbyInterface::JoinLobby() with the given password. Always closes
	// GSOVERLAY_GAMEPASSWORD itself (same as the original), so the caller only needs to clear its
	// own parentPopup handle afterward.
	void joinGame( const UnicodeString &password );

	// Mirrors ButtonCancel's GBM_SELECTED body / the ESC handler in PopupJoinGameInput() exactly:
	// GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD) + SetLobbyAttemptHostJoin(FALSE).
	void cancel();
}
