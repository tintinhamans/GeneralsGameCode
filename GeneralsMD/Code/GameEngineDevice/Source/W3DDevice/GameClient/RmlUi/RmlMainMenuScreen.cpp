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

// Ronin @feature 29/10/2026 DX9: the main menu over the shell map. Each button runs what MainMenu.cpp's runs, without its
// transitions; ported from tintinhamans' fork (arctic/go/feat/rmlui), cut to the game's own menus.
#include "W3DDevice/GameClient/RmlUi/RmlMainMenuScreen.h"

#include "gamespy/ghttp/ghttp.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/OptionPreferences.h"
#include "Common/RandomValue.h"
#include "Common/version.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/IOnlineGameHooks.h"
#include "W3DDevice/GameClient/RmlUi/RmlTransitionSounds.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiText.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	const char *const kDocument = "Data/UI/MainMenu.rml";

	// MainMenu.cpp's quitCallback(), for the exit box's Yes.
	void quitGame()
	{
		TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_SELECTED]);
		TheShell->pop();
		TheGameEngine->setQuitting(TRUE);
		if (TheGameLogic->isInGame())
			TheGameLogic->exitGame();
	}
}

RmlMainMenuScreen &RmlMainMenuScreen::instance()
{
	static RmlMainMenuScreen s_screen;
	return s_screen;
}

void RmlMainMenuScreen::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;
	Rml::Context *context = TheRmlUiManager->getContext();
	if (!m_model)
	{
		Rml::DataModelConstructor c = context->CreateDataModel("mainmenu");
		if (!c)
			return;
		// His document's names (MainMenu.rml), each event onto the action MainMenu.cpp's button runs.
		c.Bind("panel", &m_panel);
		c.Bind("selected_faction", &m_faction);
		c.Bind("hover_faction", &m_hover);
		c.Bind("version", &m_version);
		c.Bind("no_shellmap", &m_noShellMap);
		c.RegisterArray<Rml::Vector<Rml::String>>();
		c.Bind("news_lines", &m_newsLines);
		c.Bind("has_news", &m_hasNews);
		c.Bind("players_online_text", &m_playersOnline);
		m_hq.bind(c);
		struct Event { const char *name, *what, *arg; };
		static const Event events[] =
		{
			{ "go_single", "go", "single" }, { "go_multi", "go", "multi" }, { "go_loadreplay", "go", "loadreplay" },
			{ "back_main", "go", "main" }, { "diff_back", "back", "" },
			{ "go_options", "options", "" }, { "go_credits", "credits", "" }, { "go_exit", "exit", "" },
			{ "go_skirmish", "skirmish", "" }, { "go_online", "online", "" }, { "go_network", "network", "" },
			{ "go_load_game", "load", "" }, { "go_replay", "replays", "" },
			{ "select_usa", "campaign", "USA" }, { "select_gla", "campaign", "GLA" }, { "select_china", "campaign", "China" },
			{ "select_challenge", "challenge", "" },
			{ "preview_usa", "hover", "USA" }, { "preview_gla", "hover", "GLA" }, { "preview_china", "hover", "China" },
			{ "preview_challenge", "hover", "Training" }, { "preview_clear", "hover", "" },
			{ "select_easy", "difficulty", "0" }, { "select_medium", "difficulty", "1" }, { "select_hard", "difficulty", "2" },
		};
		for (const Event &e : events)
		{
			const Rml::String what = e.what, arg = e.arg;
			c.BindEventCallback(e.name, [this, what, arg](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { act(what, arg); });
		}
		m_model = c.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen(kDocument);
}

// MainMenuInit(): the shell map behind, the cursor on, GameSpy's leftovers torn down, the map list fresh.
void RmlMainMenuScreen::open()
{
	load();
	if (!m_document || m_opening)
		return;
	m_opening = true;
	const bool wasShown = isVisible();

	TheWritableGlobalData->m_breakTheMovie = FALSE;	// the intro stops drawing until a menu says otherwise
	if (TheShell)
		TheShell->showShellMap(TRUE);
	if (TheMouse)
		TheMouse->setVisibility(TRUE);

	if (!wasShown)
	{
		m_panel = "main";
		m_faction.clear();
		m_hover.clear();
		m_challenge = false;
		if (TheGameSpyPeerMessageQueue && !TheGameSpyPeerMessageQueue->isConnected())
			TearDownGameSpy();
		if (TheMapCache)
			TheMapCache->updateCache();
		if (TheShell)
			TheShell->loadScheme("MainMenu");
		m_raiseMessageBoxes = true;
		TransitionSounds::play("MainMenuDefaultMenuLogoFade");	// MainMenuInit()'s entrance
		m_version = TheVersion ? unicodeToUtf8(TheVersion->getUnicodeProductVersionHashString()) : Rml::String();
		m_hq.refresh(m_model, true);
		if (m_model)
			m_model.DirtyAllVariables();
	}

	// Without the shell map the shell lays a black window over everything, RmlUi included; this screen draws its own.
	m_noShellMap = TheGlobalData && !TheGlobalData->m_shellMapOn;
	if (m_noShellMap)
	{
		GameWindow *blank = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("BlankWindow.wnd:BlankWindow"));
		if (blank)
			blank->winHide(TRUE);
	}
	if (m_model)
		m_model.DirtyVariable("no_shellmap");

	TheRmlUiManager->showScreen(m_document);
	m_opening = false;
}

