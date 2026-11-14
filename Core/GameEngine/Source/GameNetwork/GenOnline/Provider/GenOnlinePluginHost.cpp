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

// FILE: GenOnlinePluginHost.cpp //////////////////////////////////////////////
// Description: Host of the GenOnline plugins.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginKeys.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginPlatform.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginTrust.h"

#include "GameNetwork/GenOnline/PluginApi/GenOnlinePluginApi.h"
#include "GameNetwork/GenOnline/AntiCheat/GenOnlineAntiCheat.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlinePluginRoles.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <vector>

extern HWND ApplicationHWnd;

std::atomic<bool> GenOnlinePluginHost::g_bPendingExitLobby = false;

static_assert(static_cast<int32_t>(EConnectionState::CONNECTION_DISCONNECTED) == GENONLINE_PLUGIN_CONNECTION_DISCONNECTED, "Connection states must match the plugin API");
static_assert(static_cast<uint8_t>(ENetworkChannels::Pong) == GENONLINE_PLUGIN_CHANNEL_PONG, "Channels must match the plugin API");
static_assert(static_cast<int32_t>(EPacketReliability::PACKET_RELIABILITY_RELIABLE_ORDERED) == GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED, "Reliability modes must match the plugin API");
static_assert(static_cast<int32_t>(EAnticheatActionType::KICK) == GENONLINE_PLUGIN_ACTION_KICK, "Action types must match the plugin API");
static_assert(static_cast<int32_t>(EAnticheatActionReason::PermaBanned) == GENONLINE_PLUGIN_ACTION_REASON_PERMA_BANNED, "Action reasons must match the plugin API");

namespace
{
// The end of the last field that version 1 of a table defines is the smallest structSize the host accepts.
constexpr uint32_t kPluginApiMinSize = static_cast<uint32_t>(offsetof(GenOnlinePluginApi, transport) + sizeof(void*));
constexpr uint32_t kLinkedAccountApiMinSize = static_cast<uint32_t>(offsetof(GenOnlinePluginLinkedAccountApi, GetIdentityProof) + sizeof(void*));
constexpr uint32_t kAnticheatApiMinSize = static_cast<uint32_t>(offsetof(GenOnlinePluginAnticheatApi, MessageArrived) + sizeof(void*));
constexpr uint32_t kTransportApiMinSize = static_cast<uint32_t>(offsetof(GenOnlinePluginTransportApi, DisconnectAll) + sizeof(void*));

// Minisign public keys that sign the plugin manifests, see GenOnlinePluginKeys.h.
constexpr const char* s_pluginPublicKeys[] = { GenOnlinePluginKeys::kPluginsCurrent, GenOnlinePluginKeys::kPluginsNext };

// The plugin names that this build loads, each with the lowest manifest version that it accepts, so an older
// signed plugin cannot replace a newer one. A name that the list does not hold is refused. A new plugin gets an
// entry with its folder name, which is also the name in its manifest, and the version of its first signed release.
constexpr GenOnlinePluginTrust::AllowedPlugin s_allowedPlugins[] =
{
	{ "genonline_eos", { 2026, 11, 0 } },
};

// GENONLINE_ALLOW_UNSIGNED_PLUGINS comes from the CMake option of the same name, which is off by default and
// which no preset and no workflow turns on. It lets a build without plugin keys load unsigned plugins.
#if defined(GENONLINE_ALLOW_UNSIGNED_PLUGINS)
constexpr bool kAllowUnsignedPlugins = true;
#else
constexpr bool kAllowUnsignedPlugins = false;
#endif

// Keeps the plugin files open and locked while the plugin is loaded.
GenOnlinePluginTrust::PluginFiles s_pluginFiles;

GenOnlinePluginPlatform::Module s_hPluginModule = nullptr;
GenOnlinePluginApi s_pluginApi = {};
GenOnlinePluginHostApi s_hostApi = {};
// The host table of a plugin that loads for its transport only. Its anticheat callbacks only log.
GenOnlinePluginHostApi s_transportOnlyHostApi = {};
bool s_bPluginLoaded = false;
bool s_bPluginLoadFailed = false;
// True when the plugin that LoadPlugin chose for its transport only failed to load. The game then uses the standard connection.
bool s_bTransportPluginLoadFailed = false;
// True while LoadPlugin loads a plugin whose failure blocks online play.
bool s_bPluginLoadBlocksOnline = false;
std::string s_pluginName;
bool s_bPluginIsAnticheat = false;

const GenOnlinePluginLinkedAccountApi* s_pLinkedAccount = nullptr;
const GenOnlinePluginAnticheatApi* s_pAnticheat = nullptr;
const GenOnlinePluginTransportApi* s_pTransport = nullptr;
// The anticheat table of a transport only plugin. It gets the session and peer calls so that its transport knows its peers.
const GenOnlinePluginAnticheatApi* s_pPeerTracking = nullptr;

// GenOnline Anti-Cheat is linked into the game and serves the anticheat calls when the plugin DLL provides no anticheat.
GenOnlinePluginApi s_builtInApi = {};
bool s_bBuiltInLoaded = false;

bool s_bSessionStarted = false;
int64_t s_tokenCreationTime = -1;

int64_t UtcNowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
}

