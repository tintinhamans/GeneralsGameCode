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
#include "Common/StatsExporterQuery.h"

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
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameNetwork/ConnectionManager.h"
#include "GameClient/Display.h"
#include "GameLogic/Module/AIUpdate.h"
#include "Common/FramePacer.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "Common/version.h"
#include "GameLogic/Weapon.h"
#include "Common/Radar.h"	// TheRadar: terrain average Z for the view box
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
#include "Common/StatsExporterControl.h"
#include "GameLogic/Module/ParticleUplinkCannonUpdate.h"

#include <malloc.h>	// _aligned_malloc
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>
#include <vector>

#include "StatsExporter.pb.h"

namespace
{
	UnsignedInt intervalFrames(UnsignedInt ms)
	{
		const UnsignedInt frames = (LOGICFRAMES_PER_SECOND * ms + 500) / 1000;
		return (frames == 0) ? 1 : frames;
	}

	// Only structures with something queued are sent, in object-list order, and a queue longer than the cap loses its tail.
	const int kMaxProductionStructures = 16;
	const int kMaxProductionEntries = 6;

	const size_t kMaxTechStructures = 64;
	const size_t kMaxPlayerTechStructures = 32;
	const size_t kMaxUpgrades = 64;
	const size_t kMaxSelectedObjects = 64;

	// Grid cells per side, capping FeedConfig.grid_resolution.
	const Int kMaxGridSize = 64;

	// Supply sources reported per snapshot, non-empty ones first.
	const size_t kMaxSupplySources = 64;

	const size_t kMaxSciences = 64;

	const Int kMaxCounterPlayers = 16;

	// Events per Events frame, equal to the pending cap so one flush always empties the queue.
	const size_t kMaxEventsPerFrame = 512;

	// Once the pending-event cap is reached, new events are dropped.
	const size_t kMaxPendingEvents = 512;

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

	std::string toUtf8(const UnicodeString& text)
	{
		return to_utf8(std::wstring(text.str()));
	}

	// AsciiString text (paths) is in the process code page.
	std::string localToUtf8(const char* text)
	{
		return local_to_utf8(std::string((text != NULL) ? text : ""));
	}

	typedef StatsExporterReaders Readers;

	// Set by the first tick. Hello is sent to new readers only on this thread.
	DWORD s_mainThreadId = 0;

	// Discrete events are queued here and drained at the end of each logic frame.
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
		UnsignedInt objectId;
		UnsignedInt killerObjectId;
		std::string killerTemplate;
		std::string weapon;

		PendingEvent()
			: frame(0)
			, playerIndex(-1)
			, kind(statsexporter::STATEVENT_ABILITY_USE)
			, hasLocation(FALSE)
			, x(0.0f), y(0.0f)
			, intValue1(0), intValue2(0)
			, cost(0)
			, objectId(0)
			, killerObjectId(0)
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

	// The engine operator new is only 4-byte aligned, but protobuf RepeatedField needs 8-byte aligned heap pointers for its tag bits.
	// So every long-lived message lives on this process-lifetime arena, whose blocks bypass the engine operator new.
	// The Arena object itself is tagged the same way, so it must not come from the engine operator new either.
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

	// Arena messages are reused across samples and never deleted. Clear() keeps repeated elements and strings, so the arena stops growing after the first samples.
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

	statsexporter::Events& eventsMessage()
	{
		static statsexporter::Events* s_message = NULL;
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

	// MatchInfo state for one match at a time, cached at STARTED and reused by ENDED.
	Bool s_matchActive = FALSE;
	Bool s_matchEndedSent = FALSE;
	UnsignedInt s_matchSerial = 0;	// counts matches in this process, so per-match caches know when to start over
	std::string s_matchId;
	statsexporter::MatchMode s_matchMode = statsexporter::MATCH_MODE_UNKNOWN;
	std::string s_matchMapName;
	Bool s_matchMapIsUserMap = FALSE;
	UnsignedInt s_matchStartUnixTime = 0;
	UnsignedInt s_matchStartFrame = 0;
	// The readers a MatchInfo STARTED was sent to, at their epochs. A reader missing here has not seen it yet.
	Readers s_matchInfoSent;

	// Players that left (quit, disconnect or surrender) this match. PLAYER_LEFT is emitted once per player.
	Bool s_playerLeft[16];

	// The last Generals Online match id that was exported. The lobby keeps the previous id until the next one is assigned.
	uint64_t s_lastExportedOnlineMatchId = 0;

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

	// Time the logic has been waiting on a peer, from wall-clock time, since the engine keeps no frame counter for it.
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

	// 0 infantry, 1 vehicle, 2 aircraft, 3 structure, -1 for anything else. Aircraft come first because a plane can also carry the vehicle kind.
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

	// The hooks that feed the in-view tracking run inside the logic and only read, so they never draw a random number or change an object.
	const Int kBucketsPerSecond = 10;							// rolling windows are kept in 100 ms buckets of game time
	const Int kWindowBuckets = kBucketsPerSecond * 60;			// the longest window is 60 s
	const Int kInterestGrid = 64;								// the interest map is kept at the finest resolution and summed down for coarser readers
	const size_t kMaxNotablePerPlayer = 8;
	const size_t kMaxHighlights = 16;
	const Real kInterestDamageWeight = 1.0f;					// per health point dealt
	const Real kInterestKillWeight = 0.5f;						// per build cost unit destroyed
	const Real kInterestContestWeight = 0.05f;					// per build cost unit of the weaker side where armies of enemies stand together
	const Real kInterestStructureWeight = 0.5f;					// a structure counts this much of its cost in an army
	const UnsignedInt kInterestHalfLifeMs = 6000;
	const UnsignedInt kHighlightStructureCost = 1000;
	const UnsignedInt kHighlightSeconds = 6;					// how long a hero death or a structure loss stays listed
	const UnsignedInt kHighlightUnknownImpactSeconds = 45;		// how long a superweapon launch stays listed when the game gives no impact time

	struct WindowBucket
	{
		UnsignedInt id;
		UnsignedInt killed[16];
		UnsignedInt lost[16];
		Real damage[16];
	};
	WindowBucket s_window[kWindowBuckets];

	struct ViewPolygon
	{
		Bool valid;
		Real x[4];
		Real y[4];
		Real minX, maxX, minY, maxY;
	};
	ViewPolygon s_view;

	Real s_interestEvents[kInterestGrid * kInterestGrid];
	UnsignedInt s_interestDecayFrame = 0;
	Region3D s_interestExtent;
	Bool s_interestExtentValid = FALSE;

	struct Highlight
	{
		UnsignedInt id;
		int kind;				// statsexporter::HighlightKind
		Int playerIndex;
		std::string templateName;
		std::string powerName;
		Real x, y;
		UnsignedInt startFrame;
		UnsignedInt impactFrame;
		Bool estimated;
		UnsignedInt expireFrame;
		UnsignedInt value;
		UnsignedInt objectId;
	};

	std::vector<Highlight>& highlights()
	{
		static std::vector<Highlight> s_highlights;
		return s_highlights;
	}
	UnsignedInt s_highlightSerial = 0;
	Bool s_highlightsDirty = FALSE;

	// BEGIN-PURE (copied into the test harness)
	// A point in a four sided polygon, even-odd rule, either winding.
	Bool pointInQuad(const Real* px, const Real* py, Real x, Real y)
	{
		Bool inside = FALSE;
		Int j = 3;
		for (Int i = 0; i < 4; ++i)
		{
			if (((py[i] > y) != (py[j] > y)) && (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]))
			{
				inside = !inside;
			}
			j = i;
		}
		return inside;
	}

	Real halfLifeFactor(UnsignedInt frames, UnsignedInt halfLifeMs)
	{
		const Real seconds = (Real)frames / (Real)LOGICFRAMES_PER_SECOND;
		return powf(0.5f, seconds * 1000.0f / (Real)halfLifeMs);
	}

	UnsignedInt bucketIdOf(UnsignedInt frame)
	{
		return (UnsignedInt)(((UnsignedInt64)frame * kBucketsPerSecond) / LOGICFRAMES_PER_SECOND);
	}

	WindowBucket& windowBucket(UnsignedInt frame)
	{
		const UnsignedInt id = bucketIdOf(frame);
		WindowBucket& bucket = s_window[id % kWindowBuckets];
		if (bucket.id != id)
		{
			memset(&bucket, 0, sizeof(bucket));
			bucket.id = id;
		}
		return bucket;
	}

	// Any of the output pointers can be NULL.
	void windowSums(UnsignedInt frame, UnsignedInt windowMs, UnsignedInt* killed, UnsignedInt* lost, Real* damage)
	{
		const UnsignedInt nowId = bucketIdOf(frame);
		UnsignedInt count = (windowMs + 99) / 100;
		if (count > (UnsignedInt)kWindowBuckets)
		{
			count = kWindowBuckets;
		}
		for (UnsignedInt back = 0; back < count && back <= nowId; ++back)
		{
			const WindowBucket& bucket = s_window[(nowId - back) % kWindowBuckets];
			if (bucket.id != nowId - back)
			{
				continue;
			}
			for (Int p = 0; p < 16; ++p)
			{
				if (killed != NULL)
				{
					killed[p] += bucket.killed[p];
				}
				if (lost != NULL)
				{
					lost[p] += bucket.lost[p];
				}
				if (damage != NULL)
				{
					damage[p] += bucket.damage[p];
				}
			}
		}
	}

	Bool interestCellOf(const Region3D& extent, Real x, Real y, Int* cell)
	{
		if (extent.width() <= 0.0f || extent.height() <= 0.0f || x < extent.lo.x || y < extent.lo.y || x > extent.hi.x || y > extent.hi.y)
		{
			return FALSE;
		}
		Int cellX = (Int)((x - extent.lo.x) / extent.width() * (Real)kInterestGrid);
		Int cellY = (Int)((y - extent.lo.y) / extent.height() * (Real)kInterestGrid);
		cellX = (cellX >= kInterestGrid) ? kInterestGrid - 1 : cellX;
		cellY = (cellY >= kInterestGrid) ? kInterestGrid - 1 : cellY;
		*cell = cellY * kInterestGrid + cellX;
		return TRUE;
	}
	// END-PURE

	Bool pointInView(Real x, Real y)
	{
		return s_view.valid && x >= s_view.minX && x <= s_view.maxX && y >= s_view.minY && y <= s_view.maxY && pointInQuad(s_view.x, s_view.y, x, y);
	}

	// Scores decay lazily, in steps of at least 100 ms of game time.
	void decayInterest(UnsignedInt now)
	{
		if (now < s_interestDecayFrame)
		{
			s_interestDecayFrame = now;
			return;
		}
		const UnsignedInt elapsed = now - s_interestDecayFrame;
		if (elapsed * 10 < (UnsignedInt)LOGICFRAMES_PER_SECOND)
		{
			return;
		}
		const Real factor = halfLifeFactor(elapsed, kInterestHalfLifeMs);
		for (Int cell = 0; cell < kInterestGrid * kInterestGrid; ++cell)
		{
			s_interestEvents[cell] *= factor;
		}
		s_interestDecayFrame = now;
	}

	// The playable extent the interest grid covers, read once per match.
	Bool ensureInterestExtent()
	{
		if (!s_interestExtentValid && TheTerrainLogic != NULL)
		{
			s_interestExtent.zero();
			TheTerrainLogic->getExtent(&s_interestExtent);
			s_interestExtentValid = s_interestExtent.width() > 0.0f && s_interestExtent.height() > 0.0f;
		}
		return s_interestExtentValid;
	}

	void addInterest(Real x, Real y, Real amount, UnsignedInt now)
	{
		Int cell = 0;
		if (!ensureInterestExtent() || !interestCellOf(s_interestExtent, x, y, &cell))
		{
			return;
		}
		decayInterest(now);
		s_interestEvents[cell] += amount;
	}

	void resetViewTracking()
	{
		memset(s_window, 0, sizeof(s_window));
		memset(&s_view, 0, sizeof(s_view));
		memset(s_interestEvents, 0, sizeof(s_interestEvents));
		s_interestDecayFrame = 0;
		s_interestExtentValid = FALSE;
		highlights().clear();
		s_highlightSerial = 0;
		s_highlightsDirty = FALSE;
	}

