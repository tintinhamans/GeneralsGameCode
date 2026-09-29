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

#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/EmbeddedArchiveFile.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/Registry.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/RmlUiMessageBoxHook.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"

#include <set>
#include <string>
#include <vector>

#if defined(GENERALS_ONLINE)
#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#endif

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

//-------------------------------------------------------------------------------------------------
// Mod .wnd fallback. A mod that ships its own Window\<path> means that one screen goes back to the
// .wnd. Stock is what the retail archives or the embedded Generals Online archive provide; a loose
// file or any other .big is a mod's.
namespace
{
	// Retail Generals and Zero Hour .big names (Zero Hour's with a "ZH" suffix), per language where
	// the SKUs ship one.
	bool isRetailArchiveName(std::string name)
	{
		for (size_t i = 0; i < name.size(); ++i)
			name[i] = (char)tolower((unsigned char)name[i]);
		if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".big") != 0)
			return false;
		name.erase(name.size() - 4);
		if (name.size() > 2 && name.compare(name.size() - 2, 2, "zh") == 0)
			name.erase(name.size() - 2);

		static const char *const plain[] = { "audio", "english", "gensec", "ini", "maps", "music", "patch", "shaders", "speech", "terrain", "textures", "w3d", "window" };
		static const char *const localizedPrefixes[] = { "", "audio", "speech", "w3d" };
		static const char *const languages[] = { "english", "german", "french", "spanish", "italian", "korean", "chinese", "brazilian", "polish" };
		for (size_t i = 0; i < ARRAY_SIZE(plain); ++i)
			if (name == plain[i])
				return true;
		for (size_t p = 0; p < ARRAY_SIZE(localizedPrefixes); ++p)
			for (size_t l = 0; l < ARRAY_SIZE(languages); ++l)
				if (name == std::string(localizedPrefixes[p]) + languages[l])
					return true;
		return false;
	}

	std::string normalizedPath(const char *path)
	{
		std::string s(path ? path : "");
		for (size_t i = 0; i < s.size(); ++i)
			s[i] = s[i] == '/' ? '\\' : (char)tolower((unsigned char)s[i]);
		return s;
	}

	// Win32BIGFileSystem::init() loads the game folder's .bigs (subfolders included, which is where
	// mods go) and, for Zero Hour, the original Generals install's. Only the top of those folders is retail.
	bool isStockArchive(ArchiveFile *archive)
	{
		if (dynamic_cast<EmbeddedArchiveFile *>(archive))
			return true;

		const std::string name = normalizedPath(archive->getName().str());
		const size_t slash = name.find_last_of('\\');
		const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
		if (!isRetailArchiveName(base))
			return false;
		if (slash == std::string::npos)
			return true;

#if RTS_ZEROHOUR
		AsciiString installPath;
		if (GetStringFromGeneralsRegistry("", "InstallPath", installPath) && !installPath.isEmpty())
		{
			std::string dir = normalizedPath(installPath.str());
			if (dir[dir.size() - 1] != '\\')
				dir += '\\';
			if (name.compare(0, slash + 1, dir) == 0)
				return true;
		}
#endif
		return false;
	}

	// FileSystem::openFile()'s order: a loose file first, then the archive on top of the directory tree.
	// found is FALSE if nothing provides the file; source names what does.
	bool isStockFile(const AsciiString &path, bool &found, AsciiString &source)
	{
		found = false;
		if (TheLocalFileSystem && TheLocalFileSystem->doesFileExist(path.str()))
		{
			found = true;
			source = "a loose file";
			return false;
		}
		ArchiveFile *archive = TheArchiveFileSystem ? TheArchiveFileSystem->getArchiveFile(path) : nullptr;
		if (!archive)
			return true;
		found = true;
		source = archive->getName();
		return isStockArchive(archive);
	}

	// Where winCreateFromScript() reads wndPath from.
	bool isModWnd(const AsciiString &wndPath, AsciiString &source)
	{
		const bool bare = strchr(wndPath.str(), '\\') == nullptr;
		bool found = false;
		AsciiString path;
#if defined(GENERALS_ONLINE)
		path = bare ? AsciiString("GeneralsOnlineGameData\\") : AsciiString::TheEmptyString;
		path.concat(wndPath);
		const bool goStock = isStockFile(path, found, source);
		if (found)
			return !goStock;
#endif
		path = bare ? AsciiString("Window\\") : AsciiString::TheEmptyString;
		path.concat(wndPath);
		const bool stock = isStockFile(path, found, source);
		return found && !stock;
	}

	// A .wnd popup cannot show over an RmlUi screen (RmlUi draws on top and takes the input), so
	// when a popup falls back, the screens it opens over fall back with it. The map select popups
	// also work only with their own setup screen, so those pairs go together both ways.
	struct PopupHost
	{
		const char *popup;
		const char *host;
		bool both; ///< the host's fallback takes the popup along too
	};

	const PopupHost s_popupHosts[] =
	{
		{ "Menus/SkirmishMapSelectMenu.wnd", "Menus/SkirmishGameOptionsMenu.wnd", true },
		{ "Menus/LanMapSelectMenu.wnd", "Menus/LanGameOptionsMenu.wnd", true },
		{ "Menus/WOLMapSelectMenu.wnd", "Menus/GameSpyGameOptionsMenu.wnd", true },
		{ "Menus/OptionsMenu.wnd", "Menus/MainMenu.wnd", false },
		{ "Menus/OptionsMenu.wnd", "Menus/QuitMenu.wnd", false },
		{ "Menus/OptionsMenu.wnd", "Menus/QuitNoSave.wnd", false },
		{ "Menus/OptionsMenu.wnd", "Menus/WOLWelcomeMenu.wnd", false },
		{ "Menus/OptionsMenu.wnd", "Menus/PopupPlayerInfo.wnd", false },
		{ "Menus/DownloadMenu.wnd", "Menus/MainMenu.wnd", false },
		{ "Menus/PopupSaveLoad.wnd", "Menus/QuitMenu.wnd", false },
		{ "Menus/PopupSaveLoad.wnd", "Menus/QuitNoSave.wnd", false },
		{ "Menus/PopupReplay.wnd", "Menus/ScoreScreen.wnd", false },
		{ "Menus/PopupPlayerInfo.wnd", "Menus/WOLWelcomeMenu.wnd", false },
		{ "Menus/PopupPlayerInfo.wnd", "Menus/WOLCustomLobby.wnd", false },
		{ "Menus/PopupPlayerInfo.wnd", "Menus/WOLBuddyOverlay.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/WOLWelcomeMenu.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/WOLCustomLobby.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/GameSpyGameOptionsMenu.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/WOLQuickMatchMenu.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/ScoreScreen.wnd", false },
		{ "Menus/WOLBuddyOverlay.wnd", "Menus/PopupPlayerInfo.wnd", false },
		{ "Menus/PopupHostGame.wnd", "Menus/WOLCustomLobby.wnd", false },
		{ "Menus/PopupJoinGame.wnd", "Menus/WOLCustomLobby.wnd", false },
	};

	// The registered paths that go to the .wnd, worked out once all screens are registered (the
	// mods are loaded by then) and logged once each.
	struct ModFallback
	{
		bool computed = false;
		std::set<AsciiString> paths;
	};

	ModFallback &modFallback()
	{
		static ModFallback s_fallback;
		return s_fallback;
	}

	void logFallback(const AsciiString &wndPath, const char *reason, const char *detail)
	{
		DEBUG_LOG(("RmlUi: %s uses its .wnd, %s %s", wndPath.str(), reason, detail));
#if defined(GENERALS_ONLINE)
		NetworkLog(ELogVerbosity::LOG_RELEASE, "RmlUi: %s uses its .wnd, %s %s", wndPath.str(), reason, detail);
#endif
	}

	void computeModFallback()
	{
		ModFallback &fallback = modFallback();
		fallback.computed = true;
		fallback.paths.clear();

		const std::vector<Entry> &e = entries();
		for (size_t i = 0; i < e.size(); ++i)
		{
			AsciiString source;
			if (isModWnd(e[i].wndPath, source))
			{
				fallback.paths.insert(e[i].wndPath);
				logFallback(e[i].wndPath, "a mod provides it in", source.str());
			}
		}

		bool changed = true;
		while (changed)
		{
			changed = false;
			for (size_t i = 0; i < ARRAY_SIZE(s_popupHosts); ++i)
			{
				const AsciiString popup(s_popupHosts[i].popup);
				const AsciiString host(s_popupHosts[i].host);
				const bool popupFalls = fallback.paths.count(popup) != 0;
				const bool hostFalls = fallback.paths.count(host) != 0;
				if (popupFalls && !hostFalls && find(host))
				{
					fallback.paths.insert(host);
					logFallback(host, "along with its popup", popup.str());
					changed = true;
				}
				else if (s_popupHosts[i].both && hostFalls && !popupFalls && find(popup))
				{
					fallback.paths.insert(popup);
					logFallback(popup, "along with its screen", host.str());
					changed = true;
				}
			}
		}
	}
}

bool RmlUiScreenRegistry::usesModWnd(const AsciiString &wndPath)
{
	if (TheGlobalData && TheGlobalData->m_rmlIgnoreModWnds)
		return false;
	if (!TheLocalFileSystem || !TheArchiveFileSystem)
		return false;

	ModFallback &fallback = modFallback();
	if (!fallback.computed)
		computeModFallback();
	return fallback.paths.count(wndPath) != 0;
}

void RmlUiScreenRegistry::registerScreen(const char *wndPath, RmlUiScreenFunc open, RmlUiScreenFunc close, RmlUiScreenQueryFunc isVisible, RmlUiScreenFunc back, bool capturesInput, RmlUiScreenKeyFunc keys)
{
	modFallback().computed = false;
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
	modFallback().computed = false;
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
	return !usesLegacyMenus() && isRegistered(wndPath) && !usesModWnd(wndPath);
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
