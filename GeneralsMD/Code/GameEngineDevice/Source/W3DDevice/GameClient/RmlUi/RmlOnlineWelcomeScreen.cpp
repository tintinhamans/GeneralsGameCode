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

// Ronin @feature 21/11/2026 DX9: Generals Online's welcome hub, read from its current backend: the MOTD, win rates and buddy
// count as GenOnlineWelcomeMenu.cpp reads them, the player card as GenOnlinePopulatePlayerInfoWindows() does.
#include "W3DDevice/GameClient/RmlUi/RmlOnlineWelcomeScreen.h"

// GO's udp.h brings winsock.h and its NetworkMesh.h ws2ipdef.h, which needs winsock2.h first (the engine's own headers have it).
#include <winsock2.h>
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h"

#include "Common/AsciiString.h"
#include "Common/AudioEventRTS.h"
#include "Common/BattleHonors.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/GOUIColors.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuickmatchPreferences.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/RankPointValue.h"
#include "W3DDevice/GameClient/RmlUi/RmlTransitionSounds.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace
{
	Rml::String colorHex(Color color)
	{
		char hex[8];
		_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
		return Rml::String(hex);
	}

	// GenOnlineWelcomeMenu.cpp's grabUByte(): two hex digits of a "\aarrggbbText" MOTD colour prefix.
	UnsignedByte grabUByte(const char *s)
	{
		char tmp[5] = "0xff";
		tmp[2] = s[0];
		tmp[3] = s[1];
		return (UnsignedByte)strtol(tmp, nullptr, 16);
	}

	Rml::String numberText(const char *format, Int a, Int b = 0, Int c = 0)
	{
		char text[64];
		_snprintf_s(text, sizeof(text), _TRUNCATE, format, a, b, c);
		return Rml::String(text);
	}

	// GenOnlinePlayerInfoOverlay.cpp's rankNames and lookupRankImage(), by name for <mappedimage>.
	const char *const kRankNames[] =
	{
		"Private", "Corporal", "Sergeant", "Lieutenant", "Captain",
		"Major", "Colonel", "General", "Brigadier", "Commander",
	};
	static_assert(sizeof(kRankNames) / sizeof(kRankNames[0]) == MAX_RANKS, "one name a rank");

	Rml::String rankImageName(AsciiString side, Int rank)
	{
		if (side.isEmpty())
			return "NewPlayer";
		if (rank < 0 || rank >= MAX_RANKS)
			return Rml::String();
		if (side == "USA")
			side = "_USA";
		else if (side == "China")
			side = "_China";
		else if (side == "GLA")
			side = "_GLA";
		else if (side == "Random")
			side = "Elite";
		AsciiString name;
		name.format("Rank_%s%s", kRankNames[rank], side.str());
		if (name.compare("Rank_PrivateElite") == 0)
			name = "Rank";
		return Rml::String(name.str());
	}

	// The image each PercentXxx checkbox of the .wnd draws (its silver medallion), in the global stats' index order.
	const char *const kFactionStatIcons[] =
	{
		"USAGeneral_slvr", "ChinaGeneral_slvr", "GLAGeneral_slvr",
		"SuperWGeneral_slvr", "LaserGeneral_slvr", "AirGeneral_slvr",
		"TankGeneral_slvr", "InfantryGeneral_slvr", "NukeGeneral_slvr",
		"ToxinGeneral_slvr", "DemoGeneral_slvr", "StealthGeneral_slvr",
	};
	const int kFactionStatIconCount = (int)(sizeof(kFactionStatIcons) / sizeof(kFactionStatIcons[0]));

	// Ronin @feature 22/11/2026 DX9: the card's army rows, a side then its three generals, as GO's .wnd orders its checkboxes.
	const int kFactionDisplayOrder[] = { 0, 3, 4, 5, 1, 6, 7, 8, 2, 9, 10, 11 };

	// The army's own name, as the .wnd's checkbox tooltip names it ("SIDE:America" for the stats' "USA").
	UnicodeString factionName(int serviceIndex)
	{
		std::unordered_map<int, std::string>::const_iterator side = g_mapServiceIndexToPlayerTemplateString.find(serviceIndex);
		if (side == g_mapServiceIndexToPlayerTemplateString.end())
			return UnicodeString();
		AsciiString key;
		key.format("SIDE:%s", side->second == "USA" ? "America" : side->second.c_str());
		return TheGameText->fetch(key);
	}

	// A server section line, "========= Useful Links =========": its title, or false.
	bool sectionTitle(const std::string &line, std::string &title)
	{
		if (line.size() < 3 || line.front() != '=' || line.back() != '=')
			return false;
		const size_t begin = line.find_first_not_of("= ");
		const size_t end = line.find_last_not_of("= ");
		if (begin == std::string::npos || end < begin)
			return false;
		title = line.substr(begin, end - begin + 1);
		return true;
	}

	struct MotdLine
	{
		Rml::String text;
		Rml::String colorHex;
		bool section;
	};

	// Rough rows a line takes in a column, to balance the two.
	int motdWeight(const std::vector<MotdLine> &lines)
	{
		int rows = 0;
		for (size_t i = 0; i < lines.size(); ++i)
			rows += lines[i].section ? 2 : 1 + (int)lines[i].text.size() / 60;
		return rows;
	}

	// Ronin @feature 22/11/2026 DX9: GenOnlineQuickMatchMenu.cpp's constants and colours.
	const int kQmLobbyTimeoutMs = 10000;
	const int kQmDefaultCountdownMs = 5000;

	Color qmNoticeColor() { return GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 25, 255)); }

	// A playlist's own index past the cached list comes back with PlaylistID still at its default.
	bool qmValidPlaylist(const PlaylistEntry &playlist)
	{
		return playlist.PlaylistID != (uint16_t)-1;
	}
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 21/11/2026 DX9: the header pill's Generals Online state (RmlHqStatus.h).
bool RmlOnlineLoggedIn()
{
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	return auth != nullptr && auth->IsLoggedIn();
}

Rml::String RmlOnlineDisplayName()
{
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	return auth != nullptr ? unicodeToUtf8(UnicodeString(auth->GetDisplayNameW().c_str())) : Rml::String();
}

// The server's one count is its MOTD's "There are currently 1668 player(s) online."; 0 without it.
Int RmlOnlinePlayersOnline()
{
	GenOnlineServicesManager *services = GenOnlineServicesManager::GetInstance();
	if (services == nullptr)
		return 0;
	const std::string &motd = services->GetMOTD();
	const size_t tag = motd.find("player(s) online");
	if (tag == std::string::npos)
		return 0;
	size_t end = tag;
	while (end > 0 && motd[end - 1] == ' ')
		--end;
	size_t begin = end;
	while (begin > 0 && motd[begin - 1] >= '0' && motd[begin - 1] <= '9')
		--begin;
	return begin == end ? 0 : atoi(motd.substr(begin, end - begin).c_str());
}

//-------------------------------------------------------------------------------------------------
RmlOnlineWelcomeScreen &RmlOnlineWelcomeScreen::instance()
{
	static RmlOnlineWelcomeScreen s_screen;
	return s_screen;
}

void RmlOnlineWelcomeScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("onlinewelcome");
	if (constructor)
	{
		m_hq.bind(constructor);
		Rml::StructHandle<MotdLineModel> motdHandle = constructor.RegisterStruct<MotdLineModel>();
		if (motdHandle)
		{
			motdHandle.RegisterMember("text", &MotdLineModel::text);
			motdHandle.RegisterMember("color_hex", &MotdLineModel::colorHex);
			motdHandle.RegisterMember("is_heading", &MotdLineModel::isHeading);
			motdHandle.RegisterMember("is_section", &MotdLineModel::isSection);
			motdHandle.RegisterMember("used", &MotdLineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<MotdLineModel>>();

		Rml::StructHandle<FactionStatModel> factionHandle = constructor.RegisterStruct<FactionStatModel>();
		if (factionHandle)
		{
			factionHandle.RegisterMember("icon", &FactionStatModel::icon);
			factionHandle.RegisterMember("tooltip_text", &FactionStatModel::tooltipText);
			factionHandle.RegisterMember("percent_text", &FactionStatModel::percentText);
			factionHandle.RegisterMember("used", &FactionStatModel::used);
		}
		constructor.RegisterArray<Rml::Vector<FactionStatModel>>();

		Rml::StructHandle<QmOptionModel> optionHandle = constructor.RegisterStruct<QmOptionModel>();
		if (optionHandle)
		{
			optionHandle.RegisterMember("label", &QmOptionModel::label);
			optionHandle.RegisterMember("color_hex", &QmOptionModel::colorHex);
			optionHandle.RegisterMember("value", &QmOptionModel::value);
			optionHandle.RegisterMember("used", &QmOptionModel::used);
		}
		constructor.RegisterArray<Rml::Vector<QmOptionModel>>();

		Rml::StructHandle<QmMapModel> mapHandle = constructor.RegisterStruct<QmMapModel>();
		if (mapHandle)
		{
			mapHandle.RegisterMember("file", &QmMapModel::file);
			mapHandle.RegisterMember("name", &QmMapModel::name);
			mapHandle.RegisterMember("players", &QmMapModel::players);
			mapHandle.RegisterMember("selected", &QmMapModel::selected);
			mapHandle.RegisterMember("index", &QmMapModel::index);
			mapHandle.RegisterMember("used", &QmMapModel::used);
		}
		constructor.RegisterArray<Rml::Vector<QmMapModel>>();

		Rml::StructHandle<QmLogLineModel> logHandle = constructor.RegisterStruct<QmLogLineModel>();
		if (logHandle)
		{
			logHandle.RegisterMember("text", &QmLogLineModel::text);
			logHandle.RegisterMember("color_hex", &QmLogLineModel::colorHex);
			logHandle.RegisterMember("used", &QmLogLineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<QmLogLineModel>>();

		Rml::StructHandle<HonorModel> honorHandle = constructor.RegisterStruct<HonorModel>();
		if (honorHandle)
		{
			honorHandle.RegisterMember("image", &HonorModel::image);
			honorHandle.RegisterMember("title", &HonorModel::title);
			honorHandle.RegisterMember("text", &HonorModel::text);
			honorHandle.RegisterMember("gained", &HonorModel::gained);
			honorHandle.RegisterMember("used", &HonorModel::used);
		}
		constructor.RegisterArray<Rml::Vector<HonorModel>>();
		constructor.Bind("honors", &m_model.honors);
		constructor.Bind("honors_text", &m_model.honorsText);

		constructor.Bind("tab", &m_model.tab);
		constructor.Bind("qm_playlists", &m_model.qmPlaylists);
		constructor.Bind("qm_playlist", &m_model.qmPlaylist);
		constructor.Bind("qm_sides", &m_model.qmSides);
		constructor.Bind("qm_side", &m_model.qmSide);
		constructor.Bind("qm_army_enabled", &m_model.qmArmyEnabled);
		constructor.Bind("qm_colors", &m_model.qmColors);
		constructor.Bind("qm_color", &m_model.qmColor);
		constructor.Bind("qm_disconnects", &m_model.qmDisconnects);
		constructor.Bind("qm_max_disconnects", &m_model.qmMaxDisconnects);
		constructor.Bind("qm_maps", &m_model.qmMaps);
		constructor.Bind("qm_maps_text", &m_model.qmMapsText);
		constructor.Bind("qm_log", &m_model.qmLog);
		constructor.Bind("qm_searching", &m_model.qmSearching);
		constructor.Bind("qm_start_enabled", &m_model.qmStartEnabled);
		constructor.Bind("qm_stop_enabled", &m_model.qmStopEnabled);
		constructor.Bind("qm_widen_enabled", &m_model.qmWidenEnabled);
		constructor.Bind("qm_setup_enabled", &m_model.qmSetupEnabled);
		constructor.Bind("qm_locked", &m_model.qmLocked);
		constructor.Bind("monthly_elo_text", &m_model.monthlyEloText);

		constructor.Bind("buddies_count", &m_model.buddiesCount);
		constructor.Bind("motd_intro", &m_model.motdIntro);
		constructor.Bind("motd_left", &m_model.motdLeft);
		constructor.Bind("motd_right", &m_model.motdRight);
		constructor.Bind("faction_stats", &m_model.factionStats);

		constructor.Bind("rank_at_max", &m_model.rankAtMax);
		constructor.Bind("rank_progress_width_style", &m_model.rankProgressWidthStyle);
		constructor.Bind("rank_image_name", &m_model.rankImageName);
		constructor.Bind("show_faction_image", &m_model.showFactionImage);
		constructor.Bind("faction_image_name", &m_model.factionImageName);
		constructor.Bind("rank_text", &m_model.rankText);
		constructor.Bind("next_rank_text", &m_model.nextRankText);
		constructor.Bind("has_stats", &m_model.hasStats);
		constructor.Bind("games_text", &m_model.gamesText);
		constructor.Bind("quick_match_text", &m_model.quickMatchText);
		constructor.Bind("elo_text", &m_model.eloText);
		constructor.Bind("wins_text", &m_model.winsText);
		constructor.Bind("losses_text", &m_model.lossesText);
		constructor.Bind("win_percent_text", &m_model.winPercentText);
		constructor.Bind("best_streak_text", &m_model.bestStreakText);
		constructor.Bind("streak_label_text", &m_model.streakLabelText);
		constructor.Bind("streak_value_text", &m_model.streakValueText);

		constructor.BindEventCallback("back", &RmlOnlineWelcomeScreen::onBackPressed, this);
		constructor.BindEventCallback("home", &RmlOnlineWelcomeScreen::onHome, this);
		constructor.BindEventCallback("quick_match", &RmlOnlineWelcomeScreen::onQuickMatch, this);
		constructor.BindEventCallback("my_info", &RmlOnlineWelcomeScreen::onMyInfo, this);
		constructor.BindEventCallback("custom_match", &RmlOnlineWelcomeScreen::onCustomMatch, this);
		constructor.BindEventCallback("buddies", &RmlOnlineWelcomeScreen::onBuddies, this);
		constructor.BindEventCallback("log_out", &RmlOnlineWelcomeScreen::onLogOut, this);
		constructor.BindEventCallback("qm_start", &RmlOnlineWelcomeScreen::onQmStart, this);
		constructor.BindEventCallback("qm_stop", &RmlOnlineWelcomeScreen::onQmStop, this);
		constructor.BindEventCallback("qm_widen", &RmlOnlineWelcomeScreen::onQmWiden, this);
		constructor.BindEventCallback("qm_playlist_changed", &RmlOnlineWelcomeScreen::onQmPlaylistChanged, this);
		constructor.BindEventCallback("qm_side_changed", &RmlOnlineWelcomeScreen::onQmSideChanged, this);
		constructor.BindEventCallback("qm_map_toggle", &RmlOnlineWelcomeScreen::onQmMapToggle, this);
		constructor.BindEventCallback("qm_all_maps", &RmlOnlineWelcomeScreen::onQmAllMaps, this);
		constructor.BindEventCallback("qm_no_maps", &RmlOnlineWelcomeScreen::onQmNoMaps, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = TheRmlUiManager->loadScreen("Data/UI/OnlineWelcome.rml");
}

//-------------------------------------------------------------------------------------------------
// GenOnlineWelcomeMenuInit(): the MOTD, the armies, the buddy count; the two stats requests reply later. The greeting's name is
// the header status's player_name.
void RmlOnlineWelcomeScreen::show()
{
	if (!m_document)
		return;

	++m_opening;
	m_raiseBoxes = true;

	fillMotd();
	fillFactionStats();
	m_model.hasStats = false;
	m_model.rankAtMax = false;
	m_model.rankProgressWidthStyle = "0%";
	m_model.rankImageName.clear();
	m_model.showFactionImage = false;
	m_model.rankText.clear();
	m_model.nextRankText.clear();

	// The buddy count comes to one menu at a time: each GenOnline menu takes it over as it opens.
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	setBuddiesCount(social != nullptr ? social->GetNumTotalNotifications() : 0);
	if (social != nullptr)
	{
		const unsigned opening = m_opening;
		social->RegisterForCallback_OnNumberGlobalNotificationsChanged([opening](int numNotifications)
			{
				RmlOnlineWelcomeScreen &screen = instance();
				if (screen.m_opening == opening)
					screen.setBuddiesCount(numNotifications);
			});
	}

	// Ronin @feature 22/11/2026 DX9: Quick Match, GenOnlineQuickMatchMenuInit()'s part. The tab stays as left (a match brings its
	// player back to it); hide() ended any search unless the screen closed for the match, so the panel starts over here.
	qmRegisterCallbacks();
	if (TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress())
		qmReset();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_hq.refresh(m_modelHandle, true);
	m_document->Show();
	TheShell->showShellMap(TRUE);

	// GenOnlineWelcomeMenuInit()'s entrance group, and GenOnlineWelcomeMenuShutdown()'s reverse in hide().
	TransitionSounds::play("WOLWelcomeMenuFade");

	requestFactionStats();
	requestPlayerCard();
}

void RmlOnlineWelcomeScreen::hide()
{
	++m_opening;	// replies still on their way are dropped
	if (m_document && m_document->IsVisible())
		TransitionSounds::play("WOLWelcomeMenuFade", TRUE);
	if (m_document)
		m_document->Hide();

	// GenOnlineQuickMatchMenuShutdown(): a search ends with the screen, unless the screen closes for the match itself.
	if (qmBusy() && (TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress()))
		qmCancelSearch();
	qmDeregisterCallbacks();
	if (!TheGameEngine->getQuitting())
		qmSavePrefs();

	// GenOnlineWelcomeMenuShutdown()
	delete TheFirewallHelper;
	TheFirewallHelper = nullptr;
	RaiseGSMessageBox();
}

bool RmlOnlineWelcomeScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

// The way back: while a search runs it asks first; while a match is being set up there is no way out (the .wnd disables Back).
void RmlOnlineWelcomeScreen::onBack()
{
	if (m_qmState == QM_MATCH_FOUND)
		return;
	if (qmBusy())
	{
		MessageBoxYesNo(TheGameText->fetch("GUI:QuickMatch"), UnicodeString(L"Cancel the search and return to the main menu?"),
			&RmlOnlineWelcomeScreen::qmConfirmedToMainMenu, nullptr);
		return;
	}
	leaveToMainMenu();
}

// ButtonBack: a silent teardown, and the pop. The .wnd's update ended the teardown during its fade; this screen is closed by then.
void RmlOnlineWelcomeScreen::leaveToMainMenu()
{
	m_model.tab = "home";
	GenOnlineServicesManager *services = GenOnlineServicesManager::GetInstance();
	if (services != nullptr)
	{
		services->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);
		services->ConsumePendingFullTeardown();
	}
	TheShell->pop();
	if (services != nullptr)
		TearDownGenOnline();
}

// GenOnlineWelcomeMenuUpdate(), and GenOnlineQuickMatchMenuUpdate()'s countdown and match-setup timeout.
void RmlOnlineWelcomeScreen::update()
{
	m_hq.refresh(m_modelHandle);

	if (m_raiseBoxes)
	{
		RaiseGSMessageBox();
		m_raiseBoxes = false;
	}

	GenOnlineServicesManager *services = GenOnlineServicesManager::GetInstance();
	if (services != nullptr && services->IsPendingFullTeardown())
	{
		services->ConsumePendingFullTeardown();
		TheShell->pop();
		TearDownGenOnline();
		return;
	}

	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_ENTERED_FROM_GAME);

	GenOnlineHandleBuddyResponses();

	if (m_qmCountdownLastSecond > 0)
	{
		const int elapsedMs = (int)(::GetTickCount() - m_qmTimeoutStart);
		const int remainingMs = m_qmCountdownMs - elapsedMs;
		const int secondsRemaining = remainingMs > 0 ? (remainingMs + 999) / 1000 : 0;
		if (secondsRemaining > 0 && secondsRemaining < m_qmCountdownLastSecond)
		{
			UnicodeString message;
			message.format(TheGameText->fetch(secondsRemaining == 1 ? "LAN:GameStartTimerSingular" : "LAN:GameStartTimerPlural"),
				secondsRemaining);
			qmLog(unicodeToUtf8(message), GOUIColor(GOUICOLOR_MUTED, GameMakeColor(192, 192, 192, 255)));
			m_qmCountdownLastSecond = secondsRemaining;
		}
	}

	// Leave time for the server's START_GAME event to arrive.
	const int timeoutMs = m_qmTimeoutMs < kQmLobbyTimeoutMs ? kQmLobbyTimeoutMs : m_qmTimeoutMs;
	if (m_qmTimeoutStart != 0 && (int)(::GetTickCount() - m_qmTimeoutStart) >= timeoutMs)
	{
		m_qmTimeoutStart = 0;
		qmSetState(QM_SEARCHING);
		qmLog("Match setup timed out. You may cancel or continue waiting.", qmNoticeColor());
	}

	// The log holds its newest line in view (two frames: the new row is laid out on the first).
	if (m_qmScrollFrames > 0 && m_document)
	{
		--m_qmScrollFrames;
		if (Rml::Element *log = m_document->GetElementById("qm_log"))
			log->SetScrollTop(log->GetScrollHeight());
	}
}

//-------------------------------------------------------------------------------------------------
// updateNumPlayersOnline()'s listbox: the heading lines, then the MOTD with its "\aarrggbb" colour prefixes. Ronin @feature
// 22/11/2026 DX9: lines before the first "=====" section open the panel; the sections go to the shorter of two columns.
void RmlOnlineWelcomeScreen::fillMotd()
{
	m_motdIntroRows.beginUpdate();

	// The shipped heading still has its "%d players"; the .wnd formats it with no argument, this with the MOTD's count.
	const Color headingColor = GameSpyColor[GSCOLOR_MOTD_HEADING];
	UnicodeString heading, line;
	heading.format(TheGameText->fetch("MOTD:NumPlayersHeading"), RmlOnlinePlayersOnline());
	while (heading.nextToken(&line, L"\n"))
	{
		if (!line.isEmpty() && line.getCharAt(line.getLength() - 1) == '\r')
			line.removeLastChar();
		line.trim();
		if (line.isEmpty())
			continue;
		MotdLineModel &row = m_motdIntroRows.next();
		row.text = unicodeToUtf8(line);
		row.colorHex = colorHex(headingColor);
		row.isHeading = true;
	}

	std::vector<std::vector<MotdLine> > sections;
	GenOnlineServicesManager *services = GenOnlineServicesManager::GetInstance();
	AsciiString motd = services == nullptr ? AsciiString() : AsciiString(services->GetMOTD().c_str());
	AsciiString aLine;
	while (motd.nextToken(&aLine, "\n"))
	{
		if (!aLine.isEmpty() && aLine.getCharAt(aLine.getLength() - 1) == '\r')
			aLine.removeLastChar();
		aLine.trim();

		Color color = GameSpyColor[GSCOLOR_MOTD];
		if (aLine.startsWith("\\\\"))
		{
			AsciiString rest(aLine.str() + 1);
			aLine = rest;
		}
		else if (aLine.startsWith("\\") && aLine.getLength() > 9)
		{
			const UnsignedByte a = grabUByte(aLine.str() + 1);
			const UnsignedByte r = grabUByte(aLine.str() + 3);
			const UnsignedByte g = grabUByte(aLine.str() + 5);
			const UnsignedByte b = grabUByte(aLine.str() + 7);
			color = GameMakeColor(r, g, b, a);
			AsciiString rest(aLine.str() + 9);
			aLine = rest;
			aLine.trim();
		}
		if (aLine.isEmpty())
			continue;

		std::string title;
		const std::string text(aLine.str());	// the MOTD is UTF-8 already
		if (sectionTitle(text, title))
		{
			sections.push_back(std::vector<MotdLine>());
			MotdLine head = { title, colorHex(color), true };
			sections.back().push_back(head);
			continue;
		}
		MotdLine body = { text, colorHex(color), false };
		if (sections.empty())
		{
			MotdLineModel &row = m_motdIntroRows.next();
			row.text = body.text;
			row.colorHex = body.colorHex;
		}
		else
			sections.back().push_back(body);
	}
	m_motdIntroRows.endUpdate();

	// Each section to the column with fewer rows so far, in the server's order.
	m_motdLeftRows.beginUpdate();
	m_motdRightRows.beginUpdate();
	int leftRows = 0;
	int rightRows = 0;
	for (size_t s = 0; s < sections.size(); ++s)
	{
		const bool left = leftRows <= rightRows;
		RmlGrowOnlyList<MotdLineModel> &column = left ? m_motdLeftRows : m_motdRightRows;
		(left ? leftRows : rightRows) += motdWeight(sections[s]);
		for (size_t l = 0; l < sections[s].size(); ++l)
		{
			MotdLineModel &row = column.next();
			row.text = sections[s][l].text;
			row.colorHex = sections[s][l].colorHex;
			row.isSection = sections[s][l].section;
		}
	}
	m_motdLeftRows.endUpdate();
	m_motdRightRows.endUpdate();
}

// The 12 medallions before the stats reply: each army's name, its rate unknown.
void RmlOnlineWelcomeScreen::fillFactionStats()
{
	m_factionRows.beginUpdate();
	for (int slot = 0; slot < (int)(sizeof(kFactionDisplayOrder) / sizeof(kFactionDisplayOrder[0])); ++slot)
	{
		const int i = kFactionDisplayOrder[slot];
		FactionStatModel &row = m_factionRows.next();
		row.icon = i < kFactionStatIconCount ? kFactionStatIcons[i] : "";
		row.tooltipText = unicodeToUtf8(factionName(i));
		row.percentText = "-";
	}
	m_factionRows.endUpdate();
}

// updateOverallStats(): each army's wins over its own games; the count goes to the tooltip.
void RmlOnlineWelcomeScreen::requestFactionStats()
{
	GenOnlineStatsInterface *statsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (statsInterface == nullptr)
		return;

	const unsigned opening = m_opening;
	statsInterface->GetGlobalStats([opening](GlobalStats stats)
		{
			RmlOnlineWelcomeScreen &screen = instance();
			if (screen.m_opening != opening)
				return;

			screen.m_factionRows.beginUpdate();
			for (int slot = 0; slot < (int)(sizeof(kFactionDisplayOrder) / sizeof(kFactionDisplayOrder[0])); ++slot)
			{
				const int i = kFactionDisplayOrder[slot];
				FactionStatModel &row = screen.m_factionRows.next();
				row.icon = i < kFactionStatIconCount ? kFactionStatIcons[i] : "";
				const Rml::String name = unicodeToUtf8(factionName(i));
				if (stats.matches.find(i) == stats.matches.end())
				{
					row.tooltipText = name;
					row.percentText = "-";
					continue;
				}
				const Int wins = (Int)stats.wins[i];
				const Int matches = (Int)stats.matches[i];
				const Int percent = (Int)(100.f * ((float)wins / (float)(matches > 0 ? matches : 1)));
				row.percentText = numberText("%d%%", percent);
				row.tooltipText = name + numberText(": %d of %d games", wins, matches);
			}
			screen.m_factionRows.endUpdate();

			if (screen.m_modelHandle)
				screen.m_modelHandle.DirtyVariable("faction_stats");
		});
}

// GenOnlinePopulatePlayerInfoWindows() for the local player: the rank, its art and progress, the career record.
void RmlOnlineWelcomeScreen::requestPlayerCard()
{
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineStatsInterface *statsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (auth == nullptr || statsInterface == nullptr)
		return;

	const int64_t localID = auth->GetUserID();
	const unsigned opening = m_opening;
	statsInterface->findPlayerStatsByID(localID, [opening, localID](bool success, PSPlayerStats stats)
		{
			RmlOnlineWelcomeScreen &screen = instance();
			if (!success || TheRankPointValues == nullptr || screen.m_opening != opening)
				return;

			const Int rankPoints = GenOnlineCalculateRank(stats);
			Int currentRank = 0;
			while (currentRank + 1 < MAX_RANKS && rankPoints >= TheRankPointValues->m_ranks[currentRank + 1])
				++currentRank;

			Int numWins = 0;
			Int numLosses = 0;
			Int numDiscons = 0;
			PerGeneralMap::const_iterator it;
			for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
				numWins += it->second;
			for (it = stats.losses.begin(); it != stats.losses.end(); ++it)
				numLosses += it->second;
			for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
				numDiscons += it->second;
			for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
				numDiscons += it->second;
			const Int numGames = numWins + numLosses + numDiscons;

			// The favourite army: the most games; none before the first.
			const PlayerTemplate *favourite = nullptr;
			Int mostGames = 0;
			Int favouriteIndex = 0;
			for (it = stats.games.begin(); it != stats.games.end(); ++it)
			{
				if ((Int)it->second >= mostGames)
				{
					mostGames = it->second;
					favouriteIndex = it->first;
				}
			}
			if (mostGames > 0)
				favourite = ThePlayerTemplateStore->getNthPlayerTemplate(favouriteIndex);

			Model &m = screen.m_model;
			m.rankAtMax = currentRank == MAX_RANKS - 1;
			if (!m.rankAtMax)
			{
				const Real span = (Real)(TheRankPointValues->m_ranks[currentRank + 1] - TheRankPointValues->m_ranks[currentRank]);
				const Real progress = span > 0 ? 100.0f * (Real)(rankPoints - TheRankPointValues->m_ranks[currentRank]) / span : 0.0f;
				char width[16];
				_snprintf_s(width, sizeof(width), _TRUNCATE, "%.3f%%", progress);
				m.rankProgressWidthStyle = width;
			}

			m.rankImageName = rankPoints == 0 || favourite == nullptr ? Rml::String("NewPlayer") : rankImageName(favourite->getBaseSide(), currentRank);
			const Image *general = favourite != nullptr && rankPoints != 0 ? favourite->getGeneralImage() : nullptr;
			m.showFactionImage = general != nullptr;
			m.factionImageName = general != nullptr ? Rml::String(general->getName().str()) : Rml::String();

			AsciiString rankKey;
			rankKey.format("GUI:GSRank%d", currentRank);
			m.rankText = unicodeToUtf8(TheGameText->fetch(rankKey));
			m.nextRankText.clear();
			if (!m.rankAtMax)
			{
				rankKey.format("GUI:GSRank%d", currentRank + 1);
				m.nextRankText = "Next: " + unicodeToUtf8(TheGameText->fetch(rankKey));
			}

			// Ronin @feature 22/11/2026 DX9: the .wnd's "12 (3 QM)" and "5 (Elo: 1000)" as separate numbers.
			m.hasStats = true;
			m.gamesText = numberText("%d", numGames);
			m.quickMatchText = numberText("%d", (Int)stats.elo_num_matches);
			m.eloText = numberText("%d", (Int)stats.elo_rating);
			m.monthlyEloText = numberText("%d", (Int)stats.monthly_elo_rating);
			m.winsText = numberText("%d", numWins);
			m.lossesText = numberText("%d", numLosses);
			UnicodeString winPercent;
			winPercent.format(TheGameText->fetch("GUI:WinPercent"), numGames > 0 ? REAL_TO_INT(numWins / (Real)numGames * 100.0f) : 0);
			m.winPercentText = unicodeToUtf8(winPercent);
			m.bestStreakText = numberText("%d", (Int)stats.maxWinsInARow);
			m.streakLabelText = stats.lossesInARow > 0 ? "Loss streak" : "Win streak";
			m.streakValueText = numberText("%d", (Int)(stats.lossesInARow > stats.winsInARow ? stats.lossesInARow : stats.winsInARow));

			if (screen.m_modelHandle)
			{
				for (const char *name : { "rank_at_max", "rank_progress_width_style", "rank_image_name", "show_faction_image",
						"faction_image_name", "rank_text", "next_rank_text", "has_stats", "games_text", "quick_match_text",
						"elo_text", "monthly_elo_text", "wins_text", "losses_text", "win_percent_text", "best_streak_text",
						"streak_label_text", "streak_value_text" })
					screen.m_modelHandle.DirtyVariable(name);
			}

			screen.fillHonors(stats);

			// GenOnlineQuickMatchMenuInit()'s Elo line, once per fresh log.
			if (screen.m_qmEloPending)
			{
				screen.m_qmEloPending = false;
				UnicodeString elo;
#if defined(WORLD_SERIES_CLIENT)
				elo.format(L"Your current Elo rating is %d (World Series: %d) after %d match(es)", stats.elo_rating, stats.monthly_elo_rating, stats.elo_num_matches);
#else
				elo.format(L"Your current Elo rating is %d (monthly: %d) after %d match(es)", stats.elo_rating, stats.monthly_elo_rating, stats.elo_num_matches);
#endif
				screen.qmLog(unicodeToUtf8(elo), qmNoticeColor());
			}
		}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
}

// Ronin @feature 22/11/2026 DX9: Social's badge; the .wnd wrote "Buddies [3]" on its button.
void RmlOnlineWelcomeScreen::setBuddiesCount(int numNotifications)
{
	m_model.buddiesCount = numNotifications > 0 ? numNotifications : 0;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("buddies_count");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineWelcomeScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

// Ronin @feature 22/11/2026 DX9: the tabs. Home and Quick Match are this screen; Custom Match is GO's lobby (the .wnd's
// nextScreen + pop, which comes back here the same way), and asks to cancel a running search first.
void RmlOnlineWelcomeScreen::onHome(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_model.tab == "home")
		return;
	m_model.tab = "home";
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("tab");
}

void RmlOnlineWelcomeScreen::onQuickMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_model.tab == "qm")
		return;
	m_model.tab = "qm";
	m_qmScrollFrames = 2;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("tab");
}

// Ronin @feature 22/11/2026 DX9: My Persona opens its panel on the left in place of GO's popup; again, back to Home.
void RmlOnlineWelcomeScreen::onMyInfo(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	m_model.tab = m_model.tab == "persona" ? "home" : "persona";
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("tab");
}

// The popup's Delete Account button, which GO relabels LOGOUT for the player's own persona, and its question.
void RmlOnlineWelcomeScreen::onLogOut(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState == QM_MATCH_FOUND)
		return;
	MessageBoxYesNo(UnicodeString(L"Log Out"), UnicodeString(L"Are you sure you want to log out?"),
		&RmlOnlineWelcomeScreen::confirmedLogOut, nullptr);
}

// GenOnlinePlayerInfoOverlay.cpp's messageBoxYes(): the teardown waits for the logout, which cancels the session's requests;
// update() then sees it, as for any teardown.
void RmlOnlineWelcomeScreen::confirmedLogOut()
{
	RmlOnlineWelcomeScreen &screen = instance();
	if (screen.m_qmState == QM_MATCH_FOUND)
		return;
	if (screen.qmBusy())
		screen.qmCancelSearch();
	screen.m_model.tab = "home";
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (auth != nullptr)
	{
		auth->LogoutOfMyAccount([]()
			{
				if (GenOnlineServicesManager::GetInstance() != nullptr)
					GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_LOGOUT);
			});
	}
}

