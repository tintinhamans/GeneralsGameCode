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

#include "W3DDevice/GameClient/RmlUi/RmlScoreScreen.h"

#include "Common/AsciiString.h"
#include "Common/Recorder.h"
#include "Common/UnicodeString.h"
#include "GameClient/GUI/GUICallbacks/Menus/ScoreScreenActions.h"
#include "GameClient/Image.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same conversion RmlMainMenuScreen.cpp/RmlOptionsScreen.cpp/etc. each keep as a private helper.
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
RmlScoreScreen &RmlScoreScreen::instance()
{
	static RmlScoreScreen s_screen;
	return s_screen;
}

void RmlScoreScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("scorescreen");
	if (constructor)
	{
		Rml::StructHandle<RowModel> rowHandle = constructor.RegisterStruct<RowModel>();
		if (rowHandle)
		{
			rowHandle.RegisterMember("display_name", &RowModel::displayName);
			rowHandle.RegisterMember("is_observer", &RowModel::isObserver);
			rowHandle.RegisterMember("money_earned", &RowModel::moneyEarned);
			rowHandle.RegisterMember("units_built", &RowModel::unitsBuilt);
			rowHandle.RegisterMember("units_lost", &RowModel::unitsLost);
			rowHandle.RegisterMember("units_destroyed", &RowModel::unitsDestroyed);
			rowHandle.RegisterMember("buildings_built", &RowModel::buildingsBuilt);
			rowHandle.RegisterMember("buildings_lost", &RowModel::buildingsLost);
			rowHandle.RegisterMember("buildings_destroyed", &RowModel::buildingsDestroyed);
			rowHandle.RegisterMember("side_icon_image", &RowModel::sideIconImage);
			rowHandle.RegisterMember("show_side_icon", &RowModel::showSideIcon);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();
		constructor.RegisterArray<Rml::Vector<Rml::String>>();

		constructor.Bind("rows", &m_model.rows);
		constructor.Bind("background_image", &m_model.backgroundImage);
		constructor.Bind("has_background_image", &m_model.hasBackgroundImage);
		constructor.Bind("show_chat_entry", &m_model.showChatEntry);
		constructor.Bind("show_emote_button", &m_model.showEmoteButton);
		constructor.Bind("show_chat_box_border", &m_model.showChatBoxBorder);
		constructor.Bind("show_chat_log", &m_model.showChatLog);
		constructor.Bind("show_buddies_button", &m_model.showBuddiesButton);
		constructor.Bind("show_continue_button", &m_model.showContinueButton);
		constructor.Bind("show_default_continue_label", &m_model.showDefaultContinueLabel);
		constructor.Bind("continue_button_caption", &m_model.continueButtonCaption);
		constructor.Bind("show_academy_panel", &m_model.showAcademyPanel);
		constructor.Bind("academy_advice", &m_model.academyAdvice);
		constructor.Bind("show_challenge_splash", &m_model.showChallengeSplash);
		constructor.Bind("challenge_portrait_image", &m_model.challengePortraitImage);
		constructor.Bind("challenge_header_text", &m_model.challengeHeaderText);
		constructor.Bind("challenge_remarks_text", &m_model.challengeRemarksText);
		constructor.Bind("show_save_game_text", &m_model.showSaveGameText);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);
		constructor.Bind("chat_lines", &m_model.chatLines);

		constructor.BindEventCallback("ok", &RmlScoreScreen::onOk, this);
		constructor.BindEventCallback("continue_pressed", &RmlScoreScreen::onContinue, this);
		constructor.BindEventCallback("buddies", &RmlScoreScreen::onBuddies, this);
		constructor.BindEventCallback("save_replay", &RmlScoreScreen::onSaveReplay, this);
		constructor.BindEventCallback("send_chat", &RmlScoreScreen::onSendChat, this);
		constructor.BindEventCallback("send_emote", &RmlScoreScreen::onSendEmote, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/ScoreScreen.rml");
}

//-------------------------------------------------------------------------------------------------
// Mode dispatch moved from ScoreScreen.cpp's ScoreScreenInit(), which never runs for this
// RmlUi-routed .wnd path (see RmlScoreScreen.h). Builds ScoreScreenData + ScoreScreenLayout the
// same way the matching initX() function does and copies them into m_model.
void RmlScoreScreen::refreshFromGameState()
{
	ScoreScreenData data;

	if (TheGameLogic->isInReplayGame())
	{
		m_mode = SCORESCREENMODE_REPLAY;
		if (TheRecorder->isMultiplayer())
			data = ScoreScreenData::buildForMultiPlayer(m_mode);
		else
			data = ScoreScreenData::buildForSinglePlayer();
	}
	else if (TheGameLogic->isInInternetGame())
	{
		m_mode = SCORESCREENMODE_INTERNET;
		data = ScoreScreenData::buildForMultiPlayer(m_mode);
	}
	else if (TheGameLogic->isInLanGame())
	{
		m_mode = SCORESCREENMODE_LAN;
		data = ScoreScreenData::buildForMultiPlayer(m_mode);
	}
	else if (TheGameLogic->isInSkirmishGame())
	{
		m_mode = SCORESCREENMODE_SKIRMISH;
		data = ScoreScreenData::buildForMultiPlayer(m_mode);
	}
	else
	{
		m_mode = SCORESCREENMODE_SINGLEPLAYER;
		data = ScoreScreenData::buildForSinglePlayer();
	}

	ScoreScreenLayout layout = ScoreScreenLayout::forMode(m_mode);

	m_model.rows.clear();
	m_model.academyAdvice.clear();
	for (const ScoreScreenPlayerRow &row : data.m_rows)
	{
		RowModel rowModel;
		rowModel.displayName = unicodeToUtf8(row.m_displayName);
		rowModel.isObserver = row.m_isObserver == TRUE;
		rowModel.moneyEarned = row.m_stats.m_totalMoneyEarned;
		rowModel.unitsBuilt = row.m_stats.m_totalUnitsBuilt;
		rowModel.unitsLost = row.m_stats.m_totalUnitsLost;
		rowModel.unitsDestroyed = row.m_stats.m_totalUnitsDestroyed;
		rowModel.buildingsBuilt = row.m_stats.m_totalBuildingsBuilt;
		rowModel.buildingsLost = row.m_stats.m_totalBuildingsLost;
		rowModel.buildingsDestroyed = row.m_stats.m_totalBuildingsDestroyed;
		if (row.m_touchSideIcon && row.m_sideIconImage)
		{
			rowModel.sideIconImage = row.m_sideIconImage->getName().str();
			rowModel.showSideIcon = true;
		}
		m_model.rows.push_back(rowModel);

		if (row.m_isLocalPlayerRow)
			for (const UnicodeString &tip : row.m_academyAdvice)
				m_model.academyAdvice.push_back(unicodeToUtf8(tip));
	}

	m_model.hasBackgroundImage = data.m_hasBackgroundImage == TRUE;
	m_model.backgroundImage = data.m_hasBackgroundImage && data.m_backgroundImage ? data.m_backgroundImage->getName().str() : Rml::String();

	m_model.showChatEntry = layout.m_showChatEntry == TRUE;
	m_model.showEmoteButton = layout.m_showEmoteButton == TRUE;
	m_model.showChatBoxBorder = layout.m_showChatBoxBorder == TRUE;
	m_model.showChatLog = layout.m_showChatLog == TRUE;
	m_model.showBuddiesButton = layout.m_showBuddiesButton == TRUE;
	m_model.showContinueButton = layout.m_showContinueButton == TRUE;
	m_model.showDefaultContinueLabel = layout.m_continueButtonCaption.isEmpty();
	m_model.continueButtonCaption = m_model.showDefaultContinueLabel ? Rml::String() : unicodeToUtf8(layout.m_continueButtonCaption);
	m_model.showAcademyPanel = layout.m_touchAcademyPanel == TRUE && layout.m_showAcademyPanel == TRUE;

	m_model.showChallengeSplash = false;
	m_model.challengePortraitImage.clear();
	m_model.challengeHeaderText.clear();
	m_model.challengeRemarksText.clear();
	m_model.showSaveGameText = false;
	m_buttonIsFinishCampaign = false;

	m_model.chatEntryText.clear();
	m_model.chatLines.clear();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	// See finishSinglePlayerIfNeeded(): same one-tick-later timing as the .wnd path's
	// s_needToFinishSinglePlayerInit/ScoreScreenUpdate().
	m_needsFinishSinglePlayer = (m_mode == SCORESCREENMODE_SINGLEPLAYER);
}

// Forwards LANAPI::OnChat()'s score-screen chat/emote/system lines (see LANAPICallbacks.cpp), and
// the WOLGameSetupMenu.cpp/WOLQuickMatchMenu.cpp disconnect notices, into this screen's chat log
// while it's the active score screen. No color support: chat_lines is a plain string list, same
// as the local-echo path in onSendChat()/onSendEmote().
static void onScoreScreenChatDelivered(const UnicodeString &line, Color /*color*/)
{
	RmlScoreScreen::instance().appendChatLine(unicodeToUtf8(line));
}

void RmlScoreScreen::show()
{
	if (!m_document)
		return;

	refreshFromGameState();
	m_document->Show();
	g_scoreScreenChatDeliveryHook = &onScoreScreenChatDelivered;
}

void RmlScoreScreen::hide()
{
	if (m_document)
		m_document->Hide();
	if (g_scoreScreenChatDeliveryHook == &onScoreScreenChatDelivered)
		g_scoreScreenChatDeliveryHook = nullptr;
}

bool RmlScoreScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlScoreScreen::onBack()
{
	// Same as ButtonOk (see ScoreScreen.cpp's GWM_CHAR/KEY_ESC); duplicated instead of routed
	// through onOk() since that takes an Rml::Event& this call site doesn't have (see
	// RmlOptionsScreen::onBack() for the same pattern).
	ScoreScreenActions::pressOk();
}

void RmlScoreScreen::appendChatLine(const Rml::String &line)
{
	m_model.chatLines.push_back(line);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_lines");
}

//-------------------------------------------------------------------------------------------------
void RmlScoreScreen::onOk(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ScoreScreenActions::pressOk();
}

void RmlScoreScreen::onContinue(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ScoreScreenActions::pressContinue(m_mode, m_buttonIsFinishCampaign ? TRUE : FALSE);
}

void RmlScoreScreen::update()
{
	finishSinglePlayerIfNeeded();
}

// Moved from ScoreScreen.cpp's finishSinglePlayerInit(), see ScoreScreenActions::finishSinglePlayer().
// Runs once, one update() tick after show() (matching the .wnd path's
// s_needToFinishSinglePlayerInit/ScoreScreenUpdate() timing).
void RmlScoreScreen::finishSinglePlayerIfNeeded()
{
	if (!m_needsFinishSinglePlayer)
		return;
	m_needsFinishSinglePlayer = false;

	ScoreScreenCampaignFinish result = ScoreScreenActions::finishSinglePlayer();
	m_buttonIsFinishCampaign = result.m_campaignComplete == TRUE;

	m_model.showChallengeSplash = result.m_showChallengeSplash == TRUE;
	m_model.challengePortraitImage = result.m_challengePortrait ? result.m_challengePortrait->getName().str() : Rml::String();
	m_model.challengeHeaderText = unicodeToUtf8(result.m_challengeHeaderText);
	m_model.challengeRemarksText = unicodeToUtf8(result.m_challengeRemarksText);

	m_model.showDefaultContinueLabel = result.m_continueButtonCaption.isEmpty();
	m_model.continueButtonCaption = m_model.showDefaultContinueLabel ? Rml::String() : unicodeToUtf8(result.m_continueButtonCaption);

	m_model.showSaveGameText = result.m_showSaveGameText == TRUE;

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();

	if (result.m_campaignCompletionMovie.isNotEmpty())
		ScoreScreenActions::playCampaignCompletionMovie(result.m_campaignCompletionMovie);
}

void RmlScoreScreen::onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ScoreScreenActions::toggleBuddyOverlay();
}

void RmlScoreScreen::onSaveReplay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ScoreScreenActions::startSaveReplayFlow();
}

// Chat text entry is plain ASCII (RmlUi's Rml::String is UTF-8, but AsciiString::translate()
// does a naive single-byte widen, same limitation the .wnd TextEntry gadget always had --
// no full Unicode chat input either).
void RmlScoreScreen::onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	AsciiString ascii(m_model.chatEntryText.c_str());
	UnicodeString text;
	text.translate(ascii);
	text.trim();
	if (!text.isEmpty())
		appendChatLine(m_model.chatEntryText);
	ScoreScreenActions::sendChat(text, FALSE);
	m_model.chatEntryText.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_entry_text");
}

void RmlScoreScreen::onSendEmote(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	AsciiString ascii(m_model.chatEntryText.c_str());
	UnicodeString text;
	text.translate(ascii);
	text.trim();
	if (!text.isEmpty())
		appendChatLine(m_model.chatEntryText);
	ScoreScreenActions::sendChat(text, TRUE);
	m_model.chatEntryText.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_entry_text");
}

//-------------------------------------------------------------------------------------------------
void OpenRmlScoreScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlScoreScreen::instance());
}

void CloseRmlScoreScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlScoreScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