	void addHighlight(int kind, Int playerIndex, const char* templateName, const char* powerName, Real x, Real y,
		UnsignedInt now, UnsignedInt impactFrame, Bool estimated, UnsignedInt expireFrame, UnsignedInt value, UnsignedInt objectId)
	{
		std::vector<Highlight>& list = highlights();
		if (list.size() >= kMaxHighlights)
		{
			list.erase(list.begin());
		}
		Highlight highlight;
		highlight.id = ++s_highlightSerial;
		highlight.kind = kind;
		highlight.playerIndex = playerIndex;
		highlight.templateName = templateName;
		highlight.powerName = powerName;
		highlight.x = x;
		highlight.y = y;
		highlight.startFrame = now;
		highlight.impactFrame = impactFrame;
		highlight.estimated = estimated;
		highlight.expireFrame = expireFrame;
		highlight.value = value;
		highlight.objectId = objectId;
		list.push_back(highlight);
		s_highlightsDirty = TRUE;
	}

	Highlight* findLaunchHighlight(Int playerIndex, Real x, Real y, UnsignedInt now)
	{
		std::vector<Highlight>& list = highlights();
		for (size_t i = 0; i < list.size(); ++i)
		{
			Highlight& h = list[i];
			if (h.kind == statsexporter::HIGHLIGHT_SUPERWEAPON_LAUNCH && h.playerIndex == playerIndex && h.expireFrame > now
				&& fabsf(h.x - x) < 100.0f && fabsf(h.y - y) < 100.0f)
			{
				return &h;
			}
		}
		return NULL;
	}

	Bool viewTrackingActive()
	{
		StatsExporterSink* sink = StatsExporterPipeGetSink();
		return sink != NULL && sink->isConnected() && TheGameLogic != NULL && TheGameLogic->isInInteractiveGame() && TheGameLogic->getFrame() > 0;
	}

	void resetMatchCounters()
	{
		resetViewTracking();
		memset(s_actionsTotal, 0, sizeof(s_actionsTotal));
		memset(s_actionBucket, 0, sizeof(s_actionBucket));
		memset(s_actionBucketSecond, 0, sizeof(s_actionBucketSecond));
		memset(s_valueDestroyed, 0, sizeof(s_valueDestroyed));
		memset(s_valueLost, 0, sizeof(s_valueLost));
		memset(s_kills, 0, sizeof(s_kills));
		memset(s_losses, 0, sizeof(s_losses));
		memset(s_spent, 0, sizeof(s_spent));
		memset(s_playerLeft, 0, sizeof(s_playerLeft));
		heroRecords().clear();
	}

	// Sciences are sent by name each sample. Names are cached by science id so the sample does not build strings.
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
	Readers s_loadingSent;

	// The lobby prediction last sent, kept to compare against the values the engine resolved at LOADING.
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

	// GAME_SHELL and GAME_NONE never reach this.
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

	Int localPlayerIndex()
	{
		Player* local = (ThePlayerList != NULL) ? ThePlayerList->getLocalPlayer() : NULL;
		return (local != NULL) ? local->getPlayerIndex() : -1;
	}

	// Online match data is captured at STARTED so ENDED and reconnects agree.
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

	// A map path with the user data folder stripped so it never carries the Windows user name. Sets isUserMap when it did.
	std::string exportedMapNameLocal(const char* raw, Bool* isUserMap)
	{
		std::string name = (raw != NULL) ? raw : "";
		if (isUserMap != NULL)
		{
			*isUserMap = FALSE;
		}
		if (TheGlobalData == NULL)
		{
			return name;
		}
		const AsciiString& prefix = TheGlobalData->getPath_UserData();
		const size_t length = (size_t)prefix.getLength();
		if (length == 0 || name.size() <= length)
		{
			return name;
		}
		for (size_t i = 0; i < length; ++i)
		{
			char a = name[i];
			char b = prefix.str()[i];
			if (a == '/')
			{
				a = '\\';
			}
			if (b == '/')
			{
				b = '\\';
			}
			if (tolower((unsigned char)a) != tolower((unsigned char)b))
			{
				return name;
			}
		}
		name.erase(0, length);
		if (isUserMap != NULL)
		{
			*isUserMap = TRUE;
		}
		return name;
	}

	std::string exportedMapName(const char* raw, Bool* isUserMap)
	{
		return localToUtf8(exportedMapNameLocal(raw, isUserMap).c_str());
	}

