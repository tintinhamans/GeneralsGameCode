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

// FILE: OnlineSessionExit.cpp ////////////////////////////////////////////////
// See OnlineSessionExit.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineSessionExit.h"

#include "Common/GameEngine.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/Shell.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

extern NGMPGame *TheNGMPGame;

namespace OnlineSessionExit
{

//-------------------------------------------------------------------------------------------------
Bool isTeardownReady()
{
	NGMP_OnlineServicesManager *pManager = NGMP_OnlineServicesManager::GetInstance();
	if( pManager == nullptr || !pManager->IsPendingFullTeardown() )
	{
		return FALSE;
	}

	// Only if not in game and not in anim
	return ( TheNGMPGame == nullptr || !TheNGMPGame->isGameInProgress() ) && TheShell->isAnimFinished() && TheTransitionHandler->isFinished();
}

//-------------------------------------------------------------------------------------------------
void tearDownAndPop()
{
	TearDownGeneralsOnline();
	TheShell->pop();
}

} // namespace OnlineSessionExit
