/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/GameSpy/GameSpyGameHooks.h"
#include "GameNetwork/Online/IOnlineLobbyState.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"

Bool GameSpyGameHooks::getMatchFrameRateLimit( Int &outLimit )
{
	return FALSE;
}

GameInfo *GameSpyGameHooks::getGameInfo()
{
	return TheGameSpyGame;
}

Bool GameSpyGameHooks::isLoggedIn()
{
	return TheGameSpyInfo != nullptr;
}
