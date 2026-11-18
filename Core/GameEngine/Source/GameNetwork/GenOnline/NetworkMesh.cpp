#include "GameNetwork/GenOnline/NetworkMesh.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/NetBytes.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"

#include <ws2ipdef.h>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <deque>
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"
#include "Common/GlobalData.h"
#include "GameLogic/GameLogic.h"
#include "../GenOnlineRoomsInterface.h"
#include "../json.hpp"
#include "../HTTP/HTTPManager.h"
#include "../GenOnlineInit.h"
#include "../GenOnlineRealtimeBridge.h"
#include "../GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"

// set by the /net relay, /net gns ice and /net transport chat commands, -1 follows the service config
int g_relayOverride = -1;             // 1 = relay only, -1 = service policy
int g_iceImplementationOverride = -1; // 1 = native, 2 = WebRTC
int g_transportOverride = -1;         // 1 = GNS only, -1 = offer every available plugin transport

// The plugin packets the lobby takes per tick, 255 for each of up to 8 peers like the per connection GNS receive.
static constexpr int k_pluginLitePacketsPerTick = 255 * 8;
// The packets the lobby takes per tick from each connection that receives on its own.
static constexpr int k_maxFramesPerReceive = 255;

static int64_t NowSteadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The reason a pair that restarts gives for closing its connection. The peer looks for it to tell a restart from a leave.
static const char* const k_szRestartCloseReason = "Client Disconnecting Gracefully (new connection being negotiated)";

// The per-peer transports write to the Generals Online network log.
static void WritePeerTransportLog(PeerLogLevel level, const char* szLine)
{
	NetworkLog(level == PeerLogLevel::Debug ? ELogVerbosity::LOG_DEBUG : ELogVerbosity::LOG_RELEASE, "%s", szLine);
}

// Carries the GameNetworkingSockets signals over the realtime connection of the services.
class GenOnlineSignalChannel : public IPeerSignalChannel
{
public:
	virtual ~GenOnlineSignalChannel() override {}

	virtual bool IsOpen() const override
	{
		return GenOnlineServicesManager::GetWebSocket() != nullptr;
	}

	virtual void SendSignal(int64_t targetUserID, const std::vector<uint8_t>& payload) override
	{
		std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
		if (pWS)
		{
			pWS->SendData_Signalling(targetUserID, payload);
		}
	}

	// The service stamps each signal with the user it came from, see WebSocket::PushPendingSignal.
	virtual void DrainSignals(std::deque<PeerSignal>& out) override
	{
		std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
		if (pWS)
		{
			std::deque<PeerSignal> pendingSignals = pWS->DrainPendingSignals();
			for (PeerSignal& signal : pendingSignals)
			{
				out.push_back(std::move(signal));
			}
		}
	}
};

static GenOnlineSignalChannel s_signalChannel;

// Takes the GameNetworkingSockets connection changes into the mesh of the current lobby. The library calls it on the
// main thread through RunCallbacks().
class GenOnlineGnsListener : public IGnsPeerListener
{
public:
	virtual ~GenOnlineGnsListener() override {}

	virtual GnsPeerNetwork* GetActiveNetwork() override
	{
		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		return pMesh != nullptr ? &pMesh->GetGnsNetwork() : nullptr;
	}

	// Called on connection state transitions. Always runs on the main thread via RunCallbacks(), so
	// m_mapConnections is accessed here without m_mapConnectionsMutex.
	virtual bool FindConnection(HSteamNetConnection hConnection, GnsPeerRef& peer) override
	{
		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		if (pMesh == nullptr)
		{
			return false;
		}

		std::map<int64_t, PlayerConnection>& connections = pMesh->GetAllConnections();
		for (auto& kvPair : connections)
		{
			if (kvPair.second.GetSteamConnection() == hConnection)
			{
				peer.key = kvPair.first;
				peer.userID = kvPair.second.m_userID;
				peer.hConnection = kvPair.second.GetSteamConnection();
				return true;
			}
		}

		return false;
	}

	virtual void OnConnectionEnded(const GnsPeerRef& peer, const GnsPeerEnd& end) override;
	virtual bool OnIncomingConnection(const GnsPeerRef& peer, HSteamNetConnection hConnection, const char* szDescription) override;

	virtual GnsIncomingSetup GetIncomingSetup(const GnsPeerRef& peer) override
	{
		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		return pMesh != nullptr ? pMesh->GetIncomingSetup(peer.userID) : GnsIncomingSetup();
	}

	virtual void OnStateChanged(const GnsPeerRef& peer, GnsPeerState state) override
	{
		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		if (pMesh == nullptr)
		{
			return;
		}

		PlayerConnection& plrConnection = pMesh->GetAllConnections()[peer.key];
		switch (state)
		{
		case GnsPeerState::Connecting:
			plrConnection.UpdateState(EConnectionState::CONNECTING_DIRECT, pMesh);
			break;
		case GnsPeerState::FindingRoute:
			plrConnection.UpdateState(EConnectionState::FINDING_ROUTE, pMesh);
			break;
		case GnsPeerState::Connected:
			plrConnection.UpdateState(EConnectionState::CONNECTED_DIRECT, pMesh);
			break;
		}
	}
};

static GenOnlineGnsListener s_gnsListener;

void GenOnlineGnsListener::OnConnectionEnded(const GnsPeerRef& peer, const GnsPeerEnd& end)
{
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh == nullptr)
	{
		return;
	}

	std::map<int64_t, PlayerConnection>& connections = pMesh->GetAllConnections();
	PlayerConnection& plrConnection = connections[peer.key];

	// Capture before SetDisconnected(), which can erase this entry via UpdateState().
	const int64_t userID = plrConnection.m_userID;
	const int signallingAttemptsBeforeDisconnect = plrConnection.m_SignallingAttempts;
	plrConnection.m_retryTimeout = true;

	const bool bInMatch = TheGameLogic != nullptr && TheGameLogic->isInInternetGame();
	if (TheNetwork != nullptr)
	{
		TheNetwork->GetConnectionManager()->disconnectPlayer(userID);

		// a peer that closed on purpose during the match has left; closing to negotiate a new connection is not a leave
		if (end.closedOnPurpose && bInMatch)
		{
			TheNetwork->GetConnectionManager()->playerClosedGracefully(userID);
		}
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Closing connection %lld", userID);

	ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
	const int numSignallingAttempts = 2;

	// only the later joiner of a pair gives up; unknown join order caps both sides, a departed peer is capped without leaving
	GenOnlineLobbyInterface* pJoinOrderLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	const bool bWeJoinedLater = pJoinOrderLobby == nullptr || !pJoinOrderLobby->IsJoinOrderKnown() || pJoinOrderLobby->JoinedAfter(userID);
	const bool bPeerLeft = pJoinOrderLobby != nullptr && pJoinOrderLobby->IsJoinOrderKnown() && !pJoinOrderLobby->IsLobbyMember(userID);
	// a match can't leave its lobby, so keep repairing the link until the service drops the player
	const bool bManualLobby = pJoinOrderLobby != nullptr && pJoinOrderLobby->GetCurrentLobby().lobby_type == ELobbyType::CustomGame;

	// A timed step that ends before it connects moves the pair to the next step of its connection order instead of retrying.
	if (!bPeerLeft && plrConnection.m_stepWatch.IsOpen())
	{
		pMesh->EndStepAttempt(plrConnection, "closed before it connected", plrConnection.m_stepWatch.TakeFailure());
		return;
	}

	bool bShouldRetry = serviceConf.connection.retry_signalling && !bPeerLeft
		&& (bInMatch || (!bManualLobby && !bWeJoinedLater) || signallingAttemptsBeforeDisconnect < numSignallingAttempts);

	bool bWasError = end.wasError;
	plrConnection.SetDisconnected(bWasError, pMesh, bShouldRetry && bWasError);
	// plrConnection may be dangling past this point; use the captured locals.

	// the highest slot player, should leave. In most cases, this is the most recently joined player, but this may not be 100% accurate due to backfills.
	// TODO_NGMP: In the future, we should pick the most recently joined by timestamp
	if (bWasError) // only if it wasn't a clean disconnect (e.g. lobby leave)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Determined we didn't connect due to an error, Retrying: %d (currently at %d/%d attempts)", bShouldRetry, signallingAttemptsBeforeDisconnect, numSignallingAttempts);

		// should we retry signaling?
		if (bShouldRetry)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Retrying...");
			std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
			if (pWS)
			{
				GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobbyInterface != nullptr && pAuthInterface != nullptr)
				{
					int64_t myUserID = pAuthInterface->GetUserID();

					// Behavior:
					// disconnected slot userID is higher than ours, do nothing, they will signal
					// disconnected slot userID is lower than ours, we signal
					if ((myUserID > userID))
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Send signal start request...");

						pWS->SendData_RequestSignalling(userID);
					}
					else
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Not sending signal start request, other player should");
					}
				}

			}
			else
			{
				// Should always have a websocket... so lets just fail
				bShouldRetry = false;
			}
		}

		if (!bShouldRetry && bPeerLeft)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Not retrying, user %lld is no longer in the lobby", userID);
		}
		else if (!bShouldRetry && !bWeJoinedLater)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Not retrying, user %lld joined after us and will leave", userID);
		}
		else if (!bShouldRetry)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Not retrying, handling disconnect as failure...");

			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface != nullptr && pLobbyInterface->IsHost())
			{
				// the host keeps its lobby; the peer that can't connect is the one to go
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Not leaving, we host this lobby; dropping user %lld only", userID);
			}
			else if (pLobbyInterface != nullptr)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][DISCONNECT HANDLER] Connection attempts exhausted for user %lld\n", userID);

				if (pLobbyInterface->GetCurrentLobby().lobby_type == ELobbyType::QuickMatch)
				{
					// deferred: the handler leaves the lobby, which deletes this mesh while we're still inside its RunCallbacks
					pLobbyInterface->QueueCannotConnectToLobby();
				}
				else
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Staying in lobby after connection failure peer=%lld; manual retry available", userID);
				}
			}
		}
	}


	// A peer that left the lobby is removed from the mesh.
	if (bPeerLeft)
	{
		pMesh->RemoveConnection(userID);
	}

	// In this example, we will bail the test whenever this happens.
	// Was this a normal termination?
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING]DISCONNECTED OR PROBLEM DETECTED %d\n", end.endReason);
}

bool GenOnlineGnsListener::OnIncomingConnection(const GnsPeerRef& peer, HSteamNetConnection hConnection, const char* szDescription)
{
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh == nullptr)
	{
		return false;
	}

	PlayerConnection& plrConnection = pMesh->GetAllConnections()[peer.key];

	plrConnection.UpdateState(EConnectionState::CONNECTING_DIRECT, pMesh);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM CONNECTION] Updating connection from %u to %u on user %lld", plrConnection.GetSteamConnection(), hConnection, plrConnection.m_userID);
	GnsPeerTransport::NameConnection(hConnection, plrConnection.m_userID);
	plrConnection.SetSteamConnection(hConnection);

	// check user is in the lobby, otherwise reject
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][%s] Rejecting - Lobby interface is null\n", szDescription);

		NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Closing connection 2 %lld", plrConnection.m_userID);
		GnsPeerNetwork::CloseConnection(hConnection, 1000, "Lobby interface is null (Rejected)");

		if (TheNetwork != nullptr)
		{
			TheNetwork->GetConnectionManager()->disconnectPlayer(plrConnection.m_userID);
		}

		return false;
	}

	return true;
}

