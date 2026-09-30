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

#include "W3DDevice/GameClient/RmlUi/RmlOnlineGameSetupScreen.h"
#include "W3DDevice/GameClient/RmlUi/RmlChatInput.h"

#include "Common/AsciiString.h"
#include "Common/Money.h"
#include "Common/PlayerTemplate.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupData.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupSession.h"
#include "GameClient/GUI/GUICallbacks/Menus/OnlineSessionExit.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameClient/TransitionSounds.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <cstdio>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
static Rml::String rgbToHex(UnsignedInt rgb)
{
	char hex[8];
	_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X",
		(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return Rml::String(hex);
}

// Same bit layout as rgbToHex (red bits 16-23, green 8-15, blue 0-7) -- Color (see Color.h's
// GameMakeColor) and GameSetupColorOption::m_rgb both pack this way, alpha in the unused top byte.
static Rml::String colorToHex(Color color)
{
	return rgbToHex((UnsignedInt)color);
}

// Fallback for an unset/unmatched color, see RmlLanGameSetupScreen.cpp's kNoColorHex.
static const char *const kNoColorHex = "transparent";

// ARGB packing, see Color.h's GameMakeColor(); RmlUi's rgba() takes 0-255 ints for every channel
// including alpha, not a 0-1 float. Same helper RmlOnlineLobbyScreen.cpp/RmlLanLobbyScreen.cpp each
// keep privately -- binds a chat line's exact Color (player/buddy/system colors alike) instead of
// snapping it to a handful of CSS classes, which lost any color outside a tight RGB tolerance.
static Rml::String colorToCss(Color color)
{
	const int a = (color >> 24) & 0xFF;
	const int r = (color >> 16) & 0xFF;
	const int g = (color >> 8) & 0xFF;
	const int b = color & 0xFF;
	char buf[48];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "rgba(%d,%d,%d,%d)", r, g, b, a);
	return Rml::String(buf);
}

// Mirrors playerTemplateComboBoxTooltip()/playerTemplateListBoxTooltip() (LobbyUtils.cpp): the
// "Army Tooltip" text for a faction dropdown entry.
static Rml::String factionTooltipFor(Int playerTemplate)
{
	if (playerTemplate == PLAYERTEMPLATE_RANDOM)
		return unicodeToUtf8(TheGameText->fetch("TOOLTIP:BioStrategyLong_Random"));

	const PlayerTemplate *tmpl = ThePlayerTemplateStore ? ThePlayerTemplateStore->getNthPlayerTemplate(playerTemplate) : nullptr;
	if (tmpl)
		return unicodeToUtf8(TheGameText->fetch(tmpl->getTooltip()));
	return Rml::String();
}

// Formats the per-slot connection indicator's tooltip text from OnlineGameSetupConnectionInfo --
// simplified reproduction of playerTooltip()'s mesh-connection section (WOLGameSetupMenu.cpp):
// region/latency/jitter/quality/score, no live stats-service fetch (that part of the original
// tooltip isn't reproduced -- see report). is_connected alone drives the indicator's CSS
// (EConnectionState::CONNECTED_DIRECT, see OnlineGameSetupConnectionInfo::m_isConnected).
static Rml::String connectionTooltipFor(const OnlineGameSetupConnectionInfo &info)
{
	if (info.m_isLocalPlayer)
		return Rml::String();

	char buf[256];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s%d ms%s%d%%",
		info.m_region.empty() ? "" : (info.m_region + " - ").c_str(),
		info.m_connectionType.empty() ? "" : (info.m_connectionType + " - ").c_str(),
		"Ping: ", info.m_latencyMs,
		" - Quality: ", info.m_qualityPct);
	return Rml::String(buf);
}

//-------------------------------------------------------------------------------------------------
RmlOnlineGameSetupScreen &RmlOnlineGameSetupScreen::instance()
{
	static RmlOnlineGameSetupScreen s_screen;
	return s_screen;
}

void RmlOnlineGameSetupScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("onlinegamesetup");
	if (constructor)
	{
		// Registered before the slot rows, which hold a per-row list of these.
		Rml::StructHandle<OptionModel> optionHandle = constructor.RegisterStruct<OptionModel>();
		if (optionHandle)
		{
			optionHandle.RegisterMember("value", &OptionModel::value);
			optionHandle.RegisterMember("label", &OptionModel::label);
			optionHandle.RegisterMember("swatch", &OptionModel::swatch);
			optionHandle.RegisterMember("icon", &OptionModel::icon);
			optionHandle.RegisterMember("taken", &OptionModel::taken);
		}
		constructor.RegisterArray<Rml::Vector<OptionModel>>();

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
			rowHandle.RegisterMember("faction_tooltip", &SlotRowModel::factionTooltip);
			rowHandle.RegisterMember("color", &SlotRowModel::color);
			rowHandle.RegisterMember("color_hex", &SlotRowModel::colorHex);
			rowHandle.RegisterMember("color_name", &SlotRowModel::colorName);
			rowHandle.RegisterMember("color_options", &SlotRowModel::colorOptions);
			rowHandle.RegisterMember("team_number", &SlotRowModel::teamNumber);
			rowHandle.RegisterMember("start_position", &SlotRowModel::startPosition);
			rowHandle.RegisterMember("accepted", &SlotRowModel::accepted);
			rowHandle.RegisterMember("has_map", &SlotRowModel::hasMap);
			rowHandle.RegisterMember("show_accept", &SlotRowModel::showAccept);
			rowHandle.RegisterMember("is_host_slot", &SlotRowModel::isHostSlot);
			rowHandle.RegisterMember("is_local", &SlotRowModel::isLocal);
			rowHandle.RegisterMember("latency_ms", &SlotRowModel::latencyMs);
			rowHandle.RegisterMember("is_connected", &SlotRowModel::isConnected);
			rowHandle.RegisterMember("connection_tooltip", &SlotRowModel::connectionTooltip);
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
			markerHandle.RegisterMember("used", &StartMarkerModel::used);
		}

		Rml::StructHandle<ChatLineModel> chatHandle = constructor.RegisterStruct<ChatLineModel>();
		if (chatHandle)
		{
			chatHandle.RegisterMember("text", &ChatLineModel::text);
			chatHandle.RegisterMember("color", &ChatLineModel::color);
		}

		constructor.RegisterArray<Rml::Vector<SlotRowModel>>();
		constructor.RegisterArray<Rml::Vector<StartMarkerModel>>();
		constructor.RegisterArray<Rml::Vector<ChatLineModel>>();

		constructor.Bind("slots", &m_model.slots);
		constructor.Bind("start_markers", &m_model.startMarkers);
		constructor.Bind("faction_options", &m_model.factionOptions);
		constructor.Bind("starting_cash_options", &m_model.startingCashOptions);
		constructor.Bind("game_name", &m_model.gameName);
		constructor.Bind("map_name", &m_model.mapName);
		constructor.Bind("map_display_text", &m_model.mapDisplayText);
		constructor.Bind("map_found", &m_model.mapFound);
		constructor.Bind("starting_cash", &m_model.startingCash);
		constructor.Bind("superweapons_restricted", &m_model.superweaponsRestricted);
		constructor.Bind("use_stats", &m_model.useStats);
		constructor.Bind("limit_armies", &m_model.limitArmies);
		constructor.Bind("is_host", &m_model.isHost);
		constructor.Bind("cash_and_sw_enabled", &m_model.cashAndSuperweaponsEnabled);
		constructor.Bind("start_enabled", &m_model.startEnabled);
		constructor.Bind("back_enabled", &m_model.backEnabled);
		constructor.Bind("settings_locked", &m_model.settingsLocked);
		constructor.Bind("countdown_seconds", &m_model.countdownSeconds);
		constructor.Bind("local_accepted", &m_model.localAccepted);
		constructor.Bind("communicator_enabled", &m_model.communicatorEnabled);
		constructor.Bind("communicator_label", &m_model.communicatorLabel);
		constructor.Bind("chat_lines", &m_model.chatLines);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);

		constructor.BindEventCallback("slot_occupant_changed", &RmlOnlineGameSetupScreen::onSlotOccupantChanged, this);
		constructor.BindEventCallback("slot_occupant_picked", &RmlOnlineGameSetupScreen::onSlotOccupantPicked, this);
		constructor.BindEventCallback("slot_color_picked", &RmlOnlineGameSetupScreen::onSlotColorPicked, this);
		constructor.BindEventCallback("slot_team_picked", &RmlOnlineGameSetupScreen::onSlotTeamPicked, this);
		constructor.BindEventCallback("starting_cash_picked", &RmlOnlineGameSetupScreen::onStartingCashPicked, this);
		constructor.BindEventCallback("slot_faction_changed", &RmlOnlineGameSetupScreen::onSlotFactionChanged, this);
		constructor.BindEventCallback("slot_color_changed", &RmlOnlineGameSetupScreen::onSlotColorChanged, this);
		constructor.BindEventCallback("slot_team_changed", &RmlOnlineGameSetupScreen::onSlotTeamChanged, this);
		constructor.BindEventCallback("start_marker_clicked", &RmlOnlineGameSetupScreen::onStartPositionMarkerClick, this);
		constructor.BindEventCallback("start_marker_mousedown", &RmlOnlineGameSetupScreen::onStartPositionMarkerMouseDown, this);
		constructor.BindEventCallback("starting_cash_changed", &RmlOnlineGameSetupScreen::onStartingCashChanged, this);
		constructor.BindEventCallback("superweapons_changed", &RmlOnlineGameSetupScreen::onSuperweaponsChanged, this);
		constructor.BindEventCallback("select_map", &RmlOnlineGameSetupScreen::onSelectMap, this);
		constructor.BindEventCallback("start", &RmlOnlineGameSetupScreen::onStart, this);
		constructor.BindEventCallback("back", &RmlOnlineGameSetupScreen::onBackPressed, this);
		constructor.BindEventCallback("chat_entry_committed", &RmlOnlineGameSetupScreen::onChatEntryCommitted, this);
		constructor.BindEventCallback("send_chat", &RmlOnlineGameSetupScreen::onSendChat, this);
		constructor.BindEventCallback("communicator_clicked", &RmlOnlineGameSetupScreen::onCommunicatorClicked, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/OnlineGameSetup.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineGameSetupScreen::refreshFromGameState()
{
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	OnlineGameSetupData data = OnlineGameSetupData::build(game);

	m_model.isHost = data.m_isHost == TRUE;
	m_model.cashAndSuperweaponsEnabled = data.m_cashAndSuperweaponsEnabled == TRUE;
	m_model.gameName = unicodeToUtf8(data.m_gameName);
	m_model.mapName = data.m_options.m_mapName.str();
	m_model.mapDisplayText = unicodeToUtf8(data.m_mapDisplayText);
	m_model.useStats = data.m_useStats == TRUE;
	m_model.limitArmies = data.m_limitArmies == TRUE;
	m_model.countdownSeconds = data.m_countdownSecondsRemaining;
	m_model.localAccepted = false;

	m_model.slots.clear();
	for (size_t i = 0; i < data.m_slots.size(); ++i)
	{
		const OnlineGameSetupSlotRow &src = data.m_slots[i];
		const GameSetupSlotRow &base = src.m_base;

		SlotRowModel row;
		row.slotIndex = (int)i;
		row.occupantState = (int)base.m_state;
		row.isOccupied = base.m_state != SLOT_OPEN && base.m_state != SLOT_CLOSED;
		row.isHumanOccupant = base.m_state == SLOT_PLAYER;
		row.occupantLabel = unicodeToUtf8(base.m_name);
		row.canEdit = base.m_canEdit == TRUE;
		row.canEditOccupant = base.m_canEditOccupant == TRUE;
		row.playerName = unicodeToUtf8(base.m_name);
		row.playerTemplate = base.m_playerTemplate;
		row.color = base.m_color;
		row.colorHex = kNoColorHex;
		row.teamNumber = base.m_teamNumber;
		row.startPosition = base.m_startPosition;
		row.accepted = src.m_accepted == TRUE;
		row.hasMap = src.m_hasMap == TRUE;
		row.showAccept = (i == 0); // single local-player accept indicator, see SlotRowModel::showAccept
		row.factionTooltip = factionTooltipFor(base.m_playerTemplate);
		row.isConnected = src.m_connection.m_isConnected == TRUE;
		row.connectionTooltip = connectionTooltipFor(src.m_connection);
		row.latencyMs = src.m_connection.m_isLocalPlayer ? -1 : src.m_connection.m_latencyMs;
		row.isHostSlot = src.m_isHostSlot == TRUE;
		row.isLocal = base.m_isLocalSlot == TRUE;
		if (row.isLocal)
			m_model.localAccepted = row.accepted;

		for (const GameSetupFactionOption &faction : data.m_options.m_factionOptions)
		{
			if (faction.m_playerTemplate == base.m_playerTemplate)
			{
				row.factionLabel = unicodeToUtf8(faction.m_displayName);
				break;
			}
		}
		for (const GameSetupColorOption &color : data.m_options.m_colorOptions)
		{
			if (color.m_color == base.m_color)
			{
				row.colorName = unicodeToUtf8(color.m_name);
				if (color.m_color >= 0)
					row.colorHex = rgbToHex(color.m_rgb);
			}
			// Every row lists the whole palette in the same order so the swatches never move; a
			// color missing from m_colorChoices (PopulateColorComboBox()'s filter) is shown taken.
			bool offered = false;
			for (Int choice : base.m_colorChoices)
				offered = offered || choice == color.m_color;
			OptionModel option{ color.m_color, unicodeToUtf8(color.m_name), color.m_color >= 0 ? rgbToHex(color.m_rgb) : Rml::String(kNoColorHex) };
			option.taken = !offered;
			row.colorOptions.push_back(option);
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

		for (const OnlineGameSetupSlotRow &srcRow : data.m_slots)
		{
			const GameSetupSlotRow &src = srcRow.m_base;
			if (markerModel.used && src.m_startPosition == marker.m_position)
			{
				markerModel.isOccupied = true;
				markerModel.occupantLabel = unicodeToUtf8(src.m_name);
				for (const GameSetupColorOption &color : data.m_options.m_colorOptions)
				{
					if (color.m_color >= 0 && color.m_color == src.m_color)
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
		m_model.factionOptions.push_back(OptionModel{ faction.m_playerTemplate, unicodeToUtf8(faction.m_displayName), Rml::String(), faction.m_iconImage.str() });

	m_model.startingCashOptions.clear();
	for (const GameSetupStartingCashOption &cash : data.m_options.m_startingCashOptions)
		m_model.startingCashOptions.push_back(OptionModel{ cash.m_amount, unicodeToUtf8(cash.m_label) });

	m_model.mapFound = data.m_options.m_mapFound == TRUE;
	m_model.startingCash = (int)data.m_options.m_startingCash.countMoney();
	m_model.superweaponsRestricted = data.m_options.m_superweaponsRestricted == TRUE;

	// Per-variable Dirty*() instead of DirtyAllVariables() -- see RmlLanGameSetupScreen::
	// refreshFromGameState()'s comment for why.
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("slots");
		m_modelHandle.DirtyVariable("start_markers");
		m_modelHandle.DirtyVariable("faction_options");
		m_modelHandle.DirtyVariable("starting_cash_options");
		m_modelHandle.DirtyVariable("game_name");
		m_modelHandle.DirtyVariable("map_name");
		m_modelHandle.DirtyVariable("map_display_text");
		m_modelHandle.DirtyVariable("map_found");
		m_modelHandle.DirtyVariable("starting_cash");
		m_modelHandle.DirtyVariable("superweapons_restricted");
		m_modelHandle.DirtyVariable("use_stats");
		m_modelHandle.DirtyVariable("limit_armies");
		m_modelHandle.DirtyVariable("is_host");
		m_modelHandle.DirtyVariable("cash_and_sw_enabled");
		m_modelHandle.DirtyVariable("countdown_seconds");
		m_modelHandle.DirtyVariable("local_accepted");
	}
}

//-------------------------------------------------------------------------------------------------
// OnlineGameSetupSignals listeners -- see OnlineGameSetupSession.h. Every write the async NGMP
// callbacks and the per-frame update used to make straight to a GameWindow (WOLGameSetupMenu.cpp's
// ConnectGameSetupSignals()) lands on m_model + DirtyVariable() here instead.
static void connectSessionSignals(RmlOnlineGameSetupScreen *screen, SignalConnections &connections)
{
	connections.disconnect();

	connections.add( OnlineGameSetupSignals::chatLine().connect( [screen](const UnicodeString &text, Color color) { screen->onChatLine(unicodeToUtf8(text), colorToCss(color)); } ) );
	connections.add( OnlineGameSetupSignals::slotsChanged().connect( [screen]() { screen->refreshFromGameState(); } ) );
	connections.add( OnlineGameSetupSignals::optionsChanged().connect( [screen]() { screen->refreshFromGameState(); } ) );
	connections.add( OnlineGameSetupSignals::becameHost().connect( [screen]() { screen->onBecameHost(); } ) );
	connections.add( OnlineGameSetupSignals::backButtonEnabled().connect( [screen](Bool enabled) { screen->setBackButtonEnabled(enabled == TRUE); } ) );
	connections.add( OnlineGameSetupSignals::startButtonEnabled().connect( [screen](Bool enabled) { screen->setStartButtonEnabled(enabled == TRUE); } ) );
	connections.add( OnlineGameSetupSignals::communicatorButtonEnabled().connect( [screen](Bool enabled) { screen->setCommunicatorButtonEnabled(enabled == TRUE); } ) );
	connections.add( OnlineGameSetupSignals::lockSettings().connect( [screen]() { screen->lockSettings(); } ) );
	connections.add( OnlineGameSetupSignals::communicatorCount().connect( [screen](int numNotifications) { screen->onCommunicatorCountChanged(numNotifications); } ) );
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineGameSetupScreen::show()
{
	if (!m_document)
		return;

	// InitWOLGameGadgets(): drops the "Creating Lobby" progress box.
	ClearGSMessageBoxes();

	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;

	if (game->isGameInProgress())
	{
		// Same as WOLGameSetupMenuInit(): returning here after a match started means going
		// straight back to the lobby.
		if (TheShell)
			TheShell->popImmediate();
		return;
	}

	m_model.chatLines.clear();
	m_model.chatEntryText.clear();
	m_model.startEnabled = true;
	m_model.backEnabled = true;
	m_model.settingsLocked = false;
	m_model.communicatorEnabled = true;
	m_model.communicatorLabel = unicodeToUtf8(TheGameText->fetch("GUI:Buddies"));

	OnlineGameSetupSession::prepareGameState();
	refreshFromGameState();

	connectSessionSignals(this, m_connections);
	OnlineGameSetupSession::enter();

	m_document->Show();

	// WOLGameSetupMenuInit()'s entrance group, and WOLGameSetupMenuShutdown()'s reverse below.
	TransitionSounds::play("GameSpyGameOptionsMenuFade");
}

void RmlOnlineGameSetupScreen::hide()
{
	if (m_document && m_document->IsVisible())
		TransitionSounds::play("GameSpyGameOptionsMenuFade", TRUE);
	if (m_document)
		m_document->Hide();

	OnlineGameSetupSession::leave();
	m_connections.disconnect();
}

bool RmlOnlineGameSetupScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOnlineGameSetupScreen::onBack()
{
	hide();
	OnlineGameSetupSession::backToLobby();
}

void RmlOnlineGameSetupScreen::update()
{
	if (!m_document || !isVisible())
		return;

	// WOLGameSetupMenuUpdate() never runs for a registry-routed screen; this is its pending-full-teardown exit.
	if (OnlineSessionExit::isTeardownReady())
	{
		hide();
		OnlineSessionExit::tearDownAndPop();
		return;
	}

	if (OnlineGameSetupSession::update() == TRUE)
		return; // host left this frame -- backToLobby() already popped the shell

	refreshFromGameState(); // pick up connection-indicator/roster changes every frame, same as WOLGameSetupMenuUpdate()
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineGameSetupScreen::onSlotOccupantChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int slotIndex = args[0].Get<int>();
	Int state = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	Bool isAIChanged = FALSE;
	OnlineGameSetupActions::selectSlotState(game, slotIndex, (SlotState)state, &isAIChanged);
	refreshFromGameState();
}

// An open slot's Add AI buttons: the same path as its occupant select.
void RmlOnlineGameSetupScreen::onSlotOccupantPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.size() < 2 || m_model.settingsLocked)
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Bool isAIChanged = FALSE;
	OnlineGameSetupActions::selectSlotState(game, args[0].Get<int>(), (SlotState)args[1].Get<int>(), &isAIChanged);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSlotColorPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.size() < 2 || m_model.settingsLocked)
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	OnlineGameSetupActions::selectColor(game, args[0].Get<int>(), args[1].Get<int>());
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSlotTeamPicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.size() < 2 || m_model.settingsLocked)
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	OnlineGameSetupActions::selectTeam(game, args[0].Get<int>(), args[1].Get<int>());
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onStartingCashPicked(Rml::DataModelHandle handle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || !m_model.cashAndSuperweaponsEnabled || m_model.settingsLocked)
		return;
	m_model.startingCash = args[0].Get<int>();
	onStartingCashChanged(handle, ev, args);
}

void RmlOnlineGameSetupScreen::onSlotFactionChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int slotIndex = args[0].Get<int>();
	Int playerTemplate = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	OnlineGameSetupActions::selectPlayerTemplate(game, slotIndex, playerTemplate);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSlotColorChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int slotIndex = args[0].Get<int>();
	Int color = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	OnlineGameSetupActions::selectColor(game, slotIndex, color);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSlotTeamChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty())
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int slotIndex = args[0].Get<int>();
	Int team = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	OnlineGameSetupActions::selectTeam(game, slotIndex, team);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onStartPositionMarkerClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	// WOLLockSettings() disables the start position buttons for the countdown's last second.
	if (args.empty() || m_model.settingsLocked)
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int position = args[0].Get<int>();
	OnlineGameSetupActions::handleStartPositionMarkerClick(game, position);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onStartPositionMarkerMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	// GBM_SELECTED_RIGHT equivalent: RmlUi only synthesizes "click" for the left button,
	// so the right-click marker clear has to be caught on the raw mousedown (button 1).
	if (args.empty() || m_model.settingsLocked || ev.GetParameter<int>("button", 0) != 1)
		return;
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;
	Int position = args[0].Get<int>();
	OnlineGameSetupActions::handleStartPositionMarkerRightClick(game, position);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onStartingCashChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	Money money;
	money.deposit((UnsignedInt)max(0, m_model.startingCash), FALSE, FALSE);
	OnlineGameSetupActions::setStartingCash(money);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSuperweaponsChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineGameSetupActions::setSuperweaponRestriction(m_model.superweaponsRestricted ? TRUE : FALSE);
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onSelectMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!OnlineGameSetupActions::canOpenMapSelect(game))
		return;

	// Same as RmlLanGameSetupScreen::onSelectMap(): hide this screen and open the map-select screen
	// through the registry instead of a legacy window layout, same shape as LanGameOptionsMenu.cpp's
	// showLANGameOptionsUnderlyingGUIElements(FALSE) precedent.
	hide();
	RmlUiScreenRegistry::open("Menus/WOLMapSelectMenu.wnd");
}

