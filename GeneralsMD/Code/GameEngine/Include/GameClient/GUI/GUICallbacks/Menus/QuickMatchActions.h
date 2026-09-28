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

// FILE: QuickMatchActions.h ///////////////////////////////////////////////////
// Widget-agnostic Quick Match (Generals Online) button/lifecycle actions,
// extracted from WOLQuickMatchMenu.cpp's live GENERALS_ONLINE code path -- same
// split as OnlineWelcomeActions.h: each action does the same NGMP call the
// original inline code did, no GameWindow involved, so WOLQuickMatchMenu.cpp's
// .wnd path and a future RmlUi screen can share one implementation. Button state
// (winEnable/winHide/winSetText, listbox refresh) stays with the caller, same as
// OnlineWelcomeActions.
//
// Out of scope for this commit (see the quick match triage note): the 7
// RegisterForMatchmaking*/RegisterForJoinLobbyCallback async callback
// registrations and WOLQuickMatchMenuUpdate()'s polled state -- those move into a
// QuickMatchSession in the next commit, mirroring OnlineGameSetupSession.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchData.h"

#include <functional>
#include <vector>

namespace QuickMatchActions
{
	// Resolves a ComboBoxNumPlayers (repurposed as the playlist selector, see
	// WOLQuickMatchMenu.cpp:1193-1210) selection index into its playlist's maps + player-count
	// floor. Shared by populateQuickMatchMapSelectListbox() and saveQuickMatchOptions(), which
	// previously each reimplemented this inline (and had drifted: only the map-list-display copy
	// handled custom/non-official map paths). Returns a default-constructed (m_valid == FALSE)
	// PlaylistMapInfo if playlistIndex doesn't resolve to a cached playlist.
	QuickMatchData::PlaylistMapInfo getPlaylistMapInfo( Int playlistIndex );

	// Mirrors ButtonStart's GENERALS_ONLINE branch (WOLQuickMatchMenu.cpp:2310-2401) exactly,
	// minus the button winEnable/winHide/winHide the caller still owns. Returns FALSE without
	// calling onComplete if selectedMapIndexes.size() < minSelectedMaps -- caller posts the
	// "You must select at least N maps." message and resets its own buttons, same as before.
	// Returns TRUE once the NGMP StartMatchmaking request itself has been made (onComplete then
	// fires asynchronously with the result, same callback shape as the original inline lambda).
	Bool startMatchmaking( UnsignedShort playlistID, const std::vector<Int> &selectedMapIndexes, Int minSelectedMaps, std::function<void( Bool )> onComplete );

	// Mirrors ButtonStop's GENERALS_ONLINE branch (WOLQuickMatchMenu.cpp:2246-2255): calls
	// NGMP_OnlineServices_MatchmakingInterface::CancelMatchmaking(). Caller still posts
	// GUI:QMAborted and resets its own buttons.
	void cancelMatchmaking();

	// Mirrors ButtonWiden's GENERALS_ONLINE branch (WOLQuickMatchMenu.cpp:2288-2296): calls
	// NGMP_OnlineServices_MatchmakingInterface::WidenSearch(). Caller still posts QM:WIDENINGSEARCH
	// and disables its own Widen button.
	void widenSearch();

	// Mirrors ButtonBuddies' handler (WOLQuickMatchMenu.cpp:2578-2580): GameSpyToggleOverlay(GSOVERLAY_BUDDY).
	void toggleBuddiesOverlay();
}