// populateBattleHonors(), Zero Hour's online set: each medal lit or dimmed, its tooltip shown as its title and line.
void RmlOnlineWelcomeScreen::fillHonors(const PSPlayerStats &stats)
{
	Int numGames = 0;
	Int numDiscons = 0;
	Int totalWins = 0;
	PerGeneralMap::const_iterator it;
	for (it = stats.games.begin(); it != stats.games.end(); ++it)
		numGames += it->second;
	for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
		numDiscons += it->second;
	for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
		numDiscons += it->second;
	for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
		totalWins += it->second;
	const Int bits = stats.battleHonors;

	int gained = 0;
	m_honorRows.beginUpdate();
	auto add = [&](const char *image, bool isGained, const char *tooltipKey)
		{
			HonorModel &row = m_honorRows.next();
			row.image = image;
			row.gained = isGained;
			gained += isGained ? 1 : 0;
			// "Air Wing Honor \n Build 20+ aircraft..." - the first line is the title.
			const std::string tooltip = unicodeToUtf8(TheGameText->fetch(tooltipKey));
			const size_t cut = tooltip.find('\n');
			std::string title = cut == std::string::npos ? tooltip : tooltip.substr(0, cut);
			std::string text = cut == std::string::npos ? std::string() : tooltip.substr(cut + 1);
			for (size_t i = 0; i < text.size(); ++i)
				if (text[i] == '\n' || text[i] == '\r')
					text[i] = ' ';
			const size_t titleEnd = title.find_last_not_of(" \t");
			row.title = titleEnd == std::string::npos ? std::string() : title.substr(0, titleEnd + 1);
			const size_t textBegin = text.find_first_not_of(" \t");
			row.text = textBegin == std::string::npos ? std::string() : text.substr(textBegin);
		};

	const bool fairPlay = numGames >= 10 && numDiscons * 10 < numGames;
	add("FairPlay", fairPlay, fairPlay ? "TOOLTIP:BattleHonorFairPlay" : "TOOLTIP:BattleHonorFairPlayDisabled");
	const bool airWing = BitIsSet(bits, BATTLE_HONOR_AIR_WING);
	add("HonorAirWing", airWing, airWing ? "TOOLTIP:BattleHonorAirWing" : "TOOLTIP:BattleHonorAirWingDisabled");
	const bool battleTank = BitIsSet(bits, BATTLE_HONOR_BATTLE_TANK);
	add("HonorBattleTank", battleTank, battleTank ? "TOOLTIP:BattleHonorBattleTank" : "TOOLTIP:BattleHonorBattleTankDisabled");
	const bool apocalypse = BitIsSet(bits, BATTLE_HONOR_APOCALYPSE);
	add("Apocalypse", apocalypse, apocalypse ? "TOOLTIP:BattleHonorApocalypse" : "TOOLTIP:BattleHonorApocalypseDisabled");

	if (BitIsSet(bits, BATTLE_HONOR_BLITZ5))
		add("HonorBlitz5", true, "TOOLTIP:BattleHonorBlitz5");
	else if (BitIsSet(bits, BATTLE_HONOR_BLITZ10))
		add("HonorBlitz10", true, "TOOLTIP:BattleHonorBlitz10");
	else
		add("HonorBlitz10", false, "TOOLTIP:BattleHonorBlitzDisabled");

	// GO's listbox never passed the streak to its tooltip, so it always read "not earned"; this uses the count.
	const Int streak = stats.winsInARow;
	if (streak >= 1000)
		add("HonorStreak_1000", true, "TOOLTIP:BattleHonorStreak1000Online");
	else if (streak >= 500)
		add("HonorStreak_500", true, "TOOLTIP:BattleHonorStreak500Online");
	else if (streak >= 100)
		add("HonorStreak_100", true, "TOOLTIP:BattleHonorStreak100Online");
	else if (streak >= 25)
		add("HonorStreak_G", true, "TOOLTIP:BattleHonorStreak25Online");
	else if (streak >= 10)
		add("HonorStreak_S", true, "TOOLTIP:BattleHonorStreak10Online");
	else if (streak >= 3)
		add("HonorStreak_B", true, "TOOLTIP:BattleHonorStreak3Online");
	else
		add("HonorStreak_B", false, "TOOLTIP:BattleHonorStreakOnlineDisabled");

	if (totalWins >= 10000)
		add("Domination_10000", true, "TOOLTIP:BattleHonorDomination10000Online");
	else if (totalWins >= 1000)
		add("Domination_1000", true, "TOOLTIP:BattleHonorDomination1000Online");
	else if (totalWins >= 500)
		add("Domination_500", true, "TOOLTIP:BattleHonorDomination500Online");
	else if (totalWins >= 100)
		add("Domination_100", true, "TOOLTIP:BattleHonorDomination100Online");
	else
		add("Domination_100", false, "TOOLTIP:BattleHonorDominationOnlineDisabled");

	const bool globalGeneral = BitIsSet(bits, BATTLE_HONOR_GLOBAL_GENERAL);
	add("GlobalGen", globalGeneral, globalGeneral ? "TOOLTIP:BattleHonorGlobalGeneral" : "TOOLTIP:BattleHonorGlobalGeneralDisabled");
	add("OfficersClub", true, "TOOLTIP:BattleHonorOfficersClub");	// GO grants it to everyone (TODO_NGMP_STATS)
	m_honorRows.endUpdate();

	m_model.honorsText = numberText("%d of %d earned", gained, m_honorRows.liveCount());
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("honors");
		m_modelHandle.DirtyVariable("honors_text");
	}
}