// MainMenuShutdown(): a pending patch check stops reporting here.
void RmlMainMenuScreen::close()
{
	GetOnlineGameHooks()->cancelPatchCheck();	// Ronin @feature 21/11/2026 DX9: the provider's, as MainMenuShutdown()
	setShellHook("");
	if (m_document)
		m_document->Hide();
}

bool RmlMainMenuScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlMainMenuScreen::back()
{
	if (m_panel == "difficulty")
		setPanel("single");
	else if (m_panel != "main")
		setPanel("main");
}

void RmlMainMenuScreen::reload()
{
	if (!m_document)
		return;
	const bool shown = isVisible();
	m_document->Close();
	m_document = nullptr;
	load();
	if (shown && m_document)
		TheRmlUiManager->showScreen(m_document);
}

void RmlMainMenuScreen::release()
{
	m_document = nullptr;
	m_model = Rml::DataModelHandle();
}

//-------------------------------------------------------------------------------------------------
// The script hooks the shell map's camera follows, from GBM_MOUSE_ENTERING / LEAVING on four buttons: the nearest data-hook.
void RmlMainMenuScreen::update()
{
	Rml::String hook;
	Rml::Context *context = TheRmlUiManager ? TheRmlUiManager->getContext() : nullptr;
	for (Rml::Element *e = context ? context->GetHoverElement() : nullptr; e && e != m_document; e = e->GetParentNode())
		if (e->HasAttribute("data-hook"))
		{
			hook = e->GetAttribute<Rml::String>("data-hook", "");
			break;
		}
	setShellHook(hook);
	m_hq.refresh(m_model);	// the header's clock
}

void RmlMainMenuScreen::setShellHook(const Rml::String &hook)
{
	if (hook == m_hook || !TheScriptEngine)
		return;
	struct Hook { const char *name; int lit, unlit; };
	static const Hook hooks[] =
	{
		{ "online", SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_UNHIGHLIGHTED },
		{ "network", SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_UNHIGHLIGHTED },
		{ "options", SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_UNHIGHLIGHTED },
		{ "exit", SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_HIGHLIGHTED, SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_UNHIGHLIGHTED },
	};
	for (const Hook &h : hooks)
		if (m_hook == h.name)
			TheScriptEngine->signalUIInteract(TheShellHookNames[h.unlit]);
	for (const Hook &h : hooks)
		if (hook == h.name)
			TheScriptEngine->signalUIInteract(TheShellHookNames[h.lit]);
	// Ronin @feature 30/10/2026 DX9: the single player buttons signal no hook; their hover's faction group plays its logo sound (his).
	struct HoverGroup { const char *name, *group; };
	static const HoverGroup hoverGroups[] =
	{
		{ "skirmish", "MainMenuFactionSkirmish" }, { "usa", "MainMenuFactionUS" }, { "gla", "MainMenuFactionGLA" },
		{ "china", "MainMenuFactionChina" }, { "challenge", "MainMenuFactionTraining" },
	};
	for (const HoverGroup &h : hoverGroups)
		if (hook == h.name)
			TransitionSounds::play(h.group);
	m_hook = hook;
}