// The GameNetworkingSockets settings of Generals Online, without the TURN credentials and the settings of a lobby.
static GnsNetworkConfig MakeGnsNetworkConfig()
{
	GnsNetworkConfig config;

	// Every STUN entry must resolve to a distinct address, or the native ICE client retries
	// duplicates forever.
	config.stunServers = "stun:stun.generals.online:53,stun:stun.generals.online:3478,stun:stun.l.google.com:19302";

#if defined(_DEBUG)
	config.verboseLogging = true;
#else
	config.verboseLogging = GenOnlineServicesManager::Settings.Debug_VerboseLogging();
#endif

	// comma-separated; no "?transport=udp" suffix, the native ICE client takes everything after
	// the host as the port
	config.turnServers = "turn:turn.generals.online:53,turn:turn.generals.online:3478";

	return config;
}

bool NetworkMeshLibrary::EnsureInitialized(int64_t userID)
{
	SetPeerTransportLogSink(&WritePeerTransportLog);

	// lives until the online services shut down; a new login always follows a full teardown
	if (GnsPeerLibrary::IsInitialized())
	{
		return true;
	}

	return GnsPeerLibrary::EnsureInitialized(userID, MakeGnsNetworkConfig(), &s_gnsListener);
}

void NetworkMeshLibrary::Shutdown()
{
	GnsPeerLibrary::Shutdown();
}

void NetworkMeshLibrary::Tick()
{
	GnsPeerLibrary::Tick();
}

NetworkMesh::NetworkMesh()
{
	GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
	if (pOnlineServicesMgr == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "pOnlineServicesMgr is invalid");
		return;
	}

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "pAuthInterface is invalid");
		return;
	}

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "pLobbyInterface is invalid");
		return;
	}

	if (!NetworkMeshLibrary::EnsureInitialized(pAuthInterface->GetUserID()))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "NetworkMeshLibrary::EnsureInitialized failed");
		return;
	}

	GnsNetworkConfig gnsConfig = MakeGnsNetworkConfig();
	gnsConfig.turnUsername = pLobbyInterface->GetLobbyTurnUsername();
	gnsConfig.turnToken = pLobbyInterface->GetLobbyTurnToken();

	ServiceConfig& serviceConf = pOnlineServicesMgr->GetServiceConfig();
	const int reconnectTimeoutMs = MeshConnectionTimeoutPolicy::ClampReconnectTimeout(serviceConf.connection.reconnect_timeout_ms);
	const auto firstTimeouts = MeshConnectionTimeoutPolicy::ForAttempt(false, reconnectTimeoutMs);
	const auto retryTimeouts = MeshConnectionTimeoutPolicy::ForAttempt(true, reconnectTimeoutMs);
	gnsConfig.firstAttemptTimeouts = { firstTimeouts.initialMs, firstTimeouts.connectedMs };
	gnsConfig.retryTimeouts = { retryTimeouts.initialMs, retryTimeouts.connectedMs };
	gnsConfig.relayOnly = GnsNetworkConfig::ResolveRelayOnly(serviceConf.connection.relay_only, g_relayOverride);
	// The plugin transport follows the same relay rule as GNS for the connections of this lobby.
	GenOnlinePluginHost::Transport::SetRelayPolicy(gnsConfig.relayOnly);

	// 0 = library default, 1 = native, 2 = WebRTC
	gnsConfig.iceImplementation = GnsNetworkConfig::ResolveIceImplementation(serviceConf.connection.ice_engine, g_iceImplementationOverride);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "NetworkMesh: using ICE implementation %d (0=default, 1=native, 2=WebRTC)", gnsConfig.iceImplementation);

	if (!m_gns.Init(gnsConfig, &s_signalChannel))
	{
		return;
	}

	m_bInitialized = true;

	// the key of this client for the lobby, peers get it as they connect
	BeginLobbyKeys();
}

void NetworkMesh::Flush()
{
	ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
	bool bDoImmediateFlushPerFrame = serviceConf.send.flush_every_frame;

	if (bDoImmediateFlushPerFrame)
	{
		for (auto& connectionData : m_mapConnections)
		{
			connectionData.second.FlushTransport();
		}
	}
}


void NetworkMesh::UpdateConnectivity(PlayerConnection* connection)
{
	// Only the final result of a pair is reported, never the connecting or route finding states.
	const EConnectionState state = connection->GetState();
	const bool bConnected = state == EConnectionState::CONNECTED_DIRECT;
	const bool bDisconnected = state == EConnectionState::CONNECTION_DISCONNECTED;
	if (!bConnected && !bDisconnected && state != EConnectionState::CONNECTION_FAILED)
	{
		return;
	}

	// A report belongs to the lobby of the pair, outside a lobby there is nothing to report.
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr || !pLobbyInterface->IsInLobby())
	{
		return;
	}

	// A placeholder entry of a manual retry is no attempt, and a report for it would replace the report of a real attempt.
	if (!connection->m_step.IsValid())
	{
		return;
	}

	GenOnlineRealtime::ConnectionReportBody body;
	body.target_user_id = connection->m_userID;
	body.outcome = GenOnlineRealtime::ConnectionReportOutcome(bConnected, !connection->IsDirect(), bDisconnected);
	// The StartSignalling of the attempt names its transport and its number.
	body.transport = connection->m_step.transport;
	body.ip_version = connection->IsIPV4() ? 4 : 6;
	body.attempt = connection->m_step.attempt;
	if (!connection->m_timeline.Events().empty())
	{
		body.events = connection->m_timeline.Events();
	}
	const std::string path = GenOnlineRealtime::RestConnectionReportPath(pLobbyInterface->GetCurrentLobby().lobbyID);
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Post, path, nlohmann::json(body).dump(), nullptr);
}

int NetworkMesh::SendGamePacket(void* pBuffer, uint32_t totalDataSize, int64_t user_id)
{
	if (!pBuffer || totalDataSize == 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SendGamePacket] CRITICAL: Received null pBuffer or zero size from user %lld, size=%u", static_cast<long long>(user_id), totalDataSize);
		return -3;  // Invalid buffer
	}

	// Thread safety: Lock connection map during access
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	
	auto it = m_mapConnections.find(user_id);
	if (it != m_mapConnections.end())
	{
		return it->second.SendGamePacket(pBuffer, totalDataSize);
	}
	
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SendGamePacket] Connection not found for user %lld", static_cast<long long>(user_id));
	return -2;
}


void NetworkMesh::SendACPacket(uint32_t userID, const void* pData, uint32_t dataLen)
{
    if (dataLen == 0)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Cannot send empty AC packet to user %u", userID);
        return;
    }

    if (pData == nullptr)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Cannot send AC packet with null data to user %u", userID);
        return;
    }

	// Thread safety: Lock connection map during access
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	// A pair that cannot send yet holds the message until it connects, on any transport and attempt.
	auto it = m_mapConnections.find(userID);
	if (it != m_mapConnections.end() && it->second.GetState() == EConnectionState::CONNECTED_DIRECT && it->second.CanSend())
	{
		SendHeldACMessages(it->second);
		it->second.SendACPacket(pData, dataLen);
		return;
	}

	const uint8_t* pBytes = static_cast<const uint8_t*>(pData);
	if (m_heldACMessages.Push(userID, std::vector<uint8_t>(pBytes, pBytes + dataLen), NowSteadyMs()))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Dropped a new AC message for user %u, %u messages are already waiting", userID,
			static_cast<uint32_t>(GenOnlineRealtime::AnticheatHoldQueue::MAX_MESSAGES));
	}
	NetworkLog(ELogVerbosity::LOG_DEBUG, "[AC] Holding an AC message of %u bytes for user %u until the pair connects", dataLen, userID);
}

void NetworkMesh::SendHeldACMessages(PlayerConnection& connection)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	const std::vector<std::vector<uint8_t>> messages = m_heldACMessages.Take(connection.m_userID, NowSteadyMs());
	if (messages.empty())
	{
		return;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Sending %u held AC messages to user %lld", static_cast<uint32_t>(messages.size()), connection.m_userID);
	for (const std::vector<uint8_t>& message : messages)
	{
		connection.SendACPacket(message.data(), static_cast<uint32_t>(message.size()));
	}
}

void NetworkMesh::ClearHeldACMessages(int64_t userID)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	m_heldACMessages.Clear(userID);
}

GnsIncomingSetup NetworkMesh::GetIncomingSetup(int64_t userID)
{
	GnsIncomingSetup setup;
	auto it = m_mapConnections.find(userID);
	setup.retryTimeouts = (it != m_mapConnections.end() && it->second.m_retryTimeout)
		|| (TheGameLogic != nullptr && TheGameLogic->isInInternetGame());

	// A direct step of the pair takes the TURN servers of the listen socket away.
	if (it != m_mapConnections.end() && it->second.m_step.IsValid() && !it->second.m_step.relay)
	{
		setup.directOnly = true;
		setup.stepAttempt = it->second.m_step.attempt;
	}

	return setup;
}

void NetworkMesh::StartConnectionSignalling(const char* szMiddlewareID, int64_t remoteUserID, uint16_t preferredPort, const GenOnlineRealtime::ConnectionStep& step)
{
	// Thread safety: Lock connection map during access
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	// The service numbers the attempts of a pair upwards, an older attempt changes nothing.
	auto current = m_mapConnections.find(remoteUserID);
	if (current != m_mapConnections.end() && current->second.m_step.attempt > step.attempt)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Ignoring attempt %d with %lld, attempt %d is current", step.attempt, remoteUserID, current->second.m_step.attempt);
		return;
	}

	DropPendingStateUpdates(remoteUserID);

	if (step.IsPlugin())
	{
		// A repeated StartSignalling restarts the pair. The old connection closes first, because a single-direction connection breaks signalling.
		// A pair that moves from GameNetworkingSockets to the plugin closes its old connection like a restart does.
		auto existing = m_mapConnections.find(remoteUserID);
		if (existing != m_mapConnections.end())
		{
			CloseForRestart(remoteUserID, existing->second, true);
		}

		// The plugin step fails at once when this client has no plugin transport of that name.
		const std::string ownTransport = GenOnlinePluginHost::Transport::GetName();
		const bool bTransportReady = !ownTransport.empty() && ownTransport == step.transport;
		DropParkedSignalling(remoteUserID);
		if (bTransportReady)
		{
			GenOnlinePluginHost::Transport::StartSignalling(szMiddlewareID, remoteUserID);
		}
		m_bUsedPluginTransport = true;

        // create a local user type
        {
            std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
            auto it = m_mapConnections.find(remoteUserID);
            int previousAttempts = it != m_mapConnections.end() ? it->second.m_SignallingAttempts : 0;
            m_mapConnections[remoteUserID] = PlayerConnection(remoteUserID, GenOnlinePluginHost::Transport::GetPeerTransportApi(), szMiddlewareID);
            PlayerConnection& created = m_mapConnections[remoteUserID];

            // add attempt; carried over so the retry cap holds across re-signals
            // An attempt of a timed step does not count towards the cap, the hub moves the pair on instead.
            created.m_SignallingAttempts = previousAttempts + (step.timeoutS > 0 ? 0 : 1);
            created.m_step = step;
            created.m_stepWatch.Begin(step, NowSteadyMs());

            if (!bTransportReady)
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Attempt %d with %lld names the plugin transport %s, this client has '%s'", step.attempt, remoteUserID, step.transport.c_str(), ownTransport.c_str());
                // An untimed step reports too, the watch only keeps a timed one from reporting twice.
                created.m_stepWatch.TakeFailure();
                EndStepAttempt(created, "no plugin transport", true);
            }
        }
	}
	else
	{
        // no relay without our TURN credentials
        const bool bRelay = step.relay;
        if (m_bAwaitingTurnCredentials && bRelay)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Holding signalling with %lld until our TURN credentials arrive", remoteUserID);
            // A repeated StartSignalling replaces the held request of that peer, so the pair starts once.
            for (auto pending = m_vecSignallingAwaitingTurn.begin(); pending != m_vecSignallingAwaitingTurn.end(); ++pending)
            {
                if (pending->remoteUserID == remoteUserID)
                {
                    m_vecSignallingAwaitingTurn.erase(pending);
                    break;
                }
            }
            m_vecSignallingAwaitingTurn.push_back({ szMiddlewareID != nullptr ? szMiddlewareID : "", remoteUserID, preferredPort, step });
            return;
        }
        DropParkedSignalling(remoteUserID);

        // if we already have a connection to this use, drop it, having a single-direction connection will break signalling
        int previousAttempts = 0;
        bool retryTimeout = TheGameLogic != nullptr && TheGameLogic->isInInternetGame();
        auto it = m_mapConnections.find(remoteUserID);
        if (it != m_mapConnections.end())
        {
            previousAttempts = it->second.m_SignallingAttempts;
            retryTimeout = true;

            // A pair that moves from the plugin to GameNetworkingSockets ends its plugin connection first.
            CloseForRestart(remoteUserID, it->second, false);

            NetworkLog(ELogVerbosity::LOG_RELEASE, "[ERASE 3] Removing user %lld", it->second.m_userID);
            m_mapConnections.erase(it);
        }

        GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
        GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();

        if (pAuthInterface == nullptr || pOnlineServicesMgr == nullptr)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "NetworkMesh::ConnectToSingleUser - Auth or OSM interface is null");
            return;
        }

        // never connect to ourself
        if (remoteUserID == pAuthInterface->GetUserID())
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "NetworkMesh::ConnectToSingleUser - Skipping connection to user %lld - user is local", remoteUserID);
            return;
        }

        ServiceConfig& serviceConf = pOnlineServicesMgr->GetServiceConfig();

        int g_nVirtualPortRemote = serviceConf.connection.use_mapped_port ? preferredPort : 0;

        // make a steam connection obj
        HSteamNetConnection hSteamConnection = m_gns.Connect(remoteUserID, g_nVirtualPortRemote, bRelay, retryTimeout);

        if (hSteamConnection == k_HSteamNetConnection_Invalid)
        {
            return;
        }

        // create a local user type
        {
            std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
            m_mapConnections[remoteUserID] = PlayerConnection(remoteUserID, hSteamConnection);

            // add attempt; carried over so the retry cap holds across re-signals
            // An attempt of a timed step does not count towards the cap, the hub moves the pair on instead.
            m_mapConnections[remoteUserID].m_SignallingAttempts = previousAttempts + (step.timeoutS > 0 ? 0 : 1);
            m_mapConnections[remoteUserID].m_retryTimeout = retryTimeout;
            m_mapConnections[remoteUserID].m_step = step;
            m_mapConnections[remoteUserID].m_stepWatch.Begin(step, NowSteadyMs());
        }
	}

}

