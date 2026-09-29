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

// FILE: GameSetupData.h ////////////////////////////////////////////////////////
// Widget-agnostic skirmish game setup content: what the setup screen displays for
// every slot and for the game options, with no GameWindow/gadget coupling. Built
// from a GameInfo (SkirmishGameInfo today; LAN/online lobby's GameInfo subclasses
// tomorrow) so a future non-.wnd front end (e.g. RmlUi) can render the same thing
// SkirmishGameOptionsMenu.cpp's InitSkirmishGameGadgets()/skirmishUpdateSlotList()/
// updateSkirmishGameOptions() build from GameSlot/GameInfo/MapMetaData directly.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/Money.h"
#include "GameNetwork/GameInfo.h"

#include <vector>

// One slot row: occupant state (open/closed/AI difficulty/human), and the
// faction/color/team/start-position selections that only make sense once
// occupied. Mirrors GameSlot exactly; carried as its own struct so a renderer
// never needs GameSlot/GameInfo internals.
struct GameSetupSlotRow
{
	SlotState m_state = SLOT_OPEN;
	UnicodeString m_name;
	Int m_color = -1;
	Int m_playerTemplate = PLAYERTEMPLATE_RANDOM;
	Int m_teamNumber = -1;
	Int m_startPosition = -1;
	Bool m_isAI = FALSE;

	// True for the slot the local machine controls (the only slot a non-host can edit).
	Bool m_isLocalSlot = FALSE;

	// True if this slot's color/faction/team can be set from here: the local slot, and AI slots
	// for the host. Same rule as UpdateSlotList()'s EnableAcceptControls() calls.
	Bool m_canEdit = FALSE;

	// True if this slot's occupant (open/closed/AI) can be set from here: every slot but the
	// local one, for the host only.
	Bool m_canEditOccupant = FALSE;

	// Colors this slot's color combo offers, in order: -1 (random) first, then every color no other
	// slot has taken; only -1 for an observer. Same list PopulateColorComboBox() builds per slot.
	std::vector<Int> m_colorChoices;
};

// One start-position marker for the map preview: waypoint position expressed as a fraction
// of the map preview's drawn area (0..1, top-left origin), matching SkirmishGameOptionsMenu.cpp's
// positionStartSpotControls() fractional math exactly (only its per-marker overlap nudge, a pure
// .wnd absolute-pixel layout nicety, isn't reproduced -- RmlUi lays these out as CSS percentages).
struct GameSetupStartPositionMarker
{
	Int m_position = -1; // 0-based, matches GameSetupSlotRow::m_startPosition / Player_N_Start (N = position+1)
	Real m_xFraction = 0.0f;
	Real m_yFraction = 0.0f;

	// FALSE for a position the current map doesn't have (index >= map's start-position count).
	// The entry still exists -- see GameSetupOptionsData::m_startPositionMarkers -- so a renderer
	// hides it (data-if) instead of the array changing size when the map changes.
	Bool m_used = FALSE;
};

// One selectable faction entry, for the faction dropdown every slot row shares. Same entries and
// order as PopulatePlayerTemplateComboBox(): Random first, one entry per playable side, then
// Observer when the game allows observers.
struct GameSetupFactionOption
{
	Int m_playerTemplate = PLAYERTEMPLATE_RANDOM;
	UnicodeString m_displayName;
};

// One color entry (TheMultiplayerSettings order, -1 first). A slot row offers the subset in its
// GameSetupSlotRow::m_colorChoices.
struct GameSetupColorOption
{
	Int m_color = -1;
	UnsignedInt m_rgb = 0; // 0x00RRGGBB, from MultiplayerColorDefinition::getColor()
	UnicodeString m_name; // the color's tooltip name; GUI:Random for -1
};

// One selectable starting-cash preset (TheMultiplayerSettings::getStartingMoneyList() order),
// for the starting-cash dropdown. Mirrors PopulateStartingCashComboBox()'s combo entries exactly
// (same list, same FormatStartingCashLabel() text) instead of a free-form amount.
struct GameSetupStartingCashOption
{
	Int m_amount = 0; // Money::countMoney()
	UnicodeString m_label;
};

// Map/options half of the setup screen: what updateSkirmishGameOptions() /
// InitSkirmishGameGadgets() derive from the current map and GameInfo.
struct GameSetupOptionsData
{
	AsciiString m_mapName;
	UnicodeString m_mapDisplayName;
	Bool m_mapFound = FALSE;

	// False for a solo-campaign map picked from the map selector (Skirmish now allows
	// selecting them); gates the per-player gadgets exactly like updateSkirmishGameOptions().
	Bool m_mapIsMultiplayer = TRUE;
	Int m_mapNumPlayers = 0;

	Money m_startingCash;
	Bool m_superweaponsRestricted = FALSE;

	// Start-position markers, always MAX_SLOTS entries (see positionStartSpots()): a bound RmlUi
	// array must never shrink in place (it logs "Data array index out of bounds" and can hit a
	// stale nested data-style/data-class binding) so every map exposes the same fixed-size array,
	// with m_used marking which positions the current map actually has.
	std::vector<GameSetupStartPositionMarker> m_startPositionMarkers;

	// Faction/color option lists, shared by every slot's dropdowns (see InitSkirmishGameGadgets()'s
	// per-slot ComboBoxPlayerTemplate/ComboBoxColor population). Not map-dependent; still carried
	// here (rather than queried live by each renderer) so build() stays the single read-only
	// snapshot point widget code needs.
	std::vector<GameSetupFactionOption> m_factionOptions;
	std::vector<GameSetupColorOption> m_colorOptions;
	std::vector<GameSetupStartingCashOption> m_startingCashOptions;
};

struct GameSetupData
{
	std::vector<GameSetupSlotRow> m_slots; // always MAX_SLOTS entries
	GameSetupOptionsData m_options;

	// Snapshot the current slots and map/options state out of game. Read-only: no side
	// effects, safe to call every frame a renderer needs to refresh.
	// allowObservers adds the Observer faction entry, as PopulatePlayerTemplateComboBox()'s
	// allowObservers does (LAN and online setup; never skirmish).
	static GameSetupData build( GameInfo *game, Bool allowObservers = FALSE );

	// Same fractional math as positionStartSpots( AsciiString, ... )/positionStartSpotControls(),
	// keyed only by map name so a map-browse preview (no GameInfo/slots yet, e.g. the map select
	// screen) can show the same markers as the setup screen's. Always MAX_SLOTS entries (see
	// GameSetupStartPositionMarker::m_used); every entry unused if the map isn't found or isn't
	// multiplayer. Shared by GameSetupData::build() so the two call sites can't drift apart.
	static std::vector<GameSetupStartPositionMarker> computeStartPositionMarkers( AsciiString mapName );
};