	// 0 when the lobby holds no id. The last exported id counts as unknown because the lobby keeps it until the next one is assigned.
	uint64_t currentOnlineMatchId()
	{
		GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		const uint64_t id = (lobby != nullptr) ? lobby->GetCurrentMatchID() : 0;
		return (id != s_lastExportedOnlineMatchId) ? id : 0;
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

	// Fields that are not known stay -1.
	// reported_fps and latency_ms come from the run ahead metrics of the engine. Each client measures its own display FPS and frame
	// round-trip latency and sends them to the packet router only, so a client that is not the router knows only its own.
	// ping_ms / relayed come from the Generals Online mesh connection between the local client and that player.
	void fillNetworkStats(statsexporter::PlayerStats* out, Int slot, const Player* player)
	{
		if (TheNetwork == NULL || TheGameInfo == NULL)
		{
			return;
		}
		const GameSlot* gameSlot = TheGameInfo->getConstSlot(slot);
		if (gameSlot == NULL || !gameSlot->isHuman())
		{
			return;
		}

		statsexporter::NetworkStats* network = out->mutable_network();
		const Bool isLocal = (player == ThePlayerList->getLocalPlayer());
		network->set_is_local(isLocal);
		network->set_reported_fps(-1);
		network->set_latency_ms(-1);
		network->set_ping_ms(-1);

		ConnectionManager* connections = TheNetwork->GetConnectionManager();
		if (connections != NULL)
		{
			// The numbers of another player count only once their run ahead metrics have arrived. Before that the engine holds seeds or zeros.
			const Bool known = isLocal || connections->hasRunAheadMetrics(slot);
			const Int fps = isLocal ? connections->getAverageFPS() : (known ? connections->getSlotAverageFPS(slot) : -1);
			const Real latencySeconds = isLocal ? connections->getLocalAverageLatency() : (known ? connections->getSlotAverageLatency(slot) : -1.0f);
			network->set_reported_fps(fps >= 0 ? fps : -1);
			network->set_latency_ms(latencySeconds >= 0.0f ? (Int)(latencySeconds * 1000.0f + 0.5f) : -1);
		}

		if (!isLocal && TheGenOnlineGame != nullptr)
		{
			GenOnlineGameSlot* onlineSlot = TheGenOnlineGame->getGameSpySlot(slot);
			NetworkMesh* mesh = GenOnlineServicesManager::GetNetworkMesh();
			if (onlineSlot != nullptr && onlineSlot->isHuman() && mesh != nullptr)
			{
				std::map<int64_t, PlayerConnection>& meshConnections = mesh->GetAllConnections();
				std::map<int64_t, PlayerConnection>::iterator it = meshConnections.find(onlineSlot->m_userID);
				if (it != meshConnections.end() && it->second.IsValid())
				{
					network->set_ping_ms(it->second.GetLatency());
					network->set_relayed(!it->second.IsDirect());
				}
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

	// The observer camera starts on the start position of the local slot (GameLogic::tryStartNewGame reads the same slot),
	// which for an observer is a spot a player holds. The engine already picked it, so nothing is drawn here.
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

	// Fixes the match identity (id, mode, map, start time, online data) once per match, at LOADING or at STARTED.
	void beginMatchState(GameInfo* game)
	{
		s_matchActive = TRUE;
		s_matchEndedSent = FALSE;
		++s_matchSerial;
		s_matchMode = mapMatchMode();
		captureOnlineMatch();
		captureObserverCamera(game);
		resetMatchCounters();
		// The Generals Online match id when this is an online match and the lobby holds one now. It is fixed for the whole match.
		const uint64_t onlineMatchId = s_hasOnlineMatch ? onlineMatchMessage().match_id() : 0;
		if (onlineMatchId != 0)
		{
			s_lastExportedOnlineMatchId = onlineMatchId;
			char idText[32];
			sprintf(idText, "%I64u", onlineMatchId);
			s_matchId = idText;
		}
		else
		{
			s_matchId = buildMatchId();
		}
		s_matchMapName = exportedMapName((TheWritableGlobalData != NULL) ? TheWritableGlobalData->m_mapName.str() : "", &s_matchMapIsUserMap);
		s_matchStartUnixTime = (UnsignedInt)time(NULL);
		s_matchStartFrame = TheGameLogic->getFrame();
	}

	// An oversized message is dropped, never sent partially.
	void sendMessage(StatsExporterSink* sink, UnsignedByte frameType, const google::protobuf::MessageLite& message, const Readers& to)
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
		sink->enqueueFrame(frameType, (const UnsignedByte*)buffer.data(), (UnsignedInt)size, to);
	}

	statsexporter::Hello& helloMessage()
	{
		static statsexporter::Hello* s_message = NULL;
		return arenaMessage(s_message);
	}

	void sendHello(StatsExporterSink* sink, Int slot, const Readers& to)
	{
		static const char* const kCapabilities[] =
		{
			"frame.sequence", "frame.timestamp", "multi_reader", "chat.send",
			"request.catalog", "request.object", "request.objects", "request.player", "request.map", "request.resync", "request.feed_config",
			"map.minimap_rgba", "event.object_ids", "event.kill_detail", "feed.per_reader",
			"camera.control", "camera.follow", "camera.source", "replay.control", "observe.player", "view_stats", "interest_map", "status.not_allowed", "lobby.control"
		};

		statsexporter::Hello& hello = helloMessage();
		hello.Clear();
		hello.set_protocol_version(STATS_EXPORTER_PROTOCOL_VERSION);
		if (TheVersion != NULL)
		{
			hello.set_game_version(TheVersion->getAsciiVersion().str());
			std::string build = TheVersion->getAsciiGitVersion().str();
			build += " ";
			build += TheVersion->getAsciiBuildTime().str();
			hello.set_game_build(build);
		}
		hello.set_process_id(GetCurrentProcessId());
		hello.set_logic_frames_per_second(LOGICFRAMES_PER_SECOND);
		for (size_t i = 0; i < sizeof(kCapabilities) / sizeof(kCapabilities[0]); ++i)
		{
			hello.add_capabilities(kCapabilities[i]);
		}
		hello.set_max_requests_per_second(STATS_EXPORTER_MAX_REQUESTS_PER_SECOND);
		hello.set_reader_slot((uint32_t)slot);
		hello.set_max_readers(STATS_EXPORTER_MAX_READERS);
		hello.set_epoch_unix_ms(StatsExporterEpochUnixMs());
		hello.set_max_request_burst(STATS_EXPORTER_REQUEST_BURST);
		hello.set_max_page_size(STATS_EXPORTER_MAX_PAGE_SIZE);
		hello.set_queue_capacity_bytes(STATS_EXPORTER_QUEUE_CAPACITY_BYTES);
		sendMessage(sink, STATS_EXPORTER_FRAME_HELLO, hello, to);
	}

	// Sends the Hello to every reader that connected since the last call, and only then lets the other frames reach it.
	void welcomeNewReaders(StatsExporterSink* sink)
	{
		Readers fresh;
		sink->getUnwelcomedReaders(fresh);
		for (Int slot = 0; slot < STATS_EXPORTER_MAX_READERS; ++slot)
		{
			if (!fresh.has(slot))
			{
				continue;
			}
			Readers one;
			one.add(slot, fresh.epoch(slot));
			sendHello(sink, slot, one);
			sink->markWelcomed(one);
		}
	}

	// The readers frames can be sent to now (their Hello is out). On the main thread this welcomes new readers first.
	Readers readyReaders(StatsExporterSink* sink)
	{
		Readers readers;
		if (GetCurrentThreadId() == s_mainThreadId)
		{
			welcomeNewReaders(sink);
		}
		sink->getReaders(readers);
		return readers;
	}

	Readers feedFilter(StatsExporterSink* sink, const Readers& readers, Bool StatsExporterFeedConfig::* flag)
	{
		Readers result;
		for (Int slot = 0; slot < STATS_EXPORTER_MAX_READERS; ++slot)
		{
			if (!readers.has(slot))
			{
				continue;
			}
			StatsExporterFeedConfig config;
			sink->getFeedConfig(slot, config);
			if (config.*flag)
			{
				result.add(slot, readers.epoch(slot));
			}
		}
		return result;
	}

	// The weapon of `killer` that fired last, "" if it never fired one.
	std::string lastFiredWeaponName(const Object* killer)
	{
		const Weapon* best = NULL;
		for (Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
		{
			const Weapon* weapon = killer->getWeaponInWeaponSlot((WeaponSlotType)slot);
			if (weapon != NULL && weapon->getLastShotFrame() > 0 && (best == NULL || weapon->getLastShotFrame() > best->getLastShotFrame()))
			{
				best = weapon;
			}
		}
		return (best != NULL) ? best->getName().str() : "";
	}

	// The defeat state of the engine: killed (surrender, elimination) or nothing left that counts for victory.
	Bool isPlayerDefeated(Player* player)
	{
		return player->isPlayerDead() || (TheVictoryConditions != NULL && TheVictoryConditions->hasSinglePlayerBeenDefeated(player));
	}

	Bool hasPlayerLeft(const Player* player)
	{
		const Int index = player->getPlayerIndex();
		return index >= 0 && index < kMaxCounterPlayers && s_playerLeft[index];
	}

	// Per-slot MatchPlayer builder. Results are UNKNOWN (OBSERVER for observers) until `ended`. On ENDED a decisive result
	// (`completed`) gives the victorious alliance WON and everyone else LOST, and without one a player who left is LEFT, a defeated
	// player LOST and any other UNKNOWN.
	void buildMatchPlayers(statsexporter::MatchInfo& info, Bool ended, Bool completed)
	{
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
			p->set_name(toUtf8(player->getPlayerDisplayName()));
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
			else if (!ended)
			{
				p->set_result(statsexporter::PLAYER_RESULT_UNKNOWN);
			}
			else if (completed && TheVictoryConditions != NULL)
			{
				p->set_result(TheVictoryConditions->hasAchievedVictory(player) ? statsexporter::PLAYER_RESULT_WON : statsexporter::PLAYER_RESULT_LOST);
			}
			else if (hasPlayerLeft(player))
			{
				p->set_result(statsexporter::PLAYER_RESULT_LEFT);
			}
			else if (isPlayerDefeated(player))
			{
				p->set_result(statsexporter::PLAYER_RESULT_LOST);
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
		info.set_map_is_user_map(s_matchMapIsUserMap);
		info.set_logic_frames_per_second(LOGICFRAMES_PER_SECOND);
		info.set_start_unix_time(s_matchStartUnixTime);
		info.set_duration_frames(durationFrames);
		info.set_replay_path(localToUtf8(replayPath.str()));
		info.set_replay_is_playback(replayIsPlayback);
		info.set_completed(completed);
		info.set_local_player_index(localPlayerIndex());
		buildMatchPlayers(info, phase == statsexporter::MATCH_PHASE_ENDED, completed);

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
		// Single choke point for every event hook: nothing accumulates while no reader is connected.
		StatsExporterSink* sink = StatsExporterPipeGetSink();
		if (sink == NULL || !sink->isConnected())
		{
			return;
		}

		// The shell map is a scripted game too, so do not let its events leak into the next match.
		if (TheGameLogic == NULL || !TheGameLogic->isInInteractiveGame())
		{
			return;
		}

		// Nothing before the first logic frame: the map load places the starting buildings and units, and grants radar and upgrades.
		if (TheGameLogic->getFrame() == 0)
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

	void fillSpeedFields(statsexporter::StatsSnapshot& snapshot)
	{
		if (TheFramePacer != NULL)
		{
			const Int speedFps = TheFramePacer->getActualLogicTimeScaleFps();
			// -1 marks an uncapped logic rate and 0 is reserved for a frozen or halted game
			snapshot.set_game_speed_fps(speedFps >= (Int)RenderFpsPreset::UncappedFpsValue ? -1 : speedFps);
			snapshot.set_game_speed_ratio(TheFramePacer->getActualLogicTimeScaleRatio());
		}
		if (TheDisplay != NULL)
		{
			snapshot.set_local_render_fps(TheDisplay->getAverageFPS());
		}
	}

	// The readers of `readers` that already have STARTED, which snapshots, Events and Camera wait for.
	Readers startedReaders(const Readers& readers)
	{
		return s_matchActive ? readers.both(s_matchInfoSent) : Readers();
	}

	// Sends the pending events to the readers that want them, one Events frame per logic frame they happened in, and empties the queue.
	void flushPendingEvents(StatsExporterSink* sink, const Readers& started)
	{
		std::vector<PendingEvent>& events = pendingEvents();
		const Readers recipients = feedFilter(sink, started, &StatsExporterFeedConfig::events);
		if (recipients.empty())
		{
			events.clear();
			return;
		}
		while (!events.empty())
		{
			const UnsignedInt frame = events[0].frame;
			size_t count = 0;
			while (count < events.size() && count < kMaxEventsPerFrame && events[count].frame == frame)
			{
				++count;
			}

			statsexporter::Events& message = eventsMessage();
			message.Clear();
			message.set_frame(frame);
			for (size_t i = 0; i < count; ++i)
			{
				const PendingEvent& src = events[i];
				statsexporter::StatEvent* dst = message.add_events();
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
				dst->set_object_id(src.objectId);
				dst->set_killer_object_id(src.killerObjectId);
				dst->set_killer_template(src.killerTemplate);
				dst->set_weapon(src.weapon);
			}
			events.erase(events.begin(), events.begin() + count);
			sendMessage(sink, STATS_EXPORTER_FRAME_EVENTS, message, recipients);
		}
	}

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

	// Per player, indexed by template id: where the entry of that template sits in templateCountRefs(). An entry is
	// valid only when its generation matches that of the current snapshot, so nothing is cleared between snapshots.
	struct TemplateCountSlot
	{
		UnsignedInt generation;
		UnsignedInt index;
	};
	UnsignedInt s_templateCountGeneration = 0;

	std::vector<TemplateCountSlot>& templateCountSlots(Int playerIndex)
	{
		static std::vector<TemplateCountSlot> s_slots[kMaxCounterPlayers];
		return s_slots[playerIndex];
	}

	void beginTemplateCounts()
	{
		templateCountRefs().clear();
		if (++s_templateCountGeneration == 0)
		{
			for (Int i = 0; i < kMaxCounterPlayers; ++i)
			{
				templateCountSlots(i).clear();
			}
			s_templateCountGeneration = 1;
		}
	}

	statsexporter::TemplateCount* templateCount(statsexporter::PlayerStats* accumulator, Int playerIndex, const ThingTemplate* tmpl)
	{
		std::vector<TemplateCountRef>& refs = templateCountRefs();
		TemplateCountSlot* slot = NULL;
		if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers)
		{
			std::vector<TemplateCountSlot>& slots = templateCountSlots(playerIndex);
			const UnsignedShort id = tmpl->getTemplateID();
			if (id >= slots.size())
			{
				TemplateCountSlot unused;
				unused.generation = 0;
				unused.index = 0;
				slots.resize((size_t)id + 1, unused);
			}
			slot = &slots[id];
			if (slot->generation == s_templateCountGeneration)
			{
				return refs[slot->index].count;
			}
		}
		else
		{
			for (size_t i = 0; i < refs.size(); ++i)
			{
				if (refs[i].tmpl == tmpl && refs[i].playerIndex == playerIndex)
				{
					return refs[i].count;
				}
			}
		}

		statsexporter::TemplateCount* found = accumulator->add_live_template_counts();
		found->set_template_name(tmpl->getName().str());
		TemplateCountRef ref;
		ref.playerIndex = playerIndex;
		ref.tmpl = tmpl;
		ref.count = found;
		if (slot != NULL)
		{
			slot->generation = s_templateCountGeneration;
			slot->index = (UnsignedInt)refs.size();
		}
		refs.push_back(ref);
		return found;
	}
}

google::protobuf::Arena& StatsExporterArena()
{
	return exporterArena();
}

std::string StatsExporterCurrentMapName()
{
	return (TheWritableGlobalData != NULL) ? exportedMapName(TheWritableGlobalData->m_mapName.str(), NULL) : std::string();
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
	// With a target (building capture) secondary_name is the target and the acting unit moves to the variant, otherwise it is the source.
	event.secondaryName = (targetTmpl != NULL) ? templateName(targetTmpl) : templateName(sourceTmpl);
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
	else if (targetTmpl != NULL && sourceTmpl != NULL)
	{
		event.variant = std::string("source=") + templateName(sourceTmpl);
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

void StatsExporterLiveBuild(Int playerIndex, const ThingTemplate* tmpl, ObjectID objectId, Int veterancyLevel)
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
	event.intValue1 = veterancyLevel;	// the level the object starts with, not a promotion
	event.objectId = objectId;
	pushPendingEvent(event);
}

void StatsExporterLiveKill(Int playerIndex, const Object* killer, const Object* victim)
{
	if (StatsExporterPipeGetSink() == NULL || victim == NULL)
	{
		return;
	}

	const ThingTemplate* victimTmpl = victim->getTemplate();
	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_KILL;
	event.category = classifyThingTemplateCategory(victimTmpl);
	event.primaryName = templateName(victimTmpl);
	event.objectId = victim->getID();
	if (killer != NULL)
	{
		event.killerObjectId = killer->getID();
		event.killerTemplate = templateName(killer->getTemplate());
		event.weapon = lastFiredWeaponName(killer);
	}
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
	if (viewTrackingActive())
	{
		const Coord3D* where = victim->getPosition();
		const UnsignedInt now = TheGameLogic->getFrame();
		addInterest(where->x, where->y, (Real)event.cost * kInterestKillWeight, now);
		if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers && pointInView(where->x, where->y))
		{
			windowBucket(now).killed[playerIndex] += event.cost;
		}
	}
	pushPendingEvent(event);
}

void StatsExporterLiveLoss(Int playerIndex, const Object* victim, const Object* killer)
{
	if (StatsExporterPipeGetSink() == NULL || victim == NULL)
	{
		return;
	}

	const ThingTemplate* victimTmpl = victim->getTemplate();
	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_LOSS;
	event.category = classifyThingTemplateCategory(victimTmpl);
	event.primaryName = templateName(victimTmpl);
	event.objectId = victim->getID();
	if (killer != NULL)
	{
		event.killerObjectId = killer->getID();
		event.killerTemplate = templateName(killer->getTemplate());
		event.weapon = lastFiredWeaponName(killer);
	}
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
	if (viewTrackingActive())
	{
		const Coord3D* where = victim->getPosition();
		const UnsignedInt now = TheGameLogic->getFrame();
		if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers && pointInView(where->x, where->y))
		{
			windowBucket(now).lost[playerIndex] += event.cost;
		}
		const UnsignedInt expire = now + kHighlightSeconds * LOGICFRAMES_PER_SECOND;
		if (victimTmpl != NULL && victimTmpl->isKindOf(KINDOF_HERO))
		{
			addHighlight(statsexporter::HIGHLIGHT_HERO_DEATH, playerIndex, templateName(victimTmpl), "", where->x, where->y, now, 0, FALSE, expire, event.cost, victim->getID());
		}
		else if (victimTmpl != NULL && victimTmpl->isKindOf(KINDOF_STRUCTURE) && event.cost >= kHighlightStructureCost)
		{
			addHighlight(statsexporter::HIGHLIGHT_STRUCTURE_LOST, playerIndex, templateName(victimTmpl), "", where->x, where->y, now, 0, FALSE, expire, event.cost, victim->getID());
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

	// Only promotions earned from experience come through here, never levels assigned at creation.
	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_VETERANCY_CHANGE;
	event.category = classifyThingTemplateCategory(tmpl);
	event.primaryName = templateName(tmpl);
	event.intValue1 = oldLevel;
	event.intValue2 = newLevel;
	event.objectId = objectId;
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

void StatsExporterLiveDamage(const Object* victim, ObjectID attackerId, Real healthRemoved)
{
	if (victim == NULL || healthRemoved <= 0.0f || !viewTrackingActive() || killCategory(victim->getTemplate()) < 0)
	{
		return;
	}

	// Only damage between enemies is fighting. Fire on neutral objects, friendly fire and damage nobody dealt are not.
	const Object* attacker = TheGameLogic->findObjectByID(attackerId);
	const Player* attackerOwner = (attacker != NULL) ? attacker->getControllingPlayer() : NULL;
	if (attackerOwner == NULL || attackerOwner == ThePlayerList->getNeutralPlayer() || attackerOwner->getRelationship(victim->getTeam()) != ENEMIES)
	{
		return;
	}

	const Coord3D* where = victim->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();
	addInterest(where->x, where->y, healthRemoved * kInterestDamageWeight, now);
	const Int attackerIndex = attackerOwner->getPlayerIndex();
	if (attackerIndex >= 0 && attackerIndex < kMaxCounterPlayers && pointInView(where->x, where->y))
	{
		windowBucket(now).damage[attackerIndex] += healthRemoved;
	}
}

void StatsExporterLiveSuperweaponFired(const Object* source, const SpecialPowerTemplate* power, const Coord3D* location)
{
	if (source == NULL || power == NULL || location == NULL || !isSuperweaponPower(power) || !viewTrackingActive())
	{
		return;
	}
	const Player* owner = source->getControllingPlayer();
	if (owner == NULL)
	{
		return;
	}

	// A launch is listed once, whichever of the two hooks (this one or the one on the missile) sees it first.
	const UnsignedInt now = TheGameLogic->getFrame();
	if (findLaunchHighlight(owner->getPlayerIndex(), location->x, location->y, now) != NULL)
	{
		return;
	}

	UnsignedInt impact = 0;
	UnsignedInt expire = now + kHighlightUnknownImpactSeconds * LOGICFRAMES_PER_SECOND;
	const SpecialPowerType type = power->getSpecialPowerType();
	if (type == SPECIAL_PARTICLE_UPLINK_CANNON || type == SUPW_SPECIAL_PARTICLE_UPLINK_CANNON || type == LAZR_SPECIAL_PARTICLE_UPLINK_CANNON)
	{
		// The cannon keeps a schedule: the beam is born beam_travel frames after the launch, and fires for the total firing time.
		static const NameKeyType cannonKey = TheNameKeyGenerator->nameToKey("ParticleUplinkCannonUpdate");
		const ParticleUplinkCannonUpdate* cannon = static_cast<const ParticleUplinkCannonUpdate*>(source->findUpdateModule(cannonKey));
		if (cannon != NULL)
		{
			UnsignedInt beamTravel = 0;
			UnsignedInt firing = 0;
			UnsignedInt grow = 0;
			cannon->getBeamScheduleForStats(beamTravel, firing, grow);
			impact = now + beamTravel;
			expire = impact + firing + grow + 2 * LOGICFRAMES_PER_SECOND;
		}
	}
	addHighlight(statsexporter::HIGHLIGHT_SUPERWEAPON_LAUNCH, owner->getPlayerIndex(), templateName(source->getTemplate()), power->getName().str(),
		location->x, location->y, now, impact, FALSE, expire, 0, source->getID());
}

void StatsExporterLiveMissileLaunched(const Object* missile, const Coord3D* target, Real secondsToImpact)
{
	if (missile == NULL || target == NULL || secondsToImpact <= 0.0f || !viewTrackingActive())
	{
		return;
	}
	const Player* owner = missile->getControllingPlayer();
	if (owner == NULL)
	{
		return;
	}

	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt impact = now + (UnsignedInt)(secondsToImpact * (Real)LOGICFRAMES_PER_SECOND + 0.5f);
	const UnsignedInt expire = impact + 5 * LOGICFRAMES_PER_SECOND;
	Highlight* known = findLaunchHighlight(owner->getPlayerIndex(), target->x, target->y, now);
	if (known != NULL)
	{
		// The flight model is an estimate built from the data of the missile. The launch highlight made at the trigger gets its time now.
		known->impactFrame = impact;
		known->estimated = TRUE;
		known->expireFrame = expire;
		s_highlightsDirty = TRUE;
		return;
	}
	addHighlight(statsexporter::HIGHLIGHT_SUPERWEAPON_LAUNCH, owner->getPlayerIndex(), templateName(missile->getTemplate()), "",
		target->x, target->y, now, impact, TRUE, expire, 0, missile->getID());
}

namespace
{
	struct SnapshotOptions
	{
		Bool grids;
		Bool supply;
		Bool tech;
		Bool production;
		UnsignedInt gridResolution;
		Real localLogicFps;

		SnapshotOptions()
			: grids(TRUE), supply(TRUE), tech(TRUE), production(TRUE), gridResolution(32), localLogicFps(0.0f)
		{
		}
	};

	Bool buildSnapshot(UnsignedInt now, Bool full, const SnapshotOptions& options)
	{
		statsexporter::StatsSnapshot& snapshot = snapshotMessage();
		snapshot.Clear();
		snapshot.set_frame(now);
		snapshot.set_full(full);
		beginTemplateCounts();

		if (TheRecorder != NULL && TheRecorder->isPlaybackMode())
		{
			snapshot.set_replay_total_frames(TheRecorder->getPlaybackFrameCount());
		}
		fillSpeedFields(snapshot);

		static UnsignedInt s_gridCells[MAX_SLOTS][kMaxGridSize * kMaxGridSize];
		const Int gridSize = (options.gridResolution > (UnsignedInt)kMaxGridSize) ? kMaxGridSize : (Int)options.gridResolution;
		Region3D gridExtent;
		gridExtent.zero();
		if (TheTerrainLogic != NULL)
		{
			TheTerrainLogic->getExtent(&gridExtent);
		}
		const Bool haveGrid = options.grids && gridSize > 0 && gridExtent.width() > 0.0f && gridExtent.height() > 0.0f;
		const Real gridCellWidth = haveGrid ? gridExtent.width() / (Real)gridSize : 0.0f;
		const Real gridCellHeight = haveGrid ? gridExtent.height() / (Real)gridSize : 0.0f;
		if (full && haveGrid)
		{
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				memset(s_gridCells[i], 0, (size_t)(gridSize * gridSize) * sizeof(s_gridCells[i][0]));
			}
		}
		std::vector<SupplyRecord>& supply = supplyRecords();
		supply.clear();
		std::vector<TechRecord>& tech = techRecords();
		tech.clear();
		std::vector<HeroRecord>& heroes = heroRecords();
		for (size_t h = 0; h < heroes.size(); ++h)
		{
			heroes[h].seen = FALSE;
		}

		// One entry per non-observer slot player, defeated and departed ones included.
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			Player* player = playerFromSlot(slot);
			if (player == NULL || player->isPlayerObserver())
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
			p->set_state(hasPlayerLeft(player) ? statsexporter::PLAYER_STATE_LEFT
				: (isPlayerDefeated(player) ? statsexporter::PLAYER_STATE_DEFEATED : statsexporter::PLAYER_STATE_ACTIVE));
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

			// Deposits over the last 60 seconds, kept by Money whether or not a HUD shows it
			p->set_money_per_minute(player->getMoney()->getCashPerMinute());
			p->set_rank_level(player->getRankLevel());
			p->set_skill_points(player->getSciencePurchasePoints());

			if (full)
			{
				const ScienceVec& sciences = player->getSciences();
				for (size_t sc = 0; sc < sciences.size() && sc < kMaxSciences; ++sc)
				{
					p->add_sciences(scienceName(sciences[sc]));
				}

				size_t upgradeCount = 0;
				for (UpgradeTemplate* upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade != NULL && upgradeCount < kMaxUpgrades; upgrade = upgrade->friend_getNext())
				{
					if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER && player->hasUpgradeComplete(upgrade))
					{
						p->add_upgrades(upgrade->getUpgradeName().str());
						++upgradeCount;
					}
				}
			}

			p->set_radar((statsexporter::RadarState)player->getRadarStateForStats());
			const Int battlePlan = player->getActiveBattlePlan();
			p->set_battle_plan((statsexporter::BattlePlan)battlePlan);
			if (full)
			{
				// deploying and packing are refined from the Strategy Centers in the object walk
				p->set_battle_plan_transition(battlePlan != PLANSTATUS_NONE ? statsexporter::BATTLE_PLAN_ACTIVE : statsexporter::BATTLE_PLAN_IDLE);
			}
			const UnsignedInt sabotagedUntil = player->getEnergy()->getPowerSabotagedTillFrame();
			if (sabotagedUntil > now)
			{
				p->set_power_sabotaged(true);
				p->set_power_sabotage_frames_left(sabotagedUntil - now);
			}
			fillNetworkStats(p, slot, player);

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

		// Campaign and solo games have no lobby slots to map players to, so they get no per-player snapshots.
		if (snapshot.players_size() == 0)
		{
			return FALSE;
		}

		if (full)
		{
			for (Object* obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject())
			{
				// Supply sources are neutral, so they are read before the per-player filtering. Boxes are read the way the HUD and the AI read them.
				if (options.supply && obj->isKindOf(KINDOF_SUPPLY_SOURCE) && obj->getTemplate() != NULL)
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
				if (options.tech && obj->getTemplate() != NULL && isTechStructure(obj))
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
					continue;	// e.g. an observer's stray objects
				}

				// Effectively dead objects (destroyed, waiting for removal) are not live: they count nowhere below.
				// Objects still under construction are live: they count in army value, class and template counts, but not in readiness.
				if (obj->isEffectivelyDead())
				{
					continue;
				}

				if (obj->isKindOf(KINDOF_FS_STRATEGY_CENTER))
				{
					static const NameKeyType battlePlanKey = TheNameKeyGenerator->nameToKey("BattlePlanUpdate");
					BattlePlanUpdate* battlePlanUpdate = (BattlePlanUpdate*)obj->findUpdateModule(battlePlanKey);
					if (battlePlanUpdate != NULL)
					{
						const TransitionStatus transition = battlePlanUpdate->getTransitionStatus();
						if (transition == TRANSITIONSTATUS_UNPACKING)
						{
							accumulator->set_battle_plan_transition(statsexporter::BATTLE_PLAN_DEPLOYING);
						}
						else if (transition == TRANSITIONSTATUS_PACKING && accumulator->battle_plan_transition() != statsexporter::BATTLE_PLAN_DEPLOYING)
						{
							accumulator->set_battle_plan_transition(statsexporter::BATTLE_PLAN_PACKING);
						}
					}
				}

				// Uses the same classification and cost source as kills and army_value.
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

				{
					// The idle test is the one Player uses for the idle worker button.
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

					if (haveGrid && (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_VEHICLE) || obj->isKindOf(KINDOF_AIRCRAFT)))
					{
						const Coord3D* position = obj->getPosition();
						Int cellX = (Int)((position->x - gridExtent.lo.x) / gridCellWidth);
						Int cellY = (Int)((position->y - gridExtent.lo.y) / gridCellHeight);
						cellX = (cellX < 0) ? 0 : ((cellX >= gridSize) ? gridSize - 1 : cellX);
						cellY = (cellY < 0) ? 0 : ((cellY >= gridSize) ? gridSize - 1 : cellY);
						++s_gridCells[accumulatorIndex][cellY * gridSize + cellX];
					}
				}

				accumulator->set_army_value(accumulator->army_value() + tmpl->friend_getBuildCost());
				if (isSuperweaponStructureTemplate(tmpl))
				{
					accumulator->set_live_superweapon_structure_count(accumulator->live_superweapon_structure_count() + 1);
				}

				{
					statsexporter::TemplateCount* found = templateCount(accumulator, controller->getPlayerIndex(), tmpl);
					found->set_count(found->count() + 1);
				}

				// Readiness is not deduped by power name.
				for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
				{
					SpecialPowerModuleInterface* spmi = (*m)->getSpecialPower();
					if (spmi == NULL)
					{
						continue;
					}
					// The module exists whether or not the science is unlocked, so skip powers the player cannot fire.
					// A power with no required science is always available (same test as InGameUI).
					const ScienceType requiredScience = spmi->getRequiredScience();
					if (requiredScience != SCIENCE_INVALID && !controller->hasScience(requiredScience))
					{
						continue;
					}
					// A structure still being built cannot fire. It is listed once construction completes.
					if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
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

				if (options.production && accumulator->production_size() < kMaxProductionStructures)
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

			// Heroes not seen alive this sample are listed dead. The record keeps them for the rest of the match.
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
					grid->set_width((uint32_t)gridSize);
					grid->set_height((uint32_t)gridSize);
					grid->set_cell_width(gridCellWidth);
					grid->set_cell_height(gridCellHeight);
					for (Int cell = 0; cell < gridSize * gridSize; ++cell)
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
		}

		// The selection of this client: client-side state, read only. The camera has its own frame.
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

		// Local client only: the logic frames achieved per wall-clock second since the last sample (the display FPS is set above).
		if (options.localLogicFps > 0.0f)
		{
			snapshot.set_local_logic_fps(options.localLogicFps);
		}
		snapshot.set_stalled_frames((s_stallStartMs != 0) ? (UnsignedInt)((GetTickCount64() - s_stallStartMs) * LOGICFRAMES_PER_SECOND / 1000) : 0);

		if (full)
		{
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
		}


		return TRUE;
	}
}

void StatsExporterLiveUpdate()
{
	static UnsignedInt s_lastLightFrame[STATS_EXPORTER_MAX_READERS];
	static Bool s_hasLight[STATS_EXPORTER_MAX_READERS];
	static UnsignedInt s_lastFullFrame[STATS_EXPORTER_MAX_READERS];
	static Bool s_hasFull[STATS_EXPORTER_MAX_READERS];
	static UnsignedInt s_fullEpoch[STATS_EXPORTER_MAX_READERS];
	static UnsignedInt s_previousFrame = 0;
	static ULONGLONG s_previousMs = 0;
	static Real s_localLogicFps = 0.0f;

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
		memset(s_hasLight, 0, sizeof(s_hasLight));	// the next match starts again at frame 0
		memset(s_hasFull, 0, sizeof(s_hasFull));
		return;
	}

	sink->takeResyncRequests();
	const Readers readers = readyReaders(sink);
	if (readers.empty())
	{
		pendingEvents().clear();	// nothing is kept for a reader that is gone
	}

	// UiState goes first to a new reader, ahead of MatchInfo.
	syncUiState(sink);

	// MatchInfo STARTED for every reader that has not got it, before any snapshot of that reader.
	if (s_matchActive)
	{
		const Readers fresh = readers.minus(s_matchInfoSent);
		if (!fresh.empty())
		{
			// A reader that connected during the load gets LOADING now, ahead of STARTED.
			const Readers loadingTargets = fresh.minus(s_loadingSent);
			if (s_hasLoadingBody && !loadingTargets.empty())
			{
				sink->enqueueFrame(STATS_EXPORTER_FRAME_MATCH_INFO, (const UnsignedByte*)s_loadingBody.data(), (UnsignedInt)s_loadingBody.size(), loadingTargets);
				s_loadingSent.merge(loadingTargets);
			}

			statsexporter::MatchInfo& info = matchInfoMessage();
			fillMatchInfo(info, statsexporter::MATCH_PHASE_STARTED, 0, AsciiString::TheEmptyString, FALSE, FALSE);
			sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_INFO, info, fresh);
			s_matchInfoSent.merge(fresh);
		}

		// LOADING is only for readers that connect while the match is loading, and STARTED has been handled now.
		s_hasLoadingBody = FALSE;
	}

	// The Generals Online match id can be assigned after the match started. Once it appears, STARTED is resent once with it.
	if (s_matchActive && s_hasOnlineMatch && onlineMatchMessage().match_id() == 0)
	{
		const uint64_t onlineMatchId = currentOnlineMatchId();
		if (onlineMatchId != 0)
		{
			onlineMatchMessage().set_match_id(onlineMatchId);
			s_lastExportedOnlineMatchId = onlineMatchId;
			const Readers resend = startedReaders(readers);
			if (!resend.empty())
			{
				statsexporter::MatchInfo& info = matchInfoMessage();
				fillMatchInfo(info, statsexporter::MATCH_PHASE_STARTED, 0, AsciiString::TheEmptyString, FALSE, FALSE);
				sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_INFO, info, resend);
			}
		}
	}

	// Static player data lives only in MatchInfo, so the snapshots of a reader wait until its STARTED has been queued.
	const Readers started = startedReaders(readers);
	if (started.empty())
	{
		return;
	}

	const UnsignedInt now = TheGameLogic->getFrame();

	// Events leave from StatsExporterLiveFrameEnd(), in the frame they happened in. This sample is taken before the frame
	// commands and updates run, so it shows the state at the end of the previous frame. Snapshot frame N sends Events up to N-1.

	// Every reader has its own cadence (FeedConfig): light and full run on their own clocks, and a sample that is due for both is one
	// full snapshot. Readers that share the same full-snapshot content share one build.
	Readers lightGroup;
	Readers fullReaders[STATS_EXPORTER_MAX_READERS];
	StatsExporterFeedConfig fullConfigs[STATS_EXPORTER_MAX_READERS];
	Int fullGroups = 0;
	for (Int slot = 0; slot < STATS_EXPORTER_MAX_READERS; ++slot)
	{
		if (!started.has(slot))
		{
			continue;
		}

		// A new match restarts the frame counter below the last sample frame.
		if ((s_hasLight[slot] && now < s_lastLightFrame[slot]) || (s_hasFull[slot] && now < s_lastFullFrame[slot]))
		{
			s_hasLight[slot] = FALSE;
			s_hasFull[slot] = FALSE;
		}

		StatsExporterFeedConfig config;
		sink->getFeedConfig(slot, config);
		const UnsignedInt epoch = started.epoch(slot);
		const Bool lightDue = config.lightIntervalMs > 0 && (!s_hasLight[slot] || s_lastLightFrame[slot] + intervalFrames(config.lightIntervalMs) <= now);
		// The first snapshot of every connection (and after every resync) is a full one.
		const Bool fullDue = config.fullIntervalMs > 0 && (!s_hasFull[slot] || s_fullEpoch[slot] != epoch || s_lastFullFrame[slot] + intervalFrames(config.fullIntervalMs) <= now);
		if (lightDue)
		{
			s_lastLightFrame[slot] = now;
			s_hasLight[slot] = TRUE;
		}
		if (fullDue)
		{
			s_lastFullFrame[slot] = now;
			s_hasFull[slot] = TRUE;
			s_fullEpoch[slot] = epoch;

			Int group = 0;
			while (group < fullGroups && fullConfigs[group].fullSignature() != config.fullSignature())
			{
				++group;
			}
			if (group == fullGroups)
			{
				fullConfigs[group] = config;
				++fullGroups;
			}
			fullReaders[group].add(slot, epoch);
		}
		else if (lightDue)
		{
			lightGroup.add(slot, epoch);
		}
	}
	if (fullGroups == 0 && lightGroup.empty())
	{
		return;
	}

	// Local client only: the logic frames achieved per wall-clock second, over at least 250 ms (the tick count is too coarse for per-frame samples).
	SnapshotOptions options;
	const ULONGLONG nowMs = GetTickCount64();
	if (s_previousMs == 0 || now < s_previousFrame)
	{
		s_previousFrame = now;
		s_previousMs = nowMs;
		s_localLogicFps = 0.0f;
	}
	else if (now > s_previousFrame && nowMs >= s_previousMs + 250)
	{
		s_localLogicFps = (Real)(now - s_previousFrame) * 1000.0f / (Real)(nowMs - s_previousMs);
		s_previousFrame = now;
		s_previousMs = nowMs;
	}
	options.localLogicFps = s_localLogicFps;

	for (Int group = 0; group < fullGroups; ++group)
	{
		options.grids = fullConfigs[group].grids;
		options.supply = fullConfigs[group].supply;
		options.tech = fullConfigs[group].tech;
		options.production = fullConfigs[group].production;
		options.gridResolution = fullConfigs[group].gridResolution;
		if (buildSnapshot(now, TRUE, options))
		{
			sendMessage(sink, STATS_EXPORTER_FRAME_STATS_SNAPSHOT, snapshotMessage(), fullReaders[group]);
		}
	}
	if (!lightGroup.empty())
	{
		if (buildSnapshot(now, FALSE, options))
		{
			sendMessage(sink, STATS_EXPORTER_FRAME_STATS_SNAPSHOT, snapshotMessage(), lightGroup);
		}
	}
}

void StatsExporterLiveFrameEnd()
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || TheGameLogic == NULL || !TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	const Readers started = startedReaders(readyReaders(sink));
	if (!started.empty())
	{
		flushPendingEvents(sink, started);
	}
}

void StatsExporterLiveMatchSummary()
{
	if (TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}

	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	StatsExporterSink* sink = StatsExporterPipeGetSink();
	const Readers readers = (sink != NULL) ? readyReaders(sink) : Readers();
	if (readers.empty())
	{
		return;
	}

	statsexporter::MatchSummary summary;
	summary.set_frame(TheGameLogic->getFrame());
	if (TheWritableGlobalData != NULL)
	{
		summary.set_map_name(exportedMapName(TheWritableGlobalData->m_mapName.str(), NULL));
	}

	// A decisive result is one the victory conditions of this client have reached (the same test as MatchInfo.completed),
	// is_winner is then true for the players of the victorious alliance, and always false without one.
	const Bool decisive = (TheVictoryConditions != NULL)
		&& (TheVictoryConditions->isLocalAlliedVictory() || TheVictoryConditions->isLocalAlliedDefeat());

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

		const Bool isWinner = decisive && !player->isPlayerObserver() && TheVictoryConditions->hasAchievedVictory(player);

		statsexporter::MatchSummaryPlayer* p = summary.add_players();
		p->set_lobby_slot(slot);
		p->set_player_index(player->getPlayerIndex());
		p->set_player_name(toUtf8(player->getPlayerDisplayName()));
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

	sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_SUMMARY, summary, readers);
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
	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}

	// LOADING already fixed the identity of this match, so STARTED reuses it.
	if (!s_loadingPrepared)
	{
		beginMatchState(TheGameInfo);
	}
	s_loadingPrepared = FALSE;
	// No reader has STARTED for this match yet, so the next tick sends it through the resend path.
	s_matchInfoSent.clear();
}

