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

#include "W3DDevice/GameClient/RmlUi/RmlLanGameSetupScreen.h"

#include "Common/AsciiString.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/UnicodeString.h"
#include "Common/UserPreferences.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GUICallbacks/Menus/LanGameSetupActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/LanGameSetupData.h"
#include "GameClient/MapUtil.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/LANPlayer.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <cstdio>
#include <map>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
// Same private per-file helper every RmlScreen keeps (see e.g. RmlSkirmishSetupScreen.cpp).
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

// Chat text entry is plain ASCII, same limitation the .wnd TextEntry gadget always had -- see
// RmlLanLobbyScreen::utf8ToUnicode().
static UnicodeString utf8ToUnicode(const Rml::String &utf8)
{
	AsciiString ascii(utf8.c_str());
	UnicodeString text;
	text.translate(ascii);
	return text;
}

static Rml::String rgbToHex(UnsignedInt rgb)
{
	char hex[8];
	_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X",
		(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return Rml::String(hex);
}

// Fallback for an unset/unmatched color, see RmlSkirmishSetupScreen.cpp's kNoColorHex.
static const char *const kNoColorHex = "transparent";

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

// Mirrors playerTooltip()/setLANPlayerTooltip() (LanGameOptionsMenu.cpp): the LAN identity tooltip
// for an occupied human slot.
static Rml::String lanPlayerTooltipFor(LANPlayer *player)
{
	if (!player)
		return Rml::String();

	UnicodeString tooltip;
	if (!player->getLogin().isEmpty() || !player->getHost().isEmpty())
		tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getLogin().str(), player->getHost().str());

#if defined(RTS_DEBUG)
	UnicodeString ip;
	ip.format(L" - %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(player->getIP()));
	tooltip.concat(ip);
#endif

	return unicodeToUtf8(tooltip);
}

//-------------------------------------------------------------------------------------------------
RmlLanGameSetupScreen &RmlLanGameSetupScreen::instance()
{
	static RmlLanGameSetupScreen s_screen;
	return s_screen;
}

void RmlLanGameSetupScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("langamesetup");
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
			rowHandle.RegisterMember("faction_tooltip", &SlotRowModel::factionTooltip);
			rowHandle.RegisterMember("color", &SlotRowModel::color);
			rowHandle.RegisterMember("color_hex", &SlotRowModel::colorHex);
			rowHandle.RegisterMember("team_number", &SlotRowModel::teamNumber);
			rowHandle.RegisterMember("start_position", &SlotRowModel::startPosition);
			rowHandle.RegisterMember("accepted", &SlotRowModel::accepted);
			rowHandle.RegisterMember("has_map", &SlotRowModel::hasMap);
			rowHandle.RegisterMember("show_accept", &SlotRowModel::showAccept);
			rowHandle.RegisterMember("player_tooltip", &SlotRowModel::playerTooltip);
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

		Rml::StructHandle<OptionModel> optionHandle = constructor.RegisterStruct<OptionModel>();
		if (optionHandle)
		{
			optionHandle.RegisterMember("value", &OptionModel::value);
			optionHandle.RegisterMember("label", &OptionModel::label);
		}

		constructor.RegisterArray<Rml::Vector<SlotRowModel>>();
		constructor.RegisterArray<Rml::Vector<StartMarkerModel>>();
		constructor.RegisterArray<Rml::Vector<OptionModel>>();
		constructor.RegisterArray<Rml::Vector<Rml::String>>();

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
		constructor.Bind("is_host", &m_model.isHost);
		constructor.Bind("start_enabled", &m_model.startEnabled);
		constructor.Bind("chat_lines", &m_model.chatLines);
		constructor.Bind("chat_entry_text", &m_model.chatEntryText);

		constructor.BindEventCallback("slot_occupant_changed", &RmlLanGameSetupScreen::onSlotOccupantChanged, this);
		constructor.BindEventCallback("slot_faction_changed", &RmlLanGameSetupScreen::onSlotFactionChanged, this);
		constructor.BindEventCallback("slot_color_changed", &RmlLanGameSetupScreen::onSlotColorChanged, this);
		constructor.BindEventCallback("slot_team_changed", &RmlLanGameSetupScreen::onSlotTeamChanged, this);
		constructor.BindEventCallback("start_marker_clicked", &RmlLanGameSetupScreen::onStartPositionMarkerClick, this);
		constructor.BindEventCallback("start_marker_mousedown", &RmlLanGameSetupScreen::onStartPositionMarkerMouseDown, this);
		constructor.BindEventCallback("starting_cash_changed", &RmlLanGameSetupScreen::onStartingCashChanged, this);
		constructor.BindEventCallback("superweapons_changed", &RmlLanGameSetupScreen::onSuperweaponsChanged, this);
		constructor.BindEventCallback("select_map", &RmlLanGameSetupScreen::onSelectMap, this);
		constructor.BindEventCallback("start", &RmlLanGameSetupScreen::onStart, this);
		constructor.BindEventCallback("back", &RmlLanGameSetupScreen::onBackPressed, this);
		constructor.BindEventCallback("chat_entry_committed", &RmlLanGameSetupScreen::onChatEntryCommitted, this);
		constructor.BindEventCallback("send_emote", &RmlLanGameSetupScreen::onSendEmote, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/LanGameOptions.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlLanGameSetupScreen::connectSignals()
{
	m_connections.disconnect();
	m_connections.add(LanGameSetupSignals::slotsChanged().connect([this](LANGameInfo *game) { onSlotsChanged(game); }));
	m_connections.add(LanGameSetupSignals::optionsChanged().connect([this](LANGameInfo *game) { onOptionsChanged(game); }));
	m_connections.add(LanGameSetupSignals::startButton().connect([this](Bool enabled) { onStartButtonEnabledChanged(enabled == TRUE); }));
	m_connections.add(LanGameSetupSignals::chatLine().connect([this](const UnicodeString &line, Color) { onChatLine(unicodeToUtf8(line)); }));
}

//-------------------------------------------------------------------------------------------------
void RmlLanGameSetupScreen::refreshFromGameState()
{
	LANGameInfo *game = TheLAN ? TheLAN->GetMyGame() : nullptr;
	LanGameSetupData data = LanGameSetupData::build(game, m_model.startEnabled);

	m_model.isHost = data.m_isHost == TRUE;

	m_model.slots.clear();
	for (size_t i = 0; i < data.m_slots.size(); ++i)
	{
		const LanGameSetupSlotRow &src = data.m_slots[i];
		const GameSetupSlotRow &base = src.m_base;

		SlotRowModel row;
		row.slotIndex = (int)i;
		row.occupantState = (int)base.m_state;
		row.isOccupied = base.m_state != SLOT_OPEN && base.m_state != SLOT_CLOSED;
		row.isHumanOccupant = base.m_state == SLOT_PLAYER;
		row.occupantLabel = unicodeToUtf8(base.m_name);
		row.canEdit = base.m_canEdit == TRUE;
		row.canEditOccupant = row.canEdit && !base.m_isLocalSlot;
		row.playerName = unicodeToUtf8(base.m_name);
		row.playerTemplate = base.m_playerTemplate;
		row.color = base.m_color;
		row.colorHex = kNoColorHex;
		row.teamNumber = base.m_teamNumber;
		row.startPosition = base.m_startPosition;
		row.accepted = src.m_accepted == TRUE;
		row.hasMap = src.m_hasMap == TRUE;
		row.showAccept = (i == 0);
		row.factionTooltip = factionTooltipFor(base.m_playerTemplate);

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
				row.colorHex = rgbToHex(color.m_rgb);
				break;
			}
		}

		if (row.isHumanOccupant && game)
		{
			LANGameSlot *slot = game->getLANSlot((Int)i);
			row.playerTooltip = lanPlayerTooltipFor(slot ? slot->getUser() : nullptr);
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

		for (const LanGameSetupSlotRow &srcRow : data.m_slots)
		{
			const GameSetupSlotRow &src = srcRow.m_base;
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

	// Per-variable Dirty*() instead of DirtyAllVariables() -- see RmlSkirmishSetupScreen::
	// refreshFromGameState()'s comment for why (start_markers is fixed-size but slots'/markers'
	// per-row contents still change shape often enough that dirtying only what changed matters).
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
		m_modelHandle.DirtyVariable("is_host");
	}
}

//-------------------------------------------------------------------------------------------------
void RmlLanGameSetupScreen::show()
{
	if (!m_document || !TheLAN)
		return;

	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game)
		return;

	if (game->isGameInProgress())
	{
		// Same as LanGameOptionsMenuInit(): returning here after a match means going straight back
		// to the lobby.
		if (TheShell)
			TheShell->popImmediate();
		return;
	}

	m_model.chatLines.clear();
	m_model.chatEntryText.clear();
	m_model.startEnabled = true;

	TheMapCache->updateCache();

	if (TheLAN->AmIHost())
	{
		LANGameSlot *slot = game->getLANSlot(0);
		LANPreferences pref;
		slot->setColor(pref.getPreferredColor());
		slot->setPlayerTemplate(pref.getPreferredFaction());
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap(pref.getPreferredMap());
		game->setStartingCash(pref.getStartingCash());
		game->setSuperweaponRestriction(pref.getSuperweaponRestricted() ? 1 : 0);

		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			game->getSlot(0)->setMapAvailability(true);
			game->setMapCRC(it->second.m_CRC);
			game->setMapSize(it->second.m_filesize);
			game->adjustSlotsForMap();
		}

		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		TheLAN->RequestGameAnnounce();
	}
	else
	{
		game->setMapCRC(game->getMapCRC()); // force a recheck
		game->setMapSize(game->getMapSize()); // of if we have the map
		TheLAN->RequestHasMap();
	}

	refreshFromGameState();

	connectSignals();

	m_document->Show();
}

