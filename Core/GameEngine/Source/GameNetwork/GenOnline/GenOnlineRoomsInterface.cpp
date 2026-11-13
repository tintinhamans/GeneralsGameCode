#include "GameNetwork/GenOnline/ProxyLogin.h"
#include <random>
#include "GameNetwork/GenOnline/ServiceEndpointPolicy.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#if RTS_ZEROHOUR
#include "Common/StatsExporterLive.h"
#endif
#include "GameNetwork/GenOnline/NetworkPacket.h"
#include "GameNetwork/GenOnline/NetworkBitstream.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/JsonHelpers.h"
#include "../GenOnlineInit.h"
#include "../HTTP/HTTPManager.h"
#include "GameNetwork/GameSpy/PeerDefs.h"

// -----------------------------
// Module info structure
// -----------------------------
struct GOModuleInfo {
    std::string path;
    std::string directory;
    DWORD size;
    void* baseAddress;
    std::string publisher;
    bool isSigned;
};

#include <windows.h>
#include <psapi.h>
#include <wincrypt.h>
#include <softpub.h>
#include <string>
#include <vector>
#include <iostream>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wintrust.lib")

// -----------------------------
// UTF-8 <-> UTF-16 helpers
// -----------------------------
std::wstring ToWide(const std::string& s) {
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);
    out.resize(size - 1);
    return out;
}

std::string ToUtf8(const std::wstring& s) {
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], size, nullptr, nullptr);
    out.resize(size - 1);
    return out;
}

std::vector<GOModuleInfo> GetLoadedModules() {
    std::vector<GOModuleInfo> modules;

    HMODULE hMods[1024];
    DWORD cbNeeded;

    HANDLE hProcess = GetCurrentProcess();

    if (!EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
        return modules;

    int count = cbNeeded / sizeof(HMODULE);

    for (int i = 0; i < count; i++) {
        wchar_t wpath[MAX_PATH];
        if (!GetModuleFileNameExW(hProcess, hMods[i], wpath, MAX_PATH))
            continue;

        MODULEINFO modInfo;
        if (!GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo)))
            continue;

        std::string path = ToUtf8(wpath);

        std::string directory;
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos)
            directory = path.substr(0, pos);
        else
            directory = "";

        bool isSigned = false;
        //std::string publisher = GetPublisherFromSignature(path, isSigned);
		std::string publisher = "";


        modules.push_back({
            path,
            directory,
            (DWORD)modInfo.SizeOfImage,
            modInfo.lpBaseOfDll,
            publisher,
            isSigned
            });
    }

    return modules;
}


static double RealtimeReconnectRandom()
{
	static thread_local std::mt19937 rng{ std::random_device{}() };
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
}

WebSocket::WebSocket(bool bRealtimeHub)
	: m_bRealtimeHub(bRealtimeHub)
	, m_hubReconnectPolicy(RealtimeReconnectRandom)
{
	m_pMulti = curl_multi_init();
	m_pHeaders = nullptr;
	if (m_bRealtimeHub)
	{
		GenOnlineRealtime::HubSession::Callbacks callbacks;
		callbacks.sendText = [this](const std::string& text)
		{
			return QueueOutbound(text, false);
		};
		callbacks.closeTransport = [this]()
		{
			ReleaseConnection();
		};
		callbacks.onReady = [this](const GenOnlineRealtime::ReadyEvent& ready)
		{
			GenOnlineRealtimeBridge::OnReady(ready, !m_bHubReadyOnce);
			OnHubReady();
		};
		callbacks.onDisconnected = [this](GenOnlineRealtime::DisconnectAction action, const std::string& reason)
		{
			OnHubDisconnected(action, reason);
		};
		callbacks.log = [](const std::string& line)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] %s", line.c_str());
		};
		m_pHubSession = std::make_unique<GenOnlineRealtime::HubSession>(callbacks);
		GenOnlineRealtimeBridge::RegisterHandlers(*m_pHubSession);
		RegisterPeerHandlers();
	}
}

void WebSocket::StartPeerSignalling(int64_t lobbyID, int64_t userID, uint16_t preferredPort, const std::string& middlewareID)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Lobby interface is null");
		return;
	}

	// -1 while a join is still in flight, when this can legitimately arrive first
	const int64_t currentLobbyID = pLobbyInterface->GetCurrentLobby().lobbyID;
	if (currentLobbyID != -1 && currentLobbyID != lobbyID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Ignoring signalling for lobby %lld, we are in lobby %lld", lobbyID, currentLobbyID);
		return;
	}

	// TODO_NGMP: Better location for this
	// When we find a new player, get their latest stats. Tooltip and loading screen need it, so we'll grab it now and then use cached data later since it cannot possibly change while in a lobby
	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (pStatsInterface != nullptr)
	{
		pStatsInterface->findPlayerStatsByID(userID, [=](bool bSuccess, PSPlayerStats stats)
			{

			}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
	}

	NetworkMesh* pMesh = pLobbyInterface->GetNetworkMeshForLobby();
	if (pMesh == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Network mesh is null");
		return;
	}
	pMesh->StartConnectionSignalling(middlewareID.c_str(), userID, preferredPort);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Starting signalling with %lld (MWID: %s)", userID, middlewareID.c_str());
}

void WebSocket::DisconnectPeer(int64_t lobbyID, int64_t userID)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby interface is null");
		return;
	}

	int64_t currentLobbyID = pLobbyInterface->GetCurrentLobby().lobbyID;
	if (currentLobbyID == -1 || currentLobbyID != lobbyID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby ID mismatch! Expected %lld, got %lld", currentLobbyID, lobbyID);
		return;
	}

	NetworkMesh* pMesh = pLobbyInterface->GetNetworkMeshForLobby();
	if (pMesh == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Network mesh is null");
		return;
	}
	pMesh->DisconnectUser(userID);
}

void WebSocket::CollectMeshConnectivity(std::vector<int64_t>& connected, std::vector<int64_t>& connecting)
{
	NetworkMesh* pMesh = nullptr;
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pMesh = pLobbyInterface->GetNetworkMeshForLobby();
	}

	if (pMesh != nullptr)
	{
		for (auto& conn : pMesh->GetAllConnections())
		{
			int64_t userID = conn.first;
			PlayerConnection& playerConn = conn.second;

			if (playerConn.GetState() == EConnectionState::CONNECTED_DIRECT)
			{
				// NOTE: Useful for testing
				//if (userID != 1)
				{
					connected.push_back(userID);
				}
			}
			else if (playerConn.GetState() == EConnectionState::CONNECTING_DIRECT || playerConn.GetState() == EConnectionState::FINDING_ROUTE)
			{
				// still negotiating: lets the service hold off restarting it
				connecting.push_back(userID);
			}
		}
	}
}

// The peer networking events of the hub. They run the same code as the legacy messages.
void WebSocket::RegisterPeerHandlers()
{
	using namespace GenOnlineRealtime;

	m_pHubSession->On<StartSignallingEvent>([this](const StartSignallingEvent& event)
		{
			if (event.preferred_port < 0 || event.preferred_port > 65535)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] StartSignalling with an invalid port %d", event.preferred_port);
				return;
			}
			StartPeerSignalling(event.lobby_id, event.user_id, (uint16_t)event.preferred_port, event.middleware_id);
		});
	m_pHubSession->On<DisconnectPeerEvent>([this](const DisconnectPeerEvent& event)
		{
			DisconnectPeer(event.lobby_id, event.user_id);
		});
	m_pHubSession->On<SignalEvent>([this](const SignalEvent& event)
		{
			std::vector<uint8_t> payload;
			if (!DecodePeerPayload(event.payload, payload))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Signal from %lld has an invalid payload", event.from);
				return;
			}
			PushPendingSignal(event.from, std::move(payload));
		});
	m_pHubSession->On<AnticheatMessageEvent>([](const AnticheatMessageEvent& event)
		{
			std::vector<uint8_t> payload;
			if (!DecodePeerPayload(event.payload, payload))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] AnticheatMessage from %lld has an invalid payload", event.from);
				return;
			}
			GenOnlinePluginHost::Anticheat::MessageArrived(event.from, payload.data(), payload.size());
		});
	m_pHubSession->On<AnticheatRegisterEvent>([](const AnticheatRegisterEvent& event)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Realtime AnticheatRegister for %lld and %s", event.user_id, event.middleware_id.c_str());
			if (!GenOnlinePluginHost::Anticheat::RegisterPeer(event.middleware_id, event.user_id))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GenOnlinePluginHost::Anticheat::RegisterPeer failed");
			}
		});
	m_pHubSession->On<AnticheatDeregisterEvent>([](const AnticheatDeregisterEvent& event)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Realtime AnticheatDeregister for %lld and %s", event.user_id, event.middleware_id.c_str());
			if (!GenOnlinePluginHost::Anticheat::DeregisterPeer(event.middleware_id, event.user_id))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GenOnlinePluginHost::Anticheat::DeregisterPeer failed");
			}
		});
	m_pHubSession->On<MeshCheckRequestEvent>([this](const MeshCheckRequestEvent& event)
		{
			MeshCheckResponseRequest response;
			response.mesh_check_id = event.mesh_check_id;
			response.attempt = event.attempt;
			CollectMeshConnectivity(response.connected, response.connecting);
			FireHub(response);
		});
	m_pHubSession->On<MeshCheckResultEvent>([](const MeshCheckResultEvent& event)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Mesh check %lld attempt %d: complete %d, reason %s, %d missing links", event.mesh_check_id, event.attempt, event.complete ? 1 : 0, event.reason.c_str(), (int)event.missing.size());
		});
	m_pHubSession->On<ProbeRequestEvent>([this](const ProbeRequestEvent& event)
		{
			if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] GOT PROBE REQUEST: %s!", event.url.c_str());
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] Received request");
			}
			GenOnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY, event.url);

			ProbeResponseRequest response;
			response.probe_id = event.probe_id;
			FireHub(response);
		});
	m_pHubSession->On<ModuleScanRequestEvent>([this](const ModuleScanRequestEvent& event)
		{
			ModuleScanResponseRequest response;
			response.scan_id = event.scan_id;
			for (const GOModuleInfo& module : GetLoadedModules())
			{
				ScannedModule entry;
				entry.path = module.path;
				entry.size = (int64_t)module.size;
				response.modules.push_back(entry);
			}
			FireHub(response);
		});
}


// The hub address must be a wss URL on the origin of the current service route.
bool WebSocket::IsDestinationTrusted(const std::string& url) const
{
	if (!m_bRealtimeHub)
		return GenOnlineServicesManager::IsWebSocketURLTrusted(url);
	return !GenOnlineRealtime::AcceptRealtimeUrl(url, GenOnlineServicesManager::GetAPIEndpoint("")).empty();
}

int64_t WebSocket::HubNowMs()
{
	return NowMs();
}

// Drops the socket without reconnecting. The caller decides what happens next.
void WebSocket::ReleaseConnection()
{
	m_proxyResolution.reset();
	if (m_pCurlWS != nullptr)
	{
		if (m_bConnected)
		{
			size_t sent = 0;
			(void)curl_ws_send(m_pCurlWS, "", 0, &sent, 0, CURLWS_CLOSE);
		}
		if (m_pMulti != nullptr)
		{
			curl_multi_remove_handle(m_pMulti, m_pCurlWS);
		}
		curl_easy_cleanup(m_pCurlWS);
		m_pCurlWS = nullptr;
	}
	ClearOutbound();
	m_vecWSPartialBuffer.clear();
	m_bConnected = false;
}