void StatsExporterLiveGameDataCleared()
{
	StatsExporterControlReset();
	if (!s_matchActive || s_matchEndedSent)
	{
		return;
	}

	// A replay that is left before its end never reaches the hook of the recorder, so the replay path is taken here.
	AsciiString replayPath;
	Bool replayIsPlayback = FALSE;
	if (TheRecorder != NULL && TheRecorder->isPlaybackMode())
	{
		replayPath = RecorderClass::getReplayDir();
		replayPath.concat(TheRecorder->getCurrentReplayFilename());
		replayIsPlayback = TRUE;
	}
	StatsExporterLiveMatchEnded(replayPath, replayIsPlayback);
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

	const Bool completed = (TheVictoryConditions != NULL)
		&& (TheVictoryConditions->isLocalAlliedVictory() || TheVictoryConditions->isLocalAlliedDefeat());

	// ENDED is a true one-shot: sent only if a reader is connected right now.
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	const Readers readers = (sink != NULL) ? readyReaders(sink) : Readers();
	if (!readers.empty())
	{
		// the events of the last frame go out ahead of ENDED
		const Readers started = startedReaders(readers);
		if (!started.empty())
		{
			flushPendingEvents(sink, started);
		}

		statsexporter::MatchInfo& info = matchInfoMessage();
		fillMatchInfo(info, statsexporter::MATCH_PHASE_ENDED, TheGameLogic->getFrame() - s_matchStartFrame, replayPath, replayIsPlayback, completed);
		sendMessage(sink, STATS_EXPORTER_FRAME_MATCH_INFO, info, readers);
	}
	pendingEvents().clear();

	s_matchActive = FALSE;
	s_hasLoadingBody = FALSE;
}

