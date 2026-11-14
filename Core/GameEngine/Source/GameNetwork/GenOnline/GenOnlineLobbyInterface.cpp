#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/JsonHelpers.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GenOnline/LobbyConnectionPolicy.h"
#include "Lib/PathUtil.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"
#if RTS_ZEROHOUR
#include "Common/StatsExporterLive.h"
#endif

#include <algorithm>

extern void OnKickedFromLobby();

extern GenOnlineGame* TheGenOnlineGame;

struct JoinLobbyResponse
{
	bool success = false;
	std::string turn_username;
	std::string turn_token;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinLobbyResponse, success, turn_username, turn_token)
};

UnicodeString GenOnlineLobbyInterface::GetCurrentLobbyDisplayName()
{
	UnicodeString strDisplayName;

	if (IsInLobby())
	{
		strDisplayName = UnicodeString(from_utf8(m_CurrentLobby.name).c_str());
	}

	return strDisplayName;
}

UnicodeString GenOnlineLobbyInterface::GetCurrentLobbyMapDisplayName()
{
	UnicodeString strDisplayName;

	if (IsInLobby())
	{
		strDisplayName = UnicodeString(from_utf8(m_CurrentLobby.map_name).c_str());
	}

	return strDisplayName;
}

AsciiString GenOnlineLobbyInterface::GetCurrentLobbyMapPath()
{
	AsciiString strPath;

	if (IsInLobby())
	{
		strPath = m_CurrentLobby.map_path.c_str();
	}

	return strPath;
}

// Calls a lobby method of the realtime hub.
template <class R>
static void InvokeLobbyHub(const R& request, std::function<void(const GenOnlineRealtime::InvokeOutcome&)> callback)
{
	InvokeRealtimeHub(request, callback);
}

// The status code and body of the legacy join response for a JoinLobby result.
static void JoinResponseFromOutcome(const GenOnlineRealtime::InvokeOutcome& outcome, int& statusCode, std::string& strBody)
{
	strBody.clear();
	if (outcome.Succeeded())
	{
		GenOnlineRealtime::JoinLobbyResult result;
		outcome.Data(result);
		nlohmann::json j;
		j["success"] = true;
		j["turn_username"] = result.turn.username;
		j["turn_token"] = result.turn.token;
		strBody = j.dump();
		statusCode = 200;
		return;
	}

	const std::string code = outcome.Code();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] JoinLobby failed: %s", outcome.delivered ? code.c_str() : outcome.transportError.c_str());
	if (!outcome.delivered)
	{
		statusCode = 0;
	}
	else if (code == "lobby.wrong_password")
	{
		statusCode = 401;
	}
	else if (code == "lobby.full")
	{
		statusCode = 406;
	}
	else if (code == "lobby.anticheat_mismatch")
	{
		statusCode = 417;
	}
	else if (code == "not_found")
	{
		statusCode = 404;
	}
	else
	{
		statusCode = 400;
	}
}

// The body of the legacy create response for a CreateLobby result.
static std::string CreateResponseFromOutcome(const GenOnlineRealtime::InvokeOutcome& outcome)
{
	nlohmann::json j;
	j["result"] = 0;
	j["lobby_id"] = -1;
	j["turn_username"] = "";
	j["turn_token"] = "";

	GenOnlineRealtime::CreateLobbyResult result;
	if (outcome.Data(result))
	{
		j["result"] = 1;
		j["lobby_id"] = result.lobby_id;
		j["turn_username"] = result.turn.username;
		j["turn_token"] = result.turn.token;
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] CreateLobby failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
	}
	return j.dump();
}

// Sends a lobby change to the hub. A refusal shows in the lobby chat.
template <class R>
static void InvokeLobbyUpdate(const R& request)
{
	InvokeLobbyHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			if (outcome.Succeeded())
			{
				return;
			}
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] %s failed: %s", R::METHOD, outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->ShowRealtimeLobbyFailure(outcome);
			}
		});
}

// The hub team of a game team, where -1 is no team.
static GenOnlineRealtime::Nullable<int> HubTeam(int team)
{
	return team < 0 ? GenOnlineRealtime::Nullable<int>() : GenOnlineRealtime::Nullable<int>(team);
}

static GenOnlineRealtime::UpdateSlotsRequest SingleSlotUpdate(const GenOnlineRealtime::SlotUpdate& slot)
{
	GenOnlineRealtime::UpdateSlotsRequest request;
	request.slots.push_back(slot);
	return request;
}

static const char* SlotStateToWire(uint16_t slotState)
{
	switch (slotState)
	{
	case SlotState::SLOT_OPEN:
		return "open";
	case SlotState::SLOT_EASY_AI:
		return "ai_easy";
	case SlotState::SLOT_MED_AI:
		return "ai_medium";
	case SlotState::SLOT_BRUTAL_AI:
		return "ai_brutal";
	case SlotState::SLOT_PLAYER:
		return "player";
	default:
		return "closed";
	}
}

static uint16_t SlotStateFromWire(const std::string& state)
{
	if (state == "player")
	{
		return SlotState::SLOT_PLAYER;
	}
	if (state == "open")
	{
		return SlotState::SLOT_OPEN;
	}
	if (state == "ai_easy")
	{
		return SlotState::SLOT_EASY_AI;
	}
	if (state == "ai_medium")
	{
		return SlotState::SLOT_MED_AI;
	}
	if (state == "ai_brutal")
	{
		return SlotState::SLOT_BRUTAL_AI;
	}
	// A slot state this build does not know stays closed.
	return SlotState::SLOT_CLOSED;
}

// A lobby list entry of the hub. The list carries the host as the only member.
static LobbyEntry LobbyEntryFromSummary(const GenOnlineRealtime::LobbySummary& summary)
{
	LobbyEntry lobbyEntry;
	lobbyEntry.lobbyID = summary.id;
	lobbyEntry.owner = summary.host.id;
	lobbyEntry.name = summary.name;
	lobbyEntry.map_name = summary.map.name;
	// The list has no map path. The lobby state carries it after the join.
	lobbyEntry.map_official = summary.map.official;
	lobbyEntry.current_players = summary.players;
	lobbyEntry.max_players = summary.max_players;
	lobbyEntry.vanilla_teams = summary.settings.vanilla_teams;
	lobbyEntry.starting_cash = (uint32_t)(std::max)(0, summary.settings.starting_cash);
	lobbyEntry.limit_superweapons = summary.settings.limit_superweapons;
	lobbyEntry.track_stats = summary.settings.track_stats;
	lobbyEntry.allow_observers = summary.settings.allow_observers;
	lobbyEntry.exe_crc = summary.build.exe_crc;
	lobbyEntry.ini_crc = summary.build.ini_crc;
	// The list only contains custom lobbies.
	lobbyEntry.lobby_type = ELobbyType::CustomGame;
	lobbyEntry.passworded = summary.has_password;
	lobbyEntry.region = summary.region;
	lobbyEntry.latency = summary.latency_ms;

	LobbyMemberEntry host;
	host.user_id = summary.host.id;
	host.display_name = summary.host.name;
	host.preferredPort = 0;
	host.m_bIsReady = true;
	host.m_SlotIndex = 0;
	host.m_SlotState = SlotState::SLOT_PLAYER;
	host.has_map = true;
	host.region = summary.region;
	host.latency = summary.latency_ms;
	lobbyEntry.members.push_back(host);
	return lobbyEntry;
}

