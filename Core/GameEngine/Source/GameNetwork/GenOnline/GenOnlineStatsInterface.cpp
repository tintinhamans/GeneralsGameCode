#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/RankPointValue.h"
#include "../GenOnlineInit.h"
#include "../HTTP/HTTPManager.h"

#include "Common/PlayerTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/Online/GameSpy/GameSpyLadder.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

GenOnlineStatsInterface::GenOnlineStatsInterface()
{
	TheRankPointValues = NEW RankPoints;

	// populate ranks
	// TODO_GENONLINE: Perhaps get this from the service?
	TheRankPointValues->m_ranks[RANK_PRIVATE] = 0;
	TheRankPointValues->m_ranks[RANK_CORPORAL] = getPointsForRank(RANK_CORPORAL); // 5
	TheRankPointValues->m_ranks[RANK_SERGEANT] = getPointsForRank(RANK_SERGEANT); // 10
	TheRankPointValues->m_ranks[RANK_LIEUTENANT] = getPointsForRank(RANK_LIEUTENANT); // 20
	TheRankPointValues->m_ranks[RANK_CAPTAIN] = getPointsForRank(RANK_CAPTAIN); // 50
	TheRankPointValues->m_ranks[RANK_MAJOR] = getPointsForRank(RANK_MAJOR); // 100
	TheRankPointValues->m_ranks[RANK_COLONEL] = getPointsForRank(RANK_COLONEL); // 200
	TheRankPointValues->m_ranks[RANK_BRIGADIER_GENERAL] = getPointsForRank(RANK_BRIGADIER_GENERAL); // 500
	TheRankPointValues->m_ranks[RANK_GENERAL] = getPointsForRank(RANK_GENERAL); // 1000
	TheRankPointValues->m_ranks[RANK_COMMANDER_IN_CHIEF] = getPointsForRank(RANK_COMMANDER_IN_CHIEF); // 2000

	// TODO_GENONLINE: Better location
	TheLadderList = NEW GameSpyLadder;
}

GenOnlineStatsInterface::~GenOnlineStatsInterface()
{
	if (TheRankPointValues != nullptr)
	{
		delete TheRankPointValues;
		TheRankPointValues = nullptr;
	}

	if (TheLadderList != nullptr)
	{
		delete TheLadderList;
		TheLadderList = nullptr;
	}
}