bool NetworkMesh::BeginLobbyKeys()
{
	GenOnlineAuthInterface* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (auth == nullptr)
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	if (!m_matchKeys.BeginLobby(auth->GetUserID()))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Could not make the key pair of this client");
		return false;
	}
	m_keySync.clear();
	return true;
}

void NetworkMesh::TickLobbyKeys(int64_t nowMs)
{
	// once a match is bound its keys are done, the roster does not change
	if (!m_matchKeys.HasLocalKey() || m_matchKeys.IsActive())
	{
		return;
	}

	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	// the key of a peer that is gone is dropped, it announces again when it is back
	for (int64_t userID : m_matchKeys.GetPeerKeyUsers())
	{
		auto it = m_mapConnections.find(userID);
		if (it == m_mapConnections.end() || !it->second.IsValid())
		{
			m_matchKeys.DropPeer(userID);
			m_keySync.erase(userID);
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Dropped the key of user %lld, it is no longer connected", static_cast<long long>(userID));
		}
	}

	const std::set<int64_t> self = { m_matchKeys.GetLocalUserID() };
	const std::vector<uint8_t> announcement = m_matchKeys.BuildAnnouncement();
	const std::vector<uint8_t> status = m_matchKeys.BuildStatus();
	for (auto& entry : m_mapConnections)
	{
		PlayerConnection& connection = entry.second;
		if (connection.GetState() != EConnectionState::CONNECTED_DIRECT || !connection.CanSend())
		{
			m_keySync.erase(entry.first);
			continue;
		}

		KeySync& sync = m_keySync[entry.first];
		const bool announce = nowMs >= sync.nextAnnounceMs && !m_matchKeys.PeerHoldsAll(entry.first, self);
		const bool report = nowMs >= sync.nextStatusMs || status != sync.lastStatus;
		if (!announce && !report)
		{
			continue;
		}

		for (int pass = 0; pass < 2; ++pass)
		{
			if (pass == 0 ? !announce : !report)
			{
				continue;
			}
			const std::vector<uint8_t>& body = pass == 0 ? announcement : status;
			std::vector<BYTE> packet(sizeof(ENetworkChannel) + body.size());
			packet[0] = static_cast<BYTE>(ENetworkChannel::NETWORK_CHANNEL_KEYS);
			memcpy(packet.data() + sizeof(ENetworkChannel), body.data(), body.size());
			const EResult result = static_cast<EResult>(connection.SendFramed(packet.data(), packet.size(), k_nSteamNetworkingSend_ReliableNoNagle));
			if (result != k_EResultOK)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Could not send to user %lld, error %d", static_cast<long long>(entry.first), static_cast<int>(result));
			}
		}
		if (announce)
		{
			sync.nextAnnounceMs = nowMs + 1000;
		}
		if (report)
		{
			sync.nextStatusMs = nowMs + 5000;
			sync.lastStatus = status;
		}
	}
}

bool NetworkMesh::BeginMatchKeys()
{
	GenOnlineAuthInterface* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (auth == nullptr || lobby == nullptr || TheGenOnlineGame == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Cannot authenticate match without auth, lobby and roster");
		EndMatchKeys();
		return false;
	}

	std::set<int64_t> peers;
	bool haveLocalPlayer = false;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GenOnlineGameSlot* slot = (const GenOnlineGameSlot*)TheGenOnlineGame->getConstSlot(i);
		if (slot == nullptr || !slot->isHuman())
		{
			continue;
		}
		if (slot->m_userID < 0 || !peers.insert(slot->m_userID).second)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Invalid or duplicate user identity in match roster");
			EndMatchKeys();
			return false;
		}
		haveLocalPlayer |= slot->m_userID == auth->GetUserID();
	}
	if (!haveLocalPlayer)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Local user is missing from match roster");
		EndMatchKeys();
		return false;
	}
	peers.erase(auth->GetUserID());

	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	if (!m_matchKeys.HasLocalKey() && !BeginLobbyKeys())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Could not make the key pair of this client");
		return false;
	}

	// the keys came in while the lobby was open, a launch never waits for one
	for (int64_t userID : peers)
	{
		if (!m_matchKeys.HasPeerKey(userID))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] No key of user %lld in the roster of the match", static_cast<long long>(userID));
			EndMatchKeys();
			return false;
		}
	}

	if (!m_matchKeys.BeginMatch(lobby->GetCurrentMatchID(), peers))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Could not bind the keys to the match");
		return false;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Keys bound to match %llu with %zu peers", static_cast<unsigned long long>(lobby->GetCurrentMatchID()), peers.size());
	return true;
}

void NetworkMesh::EndMatchKeys()
{
	m_matchKeys.EndMatch();
}

void NetworkMesh::GetLobbyStartBlockers(std::vector<LobbyStartBlocker>& blockers)
{
	blockers.clear();
	GenOnlineAuthInterface* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (auth == nullptr || lobby == nullptr)
	{
		return;
	}

	std::set<int64_t> humans;
	for (const LobbyMemberEntry& member : lobby->GetCurrentLobby().members)
	{
		if (member.IsHuman())
		{
			humans.insert(member.user_id);
		}
	}

	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	for (int64_t userID : humans)
	{
		if (userID == auth->GetUserID())
		{
			continue;
		}
		const PlayerConnection* connection = GetConnectionForUser(userID);
		if (connection == nullptr || connection->GetState() != EConnectionState::CONNECTED_DIRECT)
		{
			blockers.push_back({ userID, true });
		}
		else if (!m_matchKeys.HasPeerKey(userID) || !m_matchKeys.PeerHoldsAll(userID, humans))
		{
			blockers.push_back({ userID, false });
		}
	}
}

void NetworkMesh::OnKeyAnnouncement(int64_t connectionUserID, const uint8_t* data, size_t len)
{
	if (data == nullptr || len == 0 || !m_matchKeys.HasLocalKey())
	{
		return;
	}

	// in the lobby only its members count, a sender whose member entry has not arrived yet announces again
	if (!m_matchKeys.IsActive())
	{
		GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		bool member = false;
		if (lobby != nullptr)
		{
			for (const LobbyMemberEntry& entry : lobby->GetCurrentLobby().members)
			{
				member |= entry.IsHuman() && entry.user_id == connectionUserID;
			}
		}
		if (!member)
		{
			return;
		}
	}

	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	const char* szReason = nullptr;
	bool changed = false;
	const bool accepted = data[0] == MatchKeys::KIND_STATUS
		? m_matchKeys.OnStatus(connectionUserID, data, len, szReason, changed)
		: m_matchKeys.OnAnnouncement(connectionUserID, data, len, szReason, changed);
	if (!accepted)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] Rejected a key message from user %lld: %s", static_cast<long long>(connectionUserID), szReason != nullptr ? szReason : "unknown");
		return;
	}

	if (changed && data[0] == MatchKeys::KIND_ANNOUNCEMENT)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[KEYS] have keys for %zu peers, got one from user %lld", m_matchKeys.GetPeerKeyCount(), static_cast<long long>(connectionUserID));
	}
}

// Start run ahead payload after the channel byte, little endian:
//   report   u8 1, u64 match id, u8 count, u8 flags (bit 0: sends directly to every peer), count x (i64 user id, u16 p75 round trip ms)
//   decision u8 2, u64 match id, u8 run ahead, u16 highest pair round trip ms, u8 pairs
//   ready    u8 3, u64 match id, u8 stage, u8 flags (bit 0: the sender sends directly to every peer, or waited long enough for that)
//   go       u8 4, u64 match id, u8 stage, u16 ms the receiver waits before it starts
static constexpr uint8_t START_MSG_REPORT = 1;
static constexpr uint8_t START_MSG_DECISION = 2;
static constexpr int64_t START_REPORT_WAIT_MS = 3000;	// the router decides without the reports still missing after this
static constexpr size_t START_REPORT_MAX = 16;
static constexpr int START_REPORT_MAX_RTT_MS = 2000;	// longer lobby round trips are clamped
static constexpr int64_t START_LOST_GRACE_MS = 2000;	// a connection that is down this long belongs to a player that left
static constexpr uint8_t START_MSG_READY = 3;
static constexpr uint8_t START_MSG_GO = 4;
static constexpr int64_t SYNC_TIMEOUT_MS = 10000;	// the load screen leaves without the readies still missing after this
static constexpr int64_t SYNC_DIRECT_WAIT_MS = 3000;	// a client that has no direct path to every peer by now is ready anyway, the router path carries it
static constexpr int64_t SYNC_GO_WAIT_MS = 1500;	// the router's go is awaited this long after every ready is in
static constexpr int SYNC_GO_MAX_DELAY_MS = 250;

static void WriteStartBytes(std::vector<uint8_t>& out, const void* pData, size_t len)
{
	const uint8_t* pBytes = static_cast<const uint8_t*>(pData);
	out.insert(out.end(), pBytes, pBytes + len);
}