// The current lobby of the hub. Every slot becomes a member, as in the legacy lobby response.
static void LobbyEntryFromState(const GenOnlineRealtime::LobbyState& lobby, LobbyEntry& lobbyEntry, std::vector<LobbyMemberEntry>& members)
{
	lobbyEntry.lobbyID = lobby.id;
	lobbyEntry.owner = lobby.host_id;
	lobbyEntry.name = lobby.name;
	lobbyEntry.map_name = lobby.map.name;
	lobbyEntry.map_path = utf8_to_local(lobby.map.path); // local file path from here on
	lobbyEntry.map_official = lobby.map.official;
	lobbyEntry.vanilla_teams = lobby.settings.vanilla_teams;
	lobbyEntry.starting_cash = (uint32_t)(std::max)(0, lobby.settings.starting_cash);
	lobbyEntry.limit_superweapons = lobby.settings.limit_superweapons;
	lobbyEntry.track_stats = lobby.settings.track_stats;
	lobbyEntry.allow_observers = lobby.settings.allow_observers;
	lobbyEntry.max_cam_height = (uint16_t)(std::clamp)(lobby.settings.max_camera_height, 0, 65535);
	// The service only lets sessions of the same build into a lobby.
	lobbyEntry.exe_crc = TheGlobalData->m_exeCRC;
	lobbyEntry.ini_crc = TheGlobalData->m_iniCRC;
	lobbyEntry.match_id = lobby.match_id.isNull ? 0 : (uint64_t)lobby.match_id.value;
	lobbyEntry.lobby_type = lobby.type == "quick_match" ? ELobbyType::QuickMatch : ELobbyType::CustomGame;
	lobbyEntry.passworded = lobby.has_password;
	lobbyEntry.region = lobby.region;

	members.clear();
	for (const GenOnlineRealtime::LobbySlot& slot : lobby.slots)
	{
		LobbyMemberEntry memberEntry;
		memberEntry.preferredPort = 0;
		memberEntry.m_SlotState = SlotStateFromWire(slot.state);
		if (slot.index >= 0 && slot.index < MAX_SLOTS)
		{
			memberEntry.m_SlotIndex = (uint16_t)slot.index;
		}
		if (memberEntry.m_SlotState == SlotState::SLOT_PLAYER && slot.member.has_value())
		{
			memberEntry.user_id = slot.member->id;
			memberEntry.display_name = slot.member->name;
			memberEntry.has_map = slot.has_map;
		}
		else
		{
			memberEntry.user_id = -1;
			memberEntry.m_SlotState = memberEntry.m_SlotState == SlotState::SLOT_PLAYER ? SlotState::SLOT_OPEN : memberEntry.m_SlotState;
			// Only players can miss the map.
			memberEntry.has_map = true;
		}
		memberEntry.m_bIsReady = slot.ready;
		memberEntry.side = slot.side;
		memberEntry.color = slot.color;
		memberEntry.team = slot.team.isNull ? -1 : slot.team.value;
		memberEntry.startpos = slot.start_position;
		memberEntry.region = lobby.region;

		if (memberEntry.m_SlotState == SlotState::SLOT_PLAYER || memberEntry.m_SlotState == SlotState::SLOT_EASY_AI || memberEntry.m_SlotState == SlotState::SLOT_MED_AI || memberEntry.m_SlotState == SlotState::SLOT_BRUTAL_AI)
		{
			++lobbyEntry.current_players;
		}
		if (memberEntry.m_SlotState != SlotState::SLOT_CLOSED)
		{
			++lobbyEntry.max_players;
		}
		members.push_back(memberEntry);
	}
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_Map(UnicodeString strMap, AsciiString strMapPath, bool bIsOfficial, int newMaxPlayers)
{
	// sanitize map path
	// we need to parse out the map name for custom maps... its an absolute path
	// it's safe to just get the file name, dir name and file name MUST be the same. Game enforces this
	AsciiString sanitizedMapPath = getFileName(strMapPath.str());

	GenOnlineRealtime::UpdateLobbyRequest request;
	GenOnlineRealtime::UpdateLobbyMap map;
	map.name = to_utf8(strMap.str());
	map.path = local_to_utf8(sanitizedMapPath.str());
	map.official = bIsOfficial;
	map.max_players = newMaxPlayers;
	request.map = map;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_LimitSuperweapons(bool bLimitSuperweapons)
{
	GenOnlineRealtime::UpdateLobbyRequest request;
	request.settings = GenOnlineRealtime::UpdateLobbySettings();
	request.settings->limit_superweapons = bLimitSuperweapons;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_StartingCash(UnsignedInt startingCashValue)
{
	GenOnlineRealtime::UpdateLobbyRequest request;
	request.settings = GenOnlineRealtime::UpdateLobbySettings();
	request.settings->starting_cash = (int)startingCashValue;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_HasMap()
{
	// do we have the map?
	bool bHasMap = TheMapCache->findMap(AsciiString(m_CurrentLobby.map_path.c_str()));

	GenOnlineRealtime::SetHasMapRequest request;
	request.has_map = bHasMap;
	InvokeLobbyUpdate(request);
}

// start AI
void GenOnlineLobbyInterface::UpdateCurrentLobby_AISide(int slot, int side, int updatedStartPos)
{
	GenOnlineRealtime::SlotUpdate update;
	update.index = slot;
	update.side = side;
	update.start_position = updatedStartPos;
	InvokeLobbyUpdate(SingleSlotUpdate(update));
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_AITeam(int slot, int team)
{
	GenOnlineRealtime::SlotUpdate update;
	update.index = slot;
	update.team = HubTeam(team);
	InvokeLobbyUpdate(SingleSlotUpdate(update));
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_AIStartPos(int slot, int startpos)
{
	GenOnlineRealtime::SlotUpdate update;
	update.index = slot;
	update.start_position = startpos;
	InvokeLobbyUpdate(SingleSlotUpdate(update));
}

void GenOnlineLobbyInterface::UpdateCurrentLobbyMaxCameraHeight(uint16_t maxCameraHeight)
{
	if (IsHost())
	{
		GenOnlineRealtime::UpdateLobbyRequest request;
		request.settings = GenOnlineRealtime::UpdateLobbySettings();
		request.settings->max_camera_height = maxCameraHeight;
		InvokeLobbyUpdate(request);
	}
}


void GenOnlineLobbyInterface::SetJoinability(ELobbyJoinability joinabilityFlag)
{
    if (IsHost())
    {
        GenOnlineRealtime::UpdateLobbyRequest request;
        request.visibility = std::string(joinabilityFlag == ELobbyJoinability::LobbyJoinability_FriendsOnly ? "friends" : "public");
        InvokeLobbyUpdate(request);
    }
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_AIColor(int slot, int color)
{
	GenOnlineRealtime::SlotUpdate update;
	update.index = slot;
	update.color = color;
	InvokeLobbyUpdate(SingleSlotUpdate(update));
}
// end AI

void GenOnlineLobbyInterface::UpdateCurrentLobby_MySide(int side, int updatedStartPos)
{
	GenOnlineRealtime::UpdateMySlotRequest request;
	request.side = side;
	request.start_position = updatedStartPos;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_MyColor(int color)
{
	GenOnlineRealtime::UpdateMySlotRequest request;
	request.color = color;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_MyStartPos(int startpos)
{
	GenOnlineRealtime::UpdateMySlotRequest request;
	request.start_position = startpos;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_MyTeam(int team)
{
	GenOnlineRealtime::UpdateMySlotRequest request;
	request.team = HubTeam(team);
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_SetSlotState(uint16_t slotIndex, uint16_t slotState)
{
	GenOnlineRealtime::SetSlotStateRequest request;
	request.index = slotIndex;
	request.state = SlotStateToWire(slotState);
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::UpdateCurrentLobby_KickUser(int64_t userID, UnicodeString name)
{
	GenOnlineRealtime::KickFromLobbyRequest request;
	request.user_id = userID;
	InvokeLobbyUpdate(request);
}

void GenOnlineLobbyInterface::SendChatMessageToCurrentLobby(UnicodeString& strChatMsgUnicode, bool bIsAction)
{
	std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();;
	if (pWS != nullptr)
	{
		pWS->SendData_LobbyChatMessage(strChatMsgUnicode, bIsAction, false, false);
	}
}

GenOnlineLobbyInterface::GenOnlineLobbyInterface()
{

}

void GenOnlineLobbyInterface::SearchForLobbies(std::function<void()> onStartCallback, std::function<void(std::vector<LobbyEntry>)> onCompleteCallback)
{
	const bool lobbyListWasDirty = m_bLobbyListDirty.exchange(false);
	if (m_bSearchInProgress.exchange(true))
	{
		if (lobbyListWasDirty)
		{
			m_bLobbyListDirty.store(true);
		}
		return;
	}

	// A notification received after the exchange above stays set and triggers a later refresh.
	m_fnCallbackSearchForLobbiesComplete = onCompleteCallback;
	if (onStartCallback != nullptr)
	{
		onStartCallback();
	}

	SearchForLobbiesRealtime();
}

void GenOnlineLobbyInterface::RequestRosterRefresh()
{
	// A service that keeps sending a member without a slot gets a bounded number of refreshes per lobby.
	static constexpr int MAX_ROSTER_REFRESHES_PER_LOBBY = 10;
	if (m_rosterRefreshLobbyID != m_CurrentLobby.lobbyID)
	{
		m_rosterRefreshLobbyID = m_CurrentLobby.lobbyID;
		m_rosterRefreshCount = 0;
	}

	if (m_rosterRefreshCount >= MAX_ROSTER_REFRESHES_PER_LOBBY)
	{
		return;
	}

	const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	if (nowMs - m_lastRosterRefreshRequestMs < 2000)
	{
		return;
	}

	m_lastRosterRefreshRequestMs = nowMs;
	if (++m_rosterRefreshCount == MAX_ROSTER_REFRESHES_PER_LOBBY)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Roster refresh limit reached for lobby %lld", m_CurrentLobby.lobbyID);
	}
	UpdateRoomDataCache();
}

void GenOnlineLobbyInterface::ResetJoinOrder()
{
	m_setMembersBeforeUs.clear();
	m_bJoinOrderKnown = false;
}

// the first member list after joining holds everyone who was there before us
void GenOnlineLobbyInterface::RecordJoinOrder(const std::vector<LobbyMemberEntry>& members)
{
	if (m_bJoinOrderKnown)
	{
		return;
	}

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	int64_t myUserID = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();

	// only a list from after our join counts
	std::set<int64_t> setOthers;
	bool bContainsUs = false;
	for (const LobbyMemberEntry& member : members)
	{
		if (member.user_id == myUserID)
		{
			bContainsUs = true;
		}
		else
		{
			setOthers.insert(member.user_id);
		}
	}

	if (!bContainsUs)
	{
		return;
	}

	m_setMembersBeforeUs = std::move(setOthers);
	m_bJoinOrderKnown = true;
}

bool GenOnlineLobbyInterface::IsHost()
{
	if (IsInLobby())
	{
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		int64_t myUserID = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();
		return m_CurrentLobby.owner == myUserID;
	}

	return false;
}


void GenOnlineLobbyInterface::Tick()
{
	// The held history of a join shows once the lobby menu takes chat lines, or at the deadline.
	if (IsInLobby() && m_PendingLobbyHistory.IsHolding(m_LobbyChat.ChannelID())
		&& (m_OnChatCallback != nullptr || m_OnChatMessageCallback != nullptr || std::chrono::steady_clock::now() >= m_LobbyChatHistoryDeadline))
	{
		GenOnlineRealtime::ChannelHistoryEvent history;
		if (m_PendingLobbyHistory.Take(m_LobbyChat.ChannelID(), history))
		{
			ShowLobbyChatUpdate(m_LobbyChat.AddHistory(history));
		}
	}

	// Live lobby messages stop waiting for a ChannelHistory that does not come.
	if (m_LobbyChat.IsAwaitingHistory() && std::chrono::steady_clock::now() >= m_LobbyChatHistoryDeadline)
	{
		for (const GenOnlineRealtime::ChannelChatLine& line : m_LobbyChat.ReleaseWaiting())
		{
			ShowLobbyChatLine(line);
		}
	}

	// cheats
#if defined(GENONLINE_ALL_SCIENCES)
	static bool GrantAllSciences = true;
	if (GrantAllSciences)
	{
		//GrantAllSciences = false;
		Player* player = ThePlayerList->getLocalPlayer();
		if (player)
		{
			// cheese festival: do NOT imitate this code. it is for debug purposes only.
			std::vector<AsciiString> v = TheScienceStore->friend_getScienceNames();
			for (int i = 0; i < v.size(); ++i)
			{
				ScienceType st = TheScienceStore->getScienceFromInternalName(v[i]);
				if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st))
				{
					player->grantScience(st);
					TheInGameUI->message(UnicodeString(L"Granting all sciences!"));
				}
			}
		}

	}
#endif

#if defined(GENONLINE_MAX_SCIENCES_POINTS)
	static bool GrantSciencePoints = false;
	if (GrantSciencePoints)
	{
		GrantSciencePoints = false;
		Player* player2 = ThePlayerList->getLocalPlayer();
		if (player2 && player2->isPlayerActive())
		{
			player2->setRankLevel(5);
			player2->addSciencePurchasePoints(100);
			TheInGameUI->message(UnicodeString(L"Adding a SciencePurchasePoint and setting to max general level"));
		}
	}
#endif

	if (m_pLobbyMesh != nullptr)
	{
		m_pLobbyMesh->Flush();
		m_pLobbyMesh->Tick();
	}

	// stays pending until a menu has registered to handle it
	if (m_bCannotConnectToLobbyPending)
	{
		auto callbackCopy = m_OnCannotConnectToLobbyCallback;
		if (callbackCopy != nullptr)
		{
			m_bCannotConnectToLobbyPending = false;
			callbackCopy();
		}
	}

	// TODO_NGMP: Do we still need this safety measure?
	if (IsInLobby())
	{
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
		if ((currTime - m_lastForceRefresh) > 5000)
		{
			//UpdateRoomDataCache();
			m_lastForceRefresh = currTime;
		}
	}

	// real time outcome upload
	if (TheVictoryConditions != nullptr && TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
	{
		// is the game over for us?
		if (TheVictoryConditions->isLocalAlliedDefeat() || TheVictoryConditions->isLocalDefeat() || TheVictoryConditions->isLocalAlliedVictory())
		{
			GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
			if (pStatsInterface != nullptr)
			{
				Player* localPlayer = ThePlayerList->getLocalPlayer();

				if (localPlayer != nullptr)
				{
					if (!TheGenOnlineGame->HasCommittedOutcome())
					{
						TheGenOnlineGame->SetHasCommittedOutcome();
						if (m_pLobbyMesh != nullptr)
						{
							m_pLobbyMesh->EndMatchKeys();
						}
						pStatsInterface->CommitMyOutcome(localPlayer->getScoreKeeper(), TheVictoryConditions->isLocalAlliedVictory());
					}
				}
			}
		}
	}
}

void GenOnlineLobbyInterface::ApplyLocalUserPropertiesToCurrentNetworkRoom()
{
	// TODO_NGMP: Better detection of this, dont update always
	// are we ready?

	std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();;
	if (pWS != nullptr)
	{
		if (IsHost())
		{
			pWS->SendData_MarkReady(true);
		}
		else
		{
			if (TheGenOnlineGame == nullptr)
				return;

			GameSlot* pLocalSlot = TheGenOnlineGame->getSlot(TheGenOnlineGame->getLocalSlotNum());

			if (pLocalSlot != nullptr)
			{
				pWS->SendData_MarkReady(pLocalSlot->isAccepted());
			}
		}
	}
}

// The service no longer has the current lobby.
void GenOnlineLobbyInterface::HandleCurrentLobbyGone()
{
	// the match runs on this lobby's mesh
	if (TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Lobby lookup returned 404 during a match, keeping the match running");
		return;
	}

	// the server requeues quick match players itself, a DELETE would cancel that
	if (TheGenOnlineGame->isQMGame() || m_CurrentLobby.lobby_type == ELobbyType::QuickMatch)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Quick match lobby lookup returned 404, tearing down locally only");
		ResetForMatchmakingRequeue();
		return;
	}

	LeaveCurrentLobby();

	// Set after LeaveCurrentLobby because it clears the flag.
	m_bPendingHostHasLeft = true;
}

// Applies a lobby from the service: kick and host change detection, map ownership and the game slots.
void GenOnlineLobbyInterface::ApplyLobbyUpdate(LobbyEntry lobbyEntry, const std::vector<LobbyMemberEntry>& parsedMembers, uint64_t requestSeq)
{
	if (lobbyEntry.lobby_type == ELobbyType::QuickMatch)
	{
		TheGenOnlineGame->markGameAsQM();
	}

	// store, we'll need it later and lobby obj gets destroyed on leave
	m_CurrentMatchID = lobbyEntry.match_id;

	// correct map path
	if (lobbyEntry.map_official)
	{
		lobbyEntry.map_path = std::format("maps\\{}", lobbyEntry.map_path.c_str());
	}
	else
	{
		// TODO_NGMP: This needs to match identically, but why did it change from the base game?
		AsciiString strUserMapDIr = TheMapCache->getUserMapDir(true);
		strUserMapDIr.toLower();

		lobbyEntry.map_path = std::format("{}\\{}", strUserMapDIr.str(), lobbyEntry.map_path.c_str());
	}

	// did the map change? cache that we need to reset and transmit our ready state
	bool bNeedsHasMapUpdate = false;
	if (strcasecmp(lobbyEntry.map_path.c_str(), TheGenOnlineGame->getMap().str()) != 0)
	{
		bNeedsHasMapUpdate = true;
	}

	// dont let the service be authoritative during gameplay over play list, the game host handles connections at this point
	// we do care about everything else up until members list though, because we need things like matchID which is determined as the game transitions to in progress
	if (TheGenOnlineGame->isGameInProgress() && !TheGameLogic->IsLoadScreenActive())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Ignoring lobby members update request during gameplay.");

		// retain the old members list
		lobbyEntry.members = m_CurrentLobby.members;

		// still store the lobby though
		m_LobbyUpdateAppliedSeq = requestSeq;
		m_CurrentLobby = lobbyEntry;
	}
	else
	{
		bool bFoundSelfInOld = false;
		bool bFoundSelfInNew = false;
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		int64_t myUserID = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();

		// check for user in old lobby data
		for (LobbyMemberEntry& currentMember : m_CurrentLobby.members)
		{
			if (currentMember.IsHuman())
			{
				// detect local kick
				if (currentMember.user_id == myUserID)
				{
					bFoundSelfInOld = true;
					break;
				}
			}
		}

		for (const LobbyMemberEntry& memberEntry : parsedMembers)
		{
			lobbyEntry.members.push_back(memberEntry);

			// TODO_NGMP: Much more robust system here
			// TODO_NGMP: If we lose connection to someone in the mesh, who is STILL in otehrs mesh, we need to disconnect or retry
			// TODO_NGMP: handle failure to connect to some users

			bool bMapOwnershipStateChanged = true;
			for (LobbyMemberEntry& currentMember : m_CurrentLobby.members)
			{
				if (memberEntry.IsHuman())
				{
					// detect local kick
					if (memberEntry.user_id == myUserID)
					{
						bFoundSelfInNew = true;
					}

					if (currentMember.user_id == memberEntry.user_id)
					{
						// check if the map state changes
						if (currentMember.has_map == memberEntry.has_map)
						{
							bMapOwnershipStateChanged = false;
						}

						break;
					}
				}
			}

			if (bMapOwnershipStateChanged)
			{
				// changed and the person no longer has the map
				if (!memberEntry.has_map)
				{
					if (m_cbPlayerDoesntHaveMap != nullptr)
					{
						m_cbPlayerDoesntHaveMap(memberEntry);
					}
				}
			}
		}


		if (bFoundSelfInOld && !bFoundSelfInNew)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "We were kicked from the lobby...");
			OnKickedFromLobby();
		}

		// did the host change?
		if (lobbyEntry.owner != m_CurrentLobby.owner)
		{
			// no host migration in QM since it doesn't even have a lobby
			if (lobbyEntry.lobby_type != ELobbyType::QuickMatch)
			{
				m_bHostMigrated = true;
			}
		}

		// store
		m_LobbyUpdateAppliedSeq = requestSeq;
		m_CurrentLobby = lobbyEntry;
		RecordJoinOrder(lobbyEntry.members);

		// inform game instance too
		if (TheGenOnlineGame != nullptr)
		{
			TheGenOnlineGame->SyncWithLobby(m_CurrentLobby);
			TheGenOnlineGame->UpdateSlotsFromCurrentLobby();

			if (bNeedsHasMapUpdate)
			{
				UpdateCurrentLobby_HasMap();
			}

			std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
			if (m_RosterNeedsRefreshCallback != nullptr)
			{
				m_RosterNeedsRefreshCallback();
			}
		}
	}
}

void GenOnlineLobbyInterface::UpdateRoomDataCache(std::function<void(bool)> fnCallback)
{
	UpdateRoomDataCacheRealtime(fnCallback);
}

bool GenOnlineLobbyInterface::JoinLobby(LobbyEntry lobbyInfo, std::string strPassword)
{
	if (m_bAttemptingToJoinLobby)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Not attempting to join lobby because a join attempt is already in progress");
		return false;
	}

	const uint64_t lobbyJoinGeneration = ++m_LobbyJoinGeneration;

    GenOnlinePluginHost::Anticheat::EndSession();

	m_bAttemptingToJoinLobby = true;
	m_CurrentLobby = LobbyEntry();
	m_PendingLobbyHistory.Clear();
	ResetJoinOrder();
	ResetHostMigrationFlags();

	GenOnlineServicesManager::GetInstance()->GetAndParseServiceConfig([=]()
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface == nullptr || pLobbyInterface != this || pLobbyInterface->m_LobbyJoinGeneration.load() != lobbyJoinGeneration)
			{
				return;
			}

			NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Joining lobby with id %d", lobbyInfo.lobbyID);

			bool bHasMap = TheMapCache->findMap(AsciiString(lobbyInfo.map_path.c_str()));

			// create our mesh
			if (m_pLobbyMesh == nullptr)
			{
				NetworkMesh* pNewMesh = new NetworkMesh();
				if (!pNewMesh->IsInitialized())
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to initialize network mesh, aborting lobby join");
					delete pNewMesh;
					ResetLobbyTryingToJoin();
					m_bAttemptingToJoinLobby = false;
					if (m_callbackJoinedLobby != nullptr)
					{
						m_callbackJoinedLobby(EJoinLobbyResult::JoinLobbyResult_JoinFailed);
					}
					return;
				}
				m_pLobbyMesh = pNewMesh;
			}

			// TURN credentials arrive with the join response
			m_pLobbyMesh->AwaitTurnCredentials();

			auto onJoinResponse = [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
				{
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
					if (pLobbyInterface == nullptr || pLobbyInterface != this || pLobbyInterface->m_LobbyJoinGeneration.load() != lobbyJoinGeneration)
					{
						// stale join response landed after we already left/moved on; leave again if it succeeded
						if (pLobbyInterface == this && statusCode == 200 && bSuccess && m_CurrentLobby.lobbyID != lobbyInfo.lobbyID)
						{
							LeaveLobbyOnService(lobbyInfo.lobbyID);
						}
						return;
					}

					// reset trying to join
					ResetLobbyTryingToJoin();

					// TODO_NGMP: Dont do extra get here, just return it in the put...
					EJoinLobbyResult JoinResult = EJoinLobbyResult::JoinLobbyResult_JoinFailed;

					if (statusCode == 200 && bSuccess)
					{
						JoinResult = EJoinLobbyResult::JoinLobbyResult_Success;
					}
					else if (statusCode == 401)
					{
						JoinResult = EJoinLobbyResult::JoinLobbyResult_BadPassword;
					}
					else if (statusCode == 406)
					{
						JoinResult = EJoinLobbyResult::JoinLobbyResult_FullRoom;
					}
                    else if (statusCode == 417)
                    {
                        JoinResult = EJoinLobbyResult::JoinLobbyResult_AnticheatMismatch;
                    }
					// TODO_NGMP: Handle room full error (JoinLobbyResult_FullRoom, can we even get that?

					// no response body from this, just http codes
					if (JoinResult == EJoinLobbyResult::JoinLobbyResult_Success)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Joined lobby");

						m_CurrentLobby = lobbyInfo;
						StartJoinedLobbyChat();

						// failing to parse this isnt really a fatal error, but would be weird
						try
						{
							nlohmann::json jsonObject = nlohmann::json::parse(strBody);
							JoinLobbyResponse resp = jsonObject.get<JoinLobbyResponse>();

							m_strTURNUsername = resp.turn_username;
							m_strTURNToken = resp.turn_token;
							NetworkLog(ELogVerbosity::LOG_DEBUG, "Got TURN username: %s, token: %s", m_strTURNUsername.c_str(), m_strTURNToken.c_str());
						}
						catch (...)
						{

						}

						if (m_pLobbyMesh != nullptr)
						{
							m_pLobbyMesh->SetTurnCredentials(m_strTURNUsername, m_strTURNToken);
						}

						// for safety
						if (TheGenOnlineGame != nullptr)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "NGMP_OnlineServices_LobbyInterface::JoinLobby - Safety check - Expected NGMPGame to be null by now, it wasn't so forcefully destroying");
							delete TheGenOnlineGame;
							TheGenOnlineGame = nullptr;
						}

						// TODO_NGMP: Cleanup game + dont store 2 ptrs
						if (TheGenOnlineGame == nullptr)
						{
							TheGenOnlineGame = new GenOnlineGame();

							if (lobbyInfo.lobby_type == ELobbyType::QuickMatch)
							{
								TheGenOnlineGame->markGameAsQM();
							}

							// set in game, this actually means in lobby... not in game play, and is necessary to start the game
							TheGenOnlineGame->setInGame();

							// set some initial dummy data so the game doesnt balk, we'll do UpdateRoomDataCache immediately below before invoking callback and doing the UI transition, user will never see it
							TheGenOnlineGame->setStartingCash(TheGlobalData->m_defaultStartingCash);

							// dont need to do these here, updateroomdatacache does it for us
							//TheGenOnlineGame->SyncWithLobby(m_CurrentLobby);
							//TheGenOnlineGame->UpdateSlotsFromCurrentLobby();

							// TODO_NGMP: Rest of these
							/*
							TheGenOnlineGame.setExeCRC(info->getExeCRC());
							TheGenOnlineGame.setIniCRC(info->getIniCRC());

							TheGenOnlineGame.setHasPassword(info->getHasPassword());
							TheGenOnlineGame.setGameName(info->getGameName());
							*/
						}

						OnJoinedOrCreatedLobby(false, [=](bool bSuccess)
							{
								GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
								if (pLobbyInterface == nullptr || pLobbyInterface != this || pLobbyInterface->m_LobbyJoinGeneration.load() != lobbyJoinGeneration)
								{
									return;
								}

								pLobbyInterface->m_bAttemptingToJoinLobby = false;
								if (pLobbyInterface->m_callbackJoinedLobby != nullptr)
								{
									pLobbyInterface->m_callbackJoinedLobby(bSuccess ? EJoinLobbyResult::JoinLobbyResult_Success : EJoinLobbyResult::JoinLobbyResult_JoinFailed);
								}
							});

						// get latest lobby info immediately
						UpdateRoomDataCache([=](bool bSuccess)
							{

							});
					}
					else if (statusCode == 401)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Couldn't join lobby, unauthorized, probably the wrong password");
					}
					else if (statusCode == 404)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to join lobby: Lobby not found");
					}
					else if (statusCode == 406)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to join lobby: Lobby is full");
					}
                    else if (statusCode == 417)
                    {
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to join lobby: Anticheat mismatch");
                    }


					if (JoinResult != EJoinLobbyResult::JoinLobbyResult_Success)
					{
						GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
						if (pLobbyInterface != nullptr && pLobbyInterface->m_callbackJoinedLobby != nullptr)
						{
							pLobbyInterface->m_callbackJoinedLobby(JoinResult);
						}
						m_bAttemptingToJoinLobby = false;
					}


				};

			GenOnlineRealtime::JoinLobbyRequest request;
			request.lobby_id = lobbyInfo.lobbyID;
			if (!strPassword.empty())
			{
				request.password = local_to_utf8(strPassword);
			}
			request.has_map = bHasMap;
			InvokeLobbyHub(request, [onJoinResponse](const GenOnlineRealtime::InvokeOutcome& outcome)
				{
					int statusCode = 0;
					std::string strBody;
					JoinResponseFromOutcome(outcome, statusCode, strBody);
					onJoinResponse(outcome.delivered, statusCode, strBody, nullptr);
				});
		});

	return true;
}

