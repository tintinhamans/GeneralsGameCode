#pragma once

#include "GenOnlineInclude.h"
#include "GenOnlineRoomsInterface.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeLobbyStart.h"
#include "GameNetwork/GameInfo.h"
#include <chrono>
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/VictoryConditions.h"
#include <atomic>
#include <optional>
#include <set>

extern GenOnlineGame* TheGenOnlineGame;

struct LobbyMemberEntry : public NetworkMemberBase
{

	uint16_t preferredPort;
	// NOTE: NetworkMemberBase is not deserialized

	int side = -1;
	int color = -1;
	int team = -1;
	int startpos = -1;
	bool has_map = false;

	// marks a member the service sent without a usable slot
	static constexpr uint16_t NO_SLOT_INDEX = UINT16_MAX;
	uint16_t m_SlotIndex = NO_SLOT_INDEX;
	uint16_t m_SlotState = SlotState::SLOT_OPEN;

	std::string region;
	std::string middlewareUserID;
	int latency = 0;

	bool HasValidSlot() const
	{
		return m_SlotIndex < MAX_SLOTS;
	}

	bool IsHuman() const
	{
		return user_id != -1 && m_SlotState == SlotState::SLOT_PLAYER;
	}
};

enum class ELobbyType
{
	UNKNOWN = -1,
	CustomGame = 0,
	QuickMatch = 1
};

struct LobbyEntry
{
	int64_t lobbyID = -1;

	int64_t owner = -1;
	std::string name;
	std::string map_name;
	std::string map_path;
	bool map_official = false;
	int current_players = 0;
	int max_players = 0;
	bool vanilla_teams = false;
	uint32_t starting_cash = 0;
	bool limit_superweapons = false;
	bool track_stats = false;
	bool allow_observers = false;
	uint16_t max_cam_height = 0;

	uint32_t exe_crc = 0;
	uint32_t ini_crc = 0;

	uint64_t match_id = 0;

	ELobbyType lobby_type = ELobbyType::UNKNOWN;

	int rng_seed = -1;

	bool passworded = false;
	std::string password;

	std::vector<LobbyMemberEntry> members;

	std::string region;
	int latency = 0;
};

enum class EJoinLobbyResult
{
	JoinLobbyResult_Success, // The room was joined.
	JoinLobbyResult_FullRoom,       // The room is full.
	JoinLobbyResult_BadPassword,    // An incorrect password (or none) was given for a passworded room.
	JoinLobbyResult_JoinFailed, // Generic failure.
	JoinLobbyResult_AnticheatMismatch // Anticheat mismatch
};

enum class ELobbyJoinability
{
	LobbyJoinability_Public,
	LobbyJoinability_FriendsOnly,
};

struct LobbyMemberEntry;
struct LobbyEntry;

struct LobbyEvent
{
	std::string event;
	int64_t duration_ms = 0;
	std::string reason;
	int64_t value = 0;
	bool friends_only = false;
	int64_t user_id = -1;
	std::string display_name;
	std::vector<std::pair<int64_t, int64_t>> missingLinks;
};

class GenOnlineLobbyInterface
{
public:
	GenOnlineLobbyInterface();
	void StopMatchStartCountdownIfRunning();
	void SearchForLobbies(std::function<void()> onStartCallback, std::function<void(std::vector<LobbyEntry>)> onCompleteCallback);
	void DeregisterForSearchForLobbiesCallback()
	{
		m_fnCallbackSearchForLobbiesComplete = nullptr;
		StopWatchingLobbyList();
	}
	std::function<void(std::vector<LobbyEntry>)> m_fnCallbackSearchForLobbiesComplete = nullptr;

	std::function<void(std::string)> m_fnCallbackMatchmakingMessage = nullptr;
	void RegisterForMatchmakingMessageCallback(std::function<void(std::string)> cb)
	{
		m_fnCallbackMatchmakingMessage = cb;
	}

	void DeregisterForMatchmakingMessageCallback()
	{
		m_fnCallbackMatchmakingMessage = nullptr;
	}

	void InvokeMatchmakingMessageCallback(std::string str)
	{
		if (m_fnCallbackMatchmakingMessage != nullptr)
		{
			m_fnCallbackMatchmakingMessage(str);
		}
	}

