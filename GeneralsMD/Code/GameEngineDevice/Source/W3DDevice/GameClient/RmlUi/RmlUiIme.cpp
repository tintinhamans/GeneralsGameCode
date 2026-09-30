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

#include "W3DDevice/GameClient/RmlUi/RmlUiIme.h"

#include <imm.h>
#include <string>
#include <vector>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/StringUtilities.h>

#include "GameClient/IMEManager.h"

extern HWND ApplicationHWnd;

static Rml::String toUtf8(const std::wstring &wide)
{
	if (wide.empty())
		return Rml::String();
	const int count = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), nullptr, 0, nullptr, nullptr);
	Rml::String out(count, 0);
	::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), &out[0], count, nullptr, nullptr);
	return out;
}

static std::wstring getCompositionString(HIMC himc, bool result)
{
	const LONG bytes = ::ImmGetCompositionStringW(himc, result ? GCS_RESULTSTR : GCS_COMPSTR, nullptr, 0);
	if (bytes <= 0)
		return std::wstring();
	std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, 0);
	::ImmGetCompositionStringW(himc, result ? GCS_RESULTSTR : GCS_COMPSTR, buffer.data(), bytes);
	return std::wstring(buffer.data(), bytes / sizeof(wchar_t));
}

// Only the keys an IME takes for itself while composing; letters and digits reach RmlUi as text.
static int imeKeyFromVirtualKey(int vk)
{
	using namespace Rml::Input;
	switch (vk)
	{
		case VK_RETURN: return KI_RETURN;
		case VK_BACK: return KI_BACK;
		case VK_ESCAPE: return KI_ESCAPE;
		case VK_SPACE: return KI_SPACE;
		case VK_TAB: return KI_TAB;
		case VK_LEFT: return KI_LEFT;
		case VK_RIGHT: return KI_RIGHT;
		case VK_UP: return KI_UP;
		case VK_DOWN: return KI_DOWN;
		case VK_HOME: return KI_HOME;
		case VK_END: return KI_END;
		case VK_PRIOR: return KI_PRIOR;
		case VK_NEXT: return KI_NEXT;
		case VK_DELETE: return KI_DELETE;
		default: return KI_UNKNOWN;
	}
}

static bool isEditingKey(Rml::Input::KeyIdentifier key)
{
	using namespace Rml::Input;
	return key != KI_UNKNOWN && key != KI_LSHIFT && key != KI_RSHIFT && key != KI_LCONTROL && key != KI_RCONTROL
		&& key != KI_LMENU && key != KI_RMENU;
}

//-------------------------------------------------------------------------------------------------
void RmlUiIme::OnActivate(Rml::TextInputContext *input)
{
	m_input = input;
	engage(m_hwnd ? m_hwnd : ApplicationHWnd);
	applyCaret();
}

void RmlUiIme::OnDeactivate(Rml::TextInputContext *input)
{
	if (m_input != input)
		return;
	deactivate();
}

void RmlUiIme::deactivate()
{
	if (!m_input)
		return;

	// Focus left mid-composition: drop the text, as a browser does for a lost field.
	if (m_composing)
	{
		if (HIMC himc = m_hwnd ? ::ImmGetContext(m_hwnd) : nullptr)
		{
			::ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
			::ImmReleaseContext(m_hwnd, himc);
		}
		if (m_composing)
			cancelComposition();
	}
	m_input = nullptr;
	m_haveCaret = false;
	m_imeKeys.clear();
	// The engine's IME state comes back from update(), so moving focus from one field to the next
	// does not tear the input context down in between.
}

void RmlUiIme::OnDestroy(Rml::TextInputContext *input)
{
	if (m_input == input)
	{
		m_composing = false;
		m_rangeStart = m_rangeEnd = 0;
		m_input = nullptr;
		m_haveCaret = false;
		m_imeKeys.clear();
	}
}

void RmlUiIme::update()
{
	if (m_engaged && !m_input)
		release();
}

void RmlUiIme::shutdown()
{
	m_input = nullptr;
	if (m_engaged)
	{
		// The IMEManager may already be gone and have restored the window's own context.
		if (TheIMEManager && ::IsWindow(m_hwnd))
			release();
		else
			m_engaged = false;
	}
	m_context = nullptr;
}

