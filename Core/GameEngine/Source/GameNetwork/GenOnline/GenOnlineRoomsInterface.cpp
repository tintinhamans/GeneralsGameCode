#include "GameNetwork/GenOnline/ProxyLogin.h"
#include <random>
#include "GameNetwork/GenOnline/ServiceEndpointPolicy.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "Common/LocalApiLive.h"
#include "GameNetwork/GenOnline/NetworkPacket.h"
#include "GameNetwork/GenOnline/NetworkBitstream.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/JsonHelpers.h"
#include "GameNetwork/PeerTransport/PeerRouteLabel.h"
#include "../GenOnlineInit.h"
#include "../HTTP/HTTPManager.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "GameClient/Shell.h"

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

WebSocket::WebSocket()
	: m_hubReconnectPolicy(RealtimeReconnectRandom)
{
	m_pMulti = curl_multi_init();
	m_pHeaders = nullptr;
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

void WebSocket::StartPeerSignalling(int64_t lobbyID, int64_t userID, uint16_t preferredPort, const std::string& middlewareID, const GenOnlineRealtime::ConnectionStep& step)
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

	// TODO_GENONLINE: Better location for this
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
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Starting signalling with %lld (MWID: %s), attempt %d, transport %s, relay %d, timeout %d s",
		userID, middlewareID.c_str(), step.attempt, step.transport.c_str(), step.relay ? 1 : 0, step.timeoutS);
	pMesh->StartConnectionSignalling(middlewareID.c_str(), userID, preferredPort, step);
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
			// A step with an attempt below 1 or a transport of the wrong syntax makes the event malformed.
			const ConnectionStep step = ConnectionStepOf(event);
			if (!step.IsValid())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] dropped malformed StartSignalling event: attempt %d, transport of %zu chars", event.attempt, event.transport.size());
				return;
			}
			StartPeerSignalling(event.lobby_id, event.user_id, (uint16_t)event.preferred_port, event.middleware_id, step);
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

			NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
			if (pMesh != nullptr)
			{
				pMesh->ClearHeldACMessages(event.user_id);
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


// The hub address must be a wss URL on the origin of the current service route, or a ws URL for a loopback http route.
bool WebSocket::IsDestinationTrusted(const std::string& url) const
{
	return !GenOnlineRealtime::AcceptRealtimeUrl(url, GenOnlineServicesManager::GetAPIEndpoint("")).empty();
}

int64_t WebSocket::HubNowMs()
{
	return NowMs();
}

// Drops the socket without reconnecting.
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
	m_bConnected = false;
}

// The hub session reports the loss and decides how to continue. A loss outside a session reconnects.
void WebSocket::HubTransportLost(const char* reason)
{
	m_bConnected = false;
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
	m_upgradeStandby.Reset();
	m_bHubSessionRecoveryTried = false;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Ready");
	// A new session has no transport list yet, so a failed send repeats on the next tick.
	m_lastPeerTransports.reset();
	SendPeerTransports(true);
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

void WebSocket::SendPeerTransports(bool bForce)
{
	// /net transport gns offers no plugin transport. A change of the setting shows as a changed list and sends it on the next tick.
	extern int g_transportOverride;
	std::vector<std::string> available;
	const std::string pluginTransport = GenOnlinePluginHost::Transport::GetName();
	if (!pluginTransport.empty())
	{
		available.push_back(pluginTransport);
	}
	const std::vector<std::string> offered = FilterOfferedTransports(available, g_transportOverride);
	const std::string transport = offered.empty() ? std::string() : offered.front();
	if (!bForce && m_lastPeerTransports.has_value() && *m_lastPeerTransports == transport)
	{
		return;
	}
	if (m_pHubSession == nullptr || m_pHubSession->GetState() != GenOnlineRealtime::HubSession::State::Ready)
	{
		return;
	}

	GenOnlineRealtime::SetPeerTransportsRequest request;
	if (!transport.empty())
	{
		request.transports.push_back(transport);
	}
	if (FireHub(request))
	{
		m_lastPeerTransports = transport;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] SetPeerTransports [%s]", transport.c_str());
	}
}