const char* DescribeResult(int32_t result)
{
	switch (result)
	{
		case GENONLINE_PLUGIN_RESULT_OK: return "success";
		case GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH: return "plugin API version mismatch";
		case GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT: return "invalid argument";
		case GENONLINE_PLUGIN_RESULT_BAD_ENVIRONMENT: return "bad environment (unparseable exe path, non-ASCII install path, or missing XAudio redist)";
		case GENONLINE_PLUGIN_RESULT_PLATFORM_OPTIONS_FAILED: return "failed to create the integrated platform options container";
		case GENONLINE_PLUGIN_RESULT_SDK_INIT_FAILED: return "middleware SDK initialization failed";
		case GENONLINE_PLUGIN_RESULT_PLATFORM_CREATE_FAILED: return "middleware platform creation failed";
		case GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS: return "plugin was built without its middleware deployment credentials";
		case GENONLINE_PLUGIN_RESULT_INTERNAL_ERROR: return "internal error";
		default: return "unknown failure";
	}
}

// Plugin names become folder and file names, so they are limited to letters, digits and underscores.
bool IsValidPluginName(const std::string& pluginName)
{
	if (pluginName.empty() || pluginName.size() > 64)
	{
		return false;
	}

	for (char c : pluginName)
	{
		const bool bAllowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!bAllowed)
		{
			return false;
		}
	}

	return true;
}

std::wstring GetExecutableDirectory()
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0)
		{
			return std::wstring();
		}

		if (length < buffer.size())
		{
			std::wstring path(buffer.data(), length);
			const size_t separator = path.find_last_of(L"\\/");
			return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator + 1);
		}

		buffer.resize(buffer.size() * 2);
	}
}

bool ValidateLinkedAccountApi(const GenOnlinePluginLinkedAccountApi* pApi)
{
	return pApi != nullptr
		&& pApi->structSize >= kLinkedAccountApiMinSize
		&& pApi->version == GENONLINE_PLUGIN_LINKED_ACCOUNT_API_VERSION
		&& pApi->SignIn != nullptr
		&& pApi->Refresh != nullptr
		&& pApi->IsSignedIn != nullptr
		&& pApi->GetPlatformUserId != nullptr
		&& pApi->GetIdentityProof != nullptr;
}

bool ValidateAnticheatApi(const GenOnlinePluginAnticheatApi* pApi)
{
	return pApi != nullptr
		&& pApi->structSize >= kAnticheatApiMinSize
		&& pApi->version == GENONLINE_PLUGIN_ANTICHEAT_API_VERSION
		&& pApi->BeginSession != nullptr
		&& pApi->EndSession != nullptr
		&& pApi->RegisterPeer != nullptr
		&& pApi->DeregisterPeer != nullptr
		&& pApi->MessageArrived != nullptr;
}

bool ValidateTransportApi(const GenOnlinePluginTransportApi* pApi)
{
	return pApi != nullptr
		&& pApi->structSize >= kTransportApiMinSize
		&& pApi->version == GENONLINE_PLUGIN_TRANSPORT_API_VERSION
		&& pApi->IsAvailable != nullptr
		&& pApi->StartSignalling != nullptr
		&& pApi->SendPacket != nullptr
		&& pApi->GetNextPacketSize != nullptr
		&& pApi->ReceivePacket != nullptr
		&& pApi->FreePacket != nullptr
		&& pApi->GetLatency != nullptr
		&& pApi->IsRelayed != nullptr
		&& pApi->DisconnectPeer != nullptr
		&& pApi->DisconnectAll != nullptr;
}

// Checks one sub interface against its capability bit. A set bit with an invalid table refuses the plugin.
template <typename TApi>
bool AcceptSubInterface(const GenOnlinePluginApi& api, uint32_t capability, const TApi* pApi, bool (*validate)(const TApi*), const char* szName, const TApi*& outApi)
{
	outApi = nullptr;

	if ((api.capabilities & capability) == 0)
	{
		if (pApi != nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Ignoring the %s table because the capability bit is not set", szName);
		}
		return true;
	}

	if (!validate(pApi))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: The %s table is missing, too small, of another version or incomplete", szName);
		return false;
	}

	outApi = pApi;
	return true;
}

void GENONLINE_PLUGIN_CALL Host_Log(void* /*userData*/, int32_t level, const char* szMessage)
{
	// never pass plugin supplied text as a format string
	const ELogVerbosity verbosity = level <= GENONLINE_PLUGIN_LOG_DEBUG ? ELogVerbosity::LOG_DEBUG : ELogVerbosity::LOG_RELEASE;
	NetworkLog(verbosity, "%s", szMessage != nullptr ? szMessage : "(null message)");
}

void GENONLINE_PLUGIN_CALL Host_LobbyChatOutput(void* /*userData*/, const char* szMsg)
{
	if (szMsg == nullptr)
	{
		return;
	}

	UnicodeString unicodeStr;
	unicodeStr.translate(AsciiString(szMsg));

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		if (pLobbyInterface->m_OnChatCallback != nullptr)
		{
			pLobbyInterface->m_OnChatCallback(unicodeStr, DetermineSystemNoticeColor(false, false));
		}
	}
}

