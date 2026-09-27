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

#include "W3DDevice/GameClient/RmlUi/RmlSkirmishSetupScreen.h"

#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/SkirmishPreferences.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/GameSetupData.h"
#include "GameClient/GUI/GUICallbacks/Menus/SkirmishSetupActions.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameNetwork/GameInfo.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <cstdio>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same conversion every other RmlUi screen keeps as a private helper.
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1);
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

static Rml::String rgbToHex(UnsignedInt rgb)
{
	char hex[8];
	_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X",
		(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return Rml::String(hex);
}

// Fallback for an unset/unmatched color (row.m_color == -1, or a marker with no occupant): a
// CSS-legal "no color assigned yet" swatch, since data-style-background-color can't bind to an
// empty string (RmlUi logs a syntax-error warning and leaves the previous value in place).
static const char *const kNoColorHex = "transparent";

//-------------------------------------------------------------------------------------------------
RmlSkirmishSetupScreen &RmlSkirmishSetupScreen::instance()
{
	static RmlSkirmishSetupScreen s_screen;
	return s_screen;
}

void RmlSkirmishSetupScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("skirmishsetup");
	if (constructor)
	{
		Rml::StructHandle<SlotRowModel> rowHandle = constructor.RegisterStruct<SlotRowModel>();
		if (rowHandle)
		{
			rowHandle.RegisterMember("slot_index", &SlotRowModel::slotIndex);
			rowHandle.RegisterMember("occupant_state", &SlotRowModel::occupantState);
			rowHandle.RegisterMember("is_occupied", &SlotRowModel::isOccupied);
			rowHandle.RegisterMember("is_human_occupant", &SlotRowModel::isHumanOccupant);
			rowHandle.RegisterMember("occupant_label", &SlotRowModel::occupantLabel);
			rowHandle.RegisterMember("can_edit", &SlotRowModel::canEdit);
			rowHandle.RegisterMember("can_edit_occupant", &SlotRowModel::canEditOccupant);
			rowHandle.RegisterMember("player_name", &SlotRowModel::playerName);
			rowHandle.RegisterMember("player_template", &SlotRowModel::playerTemplate);
			rowHandle.RegisterMember("faction_label", &SlotRowModel::factionLabel);
			rowHandle.RegisterMember("color", &SlotRowModel::color);
			rowHandle.RegisterMember("color_hex", &SlotRowModel::colorHex);
			rowHandle.RegisterMember("team_number", &SlotRowModel::teamNumber);
			rowHandle.RegisterMember("start_position", &SlotRowModel::startPosition);
		}

		Rml::StructHandle<StartMarkerModel> markerHandle = constructor.RegisterStruct<StartMarkerModel>();
		if (markerHandle)
		{
			markerHandle.RegisterMember("position", &StartMarkerModel::position);
			markerHandle.RegisterMember("x_style", &StartMarkerModel::xStyle);
			markerHandle.RegisterMember("y_style", &StartMarkerModel::yStyle);
			markerHandle.RegisterMember("is_occupied", &StartMarkerModel::isOccupied);
			markerHandle.RegisterMember("occupant_label", &StartMarkerModel::occupantLabel);
			markerHandle.RegisterMember("color_hex", &StartMarkerModel::colorHex);
		}

		Rml::StructHandle<OptionModel> optionHandle = constructor.RegisterStruct<OptionModel>();
		if (optionHandle)
		{
			optionHandle.RegisterMember("value", &OptionModel::value);
			optionHandle.RegisterMember("label", &OptionModel::label);
		}

		constructor.RegisterArray<Rml::Vector<SlotRowModel>>();
		constructor.RegisterArray<Rml::Vector<StartMarkerModel>>();
		constructor.RegisterArray<Rml::Vector<OptionModel>>();

		constructor.Bind("slots", &m_model.slots);
		constructor.Bind("start_markers", &m_model.startMarkers);
		constructor.Bind("faction_options", &m_model.factionOptions);
		constructor.Bind("color_options", &m_model.colorOptions);
		constructor.Bind("starting_cash_options", &m_model.startingCashOptions);
		constructor.Bind("map_name", &m_model.mapName);
		constructor.Bind("map_display_name", &m_model.mapDisplayName);
		constructor.Bind("map_found", &m_model.mapFound);
		constructor.Bind("starting_cash", &m_model.startingCash);
		constructor.Bind("superweapons_restricted", &m_model.superweaponsRestricted);
		constructor.Bind("game_speed_slider_pos", &m_model.gameSpeedSliderPos);
		constructor.Bind("honor_wins", &m_model.honorWins);
		constructor.Bind("honor_losses", &m_model.honorLosses);
		constructor.Bind("honor_win_streak", &m_model.honorWinStreak);
		constructor.Bind("honor_best_win_streak", &m_model.honorBestWinStreak);

		constructor.BindEventCallback("slot_occupant_changed", &RmlSkirmishSetupScreen::onSlotOccupantChanged, this);
		constructor.BindEventCallback("slot_faction_changed", &RmlSkirmishSetupScreen::onSlotFactionChanged, this);
		constructor.BindEventCallback("slot_color_changed", &RmlSkirmishSetupScreen::onSlotColorChanged, this);
		constructor.BindEventCallback("slot_team_changed", &RmlSkirmishSetupScreen::onSlotTeamChanged, this);
		constructor.BindEventCallback("start_marker_clicked", &RmlSkirmishSetupScreen::onStartPositionMarkerClick, this);
		constructor.BindEventCallback("start_marker_mousedown", &RmlSkirmishSetupScreen::onStartPositionMarkerMouseDown, this);
		constructor.BindEventCallback("starting_cash_changed", &RmlSkirmishSetupScreen::onStartingCashChanged, this);
		constructor.BindEventCallback("superweapons_changed", &RmlSkirmishSetupScreen::onSuperweaponsChanged, this);
		constructor.BindEventCallback("game_speed_changed", &RmlSkirmishSetupScreen::onGameSpeedChanged, this);
		constructor.BindEventCallback("start", &RmlSkirmishSetupScreen::onStart, this);
		constructor.BindEventCallback("back", &RmlSkirmishSetupScreen::onBackPressed, this);
		constructor.BindEventCallback("reset_honors", &RmlSkirmishSetupScreen::onResetHonors, this);
		constructor.BindEventCallback("select_map", &RmlSkirmishSetupScreen::onSelectMap, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/SkirmishGameOptions.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishSetupScreen::refreshFromGameState()
{
	GameSetupData data = GameSetupData::build(TheSkirmishGameInfo);

	m_model.slots.clear();
	for (size_t i = 0; i < data.m_slots.size(); ++i)
	{
		const GameSetupSlotRow &src = data.m_slots[i];

		SlotRowModel row;
		row.slotIndex = (int)i;
		row.occupantState = (int)src.m_state;
		row.isOccupied = src.m_state != SLOT_OPEN && src.m_state != SLOT_CLOSED;
		row.isHumanOccupant = src.m_state == SLOT_PLAYER;
		row.occupantLabel = unicodeToUtf8(src.m_name);
		row.canEdit = src.m_canEdit == TRUE;
		row.canEditOccupant = row.canEdit && !src.m_isLocalSlot;
		row.playerName = unicodeToUtf8(src.m_name);
		row.playerTemplate = src.m_playerTemplate;
		row.color = src.m_color;
		row.colorHex = kNoColorHex;
		row.teamNumber = src.m_teamNumber;
		row.startPosition = src.m_startPosition;

		for (const GameSetupFactionOption &faction : data.m_options.m_factionOptions)
		{
			if (faction.m_playerTemplate == src.m_playerTemplate)
			{
				row.factionLabel = unicodeToUtf8(faction.m_displayName);
				break;
			}
		}
		for (const GameSetupColorOption &color : data.m_options.m_colorOptions)
		{
			if (color.m_color == src.m_color)
			{
				row.colorHex = rgbToHex(color.m_rgb);
				break;
			}
		}

		m_model.slots.push_back(row);
	}

	m_model.startMarkers.clear();
	for (const GameSetupStartPositionMarker &marker : data.m_options.m_startPositionMarkers)
	{
		StartMarkerModel markerModel;
		markerModel.position = marker.m_position;
		markerModel.used = marker.m_used == TRUE;
		char xBuf[16], yBuf[16];
		_snprintf_s(xBuf, sizeof(xBuf), _TRUNCATE, "%.3f%%", marker.m_xFraction * 100.0f);
		_snprintf_s(yBuf, sizeof(yBuf), _TRUNCATE, "%.3f%%", marker.m_yFraction * 100.0f);
		markerModel.xStyle = xBuf;
		markerModel.yStyle = yBuf;
		markerModel.colorHex = kNoColorHex;

		for (const GameSetupSlotRow &src : data.m_slots)
		{
			if (markerModel.used && src.m_startPosition == marker.m_position)
			{
				markerModel.isOccupied = true;
				markerModel.occupantLabel = unicodeToUtf8(src.m_name);
				for (const GameSetupColorOption &color : data.m_options.m_colorOptions)
				{
					if (color.m_color == src.m_color)
					{
						markerModel.colorHex = rgbToHex(color.m_rgb);
						break;
					}
				}
				break;
			}
		}

		m_model.startMarkers.push_back(markerModel);
	}

	m_model.factionOptions.clear();
	for (const GameSetupFactionOption &faction : data.m_options.m_factionOptions)
		m_model.factionOptions.push_back(OptionModel{ faction.m_playerTemplate, unicodeToUtf8(faction.m_displayName) });

	m_model.colorOptions.clear();
	for (const GameSetupColorOption &color : data.m_options.m_colorOptions)
		m_model.colorOptions.push_back(OptionModel{ color.m_color, rgbToHex(color.m_rgb) });

	m_model.startingCashOptions.clear();
	for (const GameSetupStartingCashOption &cash : data.m_options.m_startingCashOptions)
		m_model.startingCashOptions.push_back(OptionModel{ cash.m_amount, unicodeToUtf8(cash.m_label) });

	m_model.mapName = data.m_options.m_mapName.str();
	m_model.mapDisplayName = unicodeToUtf8(data.m_options.m_mapDisplayName);
	m_model.mapFound = data.m_options.m_mapFound == TRUE;
	m_model.startingCash = (int)data.m_options.m_startingCash.countMoney();
	m_model.superweaponsRestricted = data.m_options.m_superweaponsRestricted == TRUE;

	SkirmishBattleHonors honors;
	m_model.honorWins = honors.getWins();
	m_model.honorLosses = honors.getLosses();
	m_model.honorWinStreak = honors.getWinStreak();
	m_model.honorBestWinStreak = honors.getBestWinStreak();

	// Per-variable Dirty*() instead of DirtyAllVariables(): the latter, dirtying every bound
	// variable in one Update() pass while start_markers/slots also shrink (fewer occupied slots,
	// or a map with fewer start positions), makes RmlUi transiently evaluate a removed data-for
	// element's nested data-style-left/top/background-color binding against an already-out-of-
	// range index -- logged as "Data array index out of bounds" plus a bad inline-property
	// warning. Dirtying only what actually changed avoids that ordering entirely (confirmed via
	// the offline checker's shrink-to-empty combo; see report).
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("slots");
		m_modelHandle.DirtyVariable("start_markers");
		m_modelHandle.DirtyVariable("faction_options");
		m_modelHandle.DirtyVariable("color_options");
		m_modelHandle.DirtyVariable("starting_cash_options");
		m_modelHandle.DirtyVariable("map_name");
		m_modelHandle.DirtyVariable("map_display_name");
		m_modelHandle.DirtyVariable("map_found");
		m_modelHandle.DirtyVariable("starting_cash");
		m_modelHandle.DirtyVariable("superweapons_restricted");
		m_modelHandle.DirtyVariable("honor_wins");
		m_modelHandle.DirtyVariable("honor_losses");
		m_modelHandle.DirtyVariable("honor_win_streak");
		m_modelHandle.DirtyVariable("honor_best_win_streak");
	}
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishSetupScreen::show()
{
	if (!m_document)
		return;

	SkirmishSetupActions::enterSkirmishSetup();

	// Same read/clamp as SkirmishGameOptionsMenuInit()'s slider setup; game speed isn't part of
	// GameInfo, so it isn't covered by refreshFromGameState()/GameSetupData.
	SkirmishPreferences prefs;
	m_model.gameSpeedSliderPos = max(15, min(61, prefs.getInt("FPS", TheGlobalData->m_framesPerSecondLimit)));
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("game_speed_slider_pos");

	refreshFromGameState();
	m_document->Show();
}

void RmlSkirmishSetupScreen::hide()
{
	if (m_document)
		m_document->Hide();
}

bool RmlSkirmishSetupScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlSkirmishSetupScreen::onBack()
{
	// Same as the "back"/ButtonExit event (see onBackPressed()), duplicated instead of routed
	// through it since that takes an Rml::Event& this call site doesn't have (see
	// RmlOptionsScreen::onBack() for the same pattern).
	SkirmishSetupActions::leaveSkirmishSetup(m_model.gameSpeedSliderPos);
	hide();
	if (TheShell)
		TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
void RmlSkirmishSetupScreen::onSlotOccupantChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	Int slotIndex = args[0].Get<int>();
	Int state = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	SkirmishSetupActions::selectPlayerState(TheSkirmishGameInfo, slotIndex, (SlotState)state, UnicodeString::TheEmptyString);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onSlotFactionChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	Int slotIndex = args[0].Get<int>();
	Int playerTemplate = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	SkirmishSetupActions::selectPlayerTemplate(TheSkirmishGameInfo, slotIndex, playerTemplate);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onSlotColorChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	Int slotIndex = args[0].Get<int>();
	Int color = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	SkirmishSetupActions::selectColor(TheSkirmishGameInfo, slotIndex, color);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onSlotTeamChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	Int slotIndex = args[0].Get<int>();
	Int team = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	SkirmishSetupActions::selectTeam(TheSkirmishGameInfo, slotIndex, team);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onStartPositionMarkerClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	Int position = args[0].Get<int>();
	SkirmishSetupActions::handleStartPositionMarkerClick(TheSkirmishGameInfo, position);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onStartPositionMarkerMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	// GBM_SELECTED_RIGHT equivalent: RmlUi only synthesizes "click" for the left button,
	// so the right-click marker clear has to be caught on the raw mousedown (button 1).
	if (args.empty() || ev.GetParameter<int>("button", 0) != 1)
		return;
	Int position = args[0].Get<int>();
	SkirmishSetupActions::handleStartPositionMarkerRightClick(TheSkirmishGameInfo, position);
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onStartingCashChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	Money money;
	money.setStartingCash((UnsignedInt)max(0, m_model.startingCash));
	SkirmishSetupActions::setStartingCash(TheSkirmishGameInfo, money);
}

void RmlSkirmishSetupScreen::onSuperweaponsChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SkirmishSetupActions::setSuperweaponRestriction(TheSkirmishGameInfo, m_model.superweaponsRestricted ? TRUE : FALSE);
}

void RmlSkirmishSetupScreen::onGameSpeedChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Read back only; applied at Start (see onStart()), matching the .wnd slider exactly.
}

void RmlSkirmishSetupScreen::onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// SkirmishGameOptionsMenu.cpp's ButtonStart persists prefs before validating; match that order.
	SkirmishSetupActions::persistPreferences(m_model.gameSpeedSliderPos);

	SkirmishSetupActions::StartValidationResult result = SkirmishSetupActions::validateStart(TheSkirmishGameInfo);
	switch (result)
	{
		case SkirmishSetupActions::STARTVALIDATION_MAP_NOT_FOUND:
			MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), TheGameText->fetch("GUI:CantFindMap"), nullptr);
			break;

		case SkirmishSetupActions::STARTVALIDATION_TOO_MANY_PLAYERS:
		{
			const MapMetaData *mmd = TheMapCache ? TheMapCache->findMap(TheSkirmishGameInfo->getMap()) : nullptr;
			UnicodeString msg;
			msg.format(TheGameText->fetch("GUI:TooManyPlayers"), mmd ? mmd->m_numPlayers : 0);
			MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), msg, nullptr);
			break;
		}

		case SkirmishSetupActions::STARTVALIDATION_READY:
		{
			Int maxFPS = m_model.gameSpeedSliderPos;
			if (maxFPS > 60) // GREATER_NO_FPS_LIMIT
				maxFPS = 1000;
			if (maxFPS < 15)
				maxFPS = 15;
			SkirmishSetupActions::startGame(TheSkirmishGameInfo, maxFPS);
			break;
		}
	}
}

void RmlSkirmishSetupScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SkirmishSetupActions::leaveSkirmishSetup(m_model.gameSpeedSliderPos);
	hide();
	if (TheShell)
		TheShell->pop();
}

void RmlSkirmishSetupScreen::onResetHonors(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SkirmishSetupActions::resetBattleHonors();
	refreshFromGameState();
}

void RmlSkirmishSetupScreen::onSelectMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Same as SkirmishGameOptionsMenu.cpp's ButtonSelectMap: hide this screen while the map-select
	// overlay is up, same shape as the .wnd's showSkirmishGameOptionsUnderlyingGUIElements(FALSE).
	hide();
	RmlUiScreenRegistry::open("Menus/SkirmishMapSelectMenu.wnd");
}

void RmlSkirmishSetupScreen::returnFromMapSelect()
{
	if (!m_document)
		return;
	refreshFromGameState();
	m_document->Show();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlSkirmishSetupScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlSkirmishSetupScreen::instance());
}

void CloseRmlSkirmishSetupScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlSkirmishSetupScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}

void ReturnToRmlSkirmishSetupScreen()
{
	RmlSkirmishSetupScreen::instance().returnFromMapSelect();
}
