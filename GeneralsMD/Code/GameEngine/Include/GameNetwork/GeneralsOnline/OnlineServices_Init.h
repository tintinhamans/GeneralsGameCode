#pragma once

#include "NGMP_include.h"

#include <algorithm>
#include <thread>
#include <memory>

class HTTPManager;

class NGMP_OnlineServices_AuthInterface;
class NGMP_OnlineServices_LobbyInterface;
class NGMP_OnlineServices_RoomsInterface;
class NGMP_OnlineServices_StatsInterface;
class NGMP_OnlineServices_MatchmakingInterface;
class NGMP_OnlineServices_SocialInterface;

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
#include "GeneralsOnline_Settings.h"
#include "GameClient/DisplayStringManager.h"
#include "Common/GameEngine.h"

enum EWebSocketMessageID
{
	UNKNOWN = -1,
	NETWORK_ROOM_CHAT_FROM_CLIENT = 1,
	NETWORK_ROOM_CHAT_FROM_SERVER = 2,
	NETWORK_ROOM_CHANGE_ROOM = 3,
	NETWORK_ROOM_MEMBER_LIST_UPDATE = 4,
	NETWORK_ROOM_MARK_READY = 5,
	LOBBY_CURRENT_LOBBY_UPDATE = 6,
	NETWORK_ROOM_LOBBY_LIST_UPDATE = 7,
	ANTICHEAT_MESSAGE = 8,
	PLAYER_NAME_CHANGE = 9,
	LOBBY_ROOM_CHAT_FROM_CLIENT = 10,
	LOBBY_CHAT_FROM_SERVER = 11,
	NETWORK_SIGNAL = 12,
	START_GAME = 13,
	PING = 14,
	PONG = 15,
	PROBE = 16,
	NETWORK_CONNECTION_START_SIGNALLING = 17,
	NETWORK_CONNECTION_DISCONNECT_PLAYER = 18,
	NETWORK_CONNECTION_CLIENT_REQUEST_SIGNALLING = 19,
	MATCHMAKING_ACTION_JOIN_PREARRANGED_LOBBY = 20,
	MATCHMAKING_ACTION_START_GAME = 21,
	MATCHMAKING_MESSAGE = 22,
	START_GAME_COUNTDOWN_STARTED = 23,
	LOBBY_REMOVE_PASSWORD = 24,
	LOBBY_CHANGE_PASSWORD = 25,
	FULL_MESH_CONNECTIVITY_CHECK_HOST_REQUESTS_BEGIN = 26,
	FULL_MESH_CONNECTIVITY_CHECK_RESPONSE = 27,
	FULL_MESH_CONNECTIVITY_CHECK_RESPONSE_COMPLETE_TO_HOST = 28,
	SOCIAL_NEW_FRIEND_REQUEST = 29,
	SOCIAL_FRIEND_CHAT_MESSAGE_CLIENT_TO_SERVER = 30,
	SOCIAL_FRIEND_CHAT_MESSAGE_SERVER_TO_CLIENT = 31,
	SOCIAL_FRIEND_ONLINE_STATUS_CHANGED = 32,
	SOCIAL_SUBSCRIBE_REALTIME_UPDATES = 33,
	SOCIAL_UNSUBSCRIBE_REALTIME_UPDATES = 34,
	SOCIAL_FRIENDS_OVERALL_STATUS_UPDATE = 35,
	SOCIAL_FRIEND_FRIEND_REQUEST_ACCEPTED_BY_TARGET = 36,
	SOCIAL_FRIENDS_LIST_DIRTY = 37,
	SOCIAL_CANT_ADD_FRIEND_LIST_FULL = 38,
	PROBE_RESP = 39,
	AC_REGISTER_PLAYER = 40,
	AC_DEREGISTER_PLAYER = 41,
	WS_KEEPALIVE = 42,
	WS_KEEPALIVE_CLIENT = 43,
	MATCHMAKING_ACTION_REQUEUE = 44,
	MATCHMAKING_ACTION_SETUP_PROGRESS = 45,
	MODERATION_NOTICE = 46,
	MODERATION_COMMAND = 47,
	MODERATION_COMMAND_RESULT = 48
};

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
	void Connect(const char* url, bool bIsReconnect, std::function<void(void)> fnWebsocketConnectedCallback);
	void Disconnect();

	bool IsConnected()
	{
		return m_bConnected;
	}

	std::vector<char> m_vecWSPartialBuffer;

	std::function<void(void)> m_fnWebsocketConnectedCallback = nullptr;

	void Shutdown();

	void SendData_ChangeName(UnicodeString& strNewName);
	void SendData_RoomChatMessage(UnicodeString& msg, bool bIsAction);
	void SendData_FriendMessage(UnicodeString& msg, int64_t target_user_id);
	void SendData_LobbyChatMessage(UnicodeString& msg, bool bIsAction, bool bIsAnnouncement, bool bShowAnnouncementToHost);
	void SendData_JoinNetworkRoom(int roomID, uint64_t requestID = 0);
	void SendData_LeaveNetworkRoom();
	void SendData_MarkReady(bool bReady);

	void SendData_RequestSignalling(int64_t targetUserID);
	void SendData_Signalling(int64_t targetUserID, std::vector<uint8_t> vecPayload);
	void SendData_StartGame();

	void SendData_ACMessage(int64_t targetUserID, std::vector<uint8_t> vecPayload);

	void SendData_ChangeLobbyPassword(UnicodeString& strNewPassword);
	void SendData_RemoveLobbyPassword();

	void SendData_SubscribeRealtimeUpdates();
	void SendData_UnsubscribeRealtimeUpdates();


	void SendData_CountdownStarted();

	// params: fully connected, missing links, reason ("" on success, else e.g. "missing_connections",
	// "timeout", "member_left", "check_superseded")
	std::function<void(bool, std::list<std::pair<int64_t, int64_t>>, std::string)> m_cbOnConnectivityCheckComplete = nullptr;

	// lobby ID the check was started for; a stale reply (different lobby) is ignored
	int64_t m_connectivityCheckLobbyID = -1;

	void SendData_StartFullMeshConnectivityCheck(std::function<void(bool, std::list<std::pair<int64_t, int64_t>>, std::string)> cbOnConnectivityCheckComplete);

	void ClearConnectivityCheckCallback()
	{
		m_cbOnConnectivityCheckComplete = nullptr;
		m_connectivityCheckLobbyID = -1;
	}

	void Tick();

	int Ping();

	// Queues the message; Tick() flushes it on the main thread. Thread-safe.
	void Send(const char* message);

	// Thread-safe queue of inbound P2P signal payloads; drained by CSignalingClient::Poll().
	void PushPendingSignal(std::vector<uint8_t> payload)
	{
		std::scoped_lock<std::mutex> lock(m_pendingSignalsMutex);

		static constexpr size_t kMaxPendingSignals = 256;
		if (m_pendingSignals.size() >= kMaxPendingSignals)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Pending signal queue full (%zu), discarding oldest signal", m_pendingSignals.size());
			m_pendingSignals.pop();
		}

		m_pendingSignals.push(std::move(payload));
	}

	std::queue<std::vector<uint8_t>> DrainPendingSignals()
	{
		std::scoped_lock<std::mutex> lock(m_pendingSignalsMutex);
		std::queue<std::vector<uint8_t>> drained;
		drained.swap(m_pendingSignals);
		return drained;
	}