	void InvokeMatchmakingStartGameCallback()
	{
		if (m_fnCallbackMatchmakingStartGame != nullptr)
		{
			m_fnCallbackMatchmakingStartGame();
		}
	}

	std::function<void()> m_fnCallbackMatchmakingStartGame = nullptr;
	void RegisterForMatchmakingStartGameCallback(std::function<void()> cb)
	{
		m_fnCallbackMatchmakingStartGame = cb;
	}

	void DeregisterForMatchmakingStartGameCallback()
	{
		m_fnCallbackMatchmakingStartGame = nullptr;
	}

	std::function<void()> m_fnCallbackMatchmakingMatchFound = nullptr;
	void RegisterForMatchmakingMatchFoundCallback(std::function<void()> cb)
	{
		m_fnCallbackMatchmakingMatchFound = cb;
	}

	void DeRegisterForMatchmakingMatchFoundCallback()
	{
		m_fnCallbackMatchmakingMatchFound = nullptr;
	}

	void InvokeMatchmakingMatchFoundCallback()
	{
		if (m_fnCallbackMatchmakingMatchFound != nullptr)
		{
			m_fnCallbackMatchmakingMatchFound();
		}
	}

	std::function<void()> m_fnCallbackMatchmakingRequeue = nullptr;
	void RegisterForMatchmakingRequeueCallback(std::function<void()> cb)
	{
		m_fnCallbackMatchmakingRequeue = cb;
	}

	void DeregisterForMatchmakingRequeueCallback()
	{
		m_fnCallbackMatchmakingRequeue = nullptr;
	}

	void InvokeMatchmakingRequeueCallback()
	{
		if (m_fnCallbackMatchmakingRequeue != nullptr)
		{
			m_fnCallbackMatchmakingRequeue();
		}
	}

	// The service took the player out of the queue without a requeue.
	std::function<void()> m_fnCallbackMatchmakingLeft = nullptr;
	void RegisterForMatchmakingLeftCallback(std::function<void()> cb)
	{
		m_fnCallbackMatchmakingLeft = cb;
	}

	void DeregisterForMatchmakingLeftCallback()
	{
		m_fnCallbackMatchmakingLeft = nullptr;
	}

	void InvokeMatchmakingLeftCallback()
	{
		if (m_fnCallbackMatchmakingLeft != nullptr)
		{
			m_fnCallbackMatchmakingLeft();
		}
	}

	// timeoutMs: how long to wait for the next step; countdownMs: start countdown length, 0 while setting up, -1 if not sent
	std::function<void(int, int)> m_fnCallbackMatchmakingSetupProgress = nullptr;
	void RegisterForMatchmakingSetupProgressCallback(std::function<void(int, int)> cb)
	{
		m_fnCallbackMatchmakingSetupProgress = cb;
	}

	void DeregisterForMatchmakingSetupProgressCallback()
	{
		m_fnCallbackMatchmakingSetupProgress = nullptr;
	}

	void InvokeMatchmakingSetupProgressCallback(int timeoutMs, int countdownMs)
	{
		if (m_fnCallbackMatchmakingSetupProgress != nullptr)
		{
			m_fnCallbackMatchmakingSetupProgress(timeoutMs, countdownMs);
		}
	}
	

	// updates
	void UpdateCurrentLobby_Map(UnicodeString strMap, AsciiString strMapPath, bool bIsOfficial, int newMaxPlayers);
	void UpdateCurrentLobby_LimitSuperweapons(bool bLimitSuperweapons);
	void UpdateCurrentLobby_StartingCash(UnsignedInt startingCashValue);

	void UpdateCurrentLobby_HasMap();

	void UpdateCurrentLobby_MySide(int side, int updatedStartPos);
	void UpdateCurrentLobby_MyColor(int side);
	void UpdateCurrentLobby_MyStartPos(int side);
	void UpdateCurrentLobby_MyTeam(int side);

	// AI
	void UpdateCurrentLobby_AIColor(int slot, int color);
	void UpdateCurrentLobby_AISide(int slot, int side, int updatedStartPos);
	void UpdateCurrentLobby_AITeam(int slot, int team);
	void UpdateCurrentLobby_AIStartPos(int slot, int startpos);

