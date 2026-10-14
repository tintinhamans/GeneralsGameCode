/*
**	Command & Conquer Generals Zero Hour(tm)
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

// Live stats export: builds the wire messages (StatsExporter.proto) from Player/ScoreKeeper/Object state
// and hands them to the StatsExporterSink (StatsExporterPipe.h). Every entry point is a cheap
// early-out while the stats pipe is disabled.
#pragma once

#include "Lib/BaseType.h"
#include "Common/GameType.h"	// ObjectID
#include "Common/UnicodeString.h"

class Object;
class Player;
class ThingTemplate;
class SpecialPowerTemplate;
class AsciiString;
class GameInfo;
struct Coord3D;

enum StatsExporterChatChannel
{
	STATS_EXPORTER_CHAT_MATCH = 0,
	STATS_EXPORTER_CHAT_GAME_LOBBY = 1,
	STATS_EXPORTER_CHAT_ROOM = 2,
	STATS_EXPORTER_CHAT_LAN_LOBBY = 3,
	STATS_EXPORTER_CHAT_LAN_GAME = 4
};

/// Main-thread tick that also runs in the shell: sends UiState changes and runs pending ChatSend requests.
void StatsExporterLiveTick();

/// An in-match chat line that is being shown to the local player. `playerMask` is the line's recipient mask.
void StatsExporterLiveChatInGame(Int senderSlot, const UnicodeString& senderName, const UnicodeString& text, Int playerMask, Bool isLocal);

/// A game lobby or room chat line that is being shown to the local player. `utf8Text` is the line as the server formatted it.
void StatsExporterLiveChatOnline(Int channel, Int64 userId, const char* senderName, const char* utf8Text, Bool isAction, Bool isSystem, Bool isLocal);

/// A LAN lobby or LAN game setup chat line that is being shown to the local player.
void StatsExporterLiveChatLan(Int channel, const UnicodeString& senderName, const UnicodeString& text, Bool isAction, Bool isSystem, Bool isLocal);

/// One command executed for `playerIndex` (a GameMessage type), counted for the player's actions per minute.
/// Called from GameLogic::logicMessageDispatcher, where every client sees the same synchronized commands.
void StatsExporterLiveCommand(Int playerIndex, Int messageType);

/// Once per rendered frame, after the client update has moved the views (also while paused and in replays). Sends the Camera
/// frame when the tactical view changed. Reads only.
void StatsExporterLiveCamera();

/// Once per GameLogic::update() tick. Sends the MatchInfo STARTED on (re)connect and, at the configured interval, the StatsSnapshot.
void StatsExporterLiveUpdate();

/// Once from the score screen, before the game data is cleared.
void StatsExporterLiveMatchSummary();

/// Once, in the loading screen, right after the engine resolved random sides, colors and start positions into `game`'s
/// slots. Sends MatchInfo LOADING from those resolved slots. Reads only; no random draws.
void StatsExporterLiveMatchLoading(GameInfo* game, UnsignedInt randomCrcBeforeResolve);

/// Once, right after a real (non-shell) match is fully set up. The STARTED frame goes out on the next
/// tick, and again on every later reader connect.
void StatsExporterLiveMatchStarted();

/// Once when the match is over. `replayPath` is the absolute path of the closed replay file, or empty if
/// none exists; `replayIsPlayback` is TRUE for a .rep file being watched. Later calls for the same match are no-ops.
void StatsExporterLiveMatchEnded(const AsciiString& replayPath, Bool replayIsPlayback);

// -- Discrete event hooks, each at its outcome convergence point; playerIndex is the acting player ----

void StatsExporterLiveAbilityUse(Int playerIndex, const SpecialPowerTemplate* power,
	const ThingTemplate* sourceTmpl, const ThingTemplate* targetTmpl,
	const Coord3D* location, const char* variant);
void StatsExporterLiveScienceAcquired(Int playerIndex, Int rankLevel, Int sciencePurchasePoints, const char* scienceName);
void StatsExporterLiveBuild(Int playerIndex, const ThingTemplate* tmpl);
void StatsExporterLiveKill(Int playerIndex, const ThingTemplate* victimTmpl);
void StatsExporterLiveLoss(Int playerIndex, const ThingTemplate* victimTmpl);
void StatsExporterLiveEvacuate(Int playerIndex, const ThingTemplate* containerTmpl, UnsignedInt passengerCount);

/// Birth-time production veterancy is excluded.
void StatsExporterLiveVeterancyChange(Int playerIndex, const ThingTemplate* tmpl, ObjectID objectId, Int oldLevel, Int newLevel);

/// upgradeKind is "WEAPON" or "ARMOR".
enum StatsExporterSpendCategory
{
	STATS_EXPORTER_SPEND_UNITS = 0,
	STATS_EXPORTER_SPEND_STRUCTURES = 1,
	STATS_EXPORTER_SPEND_UPGRADES = 2,
	STATS_EXPORTER_SPEND_SCIENCES = 3
};

/// Money (or general points for sciences) spent, at the places the ScoreKeeper counts money spent.
void StatsExporterLiveSpend(Int playerIndex, Int category, Int amount);

/// A structure finished selling and its refund was paid.
void StatsExporterLiveSell(Int playerIndex, const ThingTemplate* tmpl, UnsignedInt refund);

/// A beacon was placed (from the beacon placement command).
void StatsExporterLiveBeacon(Int playerIndex, const Coord3D* location);

/// A player left the match: `slot` is the network slot from the player-leave command ("left"), or a player index
/// from the self destruct (surrender/quit) command; `variant` says which.
void StatsExporterLivePlayerLeftSlot(Int slot, const char* variant);
void StatsExporterLivePlayerLeft(Int playerIndex, const char* variant);

/// The engine marked the player defeated.
void StatsExporterLivePlayerDefeated(Int playerIndex);

/// A player's radar state changed (RadarState values: 0 none, 1 unpowered, 2 active). No event if old == new.
void StatsExporterLiveRadarChanged(Int playerIndex, Int oldState, Int newState);

/// The battle plan affecting a player's troops changed (BattlePlanStatus values). No event if old == new.
void StatsExporterLiveBattlePlanChanged(Int playerIndex, Int oldPlan, Int newPlan);

/// A player-level upgrade completed (isObjectUpgrade FALSE), or an object-level one (TRUE).
void StatsExporterLiveUpgradeComplete(Int playerIndex, const char* upgradeName, UnsignedInt cost, Bool isObjectUpgrade);

/// A structure changed owner by capture. Reads only.
void StatsExporterLiveCapture(const Object* captured, const Player* oldOwner, const Player* newOwner);

/// The initial capture bonus a capture just paid; attached to the capture event that follows in the same call.
void StatsExporterLiveNoteCaptureBonus(Int amount);

/// A player's power was sabotaged for `durationFrames` logic frames.
void StatsExporterLivePowerSabotaged(Int playerIndex, UnsignedInt durationFrames);

void StatsExporterLiveSalvageUpgrade(Int playerIndex, const ThingTemplate* tmpl, const char* upgradeKind, Int tier);
