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

// FILE: ControlBarData.h ////////////////////////////////////////////////////
// Widget-agnostic snapshot of the control bar for a view other than its .wnd (the RmlUi HUD). The
// ControlBar keeps its state in the .wnd windows it drives; while a view shows, those windows are
// headless (loaded and updated, never drawn) and ControlBar::fillData() copies what they show into
// this once a frame. Buttons are addressed by ControlBarButtonId for ControlBarActions.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/Override.h"
#include "Common/UnicodeString.h"

class Team;

#include "GameClient/ControlBar.h"

class Image;

//-------------------------------------------------------------------------------------------------
enum ControlBarButtonGroup CPP_11(: Int)
{
	CBB_COMMAND,			///< the context command slots, ButtonCommand01..
	CBB_QUEUE,				///< the build queue, ButtonQueue01..
	CBB_SIDE,					///< the buttons round the bar, ControlBarSideButton
	CBB_CONTEXT,			///< the single buttons of a context, ControlBarContextButton
	CBB_INFO,					///< the readouts with a tooltip, ControlBarInfo (not buttons)
	CBB_SHORTCUT,			///< the general's powers bar, GenPowersShortcutBar*.wnd ButtonCommand1..
	CBB_SCIENCE,			///< the promotions panel: rank 1, rank 3 and rank 8 buttons in a row, then Done
	CBB_OBSERVER,			///< the observer's players (ButtonPlayer0..7), then Back (ButtonCancel)

	CBB_COUNT
};

enum ControlBarSideButton CPP_11(: Int)
{
	CB_SIDE_OPTIONS,				///< the in-game menu
	CB_SIDE_IDLE_WORKER,		///< select the next idle worker; its text is the idle count
	CB_SIDE_BEACON,					///< place a beacon (multiplayer)
	CB_SIDE_COMMUNICATOR,		///< diplomacy
	CB_SIDE_GENERAL,				///< the general's promotions
	CB_SIDE_MIN_MAX,				///< collapse or restore the bar

	CB_SIDE_COUNT
};

enum ControlBarContextButton CPP_11(: Int)
{
	CB_CTX_CANCEL_CONSTRUCTION,	///< under construction
	CB_CTX_OCL_BUTTON,					///< OCL timer: sell, or a rally point for tech buildings
	CB_CTX_BEACON_DELETE,				///< a beacon
	CB_CTX_BEACON_CLEAR,				///< clears its text

	CB_CTX_COUNT
};

enum ControlBarInfo CPP_11(: Int)
{
	CB_INFO_MONEY,
	CB_INFO_POWER,
	CB_INFO_EXPERIENCE,

	CB_INFO_COUNT
};

struct ControlBarButtonId
{
	ControlBarButtonGroup group;
	Int index;
};

enum { CONTROL_BAR_VISIBLE_COMMANDS = 14 };
enum
{
	CB_SCIENCE_RANK3_FIRST = MAX_PURCHASE_SCIENCE_RANK_1,
	CB_SCIENCE_RANK8_FIRST = CB_SCIENCE_RANK3_FIRST + MAX_PURCHASE_SCIENCE_RANK_3,
	CB_SCIENCE_DONE = CB_SCIENCE_RANK8_FIRST + MAX_PURCHASE_SCIENCE_RANK_8,
	CB_SCIENCE_COUNT
}; ///< ButtonCommand01..14: column i / 2, row i % 2

//-------------------------------------------------------------------------------------------------
struct ControlBarButtonData
{
	ControlBarButtonData() { clear(); }
	void clear();
	Bool operator==( const ControlBarButtonData &other ) const;
	Bool operator!=( const ControlBarButtonData &other ) const { return !(*this == other); }

	Bool shown;
	Bool enabled;
	Bool checked;						///< check-like command that is on, or pressed
	Bool notReady;					///< disabled only until it recharges: drawn in colour under its clock
	Bool alwaysColor;				///< disabled but not greyed (can't afford)
	Bool flashing;					///< a script made the cameo flash
	const Image *image;			///< the icon
	const Image *overlay;		///< veterancy chevrons and the like, drawn over the icon
	Int clockPercent;				///< -1 without a clock
	Bool clockInverse;			///< the part still to go is darkened (the build and recharge clocks)
	UnicodeString text;			///< drawn on the button (the idle worker count)
	AsciiString hotkey;			///< from the '&' of the command's label
	Int border;							///< CommandButtonMappedBorderType
	const CommandButton *command;
};