void RmlLanGameSetupScreen::hide()
{
	if (m_document)
		m_document->Hide();

	m_connections.disconnect();
}

bool RmlLanGameSetupScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlLanGameSetupScreen::onBack()
{
	LanGameSetupActions::leaveGame();
	hide();
	if (TheShell)
		TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
void RmlLanGameSetupScreen::onSlotOccupantChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || !TheLAN)
		return;
	Int slotIndex = args[0].Get<int>();
	Int state = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	Bool isAIChanged = FALSE;
	LanGameSetupActions::selectPlayerState(TheLAN->GetMyGame(), slotIndex, (SlotState)state, &isAIChanged);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onSlotFactionChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || !TheLAN)
		return;
	Int slotIndex = args[0].Get<int>();
	Int playerTemplate = atoi(ev.GetParameter<Rml::String>("value", "0").c_str());
	Bool observerChanged = FALSE;
	LanGameSetupActions::selectPlayerTemplate(TheLAN->GetMyGame(), slotIndex, playerTemplate, FALSE, &observerChanged);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onSlotColorChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || !TheLAN)
		return;
	Int slotIndex = args[0].Get<int>();
	Int color = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	LanGameSetupActions::selectColor(TheLAN->GetMyGame(), slotIndex, color, FALSE);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onSlotTeamChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (args.empty() || !TheLAN)
		return;
	Int slotIndex = args[0].Get<int>();
	Int team = atoi(ev.GetParameter<Rml::String>("value", "-1").c_str());
	LanGameSetupActions::selectTeam(TheLAN->GetMyGame(), slotIndex, team, FALSE);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onStartPositionMarkerClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty() || !TheLAN)
		return;
	Int position = args[0].Get<int>();
	LanGameSetupActions::handleStartPositionMarkerClick(TheLAN->GetMyGame(), position);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onStartPositionMarkerMouseDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	// GBM_SELECTED_RIGHT equivalent: RmlUi only synthesizes "click" for the left button,
	// so the right-click marker clear has to be caught on the raw mousedown (button 1).
	if (args.empty() || !TheLAN || ev.GetParameter<int>("button", 0) != 1)
		return;
	Int position = args[0].Get<int>();
	LanGameSetupActions::handleStartPositionMarkerRightClick(TheLAN->GetMyGame(), position);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onStartingCashChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (!TheLAN)
		return;
	Money money;
	money.deposit((UnsignedInt)max(0, m_model.startingCash), FALSE, FALSE);
	LanGameSetupActions::setStartingCash(TheLAN->GetMyGame(), money, FALSE);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onSuperweaponsChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (!TheLAN)
		return;
	LanGameSetupActions::setSuperweaponRestriction(TheLAN->GetMyGame(), m_model.superweaponsRestricted ? TRUE : FALSE, FALSE);
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onSelectMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Same as LanGameOptionsMenu.cpp's ButtonSelectMap: hide this screen while the map-select
	// overlay is up, same shape as the .wnd's showLANGameOptionsUnderlyingGUIElements(FALSE).
	hide();
	RmlUiScreenRegistry::open("Menus/LanMapSelectMenu.wnd");
}

