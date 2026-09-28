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

#include "W3DDevice/GameClient/RmlUi/RmlOnlineWelcomeScreen.h"

#include "Common/GameEngine.h"
#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeData.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same conversion RmlLanLobbyScreen.cpp/RmlScoreScreen.cpp/etc. each keep as a private helper.
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
		Rml::StructHandle<MotdLineModel> motdHandle = constructor.RegisterStruct<MotdLineModel>();
		if (motdHandle)
		{
			motdHandle.RegisterMember("text", &MotdLineModel::text);
			motdHandle.RegisterMember("is_heading", &MotdLineModel::isHeading);
			motdHandle.RegisterMember("used", &MotdLineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<MotdLineModel>>();

		Rml::StructHandle<FactionStatModel> factionHandle = constructor.RegisterStruct<FactionStatModel>();
		if (factionHandle)
		{
			factionHandle.RegisterMember("side", &FactionStatModel::side);
			factionHandle.RegisterMember("text", &FactionStatModel::text);
			factionHandle.RegisterMember("used", &FactionStatModel::used);
		}
		constructor.RegisterArray<Rml::Vector<FactionStatModel>>();

		constructor.Bind("title", &m_model.title);
		constructor.Bind("num_players_text", &m_model.numPlayersText);
		constructor.Bind("buddies_button_text", &m_model.buddiesButtonText);
		constructor.Bind("motd_lines", &m_model.motdLines);
		constructor.Bind("faction_stats", &m_model.factionStats);

		constructor.BindEventCallback("back", &RmlOnlineWelcomeScreen::onBackPressed, this);
		constructor.BindEventCallback("options", &RmlOnlineWelcomeScreen::onOptions, this);
		constructor.BindEventCallback("quick_match", &RmlOnlineWelcomeScreen::onQuickMatch, this);
		constructor.BindEventCallback("my_info", &RmlOnlineWelcomeScreen::onMyInfo, this);
		constructor.BindEventCallback("custom_match", &RmlOnlineWelcomeScreen::onCustomMatch, this);
		constructor.BindEventCallback("buddies", &RmlOnlineWelcomeScreen::onBuddies, this);
		constructor.BindEventCallback("ladder", &RmlOnlineWelcomeScreen::onLadder, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/OnlineWelcome.rml");
}

//-------------------------------------------------------------------------------------------------
// g_onlineWelcomeNotificationsChangedHook target.
static void onOnlineWelcomeNotificationsDelivered(int numNotifications)
{
	RmlOnlineWelcomeScreen::instance().onNotificationsChanged(numNotifications);
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineWelcomeScreen::show()
{
	if (!m_document)
		return;

	// Mirrors WOLWelcomeMenuInit()'s live GENERALS_ONLINE body: title, MOTD, num-players text, win
	// stats, and the initial buddy notification count -- see OnlineWelcomeData.h.
	m_model.title = unicodeToUtf8(OnlineWelcomeData::buildWelcomeTitle());
	m_model.numPlayersText = unicodeToUtf8(OnlineWelcomeData::buildNumPlayersOnlineText(1));

	m_motdRows.beginUpdate();
	for (const OnlineWelcomeMotdLine &line : OnlineWelcomeData::buildMotdLines())
	{
		MotdLineModel &row = m_motdRows.next();
		row.text = unicodeToUtf8(line.text);
		row.isHeading = line.isHeading == TRUE;
	}
	m_motdRows.endUpdate();

	m_factionRows.beginUpdate();
	m_factionRows.endUpdate(); // filled in asynchronously below once GetGlobalStats() replies

	Int initialNotifications = OnlineWelcomeData::getCurrentNotificationCount();
	m_model.buddiesButtonText = unicodeToUtf8(OnlineWelcomeData::buildBuddiesButtonText(initialNotifications));

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	g_onlineWelcomeNotificationsChangedHook = &onOnlineWelcomeNotificationsDelivered;
	OnlineWelcomeData::registerNotificationsHook();

	OnlineWelcomeData::requestFactionWinStats([this](std::vector<OnlineWelcomeFactionStat> stats)
		{
			m_factionRows.beginUpdate();
			for (const OnlineWelcomeFactionStat &stat : stats)
			{
				FactionStatModel &row = m_factionRows.next();
				row.side = stat.side.str();
				row.text = unicodeToUtf8(stat.text);
			}
			m_factionRows.endUpdate();

			if (m_modelHandle)
				m_modelHandle.DirtyVariable("faction_stats");
		});

	m_document->Show();
}

void RmlOnlineWelcomeScreen::hide()
{
	if (m_document)
		m_document->Hide();

	if (g_onlineWelcomeNotificationsChangedHook == &onOnlineWelcomeNotificationsDelivered)
		g_onlineWelcomeNotificationsChangedHook = nullptr;
}

bool RmlOnlineWelcomeScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOnlineWelcomeScreen::onBack()
{
	// Mirrors buttonBackID's live GENERALS_ONLINE body exactly (see OnlineWelcomeActions.h), but pops
	// directly instead of the .wnd's deferred fade-then-pop (same precedent as
	// RmlOnlineLoginScreen::onLoginSucceeded()/RmlLanLobbyScreen::onBack()).
	OnlineWelcomeActions::requestLogout();
	TheShell->pop();
}

void RmlOnlineWelcomeScreen::update()
{
	// WOLWelcomeMenuUpdate() never runs for a registry-routed screen (see header comment); this is
	// its replacement for the pending-full-teardown branch.
	if (OnlineWelcomeActions::consumePendingFullTeardown())
	{
		TheShell->pop();
		TearDownGeneralsOnline();
	}
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineWelcomeScreen::onNotificationsChanged(int numNotifications)
{
	m_model.buddiesButtonText = unicodeToUtf8(OnlineWelcomeData::buildBuddiesButtonText(numNotifications));
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("buddies_button_text");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineWelcomeScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

void RmlOnlineWelcomeScreen::onOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineWelcomeActions::openOptions();
}

void RmlOnlineWelcomeScreen::onQuickMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors buttonQuickMatchID's handler: push directly instead of the .wnd's deferred
	// nextScreen/pop (same precedent as RmlLanLobbyScreen::onDirectConnect()).
	if (OnlineWelcomeActions::canStartQuickMatch())
		TheShell->push("Menus/WOLQuickMatchMenu.wnd");
}

void RmlOnlineWelcomeScreen::onMyInfo(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineWelcomeActions::openMyInfo();
}

void RmlOnlineWelcomeScreen::onCustomMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors buttonLobbyID's handler: push directly instead of the .wnd's deferred nextScreen/pop.
	TheShell->push("Menus/WOLCustomLobby.wnd");
}

void RmlOnlineWelcomeScreen::onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineWelcomeActions::toggleBuddiesOverlay();
}

void RmlOnlineWelcomeScreen::onLadder(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	TheShell->push("Menus/WOLLadderScreen.wnd");
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOnlineWelcomeScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlOnlineWelcomeScreen::instance());
}

void CloseRmlOnlineWelcomeScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlOnlineWelcomeScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