	void UpdateCurrentLobbyMaxCameraHeight(uint16_t maxCameraHeight);

	void SetJoinability(ELobbyJoinability joinabilityFlag);

	void UpdateCurrentLobby_KickUser(int64_t userID, UnicodeString name);
	void UpdateCurrentLobby_SetSlotState(uint16_t slotIndex, uint16_t slotState);

	void SetLobbyListDirty()
	{
		m_bLobbyListDirty.store(true);
	}

	void ConsumeLobbyListDirtyFlag()
	{
		m_bLobbyListDirty.store(false);
	}

	bool IsLobbyListDirty() const
	{
		return m_bLobbyListDirty.load();
	}

	UnicodeString m_PendingCreation_LobbyName;
	UnicodeString m_PendingCreation_InitialMapDisplayName;
	AsciiString m_PendingCreation_InitialMapPath;
	void CreateLobby(UnicodeString strLobbyName, UnicodeString strInitialMapName, AsciiString strInitialMapPath, bool bIsOfficial, int initialMaxSize, bool bVanillaTeamsOnly, bool bTrackStats, uint32_t startingCash, bool bPassworded, std::string strPassword, bool bAllowObservers);

	void OnJoinedOrCreatedLobby(bool bAlreadyUpdatedDetails, std::function<void(bool)> fnCallback);

	UnicodeString GetCurrentLobbyDisplayName();
	UnicodeString GetCurrentLobbyMapDisplayName();
	AsciiString GetCurrentLobbyMapPath();

	void SendChatMessageToCurrentLobby(UnicodeString& strChatMsgUnicode, bool bIsAction);

	void InvokeCreateLobbyCallback(bool bSuccess)
	{
		if (m_cb_CreateLobbyPendingCallback != nullptr)
		{
			m_cb_CreateLobbyPendingCallback(bSuccess);
		}
	}


	bool AreLocalLobbyPeersConnected();
	bool HasFailedLobbyPeers();
	int RetryFailedLobbyPeers();

	LobbyEntry& GetCurrentLobby()
	{
		return m_CurrentLobby;
	}

	const LobbyEntry& GetCurrentLobby() const
	{
		return m_CurrentLobby;
	}

	// Changes with every join or create attempt and whenever the local lobby state is torn down.
	uint64_t GetLobbyJoinGeneration() const
	{
		return m_LobbyJoinGeneration;
	}

	GenOnlineGame* GetCurrentGame()
	{
		return TheGenOnlineGame;
	}

	// lobby roster
	std::function<void()> m_RosterNeedsRefreshCallback = nullptr;
	mutable std::mutex m_rosterCallbackMutex;
	void RegisterForRosterNeedsRefreshCallback(std::function<void()> cb)
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = cb;
	}

	void DeregisterForRosterNeedsRefreshCallback()
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = nullptr;
	}

	// TODO_GENONLINE: Better support for packet callbacks
	std::function<void()> m_callbackStartGamePacket = nullptr;
	void RegisterForGameStartPacket(std::function<void()> cb)
	{
		m_callbackStartGamePacket = cb;
	}

	void DeregisterForGameStartPacket()
	{
		m_callbackStartGamePacket = nullptr;
	}

	// Receives the reason code of a match start the service refused and the missing links for mesh_incomplete.
	std::function<void(const std::string&, const std::vector<std::pair<int64_t, int64_t>>&)> m_callbackStartGameRejected = nullptr;
	void RegisterForStartGameRejected(std::function<void(const std::string&, const std::vector<std::pair<int64_t, int64_t>>&)> cb)
	{
		m_callbackStartGameRejected = cb;
	}

	void DeregisterForStartGameRejected()
	{
		m_callbackStartGameRejected = nullptr;
	}

	std::function<void(const LobbyEvent&)> m_callbackLobbyEvent = nullptr;
	void RegisterForLobbyEvent(std::function<void(const LobbyEvent&)> cb)
	{
		m_callbackLobbyEvent = cb;
	}

	void DeregisterForLobbyEvent()
	{
		m_callbackLobbyEvent = nullptr;
	}

	// periodically force refresh the lobby for data accuracy
	int64_t m_lastForceRefresh = 0;

	void Tick();

	int64_t GetCurrentLobbyOwnerID()
	{
		return m_CurrentLobby.owner;
	}

	LobbyMemberEntry GetRoomMemberFromIndex(int index)
	{
		// TODO_GENONLINE: Optimize data structure
		if (index < m_CurrentLobby.members.size())
		{
			return m_CurrentLobby.members.at(index);
		}

		return LobbyMemberEntry();
	}

	LobbyMemberEntry GetRoomMemberFromID(int64_t userid)
	{
		for (const LobbyMemberEntry& lobbyMember : m_CurrentLobby.members)
		{
			if (lobbyMember.user_id == userid)
			{
				return lobbyMember;
			}
		}

		return LobbyMemberEntry();
	}

	std::vector<LobbyMemberEntry>& GetMembersListForCurrentRoom()
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Refreshing network room roster");
		return m_CurrentLobby.members;
	}

	void RegisterForCreateLobbyCallback(std::function<void(bool)> callback)
	{
		m_cb_CreateLobbyPendingCallback = callback;
	}

	void DeregisterForCreateLobbyCallback()
	{
		m_cb_CreateLobbyPendingCallback = nullptr;
	}

	void ApplyLocalUserPropertiesToCurrentNetworkRoom();

	void SetCurrentLobby_AcceptState(bool bAccepted)
	{

	}

	bool IsHost();