// The hub session reports the loss and decides how to continue. A loss outside a session reconnects.
void WebSocket::HubTransportLost(const char* reason)
{
	m_bConnected = false;
	m_vecWSPartialBuffer.clear();
	const GenOnlineRealtime::HubSession::State state = m_pHubSession->GetState();
	if (state == GenOnlineRealtime::HubSession::State::Idle || state == GenOnlineRealtime::HubSession::State::Closed)
	{
		BeginReconnect();
		return;
	}
	m_pHubSession->OnTransportLost(reason);
}

void WebSocket::OnHubReady()
{
	EndReconnect();
	m_hubReconnectPolicy.Reset();
	m_bHubSessionRecoveryTried = false;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Ready");
	// Later connections restore their state from the snapshot events, so only the first one completes the login.
	if (!m_bHubReadyOnce)
	{
		m_bHubReadyOnce = true;
		if (m_fnWebsocketConnectedCallback != nullptr)
		{
			m_fnWebsocketConnectedCallback();
		}
	}
}

// Refreshes the tokens and registers the session again, then reconnects. A second refusal tears down.
void WebSocket::RecoverHubSession()
{
	m_bHubSessionRecoveryTried = true;
	EndReconnect();
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth == nullptr)
	{
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
		return;
	}
	pAuth->RecoverRealtimeSession([this](bool bRecovered)
		{
			// The socket may have been replaced or shut down while the refresh ran.
			GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
			std::shared_ptr<WebSocket> pSocket = pManager != nullptr ? pManager->Internal_GetWebSocket() : nullptr;
			if (pSocket.get() != this || m_bShuttingDown)
			{
				return;
			}
			if (bRecovered)
			{
				BeginReconnect();
				return;
			}
			pManager->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
		});
}

void WebSocket::OnHubDisconnected(GenOnlineRealtime::DisconnectAction action, const std::string& reason)
{
	m_bConnected = false;
	switch (action)
	{
	case GenOnlineRealtime::DisconnectAction::Reconnect:
		BeginReconnect();
		break;
	case GenOnlineRealtime::DisconnectAction::ReconnectAfterKick:
		BeginReconnect();
		m_nextReconnectAttempt = NowMs() + GenOnlineRealtime::KICK_RECONNECT_DELAY_MS;
		break;
	case GenOnlineRealtime::DisconnectAction::RefreshAndReconnect:
		if (m_bHubSessionRecoveryTried)
		{
			EndReconnect();
			GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
			break;
		}
		RecoverHubSession();
		break;
	case GenOnlineRealtime::DisconnectAction::StopSignedInElsewhere:
	case GenOnlineRealtime::DisconnectAction::ReturnToLogin:
	default:
		EndReconnect();
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(reason == "moderation.banned" ? EGOTearDownReason::MODERATION_BAN : EGOTearDownReason::LOST_CONNECTION);
		break;
	}
}

WebSocket::~WebSocket()
{
	Shutdown();
	// Only call Shutdown if it has not been initiated already.
	// GenOnlineServicesManager::Shutdown() calls Shutdown() before releasing the shared_ptr,
	// so calling it again from the destructor would redundantly block for another 100ms sleep
	// and attempt to free already-released curl resources.
	if (!m_bShuttingDown)
	{
		Shutdown();
	}




	if (m_pHeaders != nullptr)
	{
		curl_slist_free_all(m_pHeaders);
		m_pHeaders = nullptr;
	}
}

// Reads the missing links of a start refusal or cancellation. Each entry is a [source, target] pair of user IDs.
static std::vector<std::pair<int64_t, int64_t>> ParseMissingLinks(const nlohmann::json& jsonObject)
{
	std::vector<std::pair<int64_t, int64_t>> missingLinks;
	if (jsonObject.contains("missing") && jsonObject["missing"].is_array())
	{
		for (const auto& link : jsonObject["missing"])
		{
			if (link.is_array() && link.size() == 2 && link[0].is_number_integer() && link[1].is_number_integer())
			{
				missingLinks.emplace_back(link[0].get<int64_t>(), link[1].get<int64_t>());
			}
		}
	}
	return missingLinks;
}

int WebSocket::Ping()
{
	// the hub session sends its own ping records
	if (!m_bConnected || m_bRealtimeHub)
	{
		return CURLE_AGAIN;
	}

	// keep the app heartbeat for older services, no raw PING, at most one queued under backpressure
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::PING;
	QueueOutbound(j.dump(), true);
	return CURLE_OK;
}

void WebSocket::Connect(const char* url, bool bIsReconnect, std::function<void(void)> fnWebsocketConnectedCallback)
{
	if (m_bConnected || m_bShuttingDown)
		return;
	// Copy before replacing the stored address: reconnect passes its c_str().
	const std::string destination(url);
	m_strWebsocketAddr = destination;
	m_fnWebsocketConnectedCallback = fnWebsocketConnectedCallback;
	m_connectAsReconnect = bIsReconnect;
	m_lastPong = NowMs();
	m_proxySetupStarted = NowMs();
	m_proxyResolution.reset();
	m_proxyPolicy = {};
	m_proxyRoute = 0;
	m_proxyPrompted = false;
	if (m_pCurlWS != nullptr)
	{
		curl_multi_remove_handle(m_pMulti, m_pCurlWS);
		curl_easy_cleanup(m_pCurlWS);
		m_pCurlWS = nullptr;
	}
	curl_slist_free_all(m_pHeaders);
	m_pHeaders = nullptr;
	if (!IsDestinationTrusted(destination))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Refused untrusted service destination");
		FailConnectionSetup();
		return;
	}
	m_proxyResolution = ServiceConnection::StartProxyResolution(
		GenOnlineServicesManager::Settings.Network_GetConnectionSettings(), destination);
}

// the lobby connection is a visible flow: a proxy that wants a login gets one prompt, then the handshake runs again
void WebSocket::ResumeAfterProxyLogin(bool loggedIn)
{
	if (m_bShuttingDown || m_bConnected)
	{
		return;
	}
	if (!loggedIn)
	{
		FailConnectionSetup();
		return;
	}
	m_proxySetupStarted = NowMs();
	StartResolvedConnection();
}

void WebSocket::FailConnectionSetup()
{
	m_proxyResolution.reset();
	m_bConnected = false;
	m_vecWSPartialBuffer.clear();
	if (m_bReconnecting)
	{
		m_reconnectAttemptStarted = -1;
		m_nextReconnectAttempt = NowMs() + ComputeReconnectDelay();
	}
	else
	{
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
	}
}

void WebSocket::PollConnectionSetup()
{
	if (!m_proxyResolution)
		return;
	if (NowMs() - m_proxySetupStarted >= m_reconnectAttemptTimeout)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Proxy policy failed: phase=proxy-timeout native_code=0");
		FailConnectionSetup();
		return;
	}
	if (!m_proxyResolution->ready.load(std::memory_order_acquire))
		return;
	m_proxyPolicy = std::move(m_proxyResolution->result);
	m_proxyResolution.reset();
	if (!m_proxyPolicy.error.empty() || m_proxyPolicy.routes.empty())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Proxy policy failed: phase=%s native_code=%lu detail=%s",
			m_proxyPolicy.failurePhase.empty() ? "proxy-policy" : m_proxyPolicy.failurePhase.c_str(),
			m_proxyPolicy.nativeError, m_proxyPolicy.error.c_str());
		FailConnectionSetup();
		return;
	}
	if (ServiceConnection::ShouldLogProxyPolicy(true, m_proxyPolicy))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Proxy route policy: %zu route(s), first=%s",
			m_proxyPolicy.routes.size(), m_proxyPolicy.routes.front().direct ? "direct" : m_proxyPolicy.routes.front().url.c_str());
		if (!m_proxyPolicy.fallbackReason.empty())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Proxy policy fallback: native_code=%lu detail=%s",
				m_proxyPolicy.nativeError, m_proxyPolicy.fallbackReason.c_str());
		}
	}
	StartResolvedConnection();
}

void WebSocket::StartResolvedConnection()
{
	if (m_pCurlWS != nullptr)
	{
		curl_multi_remove_handle(m_pMulti, m_pCurlWS);
		curl_easy_cleanup(m_pCurlWS);
	}
	curl_slist_free_all(m_pHeaders);
	m_pHeaders = nullptr;
	m_pCurlWS = curl_easy_init();
	if (m_proxyRoute > 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Route %zu: %s", m_proxyRoute + 1, m_proxyPolicy.routes[m_proxyRoute].direct ? "direct" : m_proxyPolicy.routes[m_proxyRoute].url.c_str());
	}
	std::string error;
	auto* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (m_pCurlWS == nullptr || auth == nullptr || !auth->IsLoggedIn() || !IsDestinationTrusted(m_strWebsocketAddr)
		|| !ServiceConnection::ConfigureCurl(m_pCurlWS, m_strWebsocketAddr, m_proxyPolicy.routes[m_proxyRoute], error))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Connection policy: %s", error.c_str());
		FailConnectionSetup();
		return;
	}
	const std::string authorization = "Authorization: Bearer " + auth->GetAuthToken();
	m_pHeaders = curl_slist_append(nullptr, authorization.c_str());
	if (m_pHeaders == nullptr)
	{
		FailConnectionSetup();
		return;
	}
	if (!m_bRealtimeHub)
	{
		curl_slist* updated = curl_slist_append(m_pHeaders, m_connectAsReconnect ? "is-reconnect: true" : "is-reconnect: false");
		if (updated == nullptr)
		{
			FailConnectionSetup();
			return;
		}
		m_pHeaders = updated;
	}
	CURLcode result = CURLE_OK;
	auto option = [&](CURLoption key, auto value) {
		if (result == CURLE_OK)
			result = curl_easy_setopt(m_pCurlWS, key, value);
	};
	option(CURLOPT_URL, m_strWebsocketAddr.c_str());
	option(CURLOPT_CONNECT_ONLY, 2L);
	option(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	const long remaining = static_cast<long>(std::max<int64_t>(1, m_reconnectAttemptTimeout - (NowMs() - m_proxySetupStarted)));
	option(CURLOPT_CONNECTTIMEOUT_MS, remaining);
	option(CURLOPT_TIMEOUT_MS, remaining);
	option(CURLOPT_HTTPHEADER, m_pHeaders);
	if (result != CURLE_OK || curl_multi_add_handle(m_pMulti, m_pCurlWS) != CURLM_OK)
		FailConnectionSetup();
}

void WebSocket::SendData_RoomChatMessage(UnicodeString& msg, bool bIsAction)
{
	if (m_bRealtimeHub)
	{
		GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
		if (pRooms == nullptr || !pRooms->GetEffectiveRoomID().has_value())
		{
			return;
		}
		GenOnlineRealtime::SendMessageRequest request;
		request.channel_id = "room:" + std::to_string(*pRooms->GetEffectiveRoomID());
		request.type = bIsAction ? "action" : "chat";
		request.content = to_utf8(msg.str());
		InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
			{
				if (!outcome.Succeeded())
				{
					GenOnlineRealtimeBridge::ShowRoomNotice(GenOnlineRealtimeBridge::FailureText(outcome), true);
				}
			});
		return;
	}

	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_CHAT_FROM_CLIENT;
	j["message"] = to_utf8(msg.str());
	j["action"] = bIsAction;
	std::string strBody = j.dump(-1, 32, true);

	Send(strBody.c_str());
}

