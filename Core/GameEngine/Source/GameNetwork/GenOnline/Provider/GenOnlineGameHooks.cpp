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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h"

#include "GameClient/Color.h"
#include "GameClient/DisplayString.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineDefines.h"
#include "GameNetwork/GenOnline/GenOnlineGame.h"
#include "GameNetwork/GenOnline/DiscordRichPresence.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"
#include <steam/isteamnetworkingsockets.h>

static bool g_bTearDownGeneralsOnlineRequested = false;
void TearDownGeneralsOnline()
{
	g_bTearDownGeneralsOnlineRequested = true;

	if (GenOnlineServicesManager::GetInstance() == nullptr)
		return;

	EGOTearDownReason teardownReason = GenOnlineServicesManager::GetInstance()->GetTeardownReason();

	if (teardownReason != EGOTearDownReason::USER_REQUESTED_SILENT && !IsModerationTeardownReason(teardownReason))
	{
		UnicodeString title, body;

		if (teardownReason == EGOTearDownReason::USER_LOGOUT)
		{
			title = L"Logged Out";
			body = L"You are now logged out of GeneralsOnline.";
		}
		else if (teardownReason == EGOTearDownReason::LOST_CONNECTION)
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"Your connection to the Generals Online servers was lost.";
		}
        else if (teardownReason == EGOTearDownReason::AUTH_FAILED)
        {
            title = TheGameText->fetch("GUI:GSErrorTitle");
            body = L"Authentication with the Generals Online servers failed.";
        }
		else if (teardownReason == EGOTearDownReason::MIDDLEWARE_LOGIN_FAILED)
		{
            title = TheGameText->fetch("GUI:GSErrorTitle");
            body = L"Middleware Authentication failed.";
		}
		else
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"An unknown error occurred.";
		}

		GenOnlineServicesManager::GetInstance()->ResetPendingFullTeardownReason();

		GameSpyCloseAllOverlays();
		GSMessageBoxOk(title, body);
	}
}

//-------------------------------------------------------------------------------------------------
GenOnlineGameHooks::GenOnlineGameHooks()
	: m_discordRichPresence(nullptr)
{
}

GenOnlineGameHooks::~GenOnlineGameHooks()
{
	delete m_discordRichPresence;
	m_discordRichPresence = nullptr;
}

void GenOnlineGameHooks::onGlobalDataLoaded()
{
	GenOnlineServicesManager::InitSentry();
}

void GenOnlineGameHooks::loadSettings()
{
	GenOnlineServicesManager::Settings.Initialize();
}

void GenOnlineGameHooks::onEngineInitialized()
{
	m_discordRichPresence = new GenOnlineDiscordRPC();
	m_discordRichPresence->Initialize();
}

void GenOnlineGameHooks::onEngineUpdate()
{
	if (g_bTearDownGeneralsOnlineRequested) // delayed tear down
	{
		g_bTearDownGeneralsOnlineRequested = false;

		GenOnlineServicesManager::DestroyInstance();

	}

	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}

	if (m_discordRichPresence != nullptr)
	{
		m_discordRichPresence->Tick(
			GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>());
	}
}

void GenOnlineGameHooks::onEngineShutdown()
{
	delete m_discordRichPresence;
	m_discordRichPresence = nullptr;

	GenOnlineServicesManager::DestroyInstance();
}

void GenOnlineGameHooks::onEngineExit()
{
	GenOnlineServicesManager::ShutdownSentry();
}

Bool GenOnlineGameHooks::getMatchFrameRateLimit( Int &outLimit )
{
	if (TheGenOnlineGame == nullptr)
		return FALSE;

	outLimit = GenOnlineServicesManager::Settings.Graphics_GetFPSLimit();
	return TRUE;
}

GameInfo *GenOnlineGameHooks::getGameInfo()
{
	return TheGenOnlineGame;
}

Bool GenOnlineGameHooks::isLoggedIn()
{
	return GenOnlineServicesManager::GetInstance() != nullptr;
}

void GenOnlineGameHooks::onCRCMismatch( const UnicodeString &strMismatchDetails )
{
#if defined(GENONLINE_USE_SENTRY)
	if (TheGenOnlineGame != nullptr)
	{
		// local player info
		int64_t userID = -1;
		std::string strDisplayname = "Unknown";
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (pAuthInterface != nullptr)
		{
			userID = pAuthInterface->GetUserID();
			strDisplayname = pAuthInterface->GetDisplayName();
		}
		std::string strUserID = std::format("{}", userID);

		sentry_set_extra("user_id", sentry_value_new_int32(userID));
		sentry_set_extra("user_displayname", sentry_value_new_string(strDisplayname.c_str()));

		AsciiString sentryMsg;
		sentryMsg.translate(strMismatchDetails);

		// send event to sentry
		sentry_capture_event(sentry_value_new_message_event(
			SENTRY_LEVEL_ERROR,
			"CRC_MISMATCH",
			sentryMsg.str()
		));
	}
#endif
}

