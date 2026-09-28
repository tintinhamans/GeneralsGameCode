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

#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "W3DDevice/GameClient/RmlUi/RmlCreditsScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanGameSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanLobbyScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanMapSelectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlMainMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlMessageBox.h"
#include "W3DDevice/GameClient/RmlUi/RmlEndGameOverlayScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlNetworkDirectConnectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineLoginScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineWelcomeScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOptionsScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlQuitMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlScoreScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlSkirmishMapSelectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlSkirmishSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiElements.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Debugger.h>

#include <windows.h>

// Same private per-file helper every RmlScreen keeps (see e.g. RmlLanLobbyScreen.cpp) -- converts a
// data-bound data-tooltip-text value (RmlUi strings are UTF-8) back to the UnicodeString TheMouse's
// tooltip API wants. ASCII-only limitation noted there applies here too.
static UnicodeString utf8ToUnicode(const Rml::String &utf8)
{
	AsciiString ascii(utf8.c_str());
	UnicodeString text;
	text.translate(ascii);
	return text;
}

RmlUiManager *RmlUiManager::s_instance = nullptr;
RmlUiManager *TheRmlUiManager = nullptr;

void RmlUiManager::createInstance()
{
	if (!s_instance)
	{
		s_instance = new RmlUiManager();
		TheRmlUiManager = s_instance;
	}
}

void RmlUiManager::destroyInstance()
{
	if (s_instance)
	{
		s_instance->shutdown();
		delete s_instance;
		s_instance = nullptr;
		TheRmlUiManager = nullptr;
	}
}

RmlUiManager::RmlUiManager()
{
}

RmlUiManager::~RmlUiManager()
{
	shutdown();
}

