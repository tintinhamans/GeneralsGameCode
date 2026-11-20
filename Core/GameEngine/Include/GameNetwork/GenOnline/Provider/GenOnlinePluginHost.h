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

// FILE: GenOnlinePluginHost.h ////////////////////////////////////////////////
// Description: Host of the GenOnline plugins. It loads at most one plugin DLL
//              through the GenOnline plugin API, checks its signed manifest,
//              version and table sizes, and routes the game calls to the
//              linked account, anticheat and transport sub interfaces.
//              GenOnline Anti-Cheat serves the anticheat calls when the plugin
//              provides no anticheat or loads for its transport only.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct PluginPeerTransportApi;

enum class EConnectionState : uint8_t
{
    NOT_CONNECTED,
    CONNECTING_DIRECT,
    FINDING_ROUTE,
    CONNECTED_DIRECT,
    CONNECTION_FAILED,
    CONNECTION_DISCONNECTED
};

enum class ENetworkChannels : uint8_t
{
    Game = 0,
    Anticheat,
    Signalling,
    Ping,
    Pong
};

enum class EPacketReliability : int32_t
{
    PACKET_RELIABILITY_UNRELIABLE_UNORDERED = 0,
    PACKET_RELIABILITY_RELIABLE_UNORDERED = 1,
    PACKET_RELIABILITY_RELIABLE_ORDERED = 2
};



enum class EAnticheatActionType : int32_t
{
    NONE = 0,
    KICK = 1
};

enum class EAnticheatActionReason : int32_t
{
    Unknown = 0,
    InternalError = 1,
    InvalidMessage = 2,
    AuthFailure = 3,
    ACNotRunning = 4,
    HeartbeatTimedOut = 5,
    ClientViolation = 6,
    BackendViolation = 7,
    TempCooldown = 8,
    TempBanned = 9,
    PermaBanned = 10
};

class GenOnlinePluginHost
{
public:
	// Set from plugin threads when the local client has to leave the lobby.
	static std::atomic<bool> g_bPendingExitLobby;

	// Starts GenOnline Anti-Cheat and loads plugins/<name>/<name>.dll next to the game executable. The external anticheat
	// plugin loads when there is one, else the first transport plugin loads for its transport and linked account only.
	// An empty anticheat plugin and an empty transport list load no plugin DLL.
	static void LoadPlugin(const std::string& anticheatPlugin, const std::vector<std::string>& transportPlugins);
	static void UnloadPlugin();
	static void Tick();

	// True while a plugin DLL is loaded and initialized.
	static bool IsPluginLoaded();

	// True when the anticheat plugin was configured but could not be loaded. Online play stays blocked then.
	static bool DidPluginFailToLoad();

	// True when the plugin chosen for its transport only could not be loaded. The game uses the standard connection and
	// GenOnline Anti-Cheat then.
	static bool DidTransportPluginFailToLoad();

	// The plugin DLL that LoadPlugin chose, empty when none was configured.
	static const std::string& GetPluginName();

	// True when the plugin DLL is the anticheat provider. GenOnline Anti-Cheat is the anticheat otherwise.
	static bool IsPluginAnticheat();

	static bool IsExternalProcessRunning();
	static int GetAnticheatIdentifier();

	class LinkedAccount
	{
	public:
		// Signs in to the platform of the plugin with the GenOnline token and hands the identity proof to the service.
		static void SignIn();
		static void Refresh();
	};

	class Anticheat
	{
	public:
		static void BeginSession();
		static void EndSession();
		static bool RegisterPeer(const std::string& platformUserID, uint32_t userID);
		static bool DeregisterPeer(const std::string& platformUserID, uint32_t userID);
		static void MessageArrived(uint32_t userID, const void* pData, uint32_t dataLen);
	};

	class Transport
	{
	public:
		// True when a plugin transport is loaded and reports that it can carry packets.
		static bool IsAvailable();
		// The name this client offers in SetPeerTransports and the plugin steps of the connection order name: the provider
		// of the linked account table while a plugin transport is loaded and available. Empty when there is none.
		static std::string GetName();
		static void StartSignalling(const char* szPlatformUserID, uint64_t userID);
		// False when no plugin transport is loaded.
		static bool SendPacket(const char* szPlatformUserID, uint64_t userID, const void* pData, uint32_t numBytes, ENetworkChannels channel, EPacketReliability reliability);
		static uint32_t GetNextPacketSize(ENetworkChannels channel);
		// The buffer belongs to the plugin and goes back through FreePacket. pOutSenderUserID gets the GenOnline user id of the sender.
		static bool ReceivePacket(ENetworkChannels channel, const uint8_t** ppOutData, uint32_t* pOutNumBytes, int64_t* pOutSenderUserID);
		static void FreePacket(const uint8_t* pData);
		static int GetLatency(const char* szPlatformUserID, uint64_t userID);
		static bool IsRelayed(const char* szPlatformUserID, uint64_t userID);
		static void DisconnectPeer(const char* szPlatformUserID, uint64_t userID);
		static void DisconnectAll();
		// Sets the relay policy of the plugin for the connections that start after the call. A plugin without SetRelayPolicy keeps its own policy.
		static void SetRelayPolicy(bool bForceRelay);
		// Ticks a loaded plugin transport, so packets queued since the last tick go out and arrived packets are taken in now.
		static void Pump();

		// The calls above as the table of the per-peer plugin transport.
		static const PluginPeerTransportApi& GetPeerTransportApi();
	};
};
