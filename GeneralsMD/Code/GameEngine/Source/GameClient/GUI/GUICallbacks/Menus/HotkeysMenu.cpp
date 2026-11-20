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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: HotkeysMenu.cpp
// Description: Options > Hotkeys, rebinding commands and command bar letters into Hotkeys.ini
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MetaEvent.h"
#include "GameLogic/Module/CommandSetUpgrade.h"

namespace
{

typedef MetaMap::DefaultKey Binding;

enum
{
	SIDE_COUNT = 3,
	CARD_SLOTS = 18,
	ICON_POOL = 200,	///< object icons the layout holds for the middle grid
	SECTION_COUNT = 4,
	POWER_KEYS = GameMessage::MSG_META_SPECIAL_POWER_16 - GameMessage::MSG_META_SPECIAL_POWER_1 + 1
};

enum View
{
	VIEW_SIDE,
	VIEW_COMMANDS,
	VIEW_CONFLICTS
};

enum Section
{
	SECTION_POWERS,
	SECTION_HEROES,
	SECTION_BUILDINGS,
	SECTION_UNITS
};

enum CaptureKind
{
	CAPTURE_NONE,
	CAPTURE_COMMAND,
	CAPTURE_BUTTON
};

struct CommandRow
{
	GameMessage::Type meta;
	GameMessage::Type pair;	///< the END_ half of a BEGIN_ hold, MSG_INVALID otherwise
	UnicodeString name;
	Int category;
};

struct BarObject
{
	UnicodeString name;
	Int section;
	const Image *image;
	const CommandButton *slots[ CARD_SLOTS ];
	Bool powers;	///< the army's Powers Menu card, whose slots take the SPECIAL_POWER keys
};

struct BarGeneral
{
	UnicodeString name;
	std::vector<BarObject> objects;
};

/// A row of the key conflicts list and where it leads
struct ConflictRef
{
	Int side;	///< -1 for a command clash
	Int general;
	Int object;
};

const char *const SIDE_BASES[ SIDE_COUNT ] = { "USA", "China", "GLA" };

/// The screen's English text, which a string file label of the same name overrides
struct BuiltInText
{
	const char *label;
	const WideChar *text;
};

const BuiltInText BUILT_IN_TEXT[] =
{
	{ "GUI:Hotkeys", L"Hotkeys" },
	{ "TOOLTIP:Hotkeys", L"Rebind commands and command bar letters" },
	{ "GUI:HotkeysCommands", L"Commands" },
	{ "GUI:HotkeysCommandBar", L"Command bar" },
	{ "GUI:HotkeysChange", L"Change" },
	{ "GUI:HotkeysClear", L"No key" },
	{ "GUI:HotkeysDefault", L"Game's key" },
	{ "GUI:HotkeysResetAll", L"RESET ALL" },
	{ "GUI:HotkeysResetDefault", L"RESET TO DEFAULT" },
	{ "GUI:HotkeysSave", L"SAVE" },
	{ "TOOLTIP:HotkeysSave", L"Write Hotkeys.ini and use the new keys at once." },
	{ "GUI:HotkeysSideUSA", L"USA" },
	{ "GUI:HotkeysSideChina", L"CHINA" },
	{ "GUI:HotkeysSideGLA", L"GLA" },
	{ "GUI:HotkeysGeneral", L"GENERAL" },
	{ "TOOLTIP:HotkeysGeneral", L"Commands that need no unit: selection, control groups, camera, chat." },
	{ "GUI:HotkeysConflicts", L"KEY CONFLICTS" },
	{ "TOOLTIP:HotkeysConflicts", L"Every key that two buttons on one card, or two commands, share." },
	{ "GUI:HotkeysPowers", L"SPECIAL POWERS" },
	{ "GUI:HotkeysPowersCard", L"Special powers" },
	{ "GUI:HotkeysHeroes", L"HEROES" },
	{ "GUI:HotkeysBuildings", L"BUILDINGS" },
	{ "GUI:HotkeysUnits", L"UNITS" },
	{ "GUI:HotkeysFilter", L"Quick filter" },
	{ "GUI:HotkeysKeyIs", L"Key: %s" },
	{ "GUI:HotkeysPickButton", L"Click a button on the card, then press its new letter." },
	{ "GUI:HotkeysPickObject", L"Pick a building or unit." },
	{ "GUI:HotkeysConflictCard", L"%s, %s: %s on %s" },
	{ "GUI:HotkeysConflictCommand", L"Commands: %s shares %s with %s" },
	{ "GUI:HotkeysNoConflicts", L"No key is shared." },
	{ "GUI:HotkeysConflictsHelp", L"Double-click a line to go to it." },
	{ "TOOLTIP:HotkeyChange", L"Wait for a new key for the selected command or button. A double click or Enter does the same." },
	{ "TOOLTIP:HotkeyClear", L"Take the key off the selected command or button." },
	{ "TOOLTIP:HotkeyDefault", L"Put back the game's keys for the selected building or unit, or the selected command." },
	{ "TOOLTIP:HotkeyResetAll", L"Put back the game's own key for every command and button." },
	{ "GUI:HotkeysNone", L"none" },
	{ "GUI:HotkeysQueueReorder", L"Move queue entry earlier" },
	{ "GUI:HotkeysRightClick", L"Right click" },
	{ "GUI:HotkeysPressKey", L"Press a key, a mouse button or the wheel, with Ctrl, Alt or Shift if you like. Esc cancels." },
	{ "GUI:HotkeysPressHold", L"Press a key or side mouse button, or press and let go of Ctrl, Alt or Shift alone to hold it. Esc cancels." },
	{ "GUI:HotkeysPressLetter", L"Press a letter, a side or middle mouse button, or the wheel. Backspace takes the key off, Esc cancels." },
	{ "GUI:HotkeysClash", L"%s is on %s, and only one of them gets it." },
	{ "GUI:HotkeysAlsoOn", L"Same key as %s." },
	{ "GUI:HotkeysGameKey", L"The game's own key is %s." },
	{ "GUI:HotkeysButtonEverywhere", L"A letter holds on every bar that has the button." },
	{ "GUI:HotkeysPowerEverywhere", L"This is %s. Every army fires its own power on that key." },
	{ "GUI:HotkeysGridKeys", L"Grid hotkeys are on, so buttons take the key of their place." },
	{ "GUI:HotkeysGridUnitOrders", L"Grid hotkeys are on, so most buttons take the key of their place. Unit orders use these letters." },
	{ "GUI:HotkeysPick", L"Pick a command or a button, then double-click it or press Change." },
	{ "GUI:HotkeysCategorySELECTION", L"Selection" },
	{ "GUI:HotkeysCategoryTEAM", L"Control groups" },
	{ "GUI:HotkeysCategoryCONTROL", L"Orders" },
	{ "GUI:HotkeysCategoryINFORMATION", L"Information" },
	{ "GUI:HotkeysCategoryINTERFACE", L"Interface" },
	{ "GUI:HotkeysCategoryTAUNT", L"Taunts" },
	{ "GUI:HotkeysCategoryMISC", L"Other" },
	{ "GUI:HotkeysTabALL", L"All" },
	{ "GUI:HotkeysTabSELECTION", L"Selection" },
	{ "GUI:HotkeysTabTEAM", L"Groups" },
	{ "GUI:HotkeysTabCONTROL", L"Orders" },
	{ "GUI:HotkeysTabINFORMATION", L"Info" },
	{ "GUI:HotkeysTabINTERFACE", L"Interface" },
	{ "GUI:HotkeysTabTAUNT", L"Taunts" },
	{ "GUI:HotkeysTabMISC", L"Other" },
	{ "GUI:HotkeysCommandSELECT_HERO", L"Select Hero" },
	{ "GUI:HotkeysCommandADD_TEAM0", L"Add to Team 0" },
	{ "GUI:HotkeysCommandADD_TEAM1", L"Add to Team 1" },
	{ "GUI:HotkeysCommandADD_TEAM2", L"Add to Team 2" },
	{ "GUI:HotkeysCommandADD_TEAM3", L"Add to Team 3" },
	{ "GUI:HotkeysCommandADD_TEAM4", L"Add to Team 4" },
	{ "GUI:HotkeysCommandADD_TEAM5", L"Add to Team 5" },
	{ "GUI:HotkeysCommandADD_TEAM6", L"Add to Team 6" },
	{ "GUI:HotkeysCommandADD_TEAM7", L"Add to Team 7" },
	{ "GUI:HotkeysCommandADD_TEAM8", L"Add to Team 8" },
	{ "GUI:HotkeysCommandADD_TEAM9", L"Add to Team 9" },
	{ "GUI:HotkeysCommandSAVE_VIEW5", L"Set Bookmark 5" },
	{ "GUI:HotkeysCommandVIEW_VIEW5", L"View Bookmark 5" },
	{ "GUI:HotkeysCommandSAVE_VIEW6", L"Set Bookmark 6" },
	{ "GUI:HotkeysCommandVIEW_VIEW6", L"View Bookmark 6" },
	{ "GUI:HotkeysCommandSAVE_VIEW7", L"Set Bookmark 7" },
	{ "GUI:HotkeysCommandVIEW_VIEW7", L"View Bookmark 7" },
	{ "GUI:HotkeysCommandSAVE_VIEW8", L"Set Bookmark 8" },
	{ "GUI:HotkeysCommandVIEW_VIEW8", L"View Bookmark 8" },
	{ "GUI:HotkeysCommandCHAT_ALLIES", L"Chat to allies" },
	{ "GUI:HotkeysCommandCHAT_EVERYONE", L"Chat to everyone" },
	{ "GUI:HotkeysCommandDIPLOMACY", L"Diplomacy" },
	{ "GUI:HotkeysCommandPLACE_BEACON", L"Place beacon" },
	{ "GUI:HotkeysCommandDELETE_BEACON", L"Delete beacon" },
	{ "GUI:HotkeysCommandBEGIN_FACING_DRAG", L"Drag to Face (hold)" },
	{ "GUI:HotkeysCommandBEGIN_FORCEATTACK", L"Force attack (hold)" },
	{ "GUI:HotkeysCommandBEGIN_WAYPOINTS", L"Waypoints (hold)" },
	{ "GUI:HotkeysCommandBEGIN_PREFER_SELECTION", L"Prefer selection (hold)" },
	{ "GUI:HotkeysCommandBEGIN_CAMERA_ROTATE_LEFT", L"Turn camera left" },
	{ "GUI:HotkeysCommandBEGIN_CAMERA_ROTATE_RIGHT", L"Turn camera right" },
	{ "GUI:HotkeysCommandBEGIN_CAMERA_ZOOM_IN", L"Zoom in" },
	{ "GUI:HotkeysCommandBEGIN_CAMERA_ZOOM_OUT", L"Zoom out" },
	{ "GUI:HotkeysCommandCAMERA_RESET", L"Reset camera" },
	{ "GUI:HotkeysCommandTOGGLE_CAMERA_TRACKING_DRAWABLE", L"Camera follows selection" },
	{ "GUI:HotkeysCommandTOGGLE_FAST_FORWARD_REPLAY", L"Fast forward replay" },
	{ "GUI:HotkeysCommandTOGGLE_PAUSE", L"Pause" },
	{ "GUI:HotkeysCommandTOGGLE_PAUSE_ALT", L"Pause, second key" },
	{ "GUI:HotkeysCommandSTEP_FRAME", L"Step one frame" },
	{ "GUI:HotkeysCommandSTEP_FRAME_ALT", L"Step one frame, second key" },
	{ "GUI:HotkeysCommandSELECT_NEXT_IDLE_WORKER", L"Next idle worker" },
	{ "GUI:HotkeysCommandINCREASE_MAX_RENDER_FPS", L"Raise frame rate cap" },
	{ "GUI:HotkeysCommandDECREASE_MAX_RENDER_FPS", L"Lower frame rate cap" },
	{ "GUI:HotkeysCommandINCREASE_LOGIC_TIME_SCALE", L"Speed up game" },
	{ "GUI:HotkeysCommandDECREASE_LOGIC_TIME_SCALE", L"Slow down game" },
	{ "GUI:HotkeysCommandTOGGLE_PLAYER_OBSERVER", L"Switch observed player view" },
	{ "GUI:HotkeysCommandCYCLE_HEALTH_BAR_MODE", L"Cycle health bars" },
	{ "GUI:HotkeysCommandCYCLE_HOUSE_COLOR_MODE", L"Cycle house colors" },
	{ "GUI:HotkeysCommandTAKE_SCREENSHOT_PNG", L"Screenshot as PNG" },
	{ "GUI:HotkeysCommandSPECIAL_POWER", L"Special power %d" },
#if defined(GENERALS_ONLINE)
	{ "GUI:HotkeysCategoryGO", L"GO" },
	{ "GUI:HotkeysTabGO", L"GO" },
	{ "GUI:HotkeysCommandTOGGLE_NETWORK_STATS", L"Network stats" },
	{ "GUI:HotkeysCommandTAKE_SCREENSHOT_ALT", L"Screenshot, second key" },
	{ "GUI:HotkeysCommandTOGGLE_BUDDY_OVERLAY", L"Buddy list" },
	{ "GUI:HotkeysCommandTOGGLE_BUDDY_OVERLAY_ALT", L"Buddy list, second key" },
#endif
};

const Color TEXT_COLOR = GameMakeColor( 254, 254, 254, 255 );
const Color HEADING_COLOR = GameMakeColor( 209, 253, 4, 255 );
const Color WAITING_COLOR = GameMakeColor( 40, 230, 70, 255 );
const Color SELECTED_COLOR = GameMakeColor( 255, 200, 40, 255 );
const Color CHANGED_COLOR = GameMakeColor( 255, 214, 92, 255 );
const Color CLASH_COLOR = GameMakeColor( 255, 72, 48, 255 );
const Color QUIET_COLOR = GameMakeColor( 150, 150, 150, 255 );
const Color BLACK_COLOR = GameMakeColor( 0, 0, 0, 255 );
const Color GOLD_COLOR = GameMakeColor( 100, 90, 4, 255 );

// the middle grid's area and the card's first cell, in the layout's 800 by 600 units
const Int GRID_LEFT = 194;
const Int GRID_TOP = 94;
const Int GRID_WIDTH = 276;
const Int GRID_HEIGHT = 448;
const Int CARD_LEFT = 494;
const Int CARD_TOP = 150;
// the gaps between cells hold the selection ring
const Int CARD_CELL_WIDTH = 27;
const Int CARD_CELL_HEIGHT = 21;
const Int CARD_STEP_X = 30;
const Int CARD_STEP_Y = 25;

// commands the engine names but Zero Hour never acts on
const char *const HIDDEN_COMMANDS[] =
{
	"HELP", "DEPLOY", "FOLLOW", "CHAT_PLAYERS", "BEGIN_PATH_BUILD", "BEGIN_FORCEMOVE"
};

const Int CATEGORY_ORDER[] =
{
	CATEGORY_SELECTION, CATEGORY_TEAM, CATEGORY_CONTROL, CATEGORY_INFORMATION, CATEGORY_INTERFACE, CATEGORY_TAUNT, CATEGORY_MISC,
#if defined(GENERALS_ONLINE)
	CATEGORY_GO,
#endif
};

enum { COMMAND_TAB_COUNT = ARRAY_SIZE( CATEGORY_ORDER ) + 1 };

GameWindow *s_parent = nullptr;
GameWindow *s_sideButtons[ SIDE_COUNT ] = { nullptr };
GameWindow *s_commandsButton = nullptr;
GameWindow *s_conflictsButton = nullptr;
GameWindow *s_generalCombo = nullptr;
GameWindow *s_filter = nullptr;
GameWindow *s_gridBox = nullptr;
GameWindow *s_headings[ SECTION_COUNT ] = { nullptr };
GameWindow *s_icons[ ICON_POOL ] = { nullptr };
GameWindow *s_cardBox = nullptr;

/// Four bars around a picked icon, in the gap beside it: a window that takes no input still swallows clicks
struct Ring
{
	GameWindow *bars[4];
};

Ring s_objectRing = { { nullptr } };
Ring s_slotRing = { { nullptr } };
GameWindow *s_portrait = nullptr;
GameWindow *s_objectName = nullptr;
GameWindow *s_cardCells[ CARD_SLOTS ] = { nullptr };
GameWindow *s_card[ CARD_SLOTS ] = { nullptr };
GameWindow *s_info = nullptr;
GameWindow *s_note = nullptr;
GameWindow *s_commandList = nullptr;
GameWindow *s_commandTabs[ COMMAND_TAB_COUNT ] = { nullptr };	///< All, then one per CATEGORY_ORDER entry
GameWindow *s_conflictList = nullptr;
GameWindow *s_change = nullptr;
GameWindow *s_clear = nullptr;
GameWindow *s_save = nullptr;
GameWindow *s_resetDefault = nullptr;

GameWindow *s_back = nullptr;
GameWindow *s_resetAll = nullptr;
GameWindow *s_catcher = nullptr;	///< over everything while a key is awaited, so the wheel and middle button reach the wait

std::vector<CommandRow> s_rows;
std::map<Int, Binding> s_keys;	///< the page's key for every command, by message type
HotKeyManager::ButtonKeyMap s_buttons;	///< the page's player letters, by lower case button name
std::map<AsciiString, AsciiString> s_buttonNames;	///< lower case button name to the name as the INI spells it
std::vector<BarGeneral> s_sides[ SIDE_COUNT ];
std::vector<ConflictRef> s_conflicts;
std::vector<Int> s_iconObjects;	///< the object each shown icon stands for

View s_view = VIEW_SIDE;
Int s_side = 0;
Int s_object = -1;
Int s_slot = -1;
Int s_commandRow = -1;
Int s_commandTab = 0;	///< 0 is All
CaptureKind s_capture = CAPTURE_NONE;
Int s_holdMods = 0;	///< modifiers pressed alone while a hold command waits for its key
Bool s_holdBroken = FALSE;
UnsignedByte s_boundKey = KEY_NONE;	///< the key that just ended a wait, whose release must not start another
Bool s_dirty = FALSE;
Bool s_conflictsDirty = TRUE;	///< a key changed since the conflicts list was filled
Int s_conflictCount = 0;
Bool s_filling = FALSE;

//-------------------------------------------------------------------------------------------------
// Hotkeys.ini can only name these, so COMMANDUSABLE_EVERYWHERE's other bits never come back
const UnsignedInt NAMED_USABLE = COMMANDUSABLE_SHELL | COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER;

Bool sameBinding( const Binding& a, const Binding& b )
{
	return a.key == b.key && a.modState == b.modState && ( a.usableIn & NAMED_USABLE ) == ( b.usableIn & NAMED_USABLE )
		&& a.transition == b.transition;
}

Bool isBound( const Binding& binding )
{
	return binding.usableIn != COMMANDUSABLE_NONE && ( binding.key != MK_NONE || binding.modState != NONE );
}

Binding defaultOf( GameMessage::Type meta )
{
	Binding binding;
	TheMetaMap->getDefaultKey( meta, &binding );
	return binding;
}

Binding bindingOf( GameMessage::Type meta )
{
	std::map<Int, Binding>::const_iterator it = s_keys.find( meta );
	if( it != s_keys.end() )
	{
		return it->second;
	}
	return defaultOf( meta );
}

Bool isChanged( const CommandRow& row )
{
	return !sameBinding( bindingOf( row.meta ), defaultOf( row.meta ) );
}

GameMessage::Type metaTypeOf( const AsciiString& name )
{
	for( const LookupListRec *rec = MetaMap::getMetaTypeNames(); rec->name; ++rec )
	{
		if( name.compareNoCase( rec->name ) == 0 )
		{
			return (GameMessage::Type)rec->value;
		}
	}
	return GameMessage::MSG_INVALID;
}

const char *lookupName( const LookupListRec *list, Int value )
{
	for( const LookupListRec *rec = list; rec->name; ++rec )
	{
		if( rec->value == value )
		{
			return rec->name;
		}
	}
	return nullptr;
}

UnicodeString wide( const AsciiString& text )
{
	UnicodeString out;
	out.translate( text );
	return out;
}

/// Lower case for matching the quick filter.
UnicodeString folded( const UnicodeString& text )
{
	UnicodeString out;
	for( const WideChar *c = text.str(); *c; ++c )
	{
		out.concat( (WideChar)towlower( *c ) );
	}
	return out;
}

Bool matchesFilter( const UnicodeString& name )
{
	const UnicodeString filter = folded( GadgetTextEntryGetText( s_filter ) );
	if( filter.isEmpty() )
	{
		return TRUE;
	}
	return wcsstr( folded( name ).str(), filter.str() ) != nullptr;
}

/// Words for a binding: "Ctrl + F1", "Alt", "Num 4"; "none" when unbound.
UnicodeString chordText( const Binding& binding )
{
	if( !isBound( binding ) )
	{
		return HotkeysMenuText( "GUI:HotkeysNone" );
	}
	static const char *const WORDS[][2] =
	{
		{ "ESC", "Esc" }, { "BACKSPACE", "Backspace" }, { "ENTER", "Enter" }, { "SPACE", "Space" }, { "TAB", "Tab" },
		{ "LBRACKET", "[" }, { "RBRACKET", "]" }, { "SEMICOLON", ";" }, { "APOSTROPHE", "'" }, { "TICK", "`" },
		{ "BACKSLASH", "\\" }, { "COMMA", "," }, { "PERIOD", "." }, { "SLASH", "/" }, { "MINUS", "-" }, { "EQUAL", "=" },
		{ "UP", "Up" }, { "DOWN", "Down" }, { "LEFT", "Left" }, { "RIGHT", "Right" }, { "HOME", "Home" }, { "END", "End" },
		{ "PGUP", "Page Up" }, { "PGDN", "Page Down" }, { "INS", "Insert" }, { "DEL", "Delete" }, { "CAPS", "Caps Lock" },
		{ "KPPLUS", "Num +" }, { "KPMINUS", "Num -" }, { "KPSLASH", "Num /" }, { "KPSTAR", "Num *" },
		{ "KPDEL", "Num Del" }, { "KPENTER", "Num Enter" },
		{ "MOUSE_LEFT_RIGHT", "LMB + RMB" },
		{ "MOUSE3", "Middle mouse" }, { "MOUSE4", "Mouse 4" }, { "MOUSE5", "Mouse 5" }, { "WHEELUP", "Wheel up" }, { "WHEELDOWN", "Wheel down" },
	};
	AsciiString text;
	if( binding.modState & CTRL )
	{
		text.concat( "Ctrl + " );
	}
	if( binding.modState & ALT )
	{
		text.concat( "Alt + " );
	}
	if( binding.modState & SHIFT )
	{
		text.concat( "Shift + " );
	}
	if( binding.key == MK_NONE )
	{
		// a hold on modifiers alone, without the trailing " + "
		text.truncateBy( 3 );
	}
	else
	{
		const char *name = lookupName( KeyNames, binding.key );
		AsciiString word = name ? name + 4 : "?";
		for( Int i = 0; i < ARRAY_SIZE( WORDS ); ++i )
		{
			if( word.compare( WORDS[i][0] ) == 0 )
			{
				word = WORDS[i][1];
			}
		}
		if( word.getLength() == 3 && word.startsWith( "KP" ) && isdigit( (unsigned char)word.getCharAt( 2 ) ) )
		{
			word.format( "Num %c", word.getCharAt( 2 ) );
		}
		text.concat( word );
	}
	return wide( text );
}

UnicodeString commandName( const MetaMapRec *rec, const char *id )
{
	// Contra's CommandMap.ini names some labels its string file lacks
	static const WideChar MISSING[] = L"MISSING:";
	if( rec && !rec->m_displayName.isEmpty() && wcsncmp( rec->m_displayName.str(), MISSING, 8 ) != 0 )
	{
		return rec->m_displayName;
	}
	if( strncmp( id, "SPECIAL_POWER_", 14 ) == 0 )
	{
		UnicodeString power;
		power.format( HotkeysMenuText( "GUI:HotkeysCommandSPECIAL_POWER" ), atoi( id + 14 ) );
		return power;
	}
	AsciiString label;
	label.format( "GUI:HotkeysCommand%s", id );
	const UnicodeString named = HotkeysMenuText( label.str() );
	if( wcsncmp( named.str(), MISSING, 8 ) != 0 )
	{
		return named;
	}
	// a command with no name anywhere reads as its id in sentence case
	AsciiString plain;
	for( const char *c = id; *c; ++c )
	{
		if( *c == '_' )
		{
			plain.concat( ' ' );
		}
		else
		{
			plain.concat( (char)( c == id ? *c : tolower( (unsigned char)*c ) ) );
		}
	}
	return wide( plain );
}

void buildRows()
{
	s_rows.clear();
	s_keys.clear();
	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
	{
		Binding binding;
		binding.key = rec->m_key;
		binding.transition = rec->m_transition;
		binding.modState = rec->m_modState;
		binding.usableIn = rec->m_usableIn;
		s_keys[ rec->m_meta ] = binding;
	}

	for( const LookupListRec *names = MetaMap::getMetaTypeNames(); names->name; ++names )
	{
		const AsciiString id = names->name;
		if( id.startsWith( "DEMO_" ) || id.startsWith( "DEBUG_" ) || id.startsWith( "CHEAT_" ) || id.startsWith( "END_" ) )
		{
			continue;
		}
		Bool hidden = FALSE;
		for( Int i = 0; i < ARRAY_SIZE( HIDDEN_COMMANDS ); ++i )
		{
			hidden = hidden || id.compare( HIDDEN_COMMANDS[i] ) == 0;
		}
		if( hidden )
		{
			continue;
		}

		CommandRow row;
		row.meta = (GameMessage::Type)names->value;
		row.pair = GameMessage::MSG_INVALID;
		if( id.startsWith( "BEGIN_" ) )
		{
			AsciiString end;
			end.format( "END_%s", id.str() + 6 );
			row.pair = metaTypeOf( end );
		}
		const MetaMapRec *rec = nullptr;
		for( const MetaMapRec *each = TheMetaMap->getFirstMetaMapRec(); each; each = each->m_next )
		{
			if( each->m_meta == row.meta )
			{
				rec = each;
			}
		}
		row.name = commandName( rec, id.str() );
		row.category = rec ? rec->m_category : CATEGORY_MISC;
		s_rows.push_back( row );
	}
}

//-------------------------------------------------------------------------------------------------
UnicodeString buttonName( const CommandButton *button )
{
	const UnicodeString text = TheGameText->fetch( button->getTextLabel() );
	UnicodeString out;
	for( const WideChar *c = text.str(); *c; ++c )
	{
		if( *c != L'&' )
		{
			out.concat( *c );
		}
	}
	if( out.isEmpty() )
	{
		out.translate( button->getName() );
	}
	return out;
}

AsciiString lowerName( const CommandButton *button )
{
	AsciiString name = button->getName();
	name.toLower();
	return name;
}

UnicodeString keyText( const AsciiString& letter )
{
	if( letter.getLength() > 1 )
	{
		return UnicodeString( HotKeyManager::mouseKeyText( HotKeyManager::mouseKeyFromName( letter ) ) );
	}
	return wide( letter );
}

AsciiString shippedLetter( const CommandButton *button )
{
	AsciiString letter = TheHotKeyManager->searchHotKey( button->getTextLabel() );
	letter.toUpper();
	return letter;
}

/// The letter a button answers to with the page's keys; empty for none.
AsciiString buttonLetter( const CommandButton *button )
{
	HotKeyManager::ButtonKeyMap::const_iterator it = s_buttons.find( lowerName( button ) );
	if( it == s_buttons.end() )
	{
		return shippedLetter( button );
	}
	AsciiString letter = it->second;
	letter.toUpper();
	return letter;
}

/// A name a player should read: not EA's "<<ANGRY MOB>>" placeholders, not a missing label.
Bool readableName( const UnicodeString& text )
{
	if( text.isEmpty() || text.getCharAt( 0 ) == L'<' )
	{
		return FALSE;
	}
	static const WideChar MISSING[] = L"MISSING";
	for( Int i = 0; MISSING[i]; ++i )
	{
		if( i >= text.getLength() || text.getCharAt( i ) != MISSING[i] )
		{
			return TRUE;
		}
	}
	return FALSE;
}

Int sideOf( const PlayerTemplate *side )
{
	const AsciiString base = side->getBaseSide();
	for( Int i = 0; i < SIDE_COUNT; ++i )
	{
		if( base.compareNoCase( SIDE_BASES[i] ) == 0 )
		{
			return i;
		}
	}
	return base.compareNoCase( "America" ) == 0 ? 0 : -1;
}

/// A starting choice such as Superweapon's tech paths: an upgrade that swaps command sets, named by its button
struct PathChoice
{
	AsciiString upgrade;
	UnicodeString label;
};

/// What a general's own templates' CommandSetUpgrades do, for finding its paths
struct SideSwaps
{
	std::map<AsciiString, std::set<AsciiString> > swapped;	///< upgrade to the templates it swaps a command set on
	std::map<AsciiString, std::set<AsciiString> > holders;	///< lower case command set name to the templates that start on it
};

/// The command sets a template's CommandSetUpgrades swap in, by the upgrade that triggers each.
void swapsOf( const ThingTemplate *thing, std::map<AsciiString, AsciiString> *swaps )
{
	const ModuleInfo& info = thing->getBehaviorModuleInfo();
	for( Int i = 0; i < info.getCount(); ++i )
	{
		if( info.getNthName( i ).compareNoCase( "CommandSetUpgrade" ) != 0 )
		{
			continue;
		}
		const CommandSetUpgradeModuleData *data = (const CommandSetUpgradeModuleData *)info.getNthData( i );
		// copies that need every trigger at once, like the General's Challenge ones, are not a choice
		if( data == nullptr || data->m_upgradeMuxData.m_requiresAllTriggers )
		{
			continue;
		}
		const std::vector<AsciiString>& triggers = data->m_upgradeMuxData.m_activationUpgradeNames;
		for( size_t t = 0; t < triggers.size(); ++t )
		{
			( *swaps )[ triggers[t] ] = data->m_newCommandSet;
		}
	}
}

/// The Side the general's own templates carry, read off its starting building, as Contra's base generals say America
AsciiString ownSide( const PlayerTemplate *side )
{
	const ThingTemplate *start = TheThingFactory->findTemplate( side->getStartingBuilding(), FALSE );
	return start && start->getDefaultOwningSide().isNotEmpty() ? start->getDefaultOwningSide() : side->getSide();
}

void findSideSwaps( const PlayerTemplate *side, SideSwaps *out )
{
	const AsciiString owner = ownSide( side );
	for( const ThingTemplate *thing = TheThingFactory->firstTemplate(); thing; thing = thing->friend_getNextTemplate() )
	{
		if( thing->getDefaultOwningSide().compareNoCase( owner ) != 0 )
		{
			continue;
		}
		AsciiString setName = thing->friend_getCommandSetString();
		setName.toLower();
		out->holders[ setName ].insert( thing->getName() );
		std::map<AsciiString, AsciiString> swaps;
		swapsOf( thing, &swaps );
		for( std::map<AsciiString, AsciiString>::const_iterator it = swaps.begin(); it != swaps.end(); ++it )
		{
			out->swapped[ it->first ].insert( thing->getName() );
		}
	}
}

/// The command set a template shows on a path: the one the path's upgrade swaps in, else its own.
const CommandSet *commandSetOn( const ThingTemplate *thing, const AsciiString& path )
{
	if( path.isNotEmpty() )
	{
		std::map<AsciiString, AsciiString> swaps;
		swapsOf( thing, &swaps );
		std::map<AsciiString, AsciiString>::const_iterator it = swaps.find( path );
		if( it != swaps.end() )
		{
			return TheControlBar->findCommandSet( it->second );
		}
	}
	return TheControlBar->findCommandSet( thing->friend_getCommandSetString() );
}

/// Rival upgrade buttons on one card that re-card the same other templates: a choice between paths.
void findPaths( const CommandSet *set, const SideSwaps& swaps, std::vector<PathChoice> *paths )
{
	if( !paths->empty() )
	{
		return;
	}
	AsciiString setName = set->getName();
	setName.toLower();
	std::map<AsciiString, std::set<AsciiString> >::const_iterator own = swaps.holders.find( setName );
	std::vector<PathChoice> found;
	std::vector<const std::set<AsciiString> *> recards;
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; ++slot )
	{
		const CommandButton *button = set->getCommandButton( slot );
		// a path is bought for the whole player; per-unit add-ons are object upgrades
		const Bool player = button && button->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE;
		const UpgradeTemplate *upgrade = player ? button->getUpgradeTemplate() : nullptr;
		std::map<AsciiString, std::set<AsciiString> >::const_iterator it = upgrade ? swaps.swapped.find( upgrade->getUpgradeName() ) : swaps.swapped.end();
		if( it == swaps.swapped.end() )
		{
			continue;
		}
		// an upgrade that re-cards only the card's own holders is no path
		Bool others = FALSE;
		for( std::set<AsciiString>::const_iterator name = it->second.begin(); name != it->second.end() && !others; ++name )
		{
			others = own == swaps.holders.end() || !own->second.count( *name );
		}
		if( !others )
		{
			continue;
		}
		PathChoice choice;
		choice.upgrade = upgrade->getUpgradeName();
		choice.label = buttonName( button );
		found.push_back( choice );
		recards.push_back( &it->second );
	}
	for( size_t a = 0; a < found.size(); ++a )
	{
		for( size_t b = 0; b < found.size(); ++b )
		{
			if( a != b && *recards[a] == *recards[b] && found[a].upgrade != found[b].upgrade )
			{
				paths->push_back( found[a] );
				break;
			}
		}
	}
	if( paths->size() < 2 )
	{
		paths->clear();
	}
}

