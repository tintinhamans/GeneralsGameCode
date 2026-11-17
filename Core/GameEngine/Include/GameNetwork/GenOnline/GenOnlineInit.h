#pragma once
#include "ServiceConnectionPolicy.h"

#include "GameNetwork/NetworkSettings.h"
#include "GenOnlineInclude.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeHubSession.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"
#include "MeshConnectionTimeoutPolicy.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <memory>
#include <chrono>

class HTTPManager;

// steady clock, so deadlines do not move with system clock changes
inline int64_t NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class GenOnlineAuthInterface;
class GenOnlineLobbyInterface;
class GenOnlineRoomsInterface;
class GenOnlineStatsInterface;
class GenOnlineMatchmakingInterface;
class GenOnlineSocialInterface;

class NetworkMesh;

enum class EScreenshotType : int
{
    SCREENSHOT_TYPE_LOADSCREEN = 0,
    SCREENSHOT_TYPE_GAMEPLAY = 1,
    SCREENSHOT_TYPE_SCORESCREEN = 2
};

struct S3ScreenshotEntry
{
    std::vector<uint8_t> vecBytes;
    std::string strSignedURI;
	EScreenshotType screenshotType;
	std::chrono::steady_clock::time_point notBefore; // upload is held back until this time

    bool operator==(const S3ScreenshotEntry& other) const
	{
		return (vecBytes == other.vecBytes && strSignedURI == other.strSignedURI && screenshotType == other.screenshotType);
    }
};

#include <mutex>
#include <atomic>

#include <curl/curl.h>
#include <sentry.h>
#include <chrono>
#include "GenOnlineSettings.h"
#include "GameClient/DisplayStringManager.h"
#include "Common/GameEngine.h"

enum class EQoSRegions
{
	UNKNOWN = -1,
	WestUS = 0,
	CentralUS = 1,
	WestEurope = 2, 
	SouthCentralUS = 3,
	NorthEurope = 4,
	NorthCentralUS = 5,
	EastUS = 6,
	BrazilSouth = 7,
	AustraliaEast = 8,
	JapanWest = 9,
	AustraliaSoutheast = 10,
	EastAsia = 11,
	JapanEast = 12,
	SoutheastAsia = 13,
	SouthAfricaNorth = 14,
	UaeNorth = 15
};

enum class EGOTearDownReason
{
	UNKNOWN = -1,
	LOST_CONNECTION = 0,
	USER_LOGOUT = 1,
	USER_REQUESTED_SILENT = 2,
	AUTH_FAILED = 3,
	MODERATION_BAN = 4,
	MODERATION_KICK = 5,
	MIDDLEWARE_LOGIN_FAILED = 6
};

constexpr bool IsModerationTeardownReason(EGOTearDownReason reason) noexcept
{
	return reason == EGOTearDownReason::MODERATION_BAN || reason == EGOTearDownReason::MODERATION_KICK;
}

class WebSocket
{
public:
	WebSocket();
	~WebSocket();

	// The hub session of this socket.
	GenOnlineRealtime::HubSession* GetHubSession()
	{
		return m_pHubSession.get();
	}

	// Calls a hub method that answers with a Result. Without a hub session the callback reports a transport error.
	// A standby Result sends the call again later without its callback, see StandbyHubCalls.
	template <class R>
	void InvokeHub(const R& request, std::function<void(const GenOnlineRealtime::InvokeOutcome&)> callback)
	{
		static_assert(R::EXPECTS_RESULT, "use FireHub for methods without a Result");
		GenOnlineRealtime::ParkedHubCall call;
		call.method = R::METHOD;
		call.request = nlohmann::json(request);
		call.callback = callback;
		call.fnWanted = HubCallWanted(call.method, call.request);
		SendHubCall(call);
	}

	// Sends a fire-and-forget hub method. Returns false without a ready hub session.
	template <class R>
	bool FireHub(const R& request)
	{
		return m_pHubSession != nullptr && m_pHubSession->Fire(request, HubNowMs());
	}

	static int64_t HubNowMs();
	void Connect(const char* url, std::function<void(void)> fnWebsocketConnectedCallback);
	void Disconnect();

	bool IsConnected()
	{
		return m_bConnected;
	}

	// A reconnect the player is told about. A handover reconnect stays hidden for its first 3 seconds.
	bool IsReconnectShown() const
	{
		return m_bReconnecting && !m_bSwitchReconnect && !m_bStandbyReconnect;
	}

