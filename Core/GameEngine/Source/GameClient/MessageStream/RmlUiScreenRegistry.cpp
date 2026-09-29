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

#include "PreRTS.h"
#include "GameClient/RmlUiScreenRegistry.h"

#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/RmlUiMessageBoxHook.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"

#include <vector>

namespace
{
	struct Entry
	{
		AsciiString wndPath;
		RmlUiScreenFunc open;
		RmlUiScreenFunc close;
		RmlUiScreenQueryFunc isVisible;
		RmlUiScreenFunc back;
		bool capturesInput;
		RmlUiScreenKeyFunc keys;
		unsigned order; ///< s_openCount at the last open(); orders the visible layers
	};

	// Small and linearly scanned: at most a few dozen screens, looked up on menu navigation only.
	unsigned s_openCount = 0;

	std::vector<Entry> &entries()
	{
		static std::vector<Entry> s_entries;
		return s_entries;
	}

	// A hidden window standing in for a screen (wndPath) or a message box (boxId != 0).
	struct Placeholder
	{
		GameWindow *window;
		AsciiString wndPath;
		UnsignedInt boxId;
	};

	std::vector<Placeholder> &placeholders()
	{
		static std::vector<Placeholder> s_placeholders;
		return s_placeholders;
	}

	GameWindow *makePlaceholder(const AsciiString &wndPath, UnsignedInt boxId)
	{
		GameWindow *window = TheWindowManager->winCreate(nullptr, WIN_STATUS_HIDDEN, 0, 0, 0, 0, GameWinDefaultSystem, nullptr);
		if (window)
		{
			Placeholder p;
			p.window = window;
			p.wndPath = wndPath;
			p.boxId = boxId;
			placeholders().push_back(p);
		}
		return window;
	}

	void layoutInit(WindowLayout *layout, void *userData)
	{
		RmlUiScreenRegistry::open(layout->getFilename());
	}

	// RmlUi documents have no shutdown animation to wait on, so a shell screen completes its
	// push/pop immediately.
	void layoutShutdown(WindowLayout *layout, void *userData)
	{
		RmlUiScreenRegistry::close(layout->getFilename());
		if (TheShell && TheShell->top() == layout)
			TheShell->shutdownComplete(layout);
	}

	Entry *find(const AsciiString &wndPath)
	{
		std::vector<Entry> &e = entries();
		for (size_t i = 0; i < e.size(); ++i)
			if (e[i].wndPath == wndPath)
				return &e[i];
		return nullptr;
	}
}

void RmlUiScreenRegistry::registerScreen(const char *wndPath, RmlUiScreenFunc open, RmlUiScreenFunc close, RmlUiScreenQueryFunc isVisible, RmlUiScreenFunc back, bool capturesInput, RmlUiScreenKeyFunc keys)
{
	AsciiString path(wndPath);
	Entry *existing = find(path);
	if (existing)
	{
		existing->open = open;
		existing->close = close;
		existing->isVisible = isVisible;
		existing->back = back;
		existing->capturesInput = capturesInput;
		existing->keys = keys;
		return;
	}
	Entry e;
	e.wndPath = path;
	e.open = open;
	e.close = close;
	e.isVisible = isVisible;
	e.back = back;
	e.order = 0;
	e.capturesInput = capturesInput;
	e.keys = keys;
	entries().push_back(e);
}

void RmlUiScreenRegistry::unregisterScreen(const char *wndPath)
{
	AsciiString path(wndPath);
	std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
	{
		if (e[i].wndPath == path)
		{
			e.erase(e.begin() + i);
			return;
		}
	}
}

void RmlUiScreenRegistry::unregisterAll()
{
	entries().clear();
}

bool RmlUiScreenRegistry::isRegistered(const AsciiString &wndPath)
{
	return find(wndPath) != nullptr;
}

bool RmlUiScreenRegistry::open(const AsciiString &wndPath)
{
	Entry *e = find(wndPath);
	if (!e || !e->open)
		return false;
	e->order = ++s_openCount;
	e->open();
	return true;
}

bool RmlUiScreenRegistry::close(const AsciiString &wndPath)
{
	Entry *e = find(wndPath);
	if (!e || !e->close)
		return false;
	e->close();
	return true;
}

