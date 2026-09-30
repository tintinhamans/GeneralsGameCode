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

namespace OnlineWelcomeSignals
{
	Signal1<int> &notificationsChanged() { static Signal1<int> s; return s; }
	Signal1<int> &numPlayersOnline() { static Signal1<int> s; return s; }
}

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
		title = TheGameText->fetchFormat("GUI:GOWelcomeName", pAuthInterface->GetDisplayNameW().c_str());
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
Bool isLoggedIn()
{
	NGMP_OnlineServices_AuthInterface *auth = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	return auth != nullptr && auth->IsLoggedIn();
}

//-------------------------------------------------------------------------------------------------
UnicodeString localDisplayName()
{
	UnicodeString name;
	NGMP_OnlineServices_AuthInterface *auth = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (auth != nullptr)
		name = auth->GetDisplayNameW().c_str();
	return name;
}

//-------------------------------------------------------------------------------------------------
Int currentNumPlayersOnline()
{
	Int last = GetLastNumPlayersOnline();
	if (last > 0)
		return last;

	// "... currently 188 player(s) online ..." -- the only count the Generals Online server sends.
	if (NGMP_OnlineServicesManager::GetInstance() == nullptr)
		return 0;
	const std::string motd = NGMP_OnlineServicesManager::GetInstance()->GetMOTD();
	const size_t tag = motd.find("player(s) online");
	if (tag == std::string::npos)
		return 0;
	size_t end = tag;
	while (end > 0 && motd[end - 1] == ' ')
		--end;
	size_t begin = end;
	while (begin > 0 && motd[begin - 1] >= '0' && motd[begin - 1] <= '9')
		--begin;
	if (begin == end)
		return 0;
	return atoi(motd.substr(begin, end - begin).c_str());
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

// The image WOLWelcomeMenu.wnd's PercentXxx CHECKBOX really draws: W3DGadgetCheckBoxImageDraw()
// paints only the box (ENABLEDDRAWDATA slot 1 while unchecked, the silver medallion), never slot 0.
// Index order matches g_mapServiceIndexToPlayerTemplateString; not derivable from side by any rule.
static const char *const kFactionStatIcons[] =
{
	"USAGeneral_slvr", "ChinaGeneral_slvr", "GLAGeneral_slvr",
	"SuperWGeneral_slvr", "LaserGeneral_slvr", "AirGeneral_slvr",
	"TankGeneral_slvr", "InfantryGeneral_slvr", "NukeGeneral_slvr",
	"ToxinGeneral_slvr", "DemoGeneral_slvr", "StealthGeneral_slvr",
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
				stat.text = TheGameText->fetchFormat("GUI:GOWinRateOf", (int)(100.f*fThisPercent), stats.wins[i], stats.matches[i]);
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
void registerNotificationsCallback()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface == nullptr)
		return;

	pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
		{
			OnlineWelcomeSignals::notificationsChanged().emit(numNotifications);
		});
}

} // namespace OnlineWelcomeData