void GENONLINE_PLUGIN_CALL Host_ConnectionStateChanged(void* /*userData*/, const char* /*szPlatformUserID*/, uint64_t userID, int32_t newState)
{
	// The mesh applies these states only to the pairs that use the plugin transport.
	if (newState < GENONLINE_PLUGIN_CONNECTION_NOT_CONNECTED || newState > GENONLINE_PLUGIN_CONNECTION_DISCONNECTED)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Ignoring unknown connection state %d", newState);
		return;
	}

	// this can arrive on a plugin owned thread, the mesh locks internally
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->UpdateConnectionStateForUser(static_cast<int64_t>(userID), static_cast<EConnectionState>(newState));
	}
}

void GENONLINE_PLUGIN_CALL Host_ActionRequired(void* /*userData*/, uint64_t userID64, const char* szReason, int32_t /*actionType*/, int32_t /*actionReason*/)
{
	if (szReason == nullptr)
	{
		szReason = "(null reason)";
	}

	const uint32_t userID = static_cast<uint32_t>(userID64);

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Action required: %s", szReason);

	if (pAuthInterface == nullptr)
	{
		// no auth interface? bail out
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Leaving lobby, lobby isn't secure, no auth interface.");
		GenOnlinePluginHost::g_bPendingExitLobby = true;
		return;
	}

	// If it's us, leave, if its someone else, d/c them
	uint32_t localUserID = pAuthInterface->GetUserID();
	if (localUserID == userID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Leaving lobby, lobby isn't secure, action was requested against local user.");
		GenOnlinePluginHost::g_bPendingExitLobby = true;
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Disconnecting remote user, lobby isn't secure, action was requested against remote user %u.", userID);

		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		if (pMesh != nullptr)
		{
			pMesh->DisconnectUser(userID);
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Disconnected: %u.", userID);
		}
		else // no mesh, just back out
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Leaving lobby, lobby isn't secure, actionable player was remote, but no mesh exists to take action.");
			GenOnlinePluginHost::g_bPendingExitLobby = true;
		}
	}
}

void GENONLINE_PLUGIN_CALL Host_IntegrityViolation(void* /*userData*/, const char* szReason, int32_t violationType)
{
	if (szReason == nullptr)
	{
		szReason = "(null reason)";
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Leaving lobby, local AC integrity violation occured (%d): %s.", violationType, szReason);
	GenOnlinePluginHost::g_bPendingExitLobby = true;
}

void GENONLINE_PLUGIN_CALL Host_SendMessageViaTransport(void* /*userData*/, uint64_t userID64, const void* pData, uint32_t dataLen)
{
	if (pData == nullptr || dataLen == 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: SendMessageViaTransport received null/empty data");
		return;
	}

	const uint32_t goUserID = static_cast<uint32_t>(userID64);

	// Anti-cheat messages travel over the pair connection only, the mesh holds them until the pair connects.
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->SendACPacket(goUserID, pData, dataLen);
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Cannot send AC packet - NetworkMesh is null");
	}
}

// A plugin that loads for its transport only is not the anticheat, so its anticheat requests do not act on the lobby.
void GENONLINE_PLUGIN_CALL Host_IgnoreActionRequired(void* /*userData*/, uint64_t userID64, const char* szReason, int32_t /*actionType*/, int32_t /*actionReason*/)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Ignoring an anticheat action for user %u from a plugin that is not the anticheat: %s",
		static_cast<uint32_t>(userID64), szReason != nullptr ? szReason : "(null reason)");
}

void GENONLINE_PLUGIN_CALL Host_IgnoreIntegrityViolation(void* /*userData*/, const char* szReason, int32_t violationType)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Ignoring an integrity violation (%d) from a plugin that is not the anticheat: %s",
		violationType, szReason != nullptr ? szReason : "(null reason)");
}

void GENONLINE_PLUGIN_CALL Host_IgnoreMessageViaTransport(void* /*userData*/, uint64_t userID64, const void* /*pData*/, uint32_t dataLen)
{
	NetworkLog(ELogVerbosity::LOG_DEBUG, "[Plugin] Dropping an anticheat message of %u bytes for user %u from a plugin that is not the anticheat",
		dataLen, static_cast<uint32_t>(userID64));
}

void FillHostApi()
{
	s_hostApi = GenOnlinePluginHostApi();
	s_hostApi.structSize = sizeof(GenOnlinePluginHostApi);
	s_hostApi.userData = nullptr;
	s_hostApi.Log = Host_Log;
	s_hostApi.LobbyChatOutput = Host_LobbyChatOutput;
	s_hostApi.ConnectionStateChanged = Host_ConnectionStateChanged;
	s_hostApi.ActionRequired = Host_ActionRequired;
	s_hostApi.IntegrityViolation = Host_IntegrityViolation;
	s_hostApi.SendMessageViaTransport = Host_SendMessageViaTransport;
	s_hostApi.enabledCapabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT;

	s_transportOnlyHostApi = s_hostApi;
	s_transportOnlyHostApi.ActionRequired = Host_IgnoreActionRequired;
	s_transportOnlyHostApi.IntegrityViolation = Host_IgnoreIntegrityViolation;
	s_transportOnlyHostApi.SendMessageViaTransport = Host_IgnoreMessageViaTransport;
}