void RmlOnlineWelcomeScreen::onCustomMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState == QM_MATCH_FOUND)
		return;
	if (qmBusy())
	{
		MessageBoxYesNo(TheGameText->fetch("GUI:QuickMatch"), UnicodeString(L"Cancel the search and open Custom Match?"),
			&RmlOnlineWelcomeScreen::qmConfirmedToLobby, nullptr);
		return;
	}
	RmlUiScreenRegistry::popTo("Menus/GenOnlineCustomLobby.wnd");
}

void RmlOnlineWelcomeScreen::qmConfirmedToLobby()
{
	RmlOnlineWelcomeScreen &screen = instance();
	if (screen.m_qmState == QM_MATCH_FOUND)
		return;	// a match turned up while the box was open
	if (screen.qmBusy())
		screen.qmCancelSearch();
	RmlUiScreenRegistry::popTo("Menus/GenOnlineCustomLobby.wnd");
}

void RmlOnlineWelcomeScreen::qmConfirmedToMainMenu()
{
	RmlOnlineWelcomeScreen &screen = instance();
	if (screen.m_qmState == QM_MATCH_FOUND)
		return;
	if (screen.qmBusy())
		screen.qmCancelSearch();
	screen.leaveToMainMenu();
}

void RmlOnlineWelcomeScreen::onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	GameSpyToggleOverlay(GSOVERLAY_BUDDY);
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 22/11/2026 DX9: Quick Match, from GenOnlineQuickMatchMenu.cpp. GO's own screen leaves army, colour, ladder,
// max ping and max disconnects disabled ("not supported in GO"); StartMatchmaking() takes the playlist and the maps only.