void NetworkMesh::SendStartMessage(int64_t userID, const std::vector<uint8_t>& payload)
{
	PlayerConnection* pConnection = GetConnectionForUser(userID);
	if (pConnection == nullptr || !pConnection->CanSend())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] No connection to user %lld for the start run ahead", static_cast<long long>(userID));
		return;
	}

	std::vector<uint8_t> packet;
	packet.push_back(static_cast<uint8_t>(ENetworkChannel::NETWORK_CHANNEL_START));
	packet.insert(packet.end(), payload.begin(), payload.end());
	const EResult r = static_cast<EResult>(pConnection->SendFramed(packet.data(), packet.size(), k_nSteamNetworkingSend_ReliableNoNagle));
	if (r != k_EResultOK)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] Could not send the start run ahead message to user %lld, error %d", static_cast<long long>(userID), static_cast<int>(r));
	}
}

void NetworkMesh::BeginStartRunAhead(int routerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (TheNetwork == nullptr || TheGenOnlineGame == nullptr || pAuthInterface == nullptr || pLobbyInterface == nullptr)
	{
		return;
	}

	const int64_t localUserID = pAuthInterface->GetUserID();
	m_startLocalUserID = localUserID;
	m_startMatchID = pLobbyInterface->GetCurrentMatchID();
	m_startActive = false;
	m_startDecided = false;
	m_startPlayers.clear();
	m_startRouterUserID = -1;
	m_startLostSinceMs.clear();
	for (Int slotNum = 0; slotNum < MAX_SLOTS; ++slotNum)
	{
		const GenOnlineGameSlot* pSlot = (const GenOnlineGameSlot*)TheGenOnlineGame->getConstSlot(slotNum);
		if (pSlot == nullptr || !pSlot->isHuman())
		{
			continue;
		}
		if (slotNum == routerSlot)
		{
			m_startRouterUserID = pSlot->m_userID;
		}
		if (pSlot->m_userID != localUserID)
		{
			m_startPlayers.push_back(pSlot->m_userID);
		}
	}

	// reports of another match are stale, reports from a user who is no start player are dropped
	for (auto it = m_startReports.begin(); it != m_startReports.end(); )
	{
		if (it->second.matchID != m_startMatchID || !IsStartPlayer(it->first))
		{
			it = m_startReports.erase(it);
		}
		else
		{
			SanitizeStartReport(it->first, it->second);
			++it;
		}
	}

	for (SyncStage& st : m_sync)
	{
		for (auto it = st.ready.begin(); it != st.ready.end(); )
		{
			it = (it->second.matchID != m_startMatchID) ? st.ready.erase(it) : std::next(it);
		}
		st.hasGo = st.hasGo && st.goMatchID == m_startMatchID && st.goFromUserID == m_startRouterUserID;
		st.active = false;
	}

	// no remote player: retail start
	if (m_startPlayers.empty() || m_startRouterUserID < 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] start R=%d, no start run ahead exchange (players=%d, router=%lld)", (int)TheNetwork->getRunAhead(), (int)m_startPlayers.size(), static_cast<long long>(m_startRouterUserID));
		return;
	}

	m_startIsRouter = (m_startRouterUserID == localUserID);

	TheNetwork->setStartRunAheadRequired(TRUE);
	m_startActive = true;
	m_startBeginMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

	// our round trip to every other player, the router keeps its own as a report
	StartRunAheadReport own;
	own.matchID = m_startMatchID;
	own.directSend = TheNetwork->GetConnectionManager() != nullptr && TheNetwork->GetConnectionManager()->isDirectSendActive(true);
	for (int64_t userID : m_startPlayers)
	{
		PlayerConnection* pConnection = GetConnectionForUser(userID);
		const int rtt = (pConnection != nullptr) ? pConnection->GetSeedLatency() : -1;
		if (rtt >= 0)
		{
			own.rtts.push_back(std::make_pair(userID, rtt));
		}
	}

	if (m_startIsRouter)
	{
		m_startReports[localUserID] = own;
		TickStartRunAhead(m_startBeginMs);
		return;
	}

	std::vector<uint8_t> payload;
	payload.push_back(START_MSG_REPORT);
	WriteStartBytes(payload, &m_startMatchID, sizeof(m_startMatchID));
	const uint8_t count = static_cast<uint8_t>((std::min)(own.rtts.size(), START_REPORT_MAX));
	payload.push_back(count);
	payload.push_back(own.directSend ? 1 : 0);
	for (uint8_t i = 0; i < count; ++i)
	{
		const int64_t userID = own.rtts[i].first;
		const uint16_t rtt = static_cast<uint16_t>((std::min)(own.rtts[i].second, 0xFFFE));
		WriteStartBytes(payload, &userID, sizeof(userID));
		WriteStartBytes(payload, &rtt, sizeof(rtt));
	}
	SendStartMessage(m_startRouterUserID, payload);

	// the router may have decided before this client began
	ApplyStartDecision();
}

void NetworkMesh::OnStartRunAheadMessage(int64_t connectionUserID, const uint8_t* data, size_t len)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	if (data == nullptr || len < 1 + sizeof(uint64_t))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] Ignoring a malformed start run ahead message from user %lld", static_cast<long long>(connectionUserID));
		return;
	}

	uint64_t matchID = 0;
	memcpy(&matchID, data + 1, sizeof(matchID));
	size_t offset = 1 + sizeof(matchID);

	if (data[0] == START_MSG_REPORT)
	{
		if (len < offset + 1)
		{
			return;
		}
		const uint8_t count = data[offset++];
		if (len < offset + 1)
		{
			return;
		}
		const uint8_t flags = data[offset++];
		if (len < offset + count * (sizeof(int64_t) + sizeof(uint16_t)))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] Ignoring a truncated round trip report from user %lld", static_cast<long long>(connectionUserID));
			return;
		}

		StartRunAheadReport report;
		report.matchID = matchID;
		report.directSend = (flags & 1) != 0;
		for (uint8_t i = 0; i < count; ++i)
		{
			int64_t userID = 0;
			uint16_t rtt = 0;
			memcpy(&userID, data + offset, sizeof(userID));
			offset += sizeof(userID);
			memcpy(&rtt, data + offset, sizeof(rtt));
			offset += sizeof(rtt);
			report.rtts.push_back(std::make_pair(userID, static_cast<int>(rtt)));
		}
		if (m_startActive && matchID == m_startMatchID)
		{
			// the start players are known: only they report, and only about start players
			if (!IsStartPlayer(connectionUserID))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[SECURITY] Ignoring a start round trip report from user %lld, who is not a start player", static_cast<long long>(connectionUserID));
				return;
			}
			SanitizeStartReport(connectionUserID, report);
		}
		m_startReports[connectionUserID] = report;

		const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		TickStartRunAhead(nowMs);
	}
	else if (data[0] == START_MSG_DECISION)
	{
		if (len < offset + 1 + sizeof(uint16_t) + 1)
		{
			return;
		}
		if (m_startActive && connectionUserID != m_startRouterUserID)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[SECURITY] Ignoring a start run ahead decision from user %lld, who is not the router", static_cast<long long>(connectionUserID));
			return;
		}
		uint16_t maxRtt = 0;
		StartDecision decision;
		decision.matchID = matchID;
		decision.runAhead = data[offset];
		memcpy(&maxRtt, data + offset + 1, sizeof(maxRtt));
		decision.maxRtt = maxRtt;
		decision.pairs = data[offset + 1 + sizeof(uint16_t)];
		m_startDecisions[connectionUserID] = decision;

		ApplyStartDecision();
	}
	else if (data[0] == START_MSG_READY)
	{
		if (len < offset + 2 || data[offset] >= SYNC_STAGES)
		{
			return;
		}
		const int stage = data[offset];
		if (m_startActive && matchID == m_startMatchID && !IsStartPlayer(connectionUserID))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[SECURITY] Ignoring a start ready from user %lld, who is not a start player", static_cast<long long>(connectionUserID));
			return;
		}
		StartReady ready;
		ready.matchID = matchID;
		ready.direct = (data[offset + 1] & 1) != 0;
		m_sync[stage].ready[connectionUserID] = ready;

		int total = 0;
		const int missing = CountSyncMissing(stage, NowSteadyMs(), total);
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d ready from user %lld (direct=%d), %d of %d heard", stage, static_cast<long long>(connectionUserID), (int)ready.direct, total - missing, total);
	}
	else if (data[0] == START_MSG_GO)
	{
		if (len < offset + 1 + sizeof(uint16_t) || data[offset] >= SYNC_STAGES)
		{
			return;
		}
		if (m_startActive && connectionUserID != m_startRouterUserID)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[SECURITY] Ignoring a start go from user %lld, who is not the router", static_cast<long long>(connectionUserID));
			return;
		}
		const int stage = data[offset];
		uint16_t delayMs = 0;
		memcpy(&delayMs, data + offset + 1, sizeof(delayMs));
		SyncStage& st = m_sync[stage];
		st.goMatchID = matchID;
		st.goDelayMs = (std::min)(static_cast<int>(delayMs), SYNC_GO_MAX_DELAY_MS);
		st.goReceivedMs = NowSteadyMs();
		st.goFromUserID = connectionUserID;
		st.hasGo = true;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d go from user %lld, starting in %d ms", stage, static_cast<long long>(connectionUserID), st.goDelayMs);
	}
}

void NetworkMesh::ApplyStartDecision()
{
	if (!m_startActive || m_startIsRouter || m_startDecided || TheNetwork == nullptr)
	{
		return;
	}
	// decisions of other users are kept until the router is known, only the router's counts
	auto it = m_startDecisions.find(m_startRouterUserID);
	if (it == m_startDecisions.end())
	{
		return;
	}
	if (it->second.matchID != m_startMatchID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] Ignoring the start R of match %llu, this is match %llu", static_cast<unsigned long long>(it->second.matchID), static_cast<unsigned long long>(m_startMatchID));
		return;
	}

	m_startDecided = true;
	TheNetwork->applyStartRunAhead(it->second.runAhead, it->second.maxRtt, it->second.pairs);
}

bool NetworkMesh::StartReportsComplete() const
{
	for (int64_t userID : m_startPlayers)
	{
		auto it = m_startReports.find(userID);
		if (it == m_startReports.end() || it->second.matchID != m_startMatchID)
		{
			return false;
		}
	}
	return true;
}

bool NetworkMesh::IsStartPlayer(int64_t userID) const
{
	return std::find(m_startPlayers.begin(), m_startPlayers.end(), userID) != m_startPlayers.end();
}

void NetworkMesh::SanitizeStartReport(int64_t reporter, StartRunAheadReport& report) const
{
	// an entry names the user the reporter measured: a start player or the local user, never the reporter itself
	for (auto it = report.rtts.begin(); it != report.rtts.end(); )
	{
		if (it->first == reporter || (!IsStartPlayer(it->first) && it->first != m_startLocalUserID))
		{
			it = report.rtts.erase(it);
		}
		else
		{
			it->second = (std::min)(it->second, START_REPORT_MAX_RTT_MS);
			++it;
		}
	}
}

bool NetworkMesh::IsStartPlayerLost(int64_t userID, int64_t nowMs)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	const PlayerConnection* pConnection = GetConnectionForUser(userID);
	if (pConnection != nullptr && pConnection->IsValid())
	{
		m_startLostSinceMs.erase(userID);
		return false;
	}
	auto it = m_startLostSinceMs.emplace(userID, nowMs).first;
	return nowMs - it->second >= START_LOST_GRACE_MS;
}