void RmlUiManager::init(int width, int height)
{
	if (m_initialized)
		return;

	m_width = width;
	m_height = height;

	Rml::SetSystemInterface(&m_systemInterface);
	Rml::SetFileInterface(&m_fileInterface);
	Rml::SetRenderInterface(&m_renderInterface);

	if (!Rml::Initialise())
		return;

	m_renderInterface.onDeviceCreated();

	// SIL OFL-licensed Barlow (see Data/UI/Fonts/OFL.txt), the bundled default UI font.
	// LoadFontFace reads family/style/weight straight from the font, so both weights
	// register under the "Barlow" family; common.rcss picks weight via font-weight.
	Rml::LoadFontFace("UI/Fonts/Barlow-Regular.ttf", true);
	Rml::LoadFontFace("UI/Fonts/Barlow-Bold.ttf");

	// Arial, if present in the Windows fonts folder, is loaded only as a fallback face (not
	// bundled) so glyphs Barlow lacks -- other scripts in player names/translations -- still
	// render. Missing files are not an error: Barlow alone remains fully usable.
	char winDir[MAX_PATH] = {};
	if (::GetEnvironmentVariableA("WINDIR", winDir, MAX_PATH) > 0)
	{
		Rml::String regularPath = Rml::String(winDir) + "\\Fonts\\arial.ttf";
		Rml::String boldPath = Rml::String(winDir) + "\\Fonts\\arialbd.ttf";
		bool regularOk = Rml::LoadFontFace(regularPath, true);
		bool boldOk = Rml::LoadFontFace(boldPath, true);
		if (!regularOk && !boldOk)
			Rml::Log::Message(Rml::Log::LT_INFO, "Arial not found under %%WINDIR%%\\Fonts; falling back to Barlow only.");
	}

	m_context = Rml::CreateContext("main", Rml::Vector2i(width, height));

	// dp units scale off a 1080p baseline so common.rcss's spacing/sizes stay consistent
	// across resolutions and aspect ratios (see report for the palette this backs).
	if (m_context)
		m_context->SetDensityIndependentPixelRatio(height / 1080.0f);

	registerCustomElements();

	TheRmlUiInputHook = this;

	m_initialized = true;

	if (TheGlobalData && TheGlobalData->m_useLegacyMenus)
	{
		Rml::Log::Message(Rml::Log::LT_INFO, "-wnd given: legacy .wnd menus are active, RmlUi will show no documents.");
	}

	if (TheGlobalData && TheGlobalData->m_rmlDebugger && m_context)
	{
		m_debuggerInitialized = Rml::Debugger::Initialise(m_context);
		if (m_debuggerInitialized)
			Rml::Debugger::SetVisible(true);
	}

	// Shell::push/pop and the ad-hoc call sites (MainMenu.cpp/QuitMenu.cpp options button) look
	// screens up in the registry by .wnd path, gated on !m_useLegacyMenus; see RmlUiScreenRegistry.h.
	RmlUiScreenRegistry::registerScreen("Menus/OptionsMenu.wnd", &OpenRmlOptionsScreen, &CloseRmlOptionsScreen);
	RmlUiScreenRegistry::registerScreen("Menus/MainMenu.wnd", &OpenRmlMainMenuScreen, &CloseRmlMainMenuScreen);

	// MainMenuActions::startSkirmishOptions() pushes this same path; see RmlSkirmishSetupScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/SkirmishGameOptionsMenu.wnd", &OpenRmlSkirmishSetupScreen, &CloseRmlSkirmishSetupScreen);
	RmlUiScreenRegistry::registerScreen("Menus/SkirmishMapSelectMenu.wnd", &OpenRmlSkirmishMapSelectScreen, &CloseRmlSkirmishMapSelectScreen);

	// MainMenuActions pushes this same path; see RmlLanLobbyScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/LanLobbyMenu.wnd", &OpenRmlLanLobbyScreen, &CloseRmlLanLobbyScreen);

	// RmlLanLobbyScreen::onDirectConnect() pushes this same path; see RmlNetworkDirectConnectScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/NetworkDirectConnect.wnd", &OpenRmlNetworkDirectConnectScreen, &CloseRmlNetworkDirectConnectScreen);

	// startOnline() (MainMenuUtils.cpp) pushes one or the other depending on
	// ALLOW_NON_PROFILED_LOGIN/GameSpyUseProfiles; both behave identically under GENERALS_ONLINE
	// (see RmlOnlineLoginScreen.h), so one screen serves both paths.
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyLoginProfile.wnd", &OpenRmlOnlineLoginScreen, &CloseRmlOnlineLoginScreen);
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyLoginQuick.wnd", &OpenRmlOnlineLoginScreen, &CloseRmlOnlineLoginScreen);

	// RmlOnlineLoginScreen::onLoginSucceeded() pushes this path; see RmlOnlineWelcomeScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/WOLWelcomeMenu.wnd", &OpenRmlOnlineWelcomeScreen, &CloseRmlOnlineWelcomeScreen);

	// LANAPI::OnGameJoin() pushes this same path once LanLobbyActions::hostGame()/joinGame()'s
	// RequestGameCreate()/RequestGameJoin() succeeds; see RmlLanGameSetupScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/LanGameOptionsMenu.wnd", &OpenRmlLanGameSetupScreen, &CloseRmlLanGameSetupScreen);
	RmlUiScreenRegistry::registerScreen("Menus/LanMapSelectMenu.wnd", &OpenRmlLanMapSelectScreen, &CloseRmlLanMapSelectScreen);
	RmlUiScreenRegistry::registerScreen("Menus/CreditsMenu.wnd", &OpenRmlCreditsScreen, &CloseRmlCreditsScreen);
	RmlUiScreenRegistry::registerScreen("Menus/QuitMenu.wnd", &OpenRmlQuitMenuScreen, &CloseRmlQuitMenuScreen);
	RmlUiScreenRegistry::registerScreen("Menus/QuitNoSave.wnd", &OpenRmlQuitNoSaveScreen, &CloseRmlQuitNoSaveScreen);

	// GameLogicDispatch pushes this after a match/campaign mission ends; see RmlScoreScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/ScoreScreen.wnd", &OpenRmlScoreScreen, &CloseRmlScoreScreen);

	// ScriptActions::doVictory()/doDefeat()/doLocalDefeat() create these via winCreateFromScript();
	// see GameWindowManagerScript.cpp's winCreateFromScript() and RmlEndGameOverlayScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/Victorious.wnd", &OpenRmlVictoriousScreen, &CloseRmlVictoriousScreen);
	RmlUiScreenRegistry::registerScreen("Menus/Defeat.wnd", &OpenRmlDefeatScreen, &CloseRmlDefeatScreen);
	RmlUiScreenRegistry::registerScreen("Menus/LocalDefeat.wnd", &OpenRmlLocalDefeatScreen, &CloseRmlLocalDefeatScreen);
	RmlUiScreenRegistry::registerScreen("Menus/ObserverQuit.wnd", &OpenRmlObserverQuitScreen, &CloseRmlObserverQuitScreen);

	// GameWindowManager::gogoMessageBox() looks this hook up the same way, gated on
	// !m_useLegacyMenus; see RmlUiMessageBoxHook.h.
	RegisterRmlMessageBoxHook(m_context);
}

