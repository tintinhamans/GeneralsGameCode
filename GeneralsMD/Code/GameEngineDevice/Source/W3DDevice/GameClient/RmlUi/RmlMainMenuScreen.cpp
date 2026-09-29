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

#include "W3DDevice/GameClient/RmlUi/RmlMainMenuScreen.h"

#include "Common/AsciiString.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/version.h"
#include "GameClient/GUI/GUICallbacks/Menus/MainMenuActions.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// GameWinMsgBoxFunc (see GameWindow.h) is a plain void(*)(); wraps the shared quit action for
// QuitMessageBoxYesNo's Yes button the same way MainMenu.cpp's file-local quitCallback() does.
static void quitConfirmedCallback()
{
	MainMenuActions::quit();
}

//-------------------------------------------------------------------------------------------------
// Same conversion as RmlGameTextElement (RmlUiElements.cpp): TheVersion's string is UnicodeString
// (UTF-16), Rml::String is UTF-8.
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1); // len includes the null terminator
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

//-------------------------------------------------------------------------------------------------
RmlMainMenuScreen::RmlMainMenuScreen()
{
}

RmlMainMenuScreen::~RmlMainMenuScreen()
{
	if (m_context && m_document)
	{
		m_context->UnloadDocument(m_document);
		m_context->RemoveDataModel("mainmenu");
	}
}

void RmlMainMenuScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("mainmenu");
	if (constructor)
	{
		constructor.Bind("panel", &m_model.panel);
		constructor.Bind("selected_faction", &m_model.selectedFaction);
		constructor.Bind("hover_faction", &m_model.hoverFaction);
		constructor.Bind("version", &m_model.version);

		constructor.BindEventCallback("go_single", &RmlMainMenuScreen::onGoSingle, this);
		constructor.BindEventCallback("go_multi", &RmlMainMenuScreen::onGoMulti, this);
		constructor.BindEventCallback("go_loadreplay", &RmlMainMenuScreen::onGoLoadReplay, this);
		constructor.BindEventCallback("back_main", &RmlMainMenuScreen::onBackToMain, this);
		constructor.BindEventCallback("go_options", &RmlMainMenuScreen::onGoOptions, this);
		constructor.BindEventCallback("go_credits", &RmlMainMenuScreen::onGoCredits, this);
		constructor.BindEventCallback("go_exit", &RmlMainMenuScreen::onGoExit, this);

		constructor.BindEventCallback("go_skirmish", &RmlMainMenuScreen::onGoSkirmish, this);
		constructor.BindEventCallback("select_usa", &RmlMainMenuScreen::onSelectUSA, this);
		constructor.BindEventCallback("select_gla", &RmlMainMenuScreen::onSelectGLA, this);
		constructor.BindEventCallback("select_china", &RmlMainMenuScreen::onSelectChina, this);
		constructor.BindEventCallback("select_challenge", &RmlMainMenuScreen::onSelectChallenge, this);
		constructor.BindEventCallback("diff_back", &RmlMainMenuScreen::onDiffBack, this);
		constructor.BindEventCallback("preview_usa", &RmlMainMenuScreen::onPreviewUSA, this);
		constructor.BindEventCallback("preview_gla", &RmlMainMenuScreen::onPreviewGLA, this);
		constructor.BindEventCallback("preview_china", &RmlMainMenuScreen::onPreviewChina, this);
		constructor.BindEventCallback("preview_challenge", &RmlMainMenuScreen::onPreviewChallenge, this);
		constructor.BindEventCallback("preview_clear", &RmlMainMenuScreen::onPreviewClear, this);
		constructor.BindEventCallback("select_easy", &RmlMainMenuScreen::onSelectEasy, this);
		constructor.BindEventCallback("select_medium", &RmlMainMenuScreen::onSelectMedium, this);
		constructor.BindEventCallback("select_hard", &RmlMainMenuScreen::onSelectHard, this);

		constructor.BindEventCallback("go_online", &RmlMainMenuScreen::onGoOnline, this);
		constructor.BindEventCallback("go_network", &RmlMainMenuScreen::onGoNetwork, this);
		constructor.BindEventCallback("go_load_game", &RmlMainMenuScreen::onGoLoadGame, this);
		constructor.BindEventCallback("go_replay", &RmlMainMenuScreen::onGoReplay, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/MainMenu.rml");
}

void RmlMainMenuScreen::show()
{
	if (!m_document)
		return;

	// Mirrors MainMenuInit()'s TheShell->showShellMap(TRUE)/TheMouse->setVisibility(TRUE); both
	// are no-ops if the shell map is already running (see Shell::showShellMap).
	if (TheShell)
		TheShell->showShellMap(TRUE);
	if (TheMouse)
		TheMouse->setVisibility(TRUE);

	// Always land back on the root panel, same as returning to a freshly-(re)init'd .wnd MainMenu.
	m_model.panel = "main";
	m_model.selectedFaction = "";
	m_model.hoverFaction = "";
	m_challengePending = false;
	m_model.version = TheVersion ? unicodeToUtf8(TheVersion->getUnicodeProductVersionHashString()) : Rml::String();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_document->Show();
}

void RmlMainMenuScreen::hide()
{
	setShellHook("");
	if (m_document)
		m_document->Hide();
}

bool RmlMainMenuScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlMainMenuScreen::onBack()
{
	// Escape steps back one panel, like the corresponding Back button; no-op already at the root.
	if (m_model.panel == "difficulty")
		goBackFromDifficulty();
	else if (m_model.panel != "main")
		setPanel("main");
}

void RmlMainMenuScreen::update()
{
	// Only what MainMenuUpdate() does that isn't .wnd-animation bookkeeping: patch download pump
	// and GameSpy/HTTP think (see report). doGameStart()'s deferred completion isn't needed here --
	// MainMenuActions::startCampaignAtDifficulty() fires it immediately, there is no animation to
	// wait for.
	if (TheDownloadManager && !TheDownloadManager->isDone())
		TheDownloadManager->update();

	HTTPThinkWrapper();

	updateShellHook();
}

//-------------------------------------------------------------------------------------------------
void RmlMainMenuScreen::updateShellHook()
{
	Rml::String hook;
	if (m_context && isVisible())
	{
		for (Rml::Element *e = m_context->GetHoverElement(); e && e != m_document; e = e->GetParentNode())
		{
			if (e->HasAttribute("data-hook"))
			{
				hook = e->GetAttribute<Rml::String>("data-hook", "");
				break;
			}
		}
	}
	setShellHook(hook);
}

void RmlMainMenuScreen::setShellHook(const Rml::String &hook)
{
	if (hook == m_hookedName || !TheScriptEngine)
		return;

	struct HookPair { const char *name; int highlighted, unhighlighted; };
	static const HookPair hooks[] =
	{
		{ "online", SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_UNHIGHLIGHTED },
		{ "network", SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_UNHIGHLIGHTED },
		{ "options", SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_UNHIGHLIGHTED },
		{ "exit", SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_UNHIGHLIGHTED },
	};

	for (const HookPair &pair : hooks)
	{
		if (m_hookedName == pair.name)
			TheScriptEngine->signalUIInteract(TheShellHookNames[pair.unhighlighted]);
	}
	for (const HookPair &pair : hooks)
	{
		if (hook == pair.name)
			TheScriptEngine->signalUIInteract(TheShellHookNames[pair.highlighted]);
	}

	// Solo-menu buttons signal no script hook in MainMenu.cpp; their hover plays the logo
	// scale-up transition (MainMenuMediumScaleUpTransition frame 1, forward only).
	static const char *const logoHoverHooks[] = { "skirmish", "usa", "gla", "china", "challenge" };
	for (const char *name : logoHoverHooks)
	{
		if (hook == name && TheAudio)
		{
			AudioEventRTS logoHover("GUILogoMouseOver");
			TheAudio->addAudioEvent(&logoHover);
		}
	}
	m_hookedName = hook;
}

//-------------------------------------------------------------------------------------------------
void RmlMainMenuScreen::setPanel(const Rml::String &panel)
{
	m_model.panel = panel;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("panel");

	// Preview art only exists on the "single" panel; drop it on every panel change so it can't
	// linger, matching the .wnd's "campaignSelected || dontAllowTransitions" hover guard.
	setHoverFaction("");
}

void RmlMainMenuScreen::setHoverFaction(const Rml::String &faction)
{
	if (m_model.hoverFaction == faction)
		return;
	m_model.hoverFaction = faction;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("hover_faction");
}

void RmlMainMenuScreen::onBackToMain(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	setPanel("main");
}

void RmlMainMenuScreen::onGoOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (TheScriptEngine)
		TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_SELECTED]);
	MainMenuActions::openOptions();
}