// Refreshes the tokens and registers the session again, then reconnects. A second refusal tears down.
void WebSocket::RecoverHubSession(const GenOnlineRealtime::SessionRecovery& recovery)
{
	m_bHubSessionRecoveryTried = true;
	EndReconnect();
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth == nullptr)
	{
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
		return;
	}
	// The service closes with auth.revoked after a logout of this client, which already tears the session down.
	if (!pAuth->IsLoggedIn())
	{
		return;
	}
	pAuth->RecoverRealtimeSession(recovery.refreshToken, recovery.registerSession, [this](bool bRecovered)
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
	// Only standby refusals in a row count toward the standby window.
	if (action != GenOnlineRealtime::DisconnectAction::ReconnectAfterStandby)
	{
		m_upgradeStandby.Reset();
	}
	switch (action)
	{
	case GenOnlineRealtime::DisconnectAction::Reconnect:
		BeginReconnect();
		break;
	case GenOnlineRealtime::DisconnectAction::ReconnectAfterKick:
		BeginReconnect();
		m_nextReconnectAttempt = NowMs() + GenOnlineRealtime::KICK_RECONNECT_DELAY_MS;
		break;
	case GenOnlineRealtime::DisconnectAction::ReconnectAfterSwitch:
	{
		const int64_t delayMs = GenOnlineRealtime::SwitchReconnectDelayMs(m_pHubSession->ServerSwitchingRetryAfterMs(), RealtimeReconnectRandom());
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Server switching, reconnecting in %lld ms", static_cast<long long>(delayMs));
		BeginSwitchReconnect(delayMs);
		break;
	}
	case GenOnlineRealtime::DisconnectAction::ReconnectAfterStandby:
	{
		const int64_t delayMs = GenOnlineRealtime::StandbyRefusalDelayMs(m_upgradeStandby, m_pHubSession->ServerSwitchingRetryAfterMs(), NowMs());
		if (delayMs < 0)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Standby service refusals for %lld ms, reconnecting with backoff", static_cast<long long>(GenOnlineRealtime::STANDBY_RETRY_WINDOW_MS));
			BeginReconnect();
			break;
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Refused by a standby service, connecting again in %lld ms", static_cast<long long>(delayMs));
		BeginStandbyReconnect(delayMs);
		break;
	}
	case GenOnlineRealtime::DisconnectAction::RefreshAndReconnect:
	case GenOnlineRealtime::DisconnectAction::RefreshTokenAndReconnect:
	case GenOnlineRealtime::DisconnectAction::RegisterAndReconnect:
		if (m_bHubSessionRecoveryTried)
		{
			EndReconnect();
			GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
			break;
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Connection closed with %s, renewing the session", reason.c_str());
		RecoverHubSession(GenOnlineRealtime::RecoveryForAction(action));
		break;
	case GenOnlineRealtime::DisconnectAction::StopSignedInElsewhere:
	case GenOnlineRealtime::DisconnectAction::ReturnToLogin:
	default:
		EndReconnect();
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(reason == "moderation.banned" ? EGOTearDownReason::MODERATION_BAN : EGOTearDownReason::LOST_CONNECTION);
		break;
	}
}

// Lobby methods that act on the lobby the client is in. Their parked calls need the same lobby.
static bool IsInLobbyHubMethod(const std::string& method)
{
	static const char* const METHODS[] = { "UpdateLobby", "SetLobbyPassword", "UpdateMySlot", "SetHasMap", "SetReady", "SetSlotState",
		"UpdateSlots", "KickFromLobby", "BanFromLobby", "ForceReady", "StartGame", "BeginMeshCheck" };
	for (const char* lobbyMethod : METHODS)
	{
		if (method == lobbyMethod)
		{
			return true;
		}
	}
	return false;
}

static int64_t HubCallUserID()
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	return pAuth != nullptr && pAuth->IsLoggedIn() ? pAuth->GetUserID() : -1;
}