// Fills the stats of a player from the service body. The service sends no locale, options, system spec, FPS or ladder.
static PSPlayerStats PlayerStatsFromBody(const GenOnlineRealtime::PlayerStatsBody& body)
{
	PSPlayerStats stats;
	stats.id = (Int)body.user_id;
	stats.elo_rating = (int)body.elo_rating;
	stats.elo_num_matches = (int)body.elo_matches;
	stats.monthly_elo_rating = (int)body.monthly_elo_rating;

	auto copyPerGeneral = [](PerGeneralMap& to, const std::vector<int64_t>& from)
		{
			for (size_t i = 0; i < from.size(); ++i)
			{
				to[(Int)i] = (UnsignedInt)from[i];
			}
		};
	const GenOnlineRealtime::PlayerGeneralStats& generals = body.generals;
	copyPerGeneral(stats.wins, generals.wins);
	copyPerGeneral(stats.losses, generals.losses);
	copyPerGeneral(stats.games, generals.games);
	copyPerGeneral(stats.duration, generals.duration);
	copyPerGeneral(stats.unitsKilled, generals.units_killed);
	copyPerGeneral(stats.unitsLost, generals.units_lost);
	copyPerGeneral(stats.unitsBuilt, generals.units_built);
	copyPerGeneral(stats.buildingsKilled, generals.buildings_killed);
	copyPerGeneral(stats.buildingsLost, generals.buildings_lost);
	copyPerGeneral(stats.buildingsBuilt, generals.buildings_built);
	copyPerGeneral(stats.earnings, generals.earnings);
	copyPerGeneral(stats.techCaptured, generals.tech_captured);
	copyPerGeneral(stats.discons, generals.discons);
	copyPerGeneral(stats.desyncs, generals.desyncs);
	copyPerGeneral(stats.surrenders, generals.surrenders);
	copyPerGeneral(stats.gamesOf2p, generals.games_of_2p);
	copyPerGeneral(stats.gamesOf3p, generals.games_of_3p);
	copyPerGeneral(stats.gamesOf4p, generals.games_of_4p);
	copyPerGeneral(stats.gamesOf5p, generals.games_of_5p);
	copyPerGeneral(stats.gamesOf6p, generals.games_of_6p);
	copyPerGeneral(stats.gamesOf7p, generals.games_of_7p);
	copyPerGeneral(stats.gamesOf8p, generals.games_of_8p);
	copyPerGeneral(stats.customGames, generals.custom_games);
	copyPerGeneral(stats.QMGames, generals.quick_matches);

	stats.gamesAsRandom = (Int)body.games_as_random;
	stats.lastGeneral = (Int)body.last_general;
	stats.gamesInRowWithLastGeneral = (Int)body.games_in_row_with_last_general;
	stats.challengeMedals = (Int)body.challenge_medals;
	stats.battleHonors = (Int)body.battle_honors;
	stats.QMwinsInARow = (Int)body.qm_wins_in_a_row;
	stats.maxQMwinsInARow = (Int)body.max_qm_wins_in_a_row;
	stats.winsInARow = (Int)body.wins_in_a_row;
	stats.maxWinsInARow = (Int)body.max_wins_in_a_row;
	stats.lossesInARow = (Int)body.losses_in_a_row;
	stats.maxLossesInARow = (Int)body.max_losses_in_a_row;
	stats.disconsInARow = (Int)body.discons_in_a_row;
	stats.maxDisconsInARow = (Int)body.max_discons_in_a_row;
	stats.desyncsInARow = (Int)body.desyncs_in_a_row;
	stats.maxDesyncsInARow = (Int)body.max_desyncs_in_a_row;
	stats.builtParticleCannon = (Int)body.built_particle_cannon;
	stats.builtNuke = (Int)body.built_nuke;
	stats.builtSCUD = (Int)body.built_scud;
	return stats;
}

void GenOnlineStatsInterface::GetGlobalStats(std::function<void(GlobalStats)> cb)
{
	// The global stats are anonymous, so they need no session.
	const std::string strOrigin = GenOnlineAuthInterface::ServiceOriginV1();
	GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
	if (strOrigin.empty() || pManager == nullptr || pManager->GetHTTPManager() == nullptr)
	{
		cb(GlobalStats());
		return;
	}
	const std::string strURI = strOrigin + GenOnlineRealtime::RestGlobalStatsPath();

	GenOnlineRealtimeBridge::SendWithStandbyRetry("GET /api/v1/stats/global",
		[strURI](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			GenOnlineServicesManager* pServices = GenOnlineServicesManager::GetInstance();
			if (pServices == nullptr || pServices->GetHTTPManager() == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			std::map<std::string, std::string> mapHeaders;
			pServices->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, fnAnswer);
		},
		[=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			GlobalStats stats;
			GenOnlineRealtime::GlobalStatsResponse global;
			if (GenOnlineRealtime::MakeRestOutcome(bSuccess, statusCode, strBody).Data(global))
			{
				// Every playable side gets an entry, also one the service leaves out.
				for (int side = 2; side <= 13; ++side)
				{
					const int index = GenOnlineRealtime::GlobalStatsFactionIndex(side);
					stats.wins[index] = 0;
					stats.matches[index] = 0;
				}
				for (const GenOnlineRealtime::FactionStats& faction : global.factions)
				{
					const int index = GenOnlineRealtime::GlobalStatsFactionIndex(faction.side);
					if (index >= 0)
					{
						stats.wins[index] = (UnsignedInt)faction.wins;
						stats.matches[index] = (UnsignedInt)faction.matches;
					}
				}
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Stats] Global stats failed: status %d", statusCode);
			}

			cb(stats);
		});
}

