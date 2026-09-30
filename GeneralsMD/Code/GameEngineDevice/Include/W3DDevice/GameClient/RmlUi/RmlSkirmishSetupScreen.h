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

// FILE: RmlSkirmishSetupScreen.h ///////////////////////////////////////////////
// RmlScreen for Data/UI/SkirmishGameOptions.rml. Registered for
// Menus/SkirmishGameOptionsMenu.wnd (MainMenuActions::startSkirmishOptions()
// pushes that path; RmlUiScreenRegistry routes it here instead of loading the
// .wnd). Unlike the .wnd version this never gets a SkirmishGameOptionsMenuInit()
// WIN_CREATE callback, so show() calls SkirmishSetupActions::enterSkirmishSetup()
// itself to create/reset TheSkirmishGameInfo -- everything after that is read
// through GameSetupData::build() and written back through SkirmishSetupActions,
// same as the .wnd callbacks (see GameSetupData.h/SkirmishSetupActions.h).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

class GameInfo;

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlSkirmishSetupScreen : public RmlScreen
{
public:
	static RmlSkirmishSetupScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonExit/Back
	virtual void update() override; // ticks the .hq-header status

private:
	RmlSkirmishSetupScreen() {}

	void refreshFromGameState(); // GameSetupData::build(TheSkirmishGameInfo) -> m_model

	void onSlotOccupantChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotOccupantPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // an open slot's add-AI button: (slot, occupant state)
	void onSlotFactionChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotColorChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotTeamChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotColorPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // swatch click: (slot, color)
	void onSlotTeamPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // team segment click: (slot, team)
	void onStartingCashPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // cash segment click: (amount)
	void onStartingCashSelected(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // cash dropdown: same as a segment, ignores its value echo
	void onStartPositionMarkerClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStartPositionMarkerMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStartingCashChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSuperweaponsChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGameSpeedChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onResetHonors(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onFillAI(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // "Fill with AI": (occupant state)

public:
	// Called by RmlSkirmishMapSelectScreen when it closes (OK or Back): re-shows this screen and
	// refreshes it from TheSkirmishGameInfo, without show()'s SkirmishSetupActions::
	// enterSkirmishSetup() reset (which would wipe the map/slots the map-select screen just wrote).
	// See ReturnToRmlSkirmishSetupScreen() below.
	void returnFromMapSelect();

private:
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status

	struct OptionModel
	{
		int value = 0;
		Rml::String label;
		Rml::String swatch; // color options only: "#RRGGBB", "transparent" for random
		Rml::String icon; // faction options only: mapped image name, empty for random
		bool taken = false; // color options only: another slot holds this color
	};

	// One slot row's worth of fields for the data-for-bound slot table (see load()). Field names
	// mirror GameSetupSlotRow, plus the occupant/faction/color/team labels/hex the RML needs to
	// draw the row without re-deriving them from raw ids.
	struct SlotRowModel
	{
		int slotIndex = 0;
		int occupantState = 0; // SlotState value
		bool isOccupied = false; // state == SLOT_PLAYER or an AI tier
		bool isHumanOccupant = false; // state == SLOT_PLAYER (name comes from the player, not a fixed AI label)
		Rml::String occupantLabel; // player name, or "Open"/"Closed"/"Easy AI"/etc for the dropdown's current value
		bool canEdit = false;
		bool canEditOccupant = false; // GameSetupSlotRow::m_canEditOccupant
		Rml::String playerName;
		int playerTemplate = 0;
		Rml::String factionLabel;
		int color = -1;
		Rml::String colorHex; // "#RRGGBB", "transparent" if color == -1
		Rml::String colorName;
		Rml::Vector<OptionModel> colorOptions; // the whole palette, Random first; taken = not in GameSetupSlotRow::m_colorChoices
		int teamNumber = -1;
		int startPosition = -1;

		// Where the row sits in the list (GameSetupData::displayOrder()): folded away as unused on
		// this map, the first of its team group, or the first unused one.
		bool unused = false;
		bool groupHead = false;
		int groupTeam = -1;
		bool foldHead = false;
	};

	// One start-position marker on the map preview (see GameSetupStartPositionMarker). x_style/
	// y_style are pre-formatted CSS length strings ("12.5%") rather than raw numbers: RmlUi's data
	// binding can't interpolate "{{expr}}" inside a style="..." attribute (that's parsed as a plain
	// RCSS property value, not text), and data-style-<prop> bindings only take a single expression
	// value, not "expr + '%'" concatenation -- so the percent sign is baked in here instead.
	struct StartMarkerModel
	{
		int position = 0;
		Rml::String xStyle; // e.g. "12.500%", bound via data-style-left
		Rml::String yStyle; // e.g. "34.200%", bound via data-style-top
		bool isOccupied = false;
		Rml::String occupantLabel;
		Rml::String colorHex;

		// Mirrors GameSetupStartPositionMarker::m_used: the array is always MAX_SLOTS long (see
		// GameSetupData.h), and the .rml hides an unused entry with data-if instead of the array
		// shrinking, which RmlUi data binding doesn't tolerate mid-document.
		bool used = false;
	};

	struct Model
	{
		Rml::Vector<SlotRowModel> slots;
		Rml::Vector<StartMarkerModel> startMarkers;
		Rml::Vector<OptionModel> factionOptions;
		Rml::Vector<OptionModel> startingCashOptions; // GameSetupStartingCashOption, same preset list as the .wnd combo box

		Rml::String mapName;
		Rml::String mapDisplayName;
		bool mapFound = false;

		int startingCash = 0;
		bool superweaponsRestricted = false;

		// Not part of GameInfo -- read from SkirmishPreferences("FPS") in show(), same as the .wnd
		// slider at SkirmishGameOptionsMenuInit(); 15..60, or 61 for "no limit" (GREATER_NO_FPS_LIMIT),
		// matching setFPSTextBox()'s slider range exactly. Persisted back on Start/Back, same as the
		// .wnd ButtonStart/ButtonExit handlers (see SkirmishSetupActions::persistPreferences()).
		int gameSpeedSliderPos = 61;

		// SkirmishBattleHonors snapshot (see refreshFromGameState()); not part of GameSetupData,
		// same as SkirmishGameOptionsMenu.cpp's own direct SkirmishBattleHonors reads.
		int honorWins = 0;
		int honorLosses = 0;
		int honorWinStreak = 0;
		int honorBestWinStreak = 0;

		// Presentation: the map's player count, team grouping, the unused-slot fold, the open colour
		// popover (-1: none) and why Start would be refused (GameSetupData::startBlockers()).
		int mapNumPlayers = 0;
		bool teamMode = false;
		int unusedCount = 0;
		bool showUnused = false;
		int colorPopoverSlot = -1;
		Rml::Vector<Rml::String> startBlockers;
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlSkirmishSetupScreen();
void CloseRmlSkirmishSetupScreen();

// Called by RmlSkirmishMapSelectScreen on OK/Back instead of OpenRmlSkirmishSetupScreen(), which
// would re-run SkirmishSetupActions::enterSkirmishSetup() and wipe the map/slot state just chosen.
void ReturnToRmlSkirmishSetupScreen();