std::function<bool()> WebSocket::HubCallWanted(const std::string& method, const nlohmann::json& request)
{
	enum class Scope
	{
		Session,
		InLobby,
		JoinOrCreate,
		Leave,
	};
	Scope scope = Scope::Session;
	if (IsInLobbyHubMethod(method))
	{
		scope = Scope::InLobby;
	}
	else if (method == GenOnlineRealtime::JoinLobbyRequest::METHOD || method == GenOnlineRealtime::CreateLobbyRequest::METHOD)
	{
		scope = Scope::JoinOrCreate;
	}
	else if (method == GenOnlineRealtime::LeaveLobbyRequest::METHOD)
	{
		scope = Scope::Leave;
	}

	const int64_t userID = HubCallUserID();
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	const int64_t lobbyID = pLobby != nullptr ? pLobby->GetCurrentLobby().lobbyID : -1;
	const uint64_t joinGeneration = pLobby != nullptr ? pLobby->GetLobbyJoinGeneration() : 0;
	int64_t targetLobbyID = -1;
	if (method == GenOnlineRealtime::JoinLobbyRequest::METHOD && request.is_object())
	{
		auto it = request.find("lobby_id");
		if (it != request.end() && it->is_number_integer())
		{
			targetLobbyID = it->get<int64_t>();
		}
	}

	return [scope, userID, lobbyID, joinGeneration, targetLobbyID]()
	{
		if (userID < 0 || HubCallUserID() != userID)
		{
			return false;
		}
		if (scope == Scope::Session)
		{
			return true;
		}
		GenOnlineLobbyInterface* pLobbyNow = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (pLobbyNow == nullptr)
		{
			return false;
		}
		const int64_t lobbyNow = pLobbyNow->GetCurrentLobby().lobbyID;
		switch (scope)
		{
		case Scope::InLobby:
			return lobbyID != -1 && lobbyNow == lobbyID;
		case Scope::JoinOrCreate:
			return pLobbyNow->GetLobbyJoinGeneration() == joinGeneration && (lobbyNow == -1 || (targetLobbyID != -1 && lobbyNow == targetLobbyID));
		case Scope::Leave:
			// LeaveLobby leaves the lobby the session is in, so any later join or teardown makes it stale.
			return pLobbyNow->GetLobbyJoinGeneration() == joinGeneration && (lobbyNow == -1 || lobbyNow == lobbyID);
		default:
			return true;
		}
	};
}

void WebSocket::SendHubCall(const GenOnlineRealtime::ParkedHubCall& call)
{
	if (m_pHubSession == nullptr)
	{
		if (call.callback)
		{
			GenOnlineRealtime::InvokeOutcome outcome;
			outcome.transportError = "not connected";
			call.callback(outcome);
		}
		return;
	}
	// The hub session lives inside this socket, so every completion runs while the socket exists.
	m_pHubSession->InvokeMethod(call.method, call.request, [this, call](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			const bool bStandby = GenOnlineRealtime::IsStandbyResult(outcome);
			if (m_standbyHubCalls.Complete(call, outcome, HubNowMs()))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] %s answered standby, sends again once the session is ready", call.method.c_str());
			}
			else if (bStandby)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] %s answered standby for 10 seconds, the call fails", call.method.c_str());
			}
		}, HubNowMs());
}

void WebSocket::TickStandbyHubCalls()
{
	if (m_standbyHubCalls.Size() == 0)
	{
		return;
	}
	const bool bReady = m_pHubSession != nullptr && m_pHubSession->GetState() == GenOnlineRealtime::HubSession::State::Ready;
	std::vector<GenOnlineRealtime::ParkedHubCall> due = m_standbyHubCalls.TakeDue(HubNowMs(), bReady, [](const GenOnlineRealtime::ParkedHubCall& call)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] %s dropped while it waited after a standby answer", call.method.c_str());
		});
	for (const GenOnlineRealtime::ParkedHubCall& call : due)
	{
		SendHubCall(call);
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

void WebSocket::Connect(const char* url, std::function<void(void)> fnWebsocketConnectedCallback)
{
	if (m_bConnected || m_bShuttingDown)
		return;
	// Copy before replacing the stored address: reconnect passes its c_str().
	const std::string destination(url);
	m_strWebsocketAddr = destination;
	m_fnWebsocketConnectedCallback = fnWebsocketConnectedCallback;
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
	GenOnlineRoomsInterface* pRooms = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRooms == nullptr || !pRooms->GetEffectiveRoomID().has_value())
	{
		return;
	}
	GenOnlineRealtime::SendMessageRequest request;
	request.channel_id = "channel:" + std::to_string(*pRooms->GetEffectiveRoomID());
	request.type = bIsAction ? "action" : "chat";
	request.content = to_utf8(msg.str());
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			if (!outcome.Succeeded())
			{
				GenOnlineRealtimeBridge::ShowRoomNotice(GenOnlineRealtimeBridge::FailureText(outcome), true);
			}
		});
}