// GenOnlineQuickMatchMenuInit(): a fresh log, the playlists (the service's), the maps they allow.
void RmlOnlineWelcomeScreen::qmReset()
{
	m_qmState = QM_IDLE;
	m_qmWidenAvailable = false;
	m_qmTimeoutStart = 0;
	m_qmTimeoutMs = kQmLobbyTimeoutMs;
	m_qmCountdownLastSecond = 0;

	delete TheNAT;
	TheNAT = nullptr;
	if (TheGenOnlineGame != nullptr && !TheGenOnlineGame->isGameInProgress())
		TheGenOnlineGame->reset();

	for (size_t i = 0; i < m_model.qmLog.size(); ++i)
		m_model.qmLog[i].used = false;
	m_qmLogCount = 0;
	qmLog("Welcome to QuickMatch. Pick a playlist and your maps, then start matchmaking.", qmNoticeColor());
	qmLog("Special thanks to map makers Tanso, ReLaX, cncHD, Specovik, Mp3, Jundiyy & Bamovich for making quickmatch possible.",
		qmNoticeColor());
	m_qmEloPending = true;

	qmFillOptions();
	qmFillMaps();	// from the cached playlists, until the service answers
	qmSetState(QM_IDLE);

	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking == nullptr)
		return;
	const unsigned opening = m_opening;
	matchmaking->RetrievePlaylists([opening](std::vector<PlaylistEntry> playlists)
		{
			RmlOnlineWelcomeScreen &screen = instance();
			if (screen.m_opening != opening)
				return;

			screen.m_playlistRows.beginUpdate();
			for (size_t i = 0; i < playlists.size(); ++i)
			{
				QmOptionModel &row = screen.m_playlistRows.next();
				row.label = playlists[i].Name;
				row.value = (int)i;
			}
			screen.m_playlistRows.endUpdate();
			// An <option> takes no data-if beside its data-for: a playlist gone since the last opening keeps its row, unnamed.
			for (size_t i = playlists.size(); i < screen.m_model.qmPlaylists.size(); ++i)
				screen.m_model.qmPlaylists[i].label.clear();

			QuickMatchPreferences pref;
			const int saved = pref.getNumPlayers();
			screen.m_model.qmPlaylist = saved >= 0 && saved < (int)playlists.size() ? saved : 0;
			screen.qmFillMaps();
			screen.qmSetState(screen.m_qmState);
			if (screen.m_modelHandle)
			{
				screen.m_modelHandle.DirtyVariable("qm_playlists");
				screen.m_modelHandle.DirtyVariable("qm_playlist");
			}
		});
}