void LoadBuiltInAnticheat()
{
	if (s_bBuiltInLoaded)
	{
		return;
	}

	GenOnlinePluginApi api = {};
	api.structSize = sizeof(GenOnlinePluginApi);
	const int32_t getApiResult = GenOnlineAntiCheat_GetApi(GENONLINE_PLUGIN_API_VERSION, &s_hostApi, &api);
	if (getApiResult != GENONLINE_PLUGIN_RESULT_OK || !ValidateAnticheatApi(api.anticheat))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: %s did not provide a valid anticheat table (%d)", GENONLINE_ANTICHEAT_PLUGIN_NAME, getApiResult);
		return;
	}

	const int32_t initResult = api.Initialize();
	if (initResult != GENONLINE_PLUGIN_RESULT_OK)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: %s Initialize returned %d - %s", GENONLINE_ANTICHEAT_PLUGIN_NAME, initResult, DescribeResult(initResult));
		api.Shutdown();
		return;
	}

	s_builtInApi = api;
	s_bBuiltInLoaded = true;
}

void UnloadBuiltInAnticheat()
{
	if (!s_bBuiltInLoaded)
	{
		return;
	}

	s_builtInApi.Shutdown();
	s_builtInApi = GenOnlinePluginApi();
	s_bBuiltInLoaded = false;
}

// The plugin DLL serves the anticheat calls when it provides them, GenOnline Anti-Cheat serves them otherwise.
const GenOnlinePluginAnticheatApi* GetActiveAnticheat()
{
	if (s_pAnticheat != nullptr)
	{
		return s_pAnticheat;
	}

	return s_bBuiltInLoaded ? s_builtInApi.anticheat : nullptr;
}

void ResetPluginState()
{
	s_hPluginModule = nullptr;
	s_pluginApi = GenOnlinePluginApi();
	s_pLinkedAccount = nullptr;
	s_pAnticheat = nullptr;
	s_pTransport = nullptr;
	s_pPeerTracking = nullptr;
	s_bPluginLoaded = false;
	s_bPluginIsAnticheat = false;
	s_bSessionStarted = false;
	s_tokenCreationTime = -1;
	s_pluginFiles.Close();
}

// A failed anticheat plugin blocks online play. A failed transport only plugin leaves the standard connection and GenOnline Anti-Cheat.
void MarkPluginLoadFailed()
{
	if (s_bPluginLoadBlocksOnline)
	{
		s_bPluginLoadFailed = true;
		return;
	}

	s_bTransportPluginLoadFailed = true;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] The transport plugin %s did not load, the game uses the standard connection", s_pluginName.c_str());
}

void FailPluginLoad(GenOnlinePluginPlatform::Module hModule)
{
	if (hModule != nullptr)
	{
		GenOnlinePluginPlatform::UnloadModule(hModule);
	}

	ResetPluginState();
	MarkPluginLoadFailed();
}

void GENONLINE_PLUGIN_CALL OnSignInStatus(void* /*userData*/, int32_t success)
{
	// NOTE: the plugin guarantees this fires exactly once, including on
	// failure, so these branches are genuinely reachable.
	if (success == GENONLINE_PLUGIN_FALSE)
	{
		// TODO_AC: Handle this, its a fatal error
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Middleware login failed - session will not be secure");
		return;
	}

	s_tokenCreationTime = UtcNowMs();

	if (s_pLinkedAccount != nullptr && s_pLinkedAccount->IsSignedIn() != GENONLINE_PLUGIN_FALSE)
	{
		char buf[4096] = { 0 };
		if (s_pLinkedAccount->GetIdentityProof(buf, sizeof(buf)) != GENONLINE_PLUGIN_FALSE)
		{
			// don't trust the plugin to terminate the buffer
			buf[sizeof(buf) - 1] = '\0';

			// The identity proof is a credential, so every build logs only its length and the first 8 hex digits of its SHA256.
			const size_t proofLength = strlen(buf);
			std::array<uint8_t, 32> proofHash = {};
			if (GenOnlinePluginPlatform::ComputeSha256OfData(buf, proofLength, proofHash))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Got MW token (%zu chars, sha256 %02x%02x%02x%02x)", proofLength, proofHash[0], proofHash[1], proofHash[2], proofHash[3]);
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Got MW token (%zu chars, sha256 unavailable)", proofLength);
			}

			// Now we can begin login
			GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
			if (pAuthInterface != nullptr)
			{
				pAuthInterface->SendMiddlewareToken(std::string(buf));
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Auth interface became null during login callback");
			}
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Failed to retrieve middleware auth token after a successful login");
		}
	}
	else
	{
		// TODO_AC: Handle this, its a fatal error
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Login reported success but the plugin is not logged in");
	}
}

void GENONLINE_PLUGIN_CALL OnRefreshStatus(void* /*userData*/, int32_t success)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Refreshed token: %d", success);
	if (success == GENONLINE_PLUGIN_FALSE)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: Token refresh failed");
		// TODO_AC: Handle this, its a fatal error
		return;
	}
}
} // namespace

