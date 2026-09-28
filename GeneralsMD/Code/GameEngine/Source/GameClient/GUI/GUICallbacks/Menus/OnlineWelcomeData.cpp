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
// code path, including its per-line MOTD color (GSCOLOR_MOTD_HEADING/GSCOLOR_MOTD,
// plus the original's "\ffffffffText" hex-color-prefix parsing via grabUByte()) --
// this is the ONE implementation now; WOLWelcomeMenu.cpp's updateNumPlayersOnline()
// calls buildMotdLines() instead of duplicating the parse.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h" // This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OnlineWelcomeData.h"

#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

void (*g_onlineWelcomeNotificationsChangedHook)(int numNotifications) = nullptr;
void (*g_onlineWelcomeNumPlayersOnlineHook)(int numPlayersOnline) = nullptr;

// grabUByte()/color-prefix parse mirrors WOLWelcomeMenu.cpp's original static grabUByte() exactly.
static UnsignedByte grabUByte(const char *s)
{
	char tmp[5] = "0xff";
	tmp[2] = s[0];
	tmp[3] = s[1];
	return (UnsignedByte)strtol(tmp, nullptr, 16);
}

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

		lines.push_back({ line, TRUE, GameSpyColor[GSCOLOR_MOTD_HEADING] });
	}
	lines.push_back({ UnicodeString(L" "), TRUE, GameSpyColor[GSCOLOR_MOTD_HEADING] });

	AsciiString aLine;
	while (aMotd.nextToken(&aLine, "\n"))
	{
		if (aLine.getCharAt(aLine.getLength()-1) == '\r')
			aLine.removeLastChar();	// there is a trailing '\r'

		aLine.trim();

		if (aLine.isEmpty())
			aLine = " ";

		Color c = GameSpyColor[GSCOLOR_MOTD];
		if (aLine.startsWith("\\\\"))
		{
			aLine = aLine.str()+1;
		}
		else if (aLine.startsWith("\\") && aLine.getLength() > 9)
		{
			// take out the hex value from strings starting as "\ffffffffText"
			UnsignedByte a, r, g, b;
			a = grabUByte(aLine.str()+1);
			r = grabUByte(aLine.str()+3);
			g = grabUByte(aLine.str()+5);
			b = grabUByte(aLine.str()+7);
			c = GameMakeColor(r, g, b, a);
			aLine = aLine.str() + 9;
		}

		UnicodeString bodyLine = UnicodeString(MultiByteToWideCharSingleLine(aLine.str()).c_str());
		lines.push_back({ bodyLine, FALSE, c });
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
AsciiString buildFactionStatTooltipKey(const AsciiString &side)
{
	// See NGMP_include.h's g_mapServiceIndexToPlayerTemplateString comment: GameSpy/NGMP calls index 0
	// "USA", but the .wnd's own TOOLTIPTEXT for PercentUSA is "SIDE:America".
	AsciiString key;
	key.format("SIDE:%s", side == "USA" ? "America" : side.str());
	return key;
}

// WOLWelcomeMenu.wnd's PercentXxx ENABLEDDRAWDATA image, keyed the same way (index order matches
// g_mapServiceIndexToPlayerTemplateString exactly; not derivable from side by any naming rule).
static const char *const kFactionStatIcons[] =
{
	"USA_Logo", "China_Logo", "GLA_Logo",
	"USA_Superweapon", "USA_Laser", "USA_Air",
	"China_Tank", "China_Infantry", "China_Nuke",
	"GLA_Toxin", "GLA_Demo", "GLA_Stealth",
};

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
				stat.icon = (i >= 0 && i < (int)(sizeof(kFactionStatIcons)/sizeof(kFactionStatIcons[0]))) ? kFactionStatIcons[i] : "";
				stat.text.format(L"%d%% (%d of %d)", (int)(100.f*fThisPercent), stats.wins[i], stats.matches[i]);
				stat.tooltip = TheGameText->fetch(buildFactionStatTooltipKey(stat.side));
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
