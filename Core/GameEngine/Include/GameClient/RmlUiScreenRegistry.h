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

// FILE: RmlUiScreenRegistry.h //////////////////////////////////////////////////
// .wnd-path-keyed registry of RmlUi replacements for shell screens. GameEngine
// (Shell::push/pop/showShell, getOptionsLayout, MessageBoxOk/YesNo/OkCancel, ...)
// consults this instead of depending on GameEngineDevice/RmlUi directly -- same
// reasoning as RmlUiScreenHooks.h, generalized to every screen instead of one
// hardcoded pair of globals. GameEngineDevice's RmlUiManager registers entries at
// init() and clears them at shutdown(); entries stay empty on targets that don't
// link RmlUi, so isRegistered() is always safe to call.
//
// It also owns the one placeholder mechanism between RmlUi and the GameWindow world: a hidden
// GameWindow standing in for a screen or message box. winDestroy() of it closes the RmlUi side.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

class AsciiString;
class GameWindow;
class WindowLayout;

typedef void (*RmlUiScreenFunc)();
typedef bool (*RmlUiScreenQueryFunc)();

//-------------------------------------------------------------------------------------------------
class RmlUiScreenRegistry
{
public:
	// wndPath must outlive the registration (string literals from the .wnd callers are fine).
	// isVisible: the RmlUi side's own answer, so open state is never bookkept here.
	// capturesInput: while visible, RmlUi owns all mouse/keyboard input; FALSE for overlays drawn over
	// live gameplay, which only take the mouse over their own document (see ownsInput()).
	static void registerScreen(const char *wndPath, RmlUiScreenFunc open, RmlUiScreenFunc close, RmlUiScreenQueryFunc isVisible, bool capturesInput = true);
	static void unregisterScreen(const char *wndPath);
	static void unregisterAll(); ///< RmlUiManager::shutdown()

	static bool isRegistered(const AsciiString &wndPath);

	// The one place that decides whether RmlUi or the legacy .wnd handles a path (or a message box).
	static bool usesLegacyMenus(); ///< -wnd was given
	static bool routesToRmlUi(const AsciiString &wndPath); ///< registered and not -wnd
	static bool routesMessageBoxToRmlUi(); ///< message box hook set and not -wnd

	static bool open(const AsciiString &wndPath);  ///< calls the registered open func; returns true if handled
	static bool close(const AsciiString &wndPath); ///< calls the registered close func; returns true if handled
	static bool isOpen(const AsciiString &wndPath); ///< the screen is visible right now

	// TRUE while the topmost interactive layer is RmlUi: a visible capturing screen, popup or overlay,
	// or an open message box. Everything else (legacy HUD, shell windows) gets input otherwise.
	static bool ownsInput();

	// Placeholders: hidden GameWindows tracked against an RmlUi screen or message box.
	static WindowLayout *createLayout(const AsciiString &wndPath); ///< layout holding a placeholder; runInit/hide(FALSE)/bringForward open the screen, runShutdown/hide(TRUE)/destroyWindows close it
	static GameWindow *createWindow(const AsciiString &wndPath); ///< opens the screen and returns its placeholder; winDestroy() it to close
	static GameWindow *createMessageBoxWindow(UnsignedInt boxId); ///< placeholder for a box RmlUiMessageBoxHook::show() returned
	static void windowDestroyed(GameWindow *window); ///< GameWindowManager::winDestroy(): closes the RmlUi side of a placeholder; no-op for other windows
	static void destroyMessageBox(UnsignedInt boxId); ///< the RmlUi box closed itself: destroys its placeholder like the .wnd box does
};