void RmlOnlineGameSetupScreen::onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	NGMPGame *game = OnlineGameSetupSession::getCurrentGame();
	if (!game)
		return;

	if (OnlineGameSetupSession::isHost())
	{
		OnlineGameSetupActions::StartPressCallbacks callbacks;
		callbacks.chatLine = [this](const UnicodeString &text, Color color) { onChatLine(unicodeToUtf8(text), colorToCss(color)); };
		callbacks.setStartButtonEnabled = [this](Bool enabled) { setStartButtonEnabled(enabled == TRUE); };
		callbacks.setBackButtonEnabled = [this](Bool enabled) { setBackButtonEnabled(enabled == TRUE); };
		// setSelectMapButtonEnabled left unset: select_map's data-attr-disabled already reads
		// is_host && !settings_locked, which lockSettings()/onBecameHost() already drive.
		OnlineGameSetupActions::pressStart(game, callbacks);
	}
	else
	{
		OnlineGameSetupActions::requestAccept(game);
		setStartButtonEnabled(false);
	}
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

void RmlOnlineGameSetupScreen::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	// Change fires on every edit; only Enter (linebreak) commits, like the .wnd's GEM_EDIT_DONE.
	if (!ev.GetParameter<bool>("linebreak", false))
		return;

	sendChatEntry(ev);
}

