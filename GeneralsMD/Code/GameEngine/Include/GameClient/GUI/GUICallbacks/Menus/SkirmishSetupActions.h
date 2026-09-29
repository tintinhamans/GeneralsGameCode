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

// FILE: SkirmishSetupActions.h ////////////////////////////////////////////////
// Widget-agnostic skirmish game setup logic: slot occupant/faction/color/team/
// start-position mutation with the same availability/validation rules the .wnd
// callbacks have always used, game option mutation, and start/reset validation.
// No GameWindow/gadget coupling, so LAN and the online lobby (or a future RmlUi
// front end) can drive the same SkirmishGameInfo/GameSlot state through these
// functions instead of duplicating the logic. Moved from
// SkirmishGameOptionsMenu.cpp's handle*Selection()/startPressed()/
// reallyDoStart()/getNextSelectablePlayer() and the ButtonMapStartPosition
// GBM_SELECTED case.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameMemory.h"
#include "GameNetwork/GameInfo.h"

class Money;
class UnicodeString;

namespace SkirmishSetupActions
{
	// Result of validateStart(): whether the game is ready to start, and if not, why.
	enum StartValidationResult
	{
		STARTVALIDATION_READY,
		STARTVALIDATION_MAP_NOT_FOUND,
		STARTVALIDATION_TOO_MANY_PLAYERS
	};

	// Creates (or resets) TheSkirmishGameInfo the same way SkirmishGameOptionsMenu.cpp's
	// SkirmishGameOptionsMenuInit() does, minus the .wnd gadget setup: local player slot from
	// SkirmishPreferences, a default AI in slot 1 (see defaultSlot1AIDifficulty()), the preorder
	// registry flag (see applyPreorderFlag()), saved slot list/map/cash/superweapon prefs, and a
	// fresh RNG seed. Called once by a setup screen's show() (widget-agnostic, so RmlUi and any
	// future front end share it instead of only the .wnd path constructing TheSkirmishGameInfo).
	// Safe to call with TheSkirmishGameInfo already set (e.g. re-entering setup after a match).
	void enterSkirmishSetup();

	// Slot 1's default AI difficulty, tiered off the local player's SkirmishBattleHonors win
	// count exactly like SkirmishGameOptionsMenuInit() (<=5 wins: Easy, <=10: Medium, >10: Brutal).
	// Shared so the .wnd Init and enterSkirmishSetup() apply the same tiering instead of duplicating it.
	SlotState defaultSlot1AIDifficulty();

	// Marks slotIndex as a preorder player if the registry's "Preorder" DWORD is non-zero, same as
	// SkirmishGameOptionsMenuInit(). No-op if game is null.
	void applyPreorderFlag( GameInfo *game, Int slotIndex );

	// Saves SkirmishPreferences (see persistPreferences()) and frees TheSkirmishGameInfo, same as
	// the .wnd ButtonExit/Back handler (SkirmishGameOptionsMenu.cpp's GBM_SELECTED case). Does not
	// pop the shell screen -- callers do that themselves (TheShell->pop() for the .wnd path,
	// onBack() for RmlUi). gameSpeedFPS is the current game-speed control's raw 15..61 value (the
	// .wnd's slider position), persisted the same way ButtonExit's prefs.write() does.
	void leaveSkirmishSetup( Int gameSpeedFPS );

	// Writes SkirmishPreferences, same fields as SkirmishPreferences::write() but without that
	// function's direct SkirmishGameOptionsMenu.cpp slider-gadget lookup: gameSpeedFPS (raw 15..61)
	// is passed in and stored via SkirmishPreferences::setGameSpeedFPS() instead. Used by both
	// leaveSkirmishSetup() and ButtonStart's equivalent (RmlSkirmishSetupScreen::onStart()), since
	// the .wnd persists prefs on Start too, not only on Exit.
	void persistPreferences( Int gameSpeedFPS );

	// getNextSelectablePlayer: the next slot at or after start that the local host can
	// still move into a start position (the local slot, or any AI slot), or -1 if none.
	// Non-hosts can never move anyone, so this always returns -1 for them.
	Int getNextSelectablePlayer( GameInfo *game, Int start );

	// ComboBoxPlayer[i]: set slot i's occupant (open/closed/AI difficulty/human) and name. No-op for
	// the local slot or an unchanged state.
	void selectPlayerState( GameInfo *game, Int slotIndex, SlotState state, const UnicodeString &name );

	// ComboBoxColor[i]: set slot i's color, unless another slot already has it. Returns
	// TRUE if the color was applied.
	Bool selectColor( GameInfo *game, Int slotIndex, Int color );

	// ComboBoxPlayerTemplate[i]: set slot i's faction.
	Bool selectPlayerTemplate( GameInfo *game, Int slotIndex, Int playerTemplate );

	// ComboBoxTeam[i]: set slot i's team.
	Bool selectTeam( GameInfo *game, Int slotIndex, Int team );

	// ButtonMapStartPosition/map preview marker click for slot i: set slot i's start
	// position, unless another slot already occupies it (position < 0 always allowed,
	// since it just clears the slot's own position).
	Bool selectStartPosition( GameInfo *game, Int slotIndex, Int position );

	// Map preview start-position marker click: move whichever slot the local host can
	// move out of position, and move the next selectable player into it, following the
	// exact same rules as SkirmishGameOptionsMenu.cpp's ButtonMapStartPosition handler.
	void handleStartPositionMarkerClick( GameInfo *game, Int position );

	// Map preview start-position marker right click: clear whichever slot the local
	// host can move out of position, following the exact same rules as
	// SkirmishGameOptionsMenu.cpp's ButtonMapStartPosition GBM_SELECTED_RIGHT handler.
	void handleStartPositionMarkerRightClick( GameInfo *game, Int position );

	// ComboBoxStartingCash: set the game's starting cash.
	void setStartingCash( GameInfo *game, const Money &startingCash );

	// CheckboxLimitSuperweapons: set (1) or clear (0) the superweapon restriction.
	void setSuperweaponRestriction( GameInfo *game, Bool restricted );

	// ButtonStart: whether the game is ready to start (map found, player count fits the
	// map), matching startPressed()'s checks exactly.
	StartValidationResult validateStart( GameInfo *game );

	// ButtonStart, once validateStart() returns STARTVALIDATION_READY: post the
	// MSG_NEW_GAME message and start the local game, same as reallyDoStart(). maxFPS is
	// the already-clamped (15..1000, or 1000 for "no limit") game speed slider value.
	void startGame( GameInfo *game, Int maxFPS );

	// ButtonReset: clear the local player's skirmish battle honors.
	void resetBattleHonors();
}