	std::function<void(void)> m_fnWebsocketConnectedCallback = nullptr;

	void Shutdown();

	void SendData_ChangeName(UnicodeString& strNewName);
	void SendData_RoomChatMessage(UnicodeString& msg, bool bIsAction);
	void SendData_FriendMessage(UnicodeString& msg, int64_t target_user_id);
	void SendData_LobbyChatMessage(UnicodeString& msg, bool bIsAction, bool bIsAnnouncement, bool bShowAnnouncementToHost);
	void SendData_JoinNetworkRoom(int roomID);
	void SendData_LeaveNetworkRoom();
	void SendData_MarkReady(bool bReady);

	void SendData_RequestSignalling(int64_t targetUserID);
	// Reports the attempt of the pair as failed, which moves the pair to the next step of its connection order.
	void SendData_RequestSignalling(int64_t targetUserID, int failedAttempt);
	void SendData_Signalling(int64_t targetUserID, std::vector<uint8_t> vecPayload);
	void SendData_StartGame();

	void SendData_ChangeLobbyPassword(UnicodeString& strNewPassword);
	void SendData_RemoveLobbyPassword();


	// Asks the service to tell the players that the host wants to start.
	void SendData_HostStartIntent();

	void Tick();

	// Continues the initial connect after the proxy login prompt closed.
	void ResumeAfterProxyLogin(bool loggedIn);

	// An inbound P2P signal and the user the service says it came from. The service sets the sender itself
	// from the authenticated websocket session, the payload is whatever the peer wrote.
	struct PendingSignal
	{
		int64_t senderUserID = -1;
		std::vector<uint8_t> payload;
	};

	// Thread-safe queue of inbound P2P signal payloads; drained by CSignalingClient::Poll().
	void PushPendingSignal(int64_t senderUserID, std::vector<uint8_t> payload)
	{
		std::scoped_lock<std::mutex> lock(m_pendingSignalsMutex);

		static constexpr size_t kMaxPendingSignals = 256;
		if (m_pendingSignals.size() >= kMaxPendingSignals)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Pending signal queue full (%zu), discarding oldest signal", m_pendingSignals.size());
			m_pendingSignals.pop();
		}

		PendingSignal signal;
		signal.senderUserID = senderUserID;
		signal.payload = std::move(payload);
		m_pendingSignals.push(std::move(signal));
	}

	// Drops queued signals so they never reach the next lobby.
	void ClearPendingSignals()
	{
		std::scoped_lock<std::mutex> lock(m_pendingSignalsMutex);
		m_pendingSignals = std::queue<PendingSignal>();
	}

	std::queue<PendingSignal> DrainPendingSignals()
	{
		std::scoped_lock<std::mutex> lock(m_pendingSignalsMutex);
		std::queue<PendingSignal> drained;
		drained.swap(m_pendingSignals);
		return drained;
	}