/// The general's tree on a path, then its other Side templates no other path reaches; the base walk finds paths
BarGeneral findGeneral( const PlayerTemplate *side, const AsciiString& path, const std::set<AsciiString> *exclude,
	std::set<AsciiString> *reached, const SideSwaps *swaps, std::vector<PathChoice> *paths )
{
	BarGeneral general;
	general.name = side->getDisplayName();
	std::vector<AsciiString> queue;
	queue.push_back( side->getStartingBuilding() );
	queue.push_back( side->getStartingUnit( 0 ) );
	std::set<AsciiString> seen;
	std::map<AsciiString, const CommandButton *> builtBy;
	std::set<std::pair<const CommandSet *, const Image *> > cards;
	Bool ownedQueued = FALSE;
	for( size_t at = 0; ; ++at )
	{
		if( at == queue.size() )
		{
			if( ownedQueued )
			{
				break;
			}
			ownedQueued = TRUE;
			const AsciiString owner = ownSide( side );
			for( const ThingTemplate *owned = TheThingFactory->firstTemplate(); owned; owned = owned->friend_getNextTemplate() )
			{
				if( owned->getDefaultOwningSide().compareNoCase( owner ) == 0 && !( exclude && exclude->count( owned->getName() ) ) )
				{
					queue.push_back( owned->getName() );
				}
			}
			if( at == queue.size() )
			{
				break;
			}
		}
		const AsciiString thingName = queue[at];
		if( thingName.isEmpty() || seen.count( thingName ) )
		{
			continue;
		}
		seen.insert( thingName );
		if( reached && !ownedQueued )
		{
			reached->insert( thingName );
		}
		const ThingTemplate *thing = TheThingFactory->findTemplate( thingName, FALSE );
		const CommandSet *set = thing ? commandSetOn( thing, path ) : nullptr;
		if( set == nullptr )
		{
			continue;
		}
		if( paths && swaps )
		{
			findPaths( set, *swaps, paths );
		}
		BarObject object;
		object.powers = FALSE;
		Bool any = FALSE;
		for( Int slot = 0; slot < CARD_SLOTS; ++slot )
		{
			const CommandButton *button = set->getCommandButton( slot );
			// a button with no picture is a leftover no player sees
			if( button == nullptr || BitIsSet( button->getOptions(), SCRIPT_ONLY ) || button->getButtonImage() == nullptr )
			{
				object.slots[ slot ] = nullptr;
				continue;
			}
			object.slots[ slot ] = button;
			any = TRUE;
			s_buttonNames[ lowerName( button ) ] = button->getName();
			const GUICommandType type = button->getCommandType();
			const ThingTemplate *built = button->getThingTemplate();
			if( built && ( type == GUI_COMMAND_UNIT_BUILD || type == GUI_COMMAND_DOZER_CONSTRUCT ) )
			{
				queue.push_back( built->getName() );
				if( !builtBy.count( built->getName() ) )
				{
					builtBy[ built->getName() ] = button;
				}
			}
		}
		if( !any )
		{
			continue;
		}
		const CommandButton *builder = builtBy.count( thingName ) ? builtBy[ thingName ] : nullptr;
		object.name = thing->getDisplayName();
		if( !readableName( object.name ) )
		{
			object.name = builder ? buttonName( builder ) : wide( thingName );
		}
		object.image = thing->getButtonImage();
		if( object.image == nullptr && builder )
		{
			object.image = builder->getButtonImage();
		}
		// without a cameo it is no unit or building a player picks
		if( object.image == nullptr )
		{
			continue;
		}
		// a build variation or reskin repeats its base's card and cameo, and has nothing else to rebind
		if( !cards.insert( std::make_pair( set, object.image ) ).second )
		{
			continue;
		}
		if( thing->isKindOf( KINDOF_HERO ) )
		{
			object.section = SECTION_HEROES;
		}
		else if( thing->isKindOf( KINDOF_STRUCTURE ) )
		{
			object.section = SECTION_BUILDINGS;
		}
		else
		{
			object.section = SECTION_UNITS;
		}
		general.objects.push_back( object );
	}
	return general;
}