namespace
{
	// Largest ChatSend text taken from the pipe. The limits of the chat boxes apply on top.
	const size_t kMaxChatSendBytes = 1024;

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

	void sendChatMessage(StatsExporterSink* sink, const statsexporter::ChatMessage& message, const Readers& readers)
	{
		const Readers recipients = feedFilter(sink, readers, &StatsExporterFeedConfig::chat);
		if (recipients.empty())
		{
			return;
		}

		// May run off the main thread, so it keeps its own buffer instead of the shared one.
		std::string body;
		if (message.SerializeToString(&body) && !body.empty() && body.size() <= STATS_EXPORTER_MAX_BODY_BYTES)
		{
			sink->enqueueFrame(STATS_EXPORTER_FRAME_CHAT_MESSAGE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), recipients);
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

	// Everything the screen is derived from, read without building a string.
	struct UiInputs
	{
		const char* layout;
		Bool loading;
		Bool interactive;
		Bool shellActive;
		Bool quitMenu;
		Bool paused;
		Int gameMode;
	};

	struct UiInputsCopy
	{
		std::string layout;
		Bool loading;
		Bool interactive;
		Bool shellActive;
		Bool quitMenu;
		Bool paused;
		Int gameMode;
	};

	UiInputs readUiInputs()
	{
		UiInputs in;
		in.layout = (TheShell != NULL && TheShell->top() != NULL) ? TheShell->top()->getFilename().str() : "";
		in.shellActive = (TheShell != NULL) && TheShell->isShellActive();
		in.loading = (TheGameLogic != NULL) && (TheGameLogic->isLoadingMap() || TheGameLogic->isLoadingSave());
		in.interactive = (TheGameLogic != NULL) && TheGameLogic->isInInteractiveGame();
		in.quitMenu = (TheInGameUI != NULL) && TheInGameUI->isQuitMenuVisible();
		in.paused = (TheGameLogic != NULL) && TheGameLogic->isGamePaused();
		in.gameMode = (TheGameLogic != NULL) ? (Int)TheGameLogic->getGameMode() : 0;
		return in;
	}

	Bool uiInputsSame(const UiInputs& in, const UiInputsCopy& copy)
	{
		return in.loading == copy.loading && in.interactive == copy.interactive && in.shellActive == copy.shellActive
			&& in.quitMenu == copy.quitMenu && in.paused == copy.paused && in.gameMode == copy.gameMode
			&& strcmp(in.layout, copy.layout.c_str()) == 0;
	}

	void copyUiInputs(const UiInputs& in, UiInputsCopy& copy)
	{
		copy.layout = in.layout;
		copy.loading = in.loading;
		copy.interactive = in.interactive;
		copy.shellActive = in.shellActive;
		copy.quitMenu = in.quitMenu;
		copy.paused = in.paused;
		copy.gameMode = in.gameMode;
	}

	UiSnapshot buildUiState(const UiInputs& in)
	{
		UiSnapshot state;
		state.screen = statsexporter::UI_SCREEN_UNKNOWN;
		state.gameMode = statsexporter::MATCH_MODE_UNKNOWN;
		state.paused = false;
		state.layout = in.layout;

		if (in.loading)
		{
			state.screen = statsexporter::UI_SCREEN_LOADING;
		}
		else if (in.interactive && !in.shellActive)
		{
			state.screen = in.quitMenu ? statsexporter::UI_SCREEN_IN_GAME_MENU : statsexporter::UI_SCREEN_IN_GAME;
			state.gameMode = mapMatchMode();
			state.paused = in.paused;
		}
		else
		{
			state.screen = screenForLayout(state.layout);
		}
		return state;
	}

	UiSnapshot currentUiState()
	{
		return buildUiState(readUiInputs());
	}

	// Sends the current screen to every reader when it changed, and once to every new reader (by connection epoch).
	void syncUiState(StatsExporterSink* sink)
	{
		static Bool s_hasSent = FALSE;
		static Readers s_sentReaders;
		static UiSnapshot s_sent;
		static UiInputsCopy s_inputs;

		const Readers readers = readyReaders(sink);
		if (readers.empty())
		{
			return;
		}

		// Called every render frame while connected: unchanged inputs are detected without building any string.
		const UiInputs inputs = readUiInputs();
		const Readers fresh = readers.minus(s_sentReaders);
		if (s_hasSent && fresh.empty() && uiInputsSame(inputs, s_inputs))
		{
			return;
		}

		const UiSnapshot state = buildUiState(inputs);
		copyUiInputs(inputs, s_inputs);
		const Bool unchanged = s_hasSent && s_sent.screen == state.screen && s_sent.layout == state.layout
			&& s_sent.gameMode == state.gameMode && s_sent.paused == state.paused;
		if (unchanged && fresh.empty())
		{
			return;
		}
		const Readers targets = unchanged ? fresh : readers;

		statsexporter::UiState message;
		message.set_screen(state.screen);
		message.set_layout(state.layout);
		message.set_game_mode(state.gameMode);
		message.set_paused(state.paused);
		std::string body;
		if (message.SerializeToString(&body))
		{
			if (body.empty())
			{
				body.push_back(0x08);	// field 1 (screen) = UNKNOWN written out so the frame is never empty
				body.push_back(0x00);
			}
			sink->enqueueFrame(STATS_EXPORTER_FRAME_UI_STATE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), targets);
		}
		s_hasSent = TRUE;
		s_sentReaders = readers;
		s_sent = state;
	}