void WebSocket::SendData_MarkReady(bool bReady)
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
}


void WebSocket::SendData_JoinNetworkRoom(int roomID)
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
		InvokeHub(GenOnlineRealtime::LeaveChannelRequest(), nullptr);
		return;
	}
	// The members arrive as ChannelMembers, which completes the room change.
	GenOnlineRealtime::JoinChannelRequest request;
	request.channel_id = roomID;
	InvokeHub(request, onOutcome);
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
	m_bConnected = false;
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
			remaining, &sent, 0, CURLWS_TEXT);
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

//static std::string strSignal = "str:1 ";

// Idempotent: repeated drop signals must not reset the backoff.
void WebSocket::BeginReconnect()
{
	if (m_bReconnecting)
	{
		return;
	}

	StartReconnect(ComputeReconnectDelay(), false);
}

// The handover reconnect skips the backoff once. A reconnect already in progress keeps its own delays.
void WebSocket::BeginSwitchReconnect(int64_t delayMs)
{
	if (m_bReconnecting)
	{
		return;
	}

	StartReconnect(delayMs, true);
}

// The reconnect after a standby refusal neither advances the backoff nor shows the reconnect screen.
void WebSocket::BeginStandbyReconnect(int64_t delayMs)
{
	if (m_bReconnecting)
	{
		return;
	}

	StartReconnect(delayMs, false);
	m_bStandbyReconnect = true;
}

void WebSocket::StartReconnect(int64_t delayMs, bool bSwitch)
{
	ClearOutbound();
	m_bReconnecting = true;
	m_bSwitchReconnect = bSwitch;
	m_bStandbyReconnect = false;
	m_numReconnectAttempts = 0;
	m_reconnectAttemptStarted = -1;
	m_reconnectStartTime = NowMs();
	m_switchClosedAt = bSwitch ? m_reconnectStartTime : -1;
	m_nextReconnectAttempt = m_reconnectStartTime + delayMs;
}

void WebSocket::EndReconnect()
{
	m_bReconnecting = false;
	m_bSwitchReconnect = false;
	m_bStandbyReconnect = false;
	m_switchClosedAt = -1;
	m_reconnectAttemptStarted = -1;
}

int64_t WebSocket::ComputeReconnectDelay() const
{
	return GenOnlineRealtime::SwitchWindowDelayMs(m_hubReconnectPolicy.NextDelayMs(), m_switchClosedAt, NowMs());
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

	// A handover reconnect that did not connect within 3 seconds of the close becomes a normal reconnect.
	if (m_bSwitchReconnect && !GenOnlineRealtime::SwitchReconnectHidden(m_reconnectStartTime, currTime))
	{
		m_bSwitchReconnect = false;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Server switch reconnect still pending after %lld ms, continuing as a normal reconnect", static_cast<long long>(currTime - m_reconnectStartTime));
	}

	// Connect() kills the previous handle: leave a pending attempt alone unless stuck.
	if (m_reconnectAttemptStarted != -1 && (currTime - m_reconnectAttemptStarted) < m_reconnectAttemptTimeout)
	{
		return;
	}

	// The hub keeps reconnecting for as long as the user stays logged in.
	if (currTime < m_nextReconnectAttempt)
	{
		return;
	}

	++m_numReconnectAttempts;
	m_reconnectAttemptStarted = currTime;
	// keeps the login callback in case the first connection never reached Ready
	Connect(m_strWebsocketAddr.c_str(), m_fnWebsocketConnectedCallback);
}