void WebSocket::SendData_MarkReady(bool bReady)
{
	if (m_bRealtimeHub)
	{
		GenOnlineRealtime::SetReadyRequest request;
		request.ready = bReady;
		InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
			{
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (!outcome.Succeeded() && pLobbyInterface != nullptr)
				{
					pLobbyInterface->ShowRealtimeLobbyFailure(outcome);
				}
			});
		return;
	}

	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_MARK_READY;
	j["ready"] = bReady;
	std::string strBody = j.dump();

	Send(strBody.c_str());
}


void WebSocket::SendData_JoinNetworkRoom(int roomID, uint64_t requestID)
{
	if (m_bRealtimeHub)
	{
		auto onOutcome = [roomID](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
			if (!outcome.Succeeded() && pRooms != nullptr)
			{
				pRooms->OnRealtimeRoomJoinFailed(roomID, GenOnlineRealtimeBridge::FailureText(outcome));
			}
		};
		if (roomID < 0)
		{
			InvokeHub(GenOnlineRealtime::LeaveRoomRequest(), nullptr);
			return;
		}
		// The members arrive as RoomMembers, which completes the room change.
		GenOnlineRealtime::JoinRoomRequest request;
		request.room_id = roomID;
		InvokeHub(request, onOutcome);
		return;
	}

	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_CHANGE_ROOM;
	j["room"] = roomID;
	if (requestID != 0)
	{
		j["request_id"] = requestID;
	}
	std::string strBody = j.dump();

	Send(strBody.c_str());
}

void WebSocket::Disconnect()
{
	m_proxyResolution.reset();
	EndReconnect();
	if (!m_bConnected)
	{
		ClearOutbound();
		return;
	}

	if (m_pCurlWS != nullptr)
	{
		// best effort, never insert CLOSE into a partially sent frame
		const CURLcode result = FlushOutbound();
		if (result == CURLE_OK)
		{
			size_t sent = 0;
			(void)curl_ws_send(m_pCurlWS, "", 0, &sent, 0, CURLWS_CLOSE);
		}

		// release headers
		if (m_pHeaders != nullptr)
		{
			curl_slist_free_all(m_pHeaders);
			m_pHeaders = nullptr;
		}


		// Remove from multi handle before cleanup (required by libcurl)
		if (m_pMulti != nullptr)
		{
			curl_multi_remove_handle(m_pMulti, m_pCurlWS);
		}


		// cleanup
		curl_easy_cleanup(m_pCurlWS);
		m_pCurlWS = nullptr;
	}

	ClearOutbound();
	m_vecWSPartialBuffer.clear();
	m_bConnected = false;
}

void WebSocket::Send(const char* send_payload)
{
	// A legacy message would break the hub record stream, so the hub socket drops it.
	if (m_bRealtimeHub)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Dropping a legacy message that the realtime hub does not accept");
		return;
	}
	QueueOutbound(std::string(send_payload), false);
}

bool WebSocket::QueueOutbound(std::string payload, bool heartbeat)
{
	std::scoped_lock<std::mutex> lock(m_outboundQueueMutex);
	if (m_bShuttingDown || !m_acceptOutbound)
	{
		if (!heartbeat && !m_bShuttingDown)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Rejecting message while disconnected; actions are not replayed after reconnect");
		}
		return false;
	}
	if (heartbeat && m_heartbeatQueued)
	{
		return false;
	}

	static constexpr size_t kMaxQueuedOutboundMsgs = 256;
	static constexpr size_t kMaxQueuedOutboundBytes = 2 * 1024 * 1024;
	if (m_vecQueuedOutboungMsgs.size() >= kMaxQueuedOutboundMsgs ||
		payload.size() > kMaxQueuedOutboundBytes - m_queuedOutboundBytes)
	{
		// the front may be partially transmitted, do not evict it
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Outbound queue full (%zu messages, %zu bytes), rejecting new message", m_vecQueuedOutboungMsgs.size(), m_queuedOutboundBytes);
		return false;
	}

	m_queuedOutboundBytes += payload.size();
	m_vecQueuedOutboungMsgs.push_back({ std::move(payload), heartbeat });
	m_heartbeatQueued |= heartbeat;
	return true;
}

void WebSocket::ClearOutbound()
{
	std::scoped_lock<std::mutex> lock(m_outboundQueueMutex);
	// delivery is ambiguous after a socket failure, so old actions are not replayed on a new socket
	if (!m_vecQueuedOutboungMsgs.empty())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Discarding %zu messages from closed connection (%zu bytes, %zu bytes consumed from current message)", m_vecQueuedOutboungMsgs.size(), m_queuedOutboundBytes, m_outboundOffset);
	}
	m_vecQueuedOutboungMsgs.clear();
	m_queuedOutboundBytes = 0;
	m_outboundOffset = 0;
	m_heartbeatQueued = false;
	m_acceptOutbound = false;
}

CURLcode WebSocket::FlushOutbound()
{
	// bounded work on the game thread, each call passes the remaining payload of the same frame (curl_ws_send partial writes)
	size_t bytesSent = 0;
	for (int attempt = 0; attempt < 16; ++attempt)
	{
		std::scoped_lock<std::mutex> lock(m_outboundQueueMutex);
		if (m_vecQueuedOutboungMsgs.empty())
		{
			return CURLE_OK;
		}
		const OutboundMessage& message = m_vecQueuedOutboungMsgs.front();
		size_t sent = 0;
		const size_t remaining = message.payload.size() - m_outboundOffset;
		CURLcode result = curl_ws_send(m_pCurlWS, message.payload.data() + m_outboundOffset,
			remaining, &sent, 0, m_bRealtimeHub ? CURLWS_TEXT : CURLWS_BINARY);
		if (sent > remaining)
		{
			return CURLE_SEND_ERROR;
		}
		m_outboundOffset += sent;
		bytesSent += sent;
		if (result != CURLE_OK && result != CURLE_AGAIN)
		{
			return result;
		}
		if (m_outboundOffset == message.payload.size())
		{
			m_queuedOutboundBytes -= message.payload.size();
			if (message.heartbeat)
			{
				m_heartbeatQueued = false;
			}
			m_vecQueuedOutboungMsgs.erase(m_vecQueuedOutboungMsgs.begin());
			m_outboundOffset = 0;
		}
		if (result == CURLE_AGAIN || sent == 0 || bytesSent >= 256 * 1024)
		{
			return CURLE_AGAIN;
		}
	}
	return CURLE_AGAIN;
}

class WebSocketMessageBase
{
public:
	EWebSocketMessageID msg_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessageBase, msg_id)
};

class WebSocketMessage_ModerationNotice : public WebSocketMessageBase
{
public:
	std::string action_type;
	std::string reason;
	std::string scope_type;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WebSocketMessage_ModerationNotice, msg_id, action_type, reason, scope_type)
};

class WebSocketMessage_ModerationCommandResult : public WebSocketMessageBase
{
public:
	uint64_t request_id = 0;
	bool success = false;
	std::string error_code;
	std::string message;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
		WebSocketMessage_ModerationCommandResult,
		msg_id,
		request_id,
		success,
		error_code,
		message)
};

class WebSocketMessage_NetworkStartSignalling : public WebSocketMessageBase
{
public:
	int64_t lobby_id;
	int64_t user_id;
	uint16_t preferred_port;
	std::string middleware_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkStartSignalling, msg_id, lobby_id, user_id, preferred_port, middleware_id)
};

class WebSocketMessage_ACRegisterPlayer : public WebSocketMessageBase
{
public:
    int64_t user_id;
    std::string mwid;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ACRegisterPlayer, msg_id, user_id, mwid)
};

class WebSocketMessage_ACDeregisterPlayer : public WebSocketMessageBase
{
public:
    int64_t user_id;
    std::string mwid;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ACDeregisterPlayer, msg_id, user_id, mwid)
};

class WebSocketMessage_NetworkDisconnectPlayer : public WebSocketMessageBase
{
public:
	int64_t lobby_id;
	int64_t user_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkDisconnectPlayer, msg_id, lobby_id, user_id)
};

class WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby : public WebSocketMessageBase
{
public:
	int64_t lobby_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby, msg_id, lobby_id)
};


class WebSocketMessage_RoomChatIncoming : public WebSocketMessageBase
{
public:
	std::string message;
	bool action;
	bool admin;
	bool name_change;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_RoomChatIncoming, msg_id, message, action, admin, name_change)
};

class WebSocketMessage_Social_FriendChatMessage_Incoming : public WebSocketMessageBase
{
public:
	int64_t source_user_id;
	int64_t target_user_id;
	std::string message;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendChatMessage_Incoming, msg_id, source_user_id, target_user_id, message)
};

class WebSocketMessage_Social_FriendStatusChanged : public WebSocketMessageBase
{
public:
	std::string display_name;
	bool online;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendStatusChanged, display_name, online)
};

class WebSocketMessage_Social_FriendRequestAccepted : public WebSocketMessageBase
{
public:
	std::string display_name;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendRequestAccepted, display_name)
};

class WebSocketMessage_FriendsOverallStatusUpdate : public WebSocketMessageBase
{
public:
	int num_online;
	int num_pending;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_FriendsOverallStatusUpdate, num_online, num_pending)
};

class WebSocketMessage_NetworkSignal : public WebSocketMessageBase
{
public:
	int64_t target_user_id = -1;
	std::vector<uint8_t> payload;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkSignal, target_user_id, payload)
};

class WebSocketMessage_AnticheatMessage : public WebSocketMessageBase
{
public:
    int64_t target_user_id = -1;
    std::vector<uint8_t> payload;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_AnticheatMessage, target_user_id, payload)
};

class WebSocketMessage_ServerProbe : public WebSocketMessageBase
{
public:
	std::string url;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ServerProbe, msg_id, url)
};

class WebSocketMessage_StartGameResponse : public WebSocketMessageBase
{
public:
    std::string screenshot_url;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_StartGameResponse, msg_id, screenshot_url)
};

class WebSocketMessage_LobbyChatIncoming : public WebSocketMessageBase
{
public:
	std::string message;
	bool action;
	bool announcement;
	bool show_announcement_to_host;
	int64_t user_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_LobbyChatIncoming, msg_id, message, action, announcement, show_announcement_to_host, user_id)
};

class WebSocketMessage_MatchmakingMessage : public WebSocketMessageBase
{
public:
	std::string message;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_MatchmakingMessage, msg_id, message)
};

class WebSocketMessage_Social_NewFriendRequest : public WebSocketMessageBase
{
public:
	std::string display_name;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_NewFriendRequest, msg_id, display_name)
};

static bool JSONDeserialize(const char* szBuffer, nlohmann::json* jsonObject)
{
	try
	{
		*jsonObject = nlohmann::json::parse(szBuffer);
		return true;
	}
	catch (nlohmann::json::exception& jsonException)
	{
		if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Unparsable JSON: %s (%s)", szBuffer, jsonException.what());
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Invalid message JSON");
		}
		return false;
	}
	catch (...)
	{
		if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Unparsable JSON: %s", szBuffer);
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Invalid message JSON");
		}
		return false;
	}

	return false;
}