void NetworkMesh::PruneLostStartPlayers(int64_t nowMs)
{
	for (auto it = m_startPlayers.begin(); it != m_startPlayers.end(); )
	{
		if (IsStartPlayerLost(*it, nowMs))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] user %lld left before the start run ahead, not waiting for its report", static_cast<long long>(*it));
			m_startReports.erase(*it);
			it = m_startPlayers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void NetworkMesh::FallBackToRetailStart()
{
	m_startDecided = true;
	if (TheNetwork == nullptr)
	{
		return;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] the router (user %lld) left before it decided, start R=%d as in retail", static_cast<long long>(m_startRouterUserID), (int)TheNetwork->getRunAhead());
	TheNetwork->applyStartRunAhead((Int)TheNetwork->getRunAhead(), 0, 0);
}

void NetworkMesh::TickStartRunAhead(int64_t nowMs)
{
	if (!m_startActive || m_startDecided)
	{
		return;
	}

	if (!m_startIsRouter)
	{
		if (IsStartPlayerLost(m_startRouterUserID, nowMs))
		{
			FallBackToRetailStart();
		}
		return;
	}

	PruneLostStartPlayers(nowMs);
	if (!StartReportsComplete() && nowMs - m_startBeginMs < START_REPORT_WAIT_MS)
	{
		return;
	}

	DecideStartRunAhead();
}

void NetworkMesh::DecideStartRunAhead()
{
	m_startDecided = true;
	if (TheNetwork == nullptr)
	{
		return;
	}

	// round trips count only if every player reported, otherwise everybody starts at the retail value
	const bool allReported = StartReportsComplete();

	// a pair is the higher round trip of the two its players measured to each other
	std::map<std::pair<int64_t, int64_t>, int> pairRtts;
	int reports = 0;

	// a command travels one way only if every player sends directly right now: the router by its own state, the others by what they reported
	ConnectionManager* pConnectionManager = TheNetwork->GetConnectionManager();
	bool oneWay = pConnectionManager != nullptr && pConnectionManager->usesOneWayRunAhead(true);
	for (auto& kvReport : m_startReports)
	{
		// a player that left after it reported does not count
		const bool reporterStays = kvReport.first == m_startLocalUserID || IsStartPlayer(kvReport.first);
		if (!allReported || kvReport.second.matchID != m_startMatchID || !reporterStays)
		{
			continue;
		}
		++reports;
		if (kvReport.first != m_startLocalUserID)
		{
			oneWay = oneWay && kvReport.second.directSend;
		}
		for (const auto& entry : kvReport.second.rtts)
		{
			if (entry.first != m_startLocalUserID && !IsStartPlayer(entry.first))
			{
				continue;
			}
			const int64_t a = (std::min)(kvReport.first, entry.first);
			const int64_t b = (std::max)(kvReport.first, entry.first);
			int& rtt = pairRtts[std::make_pair(a, b)];
			rtt = (std::max)(rtt, entry.second);
		}
	}

	int maxRtt = 0;
	for (const auto& kvPair : pairRtts)
	{
		maxRtt = (std::max)(maxRtt, kvPair.second);
	}

	const int pairs = static_cast<int>(pairRtts.size());
	int runAhead = static_cast<int>(TheNetwork->getRunAhead());
	if (pairs > 0 && pConnectionManager != nullptr)
	{
		runAhead = pConnectionManager->computeStartRunAhead(static_cast<Real>(maxRtt), oneWay);
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[RUNAHEAD] router decides the start R=%d from %d reports of %d players (pairs=%d, max rtt=%dms, one_way=%d)%s", runAhead, reports, (int)m_startPlayers.size() + 1, pairs, maxRtt, (int)oneWay,
		allReported ? "" : ", a player did not report: retail start");

	std::vector<uint8_t> payload;
	payload.push_back(START_MSG_DECISION);
	WriteStartBytes(payload, &m_startMatchID, sizeof(m_startMatchID));
	payload.push_back(static_cast<uint8_t>(runAhead));
	const uint16_t maxRtt16 = static_cast<uint16_t>((std::min)(maxRtt, 0xFFFE));
	WriteStartBytes(payload, &maxRtt16, sizeof(maxRtt16));
	payload.push_back(static_cast<uint8_t>((std::min)(pairs, 255)));
	for (int64_t userID : m_startPlayers)
	{
		SendStartMessage(userID, payload);
	}

	TheNetwork->applyStartRunAhead(runAhead, maxRtt, pairs);
}

void NetworkMesh::SendSyncReady(int stage, int64_t nowMs, bool direct)
{
	std::vector<uint8_t> payload;
	payload.push_back(START_MSG_READY);
	WriteStartBytes(payload, &m_startMatchID, sizeof(m_startMatchID));
	payload.push_back(static_cast<uint8_t>(stage));
	payload.push_back(direct ? 1 : 0);
	for (int64_t userID : m_startPlayers)
	{
		if (!IsStartPlayerLost(userID, nowMs))
		{
			SendStartMessage(userID, payload);
		}
	}
	m_sync[stage].readySent = true;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d ready sent after %lld ms (direct=%d)", stage, static_cast<long long>(nowMs - m_sync[stage].beginMs), (int)direct);
}

int NetworkMesh::CountSyncMissing(int stage, int64_t nowMs, int& total)
{
	int missing = 0;
	total = 0;
	for (int64_t userID : m_startPlayers)
	{
		if (IsStartPlayerLost(userID, nowMs))
		{
			continue;
		}
		++total;
		auto it = m_sync[stage].ready.find(userID);
		if (it == m_sync[stage].ready.end() || it->second.matchID != m_startMatchID)
		{
			++missing;
		}
	}
	return missing;
}

void NetworkMesh::FinishSyncedStart(int stage, const char* szReason, int64_t nowMs)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d done %lld ms after the local part was done: %s", stage, static_cast<long long>(nowMs - m_sync[stage].beginMs), szReason);
	m_sync[stage].active = false;
}

bool NetworkMesh::BeginSyncedStart(int stage)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	if (stage < 0 || stage >= SYNC_STAGES)
	{
		return false;
	}
	SyncStage& st = m_sync[stage];
	st.active = m_startActive && !m_startPlayers.empty();
	st.readySent = false;
	st.allInMs = 0;
	st.leaveAtMs = 0;
	st.beginMs = NowSteadyMs();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d local part done, waiting for %d players (router=%d)", stage, st.active ? (int)m_startPlayers.size() : 0, (int)m_startIsRouter);
	return st.active;
}

bool NetworkMesh::SyncedStartDone(int stage)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	if (stage < 0 || stage >= SYNC_STAGES || !m_sync[stage].active)
	{
		return true;
	}
	SyncStage& st = m_sync[stage];
	const int64_t nowMs = NowSteadyMs();

	if (!st.readySent)
	{
		// on the load screen: ready once the direct path to every peer is up, or if direct sending is off or does not come up in time
		bool directUp = true;
		ConnectionManager* pConnectionManager = TheNetwork != nullptr ? TheNetwork->GetConnectionManager() : nullptr;
		if (stage == 0 && pConnectionManager != nullptr && pConnectionManager->getNetworkSettings().send.direct)
		{
			directUp = pConnectionManager->isDirectSendActive(true);
		}
		if (directUp || nowMs - st.beginMs >= SYNC_DIRECT_WAIT_MS)
		{
			SendSyncReady(stage, nowMs, directUp);
		}
	}

	if (st.leaveAtMs != 0)
	{
		if (nowMs < st.leaveAtMs)
		{
			return false;
		}
		FinishSyncedStart(stage, "common start time", nowMs);
		return true;
	}

	if (nowMs - st.beginMs >= SYNC_TIMEOUT_MS)
	{
		int total = 0;
		const int missing = CountSyncMissing(stage, nowMs, total);
		FinishSyncedStart(stage, missing > 0 ? "timeout, a player is not ready" : "timeout", nowMs);
		return true;
	}

	// the router's go decides, even if a ready of ours is still on the way
	if (!m_startIsRouter && st.hasGo && st.goMatchID == m_startMatchID)
	{
		st.leaveAtMs = st.goReceivedMs + st.goDelayMs;
		return false;
	}

	if (!st.readySent)
	{
		return false;
	}
	int total = 0;
	const int missing = CountSyncMissing(stage, nowMs, total);
	if (missing > 0)
	{
		st.allInMs = 0;
		return false;
	}
	if (st.allInMs == 0)
	{
		st.allInMs = nowMs;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d every player is ready after %lld ms", stage, static_cast<long long>(nowMs - st.beginMs));
	}

	if (m_startIsRouter)
	{
		// every client starts when the farthest go has arrived: the wait is that one way time minus its own
		int maxOneWay = 0;
		for (int64_t userID : m_startPlayers)
		{
			const PlayerConnection* pConnection = GetConnectionForUser(userID);
			const int rtt = pConnection != nullptr ? pConnection->GetSeedLatency() : -1;
			maxOneWay = (std::max)(maxOneWay, (std::min)((std::max)(rtt, 0) / 2, SYNC_GO_MAX_DELAY_MS));
		}
		for (int64_t userID : m_startPlayers)
		{
			if (IsStartPlayerLost(userID, nowMs))
			{
				continue;
			}
			const PlayerConnection* pConnection = GetConnectionForUser(userID);
			const int rtt = pConnection != nullptr ? pConnection->GetSeedLatency() : -1;
			const uint16_t delayMs = static_cast<uint16_t>((std::max)(maxOneWay - (std::min)((std::max)(rtt, 0) / 2, SYNC_GO_MAX_DELAY_MS), 0));
			std::vector<uint8_t> payload;
			payload.push_back(START_MSG_GO);
			WriteStartBytes(payload, &m_startMatchID, sizeof(m_startMatchID));
			payload.push_back(static_cast<uint8_t>(stage));
			WriteStartBytes(payload, &delayMs, sizeof(delayMs));
			SendStartMessage(userID, payload);
		}
		st.leaveAtMs = nowMs + maxOneWay;
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SYNC] stage %d go sent to %d players, starting in %d ms", stage, total, maxOneWay);
		if (maxOneWay == 0)
		{
			FinishSyncedStart(stage, "common start time", nowMs);
			return true;
		}
		return false;
	}

	// no go from a router that left or does not answer: everybody has its ready, start as the others do
	if (IsStartPlayerLost(m_startRouterUserID, nowMs) || nowMs - st.allInMs >= SYNC_GO_WAIT_MS)
	{
		FinishSyncedStart(stage, "every ready is in, no go from the router", nowMs);
		return true;
	}
	return false;
}

void NetworkMesh::AwaitTurnCredentials()
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	m_bAwaitingTurnCredentials = true;
}

void NetworkMesh::SetTurnCredentials(const std::string& strUsername, const std::string& strToken)
{
	std::vector<PendingSignalling> vecPending;
	{
		std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

		m_gns.SetTurnCredentials(strUsername, strToken);

		m_bAwaitingTurnCredentials = false;
		vecPending.swap(m_vecSignallingAwaitingTurn);
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Got TURN credentials, starting %d held signalling request(s)", (int)vecPending.size());
	for (const PendingSignalling& pending : vecPending)
	{
		StartConnectionSignalling(pending.strMiddlewareID.c_str(), pending.remoteUserID, pending.preferredPort, pending.step);
	}
}


void NetworkMesh::DropParkedSignalling(int64_t remoteUserID)
{
	for (auto pending = m_vecSignallingAwaitingTurn.begin(); pending != m_vecSignallingAwaitingTurn.end(); ++pending)
	{
		if (pending->remoteUserID == remoteUserID)
		{
			m_vecSignallingAwaitingTurn.erase(pending);
			return;
		}
	}
}

void NetworkMesh::DropPendingStateUpdates(int64_t userID)
{
	// A plugin state queued before the restart of a pair belongs to the attempt before it.
	std::lock_guard<std::mutex> lock(m_pendingStateUpdatesMutex);
	m_vecPendingStateUpdates.erase(std::remove_if(m_vecPendingStateUpdates.begin(), m_vecPendingStateUpdates.end(),
		[userID](const std::pair<int64_t, EConnectionState>& update)
		{
			return update.first == userID;
		}), m_vecPendingStateUpdates.end());
}

void NetworkMesh::CloseForRestart(int64_t remoteUserID, PlayerConnection& existing, bool bNextIsPlugin)
{
	if (!existing.CanSend())
	{
		return;
	}

	const bool bWasPlugin = existing.UsesPluginTransport();
	if (bWasPlugin && !bNextIsPlugin)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Closing plugin connection %lld, new connection is being negotiated", remoteUserID);
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Closing connection %lld, new connection is being negotiated", remoteUserID);
	}
	existing.CloseTransport(k_szRestartCloseReason);

	// States the plugin raises while it closes the old connection belong to the old attempt.
	if (bWasPlugin)
	{
		DropPendingStateUpdates(remoteUserID);
	}

	if (TheNetwork != nullptr)
	{
		TheNetwork->GetConnectionManager()->disconnectPlayer(remoteUserID);
	}
}

