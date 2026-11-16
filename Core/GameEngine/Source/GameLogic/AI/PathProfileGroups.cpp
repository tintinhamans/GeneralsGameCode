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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding counters per scenario group (-pathScenario, PathProfile.h):
// each unit of a named group followed from its order to its end, written per phase to <file>.groups.csv and
// <file>.units.csv. Reads game state only; fixed-size static tables; C runtime files.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/PathProfile.h"

#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"

#include <stdio.h>
#include <string.h>

namespace PathProfile
{
	const Int MAX_GROUPS = 32;
	const Int MAX_UNITS = 4096;
	const Int MAP_SLOTS = 8192;
	const Int STUCK_AFTER = 60;		///< frames without a cell of progress before a moving unit counts as stuck
	const Int JAM_UNITS = 10;		///< a group frame with this many stuck units is a jam frame
	const Int STILL_AFTER_EVENT = 150;	///< 5 s
	const Real CELL = PATHFIND_CELL_SIZE_F;

	enum Outcome { PENDING, ARRIVED, ENDED_SHORT, STOPPED_BY_ORDER, SUPERSEDED, DIED, NO_ORDER };
	const char * const OutcomeNames[] = { "pending", "arrived", "ended_short", "stopped_by_order", "superseded", "died", "no_order" };
	const char * const KindNames[] = { "move", "attackmove", "guard", "stop", "repair", "enter", "evacuate", "gather",
		"reversemove" };

	struct GroupInfo
	{
		char name[48];
		Int kind;
		Real clickX, clickY;
		UnsignedInt orderFrame;
		Bool ordered;
		UnsignedInt firstArrival;	///< absolute frame, 0: none yet
		UnsignedInt lastMove;		///< the last frame a unit moved after the first arrival
		Int jamFrames;
		Int rerouteMax;
	};

	struct GroupUnit
	{
		ObjectID id;
		Int group;
		char tmpl[48];
		Int footN;				///< footprint cells across (1..5)
		Int footR;				///< the pathfinder's iRadius
		Bool footCenter;
		Bool dead;
		Real spawnX, spawnY, lastX, lastY, maxFromSpawn;
		// the current order
		Int outcome;
		Int kind;
		UnsignedInt orderFrame, endFrame, engagedFrame;
		Real clickX, clickY, startX, startY, goalX, goalY, restClick;
		Bool sawGoal, moved;
		Int goalMoves;
		// stuck
		Real progX, progY;
		UnsignedInt progFrame;
		Bool stuck;
		UnsignedInt stuckStart;
		// per phase
		Int stuckFrames, stuckEpisodes, over5, over10, longestStuck, blockedFrames, hullFrames, searches, repaths, partials,
			failed, pathNodes, superseded;
		Int trips;				///< deliveries (a supply carrier's box count went down)
		Int insideFrames;		///< frames spent inside a container
		Int lastBoxes;
		Bool inside;			///< in a container this frame (left out of the stuck, hull and overlap counts)
		Real driven;
		// state of the last frame
		Bool hullNow, atRest, overlapNow;
		// events
		Bool reroutePending;
		Real eventX, eventY;
	};

	Bool GroupsActive = FALSE;
	GroupInfo Groups[MAX_GROUPS];
	Int GroupCount = 0;
	GroupUnit GroupUnits[MAX_UNITS];
	Int UnitCount = 0;
	Int UnitMap[MAP_SLOTS];		///< ObjectID -> GroupUnits index + 1 (0: empty)
	char PhaseName[48] = "main";
	UnsignedInt PhaseStart = 0;
	UnsignedInt EventFrame = 0;
	Bool EventOpen = FALSE;
	Int StillAfterEvent = 0;
	FILE * GroupsFile = nullptr;
	FILE * UnitsFile = nullptr;

	void Groups_On()
	{
		GroupsActive = TRUE;
		GroupCount = UnitCount = 0;
		memset(UnitMap, 0, sizeof(UnitMap));
		strcpy(PhaseName, "main");
		PhaseStart = 0;
		EventOpen = FALSE;
		StillAfterEvent = 0;
	}

	Int Group_Index(const char * name)
	{
		for (Int g = 0; g < GroupCount; ++g)
			if (strcmp(Groups[g].name, name) == 0)
				return g;
		if (GroupCount >= MAX_GROUPS)
			return -1;
		GroupInfo & g = Groups[GroupCount];
		memset(&g, 0, sizeof(g));
		strncpy(g.name, name, sizeof(g.name) - 1);
		return GroupCount++;
	}

