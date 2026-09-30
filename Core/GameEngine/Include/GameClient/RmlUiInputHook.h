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

// FILE: RmlUiInputHook.h /////////////////////////////////////////////////////
// Minimal interface so the shared input translator (WindowXlat.cpp) can
// route input to an RmlUi context without Core depending on RmlUi.
// TheRmlUiInputHook stays null on targets that do not link RmlUi (Generals,
// VC6), so behavior there is unchanged.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

#include "Common/AsciiString.h"

//-------------------------------------------------------------------------------------------------
class RmlUiInputHook
{
public:
	virtual ~RmlUiInputHook() {}

	// TRUE if RmlUi should intercept mouse input right now: the topmost interactive layer is RmlUi
	// (see RmlUiScreenRegistry::ownsInput()), or the cursor is over a visible document.
	virtual bool wantsMouseInput(int mouseX, int mouseY) const = 0;

	// TRUE while a capturing RmlUi layer (screen, popup, message box) is up; an overlay drawn over the game
	// (the HUD) is not one, and leaves the mouse to the world while the world holds it (drag-select, scrolling).
	virtual bool capturesInput() const = 0;

	// TRUE if RmlUi owns the keyboard (same rule), or a text field in a visible document has
	// focus, so keyboard input should not reach the game's window manager.
	virtual bool wantsKeyboardInput() const = 0;

	virtual void processMouseMove(int x, int y) = 0;
	virtual void processMouseButton(int button, bool down) = 0; // 0=left,1=right,2=middle
	virtual void processMouseWheel(float delta) = 0;
	virtual bool processKey(unsigned char engineKey, unsigned short engineKeyState) = 0; // FALSE: not consumed, the game handles the key; state is 16 bits so KEY_STATE_AUTOREPEAT (0x100) survives

	// One UTF-16 code unit from WM_CHAR. DirectInput (processKey above) gives scan codes only,
	// not text, so the window proc forwards WM_CHAR here directly for text entry fields.
	virtual void processTextInput(unsigned short utf16Char) = 0;

	// TRUE while an RmlUi text field has focus and owns the IME: the window proc then offers every
	// window message to processImeMessage() and keeps the legacy IMEManager out of it.
	virtual bool wantsImeInput() const = 0;

	// TRUE if consumed; result is then the window proc's return value.
	virtual bool processImeMessage(void *hwnd, unsigned int message, uintptr_t wParam, intptr_t lParam, intptr_t &result) = 0;
};

extern RmlUiInputHook *TheRmlUiInputHook; ///< null unless an RmlUi-enabled target has set it