template<typename T>
static bool JSONGetAsObject(nlohmann::json& jsonObject, T* outMsg)
{
	try
	{
		*outMsg = jsonObject.get<T>();

		return true;
	}
	catch (nlohmann::json::exception& jsonException)
	{
		std::string targetTypeName = typeid(T).name();
		if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONGetAsObject: Unparsable JSON: Target Type is %s (%s)", targetTypeName.c_str(), jsonException.what());
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONGetAsObject: Invalid message type %s", targetTypeName.c_str());
		}
		return false;
	}
	catch (...)
	{
		std::string targetTypeName = typeid(T).name();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONGetAsObject: Unparsable JSON: Target Type is %s", targetTypeName.c_str());
		return false;
	}

	return false;
}

//static std::string strSignal = "str:1 ";

// Idempotent: repeated drop signals must not reset the backoff.
void WebSocket::BeginReconnect()
{
	if (m_bReconnecting)
	{
		return;
	}

	ClearOutbound();
	m_bReconnecting = true;
	m_numReconnectAttempts = 0;
	m_bFreshSession = false;
	m_reconnectAttemptStarted = -1;
	m_reconnectStartTime = NowMs();
	m_nextReconnectAttempt = m_reconnectStartTime + ComputeReconnectDelay();
}

void WebSocket::EndReconnect()
{
	m_bReconnecting = false;
	m_reconnectAttemptStarted = -1;
}

// First retry 0.5-3s, later ones 60-100% of the step (2s..10s cap), so clients that dropped together spread out.
int64_t WebSocket::ComputeReconnectDelay() const
{
	if (m_bRealtimeHub)
	{
		return m_hubReconnectPolicy.NextDelayMs();
	}

	static thread_local std::mt19937 rng{ std::random_device{}() };

	int64_t lo = m_reconnectBackoffBase / 4;
	int64_t hi = m_reconnectBackoffBase * 3 / 2;
	if (m_numReconnectAttempts > 0)
	{
		int64_t step = m_reconnectBackoffBase;
		for (int i = 1; i < m_numReconnectAttempts && step < m_reconnectBackoffCap; ++i)
		{
			step *= 2;
		}
		step = std::min<int64_t>(step, m_reconnectBackoffCap);
		lo = step * 6 / 10;
		hi = step;
	}

	return std::uniform_int_distribution<int64_t>(lo, hi)(rng);
}

// Restores what the old session had once a fresh one is connected.
static void RestoreSessionState()
{
	GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocial == nullptr)
	{
		return;
	}

	pSocial->GetFriendsList(false, nullptr);
	pSocial->GetBlockList(nullptr);
	if (pSocial->IsOverlayActive())
	{
		pSocial->RegisterForRealtimeServiceUpdates();
	}

	// resolve the previous room by stable ID against a freshly fetched room list, not by index
	GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRooms == nullptr)
	{
		return;
	}

	const std::vector<NetworkRoom>& roomsBeforeRefresh = pRooms->GetGroupRooms();
	const int previousRoomIndex = pRooms->GetCurrentRoomIndex();
	std::optional<int> previousRoomID;
	UnicodeString strPreviousRoomName;
	if (previousRoomIndex >= 0 && previousRoomIndex < (int)roomsBeforeRefresh.size())
	{
		previousRoomID = roomsBeforeRefresh[previousRoomIndex].GetRoomID();
		strPreviousRoomName = roomsBeforeRefresh[previousRoomIndex].GetRoomDisplayName();
	}

	pRooms->GetRoomList([pRooms, previousRoomID, strPreviousRoomName](bool bSuccess)
		{
			const std::vector<NetworkRoom>& rooms = pRooms->GetGroupRooms();
			if (!bSuccess || rooms.empty())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] RestoreSessionState: failed to fetch room list, cannot rejoin a room");
				return;
			}

			// fall back to the default room (index 0) if the room we were in no longer exists
			int roomIndexToJoin = 0;
			bool bFoundPreviousRoom = false;
			if (previousRoomID.has_value())
			{
				for (size_t i = 0; i < rooms.size(); ++i)
				{
					if (rooms[i].GetRoomID() == *previousRoomID)
					{
						roomIndexToJoin = (int)i;
						bFoundPreviousRoom = true;
						break;
					}
				}
			}

			if (previousRoomID.has_value() && !bFoundPreviousRoom)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] RestoreSessionState: room '%s' (id %d) no longer exists, joining the default room instead", to_utf8(strPreviousRoomName.str()).c_str(), *previousRoomID);
			}

			pRooms->JoinRoom(roomIndexToJoin);
		});
}

void WebSocket::UpdateReconnect()
{
	if (!m_bReconnecting)
	{
		return;
	}

	// Teardown already pending (kick, ban, auth failure): stop reconnecting.
	if (GenOnlineServicesManager::GetInstance()->IsPendingFullTeardown())
	{
		EndReconnect();
		return;
	}

	const int64_t currTime = NowMs();
	const bool bGameInProgress = TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress();

	// Session gone server-side: nothing to retry during a match, the window starts after it.
	if (m_bFreshSession && bGameInProgress)
	{
		m_reconnectStartTime = currTime;
		m_numReconnectAttempts = 0;
		m_reconnectAttemptStarted = -1;
		m_nextReconnectAttempt = currTime + ComputeReconnectDelay();
		return;
	}

	// Connect() kills the previous handle: leave a pending attempt alone unless stuck.
	if (m_reconnectAttemptStarted != -1 && (currTime - m_reconnectAttemptStarted) < m_reconnectAttemptTimeout)
	{
		return;
	}

	// The hub keeps reconnecting for as long as the user stays logged in.
	if (!m_bRealtimeHub && !bGameInProgress && (currTime - m_reconnectStartTime) >= m_reconnectMenuWindow)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (%d reconnect attempts failed)", m_numReconnectAttempts);
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
		m_bConnected = false;
		m_vecWSPartialBuffer.clear();
		EndReconnect();
		return;
	}

	// New session only once out of the lobby the server dropped us from.
	if (currTime < m_nextReconnectAttempt || (m_bFreshSession && TheGenOnlineGame != nullptr))
	{
		return;
	}

	++m_numReconnectAttempts;
	m_reconnectAttemptStarted = currTime;
	if (m_bRealtimeHub)
	{
		// keeps the login callback in case the first connection never reached Ready
		Connect(m_strWebsocketAddr.c_str(), true, m_fnWebsocketConnectedCallback);
		return;
	}
	Connect(m_strWebsocketAddr.c_str(), !m_bFreshSession, m_bFreshSession ? std::function<void()>(RestoreSessionState) : nullptr);
}

void WebSocket::Tick()
{
	if (m_bShuttingDown || m_pMulti == nullptr)
	{
		return;
	}
	// Main thread only; m_pCurlWS/m_vecWSPartialBuffer/m_bConnected are unlocked here.
	UpdateReconnect();
	PollConnectionSetup();

	/*
	if (strSignal.length() == 6)
	{
		for (int i = 0; i < 5000 - 6; ++i)
		{
			if (i == 5000 - 6 - 1)
			{
				strSignal += "+";
			}
			else
			{
				strSignal += i % 2 == 0 ? 'a' : 'b';
			}
		}
	}

	WebSocket* pWS = GenOnlineServicesManager::GetWebSocket();;
	pWS->SendData_Signalling(strSignal);
	*/

	int64_t currTime = NowMs();

    int numReqs = 0;
    curl_multi_perform(m_pMulti, &numReqs);
    curl_multi_poll(m_pMulti, NULL, 0, 0, NULL);

    {
        // Check for completed requests (initial connection only)
        int msgq = 0;
        CURLMsg* m = nullptr;
        while ((m = curl_multi_info_read(m_pMulti, &msgq)) != nullptr)
        {
            if (m->msg == CURLMSG_DONE)
            {
                CURL* pCurlHandle = m->easy_handle;

                if (pCurlHandle == m_pCurlWS) // shouldnt hear about anything else
                {
					long httpResponseCode = 0;
					long proxyStatus = 0;
					curl_easy_getinfo(pCurlHandle, CURLINFO_RESPONSE_CODE, &httpResponseCode);
					curl_easy_getinfo(pCurlHandle, CURLINFO_HTTP_CONNECTCODE, &proxyStatus);
					if (proxyStatus == 407)
						httpResponseCode = 407;

					/* Check for errors */
                    if (m->data.result != CURLE_OK)
                    {
						const CURLcode failure = m->data.result;
						if (httpResponseCode != 407 && (failure == CURLE_COULDNT_CONNECT || failure == CURLE_COULDNT_RESOLVE_PROXY || failure == CURLE_COULDNT_RESOLVE_HOST)
							&& m_proxyRoute + 1 < m_proxyPolicy.routes.size() && NowMs() - m_proxySetupStarted < m_reconnectAttemptTimeout)
						{
							++m_proxyRoute;
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Trying next policy route after pre-connection failure (%d)", failure);
							StartResolvedConnection();
							continue;
						}
                        m_bConnected = false;
                        m_vecWSPartialBuffer.clear();
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Handshake failed: phase=%s curl=%d status=%ld proxy_status=%ld",
                            ServiceConnection::FailurePhase(m->data.result, httpResponseCode, proxyStatus), m->data.result, httpResponseCode, proxyStatus);
						if (httpResponseCode == 407 && !m_bReconnecting && !m_proxyPrompted && m_proxyRoute < m_proxyPolicy.routes.size() && !m_proxyPolicy.routes[m_proxyRoute].direct)
						{
							m_proxyPrompted = true;
							ServiceConnection::RequestProxyLogin(m_proxyPolicy.routes[m_proxyRoute], [](bool loggedIn)
							{
								auto* manager = GenOnlineServicesManager::GetInstance();
								auto socket = manager ? manager->Internal_GetWebSocket() : nullptr;
								if (socket)
								{
									socket->ResumeAfterProxyLogin(loggedIn);
								}
							});
							continue;
						}

                        // 401 bad token and 409 session not registered get one refresh and registration, a 403 play ban ends the session
                        if (m_bRealtimeHub && (httpResponseCode == 401 || httpResponseCode == 409) && !m_bHubSessionRecoveryTried)
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Connection refused with status %ld, renewing the session", httpResponseCode);
                            RecoverHubSession();
                        }
                        else if (m_bRealtimeHub && (httpResponseCode == 401 || httpResponseCode == 403 || httpResponseCode == 409))
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Connection refused with status %ld", httpResponseCode);
                            EndReconnect();
                            GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(httpResponseCode == 403 ? EGOTearDownReason::MODERATION_BAN : EGOTearDownReason::AUTH_FAILED);
                        }
                        else if (m_bReconnecting)
                        {
                            if (!m_bRealtimeHub && !m_bFreshSession && m->data.result == CURLE_HTTP_RETURNED_ERROR && httpResponseCode == 205) // 205: session gone, can't resume
                            {
                                NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Server dropped our session (205), will reconnect as a new one");
                                m_bFreshSession = true;
                                m_reconnectAttemptStarted = -1;
                                m_reconnectStartTime = NowMs();
                                m_numReconnectAttempts = 0;
                                m_nextReconnectAttempt = m_reconnectStartTime + ComputeReconnectDelay();
                            }
                            else
                            {
                                m_reconnectAttemptStarted = -1;
                                m_nextReconnectAttempt = NowMs() + ComputeReconnectDelay();
                            }
                        }
                        else // give up immediately
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (initial connect)");
                            GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
                            m_bConnected = false;
                            m_vecWSPartialBuffer.clear();
                        }
                    }
                    else
                    {
                        if (m_bReconnecting)
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Re-Connected");

                            GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
                            if (pLobbyInterface != nullptr && pLobbyInterface->IsInLobby())
                            {
                                NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Reconnected while in a lobby, refreshing it");
                                pLobbyInterface->UpdateRoomDataCache(nullptr);
                            }
                        }
                        else
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Connected");
                        }

                        long protocol = 0;
                        curl_easy_getinfo(pCurlHandle, CURLINFO_HTTP_VERSION, &protocol);
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Handshake status=%ld protocol=%s", httpResponseCode, ServiceConnection::ProtocolName(protocol));

                        /* connected and ready */
                        m_bConnected = true;
                        {
                            std::scoped_lock<std::mutex> lock(m_outboundQueueMutex);
                            m_acceptOutbound = true;
                        }
                        m_vecWSPartialBuffer.clear();

                        EndReconnect();

                        // connecting is as good as a pong
                        m_lastPong = NowMs();

                        if (m_bRealtimeHub)
                        {
                            // the login completes on Ready, see OnHubReady
                            m_pHubSession->OnTransportOpen(NowMs());
                        }
                        else if (m_fnWebsocketConnectedCallback != nullptr)
                        {
                            m_fnWebsocketConnectedCallback();
                        }
                    }
                }
            }
        }
    }

    if (!m_bConnected)
    {
        return;
    }

	if (m_bRealtimeHub)
	{
		m_pHubSession->Tick(currTime);
		if (!m_bConnected)
		{
			return;
		}
	}
	else if ((currTime - m_lastPing) > m_timeBetweenUserPings)
	{
		m_lastPing = currTime;
		Ping();
	}

	const CURLcode sendResult = FlushOutbound();
	if (sendResult != CURLE_OK && sendResult != CURLE_AGAIN)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Send failed: %s, attempting reconnect", curl_easy_strerror(sendResult));
		if (m_bRealtimeHub)
		{
			HubTransportLost("send failed");
			return;
		}
		m_bConnected = false;
		m_vecWSPartialBuffer.clear();
		BeginReconnect();
		return;
	}

	// drain what the server queued, a signalling burst shouldn't take one frame per tick
	static constexpr int MAX_WS_FRAMES_PER_TICK = 64;
	CURLcode ret = CURL_LAST;
	for (int numFrames = 0; numFrames < MAX_WS_FRAMES_PER_TICK; ++numFrames)
	{
		ret = ReceiveOneFrame();
		if (ret != CURLE_OK || !m_bConnected)
		{
			break;
		}
	}

	// time since last pong? The hub session has its own server timeout.
	if (!m_bRealtimeHub && m_bConnected && m_lastPong != -1 && (currTime - m_lastPong) >= m_timeForWSTimeout)
	{
        // send event to sentry
#if defined(GENONLINE_USE_SENTRY)
        if (TheGenOnlineGame != nullptr)
        {
            AsciiString sentryMsg;
            sentryMsg.format("Got websocket disconnect (Timeout: %s), timeout is %lld, last pong was at %lld, current time is %lld, attempting reconnect", curl_easy_strerror(ret), currTime - m_lastPong, m_lastPong, currTime);
            sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "WEBSOCKET_DISCONNECT_TIMEOUT", sentryMsg.str()));
        }