// The send button (ButtonEmote) takes the same path as Enter: slash commands first, then plain chat.
void RmlOnlineGameSetupScreen::onSendChat(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	sendChatEntry(ev);
}

void RmlOnlineGameSetupScreen::sendChatEntry(Rml::Event &ev)
{
	UnicodeString text = utf8ToUnicode(m_model.chatEntryText);
	text.trim();
	if (!text.isEmpty())
	{
		if (!OnlineGameSetupActions::handleSlashCommand(text, [this](const UnicodeString &t, Color c) { onChatLine(unicodeToUtf8(t), colorToCss(c)); }))
			OnlineGameSetupActions::sendChat(text);
	}
	m_model.chatEntryText.clear();
	RmlClearChatInput(ev);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_entry_text");
}

void RmlOnlineGameSetupScreen::onCommunicatorClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OnlineGameSetupActions::toggleCommunicatorOverlay();
}

//-------------------------------------------------------------------------------------------------
void RmlOnlineGameSetupScreen::onChatLine(const Rml::String &text, const Rml::String &color)
{
	ChatLineModel line;
	line.text = text;
	line.color = color;
	m_model.chatLines.push_back(line);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_lines");
}

void RmlOnlineGameSetupScreen::onBecameHost()
{
	// Relabel back to GUI:Start/re-enable Select Map happens naturally: is_host drives both
	// (the .rml's gametext data-if and select_map's data-attr-disabled) once refreshFromGameState()
	// picks up amIHost() == TRUE next tick; startEnabled/backEnabled are restored explicitly here,
	// same as WOLGameSetupMenu.cpp's sink.becameHost.
	m_model.startEnabled = true;
	m_model.backEnabled = true;
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("start_enabled");
		m_modelHandle.DirtyVariable("back_enabled");
	}
	refreshFromGameState();
}