// Ronin @feature 30/10/2026 DX9: the groups MainMenu.cpp runs when it switches panels - the one left plays its reversed "Back"
// group, the one entered is set behind it. His (arctic/go/feat/rmlui), on the campaign's faction.
static void playPanelSounds(const Rml::String &from, const Rml::String &to, const Rml::String &faction)
{
	if (from == to)
		return;
	struct Faction { const char *label, *shortName; };
	static const Faction factions[] = { { "USA", "US" }, { "GLA", "GLA" }, { "China", "China" }, { "Training", "Training" } };
	const Faction *f = nullptr;
	for (const Faction &each : factions)
		if (faction == each.label)
			f = &each;

	AsciiString back, next;
	if (from == "main" && to == "single") { back = "MainMenuDefaultMenuBack"; next = "MainMenuSinglePlayerMenu"; }
	else if (from == "main" && to == "multi") { back = "MainMenuDefaultMenuBack"; next = "MainMenuMultiPlayerMenu"; }
	else if (from == "main" && to == "loadreplay") { back = "MainMenuDefaultMenuBack"; next = "MainMenuLoadReplayMenu"; }
	else if (from == "single" && to == "main") { back = "MainMenuSinglePlayerMenuBack"; next = "MainMenuDefaultMenu"; }
	else if (from == "multi" && to == "main") { back = "MainMenuMultiPlayerMenuReverse"; next = "MainMenuDefaultMenu"; }
	else if (from == "loadreplay" && to == "main") { back = "MainMenuLoadReplayMenuBack"; next = "MainMenuDefaultMenu"; }
	else if (f && from == "single" && to == "difficulty")
	{
		back.format("MainMenuSinglePlayerMenuBack%s", f->shortName);
		next.format("MainMenuDifficultyMenu%s", f->shortName);
	}
	else if (f && from == "difficulty" && to == "single")
	{
		back.format("MainMenuDifficultyMenu%sBack", f->shortName);
		next.format("MainMenuSinglePlayer%sMenuFromDiff", f->label);
	}
	else
		return;
	const Int backFrames = TransitionSounds::play(back.str(), TRUE);
	TransitionSounds::play(next.str(), FALSE, backFrames);
}

void RmlMainMenuScreen::setPanel(const Rml::String &panel)
{
	playPanelSounds(m_panel, panel, m_faction);
	if (m_panel == "difficulty" && panel != "difficulty")
	{
		TheCampaignManager->setCampaign(AsciiString::TheEmptyString);	// ButtonDiffBack's
		m_faction.clear();
		m_challenge = false;
	}
	m_panel = panel;
	m_hover.clear();
	if (m_model)
		m_model.DirtyAllVariables();
}

//-------------------------------------------------------------------------------------------------
// prepareCampaignGame(), setupGameStart() and doGameStart(), without the fade they wait for.
void RmlMainMenuScreen::startCampaign(int difficulty)
{
	const GameDifficulty diff = (GameDifficulty)difficulty;
	TransitionSounds::play(m_challenge ? "MainMenuDifficultyMenuTraining" : "MainMenuDifficultyMenuBack", TRUE);
	OptionPreferences pref;
	pref.setCampaignDifficulty(diff);
	pref.write();
	TheScriptEngine->setGlobalDifficulty(diff);
	TheCampaignManager->setGameDifficulty(diff);

	if (m_challenge)
	{
		if (TheChallengeGenerals)
			TheChallengeGenerals->setCurrentDifficulty(diff);
		TheShell->push("Menus/ChallengeMenu.wnd");
		return;
	}

	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	InitRandom(0);
	close();	// the shell's hideShell() closes it as well once the map starts loading
}