#endif

		NetworkLog(ELogVerbosity::LOG_RELEASE, "Got websocket disconnect (Timeout: %s), timeout is %lld, last pong was at %lld, current time is %lld, attempting reconnect", curl_easy_strerror(ret), currTime - m_lastPong, m_lastPong, currTime);
        m_bConnected = false;
		BeginReconnect();
        m_vecWSPartialBuffer.clear();
	};
}

// Reads and handles at most one frame, returns the curl_ws_recv result (CURL_LAST if nothing was read)
CURLcode WebSocket::ReceiveOneFrame()
{
	// do recv
	size_t rlen = 0;
	const struct curl_ws_frame* meta = nullptr;
	char bufferThisRecv[8196 * 4] = { 0 };

	CURLcode ret = curl_ws_recv(m_pCurlWS, bufferThisRecv, sizeof(bufferThisRecv) - 1, &rlen, &meta);

	// SECURITY FIX: Validate rlen is within buffer bounds
	if (rlen >= sizeof(bufferThisRecv))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Received data size %zu exceeds buffer size %zu, discarding", rlen, sizeof(bufferThisRecv));
		return CURL_LAST;
	}

	if (ret == CURLE_OK)
	{
		if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket msg: %s", bufferThisRecv);
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "[WebSocket] Received %zu bytes", rlen);
		}
		NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket len: %d", rlen);

		// any server frame proves liveness, not just a JSON PONG
		m_lastPong = NowMs();

		// what type of message?
		if (meta != nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket flags: %d", meta->flags);
			if (meta->flags & CURLWS_PONG) // PONG
			{

			}
			else if (m_bRealtimeHub && (meta->flags & CURLWS_TEXT))
			{
				m_pHubSession->OnTransportText(bufferThisRecv, rlen, NowMs());
			}
			else if (meta->flags & CURLWS_TEXT)
			{
				bool bMessageComplete = false;

				static constexpr size_t MAX_WS_PARTIAL_SIZE = 2 * 1024 * 1024; // 2 MB
				if (m_vecWSPartialBuffer.size() + rlen > MAX_WS_PARTIAL_SIZE)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Partial buffer overflow, discarding message");
					m_vecWSPartialBuffer.clear();
					return CURL_LAST;
				}

				// SECURITY FIX: Store old size BEFORE resize to avoid off-by-one error in memcpy
				size_t oldSize = m_vecWSPartialBuffer.size();
				m_vecWSPartialBuffer.resize(oldSize + rlen);
				memcpy_s(m_vecWSPartialBuffer.data() + oldSize, rlen, bufferThisRecv, rlen);

				if (meta->flags & CURLWS_CONT)
				{
					bMessageComplete = false;
					NetworkLog(ELogVerbosity::LOG_DEBUG, "WEBSOCKET PARTIAL (CONT) OF SIZE %d, offset %d, bytes left %d! [MESSAGE COMPLETE: %d]", rlen, meta->offset, meta->bytesleft, bMessageComplete);
				}
				else if (meta->bytesleft > 0)
				{
					bMessageComplete = false;
					NetworkLog(ELogVerbosity::LOG_DEBUG, "WEBSOCKET PARTIAL (BYTESLEFT) OF SIZE %d, offset %d! [MESSAGE COMPLETE: %d]", rlen, meta->offset, bMessageComplete);
				}
				else
				{
					// if we got in here, it's a whole message, or the last part of a fragmented message
					bMessageComplete = true;
					NetworkLog(ELogVerbosity::LOG_DEBUG, "WEBSOCKET LAST FRAME OF SIZE %d!", rlen);
				}

				if (bMessageComplete)
				{
					try
					{
						// null terminate buffer
						m_vecWSPartialBuffer.push_back('\0');

						// process it
						nlohmann::json jsonObject;
						bool bDeserializedOK = JSONDeserialize(m_vecWSPartialBuffer.data(), &jsonObject);

						// clear buffer and resize
						m_vecWSPartialBuffer.clear();
						m_vecWSPartialBuffer.resize(0);

						if (bDeserializedOK)
						{
							if (jsonObject.contains("msg_id"))
							{
								WebSocketMessageBase msgDetails;
								bool bParsedBase = JSONGetAsObject<WebSocketMessageBase>(jsonObject, &msgDetails);

								if (bParsedBase)
								{
									EWebSocketMessageID msgID = msgDetails.msg_id;

									switch (msgID)
									{
									case EWebSocketMessageID::MODERATION_NOTICE:
									{
										WebSocketMessage_ModerationNotice moderationNotice;
										if (JSONGetAsObject(jsonObject, &moderationNotice))
										{
											HandleModerationNotice(
												moderationNotice.action_type,
												moderationNotice.reason,
												moderationNotice.scope_type);
										}
									}
									break;

									case EWebSocketMessageID::MODERATION_COMMAND_RESULT:
									{
										WebSocketMessage_ModerationCommandResult commandResult;
										if (JSONGetAsObject(jsonObject, &commandResult))
										{
											NetworkLog(
												ELogVerbosity::LOG_RELEASE,
												"Moderation command %llu %s: %s",
												static_cast<unsigned long long>(commandResult.request_id),
												commandResult.success ? "succeeded" : "failed",
												commandResult.message.c_str());
										}
									}
									break;

									case EWebSocketMessageID::PONG:
									{
										int64_t currTime = NowMs();
										m_lastPong = currTime;
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_CHAT_FROM_SERVER:
									{
										WebSocketMessage_RoomChatIncoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											SYSTEMTIME systemTime;
											GetLocalTime(&systemTime);

											UnicodeString unicodeStr;
											unicodeStr.format(L"[%2.2d:%2.2d] %s", systemTime.wHour, systemTime.wMinute, from_utf8(chatData.message).c_str());

											Color color = DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_NETWORK_ROOM, true, chatData.action, chatData.admin, chatData.name_change);

											GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
											if (pRoomsInterface != nullptr && pRoomsInterface->m_OnChatCallback != nullptr)
											{
												// the room line is server-formatted and carries no sender id; a name change notice is a system line
#if RTS_ZEROHOUR
												StatsExporterLiveChatOnline(STATS_EXPORTER_CHAT_ROOM, 0, "", chatData.message.c_str(), chatData.action, chatData.name_change, FALSE);
#endif
												pRoomsInterface->m_OnChatCallback(unicodeStr, color);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_CHAT_MESSAGE_SERVER_TO_CLIENT:
									{
										WebSocketMessage_Social_FriendChatMessage_Incoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											UnicodeString unicodeStr(from_utf8(chatData.message).c_str());

											GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnChatMessage(chatData.source_user_id, chatData.target_user_id, unicodeStr);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_ONLINE_STATUS_CHANGED:
									{
										WebSocketMessage_Social_FriendStatusChanged statusChangedData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusChangedData);

										if (bParsed)
										{
											GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnOnlineStatusChanged(statusChangedData.display_name, statusChangedData.online);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_FRIEND_REQUEST_ACCEPTED_BY_TARGET:
									{
										WebSocketMessage_Social_FriendRequestAccepted statusChangedData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusChangedData);

										if (bParsed)
										{
											GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnFriendRequestAccepted(statusChangedData.display_name);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIENDS_LIST_DIRTY:
									{
                                        // nothing to parse here, it's just an event only
                                        extern void GenOnlineUpdateBuddyInfo(bool bIsAutoRefresh = false, bool bUseCache = false);
										GenOnlineUpdateBuddyInfo(true);
									}
									break;

									case EWebSocketMessageID::SOCIAL_CANT_ADD_FRIEND_LIST_FULL:
									{
										// always show this notification, it's tied to a local user action
										GenOnlineShowNotificationBox(AsciiString::TheEmptyString, UnicodeString(L"Cannot sent friends request. Your friends list is full."));
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIENDS_OVERALL_STATUS_UPDATE:
									{
										WebSocketMessage_FriendsOverallStatusUpdate statusUpdateData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusUpdateData);

										if (bParsed)
										{
											UnicodeString strFormat = UnicodeString::TheEmptyString;
											if (statusUpdateData.num_online > 0 && statusUpdateData.num_pending > 0)
											{
												strFormat.format(L"You have %d friend(s) online and %d pending friend request(s)", statusUpdateData.num_online, statusUpdateData.num_pending);
											}
											else if (statusUpdateData.num_online > 0)
											{
												strFormat.format(L"You have %d friend(s) online.", statusUpdateData.num_online);
											}
											else if (statusUpdateData.num_pending > 0)
											{
												strFormat.format(L"You have %d pending friend request(s)", statusUpdateData.num_pending);
											}
											else
											{
												strFormat = UnicodeString(L"Press F5 or INSERT to bring up the communicator at any time (including in-game).");
											}

											// show it on the communicator too
											if (statusUpdateData.num_pending > 0)
											{
                                                GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
                                                if (pSocialInterface != nullptr)
                                                {
													pSocialInterface->RegisterInitialPendingRequestsUponLogin(statusUpdateData.num_pending);
                                                }
											}

											if (!strFormat.isEmpty())
											{
												// always show this notification
												GenOnlineShowNotificationBox(AsciiString::TheEmptyString, strFormat);
											}
										}
									}
									break;

									case EWebSocketMessageID::START_GAME:
									{
										auto* startLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										// The service and the other players have already started, so a missing link is only logged.
										if (startLobby != nullptr && startLobby->GetCurrentLobby().lobby_type == ELobbyType::CustomGame
											&& !startLobby->AreLocalLobbyPeersConnected())
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Starting with a required peer connection missing");
										}
                                        WebSocketMessage_StartGameResponse startGameData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &startGameData);

										if (bParsed)
										{
											// store URL
                                            GenOnlineServicesManager::GetInstance()->SetScreenshotS3URI_StartMatch(startGameData.screenshot_url.c_str());
										}

										// always start, even if we couldnt parse the url
                                        GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
                                        if (pLobbyInterface != nullptr && pLobbyInterface->m_callbackStartGamePacket != nullptr)
                                        {
                                            pLobbyInterface->m_callbackStartGamePacket();
                                        }
									}
									break;

									case EWebSocketMessageID::START_GAME_REJECTED:
									{
										std::string strReason;
										if (jsonObject.contains("reason") && jsonObject["reason"].is_string())
										{
											strReason = jsonObject["reason"].get<std::string>();
										}
										NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] The service refused the match start (%s)", strReason.c_str());

										const std::vector<std::pair<int64_t, int64_t>> missingLinks = ParseMissingLinks(jsonObject);

										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr && pLobbyInterface->m_callbackStartGameRejected != nullptr)
										{
											pLobbyInterface->m_callbackStartGameRejected(strReason, missingLinks);
										}
									}
									break;

									case EWebSocketMessageID::LOBBY_EVENT:
									{
										LobbyEvent lobbyEvent;
										try
										{
											JsonGetRequired(jsonObject, "event", lobbyEvent.event);
											JsonGetOptional(jsonObject, "duration_ms", lobbyEvent.duration_ms);
											JsonGetOptional(jsonObject, "reason", lobbyEvent.reason);
											JsonGetOptional(jsonObject, "value", lobbyEvent.value);
											JsonGetOptional(jsonObject, "friends_only", lobbyEvent.friends_only);
											JsonGetOptional(jsonObject, "user_id", lobbyEvent.user_id);
											JsonGetOptional(jsonObject, "display_name", lobbyEvent.display_name);
											lobbyEvent.missingLinks = ParseMissingLinks(jsonObject);
										}
										catch (...)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Ignoring a malformed lobby event");
											break;
										}

										NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Lobby event %s", lobbyEvent.event.c_str());
										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr && pLobbyInterface->m_callbackLobbyEvent != nullptr)
										{
											pLobbyInterface->m_callbackLobbyEvent(lobbyEvent);
										}
									}
									break;

									case EWebSocketMessageID::FULL_MESH_CONNECTIVITY_CHECK_RESPONSE:
									{
										int64_t meshCheckID = 0;
										int meshCheckAttempt = 0;
										if (jsonObject.contains("mesh_check_id") && jsonObject["mesh_check_id"].is_number_integer())
										{
											meshCheckID = jsonObject["mesh_check_id"].get<int64_t>();
										}
										if (jsonObject.contains("attempt") && jsonObject["attempt"].is_number_integer())
										{
											meshCheckAttempt = jsonObject["attempt"].get<int>();
										}

										// respond with our state
										std::vector<int64_t> connectivityMap;
										std::vector<int64_t> connectingMap;
										CollectMeshConnectivity(connectivityMap, connectingMap);

										// send response
										nlohmann::json j;
										j["msg_id"] = EWebSocketMessageID::FULL_MESH_CONNECTIVITY_CHECK_RESPONSE;
										j["mesh_check_id"] = meshCheckID;
										j["attempt"] = meshCheckAttempt;
										j["connectivity_map"] = connectivityMap;
										j["connecting_map"] = connectingMap;
										std::string strBody = j.dump();

										Send(strBody.c_str());
										break;
									}

									case EWebSocketMessageID::NETWORK_CONNECTION_START_SIGNALLING:
									{
										WebSocketMessage_NetworkStartSignalling startSignallingData;
										bool bParsed = JSONGetAsObject(jsonObject, &startSignallingData);

										if (bParsed)
										{
											StartPeerSignalling(startSignallingData.lobby_id, startSignallingData.user_id, startSignallingData.preferred_port, startSignallingData.middleware_id);
										}
									}
									break;

									case EWebSocketMessageID::AC_REGISTER_PLAYER:
                                    {
                                        WebSocketMessage_ACRegisterPlayer acData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Websocket AC_REGISTER_PLAYER for %lld and %s", acData.user_id, acData.mwid.c_str());
											if (!GenOnlinePluginHost::Anticheat::RegisterPeer(acData.mwid, acData.user_id))
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GenOnlinePluginHost::Anticheat::RegisterPeer failed");
											}
										}
                                    }
                                    break;

                                    case EWebSocketMessageID::AC_DEREGISTER_PLAYER:
                                    {
                                        WebSocketMessage_ACDeregisterPlayer acData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Websocket AC_DEREGISTER_PLAYER for %lld and %s", acData.user_id, acData.mwid.c_str());
											if (!GenOnlinePluginHost::Anticheat::DeregisterPeer(acData.mwid, acData.user_id))
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GenOnlinePluginHost::Anticheat::DeregisterPeer failed");
											}
										}
                                    }
                                    break;

									case EWebSocketMessageID::NETWORK_CONNECTION_DISCONNECT_PLAYER:
									{
										WebSocketMessage_NetworkDisconnectPlayer disconnectPlayerData;
										bool bParsed = JSONGetAsObject(jsonObject, &disconnectPlayerData);

										if (bParsed)
										{
											DisconnectPeer(disconnectPlayerData.lobby_id, disconnectPlayerData.user_id);
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_SIGNAL:
									{
										NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] GOT SIGNAL!");

										WebSocketMessage_NetworkSignal signalData;
										bool bParsed = JSONGetAsObject(jsonObject, &signalData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Signal User: %lld!", signalData.target_user_id);
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Signal Payload Size: %d!", (int)signalData.payload.size());
											// target_user_id is the sender here, the service fills it in from the authenticated session
											PushPendingSignal(signalData.target_user_id, std::move(signalData.payload));
										}
									}
									break;

									case EWebSocketMessageID::LOBBY_CHAT_FROM_SERVER:
									{
										WebSocketMessage_LobbyChatIncoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											UnicodeString unicodeStr(from_utf8(chatData.message).c_str());

											GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												int lobbySlot = -1;
												std::string senderName;
												auto lobbyMembers = pLobbyInterface->GetMembersListForCurrentRoom();
												for (const auto& lobbyMember : lobbyMembers)
												{
													if (lobbyMember.user_id == chatData.user_id)
													{
														lobbySlot = lobbyMember.m_SlotIndex;
														senderName = lobbyMember.display_name;
														break;
													}
												}

												// Match local setup notice colors.
												// System announcements stay neutral even when they come from the host;
												// player chat keeps its per-slot color.
												Color color = chatData.announcement
													? DetermineSystemNoticeColor(false, false)
													: DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_LOBBY, true, chatData.action, false, false, lobbySlot);

												if (pLobbyInterface->m_OnChatCallback != nullptr)
												{
#if RTS_ZEROHOUR
													GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
													const bool bIsLocal = pAuthInterface != nullptr && pAuthInterface->GetUserID() == chatData.user_id;
													StatsExporterLiveChatOnline(STATS_EXPORTER_CHAT_GAME_LOBBY, chatData.user_id, senderName.c_str(), chatData.message.c_str(), chatData.action, chatData.announcement, bIsLocal);
#endif
													pLobbyInterface->m_OnChatCallback(unicodeStr, color);
												}
											}
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_MEMBER_LIST_UPDATE:
									{
										std::unordered_map<uint64_t, NetworkRoomMember> mapMembers;
										for (const auto& playerEntryIter : jsonObject["members"])
										{
											NetworkRoomMember newMember;
											playerEntryIter["UserID"].get_to(newMember.user_id);
											playerEntryIter["Name"].get_to(newMember.display_name);
											playerEntryIter["IsAdmin"].get_to(newMember.m_bIsAdmin);

											mapMembers.emplace(newMember.user_id, newMember);
										}

										RoomSelectionResult selectionResult;
										if (jsonObject.contains("selected_room_id") && jsonObject["selected_room_id"].is_number_integer())
										{
											selectionResult.selectedRoomID = jsonObject["selected_room_id"].get<int>();
										}
										if (jsonObject.contains("effective_room_id") && jsonObject["effective_room_id"].is_number_integer())
										{
											selectionResult.effectiveRoomID = jsonObject["effective_room_id"].get<int>();
										}
										if (jsonObject.contains("rejected_room_id") && jsonObject["rejected_room_id"].is_number_integer())
										{
											selectionResult.rejectedRoomID = jsonObject["rejected_room_id"].get<int>();
										}
										if (jsonObject.contains("room_selection_error") && jsonObject["room_selection_error"].is_string())
										{
											jsonObject["room_selection_error"].get_to(selectionResult.error);
										}
										if (jsonObject.contains("request_id") && jsonObject["request_id"].is_number_unsigned())
										{
											selectionResult.requestID = jsonObject["request_id"].get<uint64_t>();
										}

                                        GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
                                        if (pRoomsInterface != nullptr)
                                        {
											pRoomsInterface->OnRosterUpdated(std::move(mapMembers), selectionResult);
                                        }
									}
									break;

                                    case EWebSocketMessageID::ANTICHEAT_MESSAGE:
                                    {
                                        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GOT AC MSG FROM WEBSOCKET!");

										WebSocketMessage_AnticheatMessage acMsg;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acMsg);

                                        if (bParsed)
                                        {
                                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AC Msg Signal User: %lld!", acMsg.target_user_id);
                                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AC Msg Signal Payload Size: %d!", (int)acMsg.payload.size());
                                            GenOnlinePluginHost::Anticheat::MessageArrived(acMsg.target_user_id, acMsg.payload.data(), acMsg.payload.size());
                                        }
                                    }
                                    break;

									case EWebSocketMessageID::LOBBY_CURRENT_LOBBY_UPDATE:
									{
										// re-get the room info as it is stale
										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->UpdateRoomDataCache(nullptr);
										}
									}
									break;

									case EWebSocketMessageID::PROBE:
									{
										WebSocketMessage_ServerProbe probe;
                                        bool bParsed = JSONGetAsObject(jsonObject, &probe);

										if (bParsed)
										{
											if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] GOT PROBE REQUEST: %s!", probe.url.c_str());
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] Received request");
											}

											GenOnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY, probe.url);

											// service needs the response
											nlohmann::json j;
											j["msg_id"] = EWebSocketMessageID::PROBE_RESP;
											j["timestamp"] = "0";
											std::string strBody = j.dump();
											Send(strBody.c_str());
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_LOBBY_LIST_UPDATE:
									{
										// re-get the room info as it is stale
										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->SetLobbyListDirty();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_JOIN_PREARRANGED_LOBBY:
									{
										WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby mmEvent;
										bool bParsed = JSONGetAsObject(jsonObject, &mmEvent);

										if (bParsed)
										{
											GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												pLobbyInterface->InvokeMatchmakingMatchFoundCallback();

												// TODO_QUICKMATCH: Only if really in quickmatch

												// TODO_QUICKMATCH: We need to retrieve this info instead
												// basic info needed to join
												LobbyEntry lobbyEntry;
												lobbyEntry.lobbyID = mmEvent.lobby_id;
												lobbyEntry.map_path = "Maps\\Alpine Assault\\Alpine Assault.map";

												if (pLobbyInterface->JoinLobby(lobbyEntry, std::string()))
												{
													pLobbyInterface->InvokeMatchmakingMessageCallback("Joining QuickMatch Lobby");
												}
												else
												{
													pLobbyInterface->InvokeMatchmakingMessageCallback("Could not join the QuickMatch lobby, another join is still in progress");
												}
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby interface is null");
												break;
											}
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_START_GAME:
									{
										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->InvokeMatchmakingStartGameCallback();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_REQUEUE:
									{
										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->ResetForMatchmakingRequeue();
											pLobbyInterface->InvokeMatchmakingRequeueCallback();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_SETUP_PROGRESS:
									{
										static constexpr int kMinMatchSetupTimeoutMs = 1000;
										static constexpr int kMaxMatchSetupTimeoutMs = 120000;

										int timeoutMs = 0;
										if (jsonObject.contains("timeout_ms") && jsonObject["timeout_ms"].is_number_integer())
										{
											timeoutMs = jsonObject["timeout_ms"].get<int>();
											if (timeoutMs > 0)
											{
												if (timeoutMs < kMinMatchSetupTimeoutMs)
												{
													timeoutMs = kMinMatchSetupTimeoutMs;
												}
												else if (timeoutMs > kMaxMatchSetupTimeoutMs)
												{
													timeoutMs = kMaxMatchSetupTimeoutMs;
												}
											}
										}

										// -1 = older service that doesn't say, the menu infers it
										int countdownMs = -1;
										if (jsonObject.contains("countdown_ms") && jsonObject["countdown_ms"].is_number_integer())
										{
											countdownMs = jsonObject["countdown_ms"].get<int>();
										}

										GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
										if (pLobbyInterface != nullptr && timeoutMs > 0)
										{
											pLobbyInterface->InvokeMatchmakingSetupProgressCallback(timeoutMs, countdownMs);
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_MESSAGE:
									{
										WebSocketMessage_MatchmakingMessage matchmakingMsg;
										bool bParsed = JSONGetAsObject(jsonObject, &matchmakingMsg);

										if (bParsed)
										{
											GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												pLobbyInterface->InvokeMatchmakingMessageCallback(matchmakingMsg.message);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_NEW_FRIEND_REQUEST:
									{
										WebSocketMessage_Social_NewFriendRequest incomingNotify;
										bool bParsed = JSONGetAsObject(jsonObject, &incomingNotify);

										if (bParsed)
										{
											GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->InvokeCallback_NewFriendRequest(incomingNotify.display_name);
											}
										}
									}
									break;

									case EWebSocketMessageID::WS_KEEPALIVE:
									{
										std::vector<GOModuleInfo> modules = GetLoadedModules();

										std::vector<std::vector<std::string>> vecResp;

                                        for (auto& m : modules)
										{
											std::vector<std::string> newEntry(2);
											newEntry[0] = m.path;
											newEntry[1] = std::to_string(m.size);
											vecResp.push_back(newEntry);
                                        }

                                        // service needs the response
                                        nlohmann::json j;
                                        j["msg_id"] = EWebSocketMessageID::WS_KEEPALIVE_CLIENT;
                                        j["resp"] = vecResp;
                                        std::string strBody = j.dump();
                                        Send(strBody.c_str());
									}
									break;

									default:
										NetworkLog(ELogVerbosity::LOG_RELEASE, "Unhandled WebSocketMessage: %d", (int)msgID);
										break;
									}
								}
								else
								{
									NetworkLog(ELogVerbosity::LOG_RELEASE, "Malformed WebSocketMessage: couldn't parse as WebSocketMessageBase");
								}
							}
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Malformed WebSocketMessage");
						}
					}
					catch (nlohmann::json::exception& jsonException)
					{
						if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 101: %s (JSON: %s)", bufferThisRecv, jsonException.what());
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Buildup buffer is: %s", m_vecWSPartialBuffer.data());
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Invalid JSON message");
						}

						m_vecWSPartialBuffer.clear();
					}
					catch (std::exception& e)
					{
						if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 100: %s (%s)", bufferThisRecv, e.what());
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Message processing failed");
						}

						m_vecWSPartialBuffer.clear();
					}
					catch (...)
					{
						if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 102: %s", bufferThisRecv);
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Message processing failed");
						}

						m_vecWSPartialBuffer.clear();
					}
				}
			}
			else if (meta->flags & CURLWS_BINARY)
			{
				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket binary");
				// noop
			}
			else if (meta->flags & CURLWS_CLOSE)
			{
				// TODO_NGMP: Dont do this during gameplay, they can play without the WS, just 'queue' it for when they get back to the front end

				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket close");
				if (m_bRealtimeHub)
				{
					HubTransportLost("websocket closed");
					return ret;
				}
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
				m_bConnected = false;
				m_vecWSPartialBuffer.clear();
				// TODO_NGMP: Handle this
			}
			else if (meta->flags & CURLWS_PING)
			{
				// TODO_NGMP: Handle this
			}
			else if (meta->flags & CURLWS_OFFSET)
			{
				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket offset");
				// noop
			}
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "websocket meta was null");
		}
	}
	else if (ret != CURLE_AGAIN)
	{

		NetworkLog(ELogVerbosity::LOG_RELEASE, "Got websocket disconnect (ERROR: %s), Attempting reconnect", curl_easy_strerror(ret));

		if (m_bRealtimeHub)
		{
			HubTransportLost("receive failed");
		}
		else
		{
			m_bConnected = false;
			BeginReconnect();
			m_vecWSPartialBuffer.clear();
		}


		// send event to sentry
#if defined(GENONLINE_USE_SENTRY)
        if (TheGenOnlineGame != nullptr)
        {
			AsciiString sentryMsg;
            sentryMsg.format("Got websocket disconnect (ERROR: %s), Attempting reconnect", curl_easy_strerror(ret));
            sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "WEBSOCKET_DISCONNECT_ERROR", sentryMsg.str()));
        }