//-------------------------------------------------------------------------------------------------
void RmlUiIme::engage(HWND hwnd)
{
	if (m_engaged || !hwnd)
		return;
	m_hwnd = hwnd;

	// IMEManager keeps a context of its own on the window (or none at all, see disable()). Windows
	// text services, the emoji panel included, only work with the window's default one.
	m_prevContext = ::ImmGetContext(hwnd);
	if (m_prevContext)
		::ImmReleaseContext(hwnd, m_prevContext);
	::ImmAssociateContextEx(hwnd, nullptr, IACE_DEFAULT);
	m_engaged = true;
}

void RmlUiIme::release()
{
	if (!m_engaged)
		return;
	m_engaged = false;

	if (m_hasSystemCaret)
	{
		::DestroyCaret();
		m_hasSystemCaret = false;
		m_caretHeight = 0;
	}
	if (m_hwnd && ::IsWindow(m_hwnd))
		::ImmAssociateContext(m_hwnd, m_prevContext);
	m_prevContext = nullptr;
}

//-------------------------------------------------------------------------------------------------
void RmlUiIme::setCaret(Rml::Vector2f position, float lineHeight)
{
	m_caret = position;
	m_lineHeight = lineHeight;
	m_haveCaret = true;
	if (m_input)
		applyCaret();
}

// Tells Windows where the text is being entered: the composition and candidate windows of an IME,
// and the emoji panel, which finds the caret through the window's caret. The RmlUi caret is in
// context pixels; the client area may be a different size.
void RmlUiIme::applyCaret()
{
	if (!m_engaged || !m_input || !m_hwnd)
		return;

	Rml::Vector2f pos = m_caret;
	float height = m_lineHeight;
	if (!m_haveCaret)
	{
		Rml::Rectanglef box;
		if (!m_input->GetBoundingBox(box))
			return;
		pos = box.TopLeft();
		height = box.Height();
	}

	RECT client;
	::GetClientRect(m_hwnd, &client);
	const Rml::Vector2i dims = m_context ? m_context->GetDimensions() : Rml::Vector2i(0, 0);
	if (dims.x > 0 && dims.y > 0)
	{
		pos.x *= (float)client.right / (float)dims.x;
		pos.y *= (float)client.bottom / (float)dims.y;
		height *= (float)client.bottom / (float)dims.y;
	}

	const LONG x = (LONG)pos.x;
	const LONG y = (LONG)pos.y;
	const LONG h = (LONG)height + 2;

	if (HIMC himc = ::ImmGetContext(m_hwnd))
	{
		COMPOSITIONFORM comp = {};
		comp.dwStyle = CFS_FORCE_POSITION;
		comp.ptCurrentPos.x = x;
		comp.ptCurrentPos.y = y;
		::ImmSetCompositionWindow(himc, &comp);

		CANDIDATEFORM cand = {};
		cand.dwStyle = CFS_EXCLUDE;
		cand.ptCurrentPos.x = x;
		cand.ptCurrentPos.y = y;
		cand.rcArea.left = x;
		cand.rcArea.top = y;
		cand.rcArea.right = x + 1;
		cand.rcArea.bottom = y + h;
		::ImmSetCandidateWindow(himc, &cand);
		::ImmReleaseContext(m_hwnd, himc);
	}

	// A caret that is never shown, so nothing is drawn over the game; Windows still reports its
	// rectangle to whoever asks (the emoji panel).
	if (m_caretHeight != h)
	{
		if (m_hasSystemCaret)
			::DestroyCaret();
		m_hasSystemCaret = ::CreateCaret(m_hwnd, nullptr, 1, h) != FALSE;
		m_caretHeight = h;
	}
	if (m_hasSystemCaret)
		::SetCaretPos(x, y);
}

//-------------------------------------------------------------------------------------------------
bool RmlUiIme::filterKey(Rml::Input::KeyIdentifier key, bool down)
{
	if (!m_input)
		return false;

	std::set<int>::iterator it = m_imeKeys.find((int)key);
	if (it != m_imeKeys.end())
	{
		if (!down)
			m_imeKeys.erase(it);
		return true;
	}
	return m_composing && isEditingKey(key);
}

void RmlUiIme::completeComposition()
{
	if (!m_composing || !m_hwnd)
		return;
	if (HIMC himc = ::ImmGetContext(m_hwnd))
	{
		::ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
		::ImmReleaseContext(m_hwnd, himc);
	}
}

