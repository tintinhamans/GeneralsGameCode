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

#pragma once

class AsciiString;
class Object;
class Player;
class DamageInfo;

/// Export game statistics as a JSON file alongside the replay file.
/// @param replayDir Directory containing replays (e.g. "[UserDataPath]/Replays/")
/// @param replayFileName Replay filename with extension (e.g. "LastReplay.rep")
/// @return false when the file could not be written. Uploads only when -statsUrl is set.
bool ExportGameStatsJSON(const AsciiString& replayDir, const AsciiString& replayFileName);

/// Build the path of the stats file of a replay: <replayDir><replay name without extension>.stats.json.gz
void StatsExporterGetStatsPath(AsciiString& statsPath, const AsciiString& replayDir, const AsciiString& replayFileName);

/// Build the path of the stats file that belongs to a replay file path, same folder and base name: C:/x/Replays/Foo.rep -> C:/x/Replays/Foo.stats.json.gz
void StatsExporterGetStatsPathForReplay(AsciiString& statsPath, const AsciiString& replayPath);

/// True when live games are recorded: -exportStats or the settings.client.json setting stats.export. Off by default.
bool StatsExporterLiveRecordingEnabled();

/// True between StatsExporterBeginRecording and the export of the match.
bool StatsExporterIsActive();

/// Collect a time-series snapshot of all players' stats (called every game logic frame). Does nothing while no recording is active.
/// Snapshots are taken every 30 frames (~1 second) and stored in memory.
void StatsExporterCollectSnapshot();

/// Begin recording stats for a new replay. Activates recording and resets all stored data.
void StatsExporterBeginRecording();

/// Record a kill event with full context (called from Object::scoreTheKill).
/// Feeds the live stats readers (LocalApiLive.h) and, when -exportStats is on, the JSON export.
void StatsExporterRecordKill(const Object *killer, const Object *victim, const DamageInfo *damageInfo);

/// Record a build event (called from Player::onUnitCreated / onStructureConstructionComplete).
void StatsExporterRecordBuild(const Object *producer, const Object *built);

/// Record a capture event (called from Object::onCapture).
void StatsExporterRecordCapture(const Object *captured, const Player *oldOwner, const Player *newOwner);