enum { CB_OBSERVER_PLAYERS = 8, CB_OBSERVER_BACK = CB_OBSERVER_PLAYERS };

struct ControlBarObserverPlayerData
{
	Bool shown;
	const Image *image;			///< the side's button art
	UnicodeString name;			///< name and team (CONTROLBAR:ObsPlayerLabel)
	Color color;						///< the player's colour
};

struct ControlBarUpgradeData
{
	Bool shown;
	Bool owned;
	const Image *image;
};

//-------------------------------------------------------------------------------------------------
struct ControlBarData
{
	ControlBarData() { clear(); }
	void clear();

	Bool visible;										///< the bar is up (ControlBarParent is shown)
	ControlBarStages stage;
	ControlBarContext context;
	Bool observer;									///< the observer bar
	AsciiString faction;							///< base side of the viewed player ("America", "China", "GLA", ...)

	Bool moneyShown;
	UnicodeString money;						///< GUI:ControlBarMoneyDisplay (with the income, when shown)
	Bool powerShown;
	Int powerProduction;
	Int powerConsumption;
	Int powerState;									///< 0 enough, 1 within the yellow margin, 2 short
	Real powerFill;									///< production on the .wnd meter's log scale, 0..1
	Real powerNeedle;								///< consumption on the same scale

	ControlBarButtonData commands[ CONTROL_BAR_VISIBLE_COMMANDS ];
	Bool commandsShown;							///< the command context is up (commands, inventory, multi select)

	Bool queueShown;
	ControlBarButtonData queue[ MAX_BUILD_QUEUE_BUTTONS ];

	Bool portraitShown;
	const Image *portrait;
	const Image *portraitOverlay;		///< veterancy
	UnicodeString name;							///< display name of the portrait's thing
	ControlBarUpgradeData upgrades[ MAX_RIGHT_HUD_UPGRADE_CAMEOS ];
	Int selectCount;

	UnicodeString contextText;			///< under construction or OCL timer text
	Int contextPercent;							///< their progress, -1 without one
	ControlBarButtonData contextButtons[ CB_CTX_COUNT ];
	Bool beaconEditable;						///< our own beacon: its text can be typed (EditBeaconText)
	UnicodeString beaconText;				///< what EditBeaconText holds

	ControlBarButtonData sideButtons[ CB_SIDE_COUNT ];
	Bool hasRadar;										///< the local player has a working radar
	Bool radarAlert;								///< the radar's under-attack light is lit (it blinks for a while)
	Bool cameoMovie;								///< a script plays a movie in the portrait (InGameUI::playCameoMovie())

	// the build tooltip (ControlBarPopupDescription.wnd, populateBuildTooltipLayout()): shown after the hover delay
	Bool tooltipShown;
	UnicodeString tooltipName;						///< with the '&' of its hotkey
	UnicodeString tooltipCost;						///< empty when free
	UnicodeString tooltipDescription;

	// the general's powers bar
	Bool shortcutsShown;
	ControlBarButtonData shortcuts[ MAX_SPECIAL_POWER_SHORTCUTS ];

	// the promotions panel (GeneralsExpPoints.wnd)
	Bool scienceShown;
	UnicodeString scienceTitle;					///< the rank's name
	ControlBarButtonData sciences[ CB_SCIENCE_COUNT ];

	// the observer bar: its players, or the one looked at (ControlBarObserver.cpp)
	Bool observerListShown;
	ControlBarObserverPlayerData observerPlayers[ CB_OBSERVER_PLAYERS ];
	Bool observerInfoShown;
	UnicodeString observerName;
	Color observerColor;
	const Image *observerFlag;
	UnicodeString observerUnits;
	UnicodeString observerBuildings;
	UnicodeString observerKills;
	UnicodeString observerLosses;
	ControlBarButtonData observerButtons[ CB_OBSERVER_PLAYERS + 1 ];

	Bool replay;										///< a replay plays (ReplayControl.wnd is up; its buttons do nothing)

	Bool generalLit;								///< the general's button blinks while promotion points are unspent
	Int rank;
	Int sciencePoints;
	Real experience;								///< progress to the next rank, 0..1
};