//-------------------------------------------------------------------------------------------------
bool RmlUiIme::handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
	if (!m_input)
		return false;

	result = 0;
	switch (message)
	{
		case WM_KEYDOWN:
		{
			if (wParam == VK_PROCESSKEY)
			{
				const int key = imeKeyFromVirtualKey((int)::ImmGetVirtualKey(hwnd));
				if (key != Rml::Input::KI_UNKNOWN)
				{
					m_imeKeys.insert(key);
					if (key == Rml::Input::KI_RETURN)
						m_imeKeys.insert(Rml::Input::KI_NUMPADENTER);
				}
			}
			return false;
		}

		case WM_IME_SETCONTEXT:
			// Inline composition: keep Windows from showing its own composition window.
			result = ::DefWindowProcW(hwnd, message, wParam, lParam & ~(LPARAM)ISC_SHOWUICOMPOSITIONWINDOW);
			return true;

		case WM_IME_STARTCOMPOSITION:
			startComposition();
			return true;

		case WM_IME_ENDCOMPOSITION:
			if (m_composing)
				confirmComposition(Rml::String());
			return true;

		case WM_IME_COMPOSITION:
		{
			HIMC himc = ::ImmGetContext(hwnd);
			if (!himc)
				return true;

			// Not every IME starts a composition.
			if (!m_composing)
				startComposition();

			if (lParam & GCS_CURSORPOS)
			{
				// A UTF-16 offset; RmlUi counts UTF-8 characters.
				const int units = ::ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
				const std::wstring text = getCompositionString(himc, false);
				const Rml::String head = toUtf8(text.substr(0, (size_t)(units < 0 ? 0 : units)));
				m_cursorPos = (int)Rml::StringUtilities::LengthUTF8(Rml::StringView(head));
				updateCursorPosition();
			}
			if (lParam & CS_NOMOVECARET)
				m_cursorPos = -1; // moved by a later part of the same message

			if (lParam & GCS_RESULTSTR)
			{
				confirmComposition(toUtf8(getCompositionString(himc, true)));
				// A result and the next composition can share one message.
				if (lParam & GCS_COMPSTR)
					startComposition();
			}
			if (lParam & GCS_COMPSTR)
				setComposition(toUtf8(getCompositionString(himc, false)));

			// The composition was cancelled.
			if (!lParam && m_composing)
				cancelComposition();

			::ImmReleaseContext(hwnd, himc);
			return true;
		}

		// The composition is ours; the system would add it to the text a second time as WM_CHAR.
		case WM_IME_CHAR:
		case WM_IME_REQUEST:
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void RmlUiIme::startComposition()
{
	m_composing = true;
	m_cursorPos = -1;
	m_rangeStart = m_rangeEnd = 0;
}

void RmlUiIme::endComposition()
{
	if (m_input)
		m_input->SetCompositionRange(0, 0);
	m_composing = false;
	m_rangeStart = m_rangeEnd = 0;
}

void RmlUiIme::cancelComposition()
{
	if (m_input)
	{
		m_input->SetText(Rml::StringView(), m_rangeStart, m_rangeEnd);
		m_input->SetCursorPosition(m_rangeStart);
	}
	endComposition();
}

void RmlUiIme::setComposition(const Rml::String &text)
{
	setCompositionString(text);
	updateCursorPosition();

	// Editors that work on a single character (Hangul) have no cursor and show it as a selection.
	if (m_cursorPos != -1 && m_input)
		m_input->SetCompositionRange(m_rangeStart, m_rangeEnd);
}

void RmlUiIme::confirmComposition(const Rml::String &text)
{
	setCompositionString(text);
	if (m_input)
	{
		m_input->SetCompositionRange(m_rangeStart, m_rangeEnd);
		m_input->CommitComposition(Rml::StringView(text));
	}
	m_cursorPos = m_rangeEnd - m_rangeStart;
	updateCursorPosition();
	endComposition();
}

void RmlUiIme::setCompositionString(const Rml::String &text)
{
	if (!m_input)
		return;
	if (m_rangeStart == 0 && m_rangeEnd == 0)
		m_input->GetSelectionRange(m_rangeStart, m_rangeEnd);
	m_input->SetText(Rml::StringView(text), m_rangeStart, m_rangeEnd);
	m_rangeEnd = m_rangeStart + (int)Rml::StringUtilities::LengthUTF8(Rml::StringView(text));
}

void RmlUiIme::updateCursorPosition()
{
	if (!m_input || (m_rangeStart == 0 && m_rangeEnd == 0))
		return;
	if (m_cursorPos != -1)
		m_input->SetCursorPosition(m_rangeStart + m_cursorPos);
	else
		m_input->SetSelectionRange(m_rangeStart, m_rangeEnd);
}
