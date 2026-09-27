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

// FILE: RmlUiMessageBoxHook.h ///////////////////////////////////////////////
// Single function-pointer hook GameWindowManager::gogoMessageBox() calls
// instead of depending on GameEngineDevice/RmlUi directly -- same layering
// reasoning as RmlUiScreenRegistry.h, but parameterized (title/body/buttons/
// callbacks) instead of .wnd-path-keyed, since every message box shares one
// document and one caller-facing signature. GameEngineDevice's RmlUiManager
// sets the handler at init() and clears it at shutdown(); it stays unset on
// targets that don't link RmlUi, so isAvailable() is always safe to call.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GameWindow.h" // GameWinMsgBoxFunc, MSG_BOX_*

class UnicodeString;

typedef void (*RmlUiMessageBoxFunc)(UnsignedShort buttonFlags,
	const UnicodeString &titleString, const UnicodeString &bodyString,
	GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback,
	GameWinMsgBoxFunc okCallback, GameWinMsgBoxFunc cancelCallback,
	Bool useLogo);

//-------------------------------------------------------------------------------------------------
class RmlUiMessageBoxHook
{
public:
	static void setHandler(RmlUiMessageBoxFunc handler);
	static bool isAvailable();
	static void show(UnsignedShort buttonFlags, const UnicodeString &titleString, const UnicodeString &bodyString,
		GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback,
		GameWinMsgBoxFunc okCallback, GameWinMsgBoxFunc cancelCallback,
		Bool useLogo); ///< no-op if !isAvailable()
};