void RmlUiManager::registerCustomElements()
{
	m_gameTextInstancer = new Rml::ElementInstancerGeneric<RmlGameTextElement>();
	Rml::Factory::RegisterElementInstancer("gametext", m_gameTextInstancer);

	m_mappedImageInstancer = new Rml::ElementInstancerGeneric<RmlMappedImageElement>();
	Rml::Factory::RegisterElementInstancer("mappedimage", m_mappedImageInstancer);

	m_mapPreviewInstancer = new Rml::ElementInstancerGeneric<RmlMapPreviewElement>();
	Rml::Factory::RegisterElementInstancer("mappreview", m_mapPreviewInstancer);
}

void RmlUiManager::shutdown()
{
	if (!m_initialized)
		return;

	if (TheRmlUiInputHook == this)
		TheRmlUiInputHook = nullptr;

	UnregisterRmlMessageBoxHook();
	RmlUiScreenRegistry::unregisterAll();

	if (m_context)
	{
		Rml::RemoveContext(m_context->GetName());
		m_context = nullptr;
	}

	Rml::Shutdown();

	// Rml::Shutdown() does not take ownership of instancers registered via Factory; free them now.
	delete m_gameTextInstancer; m_gameTextInstancer = nullptr;
	delete m_mappedImageInstancer; m_mappedImageInstancer = nullptr;
	delete m_mapPreviewInstancer; m_mapPreviewInstancer = nullptr;
	m_currentScreen = nullptr;
	m_debuggerInitialized = false;

	m_renderInterface.onDeviceLost();

	m_initialized = false;
}

void RmlUiManager::onResize(int width, int height)
{
	m_width = width;
	m_height = height;
	if (m_context)
	{
		m_context->SetDimensions(Rml::Vector2i(width, height));
		m_context->SetDensityIndependentPixelRatio(height / 1080.0f);
	}
}

void RmlUiManager::onDeviceLost()
{
	// All RmlUi-owned D3D resources use D3DPOOL_MANAGED (see RmlUiRenderInterface), which the
	// driver evicts/restores around Reset() automatically, so there is nothing to release here
	// for the common resize/mode-change reset path. Kept for symmetry and for full shutdown.
	m_renderInterface.onDeviceLost();
}

void RmlUiManager::onDeviceReset()
{
	m_renderInterface.onDeviceCreated();
}

void RmlUiManager::update()
{
	if (m_currentScreen)
		m_currentScreen->update();
	if (m_context)
		m_context->Update();
	updateTooltip();
}

//-------------------------------------------------------------------------------------------------
// Reuses the engine's own tooltip rendering (TheMouse->setCursorTooltip(), same call
// GameWindowManager.cpp makes for a .wnd control's TOOLTIPTEXT) so RmlUi tooltips match the .wnd
// look/delay exactly instead of RmlUi drawing its own. Called once per frame from update(), which
// runs from W3DDisplay::draw() -- after GameWindowManager's own per-frame tooltip clear/set (driven
// off TheMouse->createStreamMessages(), which runs earlier in the frame during message processing),
// so this always gets the last word for whatever is under the RmlUi hover this frame.
void RmlUiManager::updateTooltip()
{
	Rml::Element *hover = m_context ? m_context->GetHoverElement() : nullptr;

	// RmlUi's hover target is usually the innermost element (e.g. text inside a button), so walk up
	// to the nearest ancestor carrying either attribute -- same idea as GameWindowManager's
	// toolTipWindow search up the .wnd tree.
	Rml::Element *tooltipElement = nullptr;
	Rml::String tooltipKey, tooltipText;
	for (Rml::Element *e = hover; e; e = e->GetParentNode())
	{
		if (e->HasAttribute("data-tooltip"))
		{
			tooltipElement = e;
			tooltipKey = e->GetAttribute<Rml::String>("data-tooltip", "");
			break;
		}
		if (e->HasAttribute("data-tooltip-text"))
		{
			tooltipElement = e;
			tooltipText = e->GetAttribute<Rml::String>("data-tooltip-text", "");
			break;
		}
	}

	if (!tooltipElement)
	{
		// Only clear if we were the one showing it -- a bare setCursorTooltip(empty) every frame
		// would fight GameWindowManager's own clear/set for whatever legacy window is under the
		// cursor once RmlUi's context reports no hover at all (e.g. -wnd legacy menus active).
		if (m_tooltipElement)
		{
			TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
			m_tooltipElement = nullptr;
		}
		return;
	}

	m_tooltipElement = tooltipElement;

	UnicodeString text;
	if (!tooltipKey.empty() && TheGameText)
		text = TheGameText->fetch(AsciiString(tooltipKey.c_str()));
	else if (!tooltipText.empty())
		text = utf8ToUnicode(tooltipText);

	if (TheMouse)
		TheMouse->setCursorTooltip(text); // delay defaults to -1: same per-window default the .wnd path uses
}