/// The army's Powers Menu card, first in the general's list, when the army has one.
void addPowers( const PlayerTemplate *side, BarGeneral& general )
{
	const CommandSet *set = TheControlBar->findPowerHotkeySet( side, nullptr );
	BarObject object;
	object.powers = TRUE;
	object.section = SECTION_POWERS;
	object.name = HotkeysMenuText( "GUI:HotkeysPowersCard" );
	object.image = nullptr;
	for( Int slot = 0; slot < CARD_SLOTS; ++slot )
	{
		object.slots[ slot ] = nullptr;
	}
	for( Int number = 1; number <= POWER_KEYS; ++number )
	{
		Int slot = 0;
		const CommandButton *button = TheControlBar->getPowerHotkeyButton( set, number, &slot );
		if( button == nullptr )
		{
			break;
		}
		if( slot < CARD_SLOTS )
		{
			object.slots[ slot ] = button;
			if( object.image == nullptr )
			{
				object.image = button->getButtonImage();
			}
		}
	}
	if( object.image )
	{
		general.objects.insert( general.objects.begin(), object );
	}
}

void addGeneral( Int which, const PlayerTemplate *side, BarGeneral& general )
{
	if( general.objects.empty() )
	{
		return;
	}
	// a boss and a campaign general may share a display name
	for( size_t i = 0; i < s_sides[ which ].size(); ++i )
	{
		if( s_sides[ which ][i].name.compare( general.name ) == 0 )
		{
			general.name.concat( L" (" );
			general.name.concat( wide( side->getSide() ) );
			general.name.concat( L')' );
			break;
		}
	}
	s_sides[ which ].push_back( general );
}

