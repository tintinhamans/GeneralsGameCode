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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: HotKey.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	HotKey.cpp
//
//	author:		Chris Huybregts
//
//	purpose:
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameText.h"
#include "GameClient/ControlBar.h"
#include "Common/AudioEventRTS.h"
#include "Common/FileSystem.h"
#include "Common/INI.h"
// TheSuperHackers @feature for hold to aim quick cast
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
GameMessageDisposition HotKeyTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	GameMessage::Type t = msg->getType();

	// TheSuperHackers @feature In QuickCastWithIndicator mode a hotkey arms on key down so the
	// player can see the targeting decal and still adjust aim, then fires on key up. Every other
	// mode keeps the original behaviour of acting only on key up.
	const Bool holdToAim = TheGlobalData &&
		TheGlobalData->m_castMode == CastMode_QuickCastWithIndicator;

	if ( t == GameMessage::MSG_RAW_KEY_UP || (holdToAim && t == GameMessage::MSG_RAW_KEY_DOWN) )
	{

		//char key = msg->getArgument(0)->integer;
		Int keyState = msg->getArgument(1)->integer;

		// for our purposes here, we don't care to distinguish between right and left keys,
		// so just fudge a little to simplify things.
		Int newModState = 0;

		if( keyState & KEY_STATE_CONTROL )
		{
			newModState |= CTRL;
		}

		if( keyState & KEY_STATE_SHIFT )
		{
			newModState |= SHIFT;
		}

		if( keyState & KEY_STATE_ALT )
		{
			newModState |= ALT;
		}

		// TheSuperHackers @feature Let Shift through so shift+hotkey batches production the same
		// way shift+clicking the cameo does. Ctrl and Alt still bail, since those carry their own
		// bindings (control groups and so on) that must not be shadowed by command hotkeys.
		if( (newModState & ~SHIFT) != 0 )
			return disp;
		WideChar key = TheKeyboard->getPrintableKey((KeyDefType)msg->getArgument(0)->integer, 0);
		UnicodeString uKey;
		uKey.concat(key);
		AsciiString aKey;
		aKey.translate(uKey);

		// a side button arrives as a key press of a mouse key code, which has no printable letter
		const char *mouseKey = HotKeyManager::mouseKeyName( msg->getArgument(0)->integer );
		if( mouseKey )
		{
			aKey = mouseKey;
		}
		if( TheHotKeyManager )
		{
			// key down arms and previews, key up commits at wherever the cursor ended up
			HotKeyManager::setQuickCastAiming( holdToAim && t == GameMessage::MSG_RAW_KEY_DOWN );

			if( TheHotKeyManager->executeHotKey(aKey) )
				disp = DESTROY_MESSAGE;

			HotKeyManager::setQuickCastAiming( FALSE );
		}
	}
	return disp;
}

//-----------------------------------------------------------------------------
HotKey::HotKey()
{
	m_win = nullptr;
	m_key.clear();
	m_priority = FALSE;
}

//-----------------------------------------------------------------------------
HotKeyManager::HotKeyManager()
{

}

//-----------------------------------------------------------------------------
HotKeyManager::~HotKeyManager()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::init()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::reset()
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::addHotKey( GameWindow *win, const AsciiString& keyIn, Bool priority )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it != m_hotKeyMap.end() )
	{
		// TheSuperHackers @feature A priority key outranks a normal one whichever registers first.
		if( it->second.m_priority && !priority )
		{
			return;
		}
		if( it->second.m_priority || !priority )
		{
			DEBUG_CRASH(("Hotkey %s is already mapped to window %s, current window is %s", key.str(), it->second.m_win->winGetInstanceData()->m_decoratedNameString.str(), win->winGetInstanceData()->m_decoratedNameString.str()));
			return;
		}
	}
	HotKey newHK;
	newHK.m_key.set(key);
	newHK.m_win = win;
	newHK.m_priority = priority;
	m_hotKeyMap[key] = newHK;
}

//-----------------------------------------------------------------------------
// TheSuperHackers @feature See HotKey.h -- set only while synthesizing a button press.
Bool HotKeyManager::s_executingHotKey = FALSE;
Bool HotKeyManager::s_quickCastAiming = FALSE;