void WebSocket::Tick()
{
	if (m_bShuttingDown || m_pMulti == nullptr)
	{
		return;
	}
	// Main thread only, m_pCurlWS/m_bConnected are unlocked here.
	UpdateReconnect();
	PollConnectionSetup();
	SendPeerTransports(false);
	TickStandbyHubCalls();

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

                        // The service refuses with close codes after the handshake. Behind the tunnel every failed upgrade
                        // arrives as a 502 whatever the cause, so any status is a lost connection that backs off.
                        m_upgradeStandby.Reset();
                        if (m_bReconnecting)
                        {
                            m_bStandbyReconnect = false;
                            m_reconnectAttemptStarted = -1;
                            m_nextReconnectAttempt = NowMs() + ComputeReconnectDelay();
                        }
                        else // give up immediately
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (initial connect)");
                            GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
                            m_bConnected = false;
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

                        EndReconnect();

                        // the login completes on Ready, see OnHubReady
                        m_pHubSession->OnTransportOpen(NowMs());
                    }
                }
            }
        }
    }

    if (!m_bConnected)
    {
        return;
    }

	m_pHubSession->Tick(currTime);
	if (!m_bConnected)
	{
		return;
	}

	const CURLcode sendResult = FlushOutbound();
	if (sendResult != CURLE_OK && sendResult != CURLE_AGAIN)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Send failed: %s, attempting reconnect", curl_easy_strerror(sendResult));
		HubTransportLost("send failed");
		return;
	}

	// drain what the server queued, a signalling burst should not take one frame per tick
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

		// what type of message?
		if (meta != nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket flags: %d", meta->flags);
			if (meta->flags & CURLWS_PONG) // PONG
			{

			}
			else if (meta->flags & CURLWS_TEXT)
			{
				m_pHubSession->OnTransportText(bufferThisRecv, rlen, NowMs());
			}
			else if (meta->flags & CURLWS_BINARY)
			{
				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket binary");
				// noop
			}
			else if (meta->flags & CURLWS_CLOSE)
			{
				// TODO_GENONLINE: Dont do this during gameplay, they can play without the WS, just 'queue' it for when they get back to the front end

				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket close");
				HubTransportLost("websocket closed");
				return ret;
			}
			else if (meta->flags & CURLWS_PING)
			{
				// TODO_GENONLINE: Handle this
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

		HubTransportLost("receive failed");


		// send event to sentry
#if defined(GENONLINE_USE_SENTRY)
        if (TheGenOnlineGame != nullptr && GenOnlineServicesManager::IsSentryActive())
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
	m_ChannelChat.Clear();

	// Cache our buddies on lobby list
	GenOnlineSocialInterface* pSocialInterface =
		GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->GetFriendsList(false, nullptr);
	}

	// The hub sent the rooms with Ready.
	m_vecRooms = m_vecRealtimeRooms;
	cb(!m_vecRooms.empty());
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
	pWS->SendData_JoinNetworkRoom(rooms[roomIndex].GetRoomID());
}

std::unordered_map<uint64_t, NetworkRoomMember>& GenOnlineRoomsInterface::GetMembersListForCurrentRoom()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Repopulating network room roster using local data");
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

void GenOnlineRoomsInterface::Tick()
{
	if (m_PendingRoomChange.has_value()
		&& std::chrono::steady_clock::now() >= m_PendingRoomChange->deadline)
	{
		m_PendingRoomChange.reset();
		ReportRoomJoinFailure("The room change timed out. Please try again.");
	}
	// Live messages stop waiting for a ChannelHistory that does not come.
	if (m_ChannelChat.IsAwaitingHistory() && std::chrono::steady_clock::now() >= m_ChannelHistoryDeadline)
	{
		for (const GenOnlineRealtime::ChannelChatLine& line : m_ChannelChat.ReleaseWaiting())
		{
			ShowRoomChatLine(line);
		}
	}
}

void GenOnlineRoomsInterface::ReportRoomJoinFailure(const std::string& error)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Room change failed: %s", error.c_str());
	if (m_OnChatCallback != nullptr)
	{
		UnicodeString message;
		message = L"Couldn't join that room. Please try again.";
		m_OnChatCallback(message, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)));
	}
}


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

bool GenOnlineMentionsLocalUser(const GenOnlineRealtime::ChatMessage& message)
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	return pAuth != nullptr && GenOnlineRealtime::MentionsUser(message, pAuth->GetUserID());
}

void GenOnlinePlayMentionSound()
{
	AudioEventRTS mentionAudio("GUIMessageReceived");
	if (TheAudio != nullptr)
	{
		TheAudio->addAudioEvent(&mentionAudio);
	}
}