void findBars()
{
	s_buttonNames.clear();
	for( Int i = 0; i < SIDE_COUNT; ++i )
	{
		s_sides[i].clear();
	}
	for( Int index = 0; index < ThePlayerTemplateStore->getPlayerTemplateCount(); ++index )
	{
		const PlayerTemplate *side = ThePlayerTemplateStore->getNthPlayerTemplate( index );
		if( side == nullptr || side->isObserver() )
		{
			continue;
		}
		const Int which = sideOf( side );
		if( which < 0 )
		{
			continue;
		}
		SideSwaps swaps;
		findSideSwaps( side, &swaps );
		std::vector<PathChoice> paths;
		BarGeneral general = findGeneral( side, AsciiString::TheEmptyString, nullptr, nullptr, &swaps, &paths );
		if( paths.empty() )
		{
			addPowers( side, general );
			addGeneral( which, side, general );
			continue;
		}

		// a general with paths is listed once per path, each without the units only another path builds
		std::vector<std::set<AsciiString> > trees( paths.size() );
		for( size_t p = 0; p < paths.size(); ++p )
		{
			findGeneral( side, paths[p].upgrade, nullptr, &trees[p], nullptr, nullptr );
		}
		for( size_t p = 0; p < paths.size(); ++p )
		{
			std::set<AsciiString> exclude;
			for( size_t q = 0; q < paths.size(); ++q )
			{
				if( q == p )
				{
					continue;
				}
				for( std::set<AsciiString>::const_iterator it = trees[q].begin(); it != trees[q].end(); ++it )
				{
					if( !trees[p].count( *it ) )
					{
						exclude.insert( *it );
					}
				}
			}
			BarGeneral onPath = findGeneral( side, paths[p].upgrade, &exclude, nullptr, nullptr, nullptr );
			onPath.name.concat( L": " );
			onPath.name.concat( paths[p].label );
			addPowers( side, onPath );
			addGeneral( which, side, onPath );
		}
	}
}

//-------------------------------------------------------------------------------------------------
Int selectedGeneral()
{
	Int general = -1;
	GadgetComboBoxGetSelectedPos( s_generalCombo, &general );
	if( general < 0 || general >= (Int)s_sides[ s_side ].size() )
	{
		return -1;
	}
	return general;
}

const BarObject *selectedObject()
{
	const Int general = selectedGeneral();
	if( general < 0 || s_object < 0 || s_object >= (Int)s_sides[ s_side ][ general ].objects.size() )
	{
		return nullptr;
	}
	return &s_sides[ s_side ][ general ].objects[ s_object ];
}

Int rowOf( GameMessage::Type meta )
{
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		if( s_rows[i].meta == meta )
		{
			return (Int)i;
		}
	}
	return -1;
}

/// The command row of a powers card slot's SPECIAL_POWER key, -1 for any other slot.
Int powerRow( const BarObject *object, Int slot )
{
	if( !( object && object->powers && slot >= 0 && slot < CARD_SLOTS && object->slots[ slot ] ) )
	{
		return -1;
	}
	Int number = 0;
	for( Int i = 0; i <= slot; ++i )
	{
		if( object->slots[i] )
		{
			++number;
		}
	}
	return rowOf( (GameMessage::Type)( GameMessage::MSG_META_SPECIAL_POWER_1 + number - 1 ) );
}

/// The corner code of a command's chord, 0 for an unbound command.
Int chordCorner( const Binding& binding )
{
	return isBound( binding ) ? HotKeyManager::chordCorner( binding.key, binding.modState ) : 0;
}

/// Letters two or more buttons of a card share, each with those buttons' names.
std::map<AsciiString, UnicodeString> cardClashes( const BarObject *object )
{
	std::map<AsciiString, std::vector<UnicodeString> > byLetter;
	std::map<AsciiString, UnicodeString> clashes;
	// a powers card's keys are commands, whose clashes sameChord reports
	if( object == nullptr || object->powers )
	{
		return clashes;
	}
	for( Int slot = 0; slot < CARD_SLOTS; ++slot )
	{
		if( object->slots[ slot ] == nullptr )
		{
			continue;
		}
		const AsciiString letter = buttonLetter( object->slots[ slot ] );
		if( letter.isNotEmpty() )
		{
			byLetter[ letter ].push_back( buttonName( object->slots[ slot ] ) );
		}
	}
	for( std::map<AsciiString, std::vector<UnicodeString> >::const_iterator it = byLetter.begin(); it != byLetter.end(); ++it )
	{
		if( it->second.size() < 2 )
		{
			continue;
		}
		UnicodeString names;
		for( size_t i = 0; i < it->second.size(); ++i )
		{
			if( i > 0 )
			{
				names.concat( L", " );
			}
			names.concat( it->second[i] );
		}
		clashes[ it->first ] = names;
	}
	return clashes;
}

/// The other commands on the same chord as this row, by name.
UnicodeString sameChord( const CommandRow& row )
{
	UnicodeString names;
	const Binding binding = bindingOf( row.meta );
	if( !isBound( binding ) )
	{
		return names;
	}
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		const Binding other = bindingOf( s_rows[i].meta );
		if( s_rows[i].meta == row.meta || !isBound( other ) || other.key != binding.key || other.modState != binding.modState
			|| ( other.usableIn & binding.usableIn ) == 0 )
		{
			continue;
		}
		if( !names.isEmpty() )
		{
			names.concat( L", " );
		}
		names.concat( s_rows[i].name );
	}
	return names;
}

//-------------------------------------------------------------------------------------------------
/// Put a window at a rectangle given in the layout's 800 by 600 units.
void placeAt( GameWindow *window, Int x, Int y, Int width, Int height )
{
	const Real scaleX = TheDisplay->getWidth() / 800.0f;
	const Real scaleY = TheDisplay->getHeight() / 600.0f;
	Int parentX = 0;
	Int parentY = 0;
	if( window->winGetParent() )
	{
		window->winGetParent()->winGetScreenPosition( &parentX, &parentY );
	}
	window->winSetPosition( REAL_TO_INT( x * scaleX ) - parentX, REAL_TO_INT( y * scaleY ) - parentY );
	window->winSetSize( REAL_TO_INT( width * scaleX ), REAL_TO_INT( height * scaleY ) );
}

void showRing( Ring& ring, Int x, Int y, Int width, Int height, Color color )
{
	const Int thick = 2;
	placeAt( ring.bars[0], x - thick, y - thick, width + 2 * thick, thick );
	placeAt( ring.bars[1], x - thick, y + height, width + 2 * thick, thick );
	placeAt( ring.bars[2], x - thick, y, thick, height );
	placeAt( ring.bars[3], x + width, y, thick, height );
	for( Int i = 0; i < 4; ++i )
	{
		ring.bars[i]->winSetEnabledColor( 0, color );
		ring.bars[i]->winSetEnabledBorderColor( 0, color );
		ring.bars[i]->winHide( FALSE );
	}
}

void hideRing( Ring& ring )
{
	for( Int i = 0; i < 4; ++i )
	{
		ring.bars[i]->winHide( TRUE );
	}
}

void showWindow( GameWindow *window, Bool show )
{
	if( window )
	{
		window->winHide( !show );
	}
}

void setButtonImage( GameWindow *button, const Image *image )
{
	GadgetButtonSetEnabledImage( button, image );
	GadgetButtonSetEnabledSelectedImage( button, image );
	GadgetButtonSetHiliteImage( button, image );
	GadgetButtonSetHiliteSelectedImage( button, image );
	GadgetButtonSetDisabledImage( button, image );
}

void setBorder( GameWindow *button, Color color )
{
	GadgetButtonSetBorder( button, color, color != GAME_COLOR_UNDEFINED );
}

/// The middle grid: each section's heading, then its icons, shrunk until every section fits.
void fillGrid()
{
	for( Int i = 0; i < ICON_POOL; ++i )
	{
		showWindow( s_icons[i], FALSE );
	}
	for( Int s = 0; s < SECTION_COUNT; ++s )
	{
		showWindow( s_headings[s], FALSE );
	}
	s_iconObjects.clear();
	hideRing( s_objectRing );
	const Int general = selectedGeneral();
	if( s_view != VIEW_SIDE || general < 0 )
	{
		return;
	}
	const std::vector<BarObject>& objects = s_sides[ s_side ][ general ].objects;

	std::vector<Int> bySection[ SECTION_COUNT ];
	for( size_t i = 0; i < objects.size(); ++i )
	{
		if( matchesFilter( objects[i].name ) )
		{
			bySection[ objects[i].section ].push_back( (Int)i );
		}
	}

	const Int headingHeight = 18;
	const Int gap = 3;
	Int iconHeight = 40;
	Int iconWidth = 0;
	Int columns = 1;
	for( ; iconHeight > 18; --iconHeight )
	{
		iconWidth = iconHeight * 61 / 48;
		columns = ( GRID_WIDTH + gap ) / ( iconWidth + gap );
		Int total = 0;
		for( Int s = 0; s < SECTION_COUNT; ++s )
		{
			if( !bySection[s].empty() )
			{
				const Int rows = ( (Int)bySection[s].size() + columns - 1 ) / columns;
				total += headingHeight + rows * ( iconHeight + gap ) + gap;
			}
		}
		if( total <= GRID_HEIGHT )
		{
			break;
		}
	}

	Int y = GRID_TOP;
	for( Int s = 0; s < SECTION_COUNT; ++s )
	{
		if( bySection[s].empty() )
		{
			continue;
		}
		placeAt( s_headings[s], GRID_LEFT, y, GRID_WIDTH, headingHeight );
		showWindow( s_headings[s], TRUE );
		y += headingHeight;
		for( size_t i = 0; i < bySection[s].size(); ++i )
		{
			const Int index = (Int)s_iconObjects.size();
			if( index >= ICON_POOL )
			{
				break;
			}
			const BarObject& object = objects[ bySection[s][i] ];
			GameWindow *icon = s_icons[ index ];
			const Int column = (Int)i % columns;
			const Int row = (Int)i / columns;
			const Int iconX = GRID_LEFT + column * ( iconWidth + gap );
			const Int iconY = y + row * ( iconHeight + gap );
			placeAt( icon, iconX, iconY, iconWidth, iconHeight );
			setButtonImage( icon, object.image );
			setBorder( icon, GAME_COLOR_UNDEFINED );
			if( bySection[s][i] == s_object )
			{
				showRing( s_objectRing, iconX, iconY, iconWidth, iconHeight, SELECTED_COLOR );
			}
			icon->winSetTooltip( object.name );
			showWindow( icon, TRUE );
			s_iconObjects.push_back( bySection[s][i] );
		}
		const Int rows = ( (Int)bySection[s].size() + columns - 1 ) / columns;
		y += rows * ( iconHeight + gap ) + gap;
	}
}

void cardRect( Int slot, Int *left, Int *top );

/// The right panel: the picked object's portrait, name and command card with each button's letter.
void fillCard()
{
	const BarObject *object = s_view == VIEW_SIDE ? selectedObject() : nullptr;
	showWindow( s_portrait, object != nullptr );
	showWindow( s_objectName, object != nullptr );
	if( object )
	{
		setButtonImage( s_portrait, object->image );
		GadgetStaticTextSetText( s_objectName, object->name );
	}
	const std::map<AsciiString, UnicodeString> clashes = cardClashes( object );
	hideRing( s_slotRing );
	for( Int slot = 0; slot < CARD_SLOTS; ++slot )
	{
		const CommandButton *button = object ? object->slots[ slot ] : nullptr;
		showWindow( s_cardCells[ slot ], s_view == VIEW_SIDE );
		showWindow( s_card[ slot ], button != nullptr );
		if( button == nullptr )
		{
			continue;
		}
		const AsciiString letter = buttonLetter( button );
		const Bool waiting = s_capture != CAPTURE_NONE && slot == s_slot;
		const Int power = powerRow( object, slot );
		setButtonImage( s_card[ slot ], button->getButtonImage() );
		Color border = GAME_COLOR_UNDEFINED;
		if( slot == s_slot )
		{
			Int left = 0;
			Int top = 0;
			cardRect( slot, &left, &top );
			showRing( s_slotRing, left, top, CARD_CELL_WIDTH, CARD_CELL_HEIGHT, waiting ? WAITING_COLOR : SELECTED_COLOR );
		}
		UnicodeString tip = buttonName( button );
		if( power >= 0 )
		{
			const CommandRow& row = s_rows[ power ];
			const Binding binding = bindingOf( row.meta );
			GadgetButtonSetCornerLetter( s_card[ slot ], waiting ? '?' : chordCorner( binding ) );
			if( !sameChord( row ).isEmpty() )
			{
				border = CLASH_COLOR;
			}
			else if( isChanged( row ) )
			{
				border = CHANGED_COLOR;
			}
			if( isBound( binding ) )
			{
				tip.concat( L" (" );
				tip.concat( chordText( binding ) );
				tip.concat( L')' );
			}
		}
		else
		{
			GadgetButtonSetCornerLetter( s_card[ slot ], waiting ? '?' : HotKeyManager::cornerOf( letter ) );
			if( clashes.count( letter ) )
			{
				border = CLASH_COLOR;
			}
			else if( s_buttons.count( lowerName( button ) ) )
			{
				border = CHANGED_COLOR;
			}
			if( letter.isNotEmpty() )
			{
				tip.concat( L" (" );
				tip.concat( keyText( letter ) );
				tip.concat( L')' );
			}
		}
		setBorder( s_card[ slot ], border );
		s_card[ slot ]->winSetTooltip( tip );
	}
}

/// A command's tab, 1 and up in CATEGORY_ORDER; Other takes a category the list does not show.
Int commandTabOf( const CommandRow& row )
{
	Int other = 0;
	for( Int i = 0; i < ARRAY_SIZE( CATEGORY_ORDER ); ++i )
	{
		if( row.category == CATEGORY_ORDER[i] )
		{
			return i + 1;
		}
		if( CATEGORY_ORDER[i] == CATEGORY_MISC )
		{
			other = i + 1;
		}
	}
	return other;
}