private:
	bool IsDestinationTrusted(const std::string& url) const;
	void ReleaseConnection();
	void HubTransportLost(const char* reason);
	void OnHubReady();
	void OnHubDisconnected(GenOnlineRealtime::DisconnectAction action, const std::string& reason);
	void RecoverHubSession(const GenOnlineRealtime::SessionRecovery& recovery);

	// Peer networking steps of the hub events.
	void StartPeerSignalling(int64_t lobbyID, int64_t userID, uint16_t preferredPort, const std::string& middlewareID, const GenOnlineRealtime::ConnectionStep& step);
	// Tells the hub which plugin transports this client can use for peers, after every Ready and whenever the answer changes.
	void SendPeerTransports(bool bForce);
	void DisconnectPeer(int64_t lobbyID, int64_t userID);
	void CollectMeshConnectivity(std::vector<int64_t>& connected, std::vector<int64_t>& connecting);
	void RegisterPeerHandlers();

	// Sends one try of a hub call. A standby Result parks it in m_standbyHubCalls.
	void SendHubCall(const GenOnlineRealtime::ParkedHubCall& call);
	// Sends the parked hub calls that are due and drops the stale ones.
	void TickStandbyHubCalls();
	// The guard of a parked hub call. It holds while the signed in user and, for lobby methods, the lobby stay the same.
	static std::function<bool()> HubCallWanted(const std::string& method, const nlohmann::json& request);

	std::unique_ptr<GenOnlineRealtime::HubSession> m_pHubSession;
	// Hub calls that got a standby Result. Touched on the main thread only.
	GenOnlineRealtime::StandbyHubCalls m_standbyHubCalls;
	mutable GenOnlineRealtime::ReconnectPolicy m_hubReconnectPolicy;
	bool m_bHubReadyOnce = false;
	// One token refresh and session registration per connection that never reached Ready.
	bool m_bHubSessionRecoveryTried = false;
	// The plugin transport name that SetPeerTransports last told the hub, empty for an empty list, unset before the first one of this connection.
	std::optional<std::string> m_lastPeerTransports;

	CURLcode ReceiveOneFrame();
	void PollConnectionSetup();
	void StartResolvedConnection();
	void FailConnectionSetup();
	bool m_proxyPrompted = false;
	std::shared_ptr<ServiceConnection::ProxyResolution> m_proxyResolution;
	ServiceConnection::ResolvedProxyPolicy m_proxyPolicy;
	size_t m_proxyRoute = 0;
	int64_t m_proxySetupStarted = -1;
	CURL* m_pCurlWS = nullptr;
    CURLM* m_pMulti = nullptr;
	struct curl_slist* m_pHeaders = nullptr;

	bool m_bConnected = false;

	const int64_t m_reconnectAttemptTimeout = 15000;

	bool m_bReconnecting = false;
	int m_numReconnectAttempts = 0;
	int64_t m_reconnectStartTime = -1;
	int64_t m_nextReconnectAttempt = -1;
	int64_t m_reconnectAttemptStarted = -1; // -1 = none in flight
	// A handover reconnect after a server.switching close, hidden until SWITCH_HIDDEN_RECONNECT_MS passed.
	bool m_bSwitchReconnect = false;
	// When the server.switching close of the running reconnect came, -1 for any other reconnect.
	int64_t m_switchClosedAt = -1;
	// The service.unavailable refusals in a row of the realtime connection, which a standby service copy gives after a failover switch.
	GenOnlineRealtime::StandbyRetry m_upgradeStandby;
	// A reconnect after a standby refusal, hidden while the refusals in a row last less than STANDBY_RETRY_WINDOW_MS.
	bool m_bStandbyReconnect = false;

	void BeginReconnect();
	void BeginSwitchReconnect(int64_t delayMs);
	void BeginStandbyReconnect(int64_t delayMs);
	void StartReconnect(int64_t delayMs, bool bSwitch);
	void EndReconnect();
	void UpdateReconnect();
	int64_t ComputeReconnectDelay() const;

	std::string m_strWebsocketAddr;

	std::atomic<bool> m_bShuttingDown = false;

	// Outbound messages queued for Tick() to flush.
	std::mutex m_outboundQueueMutex;
	struct OutboundMessage
	{
		std::string payload;
		bool heartbeat;
	};
	std::vector<OutboundMessage> m_vecQueuedOutboungMsgs;
	size_t m_queuedOutboundBytes = 0;
	size_t m_outboundOffset = 0;
	bool m_heartbeatQueued = false;
	bool m_acceptOutbound = true;
	// Returns false when the message was rejected.
	bool QueueOutbound(std::string payload, bool heartbeat);
	void ClearOutbound();
	CURLcode FlushOutbound();

	std::mutex m_pendingSignalsMutex;
	std::queue<PendingSignal> m_pendingSignals;
};

enum class ERoomFlags : int
{
	ROOM_FLAGS_DEFAULT = 0,
	ROOM_FLAGS_SHOW_ALL_MATCHES = 1
};

class NetworkRoom
{
public:
	NetworkRoom(int roomID, std::string strRoomName, ERoomFlags roomFlags, int parentRoomID = -1)
	{
		m_RoomID = roomID;
		m_strRoomDisplayName.translate(AsciiString(strRoomName.c_str()));
		m_RoomFlags = roomFlags;
		m_ParentRoomID = parentRoomID;
	}

	~NetworkRoom()
	{

	}

	int GetRoomID() const { return m_RoomID; }
	const UnicodeString& GetRoomDisplayName() const { return m_strRoomDisplayName; }
	ERoomFlags GetRoomFlags() const { return m_RoomFlags; }
	int GetParentRoomID() const { return m_ParentRoomID; }

private:
	int m_RoomID;
	UnicodeString m_strRoomDisplayName;
	ERoomFlags m_RoomFlags = ERoomFlags::ROOM_FLAGS_DEFAULT;
	int m_ParentRoomID = -1;
};

struct RegionResponse
{
	std::string countryCode;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegionResponse, countryCode)
};

