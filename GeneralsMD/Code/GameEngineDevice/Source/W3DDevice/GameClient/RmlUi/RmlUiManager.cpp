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
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlaySession.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/TransitionSounds.h"
#include "W3DDevice/GameClient/RmlUi/RmlBuddyOverlayScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlBuddyToastScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlCreditsScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanGameSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanLobbyScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLanMapSelectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlLoadScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlMainMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlMessageBox.h"
#include "W3DDevice/GameClient/RmlUi/RmlEndGameOverlayScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlHostGameScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlJoinGameScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlNetworkDirectConnectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineGameSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineLobbyScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineLoginScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineMapSelectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOnlineWelcomeScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlOptionsScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlPlayerInfoScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlPopupReplayScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlChallengeMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlDisconnectScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlDownloadScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlInGamePopupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlQuickMatchScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlQuitMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlReplayMenuScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlSaveLoadScreen.h"
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
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Debugger.h>

#include <vector>
#include <windows.h>

// Converts a data-bound data-tooltip-text value (RmlUi strings are UTF-8) back to the UnicodeString
// TheMouse's tooltip API wants; tooltips can carry player names.
static UnicodeString utf8ToUnicode(const Rml::String &utf8)
{
	UnicodeString text;
	int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (len <= 1)
		return text;

	std::vector<wchar_t> wide((size_t)len);
	::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
	text.set((const WideChar *)&wide[0]);
	return text;
}

//-------------------------------------------------------------------------------------------------
// Plays the sounds the .wnd gadgets do (context-level, so no screen has to): GUIClick on a push
// button's mouse-down (GadgetPushButton.cpp; a data-sound attribute stands in for its altSound),
// GUIClickDisabled on pressing a disabled control (GameWindow.cpp), GUIClick when a combo is
// pressed (GadgetComboBox.cpp) and GUIComboBoxClick when one of its options is picked
// (GadgetListBox.cpp, which only the combo lists enable). Check boxes, radio buttons, sliders and
// tabs are silent in the original, so they are here too.
class RmlUiSoundListener : public Rml::EventListener
{
public:
	virtual void ProcessEvent(Rml::Event &event) override;
};

static RmlUiSoundListener s_soundListener;

static void playUiSound(const Rml::String &name)
{
	if (!TheAudio || name.empty())
		return;
	AudioEventRTS sound;
	sound.setEventName(AsciiString(name.c_str()));
	TheAudio->addAudioEvent(&sound);
}

static bool isButton(const Rml::Element *e)
{
	return e->GetTagName() == "button" || e->IsClassSet("button");
}

// Nearest element at or above e with the given tag name, or the nearest button for tag == nullptr.
static Rml::Element *findControl(Rml::Element *e, const char *tag)
{
	for (; e; e = e->GetParentNode())
	{
		if (tag ? e->GetTagName() == tag : isButton(e))
			return e;
	}
	return nullptr;
}

static bool isDisabledControl(const Rml::Element *e)
{
	return e->HasAttribute("disabled") || e->IsClassSet("disabled");
}

void RmlUiSoundListener::ProcessEvent(Rml::Event &event)
{
	Rml::Element *target = event.GetTargetElement();
	if (!target)
		return;

	if (event.GetId() == Rml::EventId::Mousedown)
	{
		if (event.GetParameter<int>("button", 0) != 0)
			return; // left button only, like GWM_LEFT_DOWN

		if (Rml::Element *button = findControl(target, nullptr))
		{
			if (isDisabledControl(button))
				playUiSound("GUIClickDisabled");
			else
				playUiSound(button->GetAttribute<Rml::String>("data-sound", "GUIClick"));
		}
		else if (Rml::Element *select = findControl(target, "select"))
		{
			// The open list is a child of the select; picking an option is the click event's job.
			if (!findControl(target, "selectbox"))
				playUiSound(isDisabledControl(select) ? "GUIClickDisabled" : "GUIClick");
		}
	}
	else if (event.GetId() == Rml::EventId::Click)
	{
		Rml::Element *button = findControl(target, nullptr);
		if (button && isDisabledControl(button))
		{
			// <button> has no native disabled state in RmlUi; keep its click handlers from running.
			event.StopImmediatePropagation();
			return;
		}

		Rml::Element *option = findControl(target, "option");
		if (option && findControl(option, "selectbox") && !isDisabledControl(option))
			playUiSound("GUIComboBoxClick");
	}
}