	const char * Group_Name(Int group)
	{
		return group >= 0 && group < GroupCount ? Groups[group].name : "";
	}

	GroupUnit * Find(ObjectID id)
	{
		UnsignedInt h = ((UnsignedInt)id * 2654435761u) & (MAP_SLOTS - 1);
		for (Int probe = 0; probe < MAP_SLOTS; ++probe)
		{
			const Int v = UnitMap[(h + probe) & (MAP_SLOTS - 1)];
			if (v == 0)
				return nullptr;
			if (GroupUnits[v - 1].id == id)
				return &GroupUnits[v - 1];
		}
		return nullptr;
	}

	// The pathfinder's footprint (Pathfinder::getRadiusAndCenter).
	void Footprint(const Object * obj, Int * n, Int * r, Bool * center)
	{
		Real diameter = 2.0f * obj->getGeometryInfo().getBoundingCircleRadius();
		if (diameter > CELL && diameter < 2.0f * CELL)
			diameter = 2.0f * CELL;
		Int i = REAL_TO_INT_FLOOR(diameter / CELL + 0.3f);
		Bool c = FALSE;
		if (i == 0) i = 1;
		if (i & 1) c = TRUE;
		i /= 2;
		const Int maxRadius = obj->getTemplate() != nullptr ? obj->getTemplate()->getMaxPathFindingCellRadius() : 2;
		if (i > maxRadius) { i = maxRadius; c = TRUE; }
		*r = i;
		*center = c;
		*n = c ? 2 * i + 1 : 2 * i;
		if (*n < 1) *n = 1;
	}

	void Footprint_Box(const GroupUnit & u, Real x, Real y, Int * x0, Int * y0, Int * x1, Int * y1)
	{
		if (u.footCenter)
		{
			const Int cx = REAL_TO_INT_FLOOR(x / CELL), cy = REAL_TO_INT_FLOOR(y / CELL);
			*x0 = cx - u.footR; *x1 = cx + u.footR;
			*y0 = cy - u.footR; *y1 = cy + u.footR;
		}
		else
		{
			const Int cx = REAL_TO_INT_FLOOR(x / CELL + 0.5f), cy = REAL_TO_INT_FLOOR(y / CELL + 0.5f);
			*x0 = cx - u.footR; *x1 = cx + u.footR - 1;
			*y0 = cy - u.footR; *y1 = cy + u.footR - 1;
		}
	}

	void Group_Add(Int group, const Object * obj)
	{
		if (!GroupsActive || group < 0 || group >= GroupCount || obj == nullptr || UnitCount >= MAX_UNITS || Find(obj->getID()) != nullptr)
			return;
		GroupUnit & u = GroupUnits[UnitCount];
		memset(&u, 0, sizeof(u));
		u.id = obj->getID();
		u.group = group;
		strncpy(u.tmpl, obj->getTemplate()->getName().str(), sizeof(u.tmpl) - 1);
		Footprint(obj, &u.footN, &u.footR, &u.footCenter);
		const Coord3D * p = obj->getPosition();
		u.spawnX = u.lastX = u.progX = p->x;
		u.spawnY = u.lastY = u.progY = p->y;
		u.outcome = NO_ORDER;
		UnsignedInt h = ((UnsignedInt)u.id * 2654435761u) & (MAP_SLOTS - 1);
		for (Int probe = 0; probe < MAP_SLOTS; ++probe)
		{
			Int & slot = UnitMap[(h + probe) & (MAP_SLOTS - 1)];
			if (slot == 0)
			{
				slot = UnitCount + 1;
				break;
			}
		}
		++UnitCount;
	}

	void Group_Order(Int group, Int kind, Real x, Real y, UnsignedInt frame)
	{
		if (!GroupsActive || group < 0 || group >= GroupCount)
			return;
		GroupInfo & g = Groups[group];
		g.kind = kind;
		g.clickX = x;
		g.clickY = y;
		g.orderFrame = frame;
		g.ordered = TRUE;
		g.firstArrival = 0;
		g.lastMove = 0;
		for (Int i = 0; i < UnitCount; ++i)
		{
			GroupUnit & u = GroupUnits[i];
			if (u.group != group || u.dead)
				continue;
			if (kind == ORDER_STOP || kind == ORDER_EVACUATE)	// the order ends the current one (evacuate: the container's)
			{
				if (u.outcome == PENDING)
				{
					u.outcome = STOPPED_BY_ORDER;
					u.endFrame = frame;
				}
				continue;
			}
			if (u.outcome == PENDING)
				++u.superseded;	// a new order before the last one ended
			u.outcome = PENDING;
			u.kind = kind;
			u.orderFrame = frame;
			u.endFrame = 0;
			u.engagedFrame = 0;
			u.clickX = x;
			u.clickY = y;
			u.startX = u.lastX;
			u.startY = u.lastY;
			u.sawGoal = FALSE;
			u.moved = FALSE;
			u.goalMoves = 0;
			u.restClick = 0.0f;
		}
	}