void NetworkMesh::EndStepAttempt(PlayerConnection& connection, const char* szReason, bool bReport)
{
	const int64_t userID = connection.m_userID;
	const int attempt = connection.m_step.attempt;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Attempt %d with %lld over %s ended before it connected: %s", attempt, userID, connection.m_step.transport.c_str(), szReason);

	// The attempt stops on this side, the hub starts the next step on both.
	connection.CloseTransport(k_szRestartCloseReason);
	connection.ForgetTransport();

	if (!bReport)
	{
		connection.m_State = EConnectionState::NOT_CONNECTED;
		return;
	}

	// The connection report of the attempt says it failed, the user interface keeps waiting for the next step.
	connection.m_State = EConnectionState::CONNECTION_FAILED;
	connection.m_timeline.Add(connection.m_State, false, NowSteadyMs() - connection.m_connectStartedMs);
	UpdateConnectivity(&connection);
	connection.m_State = EConnectionState::NOT_CONNECTED;

	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS)
	{
		pWS->SendData_RequestSignalling(userID, attempt);
	}
}

void NetworkMesh::RemoveConnection(int64_t userID)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	auto it = m_mapConnections.find(userID);
	if (it != m_mapConnections.end())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[ERASE 4] Removing user %lld", it->second.m_userID);
		m_mapConnections.erase(it);
	}
}

void NetworkMesh::PrepareManualRetry(int64_t userID, int64_t nowMs)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);

	auto it = m_mapConnections.find(userID);
	if (it == m_mapConnections.end())
	{
		it = m_mapConnections.emplace(userID, PlayerConnection(userID)).first;
	}
	PlayerConnection& target = it->second;
	target.m_userID = userID;
	target.m_SignallingAttempts = 0;
	target.m_retryTimeout = true;
	target.m_connectStartedMs = nowMs;
}

void NetworkMesh::DisconnectUser(int64_t remoteUserID)
{
	std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
	
	// A user that leaves the mesh gets none of the anti-cheat messages still held for it.
	m_heldACMessages.Clear(remoteUserID);

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Dumping all Steam connections");
	for (auto& kvPair : m_mapConnections)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Dumped steam connection, Handle %u User %lld (%lld)", kvPair.second.GetSteamConnection(), kvPair.second.m_userID, kvPair.first);
	}

	if (m_mapConnections.find(remoteUserID) != m_mapConnections.end())
	{

		PlayerConnection& connection = m_mapConnections[remoteUserID];
		if (connection.CanSend())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Closing connection %lld", remoteUserID);
			if (!connection.UsesPluginTransport())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[DC] Steam connection handle is %u", connection.GetSteamConnection());
			}

			connection.CloseTransport("Client Disconnecting Gracefully (Got DisconnectPeer from service)");
			if (TheNetwork != nullptr)
			{
				TheNetwork->GetConnectionManager()->disconnectPlayer(remoteUserID);
			}
		}


		if (TheGenOnlineGame && !TheGenOnlineGame->isGameInProgress())
		{
			for (auto it = m_mapConnections.begin(); it != m_mapConnections.end(); )
			{
				if (it->second.m_userID == remoteUserID)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[ERASE] Removing user %lld", it->second.m_userID);
					it = m_mapConnections.erase(it);
					break;
				}
				else
				{
					++it;
				}
			}
		}
	}
}

void NetworkMesh::Disconnect()
{
	if (m_bDisconnected)
		return;

	m_bDisconnected = true;

	EndMatchKeys();
	m_matchKeys.End();
	m_keySync.clear();

    for (auto& connectionData : m_mapConnections)
    {
		connectionData.second.Close();
    }

    m_mapConnections.clear();
	{
		std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
		m_heldACMessages.ClearAll();
	}

	if (m_bUsedPluginTransport)
	{
		GenOnlinePluginHost::Transport::DisconnectAll();
	}

	// The listen socket is created unconditionally in the constructor, so it must
	// be released unconditionally too. Skipping this when the anti-cheat plugin
	// owns the transport leaks the socket, and the next NetworkMesh then fails
	// with "Already have a listen socket on P2P vport 0" - which makes
	// IsInitialized() false and aborts the second lobby join.
	m_gns.CloseListenSocket();
}

void NetworkMesh::Tick()
{
	// state reported from anticheat plugin threads; UpdateState reaches UI callbacks, so apply it here
	std::vector<std::pair<int64_t, EConnectionState>> vecStateUpdates;
	{
		std::lock_guard<std::mutex> lock(m_pendingStateUpdatesMutex);
		vecStateUpdates.swap(m_vecPendingStateUpdates);
	}
	for (const auto& update : vecStateUpdates)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
		auto it = m_mapConnections.find(update.first);
		if (it != m_mapConnections.end())
		{
			PlayerConnection& connection = it->second;

			// A plugin state only describes a pair on the plugin transport.
			if (!connection.UsesPluginTransport())
			{
				continue;
			}

			// The attempt of a timed step that this side already ended takes no more states.
			if (connection.m_stepWatch.IsReported())
			{
				continue;
			}

			const bool bEnded = update.second == EConnectionState::CONNECTION_FAILED || update.second == EConnectionState::CONNECTION_DISCONNECTED;
			if (bEnded && connection.m_stepWatch.IsOpen())
			{
				EndStepAttempt(connection, "the plugin connection ended", connection.m_stepWatch.TakeFailure());
				continue;
			}

			connection.UpdateState(update.second, this);
		}
	}

	m_gns.Poll();

	const int64_t nowMs = NowSteadyMs();

	// A timed step that runs out of time before it connects asks the hub for the next step.
	{
		std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
		for (auto& kvPair : m_mapConnections)
		{
			PlayerConnection& conn = kvPair.second;
			if (conn.m_stepWatch.TakeTimeout(nowMs))
			{
				EndStepAttempt(conn, "the step timed out", true);
			}
		}
	}

	// probe every connection
	for (auto& kvPair : m_mapConnections)
	{
		PlayerConnection& conn = kvPair.second;
		conn.TickProbe(nowMs);
	}

	TickLobbyKeys(nowMs);
	TickStartRunAhead(nowMs);

	// the game transport isn't created until the game begins, but we want to transfer AC packets in the lobby first, so consider this a liteupdate
	if (TheGenOnlineGame != nullptr && !TheGenOnlineGame->isGameInProgress())
	{
		for (auto& kvPair : m_mapConnections)
		{
			kvPair.second.LiteUpdateForAC();
		}

		// The plugin queue holds the packets of every plugin connection, so it drains once per tick.
		ReceivePluginPackets(k_pluginLitePacketsPerTick, [](PlayerConnection& connection, const uint8_t* data, uint32_t len, int64_t arrivalMs)
		{
			connection.DispatchLiteFrame(data, len, arrivalMs);
		});
	}
}

int NetworkMesh::ReceivePluginPackets(int maxPackets, const PluginPacketHandler& handler)
{
	return PluginPeerTransport::ReceiveShared(GenOnlinePluginHost::Transport::GetPeerTransportApi(), maxPackets,
		[&](int64_t senderUserID, const uint8_t* pData, uint32_t numBytes, int64_t arrivalMs)
		{
			std::lock_guard<std::recursive_mutex> lock(m_mapConnectionsMutex);
			auto it = m_mapConnections.find(senderUserID);
			if (it == m_mapConnections.end() || !it->second.UsesPluginTransport())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[PLUGIN PACKET] Dropping a packet of %u bytes from user %lld, the user has no plugin connection", numBytes, static_cast<long long>(senderUserID));
				return false;
			}

			handler(it->second, pData, numBytes, arrivalMs);
			return true;
		});
}

bool NetworkMesh::HasPluginPackets() const
{
	return PluginPeerTransport::HasSharedPackets(GenOnlinePluginHost::Transport::GetPeerTransportApi());
}

void PlayerConnection::LiteUpdateForAC()
{
	// The mesh drains the plugin queue for every plugin connection in NetworkMesh::Tick.
	if (!ReceivesPerConnection())
	{
		return;
	}

	ReceiveFrames(k_maxFramesPerReceive, [this](const uint8_t* data, uint32_t len, int64_t arrivalMs)
	{
		DispatchLiteFrame(data, len, arrivalMs);
	});
}

void PlayerConnection::DispatchLiteFrame(const uint8_t* data, size_t len, int64_t arrivalMs)
{
	// Check minimum packet size for AC header
	if (data == nullptr || len < sizeof(ENetworkChannel))
	{
		// Malformed AC packet - too small for header
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC PACKET] Dropping malformed AC packet - size %u is less than header size 3 from user %lld", static_cast<unsigned>(len), static_cast<long long>(m_userID));
		return;
	}

	if (!DispatchControlFrame(static_cast<ENetworkChannel>(data[0]), data + sizeof(ENetworkChannel), len - sizeof(ENetworkChannel), arrivalMs))
	{
		// not an AC packet, we dont care
		NetworkLog(ELogVerbosity::LOG_DEBUG, "[AC PACKET] Received NON AC message");
	}
}

bool PlayerConnection::DispatchControlFrame(ENetworkChannel channel, const uint8_t* payload, size_t len, int64_t arrivalMs)
{
	if (channel == ENetworkChannel::NETWORK_CHANNEL_AC)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC PACKET] Received AC message of size %u from user %lld", static_cast<unsigned>(len), static_cast<long long>(m_userID));
		GenOnlinePluginHost::Anticheat::MessageArrived(static_cast<uint32_t>(m_userID), payload, static_cast<uint32_t>(len));
		return true;
	}

	if (channel == ENetworkChannel::NETWORK_CHANNEL_KEYS)
	{
		HandleKeyMessage(payload, len);
		return true;
	}

	if (channel == ENetworkChannel::NETWORK_CHANNEL_START)
	{
		NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
		if (pMesh != nullptr)
		{
			pMesh->OnStartRunAheadMessage(m_userID, payload, len);
		}
		return true;
	}

	if (channel == ENetworkChannel::NETWORK_CHANNEL_PING)
	{
		HandleProbe(payload, len, arrivalMs);
		return true;
	}

	return false;
}

// Probe payload after the envelope, little endian: u16 seq, u16 echoSeq, u16 echoHoldMs
static constexpr size_t k_probePayloadSize = 6;

void PlayerConnection::ResetProbe()
{
	m_probeStats.reset();
	for (OutstandingProbe& probe : m_probeRing)
	{
		probe = OutstandingProbe();
	}
	m_nextProbeSeq = 0;
	m_lastPeerSeq = k_noProbeSeq;
	m_lastPeerSeqRecvMs = 0;
	m_nextProbeMs = 0;
}