void fillCommands()
{
	s_filling = TRUE;
	const Int top = GadgetListBoxGetTopVisibleEntry( s_commandList );
	GadgetListBoxReset( s_commandList );
	Int selectedRow = -1;
	for( Int c = 0; c < ARRAY_SIZE( CATEGORY_ORDER ); ++c )
	{
		if( s_commandTab > 0 && s_commandTab != c + 1 )
		{
			continue;
		}
		Bool headed = FALSE;
		for( size_t i = 0; i < s_rows.size(); ++i )
		{
			const CommandRow& row = s_rows[i];
			if( commandTabOf( row ) != c + 1 || !matchesFilter( row.name ) )
			{
				continue;
			}
			if( !headed && s_commandTab == 0 )
			{
				AsciiString label;
				label.format( "GUI:HotkeysCategory%s", lookupName( CategoryListName, CATEGORY_ORDER[c] ) );
				const Int heading = GadgetListBoxAddEntryText( s_commandList, HotkeysMenuText( label.str() ), HEADING_COLOR, -1, 0 );
				GadgetListBoxSetItemData( s_commandList, (void *)0, heading, 0 );
				headed = TRUE;
			}
			const Binding binding = bindingOf( row.meta );
			const Bool waiting = s_capture == CAPTURE_COMMAND && (Int)i == s_commandRow;
			Color keyColor = TEXT_COLOR;
			if( waiting )
			{
				keyColor = WAITING_COLOR;
			}
			else if( !sameChord( row ).isEmpty() )
			{
				keyColor = CLASH_COLOR;
			}
			else if( !isBound( binding ) )
			{
				keyColor = QUIET_COLOR;
			}
			const Int at = GadgetListBoxAddEntryText( s_commandList, row.name, isChanged( row ) ? CHANGED_COLOR : TEXT_COLOR, -1, 0 );
			GadgetListBoxAddEntryText( s_commandList, waiting ? UnicodeString( L"..." ) : chordText( binding ), keyColor, at, 1 );
			GadgetListBoxSetItemData( s_commandList, (void *)(intptr_t)( i + 1 ), at, 0 );
			if( (Int)i == s_commandRow )
			{
				selectedRow = at;
			}
		}
		// the queue reorder click is fixed, so its row carries no command to pick
		if( CATEGORY_ORDER[c] == CATEGORY_INTERFACE && TheGlobalData->m_queueReorder )
		{
			const UnicodeString name = HotkeysMenuText( "GUI:HotkeysQueueReorder" );
			if( matchesFilter( name ) )
			{
				if( !headed && s_commandTab == 0 )
				{
					AsciiString label;
					label.format( "GUI:HotkeysCategory%s", lookupName( CategoryListName, CATEGORY_ORDER[c] ) );
					const Int heading = GadgetListBoxAddEntryText( s_commandList, HotkeysMenuText( label.str() ), HEADING_COLOR, -1, 0 );
					GadgetListBoxSetItemData( s_commandList, (void *)0, heading, 0 );
					headed = TRUE;
				}
				const Int at = GadgetListBoxAddEntryText( s_commandList, name, QUIET_COLOR, -1, 0 );
				GadgetListBoxAddEntryText( s_commandList, HotkeysMenuText( "GUI:HotkeysRightClick" ), QUIET_COLOR, at, 1 );
				GadgetListBoxSetItemData( s_commandList, (void *)0, at, 0 );
			}
		}
	}
	if( selectedRow >= 0 )
	{
		GadgetListBoxSetSelected( s_commandList, selectedRow );
	}
	GadgetListBoxSetTopVisibleEntry( s_commandList, top );
	s_filling = FALSE;
}

/// The tabs over the command list: the current one is lit, and a tab's words tell of a clash or a changed key under it.
void fillCommandTabs()
{
	const Color restFill = GameMakeColor( 2, 2, 2, 150 );
	const Color hoverFill = GameMakeColor( 44, 32, 0, 170 );
	const Color hoverBorder = GameMakeColor( 214, 170, 24, 255 );
	const Color currentFill = GameMakeColor( 112, 56, 0, 150 );
	const Color currentBorder = GameMakeColor( 255, 170, 0, 255 );

	Bool clash[ COMMAND_TAB_COUNT ] = { FALSE };
	Bool changed[ COMMAND_TAB_COUNT ] = { FALSE };
	if( s_view == VIEW_COMMANDS )
	{
		for( size_t i = 0; i < s_rows.size(); ++i )
		{
			const Int tab = commandTabOf( s_rows[i] );
			if( !sameChord( s_rows[i] ).isEmpty() )
			{
				clash[0] = TRUE;
				clash[ tab ] = TRUE;
			}
			if( isChanged( s_rows[i] ) )
			{
				changed[0] = TRUE;
				changed[ tab ] = TRUE;
			}
		}
	}

	for( Int i = 0; i < COMMAND_TAB_COUNT; ++i )
	{
		GameWindow *tab = s_commandTabs[i];
		showWindow( tab, s_view == VIEW_COMMANDS );
		const Bool current = i == s_commandTab;
		Color text = TEXT_COLOR;
		if( clash[i] )
		{
			text = CLASH_COLOR;
		}
		else if( changed[i] )
		{
			text = CHANGED_COLOR;
		}
		tab->winSetEnabledTextColors( text, BLACK_COLOR );
		tab->winSetHiliteTextColors( text, BLACK_COLOR );
		for( Int state = 0; state < 2; ++state )
		{
			tab->winSetEnabledColor( state, current ? currentFill : restFill );
			tab->winSetEnabledBorderColor( state, current ? currentBorder : GOLD_COLOR );
			tab->winSetHiliteColor( state, current ? currentFill : hoverFill );
			tab->winSetHiliteBorderColor( state, current ? currentBorder : hoverBorder );
		}
	}
}

void showCommandTab( Int tab )
{
	s_view = VIEW_COMMANDS;
	if( tab == s_commandTab )
	{
		return;
	}
	s_commandTab = tab;
	// a command picked under another tab is not in this list
	if( s_commandRow >= 0 && tab > 0 && commandTabOf( s_rows[ s_commandRow ] ) != tab )
	{
		s_commandRow = -1;
	}
	s_filling = TRUE;
	GadgetListBoxSetTopVisibleEntry( s_commandList, 0 );
	s_filling = FALSE;
}

/// Every card with a shared letter and every shared command chord; returns how many.
Int fillConflicts()
{
	s_filling = TRUE;
	GadgetListBoxReset( s_conflictList );
	s_conflicts.clear();
	UnicodeString line;
	for( Int side = 0; side < SIDE_COUNT; ++side )
	{
		for( size_t general = 0; general < s_sides[side].size(); ++general )
		{
			const BarGeneral& bar = s_sides[side][general];
			for( size_t object = 0; object < bar.objects.size(); ++object )
			{
				const std::map<AsciiString, UnicodeString> clashes = cardClashes( &bar.objects[object] );
				for( std::map<AsciiString, UnicodeString>::const_iterator it = clashes.begin(); it != clashes.end(); ++it )
				{
					line.format( HotkeysMenuText( "GUI:HotkeysConflictCard" ), bar.name.str(), bar.objects[object].name.str(),
						keyText( it->first ).str(), it->second.str() );
					GadgetListBoxAddEntryText( s_conflictList, line, TEXT_COLOR, -1, 0 );
					ConflictRef ref = { side, (Int)general, (Int)object };
					s_conflicts.push_back( ref );
				}
			}
		}
	}
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		const UnicodeString others = sameChord( s_rows[i] );
		if( others.isEmpty() )
		{
			continue;
		}
		line.format( HotkeysMenuText( "GUI:HotkeysConflictCommand" ), s_rows[i].name.str(), chordText( bindingOf( s_rows[i].meta ) ).str(), others.str() );
		GadgetListBoxAddEntryText( s_conflictList, line, TEXT_COLOR, -1, 0 );
		ConflictRef ref = { -1, (Int)i, 0 };
		s_conflicts.push_back( ref );
	}
	if( s_conflicts.empty() )
	{
		GadgetListBoxAddEntryText( s_conflictList, HotkeysMenuText( "GUI:HotkeysNoConflicts" ), QUIET_COLOR, -1, 0 );
	}
	s_filling = FALSE;
	return (Int)s_conflicts.size();
}

void appendLine( UnicodeString& text, const UnicodeString& line )
{
	if( !text.isEmpty() )
	{
		text.concat( L"\n" );
	}
	text.concat( line );
}

/// The words beside the card or under the commands: the selection's key, what it clashes with, what to press.
void updateInfo()
{
	UnicodeString text;
	UnicodeString line;
	Bool canChange = FALSE;
	Bool canReset = FALSE;
	if( s_view == VIEW_SIDE )
	{
		const BarObject *object = selectedObject();
		const CommandButton *button = ( object && s_slot >= 0 ) ? object->slots[ s_slot ] : nullptr;
		const Int power = powerRow( object, s_slot );
		if( power >= 0 )
		{
			const CommandRow& row = s_rows[ power ];
			canChange = TRUE;
			appendLine( text, buttonName( button ) );
			if( s_capture != CAPTURE_NONE )
			{
				appendLine( text, HotkeysMenuText( "GUI:HotkeysPressKey" ) );
			}
			else
			{
				line.format( HotkeysMenuText( "GUI:HotkeysKeyIs" ), chordText( bindingOf( row.meta ) ).str() );
				appendLine( text, line );
				const UnicodeString others = sameChord( row );
				if( !others.isEmpty() )
				{
					line.format( HotkeysMenuText( "GUI:HotkeysAlsoOn" ), others.str() );
					appendLine( text, line );
				}
				if( isChanged( row ) )
				{
					line.format( HotkeysMenuText( "GUI:HotkeysGameKey" ), chordText( defaultOf( row.meta ) ).str() );
					appendLine( text, line );
				}
				line.format( HotkeysMenuText( "GUI:HotkeysPowerEverywhere" ), row.name.str() );
				appendLine( text, line );
			}
		}
		else if( button )
		{
			canChange = TRUE;
			appendLine( text, buttonName( button ) );
			if( s_capture == CAPTURE_BUTTON )
			{
				appendLine( text, HotkeysMenuText( "GUI:HotkeysPressLetter" ) );
			}
			else
			{
				const AsciiString letter = buttonLetter( button );
				line.format( HotkeysMenuText( "GUI:HotkeysKeyIs" ), letter.isEmpty() ? HotkeysMenuText( "GUI:HotkeysNone" ).str() : keyText( letter ).str() );
				appendLine( text, line );
				if( s_buttons.count( lowerName( button ) ) )
				{
					const AsciiString shipped = shippedLetter( button );
					line.format( HotkeysMenuText( "GUI:HotkeysGameKey" ), shipped.isEmpty() ? HotkeysMenuText( "GUI:HotkeysNone" ).str() : wide( shipped ).str() );
					appendLine( text, line );
				}
				appendLine( text, HotkeysMenuText( "GUI:HotkeysButtonEverywhere" ) );
			}
		}
		else if( object )
		{
			appendLine( text, HotkeysMenuText( "GUI:HotkeysPickButton" ) );
		}
		else
		{
			appendLine( text, HotkeysMenuText( "GUI:HotkeysPickObject" ) );
		}
		if( object )
		{
			for( Int slot = 0; slot < CARD_SLOTS && !canReset; ++slot )
			{
				const Int slotPower = powerRow( object, slot );
				if( slotPower >= 0 )
				{
					canReset = isChanged( s_rows[ slotPower ] );
				}
				else
				{
					canReset = object->slots[ slot ] && s_buttons.count( lowerName( object->slots[ slot ] ) );
				}
			}
		}
		if( s_capture == CAPTURE_NONE )
		{
			const std::map<AsciiString, UnicodeString> clashes = cardClashes( object );
			for( std::map<AsciiString, UnicodeString>::const_iterator it = clashes.begin(); it != clashes.end(); ++it )
			{
				line.format( HotkeysMenuText( "GUI:HotkeysClash" ), keyText( it->first ).str(), it->second.str() );
				appendLine( text, line );
			}
			if( TheGlobalData->m_gridHotkeysEnabled && !( object && object->powers ) )
			{
				appendLine( text, HotkeysMenuText( TheGlobalData->m_gridHotkeyUnitOrders ? "GUI:HotkeysGridUnitOrders" : "GUI:HotkeysGridKeys" ) );
			}
		}
		GadgetStaticTextSetText( s_info, text );
	}
	else if( s_view == VIEW_COMMANDS )
	{
		if( s_commandRow >= 0 )
		{
			const CommandRow& row = s_rows[ s_commandRow ];
			canChange = TRUE;
			canReset = isChanged( row );
			if( s_capture == CAPTURE_COMMAND )
			{
				text = HotkeysMenuText( row.pair != GameMessage::MSG_INVALID ? "GUI:HotkeysPressHold" : "GUI:HotkeysPressKey" );
			}
			else
			{
				text.format( L"%s: %s.", row.name.str(), chordText( bindingOf( row.meta ) ).str() );
				const UnicodeString others = sameChord( row );
				if( !others.isEmpty() )
				{
					line.format( HotkeysMenuText( "GUI:HotkeysAlsoOn" ), others.str() );
					text.concat( L"  " );
					text.concat( line );
				}
				if( canReset )
				{
					line.format( HotkeysMenuText( "GUI:HotkeysGameKey" ), chordText( defaultOf( row.meta ) ).str() );
					text.concat( L"  " );
					text.concat( line );
				}
			}
		}
		else
		{
			text = HotkeysMenuText( "GUI:HotkeysPick" );
		}
		GadgetStaticTextSetText( s_note, text );
	}
	else
	{
		GadgetStaticTextSetText( s_note, HotkeysMenuText( "GUI:HotkeysConflictsHelp" ) );
	}

	s_change->winEnable( canChange );
	s_clear->winEnable( canChange );
	s_resetDefault->winEnable( canReset );
	s_save->winEnable( s_dirty );
}