private:
	CURL* m_pCurlWS = nullptr;
    CURLM* m_pMulti = nullptr;
	struct curl_slist* m_pHeaders = nullptr;

	bool m_bConnected = false;

	// Menu window should not exceed the service's abandoned-session hold, else reconnects get 205.
	const int64_t m_reconnectMenuWindow = 30000;
	const int64_t m_reconnectBackoffBase = 2000;
	const int64_t m_reconnectBackoffCap = 10000;
	const int64_t m_reconnectAttemptTimeout = 15000;

	bool m_bReconnecting = false;
	int m_numReconnectAttempts = 0;
	int64_t m_reconnectStartTime = -1;
	int64_t m_nextReconnectAttempt = -1;
	int64_t m_reconnectAttemptStarted = -1; // -1 = none in flight
	bool m_bFreshSession = false; // 205: reconnect as a new session

	void BeginReconnect();
	void EndReconnect();
	void UpdateReconnect();
	int64_t ComputeReconnectDelay() const;

	std::string m_strWebsocketAddr;

	int64_t m_lastPong = -1;
	int64_t m_lastPing = -1;
	const int64_t m_timeBetweenUserPings = 1000;
	const int64_t m_timeForWSTimeout = 20000;

	std::atomic<bool> m_bShuttingDown = false;

	// Outbound messages queued for Tick() to flush.
	std::mutex m_outboundQueueMutex;
	std::vector<std::string> m_vecQueuedOutboungMsgs;

	std::mutex m_pendingSignalsMutex;
	std::queue<std::vector<uint8_t>> m_pendingSignals;
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

