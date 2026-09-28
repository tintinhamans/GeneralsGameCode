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

#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuickMatchData.h"

#include <functional>
#include <vector>

namespace QuickMatchActions
{
	// Mirrors StaticTextTitle's GENERALS_ONLINE body exactly (WOLQuickMatchMenu.cpp:1008-1011): GUI:
	// QuickMatchTitle formatted with the NGMP display name. Empty if the auth interface isn't available
	// yet. Kept here (not the caller) so GameEngineDevice callers don't need their own NGMP_interfaces.h
	// include -- see RmlQuickMatchScreen.cpp's header comment on why that combination is avoided.
	UnicodeString buildTitle();

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

	// Mirrors ComboBoxNumPlayers' playlist repopulation (WOLQuickMatchMenu.cpp:1160-1176): calls
	// NGMP_OnlineServices_MatchmakingInterface::RetrievePlaylists() and hands back one PlaylistOption
	// per entry, in the same order (so its index lines up with getPlaylistMapInfo()/startMatchmaking()'s
	// playlistIndex). No-op (onComplete never invoked) if the matchmaking interface isn't available.
	void retrievePlaylists( std::function<void( std::vector<QuickMatchData::PlaylistOption> )> onComplete );

	// Mirrors populateQuickMatchMapSelectListbox()'s GENERALS_ONLINE branch exactly (WOLQuickMatchMenu.cpp:
	// 642-711): resolves playlistIndex's maps via getPlaylistMapInfo(), filters to TheMapCache entries with
	// enough player slots, and reads each map's initial selected state from QuickMatchPreferences. Empty if
	// playlistIndex doesn't resolve to a cached playlist.
	std::vector<QuickMatchData::MapOption> getMapSelectOptions( Int playlistIndex );

	// Mirrors saveQuickMatchOptions()'s GENERALS_ONLINE branch exactly (WOLQuickMatchMenu.cpp:720-745):
	// writes each map's selected state into QuickMatchPreferences and clears the last-ladder preference
	// (Quick Match under GO never uses a ladder). Called once when the screen closes, same lifetime as the
	// .wnd's WOLQuickMatchMenuShutdown() -> saveQuickMatchOptions() call.
	void saveMapSelections( const std::vector<QuickMatchData::MapOption> &maps );

	// The five combos below (ComboBoxLadder/MaxPing/MaxDisconnects/Side/Color) are populated and then
	// immediately force-disabled every Init under GENERALS_ONLINE (WOLQuickMatchMenu.cpp:1119-1130) --
	// real, populated controls whose values never reach an actual GO match, not dead placeholders. These
	// mirror the .wnd's own population functions so the RmlUi screen can show the same thing in its
	// disabled selects instead of inventing new placeholder text. The .wnd keeps its own
	// populateQMSideComboBox()/populateQMColorComboBox() (they also drive the non-GO code path via a
	// live LadderInfo*, which these widget-agnostic mirrors don't need since GO's ladder never resolves).

	// Mirrors WOLQuickMatchMenu.cpp:957-994's no-ladders-found branch, which is the only branch reachable
	// under GENERALS_ONLINE (TheLadderList is always empty there): a single disabled entry, "Automatic
	// Ladder" (hardcoded literal in the .wnd too, not a GameText key).
	std::vector<QuickMatchData::ComboOption> getLadderOptions();

	// Mirrors ComboBoxMaxPing's population exactly (WOLQuickMatchMenu.cpp:1071-1091): under
	// GENERALS_ONLINE maxPingEntries is 0 (ping filtering isn't supported), so the ms-step loop never
	// runs and GUI:ANY is the only entry.
	std::vector<QuickMatchData::ComboOption> getMaxPingOptions();

	// Mirrors ComboBoxMaxDisconnects' population exactly (WOLQuickMatchMenu.cpp:1061-1069): GUI:Any plus
	// 5/10/25/50, selected by favMaxDisconnects (QuickmatchPreferences::getMaxDisconnects()).
	std::vector<QuickMatchData::ComboOption> getMaxDisconnectsOptions( Int favMaxDisconnects );

	// Mirrors populateQMSideComboBox() exactly (WOLQuickMatchMenu.cpp:340-399) with li == nullptr (GO
	// never resolves a ladder, so the li-driven faction filtering/disable never applies there). value is
	// PLAYERTEMPLATE_RANDOM or the player-template index (QuickmatchPreferences::getSide() key).
	std::vector<QuickMatchData::ComboOption> getSideOptions( Int favSide );

	// Mirrors populateQMColorComboBox() exactly (WOLQuickMatchMenu.cpp:314-336). value is -1 (random) or
	// the multiplayer color index (QuickmatchPreferences::getColor() key).
	std::vector<QuickMatchData::ComboOption> getColorOptions( Int favColor );
}