bool GenOnlineLobbyInterface::AreLocalLobbyPeersConnected()
{
	auto* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (!IsInLobby() || auth == nullptr || m_pLobbyMesh == nullptr)
	{
		return false;
	}

	bool foundSelf = false;
	for (const auto& member : m_CurrentLobby.members)
	{
		if (!member.IsHuman())
		{
			continue;
		}
		if (member.user_id == auth->GetUserID())
		{
			foundSelf = true;
			continue;
		}
		auto* connection = m_pLobbyMesh->GetConnectionForUser(member.user_id);
		if (connection == nullptr || connection->GetState() != EConnectionState::CONNECTED_DIRECT)
		{
			return false;
		}
	}
	return foundSelf;
}

bool GenOnlineLobbyInterface::HasFailedLobbyPeers()
{
	auto* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (m_CurrentLobby.lobby_type != ELobbyType::CustomGame || auth == nullptr || !IsInLobby())
	{
		return false;
	}

	const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	for (const auto& member : m_CurrentLobby.members)
	{
		if (!member.IsHuman() || member.user_id == auth->GetUserID())
		{
			continue;
		}
		auto* connection = m_pLobbyMesh != nullptr ? m_pLobbyMesh->GetConnectionForUser(member.user_id) : nullptr;
		if (connection == nullptr)
		{
			continue;
		}

		const EConnectionState state = connection->GetState();
		if (state == EConnectionState::CONNECTION_FAILED || state == EConnectionState::CONNECTION_DISCONNECTED)
		{
			return true;
		}

		// A peer still not connecting after the reconnect timeout counts as failed.
		if (state == EConnectionState::NOT_CONNECTED && now - connection->m_connectStartedMs > MeshConnectionTimeoutPolicy::ReconnectTimeoutMs)
		{
			return true;
		}
	}
	return false;
}