// TheSuperHackers @feature See HotKey.h.
//-----------------------------------------------------------------------------
Bool HotKeyManager::isHotKeyClaimed( const AsciiString& keyIn ) const
{
	AsciiString key = keyIn;
	key.toLower();

	HotKeyMap::const_iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;

	GameWindow *win = it->second.m_win;
	if( win == nullptr )
		return FALSE;

	// Only a button the player can actually press counts as claiming the key. This has to agree
	// with executeHotKey below: yielding the meta event to a button that then refuses to run it
	// loses the keypress entirely, doing neither thing.
	if( BitIsSet( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
		return FALSE;

	// A disabled button normally cannot be pressed, so it does not claim the key. The one
	// exception mirrors executeHotKey: in quick cast a button held disabled only by NOT_READY --
	// a recharging ability, or a weapon still working through its burst -- is still pressable, so
	// it does claim it.
	if( !BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED ) )
	{
		const Bool pressableWhileNotReady =
				BitIsSet( win->winGetStatus(), WIN_STATUS_NOT_READY ) &&
				TheGlobalData &&
				TheGlobalData->m_castMode != CastMode_Normal;

		if( !pressableWhileNotReady )
			return FALSE;
	}

	return TRUE;
}

Bool HotKeyManager::executeHotKey( const AsciiString& keyIn )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;
	GameWindow *win = it->second.m_win;
	if( !win )
		return FALSE;
	if( !BitIsSet( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
	{
		// TheSuperHackers @feature A button that is merely not ready yet -- a recharging ability,
		// or a weapon still working through its burst -- is disabled, which normally swallows the
		// hotkey outright. In quick cast let it through anyway.
		//
		// Without this a repeat press is dropped here, before quick cast ever runs, and the only
		// way to retarget is to Stop first. A FIRE_WEAPON cameo reports COMMAND_NOT_READY for as
		// long as its weapon is not READY_TO_FIRE, so any unit still shooting has its own button
		// disabled underneath the player. Retargeting mid burst is expected behaviour -- see the
		// DragonTank firewall note in ControlBarCommand.cpp, which describes the same case.
		//
		// Deliberately narrow: this only opens up buttons disabled by WIN_STATUS_NOT_READY. Ones
		// that are restricted or unaffordable stay rejected, and the order still only does
		// anything if the logic side accepts it, so this cannot fire something that is genuinely
		// unavailable -- it just stops the keypress being thrown away before it is even looked at.
		Bool allowWhileNotReady = FALSE;
		if( !BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED ) &&
				BitIsSet( win->winGetStatus(), WIN_STATUS_NOT_READY ) &&
				TheGlobalData &&
				TheGlobalData->m_castMode != CastMode_Normal )
		{
			allowWhileNotReady = TRUE;
		}

		if( BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED ) || allowWhileNotReady )
 		{
			// TheSuperHackers @feature Tell the command bar this press came from the keyboard, so
			// quick cast can fire at the cursor. A mouse click on the cameo leaves the cursor over
			// the control bar, where there is nothing sensible to target.
			HotKeyManager::setExecutingHotKey( TRUE );
 			TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );
			HotKeyManager::setExecutingHotKey( FALSE );

 			// here we make the same click sound that the GUI uses when you click a button
 			AudioEventRTS buttonClick("GUIClick");

 			if( TheAudio )
 			{
 				TheAudio->addAudioEvent( &buttonClick );
 			}
			return TRUE;
 		}

		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
		{
			TheAudio->addAudioEvent( &disabledClick );
		}
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
// TheSuperHackers @feature Reverse lookup for the hotkey overlay on command bar cameos.
//-----------------------------------------------------------------------------
AsciiString HotKeyManager::getHotKeyForWindow( const GameWindow *win ) const
{
	if( win == nullptr )
		return AsciiString::TheEmptyString;

	// the map only ever holds the currently displayed commands, so this stays tiny
	for( HotKeyMap::const_iterator it = m_hotKeyMap.begin(); it != m_hotKeyMap.end(); ++it )
	{
		if( it->second.m_win == win )
			return it->second.m_key;
	}

	return AsciiString::TheEmptyString;
}

//-----------------------------------------------------------------------------
GameWindow *HotKeyManager::findHotKeyWindow( const AsciiString& keyIn, Bool priority ) const
{
	AsciiString key = keyIn;
	key.toLower();

	HotKeyMap::const_iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() || it->second.m_priority != priority )
	{
		return nullptr;
	}

	return it->second.m_win;
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const AsciiString& label)
{
	return searchHotKey(TheGameText->fetch(label));
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const UnicodeString& uStr )
{
	if(uStr.isEmpty())
		return AsciiString::TheEmptyString;

	const WideChar *marker = (const WideChar *)uStr.str();
	while (marker && *marker)
	{
		if (*marker == L'&')
		{
			// found a '&' - now look for the next char
			UnicodeString tmp = UnicodeString::TheEmptyString;
			tmp.concat(*(marker+1));
			AsciiString retStr;
			retStr.translate(tmp);
			return retStr;
		}
		marker++;
	}
	return AsciiString::TheEmptyString;
}

//-----------------------------------------------------------------------------
HotKeyManager::ButtonKeyMap HotKeyManager::s_playerButtonKeys;

