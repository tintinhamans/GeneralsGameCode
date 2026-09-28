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

#include "GameClient/GUI/GUICallbacks/Menus/JoinGameActions.h"

#include "GameNetwork/GameSpy/PeerDefs.h" // SetLobbyAttemptHostJoin
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h"

namespace JoinGameActions
{
	void joinGame( const UnicodeString &password )
	{
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (pLobbyInterface == nullptr)
		{
			DEBUG_LOG(("NGMP_OnlineServices_LobbyInterface is not initialized!"));
			GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
			SetLobbyAttemptHostJoin(FALSE);
			return;
		}

		LobbyEntry lobbyTryingToJoin = pLobbyInterface->GetLobbyTryingToJoin();

		if (lobbyTryingToJoin.lobbyID == -1)
		{
			GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
			SetLobbyAttemptHostJoin(FALSE);
			return;
		}

		AsciiString passwd;
		passwd.translate(password);

		pLobbyInterface->JoinLobby(lobbyTryingToJoin, passwd.str());

		DEBUG_LOG(("Attempting to join game %d(%s) with password [%s]\n", lobbyTryingToJoin.lobbyID, lobbyTryingToJoin.name.c_str(), passwd.str()));

		GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
	}

	void cancel()
	{
		GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
		SetLobbyAttemptHostJoin(FALSE);
	}
}
