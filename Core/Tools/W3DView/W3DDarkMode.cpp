// TheSuperHackers @feature W3DView native dark mode for Windows 10+
// Lean port of NppDarkMode (notepad-plus-plus) — keeps only what W3DView needs:
// main frame chrome, menubar, titlebar, tree, toolbar, status bar, scroll bars.

#include "StdAfx.h"
#include "W3DDarkMode.h"

#include "DarkMode/DarkMode.h"
#include "DarkMode/UAHMenuBar.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <vsstyle.h>
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	static constexpr COLORREF HEXRGB(DWORD rrggbb)
	{
		return ((rrggbb & 0xFF0000) >> 16) | (rrggbb & 0x00FF00) | ((rrggbb & 0x0000FF) << 16);
	}

	struct DarkColors
	{
		COLORREF background       = HEXRGB(0x202020);
		COLORREF ctrlBackground   = HEXRGB(0x404040);
		COLORREF hotBackground    = HEXRGB(0x404040);
		COLORREF dlgBackground    = HEXRGB(0x2B2B2B);
		COLORREF text             = HEXRGB(0xE0E0E0);
		COLORREF darkerText       = HEXRGB(0xC0C0C0);
		COLORREF disabledText     = HEXRGB(0x808080);
		COLORREF edge             = HEXRGB(0x646464);
	};

	struct State
	{
		DarkColors colors{};
		HBRUSH brBackground     = nullptr;
		HBRUSH brCtrlBackground = nullptr;
		HBRUSH brHotBackground  = nullptr;
		HBRUSH brDlgBackground  = nullptr;
		HBRUSH brEdge           = nullptr;

		W3DDarkMode::Mode mode = W3DDarkMode::Mode::Auto;
		bool initialized = false;
		bool supported   = false;
		bool dark        = false;
	};

	State& S()
	{
		static State s;
		return s;
	}

	void EnsureBrushes()
	{
		State& s = S();
		if (s.brBackground) return;
		s.brBackground     = ::CreateSolidBrush(s.colors.background);
		s.brCtrlBackground = ::CreateSolidBrush(s.colors.ctrlBackground);
		s.brHotBackground  = ::CreateSolidBrush(s.colors.hotBackground);
		s.brDlgBackground  = ::CreateSolidBrush(s.colors.dlgBackground);
		s.brEdge           = ::CreateSolidBrush(s.colors.edge);
	}

	bool ReadAppsUseLightTheme()
	{
		HKEY hKey = nullptr;
		LONG r = ::RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			0, KEY_READ, &hKey);
		if (r != ERROR_SUCCESS)
			return true; // assume light
		DWORD value = 1;
		DWORD cb = sizeof(value);
		DWORD type = 0;
		r = ::RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type,
			reinterpret_cast<LPBYTE>(&value), &cb);
		::RegCloseKey(hKey);
		if (r != ERROR_SUCCESS || type != REG_DWORD)
			return true;
		return value != 0;
	}

	bool ResolveDark(W3DDarkMode::Mode mode)
	{
		if (!S().supported) return false;
		if (IsHighContrast()) return false;
		switch (mode)
		{
			case W3DDarkMode::Mode::Light: return false;
			case W3DDarkMode::Mode::Dark:  return true;
			case W3DDarkMode::Mode::Auto:  return !ReadAppsUseLightTheme();
		}
		return false;
	}

	// Subclass for owner-drawn menu bar (UAH messages).
	LRESULT CALLBACK MenuBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/);

	void PaintMenuBar(HWND hWnd, HDC hdc)
	{
		MENUBARINFO mbi{};
		mbi.cbSize = sizeof(MENUBARINFO);
		::GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);

		RECT rcWindow{};
		::GetWindowRect(hWnd, &rcWindow);

		RECT rcBar = mbi.rcBar;
		::OffsetRect(&rcBar, -rcWindow.left, -rcWindow.top);
		rcBar.top -= 1;

		::FillRect(hdc, &rcBar, W3DDarkMode::GetDlgBackgroundBrush());
	}

	void PaintMenuBarItem(UAHDRAWMENUITEM& UDMI, HTHEME hTheme)
	{
		wchar_t buffer[MAX_PATH] = { 0 };
		MENUITEMINFOW mii{};
		mii.cbSize = sizeof(MENUITEMINFOW);
		mii.fMask = MIIM_STRING;
		mii.dwTypeData = buffer;
		mii.cch = MAX_PATH - 1;
		::GetMenuItemInfoW(UDMI.um.hmenu, static_cast<UINT>(UDMI.umi.iPosition), TRUE, &mii);

		DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
		int iTextStateID = MBI_NORMAL;
		int iBgStateID   = MBI_NORMAL;
		if ((UDMI.dis.itemState & ODS_SELECTED) == ODS_SELECTED)
		{
			iTextStateID = MBI_PUSHED;
			iBgStateID = MBI_PUSHED;
		}
		else if ((UDMI.dis.itemState & ODS_HOTLIGHT) == ODS_HOTLIGHT)
		{
			iTextStateID = ((UDMI.dis.itemState & ODS_INACTIVE) == ODS_INACTIVE) ? MBI_DISABLEDHOT : MBI_HOT;
			iBgStateID = MBI_HOT;
		}
		else if ((UDMI.dis.itemState & (ODS_GRAYED | ODS_DISABLED | ODS_INACTIVE)) != 0)
		{
			iTextStateID = MBI_DISABLED;
			iBgStateID = MBI_DISABLED;
		}
		if ((UDMI.dis.itemState & ODS_NOACCEL) == ODS_NOACCEL)
			dwFlags |= DT_HIDEPREFIX;

		switch (iBgStateID)
		{
			case MBI_NORMAL:
			case MBI_DISABLED:
				::FillRect(UDMI.um.hdc, &UDMI.dis.rcItem, W3DDarkMode::GetDlgBackgroundBrush());
				break;
			case MBI_HOT:
			case MBI_DISABLEDHOT:
				::FillRect(UDMI.um.hdc, &UDMI.dis.rcItem, W3DDarkMode::GetHotBackgroundBrush());
				break;
			case MBI_PUSHED:
			case MBI_DISABLEDPUSHED:
				::FillRect(UDMI.um.hdc, &UDMI.dis.rcItem, W3DDarkMode::GetCtrlBackgroundBrush());
				break;
			default:
				::DrawThemeBackground(hTheme, UDMI.um.hdc, MENU_BARITEM, iBgStateID, &UDMI.dis.rcItem, nullptr);
				break;
		}

		DTTOPTS dttopts{};
		dttopts.dwSize = sizeof(DTTOPTS);
		dttopts.dwFlags = DTT_TEXTCOLOR;
		dttopts.crText = (iTextStateID == MBI_DISABLED || iTextStateID == MBI_DISABLEDHOT || iTextStateID == MBI_DISABLEDPUSHED)
			? W3DDarkMode::GetDisabledTextColor()
			: W3DDarkMode::GetTextColor();

		::DrawThemeTextEx(hTheme, UDMI.um.hdc, MENU_BARITEM, iTextStateID,
			buffer, static_cast<int>(mii.cch), dwFlags, &UDMI.dis.rcItem, &dttopts);
	}

	void PaintMenuNCBottomLine(HWND hWnd)
	{
		MENUBARINFO mbi{};
		mbi.cbSize = sizeof(MENUBARINFO);
		if (!::GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
			return;

		RECT rcClient{};
		::GetClientRect(hWnd, &rcClient);
		::MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&rcClient), 2);
		RECT rcWindow{};
		::GetWindowRect(hWnd, &rcWindow);
		::OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);

		// Fill the whole gap between the menu bar's bottom and the client top:
		// it is 1px on the main frame, but a thick-frame window at high DPI
		// leaves several rows there and the system line sits above the last one.
		RECT rcLine = rcClient;
		rcLine.bottom = rcLine.top;
		rcLine.top = mbi.rcBar.bottom - rcWindow.top - 1;
		if (rcLine.top >= rcLine.bottom)
			rcLine.top = rcLine.bottom - 1;

		HDC hdc = ::GetWindowDC(hWnd);
		::FillRect(hdc, &rcLine, W3DDarkMode::GetDlgBackgroundBrush());
		::ReleaseDC(hWnd, hdc);
	}

	constexpr UINT_PTR kMenuBarSubclassId = 0xD41E0001;

	LRESULT CALLBACK MenuBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		HTHEME hTheme = reinterpret_cast<HTHEME>(dwRefData);

		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, MenuBarSubclassProc, uIdSubclass);
			if (hTheme) ::CloseThemeData(hTheme);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}

		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		// Lazily open the theme.
		if (!hTheme)
		{
			hTheme = ::OpenThemeData(hWnd, VSCLASS_MENU);
			::SetWindowSubclass(hWnd, MenuBarSubclassProc, uIdSubclass,
				reinterpret_cast<DWORD_PTR>(hTheme));
		}

		switch (uMsg)
		{
			case WM_UAHDRAWMENU:
			{
				auto* p = reinterpret_cast<UAHMENU*>(lParam);
				PaintMenuBar(hWnd, p->hdc);
				return 0;
			}
			case WM_UAHDRAWMENUITEM:
			{
				auto* p = reinterpret_cast<UAHDRAWMENUITEM*>(lParam);
				if (hTheme) PaintMenuBarItem(*p, hTheme);
				return 0;
			}
			case WM_THEMECHANGED:
			case WM_DPICHANGED:
			{
				if (hTheme) { ::CloseThemeData(hTheme); }
				::SetWindowSubclass(hWnd, MenuBarSubclassProc, uIdSubclass, 0);
				break;
			}
			case WM_NCACTIVATE:
			case WM_NCPAINT:
			{
				LRESULT r = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
				PaintMenuNCBottomLine(hWnd);
				return r;
			}
			default:
				break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	constexpr UINT_PTR kStatusBarSubclassId  = 0xD41E0002;
	constexpr UINT_PTR kControlBarSubclassId = 0xD41E0003;
	constexpr UINT_PTR kToolbarSubclassId    = 0xD41E0004;
	constexpr UINT_PTR kFrameSubclassId      = 0xD41E0005;
	constexpr UINT_PTR kSplitterSubclassId   = 0xD41E0006;
	constexpr UINT_PTR kToolbarNcSubclassId  = 0xD41E0007;

	// --- Status bar paint subclass --------------------------------------------------
	LRESULT CALLBACK StatusBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, StatusBarSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}
		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		switch (uMsg)
		{
			case WM_ERASEBKGND:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				RECT rc{};
				::GetClientRect(hWnd, &rc);
				::FillRect(hdc, &rc, W3DDarkMode::GetDlgBackgroundBrush());
				return 1;
			}
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = ::BeginPaint(hWnd, &ps);
				RECT rcAll{};
				::GetClientRect(hWnd, &rcAll);
				::FillRect(hdc, &rcAll, W3DDarkMode::GetDlgBackgroundBrush());

				const int parts = static_cast<int>(::SendMessageW(hWnd, SB_GETPARTS, 0, 0));
				HFONT hFont = reinterpret_cast<HFONT>(::SendMessageW(hWnd, WM_GETFONT, 0, 0));
				HGDIOBJ oldFont = hFont ? ::SelectObject(hdc, hFont) : nullptr;
				::SetBkMode(hdc, TRANSPARENT);
				::SetTextColor(hdc, W3DDarkMode::GetTextColor());

				HPEN edgePen = ::CreatePen(PS_SOLID, 1, W3DDarkMode::GetEdgeColor());
				HGDIOBJ oldPen = ::SelectObject(hdc, edgePen);

				for (int i = 0; i < parts; ++i)
				{
					RECT rcPart{};
					::SendMessageW(hWnd, SB_GETRECT, i, reinterpret_cast<LPARAM>(&rcPart));
					if (rcPart.right <= rcPart.left) continue;

					// Pane separator (skip the last part)
					if (i < parts - 1)
					{
						::MoveToEx(hdc, rcPart.right - 1, rcPart.top + 2, nullptr);
						::LineTo(hdc, rcPart.right - 1, rcPart.bottom - 2);
					}

					wchar_t buf[256] = { 0 };
					LRESULT lr = ::SendMessageW(hWnd, SB_GETTEXTLENGTHW, i, 0);
					int len = LOWORD(lr);
					if (len > 0 && len < 255)
					{
						::SendMessageW(hWnd, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(buf));
						RECT rcText = rcPart;
						rcText.left += 4;
						rcText.right -= 4;
						::DrawTextW(hdc, buf, len, &rcText,
							DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
					}
				}

				if (oldPen) ::SelectObject(hdc, oldPen);
				::DeleteObject(edgePen);
				if (oldFont) ::SelectObject(hdc, oldFont);
				::EndPaint(hWnd, &ps);
				return 0;
			}
			default:
				break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// Handles NM_CUSTOMDRAW from a child ToolbarWindow32 — fills the bar with our
	// dark dialog brush and forces text/highlight colors. Returns true if the
	// notification was consumed (in which case the caller must return the LRESULT).
	bool HandleToolbarCustomDraw(LPARAM lParam, LRESULT& outResult)
	{
		auto* nm = reinterpret_cast<LPNMHDR>(lParam);
		if (!nm || nm->code != NM_CUSTOMDRAW) return false;
		wchar_t childCls[64] = { 0 };
		::GetClassNameW(nm->hwndFrom, childCls, 64);
		if (lstrcmpiW(childCls, L"ToolbarWindow32") != 0) return false;

		auto* cd = reinterpret_cast<LPNMTBCUSTOMDRAW>(lParam);
		switch (cd->nmcd.dwDrawStage)
		{
			case CDDS_PREPAINT:
				::FillRect(cd->nmcd.hdc, &cd->nmcd.rc, W3DDarkMode::GetDlgBackgroundBrush());
				outResult = CDRF_NOTIFYITEMDRAW;
				return true;
			case CDDS_ITEMPREPAINT:
				cd->clrText = W3DDarkMode::GetTextColor();
				cd->clrTextHighlight = W3DDarkMode::GetTextColor();
				cd->clrHighlightHotTrack = W3DDarkMode::GetHotBackgroundColor();
				cd->nStringBkMode = TRANSPARENT;
				cd->nHLStringBkMode = TRANSPARENT;
				if (cd->nmcd.uItemState & CDIS_HOT)
				{
					::FillRect(cd->nmcd.hdc, &cd->nmcd.rc,
						W3DDarkMode::GetHotBackgroundBrush());
				}
				else if (cd->nmcd.uItemState & (CDIS_SELECTED | CDIS_CHECKED))
				{
					::FillRect(cd->nmcd.hdc, &cd->nmcd.rc,
						W3DDarkMode::GetCtrlBackgroundBrush());
				}
				outResult = TBCDRF_USECDCOLORS | TBCDRF_HILITEHOTTRACK;
				return true;
			default:
				return false;
		}
	}

	// --- MFC control bar (CToolBar / AfxControlBar* / CMiniDockFrameWnd / CDockBar) ---
	// Handles WM_ERASEBKGND *and* WM_PAINT — CDockBar::OnPaint paints the bar surface
	// itself using GetSysColor(COLOR_BTNFACE), so erase-only isn't enough.
	LRESULT CALLBACK ControlBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, ControlBarSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}
		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		if (uMsg == WM_NOTIFY)
		{
			LRESULT result = 0;
			if (HandleToolbarCustomDraw(lParam, result))
				return result;
		}

		switch (uMsg)
		{
			case WM_ERASEBKGND:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				RECT rc{};
				::GetClientRect(hWnd, &rc);
				::FillRect(hdc, &rc, W3DDarkMode::GetDlgBackgroundBrush());
				return 1;
			}
			case WM_PAINT:
			{
				// Replace CDockBar::OnPaint's COLOR_BTNFACE fill — paint our dark
				// background across the update region. Embedded child bars have
				// their own HWNDs, so they remain unaffected.
				PAINTSTRUCT ps{};
				HDC hdc = ::BeginPaint(hWnd, &ps);
				::FillRect(hdc, &ps.rcPaint, W3DDarkMode::GetDlgBackgroundBrush());
				::EndPaint(hWnd, &ps);
				return 0;
			}
			case WM_NCPAINT:
			{
				// CDockBar's non-client area (the edge strip around docked bars) is
				// painted by the default proc in COLOR_3DFACE / COLOR_BTNHIGHLIGHT.
				// Fill the entire window rect minus the client rect with our dark bg.
				HDC hdc = ::GetWindowDC(hWnd);
				if (hdc)
				{
					RECT rcWin{};
					::GetWindowRect(hWnd, &rcWin);
					RECT rcClient{};
					::GetClientRect(hWnd, &rcClient);
					POINT ptClient{ 0, 0 };
					::ClientToScreen(hWnd, &ptClient);
					::OffsetRect(&rcClient, ptClient.x - rcWin.left, ptClient.y - rcWin.top);
					::OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
					::ExcludeClipRect(hdc, rcClient.left, rcClient.top,
						rcClient.right, rcClient.bottom);
					::FillRect(hdc, &rcWin, W3DDarkMode::GetDlgBackgroundBrush());
					::ReleaseDC(hWnd, hdc);
				}
				return 0;
			}
			default:
				break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// --- ToolbarWindow32 non-client subclass ----------------------------------------
	// MFC's CControlBar handlers paint WM_NCPAINT borders using COLOR_BTNHIGHLIGHT
	// (white) for the top/left edges and COLOR_BTNSHADOW for bottom/right. In dark
	// mode this manifests as a bright white outline around the toolbar. We fill the
	// non-client edges with the dark background brush instead.
	LRESULT CALLBACK ToolbarNcSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, ToolbarNcSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}
		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		if (uMsg == WM_NCPAINT)
		{
			// Let MFC/base paint first, then overwrite the non-client edges.
			LRESULT r = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
			HDC hdc = ::GetWindowDC(hWnd);
			if (hdc)
			{
				RECT rcWin{};
				::GetWindowRect(hWnd, &rcWin);
				RECT rcClient{};
				::GetClientRect(hWnd, &rcClient);
				POINT ptClient{ 0, 0 };
				::ClientToScreen(hWnd, &ptClient);
				::OffsetRect(&rcClient, ptClient.x - rcWin.left, ptClient.y - rcWin.top);
				::OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
				::ExcludeClipRect(hdc, rcClient.left, rcClient.top,
					rcClient.right, rcClient.bottom);
				::FillRect(hdc, &rcWin, W3DDarkMode::GetDlgBackgroundBrush());
				::ReleaseDC(hWnd, hdc);
			}
			return r;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// --- Splitter paint subclass ----------------------------------------------------
	// CSplitterWnd registers under _afxWndMDIFrame (winsplit.cpp:190). It has no
	// default erase-background, and its own OnPaint draws the bar strips between
	// panes in GetSysColor(COLOR_3DFACE). We fill the splitter's client area dark,
	// but DIRECT child panes are excluded from the clip region so that filling the
	// splitter doesn't overpaint the tree / 3D-view pixels (the panes themselves
	// aren't re-invalidated by a drag-resize, so any overpaint persists).
	BOOL CALLBACK ExcludeDirectChildClipProc(HWND hwnd, LPARAM lParam)
	{
		HDC hdc = reinterpret_cast<HDC>(lParam);
		if (!::IsWindowVisible(hwnd)) return TRUE;
		HWND parent = ::GetParent(hwnd);
		if (!parent) return TRUE;
		RECT rc{};
		::GetWindowRect(hwnd, &rc);
		POINT tl{ rc.left, rc.top };
		POINT br{ rc.right, rc.bottom };
		::ScreenToClient(parent, &tl);
		::ScreenToClient(parent, &br);
		::ExcludeClipRect(hdc, tl.x, tl.y, br.x, br.y);
		return TRUE;
	}

	LRESULT CALLBACK SplitterSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, SplitterSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}
		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		// EnumChildWindows-equivalent for direct children only.
		auto excludeDirectChildren = [](HWND parent, HDC hdc)
		{
			HWND child = ::GetWindow(parent, GW_CHILD);
			while (child)
			{
				ExcludeDirectChildClipProc(child, reinterpret_cast<LPARAM>(hdc));
				child = ::GetWindow(child, GW_HWNDNEXT);
			}
		};

		switch (uMsg)
		{
			case WM_ERASEBKGND:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				int saved = ::SaveDC(hdc);
				excludeDirectChildren(hWnd, hdc);
				RECT rc{};
				::GetClientRect(hWnd, &rc);
				::FillRect(hdc, &rc, W3DDarkMode::GetDlgBackgroundBrush());
				::RestoreDC(hdc, saved);
				return 1;
			}
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = ::BeginPaint(hWnd, &ps);
				int saved = ::SaveDC(hdc);
				excludeDirectChildren(hWnd, hdc);
				::FillRect(hdc, &ps.rcPaint, W3DDarkMode::GetDlgBackgroundBrush());
				::RestoreDC(hdc, saved);
				::EndPaint(hWnd, &ps);
				return 0;
			}
			default:
				break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// --- Frame parent subclass: intercept WM_NOTIFY → NM_CUSTOMDRAW from toolbars ----
	// Toolbar buttons paint themselves via the parent's NM_CUSTOMDRAW notifications.
	// We answer PREERASE with our dark fill, then PREPAINT with text color overrides.
	LRESULT CALLBACK FrameSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		if (uMsg == WM_NCDESTROY)
		{
			::RemoveWindowSubclass(hWnd, FrameSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}
		if (!W3DDarkMode::IsDark())
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		if (uMsg == WM_NOTIFY)
		{
			LRESULT result = 0;
			if (HandleToolbarCustomDraw(lParam, result))
				return result;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	BOOL CALLBACK ApplyChildProc(HWND hwnd, LPARAM /*lParam*/)
	{
		wchar_t cls[64] = { 0 };
		::GetClassNameW(hwnd, cls, 64);

		// Use explicit wide class-name literals because this project builds ANSI:
		// the WC_TREEVIEW / TOOLBARCLASSNAME macros expand to char* under TCHAR.
		if (lstrcmpiW(cls, L"SysTreeView32") == 0)
		{
			W3DDarkMode::ThemeTreeView(hwnd);
		}
		else if (lstrcmpiW(cls, L"SysListView32") == 0)
		{
			W3DDarkMode::ThemeListView(hwnd);
		}
		else if (lstrcmpiW(cls, L"ToolbarWindow32") == 0)
		{
			W3DDarkMode::ThemeToolbar(hwnd);
			DWORD_PTR existing = 0;
			if (!::GetWindowSubclass(hwnd, ToolbarNcSubclassProc, kToolbarNcSubclassId, &existing))
				::SetWindowSubclass(hwnd, ToolbarNcSubclassProc, kToolbarNcSubclassId, 0);
			::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		else if (lstrcmpiW(cls, L"msctls_statusbar32") == 0)
		{
			W3DDarkMode::SetDarkExplorerTheme(hwnd);
			DWORD_PTR existing = 0;
			if (!::GetWindowSubclass(hwnd, StatusBarSubclassProc, kStatusBarSubclassId, &existing))
				::SetWindowSubclass(hwnd, StatusBarSubclassProc, kStatusBarSubclassId, 0);
			::InvalidateRect(hwnd, nullptr, TRUE);
		}
		else if (lstrcmpiW(cls, L"ReBarWindow32") == 0)
		{
			W3DDarkMode::SetDarkExplorerTheme(hwnd);
		}
		else if (_wcsnicmp(cls, L"AfxMDIFrame", 11) == 0)
		{
			// CSplitterWnd registers under the MDIFrame class (_afxWndMDIFrame —
			// "no erase bkgnd", per MFC winsplit.cpp). Paint the splitter strips dark.
			DWORD_PTR existing = 0;
			if (!::GetWindowSubclass(hwnd, SplitterSubclassProc, kSplitterSubclassId, &existing))
				::SetWindowSubclass(hwnd, SplitterSubclassProc, kSplitterSubclassId, 0);
			::InvalidateRect(hwnd, nullptr, TRUE);
		}
		else if (_wcsnicmp(cls, L"AfxFrameOrView", 14) == 0 ||
			_wcsnicmp(cls, L"AfxFrame", 8) == 0)
		{
			// MFC view/frame pane wrappers (AfxFrameOrView<ver>) host arbitrary
			// children (tree, 3D view, etc.). They must NOT be subclassed with
			// ControlBarSubclassProc — that subclass fills WM_PAINT dark, and when
			// MFC's splitter resizes a pane the resulting WM_PAINT would overpaint
			// the tree's pixels. The tree isn't re-invalidated by the splitter,
			// so it stays hidden until the user clicks/scrolls.
			// Leave these wrappers to MFC's default painting.
		}
		else if (_wcsnicmp(cls, L"AfxWnd", 6) == 0)
		{
			// Generic AfxWnd<ver> — used by CWnd-derived windows that didn't
			// register their own class. Currently no specialized handling needed,
			// but keep the branch so it doesn't fall into the control-bar one below.
		}
		else if (_wcsnicmp(cls, L"Afx", 3) == 0)
		{
			// Other MFC control bars (CToolBar wrapper, dock targets) — just dark
			// the WM_ERASEBKGND.
			DWORD_PTR existing = 0;
			if (!::GetWindowSubclass(hwnd, ControlBarSubclassProc, kControlBarSubclassId, &existing))
				::SetWindowSubclass(hwnd, ControlBarSubclassProc, kControlBarSubclassId, 0);
			::InvalidateRect(hwnd, nullptr, TRUE);
		}

		W3DDarkMode::SetDarkScrollBar(hwnd);

		// Broadcast theme-change so owners of custom resources (e.g. DataTreeView
		// state image list) can rebuild themselves.
		::SendMessageW(hwnd, WM_W3DVIEW_THEME_CHANGED,
			W3DDarkMode::IsDark() ? 1 : 0, 0);

		// Recurse — splitter panes etc. host children.
		::EnumChildWindows(hwnd, ApplyChildProc, 0);
		return TRUE;
	}
}

namespace W3DDarkMode
{
	void Init(Mode mode)
	{
		State& s = S();
		if (s.initialized) return;

		::InitDarkMode();
		s.supported = ::IsWindows10();
		s.mode = mode;
		s.dark = ResolveDark(mode);
		EnsureBrushes();

		if (s.supported)
		{
			::SetDarkMode(s.dark, true);
			::AllowDarkModeForApp(s.dark);
		}

		s.initialized = true;
	}

	void SetMode(Mode mode, HWND topLevel)
	{
		State& s = S();
		s.mode = mode;
		bool wasDark = s.dark;
		s.dark = ResolveDark(mode);

		if (s.supported)
		{
			::SetDarkMode(s.dark, true);
			::AllowDarkModeForApp(s.dark);
		}

		if (topLevel && ::IsWindow(topLevel))
		{
			ApplyToWindow(topLevel);
			// Force a redraw of menubar / non-client.
			::DrawMenuBar(topLevel);
			::SetWindowPos(topLevel, nullptr, 0, 0, 0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			::RedrawWindow(topLevel, nullptr, nullptr,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
		}

		(void)wasDark;
	}

	Mode GetMode() { return S().mode; }
	bool IsDark()  { return S().dark; }
	bool IsSupported() { return S().supported; }

	void ApplyToWindow(HWND hwnd)
	{
		if (!hwnd || !::IsWindow(hwnd)) return;
		EnsureBrushes();

		SetDarkTitleBar(hwnd);
		SubclassWindowMenuBar(hwnd);

		// Intercept toolbar custom-draw via the parent's WM_NOTIFY.
		DWORD_PTR existing = 0;
		if (!::GetWindowSubclass(hwnd, FrameSubclassProc, kFrameSubclassId, &existing))
			::SetWindowSubclass(hwnd, FrameSubclassProc, kFrameSubclassId, 0);

		AutoThemeChildren(hwnd);
		ApplyToOwnedPopups(hwnd);
	}

	void HandleSettingChange(HWND topLevel, LPARAM lParam)
	{
		if (::IsColorSchemeChangeMessage(lParam))
		{
			State& s = S();
			if (s.mode == Mode::Auto)
				SetMode(Mode::Auto, topLevel);
		}
	}

	COLORREF GetBackgroundColor()      { return S().colors.background; }
	COLORREF GetCtrlBackgroundColor()  { return S().colors.ctrlBackground; }
	COLORREF GetHotBackgroundColor()   { return S().colors.hotBackground; }
	COLORREF GetDlgBackgroundColor()   { return S().colors.dlgBackground; }
	COLORREF GetTextColor()            { return S().colors.text; }
	COLORREF GetDarkerTextColor()      { return S().colors.darkerText; }
	COLORREF GetDisabledTextColor()    { return S().colors.disabledText; }
	COLORREF GetEdgeColor()            { return S().colors.edge; }

	HBRUSH GetBackgroundBrush()     { EnsureBrushes(); return S().brBackground; }
	HBRUSH GetCtrlBackgroundBrush() { EnsureBrushes(); return S().brCtrlBackground; }
	HBRUSH GetHotBackgroundBrush()  { EnsureBrushes(); return S().brHotBackground; }
	HBRUSH GetDlgBackgroundBrush()  { EnsureBrushes(); return S().brDlgBackground; }
	HBRUSH GetEdgeBrush()           { EnsureBrushes(); return S().brEdge; }

	void SetDarkTitleBar(HWND hwnd)
	{
		if (!S().supported) return;
		BOOL value = S().dark ? TRUE : FALSE;
		// 19041+: DwmSetWindowAttribute. Older 10 builds: fall back to the NPP helpers.
		if (::GetWindowsBuildNumber() >= 19041)
		{
			::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
		}
		else
		{
			::AllowDarkModeForWindow(hwnd, S().dark);
			::RefreshTitleBarThemeColor(hwnd);
		}
	}

	void SetDarkExplorerTheme(HWND hwnd)
	{
		::SetWindowTheme(hwnd, (S().supported && S().dark) ? L"DarkMode_Explorer" : nullptr, nullptr);
	}

	void SetDarkScrollBar(HWND hwnd)
	{
		if (S().supported && S().dark)
		{
			::EnableDarkScrollBarForWindowAndChildren(hwnd);
		}
	}

	void ThemeTreeView(HWND hwnd)
	{
		SetDarkExplorerTheme(hwnd);
		if (S().dark)
		{
			TreeView_SetBkColor(hwnd, GetCtrlBackgroundColor());
			TreeView_SetTextColor(hwnd, GetTextColor());
			TreeView_SetLineColor(hwnd, GetEdgeColor());
		}
		else
		{
			// TVM_SETBKCOLOR/SETTEXTCOLOR want (COLORREF)-1 to mean "use system
			// default". CLR_DEFAULT (0xFF000000) would actually paint solid black.
			TreeView_SetBkColor(hwnd, static_cast<COLORREF>(-1));
			TreeView_SetTextColor(hwnd, static_cast<COLORREF>(-1));
			TreeView_SetLineColor(hwnd, static_cast<COLORREF>(-1));
		}
	}

	void ThemeListView(HWND hwnd)
	{
		SetDarkExplorerTheme(hwnd);
		if (S().dark)
		{
			ListView_SetBkColor(hwnd, GetCtrlBackgroundColor());
			ListView_SetTextBkColor(hwnd, GetCtrlBackgroundColor());
			ListView_SetTextColor(hwnd, GetTextColor());
		}
		else
		{
			// Same caveat as ThemeTreeView: ListView wants -1 for "system default",
			// CLR_DEFAULT means literal black.
			ListView_SetBkColor(hwnd, static_cast<COLORREF>(-1));
			ListView_SetTextBkColor(hwnd, static_cast<COLORREF>(-1));
			ListView_SetTextColor(hwnd, static_cast<COLORREF>(-1));
		}
	}

	void ThemeToolbar(HWND hwnd)
	{
		SetDarkExplorerTheme(hwnd);
		if (S().dark)
		{
			COLORSCHEME scheme{};
			scheme.dwSize = sizeof(COLORSCHEME);
			scheme.clrBtnHighlight = GetDlgBackgroundColor();
			scheme.clrBtnShadow    = GetDlgBackgroundColor();
			::SendMessageW(hwnd, TB_SETCOLORSCHEME, 0, reinterpret_cast<LPARAM>(&scheme));
		}
		else
		{
			COLORSCHEME scheme{};
			scheme.dwSize = sizeof(COLORSCHEME);
			scheme.clrBtnHighlight = CLR_DEFAULT;
			scheme.clrBtnShadow    = CLR_DEFAULT;
			::SendMessageW(hwnd, TB_SETCOLORSCHEME, 0, reinterpret_cast<LPARAM>(&scheme));
		}
	}

	void SubclassWindowMenuBar(HWND hwnd)
	{
		// Always install — the proc itself bails when not dark.
		DWORD_PTR existing = 0;
		if (::GetWindowSubclass(hwnd, MenuBarSubclassProc, kMenuBarSubclassId, &existing))
			return;
		::SetWindowSubclass(hwnd, MenuBarSubclassProc, kMenuBarSubclassId, 0);
	}

	void AutoThemeChildren(HWND parent)
	{
		::EnumChildWindows(parent, ApplyChildProc, 0);
	}

	namespace
	{
		struct OwnedPopupCtx
		{
			HWND owner;
		};

		BOOL CALLBACK ApplyOwnedPopupProc(HWND hwnd, LPARAM lParam)
		{
			auto* ctx = reinterpret_cast<OwnedPopupCtx*>(lParam);
			if (hwnd == ctx->owner) return TRUE;
			if (::GetWindow(hwnd, GW_OWNER) != ctx->owner) return TRUE;
			// Apply dark titlebar to the popup itself, then theme its children.
			SetDarkTitleBar(hwnd);
			AutoThemeChildren(hwnd);
			// Force a non-client repaint so the new titlebar style takes effect.
			::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			::RedrawWindow(hwnd, nullptr, nullptr,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
			return TRUE;
		}
	}

	void ApplyToOwnedPopups(HWND parent)
	{
		if (!parent || !::IsWindow(parent)) return;
		OwnedPopupCtx ctx{ parent };
		::EnumThreadWindows(::GetCurrentThreadId(), ApplyOwnedPopupProc,
			reinterpret_cast<LPARAM>(&ctx));
	}
}