struct ServiceConfig
{
	// Peer connections: signalling, NAT traversal and latency probes
	struct Connection
	{
		bool relay_only = false;
		int ice_engine = 2;             // 0 library default, 1 native, 2 WebRTC, or the name
		bool use_mapped_port = true;
		bool retry_signalling = true;
		int reconnect_timeout_ms = MeshConnectionTimeoutPolicy::ReconnectTimeoutMs; // first negotiation keeps 10 s
		int probe_lobby_interval_ms = 250; // time between latency probes sent to each peer while not in a match
		int probe_match_interval_ms = 1000; // the same in a match, the command acks already measure the round trip
		bool wifi_low_latency = true;   // Wi-Fi media streaming mode while a match runs, stops background scans from stalling the game
	} connection;

	using Send = NetworkSettings::Send;
	using InputDelay = NetworkSettings::InputDelay;
	using Disconnect = NetworkSettings::Disconnect;
	Send send;
	InputDelay input_delay;
	Disconnect disconnect;

	struct Lobby
	{
		bool host_migration = false;
	} lobby;

	struct Telemetry
	{
		bool screenshot_probes = true;
		bool replay_upload = true;
		int screenshot_width = 960;
		int screenshot_height = 600;
		int screenshot_upload_bytes_per_sec = 131072; // 128 KB/s
		int screenshot_slot_offset_ms = 2000;
	} telemetry;
};

// Reads a present key into value and keeps the default otherwise
template <typename T>
inline void ReadServiceConfigValue(const nlohmann::json& j, const char* key, T& value)
{
	if (!j.is_object())
	{
		throw std::invalid_argument("Service config sections must be objects");
	}
	const auto it = j.find(key);
	if (it != j.end() && !it->is_null())
	{
		value = it->get<T>();
	}
}

// The ICE library by name or by number
inline void ReadIceEngine(const nlohmann::json& j, int& value)
{
	if (!j.is_object())
	{
		throw std::invalid_argument("Service config sections must be objects");
	}
	const auto it = j.find("ice_engine");
	if (it == j.end() || it->is_null())
	{
		return;
	}
	if (it->is_string())
	{
		const std::string name = it->get<std::string>();
		if (name == "default")
		{
			value = 0;
		}
		else if (name == "native")
		{
			value = 1;
		}
		else if (name == "webrtc")
		{
			value = 2;
		}
		return;
	}
	value = it->get<int>();
}

