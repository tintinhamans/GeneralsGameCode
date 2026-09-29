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

#include "W3DDevice/GameClient/RmlUi/RmlPlayerInfoScreen.h"

#include "Common/BattleHonors.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
// TheSuperHackers @fix RmlPlayerInfoScreen: GameNetwork/GameSpyOverlay.h + NGMP_interfaces.h pull in
// winsock2.h; combined with <windows.h> below (needed for WideCharToMultiByte) that conflicts with
// the old winsock.h GameEngineDevice's PCH already carries (C3646/C2011 in ws2ipdef.h) -- the same
// ordering PreRTS.h normally guards for GameEngine .cpp files, which GameEngineDevice files don't
// use. So this screen doesn't include either header itself: IsLookingAtLocalPlayer() and
// ClosePlayerInfoOverlay() (PlayerStatsData.h/.cpp, a GameEngine .cpp that already includes them
// safely) do that work instead.
#include "GameNetwork/GameSpy/PersistentStorageDefs.h" // PopulatePlayerInfoWindows()
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

namespace
{
	// Same conversion as RmlOnlineWelcomeScreen.cpp/RmlQuitMenuScreen.cpp.
	Rml::String unicodeToUtf8(const UnicodeString &str)
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

	// The Logout confirmation Yes callback: GameWinMsgBoxFunc is a plain void(*)(), can't bind the
	// instance, so this is a free function the same way RmlQuitMenuScreen.cpp's
	// quitConfirmedCallback() is. Mirrors PopupPlayerInfo.cpp's messageBoxYes() exactly.
	void rmlPlayerInfoLogoutConfirmed()
	{
		PerformPlayerLogout();
	}
}

//-------------------------------------------------------------------------------------------------
RmlPlayerInfoScreen &RmlPlayerInfoScreen::instance()
{
	static RmlPlayerInfoScreen s_screen;
	return s_screen;
}

void RmlPlayerInfoScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("playerinfo");
	if (constructor)
	{
		Rml::StructHandle<BattleHonorRowModel> honorHandle = constructor.RegisterStruct<BattleHonorRowModel>();
		if (honorHandle)
		{
			honorHandle.RegisterMember("image_name", &BattleHonorRowModel::imageName);
			honorHandle.RegisterMember("tooltip_text", &BattleHonorRowModel::tooltipText);
			honorHandle.RegisterMember("count_text", &BattleHonorRowModel::countText);
			honorHandle.RegisterMember("has_count", &BattleHonorRowModel::hasCount);
			honorHandle.RegisterMember("enabled", &BattleHonorRowModel::enabled);
			honorHandle.RegisterMember("used", &BattleHonorRowModel::used);
		}
		constructor.RegisterArray<Rml::Vector<BattleHonorRowModel>>();

		constructor.Bind("title", &m_model.title);
		constructor.Bind("stats_loading", &m_model.statsLoading);
		constructor.Bind("show_logout", &m_model.showLogout);

		constructor.Bind("games_played_text", &m_model.gamesPlayedText);
		constructor.Bind("wins_text", &m_model.winsText);
		constructor.Bind("losses_text", &m_model.lossesText);
		constructor.Bind("disconnects_label_text", &m_model.disconnectsLabelText);
		constructor.Bind("disconnects_value_text", &m_model.disconnectsValueText);
		constructor.Bind("best_streak_text", &m_model.bestStreakText);
		constructor.Bind("streak_label_text", &m_model.streakLabelText);
		constructor.Bind("streak_value_text", &m_model.streakValueText);
		constructor.Bind("total_kills_text", &m_model.totalKillsText);
		constructor.Bind("total_deaths_text", &m_model.totalDeathsText);
		constructor.Bind("total_built_text", &m_model.totalBuiltText);
		constructor.Bind("buildings_killed_text", &m_model.buildingsKilledText);
		constructor.Bind("buildings_lost_text", &m_model.buildingsLostText);
		constructor.Bind("buildings_built_text", &m_model.buildingsBuiltText);
		constructor.Bind("win_percent_text", &m_model.winPercentText);

		constructor.Bind("rank_at_max", &m_model.rankAtMax);
		constructor.Bind("rank_progress_width_style", &m_model.rankProgressWidthStyle);
		constructor.Bind("rank_image_name", &m_model.rankImageName);
		constructor.Bind("show_faction_image", &m_model.showFactionImage);
		constructor.Bind("faction_image_name", &m_model.factionImageName);
		constructor.Bind("rank_text", &m_model.rankText);

		constructor.Bind("battle_honors", &m_model.battleHonors);

		constructor.BindEventCallback("close", &RmlPlayerInfoScreen::onClose, this);
		constructor.BindEventCallback("logout", &RmlPlayerInfoScreen::onLogout, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/PlayerInfo.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlPlayerInfoScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	// Same LOGOUT-relabeled-ButtonDeleteAccount visibility rule GameSpyPlayerInfoOverlayInit() uses.
	m_model.showLogout = IsLookingAtLocalPlayer() == TRUE;
	m_model.statsLoading = true;
	m_model.title = unicodeToUtf8(TheGameText->fetch("GUI:FetchingPlayerInfo"));

	m_honorRows.beginUpdate();
	m_honorRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	m_playerStatsConnection = PlayerStatsSignals::lookAtPlayerUpdated().connect([this](const PlayerStatsData &data) { onPlayerStatsUpdated(data); });

	// No GameWindow parent exists for this popup (unlike the .wnd path's GameSpyPlayerInfoOverlayInit()),
	// so this screen kicks off the same async stats fetch PopulatePlayerInfoWindows() itself; the reply
	// lambda fires PlayerStatsSignals::lookAtPlayerUpdated regardless of whose stats these are (see PlayerStatsData.h).
	PopulatePlayerInfoWindows(AsciiString("PopupPlayerInfo.wnd"));

	m_document->Show();
}

void RmlPlayerInfoScreen::close()
{
	m_playerStatsConnection.disconnect();

	if (m_document)
		m_document->Hide();
}

bool RmlPlayerInfoScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void RmlPlayerInfoScreen::onPlayerStatsUpdated(const PlayerStatsData &data)
{
	m_model.statsLoading = FALSE == data.weHaveStats;
	m_model.title = unicodeToUtf8(data.playerStatisticsLabelText);

	m_model.gamesPlayedText = unicodeToUtf8(data.gamesPlayedText);
	m_model.winsText = unicodeToUtf8(data.winsText);
	m_model.lossesText = unicodeToUtf8(data.lossesText);
#if defined(GENERALS_ONLINE)
	m_model.disconnectsLabelText = unicodeToUtf8(data.disconnectsLabelText);
#endif
	m_model.disconnectsValueText = unicodeToUtf8(data.disconnectsValueText);
	m_model.bestStreakText = unicodeToUtf8(data.bestStreakText);
	m_model.streakLabelText = TheGameText ? unicodeToUtf8(TheGameText->fetch(data.streakLabelKey)) : Rml::String();
	m_model.streakValueText = unicodeToUtf8(data.streakValueText);
	m_model.totalKillsText = unicodeToUtf8(data.totalKillsText);
	m_model.totalDeathsText = unicodeToUtf8(data.totalDeathsText);
	m_model.totalBuiltText = unicodeToUtf8(data.totalBuiltText);
	m_model.buildingsKilledText = unicodeToUtf8(data.buildingsKilledText);
	m_model.buildingsLostText = unicodeToUtf8(data.buildingsLostText);
	m_model.buildingsBuiltText = unicodeToUtf8(data.buildingsBuiltText);
	m_model.winPercentText = unicodeToUtf8(data.winPercentText);

	m_model.rankAtMax = data.rankAtMax == TRUE;
	if (!data.rankAtMax)
	{
		char buf[16];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f%%", data.rankProgressPercent);
		m_model.rankProgressWidthStyle = buf;
	}
	m_model.rankImageName = data.rankImageName.str();
	m_model.showFactionImage = data.showFactionImage == TRUE;
	if (data.showFactionImage)
		m_model.factionImageName = data.factionImageName.str();
	m_model.rankText = unicodeToUtf8(data.rankText);

	// Same 9 badges/order/thresholds as PopupPlayerInfo.cpp's populateBattleHonors() (see
	// PlayerStatsData.h); tooltipKey resolved to display text here, same pattern OnlineWelcome.rml's
	// stat.tooltip_text/LanLobby.rml's player.tooltip use.
	m_honorRows.beginUpdate();
	for (const BattleHonorRow &row : BuildBattleHonorRows(data.stats))
	{
		BattleHonorRowModel &m = m_honorRows.next();
		m.imageName = row.imageName.str();
		m.tooltipText = TheGameText ? unicodeToUtf8(TheGameText->fetch(row.tooltipKey)) : Rml::String();
		m.countText = unicodeToUtf8(row.countText);
		m.hasCount = !row.countText.isEmpty();
		m.enabled = row.enabled == TRUE;
	}
	m_honorRows.endUpdate();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

//-------------------------------------------------------------------------------------------------
void RmlPlayerInfoScreen::onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonClose's GBM_SELECTED body (PopupPlayerInfo.cpp) exactly.
	ClosePlayerInfoOverlay();
}

void RmlPlayerInfoScreen::onLogout(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonDeleteAccount's (relabelled LOGOUT) GBM_SELECTED body exactly: close first, then
	// confirm. MessageBoxYesNo() already routes through RmlUiMessageBoxHook when it's set (see
	// RmlUiMessageBoxHook.h), same as every other RmlUi screen's confirmation dialogs.
	ClosePlayerInfoOverlay();
	MessageBoxYesNo(UnicodeString(L"Log Out"), UnicodeString(L"Are you sure you want to log out?"), &rmlPlayerInfoLogoutConfirmed, nullptr);
}

//-------------------------------------------------------------------------------------------------
void OpenRmlPlayerInfoScreen()
{
	RmlPlayerInfoScreen::instance().open();
}

void CloseRmlPlayerInfoScreen()
{
	RmlPlayerInfoScreen::instance().close();
}
