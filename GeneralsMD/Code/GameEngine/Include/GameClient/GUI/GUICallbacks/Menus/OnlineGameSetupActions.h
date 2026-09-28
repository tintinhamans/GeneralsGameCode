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

// FILE: OnlineGameSetupActions.h /////////////////////////////////////////////
// Widget-agnostic Generals Online (NGMP) game setup actions: the same
// NGMP_OnlineServices_LobbyInterface calls WOLGameSetupMenu.cpp's handle*Selection()/
// StartPressed()/getNextSelectablePlayer()/getFirstSelectablePlayer()/
// handleGameSetupSlashCommands() and the GCM_SELECTED/GBM_SELECTED/GEM_EDIT_DONE cases
// already send, moved out from under GameWindow so a future non-.wnd front end
// (RmlOnlineGameSetupScreen) can drive the same lobby through these functions instead of
// duplicating the request/validation logic -- same "shared actions" precedent as
// LanGameSetupActions.h/OnlineLobbyActions.h. Widget-specific work (combo box population,
// listbox scrolling, layout creation) stays at the .wnd call site; only network request +
// GameSlot mutation + validation/slash-command text moves here. No GameWindow type appears
// in this header or its .cpp -- GameEngineDevice callers (RmlOnlineGameSetupScreen) can't mix
// NGMP/winsock headers with windows.h in the same translation unit.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"
#include "GameClient/Color.h"
#include "GameNetwork/GameInfo.h"

#include <functional>

class AsciiString;
class Money;
class UnicodeString;
class NGMPGame;

namespace OnlineGameSetupActions
{
	// A chat/system-notice line, same text/color a GadgetListBoxAddEntryText(listboxGameSetupChat, ...)
	// call would have produced -- so both the .wnd listbox and a future RmlUi chat panel render the
	// exact same output for validation messages and slash-command results.
	typedef std::function<void( const UnicodeString &text, Color color )> ChatLineFn;

	// getNextSelectablePlayer/getFirstSelectablePlayer: ButtonMapStartPosition helpers, host-only,
	// ported verbatim from WOLGameSetupMenu.cpp's file-local helpers of the same name.
	Int getNextSelectablePlayer( NGMPGame *game, Int start );
	Int getFirstSelectablePlayer( const GameInfo *game );

	// ComboBoxPlayer[i] (host only): move slot i to state (open/closed/AI difficulty), or -- if
	// slot i is currently occupied by a human -- kick that human first (UpdateCurrentLobby_KickUser +
	// UpdateCurrentLobby_SetSlotState + resetAccepted() + StopCountdown() if running), same as the
	// GCM_SELECTED comboBoxPlayerID case. Returns TRUE if slot i's isAI() flipped (not set on the
	// kick path, which never flips isAI()); outWasAI (when non-null) receives the pre-change isAI()
	// so the caller can reproduce PopulatePlayerTemplateComboBox(i, ..., wasAI && game->getAllowObservers())
	// exactly, same as the .wnd path did.
	Bool selectSlotState( NGMPGame *game, Int slotIndex, SlotState state, Bool *outIsAIChanged, Bool *outWasAI = nullptr );

	// ComboBoxColor[i]: set slot i's color, unless another slot already has it. Routes through
	// UpdateCurrentLobby_MyColor (local slot) or UpdateCurrentLobby_AIColor (host + AI slot), same
	// as handleColorSelection().
	void selectColor( NGMPGame *game, Int slotIndex, Int color );

	// ComboBoxPlayerTemplate[i]: set slot i's faction. Returns TRUE if the slot became, or stopped
	// being, an observer -- caller resets that slot's color/team combo box selection to index 0
	// (widget-only work), same as handlePlayerTemplateSelection().
	Bool selectPlayerTemplate( NGMPGame *game, Int slotIndex, Int playerTemplate );

	// ComboBoxTeam[i]: set slot i's team. Routes through UpdateCurrentLobby_MyTeam/_AITeam, same as
	// handleTeamSelection().
	void selectTeam( NGMPGame *game, Int slotIndex, Int team );