void RmlMainMenuScreen::onGoCredits(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::openCreditsMenu();
}

void RmlMainMenuScreen::onGoExit(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Same skip-confirmation-when-windowed rule as MainMenu.cpp's exitID handler.
	if (TheGlobalData && TheGlobalData->m_windowed)
		MainMenuActions::quit();
	else
		QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"), quitConfirmedCallback, nullptr);
}

void RmlMainMenuScreen::onGoSkirmish(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::startSkirmishOptions();
}

void RmlMainMenuScreen::selectFaction(const char *campaignName, const char *factionLabel, bool challenge)
{
	if (campaignName)
		MainMenuActions::selectCampaign(AsciiString(campaignName));
	m_model.selectedFaction = factionLabel;
	m_challengePending = challenge;
	setPanel("difficulty");
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("selected_faction");
}

void RmlMainMenuScreen::onSelectUSA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { selectFaction("USA", "USA", false); }
void RmlMainMenuScreen::onSelectGLA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { selectFaction("GLA", "GLA", false); }
void RmlMainMenuScreen::onSelectChina(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { selectFaction("China", "China", false); }
// Generals Challenge never calls selectCampaign (matches MainMenu.cpp's buttonChallengeID handler).
void RmlMainMenuScreen::onSelectChallenge(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { selectFaction(nullptr, "Training", true); }

void RmlMainMenuScreen::goBackFromDifficulty()
{
	MainMenuActions::selectCampaign(AsciiString::TheEmptyString);
	m_model.selectedFaction = "";
	m_challengePending = false;
	setPanel("single");
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("selected_faction");
}

void RmlMainMenuScreen::onDiffBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	goBackFromDifficulty();
}

void RmlMainMenuScreen::startAtDifficulty(int diff)
{
	bool challenge = m_challengePending;
	MainMenuActions::startCampaignAtDifficulty((GameDifficulty)diff, challenge);

	// Either the map is starting now (doGameStart()'s message was just queued) or ChallengeMenu.wnd
	// was just pushed on top of us: either way this screen has nothing left to show until the shell
	// comes back to it (showShell()/a later pop re-runs OpenRmlMainMenuScreen() through the registry).
	if (TheRmlUiManager)
		TheRmlUiManager->hideCurrentScreen();
}

void RmlMainMenuScreen::onSelectEasy(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { startAtDifficulty(DIFFICULTY_EASY); }
void RmlMainMenuScreen::onSelectMedium(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { startAtDifficulty(DIFFICULTY_NORMAL); }
void RmlMainMenuScreen::onSelectHard(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { startAtDifficulty(DIFFICULTY_HARD); }

void RmlMainMenuScreen::onGoOnline(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::startOnlinePatchCheck();
}

void RmlMainMenuScreen::onGoNetwork(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::openNetworkLobby();
}

void RmlMainMenuScreen::onGoLoadGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::openLoadGame();
}

void RmlMainMenuScreen::onGoReplay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::openReplayMenu();
}

//-------------------------------------------------------------------------------------------------
static RmlMainMenuScreen &instance()
{
	static RmlMainMenuScreen s_screen;
	return s_screen;
}

void OpenRmlMainMenuScreen()
{
	if (TheRmlUiManager)
		TheRmlUiManager->showScreen(&instance());
}

void CloseRmlMainMenuScreen()
{
	// Guards against RmlUiManager::showScreen()'s reentrant call while some other screen (e.g.
	// Options) is the one actually being hidden/shown; see RmlUiManager::showScreen()'s comment.
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &instance())
		TheRmlUiManager->hideCurrentScreen();
}