void PlayerConnection::TickProbe(int64_t nowMs)
{
	int pingIntervalMs = 250;
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		const ServiceConfig::Connection& connection = GenOnlineServicesManager::GetInstance()->GetServiceConfig().connection;
		pingIntervalMs = (TheNetwork != nullptr) ? connection.probe_match_interval_ms : connection.probe_lobby_interval_ms;
	}

	// an echo rides on the next probe, so it is allowed one probe interval late
	const int64_t timeoutMs = (std::max)(static_cast<int64_t>(1000), static_cast<int64_t>(m_probeStats.getLatency() * 4.0f)) + pingIntervalMs;
	for (OutstandingProbe& probe : m_probeRing)
	{
		if (probe.seq != k_noProbeSeq && nowMs - probe.sendTimeMs > timeoutMs)
		{
			m_probeStats.noteRetry(static_cast<time_t>(nowMs));
			probe.seq = k_noProbeSeq;
		}
	}

	m_probeStats.update(static_cast<time_t>(nowMs));

	if (nowMs < m_nextProbeMs || m_State != EConnectionState::CONNECTED_DIRECT)
	{
		return;
	}

	m_nextProbeMs = nowMs + pingIntervalMs;

	if (!CanSend())
	{
		return;
	}

	const uint16_t seq = m_nextProbeSeq;
	m_nextProbeSeq = (m_nextProbeSeq == k_noProbeSeq - 1) ? 0 : static_cast<uint16_t>(m_nextProbeSeq + 1);

	uint16_t echoHoldMs = 0;
	if (m_lastPeerSeq != k_noProbeSeq)
	{
		echoHoldMs = static_cast<uint16_t>((std::min)(nowMs - m_lastPeerSeqRecvMs, static_cast<int64_t>(k_noProbeSeq - 1)));
	}

	BYTE packet[k_probePayloadSize];
	WriteU16(packet + 0, seq);
	WriteU16(packet + 2, m_lastPeerSeq);
	WriteU16(packet + 4, echoHoldMs);

	if (SendEnveloped(ENetworkChannel::NETWORK_CHANNEL_PING, packet, sizeof(packet), k_nSteamNetworkingSend_UnreliableNoNagle) != k_EResultOK)
	{
		return;
	}

	// the slot is still in use: that probe was never echoed in a full ring turn
	OutstandingProbe& slot = m_probeRing[seq % k_probeRingSize];
	if (slot.seq != k_noProbeSeq)
	{
		m_probeStats.noteRetry(static_cast<time_t>(nowMs));
	}
	slot.seq = seq;
	slot.sendTimeMs = nowMs;
}

void PlayerConnection::HandleKeyMessage(const uint8_t* data, size_t len)
{
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->OnKeyAnnouncement(m_userID, data, len);
	}
}

void PlayerConnection::HandleProbe(const uint8_t* payload, size_t len, int64_t arrivalMs)
{
	const size_t envelopeSize = (payload != nullptr) ? ConsumeEnvelope(payload, len) : 0;
	if (envelopeSize == 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] Dropping probe without a valid packet envelope from user %lld, size %zu", m_userID, len);
		return;
	}
	payload += envelopeSize;
	len -= envelopeSize;

	if (payload == nullptr || len < k_probePayloadSize)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] Dropping malformed probe of size %zu from user %lld", len, m_userID);
		return;
	}

	// the arrival time, a slow frame on either side is no network delay
	const int64_t nowMs = arrivalMs;
	const uint16_t peerSeq = ReadU16(payload);
	const uint16_t echoSeq = ReadU16(payload + 2);
	const uint16_t echoHoldMs = ReadU16(payload + 4);

	// a reordered older probe must not move the echo back
	if (peerSeq != k_noProbeSeq && (m_lastPeerSeq == k_noProbeSeq || static_cast<uint16_t>(peerSeq - m_lastPeerSeq) < 0x8000))
	{
		m_lastPeerSeq = peerSeq;
		m_lastPeerSeqRecvMs = nowMs;
	}

	if (echoSeq == k_noProbeSeq)
	{
		return;
	}

	OutstandingProbe& acked = m_probeRing[echoSeq % k_probeRingSize];
	if (acked.seq != echoSeq)
	{
		// already resolved or timed out
		return;
	}

	const int64_t rttMs = (std::max)(static_cast<int64_t>(0), nowMs - acked.sendTimeMs - echoHoldMs);
	m_probeStats.noteAcked(static_cast<Real>(rttMs), static_cast<time_t>(nowMs));
	acked.seq = k_noProbeSeq;

	// older un-echoed probes are dropped, not lost: packet feedback measures the loss
	for (OutstandingProbe& probe : m_probeRing)
	{
		if (probe.seq != k_noProbeSeq && static_cast<uint16_t>(echoSeq - probe.seq) < 0x8000)
		{
			probe.seq = k_noProbeSeq;
		}
	}
}

PlayerConnection::PlayerConnection(int64_t userID)
	: m_transport(std::make_unique<GnsPeerTransport>(userID, k_HSteamNetConnection_Invalid))
{
	m_userID = userID;
}

PlayerConnection::PlayerConnection(int64_t userID, HSteamNetConnection hSteamConnection)
	: m_transport(std::make_unique<GnsPeerTransport>(userID, hSteamConnection))
{
	m_userID = userID;
	
	// no connection yet
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM CONNECTION] Attaching connection %u to user %lld", hSteamConnection, userID);

	GnsPeerTransport::NameConnection(hSteamConnection, userID);

}

PlayerConnection::PlayerConnection(int64_t userID, const PluginPeerTransportApi& pluginApi, const char* szMiddlewareID)
	: m_transport(std::make_unique<PluginPeerTransport>(pluginApi, userID, szMiddlewareID))
{
    m_userID = userID;

    // no connection yet
    NetworkLog(ELogVerbosity::LOG_RELEASE, "[MIDDLEWARE CONNECTION] Attaching connection %s to user %lld", szMiddlewareID, userID);

}

bool PlayerConnection::LogInvalidDue()
{
	const int64_t nowMs = NowSteadyMs();
	if (nowMs < m_nextInvalidLogMs)
	{
		return false;
	}
	m_nextInvalidLogMs = nowMs + 5000;
	return true;
}

int PlayerConnection::SendGamePacket(void* pBuffer, uint32_t totalDataSize)
{
    if (totalDataSize == 0)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[GAME PACKET] Cannot send empty game packet to user %lld", m_userID);
        return -(int)k_EResultFail;
    }

    if (pBuffer == nullptr)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[GAME PACKET] Cannot send game packet with null buffer to user %lld", m_userID);
        return -(int)k_EResultFail;
    }

	if (!CanSend())
	{
		if (LogInvalidDue())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GAME PACKET] Cannot send game packet - connection is invalid for user %lld", m_userID);
		}
		return -(int)k_EResultFail;
	}

	ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();

	// datagram mode leaves loss repair to the game layer; the restart flag only applies to ISteamNetworkingMessages
	int sendFlags = k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession; // default from last patch
	if (serviceConf.send.IsDatagram())
	{
		sendFlags = k_nSteamNetworkingSend_UnreliableNoNagle;
	}

	NetworkLog(ELogVerbosity::LOG_DEBUG, "[GAME PACKET] Sending msg of size %ld to user %lld\n", totalDataSize, m_userID);
	const int r = SendEnveloped(ENetworkChannel::NETWORK_CHANNEL_GAME, static_cast<const uint8_t*>(pBuffer), totalDataSize, sendFlags);
	if (r != k_EResultOK)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[GAME PACKET] Failed to send, err code was %d", r);
		return -r;
	}

	return 0;

}


void PlayerConnection::SendACPacket(const void* pData, uint32_t dataLen)
{
    if (!CanSend())
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC PACKET] Cannot send AC packet - connection is invalid for user %ld", m_userID);
        return;
    }

    ENetworkChannel netChannel = ENetworkChannel::NETWORK_CHANNEL_AC;
    std::vector<BYTE> vecData;
    vecData.resize(dataLen + sizeof(ENetworkChannel));
    memcpy(vecData.data() + sizeof(ENetworkChannel), pData, dataLen);
    vecData[0] = (BYTE)netChannel;

    NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC PACKET] Sending AC msg of size %ld to user %ld\n", dataLen, m_userID);
    EResult r = static_cast<EResult>(SendFramed(vecData.data(), vecData.size(), k_nSteamNetworkingSend_Reliable));

    if (r != k_EResultOK)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC PACKET] Failed to send, err code was %d", r);
    }
}

void PlayerConnection::Close()
{
	m_transport->Close("Client Disconnecting Gracefully");
    
    if (TheNetwork != nullptr)
    {
        TheNetwork->GetConnectionManager()->disconnectPlayer(m_userID);
    }
}

void PlayerConnection::CloseTransport(const char* szReason)
{
	m_transport.CloseIfOpen(szReason);
}

void PlayerConnection::ForgetTransport()
{
	m_transport->Forget();
}

HSteamNetConnection PlayerConnection::GetSteamConnection() const
{
	if (!m_transport.Is(PeerTransportKind::Gns))
	{
		return k_HSteamNetConnection_Invalid;
	}

	return static_cast<const GnsPeerTransport*>(m_transport.Get())->GetHandle();
}

void PlayerConnection::SetSteamConnection(HSteamNetConnection hSteamConnection)
{
	if (m_transport.Is(PeerTransportKind::Gns))
	{
		static_cast<GnsPeerTransport*>(m_transport.Get())->SetHandle(hSteamConnection);
	}
}

bool PlayerConnection::IsIPV4()
{
	return m_transport->IsIPv4();
}


std::string PlayerConnection::GetStats()
{
	// lead with the probe numbers, or just the reported ping while there are no samples yet
	char szSummary[64] = { 0 };
	if (m_probeStats.getSampleCount() > 0)
	{
		snprintf(szSummary, sizeof(szSummary), "Lat %d j%.0f L%.0f%%\n", static_cast<int>(m_probeStats.getLatency()), m_probeStats.getJitter(), m_probeStats.getLossPercent());
	}
	else
	{
		snprintf(szSummary, sizeof(szSummary), "Lat %d\n", GetLatency());
	}

	std::string strDetail;
	if (!m_transport->GetDetailedStats(strDetail))
		return std::string(szSummary) + "(disconnected)";

	return std::string(szSummary) + strDetail;
}


bool PlayerConnection::UsesPluginTransport() const
{
	return m_transport.Is(PeerTransportKind::Plugin);
}

std::string PlayerConnection::GetConnectionType()
{
	// IsDirect() relies on relayed connections reporting "Relayed".
	return PeerRouteText(m_transport->GetRoute(m_State == EConnectionState::CONNECTED_DIRECT));
}

// The connection state as the route labels tell it apart.
static PeerLinkState PeerLinkStateOf(EConnectionState state)
{
	if (state == EConnectionState::CONNECTED_DIRECT)
	{
		return PeerLinkState::Connected;
	}
	if (state == EConnectionState::CONNECTING_DIRECT || state == EConnectionState::FINDING_ROUTE)
	{
		return PeerLinkState::Connecting;
	}
	return PeerLinkState::NotConnected;
}

std::string PlayerConnection::GetRouteLabel() const
{
	const PeerLinkState linkState = PeerLinkStateOf(m_State);
	if (linkState != PeerLinkState::Connected)
	{
		return PeerRouteLabel(GetTransportLabel(), PeerRoute::Unknown, linkState);
	}
	return PeerRouteLabel(GetTransportLabel(), m_transport->GetRoute(true), linkState);
}

std::string PlayerConnection::GetPathLabel() const
{
	const PeerLinkState linkState = PeerLinkStateOf(m_State);
	if (linkState != PeerLinkState::Connected)
	{
		return PeerRouteLabel(GetTransportLabel(), PeerRoute::Unknown, linkState);
	}
	const PeerRoute route = m_transport->GetRoute(true);
	return PeerPathLabel(GetTransportLabel(), m_transport->GetKind(), m_transport->IsIPv4(), route, linkState);
}