Bool GenOnlineGameHooks::shouldLeaveGame()
{
	return TheGenOnlineGame != nullptr && TheGenOnlineGame->canKickOnObserversDisabled();
}

void GenOnlineGameHooks::onReplayWritten( const AsciiString &absoluteReplayPath )
{
	if (TheGenOnlineGame != nullptr)
	{
		GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
		if (pOnlineServicesMgr != nullptr)
		{
			pOnlineServicesMgr->CommitReplay(absoluteReplayPath);
		}
	}
}

Int GenOnlineGameHooks::getChatMessageLifeSeconds()
{
	return GenOnlineServicesManager::Settings.GetChatLifeSeconds();
}

Bool GenOnlineGameHooks::isStatsOverlayEnabled()
{
	return GenOnlineServicesManager::Settings.Graphics_DrawStatsOverlay();
}

void GenOnlineGameHooks::drawNetworkOverlay( DisplayString *text, Int dropColor )
{
	const Color colorGood = GameMakeColor(0, 255, 0, 150);
	const Color colorBad = GameMakeColor(255, 0, 0, 150);

	// draw connections
	if (GenOnlineServicesManager::IsAdvancedNetworkStatsEnabled())
	{
		if (TheGenOnlineGame != nullptr)
		{
			NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();

			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

			if (pMesh != nullptr && pLobbyInterface != nullptr)
			{
				//std::vector<LobbyMemberEntry>& vecMembers = pLobbyInterface->GetMembersListForCurrentRoom();

				ISteamNetworkingSockets* pSteamNetSockets = SteamNetworkingSockets();
				if (!pSteamNetSockets)
				{
					return;
				}

				int i = 0;
				auto& allConnections = pMesh->GetAllConnections();
				if (allConnections.empty())
				{
					return;
				}

				for (auto& connection : allConnections)
				{
					if (!connection.second.IsValid())
					{
						continue;
					}

					LobbyMemberEntry lobbyMember = pLobbyInterface->GetRoomMemberFromID(connection.first);

					const int k_nLanes = 1;
					SteamNetConnectionRealTimeStatus_t status;
					SteamNetConnectionRealTimeLaneStatus_t laneStatus[k_nLanes];
					
					if (!TheNetwork)
					{
						continue;
					}

					EResult res = pSteamNetSockets->GetConnectionRealTimeStatus(connection.second.m_hSteamConnection, &status, k_nLanes, laneStatus);

					if (res == k_EResultNoConnection || lobbyMember.display_name.empty())
					{
						continue;
					}

					int avgFPS = TheNetwork->getSlotAverageFPS(lobbyMember.m_SlotIndex);

					UnicodeString netString;
					netString.format(L"\n[usr %s|%d][%hs %hs][AVGFPS: %d] Lat: %i, QL: %.2f, QR: %.2f OutP/s: %.2f, OutB/s: %.2f, InP/s: %.2f, InB/s: %.2f, SR %i PU: %d, PR: %d, NACK: %d, QT: %I64d",
						from_utf8(lobbyMember.display_name).c_str(),
						(int)res,
						connection.second.IsIPV4() ? "IPv4" : "IPv6",
						connection.second.IsDirect() ? "Direct" : "Relay",
						avgFPS,
						status.m_nPing,
						status.m_flConnectionQualityLocal,
						status.m_flConnectionQualityRemote,
						status.m_flOutPacketsPerSec,
						status.m_flOutBytesPerSec,
						status.m_flInPacketsPerSec,
						status.m_flInBytesPerSec,
						status.m_nSendRateBytesPerSecond,
						status.m_cbPendingUnreliable,
						status.m_cbPendingReliable,
						status.m_cbSentUnackedReliable,
						status.m_usecQueueTime);

					int w, h;
					text->getSize(&w, &h);

					bool bIsHighQuality = true;
					if (avgFPS < (LOGICFRAMES_PER_SECOND - 1) || status.m_cbSentUnackedReliable >= 1000 || (status.m_flConnectionQualityLocal != -1.f && status.m_flConnectionQualityLocal < 1.f) || (status.m_flConnectionQualityRemote != -1.f && status.m_flConnectionQualityRemote < 1.f))
					{
						bIsHighQuality = false;
					}

					text->setText(netString);
					text->draw(0, 500 + (i * h / 2), bIsHighQuality ? colorGood : colorBad, dropColor);
					++i;
				}
			}

		}
	}

}