/// Show the windows of the current view and fill them.
void refresh()
{
	const Bool side = s_view == VIEW_SIDE;
	showWindow( s_generalCombo, side );
	showWindow( s_gridBox, side );
	showWindow( s_cardBox, side );
	showWindow( s_info, side );
	showWindow( s_commandList, s_view == VIEW_COMMANDS );
	showWindow( s_conflictList, s_view == VIEW_CONFLICTS );
	showWindow( s_note, !side );
	showWindow( s_change, s_view != VIEW_CONFLICTS );
	showWindow( s_clear, s_view != VIEW_CONFLICTS );
	showWindow( s_catcher, s_capture != CAPTURE_NONE );

	for( Int i = 0; i < SIDE_COUNT; ++i )
	{
		const Color color = side && i == s_side ? SELECTED_COLOR : TEXT_COLOR;
		s_sideButtons[i]->winSetEnabledTextColors( color, BLACK_COLOR );
	}
	s_commandsButton->winSetEnabledTextColors( s_view == VIEW_COMMANDS ? SELECTED_COLOR : TEXT_COLOR, BLACK_COLOR );

	fillCommandTabs();
	fillGrid();
	fillCard();
	if( s_view == VIEW_COMMANDS )
	{
		fillCommands();
	}
	if( s_conflictsDirty )
	{
		s_conflictCount = fillConflicts();
		s_conflictsDirty = FALSE;
	}
	s_conflictsButton->winSetEnabledTextColors( s_view == VIEW_CONFLICTS ? SELECTED_COLOR : ( s_conflictCount > 0 ? CLASH_COLOR : TEXT_COLOR ), BLACK_COLOR );
	updateInfo();
}

void fillGenerals()
{
	s_filling = TRUE;
	GadgetComboBoxReset( s_generalCombo );
	for( size_t i = 0; i < s_sides[ s_side ].size(); ++i )
	{
		GadgetComboBoxAddEntry( s_generalCombo, s_sides[ s_side ][i].name, TEXT_COLOR );
	}
	GadgetComboBoxSetSelectedPos( s_generalCombo, 0 );
	s_filling = FALSE;
}

void pickObject( Int object )
{
	s_object = object;
	s_slot = -1;
}

void showSide( Int side )
{
	s_view = VIEW_SIDE;
	s_side = side;
	fillGenerals();
	pickObject( s_sides[ s_side ].empty() ? -1 : 0 );
}

//-------------------------------------------------------------------------------------------------
void startCapture()
{
	if( s_view == VIEW_COMMANDS && s_commandRow >= 0 )
	{
		s_capture = CAPTURE_COMMAND;
	}
	else if( s_view == VIEW_SIDE && powerRow( selectedObject(), s_slot ) >= 0 )
	{
		s_commandRow = powerRow( selectedObject(), s_slot );
		s_capture = CAPTURE_COMMAND;
	}
	else if( s_view == VIEW_SIDE && selectedObject() && s_slot >= 0 && selectedObject()->slots[ s_slot ] )
	{
		s_capture = CAPTURE_BUTTON;
	}
	else
	{
		return;
	}
	s_holdMods = 0;
	s_holdBroken = FALSE;
	// every key reaches the screen while it waits, Enter and the arrows included
	TheWindowManager->winSetFocus( s_parent );
	refresh();
}

void setCommand( GameMessage::Type meta, MappableKeyType key, Int mods, MappableKeyTransition transition )
{
	Binding binding = defaultOf( meta );
	binding.key = key;
	binding.modState = (MappableKeyModState)mods;
	binding.transition = transition;
	if( binding.usableIn == COMMANDUSABLE_NONE )
	{
		binding.usableIn = COMMANDUSABLE_GAME;
	}
	s_keys[ meta ] = binding;
}

void clearCommand( GameMessage::Type meta )
{
	Binding binding = defaultOf( meta );
	binding.key = MK_NONE;
	binding.modState = NONE;
	binding.usableIn = COMMANDUSABLE_NONE;
	s_keys[ meta ] = binding;
}

/// Bind the selected command to this chord, or the selected button to this letter (empty for none).
void applyKey( MappableKeyType key, Int mods, const AsciiString& letter )
{
	if( s_capture == CAPTURE_COMMAND )
	{
		const CommandRow& row = s_rows[ s_commandRow ];
		const Binding shipped = defaultOf( row.meta );
		setCommand( row.meta, key, mods, isBound( shipped ) ? shipped.transition : DOWN );
		if( row.pair != GameMessage::MSG_INVALID )
		{
			setCommand( row.pair, key, mods, UP );
		}
	}
	else if( s_capture == CAPTURE_BUTTON )
	{
		s_buttons[ lowerName( selectedObject()->slots[ s_slot ] ) ] = letter;
	}
	s_capture = CAPTURE_NONE;
	s_dirty = TRUE;
	s_conflictsDirty = TRUE;
	refresh();
}

void clearSelected()
{
	if( s_view == VIEW_COMMANDS && s_commandRow >= 0 )
	{
		clearCommand( s_rows[ s_commandRow ].meta );
		if( s_rows[ s_commandRow ].pair != GameMessage::MSG_INVALID )
		{
			clearCommand( s_rows[ s_commandRow ].pair );
		}
	}
	else if( s_view == VIEW_SIDE && powerRow( selectedObject(), s_slot ) >= 0 )
	{
		clearCommand( s_rows[ powerRow( selectedObject(), s_slot ) ].meta );
	}
	else if( s_view == VIEW_SIDE && selectedObject() && s_slot >= 0 && selectedObject()->slots[ s_slot ] )
	{
		s_buttons[ lowerName( selectedObject()->slots[ s_slot ] ) ].clear();
	}
	else
	{
		return;
	}
	s_dirty = TRUE;
	s_conflictsDirty = TRUE;
	refresh();
}

/// The selected object's whole card, or the selected command, back to the game's keys.
void resetSelected()
{
	if( s_view == VIEW_COMMANDS && s_commandRow >= 0 )
	{
		s_keys[ s_rows[ s_commandRow ].meta ] = defaultOf( s_rows[ s_commandRow ].meta );
		if( s_rows[ s_commandRow ].pair != GameMessage::MSG_INVALID )
		{
			s_keys[ s_rows[ s_commandRow ].pair ] = defaultOf( s_rows[ s_commandRow ].pair );
		}
	}
	else if( s_view == VIEW_SIDE && selectedObject() )
	{
		for( Int slot = 0; slot < CARD_SLOTS; ++slot )
		{
			const Int power = powerRow( selectedObject(), slot );
			if( power >= 0 )
			{
				s_keys[ s_rows[ power ].meta ] = defaultOf( s_rows[ power ].meta );
			}
			else if( selectedObject()->slots[ slot ] )
			{
				s_buttons.erase( lowerName( selectedObject()->slots[ slot ] ) );
			}
		}
	}
	else
	{
		return;
	}
	s_dirty = TRUE;
	s_conflictsDirty = TRUE;
	refresh();
}

void resetAll()
{
	for( std::map<Int, Binding>::iterator it = s_keys.begin(); it != s_keys.end(); ++it )
	{
		it->second = defaultOf( (GameMessage::Type)it->first );
	}
	s_buttons.clear();
	s_capture = CAPTURE_NONE;
	s_dirty = TRUE;
	s_conflictsDirty = TRUE;
	refresh();
}

Int modifiersOf( Int state )
{
	Int mods = NONE;
	if( state & KEY_STATE_CONTROL )
	{
		mods |= CTRL;
	}
	if( state & KEY_STATE_ALT )
	{
		mods |= ALT;
	}
	if( state & KEY_STATE_SHIFT )
	{
		mods |= SHIFT;
	}
	return mods;
}

Int modifierKey( UnsignedByte key )
{
	switch( key )
	{
		case KEY_LCTRL:
		case KEY_RCTRL:
			return CTRL;
		case KEY_LALT:
		case KEY_RALT:
			return ALT;
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			return SHIFT;
	}
	return NONE;
}

void captureKey( UnsignedByte key, Int state )
{
	const Bool down = BitIsSet( state, KEY_STATE_DOWN );
	if( key == KEY_ESC )
	{
		if( !down )
		{
			s_capture = CAPTURE_NONE;
			refresh();
		}
		return;
	}

	// a hold such as Force Attack may take Ctrl, Alt or Shift alone, let go with no key between
	const Int modifier = modifierKey( key );
	if( modifier != NONE )
	{
		const Bool hold = s_capture == CAPTURE_COMMAND && s_rows[ s_commandRow ].pair != GameMessage::MSG_INVALID;
		if( down )
		{
			s_holdMods |= modifier;
		}
		else
		{
			if( hold && !s_holdBroken && s_holdMods != NONE )
			{
				applyKey( MK_NONE, s_holdMods, AsciiString::TheEmptyString );
			}
			s_holdMods = NONE;
			s_holdBroken = FALSE;
		}
		return;
	}
	if( !down )
	{
		return;
	}
	s_holdBroken = TRUE;

	const char *name = lookupName( KeyNames, key );
	if( name == nullptr || key == KEY_NONE )
	{
		return;
	}
	const Int mods = modifiersOf( state );
	s_boundKey = key;
	if( s_capture == CAPTURE_COMMAND )
	{
		applyKey( (MappableKeyType)key, mods, AsciiString::TheEmptyString );
		return;
	}

	// a button takes one bare letter, as digits are control groups; Backspace takes it off
	if( key == KEY_BACKSPACE || key == KEY_DEL )
	{
		applyKey( MK_NONE, NONE, AsciiString::TheEmptyString );
		return;
	}
	const char *letter = name + 4;
	const Bool mouse = HotKeyManager::mouseKeyName( key ) != nullptr;
	if( ( mods & ( CTRL | ALT ) ) == 0 && ( mouse || ( strlen( letter ) == 1 && isalpha( (unsigned char)letter[0] ) ) ) )
	{
		applyKey( MK_NONE, NONE, AsciiString( letter ) );
	}
}

/// A wheel notch or middle click while a key is awaited.
void captureMouse( MappableKeyType key )
{
	const Int mods = modifiersOf( TheKeyboard->getModifierFlags() );
	if( s_capture == CAPTURE_COMMAND )
	{
		// a click or notch has no release to end a hold on
		if( s_rows[ s_commandRow ].pair == GameMessage::MSG_INVALID )
		{
			applyKey( key, mods, AsciiString::TheEmptyString );
		}
	}
	else if( ( mods & ( CTRL | ALT ) ) == 0 )
	{
		AsciiString name = HotKeyManager::mouseKeyName( key );
		name.toUpper();
		applyKey( MK_NONE, NONE, name );
	}
}

//-------------------------------------------------------------------------------------------------
void appendUsableIn( AsciiString& text, CommandUsableInType usableIn )
{
	AsciiString names;
	for( Int bit = 0; TheCommandUsableInNames[bit]; ++bit )
	{
		if( usableIn & ( 1 << bit ) )
		{
			if( names.isNotEmpty() )
			{
				names.concat( ' ' );
			}
			names.concat( TheCommandUsableInNames[bit] );
		}
	}
	text.concat( names.isEmpty() ? "NONE" : names.str() );
}

/// Hotkeys.ini for the page's keys: only what differs from the game's own.
AsciiString formatFile()
{
	AsciiString text;
	text.concat( "; Hotkeys written by Options > Hotkeys. Only keys that differ from the game's are here.\r\n" );
	text.concat( "; The game reads this at start and on Save; it is not part of the multiplayer checksum.\r\n\r\n" );
	for( std::map<Int, Binding>::const_iterator it = s_keys.begin(); it != s_keys.end(); ++it )
	{
		const GameMessage::Type meta = (GameMessage::Type)it->first;
		const Binding& binding = it->second;
		const char *name = MetaMap::findMetaTypeName( meta );
		if( name == nullptr || sameBinding( binding, defaultOf( meta ) ) )
		{
			continue;
		}
		const char *key = lookupName( KeyNames, binding.key );
		const char *transition = lookupName( TransitionNames, binding.transition );
		const char *mods = lookupName( ModifierNames, binding.modState );
		AsciiString block;
		block.format( "CommandMap %s\r\n  Key = %s\r\n  Transition = %s\r\n  Modifiers = %s\r\n  UseableIn = ",
			name, key ? key : "KEY_NONE", transition ? transition : "DOWN", mods ? mods : "NONE" );
		appendUsableIn( block, binding.usableIn );
		block.concat( "\r\nEnd\r\n\r\n" );
		text.concat( block );
	}
	for( HotKeyManager::ButtonKeyMap::const_iterator it = s_buttons.begin(); it != s_buttons.end(); ++it )
	{
		std::map<AsciiString, AsciiString>::const_iterator spelled = s_buttonNames.find( it->first );
		AsciiString block;
		block.format( "CommandButtonHotKey %s\r\n  Key = %s\r\nEnd\r\n\r\n",
			spelled != s_buttonNames.end() ? spelled->second.str() : it->first.str(),
			it->second.isEmpty() ? "NONE" : it->second.str() );
		text.concat( block );
	}
	return text;
}