void RmlUiManager::render()
{
	if (!m_context)
		return;
	m_renderInterface.beginFrame(m_width, m_height);
	m_context->Render();
	m_renderInterface.endFrame();
}

//-------------------------------------------------------------------------------------------------
// No document is loaded in phase 1 (see init()), so these scan whatever the context happens
// to have open -- always nothing for now -- and stay ready for phase 2's shell screens.
bool RmlUiManager::anyVisibleDocumentAt(int x, int y, bool *outModal) const
{
	if (outModal) *outModal = false;
	if (!m_context)
		return false;

	for (int i = 0; i < m_context->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument *doc = m_context->GetDocument(i);
		if (!doc || !doc->IsVisible())
			continue;

		if (doc->IsModal())
		{
			if (outModal) *outModal = true;
			return true;
		}

		float left = doc->GetAbsoluteLeft();
		float top = doc->GetAbsoluteTop();
		if (x >= left && y >= top && x <= left + doc->GetOffsetWidth() && y <= top + doc->GetOffsetHeight())
			return true;
	}
	return false;
}

bool RmlUiManager::wantsMouseInput(int mouseX, int mouseY) const
{
	return anyVisibleDocumentAt(mouseX, mouseY, nullptr);
}

bool RmlUiManager::wantsKeyboardInput() const
{
	bool modal = false;
	anyVisibleDocumentAt(0, 0, &modal);
	return modal;
}

static int computeKeyModifiers()
{
	int mods = 0;
	if (::GetKeyState(VK_SHIFT) & 0x8000) mods |= Rml::Input::KM_SHIFT;
	if (::GetKeyState(VK_CONTROL) & 0x8000) mods |= Rml::Input::KM_CTRL;
	if (::GetKeyState(VK_MENU) & 0x8000) mods |= Rml::Input::KM_ALT;
	if (::GetKeyState(VK_CAPITAL) & 1) mods |= Rml::Input::KM_CAPSLOCK;
	return mods;
}

void RmlUiManager::processMouseMove(int x, int y)
{
	if (m_context)
		m_context->ProcessMouseMove(x, y, computeKeyModifiers());
}

void RmlUiManager::processMouseButton(int button, bool down)
{
	if (!m_context)
		return;
	if (down)
		m_context->ProcessMouseButtonDown(button, computeKeyModifiers());
	else
		m_context->ProcessMouseButtonUp(button, computeKeyModifiers());
}

void RmlUiManager::processMouseWheel(float delta)
{
	if (m_context)
		m_context->ProcessMouseWheel(-delta, computeKeyModifiers()); // engine reports +up; RmlUi expects +down
}

