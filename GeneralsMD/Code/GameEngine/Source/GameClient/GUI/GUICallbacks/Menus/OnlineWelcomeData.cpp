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

// FILE: OnlineWelcomeData.cpp ///////////////////////////////////////////////////
// See OnlineWelcomeData.h. Extracted from WOLWelcomeMenu.cpp's live GENERALS_ONLINE
// code path. Per-line MOTD color (GSCOLOR_MOTD_HEADING/GSCOLOR_MOTD, and the
// original's "\ffffffffText" hex-color-prefix parsing via grabUByte()) is dropped:
// that color only ever mattered to the GameWindow listbox gadget, and RmlUi's MOTD
// list styles headings/body via a CSS class instead (see OnlineWelcome.rcss) --
// the hex-color-prefix escape itself is a NGMP-server-authored MOTD convention,
// not used by anything under GENERALS_ONLINE_TEXT_ONLY that ships today.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeData.h"

#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

void (*g_onlineWelcomeNotificationsChangedHook)(int numNotifications) = nullptr;

namespace OnlineWelcomeData
{

//-------------------------------------------------------------------------------------------------
UnicodeString buildWelcomeTitle()
{
	UnicodeString title;

	NGMP_OnlineServices_AuthInterface *pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr)
	{
		title.format(L"Welcome to Generals Online, %s", pAuthInterface->GetDisplayNameW().c_str());
	}

	return title;
}

//-------------------------------------------------------------------------------------------------
std::vector<OnlineWelcomeMotdLine> buildMotdLines()
{
	std::vector<OnlineWelcomeMotdLine> lines;

	AsciiString aMotd = NGMP_OnlineServicesManager::GetInstance() == nullptr ? AsciiString() : AsciiString(NGMP_OnlineServicesManager::GetInstance()->GetMOTD().c_str());

	UnicodeString headingStr;
	//Kris: Patch 1.01 - November 12, 2003
	//Removed number of players from string, and removed the argument. The number is incorrect anyways...
	//This was a Harvard initiated fix.
	headingStr.format(TheGameText->fetch("MOTD:NumPlayersHeading"));

	UnicodeString line;
	while (headingStr.nextToken(&line, L"\n"))
	{
		if (line.getCharAt(line.getLength()-1) == '\r')
			line.removeLastChar();	// there is a trailing '\r'

		line.trim();

		if (line.isEmpty())
			line = L" ";

		lines.push_back({ line, TRUE });
	}
	lines.push_back({ UnicodeString(L" "), TRUE });

	AsciiString aLine;
	while (aMotd.nextToken(&aLine, "\n"))
	{
		if (aLine.getCharAt(aLine.getLength()-1) == '\r')
			aLine.removeLastChar();	// there is a trailing '\r'

		aLine.trim();

		if (aLine.isEmpty())
			aLine = " ";

		if (aLine.startsWith("\\\\"))
		{
			aLine = aLine.str()+1;
		}
		else if (aLine.startsWith("\\") && aLine.getLength() > 9)
		{
			// take out the hex-color prefix from strings starting as "\ffffffffText" (color itself is
			// dropped here, see file header comment)
			aLine = aLine.str() + 9;
		}

		UnicodeString bodyLine = UnicodeString(MultiByteToWideCharSingleLine(aLine.str()).c_str());
		lines.push_back({ bodyLine, FALSE });
	}

	return lines;
}

//-------------------------------------------------------------------------------------------------
UnicodeString buildNumPlayersOnlineText(Int numPlayersOnline)
{
	UnicodeString valStr;
	valStr.format(TheGameText->fetch("GUI:NumPlayersOnline"), numPlayersOnline);
	return valStr;
}

//-------------------------------------------------------------------------------------------------
void requestFactionWinStats(std::function<void(std::vector<OnlineWelcomeFactionStat>)> callback)
{
	NGMP_OnlineServices_StatsInterface *pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	if (pStatsInterface == nullptr)
		return;

	pStatsInterface->GetGlobalStats([callback](GlobalStats stats)
		{
			std::vector<OnlineWelcomeFactionStat> result;

			for (int i = 0; i < (int)stats.matches.size(); ++i)
			{
				int wins = stats.wins[i];
				int matches = stats.matches[i];

				// div by 0 fix
				if (matches == 0)
					matches = 1;

				float fThisPercent = ((float)wins / (float)matches);

				OnlineWelcomeFactionStat stat;
				stat.side = g_mapServiceIndexToPlayerTemplateString[i].c_str();
				stat.text.format(L"%d%% (%d of %d)", (int)(100.f*fThisPercent), stats.wins[i], stats.matches[i]);
				result.push_back(stat);
			}

			callback(result);
		});
}

//-------------------------------------------------------------------------------------------------
UnicodeString buildBuddiesButtonText(Int numNotifications)
{
	UnicodeString buttonText;
	if (numNotifications > 0)
	{
		buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
	}
	else
	{
		buttonText.format(L"%s", TheGameText->fetch("GUI:Buddies").str());
	}
	return buttonText;
}

//-------------------------------------------------------------------------------------------------
Int getCurrentNotificationCount()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface == nullptr)
		return 0;

	return pSocialInterface->GetNumTotalNotifications();
}

//-------------------------------------------------------------------------------------------------
void registerNotificationsHook()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface == nullptr)
		return;

	pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
		{
			if (g_onlineWelcomeNotificationsChangedHook)
				g_onlineWelcomeNotificationsChangedHook(numNotifications);
		});
}

} // namespace OnlineWelcomeData
