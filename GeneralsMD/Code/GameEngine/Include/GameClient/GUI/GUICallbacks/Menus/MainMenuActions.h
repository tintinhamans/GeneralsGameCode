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

// FILE: MainMenuActions.h ////////////////////////////////////////////////////
// Widget-agnostic main menu actions: the terminal effect of each main menu
// button (navigate to a screen, launch a tool, quit, pick a campaign faction),
// with no GameWindow/dropdown/TheTransitionHandler animation coupling. The
// .wnd MainMenu.cpp callbacks call these instead of duplicating the logic,
// so a future non-.wnd front end (e.g. RmlUi) can call the exact same
// functions and drive its own presentation on top.
///////////////////////////////////////////////////////////////////////////////

#pragma once

class AsciiString;

namespace MainMenuActions
{
	// ButtonExit: the actual quit sequence. Callers decide whether to confirm first
	// (the .wnd menu only skips confirmation when windowed; see MainMenu.cpp).
	void quit();

	// ButtonSkirmish: pushes the skirmish game options screen.
	void startSkirmishOptions();

	// ButtonNetwork: pushes the LAN lobby screen.
	void openNetworkLobby();

	// ButtonOnline: kicks off the GameSpy/online patch check before the multiplayer flow continues.
	void startOnlinePatchCheck();

	// ButtonOptions: routes to the RmlUi options screen when registered and not -wnd, else the
	// legacy WindowLayout. Shared with QuitMenu.cpp's identical Options button.
	void openOptions();

	// ButtonWorldBuilder: spawns the (D)WorldBuilder executable; shows a message box on failure.
	void launchWorldBuilder();

	// ButtonGetUpdate: starts downloading a detected patch.
	void startPatchDownload();

	// ButtonLoadGame: pushes the save/load screen.
	void openLoadGame();

	// ButtonReplay: pushes the replay list screen.
	void openReplayMenu();

	// ButtonCredits: pushes the credits screen.
	void openCreditsMenu();

	// ButtonUSA/GLA/China/Challenge: selects the campaign (or clears it via ButtonDiffBack).
	void selectCampaign(const AsciiString &campaignName);
}