int GenOnlineLobbyInterface::RetryFailedLobbyPeers()
{
	auto* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	auto ws = GenOnlineServicesManager::GetWebSocket();
	if (auth == nullptr || ws == nullptr || !ws->IsConnected() || m_pLobbyMesh == nullptr || !IsInLobby())
	{
		return 0;
	}

	const bool inMatch = TheGenOnlineGame != nullptr && TheGenOnlineGame->isGameInProgress();
	const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	int retries = 0;
	for (const auto& member : m_CurrentLobby.members)
	{
		if (!member.IsHuman())
		{
			continue;
		}
		auto* connection = m_pLobbyMesh->GetConnectionForUser(member.user_id);
		const auto state = connection != nullptr ? connection->GetState() : EConnectionState::NOT_CONNECTED;
		if (!LobbyConnectionPolicy::CanRetry(m_CurrentLobby.lobby_type == ELobbyType::CustomGame, inMatch,
			member.user_id == auth->GetUserID(), state == EConnectionState::CONNECTED_DIRECT,
			state == EConnectionState::CONNECTING_DIRECT || state == EConnectionState::FINDING_ROUTE,
			connection != nullptr ? now - connection->m_connectStartedMs : 1000))
		{
			continue;
		}

		m_pLobbyMesh->PrepareManualRetry(member.user_id, now);
		ws->SendData_RequestSignalling(member.user_id);
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Manual retry requested lobby=%lld peer=%lld", m_CurrentLobby.lobbyID, member.user_id);
		++retries;
	}
	return retries;
}