// What the registry asks to learn whether a screen is up: the RmlUi side's own visibility.
template<class T> static bool screenVisible() { return T::instance().isVisible(); }
// What Escape does for the topmost layer: an RmlScreen's onBack(), or a popup's back().
template<class T> static void screenBack() { T::instance().onBack(); }
template<class T> static void popupBack() { T::instance().back(); }
// The keys an overlay takes ahead of the game while it is up (see RmlUiScreenRegistry::overlayKey()).
template<class T> static bool screenKey(unsigned char key, unsigned char state) { return T::instance().onKey(key, state); }
template<RmlLoadScreen::Kind K> static bool loadScreenVisible() { return RmlLoadScreen::instance(K).isVisible(); }

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

	if (m_context)
	{
		m_context->AddEventListener("mousedown", &s_soundListener, true);
		m_context->AddEventListener("click", &s_soundListener, true);
	}

	TheRmlUiInputHook = this;

	m_initialized = true;

	if (RmlUiScreenRegistry::usesLegacyMenus())
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
	RmlUiScreenRegistry::registerScreen("Menus/OptionsMenu.wnd", &OpenRmlOptionsScreen, &CloseRmlOptionsScreen, &screenVisible<RmlOptionsScreen>, &screenBack<RmlOptionsScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/MainMenu.wnd", &OpenRmlMainMenuScreen, &CloseRmlMainMenuScreen, &screenVisible<RmlMainMenuScreen>, &screenBack<RmlMainMenuScreen>);

	// MainMenuActions::startSkirmishOptions() pushes this same path; see RmlSkirmishSetupScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/SkirmishGameOptionsMenu.wnd", &OpenRmlSkirmishSetupScreen, &CloseRmlSkirmishSetupScreen, &screenVisible<RmlSkirmishSetupScreen>, &screenBack<RmlSkirmishSetupScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/SkirmishMapSelectMenu.wnd", &OpenRmlSkirmishMapSelectScreen, &CloseRmlSkirmishMapSelectScreen, &screenVisible<RmlSkirmishMapSelectScreen>, &popupBack<RmlSkirmishMapSelectScreen>);

	// MainMenuActions pushes this same path; see RmlLanLobbyScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/LanLobbyMenu.wnd", &OpenRmlLanLobbyScreen, &CloseRmlLanLobbyScreen, &screenVisible<RmlLanLobbyScreen>, &screenBack<RmlLanLobbyScreen>);

	// RmlLanLobbyScreen::onDirectConnect() pushes this same path; see RmlNetworkDirectConnectScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/NetworkDirectConnect.wnd", &OpenRmlNetworkDirectConnectScreen, &CloseRmlNetworkDirectConnectScreen, &screenVisible<RmlNetworkDirectConnectScreen>, &screenBack<RmlNetworkDirectConnectScreen>);

	// startOnline() (MainMenuUtils.cpp) pushes one or the other depending on
	// ALLOW_NON_PROFILED_LOGIN/GameSpyUseProfiles; both behave identically under GENERALS_ONLINE
	// (see RmlOnlineLoginScreen.h), so one screen serves both paths.
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyLoginProfile.wnd", &OpenRmlOnlineLoginScreen, &CloseRmlOnlineLoginScreen, &screenVisible<RmlOnlineLoginScreen>, &screenBack<RmlOnlineLoginScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyLoginQuick.wnd", &OpenRmlOnlineLoginScreen, &CloseRmlOnlineLoginScreen, &screenVisible<RmlOnlineLoginScreen>, &screenBack<RmlOnlineLoginScreen>);

	// RmlOnlineLoginScreen::onLoginSucceeded() pushes this path; see RmlOnlineWelcomeScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/WOLWelcomeMenu.wnd", &OpenRmlOnlineWelcomeScreen, &CloseRmlOnlineWelcomeScreen, &screenVisible<RmlOnlineWelcomeScreen>, &screenBack<RmlOnlineWelcomeScreen>);

	// RmlOnlineWelcomeScreen's Quick Match button pushes this same path; see RmlQuickMatchScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/WOLQuickMatchMenu.wnd", &OpenRmlQuickMatchScreen, &CloseRmlQuickMatchScreen, &screenVisible<RmlQuickMatchScreen>, &screenBack<RmlQuickMatchScreen>);

	// OnlineWelcomeActions's Custom Match button pushes this same path; see RmlOnlineLobbyScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/WOLCustomLobby.wnd", &OpenRmlOnlineLobbyScreen, &CloseRmlOnlineLobbyScreen, &screenVisible<RmlOnlineLobbyScreen>, &screenBack<RmlOnlineLobbyScreen>);

	// LANAPI::OnGameJoin() pushes this same path once LanLobbyActions::hostGame()/joinGame()'s
	// RequestGameCreate()/RequestGameJoin() succeeds; see RmlLanGameSetupScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/LanGameOptionsMenu.wnd", &OpenRmlLanGameSetupScreen, &CloseRmlLanGameSetupScreen, &screenVisible<RmlLanGameSetupScreen>, &screenBack<RmlLanGameSetupScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/LanMapSelectMenu.wnd", &OpenRmlLanMapSelectScreen, &CloseRmlLanMapSelectScreen, &screenVisible<RmlLanMapSelectScreen>, &popupBack<RmlLanMapSelectScreen>);

	// RmlOnlineLobbyScreen pushes this path once a Generals Online lobby is created/joined; see
	// RmlOnlineGameSetupScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyGameOptionsMenu.wnd", &OpenRmlOnlineGameSetupScreen, &CloseRmlOnlineGameSetupScreen, &screenVisible<RmlOnlineGameSetupScreen>, &screenBack<RmlOnlineGameSetupScreen>);
	// RmlOnlineGameSetupScreen's Select Map button pushes this same path; see RmlOnlineMapSelectScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/WOLMapSelectMenu.wnd", &OpenRmlOnlineMapSelectScreen, &CloseRmlOnlineMapSelectScreen, &screenVisible<RmlOnlineMapSelectScreen>, &popupBack<RmlOnlineMapSelectScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/CreditsMenu.wnd", &OpenRmlCreditsScreen, &CloseRmlCreditsScreen, &screenVisible<RmlCreditsScreen>, &screenBack<RmlCreditsScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/QuitMenu.wnd", &OpenRmlQuitMenuScreen, &CloseRmlQuitMenuScreen, &screenVisible<RmlQuitMenuScreen>, &popupBack<RmlQuitMenuScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/QuitNoSave.wnd", &OpenRmlQuitNoSaveScreen, &CloseRmlQuitNoSaveScreen, &screenVisible<RmlQuitMenuScreen>, &popupBack<RmlQuitMenuScreen>);
	// MainMenuActions::openLoadGame() pushes SaveLoad.wnd, openQuitMenuSaveLoad() opens the popup; see RmlSaveLoadScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/SaveLoad.wnd", &OpenRmlSaveLoadScreen, &CloseRmlSaveLoadScreen, &screenVisible<RmlSaveLoadScreen>, &popupBack<RmlSaveLoadScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/PopupSaveLoad.wnd", &OpenRmlPopupSaveLoadScreen, &CloseRmlPopupSaveLoadScreen, &screenVisible<RmlSaveLoadScreen>, &popupBack<RmlSaveLoadScreen>);
	// MainMenuActions::openReplayMenu() pushes this path; see RmlReplayMenuScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/ReplayMenu.wnd", &OpenRmlReplayMenuScreen, &CloseRmlReplayMenuScreen, &screenVisible<RmlReplayMenuScreen>, &popupBack<RmlReplayMenuScreen>);
	// MainMenuActions::startCampaignAtDifficulty() pushes this path for Generals Challenge; see RmlChallengeMenuScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/ChallengeMenu.wnd", &OpenRmlChallengeMenuScreen, &CloseRmlChallengeMenuScreen, &screenVisible<RmlChallengeMenuScreen>, &popupBack<RmlChallengeMenuScreen>);
	// ScoreScreenActions::startSaveReplayFlow() opens this over the score screen; see RmlPopupReplayScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/PopupReplay.wnd", &OpenRmlPopupReplayScreen, &CloseRmlPopupReplayScreen, &screenVisible<RmlPopupReplayScreen>, &popupBack<RmlPopupReplayScreen>);
	// GSOVERLAY_PLAYERINFO's .wnd path (see GameSpyOverlay.cpp's gsOverlays[] / GameSpyOpenOverlay()).
	RmlUiScreenRegistry::registerScreen("Menus/PopupPlayerInfo.wnd", &OpenRmlPlayerInfoScreen, &CloseRmlPlayerInfoScreen, &screenVisible<RmlPlayerInfoScreen>, &popupBack<RmlPlayerInfoScreen>);
	// GSOVERLAY_BUDDY's .wnd path, same gsOverlays[] precedent.
	RmlUiScreenRegistry::registerScreen("Menus/WOLBuddyOverlay.wnd", &OpenRmlBuddyOverlayScreen, &CloseRmlBuddyOverlayScreen, &screenVisible<RmlBuddyOverlayScreen>, &popupBack<RmlBuddyOverlayScreen>);

	// Buddy toast: only while RmlUi owns the shell, so its toast presenter replaces the .wnd one
	// (see RmlBuddyToastScreen.h); -wnd never calls this.
	if (m_context && !RmlUiScreenRegistry::usesLegacyMenus())
		InitRmlBuddyToastScreen(m_context);
	// GSOVERLAY_GAMEOPTIONS/GSOVERLAY_GAMEPASSWORD's .wnd paths, same gsOverlays[] precedent.
	RmlUiScreenRegistry::registerScreen("Menus/PopupHostGame.wnd", &OpenRmlHostGameScreen, &CloseRmlHostGameScreen, &screenVisible<RmlHostGameScreen>, &popupBack<RmlHostGameScreen>);
	RmlUiScreenRegistry::registerScreen("Menus/PopupJoinGame.wnd", &OpenRmlJoinGameScreen, &CloseRmlJoinGameScreen, &screenVisible<RmlJoinGameScreen>, &popupBack<RmlJoinGameScreen>);

	// GameLogicDispatch pushes this after a match/campaign mission ends; see RmlScoreScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/ScoreScreen.wnd", &OpenRmlScoreScreen, &CloseRmlScoreScreen, &screenVisible<RmlScoreScreen>, &screenBack<RmlScoreScreen>);

	// ScriptActions::doVictory()/doDefeat()/doLocalDefeat() create these via winCreateFromScript();
	// see GameWindowManagerScript.cpp's winCreateFromScript() and RmlEndGameOverlayScreen.h.
	// They sit over live gameplay, so they don't capture input (see RmlUiScreenRegistry::ownsInput()).
	RmlUiScreenRegistry::registerScreen("Menus/Victorious.wnd", &OpenRmlVictoriousScreen, &CloseRmlVictoriousScreen, &screenVisible<RmlEndGameOverlayScreen>, nullptr, false);
	RmlUiScreenRegistry::registerScreen("Menus/Defeat.wnd", &OpenRmlDefeatScreen, &CloseRmlDefeatScreen, &screenVisible<RmlEndGameOverlayScreen>, nullptr, false);
	RmlUiScreenRegistry::registerScreen("Menus/LocalDefeat.wnd", &OpenRmlLocalDefeatScreen, &CloseRmlLocalDefeatScreen, &screenVisible<RmlEndGameOverlayScreen>, nullptr, false);
	RmlUiScreenRegistry::registerScreen("Menus/ObserverQuit.wnd", &OpenRmlObserverQuitScreen, &CloseRmlObserverQuitScreen, &screenVisible<RmlEndGameOverlayScreen>, nullptr, false);

	// DisconnectMenu::showScreen()/hideScreen() open and close this through a placeholder layout; see
	// RmlDisconnectScreen.h. It sits over live gameplay, so it does not capture input.
	RmlUiScreenRegistry::registerScreen("Menus/DisconnectScreen.wnd", &OpenRmlDisconnectScreen, &CloseRmlDisconnectScreen, &screenVisible<RmlDisconnectScreen>, nullptr, false);

	// The Generals Online patch check creates this layout over the main menu; see RmlDownloadScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/DownloadMenu.wnd", &OpenRmlDownloadScreen, &CloseRmlDownloadScreen, &screenVisible<RmlDownloadScreen>, &popupBack<RmlDownloadScreen>);

	// InGameUI::popupMessage() creates this layout for a map script; see RmlInGamePopupScreen.h. It sits over live
	// gameplay, so it does not capture input, but it takes Enter and Escape like the .wnd did.
	RmlUiScreenRegistry::registerScreen("InGamePopupMessage.wnd", &OpenRmlInGamePopupScreen, &CloseRmlInGamePopupScreen, &screenVisible<RmlInGamePopupScreen>, nullptr, false, &screenKey<RmlInGamePopupScreen>);

	// The LoadScreen classes create these via winCreateFromScript(); see RmlLoadScreen.h.
	RmlUiScreenRegistry::registerScreen("Menus/MapTransferScreen.wnd", &OpenRmlMapTransferScreen, &CloseRmlMapTransferScreen, &loadScreenVisible<RmlLoadScreen::KIND_MAP_TRANSFER>, nullptr);
	RmlUiScreenRegistry::registerScreen("Menus/MultiplayerLoadScreen.wnd", &OpenRmlMultiplayerLoadScreen, &CloseRmlMultiplayerLoadScreen, &loadScreenVisible<RmlLoadScreen::KIND_MULTIPLAYER>, nullptr);
	RmlUiScreenRegistry::registerScreen("Menus/GameSpyLoadScreen.wnd", &OpenRmlOnlineLoadScreen, &CloseRmlOnlineLoadScreen, &loadScreenVisible<RmlLoadScreen::KIND_ONLINE>, nullptr);
	RmlUiScreenRegistry::registerScreen("Menus/ShellGameLoadScreen.wnd", &OpenRmlShellLoadScreen, &CloseRmlShellLoadScreen, &loadScreenVisible<RmlLoadScreen::KIND_SHELL>, nullptr);

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

	m_scrollLogInstancer = new Rml::ElementInstancerGeneric<RmlScrollLogElement>();
	Rml::Factory::RegisterElementInstancer("scrolllog", m_scrollLogInstancer);
}