// populateQuickMatchMapSelectListbox(): the playlist's maps this install has, big enough for its player count.
void RmlOnlineWelcomeScreen::qmFillMaps()
{
	m_mapRows.beginUpdate();
	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking != nullptr && TheMapCache != nullptr)
	{
		const PlaylistEntry playlist = matchmaking->GetCachedPlaylistFromIndex(m_model.qmPlaylist);
		if (qmValidPlaylist(playlist))
		{
			QuickMatchPreferences pref;
			for (size_t position = 0; position < playlist.Maps.size(); ++position)
			{
				const PlaylistMapEntry &entry = playlist.Maps[position];
				AsciiString path;
				if (entry.Custom)
					path.format("%smaps\\%s\\%s.map", TheGlobalData->getPath_UserData().str(), entry.Path.c_str(), entry.Path.c_str());
				else
					path.format("maps\\%s\\%s.map", entry.Path.c_str(), entry.Path.c_str());
				path.toLower();

				const MapMetaData *md = TheMapCache->findMap(path);
				if (md == nullptr || md->m_numPlayers < playlist.MinPlayers)
					continue;
				QmMapModel &row = m_mapRows.next();
				row.file = md->m_fileName.str();
				row.name = unicodeToUtf8(md->m_displayName);
				row.players = numberText("%d players", md->m_numPlayers);
				row.selected = pref.isMapSelected(path) == TRUE;
				row.index = m_mapRows.liveCount() - 1;
				row.position = (int)position;
			}
		}
	}
	m_mapRows.endUpdate();
	qmUpdateMapsText();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_maps");
}

// populateQMSideComboBox(), populateQMColorComboBox() and the max disconnects list, from the saved choices.
void RmlOnlineWelcomeScreen::qmFillOptions()
{
	QuickMatchPreferences pref;

	// Random, then each playable side once, without the generals that start locked.
	m_sideRows.beginUpdate();
	QmOptionModel &random = m_sideRows.next();
	random.label = unicodeToUtf8(TheGameText->fetch("GUI:Random"));
	random.value = PLAYERTEMPLATE_RANDOM;
	m_model.qmSide = PLAYERTEMPLATE_RANDOM;
	std::set<AsciiString> seenSides;
	for (Int c = 0; ThePlayerTemplateStore != nullptr && c < ThePlayerTemplateStore->getPlayerTemplateCount(); ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (fac == nullptr || fac->getStartingBuilding().isEmpty())
			continue;
		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;
		const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName(fac->getName()) : nullptr;
		if (general != nullptr && !general->isStartingEnabled())
			continue;
		seenSides.insert(side);
		QmOptionModel &row = m_sideRows.next();
		row.label = unicodeToUtf8(TheGameText->fetch(side));
		row.value = c;
		if (c == pref.getSide())
			m_model.qmSide = c;
	}
	m_sideRows.endUpdate();

	// The ??? random colour, then the multiplayer colours, each in its own colour; the saved choice is a list position.
	m_colorRows.beginUpdate();
	const Int numColors = TheMultiplayerSettings ? TheMultiplayerSettings->getNumColors() : 0;
	MultiplayerColorDefinition *def = TheMultiplayerSettings ? TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM) : nullptr;
	QmOptionModel &unknown = m_colorRows.next();
	unknown.label = unicodeToUtf8(TheGameText->fetch("GUI:???"));
	unknown.colorHex = def ? colorHex(def->getColor()) : Rml::String();
	unknown.value = 0;
	for (Int c = 0; c < numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (def == nullptr)
			continue;
		QmOptionModel &row = m_colorRows.next();
		row.label = unicodeToUtf8(TheGameText->fetch(def->getTooltipName().str()));
		row.colorHex = colorHex(def->getColor());
		row.value = m_colorRows.liveCount() - 1;
	}
	m_colorRows.endUpdate();
	m_model.qmColor = pref.getColor() >= 0 && pref.getColor() < m_colorRows.liveCount() ? pref.getColor() : 0;

	// Any, 5, 10, 25, 50.
	static const Int kMaxDisconnects[] = { 0, 5, 10, 25, 50 };
	m_disconnectRows.beginUpdate();
	for (int i = 0; i < (int)(sizeof(kMaxDisconnects) / sizeof(kMaxDisconnects[0])); ++i)
	{
		QmOptionModel &row = m_disconnectRows.next();
		row.label = i == 0 ? unicodeToUtf8(TheGameText->fetch("GUI:Any")) : numberText("%d", kMaxDisconnects[i]);
		row.value = i;
	}
	m_disconnectRows.endUpdate();
	m_model.qmMaxDisconnects = pref.getMaxDisconnects() > 0 && pref.getMaxDisconnects() < m_disconnectRows.liveCount()
		? pref.getMaxDisconnects() : 0;

	if (m_modelHandle)
	{
		for (const char *name : { "qm_sides", "qm_side", "qm_colors", "qm_color", "qm_disconnects", "qm_max_disconnects" })
			m_modelHandle.DirtyVariable(name);
	}
}

