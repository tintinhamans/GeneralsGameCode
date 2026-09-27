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

// FILE: RmlLanGameSetupScreen.h /////////////////////////////////////////////////
// RmlScreen for Data/UI/LanGameOptions.rml. Registered for
// Menus/LanGameOptionsMenu.wnd (LanLobbyActions::hostGame()/joinGame() push that
// path). Unlike the .wnd version this never gets a LanGameOptionsMenuInit()/
// Shutdown()/Update() callback, so:
//   - show() replays LanGameOptionsMenuInit()'s host/client setup (LANPreferences
//     read for the host, map CRC/size recheck + RequestHasMap() for the client,
//     RequestGameOptions()/RequestGameAnnounce()) itself, through
//     LanGameSetupActions/LanGameSetupData -- see LanGameOptionsMenu.cpp.
//   - hide() clears the four hooks below; leaving the game (LanGameSetupActions::
//     leaveGame()) only happens on Back, same as the .wnd's buttonBack handler.
// Slot/option state arrives via g_lanGameSetupSlotUpdateHook/
// g_lanGameSetupOptionsUpdateHook/g_lanGameSetupStartButtonHook, and chat via
// g_lanGameSetupChatHook (LANAPICallbacks.h), subscribed in show() and cleared in
// hide(), same lifetime pattern as RmlLanLobbyScreen's g_lanLobby*Hook. Slot/map
// data is built through LanGameSetupData/GameSetupData (widget-agnostic, see
// LanGameSetupData.h), same shape as RmlSkirmishSetupScreen for the parts they
// share.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

class LANGameInfo;

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlLanGameSetupScreen : public RmlScreen
{
public:
	static RmlLanGameSetupScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack

	// Hook targets (free functions in the .cpp forward into these); public so the free functions
	// can reach the singleton without befriending it.
	void onSlotsChanged(LANGameInfo *game);
	void onOptionsChanged(LANGameInfo *game);
	void onStartButtonEnabledChanged(bool enabled);
	void onChatLine(const Rml::String &line);

private:
	RmlLanGameSetupScreen() {}

	void refreshFromGameState(); // LanGameSetupData::build(TheLAN->GetMyGame()) -> m_model

	void onSlotOccupantChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotFactionChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotColorChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSlotTeamChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStartPositionMarkerClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStartPositionMarkerMouseDown(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStartingCashChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSuperweaponsChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectMap(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Enter, mirrors GEM_EDIT_DONE (LANCHAT_NORMAL)
	void onSendEmote(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // ButtonEmote, mirrors its real LANCHAT_EMOTE behavior

public:
	// Called by RmlLanMapSelectScreen (once converted) when it closes: re-shows this screen and
	// refreshes it from TheLAN->GetMyGame(), without show()'s LanGameOptionsMenuInit()-style reset.
	void returnFromMapSelect();

private:
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	// One slot row's worth of fields for the data-for-bound slot table (see load()). Same shape as
	// RmlSkirmishSetupScreen::SlotRowModel, plus the LAN-only bits (accepted/hasMap/showAccept/
	// tooltips) LanGameSetupData layers on top of GameSetupSlotRow.
	struct SlotRowModel
	{
		int slotIndex = 0;
		int occupantState = 0; // SlotState value
		bool isOccupied = false;
		bool isHumanOccupant = false;
		Rml::String occupantLabel;
		bool canEdit = false;
		bool canEditOccupant = false;
		Rml::String playerName;
		int playerTemplate = 0;
		Rml::String factionLabel;
		Rml::String factionTooltip; // "Army Tooltip" text, mirrors playerTemplateComboBoxTooltip()
		int color = -1;
		Rml::String colorHex;
		int teamNumber = -1;
		int startPosition = -1;

		bool accepted = false; // GameSlot::isAccepted()
		bool hasMap = true;    // GameSlot::hasMap(), human slots only
		bool showAccept = false; // true only for slot 0, mirrors the .wnd hiding buttonAccept[1..7]
		Rml::String playerTooltip; // LAN identity tooltip (login/host), mirrors playerTooltip(), empty if none
	};

	// One start-position marker on the map preview -- identical shape to
	// RmlSkirmishSetupScreen::StartMarkerModel; see that header's comment for why x_style/y_style
	// are pre-formatted strings and why this array is always MAX_SLOTS long.
	struct StartMarkerModel
	{
		int position = 0;
		Rml::String xStyle;
		Rml::String yStyle;
		bool isOccupied = false;
		Rml::String occupantLabel;
		Rml::String colorHex;
		bool used = false;
	};

	struct OptionModel
	{
		int value = 0;
		Rml::String label;
	};

	struct Model
	{
		Rml::Vector<SlotRowModel> slots;
		Rml::Vector<StartMarkerModel> startMarkers;
		Rml::Vector<OptionModel> factionOptions;
		Rml::Vector<OptionModel> colorOptions;
		Rml::Vector<OptionModel> startingCashOptions;

		Rml::String mapName;
		Rml::String mapDisplayName;
		bool mapFound = false;

		int startingCash = 0;
		bool superweaponsRestricted = false;

		bool isHost = false; // gates host-only controls; also picks the Start/Accept caption
		bool startEnabled = true; // mirrors LANEnableStartButton()/g_lanGameSetupStartButtonHook

		Rml::Vector<Rml::String> chatLines; // append-only while the document is open, see onChatLine()
		Rml::String chatEntryText;
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlLanGameSetupScreen();
void CloseRmlLanGameSetupScreen();

// Called by RmlLanMapSelectScreen (once converted) on OK/Back instead of OpenRmlLanGameSetupScreen(),
// which would re-run LanGameOptionsMenuInit()-style setup and disturb host/client state already in
// progress. Not wired up yet -- see report; LanMapSelectMenu.wnd is not converted in this change.
void ReturnToRmlLanGameSetupScreen();
