/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding counters (-pathProfile <file>), measurement only: see
// PathProfile.h. Nothing here writes game state; the tables are fixed-size statics and the files use the C runtime only.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/PathProfile.h"

#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AIPathfind.h"	// Path, PathNode, PATHFIND_CELL_SIZE_F

#include <stdio.h>
#include <string.h>

namespace PathProfile
{
	Bool Enabled = FALSE;

	// ---------------------------------------------------------------------------------------------------------------
	// Settings
	const Int STUCK_FRAMES = 60;					///< a moving unit with less than one cell of progress for this long is stuck
	const Real PROGRESS = PATHFIND_CELL_SIZE_F;		///< one cell
	const Real ARRIVED = 2.0f * PATHFIND_CELL_SIZE_F;	///< a move ended this close to its goal arrived
	const Real PARTIAL_DISTANCE = 1.5f * PATHFIND_CELL_SIZE_F;	///< a path ending farther than this from its goal is partial
	const Int ARRIVAL_BIN = 15;						///< frames per arrival-time histogram bin
	const Int ARRIVAL_BINS = 1200;					///< up to 600 s
	const Int TRACK_SLOTS = 4096;					///< units followed at once (more are counted as untracked)

	// ---------------------------------------------------------------------------------------------------------------
	// Output
	char Base[512] = "";
	FILE * FramesFile = nullptr;
	FILE * JsonFile = nullptr;
	FILE * SummaryFile = nullptr;
	long JsonEnd = -1;		///< where the closing ] of the JSON array is, -1 before the first record
	Int64 Frequency = 0;
	Int MatchIndex = 0;

	Int64 Now()
	{
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}

	Bool NoTime = FALSE;	///< -pathProfileNoTime: every time column 0, so two runs of one build compare byte for byte

	void Set_No_Time()
	{
		NoTime = TRUE;
	}

	Real Ms(Int64 ticks)
	{
		if (NoTime)
			return 0.0f;
		return Frequency > 0 ? (Real)((double)ticks * 1000.0 / (double)Frequency) : 0.0f;
	}

	void Set_Output(const char * file)
	{
		if (file == nullptr || file[0] == 0)
			return;
		strncpy(Base, file, sizeof(Base) - 1);
		Base[sizeof(Base) - 1] = 0;
		const size_t n = strlen(Base);
		if (n > 4 && (_stricmp(Base + n - 4, ".csv") == 0 || _stricmp(Base + n - 4, ".txt") == 0))
			Base[n - 4] = 0;
		else if (n > 5 && _stricmp(Base + n - 5, ".json") == 0)
			Base[n - 5] = 0;
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		Frequency = f.QuadPart;
		Enabled = TRUE;
	}

	const char * Output_Base()
	{
		return Base;
	}

	// PathProfileGroups.cpp: the scenario groups, fed from here
	void Groups_Search(ObjectID unit, SearchType type, Result result, Path * path, UnsignedInt frame);
	void Groups_Frame(UnsignedInt frame);
	void Groups_Begin_Match();
	void Groups_End_Match(UnsignedInt frame);

