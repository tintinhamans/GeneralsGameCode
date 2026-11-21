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

// Protocol reference: GenOnline/docs/stats-exporter.md
// Lobby control for the live stats pipe. The setup screens (skirmish, LAN, Generals Online) describe their own controls in a
// StatsLobbyScreen. The driver operates those controls the way a click would, so every change goes through the handlers of the screen.
// Main thread only, and it never touches a running match.
#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include <string>

class GameInfo;
class GameWindow;

namespace statsexporter
{
	class LobbyControlQuery;
}

/// What a game setup screen hands the pipe: its windows and the handlers behind the ones that are not plain gadget messages.
struct StatsLobbyScreen
{
	GameInfo* game;
	Bool isHost;
	Int localSlot;
	const char* blocked;	///< non-NULL: the screen ignores input right now, and why

	GameWindow** comboPlayer;			///< MAX_SLOTS entries each; entries may be NULL
	GameWindow** comboColor;
	GameWindow** comboTemplate;
	GameWindow** comboTeam;
	GameWindow** buttonStartPosition;
	GameWindow* comboStartingCash;
	GameWindow* checkLimitSuperweapons;
	GameWindow* buttonSelectMap;
	GameWindow* buttonStart;			///< the Start button of the host, the Accept button of a client
	GameWindow* sliderGameSpeed;		///< skirmish only, else NULL

	/// The handler the start position buttons run for one slot: -1 clears.
	void (*moveStartPosition)(Int slot, Int position);
	/// The OK path of the map selector for a map cache key, without the selector.
	void (*applyMap)(const AsciiString& map);
	/// Skirmish only: the update of the speed slider.
	void (*setGameSpeed)(Int fps);

	StatsLobbyScreen()
		: game(NULL), isHost(FALSE), localSlot(-1), blocked(NULL), comboPlayer(NULL), comboColor(NULL), comboTemplate(NULL), comboTeam(NULL)
		, buttonStartPosition(NULL), comboStartingCash(NULL), checkLimitSuperweapons(NULL), buttonSelectMap(NULL), buttonStart(NULL)
		, sliderGameSpeed(NULL), moveStartPosition(NULL), applyMap(NULL), setGameSpeed(NULL)
	{
	}
};

/// The OK paths of the map selectors without the selector window, one per setup screen in the source of its map select menu.
void SkirmishMapSelectMenuApplyMap(const AsciiString& map);
void LanMapSelectMenuApplyMap(const AsciiString& map);
void GenOnlineMapSelectMenuApplyMap(const AsciiString& map);

/// Returns the statsexporter::ResponseStatus value, fills `result` with the serialized LobbyControlResult and `message` with the reason of a refusal.
int StatsExporterLobbyControl(const statsexporter::LobbyControlQuery& query, std::string& result, std::string& message);