void GenOnlinePluginHost::LoadPlugin(const std::string& anticheatPlugin, const std::vector<std::string>& transportPlugins)
{
	if (s_bPluginLoaded || s_hPluginModule != nullptr)
	{
		return;
	}

	s_bPluginLoadFailed = false;
	s_bTransportPluginLoadFailed = false;

	FillHostApi();
	LoadBuiltInAnticheat();

	const GenOnlinePluginRoles::Selection selection = GenOnlinePluginRoles::Select(anticheatPlugin, transportPlugins);
	const std::string& pluginName = selection.pluginName;
	s_pluginName = pluginName;
	s_bPluginLoadBlocksOnline = GenOnlinePluginRoles::DoesLoadFailureBlockOnline(selection);

	if (!selection.skippedTransports.empty())
	{
		std::string skipped;
		for (const std::string& transport : selection.skippedTransports)
		{
			skipped += skipped.empty() ? transport : ", " + transport;
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] The host loads one plugin DLL, so %s loads and the transport plugins %s do not load", pluginName.c_str(), skipped.c_str());
	}

	// No plugin is the normal default.
	if (pluginName.empty())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] No plugin configured");
		return;
	}

	if (!IsValidPluginName(pluginName))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Refusing plugin name '%s', only letters, digits and underscores are allowed", pluginName.c_str());
		MarkPluginLoadFailed();
		return;
	}

	const std::wstring exeDirectory = GetExecutableDirectory();
	if (exeDirectory.empty())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Cannot determine the game executable directory");
		MarkPluginLoadFailed();
		return;
	}

	const std::string pluginsFolder = to_utf8(exeDirectory + L"plugins");
	const std::string pluginPathUtf8 = GenOnlinePluginPlatform::JoinPath(pluginsFolder, pluginName + "\\" + pluginName + ".dll");

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Attempting to load plugin from %s", pluginPathUtf8.c_str());

	// The signed manifest names the SHA256 of every plugin file. The files and their folders stay locked until the plugin unloads.
	// A build with a plugin key refuses every plugin that fails the check, debug builds included.
	std::string trustError;
	bool bTrusted = false;
	if (!GenOnlinePluginTrust::HasPublicKeys(s_pluginPublicKeys))
	{
		trustError = "this build has no plugin signing keys";
	}
	else
	{
		bTrusted = GenOnlinePluginTrust::VerifyPluginFolder(pluginsFolder, pluginName, GENONLINE_PLUGIN_API_VERSION, s_pluginPublicKeys, s_allowedPlugins, s_pluginFiles, trustError);
	}

	if (bTrusted)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Plugin %s %s has a valid signed manifest", pluginName.c_str(), s_pluginFiles.GetVersion().c_str());
	}
	else if (!GenOnlinePluginTrust::MayLoadUnverifiedPlugin(s_pluginPublicKeys, kAllowUnsignedPlugins))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Refusing plugin %s, %s", pluginName.c_str(), trustError.c_str());
		FailPluginLoad(nullptr);
		return;
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] WARNING: Plugin %s is not verified, %s. Loading it because this build defines GENONLINE_ALLOW_UNSIGNED_PLUGINS", pluginName.c_str(), trustError.c_str());
		std::string openError;
		if (!s_pluginFiles.OpenUnverifiedMainFile(pluginsFolder, pluginName, openError))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Refusing plugin %s, %s", pluginName.c_str(), openError.c_str());
			FailPluginLoad(nullptr);
			return;
		}
	}

	// The module loads from the locked main DLL, so the loader opens the file that was hashed.
	std::string loadError;
	GenOnlinePluginPlatform::Module hModule = GenOnlinePluginPlatform::LoadModule(s_pluginFiles.GetMainFile(), loadError);
	if (hModule == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Failed to load %s, %s", s_pluginFiles.GetMainFilePath().c_str(), loadError.c_str());
		FailPluginLoad(nullptr);
		return;
	}

	// This check runs after DllMain of the module and of its imports, so it detects a swapped module only after its code ran.
	if (!GenOnlinePluginPlatform::IsModuleBackedByFile(hModule, s_pluginFiles.GetMainFile()))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: The loaded module is not the hashed file %s", pluginPathUtf8.c_str());
		FailPluginLoad(hModule);
		return;
	}

	GenOnlinePluginGetApiFn fnGetApi = reinterpret_cast<GenOnlinePluginGetApiFn>(GenOnlinePluginPlatform::GetSymbol(hModule, GENONLINE_PLUGIN_GET_API_EXPORT_NAME));
	if (fnGetApi == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: %s does not export %s, it is not a GenOnline plugin API %u plugin", pluginPathUtf8.c_str(), GENONLINE_PLUGIN_GET_API_EXPORT_NAME, GENONLINE_PLUGIN_API_VERSION);
		FailPluginLoad(hModule);
		return;
	}

	GenOnlinePluginApi api = {};
	api.structSize = sizeof(GenOnlinePluginApi);
	// The plugin learns its roles from enabledCapabilities and runs only those.
	GenOnlinePluginHostApi* pHostApi = selection.bAnticheat ? &s_hostApi : &s_transportOnlyHostApi;
	pHostApi->enabledCapabilities = GenOnlinePluginRoles::GetEnabledCapabilities(selection);
	const int32_t getApiResult = fnGetApi(GENONLINE_PLUGIN_API_VERSION, pHostApi, &api);
	if (getApiResult != GENONLINE_PLUGIN_RESULT_OK)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: %s returned %d - %s", GENONLINE_PLUGIN_GET_API_EXPORT_NAME, getApiResult, DescribeResult(getApiResult));
		FailPluginLoad(hModule);
		return;
	}

	if (api.structSize < kPluginApiMinSize || api.structSize > sizeof(GenOnlinePluginApi) || api.apiVersion != GENONLINE_PLUGIN_API_VERSION)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Plugin table has size %u and version %u, this build needs size %u and version %u",
			api.structSize, api.apiVersion, kPluginApiMinSize, GENONLINE_PLUGIN_API_VERSION);
		FailPluginLoad(hModule);
		return;
	}

	if (api.name == nullptr || api.Initialize == nullptr || api.Tick == nullptr || api.Shutdown == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] ERROR: Plugin table lacks its name, Initialize, Tick or Shutdown");
		FailPluginLoad(hModule);
		return;
	}

	const GenOnlinePluginLinkedAccountApi* pLinkedAccount = nullptr;
	const GenOnlinePluginAnticheatApi* pAnticheat = nullptr;
	const GenOnlinePluginTransportApi* pTransport = nullptr;
	if (!AcceptSubInterface(api, GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT, api.linkedAccount, ValidateLinkedAccountApi, "linked_account", pLinkedAccount)
		|| !AcceptSubInterface(api, GENONLINE_PLUGIN_CAP_ANTICHEAT, api.anticheat, ValidateAnticheatApi, "anticheat", pAnticheat)
		|| !AcceptSubInterface(api, GENONLINE_PLUGIN_CAP_TRANSPORT, api.transport, ValidateTransportApi, "transport", pTransport))
	{
		FailPluginLoad(hModule);
		return;
	}

	// The host uses only the tables of the roles that the plugin loads for. GenOnline Anti-Cheat stays the anticheat otherwise.
	// A transport only plugin keeps its anticheat table for the session and peer calls that its transport needs.
	const GenOnlinePluginAnticheatApi* pPeerTracking = nullptr;
	if (!selection.bAnticheat)
	{
		if (GenOnlinePluginRoles::DoesPluginTrackPeersOnly(selection))
		{
			pPeerTracking = pAnticheat;
		}

		pAnticheat = nullptr;
	}

	if (!selection.bTransport)
	{
		pTransport = nullptr;
	}

	// A transport that does not name the sender of its packets counts as no transport.
	if (pTransport != nullptr && !GenOnlinePluginRoles::DoesTransportNameSenders(*pTransport))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] The transport of %s does not name the sender of its packets, the host does not use it", api.name);
		pTransport = nullptr;
		if (GenOnlinePluginRoles::DoesTransportLossFailLoad(selection))
		{
			FailPluginLoad(hModule);
			return;
		}
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Loaded %s %s as %s (linked_account=%d, anticheat=%d, transport=%d)",
		api.name, api.version != nullptr ? api.version : "(no version)", selection.bAnticheat ? "the anticheat" : "a transport only",
		pLinkedAccount != nullptr, pAnticheat != nullptr, pTransport != nullptr);

	const int32_t initResult = api.Initialize();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Initialize result = %d", initResult);
	if (initResult != GENONLINE_PLUGIN_RESULT_OK)
	{
		// A failed Initialize() leaves the plugin with no middleware platform, so every
		// subsequent call would fail. Tear it back down rather than running half-loaded.
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] FATAL: Plugin Initialize returned %d - %s", initResult, DescribeResult(initResult));

		// Initialize() is responsible for cleaning up after itself on failure, but call
		// Shutdown() anyway - it is documented as idempotent.
		api.Shutdown();
		FailPluginLoad(hModule);
		return;
	}

	s_hPluginModule = hModule;
	s_pluginApi = api;
	s_pLinkedAccount = pLinkedAccount;
	s_pAnticheat = pAnticheat;
	s_pTransport = pTransport;
	s_pPeerTracking = pPeerTracking;
	s_bPluginLoaded = true;
	s_bPluginIsAnticheat = selection.bAnticheat;

	// An accepted transport may carry pairs from now on, the service setting never reaches the plugin.
	if (s_pTransport != nullptr && s_pTransport->SetEnabled != nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Telling the transport of %s that it may carry pairs", api.name);
		s_pTransport->SetEnabled(GENONLINE_PLUGIN_TRUE);
	}