// Settings the service pushes to every client, grouped by what they affect. Defaults match
// what the service has been sending. The flat layout older services send is still read.
struct ServiceConfig
{
	// Peer mesh: signalling, NAT traversal, and the per-connection ping history
	struct Mesh
	{
		bool retry_signalling = true;
		bool use_mapped_port = true;
		bool relay_all_traffic = false;
		int ice_implementation = 2;       // 0 = library default, 1 = native, 2 = WebRTC
		bool use_new_networking = false;  // game traffic through the anticheat plugin's transport
		int latency_history_ms = 20000;   // window of ping samples kept per connection
	} mesh;

	// Game packets over the mesh
	struct Transport
	{
		bool flush_per_frame = true;
		int send_flags = -1;              // GNS send mode tried after a failed reliable send; -1 = none
	} transport;

	// Input delay, computed by the packet router from the latencies it sees
	struct RunAhead
	{
		// Every model averages the two highest latencies, the one-way delay of the two farthest
		// hops. 0 = in-band ping; 1 = higher of in-band and current mesh ping; 2 = higher of
		// in-band and the peak mesh ping in the history window; 3 = current mesh ping; 4 = peak
		// mesh ping. 10 + model takes the single highest round trip instead, as before.
		int latency_model = 2;
		int slack_percent = 30;
		int min_frames = 6;
		int update_interval_ms = 5000;
		int fps_history = 30;             // per-second FPS samples averaged
		int latency_history = 200;        // in-band ping samples averaged
		int frame_grouping_frames = 2;

		// Replaces slack_percent with a value that grows with the measured jitter
		struct JitterSlack
		{
			bool enabled = false;
			float min_slack = 0.35f;
			float max_slack = 1.0f;
			float min_slack_over_200ms = 0.4f;
			float max_slack_over_200ms = 1.0f;
			float min_slack_over_300ms = 0.4f;
			float max_slack_over_300ms = 1.0f;
		} jitter_slack;
	} run_ahead;

	struct Lobby
	{
		bool enable_host_migration = false;
	} lobby;

	struct Telemetry
	{
		bool do_probes = true;
		bool do_replay_upload = true;
		int screenshot_width = 960;
		int screenshot_height = 600;
	} telemetry;
};

// Reads key into value when present; a missing key keeps the default
template <typename T>
inline void ReadServiceConfigValue(const nlohmann::json& j, const char* key, T& value)
{
	const auto it = j.find(key);
	if (it != j.end() && !it->is_null())
	{
		value = it->get<T>();
	}
}

