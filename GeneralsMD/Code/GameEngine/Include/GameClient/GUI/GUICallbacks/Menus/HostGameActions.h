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

// FILE: HostGameActions.h ///////////////////////////////////////////////////
// Widget-agnostic Generals Online host-game-popup action, extracted from
// PopupHostGame.cpp's ButtonCreateGame GBM_SELECTED body + createGame() (the
// GENERALS_ONLINE branch only) so both PopupHostGame.wnd and the RmlUi popup
// call one function with identical validation/preference-writing/CreateLobby
// behavior. PopupHostGame.cpp still calls this passing the values it reads from
// its own gadgets; the RmlUi screen passes its data-model fields instead.
//
// Not shared here: the ladder combo box / ladder password field and
// GSOVERLAY_LADDERSELECT. PopupHostGame.wnd (this fork) has no
// ComboBoxLadderName/TextEntryLadderPassword controls -- PopulateCustomLadderComboBox()'s
// `if (!comboBoxLadderName) return;` guard makes ladder population a no-op, and the
// GCM_SELECTED case that would open GSOVERLAY_LADDERSELECT can never fire without that
// control existing. That code path is dead under GENERALS_ONLINE and intentionally not
// carried into the RmlUi popup.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"

namespace HostGameActions
{
	// Mirrors ButtonCreateGame's GBM_SELECTED body + createGame()'s GENERALS_ONLINE branch
	// exactly: trims the name, and on empty name calls SetLobbyAttemptHostJoin(FALSE) +
	// GSMessageBoxOk("GUI:Error", "Please enter a lobby name.") and returns without creating
	// anything. On a non-empty name it writes CustomMatchPreferences (last lobby name, allow
	// observers, limit armies, use stats), resolves the default/preferred map the same way
	// createGame() does, calls NGMP_OnlineServices_LobbyInterface::CreateLobby(), and shows the
	// "Creating Lobby" GSMessageBoxCancel(). The caller is still responsible for
	// GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS) afterward, same as both GBM_SELECTED branches
	// did (and, for the .wnd, for nulling its own parentPopup).
	void createGame( const UnicodeString &gameName, const UnicodeString &password,
		bool allowObservers, bool useStats, bool limitArmies );
}