void GenOnlineLobbyInterface::TearDownLocalLobbyState()
{
	m_bCannotConnectToLobbyPending = false;
	ResetJoinOrder();

	// invalidate any join/create still in flight
	++m_LobbyJoinGeneration;
	m_bAttemptingToJoinLobby = false;
	ResetLobbyTryingToJoin();

	std::shared_ptr<WebSocket> pWS = GenOnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->ClearPendingSignals();
	}

	// reset host migration flags
	ResetHostMigrationFlags();

	GenOnlinePluginHost::Anticheat::EndSession();

	// kill mesh
	if (m_pLobbyMesh != nullptr)
	{
		m_pLobbyMesh->Disconnect();
		delete m_pLobbyMesh;
		m_pLobbyMesh = nullptr;
	}

	if (TheGenOnlineGame != nullptr)
	{
		delete TheGenOnlineGame;
		TheGenOnlineGame = nullptr;
	}

	// A realtime lobby state belongs to the lobby that was left.
	m_hubLobbyState.reset();
	m_hubMatchStartLobbyID = -1;
	m_hubGameStartLobbyID = -1;
	m_MatchStart.Reset();
	m_LobbyChat.Clear();
	m_PendingLobbyHistory.Clear();
	FlushLobbyStateWaiters(false);
}

void GenOnlineLobbyInterface::LeaveCurrentLobby()
{
	TearDownLocalLobbyState();
	
	if (!IsInLobby())
	{
		return;
	}

	// leave on service
	LeaveLobbyOnService(m_CurrentLobby.lobbyID);

	// reset local data
	ResetCachedRoomData();
}

void GenOnlineLobbyInterface::ResetForMatchmakingRequeue()
{
	// The service has already removed us from the failed temporary lobby. Tear down only
	// local state here; sending the normal DELETE would cancel the server-side requeue.
	TearDownLocalLobbyState();
	ResetCachedRoomData();
}


LobbyEntry GenOnlineLobbyInterface::GetLobbyFromID(int64_t lobbyID)
{
	// TODO_NGMP: Optimize for lookup
	for (LobbyEntry& lobbyEntry : m_vecLobbies)
	{
		if (lobbyEntry.lobbyID == lobbyID)
		{
			return lobbyEntry;
		}
	}

	return LobbyEntry();
}



enum class ECreateLobbyResponseResult : int
{
	FAILED = 0,
	SUCCEEDED = 1
};

struct CreateLobbyResponse
{
	ECreateLobbyResponseResult result = ECreateLobbyResponseResult::FAILED;
	int64_t lobby_id = -1;
	std::string turn_username;
	std::string turn_token;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(CreateLobbyResponse, result, lobby_id, turn_username, turn_token)
};

