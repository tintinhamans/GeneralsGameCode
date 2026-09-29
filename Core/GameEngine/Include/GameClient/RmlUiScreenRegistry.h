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
typedef bool (*RmlUiScreenKeyFunc)(unsigned char key, unsigned short state); ///< TRUE if the screen took the key

//-------------------------------------------------------------------------------------------------
class RmlUiScreenRegistry
{
public:
	// wndPath must outlive the registration (string literals from the .wnd callers are fine).
	// isVisible: the RmlUi side's own answer, so open state is never bookkept here.
	// back: what Escape does while this is the topmost layer (the original .wnd's KEY_ESC); null = nothing.
	// capturesInput: while visible, RmlUi owns all mouse/keyboard input; FALSE for overlays drawn over
	// live gameplay, which only take the mouse over their own document (see ownsInput()).
	// keys: for an overlay that wants a few keys while it is up (an Enter or Escape that dismisses it, ...) although it
	// does not capture input; see overlayKey().
	static void registerScreen(const char *wndPath, RmlUiScreenFunc open, RmlUiScreenFunc close, RmlUiScreenQueryFunc isVisible, RmlUiScreenFunc back = nullptr, bool capturesInput = true, RmlUiScreenKeyFunc keys = nullptr);
	static void unregisterScreen(const char *wndPath);
	static void unregisterAll(); ///< RmlUiManager::shutdown()

	static bool isRegistered(const AsciiString &wndPath);

	// The one place that decides whether RmlUi or the legacy .wnd handles a path (or a message box).
	static bool usesLegacyMenus(); ///< -wnd was given
	static bool routesToRmlUi(const AsciiString &wndPath); ///< registered, not -wnd and not usesModWnd()
	// The message box stays RmlUi when a mod supplies MessageBox.wnd: a .wnd box could not show over the
	// RmlUi screens every box can pop up on.
	static bool routesMessageBoxToRmlUi(); ///< message box hook set and not -wnd

	// A mod supplies this screen's .wnd (a loose file or a .big other than the retail ones and the
	// embedded Generals Online one), or a popup of it or its map select does, so it keeps its .wnd.
	// Worked out once all screens are registered, logged once per screen; -rmlignoremodwnd turns it off.
	static bool usesModWnd(const AsciiString &wndPath);

	static bool open(const AsciiString &wndPath);  ///< calls the registered open func; returns true if handled
	static bool close(const AsciiString &wndPath); ///< calls the registered close func; returns true if handled
	static bool isOpen(const AsciiString &wndPath); ///< the screen is visible right now

	// TRUE while the topmost interactive layer is RmlUi: a visible capturing screen, popup or overlay,
	// or an open message box. Everything else (legacy HUD, shell windows) gets input otherwise.
	static bool ownsInput();

	// Escape goes to the topmost capturing layer (the most recently opened visible one) and never to a
	// screen underneath. Returns TRUE if a layer owns it (its back func runs on key down).
	static bool escape(bool isDown);

	// The topmost capturing layer (the most recently opened visible one) other than except; empty when
	// there is none or a message box is up. A layer that sits beside the screens (the social dock) asks
	// this to know which screen it is over.
	static AsciiString topLayer(const AsciiString &except);
	// Makes a visible layer the topmost again without reopening it, so Escape reaches it first; for a
	// layer that stays open while the screens change under it.
	static void raise(const AsciiString &wndPath);

	// A visible overlay with a key handler takes the keys it wants ahead of the game, as the .wnd overlay did
	// while it had the keyboard focus. Nothing is offered while a capturing layer is up: that owns the keyboard.
	static bool wantsOverlayKeys(); ///< such an overlay is visible
	static bool overlayKey(unsigned char key, unsigned short state); ///< offers a key to the topmost one; TRUE if it took it

	// Placeholders: hidden GameWindows tracked against an RmlUi screen or message box.
	static WindowLayout *createLayout(const AsciiString &wndPath); ///< layout holding a placeholder; runInit/hide(FALSE)/bringForward open the screen, runShutdown/hide(TRUE)/destroyWindows close it
	static GameWindow *createWindow(const AsciiString &wndPath); ///< opens the screen and returns its placeholder; winDestroy() it to close
	static WindowLayout *layoutFor(const AsciiString &wndPath); ///< the newest layout createLayout() made for a screen; null if there is none
	static GameWindow *createMessageBoxWindow(UnsignedInt boxId); ///< placeholder for a box RmlUiMessageBoxHook::show() returned
	static void windowDestroyed(GameWindow *window); ///< GameWindowManager::winDestroy(): closes the RmlUi side of a placeholder; no-op for other windows
	static void destroyMessageBox(UnsignedInt boxId); ///< the RmlUi box closed itself: destroys its placeholder like the .wnd box does
};