void GenOnlineStatsInterface::findPlayerStatsByID(int64_t userID, std::function<void(bool, PSPlayerStats)> cb, EStatsRequestPolicy requestPolicy)
{
	// TODO_GENONLINE: this could take a while...
	if (requestPolicy == EStatsRequestPolicy::CACHED_ONLY)
	{
		NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsRequest] Getting stats for user %lld (cache only, not making request due to policy)", userID);
		PSPlayerStats stats;
		const bool found = getPlayerStatsFromCache(userID, &stats);
		cb(found, stats);
	}
	else
	{
		bool bDoRequest = false;

		if (requestPolicy == EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST)
		{
			bDoRequest = true;
			NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsRequest] Getting stats for user %lld (bypassing cache, making request due to policy)", userID);
		}
		else if (requestPolicy == EStatsRequestPolicy::RESPECT_CACHE_ALLOW_REQUEST)
		{
			if (!HasFreshPlayerStats(userID))
			{
				NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsRequest] Getting stats for user %lld (cache is missing or stale)", userID);
				bDoRequest = true;
			}
		}

		if (bDoRequest)
		{
			const uint64_t cacheRevision = AdvancePlayerStatsCacheRevision(userID);

			GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Get, GenOnlineRealtime::RestPlayerStatsPath(userID), std::string(), [this, userID, cb, cacheRevision](const GenOnlineRealtime::RestOutcome& outcome)
				{
					GenOnlineRealtime::PlayerStatsBody body;
					if (!outcome.Data(body))
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsRequest] Stats for user %lld failed: status %d code %s", userID, outcome.status, outcome.error.code.c_str());
						PSPlayerStats stats;
						stats.id = userID;
						cb(false, stats);
						return;
					}

					const PSPlayerStats stats = PlayerStatsFromBody(body);
					if (stats.id != userID)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsRequest] Ignored mismatched stats for user %lld (received %d)", userID, stats.id);
						cb(false, PSPlayerStats());
						return;
					}

					if (!TryCachePlayerStats(stats, cacheRevision))
					{
						PSPlayerStats cachedStats;
						const bool found = getPlayerStatsFromCache(userID, &cachedStats);
						cb(found, cachedStats);
						return;
					}

					cb(true, stats);
				});
		}
		else // cached data instead
		{
			PSPlayerStats stats;
			const bool found = getPlayerStatsFromCache(userID, &stats);
			cb(found, stats);
		}
		
	}
}

void GenOnlineStatsInterface::findPlayerStatsByBatch(std::vector<int64_t> vecUserIDs, std::function<void(bool)> cb)
{
	// If they asked for nothing, just invoke the callback
	if (vecUserIDs.empty())
	{
		cb(true);
		return;
	}

	struct BatchState
	{
		std::unordered_map<int64_t, uint64_t> cacheRevisions;
		std::unordered_set<int64_t> receivedUserIDs;
		size_t pendingRequests = 0;
		bool parsedAllResults = true;
	};
	auto state = std::make_shared<BatchState>();
	std::vector<int64_t> uniqueUserIDs;
	uniqueUserIDs.reserve(vecUserIDs.size());
	for (int64_t userID : vecUserIDs)
	{
		if (!state->cacheRevisions.contains(userID))
		{
			state->cacheRevisions.emplace(userID, AdvancePlayerStatsCacheRevision(userID));
			uniqueUserIDs.push_back(userID);
		}
	}

	// The service takes at most 100 ids per request, the callback runs once every request answered.
	const std::vector<std::string> paths = GenOnlineRealtime::RestPlayerStatsBatchPaths(uniqueUserIDs);
	state->pendingRequests = paths.size();
	for (const std::string& path : paths)
	{
		GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Get, path, std::string(), [this, cb, state](const GenOnlineRealtime::RestOutcome& outcome)
			{
				GenOnlineRealtime::PlayerStatsBatchResponse batch;
				if (!outcome.Data(batch))
				{
					state->parsedAllResults = false;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsBatch] Request failed: status %d code %s", outcome.status, outcome.error.code.c_str());
				}
				else
				{
					for (const GenOnlineRealtime::PlayerStatsBody& body : batch.stats)
					{
						const PSPlayerStats stats = PlayerStatsFromBody(body);
						auto revisionIt = state->cacheRevisions.find(stats.id);
						if (revisionIt == state->cacheRevisions.end())
						{
							state->parsedAllResults = false;
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsBatch] Ignored unexpected stats for user %d", stats.id);
						}
						else if (!state->receivedUserIDs.insert(stats.id).second)
						{
							state->parsedAllResults = false;
							NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsBatch] Ignored duplicate stats for user %d", stats.id);
						}
						else if (!TryCachePlayerStats(stats, revisionIt->second))
						{
							state->parsedAllResults = false;
						}
					}
				}

				if (--state->pendingRequests > 0)
				{
					return;
				}

				if (state->receivedUserIDs.size() != state->cacheRevisions.size())
				{
					state->parsedAllResults = false;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsBatch] Response omitted %zu requested user(s)", state->cacheRevisions.size() - state->receivedUserIDs.size());
				}

				cb(state->parsedAllResults);
			});
	}
}

