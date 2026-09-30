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

// IME / TSF support for RmlUi text fields. While a field has focus the window gets its default
// input context back (the legacy IMEManager keeps a private one that the Windows emoji panel and
// IMEs can neither insert through nor locate a caret with), the composition and candidate windows
// follow the caret, and WM_IME_* is handled here instead of by IMEManager. Adapted from RmlUi's
// Win32 backend (TextInputMethodEditor_Win32).

#pragma once

#include <windows.h>
#include <set>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; }

//-------------------------------------------------------------------------------------------------
class RmlUiIme : public Rml::TextInputHandler
{
public:
	void setContext(Rml::Context *context) { m_context = context; }

	// Rml::TextInputHandler
	virtual void OnActivate(Rml::TextInputContext *input) override;
	virtual void OnDeactivate(Rml::TextInputContext *input) override;
	virtual void OnDestroy(Rml::TextInputContext *input) override;

	bool isActive() const { return m_input != nullptr; }
	bool isComposing() const { return m_composing; }

	// The caret in RmlUi context pixels, from SystemInterface::ActivateKeyboard().
	void setCaret(Rml::Vector2f position, float lineHeight);

	// TRUE if consumed (result is then the window proc's return value). Only acts while a text
	// field is active; anything else is left to the legacy IMEManager.
	bool handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);

	// TRUE if the key belongs to the IME and RmlUi must not see it. DirectInput reports the
	// physical key (Enter that confirms a composition) alongside the IME's own handling of it.
	bool filterKey(Rml::Input::KeyIdentifier key, bool down);

	void completeComposition(); ///< commit an open composition, e.g. before a mouse click
	void deactivate();          ///< the focused field went away without a blur (hidden document)
	void update();              ///< per frame: give the engine its IME state back once no field is active
	void shutdown();

private:
	void engage(HWND hwnd);
	void release();
	void applyCaret();
	void startComposition();
	void endComposition();
	void cancelComposition();
	void setComposition(const Rml::String &text);
	void confirmComposition(const Rml::String &text);
	void setCompositionString(const Rml::String &text);
	void updateCursorPosition();

	Rml::Context *m_context = nullptr;
	Rml::TextInputContext *m_input = nullptr;

	// Engine side, restored by release()
	HWND m_hwnd = nullptr;
	HIMC m_prevContext = nullptr;
	bool m_engaged = false;

	Rml::Vector2f m_caret = {};
	float m_lineHeight = 0.f;
	bool m_haveCaret = false;
	int m_caretHeight = 0;
	bool m_hasSystemCaret = false;

	bool m_composing = false;
	int m_cursorPos = -1;
	int m_rangeStart = 0;
	int m_rangeEnd = 0;

	std::set<int> m_imeKeys; ///< keys the IME took (VK_PROCESSKEY) that DirectInput has not released yet
};