inline void ReadServiceConfig(const nlohmann::json& j, ServiceConfig& c)
{
	if (j.contains("mesh") || j.contains("run_ahead"))
	{
		if (j.contains("mesh"))
		{
			const nlohmann::json& m = j["mesh"];
			ReadServiceConfigValue(m, "retry_signalling", c.mesh.retry_signalling);
			ReadServiceConfigValue(m, "use_mapped_port", c.mesh.use_mapped_port);
			ReadServiceConfigValue(m, "relay_all_traffic", c.mesh.relay_all_traffic);
			ReadServiceConfigValue(m, "ice_implementation", c.mesh.ice_implementation);
			ReadServiceConfigValue(m, "use_new_networking", c.mesh.use_new_networking);
			ReadServiceConfigValue(m, "latency_history_ms", c.mesh.latency_history_ms);
		}
		if (j.contains("transport"))
		{
			const nlohmann::json& t = j["transport"];
			ReadServiceConfigValue(t, "flush_per_frame", c.transport.flush_per_frame);
			ReadServiceConfigValue(t, "send_flags", c.transport.send_flags);
		}
		if (j.contains("run_ahead"))
		{
			const nlohmann::json& r = j["run_ahead"];
			ReadServiceConfigValue(r, "latency_model", c.run_ahead.latency_model);
			ReadServiceConfigValue(r, "slack_percent", c.run_ahead.slack_percent);
			ReadServiceConfigValue(r, "min_frames", c.run_ahead.min_frames);
			ReadServiceConfigValue(r, "update_interval_ms", c.run_ahead.update_interval_ms);
			ReadServiceConfigValue(r, "fps_history", c.run_ahead.fps_history);
			ReadServiceConfigValue(r, "latency_history", c.run_ahead.latency_history);
			ReadServiceConfigValue(r, "frame_grouping_frames", c.run_ahead.frame_grouping_frames);
			if (r.contains("jitter_slack"))
			{
				const nlohmann::json& s = r["jitter_slack"];
				ReadServiceConfigValue(s, "enabled", c.run_ahead.jitter_slack.enabled);
				ReadServiceConfigValue(s, "min_slack", c.run_ahead.jitter_slack.min_slack);
				ReadServiceConfigValue(s, "max_slack", c.run_ahead.jitter_slack.max_slack);
				ReadServiceConfigValue(s, "min_slack_over_200ms", c.run_ahead.jitter_slack.min_slack_over_200ms);
				ReadServiceConfigValue(s, "max_slack_over_200ms", c.run_ahead.jitter_slack.max_slack_over_200ms);
				ReadServiceConfigValue(s, "min_slack_over_300ms", c.run_ahead.jitter_slack.min_slack_over_300ms);
				ReadServiceConfigValue(s, "max_slack_over_300ms", c.run_ahead.jitter_slack.max_slack_over_300ms);
			}
		}
		if (j.contains("lobby"))
		{
			ReadServiceConfigValue(j["lobby"], "enable_host_migration", c.lobby.enable_host_migration);
		}
		if (j.contains("telemetry"))
		{
			const nlohmann::json& t = j["telemetry"];
			ReadServiceConfigValue(t, "do_probes", c.telemetry.do_probes);
			ReadServiceConfigValue(t, "do_replay_upload", c.telemetry.do_replay_upload);
			ReadServiceConfigValue(t, "screenshot_width", c.telemetry.screenshot_width);
			ReadServiceConfigValue(t, "screenshot_height", c.telemetry.screenshot_height);
		}
		return;
	}

	// Flat layout from older services. use_default_config chose between the retail run ahead
	// profile and a custom one; map whichever was active onto the explicit settings.
	ReadServiceConfigValue(j, "retry_signalling", c.mesh.retry_signalling);
	ReadServiceConfigValue(j, "use_mapped_port", c.mesh.use_mapped_port);
	ReadServiceConfigValue(j, "relay_all_traffic", c.mesh.relay_all_traffic);
	ReadServiceConfigValue(j, "ice_implementation", c.mesh.ice_implementation);
	ReadServiceConfigValue(j, "use_new_networking", c.mesh.use_new_networking);
	ReadServiceConfigValue(j, "network_mesh_histogram_duration", c.mesh.latency_history_ms);
	ReadServiceConfigValue(j, "network_do_immediate_flush_per_frame", c.transport.flush_per_frame);
	ReadServiceConfigValue(j, "network_send_flags", c.transport.send_flags);
	ReadServiceConfigValue(j, "network_latency_logic_model", c.run_ahead.latency_model);

	bool useRetailProfile = true;
	ReadServiceConfigValue(j, "use_default_config", useRetailProfile);
	if (useRetailProfile)
	{
		ReadServiceConfigValue(j, "ra_slack_override_percent_in_default", c.run_ahead.slack_percent);
	}
	else
	{
		int updateFrames = 10;
		ReadServiceConfigValue(j, "ra_slack_percent", c.run_ahead.slack_percent);
		ReadServiceConfigValue(j, "min_run_ahead_frames", c.run_ahead.min_frames);
		ReadServiceConfigValue(j, "ra_update_frequency_frames", updateFrames);
		ReadServiceConfigValue(j, "frame_grouping_frames", c.run_ahead.frame_grouping_frames);
		c.run_ahead.update_interval_ms = updateFrames * 1000 / GENERALS_ONLINE_HIGH_FPS_LIMIT;
		c.run_ahead.fps_history = 10;
		c.run_ahead.latency_history = 10;
	}

	ReadServiceConfigValue(j, "ibra_ra_tweaks", c.run_ahead.jitter_slack.enabled);
	ReadServiceConfigValue(j, "ibra_minslack_default", c.run_ahead.jitter_slack.min_slack);
	ReadServiceConfigValue(j, "ibra_maxslack_default", c.run_ahead.jitter_slack.max_slack);
	ReadServiceConfigValue(j, "ibra_minslack_greaterthan200ms", c.run_ahead.jitter_slack.min_slack_over_200ms);
	ReadServiceConfigValue(j, "ibra_maxslack_greaterthan200ms", c.run_ahead.jitter_slack.max_slack_over_200ms);
	ReadServiceConfigValue(j, "ibra_minslack_greaterthan300ms", c.run_ahead.jitter_slack.min_slack_over_300ms);
	ReadServiceConfigValue(j, "ibra_maxslack_greaterthan300ms", c.run_ahead.jitter_slack.max_slack_over_300ms);

	ReadServiceConfigValue(j, "enable_host_migration", c.lobby.enable_host_migration);
	ReadServiceConfigValue(j, "do_probes", c.telemetry.do_probes);
	ReadServiceConfigValue(j, "do_replay_upload", c.telemetry.do_replay_upload);
	ReadServiceConfigValue(j, "screenshot_width", c.telemetry.screenshot_width);
	ReadServiceConfigValue(j, "screenshot_height", c.telemetry.screenshot_height);
}

