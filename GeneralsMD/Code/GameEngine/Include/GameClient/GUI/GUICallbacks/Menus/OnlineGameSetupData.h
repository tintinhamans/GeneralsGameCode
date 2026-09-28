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

// FILE: OnlineGameSetupData.h //////////////////////////////////////////////////
// Widget-agnostic Generals Online (NGMP) game setup content: layers online-only
// per-slot/per-screen state (accepted flag, map availability, host slot, live
// mesh connection info) on top of GameSetupData, the same slot/options snapshot
// LAN and skirmish setup already share. Built from an NGMPGame (a GameInfo) so
// WOLGameSetupMenu.cpp's .wnd path and a future RmlUi screen render the same
// thing. Read-only snapshot: no side effects, safe to call every frame a
// renderer needs to refresh.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/GameSetupData.h"

#include <string>
#include <vector>

class NGMPGame;
class NGMPGameSlot;
class NGMP_OnlineServices_LobbyInterface;
enum class EConnectionState : uint8_t;

// Live mesh connection info for one remote human slot, as shown by the player tooltip
// (WOLGameSetupMenu.cpp:playerTooltip) and the per-slot connection indicator
// (WOLConnectionIndicatorDraw / WOLRefreshConnectionIndicators). Not meaningful for the
// local player's own slot (no PlayerConnection exists for yourself) or non-human slots.
struct OnlineGameSetupConnectionInfo
{
	Bool m_isLocalPlayer = FALSE; // true for the slot the local machine occupies -- no PlayerConnection to read
	Bool m_isConnected = FALSE;   // true only when m_state == EConnectionState::CONNECTED_DIRECT
	EConnectionState m_state = (EConnectionState)0; // NOT_CONNECTED; see PluginInterfaces.h
	Int m_score = -1;         // PlayerConnection::ComputeConnectionScore(), -1 if unknown
	Int m_latencyMs = -1;     // PlayerConnection::GetLatency(), -1 if unknown
	Int m_jitterMs = -1;      // PlayerConnection::GetJitter(), -1 if unknown
	Int m_qualityPct = -1;    // PlayerConnection::GetConnectionQuality() * 100, -1 if unknown
	std::string m_connectionType; // PlayerConnection::GetConnectionType()
	std::string m_region;         // LobbyMemberEntry::region for this slot's user
};

// One slot row: the shared GameSetupSlotRow fields, plus the online-only bits
// (SkirmishSetupActions/LanGameSetupData have no equivalent connection info).
struct OnlineGameSetupSlotRow
{
	GameSetupSlotRow m_base;
	Bool m_accepted = FALSE;   // GameSlot::isAccepted(); non-human slots are always accepted
	Bool m_hasMap = TRUE;      // GameSlot::hasMap(); only meaningful for human slots
	Bool m_isHostSlot = FALSE; // slot 0 always holds the host (WOLDisplaySlotList's DEBUG_ASSERTCRASH)

	// Only populated (m_connectionType/m_region aside) for a human, non-local slot -- see
	// OnlineGameSetupConnectionInfo.
	OnlineGameSetupConnectionInfo m_connection;
};

struct OnlineGameSetupData
{
	std::vector<OnlineGameSetupSlotRow> m_slots; // always MAX_SLOTS entries
	GameSetupOptionsData m_options;
	Bool m_isHost = FALSE;

	UnicodeString m_gameName; // StaticTextGameName / window title, theGameInfo->getGameName()

	// WOLDisplayGameOptions()'s TextEntryMapDisplay text: the map's display name only once the
	// local slot already has the map, otherwise the raw map filename -- distinct from
	// m_options.m_mapDisplayName, which is unconditional (map-cache lookup only).
	UnicodeString m_mapDisplayText;

	Bool m_useStats = FALSE;    // theGameInfo->getUseStats(); drives CheckBoxUseStats + TOOLTIP:UseStatsOn/Off
	Bool m_limitArmies = FALSE; // theGameInfo->oldFactionsOnly(); drives CheckBoxLimitArmies

	// CheckboxLimitSuperweapons / ComboBoxStartingCash are host-only edits (client-disabled);
	// GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES is always on for this fork, so the
	// stats-match lockout in WOLDisplayGameOptions()/WOLGameSetupMenuInit() never triggers --
	// enabled state is simply "am I the host".
	Bool m_cashAndSuperweaponsEnabled = FALSE;

	// Snapshot the current slots, map/options state, and live mesh connection info out of the
	// current NGMP lobby game.
	static OnlineGameSetupData build( NGMPGame *game );

	// Shared by build() (per remote human slot) and playerTooltip()/WOLRefreshConnectionIndicators()
	// so the mesh connection read only happens in one place.
	static OnlineGameSetupConnectionInfo computeConnectionInfo( NGMP_OnlineServices_LobbyInterface *pLobbyInterface, NGMPGame *game, NGMPGameSlot *slot );
};