void PlayerConnection::UpdateState(EConnectionState newState, NetworkMesh* pOwningMesh)
{
	if (newState == EConnectionState::CONNECTED_DIRECT)
	{
		m_stepWatch.MarkConnected();
	}

	if (newState == EConnectionState::CONNECTED_DIRECT && m_State != EConnectionState::CONNECTED_DIRECT)
	{
		m_connectedSinceTime = std::chrono::steady_clock::now();

		m_rating = ConnectionRating::State();
		m_nextRatingMs = 0;
		ResetProbe();
	}

	m_State = newState;

	// A connected state asks the transport for the route, the other states need no route.
	const bool bRelayed = newState == EConnectionState::CONNECTED_DIRECT && !IsDirect();
	m_timeline.Add(newState, bRelayed, NowSteadyMs() - m_connectStartedMs);

	if (newState == EConnectionState::CONNECTED_DIRECT)
	{
		int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[MESH] Connected to user %lld in %lld ms (%s, signalling attempt %d)",
			m_userID, nowMs - m_connectStartedMs, GetConnectionType().c_str(), m_SignallingAttempts);

		m_SignallingAttempts = 0;

		// The anti-cheat messages held while the pair was not connected leave first.
		pOwningMesh->SendHeldACMessages(*this);
	}
	pOwningMesh->UpdateConnectivity(this);

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	std::wstring strDisplayName = L"Unknown User";
	auto currentLobby = pLobbyInterface->GetCurrentLobby();
	for (const auto& member : currentLobby.members)
	{
		if (member.user_id == m_userID)
		{
			strDisplayName = from_utf8(member.display_name);
			break;
		}
	}

	if (pOwningMesh->m_cbOnConnected != nullptr)
	{
		pOwningMesh->m_cbOnConnected(m_userID, strDisplayName, this);
	}
}

void PlayerConnection::SetDisconnected(bool bWasError, NetworkMesh* pOwningMesh, bool bIsRetrying)
{
	// TODO_PLUGIN
	if (bWasError)
	{
		if (bIsRetrying)
		{
			// A connected pair that drops and retries reports its attempt as disconnected before the next attempt starts.
			if (m_State == EConnectionState::CONNECTED_DIRECT)
			{
				m_State = EConnectionState::CONNECTION_DISCONNECTED;
				m_timeline.Add(m_State, false, NowSteadyMs() - m_connectStartedMs);
				pOwningMesh->UpdateConnectivity(this);
			}
			m_State = EConnectionState::NOT_CONNECTED;
		}
		else
		{
			m_State = EConnectionState::CONNECTION_FAILED;
		}
	}
	else
	{
		m_State = EConnectionState::CONNECTION_DISCONNECTED;
	}

	m_connectedSinceTime = (std::chrono::steady_clock::time_point::min)();

	// Save values we need after the callback: the callback can erase this
	// PlayerConnection from the mesh's map (UAF), so we must not access
	// member variables after UpdateState fires the external callback.
	const HSteamNetConnection savedHandle = GetSteamConnection();
	const int64_t savedUserID = m_userID;

	// Invalidate the handle before firing the callback so any re-entrant
	// query sees the connection as already gone.
	ForgetTransport();

	// Dont update backend until we're actually done
	if (!bIsRetrying)
	{
		UpdateState(m_State, pOwningMesh);  // may erase *this from the map
	}

	// Use saved stack values � do NOT touch any member after this point.
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM CONNECTION] Setting connection %u to disconnected/invalid on user %lld", savedHandle, savedUserID);
	GnsPeerTransport::NameConnection(savedHandle, savedUserID);
}

int PlayerConnection::GetLatency()
{
	return m_transport->GetLatency();
}

size_t PlayerConnection::ConsumeEnvelope(const uint8_t* data, size_t len)
{
	const int64_t nowMs = NowSteadyMs();
	m_lastPacketMs = nowMs;
	return m_feedback.ReadEnvelope(data, len, nowMs);
}

int PlayerConnection::SendEnveloped(ENetworkChannel channel, const uint8_t* payload, size_t len, int sendFlags)
{
	const int64_t nowMs = NowSteadyMs();

	std::vector<BYTE> vecData;
	vecData.resize(sizeof(ENetworkChannel) + PacketFeedback::ENVELOPE_MAX_SIZE + len);
	vecData[0] = static_cast<BYTE>(channel);
	const size_t envelopeSize = m_feedback.WriteEnvelope(vecData.data() + sizeof(ENetworkChannel), nowMs);
	memcpy(vecData.data() + sizeof(ENetworkChannel) + envelopeSize, payload, len);
	vecData.resize(sizeof(ENetworkChannel) + envelopeSize + len);

	EResult r = static_cast<EResult>(SendFramed(vecData.data(), vecData.size(), sendFlags));
	if (r != k_EResultOK)
	{
		// once more with the same packet, it keeps its sequence number
		r = static_cast<EResult>(SendFramed(vecData.data(), vecData.size(), sendFlags));
	}
	if (r != k_EResultOK)
	{
		m_feedback.CancelEnvelope();
	}
	return static_cast<int>(r);
}

int PlayerConnection::SendFramed(const uint8_t* data, size_t len, int sendFlags)
{
	return m_transport->SendFramed(data, len, sendFlags);
}

bool PlayerConnection::CanSend() const
{
	return m_transport->CanSend();
}

bool PlayerConnection::GetOutgoingLoss(int windowMs, PacketFeedback::Summary& summary)
{
	const int64_t nowMs = NowSteadyMs();
	summary = m_feedback.GetOutgoing(nowMs, windowMs);
	return summary.samples > 0;
}

bool PlayerConnection::GetIncomingLoss(int windowMs, PacketFeedback::Summary& summary)
{
	const int64_t nowMs = NowSteadyMs();
	summary = m_feedback.GetIncoming(nowMs, windowMs);
	return summary.samples > 0;
}

float PlayerConnection::GetSustainedLossPercent(int windowMs)
{
	// one report of a short window is no trend
	static constexpr int k_minFeedbackReports = 3;
	PacketFeedback::Summary outLoss;
	PacketFeedback::Summary inLoss;
	GetOutgoingLoss(windowMs, outLoss);
	GetIncomingLoss(windowMs, inLoss);
	if (outLoss.samples < k_minFeedbackReports && inLoss.samples < k_minFeedbackReports)
	{
		return -1.0f;
	}
	return (std::max)((outLoss.samples >= k_minFeedbackReports) ? outLoss.lossPercent : 0.0f, (inLoss.samples >= k_minFeedbackReports) ? inLoss.lossPercent : 0.0f);
}

int PlayerConnection::GetJitter()
{
	// RFC 3550 style: the mean deviation of consecutive probe round trips, smoothed with a gain of 1/16
	if (m_probeStats.getSampleCount() < k_seedMinSamples)
	{
		return -1;
	}

	return static_cast<int>(m_probeStats.getJitter());
}

int PlayerConnection::GetMatchRatingMinSamples() const
{
	int intervalMs = 1000;
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		intervalMs = (std::max)(1, GenOnlineServicesManager::GetInstance()->GetServiceConfig().connection.probe_match_interval_ms);
	}
	// the window holds about W / interval probes, 10 at 1 s: the probe value stays until more than half of them are lost
	return (std::min)(k_seedMinSamples, (std::max)(3, NETWORK_STATS_WINDOW_MS / (2 * intervalMs)));
}

int PlayerConnection::ComputeConnectionRating()
{
	// the overlay and the lobby ask every frame, the percentile sorts inside do not change that fast
	const int64_t ratingNowMs = NowSteadyMs();
	if (ratingNowMs < m_nextRatingMs)
	{
		return m_ratingResult;
	}
	m_nextRatingMs = ratingNowMs + 250;
	m_ratingResult = UpdateConnectionRating();
	return m_ratingResult;
}

int PlayerConnection::UpdateConnectionRating()
{
	static constexpr std::chrono::milliseconds k_warmupDuration{ 1000 };
	if (m_connectedSinceTime == (std::chrono::steady_clock::time_point::min)() ||
		std::chrono::steady_clock::now() - m_connectedSinceTime < k_warmupDuration)
	{
		return -1;
	}

	if (GenOnlineServicesManager::GetInstance() == nullptr)
	{
		return -1;
	}

	// the command connections are indexed by game slot, find the slot of this user
	int slot = -1;
	const ConnectionStats* pCmd = nullptr;
	if (TheNetwork != nullptr && TheGenOnlineGame != nullptr)
	{
		slot = TheGenOnlineGame->getSlotOfUser(m_userID);
		pCmd = TheNetwork->getConnectionStats(slot);
	}

	ConnectionRating::Inputs inputs;
	NetworkOverlayInfo info;

	float feedbackLoss = -1.0f;
	static constexpr int k_minCmdSamples = 20;
	const bool inMatch = (pCmd != nullptr && pCmd->getSampleCount() >= k_minCmdSamples);
	const int feedbackWindowMs = inMatch ? NETWORK_STATS_WINDOW_MS : 60000;
	PacketFeedback::Summary outLoss;
	PacketFeedback::Summary inLoss;
	const bool hasOut = GetOutgoingLoss(feedbackWindowMs, outLoss);
	const bool hasIn = GetIncomingLoss(feedbackWindowMs, inLoss);
	if (hasOut || hasIn)
	{
		feedbackLoss = (std::max)(hasOut ? outLoss.lossPercent : 0.0f, hasIn ? inLoss.lossPercent : 0.0f);
	}
	const float sustainedLoss = GetSustainedLossPercent(feedbackWindowMs);

	ConnectionManager *commandManager = TheNetwork != nullptr ? TheNetwork->GetConnectionManager() : nullptr;
	const ServiceConfig &lobbyConfig = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
	const NetworkSettings::Send &ratingSend = commandManager != nullptr
		? commandManager->getNetworkSettings().send : lobbyConfig.send;
	const NetworkSettings::InputDelay &ratingInputDelay = commandManager != nullptr
		? commandManager->getNetworkSettings().input_delay : lobbyConfig.input_delay;
	// the state of this link, a pair that fell back to the packet router is rated with the longer path
	const Bool oneWay = commandManager != nullptr ? commandManager->usesOneWayRunAheadForUser(m_userID)
		: lobbyConfig.input_delay.controller && lobbyConfig.send.direct && m_State == EConnectionState::CONNECTED_DIRECT;

	if (inMatch && TheNetwork->getOverlayInfo(slot, info))
	{
		// probe p75, else the smoothed command round trip
		const int seedLatency = GetSeedLatency(GetMatchRatingMinSamples());
		const Real roundTripMs = (seedLatency >= 0) ? (Real)seedLatency : pCmd->getLatency();
		inputs = ConnectionRating::InputsFromMatch(*pCmd, info, feedbackLoss, ConnectionManager::estimateRunAheadMs(roundTripMs, ratingSend, ratingInputDelay, oneWay));
	}
	else if (GetSeedLatency() >= 0)
	{
		inputs = ConnectionRating::InputsFromProbe(m_probeStats, feedbackLoss, ConnectionManager::estimateRunAheadMs((Real)GetSeedLatency(), ratingSend, ratingInputDelay, oneWay));
	}
	else
	{
		// a gap in the samples keeps the last rating, the indicator must not blink out
		return (m_rating.bars > 0) ? m_rating.bars : -1;
	}

	inputs.lossPercent = sustainedLoss;
	if (m_lastPacketMs > 0)
	{
		const int64_t nowSteadyMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		inputs.silentMs = static_cast<Int>((std::min)(nowSteadyMs - m_lastPacketMs, static_cast<int64_t>(3600000)));
	}
	{
		// silence must outlast the probe interval of the current phase, probes may be the only traffic
		const ServiceConfig::Connection& pingConfig = lobbyConfig.connection;
		const int pingIntervalMs = (TheNetwork != nullptr) ? pingConfig.probe_match_interval_ms : pingConfig.probe_lobby_interval_ms;
		inputs.silentLimitMs = (std::max)(inputs.silentLimitMs, pingIntervalMs * 5 / 2);
	}

	ConnectionRating::Update(m_rating, inputs, timeGetTime());
	return m_rating.bars;
}