// Keeps values from the service inside the ranges the game can run with
inline void ClampServiceConfig(ServiceConfig& c)
{
	c.mesh.latency_history_ms = (std::max)(c.mesh.latency_history_ms, 1000);

	c.run_ahead.min_frames = std::clamp(c.run_ahead.min_frames, 1, 64);
	c.run_ahead.slack_percent = std::clamp(c.run_ahead.slack_percent, 0, 200);
	c.run_ahead.update_interval_ms = (std::max)(c.run_ahead.update_interval_ms, 100);
	c.run_ahead.fps_history = (std::max)(c.run_ahead.fps_history, 1);
	c.run_ahead.latency_history = (std::max)(c.run_ahead.latency_history, 128); // FrameMetrics drops pending acks when the window is smaller than the frames in flight
	c.run_ahead.frame_grouping_frames = (std::max)(c.run_ahead.frame_grouping_frames, 1);

	ServiceConfig::RunAhead::JitterSlack& js = c.run_ahead.jitter_slack;
	js.min_slack = std::clamp(js.min_slack, 0.0f, 2.0f);
	js.max_slack = std::clamp(js.max_slack, 0.0f, 2.0f);
	js.min_slack_over_200ms = std::clamp(js.min_slack_over_200ms, 0.0f, 2.0f);
	js.max_slack_over_200ms = std::clamp(js.max_slack_over_200ms, 0.0f, 2.0f);
	js.min_slack_over_300ms = std::clamp(js.min_slack_over_300ms, 0.0f, 2.0f);
	js.max_slack_over_300ms = std::clamp(js.max_slack_over_300ms, 0.0f, 2.0f);

	c.telemetry.screenshot_width = (std::max)(c.telemetry.screenshot_width, 16);
	c.telemetry.screenshot_height = (std::max)(c.telemetry.screenshot_height, 16);
}

inline void from_json(const nlohmann::json& j, ServiceConfig& c)
{
	ReadServiceConfig(j, c);
	ClampServiceConfig(c);
}

class NGMP_OnlineServicesManager
{
private:
	static NGMP_OnlineServicesManager* m_pOnlineServicesManager;

public:

	static GenOnlineSettings Settings;

	

	NGMP_OnlineServicesManager();
	
	enum EEnvironment
	{
		DEV,
		TEST,
		PROD
	};

#if defined(USE_TEST_ENV)
	const static EEnvironment g_Environment = EEnvironment::TEST;
	#pragma message ("Building for TEST environment")
#elif defined(USE_DEBUG_ON_LIVE_SERVER)
	const static EEnvironment g_Environment = EEnvironment::PROD;
#pragma message ("Building for PROD environment (Debug Client)")
#else
	#if defined(_DEBUG)
		const static EEnvironment g_Environment = EEnvironment::DEV;
		#pragma message ("Building for DEV environment")
	#else
		const static EEnvironment g_Environment = EEnvironment::PROD;
		#pragma message ("Building for PROD environment")
	#endif
#endif
	static std::string GetAPIEndpoint(const char* szEndpoint);