bool GenOnlineStatsInterface::getPlayerStatsFromCache(int64_t userID, PSPlayerStats* outStats)
{
	NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsRequest] Getting stats for user %lld (cache only, not making request due to policy)", userID);
	if (outStats == nullptr)
	{
		return false;
	}

	auto cacheIt = m_playerStatsCache.find(userID);
	if (cacheIt != m_playerStatsCache.end() && cacheIt->second.hasStats)
	{
		cacheIt->second.lastAccessAt = std::chrono::steady_clock::now();
		*outStats = cacheIt->second.stats;
		return true;
	}

	*outStats = PSPlayerStats();
	return false;
}

bool GenOnlineStatsInterface::HasFreshPlayerStats(int64_t userID)
{
	auto cacheIt = m_playerStatsCache.find(userID);
	if (cacheIt == m_playerStatsCache.end() || !cacheIt->second.hasStats
		|| cacheIt->second.lastRefreshAt == std::chrono::steady_clock::time_point{})
	{
		return false;
	}

	const auto currentTime = std::chrono::steady_clock::now();
	cacheIt->second.lastAccessAt = currentTime;
	return currentTime >= cacheIt->second.lastRefreshAt
		&& currentTime - cacheIt->second.lastRefreshAt < STATS_CACHE_TTL;
}

void GenOnlineStatsInterface::MarkPlayerStatsCacheStale(int64_t userID)
{
	PlayerStatsCacheEntry& entry = GetOrCreatePlayerStatsCacheEntry(userID);
	entry.lastRefreshAt = {};
	entry.lastAccessAt = std::chrono::steady_clock::now();
	entry.cacheRevision = ++m_nextStatsCacheRevision;
	NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsCache] Marked stats stale for user %lld", userID);
}

GenOnlineStatsInterface::PlayerStatsCacheEntry& GenOnlineStatsInterface::GetOrCreatePlayerStatsCacheEntry(int64_t userID)
{
	auto cacheIt = m_playerStatsCache.find(userID);
	if (cacheIt != m_playerStatsCache.end())
	{
		return cacheIt->second;
	}

	if (m_playerStatsCache.size() >= MAX_PLAYER_STATS_CACHE_ENTRIES)
	{
		auto oldestIt = std::min_element(m_playerStatsCache.begin(), m_playerStatsCache.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.second.lastAccessAt < rhs.second.lastAccessAt;
		});
		if (oldestIt != m_playerStatsCache.end())
		{
			m_playerStatsCache.erase(oldestIt);
		}
	}

	return m_playerStatsCache.try_emplace(userID).first->second;
}

uint64_t GenOnlineStatsInterface::AdvancePlayerStatsCacheRevision(int64_t userID)
{
	PlayerStatsCacheEntry& entry = GetOrCreatePlayerStatsCacheEntry(userID);
	entry.lastAccessAt = std::chrono::steady_clock::now();
	entry.cacheRevision = ++m_nextStatsCacheRevision;
	return entry.cacheRevision;
}