bool RmlUiScreenRegistry::isOpen(const AsciiString &wndPath)
{
	const Entry *e = find(wndPath);
	return e && e->isVisible && e->isVisible();
}

bool RmlUiScreenRegistry::ownsInput()
{
	const std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
		if (e[i].capturesInput && e[i].isVisible && e[i].isVisible())
			return true;

	return RmlUiMessageBoxHook::isOpen();
}

bool RmlUiScreenRegistry::escape(bool isDown)
{
	if (RmlUiMessageBoxHook::isOpen())
		return true; // like the .wnd boxes, which take Escape without acting on it

	const Entry *top = nullptr;
	const std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
		if (e[i].capturesInput && e[i].isVisible && e[i].isVisible() && (!top || e[i].order > top->order))
			top = &e[i];

	if (!top)
		return false;
	if (isDown && top->back)
		top->back();
	return true;
}

bool RmlUiScreenRegistry::wantsOverlayKeys()
{
	const std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
		if (e[i].keys && e[i].isVisible && e[i].isVisible())
			return true;

	return false;
}

bool RmlUiScreenRegistry::overlayKey(unsigned char key, unsigned char state)
{
	if (ownsInput())
		return false;

	const Entry *top = nullptr;
	const std::vector<Entry> &e = entries();
	for (size_t i = 0; i < e.size(); ++i)
		if (e[i].keys && e[i].isVisible && e[i].isVisible() && (!top || e[i].order > top->order))
			top = &e[i];

	return top && top->keys(key, state);
}

bool RmlUiScreenRegistry::usesLegacyMenus()
{
	return TheGlobalData && TheGlobalData->m_useLegacyMenus;
}

bool RmlUiScreenRegistry::routesToRmlUi(const AsciiString &wndPath)
{
	return !usesLegacyMenus() && isRegistered(wndPath);
}

bool RmlUiScreenRegistry::routesMessageBoxToRmlUi()
{
	return !usesLegacyMenus() && RmlUiMessageBoxHook::isAvailable();
}

//-------------------------------------------------------------------------------------------------
WindowLayout *RmlUiScreenRegistry::createLayout(const AsciiString &wndPath)
{
	WindowLayout *layout = newInstance(WindowLayout);
	layout->loadEmpty(wndPath);
	if (GameWindow *placeholder = makePlaceholder(wndPath, 0))
		layout->addWindow(placeholder);
	layout->setInit(layoutInit);
	layout->setShutdown(layoutShutdown);
	layout->routeToRmlUi(TRUE);
	return layout;
}

GameWindow *RmlUiScreenRegistry::createWindow(const AsciiString &wndPath)
{
	GameWindow *window = makePlaceholder(wndPath, 0);
	if (window)
		open(wndPath);
	return window;
}

WindowLayout *RmlUiScreenRegistry::layoutFor(const AsciiString &wndPath)
{
	const std::vector<Placeholder> &p = placeholders();
	for (size_t i = p.size(); i > 0; --i)
		if (p[i - 1].boxId == 0 && p[i - 1].wndPath == wndPath)
			return p[i - 1].window->winGetLayout();

	return nullptr;
}

GameWindow *RmlUiScreenRegistry::createMessageBoxWindow(UnsignedInt boxId)
{
	return makePlaceholder(AsciiString::TheEmptyString, boxId);
}

void RmlUiScreenRegistry::windowDestroyed(GameWindow *window)
{
	std::vector<Placeholder> &p = placeholders();
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i].window != window)
			continue;

		const Placeholder placeholder = p[i];
		p.erase(p.begin() + i);
		if (placeholder.boxId != 0)
			RmlUiMessageBoxHook::close(placeholder.boxId);
		else if (!placeholder.wndPath.isEmpty())
			close(placeholder.wndPath);
		return;
	}
}

void RmlUiScreenRegistry::destroyMessageBox(UnsignedInt boxId)
{
	if (boxId == 0)
		return;

	std::vector<Placeholder> &p = placeholders();
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i].boxId == boxId)
		{
			TheWindowManager->winDestroy(p[i].window);
			return;
		}
	}
}