void GenOnlineLobbyInterface::CreateLobby(UnicodeString strLobbyName, UnicodeString strInitialMapName, AsciiString strInitialMapPath, bool bIsOfficial, int initialMaxSize, bool bVanillaTeamsOnly, bool bTrackStats, uint32_t startingCash, bool bPassworded, std::string strPassword, bool bAllowObservers)
{
	GenOnlinePluginHost::Anticheat::EndSession();

	// shares the join generation counter with JoinLobby
	const uint64_t lobbyCreateGeneration = ++m_LobbyJoinGeneration;

	GenOnlineServicesManager::GetInstance()->GetAndParseServiceConfig([=]()
		{
			m_CurrentLobby = LobbyEntry();
			ResetJoinOrder();
			ResetHostMigrationFlags();
			m_bJoinOrderKnown = true;

			// convert
			std::string strMapName = to_utf8(strInitialMapName.str());

			// sanitize map path
			// we need to parse out the map name for custom maps... its an absolute path
			// it's safe to just get the file name, dir name and file name MUST be the same. Game enforces this
			AsciiString sanitizedMapPath = getFileName(strInitialMapPath.str());

			auto onCreateResponse = [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
				{
					try
					{
						GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
						GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

						if (pAuthInterface == nullptr || pLobbyInterface == nullptr)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] CreateLobby callback: required interface is null, aborting");
							return;
						}

						nlohmann::json jsonObject = nlohmann::json::parse(strBody);
						CreateLobbyResponse resp = jsonObject.get<CreateLobbyResponse>();

						if (pLobbyInterface != this || pLobbyInterface->m_LobbyJoinGeneration.load() != lobbyCreateGeneration)
						{
							// stale response; delete the orphaned lobby if the server created one
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Ignoring stale CreateLobby response (generation changed)");
							if (resp.result == ECreateLobbyResponseResult::SUCCEEDED)
							{
								LeaveLobbyOnService(resp.lobby_id);
							}
							return;
						}

						m_strTURNUsername = resp.turn_username;
						m_strTURNToken = resp.turn_token;
						NetworkLog(ELogVerbosity::LOG_DEBUG, "Got TURN username: %s, token: %s", m_strTURNUsername.c_str(), m_strTURNToken.c_str());

						// a mesh kept from a failed join has stale credentials
						if (m_pLobbyMesh != nullptr)
						{
							m_pLobbyMesh->SetTurnCredentials(m_strTURNUsername, m_strTURNToken);
						}

						if (resp.result == ECreateLobbyResponseResult::SUCCEEDED)
						{
							// for safety
							if (TheGenOnlineGame != nullptr)
							{
								NetworkLog(ELogVerbosity::LOG_RELEASE, "NGMP_OnlineServices_LobbyInterface::JoinLobby - Safety check - Expected NGMPGame to be null by now, it wasn't so forcefully destroying");
								delete TheGenOnlineGame;
								TheGenOnlineGame = nullptr;
							}


							// TODO_NGMP: Cleanup game + dont store 2 ptrs
							if (TheGenOnlineGame == nullptr)
							{
								TheGenOnlineGame = new GenOnlineGame();

								// dont need to mark as QM here, service marks it for us
							}

							// reset before copy
							pLobbyInterface->ResetCachedRoomData();

							// TODO: Do we need more info here? we kick off a lobby GET immediately, maybe that should be the response to creating

							// store the basic info (lobby id), we will immediately kick off a full get				
							m_CurrentLobby.lobbyID = resp.lobby_id;
							m_CurrentLobby.owner = pAuthInterface->GetUserID();

							AsciiString strName = AsciiString();

							m_CurrentLobby.name = to_utf8(strLobbyName.str());
							m_CurrentLobby.map_name = std::string(strMapName);
							m_CurrentLobby.map_path = std::string(sanitizedMapPath.str());
							m_CurrentLobby.current_players = 1;
							m_CurrentLobby.max_players = initialMaxSize;
							m_CurrentLobby.passworded = bPassworded;
							m_CurrentLobby.password = strPassword;

							LobbyMemberEntry me;

							me.user_id = m_CurrentLobby.owner;
							me.display_name = pAuthInterface->GetDisplayName();
							me.m_bIsReady = true; // host is always ready

							// the host always takes the first slot
							me.m_SlotIndex = 0;
							me.m_SlotState = SlotState::SLOT_PLAYER;

							// TODO_NGMP: Remove this and just hardcode it or provide from service
							me.preferredPort = 0;

							m_CurrentLobby.members.push_back(me);

							// set in game, this actually means in lobby... not in game play, and is necessary to start the game
							TheGenOnlineGame->setInGame();

							TheGenOnlineGame->SyncWithLobby(m_CurrentLobby);
							TheGenOnlineGame->UpdateSlotsFromCurrentLobby();

							// we always need to get the enc key etc
							pLobbyInterface->OnJoinedOrCreatedLobby(false, [=](bool bSuccess)
								{
									if (pLobbyInterface->m_LobbyJoinGeneration.load() != lobbyCreateGeneration)
									{
										return;
									}

									pLobbyInterface->InvokeCreateLobbyCallback(bSuccess);

									if (bSuccess)
									{
										pLobbyInterface->ApplyLocalUserPropertiesToCurrentNetworkRoom();
									}
								});
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to create lobby!\n");

							pLobbyInterface->InvokeCreateLobbyCallback(resp.result == ECreateLobbyResponseResult::SUCCEEDED);
						}


					}
					catch (...)
					{

					}

				};

			// The service takes the build identity and the anticheat id from the session.
			GenOnlineRealtime::CreateLobbyRequest request;
			request.name = to_utf8(strLobbyName.str());
			request.map.name = strMapName;
			request.map.path = local_to_utf8(sanitizedMapPath.str());
			request.map.official = bIsOfficial;
			request.max_players = initialMaxSize;
			request.visibility = "public";
			if (bPassworded)
			{
				request.password = local_to_utf8(strPassword);
			}
			request.preferred_port = 0;
			request.settings.starting_cash = (int)startingCash;
			request.settings.vanilla_teams = bVanillaTeamsOnly;
			request.settings.track_stats = bTrackStats;
			request.settings.allow_observers = bAllowObservers;
			request.settings.max_camera_height = GenOnlineServicesManager::Settings.Camera_GetMaxHeight_WhenLobbyHost();
			InvokeLobbyHub(request, [onCreateResponse](const GenOnlineRealtime::InvokeOutcome& outcome)
				{
					onCreateResponse(outcome.delivered, outcome.Succeeded() ? 200 : 400, CreateResponseFromOutcome(outcome), nullptr);
				});
		});
}

void GenOnlineLobbyInterface::OnJoinedOrCreatedLobby(bool bAlreadyUpdatedDetails, std::function<void(bool)> fnCallback)
{
	// begin AC
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Begin Session 0");

	GenOnlinePluginHost::Anticheat::BeginSession();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Begin Session End");

	// join the network mesh too
	if (m_pLobbyMesh == nullptr)
	{
		NetworkMesh* pNewMesh = new NetworkMesh();
		if (!pNewMesh->IsInitialized())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to initialize network mesh for lobby");
			delete pNewMesh;
			m_bAttemptingToJoinLobby = false;

			QueueCannotConnectToLobby();

			if (fnCallback != nullptr)
			{
				fnCallback(false);
			}
			return;
		}
		m_pLobbyMesh = pNewMesh;
	}

	m_bMarkedGameAsFinished = false;

	// TODO_NGMP: We need this on create, but this is a double call on join because we already got this info
	// must be done in a callback, this is an async function
	if (!bAlreadyUpdatedDetails)
	{
		UpdateRoomDataCache([=](bool bSuccess)
			{
				fnCallback(bSuccess);
			});
	}


}

void GenOnlineLobbyInterface::LeaveLobbyOnService(int64_t lobbyID)
{
	// LeaveLobby leaves the lobby the session is in.
	InvokeLobbyHub(GenOnlineRealtime::LeaveLobbyRequest(), [lobbyID](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			if (!outcome.Succeeded())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] LeaveLobby for %lld failed: %s", lobbyID, outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
			}
		});
}

// The hub sends lobby list changes to watchers, so a search after the first one reads the cache.
void GenOnlineLobbyInterface::SearchForLobbiesRealtime()
{
	if (m_bWatchingLobbyList)
	{
		CompleteRealtimeLobbySearch(true);
		return;
	}

	InvokeLobbyHub(GenOnlineRealtime::WatchLobbyListRequest(), [this](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineRealtime::WatchLobbyListResult result;
			const bool bWatching = outcome.Data(result);
			if (bWatching)
			{
				m_hubLobbyList = result.lobbies;
				m_bWatchingLobbyList = true;
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] WatchLobbyList failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
			}
			CompleteRealtimeLobbySearch(bWatching);
		});
}

void GenOnlineLobbyInterface::CompleteRealtimeLobbySearch(bool bSucceeded)
{
	// A failed watch is retried after the next Ready, which marks the list dirty.
	if (bSucceeded)
	{
		std::vector<LobbyEntry> lobbies;
		for (const GenOnlineRealtime::LobbySummary& summary : m_hubLobbyList)
		{
			lobbies.push_back(LobbyEntryFromSummary(summary));
		}
		m_vecLobbies = std::move(lobbies);
	}

	const std::function<void(std::vector<LobbyEntry>)> completionCallback = m_fnCallbackSearchForLobbiesComplete;
	const std::vector<LobbyEntry> lobbies = m_vecLobbies;
	m_bSearchInProgress.store(false);
	if (completionCallback != nullptr)
	{
		completionCallback(lobbies);
	}
}