	// Lobby prediction: a mirror of the random resolution of the engine, run on a private generator state.
	// It follows checkForDuplicateColors, populateRandomSideAndColor (MORE_RANDOM) and populateRandomStartPosition in
	// GameLogic.cpp line by line, on a plain copy of the slot fields, so it cannot touch the generators or slots of the game.
	// The engine takes no logic draws between seeding with the lobby seed and these calls, and the LOADING self-check below
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

		// The observer camera starts on the start position of the local observer slot, as at LOADING.
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
			// The normalised path, as in MatchInfo.map_name. The display name of the lobby is a separate field
			out.set_map_name(exportedMapName(entry->map_path.c_str(), NULL));
			out.set_map_display_name(entry->map_name);
			fillOnlineMatch(*out.mutable_online(), *entry);
			out.set_host_user_id(entry->owner);
		}
		else
		{
			out.set_map_name(exportedMapName(game->getMap().str(), NULL));
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
			// Packed ARGB like MatchPlayer.color, left unset (0) while the colour is still Random
			const Int slotColor = slot->getColor();
			if (TheMultiplayerSettings != NULL && slotColor >= 0 && slotColor < TheMultiplayerSettings->getNumColors())
			{
				p->set_color((uint32_t)TheMultiplayerSettings->getColor(slotColor)->getColor());
			}
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

	// Sends the lobby to every reader when its serialized bytes changed (a few checks per second), and once to every new reader.
	void syncLobbyState(StatsExporterSink* sink)
	{
		static Bool s_wasInLobby = FALSE;
		static Readers s_sentReaders;
		static std::string s_lastBody;
		static ULONGLONG s_lastCheckMs = 0;

		const Readers readers = readyReaders(sink);
		if (readers.empty())
		{
			return;
		}

		const ULONGLONG nowMs = GetTickCount64();
		const Readers fresh = readers.minus(s_sentReaders);
		if (fresh.empty() && nowMs - s_lastCheckMs < 250)
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
		s_sentReaders = readers;
		if (!serialized)
		{
			return;
		}

		Readers targets = readers;
		if (!inLobby)
		{
			// Left the lobby: one explicit marker, and nothing at all to a reader that was never in one.
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
			if (body == s_lastBody)
			{
				targets = fresh;
				if (targets.empty())
				{
					return;
				}
			}
		}

		s_lastBody = body;
		if (body.empty())
		{
			body.push_back(0x08);	// field 1 (in_lobby) = false written out so the frame is never empty
			body.push_back(0x00);
		}
		sink->enqueueFrame(STATS_EXPORTER_FRAME_LOBBY_STATE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), targets);
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
		// The line is handled as if typed into the chat box of that channel, slash commands included.
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

namespace
{
	statsexporter::ViewStats& viewStatsMessage()
	{
		static statsexporter::ViewStats* s_message = NULL;
		return arenaMessage(s_message);
	}

	statsexporter::InterestMap& interestMessage()
	{
		static statsexporter::InterestMap* s_message = NULL;
		return arenaMessage(s_message);
	}

	struct NotableRecord
	{
		ObjectID id;
		const ThingTemplate* tmpl;
		const ThingTemplate* addon;
		Real health;
		Int veterancy;
		UnsignedInt cost;
		Bool hero;
		Real x, y;
	};

	Bool notableBefore(const NotableRecord& a, const NotableRecord& b)
	{
		if (a.hero != b.hero)
		{
			return a.hero != FALSE;
		}
		return a.cost > b.cost;
	}

	// The portable structure a unit carries (a gattling cannon, propaganda tower or bunker on an Overlord), NULL for none.
	const ThingTemplate* carriedAddon(const Object* obj)
	{
		ContainModuleInterface* contain = obj->getContain();
		const ContainedItemsList* items = (contain != NULL) ? contain->getContainedItemsList() : NULL;
		if (items == NULL)
		{
			return NULL;
		}
		for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
		{
			if (*it != NULL && (*it)->isKindOf(KINDOF_PORTABLE_STRUCTURE))
			{
				return (*it)->getTemplate();
			}
		}
		return NULL;
	}

	// Everything the local camera sees, per player. Reads only.
	void buildViewStats(UnsignedInt now, const StatsExporterFeedConfig& config)
	{
		statsexporter::ViewStats& message = viewStatsMessage();
		message.Clear();
		message.set_frame(now);
		message.set_has_view(s_view.valid != FALSE);
		message.set_trade_window_ms(config.tradeWindowMs);
		message.set_intensity_window_ms(config.intensityWindowMs);

		statsexporter::PlayerViewStats* players[kMaxCounterPlayers];
		std::vector<NotableRecord> notable[kMaxCounterPlayers];
		for (Int i = 0; i < kMaxCounterPlayers; ++i)
		{
			players[i] = NULL;
		}
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			Player* player = playerFromSlot(slot);
			if (player == NULL || player->isPlayerObserver() || player->getScoreKeeper() == NULL)
			{
				continue;
			}
			const Int index = player->getPlayerIndex();
			if (index >= 0 && index < kMaxCounterPlayers)
			{
				players[index] = message.add_players();
				players[index]->set_player_index(index);
			}
		}

		for (Object* obj = s_view.valid ? TheGameLogic->getFirstObject() : NULL; obj != NULL; obj = obj->getNextObject())
		{
			// The view test first: most objects are off screen.
			const Coord3D* position = obj->getPosition();
			if (!pointInView(position->x, position->y))
			{
				continue;
			}
			const ThingTemplate* tmpl = obj->getTemplate();
			const Player* controller = obj->getControllingPlayer();
			if (tmpl == NULL || controller == NULL || obj->isEffectivelyDead())
			{
				continue;
			}
			const Int index = controller->getPlayerIndex();
			const Int armyClass = killCategory(tmpl);
			if (armyClass < 0 || index < 0 || index >= kMaxCounterPlayers || players[index] == NULL)
			{
				continue;
			}

			statsexporter::PlayerViewStats* p = players[index];
			const UnsignedInt cost = (UnsignedInt)tmpl->friend_getBuildCost();
			Real health = 1.0f;
			const BodyModuleInterface* body = obj->getBodyModule();
			if (body != NULL && body->getMaxHealth() > 0.0f)
			{
				health = body->getHealth() / body->getMaxHealth();
				health = (health < 0.0f) ? 0.0f : ((health > 1.0f) ? 1.0f : health);
			}
			p->set_value_in_view(p->value_in_view() + cost);
			p->set_effective_value(p->effective_value() + (UnsignedInt)((Real)cost * health + 0.5f));

			statsexporter::CategoryValue* value = p->mutable_value_by_class();
			statsexporter::CategoryCounts* count = p->mutable_count_by_class();
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

			const Int veterancy = (Int)obj->getVeterancyLevel();
			if (veterancy == 1)
			{
				p->set_veteran(p->veteran() + 1);
			}
			else if (veterancy == 2)
			{
				p->set_elite(p->elite() + 1);
			}
			else if (veterancy >= 3)
			{
				p->set_heroic(p->heroic() + 1);
			}

			if (armyClass == 3)
			{
				if (isSuperweaponStructureTemplate(tmpl) && !obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
					{
						SpecialPowerModuleInterface* spmi = (*m)->getSpecialPower();
						if (spmi == NULL)
						{
							continue;
						}
						statsexporter::ViewSuperweapon* weapon = p->add_superweapons();
						weapon->set_object_id(obj->getID());
						weapon->set_template_name(tmpl->getName().str());
						weapon->set_power_name(spmi->getPowerName().str());
						weapon->set_ready_frame(spmi->getReadyFrame());
						weapon->set_percent_ready(spmi->getPercentReady());
						weapon->set_state(spmi->getPercentReady() >= 1.0f ? statsexporter::SUPERWEAPON_READY : statsexporter::SUPERWEAPON_CHARGING);
						weapon->set_x(position->x);
						weapon->set_y(position->y);
						break;
					}
				}
				continue;
			}

			const Bool hero = obj->isKindOf(KINDOF_HERO);
			if (hero || (config.notableCost > 0 && cost >= config.notableCost))
			{
				NotableRecord record;
				record.id = obj->getID();
				record.tmpl = tmpl;
				record.addon = carriedAddon(obj);
				record.health = health;
				record.veterancy = veterancy;
				record.cost = cost;
				record.hero = hero;
				record.x = position->x;
				record.y = position->y;
				notable[index].push_back(record);
			}
		}

		UnsignedInt killed[kMaxCounterPlayers];
		UnsignedInt lost[kMaxCounterPlayers];
		Real damage[kMaxCounterPlayers];
		memset(killed, 0, sizeof(killed));
		memset(lost, 0, sizeof(lost));
		memset(damage, 0, sizeof(damage));
		windowSums(now, config.tradeWindowMs, killed, lost, NULL);
		windowSums(now, config.intensityWindowMs, NULL, NULL, damage);
		for (Int index = 0; index < kMaxCounterPlayers; ++index)
		{
			statsexporter::PlayerViewStats* p = players[index];
			if (p == NULL)
			{
				continue;
			}
			p->set_value_killed(killed[index]);
			p->set_value_lost(lost[index]);
			p->set_damage_per_second(damage[index] * 1000.0f / (Real)config.intensityWindowMs);

			std::sort(notable[index].begin(), notable[index].end(), notableBefore);
			for (size_t n = 0; n < notable[index].size() && n < kMaxNotablePerPlayer; ++n)
			{
				const NotableRecord& record = notable[index][n];
				statsexporter::NotableUnit* unit = p->add_notable();
				unit->set_object_id(record.id);
				unit->set_template_name(templateName(record.tmpl));
				unit->set_health(record.health);
				unit->set_veterancy(record.veterancy);
				unit->set_cost(record.cost);
				unit->set_is_hero(record.hero != FALSE);
				unit->set_addon(templateName(record.addon));
				unit->set_x(record.x);
				unit->set_y(record.y);
			}
		}
	}

	Real s_cellValue[kMaxCounterPlayers][kInterestGrid * kInterestGrid];
	Real s_interestTotal[kInterestGrid * kInterestGrid];

	// The interest score of every cell at the finest resolution: the decayed events plus the armies of enemies standing close together.
	// FALSE when the playable extent is not known.
	Bool computeInterest(UnsignedInt now)
	{
		if (!ensureInterestExtent())
		{
			return FALSE;
		}
		decayInterest(now);
		memset(s_cellValue, 0, sizeof(s_cellValue));

		Bool present[kMaxCounterPlayers];
		for (Int i = 0; i < kMaxCounterPlayers; ++i)
		{
			present[i] = FALSE;
		}
		for (Object* obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject())
		{
			const ThingTemplate* tmpl = obj->getTemplate();
			const Player* controller = obj->getControllingPlayer();
			const Int armyClass = killCategory(tmpl);
			if (armyClass < 0 || controller == NULL || obj->isEffectivelyDead())
			{
				continue;
			}
			const Int index = controller->getPlayerIndex();
			Int cell = 0;
			if (index < 0 || index >= kMaxCounterPlayers || controller->isPlayerObserver() || controller == ThePlayerList->getNeutralPlayer()
				|| !interestCellOf(s_interestExtent, obj->getPosition()->x, obj->getPosition()->y, &cell))
			{
				continue;
			}
			s_cellValue[index][cell] += (Real)tmpl->friend_getBuildCost() * (armyClass == 3 ? kInterestStructureWeight : 1.0f);
			present[index] = TRUE;
		}

		Bool enemies[kMaxCounterPlayers][kMaxCounterPlayers];
		memset(enemies, 0, sizeof(enemies));
		for (Int a = 0; a < ThePlayerList->getPlayerCount(); ++a)
		{
			const Player* first = ThePlayerList->getNthPlayer(a);
			for (Int b = 0; b < ThePlayerList->getPlayerCount(); ++b)
			{
				const Player* second = ThePlayerList->getNthPlayer(b);
				if (first != NULL && second != NULL && first != second && first->getPlayerIndex() < kMaxCounterPlayers && second->getPlayerIndex() < kMaxCounterPlayers
					&& present[first->getPlayerIndex()] && present[second->getPlayerIndex()] && first->getRelationship(second->getDefaultTeam()) == ENEMIES)
				{
					enemies[first->getPlayerIndex()][second->getPlayerIndex()] = TRUE;
				}
			}
		}

		for (Int cellY = 0; cellY < kInterestGrid; ++cellY)
		{
			for (Int cellX = 0; cellX < kInterestGrid; ++cellX)
			{
				const Int cell = cellY * kInterestGrid + cellX;
				Int nearby[kMaxCounterPlayers];
				Real nearbyValue[kMaxCounterPlayers];
				Int nearbyCount = 0;
				for (Int p = 0; p < kMaxCounterPlayers; ++p)
				{
					if (!present[p])
					{
						continue;
					}
					Real sum = 0.0f;
					for (Int dy = -1; dy <= 1; ++dy)
					{
						for (Int dx = -1; dx <= 1; ++dx)
						{
							const Int nx = cellX + dx;
							const Int ny = cellY + dy;
							if (nx >= 0 && nx < kInterestGrid && ny >= 0 && ny < kInterestGrid)
							{
								sum += s_cellValue[p][ny * kInterestGrid + nx];
							}
						}
					}
					if (sum > 0.0f)
					{
						nearby[nearbyCount] = p;
						nearbyValue[nearbyCount] = sum;
						++nearbyCount;
					}
				}

				Real contested = 0.0f;
				for (Int a = 0; a < nearbyCount; ++a)
				{
					for (Int b = a + 1; b < nearbyCount; ++b)
					{
						if (enemies[nearby[a]][nearby[b]])
						{
							contested += (nearbyValue[a] < nearbyValue[b]) ? nearbyValue[a] : nearbyValue[b];
						}
					}
				}
				s_interestTotal[cell] = s_interestEvents[cell] + kInterestContestWeight * contested;
			}
		}
		return TRUE;
	}

	void fillInterestMessage(UnsignedInt now, UnsignedInt resolution)
	{
		std::vector<Highlight>& list = highlights();
		for (size_t i = 0; i < list.size();)
		{
			if (list[i].expireFrame <= now)
			{
				list.erase(list.begin() + i);
			}
			else
			{
				++i;
			}
		}

		statsexporter::InterestMap& message = interestMessage();
		message.Clear();
		message.set_frame(now);
		message.set_width(resolution);
		message.set_height(resolution);
		message.set_cell_width(s_interestExtent.width() / (Real)resolution);
		message.set_cell_height(s_interestExtent.height() / (Real)resolution);
		message.set_origin_x(s_interestExtent.lo.x);
		message.set_origin_y(s_interestExtent.lo.y);
		message.set_half_life_ms(kInterestHalfLifeMs);
		const Int factor = kInterestGrid / (Int)resolution;
		for (Int cellY = 0; cellY < (Int)resolution; ++cellY)
		{
			for (Int cellX = 0; cellX < (Int)resolution; ++cellX)
			{
				Real sum = 0.0f;
				for (Int dy = 0; dy < factor; ++dy)
				{
					for (Int dx = 0; dx < factor; ++dx)
					{
						sum += s_interestTotal[(cellY * factor + dy) * kInterestGrid + cellX * factor + dx];
					}
				}
				message.add_scores((uint32_t)(sum + 0.5f));
			}
		}
		for (size_t i = 0; i < list.size(); ++i)
		{
			const Highlight& source = list[i];
			statsexporter::InterestHighlight* h = message.add_highlights();
			h->set_id(source.id);
			h->set_kind((statsexporter::HighlightKind)source.kind);
			h->set_player_index(source.playerIndex);
			h->set_template_name(source.templateName);
			h->set_power_name(source.powerName);
			h->set_x(source.x);
			h->set_y(source.y);
			h->set_start_frame(source.startFrame);
			h->set_impact_frame(source.impactFrame);
			h->set_impact_estimated(source.estimated != FALSE);
			h->set_expire_frame(source.expireFrame);
			h->set_value(source.value);
			h->set_object_id(source.objectId);
		}
	}

	// ViewStats and InterestMap, from the render loop: they describe the local view, which moves while the game is paused.
	// The cadence is wall-clock time. The rolling windows and the decay count game time.
	void emitViewFrames(StatsExporterSink* sink, const Readers& started, UnsignedInt now)
	{
		static ULONGLONG s_lastView[STATS_EXPORTER_MAX_READERS];
		static UnsignedInt s_viewEpoch[STATS_EXPORTER_MAX_READERS];
		static UnsignedInt s_viewSerial[STATS_EXPORTER_MAX_READERS];
		static ULONGLONG s_lastInterest[STATS_EXPORTER_MAX_READERS];
		static UnsignedInt s_interestEpoch[STATS_EXPORTER_MAX_READERS];
		static UnsignedInt s_interestSerial[STATS_EXPORTER_MAX_READERS];

		const ULONGLONG micros = StatsExporterNowMicros();
		StatsExporterFeedConfig configs[STATS_EXPORTER_MAX_READERS];
		Readers viewDue;
		Readers interestDue;
		for (Int slot = 0; slot < STATS_EXPORTER_MAX_READERS; ++slot)
		{
			if (!started.has(slot))
			{
				continue;
			}
			sink->getFeedConfig(slot, configs[slot]);
			const StatsExporterFeedConfig& config = configs[slot];
			const UnsignedInt epoch = started.epoch(slot);

			const Bool viewFresh = s_viewEpoch[slot] != epoch || s_viewSerial[slot] != s_matchSerial;
			if (config.viewStatsIntervalMs > 0 && (viewFresh || micros - s_lastView[slot] >= (ULONGLONG)config.viewStatsIntervalMs * 1000))
			{
				viewDue.add(slot, epoch);
				s_lastView[slot] = micros;
				s_viewEpoch[slot] = epoch;
				s_viewSerial[slot] = s_matchSerial;
			}

			const Bool interestFresh = s_interestEpoch[slot] != epoch || s_interestSerial[slot] != s_matchSerial;
			if (config.interest && (interestFresh || s_highlightsDirty || micros - s_lastInterest[slot] >= (ULONGLONG)config.interestIntervalMs * 1000))
			{
				interestDue.add(slot, epoch);
				s_lastInterest[slot] = micros;
				s_interestEpoch[slot] = epoch;
				s_interestSerial[slot] = s_matchSerial;
			}
		}

		// Readers with the same settings share one build.
		Readers remaining = viewDue;
		while (!remaining.empty())
		{
			Int first = 0;
			while (!remaining.has(first))
			{
				++first;
			}
			Readers group;
			for (Int slot = first; slot < STATS_EXPORTER_MAX_READERS; ++slot)
			{
				if (remaining.has(slot) && configs[slot].notableCost == configs[first].notableCost
					&& configs[slot].tradeWindowMs == configs[first].tradeWindowMs && configs[slot].intensityWindowMs == configs[first].intensityWindowMs)
				{
					group.add(slot, remaining.epoch(slot));
					remaining.remove(slot);
				}
			}
			buildViewStats(now, configs[first]);
			sendMessage(sink, STATS_EXPORTER_FRAME_VIEW_STATS, viewStatsMessage(), group);
		}

		if (!interestDue.empty() && computeInterest(now))
		{
			remaining = interestDue;
			while (!remaining.empty())
			{
				Int first = 0;
				while (!remaining.has(first))
				{
					++first;
				}
				Readers group;
				for (Int slot = first; slot < STATS_EXPORTER_MAX_READERS; ++slot)
				{
					if (remaining.has(slot) && configs[slot].gridResolution == configs[first].gridResolution)
					{
						group.add(slot, remaining.epoch(slot));
						remaining.remove(slot);
					}
				}
				fillInterestMessage(now, configs[first].gridResolution);
				sendMessage(sink, STATS_EXPORTER_FRAME_INTEREST_MAP, interestMessage(), group);
			}
		}
		s_highlightsDirty = FALSE;
	}
}