// A key the service sets replaces the compiled default, a missing key keeps it
inline void ReadServiceConfig(const nlohmann::json& j, ServiceConfig& c)
{
	if (j.contains("connection"))
	{
		const nlohmann::json& s = j["connection"];
		ReadServiceConfigValue(s, "relay_only", c.connection.relay_only);
		ReadIceEngine(s, c.connection.ice_engine);
		ReadServiceConfigValue(s, "use_mapped_port", c.connection.use_mapped_port);
		ReadServiceConfigValue(s, "retry_signalling", c.connection.retry_signalling);
		ReadServiceConfigValue(s, "reconnect_timeout_ms", c.connection.reconnect_timeout_ms);
		ReadServiceConfigValue(s, "probe_lobby_interval_ms", c.connection.probe_lobby_interval_ms);
		ReadServiceConfigValue(s, "probe_match_interval_ms", c.connection.probe_match_interval_ms);
		ReadServiceConfigValue(s, "wifi_low_latency", c.connection.wifi_low_latency);
	}
	if (j.contains("send"))
	{
		const nlohmann::json& s = j["send"];
		ReadServiceConfigValue(s, "mode", c.send.mode);
		ReadServiceConfigValue(s, "direct", c.send.direct);
		ReadServiceConfigValue(s, "frames_per_packet", c.send.frames_per_packet);
		ReadServiceConfigValue(s, "frames_per_packet_lossy", c.send.frames_per_packet_lossy);
		ReadServiceConfigValue(s, "flush_every_frame", c.send.flush_every_frame);
		ReadServiceConfigValue(s, "redundancy", c.send.redundancy);
		ReadServiceConfigValue(s, "redundancy_window_ms", c.send.redundancy_window_ms);
		ReadServiceConfigValue(s, "redundancy_burst_ms", c.send.redundancy_burst_ms);
		ReadServiceConfigValue(s, "fast_resend", c.send.fast_resend);
	}
	if (j.contains("input_delay"))
	{
		const nlohmann::json& s = j["input_delay"];
		ReadServiceConfigValue(s, "controller", c.input_delay.controller);
		ReadServiceConfigValue(s, "margin_frames", c.input_delay.margin_frames);
		ReadServiceConfigValue(s, "min_frames", c.input_delay.min_frames);
		ReadServiceConfigValue(s, "max_frames", c.input_delay.max_frames);
		ReadServiceConfigValue(s, "fast_recovery", c.input_delay.fast_recovery);
		ReadServiceConfigValue(s, "decision_interval_ms", c.input_delay.decision_interval_ms);
		ReadServiceConfigValue(s, "fps_history_samples", c.input_delay.fps_history_samples);
		ReadServiceConfigValue(s, "catch_up_frames", c.input_delay.catch_up_frames);
		ReadServiceConfigValue(s, "slack_percent", c.input_delay.slack_percent);
		if (s.contains("formula"))
		{
			const nlohmann::json& f = s["formula"];
			ReadServiceConfigValue(f, "latency_window_ms", c.input_delay.formula.latency_window_ms);
			ReadServiceConfigValue(f, "decrease_after_decisions", c.input_delay.formula.decrease_after_decisions);
			ReadServiceConfigValue(f, "repair_budget", c.input_delay.formula.repair_budget);
		}
	}
	if (j.contains("disconnect"))
	{
		const nlohmann::json& s = j["disconnect"];
		ReadServiceConfigValue(s, "freeze_before_screen_ms", c.disconnect.freeze_before_screen_ms);
		ReadServiceConfigValue(s, "remove_after_ms", c.disconnect.remove_after_ms);
	}
	if (j.contains("lobby"))
	{
		ReadServiceConfigValue(j["lobby"], "host_migration", c.lobby.host_migration);
	}
	if (j.contains("telemetry"))
	{
		const nlohmann::json& s = j["telemetry"];
		ReadServiceConfigValue(s, "screenshot_probes", c.telemetry.screenshot_probes);
		ReadServiceConfigValue(s, "replay_upload", c.telemetry.replay_upload);
		ReadServiceConfigValue(s, "screenshot_width", c.telemetry.screenshot_width);
		ReadServiceConfigValue(s, "screenshot_height", c.telemetry.screenshot_height);
		ReadServiceConfigValue(s, "screenshot_upload_bytes_per_sec", c.telemetry.screenshot_upload_bytes_per_sec);
		ReadServiceConfigValue(s, "screenshot_slot_offset_ms", c.telemetry.screenshot_slot_offset_ms);
	}
}

// Keeps values from the service inside the ranges the game can run with
inline void ClampServiceConfig(ServiceConfig& c)
{
	c.connection.reconnect_timeout_ms = MeshConnectionTimeoutPolicy::ClampReconnectTimeout(c.connection.reconnect_timeout_ms);
	c.connection.probe_lobby_interval_ms = std::clamp(c.connection.probe_lobby_interval_ms, 50, 5000);
	c.connection.probe_match_interval_ms = std::clamp(c.connection.probe_match_interval_ms, 50, 10000);
	c.send.redundancy_window_ms = std::clamp(c.send.redundancy_window_ms, 0, 5000);
	c.send.redundancy_burst_ms = std::clamp(c.send.redundancy_burst_ms, 0, 1000);
	c.send.frames_per_packet_lossy = std::clamp(c.send.frames_per_packet_lossy, 0, 8);

	c.input_delay.formula.decrease_after_decisions = std::clamp(c.input_delay.formula.decrease_after_decisions, 0, 20);
	c.input_delay.catch_up_frames = std::clamp(c.input_delay.catch_up_frames, 1, 4);
	c.input_delay.min_frames = std::clamp(c.input_delay.min_frames, 0, 30); // the start run ahead is at least 30, a higher floor would make configs disagree
	c.input_delay.max_frames = std::clamp(c.input_delay.max_frames, (std::max)(30, c.input_delay.min_frames), 128);
	c.input_delay.slack_percent = std::clamp(c.input_delay.slack_percent, 0, 200);
	c.input_delay.decision_interval_ms = std::clamp(c.input_delay.decision_interval_ms, 100, 60000);
	c.input_delay.fps_history_samples = std::clamp(c.input_delay.fps_history_samples, 1, 128);
	if (c.input_delay.formula.latency_window_ms > 0)
	{
		c.input_delay.formula.latency_window_ms = std::clamp(c.input_delay.formula.latency_window_ms, 30 * 1000 / LOGICFRAMES_PER_SECOND + 1, 60000);
	}
	c.send.frames_per_packet = std::clamp(c.send.frames_per_packet, 1, 16);
	c.input_delay.margin_frames = std::clamp(c.input_delay.margin_frames, 0, 10);

	c.disconnect.freeze_before_screen_ms = std::clamp(c.disconnect.freeze_before_screen_ms, 1000, 30000);
	c.disconnect.remove_after_ms = std::clamp(c.disconnect.remove_after_ms, 10000, 180000);

	c.telemetry.screenshot_width = std::clamp(c.telemetry.screenshot_width, 16, 4096);
	c.telemetry.screenshot_height = std::clamp(c.telemetry.screenshot_height, 16, 4096);
}