void GenOnlineRoomsInterface::SetRealtimeRooms(const std::vector<GenOnlineRealtime::ChannelInfo>& rooms)
{
	m_vecRealtimeChannels = rooms;
	m_vecRealtimeRooms.clear();
	for (const GenOnlineRealtime::ChannelInfo& room : rooms)
	{
		// Channel ids start at 0, so a parent_id of 0 names a real parent room.
		const int parentRoomID = room.parent_id.isNull || room.parent_id.value < 0 ? -1 : static_cast<int>(room.parent_id.value);
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
void GenOnlineRoomsInterface::OnRealtimeChannelMembers(const GenOnlineRealtime::ChannelMembersEvent& event)
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
		if (m_vecRooms[i].GetRoomID() == event.channel_id)
		{
			roomIndex = static_cast<int>(i);
			break;
		}
	}

	const bool effectiveRoomChanged = !m_EffectiveRoomID.has_value() || *m_EffectiveRoomID != event.channel_id;
	m_EffectiveRoomID = static_cast<int>(event.channel_id);
	// ChannelHistory follows for the channel actually joined, which a parent room resolves to.
	m_ChannelChat.Start("channel:" + std::to_string(event.channel_id));
	m_ChannelHistoryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	// Joining a parent room lands in its default child room, which completes the change too.
	// A parent room stays the selected room while its chat runs in the default child room.
	int selectedRoomIndex = roomIndex;
	if (m_PendingRoomChange.has_value() && (m_PendingRoomChange->roomID == event.channel_id
		|| (roomIndex >= 0 && m_vecRooms[roomIndex].GetParentRoomID() == m_PendingRoomChange->roomID)))
	{
		selectedRoomIndex = m_PendingRoomChange->roomIndex;
		m_PendingRoomChange.reset();
	}
	else if (roomIndex >= 0 && m_CurrentRoomIndex >= 0 && m_CurrentRoomIndex < (int)m_vecRooms.size()
		&& m_vecRooms[roomIndex].GetParentRoomID() == m_vecRooms[m_CurrentRoomIndex].GetRoomID())
	{
		selectedRoomIndex = m_CurrentRoomIndex;
	}
	roomIndex = selectedRoomIndex;
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
	if (!m_EffectiveRoomID.has_value() || event.channel_id != "channel:" + std::to_string(*m_EffectiveRoomID))
	{
		return;
	}
	m_mapMembers[event.member.id] = ToRoomMember(event.member);
	RefreshRoster();
}

void GenOnlineRoomsInterface::OnRealtimeMemberRemove(const GenOnlineRealtime::MemberRemoveEvent& event)
{
	if (!m_EffectiveRoomID.has_value() || event.channel_id != "channel:" + std::to_string(*m_EffectiveRoomID))
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
	if (!m_EffectiveRoomID.has_value() || message.channel_id != "channel:" + std::to_string(*m_EffectiveRoomID))
	{
		return;
	}
	for (const GenOnlineRealtime::ChannelChatLine& line : m_ChannelChat.AddLive(message))
	{
		ShowRoomChatLine(line);
	}
}

void GenOnlineRoomsInterface::OnRealtimeChannelHistory(const GenOnlineRealtime::ChannelHistoryEvent& event)
{
	const GenOnlineRealtime::ChannelChatUpdate update = m_ChannelChat.AddHistory(event);
	for (uint32_t key : update.removed)
	{
		RemoveRoomChatLine(key);
	}
	for (const GenOnlineRealtime::ChannelChatLine& line : update.shown)
	{
		ShowRoomChatLine(line);
	}
}

void GenOnlineRoomsInterface::OnRealtimeMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event)
{
	const uint32_t key = m_ChannelChat.Remove(event.channel_id, event.message_id);
	if (key != 0)
	{
		RemoveRoomChatLine(key);
	}
}

static bool ShellChatShown()
{
	return TheShell != nullptr && TheShell->isShellActive() && (TheGenOnlineGame == nullptr || !TheGenOnlineGame->isGameInProgress());
}

