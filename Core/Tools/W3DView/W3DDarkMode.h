// TheSuperHackers @feature W3DView native dark mode for Windows 10+
// Lean port of NppDarkMode (notepad-plus-plus) — see DarkMode/ for verbatim NPP files.

#pragma once

#include <windows.h>

// Broadcast to all descendants of the main frame after the theme changes, so that
// custom-owned resources (e.g. tree-view state image lists) can rebuild themselves.
// wParam = 1 if dark, 0 if light. lParam unused.
#define WM_W3DVIEW_THEME_CHANGED (WM_USER + 250)

namespace W3DDarkMode
{
	enum class Mode
	{
		Light = 0,
		Dark  = 1,
		Auto  = 2,
	};

	// Initialize the underlying NPP DarkMode hooks. Safe to call once at startup.
	// Reads the requested mode and resolves it against the Windows setting if Auto.
	void Init(Mode mode);

	// Change mode at runtime. Reapplies to the given top-level window if non-null.
	void SetMode(Mode mode, HWND topLevel);

	Mode GetMode();

	// True iff the resolved theme is dark right now.
	bool IsDark();

	// True iff the OS supports the dark-mode hooks (Windows 10 1809+).
	bool IsSupported();

	// Apply / reapply theming to a window and its children. Idempotent.
	void ApplyToWindow(HWND hwnd);

	// Forward WM_SETTINGCHANGE (lParam == "ImmersiveColorSet") and WM_SYSCOLORCHANGE here
	// so Auto mode flips live when the user changes Windows light/dark setting.
	void HandleSettingChange(HWND topLevel, LPARAM lParam);

	// Colors — only valid when IsDark() is true. In light mode callers should
	// fall through to DefWindowProc / system defaults.
	COLORREF GetBackgroundColor();
	COLORREF GetCtrlBackgroundColor();
	COLORREF GetHotBackgroundColor();
	COLORREF GetDlgBackgroundColor();
	COLORREF GetTextColor();
	COLORREF GetDarkerTextColor();
	COLORREF GetDisabledTextColor();
	COLORREF GetEdgeColor();

	HBRUSH GetBackgroundBrush();
	HBRUSH GetCtrlBackgroundBrush();
	HBRUSH GetHotBackgroundBrush();
	HBRUSH GetDlgBackgroundBrush();
	HBRUSH GetEdgeBrush();

	// Per-control theming primitives.
	void SetDarkTitleBar(HWND hwnd);
	void SetDarkExplorerTheme(HWND hwnd);
	void SetDarkScrollBar(HWND hwnd);
	void ThemeTreeView(HWND hwnd);
	void ThemeListView(HWND hwnd);
	void ThemeToolbar(HWND hwnd);
	void SubclassWindowMenuBar(HWND hwnd);

	// Walks descendants, classifies each by window class name, and applies the
	// appropriate theme/subclass.
	void AutoThemeChildren(HWND parent);

	// Walks top-level windows in this thread that are owned by `parent` (e.g. MFC
	// floating CMiniDockFrameWnd toolbars) and applies titlebar + child theming.
	// Owned popups are siblings in the window tree, so EnumChildWindows misses them.
	void ApplyToOwnedPopups(HWND parent);
}