inline void from_json(const nlohmann::json& j, ServiceConfig& c)
{
	if (!j.is_object())
	{
		throw std::invalid_argument("Service config must be an object");
	}
	ReadServiceConfig(j, c);
	ClampServiceConfig(c);
}

class GenOnlineServicesManager
{
private:
	static GenOnlineServicesManager* m_pOnlineServicesManager;

public:

	static GenOnlineSettings Settings;

	

	GenOnlineServicesManager();
	
	enum EEnvironment
	{
		DEV,
		STAGE,
		LIVE
	};

#if defined(USE_STAGE_ENV)
	const static EEnvironment g_Environment = EEnvironment::STAGE;
	#pragma message ("Building for stage environment")
#elif defined(USE_DEBUG_ON_LIVE_SERVER)
	const static EEnvironment g_Environment = EEnvironment::LIVE;
#pragma message ("Building for live environment (Debug Client)")
#else
	#if defined(_DEBUG)
		const static EEnvironment g_Environment = EEnvironment::DEV;
		#pragma message ("Building for dev environment")
	#else
		const static EEnvironment g_Environment = EEnvironment::LIVE;
		#pragma message ("Building for live environment")
	#endif
#endif
	// The name of g_Environment: live, stage or dev.
	static const char* EnvironmentName();
	static std::string GetAPIEndpoint(const char* szEndpoint);

	static void CreateInstance()
	{
		std::scoped_lock<std::recursive_mutex> lock(m_singletonMutex);
		if (m_pOnlineServicesManager == nullptr)
		{
			m_pOnlineServicesManager = new GenOnlineServicesManager();
		}
	}

	static void DestroyInstance()
	{
		std::scoped_lock<std::recursive_mutex> lock(m_singletonMutex);
		if (m_pOnlineServicesManager != nullptr)
		{
			m_pOnlineServicesManager->Shutdown();

			delete m_pOnlineServicesManager;
			m_pOnlineServicesManager = nullptr;
		}
	}

	static void AttemptLoadSteam();

	void CommitReplay(AsciiString absoluteReplayPath);

	static std::recursive_mutex m_singletonMutex;
	
	static GenOnlineServicesManager* GetInstance()
	{
		std::scoped_lock<std::recursive_mutex> lock(m_singletonMutex);
		return m_pOnlineServicesManager;
	}

	static std::shared_ptr<WebSocket> GetWebSocket()
	{
		if (m_pOnlineServicesManager != nullptr)
		{
			return m_pOnlineServicesManager->Internal_GetWebSocket();
		}

		return nullptr;
	}

	template<typename T>
	static T* GetInterface()
	{
		// need the root mgr first
		if (m_pOnlineServicesManager != nullptr)
		{
			if constexpr (std::is_same<T, GenOnlineAuthInterface>::value)
			{
				return m_pOnlineServicesManager->m_pAuthInterface;
			}
			else if constexpr (std::is_same<T, GenOnlineLobbyInterface>::value)
			{
				return m_pOnlineServicesManager->m_pLobbyInterface;
			}
			else if constexpr (std::is_same<T, GenOnlineRoomsInterface>::value)
			{
				return m_pOnlineServicesManager->m_pRoomInterface;
			}
			else if constexpr (std::is_same<T, GenOnlineStatsInterface>::value)
			{
				return m_pOnlineServicesManager->m_pStatsInterface;
			}
			else if constexpr (std::is_same<T, GenOnlineMatchmakingInterface>::value)
			{
				return m_pOnlineServicesManager->m_pMatchmakingInterface;
			}
			else if constexpr (std::is_same<T, GenOnlineSocialInterface>::value)
			{
				return m_pOnlineServicesManager->m_pSocialInterface;
			}
		}

		return nullptr;
	}

	static std::thread::id g_MainThreadID;

	static NetworkMesh* GetNetworkMesh();