void save()
{
	const AsciiString path = HotKeyManager::getPlayerKeysPath();
	const AsciiString text = formatFile();
	// through TheFileSystem, whose existence cache would otherwise keep a file made here unread
	File *file = TheFileSystem->openFile( path.str(), File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY );
	if( file == nullptr )
	{
		DEBUG_LOG(( "HOTKEYS: %s could not be written", path.str() ));
		return;
	}
	file->write( text.str(), text.getLength() );
	file->close();
	DEBUG_LOG(( "HOTKEYS: wrote %s", path.str() ));
	HotKeyManager::loadPlayerKeys();
	s_dirty = FALSE;
}

const char *const ROOT_NAME = "HotkeysMenu:Root";

/// Whether the root still exists; a GUI reset can destroy it without close()
Bool screenOpen()
{
	if( s_parent && TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( ROOT_NAME ) ) != s_parent )
	{
		s_parent = nullptr;
	}
	return s_parent != nullptr;
}

void close()
{
	if( !screenOpen() )
	{
		return;
	}
	TheWindowManager->winUnsetModal( s_parent );
	TheWindowManager->winDestroy( s_parent );
	s_parent = nullptr;
	s_capture = CAPTURE_NONE;

	// the options menu below takes the keyboard back
	GameWindow *options = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:OptionsMenuParent" ) );
	if( options )
	{
		TheWindowManager->winSetFocus( options );
	}
}

Int indexOf( GameWindow *control, GameWindow *const *windows, Int count )
{
	for( Int i = 0; i < count; ++i )
	{
		if( windows[i] == control )
		{
			return i;
		}
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
// Built in code with the options menu's look, so any mod's skin fits and no files are needed
//-------------------------------------------------------------------------------------------------

/// Options menu windows every layout has, whose images, colours and fonts the screen copies
struct Look
{
	GameWindow *button;
	GameWindow *label;
	GameWindow *combo;
	GameWindow *entry;
};

Look s_look;

GameWindow *optionsWindow( const char *name )
{
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( name ) );
}

Int toX( Int x )
{
	return REAL_TO_INT( x * TheDisplay->getWidth() / 800.0f );
}

Int toY( Int y )
{
	return REAL_TO_INT( y * TheDisplay->getHeight() / 600.0f );
}

GameFont *arial( Int size, Bool bold )
{
	if( TheGlobalLanguageData )
	{
		size = TheGlobalLanguageData->adjustFontSize( size );
	}
	return TheFontLibrary->getFont( AsciiString( "Arial" ), size, bold );
}

UnsignedInt statusOf( GameWindow *from )
{
	return ( from->winGetStatus() & ( WIN_STATUS_IMAGE | WIN_STATUS_BORDER ) ) | WIN_STATUS_ENABLED;
}

void copyDrawData( WinInstanceData *to, const WinInstanceData *from )
{
	for( Int i = 0; i < MAX_DRAW_DATA; ++i )
	{
		to->m_enabledDrawData[i] = from->m_enabledDrawData[i];
		to->m_disabledDrawData[i] = from->m_disabledDrawData[i];
		to->m_hiliteDrawData[i] = from->m_hiliteDrawData[i];
	}
}

/// Instance data for a new window that looks like the template.
void copyLook( WinInstanceData *to, GameWindow *from )
{
	const WinInstanceData *data = from->winGetInstanceData();
	copyDrawData( to, data );
	to->m_enabledText = data->m_enabledText;
	to->m_disabledText = data->m_disabledText;
	to->m_hiliteText = data->m_hiliteText;
	to->m_imeCompositeText = data->m_imeCompositeText;
	to->m_imageOffset = data->m_imageOffset;
	to->m_style = data->m_style;
	to->m_font = data->m_font;
	to->m_owner = s_parent;
}

/// A gadget's own parts (scroll buttons, drop down) take the template's parts' pictures.
void copyPart( GameWindow *to, GameWindow *from )
{
	if( to && from )
	{
		copyDrawData( to->winGetInstanceData(), from->winGetInstanceData() );
	}
}

void copyListParts( GameWindow *to, GameWindow *from )
{
	copyPart( GadgetListBoxGetUpButton( to ), GadgetListBoxGetUpButton( from ) );
	copyPart( GadgetListBoxGetDownButton( to ), GadgetListBoxGetDownButton( from ) );
	GameWindow *toSlider = GadgetListBoxGetSlider( to );
	GameWindow *fromSlider = GadgetListBoxGetSlider( from );
	copyPart( toSlider, fromSlider );
	if( toSlider && fromSlider )
	{
		copyPart( toSlider->winGetChild(), fromSlider->winGetChild() );
	}
}

GameWindow *makeFrame( Int left, Int top, Int right, Int bottom, Color fill, Color border )
{
	GameWindow *frame = TheWindowManager->winCreate( s_parent, WIN_STATUS_ENABLED, toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), nullptr );
	WinInstanceData data;
	data.m_style = GWS_USER_WINDOW;
	data.m_enabledDrawData[0].color = fill;
	data.m_enabledDrawData[0].borderColor = border;
	frame->winSetInstanceData( &data );
	return frame;
}

GameWindow *makeLabel( Int left, Int top, Int right, Int bottom, const char *label, GameFont *font, Bool centered, Bool fromTop )
{
	WinInstanceData data;
	copyLook( &data, s_look.label );
	if( font )
	{
		data.m_font = font;
	}
	TextData text;
	memset( &text, 0, sizeof( text ) );
	text.centered = centered;
	text.centeredVertically = !fromTop;
	text.leftMargin = 7;
	text.topMargin = 7;
	GameWindow *window = TheWindowManager->gogoGadgetStaticText( s_parent, WIN_STATUS_ENABLED, toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, &text, data.m_font, FALSE );
	if( label )
	{
		GadgetStaticTextSetText( window, HotkeysMenuText( label ) );
	}
	return window;
}

GameWindow *makeButton( Int left, Int top, Int right, Int bottom, const char *label, const char *tip )
{
	WinInstanceData data;
	copyLook( &data, s_look.button );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( s_parent, statusOf( s_look.button ), toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, data.m_font, FALSE );
	GadgetButtonSetText( button, HotkeysMenuText( label ) );
	if( tip )
	{
		button->winSetTooltip( HotkeysMenuText( tip ) );
	}
	return button;
}

/// A flat text button; fillCommandTabs gives it its colours.
GameWindow *makeTab( Int left, Int top, Int right, Int bottom, const char *label )
{
	WinInstanceData data;
	data.m_style = GWS_PUSH_BUTTON | GWS_MOUSE_TRACK;
	data.m_owner = s_parent;
	data.m_font = arial( 10, FALSE );
	data.m_enabledText.color = TEXT_COLOR;
	data.m_enabledText.borderColor = BLACK_COLOR;
	data.m_hiliteText = data.m_enabledText;
	data.m_disabledText = data.m_enabledText;
	GameWindow *tab = TheWindowManager->gogoGadgetPushButton( s_parent, WIN_STATUS_ENABLED, toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, data.m_font, FALSE );
	GadgetButtonSetText( tab, HotkeysMenuText( label ) );
	return tab;
}

/// A cameo button whose picture the screen sets; its font draws the corner letter.
GameWindow *makeIcon( Int left, Int top, Int right, Int bottom, Bool hidden )
{
	WinInstanceData data;
	data.m_style = GWS_PUSH_BUTTON | GWS_MOUSE_TRACK;
	data.m_owner = s_parent;
	data.m_font = arial( 10, TRUE );
	data.m_enabledText.color = TEXT_COLOR;
	data.m_enabledText.borderColor = BLACK_COLOR;
	data.m_hiliteText = data.m_enabledText;
	data.m_disabledText = data.m_enabledText;
	const UnsignedInt status = WIN_STATUS_ENABLED | WIN_STATUS_IMAGE | ( hidden ? WIN_STATUS_HIDDEN : 0 );
	return TheWindowManager->gogoGadgetPushButton( s_parent, status, toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, data.m_font, FALSE );
}

GameWindow *makeList( Int left, Int top, Int right, Int bottom, Int columns, const Int *widths )
{
	// the options menu has no list of its own, so the resolution combo's drop down lends its look
	GameWindow *from = GadgetComboBoxGetListBox( s_look.combo );
	WinInstanceData data;
	copyLook( &data, from );
	data.m_style = GWS_SCROLL_LISTBOX | GWS_MOUSE_TRACK;
	ListboxData list;
	memset( &list, 0, sizeof( list ) );
	list.listLength = 256;
	list.columns = columns;
	list.scrollBar = TRUE;
	list.audioFeedback = TRUE;
	if( columns > 1 )
	{
		// the list owns and frees this
		list.columnWidthPercentage = NEW Int[ columns ];
		for( Int i = 0; i < columns; ++i )
		{
			list.columnWidthPercentage[i] = widths[i];
		}
	}
	GameWindow *window = TheWindowManager->gogoGadgetListBox( s_parent, WIN_STATUS_ENABLED, toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, &list, data.m_font, FALSE );
	copyListParts( window, from );
	return window;
}

GameWindow *makeCombo( Int left, Int top, Int right, Int bottom, Int maxDisplay )
{
	WinInstanceData data;
	copyLook( &data, s_look.combo );
	ComboBoxData combo;
	memset( &combo, 0, sizeof( combo ) );
	combo.maxChars = 64;
	combo.maxDisplay = maxDisplay;
	// the combo owns and frees these
	combo.entryData = NEW EntryData;
	memset( combo.entryData, 0, sizeof( EntryData ) );
	combo.entryData->maxTextLen = combo.maxChars;
	combo.listboxData = NEW ListboxData;
	memset( combo.listboxData, 0, sizeof( ListboxData ) );
	combo.listboxData->listLength = 32;
	combo.listboxData->scrollBar = TRUE;
	combo.listboxData->forceSelect = TRUE;
	combo.listboxData->columns = 1;
	GameWindow *window = TheWindowManager->gogoGadgetComboBox( s_parent, statusOf( s_look.combo ), toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, &combo, data.m_font, FALSE );
	copyPart( GadgetComboBoxGetDropDownButton( window ), GadgetComboBoxGetDropDownButton( s_look.combo ) );
	copyPart( GadgetComboBoxGetEditBox( window ), GadgetComboBoxGetEditBox( s_look.combo ) );
	copyPart( GadgetComboBoxGetListBox( window ), GadgetComboBoxGetListBox( s_look.combo ) );
	copyListParts( GadgetComboBoxGetListBox( window ), GadgetComboBoxGetListBox( s_look.combo ) );
	return window;
}

GameWindow *makeEntry( Int left, Int top, Int right, Int bottom )
{
	WinInstanceData data;
	copyLook( &data, s_look.entry );
	EntryData entry;
	memset( &entry, 0, sizeof( entry ) );
	entry.maxTextLen = 64;
	entry.aSCIIOnly = TRUE;
	return TheWindowManager->gogoGadgetTextEntry( s_parent, statusOf( s_look.entry ), toX( left ), toY( top ),
		toX( right ) - toX( left ), toY( bottom ) - toY( top ), &data, &entry, data.m_font, FALSE );
}

/// The command bar numbers its slots down each column of two rows.
void cardRect( Int slot, Int *left, Int *top )
{
	*left = CARD_LEFT + ( slot / 2 ) * CARD_STEP_X;
	*top = CARD_TOP + ( slot % 2 ) * CARD_STEP_Y;
}

