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

#include "GameClient/GUI/GUICallbacks/Menus/HostGameActions.h"

#include "Common/CustomMatchPreferences.h"
#include "Common/GlobalData.h"
#include "Common/QuotedPrintable.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameSpy/PeerDefs.h" // SetLobbyAttemptHostJoin
#include "GameNetwork/GameSpyOverlay.h" // GSMessageBoxOk/GSMessageBoxCancel
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h"

namespace HostGameActions
{
	void createGame( const UnicodeString &gameNameIn, const UnicodeString &password,
		bool allowObservers, bool useStats, bool limitArmies )
	{
		UnicodeString name = gameNameIn;
		name.trim();
		if (name.isEmpty())
		{
			SetLobbyAttemptHostJoin(FALSE);
			GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
			GSMessageBoxOk(TheGameText->fetch("GUI:Error"), UnicodeString(L"Please enter a lobby name."), nullptr);
			return;
		}

		// save last used lobby name to CustomPref.ini
		{
			char buffer[256];
			const WideChar* w = name.str();
			int i = 0;
			for (; w[i] != 0 && i < 255; ++i)
			{
				buffer[i] = (char)(w[i] & 0xFF);
			}
			buffer[i] = 0;

			AsciiString lobbyNameAscii = buffer;

			CustomMatchPreferences pref;
			pref.setLastLobbyName(lobbyNameAscii);
			pref.write();
		}

		// TODO_NGMP: Support 'favorite map' again
		AsciiString defaultMap = getDefaultMap(true);
		CustomMatchPreferences pref;
		AsciiString storedMap = pref.getAsciiString("Map", AsciiString::TheEmptyString);
		if (!storedMap.isEmpty())
		{
			AsciiString decoded = QuotedPrintableToAsciiString(storedMap);
			decoded.trim();
			if (!decoded.isEmpty() && isValidMap(decoded, TRUE))
			{
				defaultMap = decoded;
			}
		}
		const MapMetaData* md = TheMapCache->findMap(defaultMap);

		{
			CustomMatchPreferences prefWrite;
			prefWrite.setAllowsObserver(allowObservers);
			prefWrite.setFactionsLimited(limitArmies);
			prefWrite.setUseStats(useStats);
			prefWrite.write();
		}

		AsciiString passwd;
		passwd.translate(password);

		// NGMP:NOTE: We count money here because mods etc sometimes change the starting money, so we dont want to hard code it, just create with whatever the client is telling us is a sensible amount
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (!pLobbyInterface)
		{
			SetLobbyAttemptHostJoin(FALSE);
			GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
			GSMessageBoxOk(UnicodeString(L"Error"), UnicodeString(L"Failed to get Online Services Lobby Interface!"));
			return;
		}

		pLobbyInterface->CreateLobby(name, md->m_displayName, md->m_fileName, md->m_isOfficial, md->m_numPlayers, limitArmies, useStats, TheGlobalData->m_defaultStartingCash.countMoney(), passwd.isNotEmpty(), std::string(passwd.str()), allowObservers);

		GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
		GSMessageBoxCancel(UnicodeString(L"Creating Lobby"), UnicodeString(L"Lobby Creation is in progress..."), nullptr);
	}

	void cancel()
	{
		GameSpyCloseOverlay(GSOVERLAY_GAMEOPTIONS);
		SetLobbyAttemptHostJoin(FALSE);
	}
}
