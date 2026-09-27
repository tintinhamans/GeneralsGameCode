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

// FILE: RmlUiScreenHooks.h ////////////////////////////////////////////////////
// Function pointers so device-agnostic GUI callbacks (MainMenu.cpp, QuitMenu.cpp,
// in GameEngine) can open an RmlUi shell screen without GameEngine depending on
// GameEngineDevice/RmlUi -- same pattern as RmlUiInputHook.h. GameEngineDevice's
// RmlUiManager sets these once at init(); they stay null on targets that don't
// link RmlUi, so callers must null-check before calling.
///////////////////////////////////////////////////////////////////////////////

#pragma once

typedef void (*RmlUiScreenFunc)();

// Options is the only screen routed this way in phase 2; later phases add one pair of
// function pointers per Shell::push/pop screen the same way (see report).
extern RmlUiScreenFunc TheRmlUiOpenOptionsScreen;  ///< null unless RmlUi is linked and initialized
extern RmlUiScreenFunc TheRmlUiCloseOptionsScreen;