#endif
	}

	return ret;
}

GenOnlineRoomsInterface::GenOnlineRoomsInterface()
{

}

void GenOnlineRoomsInterface::GetRoomList(std::function<void(bool)> cb)
{
	m_vecRooms.clear();
	m_CurrentRoomIndex = -1;
	m_EffectiveRoomID.reset();
	m_PendingRoomChange.reset();
	m_bRoomSelectionResultsSupported = false;
	m_bSupportsModerationCommands = false;

	// Cache our buddies on lobby list
	GenOnlineSocialInterface* pSocialInterface =
		GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->GetFriendsList(false, nullptr);
	}

	// The hub sent the rooms with Ready.
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS != nullptr && pWS->IsRealtimeHub())
	{
		m_vecRooms = m_vecRealtimeRooms;
		m_bRoomSelectionResultsSupported = true;
		cb(!m_vecRooms.empty());
		return;
	}

	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Rooms");
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			if (!bSuccess || statusCode != 200)
			{
				cb(false);
				return;
			}

			try
			{
				std::vector<NetworkRoom> rooms;
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);
				const bool roomSelectionResultsSupported = jsonObject.value("supports_room_selection_results", false);
				const bool supportsModerationCommands = jsonObject.value("supports_moderation_commands", false);

				for (const auto& roomEntryIter : jsonObject["rooms"])
				{
					int id = 0;
					std::string strName;
					ERoomFlags flags = ERoomFlags::ROOM_FLAGS_DEFAULT;
					int parentRoomID = -1;

					roomEntryIter["id"].get_to(id);
					roomEntryIter["name"].get_to(strName);
					if (roomEntryIter.contains("flags") && !roomEntryIter["flags"].is_null())
					{
						roomEntryIter["flags"].get_to(flags);
					}
					if (roomEntryIter.contains("parent_id") && !roomEntryIter["parent_id"].is_null())
					{
						roomEntryIter["parent_id"].get_to(parentRoomID);
					}
					rooms.emplace_back(id, strName, flags, parentRoomID);
				}

				m_vecRooms = std::move(rooms);
				m_bRoomSelectionResultsSupported = roomSelectionResultsSupported;
				m_bSupportsModerationCommands = supportsModerationCommands;

				cb(true);
				return;
			}
			catch (const std::exception& exception)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to parse room list: %s", exception.what());
			}

			cb(false);
			return;
		});
}