private:
	std::set<int64_t> m_setMembersBeforeUs;
	bool m_bJoinOrderKnown = false;
	void ResetJoinOrder();
	void RecordJoinOrder(const std::vector<LobbyMemberEntry>& members);

	// Local lobby teardown shared by a leave and a matchmaking requeue.
	void TearDownLocalLobbyState();

public:

	void UpdateRoomDataCache(std::function<void(bool)> fnCallback = nullptr);

	// re-fetches the lobby when the roster could not be trusted, throttled
	void RequestRosterRefresh();
	int64_t m_lastRosterRefreshRequestMs = 0;
	int64_t m_rosterRefreshLobbyID = -1;
	int m_rosterRefreshCount = 0;

	std::function<void(LobbyMemberEntry)> m_cbPlayerDoesntHaveMap = nullptr;
	void RegisterForPlayerDoesntHaveMapCallback(std::function<void(LobbyMemberEntry)> cb)
	{
		m_cbPlayerDoesntHaveMap = cb;
	}

	void DeregisterForPlayerDoesntHaveMapCallback()
	{
		m_cbPlayerDoesntHaveMap = nullptr;
	}

	std::function<void(void)> m_OnCannotConnectToLobbyCallback = nullptr;
	void RegisterForCannotConnectToLobbyCallback(std::function<void(void)> cb)
	{
			m_OnCannotConnectToLobbyCallback = cb;
	}

	void DeregisterForCannotConnectToLobbyCallback()
	{
		m_OnCannotConnectToLobbyCallback = nullptr;
	}

	// true if userID was already in the lobby when we joined, i.e. we are the later joiner of that pair
	bool JoinedAfter(int64_t userID) const
	{
		return m_bJoinOrderKnown && m_setMembersBeforeUs.contains(userID);
	}

	bool IsJoinOrderKnown() const
	{
		return m_bJoinOrderKnown;
	}

	bool IsLobbyMember(int64_t userID) const
	{
		for (const LobbyMemberEntry& member : m_CurrentLobby.members)
		{
			if (member.user_id == userID)
			{
				return true;
			}
		}
		return false;
	}

	// raised from inside the mesh's GNS callbacks, dispatched by Tick once the mesh is done with them
	bool m_bCannotConnectToLobbyPending = false;
	void QueueCannotConnectToLobby()
	{
		m_bCannotConnectToLobbyPending = true;
	}

	std::function<void(UnicodeString strMessage, Color color)> m_OnChatCallback = nullptr;
	void RegisterForChatCallback(std::function<void(UnicodeString strMessage, Color color)> cb)
	{
		m_OnChatCallback = cb;
	}

	void DeregisterForChatCallback()
	{
		m_OnChatCallback = nullptr;
		m_OnChatMessageCallback = nullptr;
		m_OnChatMessageRemovedCallback = nullptr;
	}

	// A lobby chat message line carries a key, so a MessageDelete can remove that line later.
	void RegisterForChatMessageCallbacks(std::function<void(UnicodeString strMessage, Color color, uint32_t key)> cbAdd, std::function<void(uint32_t key)> cbRemove)
	{
		m_OnChatMessageCallback = std::move(cbAdd);
		m_OnChatMessageRemovedCallback = std::move(cbRemove);
	}

	// The message id and author of a shown lobby chat line, for its context menu.
	bool FindChatMessage(uint32_t key, std::string& messageID, int64_t& authorID) const
	{
		return m_LobbyChat.MessageOf(key, messageID, authorID);
	}

	void RegisterForJoinLobbyCallback(std::function<void(EJoinLobbyResult)> cb)
	{
		m_callbackJoinedLobby = cb;
	}

	void DeregisterForJoinLobbyCallback()
	{
		m_callbackJoinedLobby = nullptr;
	}

	void ResetCachedRoomData()
	{
		m_CurrentLobby = LobbyEntry();

		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		if (m_RosterNeedsRefreshCallback != nullptr)
		{
			m_RosterNeedsRefreshCallback();
		}
	}

	bool IsInLobby() const { return m_CurrentLobby.lobbyID != -1; }

	NetworkMesh* GetNetworkMeshForLobby() { return m_pLobbyMesh; }

	// false when a join is already in progress and this request was dropped
	bool JoinLobby(LobbyEntry lobby, std::string strPassword);

	void LeaveCurrentLobby();
	void ResetForMatchmakingRequeue();

	void ResetHostMigrationFlags()
	{
		m_bHostMigrated = false;
		m_bPendingHostHasLeft = false;
	}

	LobbyEntry GetLobbyFromID(int64_t lobbyID);

	std::vector<LobbyEntry> m_vecLobbies;

	bool m_bHostMigrated = false;
	bool m_bPendingHostHasLeft = false;

	void SetLobbyTryingToJoin(LobbyEntry lobby)
	{
		m_LobbyTryingToJoin = lobby;
	}

	void ResetLobbyTryingToJoin()
	{
		m_LobbyTryingToJoin = LobbyEntry();
	}

	LobbyEntry GetLobbyTryingToJoin()
	{
		return m_LobbyTryingToJoin;
	}

	std::string GetLobbyTurnUsername() { return m_strTURNUsername; }
	std::string GetLobbyTurnToken() { return m_strTURNToken; }

	uint64_t GetCurrentMatchID()
	{
		return m_CurrentMatchID;
	}

	void OnRealtimeReady();
	void OnRealtimeLobbyListUpsert(const GenOnlineRealtime::LobbySummary& lobby);
	void OnRealtimeLobbyListRemove(int64_t lobbyID);
	void OnRealtimeLobbyState(const GenOnlineRealtime::LobbyState& lobby);
	void OnRealtimeLobbyLeft(const GenOnlineRealtime::LobbyLeftEvent& event);
	void OnRealtimeCountdownStarted(const GenOnlineRealtime::CountdownStartedEvent& event);
	void OnRealtimeCountdownCancelled(const GenOnlineRealtime::CountdownCancelledEvent& event);
	void OnRealtimeGameStart(const GenOnlineRealtime::GameStartEvent& event);
	void OnRealtimeLobbyMessageCreate(const GenOnlineRealtime::ChatMessage& message);
	void OnRealtimeLobbyHistory(const GenOnlineRealtime::ChannelHistoryEvent& event);
	void OnRealtimeLobbyMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event);
	void OnRealtimeStartGameOutcome(const GenOnlineRealtime::InvokeOutcome& outcome);
	void ShowRealtimeLobbyFailure(const GenOnlineRealtime::InvokeOutcome& outcome);

	void HandleMatchFound(int64_t lobbyID, const std::string& mapPath);
	void HandleMatchSetupProgress(int64_t timeoutMs, int64_t countdownMs);
	void OnRealtimeMatchmakingStatus(const GenOnlineRealtime::MatchmakingStatusEvent& event);
	void OnRealtimeMatchStart(const GenOnlineRealtime::MatchStartEvent& event);
	void OnRealtimeMatchRequeue(const GenOnlineRealtime::MatchRequeueEvent& event);