#if _DEBUG
	// Only a plugin that is the anticheat reports whether the session is secured.
	if (ApplicationHWnd != nullptr && s_bPluginIsAnticheat)
	{
		SetWindowTextA(ApplicationHWnd, IsExternalProcessRunning() ? "SECURED" : "INSECURE");
	}
#endif
}

void GenOnlinePluginHost::UnloadPlugin()
{
	if (!s_bPluginLoaded)
	{
		if (s_bSessionStarted)
		{
			Anticheat::EndSession();
		}

		UnloadBuiltInAnticheat();
		return;
	}

	// Close any live anticheat session first so the plugin can unregister its
	// middleware notifications while the platform is still valid.
	if (s_bSessionStarted)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Ending active session before shutdown");
		Anticheat::EndSession();
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Starting Shutdown");
	// REQUIRED: the plugin deliberately does not tear down its middleware from
	// DllMain (that would run under the loader lock and deadlock), so this
	// explicit call is the only thing that releases it.
	s_pluginApi.Shutdown();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Shutdown Complete");

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Unloading plugin");
	GenOnlinePluginPlatform::UnloadModule(s_hPluginModule);

	// all function pointers reference the unloaded module now
	ResetPluginState();
	s_bPluginLoadFailed = false;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Unloaded plugin");

	UnloadBuiltInAnticheat();
}