	void Shutdown();

	void WaitForScreenshotThreads();

	void GetAndParseServiceConfig(std::function<void(void)> cbOnDone);

	~GenOnlineServicesManager();

	void StartVersionCheck(std::function<void(bool bSuccess, bool bNeedsUpdate)> fnCallback);
	void StartVersionCheckOnSelectedRoute(std::function<void(bool, bool)> fnCallback);
	void EnsureServiceRoute(std::function<void(bool)> callback);
	// true when the last failed route check was a proxy login the player did not complete
	bool RouteFailedProxyAuth() const { return m_routeProxyAuthFailed; }
	static bool IsServiceURLTrusted(const std::string& url);

	std::shared_ptr<WebSocket> Internal_GetWebSocket() const { return m_pWebSocket; }
	HTTPManager* GetHTTPManager() const { return m_pHTTPManager; }

	void CancelUpdate();
	// TheSuperHackers @feature arcticdolphin 09/11/2026 A failure callback replaces the Update Failed message box, so the sign-in dialog can show it.
	void LaunchPatcher(std::function<void(void)> cbFailed = nullptr);
	void StartDownloadUpdate(std::function<void(void)> cb, std::function<void(void)> cbFailed = nullptr);
	void ContinueUpdate();

	static void CaptureScreenshot(bool bResizeForTransmit, std::function<void(std::vector<unsigned char>)> cbOnDataAvailable);
	static void CaptureScreenshotForProbe(EScreenshotType screenshotType, std::string strURI);

	// the network overlay: 0 off, 1 player view (F10), 2 dev view (Shift+F10)
	static int g_networkOverlayMode;
	static void ToggleNetworkOverlay(int mode) { g_networkOverlayMode = (g_networkOverlayMode == mode) ? 0 : mode; }
	static int GetNetworkOverlayMode() { return g_networkOverlayMode; }

	void OnLogin(ELoginResult loginResult, const char* szWSAddr, std::function<void(void)> fnWebsocketConnectedCallback);
	
	void Init();

	void Tick();

	void ProcessMOTD(const char* szMOTD)
	{
		m_strMOTD = std::string(szMOTD);
		// the welcome screen shows which custom server this is, ahead of anything the server says
		const ServiceConnectionSettings settings = Settings.Network_GetConnectionSettings();
		if (settings.serverMode == "custom")
		{
			m_strMOTD = "Custom server: " + settings.serverAddress.substr(settings.serverAddress.find("://") + 3) + "\n \n" + m_strMOTD;
		}
	}

	std::string& GetMOTD() { return m_strMOTD; }

	void SetPendingFullTeardown(EGOTearDownReason reason)
	{
		if (IsModerationTeardownReason(m_teardownReason) && !IsModerationTeardownReason(reason))
		{
			return;
		}

		m_bPendingFullTeardown = true;
		m_teardownReason = reason;
	}
	bool IsPendingFullTeardown() const { return m_bPendingFullTeardown; }
	EGOTearDownReason GetTeardownReason() const { return m_teardownReason; }
	void ConsumePendingFullTeardown() { m_bPendingFullTeardown = false; }

	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 Called when a new online attempt starts, so it never inherits the teardown of an attempt that was abandoned before reaching the online screens.
	void ClearPendingFullTeardown()
	{
		m_bPendingFullTeardown = false;
		m_teardownReason = EGOTearDownReason::UNKNOWN;
	}

	void ResetPendingFullTeardownReason() { m_teardownReason = EGOTearDownReason::UNKNOWN; }

	static void InitSentry();
	static void UpdateSentryUser();
	static void ShutdownSentry();
	// Returns TRUE between a successful InitSentry and ShutdownSentry. Every Sentry call outside InitSentry checks it first.
	static Bool IsSentryActive();
	// The game's unhandled exception filter hands a crash on to Sentry's crashpad filter after writing its own dumps.
	// Returns FALSE when there is no Sentry filter to hand it to, before InitSentry or after ShutdownSentry.
	static Bool ForwardCrashToSentry(struct _EXCEPTION_POINTERS* exceptionInfo, LONG* result);
	static std::string GetSentryRelease();
	static std::string GetSentryStatus();
	static void LogDisplayAdapter();
private:
		
		