// Engine key codes are DirectInput DIK_* scan codes (see KeyDefs.h). Map the ones needed for
// UI navigation/close to RmlUi's own KeyIdentifier enum. Unmapped keys are dropped (KI_UNKNOWN);
// full text entry (WM_CHAR-based Unicode) is a phase 2 item, see report.
static Rml::Input::KeyIdentifier engineKeyToRmlKey(unsigned char key)
{
	using namespace Rml::Input;
	switch (key)
	{
		case KEY_ESC: return KI_ESCAPE;
		case KEY_TAB: return KI_TAB;
		case KEY_ENTER: return KI_RETURN;
		case KEY_SPACE: return KI_SPACE;
		case KEY_BACKSPACE: return KI_BACK;
		case KEY_UP: return KI_UP;
		case KEY_DOWN: return KI_DOWN;
		case KEY_LEFT: return KI_LEFT;
		case KEY_RIGHT: return KI_RIGHT;
		case KEY_HOME: return KI_HOME;
		case KEY_END: return KI_END;
		case KEY_PGUP: return KI_PRIOR;
		case KEY_PGDN: return KI_NEXT;
		case KEY_INS: return KI_INSERT;
		case KEY_DEL: return KI_DELETE;
		case KEY_A: return KI_A; case KEY_B: return KI_B; case KEY_C: return KI_C;
		case KEY_D: return KI_D; case KEY_E: return KI_E; case KEY_F: return KI_F;
		case KEY_G: return KI_G; case KEY_H: return KI_H; case KEY_I: return KI_I;
		case KEY_J: return KI_J; case KEY_K: return KI_K; case KEY_L: return KI_L;
		case KEY_M: return KI_M; case KEY_N: return KI_N; case KEY_O: return KI_O;
		case KEY_P: return KI_P; case KEY_Q: return KI_Q; case KEY_R: return KI_R;
		case KEY_S: return KI_S; case KEY_T: return KI_T; case KEY_U: return KI_U;
		case KEY_V: return KI_V; case KEY_W: return KI_W; case KEY_X: return KI_X;
		case KEY_Y: return KI_Y; case KEY_Z: return KI_Z;
		case KEY_0: return KI_0; case KEY_1: return KI_1; case KEY_2: return KI_2;
		case KEY_3: return KI_3; case KEY_4: return KI_4; case KEY_5: return KI_5;
		case KEY_6: return KI_6; case KEY_7: return KI_7; case KEY_8: return KI_8;
		case KEY_9: return KI_9;
		case KEY_LSHIFT: case KEY_RSHIFT: return KI_LSHIFT;
		case KEY_LCTRL: case KEY_RCTRL: return KI_LCONTROL;
		case KEY_LALT: case KEY_RALT: return KI_LMENU;
		default: return KI_UNKNOWN;
	}
}

void RmlUiManager::processKey(unsigned char engineKey, unsigned char engineKeyState)
{
	if (!m_context)
		return;

	const bool isDown = BitIsSet(engineKeyState, KEY_STATE_DOWN);
	Rml::Input::KeyIdentifier rmlKey = engineKeyToRmlKey(engineKey);

	// Escape closes the active screen the same way the .wnd Cancel/Back button would,
	// instead of reaching the context (which has no document-level close behavior of its own).
	// A message box stacked on top must swallow this instead: the .wnd MessageBoxSystem/
	// QuitMessageBoxSystem never handle Escape (no button, no dismissal), so it must not fall
	// through to cancel whatever the box is stacked over either.
	if (rmlKey == Rml::Input::KI_ESCAPE && isDown && !AnyRmlMessageBoxOpen() && m_currentScreen && m_currentScreen->isVisible())
	{
		m_currentScreen->onBack();
		return;
	}

	if (rmlKey == Rml::Input::KI_UNKNOWN)
		return;

	int mods = computeKeyModifiers();
	if (isDown)
		m_context->ProcessKeyDown(rmlKey, mods);
	else
		m_context->ProcessKeyUp(rmlKey, mods);
}

void RmlUiManager::processTextInput(unsigned short utf16Char)
{
	if (m_context)
		m_context->ProcessTextInput(Rml::Character(utf16Char));
}

void RmlUiManager::showScreen(RmlScreen *screen)
{
	if (!screen || !m_context)
		return;

	// TheSuperHackers @fix Set m_currentScreen before hiding the previous one, not after: a
	// screen's hide() can itself call back into showScreen() for the same screen (e.g. Options
	// hiding restores the main menu via RmlUiScreenRegistry, which is exactly this call re-entered
	// while the main menu's own showScreen() call is still hiding Options). With the old screen
	// already installed as current, that reentrant call sees previous == screen and skips hiding
	// it again instead of recursing forever.
	RmlScreen *previous = m_currentScreen;
	m_previousScreen = previous;
	m_currentScreen = screen;

	if (previous && previous != screen)
		previous->hide();

	screen->load(m_context); // no-op if already loaded
	screen->show();
}

void RmlUiManager::hideCurrentScreen()
{
	if (m_currentScreen)
	{
		m_currentScreen->hide();
		m_currentScreen = nullptr;
	}
}