void StatsExporterLiveCamera()
{
	// Rendered frames are rate limited to about 120 per second, and only a changed camera is sent.
	static const ULONGLONG kMinIntervalMicros = 8333;
	static const Real kEpsilon = 0.01f;
	static Bool s_hasSent = FALSE;
	static Readers s_sentReaders;
	static UnsignedInt s_sentSerial = 0;
	static ULONGLONG s_lastMicros = 0;
	static Real s_last[14];
	static UnsignedInt s_lastExtra[4];

	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || TheTacticalView == NULL || TheGameLogic == NULL || TheRadar == NULL)
	{
		return;
	}
	if (!TheGameLogic->isInInteractiveGame())
	{
		s_view.valid = FALSE;
		return;
	}

	// Only during a match the reader has STARTED for: not the shell map camera before LOADING, nor anything after ENDED.
	const Readers started = startedReaders(readyReaders(sink));
	if (started.empty())
	{
		s_view.valid = FALSE;
		return;
	}

	// Pipe camera commands run their transitions here, ahead of the picture the frame describes.
	StatsExporterCameraReport report;
	StatsExporterControlCameraFrame(report);

	// The view box is built the way W3DRadar::reconstructViewBox does it for the minimap.
	Real values[14];
	const Coord3D position = TheTacticalView->getPosition();
	values[0] = position.x;
	values[1] = position.y;
	values[2] = TheTacticalView->getZoom();
	values[3] = TheTacticalView->getAngle();
	values[4] = TheTacticalView->getPitch();
	values[5] = TheTacticalView->getHeightAboveGround();
	Coord3D corners[4];
	const Bool hasViewBox = TheTacticalView->getScreenCornerWorldPointsAtZ(&corners[0], &corners[1], &corners[2], &corners[3], TheRadar->getTerrainAverageZForStats()) != PlaneClass::NO_INTERSECTION;
	for (Int i = 0; i < 4; ++i)
	{
		values[6 + i * 2] = hasViewBox ? corners[i].x : 0.0f;
		values[7 + i * 2] = hasViewBox ? corners[i].y : 0.0f;
	}

	// The same four ground points decide which objects and events are "in view".
	s_view.valid = hasViewBox;
	if (hasViewBox)
	{
		s_view.minX = s_view.maxX = corners[0].x;
		s_view.minY = s_view.maxY = corners[0].y;
		for (Int i = 0; i < 4; ++i)
		{
			s_view.x[i] = corners[i].x;
			s_view.y[i] = corners[i].y;
			s_view.minX = (corners[i].x < s_view.minX) ? corners[i].x : s_view.minX;
			s_view.maxX = (corners[i].x > s_view.maxX) ? corners[i].x : s_view.maxX;
			s_view.minY = (corners[i].y < s_view.minY) ? corners[i].y : s_view.minY;
			s_view.maxY = (corners[i].y > s_view.maxY) ? corners[i].y : s_view.maxY;
		}
	}

	const ULONGLONG micros = StatsExporterNowMicros();
	const Readers cameraReaders = feedFilter(sink, started, &StatsExporterFeedConfig::camera);
	if (!cameraReaders.empty())
	{
		// A reader that has not seen the camera of this match yet gets it at once, whatever the throttle and the change test say.
		const Readers fresh = cameraReaders.minus(s_sentReaders);
		const Bool sameMatch = s_hasSent && s_sentSerial == s_matchSerial;
		if (!sameMatch || !fresh.empty() || micros - s_lastMicros >= kMinIntervalMicros)
		{
			UnsignedInt extra[4];
			extra[0] = report.userInputCount;
			extra[1] = report.followObjectId;
			extra[2] = (UnsignedInt)report.followMode;
			extra[3] = report.transitionActive ? 1u : 0u;

			Bool changed = !sameMatch || report.followEnded != statsexporter::CAMERA_FOLLOW_END_NONE;
			for (Int i = 0; i < 14 && !changed; ++i)
			{
				if (fabsf(values[i] - s_last[i]) > kEpsilon)
				{
					changed = TRUE;
				}
			}
			for (Int i = 0; i < 4 && !changed; ++i)
			{
				if (extra[i] != s_lastExtra[i])
				{
					changed = TRUE;
				}
			}

			if (changed || !fresh.empty())
			{
				statsexporter::Camera message;
				message.set_timestamp_us(micros);
				message.set_logic_frame(TheGameLogic->getFrame());
				message.set_x(values[0]);
				message.set_y(values[1]);
				message.set_zoom(values[2]);
				message.set_angle(values[3]);
				message.set_pitch(values[4]);
				message.set_height_above_ground(values[5]);
				if (hasViewBox)
				{
					message.set_has_view_box(true);
					message.set_view_tl_x(values[6]);
					message.set_view_tl_y(values[7]);
					message.set_view_tr_x(values[8]);
					message.set_view_tr_y(values[9]);
					message.set_view_br_x(values[10]);
					message.set_view_br_y(values[11]);
					message.set_view_bl_x(values[12]);
					message.set_view_bl_y(values[13]);
				}
				message.set_change_source((statsexporter::CameraChangeSource)report.changeSource);
				message.set_user_input_count(report.userInputCount);
				message.set_follow_mode((statsexporter::CameraFollowMode)report.followMode);
				message.set_follow_object_id(report.followObjectId);
				message.set_follow_ended((statsexporter::CameraFollowEnd)report.followEnded);
				message.set_follow_ended_object_id(report.followEndedObjectId);
				message.set_transition_active(report.transitionActive != FALSE);
				sendMessage(sink, STATS_EXPORTER_FRAME_CAMERA, message, changed ? cameraReaders : fresh);
				StatsExporterControlCameraSent(report);

				memcpy(s_last, values, sizeof(s_last));
				memcpy(s_lastExtra, extra, sizeof(s_lastExtra));
				s_lastMicros = micros;
				s_hasSent = TRUE;
				s_sentReaders = cameraReaders;
				s_sentSerial = s_matchSerial;
			}
		}
	}

	emitViewFrames(sink, started, TheGameLogic->getFrame());
}