void GenOnlineRoomsInterface::JoinRoom(int roomIndex)
{
	const std::vector<NetworkRoom>& rooms = GetGroupRooms();
	if (roomIndex < 0 || roomIndex >= (int)rooms.size())
	{
		ReportRoomJoinFailure(std::format("Invalid room index {}.", roomIndex));
		return;
	}

	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS == nullptr)
	{
		ReportRoomJoinFailure("The room service is not connected.");
		return;
	}

	if (m_PendingRoomChange.has_value())
	{
		ReportRoomJoinFailure("Another room change is already in progress.");
		return;
	}

	const uint64_t requestID = m_NextRoomChangeRequestID++;
	m_PendingRoomChange = PendingRoomChange{
		roomIndex,
		rooms[roomIndex].GetRoomID(),
		requestID,
		std::chrono::steady_clock::now() + std::chrono::seconds(10)
	};
	pWS->SendData_JoinNetworkRoom(rooms[roomIndex].GetRoomID(), requestID);
}

std::unordered_map<uint64_t, NetworkRoomMember>& GenOnlineRoomsInterface::GetMembersListForCurrentRoom()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Repopulating network room roster using local data");
	return m_mapMembers;
}

void GenOnlineRoomsInterface::SendChatMessageToCurrentRoom(UnicodeString& strChatMsgUnicode, bool bIsAction)
{
	std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();;
	if (pWS != nullptr)
	{
		pWS->SendData_RoomChatMessage(strChatMsgUnicode, bIsAction);
	}
}

