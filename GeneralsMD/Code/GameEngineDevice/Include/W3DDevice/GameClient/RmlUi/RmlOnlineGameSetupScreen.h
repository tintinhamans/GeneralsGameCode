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

// FILE: RmlOnlineGameSetupScreen.h ////////////////////////////////////////////
// RmlScreen for Data/UI/OnlineGameSetup.rml. Registered for
// Menus/GameSpyGameOptionsMenu.wnd (RmlOnlineLobbyScreen pushes that path once a
// lobby is created/joined -- see WOLLobbyMenu.cpp's nextScreen assignments this
// mirrors). Same shape as RmlLanGameSetupScreen, but driven off the widget-agnostic
// online layer (OnlineGameSetupData/OnlineGameSetupActions/OnlineGameSetupSession)
// instead of LanGameSetupData/LanGameSetupActions/LANAPICallbacks -- see
// WOLGameSetupMenu.cpp for the .wnd this reproduces:
//   - show() calls OnlineGameSetupSession::prepareGameState() (same as
//     WOLGameSetupMenuInit() does before its widget setup) then enter(), same
//     lifetime pattern as the .wnd's Init/Shutdown pair.
//   - the per-frame OnlineGameSetupSession::update() call happens from this
//     screen's update() override (called every frame by RmlUiManager::update()
//     while this screen is visible); a TRUE return means the host left and the
//     screen must stop processing this frame, same as WOLGameSetupMenuUpdate()'s
//     early return.
//   - hide() calls leave(); Back calls backToLobby() (leaveLobby() + TheShell->pop()),
//     same as PopBackToLobby().
// The OnlineGameSetupSignals listeners (see OnlineGameSetupSession.h) all land on m_model fields and
// DirtyVariable() calls instead of GameWindow calls -- chat lines, slot/options
// refresh, host migration, Back/Start/Communicator enabled state, the last-second
// settings lock, and the Communicator notification badge.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

class NGMPGame;

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlOnlineGameSetupScreen : public RmlScreen
{
public:
	static RmlOnlineGameSetupScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as ButtonBack
	virtual void update() override; // OnlineGameSetupSession::update(), same as WOLGameSetupMenuUpdate()'s non-widget slice

	void refreshFromGameState(); // OnlineGameSetupData::build(current game) -> m_model; public, see below

private:
	RmlOnlineGameSetupScreen() {}

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
	void onChatEntryCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // Enter, mirrors TextEntryChat GEM_EDIT_DONE
	void onCommunicatorClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // ButtonCommunicator

public:
	// Called by the (not yet converted) online map-select screen when it closes: re-shows this
	// screen and refreshes it from the current game, without show()'s prepareGameState()-style reset.
	void returnFromMapSelect();

	// OnlineGameSetupSignals / OnlineGameSetupActions::StartPressCallbacks targets --
	// public so the free functions in the .cpp (which close over a raw screen pointer) can
	// reach them without befriending the class.
	// color is a pre-formatted "rgba(r,g,b,a)" string (see colorToCss() in the .cpp), same idiom as
	// RmlOnlineLobbyScreen::ChatLineModel::color -- passed already-formatted rather than as a raw
	// Color so this declaration doesn't need Color visible yet.
	void onChatLine(const Rml::String &text, const Rml::String &color);
	void onBecameHost();
	void setBackButtonEnabled(bool enabled);
	void setStartButtonEnabled(bool enabled);
	void setCommunicatorButtonEnabled(bool enabled);
	void lockSettings();
	void onCommunicatorCountChanged(int numNotifications);

private:
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnections m_connections; // OnlineGameSetupSignals, connected while showing
	Rml::DataModelHandle m_modelHandle;

	// One slot row's worth of fields for the data-for-bound slot table (see load()). Same shape as
	// RmlLanGameSetupScreen::SlotRowModel, plus the online-only bits (connection state/tooltip)
	// OnlineGameSetupData layers on top of GameSetupSlotRow.
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
		bool showAccept = false; // true only for slot 0 -- Generals Online has a single local-player
		                          // accept indicator, not one per row (buttonAccept[1..7] stay hidden)

		// Live mesh connection info (human, non-local slots only -- see OnlineGameSetupConnectionInfo).
		// is_connected drives the indicator's CSS (.conn-indicator.connected, see OnlineGameSetup.rcss).
		bool isConnected = false;
		Rml::String connectionTooltip; // formatted from region/latency/jitter/quality/score, empty if none
	};

	// One start-position marker on the map preview -- identical shape to
	// RmlLanGameSetupScreen::StartMarkerModel.
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

	// One chat/system-notice line (see OnlineGameSetupActions::ChatLineFn / OnlineGameSetupSignals::
	// chatLine). color is bound with data-style-color on a child span of the data-for row
	// (see OnlineGameSetup.rml), the same idiom RmlOnlineLobbyScreen/RmlLanLobbyScreen use for their
	// chat -- same shape as RmlOnlineLobbyScreen::ChatLineModel.
	struct ChatLineModel
	{
		Rml::String text;
		Rml::String color;
	};

	struct Model
	{
		Rml::Vector<SlotRowModel> slots;
		Rml::Vector<StartMarkerModel> startMarkers;
		Rml::Vector<OptionModel> factionOptions;
		Rml::Vector<OptionModel> colorOptions;
		Rml::Vector<OptionModel> startingCashOptions;

		Rml::String gameName; // StaticTextGameName, theGameInfo->getGameName()
		Rml::String mapName; // map filename, for <mappreview data-attr-map>
		Rml::String mapDisplayText; // TextEntryMapDisplay text (OnlineGameSetupData::m_mapDisplayText)
		bool mapFound = false;

		int startingCash = 0;
		bool superweaponsRestricted = false;
		bool useStats = false;    // CheckBoxUseStats -- always shown, never editable here
		bool limitArmies = false; // CheckBoxLimitArmies -- always shown, never editable here

		bool isHost = false; // gates host-only controls; also picks the Start/Accept caption
		bool cashAndSuperweaponsEnabled = false; // OnlineGameSetupData::m_cashAndSuperweaponsEnabled
		bool startEnabled = true;
		bool backEnabled = true;
		bool settingsLocked = false; // WOLLockSettings(): last second of the match-start countdown

		bool communicatorEnabled = true;
		Rml::String communicatorLabel; // "GUI:Buddies" [+ " [n]"], formatted like the .wnd's buttonBuddy text

		Rml::Vector<ChatLineModel> chatLines; // append-only while the document is open
		Rml::String chatEntryText;
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlOnlineGameSetupScreen();
void CloseRmlOnlineGameSetupScreen();

// Called by RmlOnlineMapSelectScreen on OK/Back instead of OpenRmlOnlineGameSetupScreen(), which
// would re-run prepareGameState()-style setup and disturb host/client state already in progress.
void ReturnToRmlOnlineGameSetupScreen();