void GenOnlinePluginHost::Tick()
{
	if (s_bBuiltInLoaded)
	{
		s_builtInApi.Tick();
	}

	if (!s_bPluginLoaded)
	{
		return;
	}

	s_pluginApi.Tick();

	// Do we need to refresh our token?
	if (s_pLinkedAccount != nullptr && s_pLinkedAccount->IsSignedIn() != GENONLINE_PLUGIN_FALSE)
	{
		int64_t now = UtcNowMs();
		if (s_tokenCreationTime != -1 && now - s_tokenCreationTime >= 45 * 60 * 1000) // refresh every 45m, tokens last 60m, giving us a 15m buffer to refresh and retry if something goes wrong
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Token is about to expire, refreshing...");
			LinkedAccount::Refresh();
		}
	}
}

bool GenOnlinePluginHost::IsPluginLoaded()
{
	return s_bPluginLoaded;
}

bool GenOnlinePluginHost::DidPluginFailToLoad()
{
	return s_bPluginLoadFailed;
}

bool GenOnlinePluginHost::DidTransportPluginFailToLoad()
{
	return s_bTransportPluginLoadFailed;
}

const std::string& GenOnlinePluginHost::GetPluginName()
{
	return s_pluginName;
}

bool GenOnlinePluginHost::IsPluginAnticheat()
{
	return s_bPluginLoaded && s_bPluginIsAnticheat;
}

bool GenOnlinePluginHost::IsExternalProcessRunning()
{
	if (!s_bPluginLoaded)
	{
		return false;
	}

	if (s_pluginApi.IsExternalProcessRunning == nullptr)
	{
		return true;
	}

	return s_pluginApi.IsExternalProcessRunning() != GENONLINE_PLUGIN_FALSE;
}

int GenOnlinePluginHost::GetAnticheatIdentifier()
{
	if (s_bPluginLoaded && s_bPluginIsAnticheat && s_pluginApi.GetAnticheatIdentifier != nullptr)
	{
		return s_pluginApi.GetAnticheatIdentifier();
	}

	if (s_bBuiltInLoaded && s_builtInApi.GetAnticheatIdentifier != nullptr)
	{
		return s_builtInApi.GetAnticheatIdentifier();
	}

	return 0;
}

void GenOnlinePluginHost::LinkedAccount::SignIn()
{
	if (s_pLinkedAccount == nullptr)
	{
		return;
	}

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return;
	}

	std::string authToken = pAuthInterface->GetAuthToken();
	s_pLinkedAccount->SignIn(authToken.c_str(), OnSignInStatus, nullptr);
}

void GenOnlinePluginHost::LinkedAccount::Refresh()
{
	if (s_pLinkedAccount == nullptr)
	{
		return;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Refreshing token");
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return;
	}

	s_tokenCreationTime = UtcNowMs();

	std::string authToken = pAuthInterface->GetAuthToken();
	s_pLinkedAccount->Refresh(authToken.c_str(), OnRefreshStatus, nullptr);
}

void GenOnlinePluginHost::Anticheat::BeginSession()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] BeginSession() called");

	const GenOnlinePluginAnticheatApi* pAnticheat = GetActiveAnticheat();
	if (pAnticheat != nullptr)
	{
		pAnticheat->BeginSession();
		s_bSessionStarted = true;
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] No anticheat provider, no session started");
	}

	if (s_pPeerTracking != nullptr)
	{
		s_pPeerTracking->BeginSession();
		s_bSessionStarted = true;
	}
}

void GenOnlinePluginHost::Anticheat::EndSession()
{
	// a stale exit request would eject us from the next lobby
	g_bPendingExitLobby = false;

	const GenOnlinePluginAnticheatApi* pAnticheat = GetActiveAnticheat();
	if (pAnticheat != nullptr)
	{
		pAnticheat->EndSession();
		s_bSessionStarted = false;
	}

	// The transport only plugin closes the connections of the session here.
	if (s_pPeerTracking != nullptr)
	{
		s_pPeerTracking->EndSession();
		s_bSessionStarted = false;
	}
}

bool GenOnlinePluginHost::Anticheat::RegisterPeer(const std::string& platformUserID, uint32_t userID)
{
	if (!s_bSessionStarted) // TODO_AC: This is hacky, it's because on lobby join, the server can send AC_REGISTER_PLAYER before we join the lobby, so we didnt actually start the session yet. We should buffer these messages until session start or something instead of relying on this hacky global
	{
		BeginSession();
	}

	// The transport only plugin learns the peer for its transport. Its result is not the anticheat result.
	if (s_pPeerTracking != nullptr)
	{
		const bool bTracked = s_pPeerTracking->RegisterPeer(platformUserID.c_str(), userID, GENONLINE_PLUGIN_CLIENT_PROTECTED) != GENONLINE_PLUGIN_FALSE;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Transport peer %s for user %" PRIu32 " added: %d", platformUserID.c_str(), userID, bTracked);
	}

	const GenOnlinePluginAnticheatApi* pAnticheat = GetActiveAnticheat();
	if (pAnticheat == nullptr)
	{
		return false;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "RegisterPlayer: %s to %" PRIu32, platformUserID.c_str(), userID);

	const bool bReg = pAnticheat->RegisterPeer(platformUserID.c_str(), userID, GENONLINE_PLUGIN_CLIENT_PROTECTED) != GENONLINE_PLUGIN_FALSE;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "RegisterPlayerFunc result: %d", bReg);
	return bReg;
}