private:
	std::function<void(bool)> m_cb_CreateLobbyPendingCallback = nullptr;

	std::function<void(EJoinLobbyResult)> m_callbackJoinedLobby = nullptr;

	LobbyEntry m_CurrentLobby;

	std::string m_strTURNUsername = "";
	std::string m_strTURNToken = "";

	// TODO_GENONLINE: cleanup
	NetworkMesh* m_pLobbyMesh = nullptr;

	std::atomic_bool m_bLobbyListDirty = false;


	bool m_bAttemptingToJoinLobby = false;

	// Lobby GET responses older than the last applied one are dropped.
	std::atomic<uint64_t> m_LobbyUpdateRequestSeq = 0;
	std::atomic<uint64_t> m_LobbyUpdateAppliedSeq = 0;
	// When this client last told the service whether it has the map, so a lobby that still shows the old answer gets it again only after a pause.
	std::chrono::steady_clock::time_point m_LastHasMapSent = {};
	// Invalidates asynchronous callbacks left behind by an abandoned lobby join.
	std::atomic<uint64_t> m_LobbyJoinGeneration = 0;
	LobbyEntry m_LobbyTryingToJoin;

	std::atomic_bool m_bSearchInProgress = false;

	bool m_bMarkedGameAsFinished = false;

	uint64_t m_CurrentMatchID = 0;

	void ApplyLobbyUpdate(LobbyEntry lobbyEntry, const std::vector<LobbyMemberEntry>& parsedMembers, uint64_t requestSeq);
	void HandleCurrentLobbyGone();
	void LeaveLobbyOnService(int64_t lobbyID);

	void SearchForLobbiesRealtime();
	void CompleteRealtimeLobbySearch(bool bSucceeded);
	void StopWatchingLobbyList();
	void UpdateRoomDataCacheRealtime(std::function<void(bool)> fnCallback);
	void ApplyRealtimeLobbyState(const GenOnlineRealtime::LobbyState& lobby);
	void FlushLobbyStateWaiters(bool bSucceeded);

	// Realtime hub lobby list cache, kept current by LobbyListUpsert and LobbyListRemove while watching.
	bool m_bWatchingLobbyList = false;
	std::vector<GenOnlineRealtime::LobbySummary> m_hubLobbyList;
	// The latest LobbyState of the hub and the refreshes that wait for the state of the current lobby.
	std::optional<GenOnlineRealtime::LobbyState> m_hubLobbyState;
	std::vector<std::function<void(bool)>> m_hubLobbyStateWaiters;
	// The quick match lobby of the latest MatchStart and GameStart, -1 while it has not arrived.
	int64_t m_hubMatchStartLobbyID = -1;
	int64_t m_hubGameStartLobbyID = -1;
	void StartQuickMatchWhenReady();
	// In a quick match the start of a LobbyState also stands for MatchStart, which the service does not resend.
	void StartMatchFromService(const GenOnlineRealtime::GameStartEvent& event, const char* szSource, bool bStandsForMatchStart);
	GenOnlineRealtime::MatchStartOnce m_MatchStart;

	// The lobby chat, deduplicated by message id across the live messages and the history of a rejoin.
	std::function<void(UnicodeString, Color, uint32_t)> m_OnChatMessageCallback = nullptr;
	std::function<void(uint32_t)> m_OnChatMessageRemovedCallback = nullptr;
	GenOnlineRealtime::ChannelChatLog m_LobbyChat;
	std::chrono::steady_clock::time_point m_LobbyChatHistoryDeadline;
	// The lobby history of a join, which arrives before the JoinLobby Result.
	GenOnlineRealtime::PendingChannelHistory m_PendingLobbyHistory;
	// Starts the chat of the current lobby once. Without a held history live messages show at once.
	bool EnsureLobbyChat();
	// Starts the chat of a joined lobby. A held history shows once the lobby menu takes chat lines, before live messages.
	void StartJoinedLobbyChat();
	void ShowLobbyChatUpdate(const GenOnlineRealtime::ChannelChatUpdate& update);
	void ShowLobbyChatLine(const GenOnlineRealtime::ChannelChatLine& line);
};
