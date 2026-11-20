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


// FILE: HotKey.h /////////////////////////////////////////////////
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
//	Filename: 	HotKey.h
//
//	author:		Chris Huybregts
//
//	purpose:
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/SubsystemInterface.h"
#include "Common/MessageStream.h"
//-----------------------------------------------------------------------------
// FORWARD REFERENCES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class AsciiString;
class GameWindow;
class INI;
//-----------------------------------------------------------------------------
// TYPE DEFINES ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class HotKeyTranslator : public GameMessageTranslator
{
public:
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg) override;
	virtual ~HotKeyTranslator() override { }
};

//-----------------------------------------------------------------------------
class HotKey
{
public:
	HotKey();
	GameWindow *m_win;
	AsciiString m_key;
	Bool m_priority;
	// we may need a checkmark system.
};

//-----------------------------------------------------------------------------
class HotKeyManager : public SubsystemInterface
{
public:
	HotKeyManager();
	virtual ~HotKeyManager() override;
	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init() override;															///< Initialize the Hotkey system
	virtual void update() override {}														///< A No-op for us
	virtual void reset() override;															///< Reset
	//-----------------------------------------------------------------------------------------------

	// TheSuperHackers @feature A priority key takes the key from a normal holder and keeps it
	// against any later normal claim.
	void addHotKey( GameWindow *win, const AsciiString& key, Bool priority = FALSE );
	Bool executeHotKey( const AsciiString& key); // called front eh HotKeyTranslator

	AsciiString searchHotKey( const AsciiString& label);
	AsciiString searchHotKey( const UnicodeString& uStr );

	// TheSuperHackers @feature Which hotkey actually got registered to this window, if any.
	// Deliberately not derived from the window's label -- addHotKey drops keys that collide
	// with an earlier button, so only this reports the key that will really work.
	AsciiString getHotKeyForWindow( const GameWindow *win ) const;

	// TheSuperHackers @feature The window holding this key, if its priority matches.
	GameWindow *findHotKeyWindow( const AsciiString& key, Bool priority ) const;

	// TheSuperHackers @feature True while a button press is being synthesized from a keyboard
	// hotkey rather than an actual mouse click. Quick cast needs to tell the two apart.
	static void setExecutingHotKey( Bool executing ) { s_executingHotKey = executing; }
	static Bool isExecutingHotKey( void ) { return s_executingHotKey; }

	// TheSuperHackers @feature True on the key down half of a hold to aim quick cast, when the
	// command should arm and show its decal rather than fire.
	static void setQuickCastAiming( Bool aiming ) { s_quickCastAiming = aiming; }
	static Bool isQuickCastAiming( void ) { return s_quickCastAiming; }

	// TheSuperHackers @feature Is this key currently claimed by a visible, enabled command
	// button? Used so grid hotkeys can take precedence over a meta event bound to the same
	// letter, but only while a button actually wants it.
	Bool isHotKeyClaimed( const AsciiString& key ) const;

	// A command button's letter: the player's from Hotkeys.ini, else the one its label marks with '&'
	AsciiString getButtonHotKey( const AsciiString& buttonName, const AsciiString& label );
	// The label with '&' moved to the player's letter, or added after it when the label lacks the letter
	UnicodeString getButtonLabel( const AsciiString& buttonName, const AsciiString& label );
	static UnicodeString markLabel( const UnicodeString& text, const AsciiString& key );	///< the text with '&' on this key, or the key's name added

	// Player letters by lower case button name; an empty letter means the button has no key
	typedef std::map<AsciiString, AsciiString> ButtonKeyMap;
	static const ButtonKeyMap& getPlayerButtonKeys() { return s_playerButtonKeys; }
	static void parsePlayerButtonKey( INI *ini );

	// Mouse inputs bound like keys: their names in Hotkeys.ini and the hotkey map, and their labels
	static const char *mouseKeyName( Int key );	///< "mouse4" for a mouse key code, null for anything else
	static Int mouseKeyFromName( const AsciiString& name );	///< the mouse key code a name stands for, 0 for none
	static const WideChar *mouseKeyText( Int key );	///< "Mouse 4" for a mouse key code, null for anything else
	// A corner label's code is a corner character, and above it the chord's modifiers, drawn before the key as ! # ^
	enum
	{
		CTRL_CORNER = 0x100,
		ALT_CORNER = 0x200,
		SHIFT_CORNER = 0x400,
		CORNER_CODES = 0x800
	};
	static Char cornerOf( const AsciiString& key );	///< the one character a cameo corner shows for a key, 0 for none
	static const char *cornerText( Char corner );	///< "M4" for a mouse key's corner character, null for a letter
	static Char functionKeyCorner( Int key );	///< the corner character of F1 to F12, 0 for any other key
	static Int chordCorner( Int key, Int modState );	///< the corner code of a command's chord, '*' when its key has no corner label

	// Hotkeys.ini in the user data folder, laid over the shipped command map and button letters
	static AsciiString getPlayerKeysPath();
	static void loadPlayerKeys();

private:
	typedef std::map<AsciiString, HotKey> HotKeyMap;
	HotKeyMap m_hotKeyMap;
	static Bool s_executingHotKey;
	static Bool s_quickCastAiming;
	static ButtonKeyMap s_playerButtonKeys;
};
extern HotKeyManager *TheHotKeyManager;
//-----------------------------------------------------------------------------
// INLINING ///////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// EXTERNALS //////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