// selectedPlaylistLocksArmy(): a playlist that plays random armies shows Random and keeps the list shut.
void RmlOnlineWelcomeScreen::qmUpdateArmy()
{
	bool allowed = false;
	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking != nullptr)
	{
		const PlaylistEntry playlist = matchmaking->GetCachedPlaylistFromIndex(m_model.qmPlaylist);
		allowed = qmValidPlaylist(playlist) && playlist.AllowArmySelection;
	}
	m_model.qmArmyEnabled = allowed && m_qmState == QM_IDLE;
	if (!allowed)
		m_model.qmSide = PLAYERTEMPLATE_RANDOM;
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("qm_army_enabled");
		m_modelHandle.DirtyVariable("qm_side");
	}
}

// saveQuickMatchOptions(): the playlist, the map choices, and the setup lists as GO writes them.
void RmlOnlineWelcomeScreen::qmSavePrefs()
{
	QuickMatchPreferences pref;
	pref.setNumPlayers(m_model.qmPlaylist);
	for (int i = 0; i < m_mapRows.liveCount(); ++i)
		pref.setMapSelected(AsciiString(m_model.qmMaps[i].file.c_str()), m_model.qmMaps[i].selected ? TRUE : FALSE);
	if (m_model.qmArmyEnabled)
		pref.setSide(m_model.qmSide > 0 ? m_model.qmSide : 0);	// GO's max(0, side): Random is saved as 0
	pref.setColor(m_model.qmColor);
	pref.setMaxDisconnects(m_model.qmMaxDisconnects);
	pref.write();
}

// GadgetListBoxAddEntryText() on the progress box: the log only grows while the screen is open.
void RmlOnlineWelcomeScreen::qmLog(const Rml::String &text, int color)
{
	if (m_qmLogCount < (int)m_model.qmLog.size())
	{
		QmLogLineModel &row = m_model.qmLog[m_qmLogCount];
		row.text = text;
		row.colorHex = colorHex(color);
		row.used = true;
	}
	else
	{
		QmLogLineModel row;
		row.text = text;
		row.colorHex = colorHex(color);
		m_model.qmLog.push_back(row);
	}
	++m_qmLogCount;
	m_qmScrollFrames = 2;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_log");
}

// The .wnd's button juggling as one state: Start or Cancel, Widen, the setup, and the way out.
void RmlOnlineWelcomeScreen::qmSetState(QmState state)
{
	m_qmState = state;
	bool anySelected = false;
	for (int i = 0; i < m_mapRows.liveCount() && !anySelected; ++i)
		anySelected = m_model.qmMaps[i].selected;

	m_model.qmSearching = state == QM_SEARCHING || state == QM_MATCH_FOUND;
	m_model.qmStartEnabled = state == QM_IDLE && anySelected;
	m_model.qmStopEnabled = state == QM_SEARCHING;
	m_model.qmWidenEnabled = state == QM_SEARCHING && m_qmWidenAvailable;
	m_model.qmSetupEnabled = state == QM_IDLE;
	m_model.qmLocked = state == QM_MATCH_FOUND;
	if (m_modelHandle)
	{
		for (const char *name : { "qm_searching", "qm_start_enabled", "qm_stop_enabled", "qm_widen_enabled", "qm_setup_enabled",
				"qm_locked" })
			m_modelHandle.DirtyVariable(name);
	}
	qmUpdateArmy();
}

// ButtonStop.
void RmlOnlineWelcomeScreen::qmCancelSearch()
{
	m_qmTimeoutStart = 0;
	m_qmCountdownLastSecond = 0;
	m_qmWidenAvailable = false;
	qmLog(unicodeToUtf8(TheGameText->fetch("GUI:QMAborted")), GameSpyColor[GSCOLOR_DEFAULT]);
	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking != nullptr)
		matchmaking->CancelMatchmaking();
	qmSetState(QM_IDLE);
}

// GenOnlineQuickMatchMenuInit()'s lobby callbacks, each one slot GO's screens take over in turn; dropped again in hide().
void RmlOnlineWelcomeScreen::qmRegisterCallbacks()
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr)
		return;

	lobby->RegisterForCannotConnectToLobbyCallback([]()
		{
			// don't cancel: racing a server-issued requeue could unregister us from its bucket
			RmlOnlineWelcomeScreen &screen = instance();
			screen.qmLog("Could not connect to a player, waiting for the matchmaker...", GameSpyColor[GSCOLOR_DEFAULT]);
			screen.m_qmTimeoutStart = 0;
			screen.m_qmCountdownLastSecond = 0;
			screen.qmSetState(QM_SEARCHING);
		});

	lobby->RegisterForMatchmakingMessageCallback([](std::string message)
		{
			instance().qmLog(message, GameSpyColor[GSCOLOR_DEFAULT]);	// UTF-8 already
		});

	lobby->RegisterForMatchmakingMatchFoundCallback([]()
		{
			RmlOnlineWelcomeScreen &screen = instance();
			screen.m_qmTimeoutMs = kQmLobbyTimeoutMs;
			screen.m_qmTimeoutStart = ::GetTickCount();
			screen.m_qmCountdownLastSecond = 0;
			screen.qmSetState(QM_MATCH_FOUND);
			if (TheAudio)
			{
				AudioEventRTS sound("GUICommunicatorOpen");
				TheAudio->addAudioEvent(&sound);
			}
		});

	lobby->RegisterForMatchmakingRequeueCallback([]()
		{
			RmlOnlineWelcomeScreen &screen = instance();
			screen.m_qmTimeoutStart = 0;
			screen.m_qmTimeoutMs = kQmLobbyTimeoutMs;
			screen.m_qmCountdownLastSecond = 0;
			screen.m_qmWidenAvailable = true;
			screen.qmSetState(QM_SEARCHING);
		});

	// The service took the player out of the queue: back to the start, as Stop.
	lobby->RegisterForMatchmakingLeftCallback([]()
		{
			RmlOnlineWelcomeScreen &screen = instance();
			screen.m_qmTimeoutStart = 0;
			screen.m_qmTimeoutMs = kQmLobbyTimeoutMs;
			screen.m_qmCountdownLastSecond = 0;
			screen.m_qmWidenAvailable = false;
			screen.qmSetState(QM_IDLE);
		});

	lobby->RegisterForMatchmakingSetupProgressCallback([](int timeoutMs, int countdownMs)
		{
			// Mirror the service-owned countdown; older services don't send its length, so a short timeout means the countdown.
			RmlOnlineWelcomeScreen &screen = instance();
			screen.m_qmTimeoutMs = timeoutMs;
			screen.m_qmTimeoutStart = ::GetTickCount();
			if (countdownMs < 0)
				countdownMs = timeoutMs < kQmLobbyTimeoutMs ? kQmDefaultCountdownMs : 0;
			screen.m_qmCountdownMs = countdownMs;
			screen.m_qmCountdownLastSecond = countdownMs > 0 ? (countdownMs + 999) / 1000 : 0;
		});

	lobby->RegisterForMatchmakingStartGameCallback([]()
		{
			RmlOnlineWelcomeScreen &screen = instance();
			screen.m_qmTimeoutStart = 0;
			screen.m_qmCountdownLastSecond = 0;
			if (TheGenOnlineGame == nullptr)
				return;

			// Everyone has the map: Quick Match allows no custom maps and no transfers.
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				GameSlot *slot = TheGenOnlineGame->getSlot(i);
				if (slot != nullptr)
					slot->setMapAvailability(TRUE);
			}

			GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			GenOnlineGame *myGame = lobby == nullptr ? nullptr : lobby->GetCurrentGame();
			if (lobby == nullptr || myGame == nullptr || !myGame->isInGame())
				return;

			GameSpyCloseOverlay(GSOVERLAY_BUDDY);
			GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);
			*TheGenOnlineGame = *myGame;
			TheGenOnlineGame->startGame(0);
		});

	lobby->RegisterForJoinLobbyCallback([](EJoinLobbyResult result)
		{
			RmlOnlineWelcomeScreen &screen = instance();
			GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (result != EJoinLobbyResult::JoinLobbyResult_Success || lobby == nullptr || !lobby->IsInLobby())
			{
				screen.qmLog("Could not join the match lobby, waiting for the matchmaker...", GameSpyColor[GSCOLOR_DEFAULT]);
				screen.m_qmTimeoutStart = 0;
				screen.m_qmCountdownLastSecond = 0;
				screen.qmSetState(QM_SEARCHING);
				return;
			}
			if (TheGenOnlineGame == nullptr)
			{
				TheGenOnlineGame = new GenOnlineGame();
				TheGenOnlineGame->markGameAsQM();
			}
			lobby->UpdateRoomDataCache([](bool) {});
		});
}

