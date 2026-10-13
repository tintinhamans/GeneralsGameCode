/*
**	Command & Conquer Generals Zero Hour(tm)
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

#include "Common/StatsExporterLive.h"
#include "Common/StatsExporterPipe.h"

#include "Common/GameCommon.h"		// LOGICFRAMES_PER_SECOND, Relationship
#include "Common/RandomValue.h"		// GetGameLogicRandomSeed, match_id source, private-state generator functions
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ScoreKeeper.h"
#include "Common/Money.h"
#include "Common/Energy.h"
#include "Common/AsciiString.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameNetwork/GameInfo.h"		// TheGameInfo, GameSlot
#include "GameLogic/TerrainLogic.h"	// start waypoints
#include "Common/MultiplayerSettings.h"
#include "Common/MessageStream.h"	// GameMessage types
#include "Common/Science.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "Common/FramePacer.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "Common/Recorder.h"
#include "GameNetwork/GenOnline/GenOnlineDefines.h"	// GENONLINE_IBRA_STARTING_POS_LOGIC
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"	// TheLAN
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/NetworkDefs.h"	// MAX_SLOTS
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// NetworkLog, to_utf8, from_utf8
#include "GameNetwork/GenOnline/GenOnlineGame.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineStatsInterface.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"
#include <steam/steamnetworkingsockets.h>
#include "GameNetwork/GameSpy/PersistentStorageThread.h"	// PSPlayerStats
#include "GameNetwork/NetworkInterface.h"	// TheNetwork
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUICallbacks.h"	// in-game, game lobby and room chat submit functions
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"

#include <malloc.h>	// _aligned_malloc
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#include "StatsExporter.pb.h"

namespace
{
	// Logic frames between StatsSnapshots.
	const UnsignedInt kSampleIntervalFrames = 15;

	// Production queues: structures per player and entries per structure that are sent; the rest is dropped.
	// Only structures with something queued are sent, in object-list order. A queue longer than the cap loses its tail.
	const int kMaxProductionStructures = 16;
	const int kMaxProductionEntries = 6;

	// Map-level tech structures reported per snapshot, and per-player lists.
	const size_t kMaxTechStructures = 64;
	const size_t kMaxPlayerTechStructures = 32;
	const size_t kMaxUpgrades = 64;
	const size_t kMaxSelectedObjects = 64;

	// Position grid: fixed 32x32 cells per player over the playable extent.
	const Int kGridSize = 32;

	// Supply sources reported per snapshot; non-empty ones first.
	const size_t kMaxSupplySources = 64;

	// Purchased sciences sent per player.
	const size_t kMaxSciences = 64;

	// Players the per-player counters track.
	const Int kMaxCounterPlayers = 16;

	// Per-snapshot event cap; events beyond it stay pending for the next snapshot.
	const size_t kMaxEventsPerSnapshot = 128;

	// Pending-event cap; once full, new events are dropped.
	const size_t kMaxPendingEvents = 512;

	// isKindOf()-derived coarse bucket.
	Bool isSuperweaponStructureTemplate(const ThingTemplate* tmpl)
	{
		return (tmpl != NULL) && tmpl->isKindOf(KINDOF_FS_SUPERWEAPON);
	}

	Bool isSuperweaponPower(const SpecialPowerTemplate* tmpl)
	{
		return (tmpl != NULL) && tmpl->hasPublicTimer();
	}

	const char* classifyThingTemplateCategory(const ThingTemplate* tmpl)
	{
		if (tmpl == NULL)
		{
			return "OTHER";
		}
		if (isSuperweaponStructureTemplate(tmpl))
		{
			return "SUPERWEAPON";
		}
		if (tmpl->isKindOf(KINDOF_STRUCTURE))
		{
			return "STRUCTURE";
		}
		if (tmpl->isKindOf(KINDOF_INFANTRY))
		{
			return "INFANTRY";
		}
		if (tmpl->isKindOf(KINDOF_VEHICLE))
		{
			return "VEHICLE";
		}
		return "OTHER";
	}

	const char* templateName(const ThingTemplate* tmpl)
	{
		return (tmpl != NULL) ? tmpl->getName().str() : "";
	}

	// -- Pending discrete-event queue: bounded, best-effort, drained by StatsExporterLiveUpdate().
	struct PendingEvent
	{
		UnsignedInt frame;		///< logic frame at enqueue time, not drain time
		Int playerIndex;
		statsexporter::StatEventKind kind;
		std::string category;
		std::string primaryName;
		std::string secondaryName;
		Bool hasLocation;
		Real x, y;
		std::string variant;
		Int intValue1;
		Int intValue2;
		UnsignedInt cost;

		PendingEvent()
			: frame(0)
			, playerIndex(-1)
			, kind(statsexporter::STATEVENT_ABILITY_USE)
			, hasLocation(FALSE)
			, x(0.0f), y(0.0f)
			, intValue1(0), intValue2(0)
			, cost(0)
		{
		}
	};

	std::vector<PendingEvent>& pendingEvents()
	{
		static std::vector<PendingEvent> s_events;
		return s_events;
	}

	void* alignedBlockAlloc(size_t size)
	{
		return _aligned_malloc(size, 16);
	}

	void alignedBlockFree(void* block, size_t)
	{
		_aligned_free(block);
	}

	// The engine's operator new is only 4-byte aligned, but protobuf RepeatedField needs 8-byte aligned heap pointers for its tag bits.
	// So every long-lived message lives on this process-lifetime arena, whose blocks bypass the engine's operator new.
	// The Arena object itself is tagged the same way, so it must not come from the engine's operator new either.
	google::protobuf::Arena& exporterArena()
	{
		static google::protobuf::Arena* s_arena = NULL;
		if (s_arena == NULL)
		{
			google::protobuf::ArenaOptions options;
			options.start_block_size = 256 * 1024;
			options.block_alloc = &alignedBlockAlloc;
			options.block_dealloc = &alignedBlockFree;
			void* storage = _aligned_malloc(sizeof(google::protobuf::Arena), 16);
			s_arena = new (storage) google::protobuf::Arena(options);	// intentionally never freed
		}
		return *s_arena;
	}

	// Arena messages are reused across samples and never deleted; Clear() keeps repeated elements and strings, so the arena stops growing after the first samples.
	template <typename T>
	T& arenaMessage(T*& slot)
	{
		if (slot == NULL)
		{
			slot = google::protobuf::Arena::Create<T>(&exporterArena());
		}
		return *slot;
	}

	statsexporter::StatsSnapshot& snapshotMessage()
	{
		static statsexporter::StatsSnapshot* s_message = NULL;
		return arenaMessage(s_message);
	}

	// The stall marker never adds repeated scalars, but it is a StatsSnapshot, so it is arena-allocated too.
	statsexporter::StatsSnapshot& stallMessage()
	{
		static statsexporter::StatsSnapshot* s_message = NULL;
		return arenaMessage(s_message);
	}

	statsexporter::MatchInfo& matchInfoMessage()
	{
		static statsexporter::MatchInfo* s_message = NULL;
		return arenaMessage(s_message);
	}

	std::string& encodeBuffer()
	{
		static std::string s_buffer;
		return s_buffer;
	}

	// -- MatchInfo state: one match at a time, cached at STARTED and reused by ENDED.
	Bool s_matchActive = FALSE;
	Bool s_matchEndedSent = FALSE;
	std::string s_matchId;
	statsexporter::MatchMode s_matchMode = statsexporter::MATCH_MODE_UNKNOWN;
	std::string s_matchMapName;
	UnsignedInt s_matchStartUnixTime = 0;
	UnsignedInt s_matchStartFrame = 0;
	// Connection generation a MatchInfo STARTED was last sent under; a different one means a (re)connect.
	UnsignedInt s_matchInfoConnectionGen = 0;

	Bool s_loggedOversizedFrame = FALSE;

	// Per-player counters for this match: executed commands (rolling 60 one-second buckets of logic time) and the
	// cumulative build cost destroyed and lost. Reset when a match begins.
	UnsignedInt s_actionsTotal[16];
	UnsignedInt s_actionBucket[16][60];
	UnsignedInt s_actionBucketSecond[16][60];
	UnsignedInt s_valueDestroyed[16];
	UnsignedInt s_valueLost[16];
	UnsignedInt s_kills[16][4];		// by victim category: infantry, vehicle, aircraft, structure
	UnsignedInt s_losses[16][4];
	UnsignedInt s_spent[16][4];		// units, structures, upgrades (cash), sciences (general points)

	struct TechRecord
	{
		ObjectID id;
		const ThingTemplate* tmpl;
		Real x, y;
		Int owner;
	};

	std::vector<TechRecord>& techRecords()
	{
		static std::vector<TechRecord> s_tech;
		return s_tech;
	}

	// Neutral tech buildings (oil derricks, hospitals, ...) and tech buildings that act as base defense when captured.
	Bool isTechStructure(const Object* obj)
	{
		return obj->isKindOf(KINDOF_TECH_BUILDING) || obj->isKindOf(KINDOF_TECH_BASE_DEFENSE);
	}

	// Time the logic has been waiting on a peer, from wall-clock time; the engine keeps no frame counter for it.
	ULONGLONG s_stallStartMs = 0;
	ULONGLONG s_lastStallSendMs = 0;

	struct HeroRecord
	{
		Int playerIndex;
		const ThingTemplate* tmpl;
		ObjectID id;
		Bool alive;
		Bool seen;
	};

	std::vector<HeroRecord>& heroRecords()
	{
		static std::vector<HeroRecord> s_heroes;
		return s_heroes;
	}

	struct SupplyRecord
	{
		ObjectID id;
		const ThingTemplate* tmpl;
		Real x, y;
		Int boxes;
	};

	std::vector<SupplyRecord>& supplyRecords()
	{
		static std::vector<SupplyRecord> s_supply;
		return s_supply;
	}

	// 0 infantry, 1 vehicle, 2 aircraft, 3 structure; aircraft first (a plane can also carry the vehicle kind), -1 for anything else.
	Int killCategory(const ThingTemplate* tmpl)
	{
		if (tmpl == NULL)
		{
			return -1;
		}
		if (tmpl->isKindOf(KINDOF_AIRCRAFT))
		{
			return 2;
		}
		if (tmpl->isKindOf(KINDOF_STRUCTURE))
		{
			return 3;
		}
		if (tmpl->isKindOf(KINDOF_INFANTRY))
		{
			return 0;
		}
		if (tmpl->isKindOf(KINDOF_VEHICLE))
		{
			return 1;
		}
		return -1;
	}

	void resetMatchCounters()
	{
		memset(s_actionsTotal, 0, sizeof(s_actionsTotal));
		memset(s_actionBucket, 0, sizeof(s_actionBucket));
		memset(s_actionBucketSecond, 0, sizeof(s_actionBucketSecond));
		memset(s_valueDestroyed, 0, sizeof(s_valueDestroyed));
		memset(s_valueLost, 0, sizeof(s_valueLost));
		memset(s_kills, 0, sizeof(s_kills));
		memset(s_losses, 0, sizeof(s_losses));
		memset(s_spent, 0, sizeof(s_spent));
		heroRecords().clear();
	}

	// Sciences are sent by name each sample; names are cached by science id so the sample does not build strings.
	const std::string& scienceName(ScienceType science)
	{
		static std::vector<std::string> s_names;
		static std::vector<Bool> s_have;
		if (science < 0)
		{
			static const std::string s_empty;
			return s_empty;
		}
		if ((size_t)science >= s_names.size())
		{
			s_names.resize((size_t)science + 1);
			s_have.resize((size_t)science + 1, FALSE);
		}
		if (!s_have[(size_t)science])
		{
			s_names[(size_t)science] = TheScienceStore->getInternalNameForScience(science).str();
			s_have[(size_t)science] = TRUE;
		}
		return s_names[(size_t)science];
	}

	// Which executed commands count as player actions: the synchronized network commands (orders, builds, selections,
	// special powers, sciences, sells). Not counted: the CRC and replay-camera messages, the SELECTED_GROUP_COMMAND prefix
	// that precedes a command, beacon text, the deprecated area selection, popup clearing, self destruct, debug messages.
	Bool isActionMessage(Int type)
	{
		if (type < GameMessage::MSG_CREATE_SELECTED_GROUP || type > GameMessage::MSG_ENABLE_RETALIATION_MODE)
		{
			return FALSE;
		}
		switch (type)
		{
			case GameMessage::MSG_SELECTED_GROUP_COMMAND:
			case GameMessage::MSG_AREA_SELECTION_DEPRECATED:
			case GameMessage::MSG_CLEAR_INGAME_POPUP_MESSAGE:
			case GameMessage::MSG_SET_BEACON_TEXT:
			case GameMessage::MSG_SET_REPLAY_CAMERA:
			case GameMessage::MSG_SELF_DESTRUCT:
			case GameMessage::MSG_LOGIC_CRC:
				return FALSE;
			default:
				return TRUE;
		}
	}

	// Commands in the last 60 logic seconds, scaled to a minute while the match is younger than that.
	UnsignedInt actionsPerMinute(Int playerIndex, UnsignedInt frame)
	{
		const UnsignedInt second = frame / LOGICFRAMES_PER_SECOND;
		UnsignedInt sum = 0;
		for (Int b = 0; b < 60; ++b)
		{
			const UnsignedInt bucketSecond = s_actionBucketSecond[playerIndex][b];
			if (bucketSecond <= second && bucketSecond + 60 > second)
			{
				sum += s_actionBucket[playerIndex][b];
			}
		}
		const UnsignedInt window = (second + 1 < 60) ? (second + 1) : 60;
		return sum * 60 / window;
	}

	// MatchInfo LOADING is built in the loading screen and kept for readers that connect during the load.
	Bool s_loadingPrepared = FALSE;
	Bool s_hasLoadingBody = FALSE;
	std::string s_loadingBody;
	UnsignedInt s_loadingSentGeneration = 0;

	// The lobby prediction last sent, kept to compare against the engine's resolved values at LOADING.
	struct PredictedSlot
	{
		Bool valid;
		Int templateIndex;
		Int color;
		Int startPos;
	};
	Bool s_predictionValid = FALSE;
	Bool s_predictionStartPositionsValid = FALSE;
	Int s_predictionSeed = 0;
	PredictedSlot s_predictedSlots[MAX_SLOTS];
	Bool s_predictedCameraSet = FALSE;
	Int s_predictedCameraStartPos = -1;

	Bool s_hasObserverCamera = FALSE;
	Int s_observerCameraStartPosition = -1;
	Int s_observerCameraLobbySlot = -1;

	void syncUiState(StatsExporterSink* sink);

	// MatchMode from GameLogic::GameMode; GAME_SHELL/GAME_NONE never get here.
	statsexporter::MatchMode mapMatchMode()
	{
		switch (TheGameLogic->getGameMode())
		{
			case GAME_SINGLE_PLAYER: return statsexporter::MATCH_MODE_SINGLE_PLAYER;
			case GAME_SKIRMISH:      return statsexporter::MATCH_MODE_SKIRMISH;
			case GAME_LAN:           return statsexporter::MATCH_MODE_LAN;
			case GAME_INTERNET:      return statsexporter::MATCH_MODE_ONLINE;
			case GAME_REPLAY:        return statsexporter::MATCH_MODE_REPLAY;
			default:                 return statsexporter::MATCH_MODE_UNKNOWN;
		}
	}

	// Unique per match in this process: the logic random seed plus the wall-clock start second.
	std::string buildMatchId()
	{
		char id[32];
		sprintf(id, "%08x-%08x", GetGameLogicRandomSeed(), (UnsignedInt)time(NULL));
		return id;
	}

	std::string narrow(const UnicodeString& src)
	{
		AsciiString ascii;
		ascii.translate(src);
		return ascii.str();
	}

	/// The player seated at the given lobby slot, or NULL.
	Player* playerFromSlot(Int slot)
	{
		for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
		{
			if (ThePlayerList->getSlotIndex(i) == slot)
			{
				return ThePlayerList->getNthPlayer(i);
			}
		}
		return NULL;
	}

	/// The local player's index (the player at this PC), or -1.
	Int localPlayerIndex()
	{
		Player* local = (ThePlayerList != NULL) ? ThePlayerList->getLocalPlayer() : NULL;
		return (local != NULL) ? local->getPlayerIndex() : -1;
	}

	// -- Generals Online data held for the current online match, captured at STARTED so ENDED and reconnects agree.
	Bool s_hasOnlineMatch = FALSE;

	statsexporter::OnlineMatch& onlineMatchMessage()
	{
		static statsexporter::OnlineMatch* s_message = NULL;
		return arenaMessage(s_message);
	}

	struct OnlinePlayerEntry
	{
		Int lobbySlot;
		statsexporter::OnlinePlayer data;
	};

	std::vector<OnlinePlayerEntry>& onlinePlayers()
	{
		static std::vector<OnlinePlayerEntry> s_players;
		return s_players;
	}

	// The Generals Online match id, 0 if the lobby does not hold one (yet).
	uint64_t currentOnlineMatchId()
	{
		GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		return (lobby != nullptr) ? lobby->GetCurrentMatchID() : 0;
	}

	UnsignedInt sumPerGeneral(const PerGeneralMap& map)
	{
		UnsignedInt total = 0;
		for (PerGeneralMap::const_iterator it = map.begin(); it != map.end(); ++it)
		{
			total += it->second;
		}
		return total;
	}

	// Only what the client already holds: the lobby entry, the lobby members and the cached stats shown in the lobby tooltip.
	void fillOnlineMatch(statsexporter::OnlineMatch& m, const LobbyEntry& entry)
	{
		m.Clear();
		m.set_match_id(currentOnlineMatchId());
		m.set_lobby_id(entry.lobbyID);
		m.set_lobby_name(entry.name);
		m.set_lobby_type(entry.lobby_type == ELobbyType::QuickMatch ? statsexporter::ONLINE_LOBBY_QUICKMATCH
			: (entry.lobby_type == ELobbyType::CustomGame ? statsexporter::ONLINE_LOBBY_CUSTOM : statsexporter::ONLINE_LOBBY_UNKNOWN));
		m.set_map_official(entry.map_official);
		m.set_starting_cash(entry.starting_cash);
		m.set_limit_superweapons(entry.limit_superweapons);
		m.set_track_stats(entry.track_stats);
		m.set_allow_observers(entry.allow_observers);
		m.set_vanilla_teams(entry.vanilla_teams);
		m.set_max_players(entry.max_players);
		m.set_max_camera_height(entry.max_cam_height);
		m.set_region(entry.region);
	}

	void fillOnlinePlayer(statsexporter::OnlinePlayer& out, GenOnlineLobbyInterface* lobby, int64_t userId)
	{
		out.set_user_id(userId);
		out.set_display_name(lobby->GetRoomMemberFromID(userId).display_name);

		GenOnlineStatsInterface* stats = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
		PSPlayerStats playerStats;
		if (stats != nullptr && stats->getPlayerStatsFromCache(userId, &playerStats))
		{
			out.set_has_stats(true);
			out.set_elo_rating(playerStats.elo_rating);
			out.set_monthly_elo_rating(playerStats.monthly_elo_rating);
			out.set_elo_num_matches(playerStats.elo_num_matches);
			out.set_total_wins(sumPerGeneral(playerStats.wins));
			out.set_total_losses(sumPerGeneral(playerStats.losses));
			out.set_total_disconnects(sumPerGeneral(playerStats.discons) + sumPerGeneral(playerStats.desyncs));
		}
	}

	void captureOnlineMatch()
	{
		s_hasOnlineMatch = FALSE;
		onlinePlayers().clear();

		if (TheGameLogic->getGameMode() != GAME_INTERNET || TheGenOnlineGame == nullptr)
		{
			return;
		}
		GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (lobby == nullptr || !lobby->IsInLobby())
		{
			return;
		}

		fillOnlineMatch(onlineMatchMessage(), lobby->GetCurrentLobby());

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(i);
			if (slot == nullptr || !slot->isHuman())
			{
				continue;
			}

			OnlinePlayerEntry playerEntry;
			playerEntry.lobbySlot = i;
			fillOnlinePlayer(playerEntry.data, lobby, slot->m_userID);
			onlinePlayers().push_back(playerEntry);
		}

		s_hasOnlineMatch = TRUE;
	}

	// One peer connection of the local client, as the lobby and in-game overlay already show it.
	void fillConnection(statsexporter::OnlineConnection* out, int64_t userId, PlayerConnection& connection)
	{
		out->set_user_id(userId);
		out->set_latency_ms(connection.GetLatency());

		const bool direct = connection.IsDirect();
		out->set_relayed(!direct);
		if (!direct)
		{
			out->mutable_endpoint()->set_relayed(true);	// the address the client holds would be the relay's, so none is exported
		}
		else if (connection.m_hSteamConnection != k_HSteamNetConnection_Invalid)
		{
			// The peer's own address as the networking library reports it for a direct connection.
			SteamNetConnectionInfo_t info;
			if (SteamNetworkingSockets()->GetConnectionInfo(connection.m_hSteamConnection, &info) && !info.m_addrRemote.IsIPv6AllZeros())
			{
				char ip[SteamNetworkingIPAddr::k_cchMaxString];
				info.m_addrRemote.ToString(ip, sizeof(ip), false);
				out->mutable_endpoint()->set_ip(ip);
				out->mutable_endpoint()->set_port(info.m_addrRemote.m_port);
			}
		}
	}

	template <typename ConnectionList>
	void fillConnections(ConnectionList* list)
	{
		NetworkMesh* mesh = GenOnlineServicesManager::GetNetworkMesh();
		if (mesh == nullptr)
		{
			return;
		}

		std::map<int64_t, PlayerConnection>& connections = mesh->GetAllConnections();
		for (std::map<int64_t, PlayerConnection>::iterator it = connections.begin(); it != connections.end(); ++it)
		{
			if (it->second.IsValid())
			{
				fillConnection(list->add_connections(), it->first, it->second);
			}
		}
	}

	// The observer camera starts on the local slot's start position (GameLogic::tryStartNewGame reads the same slot),
	// which for an observer is a spot a player holds. The engine picked it already; nothing is drawn here.
	void captureObserverCamera(GameInfo* game)
	{
		s_hasObserverCamera = FALSE;
		if (game == NULL)
		{
			return;
		}

		Int localSlot = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GameSlot* slot = game->getSlot(i);
			if (slot != NULL && slot->isOccupied() && GameLogicIsLocalSlot(game, slot))
			{
				localSlot = i;
			}
		}

		GameSlot* cameraSlot = game->getSlot(localSlot);
		if (cameraSlot == NULL || !cameraSlot->isHuman())
		{
			return;	// the map's own initial camera position is used
		}
		if (cameraSlot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER && TheGameLogic->getGameMode() != GAME_REPLAY)
		{
			return;
		}

		s_hasObserverCamera = TRUE;
		s_observerCameraStartPosition = cameraSlot->getStartPos();
		s_observerCameraLobbySlot = -1;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GameSlot* slot = game->getSlot(i);
			if (slot != NULL && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER && slot->getStartPos() == s_observerCameraStartPosition)
			{
				s_observerCameraLobbySlot = i;
				break;
			}
		}
	}

	// Fixes the match identity (id, mode, map, start time, online data); done once per match, at LOADING or at STARTED.
	void beginMatchState(GameInfo* game)
	{
		s_matchActive = TRUE;
		s_matchEndedSent = FALSE;
		s_matchMode = mapMatchMode();
		captureOnlineMatch();
		captureObserverCamera(game);
		resetMatchCounters();
		// The Generals Online match id when this is an online match and the lobby holds one now; it is fixed for the whole match.
		const uint64_t onlineMatchId = s_hasOnlineMatch ? onlineMatchMessage().match_id() : 0;
		if (onlineMatchId != 0)
		{
			char idText[32];
			sprintf(idText, "%I64u", onlineMatchId);
			s_matchId = idText;
		}
		else
		{
			s_matchId = buildMatchId();
		}
		s_matchMapName = (TheWritableGlobalData != NULL) ? TheWritableGlobalData->m_mapName.str() : "";
		s_matchStartUnixTime = (UnsignedInt)time(NULL);
		s_matchStartFrame = TheGameLogic->getFrame();
	}

	// Serializes into the reused buffer and queues the frame; an oversized message is dropped, never sent partially.
	void sendMessage(StatsExporterSink* sink, UnsignedByte frameType, const google::protobuf::MessageLite& message)
	{
		const size_t size = message.ByteSizeLong();
		if (size == 0)
		{
			return;
		}
		if (size > STATS_EXPORTER_MAX_BODY_BYTES)
		{
			if (!s_loggedOversizedFrame)
			{
				s_loggedOversizedFrame = TRUE;
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsExporter] Dropping frame type %d, %u bytes exceeds the %u byte cap", (int)frameType, (unsigned)size, (unsigned)STATS_EXPORTER_MAX_BODY_BYTES);
			}
			return;
		}

		std::string& buffer = encodeBuffer();
		buffer.resize(size);
		if (!message.SerializeToArray(&buffer[0], (int)size))
		{
			return;
		}
		sink->enqueueFrame(frameType, (const UnsignedByte*)buffer.data(), (UnsignedInt)size);
	}

	// Per-slot MatchPlayer builder for STARTED (results unknown) and ENDED (`completed` decides results).
	void buildMatchPlayers(statsexporter::MatchInfo& info, Bool completed)
	{
		Player* localPlayer = ThePlayerList->getLocalPlayer();
		Bool localVictory = completed && TheVictoryConditions != NULL && TheVictoryConditions->isLocalAlliedVictory();
		Bool localDefeat = completed && TheVictoryConditions != NULL && TheVictoryConditions->isLocalAlliedDefeat();

		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			Player* player = playerFromSlot(slot);
			if (player == NULL)
			{
				continue;
			}

			statsexporter::MatchPlayer* p = info.add_players();
			p->set_lobby_slot((uint32_t)slot);
			p->set_player_index(player->getPlayerIndex());
			p->set_name(narrow(player->getPlayerDisplayName()));
			p->set_side(player->getSide().str());
			p->set_color((uint32_t)player->getPlayerColor());
			p->set_is_human(player->getPlayerType() == PLAYER_HUMAN);
			p->set_is_observer(player->isPlayerObserver());

			p->set_team(-1);
			p->set_start_position(-1);
			p->set_is_color_random(false);
			p->set_is_side_random(false);
			if (TheGameInfo != NULL)
			{
				const Int lobbySlot = ThePlayerList->getSlotIndex(player->getPlayerIndex());
				const GameSlot* gameSlot = (lobbySlot >= 0) ? TheGameInfo->getConstSlot(lobbySlot) : NULL;
				if (gameSlot != NULL)
				{
					p->set_team(gameSlot->getTeamNumber());
					p->set_start_position(gameSlot->getStartPos());
					if (TheTerrainLogic != NULL && gameSlot->getStartPos() >= 0)
					{
						AsciiString waypointName;
						waypointName.format("Player_%d_Start", gameSlot->getStartPos() + 1);	// start waypoints are 1-based
						const Waypoint* waypoint = TheTerrainLogic->getWaypointByName(waypointName);
						if (waypoint != NULL)
						{
							p->set_has_start_location(true);
							p->set_start_x(waypoint->getLocation()->x);
							p->set_start_y(waypoint->getLocation()->y);
						}
					}
					if (s_matchMode == statsexporter::MATCH_MODE_LAN && gameSlot->getIP() != 0)
					{
						// The lobby slot address the LAN game already holds.
						char ip[32];
						sprintf(ip, "%u.%u.%u.%u", PRINTF_IP_AS_4_INTS(gameSlot->getIP()));
						p->mutable_lan_address()->set_ip(ip);
					}
					p->set_is_color_random(gameSlot->getOriginalColor() < 0);
					p->set_is_side_random(gameSlot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_RANDOM);
				}
			}

			if (player->isPlayerObserver())
			{
				p->set_result(statsexporter::PLAYER_RESULT_OBSERVER);
			}
			else if (!completed)
			{
				// No decisive result: a still-active slot left, anything else is unknown.
				p->set_result(player->isPlayerActive()
					? statsexporter::PLAYER_RESULT_LEFT
					: statsexporter::PLAYER_RESULT_UNKNOWN);
			}
			else if ((localVictory || localDefeat) && localPlayer != NULL)
			{
				// Local-player-relative, as in StatsExporterLiveMatchSummary().
				Relationship rel = (player == localPlayer) ? ALLIES : localPlayer->getRelationship(player->getDefaultTeam());
				Bool isWinner = localVictory ? (rel == ALLIES) : (rel == ENEMIES);
				p->set_result(isWinner ? statsexporter::PLAYER_RESULT_WON : statsexporter::PLAYER_RESULT_LOST);
			}
			else
			{
				p->set_result(statsexporter::PLAYER_RESULT_UNKNOWN);
			}
		}
	}

	void fillMatchInfo(statsexporter::MatchInfo& info, statsexporter::MatchPhase phase, UnsignedInt durationFrames,
		const AsciiString& replayPath, Bool replayIsPlayback, Bool completed)
	{
		info.Clear();
		info.set_phase(phase);
		info.set_match_id(s_matchId);
		info.set_mode(s_matchMode);
		info.set_map_name(s_matchMapName);
		info.set_start_unix_time(s_matchStartUnixTime);
		info.set_duration_frames(durationFrames);
		info.set_replay_path(replayPath.str());
		info.set_replay_is_playback(replayIsPlayback);
		info.set_completed(completed);
		info.set_local_player_index(localPlayerIndex());
		buildMatchPlayers(info, completed);

		if (TheTerrainLogic != NULL && TheGameLogic != NULL && !TheGameLogic->isLoadingMap())
		{
			Region3D extent;
			TheTerrainLogic->getExtent(&extent);
			if (extent.width() > 0.0f && extent.height() > 0.0f)
			{
				info.set_has_world_extent(true);
				info.set_world_min_x(extent.lo.x);
				info.set_world_min_y(extent.lo.y);
				info.set_world_max_x(extent.hi.x);
				info.set_world_max_y(extent.hi.y);
			}
		}

		if (s_hasObserverCamera)
		{
			statsexporter::ObserverCamera* camera = info.mutable_observer_camera();
			camera->set_start_position(s_observerCameraStartPosition);
			camera->set_lobby_slot(s_observerCameraLobbySlot);
			camera->set_player_index(-1);
			for (int i = 0; i < info.players_size(); ++i)
			{
				if ((Int)info.players(i).lobby_slot() == s_observerCameraLobbySlot)
				{
					camera->set_player_index(info.players(i).player_index());
				}
			}
		}

		if (s_hasOnlineMatch)
		{
			info.mutable_online()->CopyFrom(onlineMatchMessage());
			const std::vector<OnlinePlayerEntry>& entries = onlinePlayers();
			for (int i = 0; i < info.players_size(); ++i)
			{
				for (size_t j = 0; j < entries.size(); ++j)
				{
					if (entries[j].lobbySlot == (Int)info.players(i).lobby_slot())
					{
						info.mutable_players(i)->mutable_online()->CopyFrom(entries[j].data);
						break;
					}
				}
			}
		}
	}

	void pushPendingEvent(PendingEvent& event)
	{
		// Single choke point for every event hook: nothing accumulates unless a snapshot could be sent.
		StatsExporterSink* sink = StatsExporterPipeGetSink();
		if (sink == NULL || !sink->isConnected())
		{
			return;
		}

		// The shell map is a scripted game too; do not let its events leak into the next match.
		if (TheGameLogic == NULL || !TheGameLogic->isInInteractiveGame())
		{
			return;
		}

		std::vector<PendingEvent>& events = pendingEvents();
		if (events.size() >= kMaxPendingEvents)
		{
			return;
		}
		event.frame = TheGameLogic->getFrame();
		events.push_back(event);
	}

	// One (player, template) live-count entry, found by pointer during the object walk.
	struct TemplateCountRef
	{
		Int playerIndex;
		const ThingTemplate* tmpl;
		statsexporter::TemplateCount* count;
	};

	std::vector<TemplateCountRef>& templateCountRefs()
	{
		static std::vector<TemplateCountRef> s_refs;
		return s_refs;
	}
}

void StatsExporterLiveAbilityUse(Int playerIndex, const SpecialPowerTemplate* power,
	const ThingTemplate* sourceTmpl, const ThingTemplate* targetTmpl,
	const Coord3D* location, const char* variant)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_ABILITY_USE;
	event.category = isSuperweaponPower(power) ? "SUPERWEAPON" : "ABILITY";
	event.primaryName = (power != NULL) ? power->getName().str() : "";
	event.secondaryName = (sourceTmpl != NULL) ? templateName(sourceTmpl) : templateName(targetTmpl);
	if (location != NULL)
	{
		event.hasLocation = TRUE;
		event.x = location->x;
		event.y = location->y;
	}
	if (variant != NULL)
	{
		event.variant = variant;
	}
	pushPendingEvent(event);
}

void StatsExporterLiveScienceAcquired(Int playerIndex, Int rankLevel, Int sciencePurchasePoints, const char* scienceName)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_SCIENCE_PURCHASE;
	event.category = "SCIENCE";
	event.primaryName = (scienceName != NULL) ? scienceName : "";
	event.intValue1 = rankLevel;
	event.intValue2 = sciencePurchasePoints;
	pushPendingEvent(event);
}

void StatsExporterLiveBuild(Int playerIndex, const ThingTemplate* tmpl)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_BUILD;
	event.category = classifyThingTemplateCategory(tmpl);
	event.primaryName = templateName(tmpl);
	pushPendingEvent(event);
}

void StatsExporterLiveKill(Int playerIndex, const ThingTemplate* victimTmpl)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_KILL;
	event.category = classifyThingTemplateCategory(victimTmpl);
	event.primaryName = templateName(victimTmpl);
	// the same cost source as army_value
	event.cost = (victimTmpl != NULL) ? (UnsignedInt)victimTmpl->friend_getBuildCost() : 0;
	if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers)
	{
		s_valueDestroyed[playerIndex] += event.cost;
		const Int category = killCategory(victimTmpl);
		if (category >= 0)
		{
			++s_kills[playerIndex][category];
		}
	}
	pushPendingEvent(event);
}

void StatsExporterLiveLoss(Int playerIndex, const ThingTemplate* victimTmpl)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_LOSS;
	event.category = classifyThingTemplateCategory(victimTmpl);
	event.primaryName = templateName(victimTmpl);
	event.cost = (victimTmpl != NULL) ? (UnsignedInt)victimTmpl->friend_getBuildCost() : 0;
	if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers)
	{
		s_valueLost[playerIndex] += event.cost;
		const Int category = killCategory(victimTmpl);
		if (category >= 0)
		{
			++s_losses[playerIndex][category];
		}
	}
	pushPendingEvent(event);
}

void StatsExporterLiveEvacuate(Int playerIndex, const ThingTemplate* containerTmpl, UnsignedInt passengerCount)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_EVACUATE;
	event.category = classifyThingTemplateCategory(containerTmpl);
	event.primaryName = templateName(containerTmpl);
	char count[32];
	sprintf(count, "count=%u", passengerCount);
	event.variant = count;
	pushPendingEvent(event);
}

void StatsExporterLiveVeterancyChange(Int playerIndex, const ThingTemplate* tmpl, ObjectID objectId, Int oldLevel, Int newLevel)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	// Production veterancy is granted before registerObject() in Object's constructor, so a lookup
	// miss means birth time and must not report a veterancy change.
	if ((TheGameLogic == NULL) || (TheGameLogic->findObjectByID(objectId) == NULL))
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_VETERANCY_CHANGE;
	event.category = classifyThingTemplateCategory(tmpl);
	event.primaryName = templateName(tmpl);
	event.intValue1 = oldLevel;
	event.intValue2 = newLevel;
	pushPendingEvent(event);
}

void StatsExporterLiveSalvageUpgrade(Int playerIndex, const ThingTemplate* tmpl, const char* upgradeKind, Int tier)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_SALVAGE_UPGRADE;
	event.category = (upgradeKind != NULL) ? upgradeKind : "";
	event.primaryName = templateName(tmpl);
	event.intValue1 = tier;
	pushPendingEvent(event);
}

void StatsExporterLiveUpdate()
{
	static UnsignedInt s_lastSampleFrame = 0;
	static Bool s_hasSampled = FALSE;

	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL)
	{
		return;
	}

	if (TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}

	if (!TheGameLogic->isInInteractiveGame())
	{
		pendingEvents().clear();
		s_lastSampleFrame = 0;	// the next match starts again at frame 0
		s_hasSampled = FALSE;
		return;
	}

	const UnsignedInt logicFrame = TheGameLogic->getFrame();

	// UiState goes first to a new reader, ahead of MatchInfo.
	syncUiState(sink);

	// MatchInfo STARTED, resent on every (re)connect, before any snapshot of that connection.
	if (s_matchActive)
	{
		UnsignedInt connGen = sink->getConnectionGeneration();
		if (sink->isConnected() && connGen != s_matchInfoConnectionGen)
		{
			// A reader that connected during the load gets LOADING now, ahead of STARTED.
			if (s_hasLoadingBody && s_loadingSentGeneration != connGen)
			{
				sink->enqueueFrame(STATS_EXPORTER_FRAME_MATCH_INFO, (const UnsignedByte*)s_loadingBody.data(), (UnsignedInt)s_loadingBody.size());
				s_loadingSentGeneration = connGen;
			}

			statsexporter::MatchInfo& info = matchInfoMessage();
			fillMatchInfo(info, statsexporter::MATCH_PHASE_STARTED, 0, AsciiString::TheEmptyString, FALSE, FALSE);
			sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_INFO, info);
			s_matchInfoConnectionGen = connGen;
		}
	}

	// Static player data lives only in MatchInfo, so a connection's snapshots wait until its STARTED has been queued.
	if (!sink->isConnected() || !s_matchActive || s_matchInfoConnectionGen != sink->getConnectionGeneration())
	{
		return;
	}

	const UnsignedInt now = logicFrame;
	// A new match restarts the frame counter below the last sample frame.
	if (s_hasSampled && now < s_lastSampleFrame)
	{
		s_hasSampled = FALSE;
	}
	if (s_hasSampled && s_lastSampleFrame + kSampleIntervalFrames > now)
	{
		return;
	}
	s_lastSampleFrame = now;
	s_hasSampled = TRUE;

	statsexporter::StatsSnapshot& snapshot = snapshotMessage();
	snapshot.Clear();
	snapshot.set_frame(now);
	std::vector<TemplateCountRef>& countRefs = templateCountRefs();
	countRefs.clear();

	if (TheRecorder != NULL && TheRecorder->isPlaybackMode())
	{
		snapshot.set_replay_total_frames(TheRecorder->getPlaybackFrameCount());
	}
	if (TheFramePacer != NULL)
	{
		const Int speedFps = TheFramePacer->getActualLogicTimeScaleFps();
		// -1 marks an uncapped logic rate; 0 stays reserved for a frozen or halted game
		snapshot.set_game_speed_fps(speedFps >= (Int)RenderFpsPreset::UncappedFpsValue ? -1 : speedFps);
		snapshot.set_game_speed_ratio(TheFramePacer->getActualLogicTimeScaleRatio());
	}

	// The playable extent the grids cover; no grids if the map extent is not available.
	static UnsignedInt s_gridCells[MAX_SLOTS][32 * 32];
	Region3D gridExtent;
	gridExtent.zero();
	if (TheTerrainLogic != NULL)
	{
		TheTerrainLogic->getExtent(&gridExtent);
	}
	const Bool haveGrid = gridExtent.width() > 0.0f && gridExtent.height() > 0.0f;
	const Real gridCellWidth = haveGrid ? gridExtent.width() / (Real)kGridSize : 0.0f;
	const Real gridCellHeight = haveGrid ? gridExtent.height() / (Real)kGridSize : 0.0f;
	memset(s_gridCells, 0, sizeof(s_gridCells));
	std::vector<SupplyRecord>& supply = supplyRecords();
	supply.clear();
	std::vector<TechRecord>& tech = techRecords();
	tech.clear();
	std::vector<HeroRecord>& heroes = heroRecords();
	for (size_t h = 0; h < heroes.size(); ++h)
	{
		heroes[h].seen = FALSE;
	}

	// -- Economy/score fields, one entry per active slot -----------------
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		Player* player = playerFromSlot(slot);
		if (player == NULL || !player->isPlayerActive())
		{
			continue;
		}
		ScoreKeeper* scoreKeeper = player->getScoreKeeper();
		if (scoreKeeper == NULL)
		{
			continue;
		}

		statsexporter::PlayerStats* p = snapshot.add_players();
		p->set_player_index(player->getPlayerIndex());
		p->set_money(player->getMoney()->countMoney());
		p->set_energy_production(player->getEnergy()->getProduction());
		p->set_energy_consumption(player->getEnergy()->getConsumption());
		p->set_total_money_earned(scoreKeeper->getTotalMoneyEarned());
		p->set_units_built(scoreKeeper->getTotalUnitsBuilt());
		p->set_units_lost(scoreKeeper->getTotalUnitsLost());
		p->set_units_destroyed(scoreKeeper->getTotalUnitsDestroyed());
		p->set_buildings_built(scoreKeeper->getTotalBuildingsBuilt());
		p->set_buildings_lost(scoreKeeper->getTotalBuildingsLost());
		p->set_buildings_destroyed(scoreKeeper->getTotalBuildingsDestroyed());
		p->set_score(scoreKeeper->calculateScore());

		// the HUD's own income source: deposits over the last 60 seconds, kept by Money whether or not a HUD shows it
		p->set_money_per_minute(player->getMoney()->getCashPerMinute());
		p->set_rank_level(player->getRankLevel());
		p->set_skill_points(player->getSciencePurchasePoints());

		const ScienceVec& sciences = player->getSciences();
		for (size_t sc = 0; sc < sciences.size() && sc < kMaxSciences; ++sc)
		{
			p->add_sciences(scienceName(sciences[sc]));
		}

		// Completed player-level upgrades, by internal name.
		size_t upgradeCount = 0;
		for (UpgradeTemplate* upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade != NULL && upgradeCount < kMaxUpgrades; upgrade = upgrade->friend_getNext())
		{
			if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER && player->hasUpgradeComplete(upgrade))
			{
				p->add_upgrades(upgrade->getUpgradeName().str());
				++upgradeCount;
			}
		}

		const Int counterIndex = player->getPlayerIndex();
		if (counterIndex >= 0 && counterIndex < kMaxCounterPlayers)
		{
			p->set_actions_total(s_actionsTotal[counterIndex]);
			p->set_apm_current(actionsPerMinute(counterIndex, now));
			p->set_value_destroyed(s_valueDestroyed[counterIndex]);
			p->set_value_lost(s_valueLost[counterIndex]);

			statsexporter::CategoryCounts* kills = p->mutable_kills();
			kills->set_infantry(s_kills[counterIndex][0]);
			kills->set_vehicle(s_kills[counterIndex][1]);
			kills->set_aircraft(s_kills[counterIndex][2]);
			kills->set_structure(s_kills[counterIndex][3]);
			statsexporter::CategoryCounts* losses = p->mutable_losses();
			losses->set_infantry(s_losses[counterIndex][0]);
			losses->set_vehicle(s_losses[counterIndex][1]);
			losses->set_aircraft(s_losses[counterIndex][2]);
			losses->set_structure(s_losses[counterIndex][3]);

			p->set_spent_units(s_spent[counterIndex][STATS_EXPORTER_SPEND_UNITS]);
			p->set_spent_structures(s_spent[counterIndex][STATS_EXPORTER_SPEND_STRUCTURES]);
			p->set_spent_upgrades(s_spent[counterIndex][STATS_EXPORTER_SPEND_UPGRADES]);
			p->set_spent_sciences(s_spent[counterIndex][STATS_EXPORTER_SPEND_SCIENCES]);
		}
	}

	if (snapshot.players_size() == 0)
	{
		pendingEvents().clear();
		return;
	}

	// Fused live-object walk: army value, live per-type counts, superweapon structures and special-power
	// readiness in one pass over the live objects.
	for (Object* obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject())
	{
		// Supply sources are neutral, so they are read before the per-player filtering. Boxes are read the way the HUD and the AI read them.
		if (obj->isKindOf(KINDOF_SUPPLY_SOURCE) && obj->getTemplate() != NULL)
		{
			static const NameKeyType warehouseKey = TheNameKeyGenerator->nameToKey("SupplyWarehouseDockUpdate");
			SupplyWarehouseDockUpdate* warehouse = (SupplyWarehouseDockUpdate*)obj->findUpdateModule(warehouseKey);
			if (warehouse != NULL)
			{
				SupplyRecord record;
				record.id = obj->getID();
				record.tmpl = obj->getTemplate();
				record.x = obj->getPosition()->x;
				record.y = obj->getPosition()->y;
				record.boxes = warehouse->getBoxesStored();
				supply.push_back(record);
			}
		}

		// Tech structures are mostly neutral until captured, so they are read before the per-player filtering too.
		if (obj->getTemplate() != NULL && isTechStructure(obj))
		{
			TechRecord record;
			record.id = obj->getID();
			record.tmpl = obj->getTemplate();
			record.x = obj->getPosition()->x;
			record.y = obj->getPosition()->y;
			const Player* techOwner = obj->getControllingPlayer();
			record.owner = (techOwner == NULL || techOwner == ThePlayerList->getNeutralPlayer()) ? -1 : techOwner->getPlayerIndex();
			tech.push_back(record);
		}

		Player* controller = obj->getControllingPlayer();
		if (controller == NULL)
		{
			continue;
		}
		const ThingTemplate* tmpl = obj->getTemplate();
		if (tmpl == NULL)
		{
			continue;
		}

		statsexporter::PlayerStats* accumulator = NULL;
		int accumulatorIndex = -1;
		for (int i = 0; i < snapshot.players_size(); ++i)
		{
			if (snapshot.players(i).player_index() == controller->getPlayerIndex())
			{
				accumulator = snapshot.mutable_players(i);
				accumulatorIndex = i;
				break;
			}
		}
		if (accumulator == NULL)
		{
			continue;	// e.g. an eliminated/observer player with stray objects
		}

		// Army composition by class, with the same classification and cost source as kills and army_value.
		const Int armyClass = killCategory(tmpl);
		if (armyClass >= 0)
		{
			const UnsignedInt cost = (UnsignedInt)tmpl->friend_getBuildCost();
			statsexporter::CategoryValue* value = accumulator->mutable_army_value_by_class();
			statsexporter::CategoryCounts* count = accumulator->mutable_army_count_by_class();
			switch (armyClass)
			{
				case 0:
					value->set_infantry(value->infantry() + cost);
					count->set_infantry(count->infantry() + 1);
					break;
				case 1:
					value->set_vehicle(value->vehicle() + cost);
					count->set_vehicle(count->vehicle() + 1);
					break;
				case 2:
					value->set_aircraft(value->aircraft() + cost);
					count->set_aircraft(count->aircraft() + 1);
					break;
				default:
					value->set_structure(value->structure() + cost);
					count->set_structure(count->structure() + 1);
					break;
			}
		}
		if (isTechStructure(obj) && (size_t)accumulator->tech_structures_size() < kMaxPlayerTechStructures)
		{
			accumulator->add_tech_structures(tmpl->getName().str());
		}

		const Bool objectIsDead = obj->isEffectivelyDead();
		if (!objectIsDead)
		{
			// Workers, harvesters and idle workers: the idle test is the one Player uses for the idle worker button.
			if (obj->isKindOf(KINDOF_DOZER))
			{
				accumulator->set_workers(accumulator->workers() + 1);
				if (obj->getAIUpdateInterface() != NULL && obj->getAIUpdateInterface()->isIdle())
				{
					accumulator->set_idle_workers(accumulator->idle_workers() + 1);
				}
			}
			if (obj->isKindOf(KINDOF_HARVESTER))
			{
				accumulator->set_harvesters(accumulator->harvesters() + 1);
			}

			if (obj->isKindOf(KINDOF_HERO))
			{
				HeroRecord* record = NULL;
				for (size_t h = 0; h < heroes.size(); ++h)
				{
					if (heroes[h].playerIndex == controller->getPlayerIndex() && heroes[h].tmpl == tmpl)
					{
						record = &heroes[h];
						break;
					}
				}
				if (record == NULL)
				{
					HeroRecord fresh;
					fresh.playerIndex = controller->getPlayerIndex();
					fresh.tmpl = tmpl;
					fresh.id = obj->getID();
					fresh.alive = TRUE;
					fresh.seen = TRUE;
					heroes.push_back(fresh);
				}
				else
				{
					record->id = obj->getID();
					record->alive = TRUE;
					record->seen = TRUE;
				}
			}

			// Position grid: units and structures only.
			if (haveGrid && (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_VEHICLE) || obj->isKindOf(KINDOF_AIRCRAFT)))
			{
				const Coord3D* position = obj->getPosition();
				Int cellX = (Int)((position->x - gridExtent.lo.x) / gridCellWidth);
				Int cellY = (Int)((position->y - gridExtent.lo.y) / gridCellHeight);
				cellX = (cellX < 0) ? 0 : ((cellX >= kGridSize) ? kGridSize - 1 : cellX);
				cellY = (cellY < 0) ? 0 : ((cellY >= kGridSize) ? kGridSize - 1 : cellY);
				++s_gridCells[accumulatorIndex][cellY * kGridSize + cellX];
			}
		}

		accumulator->set_army_value(accumulator->army_value() + tmpl->friend_getBuildCost());
		if (isSuperweaponStructureTemplate(tmpl))
		{
			accumulator->set_live_superweapon_structure_count(accumulator->live_superweapon_structure_count() + 1);
		}

		// Live per-type count: find-or-add by (player, template).
		{
			statsexporter::TemplateCount* found = NULL;
			for (size_t i = 0; i < countRefs.size(); ++i)
			{
				if (countRefs[i].tmpl == tmpl && countRefs[i].playerIndex == controller->getPlayerIndex())
				{
					found = countRefs[i].count;
					break;
				}
			}
			if (found == NULL)
			{
				found = accumulator->add_live_template_counts();
				found->set_template_name(tmpl->getName().str());
				TemplateCountRef ref;
				ref.playerIndex = controller->getPlayerIndex();
				ref.tmpl = tmpl;
				ref.count = found;
				countRefs.push_back(ref);
			}
			found->set_count(found->count() + 1);
		}

		// Per-power readiness, in the same pass; not deduped by power name.
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			SpecialPowerModuleInterface* spmi = (*m)->getSpecialPower();
			if (spmi == NULL)
			{
				continue;
			}
			// The module exists whether or not the science is unlocked; skip powers the player cannot fire.
			if (!controller->hasScience(spmi->getRequiredScience()))
			{
				continue;
			}
			statsexporter::ReadinessSample* rs = accumulator->add_readiness();
			rs->set_power_name(spmi->getPowerName().str());
			rs->set_ready_frame(spmi->getReadyFrame());
			rs->set_percent_ready(spmi->getPercentReady());

			// One timer per superweapon structure, so two nukes are two entries.
			if (isSuperweaponStructureTemplate(tmpl))
			{
				statsexporter::SuperweaponTimer* timer = accumulator->add_superweapons();
				timer->set_object_id(obj->getID());
				timer->set_template_name(tmpl->getName().str());
				timer->set_power_name(spmi->getPowerName().str());
				timer->set_ready_frame(spmi->getReadyFrame());
				timer->set_percent_ready(spmi->getPercentReady());
				timer->set_state(spmi->getPercentReady() >= 1.0f ? statsexporter::SUPERWEAPON_READY : statsexporter::SUPERWEAPON_CHARGING);
			}
		}

		// Production queue of this structure, in the same pass.
		if (accumulator->production_size() < kMaxProductionStructures)
		{
			ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
			if (production != NULL && production->getProductionCount() > 0)
			{
				statsexporter::ProductionQueue* queue = accumulator->add_production();
				queue->set_object_id(obj->getID());
				queue->set_structure_template(tmpl->getName().str());

				int entryCount = 0;
				for (const ProductionEntry* entry = production->firstProduction(); entry != NULL && entryCount < kMaxProductionEntries; entry = production->nextProduction(entry))
				{
					statsexporter::ProductionItem* item = queue->add_entries();
					if (entry->getProductionType() == PRODUCTION_UPGRADE)
					{
						const UpgradeTemplate* upgrade = entry->getProductionUpgrade();
						item->set_kind(statsexporter::PRODUCTION_KIND_UPGRADE);
						item->set_name((upgrade != NULL) ? upgrade->getUpgradeName().str() : "");
					}
					else
					{
						const ThingTemplate* unit = entry->getProductionObject();
						item->set_kind(statsexporter::PRODUCTION_KIND_UNIT);
						item->set_name(templateName(unit));
					}
					item->set_percent_complete((entryCount == 0) ? entry->getPercentComplete() : 0.0f);
					item->set_quantity((uint32_t)entry->getProductionQuantityRemaining());
					++entryCount;
				}
			}
		}
	}

	// Heroes not seen alive this sample are listed dead; the record keeps them for the rest of the match.
	for (size_t h = 0; h < heroes.size(); ++h)
	{
		HeroRecord& record = heroes[h];
		if (!record.seen)
		{
			record.alive = FALSE;
			record.id = INVALID_ID;
		}
		for (int i = 0; i < snapshot.players_size(); ++i)
		{
			if (snapshot.players(i).player_index() == record.playerIndex)
			{
				statsexporter::HeroStatus* hero = snapshot.mutable_players(i)->add_heroes();
				hero->set_template_name(templateName(record.tmpl));
				hero->set_alive(record.alive);
				hero->set_object_id(record.alive ? record.id : 0);
				break;
			}
		}
	}

	if (haveGrid)
	{
		for (int i = 0; i < snapshot.players_size(); ++i)
		{
			statsexporter::PlayerGrid* grid = snapshot.add_grids();
			grid->set_player_index(snapshot.players(i).player_index());
			grid->set_width(kGridSize);
			grid->set_height(kGridSize);
			grid->set_cell_width(gridCellWidth);
			grid->set_cell_height(gridCellHeight);
			for (Int cell = 0; cell < kGridSize * kGridSize; ++cell)
			{
				grid->add_cells(s_gridCells[i][cell]);
			}
		}
	}

	for (size_t r = 0; r < tech.size() && (size_t)snapshot.tech_size() < kMaxTechStructures; ++r)
	{
		statsexporter::TechStructure* structure = snapshot.add_tech();
		structure->set_object_id(tech[r].id);
		structure->set_template_name(templateName(tech[r].tmpl));
		structure->set_x(tech[r].x);
		structure->set_y(tech[r].y);
		structure->set_owner_player_index(tech[r].owner);
	}

	// This client's own view and selection: client-side state, read only.
	if (TheTacticalView != NULL)
	{
		statsexporter::LocalCamera* camera = snapshot.mutable_camera();
		camera->set_x(TheTacticalView->getPosition().x);
		camera->set_y(TheTacticalView->getPosition().y);
		camera->set_zoom(TheTacticalView->getZoom());
		camera->set_angle(TheTacticalView->getAngle());
		camera->set_pitch(TheTacticalView->getPitch());
		camera->set_height_above_ground(TheTacticalView->getHeightAboveGround());
	}
	if (TheInGameUI != NULL)
	{
		const DrawableList* selected = TheInGameUI->getAllSelectedDrawables();
		if (selected != NULL)
		{
			for (DrawableList::const_iterator it = selected->begin(); it != selected->end() && (size_t)snapshot.selected_object_ids_size() < kMaxSelectedObjects; ++it)
			{
				const Object* selectedObject = (*it != NULL) ? (*it)->getObject() : NULL;
				if (selectedObject != NULL)
				{
					snapshot.add_selected_object_ids(selectedObject->getID());
				}
			}
		}
	}
	snapshot.set_network_stalled(s_stallStartMs != 0);
	snapshot.set_stalled_frames((s_stallStartMs != 0) ? (UnsignedInt)((GetTickCount64() - s_stallStartMs) * LOGICFRAMES_PER_SECOND / 1000) : 0);

	// Supply sources: the ones with something left first, up to the cap.
	for (int pass = 0; pass < 2; ++pass)
	{
		for (size_t r = 0; r < supply.size() && (size_t)snapshot.supply_size() < kMaxSupplySources; ++r)
		{
			if ((supply[r].boxes > 0) != (pass == 0))
			{
				continue;
			}
			statsexporter::SupplySource* source = snapshot.add_supply();
			source->set_object_id(supply[r].id);
			source->set_template_name(templateName(supply[r].tmpl));
			source->set_x(supply[r].x);
			source->set_y(supply[r].y);
			source->set_remaining_boxes((uint32_t)supply[r].boxes);
			source->set_remaining_value((uint32_t)supply[r].boxes * (uint32_t)TheGlobalData->m_baseValuePerSupplyBox);
		}
	}

	if (s_hasOnlineMatch)
	{
		fillConnections(&snapshot);
	}

	// -- Drain the discrete-event queue into this snapshot; the rest waits for the next one ---
	{
		std::vector<PendingEvent>& events = pendingEvents();
		const size_t drained = (events.size() < kMaxEventsPerSnapshot) ? events.size() : kMaxEventsPerSnapshot;
		for (size_t i = 0; i < drained; ++i)
		{
			const PendingEvent& src = events[i];
			statsexporter::StatEvent* dst = snapshot.add_events();
			dst->set_frame(src.frame);
			dst->set_player_index(src.playerIndex);
			dst->set_kind(src.kind);
			dst->set_category(src.category);
			dst->set_primary_name(src.primaryName);
			dst->set_secondary_name(src.secondaryName);
			dst->set_has_location(src.hasLocation);
			dst->set_x(src.x);
			dst->set_y(src.y);
			dst->set_variant(src.variant);
			dst->set_int_value1(src.intValue1);
			dst->set_int_value2(src.intValue2);
			dst->set_cost(src.cost);
		}
		events.erase(events.begin(), events.begin() + drained);
	}

	sendMessage(sink, STATS_EXPORTER_FRAME_STATS_SNAPSHOT, snapshot);
}

void StatsExporterLiveMatchSummary()
{
	if (TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}

	// Every mode except shell/none.
	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	// Don't build a summary nobody will read.
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || !sink->isConnected())
	{
		return;
	}

	statsexporter::MatchSummary summary;
	summary.set_frame(TheGameLogic->getFrame());
	if (TheWritableGlobalData != NULL)
	{
		summary.set_map_name(TheWritableGlobalData->m_mapName.str());
	}

	Player* localPlayer = ThePlayerList->getLocalPlayer();

	// Victory conditions are local-player-relative: allies of the local player share its result, enemies the opposite.
	Bool localVictory = (TheVictoryConditions != NULL) && TheVictoryConditions->isLocalAlliedVictory();
	Bool localDefeat = (TheVictoryConditions != NULL) && TheVictoryConditions->isLocalAlliedDefeat();

	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		Player* player = playerFromSlot(slot);
		if (player == NULL)
		{
			continue;
		}
		ScoreKeeper* scoreKeeper = player->getScoreKeeper();
		if (scoreKeeper == NULL)
		{
			continue;
		}

		Bool isWinner = FALSE;
		if ((localVictory || localDefeat) && localPlayer != NULL)
		{
			Relationship rel = (player == localPlayer) ? ALLIES : localPlayer->getRelationship(player->getDefaultTeam());
			if (localVictory)
			{
				isWinner = (rel == ALLIES);
			}
			else
			{
				isWinner = (rel == ENEMIES);
			}
		}

		statsexporter::MatchSummaryPlayer* p = summary.add_players();
		p->set_lobby_slot(slot);
		p->set_player_index(player->getPlayerIndex());
		p->set_player_name(narrow(player->getPlayerDisplayName()));
		p->set_side(player->getSide().str());
		p->set_score(scoreKeeper->calculateScore());
		p->set_is_winner(isWinner);
		p->set_is_observer(player->isPlayerObserver());
		p->set_units_built(scoreKeeper->getTotalUnitsBuilt());
		p->set_units_lost(scoreKeeper->getTotalUnitsLost());
		p->set_units_destroyed(scoreKeeper->getTotalUnitsDestroyed());
		p->set_buildings_built(scoreKeeper->getTotalBuildingsBuilt());
		p->set_buildings_lost(scoreKeeper->getTotalBuildingsLost());
		p->set_buildings_destroyed(scoreKeeper->getTotalBuildingsDestroyed());
		p->set_total_money_earned(scoreKeeper->getTotalMoneyEarned());
	}

	if (summary.players_size() == 0)
	{
		return;
	}

	sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_SUMMARY, summary);
}

void StatsExporterLiveMatchStarted()
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}
	if (TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}
	// Never for the shell map (or "no game at all").
	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	// LOADING already fixed the identity of this match; STARTED reuses it.
	if (!s_loadingPrepared)
	{
		beginMatchState(TheGameInfo);
	}
	s_loadingPrepared = FALSE;
	// 0 differs from any live connection generation, so the next tick sends STARTED through the resend path.
	s_matchInfoConnectionGen = 0;
}

void StatsExporterLiveMatchEnded(const AsciiString& replayPath, Bool replayIsPlayback)
{
	if (!s_matchActive || s_matchEndedSent)
	{
		return;	// no STARTED for the current match (e.g. shell), or ENDED already sent
	}
	if (TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}

	s_matchEndedSent = TRUE;

	// Decisive result at the moment the recording/playback closed.
	const Bool completed = (TheVictoryConditions != NULL)
		&& (TheVictoryConditions->isLocalAlliedVictory() || TheVictoryConditions->isLocalAlliedDefeat());

	// ENDED is a true one-shot: sent only if a reader is connected right now.
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink != NULL && sink->isConnected())
	{
		statsexporter::MatchInfo& info = matchInfoMessage();
		fillMatchInfo(info, statsexporter::MATCH_PHASE_ENDED, TheGameLogic->getFrame() - s_matchStartFrame, replayPath, replayIsPlayback, completed);
		sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_INFO, info);
	}

	s_matchActive = FALSE;
	s_hasLoadingBody = FALSE;
}

namespace
{
	// Largest ChatSend text taken from the pipe; the chat boxes' own limits apply on top.
	const size_t kMaxChatSendBytes = 1024;

	DWORD s_mainThreadId = 0;

	std::string toUtf8(const UnicodeString& text)
	{
		return to_utf8(std::wstring(text.str()));
	}

	// Who the line was addressed to, from its recipient mask.
	statsexporter::ChatAudience chatAudience(const Player* sender, Int senderSlot, Int playerMask)
	{
		if (sender == NULL)
		{
			return statsexporter::CHAT_AUDIENCE_OTHER;
		}
		if (!sender->isPlayerActive())
		{
			return statsexporter::CHAT_AUDIENCE_OBSERVERS;
		}

		Int allyMask = 0;
		Int allMask = 0;
		Int everyoneMask = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			AsciiString playerName;
			playerName.format("player%d", i);
			const Player* player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
			if (player == NULL)
			{
				continue;
			}
			allMask |= (1 << i);
			// Same recipients as an "everyone" line: every player whose slot is not muted.
			if (TheGameInfo == NULL || TheGameInfo->getConstSlot(i) == NULL || !TheGameInfo->getConstSlot(i)->isMuted())
			{
				everyoneMask |= (1 << i);
			}
			if (player == sender
				|| (player->getRelationship(sender->getDefaultTeam()) == ALLIES && sender->getRelationship(player->getDefaultTeam()) == ALLIES))
			{
				allyMask |= (1 << i);
			}
		}

		if (playerMask == everyoneMask)
		{
			return statsexporter::CHAT_AUDIENCE_EVERYONE;
		}
		if (playerMask == allyMask)
		{
			return statsexporter::CHAT_AUDIENCE_ALLIES;
		}
		if (playerMask == (1 << senderSlot))
		{
			return statsexporter::CHAT_AUDIENCE_OTHER;
		}
		if (allyMask != allMask && (playerMask & ~allyMask) == 0)
		{
			return statsexporter::CHAT_AUDIENCE_ALLIES;
		}
		return statsexporter::CHAT_AUDIENCE_EVERYONE;
	}

	void sendChatMessage(StatsExporterSink* sink, const statsexporter::ChatMessage& message)
	{
		// May run off the main thread, so it keeps its own buffer instead of the shared one.
		std::string body;
		if (message.SerializeToString(&body) && !body.empty() && body.size() <= STATS_EXPORTER_MAX_BODY_BYTES)
		{
			sink->enqueueFrame(STATS_EXPORTER_FRAME_CHAT_MESSAGE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size());
		}
	}

	struct LayoutScreen
	{
		const char* name;	// lowercase layout file name without directory
		statsexporter::UiScreen screen;
	};

	const LayoutScreen kLayoutScreens[] =
	{
		{ "mainmenu.wnd", statsexporter::UI_SCREEN_MAIN_MENU },
		{ "singleplayermenu.wnd", statsexporter::UI_SCREEN_SINGLE_PLAYER },
		{ "challengemenu.wnd", statsexporter::UI_SCREEN_SINGLE_PLAYER },
		{ "difficultyselect.wnd", statsexporter::UI_SCREEN_SINGLE_PLAYER },
		{ "mapselectmenu.wnd", statsexporter::UI_SCREEN_SINGLE_PLAYER },
		{ "skirmishgameoptionsmenu.wnd", statsexporter::UI_SCREEN_SKIRMISH_SETUP },
		{ "skirmishmapselectmenu.wnd", statsexporter::UI_SCREEN_SKIRMISH_SETUP },
		{ "lanlobbymenu.wnd", statsexporter::UI_SCREEN_LAN_LOBBY },
		{ "networkdirectconnect.wnd", statsexporter::UI_SCREEN_LAN_LOBBY },
		{ "langameoptionsmenu.wnd", statsexporter::UI_SCREEN_LAN_GAME_SETUP },
		{ "lanmapselectmenu.wnd", statsexporter::UI_SCREEN_LAN_GAME_SETUP },
		{ "wolwelcomemenu.wnd", statsexporter::UI_SCREEN_ONLINE_WELCOME },
		{ "wolcustomlobby.wnd", statsexporter::UI_SCREEN_ONLINE_LOBBY },
		{ "gamespygameoptionsmenu.wnd", statsexporter::UI_SCREEN_ONLINE_GAME_SETUP },
		{ "wolmapselectmenu.wnd", statsexporter::UI_SCREEN_ONLINE_GAME_SETUP },
		{ "wolquickmatchmenu.wnd", statsexporter::UI_SCREEN_ONLINE_QUICKMATCH },
		{ "replaymenu.wnd", statsexporter::UI_SCREEN_REPLAY_MENU },
		{ "optionsmenu.wnd", statsexporter::UI_SCREEN_OPTIONS },
		{ "keyboardoptionsmenu.wnd", statsexporter::UI_SCREEN_OPTIONS },
		{ "saveload.wnd", statsexporter::UI_SCREEN_SAVE_LOAD },
		{ "creditsmenu.wnd", statsexporter::UI_SCREEN_CREDITS },
		{ "scorescreen.wnd", statsexporter::UI_SCREEN_SCORE_SCREEN },
		{ "establishconnectionsscreen.wnd", statsexporter::UI_SCREEN_CONNECTING },
	};

	statsexporter::UiScreen screenForLayout(const std::string& layout)
	{
		std::string name = layout;
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos)
		{
			name = name.substr(slash + 1);
		}
		for (size_t i = 0; i < name.size(); ++i)
		{
			name[i] = (char)tolower((unsigned char)name[i]);
		}
		for (size_t i = 0; i < sizeof(kLayoutScreens) / sizeof(kLayoutScreens[0]); ++i)
		{
			if (name == kLayoutScreens[i].name)
			{
				return kLayoutScreens[i].screen;
			}
		}
		return statsexporter::UI_SCREEN_UNKNOWN;
	}

	struct UiSnapshot
	{
		statsexporter::UiScreen screen;
		std::string layout;
		statsexporter::MatchMode gameMode;
		bool paused;
	};

	UiSnapshot currentUiState()
	{
		UiSnapshot state;
		state.screen = statsexporter::UI_SCREEN_UNKNOWN;
		state.gameMode = statsexporter::MATCH_MODE_UNKNOWN;
		state.paused = false;

		if (TheShell != NULL && TheShell->top() != NULL)
		{
			state.layout = TheShell->top()->getFilename().str();
		}

		const Bool shellActive = (TheShell != NULL) && TheShell->isShellActive();
		if (TheGameLogic != NULL && (TheGameLogic->isLoadingMap() || TheGameLogic->isLoadingSave()))
		{
			state.screen = statsexporter::UI_SCREEN_LOADING;
		}
		else if (TheGameLogic != NULL && TheGameLogic->isInInteractiveGame() && !shellActive)
		{
			state.screen = (TheInGameUI != NULL && TheInGameUI->isQuitMenuVisible()) ? statsexporter::UI_SCREEN_IN_GAME_MENU : statsexporter::UI_SCREEN_IN_GAME;
			state.gameMode = mapMatchMode();
			state.paused = TheGameLogic->isGamePaused();
		}
		else
		{
			state.screen = screenForLayout(state.layout);
		}
		return state;
	}

	// Sends the current screen when it changed, and once to every new reader (by connection generation).
	void syncUiState(StatsExporterSink* sink)
	{
		static Bool s_hasSent = FALSE;
		static UnsignedInt s_sentGeneration = 0;
		static UiSnapshot s_sent;

		if (!sink->isConnected())
		{
			return;
		}

		const UnsignedInt generation = sink->getConnectionGeneration();
		const UiSnapshot state = currentUiState();
		if (s_hasSent && s_sentGeneration == generation && s_sent.screen == state.screen && s_sent.layout == state.layout
			&& s_sent.gameMode == state.gameMode && s_sent.paused == state.paused)
		{
			return;
		}

		statsexporter::UiState message;
		message.set_screen(state.screen);
		message.set_layout(state.layout);
		message.set_game_mode(state.gameMode);
		message.set_paused(state.paused);
		std::string body;
		if (message.SerializeToString(&body) && !body.empty())
		{
			sink->enqueueFrame(STATS_EXPORTER_FRAME_UI_STATE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size());
		}
		s_hasSent = TRUE;
		s_sentGeneration = generation;
		s_sent = state;
	}

	// -- Lobby prediction: a mirror of the engine's random resolution run on a private generator state. ------------------
	// It follows checkForDuplicateColors, populateRandomSideAndColor (MORE_RANDOM) and populateRandomStartPosition in
	// GameLogic.cpp line by line, on a plain copy of the slot fields, so it cannot touch the game's generators or slots.
	// The engine takes no logic draws between seeding with the lobby seed and these calls; the LOADING self-check below
	// confirms that and the result on every match.
#if !defined(GENONLINE_IBRA_STARTING_POS_LOGIC)
#error predictLobby mirrors the IBRA start position variant of populateRandomStartPosition; update it
#endif

	struct SimSlot
	{
		Bool occupied;
		Int tmpl;
		Int color;
		Int startPos;
		Int team;
	};

	struct LobbyPrediction
	{
		Bool valid;					// sides and colors
		Bool startPositionsValid;	// start positions and the observer camera
		SimSlot slots[MAX_SLOTS];
		Bool cameraSet;
		Int cameraStartPos;
	};

	Bool simColorTaken(const SimSlot* slots, Int colorIdx)
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (slots[i].color == colorIdx)
			{
				return TRUE;
			}
		}
		return FALSE;
	}

	Bool simStartPositionTaken(const SimSlot* slots, Int positionIdx)
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (slots[i].startPos == positionIdx)
			{
				return TRUE;
			}
		}
		return FALSE;
	}

	Bool predictLobby(GameInfo* game, LobbyPrediction& out)
	{
		out.valid = FALSE;
		out.startPositionsValid = FALSE;
		out.cameraSet = FALSE;
		out.cameraStartPos = -1;

		if (game == NULL || TheMultiplayerSettings == NULL || ThePlayerTemplateStore == NULL || TheChallengeGenerals == NULL || TheMapCache == NULL)
		{
			return FALSE;
		}

		SimSlot* slots = out.slots;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot* src = game->getConstSlot(i);
			if (src == NULL)
			{
				return FALSE;
			}
			slots[i].occupied = src->isOccupied();
			slots[i].tmpl = src->getPlayerTemplate();
			slots[i].color = src->getColor();
			slots[i].startPos = src->getStartPos();
			slots[i].team = src->getTeamNumber();
		}

		const Int numColors = TheMultiplayerSettings->getNumColors();
		if (numColors <= 0)
		{
			return FALSE;
		}

		// checkForDuplicateColors: the lowest slot keeps a color used twice, the others go back to random.
		for (Int i = MAX_SLOTS - 1; i >= 0; --i)
		{
			if (!slots[i].occupied)
			{
				continue;
			}

			Int colorIdx = slots[i].color;
			if (colorIdx < 0 || colorIdx >= numColors)
			{
				continue;
			}

			slots[i].color = -1;
			if (!simColorTaken(slots, colorIdx))
			{
				slots[i].color = colorIdx;
			}
		}

		// populateRandomSideAndColor, MORE_RANDOM path: the factions a random slot can draw.
		std::vector<Int> startSlots;
		for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
		{
			const PlayerTemplate* ptTest = ThePlayerTemplateStore->getNthPlayerTemplate(i);
			if (!ptTest || ptTest->getStartingBuilding().isEmpty())
			{
				continue;
			}
			if (game->oldFactionsOnly() && !ptTest->isOldFaction())
			{
				continue;
			}

			const GeneralPersona* general = TheChallengeGenerals->getGeneralByTemplateName(ptTest->getName());
			const Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
			if (startsLocked)
			{
				continue;
			}
			startSlots.push_back(i);
		}

		const UnsignedInt seed = (UnsignedInt)game->getSeed();
		RandomSeedState state;
		SeedRandomState(seed, state);

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!slots[i].occupied)
			{
				continue;
			}

			Int playerTemplateIdx = slots[i].tmpl;
			Int guard = 0;
			while (playerTemplateIdx != PLAYERTEMPLATE_OBSERVER && (playerTemplateIdx < 0 || playerTemplateIdx >= ThePlayerTemplateStore->getPlayerTemplateCount()))
			{
				if (startSlots.empty() || ++guard > 1000)
				{
					return FALSE;
				}

				// the engine skips seed % 7 draws first
				const UnsignedInt silly = seed % 7;
				for (UnsignedInt poo = 0; poo < silly; ++poo)
				{
					GetLogicRandomValueFromState(state, 0, 1);
				}
				const Int idxIdx = GetLogicRandomValueFromState(state, 0, 1000) % startSlots.size();
				playerTemplateIdx = startSlots[idxIdx];

				const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(playerTemplateIdx);
				if (!pt || pt->getStartingBuilding().isEmpty())
				{
					playerTemplateIdx = -1;
				}
				else
				{
					slots[i].tmpl = playerTemplateIdx;
				}
			}

			Int colorIdx = slots[i].color;
			if (colorIdx < 0 || colorIdx >= numColors)
			{
				guard = 0;
				while (colorIdx == -1)
				{
					if (++guard > 1000)
					{
						return FALSE;
					}
					colorIdx = GetLogicRandomValueFromState(state, 0, numColors - 1);
					if (simColorTaken(slots, colorIdx))
					{
						colorIdx = -1;
					}
				}
				slots[i].color = colorIdx;
			}
		}
		out.valid = TRUE;

		// populateRandomStartPosition needs the map's start waypoints from the map cache.
		const MapMetaData* md = TheMapCache->findMap(game->getMap());
		if (md == NULL)
		{
			return TRUE;
		}
		const Int numPlayers = md->m_numPlayers;
		const WaypointMap& waypoints = md->m_waypoints;

		Real startSpotDistance[MAX_SLOTS][MAX_SLOTS];
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			for (Int j = 0; j < MAX_SLOTS; ++j)
			{
				if (i != j && (i < numPlayers && j < numPlayers))
				{
					AsciiString w1, w2;
					w1.format("Player_%d_Start", i + 1);
					w2.format("Player_%d_Start", j + 1);
					WaypointMap::const_iterator c1 = waypoints.find(w1);
					WaypointMap::const_iterator c2 = waypoints.find(w2);
					if (c1 == waypoints.end() || c2 == waypoints.end())
					{
						startSpotDistance[i][j] = 1000000.0f;
					}
					else
					{
						Coord3D p1 = c1->second;
						Coord3D p2 = c2->second;
						startSpotDistance[i][j] = sqrt(sqr(p1.x - p2.x) + sqr(p1.y - p2.y));
					}
				}
				else
				{
					startSpotDistance[i][j] = 0.0f;
				}
			}
		}

		Bool hasStartSpotBeenPicked = FALSE;
		Bool taken[MAX_SLOTS];
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			taken[i] = (i < numPlayers) ? FALSE : TRUE;
		}
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!slots[i].occupied || slots[i].tmpl == PLAYERTEMPLATE_OBSERVER)
			{
				continue;
			}

			Int posIdx = slots[i].startPos;
			if (posIdx >= 0 && posIdx < numPlayers)
			{
				if (taken[posIdx])
				{
					slots[i].startPos = -1;
				}
				else
				{
					hasStartSpotBeenPicked = TRUE;
					taken[posIdx] = TRUE;
				}
			}
		}

		// teammates next to each other, teams far apart
		Int teamPosIdx[MAX_SLOTS];
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			teamPosIdx[i] = -1;
		}

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!slots[i].occupied || slots[i].tmpl == PLAYERTEMPLATE_OBSERVER)
			{
				continue;
			}

			Int posIdx = slots[i].startPos;
			if (posIdx >= 0 && posIdx < numPlayers)
			{
				continue;
			}

			const Int team = slots[i].team;
			if (team >= MAX_SLOTS)
			{
				return TRUE;	// outside what the engine's team table was written for
			}
			if (!hasStartSpotBeenPicked)
			{
				Int guard = 0;
				while (posIdx == -1)
				{
					if (++guard > 1000)
					{
						return TRUE;
					}
					posIdx = GetLogicRandomValueFromState(state, 0, numPlayers - 1);
					if (simStartPositionTaken(slots, posIdx))
					{
						posIdx = -1;
					}
				}
				hasStartSpotBeenPicked = TRUE;
				slots[i].startPos = posIdx;
				taken[posIdx] = TRUE;
				if (team > -1)
				{
					teamPosIdx[team] = posIdx;
				}
			}
			else if (team < 0 || teamPosIdx[team] == -1)
			{
				Real farthestDistance = 0.0f;
				Int farthestIndex = -1;
				for (posIdx = 0; posIdx < numPlayers; ++posIdx)
				{
					if (taken[posIdx])
					{
						continue;
					}

					if (farthestIndex < 0)
					{
						farthestIndex = posIdx;
						for (Int n = 0; n < numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
							{
								farthestDistance += startSpotDistance[posIdx][n];
							}
						}
					}
					else
					{
						Real dist = 0.0f;
						for (Int n = 0; n < numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
							{
								dist += startSpotDistance[posIdx][n];
							}
						}
						if (dist > farthestDistance)
						{
							farthestDistance = dist;
							farthestIndex = posIdx;
						}
					}
				}

				if (farthestIndex < 0)
				{
					return TRUE;
				}
				slots[i].startPos = farthestIndex;
				taken[farthestIndex] = TRUE;
				if (team > -1)
				{
					teamPosIdx[team] = farthestIndex;
				}
			}
			else
			{
				Real closestDist = FLT_MAX;
				Int closestIdx = 0;
				for (Int n = 0; n < numPlayers; ++n)
				{
					Real dist = startSpotDistance[teamPosIdx[team]][n];
					if (!taken[n] && dist < closestDist)
					{
						closestDist = dist;
						closestIdx = n;
					}
				}
				slots[i].startPos = closestIdx;
				taken[closestIdx] = TRUE;
			}
		}

		// observers take a spot a player holds
		Int numPlayersInGame = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (slots[i].occupied && slots[i].tmpl != PLAYERTEMPLATE_OBSERVER)
			{
				++numPlayersInGame;
			}
		}
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!slots[i].occupied || slots[i].tmpl != PLAYERTEMPLATE_OBSERVER)
			{
				continue;
			}

			Int posIdx = -1;
			if (numPlayersInGame == 0)
			{
				posIdx = 0;
			}
			Int guard = 0;
			while (posIdx == -1)
			{
				if (++guard > 1000)
				{
					return TRUE;
				}
				posIdx = GetLogicRandomValueFromState(state, 0, numPlayers - 1);
				if (!simStartPositionTaken(slots, posIdx))
				{
					posIdx = -1;
				}
			}
			slots[i].startPos = posIdx;
		}
		out.startPositionsValid = TRUE;

		// The observer camera starts on the local observer slot's start position, as at LOADING.
		if (game->isInGame())
		{
			Int localSlot = 0;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				GameSlot* slot = game->getSlot(i);
				if (slot != NULL && slot->isOccupied() && GameLogicIsLocalSlot(game, slot))
				{
					localSlot = i;
				}
			}
			const GameSlot* cameraSlot = game->getConstSlot(localSlot);
			if (cameraSlot != NULL && cameraSlot->isHuman() && slots[localSlot].tmpl == PLAYERTEMPLATE_OBSERVER)
			{
				out.cameraSet = TRUE;
				out.cameraStartPos = slots[localSlot].startPos;
			}
		}
		return TRUE;
	}

	statsexporter::LobbySlotState lobbySlotState(SlotState state)
	{
		switch (state)
		{
			case SLOT_CLOSED:    return statsexporter::LOBBY_SLOT_CLOSED;
			case SLOT_EASY_AI:   return statsexporter::LOBBY_SLOT_AI_EASY;
			case SLOT_MED_AI:    return statsexporter::LOBBY_SLOT_AI_MEDIUM;
			case SLOT_BRUTAL_AI: return statsexporter::LOBBY_SLOT_AI_BRUTAL;
			case SLOT_PLAYER:    return statsexporter::LOBBY_SLOT_HUMAN;
			default:             return statsexporter::LOBBY_SLOT_OPEN;
		}
	}

	// The game info behind the setup screen the user is on, or NULL when the screen is not a game setup lobby.
	GameInfo* lobbyGameForScreen(statsexporter::UiScreen screen, statsexporter::LobbyKind& kind)
	{
		kind = statsexporter::LOBBY_KIND_UNKNOWN;
		switch (screen)
		{
			case statsexporter::UI_SCREEN_ONLINE_GAME_SETUP:
				kind = statsexporter::LOBBY_KIND_ONLINE;
				return TheGenOnlineGame;
			case statsexporter::UI_SCREEN_LAN_GAME_SETUP:
				kind = statsexporter::LOBBY_KIND_LAN;
				return (TheLAN != NULL) ? TheLAN->GetMyGame() : NULL;
			case statsexporter::UI_SCREEN_SKIRMISH_SETUP:
				kind = statsexporter::LOBBY_KIND_SKIRMISH;
				return TheSkirmishGameInfo;
			default:
				return NULL;
		}
	}

	// Builds the game setup lobby from the GameInfo/GameSlot data the setup screen shows (plus the lobby interface
	// for online). Returns false when the user is not sitting in a game setup lobby.
	bool buildLobbyState(statsexporter::LobbyState& out)
	{
		out.Clear();

		statsexporter::LobbyKind kind;
		GameInfo* game = lobbyGameForScreen(currentUiState().screen, kind);
		if (game == NULL || game->isGameInProgress())
		{
			return false;
		}

		GenOnlineLobbyInterface* lobby = NULL;
		const LobbyEntry* entry = NULL;
		if (kind == statsexporter::LOBBY_KIND_ONLINE)
		{
			lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (lobby == NULL || !lobby->IsInLobby())
			{
				return false;
			}
			entry = &lobby->GetCurrentLobby();
		}

		out.set_in_lobby(true);
		out.set_kind(kind);
		if (entry != NULL)
		{
			out.set_map_name(entry->map_name);
			fillOnlineMatch(*out.mutable_online(), *entry);
			out.set_host_user_id(entry->owner);
		}
		else
		{
			out.set_map_name(game->getMap().str());
		}

		// Recomputed only when the seed, the map or a slot setting changes, not on every poll.
		static std::string s_predictionKey;
		static LobbyPrediction s_prediction;
		static Bool s_havePrediction = FALSE;
		{
			std::string key = game->getMap().str();
			char text[96];
			sprintf(text, "|%d|%d|%d", (int)kind, game->getSeed(), game->oldFactionsOnly() ? 1 : 0);
			key += text;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				const GameSlot* slot = game->getConstSlot(i);
				sprintf(text, "|%d,%d,%d,%d,%d", slot->getState(), slot->getPlayerTemplate(), slot->getColor(), slot->getStartPos(), slot->getTeamNumber());
				key += text;
			}
			if (!s_havePrediction || key != s_predictionKey)
			{
				if (!predictLobby(game, s_prediction))
				{
					s_prediction.valid = FALSE;
					s_prediction.startPositionsValid = FALSE;
					s_prediction.cameraSet = FALSE;
				}
				s_predictionKey = key;
				s_havePrediction = TRUE;
			}
		}
		const LobbyPrediction& prediction = s_prediction;
		out.set_prediction_valid(prediction.valid);
		out.set_prediction_start_positions_valid(prediction.valid && prediction.startPositionsValid);
		s_predictionValid = prediction.valid;
		s_predictionStartPositionsValid = prediction.valid && prediction.startPositionsValid;
		s_predictionSeed = game->getSeed();
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			s_predictedSlots[i].valid = prediction.valid;
			s_predictedSlots[i].templateIndex = prediction.valid ? prediction.slots[i].tmpl : -1;
			s_predictedSlots[i].color = prediction.valid ? prediction.slots[i].color : -1;
			s_predictedSlots[i].startPos = s_predictionStartPositionsValid ? prediction.slots[i].startPos : -2;
		}
		s_predictedCameraSet = s_predictionStartPositionsValid && prediction.cameraSet;
		s_predictedCameraStartPos = s_predictedCameraSet ? prediction.cameraStartPos : -1;
		if (s_predictedCameraSet)
		{
			statsexporter::ObserverCamera* camera = out.mutable_predicted_observer_camera();
			camera->set_start_position(prediction.cameraStartPos);
			camera->set_lobby_slot(-1);
			camera->set_player_index(-1);
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				if (prediction.slots[i].occupied && prediction.slots[i].tmpl != PLAYERTEMPLATE_OBSERVER && prediction.slots[i].startPos == prediction.cameraStartPos)
				{
					camera->set_lobby_slot(i);
					break;
				}
			}
		}

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot* slot = game->getConstSlot(i);
			if (slot == NULL)
			{
				continue;
			}

			statsexporter::LobbySlot* ls = out.add_slots();
			ls->set_state(lobbySlotState(slot->getState()));
			ls->set_has_map(slot->hasMap());
			ls->set_is_ready(slot->isAccepted());
			ls->set_start_position(slot->getStartPos());
			if (s_predictionValid && slot->isOccupied())
			{
				const Int templateIndex = s_predictedSlots[i].templateIndex;
				if (templateIndex == PLAYERTEMPLATE_OBSERVER)
				{
					ls->set_predicted_side("Observer");
				}
				else
				{
					const PlayerTemplate* predictedTemplate = (templateIndex >= 0) ? ThePlayerTemplateStore->getNthPlayerTemplate(templateIndex) : NULL;
					ls->set_predicted_side((predictedTemplate != NULL) ? predictedTemplate->getSide().str() : "Random");
				}
				const Int colorIndex = s_predictedSlots[i].color;
				if (colorIndex >= 0 && colorIndex < TheMultiplayerSettings->getNumColors())
				{
					ls->set_predicted_color((uint32_t)TheMultiplayerSettings->getColor(colorIndex)->getColor());
				}
				ls->set_predicted_start_position(s_predictionStartPositionsValid ? s_predictedSlots[i].startPos : -1);
			}

			if (slot->getState() == SLOT_OPEN || slot->getState() == SLOT_CLOSED)
			{
				continue;
			}

			statsexporter::MatchPlayer* p = ls->mutable_player();
			p->set_lobby_slot((uint32_t)i);
			p->set_player_index(-1);
			p->set_name(toUtf8(slot->getName()));
			p->set_color((uint32_t)slot->getColor());
			p->set_team(slot->getTeamNumber());
			p->set_is_human(slot->isHuman());
			p->set_is_color_random(slot->getOriginalColor() < 0);
			p->set_is_side_random(slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_RANDOM);
			p->set_start_position(slot->getStartPos());

			const Int templateIndex = slot->getPlayerTemplate();
			p->set_is_observer(templateIndex == PLAYERTEMPLATE_OBSERVER);
			if (templateIndex == PLAYERTEMPLATE_RANDOM)
			{
				p->set_side("Random");
			}
			else if (templateIndex == PLAYERTEMPLATE_OBSERVER)
			{
				p->set_side("Observer");
			}
			else
			{
				const PlayerTemplate* playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateIndex);
				p->set_side((playerTemplate != NULL) ? playerTemplate->getSide().str() : "");
			}

			if (kind == statsexporter::LOBBY_KIND_ONLINE)
			{
				GenOnlineGameSlot* onlineSlot = TheGenOnlineGame->getGameSpySlot(i);
				if (onlineSlot != NULL && onlineSlot->isHuman())
				{
					fillOnlinePlayer(*p->mutable_online(), lobby, onlineSlot->m_userID);
					ls->set_is_host(onlineSlot->m_userID == entry->owner);
				}
			}
			else
			{
				// LAN and skirmish: the host is slot 0, and a LAN slot carries its address.
				ls->set_is_host(i == 0);
				if (kind == statsexporter::LOBBY_KIND_LAN && slot->getIP() != 0)
				{
					char ip[32];
					sprintf(ip, "%u.%u.%u.%u", PRINTF_IP_AS_4_INTS(slot->getIP()));
					p->mutable_lan_address()->set_ip(ip);
				}
			}
		}

		if (kind == statsexporter::LOBBY_KIND_ONLINE)
		{
			fillConnections(&out);
		}
		return true;
	}

	// Sends the lobby when its serialized bytes changed (a few checks per second), and once to every new reader.
	void syncLobbyState(StatsExporterSink* sink)
	{
		static Bool s_wasInLobby = FALSE;
		static UnsignedInt s_sentGeneration = 0;
		static std::string s_lastBody;
		static ULONGLONG s_lastCheckMs = 0;

		if (!sink->isConnected())
		{
			return;
		}

		const UnsignedInt generation = sink->getConnectionGeneration();
		const ULONGLONG nowMs = GetTickCount64();
		const Bool newReader = (generation != s_sentGeneration);
		if (!newReader && nowMs - s_lastCheckMs < 250)
		{
			return;
		}
		s_lastCheckMs = nowMs;

		statsexporter::LobbyState state;
		const bool inLobby = buildLobbyState(state);
		if (!inLobby)
		{
			state.Clear();
			state.set_in_lobby(false);
		}

		std::string body;
		const bool serialized = state.SerializeToString(&body);
		s_sentGeneration = generation;
		if (!serialized)
		{
			return;
		}

		if (!inLobby)
		{
			// Left the lobby: one explicit marker, and nothing at all to a new reader that was never in one.
			if (!s_wasInLobby)
			{
				s_lastBody.clear();
				return;
			}
			s_wasInLobby = FALSE;
		}
		else
		{
			s_wasInLobby = TRUE;
			if (!newReader && body == s_lastBody)
			{
				return;
			}
		}

		s_lastBody = body;
		if (body.empty())
		{
			body.push_back(0x08);	// field 1 (in_lobby) = false written out so the frame is never empty
			body.push_back(0x00);
		}
		sink->enqueueFrame(STATS_EXPORTER_FRAME_LOBBY_STATE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size());
	}

	// Runs one ChatSend request through the same function the matching chat box uses when the player types a line.
	void dispatchChatSend(const StatsExporterChatSend& request)
	{
		if (request.text.empty() || request.text.size() > kMaxChatSendBytes)
		{
			return;
		}

		UnicodeString text(from_utf8(request.text).c_str());
		text.trim();
		// The line is handled as if typed into that channel's chat box, slash commands included.
		if (text.isEmpty())
		{
			return;
		}

		switch (request.channel)
		{
			case statsexporter::CHAT_CHANNEL_MATCH:
			{
				if (TheGameLogic == NULL || !TheGameLogic->isInMultiplayerGame() || TheNetwork == NULL
					|| ThePlayerList == NULL || ThePlayerList->getLocalPlayer() == NULL)
				{
					return;
				}

				const Bool localIsObserver = !ThePlayerList->getLocalPlayer()->isPlayerActive();
				InGameChatType type;
				switch (request.audience)
				{
					case statsexporter::CHAT_AUDIENCE_EVERYONE:
						type = INGAME_CHAT_EVERYONE;
						break;
					case statsexporter::CHAT_AUDIENCE_ALLIES:
						if (localIsObserver)
						{
							return;
						}
						type = INGAME_CHAT_ALLIES;
						break;
					case statsexporter::CHAT_AUDIENCE_OBSERVERS:
						if (!localIsObserver)
						{
							return;
						}
						type = INGAME_CHAT_EVERYONE;	// an observer's "everyone" is observers
						break;
					default:
						return;
				}
				SubmitInGameChatLine(text, type);
				break;
			}
			case statsexporter::CHAT_CHANNEL_GAME_LOBBY:
				WOLGameSetupMenuSubmitChatLine(text);
				break;
			case statsexporter::CHAT_CHANNEL_ROOM:
				WOLLobbyMenuSubmitChatLine(text);
				break;
			case statsexporter::CHAT_CHANNEL_LAN_LOBBY:
				LanLobbyMenuSubmitChatLine(text);
				break;
			case statsexporter::CHAT_CHANNEL_LAN_GAME:
				LanGameOptionsMenuSubmitChatLine(text);
				break;
			default:
				break;
		}
	}
}

void StatsExporterLiveTick()
{
	if (s_mainThreadId == 0)
	{
		s_mainThreadId = GetCurrentThreadId();
	}

	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL)
	{
		return;
	}

	syncUiState(sink);
	syncLobbyState(sink);

	// While the logic waits on a peer no snapshot is built, so the stall is reported from here.
	const ULONGLONG nowMs = GetTickCount64();
	if (TheNetwork != NULL && TheGameLogic != NULL && TheGameLogic->isInInteractiveGame() && TheNetwork->isStalling())
	{
		if (s_stallStartMs == 0)
		{
			s_stallStartMs = nowMs;
		}
		if (sink->isConnected() && s_matchActive && s_matchInfoConnectionGen == sink->getConnectionGeneration() && nowMs - s_lastStallSendMs >= 250)
		{
			s_lastStallSendMs = nowMs;
			statsexporter::StatsSnapshot& stalled = stallMessage();
			stalled.Clear();
			stalled.set_frame(TheGameLogic->getFrame());
			stalled.set_network_stalled(true);
			stalled.set_stalled_frames((UnsignedInt)((nowMs - s_stallStartMs) * LOGICFRAMES_PER_SECOND / 1000));
			sendMessage(sink, STATS_EXPORTER_FRAME_STATS_SNAPSHOT, stalled);
		}
	}
	else
	{
		s_stallStartMs = 0;
	}

	StatsExporterChatSend request;
	while (sink->takeChatSend(request))
	{
		dispatchChatSend(request);
	}
}

void StatsExporterLiveChatInGame(Int senderSlot, const UnicodeString& senderName, const UnicodeString& text, Int playerMask, Bool isLocal)
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || !sink->isConnected())
	{
		return;
	}
	if (GetCurrentThreadId() == s_mainThreadId)
	{
		syncUiState(sink);
	}

	AsciiString playerName;
	playerName.format("player%d", senderSlot);
	const Player* sender = (ThePlayerList != NULL) ? ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName)) : NULL;

	statsexporter::ChatMessage message;
	message.set_channel(statsexporter::CHAT_CHANNEL_MATCH);
	message.set_logic_frame((TheGameLogic != NULL) ? TheGameLogic->getFrame() : 0);
	message.set_player_index((sender != NULL) ? sender->getPlayerIndex() : -1);
	if (TheGenOnlineGame != nullptr)
	{
		GenOnlineGameSlot* slot = TheGenOnlineGame->getGameSpySlot(senderSlot);
		message.set_user_id((slot != nullptr) ? slot->m_userID : 0);
	}
	message.set_sender_name(toUtf8(senderName));
	message.set_text(toUtf8(text));
	message.set_audience(chatAudience(sender, senderSlot, playerMask));
	message.set_is_local(isLocal);
	sendChatMessage(sink, message);
}

void StatsExporterLiveChatOnline(Int channel, Int64 userId, const char* senderName, const char* utf8Text, Bool isAction, Bool isSystem, Bool isLocal)
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || !sink->isConnected() || utf8Text == NULL)
	{
		return;
	}
	if (GetCurrentThreadId() == s_mainThreadId)
	{
		syncUiState(sink);
	}

	statsexporter::ChatMessage message;
	message.set_channel((statsexporter::ChatChannel)channel);
	message.set_player_index(-1);
	message.set_user_id(userId);
	message.set_sender_name((senderName != NULL) ? senderName : "");
	message.set_text(utf8Text);
	message.set_is_local(isLocal);
	message.set_is_system(isSystem);
	message.set_is_action(isAction);
	if (channel == STATS_EXPORTER_CHAT_GAME_LOBBY)
	{
		GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		message.set_lobby_id((lobby != nullptr) ? lobby->GetCurrentLobby().lobbyID : 0);
	}
	sendChatMessage(sink, message);
}

namespace
{
	// MatchInfo LOADING from the slots the engine just resolved; nothing here draws random numbers or changes the slots.
	void fillLoadingInfo(statsexporter::MatchInfo& info, GameInfo* game)
	{
		info.Clear();
		info.set_phase(statsexporter::MATCH_PHASE_LOADING);
		info.set_match_id(s_matchId);
		info.set_mode(s_matchMode);
		info.set_map_name(s_matchMapName);
		info.set_start_unix_time(s_matchStartUnixTime);
		info.set_replay_path("");
		info.set_local_player_index(-1);

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot* slot = game->getConstSlot(i);
			if (slot == NULL || !slot->isOccupied())
			{
				continue;
			}

			statsexporter::MatchPlayer* p = info.add_players();
			p->set_lobby_slot((uint32_t)i);
			p->set_player_index(-1);	// players do not exist yet
			p->set_name(toUtf8(slot->getName()));
			p->set_is_human(slot->isHuman());
			p->set_team(slot->getTeamNumber());
			p->set_start_position(slot->getStartPos());
			p->set_is_color_random(slot->getOriginalColor() < 0);
			p->set_is_side_random(slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_RANDOM);
			p->set_result(statsexporter::PLAYER_RESULT_UNKNOWN);

			const Int templateIndex = slot->getPlayerTemplate();
			p->set_is_observer(templateIndex == PLAYERTEMPLATE_OBSERVER);
			if (templateIndex == PLAYERTEMPLATE_OBSERVER)
			{
				p->set_side("Observer");
			}
			else
			{
				const PlayerTemplate* playerTemplate = (templateIndex >= 0) ? ThePlayerTemplateStore->getNthPlayerTemplate(templateIndex) : NULL;
				p->set_side((playerTemplate != NULL) ? playerTemplate->getSide().str() : "Random");
			}

			const Int colorIndex = slot->getColor();
			if (TheMultiplayerSettings != NULL && colorIndex >= 0 && colorIndex < TheMultiplayerSettings->getNumColors())
			{
				p->set_color((uint32_t)TheMultiplayerSettings->getColor(colorIndex)->getColor());
			}

			if (s_matchMode == statsexporter::MATCH_MODE_LAN && slot->getIP() != 0)
			{
				char ip[32];
				sprintf(ip, "%u.%u.%u.%u", PRINTF_IP_AS_4_INTS(slot->getIP()));
				p->mutable_lan_address()->set_ip(ip);
			}

			const std::vector<OnlinePlayerEntry>& entries = onlinePlayers();
			for (size_t j = 0; j < entries.size(); ++j)
			{
				if (entries[j].lobbySlot == i)
				{
					p->mutable_online()->CopyFrom(entries[j].data);
					break;
				}
			}
		}

		if (s_hasObserverCamera)
		{
			statsexporter::ObserverCamera* camera = info.mutable_observer_camera();
			camera->set_start_position(s_observerCameraStartPosition);
			camera->set_lobby_slot(s_observerCameraLobbySlot);
			camera->set_player_index(-1);
		}
		if (s_hasOnlineMatch)
		{
			info.mutable_online()->CopyFrom(onlineMatchMessage());
		}
	}
}

void StatsExporterLiveMatchLoading(GameInfo* game, UnsignedInt randomCrcBeforeResolve)
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || game == NULL || TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}
	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	beginMatchState(game);
	s_loadingPrepared = TRUE;

	statsexporter::MatchInfo& info = matchInfoMessage();
	fillLoadingInfo(info, game);

	// Whether the logic generator was untouched between the seed being applied and the resolution. Read-only.
	const Bool randomClean = (randomCrcBeforeResolve == ComputeFreshGameLogicRandomSeedCRC((UnsignedInt)game->getSeed()));
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsExporter] Loading: logic random state before resolution %s the fresh seed state", randomClean ? "equals" : "DIFFERS from");
	if (TheGlobalData != NULL && TheGlobalData->m_headless)
	{
		printf("StatsExporter loading: logic random state before resolution %s the fresh seed state\n", randomClean ? "equals" : "DIFFERS from");
		fflush(stdout);
	}

	// Compare the lobby prediction with what the engine resolved. Read-only.
	if (s_predictionValid && s_predictionSeed == game->getSeed())
	{
		Bool matched = TRUE;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot* slot = game->getConstSlot(i);
			if (slot == NULL || !slot->isOccupied())
			{
				continue;
			}
			if (!s_predictedSlots[i].valid || slot->getPlayerTemplate() != s_predictedSlots[i].templateIndex
				|| slot->getColor() != s_predictedSlots[i].color
				|| (s_predictionStartPositionsValid && slot->getStartPos() != s_predictedSlots[i].startPos))
			{
				matched = FALSE;
			}
		}
		if (s_predictionStartPositionsValid && (s_hasObserverCamera != s_predictedCameraSet || (s_hasObserverCamera && s_observerCameraStartPosition != s_predictedCameraStartPos)))
		{
			matched = FALSE;
		}

		info.set_has_prediction_check(true);
		info.set_prediction_matched(matched);
		info.set_random_state_clean(randomClean);
		if (!matched || !randomClean)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsExporter] Lobby prediction check: matched=%d, random state clean=%d", matched ? 1 : 0, randomClean ? 1 : 0);
		}
	}

	s_hasLoadingBody = info.SerializeToString(&s_loadingBody) && !s_loadingBody.empty();
	s_loadingSentGeneration = 0;

	// The load blocks the main thread and no tick runs during it, so a connected reader gets LOADING right here.
	if (s_hasLoadingBody && sink->isConnected())
	{
		syncUiState(sink);
		sink->enqueueFrame(STATS_EXPORTER_FRAME_MATCH_INFO, (const UnsignedByte*)s_loadingBody.data(), (UnsignedInt)s_loadingBody.size());
		s_loadingSentGeneration = sink->getConnectionGeneration();
	}
}

void StatsExporterLiveChatLan(Int channel, const UnicodeString& senderName, const UnicodeString& text, Bool isAction, Bool isSystem, Bool isLocal)
{
	StatsExporterLiveChatOnline(channel, 0, toUtf8(senderName).c_str(), toUtf8(text).c_str(), isAction, isSystem, isLocal);
}

void StatsExporterLiveSpend(Int playerIndex, Int category, Int amount)
{
	if (playerIndex < 0 || playerIndex >= kMaxCounterPlayers || category < 0 || category > 3 || amount <= 0)
	{
		return;
	}
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}
	s_spent[playerIndex][category] += (UnsignedInt)amount;
}

void StatsExporterLiveSell(Int playerIndex, const ThingTemplate* tmpl, UnsignedInt refund)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_SELL;
	event.category = classifyThingTemplateCategory(tmpl);
	event.primaryName = templateName(tmpl);
	event.cost = refund;
	pushPendingEvent(event);
}

void StatsExporterLiveCommand(Int playerIndex, Int messageType)
{
	if (playerIndex < 0 || playerIndex >= kMaxCounterPlayers || !isActionMessage(messageType))
	{
		return;
	}
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	const UnsignedInt second = TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND;
	const Int bucket = (Int)(second % 60);
	if (s_actionBucketSecond[playerIndex][bucket] != second)
	{
		s_actionBucketSecond[playerIndex][bucket] = second;
		s_actionBucket[playerIndex][bucket] = 0;
	}
	++s_actionBucket[playerIndex][bucket];
	++s_actionsTotal[playerIndex];
}

void StatsExporterLiveBeacon(Int playerIndex, const Coord3D* location)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_BEACON;
	event.category = "BEACON";
	if (location != NULL)
	{
		event.hasLocation = TRUE;
		event.x = location->x;
		event.y = location->y;
	}
	pushPendingEvent(event);
}

void StatsExporterLivePlayerLeft(Int playerIndex, const char* variant)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_PLAYER_LEFT;
	event.category = "PLAYER";
	event.variant = (variant != NULL) ? variant : "";
	pushPendingEvent(event);
}

void StatsExporterLivePlayerLeftSlot(Int slot, const char* variant)
{
	if (ThePlayerList == NULL)
	{
		return;
	}

	AsciiString playerName;
	playerName.format("player%d", slot);
	const Player* player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
	StatsExporterLivePlayerLeft((player != NULL) ? player->getPlayerIndex() : -1, variant);
}

void StatsExporterLivePlayerDefeated(Int playerIndex)
{
	if (StatsExporterPipeGetSink() == NULL)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_PLAYER_DEFEATED;
	event.category = "PLAYER";
	pushPendingEvent(event);
}