	static void CreateInstance()
	{
		std::scoped_lock<std::recursive_mutex> lock(m_singletonMutex);
		if (m_pOnlineServicesManager == nullptr)
		{
			m_pOnlineServicesManager = new NGMP_OnlineServicesManager();
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
	
	static NGMP_OnlineServicesManager* GetInstance()
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
			if constexpr (std::is_same<T, NGMP_OnlineServices_AuthInterface>::value)
			{
				return m_pOnlineServicesManager->m_pAuthInterface;
			}
			else if constexpr (std::is_same<T, NGMP_OnlineServices_LobbyInterface>::value)
			{
				return m_pOnlineServicesManager->m_pLobbyInterface;
			}
			else if constexpr (std::is_same<T, NGMP_OnlineServices_RoomsInterface>::value)
			{
				return m_pOnlineServicesManager->m_pRoomInterface;
			}
			else if constexpr (std::is_same<T, NGMP_OnlineServices_StatsInterface>::value)
			{
				return m_pOnlineServicesManager->m_pStatsInterface;
			}
			else if constexpr (std::is_same<T, NGMP_OnlineServices_MatchmakingInterface>::value)
			{
				return m_pOnlineServicesManager->m_pMatchmakingInterface;
			}
			else if constexpr (std::is_same<T, NGMP_OnlineServices_SocialInterface>::value)
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

	~NGMP_OnlineServicesManager()
	{
		if (m_pAuthInterface != nullptr)
		{
			delete m_pAuthInterface;
			m_pAuthInterface = nullptr;
		}

		if (m_pStatsInterface != nullptr)
		{
			delete m_pStatsInterface;
			m_pStatsInterface = nullptr;
		}

		if (m_pLobbyInterface != nullptr)
		{
			delete m_pLobbyInterface;
			m_pLobbyInterface = nullptr;
		}

		if (m_pRoomInterface != nullptr)
		{
			delete m_pRoomInterface;
			m_pRoomInterface = nullptr;
		}

		if (m_pSocialInterface != nullptr)
		{
			delete m_pSocialInterface;
			m_pSocialInterface = nullptr;
		}

		if (m_pHTTPManager != nullptr)
		{
			delete m_pHTTPManager;
			m_pHTTPManager = nullptr;
		}

		// Reset shared_ptr, which will delete WebSocket only when all references are released
		m_pWebSocket.reset();
	}

	void StartVersionCheck(std::function<void(bool bSuccess, bool bNeedsUpdate)> fnCallback);

	std::shared_ptr<WebSocket> Internal_GetWebSocket() const { return m_pWebSocket; }
	HTTPManager* GetHTTPManager() const { return m_pHTTPManager; }

	void CancelUpdate();
	void LaunchPatcher();
	void StartDownloadUpdate(std::function<void(void)> cb);
	void ContinueUpdate();

	static void CaptureScreenshot(bool bResizeForTransmit, std::function<void(std::vector<unsigned char>)> cbOnDataAvailable);
	static void CaptureScreenshotForProbe(EScreenshotType screenshotType, std::string strURI);

	static bool g_bAdvancedNetworkStats;
	static void ToggleAdvancedNetworkStats() { g_bAdvancedNetworkStats = !g_bAdvancedNetworkStats; }
	static bool IsAdvancedNetworkStatsEnabled() { return g_bAdvancedNetworkStats; }

	void OnLogin(ELoginResult loginResult, const char* szWSAddr, std::function<void(void)> fnWebsocketConnectedCallback);
	
	void Init();

	void Tick();

	void ProcessMOTD(const char* szMOTD)
	{
		m_strMOTD = std::string(szMOTD);
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

	void ResetPendingFullTeardownReason() { m_teardownReason = EGOTearDownReason::UNKNOWN; }

	static void InitSentry();
	static void ShutdownSentry();
private:
		
		

		std::string GetPatcherDirectoryPath();

public:
	NGMP_OnlineServices_AuthInterface* m_pAuthInterface = nullptr;
	NGMP_OnlineServices_LobbyInterface* m_pLobbyInterface = nullptr;
	NGMP_OnlineServices_RoomsInterface* m_pRoomInterface = nullptr;
	NGMP_OnlineServices_StatsInterface* m_pStatsInterface = nullptr;
	NGMP_OnlineServices_MatchmakingInterface* m_pMatchmakingInterface = nullptr;
	NGMP_OnlineServices_SocialInterface* m_pSocialInterface = nullptr;

	ServiceConfig& GetServiceConfig() { return m_ServiceConfig; }

public:
	void CacheScreenshotBytes_StartMatch(std::vector<uint8_t>& vecData)
	{
		std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);
        m_vecCachedScreenshotBytes_MatchStart = vecData;
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

	// Screenshot thread management
	std::vector<std::thread*> m_vecScreenshotThreads;
	std::mutex m_mutexScreenshotThreads;

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
	mutable std::recursive_mutex m_updateCallbackMutex;

	std::string m_patcher_name;
	std::string m_patcher_path;
	int64_t m_patcher_size;
};