void GenOnlineRoomsInterface::OnRosterUpdated(std::unordered_map<uint64_t, NetworkRoomMember> mapMembers,
	const RoomSelectionResult& selectionResult)
{
	m_mapMembers = std::move(mapMembers);

	int changedRoomIndex = -1;
	bool effectiveRoomChanged = true;
	bool refreshRoster = true;
	std::string roomJoinFailure;
	if (m_PendingRoomChange.has_value())
	{
		const PendingRoomChange& pendingRoomChange = *m_PendingRoomChange;
		const bool requestMatches = !selectionResult.requestID.has_value()
			|| pendingRoomChange.requestID == *selectionResult.requestID;
		if (requestMatches && selectionResult.rejectedRoomID == pendingRoomChange.roomID)
		{
			roomJoinFailure = selectionResult.error.empty() ? "The room selection was rejected." : selectionResult.error;
			m_PendingRoomChange.reset();
		}
		else
		{
			const bool selectionMatches = requestMatches
				&& (selectionResult.selectedRoomID.has_value()
					? pendingRoomChange.roomID == *selectionResult.selectedRoomID
					: !m_bRoomSelectionResultsSupported);
			if (selectionMatches)
			{
				if (selectionResult.effectiveRoomID.has_value())
				{
					effectiveRoomChanged = !m_EffectiveRoomID.has_value()
						|| *m_EffectiveRoomID != *selectionResult.effectiveRoomID;
					m_EffectiveRoomID = selectionResult.effectiveRoomID;
					refreshRoster = effectiveRoomChanged;
				}
				m_CurrentRoomIndex = pendingRoomChange.roomIndex;
				changedRoomIndex = m_CurrentRoomIndex;
				m_PendingRoomChange.reset();
			}
		}
	}

	if (changedRoomIndex >= 0 && m_RoomChangedCallback != nullptr)
	{
		m_RoomChangedCallback(changedRoomIndex, effectiveRoomChanged);
	}
	if (!roomJoinFailure.empty())
	{
		ReportRoomJoinFailure(roomJoinFailure);
	}

	std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
	if (refreshRoster && m_RosterNeedsRefreshCallback != nullptr)
	{
		m_RosterNeedsRefreshCallback();
	}
}

void GenOnlineRoomsInterface::Tick()
{
	if (m_PendingRoomChange.has_value()
		&& std::chrono::steady_clock::now() >= m_PendingRoomChange->deadline)
	{
		m_PendingRoomChange.reset();
		ReportRoomJoinFailure("The room change timed out. Please try again.");
	}
}

void GenOnlineRoomsInterface::ReportRoomJoinFailure(const std::string& error)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Room change failed: %s", error.c_str());
	if (m_OnChatCallback != nullptr)
	{
		UnicodeString message;
		message = L"Couldn't join that room. Please try again.";
		m_OnChatCallback(message, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)));
	}
}

// Realtime protocol v1 rooms /////////////////////////////////////////////////////

static NetworkRoomMember ToRoomMember(const GenOnlineRealtime::Member& member)
{
	NetworkRoomMember roomMember;
	roomMember.user_id = member.id;
	roomMember.display_name = member.name;
	// A badge lists the member with the staff, the local user counts as staff with a moderation permission.
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	int badgeColor = 0;
	roomMember.m_bIsAdmin = pAuth != nullptr && pAuth->GetUserID() == member.id
		? GenOnlineRealtimeBridge::HasModerationPermission()
		: GenOnlineRealtimeBridge::BadgeColor(member.badges, badgeColor);
	return roomMember;
}

void GenOnlineRoomsInterface::SetRealtimeRooms(const std::vector<GenOnlineRealtime::RoomInfo>& rooms)
{
	m_vecRealtimeRooms.clear();
	for (const GenOnlineRealtime::RoomInfo& room : rooms)
	{
		const int parentRoomID = room.parent_id.isNull || room.parent_id.value <= 0 ? -1 : static_cast<int>(room.parent_id.value);
		m_vecRealtimeRooms.emplace_back(static_cast<int>(room.id), room.name,
			room.show_all_matches ? ERoomFlags::ROOM_FLAGS_SHOW_ALL_MATCHES : ERoomFlags::ROOM_FLAGS_DEFAULT, parentRoomID);
	}
}

void GenOnlineRoomsInterface::RefreshRoster()
{
	std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
	if (m_RosterNeedsRefreshCallback != nullptr)
	{
		m_RosterNeedsRefreshCallback();
	}
}

// The full member list after a join and after every reconnect. It also tells which room the session is in.
void GenOnlineRoomsInterface::OnRealtimeRoomMembers(const GenOnlineRealtime::RoomMembersEvent& event)
{
	std::unordered_map<uint64_t, NetworkRoomMember> members;
	for (const GenOnlineRealtime::Member& member : event.members)
	{
		members.emplace(member.id, ToRoomMember(member));
	}
	m_mapMembers = std::move(members);

	int roomIndex = -1;
	for (size_t i = 0; i < m_vecRooms.size(); ++i)
	{
		if (m_vecRooms[i].GetRoomID() == event.room_id)
		{
			roomIndex = static_cast<int>(i);
			break;
		}
	}

	const bool effectiveRoomChanged = !m_EffectiveRoomID.has_value() || *m_EffectiveRoomID != event.room_id;
	m_EffectiveRoomID = static_cast<int>(event.room_id);
	if (m_PendingRoomChange.has_value() && m_PendingRoomChange->roomID == event.room_id)
	{
		m_PendingRoomChange.reset();
	}
	if (roomIndex >= 0 && (effectiveRoomChanged || roomIndex != m_CurrentRoomIndex))
	{
		m_CurrentRoomIndex = roomIndex;
		if (m_RoomChangedCallback != nullptr)
		{
			m_RoomChangedCallback(roomIndex, effectiveRoomChanged);
		}
	}
	RefreshRoster();
}

void GenOnlineRoomsInterface::OnRealtimeMemberAdd(const GenOnlineRealtime::MemberAddEvent& event)
{
	if (!m_EffectiveRoomID.has_value() || event.channel_id != "room:" + std::to_string(*m_EffectiveRoomID))
	{
		return;
	}
	m_mapMembers[event.member.id] = ToRoomMember(event.member);
	RefreshRoster();
}

void GenOnlineRoomsInterface::OnRealtimeMemberRemove(const GenOnlineRealtime::MemberRemoveEvent& event)
{
	if (!m_EffectiveRoomID.has_value() || event.channel_id != "room:" + std::to_string(*m_EffectiveRoomID))
	{
		return;
	}
	if (m_mapMembers.erase(event.user_id) > 0)
	{
		RefreshRoster();
	}
}

void GenOnlineRoomsInterface::OnRealtimeMemberUpdate(const GenOnlineRealtime::Member& member)
{
	auto it = m_mapMembers.find(member.id);
	if (it == m_mapMembers.end())
	{
		return;
	}
	it->second = ToRoomMember(member);
	RefreshRoster();
}

void GenOnlineRoomsInterface::OnRealtimeRoomMessage(const GenOnlineRealtime::ChatMessage& message)
{
	if (!m_EffectiveRoomID.has_value() || message.channel_id != "room:" + std::to_string(*m_EffectiveRoomID) || m_OnChatCallback == nullptr)
	{
		return;
	}
	const bool bAction = message.type == "action";
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	UnicodeString unicodeStr;
	unicodeStr.format(bAction ? L"[%2.2d:%2.2d] %s %s" : L"[%2.2d:%2.2d] %s: %s", systemTime.wHour, systemTime.wMinute,
		from_utf8(message.author.name).c_str(), from_utf8(message.content).c_str());
	// An action keeps the emote colour, otherwise the first badge of the author colours the line.
	Color color = DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_NETWORK_ROOM, true, bAction, false, false);
	int badgeColor = 0;
	if (!bAction && GenOnlineRealtimeBridge::BadgeColor(message.author.badges, badgeColor))
	{
		color = badgeColor;
	}
#if RTS_ZEROHOUR
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const bool bLocal = pAuth != nullptr && pAuth->GetUserID() == message.author.id;
	StatsExporterLiveChatOnline(STATS_EXPORTER_CHAT_ROOM, message.author.id, message.author.name.c_str(), message.content.c_str(), bAction, FALSE, bLocal);
#endif
	m_OnChatCallback(unicodeStr, color);
}

void GenOnlineRoomsInterface::OnRealtimeNameChanged(const std::string& strOldName, const std::string& strNewName)
{
	if (m_OnChatCallback == nullptr)
	{
		return;
	}
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	UnicodeString unicodeStr;
	unicodeStr.format(L"[%2.2d:%2.2d] %s is now known as %s", systemTime.wHour, systemTime.wMinute, from_utf8(strOldName).c_str(), from_utf8(strNewName).c_str());
	m_OnChatCallback(unicodeStr, DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_NETWORK_ROOM, true, false, false, true));
}

void GenOnlineRoomsInterface::OnRealtimeRoomJoinFailed(int roomID, const std::wstring& strReason)
{
	if (m_PendingRoomChange.has_value() && m_PendingRoomChange->roomID == roomID)
	{
		m_PendingRoomChange.reset();
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Joining room %d failed", roomID);
	if (m_OnChatCallback != nullptr)
	{
		m_OnChatCallback(UnicodeString(strReason.c_str()), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)));
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyMessage(const GenOnlineRealtime::ChatMessage& message)
{
	if (!IsInLobby() || message.channel_id != "lobby:" + std::to_string(m_CurrentLobby.lobbyID) || m_OnChatCallback == nullptr)
	{
		return;
	}

	const bool bAction = message.type == "action";
	const bool bAnnouncement = message.type == "announcement";
	int lobbySlot = -1;
	for (const LobbyMemberEntry& member : m_CurrentLobby.members)
	{
		if (member.user_id == message.author.id)
		{
			lobbySlot = member.m_SlotIndex;
			break;
		}
	}

	UnicodeString unicodeStr;
	if (bAnnouncement)
	{
		unicodeStr = UnicodeString(from_utf8(message.content).c_str());
	}
	else
	{
		unicodeStr.format(bAction ? L"%s %s" : L"%s: %s", from_utf8(message.author.name).c_str(), from_utf8(message.content).c_str());
	}

	// Announcements keep the neutral notice colour, player chat keeps the slot colour.
	Color color = bAnnouncement
		? DetermineSystemNoticeColor(false, false)
		: DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_LOBBY, true, bAction, false, false, lobbySlot);

#if RTS_ZEROHOUR
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const bool bIsLocal = pAuthInterface != nullptr && pAuthInterface->GetUserID() == message.author.id;
	StatsExporterLiveChatOnline(STATS_EXPORTER_CHAT_GAME_LOBBY, message.author.id, message.author.name.c_str(), message.content.c_str(), bAction, bAnnouncement, bIsLocal);
#endif
	m_OnChatCallback(unicodeStr, color);
}

void GenOnlineLobbyInterface::ShowRealtimeLobbyFailure(const GenOnlineRealtime::InvokeOutcome& outcome)
{
	if (m_OnChatCallback == nullptr)
	{
		return;
	}
	m_OnChatCallback(UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str()), DetermineSystemNoticeColor(false, true));
}