	FILE * Open(const char * suffix)
	{
		char path[600];
		snprintf(path, sizeof(path), "%s%s", Base, suffix);
		return fopen(path, "wt");
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Counters
	struct TypeStats
	{
		Int count, ok, partial, failed, outOfPool;
		Int64 cells, cellsMax;
		Int64 ticks, ticksMax;
	};

	struct Totals	///< per match (and the per-frame part, reset each frame)
	{
		TypeStats type[TYPE_COUNT];
		Int64 searches, cells, ticks;		///< top-level searches only (a coarse search inside another is not added again)
		Int failed, partial, outOfPool;
		Int queued, duplicate, refused, queueMax;
		Int zones;
		Int64 zoneTicks;
	};

	Totals Match;
	Totals FrameT;
	Int Depth = 0;
	Int PoolFailures = 0;
	Int QueueDepth = 0;

	// per match, frame-level
	UnsignedInt FirstFrame = 0, LastFrame = 0, FrameCount = 0;
	Bool MatchOpen = FALSE;
	Int64 WorstFrameTicks = 0;
	UnsignedInt WorstFrame = 0;
	Int64 WorstSearchTicks = 0;
	Int WorstSearchType = -1;
	UnsignedInt WorstSearchFrame = 0;
	UnsignedInt CurrentFrame = 0;
	Int64 CellsMaxSearch = 0;

	// units
	Int64 StuckUnitFrames = 0, StuckEpisodes = 0, BlockedUnitFrames = 0, MovingUnitFrames = 0, Untracked = 0;
	Int Arrivals = 0, StoppedShort = 0, Redirected = 0;
	Int64 ArrivalFramesSum = 0;
	Int ArrivalFramesMax = 0;
	Int ArrivalHist[ARRIVAL_BINS];
	Int FrameArrivals = 0, FrameStuck = 0, FrameMoving = 0, FrameBlocked = 0, FrameTracked = 0;

	void Clear(Totals & t)
	{
		memset(&t, 0, sizeof(t));
	}

	void Add_Search(Totals & t, SearchType type, Result result, Int64 cells, Int64 ticks, Bool nested)
	{
		TypeStats & s = t.type[type];
		++s.count;
		if (result == OK) ++s.ok;
		else if (result == PARTIAL) ++s.partial;
		else if (result == FAILED) ++s.failed;
		else ++s.outOfPool;
		s.cells += cells;
		if (cells > s.cellsMax) s.cellsMax = cells;
		s.ticks += ticks;
		if (ticks > s.ticksMax) s.ticksMax = ticks;
		if (nested)
			return;
		++t.searches;
		t.cells += cells;
		t.ticks += ticks;
		if (result == PARTIAL) ++t.partial;
		else if (result == FAILED) ++t.failed;
		else if (result == OUT_OF_POOL) ++t.outOfPool;
	}

	void Pool_Exhausted()
	{
		if (Enabled)
			++PoolFailures;
	}

	Search::Search(SearchType type, const Int & cellCounter, const Object * unit)
		: m_type(type), m_unit(unit != nullptr ? unit->getID() : INVALID_ID), m_cells(&cellCounter), m_cellsAtStart(cellCounter),
		m_poolAtStart(PoolFailures), m_start(0), m_on(Enabled), m_nested(FALSE)
	{
		if (!m_on)
			return;
		m_nested = Depth > 0;
		++Depth;
		m_start = Now();
	}

	void Search::Finish(Result result, Path * path)
	{
		if (!m_on)
			return;
		if (!m_nested && m_unit != INVALID_ID)
			Groups_Search(m_unit, m_type, PoolFailures != m_poolAtStart ? OUT_OF_POOL : result, path, CurrentFrame);
		const Int64 ticks = Now() - m_start;
		--Depth;
		m_on = FALSE;
		if (PoolFailures != m_poolAtStart)
			result = OUT_OF_POOL;
		Int64 cells = (Int64)(*m_cells - m_cellsAtStart);
		if (cells < 0)
			cells = 0;	// the counter was reset during the search (it is only reset when the queue starts a frame)
		Add_Search(Match, m_type, result, cells, ticks, m_nested);
		Add_Search(FrameT, m_type, result, cells, ticks, m_nested);
		if (!m_nested)
		{
			if (ticks > WorstSearchTicks)
			{
				WorstSearchTicks = ticks;
				WorstSearchType = (Int)m_type;
				WorstSearchFrame = CurrentFrame;
			}
			if (cells > CellsMaxSearch)
				CellsMaxSearch = cells;
		}
	}

	Result Search::Classify(Path * path, const void * goal, Bool closestOK)
	{
		if (path == nullptr)
			return FAILED;
		if (goal == nullptr || closestOK)
			return OK;
		const PathNode * last = path->getLastNode();
		if (last == nullptr)
			return OK;
		const Coord3D * p = last->getPosition();
		const Coord3D * g = (const Coord3D *)goal;
		const Real dx = p->x - g->x, dy = p->y - g->y;
		return dx * dx + dy * dy > PARTIAL_DISTANCE * PARTIAL_DISTANCE ? PARTIAL : OK;
	}

	void Queue_Request(Int outcome)
	{
		if (!Enabled)
			return;
		Totals * all[2] = { &Match, &FrameT };
		for (Totals * t : all)
		{
			if (outcome == 0) ++t->queued;
			else if (outcome == 1) ++t->duplicate;
			else ++t->refused;
		}
	}

	void Queue_State(Int depth)
	{
		if (!Enabled)
			return;
		QueueDepth = depth;
		if (depth > FrameT.queueMax) FrameT.queueMax = depth;
		if (depth > Match.queueMax) Match.queueMax = depth;
	}

	Zones::Zones() : m_start(0), m_on(Enabled)
	{
		if (m_on)
			m_start = Now();
	}

	Zones::~Zones()
	{
		if (!m_on)
			return;
		const Int64 ticks = Now() - m_start;
		++Match.zones; Match.zoneTicks += ticks;
		++FrameT.zones; FrameT.zoneTicks += ticks;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Units: stuck and arrival, read from the AI each frame
	struct Unit
	{
		ObjectID id;
		UnsignedInt seen;		///< the frame it was last seen
		Real progressX, progressY;
		UnsignedInt progressFrame;
		Bool stuck;
		Bool episode;			///< a move order is being followed
		Real goalX, goalY;
		UnsignedInt orderFrame;
	};
	Unit Units[TRACK_SLOTS];

	// The unit's slot (open addressing). A slot not seen last frame belongs to a unit that is gone and may be reused,
	// but only after the whole probe chain was searched for the unit's own live slot.
	Unit * Slot(ObjectID id, UnsignedInt frame, Bool * isNew)
	{
		const UnsignedInt h = ((UnsignedInt)id * 2654435761u) & (TRACK_SLOTS - 1);
		Unit * reuse = nullptr;
		for (Int probe = 0; probe < TRACK_SLOTS; ++probe)
		{
			Unit & u = Units[(h + probe) & (TRACK_SLOTS - 1)];
			const Bool live = u.id != INVALID_ID && u.seen + 1 >= frame;
			if (live && u.id == id)
			{
				*isNew = FALSE;
				return &u;
			}
			if (!live && reuse == nullptr)
				reuse = &u;
			if (u.id == INVALID_ID)
				break;	// never used: the chain ends here
		}
		if (reuse == nullptr)
			return nullptr;
		memset(reuse, 0, sizeof(*reuse));
		reuse->id = id;
		*isNew = TRUE;
		return reuse;
	}

	Bool Is_Move(AIStateType type)
	{
		switch (type)
		{
			case AI_MOVE_TO: case AI_FOLLOW_PATH: case AI_MOVE_AND_TIGHTEN: case AI_MOVE_AND_EVACUATE:
			case AI_MOVE_AND_EVACUATE_AND_EXIT: case AI_MOVE_AND_DELETE: case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS: case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT:
				return TRUE;
			default:
				return FALSE;
		}
	}

	void End_Arrival(Unit & u, Real x, Real y, UnsignedInt frame)
	{
		u.episode = FALSE;
		const Real dx = x - u.goalX, dy = y - u.goalY;
		if (dx * dx + dy * dy <= ARRIVED * ARRIVED)
		{
			const Int frames = (Int)(frame - u.orderFrame);
			++Arrivals;
			++FrameArrivals;
			ArrivalFramesSum += frames;
			if (frames > ArrivalFramesMax) ArrivalFramesMax = frames;
			Int bin = frames / ARRIVAL_BIN;
			if (bin >= ARRIVAL_BINS) bin = ARRIVAL_BINS - 1;
			++ArrivalHist[bin];
		}
		else
			++StoppedShort;
	}

	void Scan_Units(UnsignedInt frame)
	{
		if (TheGameLogic == nullptr)
			return;
		for (Object * obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
		{
			const AIUpdateInterface * ai = obj->getAI();
			if (ai == nullptr || ai->isAiInDeadState() || ai->getCurLocomotor() == nullptr || obj->isKindOf(KINDOF_AIRCRAFT)
				|| obj->isKindOf(KINDOF_STRUCTURE))
				continue;
			Bool isNew = FALSE;
			Unit * u = Slot(obj->getID(), frame, &isNew);
			if (u == nullptr)
			{
				++Untracked;
				continue;
			}
			++FrameTracked;
			u->seen = frame;
			const Coord3D * pos = obj->getPosition();
			const Bool moving = ai->isMoving() || ai->isWaitingForPath();
			if (ai->isBlockedAndStuck())
			{
				++BlockedUnitFrames;
				++FrameBlocked;
			}
			// stuck: a move with no cell of progress for STUCK_FRAMES
			if (moving)
			{
				++MovingUnitFrames;
				++FrameMoving;
				const Real dx = pos->x - u->progressX, dy = pos->y - u->progressY;
				if (isNew || u->progressFrame == 0 || dx * dx + dy * dy >= PROGRESS * PROGRESS)
				{
					u->progressX = pos->x;
					u->progressY = pos->y;
					u->progressFrame = frame == 0 ? 1 : frame;
					u->stuck = FALSE;
				}
				else if (frame - u->progressFrame >= (UnsignedInt)STUCK_FRAMES)
				{
					++StuckUnitFrames;
					++FrameStuck;
					if (!u->stuck)
						++StuckEpisodes;
					u->stuck = TRUE;
				}
			}
			else
			{
				u->progressFrame = 0;
				u->stuck = FALSE;
			}
			// arrival: from a move order's start to its end at the goal
			const Bool inMove = Is_Move(ai->getAIStateType());
			const Coord3D * goal = inMove ? ai->getGoalPosition() : nullptr;
			if (inMove && goal != nullptr)
			{
				const Real gx = goal->x - u->goalX, gy = goal->y - u->goalY;
				if (u->episode && gx * gx + gy * gy > PROGRESS * PROGRESS)
				{
					++Redirected;	// a new goal before reaching the old one
					u->episode = FALSE;
				}
				if (!u->episode)
				{
					u->episode = TRUE;
					u->goalX = goal->x;
					u->goalY = goal->y;
					u->orderFrame = frame;
				}
			}
			else if (u->episode)
				End_Arrival(*u, pos->x, pos->y, frame);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Frames and matches
	char MapName[300] = "";	///< taken at the match's start, JSON-escaped (the summary may be written at exit)

	void Begin_Match(UnsignedInt frame)
	{
		const char * map = TheGlobalData != nullptr ? TheGlobalData->m_mapName.str() : "";
		Int j = 0;
		for (const char * c = map; c != nullptr && *c != 0 && j < (Int)sizeof(MapName) - 2; ++c)
		{
			if (*c == '\\' || *c == '"')
				MapName[j++] = '\\';
			MapName[j++] = *c;
		}
		MapName[j] = 0;
		Clear(Match);
		Clear(FrameT);
		memset(Units, 0, sizeof(Units));
		memset(ArrivalHist, 0, sizeof(ArrivalHist));
		FirstFrame = LastFrame = frame;
		FrameCount = 0;
		WorstFrameTicks = 0; WorstFrame = 0;
		WorstSearchTicks = 0; WorstSearchType = -1; WorstSearchFrame = 0;
		CellsMaxSearch = 0;
		StuckUnitFrames = StuckEpisodes = BlockedUnitFrames = MovingUnitFrames = Untracked = 0;
		Arrivals = StoppedShort = Redirected = 0;
		ArrivalFramesSum = 0; ArrivalFramesMax = 0;
		MatchOpen = TRUE;
		++MatchIndex;
		Groups_Begin_Match();
	}

	const char * const TypeNames[TYPE_COUNT] = { "find", "closest", "attack", "safe", "patch", "group", "aircraft", "coarse" };

	void End_Frame(UnsignedInt frame)
	{
		if (!Enabled)
			return;
		if (!MatchOpen || frame < LastFrame)
		{
			if (MatchOpen)
				End_Match();
			Begin_Match(frame);
		}
		CurrentFrame = frame;
		FrameArrivals = FrameStuck = FrameMoving = FrameBlocked = FrameTracked = 0;
		Scan_Units(frame);
		Groups_Frame(frame);
		LastFrame = frame;
		++FrameCount;
		if (FrameT.ticks > WorstFrameTicks)
		{
			WorstFrameTicks = FrameT.ticks;
			WorstFrame = frame;
		}
		if (FramesFile == nullptr)
		{
			FramesFile = Open(".frames.csv");
			if (FramesFile != nullptr)
			{
				fprintf(FramesFile, "match,frame,searches,cells,search_ms,worst_search_ms");
				for (Int t = 0; t < TYPE_COUNT; ++t)
					fprintf(FramesFile, ",%s", TypeNames[t]);
				fprintf(FramesFile, ",failed,partial,out_of_pool,queued,refused,queue_depth,zone_rebuilds,zone_ms,tracked,moving,stuck,blocked,arrivals\n");
			}
		}
		if (FramesFile != nullptr)
		{
			Int64 worst = 0;
			for (Int t = 0; t < TYPE_COUNT; ++t)
				if (t != COARSE && FrameT.type[t].ticksMax > worst)
					worst = FrameT.type[t].ticksMax;
			fprintf(FramesFile, "%d,%u,%lld,%lld,%.4f,%.4f", MatchIndex, frame, (long long)FrameT.searches, (long long)FrameT.cells,
				Ms(FrameT.ticks), Ms(worst));
			for (Int t = 0; t < TYPE_COUNT; ++t)
				fprintf(FramesFile, ",%d", FrameT.type[t].count);
			fprintf(FramesFile, ",%d,%d,%d,%d,%d,%d,%d,%.4f,%d,%d,%d,%d,%d\n", FrameT.failed, FrameT.partial, FrameT.outOfPool,
				FrameT.queued, FrameT.refused, QueueDepth, FrameT.zones, Ms(FrameT.zoneTicks), FrameTracked, FrameMoving, FrameStuck,
				FrameBlocked, FrameArrivals);
		}
		Clear(FrameT);
	}

	Int Percentile(Real p)
	{
		if (Arrivals <= 0)
			return 0;
		const Int want = (Int)(p * (Real)(Arrivals - 1)) + 1;
		Int seen = 0;
		for (Int b = 0; b < ARRIVAL_BINS; ++b)
		{
			seen += ArrivalHist[b];
			if (seen >= want)
				return b * ARRIVAL_BIN + ARRIVAL_BIN / 2;
		}
		return ArrivalFramesMax;
	}

	void End_Match()
	{
		if (!Enabled || !MatchOpen)
			return;
		MatchOpen = FALSE;
		Groups_End_Match(LastFrame);
		if (FramesFile != nullptr)
			fflush(FramesFile);
		const char * mapEsc = MapName;
		const Real seconds = (Real)FrameCount / (Real)LOGICFRAMES_PER_SECOND;
		const Real arrivalMean = Arrivals > 0 ? (Real)ArrivalFramesSum / (Real)Arrivals / (Real)LOGICFRAMES_PER_SECOND : 0.0f;
		const Real fps = (Real)LOGICFRAMES_PER_SECOND;

		// JSON: an array of match records, kept valid after every match
		if (JsonFile == nullptr)
			JsonFile = Open(".json");
		if (JsonFile != nullptr)
		{
			if (JsonEnd < 0)
				fprintf(JsonFile, "[\n");
			else
			{
				fseek(JsonFile, JsonEnd, SEEK_SET);
				fprintf(JsonFile, ",\n");
			}
			fprintf(JsonFile, "{\"match\":%d,\"map\":\"%s\",\"first_frame\":%u,\"last_frame\":%u,\"frames\":%u,\"seconds\":%.2f,\n",
				MatchIndex, mapEsc, FirstFrame, LastFrame, FrameCount, seconds);
			fprintf(JsonFile, " \"searches\":%lld,\"cells\":%lld,\"cells_max_search\":%lld,\"search_ms\":%.3f,\"search_ms_per_frame\":%.4f,"
				"\"worst_frame_ms\":%.3f,\"worst_frame\":%u,\"worst_search_ms\":%.3f,\"worst_search_type\":\"%s\",\"worst_search_frame\":%u,\n",
				(long long)Match.searches, (long long)Match.cells, (long long)CellsMaxSearch, Ms(Match.ticks),
				FrameCount > 0 ? Ms(Match.ticks) / (Real)FrameCount : 0.0f, Ms(WorstFrameTicks), WorstFrame, Ms(WorstSearchTicks),
				WorstSearchType >= 0 ? TypeNames[WorstSearchType] : "", WorstSearchFrame);
			fprintf(JsonFile, " \"failed\":%d,\"partial\":%d,\"out_of_pool\":%d,\"queued\":%d,\"queue_duplicates\":%d,\"refused\":%d,\"queue_max\":%d,"
				"\"zone_rebuilds\":%d,\"zone_ms\":%.3f,\n", Match.failed, Match.partial, Match.outOfPool, Match.queued, Match.duplicate,
				Match.refused, Match.queueMax, Match.zones, Ms(Match.zoneTicks));
			fprintf(JsonFile, " \"stuck_unit_frames\":%lld,\"stuck_episodes\":%lld,\"stuck_after_frames\":%d,\"engine_blocked_unit_frames\":%lld,"
				"\"moving_unit_frames\":%lld,\"untracked\":%lld,\n", (long long)StuckUnitFrames, (long long)StuckEpisodes, STUCK_FRAMES,
				(long long)BlockedUnitFrames, (long long)MovingUnitFrames, (long long)Untracked);
			fprintf(JsonFile, " \"arrivals\":%d,\"arrival_s_mean\":%.3f,\"arrival_s_p50\":%.3f,\"arrival_s_p90\":%.3f,\"arrival_s_max\":%.3f,"
				"\"stopped_short\":%d,\"redirected\":%d,\n", Arrivals, arrivalMean, (Real)Percentile(0.5f) / fps, (Real)Percentile(0.9f) / fps,
				(Real)ArrivalFramesMax / fps, StoppedShort, Redirected);
			fprintf(JsonFile, " \"types\":{");
			for (Int t = 0; t < TYPE_COUNT; ++t)
			{
				const TypeStats & s = Match.type[t];
				fprintf(JsonFile, "%s\"%s\":{\"count\":%d,\"ok\":%d,\"partial\":%d,\"failed\":%d,\"out_of_pool\":%d,\"cells\":%lld,\"cells_max\":%lld,"
					"\"ms\":%.3f,\"ms_max\":%.3f}", t > 0 ? "," : "", TypeNames[t], s.count, s.ok, s.partial, s.failed, s.outOfPool,
					(long long)s.cells, (long long)s.cellsMax, Ms(s.ticks), Ms(s.ticksMax));
			}
			fprintf(JsonFile, "}}");
			JsonEnd = ftell(JsonFile);
			fprintf(JsonFile, "\n]\n");
			fflush(JsonFile);
		}

		// CSV: one row per match
		if (SummaryFile == nullptr)
		{
			SummaryFile = Open(".summary.csv");
			if (SummaryFile != nullptr)
			{
				fprintf(SummaryFile, "match,map,frames,searches,cells,cells_max_search,search_ms,search_ms_per_frame,worst_frame_ms,worst_search_ms,"
					"failed,partial,out_of_pool,queued,refused,queue_max,zone_rebuilds,zone_ms,stuck_unit_frames,stuck_episodes,"
					"engine_blocked_unit_frames,arrivals,arrival_s_mean,arrival_s_p50,arrival_s_p90,arrival_s_max,stopped_short,redirected");
				for (Int t = 0; t < TYPE_COUNT; ++t)
					fprintf(SummaryFile, ",%s_count,%s_cells,%s_ms", TypeNames[t], TypeNames[t], TypeNames[t]);
				fprintf(SummaryFile, "\n");
			}
		}
		if (SummaryFile != nullptr)
		{
			fprintf(SummaryFile, "%d,\"%s\",%u,%lld,%lld,%lld,%.3f,%.4f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%.3f,%lld,%lld,%lld,%d,%.3f,%.3f,%.3f,%.3f,%d,%d",
				MatchIndex, mapEsc, FrameCount, (long long)Match.searches, (long long)Match.cells, (long long)CellsMaxSearch, Ms(Match.ticks),
				FrameCount > 0 ? Ms(Match.ticks) / (Real)FrameCount : 0.0f, Ms(WorstFrameTicks), Ms(WorstSearchTicks), Match.failed,
				Match.partial, Match.outOfPool, Match.queued, Match.refused, Match.queueMax, Match.zones, Ms(Match.zoneTicks),
				(long long)StuckUnitFrames, (long long)StuckEpisodes, (long long)BlockedUnitFrames, Arrivals, arrivalMean,
				(Real)Percentile(0.5f) / fps, (Real)Percentile(0.9f) / fps, (Real)ArrivalFramesMax / fps, StoppedShort, Redirected);
			for (Int t = 0; t < TYPE_COUNT; ++t)
				fprintf(SummaryFile, ",%d,%lld,%.3f", Match.type[t].count, (long long)Match.type[t].cells, Ms(Match.type[t].ticks));
			fprintf(SummaryFile, "\n");
			fflush(SummaryFile);
		}
	}

	// A process that ends inside a match (a replay played to its end and closed) still writes the match's summary.
	struct AtExit
	{
		~AtExit()
		{
			if (Enabled && MatchOpen)
				End_Match();
			if (FramesFile != nullptr) fclose(FramesFile);
			if (JsonFile != nullptr) fclose(JsonFile);
			if (SummaryFile != nullptr) fclose(SummaryFile);
			FramesFile = JsonFile = SummaryFile = nullptr;
		}
	};
	AtExit TheAtExit;
}