		std::string GetPatcherDirectoryPath();

public:
	GenOnlineAuthInterface* m_pAuthInterface = nullptr;
	GenOnlineLobbyInterface* m_pLobbyInterface = nullptr;
	GenOnlineRoomsInterface* m_pRoomInterface = nullptr;
	GenOnlineStatsInterface* m_pStatsInterface = nullptr;
	GenOnlineMatchmakingInterface* m_pMatchmakingInterface = nullptr;
	GenOnlineSocialInterface* m_pSocialInterface = nullptr;

	ServiceConfig& GetServiceConfig() { return m_ServiceConfig; }

public:
	void CacheScreenshotBytes_StartMatch(std::vector<uint8_t> vecData)
	{
		std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);
        m_vecCachedScreenshotBytes_MatchStart = std::move(vecData);
	}

	void CacheScreenshotBytes_EndMatch(uint64_t matchID, std::vector<uint8_t> data);
	void CacheReplayBytes(uint64_t matchID, std::vector<uint8_t> data);
    void SetScreenshotS3URI_StartMatch(const char* szURI)
    {
        std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);
		m_strCachedScreenshot_MatchStart_S3URI = std::string(szURI);
    }

	void SetScreenshotS3URI_EndMatch(uint64_t matchID, std::string uri);
	void SetScreenshotS3URI_Replay(uint64_t matchID, std::string uri);

private:
	// NOTE: Accessed from multiple threads, dont access directly, use helpers above to lock
    std::string m_strCachedScreenshot_MatchStart_S3URI;

    // screenshots / replays that require caching
    std::vector<uint8_t> m_vecCachedScreenshotBytes_MatchStart;

	struct CachedMatchUpload
	{
		uint64_t dataMatchID = 0;
		uint64_t uriMatchID = 0;
		std::vector<uint8_t> bytes;
		std::string signedURI;
	};

	void CacheMatchUploadBytes(CachedMatchUpload& upload, uint64_t matchID, std::vector<uint8_t> data);
	void CacheMatchUploadURI(CachedMatchUpload& upload, uint64_t matchID, std::string uri);

	// Data and URLs may arrive independently. Keeping their shared match ID here
	// prevents a late response from being paired with media from another match.
	CachedMatchUpload m_cachedMatchEndUpload;
	CachedMatchUpload m_cachedReplayUpload;

	// main thread SS Upload
	static std::mutex m_ScreenshotMutex;

	// normal screenshots
	static std::vector<S3ScreenshotEntry> m_vecGuardedSSData;

	// Throttled uploads start one at a time so their capped rates do not add up.
	std::chrono::steady_clock::time_point m_nextThrottledUploadTime;

	// Screenshot thread management
	std::vector<std::thread*> m_vecScreenshotThreads;
	std::mutex m_mutexScreenshotThreads;

	void ProbeServiceRoute();
	void FinishServiceRoute(bool success);
	std::string m_serviceBase;
	std::vector<std::string> m_bootstrapRoutes;
	size_t m_bootstrapIndex = 0;
	std::vector<std::function<void(bool)>> m_routeCallbacks;
	std::chrono::steady_clock::time_point m_routeValidatedAt{};
	std::string m_routeSettingsKey;
	bool m_routeProxyPrompted = false;
	bool m_routeProxyAuthFailed = false;

	ServiceConfig m_ServiceConfig;

	HTTPManager* m_pHTTPManager = nullptr;

	std::shared_ptr<WebSocket> m_pWebSocket;

	std::string m_strMOTD;

	EGOTearDownReason m_teardownReason = EGOTearDownReason::UNKNOWN;
	bool m_bPendingFullTeardown = false;

	std::queue<std::string> m_vecFilesToDownload;
	std::queue<int64_t> m_vecFilesSizes;
	std::vector<std::string> m_vecFilesDownloaded;
	std::function<void(void)> m_updateCompleteCallback = nullptr;
	std::function<void(void)> m_updateFailedCallback = nullptr;
	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 Bumped by every start and cancel, so replies of a cancelled download are ignored.
	int m_updateAttempt = 0;
	mutable std::recursive_mutex m_updateCallbackMutex;

	std::string m_patcher_name;
	std::string m_patcher_path;
	int64_t m_patcher_size;
};

// Calls a hub method on the services websocket. Without a socket the callback reports a transport error.
template <class R>
void InvokeRealtimeHub(const R& request, std::function<void(const GenOnlineRealtime::InvokeOutcome&)> callback)
{
	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->InvokeHub(request, callback);
		return;
	}
	if (callback)
	{
		GenOnlineRealtime::InvokeOutcome outcome;
		outcome.transportError = "not connected";
		callback(outcome);
	}
}
