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

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/HostGameData.h"

#include "Common/CustomMatchPreferences.h"
#include "GameNetwork/GeneralsOnline/NextGenMP_defines.h"

namespace HostGameData
{
	InitialState getInitialState()
	{
		InitialState state;

		CustomMatchPreferences pref;
		AsciiString lastLobbyName = pref.getLastLobbyName();
		if (!lastLobbyName.isEmpty())
			state.gameName.translate(lastLobbyName.str());
		else
			state.gameName.translate("Generals Online Lobby");

		state.allowObservers = pref.allowsObservers() == TRUE;
		state.useStats = pref.getUseStats() == TRUE;
		state.limitArmies = pref.getFactionsLimited() == TRUE;
		state.passwordMaxLength = GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH;

		return state;
	}
}
