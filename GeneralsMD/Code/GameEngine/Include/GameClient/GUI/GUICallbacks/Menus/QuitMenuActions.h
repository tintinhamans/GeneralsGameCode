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

// FILE: QuitMenuActions.h /////////////////////////////////////////////////////
// Widget-agnostic quit menu logic: which variant/labels/enabled-state the menu
// should show and the terminal effect of Exit/Restart/Surrender, with no
// GameWindow/gadget coupling. QuitMenu.cpp's .wnd callbacks and
// RmlQuitMenuScreen both call these instead of duplicating the rules.
///////////////////////////////////////////////////////////////////////////////

#pragma once

namespace QuitMenuActions
{
	// True when the game is multiplayer or a replay: QuitNoSave.wnd (no save/load button) applies.
	bool useNoSaveVariant();

	// ButtonRestart's caption/enable state, matching ToggleQuitMenu()'s GadgetButtonSetText/winEnable
	// calls: "Surrender" in multiplayer (disabled if already dead/allied-victory), "RestartGame" in
	// skirmish or replay (default caption, always enabled), "RestartMission" in campaign/single map.
	const char *restartLabelKey();
	bool isRestartEnabled();

	// ButtonExit's caption: "ExitMission" in campaign/single map, default "Exit" everywhere else.
	const char *exitLabelKey();

	// True while an in-game cinematic has input disabled: ButtonOptions/ButtonSaveLoad are greyed
	// out in the .wnd menu during a cinematic (save/load doesn't fit letterboxed).
	bool isCinematicInputDisabled();

	// Sets game pause the same way ToggleQuitMenu() does when opening: skirmish and campaign/replay
	// pause, plain multiplayer does not (surrendering doesn't need to pause the other players).
	void pauseForOpen();

	// ButtonExit's Yes callback body (TheGameLogic->quit(FALSE)). Callers still own destroyQuitMenu().
	void exit();

	// ButtonRestart's confirmation Yes callback: surrenders in multiplayer, else restarts the mission/
	// skirmish match. Callers still own destroyQuitMenu().
	void confirmRestartOrSurrender();
}