	// ButtonMapStartPosition/map preview marker click for slot i, folded together with
	// handleStartPositionSelection()'s single-slot setter (UpdateCurrentLobby_MyStartPos/_AIStartPos).
	void selectStartPosition( NGMPGame *game, Int slotIndex, Int startPos );

	// Map preview start-position marker click: move whichever slot the local host can move out of
	// position, and move the next selectable player into it. Ported from the GBM_SELECTED
	// buttonMapStartPositionID case.
	void handleStartPositionMarkerClick( NGMPGame *game, Int position );

	// Map preview start-position marker right-click: clear whichever slot the local host can move
	// out of position. Ported from the GBM_SELECTED_RIGHT case.
	void handleStartPositionMarkerRightClick( NGMPGame *game, Int position );

	// ComboBoxStartingCash (host only): set the lobby's starting cash via UpdateCurrentLobby_StartingCash.
	void setStartingCash( const Money &startingCash );

	// CheckboxLimitSuperweapons (host only): set/clear the superweapon restriction via
	// UpdateCurrentLobby_LimitSuperweapons.
	void setSuperweaponRestriction( Bool restricted );

	// ButtonStart (host): mesh-connectivity gate, then the same validation checks/order/messages as
	// StartPressed() (too many players, need human players, need more players, need more teams,
	// sandbox-mode notice, per-human no-map notices), then either the ready/countdown path or the
	// "notify players of start intent" path. StartPressed() has no return value the .wnd path reads
	// -- it only ever mutates listboxGameSetupChat and buttonStart/buttonBack/buttonSelectMap's
	// enabled state -- so those three effects reach the caller through callbacks instead of
	// GameWindow calls:
	struct StartPressCallbacks
	{
		ChatLineFn chatLine;                                         // GadgetListBoxAddEntryText(listboxGameSetupChat, ...)
		std::function<void( Bool enabled )> setStartButtonEnabled;   // buttonStart->winEnable()
		std::function<void( Bool enabled )> setBackButtonEnabled;    // buttonBack->winEnable()
		std::function<void( Bool enabled )> setSelectMapButtonEnabled; // buttonSelectMap->winEnable()
	};
	void pressStart( NGMPGame *game, const StartPressCallbacks &callbacks );

	// ButtonStart (client)/Accept: accept locally + ApplyLocalUserPropertiesToCurrentNetworkRoom(),
	// same as the non-host ButtonStart case.
	void requestAccept( NGMPGame *game );

	// ButtonSelectMap (host only): whether the map-select screen may be opened right now. The
	// caller still owns creating/showing the map-select screen itself (GameWindow/layout work) --
	// this only reproduces the guard the .wnd's disabled ButtonSelectMap already enforces.
	Bool canOpenMapSelect( const NGMPGame *game );

	// WOLMapSelectMenu.cpp's ButtonOK handler (~463-495): sets the map, marks the host's own slot as
	// having it, looks up CRC/size from TheMapCache (zeroed if the map isn't cached), then
	// adjustSlotsForMap()/resetAccepted()/resetStartSpots() and UpdateCurrentLobby_Map() so every
	// client picks up the change over the network. No-op if game is null. Both the .wnd's map-select
	// overlay and RmlOnlineMapSelectScreen call this instead of duplicating the write-back.
	void applySelectedMap( NGMPGame *game, const AsciiString &mapName );

	// ButtonCommunicator: toggle the buddy overlay, same as the GBM_SELECTED buttonCommunicatorID case.
	void toggleCommunicatorOverlay();

	// TextEntryChat GEM_EDIT_DONE / ButtonEmote send: slash commands first (/help /commands /support
	// /friendsonly /public /setpassword /removepassword /maxcameraheight /me, plus the debug-only
	// /steam and unknown-command fallback), output verbatim (text + color) through chatLine so both
	// the .wnd listbox and a future RmlUi chat panel render the same lines. Returns TRUE if the text
	// was a slash command (already fully handled -- caller must not also send it as chat).
	Bool handleSlashCommand( const UnicodeString &text, const ChatLineFn &chatLine );

	// Plain chat send (not a slash command): SendChatMessageToCurrentLobby(text, false), same as the
	// GEM_EDIT_DONE and ButtonEmote non-slash-command paths.
	void sendChat( const UnicodeString &text );
}