int StatsExporterLiveCurrentScreen()
{
	return (int)currentUiState().screen;
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

	sink->takeResyncRequests();
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
		const Readers stallReaders = startedReaders(readyReaders(sink));
		if (!stallReaders.empty() && nowMs - s_lastStallSendMs >= 250)
		{
			// A stall marker has no players and full is false. local_logic_fps stays 0 because the logic is not advancing.
			s_lastStallSendMs = nowMs;
			statsexporter::StatsSnapshot& stalled = stallMessage();
			stalled.Clear();
			stalled.set_frame(TheGameLogic->getFrame());
			stalled.set_network_stalled(true);
			stalled.set_stalled_frames((UnsignedInt)((nowMs - s_stallStartMs) * LOGICFRAMES_PER_SECOND / 1000));
			fillSpeedFields(stalled);
			sendMessage(sink, STATS_EXPORTER_FRAME_STATS_SNAPSHOT, stalled, stallReaders);
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

	StatsExporterQueryPump(sink);
}

void StatsExporterLiveChatInGame(Int senderSlot, const UnicodeString& senderName, const UnicodeString& text, Int playerMask, Bool isLocal)
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	const Readers readers = (sink != NULL) ? readyReaders(sink) : Readers();
	if (readers.empty())
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
	sendChatMessage(sink, message, readers);
}

void StatsExporterLiveChatOnline(Int channel, Int64 userId, const char* senderName, const char* utf8Text, Bool isAction, Bool isSystem, Bool isLocal)
{
	StatsExporterSink* sink = StatsExporterPipeGetSink();
	const Readers readers = (sink != NULL) ? readyReaders(sink) : Readers();
	if (readers.empty() || utf8Text == NULL)
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
	sendChatMessage(sink, message, readers);
}

namespace
{
	// MatchInfo LOADING from the slots the engine just resolved. Nothing here draws random numbers or changes the slots.
	void fillLoadingInfo(statsexporter::MatchInfo& info, GameInfo* game)
	{
		info.Clear();
		info.set_phase(statsexporter::MATCH_PHASE_LOADING);
		info.set_match_id(s_matchId);
		info.set_mode(s_matchMode);
		info.set_map_name(s_matchMapName);
		info.set_map_is_user_map(s_matchMapIsUserMap);
		info.set_logic_frames_per_second(LOGICFRAMES_PER_SECOND);
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

void StatsExporterLiveMatchLoading(GameInfo* game, UnsignedInt randomCrcBeforeResolve, Bool loadingSaveGame)
{
	// Whatever happens below, a stale LOADING identity must not survive into STARTED.
	s_loadingPrepared = FALSE;
	s_hasLoadingBody = FALSE;

	StatsExporterSink* sink = StatsExporterPipeGetSink();
	if (sink == NULL || game == NULL || TheGameLogic == NULL || ThePlayerList == NULL)
	{
		return;
	}
	if (!TheGameLogic->isInInteractiveGame())
	{
		return;
	}
	// A saved game being loaded is not exported: it never reaches StatsExporterLiveMatchStarted().
	if (loadingSaveGame)
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
	s_loadingSent.clear();

	// The load blocks the main thread and no tick runs during it, so a connected reader gets LOADING right here.
	const Readers readers = readyReaders(sink);
	if (s_hasLoadingBody && !readers.empty())
	{
		syncUiState(sink);
		sink->enqueueFrame(STATS_EXPORTER_FRAME_MATCH_INFO, (const UnsignedByte*)s_loadingBody.data(), (UnsignedInt)s_loadingBody.size(), readers);
		s_loadingSent = readers;
	}
}

void StatsExporterLiveChatLan(Int channel, const UnicodeString& senderName, const UnicodeString& text, Bool isAction, Bool isSystem, Bool isLocal)
{
	// The LAN API tells local from remote by IP only, which two instances on one PC share, so the name has to match as well.
	const Bool sameName = (TheLAN == NULL) || (TheLAN->GetMyName().compare(senderName) == 0);
	StatsExporterLiveChatOnline(channel, 0, toUtf8(senderName).c_str(), toUtf8(text).c_str(), isAction, isSystem, isLocal && sameName);
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
	// Starting buildings and units are placed during the map load on frame 0 and are not spending.
	if (TheGameLogic == NULL || TheGameLogic->getFrame() == 0)
	{
		return;
	}
	s_spent[playerIndex][category] += (UnsignedInt)amount;
}

namespace
{
	UnsignedInt s_captureBonus = 0;
	UnsignedInt s_captureBonusFrame = 0;

	Bool eventsWanted()
	{
		// Nothing before the first logic frame: the setup grants radar, upgrades and ownership.
		return StatsExporterPipeGetSink() != NULL && TheGameLogic != NULL && TheGameLogic->getFrame() > 0;
	}
}

void StatsExporterLiveRadarChanged(Int playerIndex, Int oldState, Int newState)
{
	if (oldState == newState || !eventsWanted())
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_RADAR_CHANGED;
	event.category = "PLAYER";
	event.intValue1 = oldState;
	event.intValue2 = newState;
	pushPendingEvent(event);
}

void StatsExporterLiveBattlePlanChanged(Int playerIndex, Int oldPlan, Int newPlan)
{
	if (oldPlan == newPlan || !eventsWanted())
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_BATTLE_PLAN_CHANGED;
	event.category = "PLAYER";
	event.intValue1 = oldPlan;
	event.intValue2 = newPlan;
	pushPendingEvent(event);
}

void StatsExporterLiveUpgradeComplete(Int playerIndex, const char* upgradeName, UnsignedInt cost, Bool isObjectUpgrade)
{
	if (!eventsWanted())
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_UPGRADE_COMPLETE;
	event.category = "UPGRADE";
	event.primaryName = (upgradeName != NULL) ? upgradeName : "";
	event.cost = cost;
	if (isObjectUpgrade)
	{
		event.variant = "object";
	}
	pushPendingEvent(event);
}

void StatsExporterLiveNoteCaptureBonus(Int amount)
{
	s_captureBonus = (amount > 0) ? (UnsignedInt)amount : 0;
	s_captureBonusFrame = (TheGameLogic != NULL) ? TheGameLogic->getFrame() : 0;
}

void StatsExporterLiveCapture(const Object* captured, const Player* oldOwner, const Player* newOwner)
{
	// The bonus is paid just before the capture hook runs, in the same call, so it only counts on the same frame.
	const UnsignedInt bonus = (TheGameLogic != NULL && s_captureBonusFrame == TheGameLogic->getFrame()) ? s_captureBonus : 0;
	s_captureBonus = 0;

	if (captured == NULL || oldOwner == NULL || newOwner == NULL || oldOwner == newOwner || !eventsWanted())
	{
		return;
	}
	if (!captured->isKindOf(KINDOF_STRUCTURE) || captured->getTemplate() == NULL)
	{
		return;
	}
	// Allies handing structures over (an asset transfer on surrender, script team changes) is not a capture:
	// no event when each owner counts the other as an ally.
	if (oldOwner->getRelationship(newOwner->getDefaultTeam()) == ALLIES && newOwner->getRelationship(oldOwner->getDefaultTeam()) == ALLIES)
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = newOwner->getPlayerIndex();
	event.kind = statsexporter::STATEVENT_CAPTURE;
	event.category = classifyThingTemplateCategory(captured->getTemplate());
	event.primaryName = templateName(captured->getTemplate());
	event.hasLocation = TRUE;
	event.x = captured->getPosition()->x;
	event.y = captured->getPosition()->y;
	event.intValue1 = oldOwner->getPlayerIndex();
	if (isTechStructure(captured))
	{
		event.variant = "tech";
	}
	event.cost = bonus;
	event.objectId = captured->getID();
	pushPendingEvent(event);
}

void StatsExporterLivePowerSabotaged(Int playerIndex, UnsignedInt durationFrames)
{
	if (!eventsWanted())
	{
		return;
	}

	PendingEvent event;
	event.playerIndex = playerIndex;
	event.kind = statsexporter::STATEVENT_POWER_SABOTAGED;
	event.category = "PLAYER";
	event.intValue1 = (Int)durationFrames;
	pushPendingEvent(event);
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

	// One event per player and match: a quit sends both the self destruct command and the network leave, and the first one wins.
	if (playerIndex >= 0 && playerIndex < kMaxCounterPlayers)
	{
		if (s_playerLeft[playerIndex])
		{
			return;
		}
		s_playerLeft[playerIndex] = TRUE;
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