bool GenOnlineStatsInterface::TryCachePlayerStats(const PSPlayerStats& stats, uint64_t expectedRevision)
{
	auto cacheIt = m_playerStatsCache.find(stats.id);
	if (cacheIt == m_playerStatsCache.end() || cacheIt->second.cacheRevision != expectedRevision)
	{
		NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsCache] Ignored superseded stats for user %d", stats.id);
		return false;
	}

	PlayerStatsCacheEntry& entry = cacheIt->second;
	entry.stats = stats;
	entry.lastRefreshAt = std::chrono::steady_clock::now();
	entry.lastAccessAt = entry.lastRefreshAt;
	entry.hasStats = true;
	NetworkLog(ELogVerbosity::LOG_DEBUG, "[StatsCache] Cached stats for user %d", stats.id);
	return true;
}

void GenOnlineStatsInterface::CommitMyOutcome(ScoreKeeper* pScoreKeeper, bool bWon)
{
	int buildingsBuilt = 0;
	int buildingsDestroyed = 0;
	int buildingsLost = 0;
	int unitsBuilt = 0;
	int unitsDestroyed = 0;
	int unitsLost = 0;
	int totalMoney = 0;
	if (pScoreKeeper != nullptr)
	{
        buildingsBuilt = pScoreKeeper->getTotalBuildingsBuilt();
        buildingsDestroyed = pScoreKeeper->getTotalBuildingsDestroyed();
        buildingsLost = pScoreKeeper->getTotalBuildingsLost();
        unitsBuilt = pScoreKeeper->getTotalUnitsBuilt();
        unitsDestroyed = pScoreKeeper->getTotalUnitsDestroyed();
        unitsLost = pScoreKeeper->getTotalUnitsLost();
        totalMoney = pScoreKeeper->getTotalMoneyEarned();
	}

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{

		uint64_t currentMatchID = pLobbyInterface->GetCurrentMatchID();

		int resolvedSide = -1;
		GenOnlineGame* myGame = pLobbyInterface->GetCurrentGame();

		    if (myGame != nullptr)
			{
				GameSlot* pLocalSlot = myGame->getSlot(myGame->getLocalSlotNum());
				if (pLocalSlot != nullptr)
				{
					resolvedSide = pLocalSlot->getPlayerTemplate();
				}
            }
		
	    const bool desynced = TheNetwork->sawCRCMismatch();
		const uint32_t duration = TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND;

		GenOnlineRealtime::MatchResultBody request;
		request.won = bWon;
		request.desynced = desynced;
		request.side = resolvedSide;
		request.duration_s = duration;
		request.stats.buildings_built = buildingsBuilt;
		request.stats.buildings_killed = buildingsDestroyed;
		request.stats.buildings_lost = buildingsLost;
		request.stats.units_built = unitsBuilt;
		request.stats.units_killed = unitsDestroyed;
		request.stats.units_lost = unitsLost;
		request.stats.total_money = totalMoney;
		// The result is safe to send again, so a lost answer or a server error retries.
		GenOnlineRealtimeBridge::CallRestWithRetry(GenOnlineRealtimeBridge::RestMethod::Put, GenOnlineRealtime::RestMatchResultPath((int64_t)currentMatchID), nlohmann::json(request).dump(),
			[currentMatchID](const GenOnlineRealtime::RestOutcome& outcome)
			{
				GenOnlineRealtime::MatchResultResponse result;
				if (!outcome.Data(result))
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[REST] Match result for match %llu failed: status %d code %s", currentMatchID, outcome.status, outcome.error.code.c_str());
					return;
				}
				GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
				if (pManager == nullptr)
				{
					return;
				}
				pManager->SetScreenshotS3URI_EndMatch(currentMatchID, result.screenshot_url);
				pManager->SetScreenshotS3URI_Replay(currentMatchID, result.replay_url);

				// The service counts the result into the stats of the player, so the cached own stats are fetched again.
				GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
				GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
				if (pAuthInterface != nullptr && pStatsInterface != nullptr && pAuthInterface->GetUserID() != -1)
				{
					const int64_t localUserID = pAuthInterface->GetUserID();
					pStatsInterface->MarkPlayerStatsCacheStale(localUserID);
					pStatsInterface->findPlayerStatsByID(localUserID, [](bool, PSPlayerStats) {}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
				}
			});
	}
}