void GenOnlineLobbyInterface::StopWatchingLobbyList()
{
	if (!m_bWatchingLobbyList)
	{
		return;
	}
	m_bWatchingLobbyList = false;
	m_hubLobbyList.clear();
	InvokeLobbyHub(GenOnlineRealtime::UnwatchLobbyListRequest(), nullptr);
}

void GenOnlineLobbyInterface::OnRealtimeReady()
{
	// A new connection has no lobby list subscription.
	m_bWatchingLobbyList = false;
	m_hubLobbyList.clear();
	SetLobbyListDirty();

	// A rejoin restores the lobby chat as ChannelHistory after LobbyState. Live messages wait for it.
	if (IsInLobby())
	{
		m_LobbyChat.Start("lobby:" + std::to_string(m_CurrentLobby.lobbyID));
		m_LobbyChatHistoryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyListUpsert(const GenOnlineRealtime::LobbySummary& lobby)
{
	if (!m_bWatchingLobbyList)
	{
		return;
	}
	for (GenOnlineRealtime::LobbySummary& entry : m_hubLobbyList)
	{
		if (entry.id == lobby.id)
		{
			entry = lobby;
			SetLobbyListDirty();
			return;
		}
	}
	m_hubLobbyList.push_back(lobby);
	SetLobbyListDirty();
}

void GenOnlineLobbyInterface::OnRealtimeLobbyListRemove(int64_t lobbyID)
{
	if (!m_bWatchingLobbyList)
	{
		return;
	}
	for (auto it = m_hubLobbyList.begin(); it != m_hubLobbyList.end(); ++it)
	{
		if (it->id == lobbyID)
		{
			m_hubLobbyList.erase(it);
			SetLobbyListDirty();
			return;
		}
	}
}

// The hub pushes the lobby state after every change, so a refresh applies the latest state or waits for it.
void GenOnlineLobbyInterface::UpdateRoomDataCacheRealtime(std::function<void(bool)> fnCallback)
{
	if (m_CurrentLobby.lobbyID == -1 || TheGenOnlineGame == nullptr)
	{
		return;
	}

	if (m_hubLobbyState.has_value() && m_hubLobbyState->id == m_CurrentLobby.lobbyID)
	{
		const GenOnlineRealtime::LobbyState lobby = *m_hubLobbyState;
		ApplyRealtimeLobbyState(lobby);
		if (fnCallback != nullptr)
		{
			fnCallback(true);
		}
		return;
	}

	if (fnCallback != nullptr)
	{
		m_hubLobbyStateWaiters.push_back(fnCallback);
	}
}

void GenOnlineLobbyInterface::ApplyRealtimeLobbyState(const GenOnlineRealtime::LobbyState& lobby)
{
	LobbyEntry lobbyEntry;
	std::vector<LobbyMemberEntry> members;
	LobbyEntryFromState(lobby, lobbyEntry, members);

	// The service never sends the password, and the seed arrives with the game start.
	lobbyEntry.password = m_CurrentLobby.password;
	lobbyEntry.rng_seed = m_CurrentLobby.rng_seed;

	ApplyLobbyUpdate(lobbyEntry, members, ++m_LobbyUpdateRequestSeq);
}

void GenOnlineLobbyInterface::FlushLobbyStateWaiters(bool bSucceeded)
{
	std::vector<std::function<void(bool)>> waiters;
	waiters.swap(m_hubLobbyStateWaiters);
	for (const std::function<void(bool)>& waiter : waiters)
	{
		waiter(bSucceeded);
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyState(const GenOnlineRealtime::LobbyState& lobby)
{
	// Kept for a join whose result arrives after the state.
	m_hubLobbyState = lobby;
	if (lobby.id != m_CurrentLobby.lobbyID || TheGenOnlineGame == nullptr)
	{
		return;
	}
	ApplyRealtimeLobbyState(lobby);
	FlushLobbyStateWaiters(true);

	// A client that missed GameStart, for example across a reconnect, starts from the start data of the lobby.
	GenOnlineRealtime::GameStartEvent start;
	if (GenOnlineRealtime::LobbyStateStart(lobby, start))
	{
		StartMatchFromService(start, "LobbyState", true);
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyLeft(const GenOnlineRealtime::LobbyLeftEvent& event)
{
	if (event.lobby_id != m_CurrentLobby.lobbyID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] LobbyLeft for lobby %lld, the current lobby is %lld", event.lobby_id, m_CurrentLobby.lobbyID);
		return;
	}

	FlushLobbyStateWaiters(false);
	if (event.reason == "kicked")
	{
		// A lobby state without the local user has already shown the kick.
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		const int64_t myUserID = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();
		bool bStillMember = false;
		for (const LobbyMemberEntry& member : m_CurrentLobby.members)
		{
			if (member.IsHuman() && member.user_id == myUserID)
			{
				bStillMember = true;
				break;
			}
		}
		if (bStillMember)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "We were kicked from the lobby...");
			OnKickedFromLobby();
		}
	}
	else if (event.reason == "closed")
	{
		if (TheGenOnlineGame != nullptr)
		{
			HandleCurrentLobbyGone();
		}
		else
		{
			ResetForMatchmakingRequeue();
		}
	}
	// "left" answers a LeaveLobby of this client, which already tore the lobby down.
}

// The legacy reason code of a refused StartGame, which picks the text of the setup menu.
static std::string StartRefusedReason(const GenOnlineRealtime::InvokeOutcome& outcome)
{
	if (!outcome.delivered)
	{
		return std::string();
	}
	const std::string code = outcome.Code();
	if (code == "lobby.not_ready")
	{
		return "not_ready";
	}
	if (code == "lobby.mesh_incomplete")
	{
		return "mesh_incomplete";
	}
	if (code == "lobby.not_host")
	{
		return "not_owner";
	}
	if (code == "lobby.already_started")
	{
		return "already_started";
	}
	if (code == "not_found" || code == "lobby.not_member")
	{
		return "no_lobby";
	}
	return code;
}

void GenOnlineLobbyInterface::OnRealtimeStartGameOutcome(const GenOnlineRealtime::InvokeOutcome& outcome)
{
	// A started countdown arrives as CountdownStarted.
	if (outcome.Succeeded())
	{
		return;
	}
	const std::string strReason = StartRefusedReason(outcome);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] The service refused the match start (%s)", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
	if (m_callbackStartGameRejected != nullptr)
	{
		m_callbackStartGameRejected(strReason, std::vector<std::pair<int64_t, int64_t>>());
	}
}

void GenOnlineLobbyInterface::OnRealtimeCountdownStarted(const GenOnlineRealtime::CountdownStartedEvent& event)
{
	if (event.lobby_id != m_CurrentLobby.lobbyID || m_callbackLobbyEvent == nullptr)
	{
		return;
	}
	LobbyEvent lobbyEvent;
	lobbyEvent.event = "countdown_started";
	lobbyEvent.duration_ms = GenOnlineRealtime::SecondsToMilliseconds(event.duration_s);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Lobby event %s", lobbyEvent.event.c_str());
	m_callbackLobbyEvent(lobbyEvent);
}

void GenOnlineLobbyInterface::OnRealtimeCountdownCancelled(const GenOnlineRealtime::CountdownCancelledEvent& event)
{
	if (event.lobby_id != m_CurrentLobby.lobbyID || m_callbackLobbyEvent == nullptr)
	{
		return;
	}
	LobbyEvent lobbyEvent;
	lobbyEvent.event = "countdown_cancelled";
	// The setup menu names a player that left member_left.
	lobbyEvent.reason = event.reason == "player_left" ? std::string("member_left") : event.reason;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Lobby event %s (%s, %d missing)", lobbyEvent.event.c_str(), event.reason.c_str(), event.missing.has_value() ? (int)event.missing->size() : 0);
	m_callbackLobbyEvent(lobbyEvent);
}

void GenOnlineLobbyInterface::OnRealtimeGameStart(const GenOnlineRealtime::GameStartEvent& event)
{
	StartMatchFromService(event, "GameStart", false);
}

void GenOnlineLobbyInterface::StartMatchFromService(const GenOnlineRealtime::GameStartEvent& event, const char* szSource, bool bStandsForMatchStart)
{
	if (event.lobby_id != m_CurrentLobby.lobbyID || TheGenOnlineGame == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Ignoring %s for lobby %lld, the current lobby is %lld", szSource, event.lobby_id, m_CurrentLobby.lobbyID);
		return;
	}

	// GameStart and LobbyState.start carry the same start, which starts the match once.
	if (!m_MatchStart.Begin(event.match_id))
	{
		NetworkLog(ELogVerbosity::LOG_DEBUG, "[LOBBY] Match %lld already started, ignoring %s", event.match_id, szSource);
		return;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Starting match %lld from %s", event.match_id, szSource);

	// The service and the other players have already started, so a missing link is only logged.
	if (m_CurrentLobby.lobby_type == ELobbyType::CustomGame && !AreLocalLobbyPeersConnected())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[LOBBY] Starting with a required peer connection missing");
	}

	m_CurrentLobby.match_id = (uint64_t)event.match_id;
	m_CurrentLobby.rng_seed = (int)event.rng_seed;
	m_CurrentMatchID = m_CurrentLobby.match_id;
	TheGenOnlineGame->SyncWithLobby(m_CurrentLobby);

	if (!event.screenshot_url.isNull)
	{
		GenOnlineServicesManager::GetInstance()->SetScreenshotS3URI_StartMatch(event.screenshot_url.value.c_str());
	}

	if (m_callbackStartGamePacket != nullptr)
	{
		m_callbackStartGamePacket();
	}

	if (m_CurrentLobby.lobby_type == ELobbyType::QuickMatch)
	{
		if (bStandsForMatchStart)
		{
			m_hubMatchStartLobbyID = event.lobby_id;
		}
		m_hubGameStartLobbyID = event.lobby_id;
		StartQuickMatchWhenReady();
	}
}

void GenOnlineLobbyInterface::HandleMatchFound(int64_t lobbyID, const std::string& mapPath)
{
	InvokeMatchmakingMatchFoundCallback();

	// TODO_QUICKMATCH: Only if really in quickmatch

	// TODO_QUICKMATCH: We need to retrieve this info instead
	// basic info needed to join
	LobbyEntry lobbyEntry;
	lobbyEntry.lobbyID = lobbyID;
	lobbyEntry.map_path = mapPath;
	// A found match always joins a quick match lobby, so the lobby type is known before the first lobby state.
	lobbyEntry.lobby_type = ELobbyType::QuickMatch;

	if (JoinLobby(lobbyEntry, std::string()))
	{
		InvokeMatchmakingMessageCallback("Joining QuickMatch Lobby");
	}
	else
	{
		InvokeMatchmakingMessageCallback("Could not join the QuickMatch lobby, another join is still in progress");
	}
}

void GenOnlineLobbyInterface::HandleMatchSetupProgress(int64_t timeoutMs, int64_t countdownMs)
{
	static constexpr int kMinMatchSetupTimeoutMs = 1000;
	static constexpr int kMaxMatchSetupTimeoutMs = 120000;

	if (timeoutMs <= 0)
	{
		return;
	}
	if (timeoutMs < kMinMatchSetupTimeoutMs)
	{
		timeoutMs = kMinMatchSetupTimeoutMs;
	}
	else if (timeoutMs > kMaxMatchSetupTimeoutMs)
	{
		timeoutMs = kMaxMatchSetupTimeoutMs;
	}
	InvokeMatchmakingSetupProgressCallback((int)timeoutMs, (int)countdownMs);
}

void GenOnlineLobbyInterface::OnRealtimeMatchmakingStatus(const GenOnlineRealtime::MatchmakingStatusEvent& event)
{
	InvokeMatchmakingMessageCallback(GenOnlineRealtime::MatchmakingStatusText(event.code, event.params));
	if (event.code != "matchmaking.left")
	{
		return;
	}
	// The player is no longer queued. A quick match lobby that was joined or is being joined belongs to the abandoned match.
	if (m_bAttemptingToJoinLobby || (IsInLobby() && m_CurrentLobby.lobby_type == ELobbyType::QuickMatch))
	{
		ResetForMatchmakingRequeue();
	}
	InvokeMatchmakingLeftCallback();
}

void GenOnlineLobbyInterface::OnRealtimeMatchStart(const GenOnlineRealtime::MatchStartEvent& event)
{
	if (event.lobby_id != m_CurrentLobby.lobbyID)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] Ignoring MatchStart for lobby %lld, the current lobby is %lld", event.lobby_id, m_CurrentLobby.lobbyID);
		return;
	}
	m_hubMatchStartLobbyID = event.lobby_id;
	StartQuickMatchWhenReady();
}