	Real Group_Arrived(Int group)
	{
		Int n = 0, arrived = 0;
		for (Int i = 0; i < UnitCount; ++i)
		{
			const GroupUnit & u = GroupUnits[i];
			if (u.group != group || u.dead)
				continue;
			++n;
			if (u.outcome == ARRIVED)
				++arrived;
		}
		return n > 0 ? (Real)arrived / (Real)n : 0.0f;
	}

	Bool Group_Near(Int group, Real x, Real y, Real radius)
	{
		for (Int i = 0; i < UnitCount; ++i)
		{
			const GroupUnit & u = GroupUnits[i];
			if (u.group != group || u.dead)
				continue;
			const Real dx = u.lastX - x, dy = u.lastY - y;
			if (dx * dx + dy * dy <= radius * radius)
				return TRUE;
		}
		return FALSE;
	}

	void Event(const char * name, UnsignedInt frame)
	{
		if (!GroupsActive)
			return;
		EventFrame = frame;
		EventOpen = TRUE;
		StillAfterEvent = 0;
		for (Int g = 0; g < GroupCount; ++g)
			Groups[g].rerouteMax = 0;
		for (Int i = 0; i < UnitCount; ++i)
		{
			GroupUnit & u = GroupUnits[i];
			u.reroutePending = !u.dead && u.outcome == PENDING;
			u.eventX = u.lastX;
			u.eventY = u.lastY;
		}
	}

	void Groups_Search(ObjectID unit, SearchType type, Result result, Path * path, UnsignedInt frame)
	{
		if (!GroupsActive)
			return;
		GroupUnit * u = Find(unit);
		if (u == nullptr)
			return;
		++u->searches;
		if (type == PATCH)
			++u->repaths;
		if (result == PARTIAL)
			++u->partials;
		else if (result == FAILED || result == OUT_OF_POOL)
			++u->failed;
		if (path != nullptr)
			for (const PathNode * n = path->getFirstNode(); n != nullptr; n = n->getNext())
				++u->pathNodes;
		if (u->reroutePending && EventOpen)
		{
			u->reroutePending = FALSE;
			const Int latency = (Int)(frame - EventFrame);
			if (latency > Groups[u->group].rerouteMax)
				Groups[u->group].rerouteMax = latency;
		}
	}

	void End_Stuck(GroupUnit & u, UnsignedInt frame)
	{
		if (!u.stuck)
			return;
		u.stuck = FALSE;
		const Int length = (Int)(frame - u.stuckStart);
		if (length > 150) ++u.over5;
		if (length > 300) ++u.over10;
		if (length > u.longestStuck) u.longestStuck = length;
	}

	Bool Hull_Over_Bad_Ground(const GroupUnit & u, const Object * obj, LocomotorSurfaceTypeMask surfaces)
	{
		if (TheAI == nullptr || TheAI->pathfinder() == nullptr || obj->getLayer() != LAYER_GROUND)
			return FALSE;
		Pathfinder * pf = TheAI->pathfinder();
		const Coord3D * p = obj->getPosition();
		const Int water = obj->getAI() != nullptr ? obj->getAI()->getLocomotorSet().getRequiredWaterLevel() : 0;
		const Short bridge = obj->getRequiredBridgeHeight();
		Int x0, y0, x1, y1;
		Footprint_Box(u, p->x, p->y, &x0, &y0, &x1, &y1);
		for (Int x = x0; x <= x1; ++x)
			for (Int y = y0; y <= y1; ++y)
			{
				PathfindCell * c = pf->getCell(LAYER_GROUND, x, y);
				if (c == nullptr)
					continue;
				const PathfindCell::CellType t = c->getType();
				if ((t == PathfindCell::CELL_WATER || t == PathfindCell::CELL_CLIFF) && !pf->validMovementPosition(FALSE, surfaces, water, bridge, c))
					return TRUE;
			}
		return FALSE;
	}