void RmlOnlineWelcomeScreen::qmDeregisterCallbacks()
{
	GenOnlineLobbyInterface *lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (lobby == nullptr)
		return;
	lobby->DeregisterForMatchmakingMessageCallback();
	lobby->DeRegisterForMatchmakingMatchFoundCallback();
	lobby->DeregisterForMatchmakingRequeueCallback();
	lobby->DeregisterForMatchmakingLeftCallback();
	lobby->DeregisterForMatchmakingSetupProgressCallback();
	lobby->DeregisterForMatchmakingStartGameCallback();
	lobby->DeregisterForJoinLobbyCallback();
	lobby->DeregisterForCannotConnectToLobbyCallback();
}

// ButtonStart: the 2003 resolution lock (off unless QMResLock is set), the playlist's minimum of maps, then the service.
void RmlOnlineWelcomeScreen::onQmStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState != QM_IDLE)
		return;
	GameSpyMiscPreferences mPref;
	if ((TheDisplay->getWidth() != 800 || TheDisplay->getHeight() != 600) && mPref.getQuickMatchResLocked())
	{
		GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:QuickMatch800x600"));
		return;
	}

	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking == nullptr)
		return;
	const PlaylistEntry playlist = matchmaking->GetCachedPlaylistFromIndex(m_model.qmPlaylist);
	if (!qmValidPlaylist(playlist))
	{
		qmLog("Failed to start matchmaking.", GameSpyColor[GSCOLOR_DEFAULT]);
		return;
	}
	std::vector<int> positions;
	for (int i = 0; i < m_mapRows.liveCount(); ++i)
		if (m_model.qmMaps[i].selected)
			positions.push_back(m_model.qmMaps[i].position);
	if ((int)positions.size() < playlist.MinSelectedMaps)
	{
		qmLog(numberText("You must select at least %d maps.", playlist.MinSelectedMaps), GameSpyColor[GSCOLOR_DEFAULT]);
		return;
	}

	qmSavePrefs();
	m_qmWidenAvailable = false;
	qmSetState(QM_STARTING);
	const unsigned opening = m_opening;
	matchmaking->StartMatchmaking(playlist.PlaylistID, positions, [opening](bool success)
		{
			RmlOnlineWelcomeScreen &screen = instance();
			if (screen.m_opening != opening || screen.m_qmState != QM_STARTING)
				return;	// closed, or cancelled meanwhile
			if (success)
			{
				screen.m_qmWidenAvailable = true;
				screen.qmSetState(QM_SEARCHING);
			}
			else
			{
				screen.qmLog("Failed to start matchmaking.", GameSpyColor[GSCOLOR_DEFAULT]);
				screen.qmSetState(QM_IDLE);
			}
		});
}

void RmlOnlineWelcomeScreen::onQmStop(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState == QM_SEARCHING)
		qmCancelSearch();
}

void RmlOnlineWelcomeScreen::onQmWiden(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState != QM_SEARCHING || !m_qmWidenAvailable)
		return;
	qmLog(unicodeToUtf8(TheGameText->fetch("QM:WIDENINGSEARCH")), GameSpyColor[GSCOLOR_DEFAULT]);
	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking != nullptr)
		matchmaking->WidenSearch();
	m_qmWidenAvailable = false;
	qmSetState(QM_SEARCHING);
}

// The select's own value, as RmlLanGameSetupScreen reads its dropdowns.
void RmlOnlineWelcomeScreen::onQmPlaylistChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	if (m_qmState != QM_IDLE)
		return;
	const Rml::String value = ev.GetParameter<Rml::String>("value", "");
	if (value.empty())
		return;
	const int playlist = atoi(value.c_str());
	if (playlist == m_model.qmPlaylist)
		return;
	m_model.qmPlaylist = playlist;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_playlist");
	qmFillMaps();
	qmSetState(QM_IDLE);
	qmSavePrefs();
}

// The army, for a playlist that lets the player choose it.
void RmlOnlineWelcomeScreen::onQmSideChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	if (!m_model.qmArmyEnabled)
		return;
	const Rml::String value = ev.GetParameter<Rml::String>("value", "");
	if (value.empty())
		return;
	m_model.qmSide = atoi(value.c_str());
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_side");
	qmSavePrefs();
}

void RmlOnlineWelcomeScreen::onQmMapToggle(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (m_qmState != QM_IDLE || args.empty())
		return;
	const int index = args[0].Get<int>();
	if (index < 0 || index >= m_mapRows.liveCount())
		return;
	m_model.qmMaps[index].selected = !m_model.qmMaps[index].selected;
	qmMapsChanged();
}

void RmlOnlineWelcomeScreen::onQmAllMaps(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState != QM_IDLE)
		return;
	for (int i = 0; i < m_mapRows.liveCount(); ++i)
		m_model.qmMaps[i].selected = true;
	qmMapsChanged();
}

void RmlOnlineWelcomeScreen::onQmNoMaps(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_qmState != QM_IDLE)
		return;
	for (int i = 0; i < m_mapRows.liveCount(); ++i)
		m_model.qmMaps[i].selected = false;
	qmMapsChanged();
}

// A map choice changed: the count, Start, the saved choices.
void RmlOnlineWelcomeScreen::qmMapsChanged()
{
	qmUpdateMapsText();
	qmSetState(QM_IDLE);
	qmSavePrefs();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_maps");
}

// "3 of 12 selected - at least 2": the playlist's minimum, which Start enforces.
void RmlOnlineWelcomeScreen::qmUpdateMapsText()
{
	int minSelected = 0;
	GenOnlineMatchmakingInterface *matchmaking = GenOnlineServicesManager::GetInterface<GenOnlineMatchmakingInterface>();
	if (matchmaking != nullptr)
	{
		const PlaylistEntry playlist = matchmaking->GetCachedPlaylistFromIndex(m_model.qmPlaylist);
		if (qmValidPlaylist(playlist))
			minSelected = playlist.MinSelectedMaps;
	}
	int selected = 0;
	for (int i = 0; i < m_mapRows.liveCount(); ++i)
		selected += m_model.qmMaps[i].selected ? 1 : 0;
	char text[96];
	if (minSelected > 0)
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%d of %d selected - at least %d", selected, m_mapRows.liveCount(), minSelected);
	else
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%d of %d selected", selected, m_mapRows.liveCount());
	m_model.qmMapsText = text;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("qm_maps_text");
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOnlineWelcomeScreen()
{
	if (!TheRmlUiManager)
		return;
	RmlOnlineWelcomeScreen &screen = RmlOnlineWelcomeScreen::instance();
	if (screen.isVisible())
		return;	// the shell's bringForward() after its runInit()
	screen.load(TheRmlUiManager->getContext());
	screen.show();
}

void CloseRmlOnlineWelcomeScreen()
{
	if (RmlOnlineWelcomeScreen::instance().isVisible())
		RmlOnlineWelcomeScreen::instance().hide();
}