void RmlMainMenuScreen::act(const Rml::String &what, const Rml::String &arg)
{
	if (what == "hover")
	{
		if (m_hover != arg && m_panel == "single")
		{
			m_hover = arg;
			if (m_model)
				m_model.DirtyVariable("hover_faction");
		}
		return;
	}
	if (what == "go")	// his root menu: back out of the difficulty first; again on the open one closes it; else via the root
	{
		if (arg != "main" && m_panel == "difficulty")
		{
			setPanel("single");
			if (arg == "single")
				return;
		}
		if (arg == "main" || m_panel == arg)
			setPanel("main");
		else
		{
			if (m_panel != "main")
				setPanel("main");
			setPanel(arg);
		}
	}
	else if (what == "back")
		back();
	else if (what == "campaign")		// USA, GLA, China: then the difficulty
	{
		TheCampaignManager->setCampaign(AsciiString(arg.c_str()));
		m_faction = arg;	// before the panel: its sounds are the faction's
		m_challenge = false;
		setPanel("difficulty");
	}
	else if (what == "challenge")		// the difficulty first, then the Generals Challenge menu; no campaign is set
	{
		m_faction = "Training";	// his document's name for the Challenge's art
		m_challenge = true;
		setPanel("difficulty");
	}
	else if (what == "difficulty")
		startCampaign(atoi(arg.c_str()));
	else if (what == "skirmish")
	{
		TransitionSounds::play("MainMenuSinglePlayerMenuBackSkirmish", TRUE);
		TheShell->push("Menus/SkirmishGameOptionsMenu.wnd");
		TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_SKIRMISH_SELECTED]);
	}
	else if (what == "online")
	{
		TransitionSounds::play("MainMenuMultiPlayerMenuTransitionToNext", TRUE);
		GetOnlineGameHooks()->startOnline();	// Ronin @feature 21/11/2026 DX9: the provider's Online path (GO's sign-in, GameSpy's patch check)
	}
	else if (what == "network")
	{
		TransitionSounds::play("MainMenuMultiPlayerMenuTransitionToNext", TRUE);
		TheShell->push("Menus/LanLobbyMenu.wnd");
		TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
	}
	else if (what == "load")
	{
		TransitionSounds::play("MainMenuLoadReplayMenuBackTransition", TRUE);
		TheShell->push("Menus/SaveLoad.wnd");
	}
	else if (what == "replays")
	{
		TransitionSounds::play("MainMenuLoadReplayMenuBackTransition", TRUE);
		TheShell->push("Menus/ReplayMenu.wnd");
	}
	else if (what == "credits")
		TheShell->push("Menus/CreditsMenu.wnd");
	else if (what == "options")
	{
		TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_SELECTED]);
		RmlUiScreenRegistry::open(AsciiString("Menus/OptionsMenu.wnd"));
	}
	else if (what == "exit")			// windowed quits at once, as MainMenu.cpp does
	{
		if (TheGlobalData->m_windowed)
			quitGame();
		else
			QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"), quitGame, nullptr);
	}
}

//-------------------------------------------------------------------------------------------------
// MainMenuSystem()'s GWM_CREATE and GWM_DESTROY.
void RmlMainMenuScreen::created()
{
	ghttpStartup();
}

void RmlMainMenuScreen::destroyed()
{
	ghttpCleanup();
	TearDownGameSpy();
	GetOnlineGameHooks()->stopOnlineChecks();	// Ronin @feature 21/11/2026 DX9: the provider's DNS check, as MainMenuSystem()
}

// MainMenuUpdate()'s pumps, for as long as the menu's layout is on the shell's stack (the online screens open over it).
void RmlMainMenuScreen::shellUpdate()
{
	if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		return;
	if (TheDownloadManager && !TheDownloadManager->isDone())
	{
		TheDownloadManager->update();
		GetOnlineGameHooks()->updateDownloadMenu(nullptr, nullptr);	// Ronin @feature 21/11/2026 DX9: the provider's, as MainMenuUpdate()
	}
	RmlMainMenuScreen &screen = instance();
	if (screen.m_raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		screen.m_raiseMessageBoxes = false;
	}
	GetOnlineGameHooks()->updateMainMenu();	// Ronin @feature 21/11/2026 DX9: GO's sign-in dialog ticks here
	GameSpyUpdateOverlays();
}
