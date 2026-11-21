#pragma once

#include "GenOnlineInclude.h"
#include "NetworkMesh.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

class PSPlayerStats;

struct GlobalStats
{
	PerGeneralMap wins;
	PerGeneralMap matches;
};

enum class EStatsRequestPolicy
{
	CACHED_ONLY,
	RESPECT_CACHE_ALLOW_REQUEST,
	BYPASS_CACHE_FORCE_REQUEST
};

class GenOnlineStatsInterface
{
public:
	GenOnlineStatsInterface();
	~GenOnlineStatsInterface();

	void GetGlobalStats(std::function<void(GlobalStats)> cb);

	Int getPointsForRank(Int rank)
	{
		std::map<int, int> mapRankToXP =
		{
			{RANK_PRIVATE, 0},
			{RANK_CORPORAL, 5},
			{RANK_SERGEANT, 10},
			{RANK_LIEUTENANT, 20},
			{RANK_CAPTAIN, 50},
			{RANK_MAJOR, 100},
			{RANK_COLONEL, 200},
			{RANK_BRIGADIER_GENERAL, 500},
			{RANK_GENERAL, 1000},
			{RANK_COMMANDER_IN_CHIEF, 2000}
		};

		if (rank < mapRankToXP.size())
		{
			return mapRankToXP[rank];
		}

		return 999999;
	}

	void findPlayerStatsByID(int64_t userID, std::function<void(bool, PSPlayerStats)> cb, EStatsRequestPolicy requestPolicy);
	void MarkPlayerStatsCacheStale(int64_t userID);
	void findPlayerStatsByBatch(std::vector<int64_t> vecUserIDs, std::function<void(bool)> cb);
	// Returns the last-known cached values, even when stale. Use
	// HasFreshPlayerStats() to decide whether a refresh is required.
	bool getPlayerStatsFromCache(int64_t userID, PSPlayerStats* outStats);
	bool HasFreshPlayerStats(int64_t userID);

	void CommitMyOutcome(ScoreKeeper* pScoreKeeper, bool bWon);
	/// Sends the last match result again, for fresh upload URLs after the first ones expired.
	void RetryMatchResult(uint64_t matchID);
	/// Tells the service how the upload of one file of a match ended. A null failureReason means it was stored.
	void ReportUpload(uint64_t matchID, const std::string& key, const char* failureReason, int httpStatus);

private:
	struct PlayerStatsCacheEntry
	{
		PSPlayerStats stats;
		std::chrono::steady_clock::time_point lastRefreshAt{};
		std::chrono::steady_clock::time_point lastAccessAt{};
		uint64_t cacheRevision = 0;
		bool hasStats = false;
	};

	PlayerStatsCacheEntry& GetOrCreatePlayerStatsCacheEntry(int64_t userID);
	uint64_t AdvancePlayerStatsCacheRevision(int64_t userID);
	bool TryCachePlayerStats(const PSPlayerStats& stats, uint64_t expectedRevision);

private:
	std::unordered_map<int64_t, PlayerStatsCacheEntry> m_playerStatsCache;
	uint64_t m_nextStatsCacheRevision = 0;
	static constexpr std::chrono::minutes STATS_CACHE_TTL{ 10 };
	static constexpr std::size_t MAX_PLAYER_STATS_CACHE_ENTRIES = 256;
};