//-----------------------------------------------------------------------------
namespace
{
	struct MouseKey
	{
		Int key;
		const char *name;
		const char *corner;
		const WideChar *text;
	};

	// a mouse key's corner character is its place here plus one
	const MouseKey MOUSE_KEYS[] =
	{
		{ MK_MOUSE3, "mouse3", "M3", L"Middle mouse" },
		{ MK_MOUSE4, "mouse4", "M4", L"Mouse 4" },
		{ MK_MOUSE5, "mouse5", "M5", L"Mouse 5" },
		{ MK_WHEELUP, "wheelup", "WU", L"Wheel up" },
		{ MK_WHEELDOWN, "wheeldown", "WD", L"Wheel down" },
	};

	struct FunctionKey
	{
		Int key;
		const char *corner;
	};

	// a function key's corner character follows the mouse ones in this order
	const FunctionKey FUNCTION_KEYS[] =
	{
		{ MK_F1, "F1" },
		{ MK_F2, "F2" },
		{ MK_F3, "F3" },
		{ MK_F4, "F4" },
		{ MK_F5, "F5" },
		{ MK_F6, "F6" },
		{ MK_F7, "F7" },
		{ MK_F8, "F8" },
		{ MK_F9, "F9" },
		{ MK_F10, "F10" },
		{ MK_F11, "F11" },
		{ MK_F12, "F12" },
	};

	Int mouseKeyIndex( Int key )
	{
		for( Int i = 0; i < ARRAY_SIZE( MOUSE_KEYS ); ++i )
		{
			if( MOUSE_KEYS[i].key == key )
			{
				return i;
			}
		}
		return -1;
	}
}

//-----------------------------------------------------------------------------
const char *HotKeyManager::mouseKeyName( Int key )
{
	const Int index = mouseKeyIndex( key );
	return index >= 0 ? MOUSE_KEYS[ index ].name : nullptr;
}