Bool buildScreen()
{
	s_look.button = optionsWindow( "OptionsMenu.wnd:ButtonAccept" );
	s_look.label = optionsWindow( "OptionsMenu.wnd:StaticTextHTTPProxy" );
	s_look.combo = optionsWindow( "OptionsMenu.wnd:ComboBoxResolution" );
	s_look.entry = optionsWindow( "OptionsMenu.wnd:TextEntryHTTPProxy" );
	if( !s_look.button || !s_look.label || !s_look.combo || !s_look.entry || !GadgetComboBoxGetListBox( s_look.combo ) )
	{
		DEBUG_LOG(( "HOTKEYS: the options menu lacks a window the Hotkeys screen copies its look from" ));
		return FALSE;
	}

	// a see-through root over the whole screen holds the callbacks and keeps the clicks
	s_parent = TheWindowManager->winCreate( nullptr, WIN_STATUS_ENABLED, 0, 0, TheDisplay->getWidth(), TheDisplay->getHeight(), HotkeysMenuSystem );
	WinInstanceData root;
	root.m_style = GWS_USER_WINDOW;
	s_parent->winSetInstanceData( &root );
	s_parent->winSetWindowId( TheNameKeyGenerator->nameToKey( ROOT_NAME ) );
	s_parent->winSetInputFunc( HotkeysMenuInput );

	// each window is created above the ones before it
	makeFrame( 20, 10, 780, 590, GameMakeColor( 2, 2, 2, 240 ), GOLD_COLOR );
	makeLabel( 30, 16, 770, 40, "GUI:Hotkeys", arial( 16, TRUE ), TRUE, FALSE );

	static const char *const SIDE_LABELS[ SIDE_COUNT ] = { "GUI:HotkeysSideUSA", "GUI:HotkeysSideChina", "GUI:HotkeysSideGLA" };
	for( Int i = 0; i < SIDE_COUNT; ++i )
	{
		s_sideButtons[i] = makeButton( 36, 56 + i * 34, 176, 84 + i * 34, SIDE_LABELS[i], nullptr );
	}
	s_commandsButton = makeButton( 36, 170, 176, 198, "GUI:HotkeysGeneral", "TOOLTIP:HotkeysGeneral" );
	s_conflictsButton = makeButton( 36, 204, 176, 232, "GUI:HotkeysConflicts", "TOOLTIP:HotkeysConflicts" );

	s_gridBox = makeFrame( 186, 56, 478, 548, GameMakeColor( 0, 0, 0, 150 ), GOLD_COLOR );
	s_cardBox = makeFrame( 486, 88, 770, 548, GameMakeColor( 0, 0, 0, 150 ), GOLD_COLOR );
	Int left = 0;
	Int top = 0;
	for( Int slot = 0; slot < CARD_SLOTS; ++slot )
	{
		cardRect( slot, &left, &top );
		s_cardCells[ slot ] = makeFrame( left, top, left + CARD_CELL_WIDTH, top + CARD_CELL_HEIGHT, GameMakeColor( 0, 0, 0, 220 ), GameMakeColor( 60, 54, 3, 255 ) );
	}

	static const char *const SECTION_LABELS[ SECTION_COUNT ] = { "GUI:HotkeysPowers", "GUI:HotkeysHeroes", "GUI:HotkeysBuildings", "GUI:HotkeysUnits" };
	for( Int s = 0; s < SECTION_COUNT; ++s )
	{
		s_headings[s] = makeLabel( GRID_LEFT, GRID_TOP, GRID_LEFT + GRID_WIDTH, GRID_TOP + 18, SECTION_LABELS[s], arial( 12, TRUE ), FALSE, FALSE );
	}
	for( Int i = 0; i < ICON_POOL; ++i )
	{
		s_icons[i] = makeIcon( GRID_LEFT, GRID_TOP, GRID_LEFT + 47, GRID_TOP + 37, TRUE );
	}

	s_portrait = makeIcon( 494, 96, 550, 140, FALSE );
	s_objectName = makeLabel( 558, 100, 764, 136, nullptr, arial( 14, TRUE ), FALSE, FALSE );
	for( Int slot = 0; slot < CARD_SLOTS; ++slot )
	{
		cardRect( slot, &left, &top );
		s_card[ slot ] = makeIcon( left, top, left + CARD_CELL_WIDTH, top + CARD_CELL_HEIGHT, FALSE );
	}
	// the selection rings, drawn above the icons they surround
	Ring *const rings[] = { &s_objectRing, &s_slotRing };
	for( Int r = 0; r < 2; ++r )
	{
		for( Int i = 0; i < 4; ++i )
		{
			rings[r]->bars[i] = makeFrame( 0, 0, 1, 1, SELECTED_COLOR, SELECTED_COLOR );
			rings[r]->bars[i]->winSetStatus( WIN_STATUS_NO_INPUT );
			rings[r]->bars[i]->winHide( TRUE );
		}
	}
	s_info = makeLabel( 494, 222, 764, 504, nullptr, nullptr, FALSE, TRUE );

	static const Int COMMAND_COLUMNS[] = { 74, 26 };
	s_commandList = makeList( 186, 116, 770, 504, 2, COMMAND_COLUMNS );
	s_conflictList = makeList( 186, 90, 770, 504, 1, nullptr );
	const Int tabGap = 3;
	const Int tabSpan = 770 - 186 + tabGap;
	for( Int i = 0; i < COMMAND_TAB_COUNT; ++i )
	{
		AsciiString label = "GUI:HotkeysTabALL";
		if( i > 0 )
		{
			label.format( "GUI:HotkeysTab%s", lookupName( CategoryListName, CATEGORY_ORDER[ i - 1 ] ) );
		}
		s_commandTabs[i] = makeTab( 186 + tabSpan * i / COMMAND_TAB_COUNT, 90, 186 + tabSpan * ( i + 1 ) / COMMAND_TAB_COUNT - tabGap, 112, label.str() );
	}
	s_note = makeLabel( 186, 508, 486, 546, nullptr, nullptr, FALSE, FALSE );
	s_change = makeButton( 494, 512, 626, 538, "GUI:HotkeysChange", "TOOLTIP:HotkeyChange" );
	s_clear = makeButton( 634, 512, 762, 538, "GUI:HotkeysClear", "TOOLTIP:HotkeyClear" );

	s_back = makeButton( 40, 556, 200, 582, "GUI:Back", nullptr );
	s_save = makeButton( 210, 556, 370, 582, "GUI:HotkeysSave", "TOOLTIP:HotkeysSave" );
	s_resetDefault = makeButton( 380, 556, 580, 582, "GUI:HotkeysResetDefault", "TOOLTIP:HotkeyDefault" );
	s_resetAll = makeButton( 590, 556, 760, 582, "GUI:HotkeysResetAll", "TOOLTIP:HotkeyResetAll" );

	makeLabel( 486, 58, 578, 82, "GUI:HotkeysFilter", nullptr, FALSE, FALSE );
	s_filter = makeEntry( 580, 58, 770, 82 );
	// the combo last: it draws over its siblings and takes the clicks its open list covers
	s_generalCombo = makeCombo( 194, 62, 470, 86, 8 );

	s_catcher = TheWindowManager->winCreate( s_parent, WIN_STATUS_ENABLED, 0, 0, TheDisplay->getWidth(), TheDisplay->getHeight(), nullptr );
	WinInstanceData catcher;
	catcher.m_style = GWS_USER_WINDOW;
	s_catcher->winSetInstanceData( &catcher );
	s_catcher->winSetInputFunc( HotkeysMenuInput );
	s_catcher->winHide( TRUE );
	return TRUE;
}

}	// namespace

//-------------------------------------------------------------------------------------------------
UnicodeString HotkeysMenuText( const char *label )
{
	Bool exists = FALSE;
	const UnicodeString text = TheGameText->fetch( label, &exists );
	if( exists )
	{
		return text;
	}
	for( Int i = 0; i < ARRAY_SIZE( BUILT_IN_TEXT ); ++i )
	{
		if( stricmp( BUILT_IN_TEXT[i].label, label ) == 0 )
		{
			return UnicodeString( BUILT_IN_TEXT[i].text );
		}
	}
	return text;
}

//-------------------------------------------------------------------------------------------------
GameWindow *HotkeysMenuOptionsButton()
{
	GameWindow *button = optionsWindow( "OptionsMenu.wnd:ButtonHotkeys" );
	if( button )
	{
		return button;
	}
	// a layout without its own Hotkeys button gets one right of Shaders, in Shaders' look
	GameWindow *shaders = optionsWindow( "OptionsMenu.wnd:ButtonShaders" );
	if( shaders == nullptr )
	{
		return nullptr;
	}
	Int x = 0;
	Int y = 0;
	Int width = 0;
	Int height = 0;
	shaders->winGetPosition( &x, &y );
	shaders->winGetSize( &width, &height );
	WinInstanceData data;
	copyLook( &data, shaders );
	data.m_owner = shaders->winGetParent();
	button = TheWindowManager->gogoGadgetPushButton( shaders->winGetParent(), statusOf( shaders ) | WIN_STATUS_HIDDEN,
		x + width + toX( 5 ), y, width, height, &data, data.m_font, FALSE );
	button->winSetWindowId( TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonHotkeys" ) );
	return button;
}

//-------------------------------------------------------------------------------------------------
void HotkeysMenuOpen()
{
	if( screenOpen() )
	{
		return;
	}
	if( !buildScreen() )
	{
		return;
	}

	s_filling = TRUE;
	GadgetTextEntrySetText( s_filter, UnicodeString::TheEmptyString );
	s_filling = FALSE;

	s_buttons = HotKeyManager::getPlayerButtonKeys();
	s_commandRow = -1;
	s_commandTab = 0;
	s_capture = CAPTURE_NONE;
	s_dirty = FALSE;
	s_conflictsDirty = TRUE;
	buildRows();
	findBars();
	showSide( 0 );
	refresh();

	TheWindowManager->winSetModal( s_parent );
	TheWindowManager->winSetFocus( s_parent );
}

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType HotkeysMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( s_capture != CAPTURE_NONE && msg != GWM_CHAR )
	{
		if( msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN )
		{
			captureMouse( msg == GWM_WHEEL_UP ? MK_WHEELUP : MK_WHEELDOWN );
		}
		else if( msg == GWM_MIDDLE_UP )
		{
			captureMouse( MK_MOUSE3 );
		}
		else if( msg == GWM_LEFT_UP || msg == GWM_RIGHT_UP )
		{
			// a click anywhere lets go of the wait
			s_capture = CAPTURE_NONE;
			refresh();
		}
		return MSG_HANDLED;
	}
	if( msg != GWM_CHAR )
	{
		return MSG_IGNORED;
	}
	const UnsignedByte key = (UnsignedByte)mData1;
	const Int state = (Int)mData2;
	if( s_capture != CAPTURE_NONE )
	{
		captureKey( key, state );
	}
	else if( BitIsSet( state, KEY_STATE_UP ) && key == s_boundKey )
	{
		s_boundKey = KEY_NONE;
	}
	else if( BitIsSet( state, KEY_STATE_UP ) )
	{
		if( key == KEY_ESC )
		{
			close();
		}
		else if( key == KEY_ENTER || key == KEY_KPENTER )
		{
			startCapture();
		}
	}
	// no key reaches the game behind the screen
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType HotkeysMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_INPUT_FOCUS:
		{
			if( mData1 == TRUE )
			{
				*(Bool *)mData2 = TRUE;
			}
			return MSG_HANDLED;
		}

		case GCM_SELECTED:
		{
			if( s_filling || s_parent == nullptr )
			{
				return MSG_HANDLED;
			}
			s_capture = CAPTURE_NONE;
			pickObject( 0 );
			refresh();
			return MSG_HANDLED;
		}

		case GEM_UPDATE_TEXT:
		{
			if( s_filling || s_parent == nullptr )
			{
				return MSG_HANDLED;
			}
			s_capture = CAPTURE_NONE;
			// typing looks through every group
			if( s_view == VIEW_COMMANDS && !GadgetTextEntryGetText( s_filter ).isEmpty() )
			{
				showCommandTab( 0 );
			}
			refresh();
			return MSG_HANDLED;
		}

		case GLM_SELECTED:
		case GLM_DOUBLE_CLICKED:
		{
			if( s_filling || s_parent == nullptr )
			{
				return MSG_HANDLED;
			}
			GameWindow *control = (GameWindow *)mData1;
			const Int row = (Int)mData2;
			s_capture = CAPTURE_NONE;
			if( row >= 0 && control == s_commandList )
			{
				const Int index = (Int)(intptr_t)GadgetListBoxGetItemData( s_commandList, row, 0 ) - 1;
				if( index >= 0 )
				{
					s_commandRow = index;
					if( msg == GLM_DOUBLE_CLICKED )
					{
						startCapture();
						return MSG_HANDLED;
					}
				}
			}
			else if( row >= 0 && row < (Int)s_conflicts.size() && control == s_conflictList && msg == GLM_DOUBLE_CLICKED )
			{
				// a conflict leads to the card or the command that has it
				const ConflictRef ref = s_conflicts[ row ];
				if( ref.side < 0 )
				{
					s_commandRow = ref.general;
					showCommandTab( commandTabOf( s_rows[ s_commandRow ] ) );
				}
				else
				{
					showSide( ref.side );
					s_filling = TRUE;
					GadgetComboBoxSetSelectedPos( s_generalCombo, ref.general );
					s_filling = FALSE;
					pickObject( ref.object );
				}
			}
			refresh();
			return MSG_HANDLED;
		}

		case GBM_SELECTED:
		{
			if( s_parent == nullptr )
			{
				return MSG_HANDLED;
			}
			GameWindow *control = (GameWindow *)mData1;
			const Bool wasWaiting = s_capture != CAPTURE_NONE;
			s_capture = CAPTURE_NONE;

			const Int side = indexOf( control, s_sideButtons, SIDE_COUNT );
			const Int icon = indexOf( control, s_icons, ICON_POOL );
			const Int slot = indexOf( control, s_card, CARD_SLOTS );
			const Int tab = indexOf( control, s_commandTabs, COMMAND_TAB_COUNT );
			if( side >= 0 )
			{
				showSide( side );
			}
			else if( icon >= 0 && icon < (Int)s_iconObjects.size() )
			{
				pickObject( s_iconObjects[ icon ] );
			}
			else if( slot >= 0 )
			{
				// a press on the card waits for the button's new letter
				s_slot = slot;
				if( !wasWaiting )
				{
					startCapture();
					return MSG_HANDLED;
				}
			}
			else if( tab >= 0 )
			{
				showCommandTab( tab );
			}
			else if( control == s_commandsButton )
			{
				s_view = VIEW_COMMANDS;
			}
			else if( control == s_conflictsButton )
			{
				s_view = VIEW_CONFLICTS;
			}
			else if( control == s_change )
			{
				if( !wasWaiting )
				{
					startCapture();
					return MSG_HANDLED;
				}
			}
			else if( control == s_clear )
			{
				clearSelected();
				return MSG_HANDLED;
			}
			else if( control == s_resetDefault )
			{
				resetSelected();
				return MSG_HANDLED;
			}
			else if( control == s_resetAll )
			{
				resetAll();
				return MSG_HANDLED;
			}
			else if( control == s_save )
			{
				save();
			}
			else if( control == s_back )
			{
				close();
				return MSG_HANDLED;
			}
			refresh();
			return MSG_HANDLED;
		}
	}
	return MSG_IGNORED;
}
