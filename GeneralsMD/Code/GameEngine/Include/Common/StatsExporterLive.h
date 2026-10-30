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
// early-out while no reader is connected (or there is no pipe, as in headless runs).
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

/// Main-thread tick that also runs in the shell: welcomes new readers (Hello), sends UiState and LobbyState changes, runs pending ChatSend
/// requests and answers the queued Requests.
void StatsExporterLiveTick();

/// The screen the game is on as the UiState frame reports it (a statsexporter::UiScreen value).
int StatsExporterLiveCurrentScreen();

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

/// Once per GameLogic::update() tick, at its start. Sends the MatchInfo STARTED on (re)connect and the StatsSnapshots
/// (light every 100 ms of game time, full every 250 ms by default; each reader can change that).
void StatsExporterLiveUpdate();

/// Once per GameLogic::update() tick, at its end (after the object updates). Sends the Events of that logic frame.
void StatsExporterLiveFrameEnd();

/// From GameLogic::clearGameData(): the match is over whatever the mode (quit, replay left early, disconnect). Sends ENDED
/// unless it was already sent; a no-op without a running match.
void StatsExporterLiveGameDataCleared();

/// Once from the score screen, before the game data is cleared.
void StatsExporterLiveMatchSummary();

/// Once, in the loading screen, right after the engine resolved random sides, colors and start positions into `game`'s
/// slots. Sends MatchInfo LOADING from those resolved slots. Reads only; no random draws. A saved game being loaded
/// (`loadingSaveGame`) is ignored.
void StatsExporterLiveMatchLoading(GameInfo* game, UnsignedInt randomCrcBeforeResolve, Bool loadingSaveGame);

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
/// `objectId` is the new object; `veterancyLevel` is the level it starts with (a VeterancyLevel), reported in int_value1.
void StatsExporterLiveBuild(Int playerIndex, const ThingTemplate* tmpl, ObjectID objectId, Int veterancyLevel);
/// `killer` is the object that scored the kill (its id, template and last fired weapon are reported); `victim` is the object that died.
void StatsExporterLiveKill(Int playerIndex, const Object* killer, const Object* victim);
/// `playerIndex` owns the lost `victim`; `killer` may be NULL.
void StatsExporterLiveLoss(Int playerIndex, const Object* victim, const Object* killer);
void StatsExporterLiveEvacuate(Int playerIndex, const ThingTemplate* containerTmpl, UnsignedInt passengerCount);

/// A promotion earned from experience (ExperienceTracker::addExperiencePoints); levels assigned at creation are not reported.
void StatsExporterLiveVeterancyChange(Int playerIndex, const ThingTemplate* tmpl, ObjectID objectId, Int oldLevel, Int newLevel);

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

/// A salvage crate upgraded a unit. upgradeKind is "WEAPON" or "ARMOR".
void StatsExporterLiveSalvageUpgrade(Int playerIndex, const ThingTemplate* tmpl, const char* upgradeKind, Int tier);

// -- Read-only taps for the in-view statistics and the interest map. They record numbers and change nothing in the game. ----

/// Health a body actually lost to damage (after armor and modifiers), from ActiveBody::attemptDamage. `attackerId` may be INVALID_ID.
void StatsExporterLiveDamage(const Object* victim, ObjectID attackerId, Real healthRemoved);

/// A superweapon was fired by `source` at `location` (NULL when the power has no target).
void StatsExporterLiveSuperweaponFired(const Object* source, const SpecialPowerTemplate* power, const Coord3D* location);

/// A neutron missile left its launcher toward `target`; `secondsToImpact` is the flight time the missile's own data predicts.
void StatsExporterLiveMissileLaunched(const Object* missile, const Coord3D* target, Real secondsToImpact);