	void Groups_Frame(UnsignedInt frame)
	{
		if (!GroupsActive || TheGameLogic == nullptr)
			return;
		Int stuckNow[MAX_GROUPS] = {};
		for (Int i = 0; i < UnitCount; ++i)
		{
			GroupUnit & u = GroupUnits[i];
			if (u.dead)
				continue;
			Object * obj = TheGameLogic->findObjectByID(u.id);
			if (obj == nullptr || obj->isEffectivelyDead())
			{
				u.dead = TRUE;
				End_Stuck(u, frame);
				if (u.outcome == PENDING)
				{
					u.outcome = DIED;
					u.endFrame = frame;
				}
				continue;
			}
			const AIUpdateInterface * ai = obj->getAI();
			const Coord3D * p = obj->getPosition();
			if (ai != nullptr && ai->getSupplyTruckAIInterface() != nullptr)
			{
				const Int boxes = ai->getSupplyTruckAIInterface()->getNumberBoxes();
				if (boxes < u.lastBoxes)
					++u.trips;
				u.lastBoxes = boxes;
			}
			u.inside = obj->getContainedBy() != nullptr;
			if (u.inside)
			{
				++u.insideFrames;
				if (u.outcome == PENDING && u.kind == ORDER_ENTER)
				{
					u.outcome = ARRIVED;	// an enter order arrives inside
					u.endFrame = frame;
				}
				End_Stuck(u, frame);
				u.progFrame = 0;
				u.lastX = p->x;
				u.lastY = p->y;
				u.hullNow = FALSE;
				u.atRest = FALSE;
				continue;
			}
			const Real mx = p->x - u.lastX, my = p->y - u.lastY;
			const Real step = sqrtf(mx * mx + my * my);
			u.driven += step;
			u.lastX = p->x;
			u.lastY = p->y;
			{
				const Real sx = p->x - u.spawnX, sy = p->y - u.spawnY;
				const Real d = sqrtf(sx * sx + sy * sy);
				if (d > u.maxFromSpawn) u.maxFromSpawn = d;
			}
			const Bool moving = ai != nullptr && (ai->isMoving() || ai->isWaitingForPath());
			if (moving)
				u.moved = TRUE;
			GroupInfo & g = Groups[u.group];
			if (g.firstArrival != 0 && step > 0.5f)
				g.lastMove = frame;
			// stuck: moving with less than a cell of progress for STUCK_AFTER frames
			if (moving)
			{
				const Real dx = p->x - u.progX, dy = p->y - u.progY;
				if (u.progFrame == 0 || dx * dx + dy * dy >= CELL * CELL)
				{
					End_Stuck(u, frame);
					u.progX = p->x;
					u.progY = p->y;
					u.progFrame = frame == 0 ? 1 : frame;
				}
				else if (frame - u.progFrame >= (UnsignedInt)STUCK_AFTER)
				{
					++u.stuckFrames;
					++stuckNow[u.group];
					if (!u.stuck)
					{
						u.stuck = TRUE;
						u.stuckStart = u.progFrame;
						++u.stuckEpisodes;
					}
				}
			}
			else
			{
				End_Stuck(u, frame);
				u.progFrame = 0;
			}
			if (ai != nullptr && ai->isBlockedAndStuck())
				++u.blockedFrames;
			u.hullNow = ai != nullptr && Hull_Over_Bad_Ground(u, obj, ai->getLocomotorSet().getValidSurfaces());
			if (u.hullNow)
				++u.hullFrames;
			if (ai != nullptr && u.engagedFrame == 0 && u.outcome == PENDING && ai->isAttacking())
				u.engagedFrame = frame;
			// the order's goal: the unit's own slot, as its AI holds it while moving
			if (u.outcome == PENDING && moving && ai != nullptr && ai->getGoalPosition() != nullptr)
			{
				const Coord3D * goal = ai->getGoalPosition();
				const Real gx = goal->x - u.goalX, gy = goal->y - u.goalY;
				if (u.sawGoal && gx * gx + gy * gy > CELL * CELL)
					++u.goalMoves;
				u.goalX = goal->x;
				u.goalY = goal->y;
				u.sawGoal = TRUE;
			}
			// the order's end
			u.atRest = !moving;
			if (u.outcome == PENDING && frame > u.orderFrame + 1 && !moving && ai != nullptr)
			{
				const Bool settled = (u.kind != ORDER_ATTACKMOVE || ai->isIdle())	// an attack-move ends when the fighting does,
					&& u.kind != ORDER_GATHER && u.kind != ORDER_ENTER;		// gathering never; an enter order inside
				const Bool started = u.moved || frame > u.orderFrame + 30;
				if (settled && started)
				{
					const Real tx = u.sawGoal ? u.goalX : u.clickX, ty = u.sawGoal ? u.goalY : u.clickY;
					const Real dx = p->x - tx, dy = p->y - ty;
					const Real reach = (2.0f + (Real)u.footR) * CELL;
					u.outcome = dx * dx + dy * dy <= reach * reach ? ARRIVED : ENDED_SHORT;
					u.endFrame = frame;
					const Real cx = p->x - u.clickX, cy = p->y - u.clickY;
					u.restClick = sqrtf(cx * cx + cy * cy);
					if (u.outcome == ARRIVED && g.firstArrival == 0)
					{
						g.firstArrival = frame;
						g.lastMove = frame;
					}
				}
			}
			// still 5 s after a map change
			if (EventOpen && frame == EventFrame + STILL_AFTER_EVENT && u.outcome == PENDING)
			{
				const Real dx = p->x - u.eventX, dy = p->y - u.eventY;
				if (dx * dx + dy * dy < CELL * CELL)
					++StillAfterEvent;
			}
		}
		for (Int g = 0; g < GroupCount; ++g)
			if (stuckNow[g] >= JAM_UNITS)
				++Groups[g].jamFrames;
		// big footprints touching another unit's footprint, both at rest
		for (Int i = 0; i < UnitCount; ++i)
			GroupUnits[i].overlapNow = FALSE;
		for (Int i = 0; i < UnitCount; ++i)
		{
			GroupUnit & a = GroupUnits[i];
			if (a.dead || !a.atRest || a.footN < 4)
				continue;
			Int ax0, ay0, ax1, ay1;
			Footprint_Box(a, a.lastX, a.lastY, &ax0, &ay0, &ax1, &ay1);
			for (Int j = 0; j < UnitCount; ++j)
			{
				const GroupUnit & b = GroupUnits[j];
				if (j == i || b.dead || !b.atRest)
					continue;
				Int bx0, by0, bx1, by1;
				Footprint_Box(b, b.lastX, b.lastY, &bx0, &by0, &bx1, &by1);
				if (ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1)
				{
					a.overlapNow = TRUE;
					break;
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Rows
	void Sort(Int * v, Int n)
	{
		for (Int i = 1; i < n; ++i)
		{
			const Int x = v[i];
			Int j = i - 1;
			while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; --j; }
			v[j + 1] = x;
		}
	}

	Real At(const Int * v, Int n, Real p)
	{
		if (n <= 0)
			return -1.0f;
		Int k = (Int)(p * (Real)(n - 1) + 0.5f);
		if (k < 0) k = 0;
		if (k >= n) k = n - 1;
		return (Real)v[k];
	}

	FILE * Open_Group_File(const char * suffix)
	{
		char path[600];
		snprintf(path, sizeof(path), "%s%s", Output_Base(), suffix);
		return fopen(path, "wt");
	}

	void Write_Rows(UnsignedInt frame)
	{
		if (!GroupsActive || GroupCount == 0)
			return;
		const Real fps = (Real)LOGICFRAMES_PER_SECOND;
		if (GroupsFile == nullptr)
		{
			GroupsFile = Open_Group_File(".groups.csv");
			if (GroupsFile != nullptr)
				fprintf(GroupsFile, "phase,phase_start,phase_end,group,units,alive,order,order_frame,ordered,arrived,arrived_pct,arrive_first_s,"
					"arrive_p50_s,arrive_p90_s,arrive_last_s,stopped_by_order,ended_short,superseded,died,goal_moved_twice,stuck_unit_frames,"
					"stuck_episodes,stuck_over_5s,stuck_over_10s,longest_stuck_s,blocked_unit_frames,jam_frames_10,driven_cells,straight_cells,"
					"driven_ratio,searches,repaths,partial,failed,path_nodes,never_left,hull_over_unit_frames,ended_on_bad_ground,engaged,"
					"engaged_first_s,engaged_p90_s,reroute_max_s,reroute_pending,still_5s_after_event,rest_radius_p50_cells,"
					"rest_radius_p90_cells,rest_radius_max_cells,shuffle_s,overlapping_at_rest\n");
		}
		if (UnitsFile == nullptr)
		{
			UnitsFile = Open_Group_File(".units.csv");
			if (UnitsFile != nullptr)
				fprintf(UnitsFile, "phase,group,unit,template,footprint,outcome,order,order_frame,end_frame,end_s,start_x,start_y,goal_x,goal_y,"
					"end_x,end_y,driven_cells,straight_cells,stuck_frames,stuck_episodes,longest_stuck_s,blocked_frames,hull_frames,"
					"ended_on_bad_ground,searches,repaths,partial,failed,path_nodes,goal_moves,engaged_s,rest_click_cells,trips,inside_frames\n");
		}
		for (Int g = 0; g < GroupCount; ++g)
		{
			const GroupInfo & gi = Groups[g];
			Int units = 0, alive = 0, ordered = 0, arrived = 0, stopped = 0, shortEnd = 0, superseded = 0, died = 0, moved2 = 0;
			Int stuckF = 0, stuckE = 0, o5 = 0, o10 = 0, longest = 0, blocked = 0, searches = 0, repaths = 0, partial = 0, failed = 0;
			Int nodes = 0, neverLeft = 0, hull = 0, badEnd = 0, engaged = 0, reroutePending = 0, overlap = 0, trips = 0, inside = 0;
			Real driven = 0.0f, straight = 0.0f;
			Int na = 0, ne = 0, nr = 0;
			Int arrive[512];
			Int engage[512];
			Int rest[512];
			for (Int i = 0; i < UnitCount; ++i)
			{
				const GroupUnit & u = GroupUnits[i];
				if (u.group != g)
					continue;
				++units;
				if (!u.dead) ++alive;
				if (u.outcome != NO_ORDER) ++ordered;
				switch (u.outcome)
				{
					case ARRIVED: ++arrived; if (na < 512) arrive[na++] = (Int)(u.endFrame - u.orderFrame); break;
					case STOPPED_BY_ORDER: ++stopped; break;
					case ENDED_SHORT: ++shortEnd; break;
					case DIED: ++died; break;
					default: break;
				}
				superseded += u.superseded;
				if (u.goalMoves > 1) ++moved2;
				stuckF += u.stuckFrames; stuckE += u.stuckEpisodes; o5 += u.over5; o10 += u.over10;
				if (u.longestStuck > longest) longest = u.longestStuck;
				blocked += u.blockedFrames; searches += u.searches; repaths += u.repaths; partial += u.partials; failed += u.failed;
				nodes += u.pathNodes; hull += u.hullFrames;
				if (u.maxFromSpawn < CELL) ++neverLeft;
				if (!u.dead && u.hullNow) ++badEnd;
				if (u.engagedFrame != 0) { ++engaged; if (ne < 512) engage[ne++] = (Int)(u.engagedFrame - u.orderFrame); }
				if (u.reroutePending) ++reroutePending;
				if (u.overlapNow) ++overlap;
				trips += u.trips;
				if (!u.dead && u.inside) ++inside;
				if (u.outcome != NO_ORDER)
				{
					driven += u.driven;
					const Real tx = u.sawGoal ? u.goalX : u.clickX, ty = u.sawGoal ? u.goalY : u.clickY;
					const Real dx = tx - u.startX, dy = ty - u.startY;
					straight += sqrtf(dx * dx + dy * dy);
				}
				if ((u.outcome == ARRIVED || u.outcome == ENDED_SHORT) && nr < 512)
					rest[nr++] = REAL_TO_INT(u.restClick / CELL * 10.0f);	// tenths of a cell
			}
			Sort(arrive, na); Sort(engage, ne); Sort(rest, nr);
			if (GroupsFile != nullptr)
			{
				fprintf(GroupsFile, "%s,%u,%u,%s,%d,%d,%s,%u,%d,%d,%.1f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d,%d,%.1f,%.1f,%.3f,"
					"%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%d,%d,%.1f,%.1f,%.1f,%.2f,%d,%d,%d\n",
					PhaseName, PhaseStart, frame, gi.name, units, alive, gi.ordered ? KindNames[gi.kind] : "none", gi.orderFrame, ordered,
					arrived, units > 0 ? 100.0f * arrived / units : 0.0f,
					na > 0 ? At(arrive, na, 0.0f) / fps : -1.0f, na > 0 ? At(arrive, na, 0.5f) / fps : -1.0f,
					na > 0 ? At(arrive, na, 0.9f) / fps : -1.0f, na > 0 ? At(arrive, na, 1.0f) / fps : -1.0f,
					stopped, shortEnd, superseded, died, moved2, stuckF, stuckE, o5, o10, (Real)longest / fps, blocked, gi.jamFrames,
					driven / CELL, straight / CELL, straight > 0.0f ? driven / straight : 0.0f, searches, repaths, partial, failed, nodes,
					neverLeft, hull, badEnd, engaged, ne > 0 ? At(engage, ne, 0.0f) / fps : -1.0f, ne > 0 ? At(engage, ne, 0.9f) / fps : -1.0f,
					(Real)gi.rerouteMax / fps, reroutePending, StillAfterEvent,
					nr > 0 ? At(rest, nr, 0.5f) / 10.0f : -1.0f, nr > 0 ? At(rest, nr, 0.9f) / 10.0f : -1.0f, nr > 0 ? At(rest, nr, 1.0f) / 10.0f : -1.0f,
					gi.firstArrival != 0 && gi.lastMove >= gi.firstArrival ? (Real)(gi.lastMove - gi.firstArrival) / fps : -1.0f, overlap,
					trips, inside);
			}
		}
		if (UnitsFile != nullptr)
		{
			for (Int i = 0; i < UnitCount; ++i)
			{
				const GroupUnit & u = GroupUnits[i];
				const Real tx = u.sawGoal ? u.goalX : u.clickX, ty = u.sawGoal ? u.goalY : u.clickY;
				const Real dx = tx - u.startX, dy = ty - u.startY;
				fprintf(UnitsFile, "%s,%s,%u,%s,%d,%s,%s,%u,%u,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.1f,%d,%d\n",
					PhaseName, Groups[u.group].name, (UnsignedInt)u.id, u.tmpl, u.footN, OutcomeNames[u.outcome],
					u.outcome == NO_ORDER ? "none" : KindNames[u.kind], u.orderFrame, u.endFrame,
					u.endFrame > u.orderFrame ? (Real)(u.endFrame - u.orderFrame) / fps : -1.0f,
					u.startX / CELL, u.startY / CELL, tx / CELL, ty / CELL, u.lastX / CELL, u.lastY / CELL, u.driven / CELL,
					sqrtf(dx * dx + dy * dy) / CELL, u.stuckFrames, u.stuckEpisodes, (Real)u.longestStuck / fps, u.blockedFrames,
					u.hullFrames, (!u.dead && u.hullNow) ? 1 : 0, u.searches, u.repaths, u.partials, u.failed, u.pathNodes, u.goalMoves,
					u.engagedFrame != 0 ? (Real)(u.engagedFrame - u.orderFrame) / fps : -1.0f, u.restClick / CELL, u.trips, u.insideFrames);
			}
			fflush(UnitsFile);
		}
		if (GroupsFile != nullptr)
			fflush(GroupsFile);
	}

	void Phase(const char * name, UnsignedInt frame)
	{
		if (!GroupsActive)
			return;
		Write_Rows(frame);
		for (Int i = 0; i < UnitCount; ++i)
		{
			GroupUnit & u = GroupUnits[i];
			u.stuckFrames = u.stuckEpisodes = u.over5 = u.over10 = u.longestStuck = u.blockedFrames = u.hullFrames = 0;
			u.searches = u.repaths = u.partials = u.failed = u.pathNodes = u.superseded = 0;
			u.trips = u.insideFrames = 0;
			u.driven = 0.0f;
		}
		for (Int g = 0; g < GroupCount; ++g)
			Groups[g].jamFrames = 0;
		strncpy(PhaseName, name, sizeof(PhaseName) - 1);
		PhaseName[sizeof(PhaseName) - 1] = 0;
		PhaseStart = frame;
	}

	void Groups_Begin_Match()
	{
		// the groups belong to the scenario (made before the match's first frame row): kept
	}

	void Groups_End_Match(UnsignedInt frame)
	{
		if (!GroupsActive)
			return;
		Write_Rows(frame);
		GroupsActive = FALSE;
		if (GroupsFile != nullptr) { fclose(GroupsFile); GroupsFile = nullptr; }
		if (UnitsFile != nullptr) { fclose(UnitsFile); UnitsFile = nullptr; }
	}
}