//-----------------------------------------------------------------------------
Int HotKeyManager::mouseKeyFromName( const AsciiString& name )
{
	for( Int i = 0; i < ARRAY_SIZE( MOUSE_KEYS ); ++i )
	{
		if( name.compareNoCase( MOUSE_KEYS[i].name ) == 0 )
		{
			return MOUSE_KEYS[i].key;
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------
const WideChar *HotKeyManager::mouseKeyText( Int key )
{
	const Int index = mouseKeyIndex( key );
	return index >= 0 ? MOUSE_KEYS[ index ].text : nullptr;
}

//-----------------------------------------------------------------------------
Char HotKeyManager::cornerOf( const AsciiString& key )
{
	if( key.getLength() == 1 )
	{
		return isprint( (unsigned char)key.getCharAt( 0 ) ) ? key.getCharAt( 0 ) : 0;
	}
	const Int index = mouseKeyIndex( mouseKeyFromName( key ) );
	return index >= 0 ? (Char)( index + 1 ) : 0;
}

//-----------------------------------------------------------------------------
const char *HotKeyManager::cornerText( Char corner )
{
	if( corner > ARRAY_SIZE( MOUSE_KEYS ) && corner <= ARRAY_SIZE( MOUSE_KEYS ) + ARRAY_SIZE( FUNCTION_KEYS ) )
	{
		return FUNCTION_KEYS[ corner - ARRAY_SIZE( MOUSE_KEYS ) - 1 ].corner;
	}
	return corner >= 1 && corner <= ARRAY_SIZE( MOUSE_KEYS ) ? MOUSE_KEYS[ corner - 1 ].corner : nullptr;
}

//-----------------------------------------------------------------------------
Char HotKeyManager::functionKeyCorner( Int key )
{
	for( Int i = 0; i < ARRAY_SIZE( FUNCTION_KEYS ); ++i )
	{
		if( FUNCTION_KEYS[i].key == key )
		{
			return (Char)( ARRAY_SIZE( MOUSE_KEYS ) + 1 + i );
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------
Int HotKeyManager::chordCorner( Int key, Int modState )
{
	Char base = functionKeyCorner( key );
	if( base == 0 )
	{
		for( const LookupListRec *rec = KeyNames; rec->name; ++rec )
		{
			if( rec->value == key )
			{
				base = cornerOf( AsciiString( rec->name + 4 ) );
				break;
			}
		}
	}
	if( base == 0 )
	{
		return '*';
	}
	Int code = (UnsignedByte)base;
	if( modState & CTRL )
	{
		code |= CTRL_CORNER;
	}
	if( modState & ALT )
	{
		code |= ALT_CORNER;
	}
	if( modState & SHIFT )
	{
		code |= SHIFT_CORNER;
	}
	return code;
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::getButtonHotKey( const AsciiString& buttonName, const AsciiString& label )
{
	if( s_playerButtonKeys.empty() )
	{
		return searchHotKey( label );
	}
	AsciiString name = buttonName;
	name.toLower();
	ButtonKeyMap::const_iterator it = s_playerButtonKeys.find( name );
	if( it != s_playerButtonKeys.end() )
	{
		return it->second;
	}
	return searchHotKey( label );
}

//-----------------------------------------------------------------------------
UnicodeString HotKeyManager::getButtonLabel( const AsciiString& buttonName, const AsciiString& label )
{
	const UnicodeString text = TheGameText->fetch( label );
	if( s_playerButtonKeys.empty() )
	{
		return text;
	}
	AsciiString name = buttonName;
	name.toLower();
	ButtonKeyMap::const_iterator it = s_playerButtonKeys.find( name );
	if( it == s_playerButtonKeys.end() )
	{
		return text;
	}
	return markLabel( text, it->second );
}

//-----------------------------------------------------------------------------
UnicodeString HotKeyManager::markLabel( const UnicodeString& text, const AsciiString& key )
{
	UnicodeString plain;
	for( const WideChar *c = text.str(); *c; ++c )
	{
		if( *c != L'&' )
		{
			plain.concat( *c );
		}
	}
	if( key.isEmpty() )
	{
		return plain;
	}
	if( key.getLength() > 1 )
	{
		plain.concat( L" (" );
		plain.concat( mouseKeyText( mouseKeyFromName( key ) ) );
		plain.concat( L')' );
		return plain;
	}

	const WideChar letter = (WideChar)towlower( key.getCharAt( 0 ) );
	UnicodeString marked;
	Bool placed = FALSE;
	for( const WideChar *c = plain.str(); *c; ++c )
	{
		if( !placed && towlower( *c ) == letter )
		{
			marked.concat( L'&' );
			placed = TRUE;
		}
		marked.concat( *c );
	}
	if( !placed )
	{
		marked.concat( L" (&" );
		marked.concat( (WideChar)towupper( letter ) );
		marked.concat( L')' );
	}
	return marked;
}

//-----------------------------------------------------------------------------
namespace
{
	struct PlayerButtonKey
	{
		AsciiString key;
		Bool found;
	};

	void parsePlayerButtonLetter( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
	{
		PlayerButtonKey *button = (PlayerButtonKey *)store;
		const char *value = ini->getNextToken();
		// NONE takes the key off; anything but one letter or a mouse key is ignored, as digits are control groups
		if( stricmp( value, "NONE" ) == 0 )
		{
			button->key.clear();
			button->found = TRUE;
		}
		else if( strlen( value ) == 1 && isalpha( (unsigned char)value[0] ) )
		{
			button->key.format( "%c", toupper( (unsigned char)value[0] ) );
			button->found = TRUE;
		}
		else if( HotKeyManager::mouseKeyFromName( value ) != 0 )
		{
			button->key = value;
			button->key.toUpper();
			button->found = TRUE;
		}
	}

	const FieldParse PlayerButtonKeyFieldParseTable[] =
	{
		{ "Key", parsePlayerButtonLetter, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
}

//-----------------------------------------------------------------------------
void HotKeyManager::parsePlayerButtonKey( INI *ini )
{
	AsciiString name = ini->getNextToken();
	name.toLower();
	PlayerButtonKey button;
	button.found = FALSE;
	ini->initFromINI( &button, PlayerButtonKeyFieldParseTable );
	if( button.found )
	{
		s_playerButtonKeys[ name ] = button.key;
	}
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::getPlayerKeysPath()
{
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat( "Hotkeys.ini" );
	return path;
}

//-----------------------------------------------------------------------------
void HotKeyManager::loadPlayerKeys()
{
	TheMetaMap->restoreDefaults();
	s_playerButtonKeys.clear();

	const AsciiString path = getPlayerKeysPath();
	if( TheFileSystem->doesFileExist( path.str() ) )
	{
		// a broken block costs the keys from there on, not the game
		INI ini;
		try
		{
			ini.loadHotKeys( path );
			DEBUG_LOG(( "HOTKEYS: read %s", path.str() ));
		}
		catch( ... )
		{
			DEBUG_LOG(( "HOTKEYS: %s stopped at a block it could not read", path.str() ));
		}
	}

	// the bar hands out its letters again on its next update
	if( TheControlBar )
	{
		TheControlBar->markUIDirty();
	}
}

//-----------------------------------------------------------------------------
/*static*/ void INI::parseCommandButtonHotKeyDefinition( INI *ini )
{
	HotKeyManager::parsePlayerButtonKey( ini );
}

//-----------------------------------------------------------------------------
HotKeyManager *TheHotKeyManager = nullptr;

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