// The notice goes to the game setup chat inside a lobby, else to the channel chat when it shows, else to the notification box.
// A chat list takes the notice only while its menu is open and the shell shows.
void GenOnlineRoomsInterface::OnRealtimeMentionCreate(const GenOnlineRealtime::MentionCreateEvent& event)
{
	GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuth == nullptr || !m_MentionNotices.Accept(event, pAuth->GetUserID()))
	{
		return;
	}
	const UnicodeString text(from_utf8(GenOnlineRealtime::MentionNoticeText(event, m_vecRealtimeChannels)).c_str());
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	const bool bChatShown = ShellChatShown();
	if (bChatShown && pLobby != nullptr && pLobby->IsInLobby() && pLobby->m_OnChatCallback != nullptr)
	{
		pLobby->m_OnChatCallback(text, DetermineMentionColor());
	}
	else if (bChatShown && m_OnChatCallback != nullptr)
	{
		m_OnChatCallback(text, DetermineMentionColor());
	}
	else
	{
		GenOnlineShowNotificationBox(AsciiString::TheEmptyString, text, false);
	}
	GenOnlinePlayMentionSound();
}

// Reads the minute of an ISO 8601 UTC time as local time.
static bool UtcTextToLocalTime(const std::string& strTime, SYSTEMTIME& localTime)
{
	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	if (sscanf_s(strTime.c_str(), "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) != 5)
	{
		return false;
	}
	SYSTEMTIME utcTime = {};
	utcTime.wYear = static_cast<WORD>(year);
	utcTime.wMonth = static_cast<WORD>(month);
	utcTime.wDay = static_cast<WORD>(day);
	utcTime.wHour = static_cast<WORD>(hour);
	utcTime.wMinute = static_cast<WORD>(minute);
	return SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &localTime) != FALSE;
}

// Shows an ISO 8601 UTC time of the service as local date and time, or the text itself when it does not parse.
std::string GenOnlineLocalTimeText(const std::string& utcText)
{
	SYSTEMTIME localTime;
	if (!UtcTextToLocalTime(utcText, localTime))
	{
		return utcText;
	}
	char text[32];
	snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", localTime.wYear, localTime.wMonth, localTime.wDay, localTime.wHour, localTime.wMinute);
	return text;
}

// A history line shows the time the service stored the message, a live line the time it arrived.
void GenOnlineRoomsInterface::ShowRoomChatLine(const GenOnlineRealtime::ChannelChatLine& line)
{
	if (m_OnChatCallback == nullptr && m_OnChatMessageCallback == nullptr)
	{
		return;
	}
	const GenOnlineRealtime::ChatMessage& message = line.message;
	const bool bAction = message.type == "action";
	SYSTEMTIME systemTime;
	if (line.live || !UtcTextToLocalTime(message.created_at, systemTime))
	{
		GetLocalTime(&systemTime);
	}
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
	// A mention of the local player colours the whole line. Only a live mention plays the sound.
	const bool bMention = GenOnlineMentionsLocalUser(message);
	if (bMention)
	{
		color = DetermineMentionColor();
	}
	// Only live chat goes to the stats exporter, so a replayed history line is not exported again.
	if (line.live)
	{
		GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		const bool bLocal = pAuth != nullptr && pAuth->GetUserID() == message.author.id;
		LocalApiLiveChatOnline(LOCAL_API_CHAT_ROOM, message.author.id, message.author.name.c_str(), message.content.c_str(), bAction, FALSE, bLocal);
	}
	if (m_OnChatMessageCallback != nullptr)
	{
		m_OnChatMessageCallback(unicodeStr, color, line.key);
	}
	else
	{
		m_OnChatCallback(unicodeStr, color);
	}
	if (bMention && line.live)
	{
		GenOnlinePlayMentionSound();
	}
}

void GenOnlineRoomsInterface::RemoveRoomChatLine(uint32_t key)
{
	if (m_OnChatMessageRemovedCallback != nullptr)
	{
		m_OnChatMessageRemovedCallback(key);
	}
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

void GenOnlineLobbyInterface::ShowRealtimeLobbyFailure(const GenOnlineRealtime::InvokeOutcome& outcome)
{
	if (m_OnChatCallback == nullptr)
	{
		return;
	}
	m_OnChatCallback(UnicodeString(GenOnlineRealtimeBridge::FailureText(outcome).c_str()), DetermineSystemNoticeColor(false, true));
}
