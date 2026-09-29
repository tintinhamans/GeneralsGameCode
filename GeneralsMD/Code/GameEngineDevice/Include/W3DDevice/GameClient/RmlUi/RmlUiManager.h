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

// FILE: RmlUiManager.h ///////////////////////////////////////////////////////
// Owns the single RmlUi context hosted on top of the shell: DX8 rendering, file
// I/O through TheFileSystem, fonts, and input routing (RmlUiInputHook). This is
// the phase 1 foundation only -- no document is loaded or shown yet; that is
// wired up per-screen in phase 2, gated on TheGlobalData->m_useLegacyMenus.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/RmlUiInputHook.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiFileInterface.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiRenderInterface.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiSystemInterface.h"

class RmlScreen;

namespace Rml { class Context; class Element; class ElementInstancer; }

//-------------------------------------------------------------------------------------------------
class RmlUiManager : public RmlUiInputHook
{
public:
	static void createInstance();
	static void destroyInstance();
	static RmlUiManager *getInstance() { return s_instance; }

	RmlUiManager();
	virtual ~RmlUiManager();

	void init(int width, int height);     ///< call once the D3D device and window exist
	void shutdown();
	void onResize(int width, int height);
	void onDeviceLost();                  ///< before Reset(); see .cpp for why this is nearly a no-op
	void onDeviceReset();

	void update();                        ///< advance the context (layout, events, animations)
	void render();                        ///< draw the context on top of everything else this frame

	Rml::Context *getContext() const { return m_context; }

	// Screen router: at most one RmlScreen is active. showScreen() hides whatever was showing,
	// lazily loads the new screen's document on first use, then shows it. Later Shell::push/pop
	// call sites route through here the same way Options does (see report).
	void showScreen(RmlScreen *screen);
	void hideCurrentScreen();
	RmlScreen *getCurrentScreen() const { return m_currentScreen; }

	bool hasVisibleDocument() const; ///< true if any document is shown (gates rendering during load screens)

	// RmlUiInputHook ---------------------------------------------------------------------------
	virtual bool wantsMouseInput(int mouseX, int mouseY) const override;
	virtual bool wantsKeyboardInput() const override;
	virtual void processMouseMove(int x, int y) override;
	virtual void processMouseButton(int button, bool down) override;
	virtual void processMouseWheel(float delta) override;
	virtual bool processKey(unsigned char engineKey, unsigned char engineKeyState) override;
	virtual void processTextInput(unsigned short utf16Char) override;

private:
	static RmlUiManager *s_instance;

	bool ownsInput() const;               ///< RmlUiScreenRegistry::ownsInput()
	bool anyVisibleDocumentAt(int x, int y) const;
	void registerCustomElements();

	// Drives TheMouse's engine tooltip from whichever RmlUi element is hovered, so tooltips reuse
	// the .wnd rendering/delay exactly (see report). Walks up from GetHoverElement() for a
	// data-tooltip (GUI:/CSF key) or data-tooltip-text (literal/bound text) attribute.
	void updateTooltip();

	RmlUiSystemInterface m_systemInterface;
	RmlUiFileInterface m_fileInterface;
	RmlUiRenderInterface m_renderInterface;
	Rml::Context *m_context = nullptr;
	Rml::ElementInstancer *m_gameTextInstancer = nullptr;
	Rml::ElementInstancer *m_mappedImageInstancer = nullptr;
	Rml::ElementInstancer *m_mapPreviewInstancer = nullptr;
	Rml::ElementInstancer *m_scrollLogInstancer = nullptr;
	RmlScreen *m_currentScreen = nullptr;
	Rml::Element *m_tooltipElement = nullptr; // element updateTooltip() last drove TheMouse's tooltip from
	int m_width = 0, m_height = 0;
	bool m_initialized = false;
	bool m_debuggerInitialized = false;
};

extern RmlUiManager *TheRmlUiManager;