void RmlLanGameSetupScreen::onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (!TheLAN)
		return;
	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game)
		return;

	if (TheLAN->AmIHost())
	{
		if (LanGameSetupActions::validateStart(game) == LanGameSetupActions::STARTVALIDATION_READY)
		{
			LanGameSetupActions::startGame(game);
			m_model.startEnabled = false;
			if (m_modelHandle)
				m_modelHandle.DirtyVariable("start_enabled");
		}
		// else: validateStart() already sent whatever chat notice/RequestAccept() applies.
	}
	else
	{
		LanGameSetupActions::requestAccept(game);
		m_model.startEnabled = false;
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("start_enabled");
	}
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	onBack();
}

void RmlLanGameSetupScreen::onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (TheLAN)
	{
		UnicodeString text = utf8ToUnicode(m_model.chatEntryText);
		text.trim();
		if (!text.isEmpty())
			TheLAN->RequestChat(text, LANAPIInterface::LANCHAT_NORMAL);
	}
	m_model.chatEntryText.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_entry_text");
}

void RmlLanGameSetupScreen::onSendEmote(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors ButtonEmote's actual behavior: unlike RmlLanLobbyScreen's onSendChat() (whose ButtonEmote
	// really sends normal chat), the LAN game setup screen's ButtonEmote genuinely sends LANCHAT_EMOTE.
	if (TheLAN)
	{
		UnicodeString text = utf8ToUnicode(m_model.chatEntryText);
		text.trim();
		if (!text.isEmpty())
			TheLAN->RequestChat(text, LANAPIInterface::LANCHAT_EMOTE);
	}
	m_model.chatEntryText.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_entry_text");
}

//-------------------------------------------------------------------------------------------------
// LanGameSetupSignals::slotsChanged/optionsChanged targets: full snapshot rebuild, same
// reasoning as RmlLanLobbyScreen::onPlayerListChanged()/onGameListChanged().
void RmlLanGameSetupScreen::onSlotsChanged(LANGameInfo * /*game*/)
{
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onOptionsChanged(LANGameInfo * /*game*/)
{
	refreshFromGameState();
}

void RmlLanGameSetupScreen::onStartButtonEnabledChanged(bool enabled)
{
	m_model.startEnabled = enabled;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("start_enabled");
}

// LanGameSetupSignals::chatLine target. No echo-on-send: LAN chat is a broadcast the sender receives too,
// same as RmlLanLobbyScreen::onChatLine().
void RmlLanGameSetupScreen::onChatLine(const Rml::String &line)
{
	m_model.chatLines.push_back(line);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_lines");
}

void RmlLanGameSetupScreen::returnFromMapSelect()
{
	if (!m_document)
		return;
	refreshFromGameState();

	connectSignals();

	m_document->Show();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlLanGameSetupScreen()
{
	if (!TheRmlUiManager)
		return;
	TheRmlUiManager->showScreen(&RmlLanGameSetupScreen::instance());
}

void CloseRmlLanGameSetupScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlLanGameSetupScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}

void ReturnToRmlLanGameSetupScreen()
{
	RmlLanGameSetupScreen::instance().returnFromMapSelect();
}