void RmlOnlineGameSetupScreen::setBackButtonEnabled(bool enabled)
{
	m_model.backEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("back_enabled");
}

void RmlOnlineGameSetupScreen::setStartButtonEnabled(bool enabled)
{
	m_model.startEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("start_enabled");
}

void RmlOnlineGameSetupScreen::setCommunicatorButtonEnabled(bool enabled)
{
	m_model.communicatorEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("communicator_enabled");
}

void RmlOnlineGameSetupScreen::lockSettings()
{
	m_model.settingsLocked = true;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("settings_locked");
}

void RmlOnlineGameSetupScreen::onCommunicatorCountChanged(int numNotifications)
{
	UnicodeString buttonText;
	if (numNotifications > 0)
		buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
	else
		buttonText = TheGameText->fetch("GUI:Buddies");
	m_model.communicatorLabel = unicodeToUtf8(buttonText);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("communicator_label");
}

void RmlOnlineGameSetupScreen::returnFromMapSelect()
{
	if (!m_document)
		return;
	refreshFromGameState();
	m_document->Show();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOnlineGameSetupScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlOnlineGameSetupScreen::instance());
}

void CloseRmlOnlineGameSetupScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlOnlineGameSetupScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}

void ReturnToRmlOnlineGameSetupScreen()
{
	RmlOnlineGameSetupScreen::instance().returnFromMapSelect();
}