void RmlUiManager::shutdown()
{
	if (!m_initialized)
		return;

	if (TheRmlUiInputHook == this)
		TheRmlUiInputHook = nullptr;

	UnregisterRmlMessageBoxHook();
	ShutdownRmlBuddyToastScreen();
	RmlUiScreenRegistry::unregisterAll();

	if (m_context)
	{
		m_context->RemoveEventListener("mousedown", &s_soundListener, true);
		m_context->RemoveEventListener("click", &s_soundListener, true);
		Rml::RemoveContext(m_context->GetName());
		m_context = nullptr;
	}

	Rml::Shutdown();

	// Rml::Shutdown() does not take ownership of instancers registered via Factory; free them now.
	delete m_gameTextInstancer; m_gameTextInstancer = nullptr;
	delete m_mappedImageInstancer; m_mappedImageInstancer = nullptr;
	delete m_mapPreviewInstancer; m_mapPreviewInstancer = nullptr;
	delete m_scrollLogInstancer; m_scrollLogInstancer = nullptr;
	m_currentScreen = nullptr;
	TransitionSounds::reset();
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
	TransitionSounds::update();
	if (m_currentScreen)
		m_currentScreen->update();
	RmlLoadScreen::tick();
	RmlReplayMenuScreen::tick();
	RmlPopupReplayScreen::tick();
	RmlChallengeMenuScreen::tick();
	RmlDisconnectScreen::tick();
	RmlDownloadScreen::tick();
	RmlUiMessageBoxHook::raise(); // a box stays above screens shown after it
	if (m_context)
		m_context->Update();
	updateTooltip();

	// Toast auto-dismiss needs a per-frame tick whichever screen is current; no-op otherwise.
	BuddyOverlaySession::tickToast();
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
bool RmlUiManager::hasVisibleDocument() const
{
	if (!m_context)
		return false;

	for (int i = 0; i < m_context->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument *doc = m_context->GetDocument(i);
		if (doc && doc->IsVisible())
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
bool RmlUiManager::anyVisibleDocumentAt(int x, int y) const
{
	if (!m_context)
		return false;

	for (int i = 0; i < m_context->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument *doc = m_context->GetDocument(i);
		if (!doc || !doc->IsVisible())
			continue;

		float left = doc->GetAbsoluteLeft();
		float top = doc->GetAbsoluteTop();
		if (x >= left && y >= top && x <= left + doc->GetOffsetWidth() && y <= top + doc->GetOffsetHeight())
			return true;
	}
	return false;
}

// The registry knows which layers (screens, popups, message boxes) are up, from the RmlUi side's
// own visibility, so this is exactly what is on screen.
bool RmlUiManager::ownsInput() const
{
	return RmlUiScreenRegistry::ownsInput();
}

// Over a capturing layer RmlUi owns the whole mouse; otherwise only over a visible document (the
// buddy toast, an end-game overlay), so the legacy HUD keeps the rest of the screen.
bool RmlUiManager::wantsMouseInput(int mouseX, int mouseY) const
{
	return ownsInput() || anyVisibleDocumentAt(mouseX, mouseY);
}

// An overlay that handles a few keys itself (the script popup's Enter and Escape) gets them offered too; see
// processKey().
bool RmlUiManager::wantsKeyboardInput() const
{
	return keyboardOwned() || RmlUiScreenRegistry::wantsOverlayKeys();
}

bool RmlUiManager::keyboardOwned() const
{
	if (ownsInput())
		return true;

	// Otherwise a visible document still owns the keyboard while one of its text fields has focus.
	Rml::Element *focus = m_context ? m_context->GetFocusElement() : nullptr;
	if (!focus || !focus->IsVisible())
		return false;
	const Rml::String &tag = focus->GetTagName();
	if (tag == "textarea")
		return true;
	if (tag != "input")
		return false;
	const Rml::String type = focus->GetAttribute<Rml::String>("type", "text");
	return type == "text" || type == "password";
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

bool RmlUiManager::processKey(unsigned char engineKey, unsigned char engineKeyState)
{
	if (!m_context)
		return false;

	// An overlay's own keys come first, taking them from the game even where RmlUi owns no keyboard.
	if (RmlUiScreenRegistry::overlayKey(engineKey, engineKeyState))
		return true;
	if (!keyboardOwned())
		return false;

	const bool isDown = BitIsSet(engineKeyState, KEY_STATE_DOWN);
	Rml::Input::KeyIdentifier rmlKey = engineKeyToRmlKey(engineKey);

	// Escape goes to the topmost open layer only (a message box swallows it), as its .wnd's KEY_ESC
	// handler would; with no RmlUi layer up it is left to the game (in-game quit menu, ...).
	if (rmlKey == Rml::Input::KI_ESCAPE)
		return RmlUiScreenRegistry::escape(isDown);

	if (rmlKey == Rml::Input::KI_UNKNOWN)
		return true;

	int mods = computeKeyModifiers();
	if (isDown)
		m_context->ProcessKeyDown(rmlKey, mods);
	else
		m_context->ProcessKeyUp(rmlKey, mods);
	return true;
}

void RmlUiManager::processTextInput(unsigned short utf16Char)
{
	// Control characters (backspace, enter, tab) arrive as key presses via processKey().
	if (utf16Char < 32 || utf16Char == 127)
		return;
	if (m_context)
		m_context->ProcessTextInput(Rml::Character(utf16Char));
}

void RmlUiManager::showScreen(RmlScreen *screen)
{
	if (!screen || !m_context)
		return;

	// TheSuperHackers @fix Set m_currentScreen before hiding the previous one, not after: a
	// screen's hide() can itself call back into showScreen() for the same screen. With the new
	// screen already installed as current, that reentrant call sees previous == screen and skips
	// hiding it again instead of recursing forever.
	RmlScreen *previous = m_currentScreen;
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