bool GenOnlinePluginHost::Anticheat::DeregisterPeer(const std::string& platformUserID, uint32_t userID)
{
	if (s_pPeerTracking != nullptr)
	{
		const bool bUntracked = s_pPeerTracking->DeregisterPeer(platformUserID.c_str(), userID) != GENONLINE_PLUGIN_FALSE;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Plugin] Transport peer %s for user %" PRIu32 " removed: %d", platformUserID.c_str(), userID, bUntracked);
	}

	const GenOnlinePluginAnticheatApi* pAnticheat = GetActiveAnticheat();
	if (pAnticheat == nullptr)
	{
		return false;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "DeregisterPlayer: %s to %" PRIu32, platformUserID.c_str(), userID);

	const bool bReg = pAnticheat->DeregisterPeer(platformUserID.c_str(), userID) != GENONLINE_PLUGIN_FALSE;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "DeregisterPlayerFunc result: %d", bReg);
	return bReg;
}

void GenOnlinePluginHost::Anticheat::MessageArrived(uint32_t userID, const void* pData, uint32_t dataLen)
{
	if (pData == nullptr || dataLen == 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] ERROR: AC_NetworkMessageArrived received null/empty data");
		return;
	}

	const GenOnlinePluginAnticheatApi* pAnticheat = GetActiveAnticheat();
	if (pAnticheat != nullptr)
	{
		pAnticheat->MessageArrived(userID, pData, dataLen);
	}
}

bool GenOnlinePluginHost::Transport::IsActive()
{
	// The service setting picks the all-plugin mode, the plugin learns nothing from it.
	ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
	if (!serviceConf.connection.anticheat_transport)
	{
		return false;
	}

	return IsAvailable();
}

bool GenOnlinePluginHost::Transport::IsAvailable()
{
	return s_pTransport != nullptr && s_pTransport->IsAvailable() != GENONLINE_PLUGIN_FALSE;
}

void GenOnlinePluginHost::Transport::StartSignalling(const char* szPlatformUserID, uint64_t userID)
{
	if (s_pTransport != nullptr)
	{
		s_pTransport->StartSignalling(szPlatformUserID, userID);
	}
}

bool GenOnlinePluginHost::Transport::SendPacket(const char* szPlatformUserID, uint64_t userID, const void* pData, uint32_t numBytes, ENetworkChannels channel, EPacketReliability reliability)
{
	if (s_pTransport == nullptr)
	{
		return false;
	}

	s_pTransport->SendPacket(szPlatformUserID, userID, pData, numBytes, static_cast<uint8_t>(channel), static_cast<int32_t>(reliability));
	return true;
}

uint32_t GenOnlinePluginHost::Transport::GetNextPacketSize(ENetworkChannels channel)
{
	if (s_pTransport != nullptr)
	{
		return s_pTransport->GetNextPacketSize(static_cast<uint8_t>(channel));
	}

	return 0;
}

bool GenOnlinePluginHost::Transport::ReceivePacket(ENetworkChannels channel, const uint8_t** ppOutData, uint32_t* pOutNumBytes, int64_t* pOutSenderUserID)
{
	if (ppOutData != nullptr)
	{
		*ppOutData = nullptr;
	}

	if (pOutNumBytes != nullptr)
	{
		*pOutNumBytes = 0;
	}

	if (pOutSenderUserID != nullptr)
	{
		*pOutSenderUserID = -1;
	}

	if (s_pTransport == nullptr || ppOutData == nullptr || pOutNumBytes == nullptr || pOutSenderUserID == nullptr)
	{
		return false;
	}

	// The load check guarantees ReceivePacketFrom for every transport the host uses.
	uint64_t senderUserID = 0;
	if (s_pTransport->ReceivePacketFrom(static_cast<uint8_t>(channel), ppOutData, pOutNumBytes, &senderUserID) == GENONLINE_PLUGIN_FALSE)
	{
		return false;
	}

	*pOutSenderUserID = static_cast<int64_t>(senderUserID);
	return true;
}

void GenOnlinePluginHost::Transport::FreePacket(const uint8_t* pData)
{
	if (pData == nullptr)
	{
		return;
	}

	// the buffer was allocated inside the plugin, it must be released there too
	if (s_pTransport != nullptr)
	{
		s_pTransport->FreePacket(pData);
	}
}

int GenOnlinePluginHost::Transport::GetLatency(const char* szPlatformUserID, uint64_t userID)
{
	if (s_pTransport != nullptr)
	{
		return s_pTransport->GetLatency(szPlatformUserID, userID);
	}

	return 0;
}

bool GenOnlinePluginHost::Transport::IsRelayed(const char* szPlatformUserID, uint64_t userID)
{
	if (s_pTransport != nullptr)
	{
		return s_pTransport->IsRelayed(szPlatformUserID, userID) != GENONLINE_PLUGIN_FALSE;
	}

	return false;
}

void GenOnlinePluginHost::Transport::DisconnectPeer(const char* szPlatformUserID, uint64_t userID)
{
	if (s_pTransport != nullptr)
	{
		s_pTransport->DisconnectPeer(szPlatformUserID, userID);
	}
}

void GenOnlinePluginHost::Transport::DisconnectAll()
{
	if (s_pTransport != nullptr)
	{
		s_pTransport->DisconnectAll();
	}
}
