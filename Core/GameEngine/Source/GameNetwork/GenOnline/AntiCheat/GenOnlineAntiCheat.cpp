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

// FILE: GenOnlineAntiCheat.cpp ///////////////////////////////////////////////
// Description: GenOnline Anti-Cheat, the built in anticheat plugin.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/AntiCheat/GenOnlineAntiCheat.h"

#include <map>
#include <mutex>
#include <string>

namespace
{
const GenOnlinePluginHostApi* s_pHost = nullptr;

// Session and peer bookkeeping. Later checks of GenOnline Anti-Cheat read it.
std::mutex s_stateMutex;
bool s_bInitialized = false;
bool s_bSessionActive = false;
std::map<uint64_t, std::string> s_sessionPeers;

void Log(int32_t level, const char* szMessage)
{
	if (s_pHost != nullptr && s_pHost->Log != nullptr)
	{
		s_pHost->Log(s_pHost->userData, level, szMessage);
	}
}

void ClearSession()
{
	s_bSessionActive = false;
	s_sessionPeers.clear();
}

int32_t GENONLINE_PLUGIN_CALL AntiCheat_Initialize(void)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	ClearSession();
	s_bInitialized = true;
	Log(GENONLINE_PLUGIN_LOG_INFO, "[" GENONLINE_ANTICHEAT_PLUGIN_NAME "] Initialized");
	return GENONLINE_PLUGIN_RESULT_OK;
}

void GENONLINE_PLUGIN_CALL AntiCheat_Tick(void)
{
	// The checks of GenOnline Anti-Cheat run here on the main thread once they exist.
}

void GENONLINE_PLUGIN_CALL AntiCheat_Shutdown(void)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	if (!s_bInitialized)
	{
		return;
	}

	ClearSession();
	s_bInitialized = false;
	Log(GENONLINE_PLUGIN_LOG_INFO, "[" GENONLINE_ANTICHEAT_PLUGIN_NAME "] Shut down");
}

int32_t GENONLINE_PLUGIN_CALL AntiCheat_GetAnticheatIdentifier(void)
{
	return GENONLINE_ANTICHEAT_IDENTIFIER;
}

void GENONLINE_PLUGIN_CALL AntiCheat_BeginSession(void)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	ClearSession();
	s_bSessionActive = true;
	Log(GENONLINE_PLUGIN_LOG_INFO, "[" GENONLINE_ANTICHEAT_PLUGIN_NAME "] Session started");
}

void GENONLINE_PLUGIN_CALL AntiCheat_EndSession(void)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	if (!s_bSessionActive)
	{
		return;
	}

	ClearSession();
	Log(GENONLINE_PLUGIN_LOG_INFO, "[" GENONLINE_ANTICHEAT_PLUGIN_NAME "] Session ended");
}

int32_t GENONLINE_PLUGIN_CALL AntiCheat_RegisterPeer(const char* szPlatformUserID, uint64_t userID, int32_t /*clientType*/)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	s_sessionPeers[userID] = szPlatformUserID != nullptr ? szPlatformUserID : "";
	return GENONLINE_PLUGIN_TRUE;
}

int32_t GENONLINE_PLUGIN_CALL AntiCheat_DeregisterPeer(const char* /*szPlatformUserID*/, uint64_t userID)
{
	std::lock_guard<std::mutex> lock(s_stateMutex);
	s_sessionPeers.erase(userID);
	return GENONLINE_PLUGIN_TRUE;
}

void GENONLINE_PLUGIN_CALL AntiCheat_MessageArrived(uint64_t /*userID*/, const void* /*pData*/, uint32_t /*dataLen*/)
{
	// GenOnline Anti-Cheat sends no peer messages, so a message from a peer carries nothing to act on.
	Log(GENONLINE_PLUGIN_LOG_DEBUG, "[" GENONLINE_ANTICHEAT_PLUGIN_NAME "] Dropping an anticheat message from a peer");
}

const GenOnlinePluginAnticheatApi s_anticheatApi =
{
	sizeof(GenOnlinePluginAnticheatApi),
	GENONLINE_PLUGIN_ANTICHEAT_API_VERSION,
	AntiCheat_BeginSession,
	AntiCheat_EndSession,
	AntiCheat_RegisterPeer,
	AntiCheat_DeregisterPeer,
	AntiCheat_MessageArrived,
};
} // namespace

int32_t GENONLINE_PLUGIN_CALL GenOnlineAntiCheat_GetApi(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, GenOnlinePluginApi* out)
{
	if (hostApiVersion != GENONLINE_PLUGIN_API_VERSION)
	{
		return GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH;
	}

	if (host == nullptr || out == nullptr || out->structSize < sizeof(GenOnlinePluginApi))
	{
		return GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT;
	}

	s_pHost = host;

	*out = GenOnlinePluginApi();
	out->structSize = sizeof(GenOnlinePluginApi);
	out->apiVersion = GENONLINE_PLUGIN_API_VERSION;
	out->name = GENONLINE_ANTICHEAT_PLUGIN_NAME;
	out->version = "1";
	out->capabilities = GENONLINE_PLUGIN_CAP_ANTICHEAT;
	out->Initialize = AntiCheat_Initialize;
	out->Tick = AntiCheat_Tick;
	out->Shutdown = AntiCheat_Shutdown;
	out->IsExternalProcessRunning = nullptr;
	out->GetAnticheatIdentifier = AntiCheat_GetAnticheatIdentifier;
	out->anticheat = &s_anticheatApi;
	return GENONLINE_PLUGIN_RESULT_OK;
}