// A quick match starts once both MatchStart and GameStart arrived, because GameStart carries the seed and the match id.
void GenOnlineLobbyInterface::StartQuickMatchWhenReady()
{
	if (m_hubMatchStartLobbyID != m_CurrentLobby.lobbyID || m_hubGameStartLobbyID != m_CurrentLobby.lobbyID)
	{
		return;
	}
	m_hubMatchStartLobbyID = -1;
	m_hubGameStartLobbyID = -1;
	InvokeMatchmakingStartGameCallback();
}

void GenOnlineLobbyInterface::OnRealtimeMatchRequeue(const GenOnlineRealtime::MatchRequeueEvent& event)
{
	InvokeMatchmakingMessageCallback(GenOnlineRealtime::MatchRequeueText(event.reason));
	ResetForMatchmakingRequeue();
	InvokeMatchmakingRequeueCallback();
}

bool GenOnlineLobbyInterface::EnsureLobbyChat()
{
	if (!IsInLobby())
	{
		return false;
	}
	const std::string channelID = "lobby:" + std::to_string(m_CurrentLobby.lobbyID);
	if (m_LobbyChat.ChannelID() != channelID)
	{
		m_LobbyChat.Start(channelID);
		m_LobbyChat.ReleaseWaiting();
	}
	return true;
}

void GenOnlineLobbyInterface::OnRealtimeLobbyMessageCreate(const GenOnlineRealtime::ChatMessage& message)
{
	if (!EnsureLobbyChat())
	{
		return;
	}
	for (const GenOnlineRealtime::ChannelChatLine& line : m_LobbyChat.AddLive(message))
	{
		ShowLobbyChatLine(line);
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyHistory(const GenOnlineRealtime::ChannelHistoryEvent& event)
{
	// The history of a join comes before the JoinLobby Result, so it waits for the lobby chat to start.
	if (!IsInLobby())
	{
		if (m_bAttemptingToJoinLobby)
		{
			m_PendingLobbyHistory.Hold(event);
		}
		return;
	}
	if (!EnsureLobbyChat())
	{
		return;
	}
	ShowLobbyChatUpdate(m_LobbyChat.AddHistory(event));
}

void GenOnlineLobbyInterface::StartJoinedLobbyChat()
{
	const std::string channelID = "lobby:" + std::to_string(m_CurrentLobby.lobbyID);
	m_LobbyChat.Start(channelID);
	if (!m_PendingLobbyHistory.IsHolding(channelID))
	{
		m_PendingLobbyHistory.Clear();
		m_LobbyChat.ReleaseWaiting();
		return;
	}
	m_LobbyChatHistoryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

void GenOnlineLobbyInterface::ShowLobbyChatUpdate(const GenOnlineRealtime::ChannelChatUpdate& update)
{
	for (uint32_t key : update.removed)
	{
		if (m_OnChatMessageRemovedCallback != nullptr)
		{
			m_OnChatMessageRemovedCallback(key);
		}
	}
	for (const GenOnlineRealtime::ChannelChatLine& line : update.shown)
	{
		ShowLobbyChatLine(line);
	}
}

void GenOnlineLobbyInterface::OnRealtimeLobbyMessageDelete(const GenOnlineRealtime::MessageDeleteEvent& event)
{
	const uint32_t key = m_LobbyChat.Remove(event.channel_id, event.message_id);
	if (key != 0 && m_OnChatMessageRemovedCallback != nullptr)
	{
		m_OnChatMessageRemovedCallback(key);
	}
}

void GenOnlineLobbyInterface::ShowLobbyChatLine(const GenOnlineRealtime::ChannelChatLine& line)
{
	if (m_OnChatCallback == nullptr && m_OnChatMessageCallback == nullptr)
	{
		return;
	}

	const GenOnlineRealtime::ChatMessage& message = line.message;
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
	// Only live chat goes to the stats exporter, so a replayed history line is not exported again.
	if (line.live)
	{
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		const bool bIsLocal = pAuthInterface != nullptr && pAuthInterface->GetUserID() == message.author.id;
		StatsExporterLiveChatOnline(STATS_EXPORTER_CHAT_GAME_LOBBY, message.author.id, message.author.name.c_str(), message.content.c_str(), bAction, bAnnouncement, bIsLocal);
	}
#endif
	if (m_OnChatMessageCallback != nullptr)
	{
		m_OnChatMessageCallback(unicodeStr, color, line.key);
	}
	else
	{
		m_OnChatCallback(unicodeStr, color);
	}
}
