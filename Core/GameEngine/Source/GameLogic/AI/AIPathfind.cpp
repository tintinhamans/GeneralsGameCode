/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// AIPathfind.cpp
// AI pathfinding system
// Author: Michael S. Booth, October 2001
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/PathTuning.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/AIPathfind.h"

#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/LatchRestore.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"

#include "GameClient/Line2D.h"
#include "GameClient/TerrainRoads.h"

#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#if RETAIL_COMPATIBLE_PATHFINDING
#include "GameClient/InGameUI.h"
#include "GameClient/GameText.h"
#include "Common/GameAudio.h"
#include "Common/MiscAudio.h"
#endif

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.

#define no_INTENSE_DEBUG

#define DEBUG_QPF

#ifdef INTENSE_DEBUG
#include "GameLogic/ScriptEngine.h"
#endif

#include "Common/Xfer.h"
#include "Common/XferCRC.h"

//------------------------------------------------------------------------------ Performance Timers
#include "Common/PerfMetrics.h"

//-------------------------------------------------------------------------------------------------
#include <algorithm>
#include <queue>
#include <vector>

static inline Bool IS_IMPASSABLE(PathfindCell::CellType type) {
	// Return true if cell is impassable to ground units. jba. [8/18/2003]
	if (type==PathfindCell::CELL_IMPASSABLE) {
		return true;
	}
	if (type==PathfindCell::CELL_OBSTACLE) {
		return true;
	}
	if (type==PathfindCell::CELL_BRIDGE_IMPASSABLE) {
		return true;
	}
	return false;
}


struct TCheckMovementInfo
{
	// Input
	ICoord2D					cell;
	PathfindLayerEnum layer;
	Int								radius;
	Bool							centerInCell;
	Bool							considerTransient;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	// Output
	Int								allyFixedCount;
	Bool							enemyFixed;
	Bool							allyMoving;
	Bool							allyGoal;
};

inline Int IABS(Int x) {	if (x>=0) return x; return -x;};

//-----------------------------------------------------------------------------------
static Int frameToShowObstacles;

constexpr const UnsignedInt ZONE_UPDATE_FREQUENCY = 300;
constexpr const UnsignedInt MAX_CELL_COUNT = 500;
constexpr const UnsignedInt MAX_ADJUSTMENT_CELL_COUNT = 800;
constexpr const Int PATHFIND_SLOT_MAX_RING = 12; // Lattice rings around the click that assignGroupSlots may scan
constexpr const UnsignedInt MAX_SAFE_PATH_CELL_COUNT = 2000;

constexpr const UnsignedInt PATHFIND_CELLS_PER_FRAME = 5000; // Number of cells we will search pathfinding per frame.
// TheSuperHackers @feature simonx645 16/11/2026 The pool's size, set when it is made: contraZH's 100,000 records, or
// 150,000 with "pool" (GameLogic/PathTuning.h). A search that runs out of records fails, so the size changes routes.
static UnsignedInt CELL_INFOS_TO_ALLOCATE = 100000;

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
constexpr const Int PATHFIND_SEARCH_CELL_CAP = 0x7fffffff;
constexpr const Int PATHFIND_SEARCH_CELL_MIN = 0x7fffffff;
constexpr const Int PATHFIND_FRAME_CELL_LIMIT = 0x7fffffff;
constexpr const Int PATHFIND_HIER_CELL_CAP = 0x7fffffff;
#else
constexpr const Int PATHFIND_SEARCH_CELL_CAP = 10000;  // Most cells one search may allocate
constexpr const Int PATHFIND_SEARCH_CELL_MIN = 3000;   // Every search gets at least this many
constexpr const Int PATHFIND_FRAME_CELL_LIMIT = 15000; // Frame total the per-search cap is derived from
constexpr const Int PATHFIND_HIER_CELL_CAP = 4000;     // Cells the coarse block search may allocate
#endif

// The map holds one PathfindCell per 10x10 world units, so its bitfield packing is what keeps memory in check
static_assert(sizeof(PathfindCell) == sizeof(void*) + 16, "PathfindCell bitfields no longer pack into 20 bytes");

// Counters behind the once-a-second PFSTAT log line
struct PathfindStats
{
	UnsignedInt droppedQueue;      ///< requests refused by a full queue
	UnsignedInt searches;          ///< searches started
	UnsignedInt cappedSearches;    ///< searches that returned a partial path at their cap
	UnsignedInt hierOk;            ///< coarse searches that found a corridor
	UnsignedInt hierFail;          ///< coarse searches that failed
	UnsignedInt cells;             ///< cell records released by searches
	UnsignedInt maxCellsOneSearch; ///< largest single search
	UnsignedInt zoneOverflow;      ///< cells that ran past the zone table
	UnsignedInt rawZoneHighWater;  ///< raw zone count of the last zone calculation
	UnsignedInt byType[PFS_SEARCH_COUNT];
	UnsignedInt events[PFE_EVENT_COUNT];
};
static PathfindStats s_pathfindStats;
static Bool s_statsLogging = false;
static FILE *s_statsFile = nullptr;

//-----------------------------------------------------------------------------------
PathNode::PathNode() :
	m_nextOpti(nullptr),
	m_next(nullptr),
	m_prev(nullptr),
	m_nextOptiDist2D(0),
	m_canOptimize(false),
	m_id(-1)
{
	m_nextOptiDirNorm2D.x = 0;
	m_nextOptiDirNorm2D.y = 0;
	m_pos.zero();
	m_layer = LAYER_INVALID;
}

//-----------------------------------------------------------------------------------
PathNode::~PathNode()
{
}

//-----------------------------------------------------------------------------------
void PathNode::setNextOptimized(PathNode *node)
{
	m_nextOpti = node;
	if (node)
	{
		m_nextOptiDirNorm2D.x = node->getPosition()->x - getPosition()->x;
		m_nextOptiDirNorm2D.y = node->getPosition()->y - getPosition()->y;
		m_nextOptiDist2D = m_nextOptiDirNorm2D.length();
		if (m_nextOptiDist2D == 0.0f)
		{
			//DEBUG_LOG(("Warning - Path Seg length == 0, adjusting. john a."));
			m_nextOptiDist2D = 0.01f;
		}
		m_nextOptiDirNorm2D.x /= m_nextOptiDist2D;
		m_nextOptiDirNorm2D.y /= m_nextOptiDist2D;
	}
	else
	{
		m_nextOptiDist2D = 0;
	}
}

//-----------------------------------------------------------------------------------
/// given a list, prepend this node, return new list
PathNode *PathNode::prependToList( PathNode *list )
{
	m_next = list;
	if (list)
		list->m_prev = this;
	m_prev = nullptr;
	return this;
}

//-----------------------------------------------------------------------------------
/// given a list, append this node, return new list.  slow implementation.
/// @todo optimize this
PathNode *PathNode::appendToList( PathNode *list )
{
	if (list == nullptr)
	{
		m_next = nullptr;
		m_prev = nullptr;
		return this;
	}

	PathNode *tail;
	for( tail = list; tail->m_next; tail = tail->m_next )
		;

	tail->m_next = this;
	m_prev = tail;
	m_next = nullptr;

	return list;
}

//-----------------------------------------------------------------------------------
/// given a node, append new node to this.
void PathNode::append( PathNode *newNode )
{
	newNode->m_next = this->m_next;
	newNode->m_prev = this;
	if (newNode->m_next) {
		newNode->m_next->m_prev = newNode;
	}
	this->m_next = newNode;

}

//-----------------------------------------------------------------------------------
/**
 * Compute direction vector to next node
 */
const Coord3D *PathNode::computeDirectionVector()
{
	static Coord3D dir;

	if (m_next == nullptr)
	{
		if (m_prev == nullptr)
		{
			// only one node on whole path - no direction
			dir.x = 0.0f;
			dir.y = 0.0f;
			dir.z = 0.0f;
		}
		else
		{
			// tail node - continue prior direction
			return m_prev->computeDirectionVector();
		}
	}
	else
	{
		dir.x = m_next->m_pos.x - m_pos.x;
		dir.y = m_next->m_pos.y - m_pos.y;
		dir.z = m_next->m_pos.z - m_pos.z;
	}

	return &dir;
}


//-----------------------------------------------------------------------------------
Path::Path():
m_path(nullptr),
m_pathTail(nullptr),
m_isOptimized(FALSE),
m_blockedByAlly(FALSE),
m_cpopRecentStart(nullptr),
m_cpopCountdown(MAX_CPOP),
m_cpopValid(FALSE),
m_moveUnderBridges(0U)
{
	m_cpopIn.zero();
	m_cpopOut.distAlongPath=0;
	m_cpopOut.layer = LAYER_GROUND;
	m_cpopOut.posOnPath.zero();
}

Path::~Path()
{
	PathNode *node, *nextNode;

	// delete all of the path nodes
	for( node = m_path; node; node = nextNode )
	{
		nextNode = node->getNext();
		deleteInstance(node);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Path::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void Path::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	PathNode *node = m_path;
	Int count = 0;
	while (node) {
		count++;
		node = node->getNext();
	}
	xfer->xferInt(&count);

	if (xfer->getXferMode() == XFER_SAVE)	{
		node = m_pathTail;  // Write them out backwards.
		while (node) {
			node->m_id = count;
			xfer->xferInt(&count);
			Coord3D pos = *node->getPosition();
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer = node->getLayer();
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt = node->getCanOptimize();
			xfer->xferBool(&canOpt);
			Int id = -1;
			if (node->getNextOptimized()) {
				id = node->getNextOptimized()->m_id;
			}
			xfer->xferInt(&id);
			count--;
			node = node->getPrevious();
		}
		DEBUG_ASSERTCRASH(count==0, ("Wrong data count"));
	} else {
		m_cpopValid = FALSE;
		while (count) {
			Int nodeId;
			xfer->xferInt(&nodeId);
			DEBUG_ASSERTCRASH(nodeId==count, ("Bad data"));
			Coord3D pos;
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer;
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt;
			xfer->xferBool(&canOpt);
			Int optID = -1;
			xfer->xferInt(&optID);
			PathNode *node = newInstance(PathNode);
			node->m_id = nodeId;
			node->setPosition(&pos);
			node->setLayer(layer);
			node->setCanOptimize(canOpt);
			PathNode *optNode = nullptr;
			if (optID > 0) {
				optNode = m_path;
				while (optNode && optNode->m_id != optID) {
					optNode = optNode->getNext();
				}
				DEBUG_ASSERTCRASH (optNode && optNode->m_id == optID, ("Could not find optimized link."));
			}
			m_path = node->prependToList(m_path);
			if (m_pathTail == nullptr)
				m_pathTail = node;
			if (optNode) {
				node->setNextOptimized(optNode);
			}
			count--;
		}
	}

	xfer->xferBool(&m_isOptimized);
	Int obsolete1 = 0;
	xfer->xferInt(&obsolete1);
	UnsignedInt obsolete2;
	xfer->xferUnsignedInt(&obsolete2);
	xfer->xferBool(&m_blockedByAlly);

	xfer->xferUnsignedShort(&m_moveUnderBridges);

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		addIcon(nullptr, 0, 0, color); // erase feedback.
		for( PathNode *node = getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		TheAI->pathfinder()->setDebugPath(this);
	}
#endif
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Path::loadPostProcess()
{
}

/**
 * Create a new node at the head of the path
 */
void Path::prependNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	m_path = node->prependToList( m_path );

	if (m_pathTail == nullptr)
		m_pathTail = node;

	m_isOptimized = false;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}

/**
 * Create a new node at the tail of the path
 */
void Path::appendNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	if (m_isOptimized && m_pathTail)
	{
		/* Check for duplicates. */
		if (pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y) {
			DEBUG_LOG(("Warning - Path Seg length == 0, ignoring. john a."));
			return;
		}
	}
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	m_path = node->appendToList( m_path );

	if (m_isOptimized && m_pathTail)
	{
		m_pathTail->setNextOptimized(node);
	}

	m_pathTail = node;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}
/**
 * Create a new node at the tail of the path
 */
void Path::updateLastNode( const Coord3D *pos )
{
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	if (m_pathTail) {
		m_pathTail->setPosition(pos);
		m_pathTail->setLayer(layer);
	}
	if (m_isOptimized && m_pathTail)
	{
		PathNode *node = m_path;
		while(node && node->getNextOptimized() != m_pathTail) {
			node = node->getNextOptimized();
		}
		if (node && node->getNextOptimized() == m_pathTail) {
			node->setNextOptimized(m_pathTail);
		}
	}
}

/**
 * Optimize the path by checking line of sight
 */
void Path::optimize( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, Bool blocked, Int requiredWaterLevel)
{
	PathNode *node, *anchor;

	// start with first node in the path
	anchor = getFirstNode();

	Bool firstNode = true;
	PathfindLayerEnum firstLayer = anchor->getLayer();

	// backwards.

	//
	// For each node in the path, check LOS from last node in path, working forward.
	// When a clear LOS is found, keep the resulting straight line segment.
	//
	while( anchor != getLastNode() )
	{
		// find the farthest node in the path that has a clear line-of-sight to this anchor
		Bool optimizedSegment = false;
		PathfindLayerEnum layer = anchor->getLayer();
		PathfindLayerEnum curLayer = anchor->getLayer();
		Int count = 0;
		const Int ALLOWED_STEPS = 3; // we can optimize 3 steps to or from a bridge.  Otherwise, we need to insert a point. jba.
		for (node = anchor->getNext(); node->getNext(); node=node->getNext()) {
			count++;
			if (curLayer==LAYER_GROUND) {
				if (node->getLayer() != curLayer) {
					layer = node->getLayer();
					curLayer = layer;
					if (count > ALLOWED_STEPS) break;
				}
			}	else {
				if (node->getNext()->getLayer() != curLayer) {
					if (count > ALLOWED_STEPS) break;
				}
			}
			curLayer = node->getLayer();
			if (node->getCanOptimize()==false) {
				break;
			}
		}
		if (firstNode) {
			layer = firstLayer;
			firstNode = false;
		}
		//PathfindLayerEnum curLayer = LAYER_GROUND;
		for( ; node != anchor; node = node->getPrevious() )
		{
			Bool isPassable = false;
			//CRCDEBUG_LOG(("Path::optimize() calling isLinePassable()"));
			if (TheAI->pathfinder()->isLinePassable( obj, acceptableSurfaces, layer, *anchor->getPosition(),
				*node->getPosition(), blocked, false, requiredWaterLevel))
			{
				isPassable = true;
			}
			PathfindCell* cell = TheAI->pathfinder()->getCell( layer, node->getPosition());
			if (cell && cell->getType()==PathfindCell::CELL_CLIFF && !cell->getPinched()) {
				isPassable = true;
			}
			// Horizontal, diagonal, and vertical steps are passable.
			if (!isPassable) {
				Int dx = node->getPosition()->x - anchor->getPosition()->x;
				Int dy = node->getPosition()->y - anchor->getPosition()->y;
				Bool mightBePassable = false;
				if (IABS(dx)==PATHFIND_CELL_SIZE && IABS(dy)==PATHFIND_CELL_SIZE) {
					isPassable = true;
				}
				PathNode *tmpNode;
				if (dx==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						if (dx!=0) mightBePassable = false;
					}
				}
				if (dy==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=0) mightBePassable = false;
					}
				}
				if (dx == dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=dx) mightBePassable = false;
					}
				}
				if (dx == -dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=-dx) mightBePassable = false;
					}
				}
				if (mightBePassable) {
					isPassable = true;
				}
			}
			if (isPassable)
			{
				// anchor can directly see this node, make it next in the optimized path
				anchor->setNextOptimized( node );
				anchor = node;
				optimizedSegment = true;
				break;
			}
		}

		if (optimizedSegment == false)
		{
			// for some reason, there is no clear LOS between the anchor node and the very next node
			anchor->setNextOptimized( anchor->getNext() );
			anchor = anchor->getNext();
		}
	}

	// the path has been optimized
	m_isOptimized = true;
}

/**
 * Optimize the path by checking line of sight
 */
void Path::optimizeGroundPath( Bool crusher, Int pathDiameter )
{
	PathNode *node, *anchor;

	// start with first node in the path
	anchor = getFirstNode();

	//
	// For each node in the path, check LOS from last node in path, working forward.
	// When a clear LOS is found, keep the resulting straight line segment.
	//
	while( anchor != getLastNode() )
	{
		// find the farthest node in the path that has a clear line-of-sight to this anchor
		Bool optimizedSegment = false;
		PathfindLayerEnum layer = anchor->getLayer();
		PathfindLayerEnum curLayer = anchor->getLayer();
		Int count = 0;
		const Int ALLOWED_STEPS = 3; // we can optimize 3 steps to or from a bridge.  Otherwise, we need to insert a point. jba.
		for (node = anchor->getNext(); node->getNext(); node=node->getNext()) {
			count++;
			if (curLayer==LAYER_GROUND) {
				if (node->getLayer() != curLayer) {
					layer = node->getLayer();
					curLayer = layer;
					if (count > ALLOWED_STEPS) break;
				}
			}	else {
				if (node->getNext()->getLayer() != curLayer) {
					if (count > ALLOWED_STEPS) break;
				}
			}
			curLayer = node->getLayer();
		}

		// find the farthest node in the path that has a clear line-of-sight to this anchor
		for( ; node != anchor; node = node->getPrevious() )
		{
			Bool isPassable = false;
			//CRCDEBUG_LOG(("Path::optimize() calling isLinePassable()"));
			if (TheAI->pathfinder()->isGroundPathPassable( crusher, *anchor->getPosition(), layer,
				*node->getPosition(), pathDiameter))
			{
				isPassable = true;
			}
			// Horizontal, diagonal, and vertical steps are passable.
			if (!isPassable) {
				Int dx = node->getPosition()->x - anchor->getPosition()->x;
				Int dy = node->getPosition()->y - anchor->getPosition()->y;
				Bool mightBePassable = false;
				PathNode *tmpNode;
				if (dx==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						if (dx!=0) mightBePassable = false;
					}
				}
				if (dy==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=0) mightBePassable = false;
					}
				}
				if (dx == dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=dx) mightBePassable = false;
					}
				}
				if (dx == -dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x;
						dy = tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y;
						if (dy!=-dx) mightBePassable = false;
					}
				}
				if (mightBePassable) {
					isPassable = true;
				}
			}
			if (isPassable)
			{
				// anchor can directly see this node, make it next in the optimized path
				anchor->setNextOptimized( node );
				anchor = node;
				optimizedSegment = true;
				break;
			}
		}

		if (optimizedSegment == false)
		{
			// for some reason, there is no clear LOS between the anchor node and the very next node
			anchor->setNextOptimized( anchor->getNext() );
			anchor = anchor->getNext();
		}
	}

	// Remove jig/jogs :) jba.
	for (anchor=getFirstNode(); anchor!=nullptr; anchor=anchor->getNextOptimized()) {
		node = anchor->getNextOptimized();
		if (node && node->getNextOptimized()) {
			Real dx = node->getPosition()->x - anchor->getPosition()->x;
			Real dy = node->getPosition()->y - anchor->getPosition()->y;
			// If the x & y offsets are less than 2 pathfind cells, kill it.
			if (dx*dx+dy*dy < sqr(PATHFIND_CELL_SIZE_F)*3.9f) {
				anchor->setNextOptimized(node->getNextOptimized());
			}
		}
	}

	// the path has been optimized
	m_isOptimized = true;
}

inline Bool isReallyClose(const Coord3D& a, const Coord3D& b)
{
	const Real CLOSE_ENOUGH = 0.1f;
	return
		WWMath::Fabs(a.x-b.x) <= CLOSE_ENOUGH &&
		WWMath::Fabs(a.y-b.y) <= CLOSE_ENOUGH &&
		WWMath::Fabs(a.z-b.z) <= CLOSE_ENOUGH;
}

/**
 * Given a location, return the closest position on the path.
 * If 'allowBacktrack' is true, the entire path is considered.
 * If it is false, the point computed cannot be prior to previously returned non-backtracking points on this path.
 * Because the path "knows" the direction of travel, it will "lead" the given position a bit
 * to ensure the path is followed in the intended direction.
 *
 * Note: The path cleanup does not take into account rolling terrain, so we can end up with
 * these situations:
 *
 *           B
 *        ######
 *      ##########
 *  A-##----------##---C
 * #######################
 *
 *
 * When an agent gets to B, he seems far off of the path, but it really not.
 * There are similar problems with valleys.
 *
 * Since agents track the closest path, if a high hill gets close to the underside of
 * a bridge, an agent may 'jump' to the higher path.  This must be avoided in maps.
 *
 * return along-path distance to the end will be returned as function result
 */
void Path::computePointOnPath(
	const Object* obj,
	const LocomotorSet& locomotorSet,
	const Coord3D& pos,
	ClosestPointOnPathInfo& out
)
{
	CRCDEBUG_LOG(("Path::computePointOnPath() for %s", DebugDescribeObject(obj).str()));

	out.layer = LAYER_GROUND;
	out.posOnPath.zero();
	out.distAlongPath = 0;

	if (m_path == nullptr)
	{
		m_cpopValid = false;
		return;
	}
	out.layer = m_path->getLayer();

	if (m_cpopValid && m_cpopCountdown>0 && isReallyClose(pos, m_cpopIn))
	{
		out = m_cpopOut;
		m_cpopCountdown--;
		CRCDEBUG_LOG(("Path::computePointOnPath() end because we're really close"));
		return;
	}
	m_cpopCountdown = MAX_CPOP;

	// default pathPos to end of the path
	out.posOnPath = *getLastNode()->getPosition();

	const PathNode* closeNode = nullptr;
	Coord2D toPos;
	Real closeDistSqr = 99999999.9f;
	Real totalPathLength = 0.0f;
	Real lengthAlongPathToPos = 0.0f;

	//
	// Find the closest segment of the path
	//
#ifdef CPOP_STARTS_FROM_PREV_SEG
	const PathNode* prevNode = m_cpopRecentStart;
	if (prevNode == nullptr)
		prevNode = m_path;
#else
	const PathNode* prevNode = m_path;
#endif
	Coord2D segmentDirNorm;
	Real segmentLength;

	// note that the seg dir and len returned by this is the dist & vec from 'prevNode' to 'node'
	for ( const PathNode* node = prevNode->getNextOptimized(&segmentDirNorm, &segmentLength);
				node != nullptr;
				node = node->getNextOptimized(&segmentDirNorm, &segmentLength) )
	{
		const Coord3D* prevNodePos = prevNode->getPosition();
		const Coord3D* nodePos = node->getPosition();

		// compute vector from start of segment to pos
		toPos.x = pos.x - prevNodePos->x;
		toPos.y = pos.y - prevNodePos->y;

		// compute distance projection of 'toPos' onto segment
		Real alongPathDist = segmentDirNorm.x * toPos.x + segmentDirNorm.y * toPos.y;

		Coord3D pointOnPath;
		if (alongPathDist < 0.0f)
		{
			// projected point is before start of segment, use starting point
			alongPathDist = 0.0f;
			pointOnPath = *prevNodePos;
		}
		else if (alongPathDist > segmentLength)
		{
			// projected point is beyond end of segment, use end point
			if (node->getNextOptimized() == nullptr)
			{
				alongPathDist = segmentLength;
				pointOnPath = *nodePos;
			}
			else
			{
				// beyond the end of this segment, skip this segment
				// if bend is sharp, start of next segment will grab this point
				// if bend is gradual, the point will project into the next segment
				totalPathLength += segmentLength;
				prevNode = node;
				continue;
			}
		}
		else
		{
			// projected point is on this segment, compute it
			pointOnPath.x = prevNodePos->x + alongPathDist * segmentDirNorm.x;
			pointOnPath.y = prevNodePos->y + alongPathDist * segmentDirNorm.y;
			pointOnPath.z = 0;
		}

		// compute distance to point on path, and track the closest we've found so far
		Coord2D offset;
		offset.x = pos.x - pointOnPath.x;
		offset.y = pos.y - pointOnPath.y;

		Real offsetDistSqr = offset.x*offset.x + offset.y*offset.y;
		if (offsetDistSqr < closeDistSqr)
		{
			closeDistSqr = offsetDistSqr;
			closeNode = prevNode;
			out.posOnPath = pointOnPath;

			lengthAlongPathToPos = totalPathLength + alongPathDist;
		}

		// add this segment's length to find total path length
		/// @todo Precompute this and store in path
		totalPathLength += segmentLength;
		prevNode = node;
		DUMPCOORD3D(&pointOnPath);
	}

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = closeNode;
#endif

	//
	// Compute the goal movement position for this agent
	//
	if (closeNode && closeNode->getNextOptimized())
	{
		// note that the seg dir and len returned by this is the dist & vec from 'closeNode' to 'closeNext'
		const PathNode* closeNext = closeNode->getNextOptimized(&segmentDirNorm, &segmentLength);
		const Coord3D* nextNodePos = closeNext->getPosition();
		const Coord3D* closeNodePos = closeNode->getPosition();

		const PathNode* closePrev = closeNode->getPrevious();
		if (closePrev && closePrev->getLayer() > LAYER_GROUND)
		{
			out.layer = closeNode->getLayer();
		}
		if (closeNode->getLayer() > LAYER_GROUND)
		{
			out.layer = closeNode->getLayer();
		}

		if (closeNext->getLayer() > LAYER_GROUND)
		{
			out.layer = closeNext->getLayer();
		}

		// compute vector from start of segment to pos
		toPos.x = pos.x - closeNodePos->x;
		toPos.y = pos.y - closeNodePos->y;

		// compute distance projection of 'toPos' onto segment
		Real alongPathDist = segmentDirNorm.x * toPos.x + segmentDirNorm.y * toPos.y;

		// we know this is the closest segment, so don't allow farther back than the start node
		if (alongPathDist < 0.0f)
			alongPathDist = 0.0f;

		// compute distance of point from this path segment
		Real toDistSqr = sqr(toPos.x) + sqr(toPos.y);
		Real offsetDistSq = toDistSqr - sqr(alongPathDist);
		Real offsetDist = (offsetDistSq <= 0.0) ? 0.0 : WWMath::Sqrt(offsetDistSq);

		// If we are basically on the path, return the next path node as the movement goal.
		// However, the farther off the path we get, the movement goal becomes closer to our
		// projected position on the path.  If we are very far off the path, we will move
		// directly towards the nearest point on the path, and not the next path node.
		const Real maxPathError = 3.0f * PATHFIND_CELL_SIZE_F;
		const Real maxPathErrorInv = 1.0f / maxPathError;
		Real k = offsetDist * maxPathErrorInv;
		if (k > 1.0f)
			k = 1.0f;

		Bool gotPos = false;
		CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 1"));

		if (TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), out.layer, pos, *nextNodePos,
			false, true, locomotorSet.getRequiredWaterLevel() ))
		{
			out.posOnPath = *nextNodePos;
			gotPos = true;

			Bool tryAhead = alongPathDist > segmentLength * 0.5;
			if (closeNext->getCanOptimize() == false)
			{
				tryAhead = false; // don't go past no-opt nodes.
			}
			if (closeNode->getLayer() != closeNext->getLayer())
			{
				tryAhead = false; // don't go past layers.
			}
			if (obj->getLayer()!=LAYER_GROUND) {
				tryAhead = false;
			}
			Bool veryClose = false;
			if (segmentLength-alongPathDist<1.0f) {
				tryAhead = true;
				veryClose = true;
			}
			if (tryAhead)
			{
				// try next segment middle.
				const PathNode *next = closeNext->getNextOptimized();
				if (next)
				{
					Coord3D tryPos;
					tryPos.x = (nextNodePos->x + next->getPosition()->x) * 0.5;
					tryPos.y = (nextNodePos->y + next->getPosition()->y) * 0.5;
					tryPos.z = nextNodePos->z;
					CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 2"));
					if (veryClose || TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), closeNext->getLayer(), pos, tryPos, false, true, locomotorSet.getRequiredWaterLevel()))
					{
						gotPos = true;
						out.posOnPath = tryPos;
					}
				}
			}
		}
		else if (k > 0.5f)
		{
			Real tryDist = alongPathDist + (0.5) * (segmentLength - alongPathDist);

			// projected point is on this segment, compute it
			out.posOnPath.x = closeNodePos->x + tryDist * segmentDirNorm.x;
			out.posOnPath.y = closeNodePos->y + tryDist * segmentDirNorm.y;
			out.posOnPath.z = closeNodePos->z;

			CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 3"));
			if (TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), out.layer, pos, out.posOnPath, false, true, locomotorSet.getRequiredWaterLevel()))
			{
				k = 0.5f;
				gotPos = true;
			}
		}

		// if we are on the path (k == 0), then alongPathDist == segmentLength
		// if we are way off the path (k == 1), then alongPathDist is unchanged, and it projection of actual pos
		alongPathDist += (1.0f - k) * (segmentLength - alongPathDist);

		if (!gotPos)
		{
			if (alongPathDist > segmentLength)
			{
				alongPathDist = segmentLength;
				out.posOnPath = *nextNodePos;
			}
			else
			{
				// projected point is on this segment, compute it
				out.posOnPath.x = closeNodePos->x + alongPathDist * segmentDirNorm.x;
				out.posOnPath.y = closeNodePos->y + alongPathDist * segmentDirNorm.y;
				out.posOnPath.z = closeNodePos->z;
				Real dx = WWMath::Fabs(pos.x - out.posOnPath.x);
				Real dy = WWMath::Fabs(pos.y - out.posOnPath.y);
				if (dx<1 && dy<1 && closeNode->getNextOptimized() && closeNode->getNextOptimized()->getNextOptimized()) {
					out.posOnPath = *closeNode->getNextOptimized()->getNextOptimized()->getPosition();
				}
			}
		}
	}

	TheAI->pathfinder()->setDebugPathPosition( &out.posOnPath );

	out.distAlongPath = totalPathLength - lengthAlongPathToPos;

	Coord3D delta;
	delta.x = out.posOnPath.x - pos.x;
	delta.y = out.posOnPath.y - pos.y;
	delta.z = 0;
	Real lenDelta = delta.length();
	if (lenDelta > out.distAlongPath && out.distAlongPath > PATHFIND_CLOSE_ENOUGH)
	{
		out.distAlongPath = lenDelta;
	}

	m_cpopIn = pos;
	m_cpopOut = out;
	m_cpopValid = true;
	CRCDEBUG_LOG(("Path::computePointOnPath() end"));

}


/**
	Given a position, computes the distance to the goal.  Returns 0 if we are past the goal.
	Returns the goal position in goalPos.  This is intended for use with flying paths, that go
	directly to the goal and don't consider obstacles.  jba.
 */
Real Path::computeFlightDistToGoal( const Coord3D *pos, Coord3D& goalPos )
{
	if (m_path == nullptr)
	{
		goalPos.x = 0.0f;
		goalPos.y = 0.0f;
		goalPos.z = 0.0f;
		return 0.0f;
	}
	const PathNode *curNode = getFirstNode();
	if (m_cpopRecentStart) {
		curNode = m_cpopRecentStart;
	} else {
		m_cpopRecentStart = curNode;
	}
	const PathNode *nextNode = curNode->getNextOptimized();
	goalPos = *curNode->getPosition();
	Real distance = 0;
	Bool useNext = true;
	while (nextNode) {

		if (useNext) {
			goalPos = *nextNode->getPosition();
		}

		Coord3D startPos = *curNode->getPosition();
		Coord3D endPos = *nextNode->getPosition();

		Coord2D posToGoalVector;
		// posToGoalVector is pos to goalPos vector.
		posToGoalVector.x = endPos.x - pos->x;
		posToGoalVector.y = endPos.y - pos->y;

		// pathVector is the startPos to goal pos vector.
		Coord2D pathVector;
		pathVector.x = endPos.x - startPos.x;
		pathVector.y = endPos.y - startPos.y;

		// Normalize pathVector
		pathVector.normalize();

		// Dot product is the posToGoal vector projected onto the path vector.
		Real dotProduct = posToGoalVector.x*pathVector.x	+ posToGoalVector.y*pathVector.y;
		if (dotProduct>=0) {
			distance += dotProduct;
			useNext = false;
		}	else if (useNext) {
			m_cpopRecentStart = nextNode;
		}
		curNode = nextNode;
		nextNode = curNode->getNextOptimized();
	}
	return distance;

}
//-----------------------------------------------------------------------------------

PathfindCellInfo *PathfindCellInfo::s_infoArray = nullptr;
PathfindCellInfo *PathfindCellInfo::s_firstFree = nullptr;
Int PathfindCellInfo::s_freeCount = 0;

#if RETAIL_COMPATIBLE_PATHFINDING
// TheSuperHackers @info This variable is here so the code will run down the retail compatible path till a failure mode is hit
// The pathfinding will then switch over to the corrected pathfinding code for SH clients
Bool s_useFixedPathfinding = false;
Bool s_forceCleanCells = false;

void PathfindCellInfo::forceCleanPathFindCellInfos()
{
	for (Int i = 0; i < CELL_INFOS_TO_ALLOCATE - 1; i++) {
		s_infoArray[i].m_nextOpen = nullptr;
		s_infoArray[i].m_prevOpen = nullptr;
		s_infoArray[i].m_open = FALSE;
		s_infoArray[i].m_closed = FALSE;
	}
}

void Pathfinder::forceCleanCells()
{
	UnicodeString pathfinderFailoverMessage = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:PathfindingCrashPrevented", L"A pathfinding crash was prevented at frame %u, now switching to the crash fixed pathfinding.", TheGameLogic->getFrame());
	TheInGameUI->message(pathfinderFailoverMessage);

	printf("%ls\n", pathfinderFailoverMessage.str());

	TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_allCheerSound);

	PathfindCellInfo::forceCleanPathFindCellInfos();
	m_openList.reset();
	m_closedList.reset();

	for (int j = 0; j <= m_extent.hi.y; ++j) {
		for (int i = 0; i <= m_extent.hi.x; ++i) {
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
			// TheSuperHackers @bugfix Mauller/DrGoldFish 20/01/2026 when pathfinding resources cannot be allocated to a pathfindCell,
			// The function to remove an obstacle returns early and PathfindCells remain flagged as obstacles.
			// We need to make sure to reset pathfindCells with a set m_obstacleID and no m_info.
			// The use of PathfindCellInfo data for obstacle handling also exausted them resulting in pathfinding lockups.
			if (m_map[i][j].isObstructionInvalid()) {
				m_map[i][j].clearObstruction();
			}
#endif
			if (m_map[i][j].hasInfo()) {
				m_map[i][j].releaseInfo();
			}
		}
	}
}
#endif

/**
 * Allocates a pool of pathfind cell infos.
 */
void PathfindCellInfo::allocateCellInfos()
{
	releaseCellInfos();
	CELL_INFOS_TO_ALLOCATE = PathTuning::Pool ? PathTuning::POOL_CELLS : 100000;
	s_infoArray = MSGNEW("PathfindCellInfo") PathfindCellInfo[CELL_INFOS_TO_ALLOCATE];	// pool[]ify
	s_infoArray[CELL_INFOS_TO_ALLOCATE-1].m_pathParent = nullptr;
	s_infoArray[CELL_INFOS_TO_ALLOCATE-1].m_isFree = true;
	s_firstFree = s_infoArray;
	for (Int i=0; i<CELL_INFOS_TO_ALLOCATE-1; i++) {
		s_infoArray[i].m_pathParent = &s_infoArray[i+1];
		s_infoArray[i].m_isFree = true;
	}
	s_freeCount = CELL_INFOS_TO_ALLOCATE;
}

/**
 * Releases a pool of pathfind cell infos.
 */
void PathfindCellInfo::releaseCellInfos()
{
	if (s_infoArray==nullptr) {
		return; // haven't allocated any yet.
	}
	Int count=0;
	while (s_firstFree) {
		count++;
		DEBUG_ASSERTCRASH(s_firstFree->m_isFree, ("Should be freed."));
		s_firstFree = s_firstFree->m_pathParent;
	}
	DEBUG_ASSERTCRASH(count==CELL_INFOS_TO_ALLOCATE, ("Error - Allocated cellinfos."));
	delete[] s_infoArray;
	s_infoArray = nullptr;
	s_firstFree = nullptr;
	s_freeCount = 0;
}

/**
 * Gets a pathfindcellinfo.
 */
PathfindCellInfo *PathfindCellInfo::getACellInfo(PathfindCell *cell,const ICoord2D &pos)
{
	PathfindCellInfo *info = s_firstFree;
	if (s_firstFree) {
		DEBUG_ASSERTCRASH(s_firstFree->m_isFree, ("Should be freed."));
		s_firstFree = s_firstFree->m_pathParent;
		s_freeCount--;
		info->m_isFree = false;  // Just allocated it.
		info->m_cell = cell;
		info->m_pos = pos;

		info->m_nextOpen = nullptr;
		info->m_prevOpen = nullptr;
		info->m_pathParent = nullptr;
		info->m_costSoFar = 0;
		info->m_totalCost = 0;
		info->m_open = 0;
		info->m_closed = 0;
		info->m_obstacleID = INVALID_ID;
		info->m_goalUnitID = INVALID_ID;
		info->m_posUnitID = INVALID_ID;
		info->m_goalAircraftID = INVALID_ID;
		info->m_obstacleIsFence = false;
		info->m_obstacleIsTransparent = false;
		info->m_blockedByAlly = false;
	}
	return info;
}

/**
 * Returns a pathfindcellinfo.
 */
void PathfindCellInfo::releaseACellInfo(PathfindCellInfo *theInfo)
{
	DEBUG_ASSERTCRASH(!theInfo->m_isFree, ("Shouldn't be free."));
	//@ todo -fix this assert on usa04.  jba.
	//DEBUG_ASSERTCRASH(theInfo->m_obstacleID==0, ("Shouldn't be obstacle."));
	theInfo->m_pathParent = s_firstFree;
	s_firstFree = theInfo;
	s_firstFree->m_isFree = true;
	s_freeCount++;
}

//-----------------------------------------------------------------------------------

Bool PathfindCellList::canReverseSort(PathfindCell& currentCell) const
{
	if (m_head && m_tail)
		return m_head->getTotalCostDifference(currentCell) > m_tail->getTotalCostDifference(currentCell);

	return false;
}

//-----------------------------------------------------------------------------------

/**
 * Constructor
 */
PathfindCell::PathfindCell() :m_info(nullptr)
{
	reset();
}

/**
 * Destructor
 */
PathfindCell::~PathfindCell()
{
	if (m_info) PathfindCellInfo::releaseACellInfo(m_info);
	m_info = nullptr;
	static Bool warn = true;
	if (warn) {
		warn = false;
		DEBUG_LOG( ("PathfindCell::~PathfindCell m_info Allocated."));
	}
}

/**
 * Reset the cell to default values
 */
void PathfindCell::reset()
{
	m_type = PathfindCell::CELL_CLEAR;
	m_flags = PathfindCell::NO_UNITS;
	m_zone = 0;
	m_aircraftGoal = false;
	m_pinched = false;
	m_clearance = 0;
	if (m_info) {
		m_info->m_obstacleID = INVALID_ID;
		PathfindCellInfo::releaseACellInfo(m_info);
		m_info = nullptr;
	}
	m_obstacleID = INVALID_ID;
	m_blockedByAlly = false;
	m_obstacleIsFence = false;
	m_obstacleIsTransparent = false;

	m_connectsToLayer = LAYER_INVALID;
	m_layer = LAYER_GROUND;
	m_waterLevel = 0;
	m_bridgeHeight = 0;
	m_bridgeLayer = 0;
}

/**
 * Reset the pathfinding values in the cell.
 */
Bool PathfindCell::startPathfind( PathfindCell *goalCell  )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;
	m_info->m_pathParent = nullptr;
	m_info->m_costSoFar = 0;		// start node, no cost to get here
	m_info->m_totalCost = 0;
	if (goalCell) {
		m_info->m_totalCost = costToGoal( goalCell );
	}
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_info->m_open = TRUE;
	} else
#endif
	{
		m_info->m_open = FALSE;
	}
	m_info->m_closed = FALSE;
	return true;
}

/**
 * Set the blocked by ally flag on the pathfind cell info.
 */
inline Bool PathfindCell::isBlockedByAlly() const
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		return m_blockedByAlly;
	}

	return m_info->m_blockedByAlly;
#else
	return m_blockedByAlly;
#endif
}

inline void PathfindCell::setBlockedByAlly(Bool blocked)
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		m_blockedByAlly = (blocked != 0);
		return;
	}

	m_info->m_blockedByAlly = (blocked != 0);
#else
	m_blockedByAlly = (blocked != 0);
#endif
}

/**
 * Determine absolute total path cost difference between two cells.
 * Returns UINT_MAX if used with an uninitialised cell, so will be sorted as maximally dissimilar.
 */
inline UnsignedInt PathfindCell::getTotalCostDifference(PathfindCell& other) const
{
	if (m_info && other.m_info)
		return abs((Int)m_info->m_totalCost - (Int)other.m_info->m_totalCost);

	return UINT_MAX;
}

/**
 * Set the parent pointer.
 */
void PathfindCell::setParentCell( PathfindCell* parent  )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = parent->m_info;
	Int dx = m_info->m_pos.x - parent->m_info->m_pos.x;
	Int dy = m_info->m_pos.y - parent->m_info->m_pos.y;
	if (dx<-1 || dx>1 || dy<-1 || dy>1) {
		DEBUG_CRASH(("Invalid parent index."));
	}
}

/**
 * Set the parent pointer.
 */
void PathfindCell::setParentCellHierarchical( PathfindCell* parent  )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = parent->m_info;
}

/**
 * Reset the parent cell.
 */
void PathfindCell::clearParentCell(  )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = nullptr;
}


/**
 * Allocates an info record for a cell.
 */
Bool PathfindCell::allocateInfo( const ICoord2D &pos )
{
	if (!m_info) {
		m_info = PathfindCellInfo::getACellInfo(this, pos);
		return (m_info != nullptr);
	}
	return true;
}

/**
 * Releases an info record for a cell.
 */
void PathfindCell::releaseInfo()
{
	// TheSuperHackers @bugfix Mauller/SkyAero 05/06/2025 Parent cell links need clearing to prevent dangling pointers on starting points that can link them to an invalid parent cell.
	// Parent cells are only cleared within Pathfinder::prependCells, so cells that do not make it onto the final path do not get their parent cell cleared.
	// Cells with a special flags also do not get their PathfindCellInfo cleared and therefore can leave a parent cell set on a starting cell.
#if RETAIL_COMPATIBLE_PATHFINDING
	if (s_useFixedPathfinding)
#endif
	{
		if (m_info) {
			m_info->m_pathParent = nullptr;
		}
	}

	if (m_type == PathfindCell::CELL_OBSTACLE || m_flags != NO_UNITS || m_aircraftGoal) {
		return;
	}

	if (!m_info) {
		return;
	}

	DEBUG_ASSERTCRASH(m_info->m_prevOpen==nullptr && m_info->m_nextOpen==nullptr, ("Shouldn't be linked."));
	DEBUG_ASSERTCRASH(m_info->m_open==0 && m_info->m_closed==0, ("Shouldn't be linked."));
	DEBUG_ASSERTCRASH(m_info->m_goalUnitID==INVALID_ID && m_info->m_posUnitID==INVALID_ID, ("Shouldn't be occupied."));
	DEBUG_ASSERTCRASH(m_info->m_goalAircraftID==INVALID_ID , ("Shouldn't be occupied by aircraft."));
	if (m_info->m_prevOpen || m_info->m_nextOpen || m_info->m_open || m_info->m_closed) {
		// Bad release.  Skip for now, better leak than crash.  jba.
		return;
	}

	PathfindCellInfo::releaseACellInfo(m_info);
	m_info = nullptr;

}

/**
 * Sets the goal unit into the info record for a cell.
 */
void PathfindCell::setGoalUnit(ObjectID unitID, const ICoord2D &pos )
{
	if (unitID==INVALID_ID) {
		// removing goal.
		if (m_info) {
			m_info->m_goalUnitID = INVALID_ID;
			if (m_info->m_posUnitID == INVALID_ID) {
				// No units here.
				DEBUG_ASSERTCRASH(m_flags==UNIT_GOAL, ("Bad flags."));
				m_flags = NO_UNITS;
				releaseInfo();
			} else{
				m_flags = UNIT_PRESENT_MOVING;
			}
		}	else {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba."));
			return;
		}
		m_info->m_goalUnitID = unitID;
		if (unitID==m_info->m_posUnitID) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (m_info->m_posUnitID==INVALID_ID) {
			m_flags = UNIT_GOAL;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


/**
 * Sets the goal aircraft into the info record for a cell.
 */
void PathfindCell::setGoalAircraft(ObjectID unitID, const ICoord2D &pos )
{
	if (unitID==INVALID_ID) {
		// removing goal.
		if (m_info) {
			m_info->m_goalAircraftID = INVALID_ID;
			m_aircraftGoal = false;
			releaseInfo();
		}	else {
			DEBUG_ASSERTCRASH(m_aircraftGoal==false, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_aircraftGoal==false, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba."));
			return;
		}
		m_info->m_goalAircraftID = unitID;
		m_aircraftGoal = true;
	}
}


/**
 * Sets the position unit into the info record for a cell.
 */
void PathfindCell::setPosUnit(ObjectID unitID, const ICoord2D &pos )
{
	if (unitID==INVALID_ID) {
		// removing position.
		if (m_info) {
			m_info->m_posUnitID = INVALID_ID;
			if (m_info->m_goalUnitID == INVALID_ID) {
				// No units here.
				DEBUG_ASSERTCRASH(m_flags==UNIT_PRESENT_MOVING, ("Bad flags."));
				m_flags = NO_UNITS;
				releaseInfo();
			}	else {
				m_flags = UNIT_GOAL;
			}
		}	else {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba."));
			return;
		}
		if (m_info->m_goalUnitID!=INVALID_ID && (m_info->m_goalUnitID==m_info->m_posUnitID)) {
			// A unit is already occupying this cell.
			return;
		}
		m_info->m_posUnitID = unitID;
		if (unitID==m_info->m_goalUnitID) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (m_info->m_goalUnitID==INVALID_ID) {
			m_flags = UNIT_PRESENT_MOVING;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


/**
 * Return the relevant obstacle ID.
 */
inline ObjectID PathfindCell::getObstacleID() const
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		return m_obstacleID;
	}

	return m_info ? m_info->m_obstacleID : INVALID_ID;
#else
	return m_obstacleID;
#endif
}


/**
 * Flag this cell as an obstacle, from the given one.
 * Return true if cell was flagged.
 */
Bool PathfindCell::setTypeAsObstacle( Object *obstacle, Bool isFence, const ICoord2D &pos )
{
	// a structure built over rubble claims its cells, so removing the rubble later leaves them alone
	if (m_type!=PathfindCell::CELL_CLEAR && m_type != PathfindCell::CELL_IMPASSABLE && m_type != PathfindCell::CELL_RUBBLE) {
		return false;
	}

	Bool isRubble = false;
	if (obstacle->getBodyModule() && obstacle->getBodyModule()->getDamageState() == BODY_RUBBLE)
	{
		isRubble = true;
	}

	if (isRubble) {
		m_type = PathfindCell::CELL_RUBBLE;
		m_obstacleID = INVALID_ID;
		m_obstacleIsFence = false;
		m_obstacleIsTransparent = false;
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
		if (s_useFixedPathfinding) {
			return true;
		}

		if (m_info) {
			m_info->m_obstacleID = INVALID_ID;
			releaseInfo();
		}
#endif
		return true;
	}

	m_type = PathfindCell::CELL_OBSTACLE;
	m_obstacleID = obstacle->getID();
	m_obstacleIsFence = isFence;
	m_obstacleIsTransparent = obstacle->isKindOf(KINDOF_CAN_SEE_THROUGH_STRUCTURE);
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	// TheSuperHackers @info In retail mode we need to track orphaned cells set as obstacles so we can cleanup and failover properly
	// So we always make sure to set and clear the local obstacle data on the PathfindCell regardless of retail compat or not
	if (s_useFixedPathfinding) {
		return true;
	}

	if (!m_info) {
		m_info = PathfindCellInfo::getACellInfo(this, pos);
		if (!m_info) {
			DEBUG_CRASH(("Not enough PathFindCellInfos in pool."));
			return false;
		}
	}
	m_info->m_obstacleID = obstacle->getID();
	m_info->m_obstacleIsFence = isFence;
	m_info->m_obstacleIsTransparent = obstacle->isKindOf(KINDOF_CAN_SEE_THROUGH_STRUCTURE);
#endif
	return true;
}

/**
 * Flag this cell as given type.
 */
void PathfindCell::setType( CellType type )
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		if (m_obstacleID != INVALID_ID) {
			DEBUG_ASSERTCRASH(type == PathfindCell::CELL_OBSTACLE, ("Wrong type."));
			m_type = PathfindCell::CELL_OBSTACLE;
			return;
		}
	}

	if (m_info && (m_info->m_obstacleID != INVALID_ID)) {
		DEBUG_ASSERTCRASH(type==PathfindCell::CELL_OBSTACLE, ("Wrong type."));
		m_type = PathfindCell::CELL_OBSTACLE;
		return;
	}
#else
	if (m_obstacleID != INVALID_ID) {
		DEBUG_ASSERTCRASH(type == PathfindCell::CELL_OBSTACLE, ("Wrong type."));
		m_type = PathfindCell::CELL_OBSTACLE;
		return;
	}
#endif
	m_type = type;
}

/**
 * Unflag this cell as an obstacle, from the given one.
 * Return true if this cell was previously flagged as an obstacle by this object.
 */
Bool PathfindCell::removeObstacle( Object *obstacle )
{
	if (m_type == PathfindCell::CELL_RUBBLE) {
		m_type = PathfindCell::CELL_CLEAR;
	}
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		if (m_obstacleID != obstacle->getID()) return false;
		m_type = PathfindCell::CELL_CLEAR;
		m_obstacleID = INVALID_ID;
		m_obstacleIsFence = false;
		m_obstacleIsTransparent = false;
		return true;
	}

	if (!m_info) return false;
	if (m_info->m_obstacleID != obstacle->getID()) return false;
	m_type = PathfindCell::CELL_CLEAR;
	m_info->m_obstacleID = INVALID_ID;
	releaseInfo();

#else
	if (m_obstacleID != obstacle->getID()) return false;
	m_type = PathfindCell::CELL_CLEAR;
#endif
	m_obstacleID = INVALID_ID;
	m_obstacleIsFence = false;
	m_obstacleIsTransparent = false;
	return true;
}

#if RETAIL_COMPATIBLE_PATHFINDING
// Insertion sort with the retail crash guards
void PathfindCell::forwardInsertionSortRetailCompatible(PathfindCellList& list, Bool breakTies)
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==FALSE && m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));

	// mark the newCell as being on the open list
	m_info->m_open = true;
	m_info->m_closed = false;

	if (list.m_head == nullptr)
	{
		list.m_head = this;
		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = nullptr;
		return;
	}

		// insertion sort
	PathfindCell* currentCell = list.m_head;
	PathfindCell* previousCell = nullptr;
	UnsignedInt cellCount = 0;
	while (currentCell && cellCount < PATHFIND_CELLS_PER_FRAME && !m_info->sortsBefore(*currentCell->m_info, breakTies))
	{
		// Prevent a retail crash where a pathfindCell has an m_info with a dangling nextOpen pointer
		if (currentCell->m_info->m_nextOpen && !currentCell->m_info->m_nextOpen->m_cell->m_info)
		{
			currentCell->m_info->m_nextOpen->m_cell = nullptr;
			currentCell->m_info->m_nextOpen = nullptr;
		}

		cellCount++;
		previousCell = currentCell;
		currentCell = currentCell->getNextOpen();
		}

	if (currentCell)
		{
		// insert just before "currentCell"
		if (currentCell->m_info->m_prevOpen)
			currentCell->m_info->m_prevOpen->m_nextOpen = this->m_info;
			else
			list.m_head = this;

		m_info->m_prevOpen = currentCell->m_info->m_prevOpen;
		currentCell->m_info->m_prevOpen = this->m_info;

		m_info->m_nextOpen = currentCell->m_info;

		}
		else
		{
		// append after "previousCell" - we are at the end of the list
		previousCell->m_info->m_nextOpen = this->m_info;
		m_info->m_prevOpen = previousCell->m_info;
			m_info->m_nextOpen = nullptr;
		}
}
#endif

// Forward insertion sort, returns early if the list is being initialized or we are prepending the list
void PathfindCell::forwardInsertionSort(PathfindCellList& list, Bool breakTies)
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed == FALSE && m_info->m_open == FALSE, ("Serious error - Invalid flags. jba"));

	// mark the new cell as being on the open list
	m_info->m_open = true;
	m_info->m_closed = false;

	if (list.m_head == nullptr) {
		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = nullptr;
		list.m_head = this;
		list.m_tail = this;
		return;
	}

	// If the node needs inserting before the current list head
	if (m_info->sortsBefore(*list.m_head->m_info, breakTies)) {
		m_info->m_prevOpen = nullptr;
		list.m_head->m_info->m_prevOpen = this->m_info;
		m_info->m_nextOpen = list.m_head->m_info;
		list.m_head = this;
		return;
	}

	// Traverse the list to find correct position
	PathfindCell* current = list.m_head;
	while (current->m_info->m_nextOpen && !m_info->sortsBefore(*current->m_info->m_nextOpen, breakTies)) {
		current = current->getNextOpen();
	}

	// Insert the new node in the correct position
	m_info->m_nextOpen = current->m_info->m_nextOpen;
	if (current->m_info->m_nextOpen != nullptr) {
		current->m_info->m_nextOpen->m_prevOpen = this->m_info;
	}
	else {
		list.m_tail = this;
	}

	current->m_info->m_nextOpen = this->m_info;
	m_info->m_prevOpen = current->m_info;
}

// Reverse insertion sort, returns early if the list is being initialized or we are appending the list
void PathfindCell::reverseInsertionSort(PathfindCellList& list, Bool breakTies)
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed == FALSE && m_info->m_open == FALSE, ("Serious error - Invalid flags. jba"));

	// mark the new cell as being on the open list
	m_info->m_open = true;
	m_info->m_closed = false;

	if (list.m_tail == nullptr) {
		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = nullptr;
		list.m_tail = this;
		list.m_head = this;
		return;
	}

	// If the node needs inserting after the current list tail
	if (!m_info->sortsBefore(*list.m_tail->m_info, breakTies)) {
		m_info->m_prevOpen = list.m_tail->m_info;
		list.m_tail->m_info->m_nextOpen = this->m_info;
		m_info->m_nextOpen = nullptr;
		list.m_tail = this;
		return;
	}

	// Traverse the list to find correct position
	PathfindCell* current = list.m_tail;
	while (current->m_info->m_prevOpen && m_info->sortsBefore(*current->m_info->m_prevOpen, breakTies)) {
		current = current->getPrevOpen();
	}

	// Insert the new node in the correct position
	m_info->m_prevOpen = current->m_info->m_prevOpen;
	if (current->m_info->m_prevOpen != nullptr) {
		current->m_info->m_prevOpen->m_nextOpen = this->m_info;
	}
	else {
		list.m_head = this;
	}

	current->m_info->m_prevOpen = this->m_info;
	m_info->m_nextOpen = current->m_info;
}

/// put self on "open" list in ascending cost order, return new list
void PathfindCell::putOnSortedOpenList( PathfindCellList &list, Bool breakTies )
{
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		forwardInsertionSortRetailCompatible(list, breakTies);
		return;
	}
#endif

	// TheSuperHackers @performance Mauller 20/03/2026 Implement reverse insertion sorting.
	// Long and complex paths often append PathfindCell's, with high total path costs, to the open list.
	// Appending and reverse traversal allow faster insertion of these cells, reducing pathfinding overhead by 50 - 66%.
	if (list.canReverseSort(*this)) {
		reverseInsertionSort(list, breakTies);
	}
	else {
		forwardInsertionSort(list, breakTies);
	}
}

/// remove self from "open" list
void PathfindCell::removeFromOpenList( PathfindCellList &list )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==FALSE && m_info->m_open==TRUE, ("Serious error - Invalid flags. jba"));
	if (m_info->m_nextOpen)
		m_info->m_nextOpen->m_prevOpen = m_info->m_prevOpen;
	else {
		list.m_tail = getPrevOpen();
	}

	if (m_info->m_prevOpen)
		m_info->m_prevOpen->m_nextOpen = m_info->m_nextOpen;
	else
		list.m_head = getNextOpen();

	m_info->m_open = false;
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;

}

/// remove all cells from "open" list
Int PathfindCell::releaseOpenList( PathfindCellList &list )
{
	Int count = 0;
	while (list.m_head) {
		count++;
		DEBUG_ASSERTCRASH(list.m_head->m_info, ("Has to have info."));
		DEBUG_ASSERTCRASH(list.m_head->m_info->m_closed==FALSE && list.m_head->m_info->m_open==TRUE, ("Serious error - Invalid flags. jba"));
		PathfindCell *cur = list.m_head;
		PathfindCellInfo *curInfo = list.m_head->m_info;

#if RETAIL_COMPATIBLE_PATHFINDING
		// TheSuperHackers @info This is only here to catch a crash point in the retail compatible pathfinding
		// One crash mode is where a cell has no PathfindCellInfo, resulting in a nullptr access and a crash.
		// Therefore we signal that we need to clean the maps cells and the PathfindCellInfos
		if(!curInfo && !s_useFixedPathfinding) {
			s_useFixedPathfinding = true;
			s_forceCleanCells = true;
			return count;
		}
#endif

		if (curInfo->m_nextOpen) {
			list.m_head = curInfo->m_nextOpen->m_cell;
		} else {
			list.reset();
		}
		DEBUG_ASSERTCRASH(cur == curInfo->m_cell, ("Bad backpointer in PathfindCellInfo"));
		curInfo->m_nextOpen = nullptr;
		curInfo->m_prevOpen = nullptr;
		curInfo->m_open = FALSE;
		cur->releaseInfo();
	}
	return count;
}

/// remove all cells from "closed" list
Int PathfindCell::releaseClosedList( PathfindCellList &list )
{
	Int count = 0;
	while (list.m_head) {
		count++;
		DEBUG_ASSERTCRASH(list.m_head->m_info, ("Has to have info."));
		DEBUG_ASSERTCRASH(list.m_head->m_info->m_closed==TRUE && list.m_head->m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
		PathfindCell *cur = list.m_head;
		PathfindCellInfo *curInfo = list.m_head->m_info;
#if RETAIL_COMPATIBLE_PATHFINDING
		// TheSuperHackers @info This is only here to catch a crash point in the retail compatible pathfinding
		// One crash mode is where a cell has no PathfindCellInfo, resulting in a nullptr access and a crash.
		// Therefore we signal that we need to clean the maps cells and the PathfindCellInfos
		if(!curInfo && !s_useFixedPathfinding) {
			s_useFixedPathfinding = true;
			s_forceCleanCells = true;
			return count;
		}
#endif

		if (curInfo->m_nextOpen) {
			list.m_head = curInfo->m_nextOpen->m_cell;
		} else {
			list.reset();
		}
		DEBUG_ASSERTCRASH(cur == curInfo->m_cell, ("Bad backpointer in PathfindCellInfo"));
		curInfo->m_nextOpen = nullptr;
		curInfo->m_prevOpen = nullptr;
		curInfo->m_closed = FALSE;
		cur->releaseInfo();
	}
	return count;
}

/// put self on "closed" list, return new list
void PathfindCell::putOnClosedList( PathfindCellList &list )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==FALSE && m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
	// only put on list if not already on it
	if (m_info->m_closed == FALSE)
	{
		m_info->m_closed = FALSE;
		m_info->m_closed = TRUE;

		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = list.m_head ? list.m_head->m_info : nullptr;
		if (list.m_head)
#if RETAIL_COMPATIBLE_PATHFINDING
		// TheSuperHackers @info This is only here to catch a crash point in the retail compatible pathfinding
		// This crash mode occurs due to the closed list head not having an m_info associated with it
		// A node cannot be put onto the closed list without an m_info under normal conditions
		{
			if (list.m_head->m_info)
			{
				list.m_head->m_info->m_prevOpen = this->m_info;
			}
		}
#else
			list.m_head->m_info->m_prevOpen = this->m_info;
#endif

		list.m_head = this;
	}

}

/// remove self from "closed" list
void PathfindCell::removeFromClosedList( PathfindCellList &list )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==TRUE && m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
	if (m_info->m_nextOpen)
		m_info->m_nextOpen->m_prevOpen = m_info->m_prevOpen;

	if (m_info->m_prevOpen)
		m_info->m_prevOpen->m_nextOpen = m_info->m_nextOpen;
	else
		list.m_head = getNextOpen();

	m_info->m_closed = false;
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;

}

/**
 * Return true if the given object ID is registered as an obstacle in this cell
 */
inline Bool PathfindCell::isObstaclePresent(ObjectID objID) const
{
	if (objID != INVALID_ID && (getType() == PathfindCell::CELL_OBSTACLE))
	{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
		if (s_useFixedPathfinding) {
			return m_obstacleID == objID;
		}

		DEBUG_ASSERTCRASH(m_info, ("Should have info to be obstacle."));
		return (m_info && m_info->m_obstacleID == objID);
#else
		return m_obstacleID == objID;
#endif
	}

	return false;
}


/**
 * return true if the obstacle in the cell is KINDOF_CAN_SEE_THROUGHT_STRUCTURE
 */
inline Bool PathfindCell::isObstacleTransparent() const
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		return m_obstacleIsTransparent;
	}

	return m_info ? m_info->m_obstacleIsTransparent : false;
#else
	return m_obstacleIsTransparent;
#endif
}

/**
 * return true if the given obstacle in the cell is a fence.
 */
inline Bool PathfindCell::isObstacleFence() const
{
#if RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	if (s_useFixedPathfinding) {
		return m_obstacleIsFence;
	}

	return m_info ? m_info->m_obstacleIsFence : false;
#else
	return m_obstacleIsFence;
#endif
}


const Int COST_ORTHOGONAL = 10;
const Int COST_DIAGONAL = 14;

const Int CLEARANCE_HARD_SHORTAGE = 2;             // Footprint cells past the clearance that make a cell impassable
const Int CLEARANCE_SOFT_COST = 4*COST_ORTHOGONAL; // Step cost when the footprint only brushes water, cliff or the map edge
const Real COST_TO_DISTANCE_FACTOR = 1.0f/10.0f;
const Real COST_TO_DISTANCE_FACTOR_SQR = COST_TO_DISTANCE_FACTOR*COST_TO_DISTANCE_FACTOR;

Int PathfindCell::octileDistance( PathfindCell *start, PathfindCell *finish )
{
	DEBUG_ASSERTCRASH(start->m_info && finish->m_info, ("Has to have info."));
	Int dx = abs(start->getXIndex() - finish->getXIndex());
	Int dy = abs(start->getYIndex() - finish->getYIndex());
	Int longSide = dx;
	Int shortSide = dy;
	if (dy > dx)
	{
		longSide = dy;
		shortSide = dx;
	}

	return COST_ORTHOGONAL*longSide + (COST_DIAGONAL - COST_ORTHOGONAL)*shortSide;
}

UnsignedInt PathfindCell::costToGoal( PathfindCell *goal )
{
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	return octileDistance(this, goal);
#else
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	Int dx = m_info->m_pos.x - goal->getXIndex();
	Int dy = m_info->m_pos.y - goal->getYIndex();
#define NO_REAL_DIST
#ifdef REAL_DIST
	Int cost = COST_ORTHOGONAL*WWMath::Sqrt((float)(dx*dx + dy*dy));
#else
	if (dx<0) dx = -dx;
	if (dy<0) dy = -dy;
	Int cost;
	if (dx>dy) {
		cost= COST_ORTHOGONAL*dx + (COST_ORTHOGONAL*dy)/2;
	}	else {
		cost= COST_ORTHOGONAL*dy + (COST_ORTHOGONAL*dx)/2;
	}

#endif


	return cost;
#endif
}

UnsignedInt PathfindCell::costToHierGoal( PathfindCell *goal )
{
	if( !m_info )
	{
		DEBUG_CRASH( ("Has to have info.") );
		return 100000; //...patch hack 1.01
	}
	Int dx = m_info->m_pos.x - goal->getXIndex();
	Int dy = m_info->m_pos.y - goal->getYIndex();
	Int cost = REAL_TO_INT_FLOOR(COST_ORTHOGONAL*WWMath::Sqrt((float)(dx*dx + dy*dy)) + 0.5f);
	return cost;
}

UnsignedInt PathfindCell::costSoFar( PathfindCell *parent )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	// very first node in path - no turns, no cost
	if (parent == nullptr)
		return 0;

	// add in number of turns in path so far
	ICoord2D prevDir;
	Int cost;

	prevDir.x = parent->getXIndex() - m_info->m_pos.x;
	prevDir.y = parent->getYIndex() - m_info->m_pos.y;

	// diagonal moves cost a bit more than orthogonal ones
	if (prevDir.x == 0 || prevDir.y == 0)
		cost = parent->getCostSoFar() + COST_ORTHOGONAL;
	else
		cost = parent->getCostSoFar() + COST_DIAGONAL;
	if (getPinched()) {
		cost += 1*COST_DIAGONAL;
	}

#if 1
	// Increase cost of turns.
	Int numTurns = 0;
	PathfindCell *prevCell = parent->getParentCell();
	if (prevCell) {

#if RETAIL_COMPATIBLE_PATHFINDING
		// TheSuperHackers @info this is a possible crash point in the retail pathfinding, we just prevent the crash at this point
		// External code should catch the issue in another block and cleanup the pathfinding before switching to the fixed pathfinding.
		if (!prevCell->hasInfo())
		{
			return cost;
		}
#endif

		ICoord2D dir;
		dir.x = prevCell->getXIndex() - parent->getXIndex();
		dir.y = prevCell->getYIndex() - parent->getYIndex();

		// count number of direction changes
		if (dir.x != prevDir.x || dir.y != prevDir.y)
		{
			Int dot = dir.x * prevDir.x + dir.y * prevDir.y;
			if (dot > 0)
				numTurns=4;				// 45 degree turn
			else if (dot == 0)
				numTurns = 8;		// 90 degree turn
			else
				numTurns = 16;		// 135 degree turn
		}
	}

	return cost + numTurns;
#else
	return cost;
#endif

}

PathfindLayerEnum PathfindCell::getUnderDestroyedBridgeLayer( void ) const {
	if (m_bridgeLayer > LAYER_GROUND && m_bridgeLayer < LAYER_WALL) {
		PathfindLayerEnum layer = static_cast<PathfindLayerEnum>(m_bridgeLayer);
		return (!TheAI->pathfinder()->isPathfindLayerPassable(layer)) ? layer : PathfindLayerEnum::LAYER_INVALID;
	}
	return PathfindLayerEnum::LAYER_INVALID;
}

inline Bool typesMatch(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if (targetType == srcType) return true;

	return false;
}

inline Bool waterGround(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if ( (targetType==PathfindCell::CELL_CLEAR &&
		(srcType==PathfindCell::CELL_WATER ))) {
			return true;
	}
	if ( (srcType==PathfindCell::CELL_CLEAR &&
		(targetType==PathfindCell::CELL_WATER ))) {
			return true;
	}

	return false;
}

inline Bool groundRubble(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if ( (targetType==PathfindCell::CELL_CLEAR &&
		(srcType==PathfindCell::CELL_RUBBLE ))) {
			return true;
	}
	if ( (srcType==PathfindCell::CELL_CLEAR &&
		(targetType==PathfindCell::CELL_RUBBLE ))) {
			return true;
	}

	return false;
}

inline Bool terrain(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	Int targetType = targetCell.getType();
	Int srcType = sourceCell.getType();
	if (targetType == PathfindCell::CELL_OBSTACLE) targetType = PathfindCell::CELL_CLEAR;
	if (srcType == PathfindCell::CELL_OBSTACLE) srcType = PathfindCell::CELL_CLEAR;
	if (targetType==srcType) {
		return true;
	}
	return false;
}

inline Bool crusherGround(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	Int targetType = targetCell.getType();
	Int srcType = sourceCell.getType();
	if (targetType==PathfindCell::CELL_OBSTACLE) {
		if (targetCell.isObstacleFence()) {
			if (srcType == PathfindCell::CELL_CLEAR) {
				return true;
			}
		}
	}
	if (srcType==PathfindCell::CELL_OBSTACLE) {
		if (sourceCell.isObstacleFence()) {
			if (targetType == PathfindCell::CELL_CLEAR) {
				return true;
			}
		}
	}
	return false;
}

inline Bool groundCliff(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();

	if ( (targetType==PathfindCell::CELL_CLIFF ) &&
			 (srcType==PathfindCell::CELL_CLEAR) ) {
			return true;
	}
	if ( (targetType==PathfindCell::CELL_CLEAR ) &&
			 (srcType==PathfindCell::CELL_CLIFF) ) {
			return true;
	}
	return false;
}

static void __fastcall resolveBlockZones(Int srcZone, Int targetZone, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	Int i;
	// We have two zones being combined now. Keep the lower zone.
	DEBUG_ASSERTCRASH(srcZone!=0 && targetZone!=0,  ("Bad resolve zones	."));
	if (targetZone<srcZone) {
		for (i=0; i<sizeOfZE; i++) {
			if (zoneEquivalency[i] == srcZone) {
				zoneEquivalency[i] = targetZone;
			}
		}
	} else {
		for (i=0; i<sizeOfZE; i++) {
			if (zoneEquivalency[i] == targetZone) {
				zoneEquivalency[i] = srcZone;
			}
		}
	}
}

static void __fastcall resolveZones(Int srcZone, Int targetZone, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	Int i;
	// We have two zones being combined now. Keep the lower zone.
	DEBUG_ASSERTCRASH(srcZone!=0 && targetZone!=0,  ("Bad resolve zones	."));
	DEBUG_ASSERTCRASH(srcZone<sizeOfZE && targetZone<sizeOfZE,  ("Bad resolve zones	."));
	srcZone = zoneEquivalency[srcZone];
	targetZone = zoneEquivalency[targetZone];
	DEBUG_ASSERTCRASH(srcZone<sizeOfZE && targetZone<sizeOfZE,  ("Bad resolve zones	."));
	zoneStorageType finalZone;
	if (targetZone<srcZone) {
		finalZone = zoneEquivalency[targetZone];
	} else {
		finalZone = zoneEquivalency[srcZone];
	}
	DEBUG_ASSERTCRASH(finalZone<sizeOfZE ,  ("Bad resolve zones	."));
	for (i=0; i<sizeOfZE; i++) {
		zoneStorageType ze = zoneEquivalency[i];
		if (ze == targetZone || ze == srcZone) {
			zoneEquivalency[i] = finalZone;
		}
	}
}

static void flattenZones(zoneStorageType *zoneArray, zoneStorageType *zoneHierarchical, Int sizeOfZones)
{
	Int i;
	for (i=0; i<sizeOfZones; i++) {
		Int zone1 = zoneArray[i];
		Int zone2 = zoneHierarchical[zone1];
		zone1 = zoneArray[zone2];
		zone2 = zoneHierarchical[zone1];
		zoneArray[i] = zone2;
	}
#if 1

	for (i=0; i<sizeOfZones; i++) {
		Int zone1 = zoneArray[i];
		Int zone2 = zoneHierarchical[i];
		if (zone1!=zone2) {
			resolveZones(zone1, zone2, zoneArray, sizeOfZones);
		}
	}
#endif
}

inline void applyZone(PathfindCell &targetCell, const PathfindCell &sourceCell, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	DEBUG_ASSERTCRASH(sourceCell.getZone()!=0, ("Unset source zone."));
	Int srcZone = zoneEquivalency[sourceCell.getZone()];
	Int targetZone = zoneEquivalency[targetCell.getZone()];

	if (targetZone == 0) {
		targetCell.setZone(srcZone);
		return;
	}
	if (targetZone == srcZone) {
		return; // already match.
	}
	resolveZones(srcZone, targetZone, zoneEquivalency, sizeOfZE);

}

inline void applyBlockZone(PathfindCell &targetCell, const PathfindCell &sourceCell,
													 zoneStorageType *zoneEquivalency, Int firstZone, Int sizeOfZE)
{
	DEBUG_ASSERTCRASH(sourceCell.getZone()>=firstZone && sourceCell.getZone()<firstZone+sizeOfZE, ("Memory overrun - FATAL ERROR."));
	Int srcZone = zoneEquivalency[sourceCell.getZone()-firstZone];
	DEBUG_ASSERTCRASH(targetCell.getZone()>=firstZone && sourceCell.getZone()<firstZone+sizeOfZE, ("Memory overrun - FATAL ERROR."));
	Int targetZone = zoneEquivalency[targetCell.getZone()-firstZone];
	if (targetZone == srcZone) {
		return; // already match.
	}
	resolveBlockZones(srcZone, targetZone, zoneEquivalency, sizeOfZE);

}

//------------------------  ZoneBlock  -------------------------------
ZoneBlock::ZoneBlock() : m_firstZone(0),
m_numZones(0),
m_groundCliffZones(nullptr),
m_groundWaterZones(nullptr),
m_groundRubbleZones(nullptr),
m_groundCliffRubbleZones(nullptr),
m_groundWaterRubbleZones(nullptr),
m_crusherZones(nullptr),
m_zonesAllocated(0),
m_interactsWithBridge(FALSE)
{
	m_cellOrigin.x = 0;
	m_cellOrigin.y = 0;
	m_firstZone = 0;
	m_markedPassable = TRUE;
}

ZoneBlock::~ZoneBlock()
{
	freeZones();
}

void ZoneBlock::freeZones()
{
	delete [] m_groundCliffZones;
	m_groundCliffZones = nullptr;

	delete [] m_groundWaterZones;
	m_groundWaterZones = nullptr;

	delete [] m_groundRubbleZones;
	m_groundRubbleZones = nullptr;

	delete [] m_groundCliffRubbleZones;
	m_groundCliffRubbleZones = nullptr;

	delete [] m_groundWaterRubbleZones;
	m_groundWaterRubbleZones = nullptr;

	delete [] m_crusherZones;
	m_crusherZones = nullptr;
}

/* Allocate zone equivalency arrays large enough to hold required entries.  If the arrays are already
large enough, reuse.  Then calculate terrain equivalencies. */
void ZoneBlock::blockCalculateZones(PathfindCell **map, PathfindLayer layers[], const IRegion2D &bounds)
{
	Int i, j;
	m_cellOrigin = bounds.lo;
	UnsignedInt minZone = map[bounds.lo.x][bounds.lo.y].getZone();
	UnsignedInt maxZone = minZone;

	for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
		for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
			PathfindCell *cell = &map[i][j];
			zoneStorageType zone = cell->getZone();
			if (minZone>zone) minZone=zone;
			if (maxZone<zone) maxZone=zone;
		}
	}
	m_firstZone = minZone;
	m_numZones = 1 + maxZone - minZone;

	allocateZones();

	if (m_numZones==1) return; // all zones are equivalent.

	// Determine water/ground equivalent zones, and ground/cliff equivalent zones.
	for (i=0; i<m_zonesAllocated; i++) {
		m_groundCliffZones[i] = i+m_firstZone;
		m_groundWaterZones[i] = i+m_firstZone;
		m_groundRubbleZones[i] = i+m_firstZone;
		m_groundCliffRubbleZones[i] = i+m_firstZone;
		m_groundWaterRubbleZones[i] = i+m_firstZone;
		m_crusherZones[i] = i+m_firstZone;
	}

	for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
		for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
			if (i>bounds.lo.x && map[i][j].getZone()!=map[i-1][j].getZone()) {

				if (waterGround(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundWaterZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i-1][j], m_groundWaterRubbleZones, m_firstZone, m_numZones);
				}
				if (groundRubble(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundRubbleZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i-1][j], m_groundCliffRubbleZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i-1][j], m_groundWaterRubbleZones, m_firstZone, m_numZones);
				}
				if (groundCliff(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundCliffZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i-1][j], m_groundCliffRubbleZones, m_firstZone, m_numZones);
				}
				if (crusherGround(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_crusherZones, m_firstZone, m_numZones);
				}
			}
			if (j>bounds.lo.y && map[i][j].getZone()!=map[i][j-1].getZone()) {
				if (waterGround(map[i][j],map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundWaterZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i][j-1], m_groundWaterRubbleZones, m_firstZone, m_numZones);
				}
				if (groundRubble(map[i][j], map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundRubbleZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i][j-1], m_groundCliffRubbleZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i][j-1], m_groundWaterRubbleZones, m_firstZone, m_numZones);
				}
				if (groundCliff(map[i][j],map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundCliffZones, m_firstZone, m_numZones);
					applyBlockZone(map[i][j], map[i][j-1], m_groundCliffRubbleZones, m_firstZone, m_numZones);
				}
				if (crusherGround(map[i][j], map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_crusherZones, m_firstZone, m_numZones);
				}
			}
			DEBUG_ASSERTCRASH(map[i][j].getZone() != 0, ("Cleared the zone."));
		}
	}

}

//
// Return the zone at this location.
//
zoneStorageType ZoneBlock::getEffectiveZone( LocomotorSurfaceTypeMask acceptableSurfaces,
																					 Bool crusher, zoneStorageType zone) const
{
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	if (zone==PathfindZoneManager::UNINITIALIZED_ZONE) {
		return zone;
	}
#endif

	if (acceptableSurfaces&LOCOMOTORSURFACE_AIR) return 1; // air is all zone 1.

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground, water & cliff, so all is zone 1.
		return 1;
	}
	if (m_numZones<2) {
		return m_firstZone; // if we only got 1 zone, it's all the same zone.
	}
	DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
	if (zone<m_firstZone || zone >= m_firstZone+m_numZones) {
		return m_firstZone;
	}
	zone -= m_firstZone;
	if (crusher) {
		zone = m_crusherZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		zone -= m_firstZone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		zone = m_groundCliffRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		zone = m_groundWaterRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		zone = m_groundCliffZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & water, so use the ground water combiner.
		zone = m_groundWaterZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		// Locomotors can go on ground & rubble, so use the ground rubble combiner.
		zone = m_groundRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		DEBUG_CRASH(("Cliff water only locomotor sets not supported yet."));
	}

	return zone+m_firstZone;
}


/* Allocate zone equivalency arrays large enough to hold m_maxZone entries.  If the arrays are already
large enough, just return. */
void ZoneBlock::allocateZones()
{
	if (m_zonesAllocated>m_numZones && m_groundCliffZones!=nullptr) {
		return;
	}
	freeZones();

	if (m_numZones==1) {
		return; // we don't need any zone equivalency tables.
	}

	if (m_zonesAllocated == 0) {
		m_zonesAllocated = 4;
	}
	while (m_zonesAllocated <= m_numZones) {
		m_zonesAllocated *= 2;
	}
	// pool[]ify
	m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType [m_zonesAllocated];
	m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundCliffRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundWaterRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
}


//------------------------  PathfindZoneManager  -------------------------------
PathfindZoneManager::PathfindZoneManager() : m_maxZone(0),
m_nextFrameToCalculateZones(0),
m_groundCliffZones(nullptr),
m_groundWaterZones(nullptr),
m_groundRubbleZones(nullptr),
m_groundCliffRubbleZones(nullptr),
m_groundWaterRubbleZones(nullptr),
m_terrainZones(nullptr),
m_crusherZones(nullptr),
m_hierarchicalZones(nullptr),
m_blockOfZoneBlocks(nullptr),
m_zoneBlocks(nullptr),
m_zonesAllocated(0)
{
	m_zoneBlockExtent.x = 0;
	m_zoneBlockExtent.y = 0;
}

PathfindZoneManager::~PathfindZoneManager()
{
	freeZones();
	freeBlocks();
}

void PathfindZoneManager::freeZones()
{
	delete [] m_groundCliffZones;
	m_groundCliffZones = nullptr;

	delete [] m_groundWaterZones;
	m_groundWaterZones = nullptr;

	delete [] m_groundRubbleZones;
	m_groundRubbleZones = nullptr;

	delete [] m_groundCliffRubbleZones;
	m_groundCliffRubbleZones = nullptr;

	delete [] m_groundWaterRubbleZones;
	m_groundWaterRubbleZones = nullptr;

	delete [] m_terrainZones;
	m_terrainZones = nullptr;

	delete [] m_crusherZones;
	m_crusherZones = nullptr;

	delete [] m_hierarchicalZones;
	m_hierarchicalZones = nullptr;

	m_zonesAllocated = 0;
}

void PathfindZoneManager::freeBlocks()
{
	delete [] m_blockOfZoneBlocks;
	m_blockOfZoneBlocks = nullptr;

	delete [] m_zoneBlocks;
	m_zoneBlocks = nullptr;

	m_zoneBlockExtent.x = 0;
	m_zoneBlockExtent.y = 0;
}

/* Allocate zone equivalency arrays large enough to hold m_maxZone entries.  If the arrays are already
large enough, just return. */
void PathfindZoneManager::allocateZones()
{
	if (m_zonesAllocated>m_maxZone && m_groundCliffZones!=nullptr) {
		return;
	}
	freeZones();

	if (m_zonesAllocated == 0) {
		m_zonesAllocated = INITIAL_ZONES;
	}
	while (m_zonesAllocated <= m_maxZone) {
		m_zonesAllocated *= 2;
	}
	DEBUG_LOG(("Allocating zone tables of size %d", m_zonesAllocated));
	// pool[]ify
	m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundCliffRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundWaterRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_terrainZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_hierarchicalZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
}

/* Allocate zone blocks for hierarchical pathfinding.   */
void PathfindZoneManager::allocateBlocks(const IRegion2D &globalBounds)
{
	freeBlocks();

	m_zoneBlockExtent.x = (globalBounds.hi.x-globalBounds.lo.x+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;
	m_zoneBlockExtent.y = (globalBounds.hi.y-globalBounds.lo.y+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;

	m_blockOfZoneBlocks = MSGNEW("PathfindZoneBlocks") ZoneBlock[(m_zoneBlockExtent.x)*(m_zoneBlockExtent.y)];
	m_zoneBlocks = MSGNEW("PathfindZoneBlocks") ZoneBlockP[m_zoneBlockExtent.x];
	Int i;
	for (i=0; i<m_zoneBlockExtent.x; i++) {
		m_zoneBlocks[i] = &m_blockOfZoneBlocks[i*(m_zoneBlockExtent.y)];
	}
}

void PathfindZoneManager::reset()  ///< Called when the map is reset.
{
	freeZones();
	freeBlocks();
}


void PathfindZoneManager::markZonesDirty()  ///< Called when the zones need to be recalculated.
{
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
	m_nextFrameToCalculateZones = TheGameLogic->getFrame();
#else
	if (TheGameLogic->getFrame()<2) {
		m_nextFrameToCalculateZones = 2;
		return;
	}
	m_nextFrameToCalculateZones = MIN( m_nextFrameToCalculateZones, TheGameLogic->getFrame() + ZONE_UPDATE_FREQUENCY );
#endif
}

void PathfindZoneManager::markZonesDirtyNow()
{
	if (TheGameLogic->getFrame()<2) {
		m_nextFrameToCalculateZones = 2;
		return;
	}
	m_nextFrameToCalculateZones = TheGameLogic->getFrame();
}

/**
 * Calculate zones.  A zone is an area of the same terrain - clear, water or cliff.
 * The utility of zones is that if current location and destination are in the same zone,
 * you can successfully pathfind.
 * If you are a multiple terrain vehicle, like amphibious transport, the lookup is a little more
 * complicated.
 */
void PathfindZoneManager::calculateZones( PathfindCell **map, PathfindLayer layers[], const IRegion2D &globalBounds )
{
#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING)
	__int64 startTime64;
	static double timeToUpdate = 0.0f;
	static double averageTimeToUpdate = 0.0f;
	static Int updateSamples = 0;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
#endif
#endif

	m_maxZone = 1;	// we start using zone 0 as a flag.
	const Int maxZones=24000;
	zoneStorageType zoneEquivalency[maxZones];
	Int i, j;
	for (i=0; i<maxZones; i++) {
		zoneEquivalency[i] = i;
	}
	for (i=0; i<=LAYER_LAST; i++) {
		layers[i].setZone(0);
	}

	Int xCount = (globalBounds.hi.x-globalBounds.lo.x+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;
	Int yCount = (globalBounds.hi.y-globalBounds.lo.y+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;

	Int xBlock, yBlock;
	for (xBlock = 0; xBlock<xCount; xBlock++) {
		for (yBlock=0; yBlock<yCount; yBlock++) {
			IRegion2D bounds;
			bounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			bounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			bounds.hi.x = bounds.lo.x + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			bounds.hi.y = bounds.lo.y + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			if (bounds.hi.x > globalBounds.hi.x) {
				bounds.hi.x = globalBounds.hi.x;
			}
			if (bounds.hi.y > globalBounds.hi.y) {
				bounds.hi.y = globalBounds.hi.y;
			}
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
			if (bounds.lo.x>bounds.hi.x || bounds.lo.y>bounds.hi.y) {
				DEBUG_CRASH(("Incorrect bounds calculation. Logic error, fix me. jba."));
				continue;
			}
#endif
			m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(false);
			for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
				for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
					PathfindCell *cell = &map[i][j];
					cell->setZone(0);

					if (i>bounds.lo.x) {
						if (map[i][j].getType() == map[i-1][j].getType()) {
							applyZone(map[i][j], map[i-1][j], zoneEquivalency, m_maxZone);
						}
					}
					if (j>bounds.lo.y) {
						if (map[i][j].getType() == map[i][j-1].getType()) {
							applyZone(map[i][j], map[i][j-1], zoneEquivalency, m_maxZone);
						}
					}
					if (cell->getZone()==0) {
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
						// Past the table size every new zone shares the last id rather than indexing off the end
						if (m_maxZone >= maxZones) {
							m_maxZone = maxZones-1;
							s_pathfindStats.zoneOverflow++;
						}
#endif
						cell->setZone(m_maxZone);
						m_maxZone++;
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
						if (m_maxZone>= maxZones) {
							DEBUG_CRASH(("Ran out of pathfind zones.  SERIOUS ERROR! jba."));
							break;
						}
#endif
					}
					if (cell->getConnectLayer() > LAYER_GROUND) {
 						m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(true);
					}

				}
			}
 		}
	}

	Int totalZones = m_maxZone;
	s_pathfindStats.rawZoneHighWater = totalZones;

	// Collapse the zones into a 1,2,3... sequence, removing collapsed zones.
	m_maxZone = 1;
	Int collapsedZones[maxZones];
	collapsedZones[0] = 0;
	for (i=1; i<totalZones; i++) {
		Int zone = zoneEquivalency[i];
		if (zone == i) {
			collapsedZones[i] = m_maxZone;
			++m_maxZone;
		} else {
			collapsedZones[i] = collapsedZones[zone];
		}
	}

	// Now map the zones in the map back into the collapsed zones.
	for( j=globalBounds.lo.y; j<=globalBounds.hi.y; j++ ) {
		for( i=globalBounds.lo.x; i<=globalBounds.hi.x; i++ ) {
			PathfindCell &cell = map[i][j];
			cell.setZone(collapsedZones[cell.getZone()]);
		}
	}
	
	for (i=0; i<=LAYER_LAST; i++) {
		PathfindLayer &r_thisLayer = layers[i];

		Int zone = collapsedZones[r_thisLayer.getZone()];
		if (zone == 0) {
			zone = m_maxZone;
			m_maxZone++;
		}

		r_thisLayer.setZone( zone );
		r_thisLayer.applyZone();

		if (!r_thisLayer.isUnused() && !r_thisLayer.isDestroyed()) {
			ICoord2D ndx;
			r_thisLayer.getStartCellIndex(&ndx);
			setBridge(ndx.x, ndx.y, true);
			r_thisLayer.getEndCellIndex(&ndx);
			setBridge(ndx.x, ndx.y, true);
		}
	}

	allocateZones();

	for (xBlock=0; xBlock<xCount; xBlock++) {
		for (yBlock=0; yBlock<yCount; yBlock++) {
			IRegion2D bounds;
			bounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			bounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			bounds.hi.x = bounds.lo.x + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			bounds.hi.y = bounds.lo.y + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.

			if (bounds.hi.x > globalBounds.hi.x)
				bounds.hi.x = globalBounds.hi.x;

			if (bounds.hi.y > globalBounds.hi.y)
				bounds.hi.y = globalBounds.hi.y;
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
			if (bounds.lo.x>bounds.hi.x || bounds.lo.y>bounds.hi.y) {
				DEBUG_CRASH(("Incorrect bounds calculation. Logic error, fix me. jba."));
				continue;
			}
#endif
			m_zoneBlocks[xBlock][yBlock].blockCalculateZones(map, layers, bounds);
		}
	}

	// Determine water/ground equivalent zones, and ground/cliff equivalent zones.
	for (i=0; i<m_zonesAllocated; i++) {
		m_groundCliffZones[i] = i;
		m_groundWaterZones[i] = i;
		m_groundRubbleZones[i] = i;
		m_groundCliffRubbleZones[i] = i;
		m_groundWaterRubbleZones[i] = i;
		m_terrainZones[i] = i;
		m_crusherZones[i] = i;
		m_hierarchicalZones[i] = i;
	}

	for( j=globalBounds.lo.y; j<=globalBounds.hi.y; j++ ) {
		for( i=globalBounds.lo.x; i<=globalBounds.hi.x; i++ ) {
			PathfindCell &r_thisCell = map[i][j];

			if ( (r_thisCell.getConnectLayer() > LAYER_GROUND) &&
				(r_thisCell.getType() == PathfindCell::CELL_CLEAR) ) {
				PathfindLayer *layer = layers + r_thisCell.getConnectLayer();
				resolveZones(r_thisCell.getZone(), layer->getZone(), m_hierarchicalZones, m_maxZone);
			}

			if ( i > globalBounds.lo.x && r_thisCell.getZone() != map[i-1][j].getZone() ) {
				const PathfindCell &r_leftCell = map[i-1][j];

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
				if (r_thisCell.getType() == r_leftCell.getType()) {
					applyZone(r_thisCell, r_leftCell, m_hierarchicalZones, m_maxZone);
				}
				if (waterGround(r_thisCell, r_leftCell)) {
					applyZone(r_thisCell, r_leftCell, m_groundWaterZones, m_maxZone);
					applyZone(r_thisCell, r_leftCell, m_groundWaterRubbleZones, m_maxZone);
				}
				if (groundRubble(r_thisCell, r_leftCell)) {
					applyZone(r_thisCell, r_leftCell, m_groundRubbleZones, m_maxZone);
					applyZone(r_thisCell, r_leftCell, m_groundCliffRubbleZones, m_maxZone);
					applyZone(r_thisCell, r_leftCell, m_groundWaterRubbleZones, m_maxZone);
				}
				if (groundCliff(r_thisCell, r_leftCell)) {
					applyZone(r_thisCell, r_leftCell, m_groundCliffZones, m_maxZone);
					applyZone(r_thisCell, r_leftCell, m_groundCliffRubbleZones, m_maxZone);
				}
				if (terrain(r_thisCell, r_leftCell)) {
					applyZone(r_thisCell, r_leftCell, m_terrainZones, m_maxZone);
				}
				if (crusherGround(r_thisCell, r_leftCell)) {
					applyZone(r_thisCell, r_leftCell, m_crusherZones, m_maxZone);
				}
#else
				//if this is true, skip all the ones below
				if (r_thisCell.getType() == r_leftCell.getType())
					applyZone(r_thisCell, r_leftCell, m_hierarchicalZones, m_maxZone);
				else {
					Bool notTerrainOrCrusher = TRUE; // if this is false, skip the if-else-ladder below

					if (terrain(r_thisCell, r_leftCell)) {
						applyZone(r_thisCell, r_leftCell, m_terrainZones, m_maxZone);
						notTerrainOrCrusher = FALSE;
					}

					if (crusherGround(r_thisCell, r_leftCell)) {
						applyZone(r_thisCell, r_leftCell, m_crusherZones, m_maxZone);
						notTerrainOrCrusher = FALSE;
					}

					if ( notTerrainOrCrusher ) {
						if (waterGround(r_thisCell, r_leftCell))
						{
							applyZone(r_thisCell, r_leftCell, m_groundWaterZones, m_maxZone);
							applyZone(r_thisCell, r_leftCell, m_groundWaterRubbleZones, m_maxZone);
						}
						else if (groundRubble(r_thisCell, r_leftCell))
						{
							applyZone(r_thisCell, r_leftCell, m_groundRubbleZones, m_maxZone);
							applyZone(r_thisCell, r_leftCell, m_groundCliffRubbleZones, m_maxZone);
							applyZone(r_thisCell, r_leftCell, m_groundWaterRubbleZones, m_maxZone);
						}
						else if (groundCliff(r_thisCell, r_leftCell))
						{
							applyZone(r_thisCell, r_leftCell, m_groundCliffZones, m_maxZone);
							applyZone(r_thisCell, r_leftCell, m_groundCliffRubbleZones, m_maxZone);
						}
					}

				}
#endif

			}

			if (j>globalBounds.lo.y && r_thisCell.getZone()!=map[i][j-1].getZone()) {
				const PathfindCell &r_topCell = map[i][j-1];

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
				if (r_thisCell.getType() == r_topCell.getType()) {
					applyZone(r_thisCell, r_topCell, m_hierarchicalZones, m_maxZone);
				}
				if (waterGround(r_thisCell, r_topCell)) {
					applyZone(r_thisCell, r_topCell, m_groundWaterZones, m_maxZone);
					applyZone(r_thisCell, r_topCell, m_groundWaterRubbleZones, m_maxZone);
				}
				if (groundRubble(r_thisCell, r_topCell)) {
					applyZone(r_thisCell, r_topCell, m_groundRubbleZones, m_maxZone);
					applyZone(r_thisCell, r_topCell, m_groundCliffRubbleZones, m_maxZone);
					applyZone(r_thisCell, r_topCell, m_groundWaterRubbleZones, m_maxZone);
				}
				if (groundCliff(r_thisCell, r_topCell)) {
					applyZone(r_thisCell, r_topCell, m_groundCliffZones, m_maxZone);
					applyZone(r_thisCell, r_topCell, m_groundCliffRubbleZones, m_maxZone);
				}
				if (terrain(r_thisCell, r_topCell)) {
					applyZone(r_thisCell, r_topCell, m_terrainZones, m_maxZone);
				}
				if (crusherGround(r_thisCell, r_topCell)) {
					applyZone(r_thisCell, r_topCell, m_crusherZones, m_maxZone);
				}
#else
				//if this is true, skip all the ones below
				if (r_thisCell.getType() == r_topCell.getType())
					applyZone(r_thisCell, r_topCell, m_hierarchicalZones, m_maxZone);
				else {
					Bool notTerrainOrCrusher = TRUE; // if this is false, skip the if-else-ladder below

					if (terrain(r_thisCell, r_topCell)) {
						applyZone(r_thisCell, r_topCell, m_terrainZones, m_maxZone);
						notTerrainOrCrusher = FALSE;
					}

					if (crusherGround(r_thisCell, r_topCell)) {
						applyZone(r_thisCell, r_topCell, m_crusherZones, m_maxZone);
						notTerrainOrCrusher = FALSE;
					}

					if (notTerrainOrCrusher) {
						if (waterGround(r_thisCell, r_topCell))
						{
							applyZone(r_thisCell, r_topCell, m_groundWaterZones, m_maxZone);
							applyZone(r_thisCell, r_topCell, m_groundWaterRubbleZones, m_maxZone);
						}
						else if (groundRubble(r_thisCell, r_topCell))
						{
							applyZone(r_thisCell, r_topCell, m_groundRubbleZones, m_maxZone);
							applyZone(r_thisCell, r_topCell, m_groundCliffRubbleZones, m_maxZone);
							applyZone(r_thisCell, r_topCell, m_groundWaterRubbleZones, m_maxZone);
						}
						else if (groundCliff(r_thisCell, r_topCell))
						{
							applyZone(r_thisCell, r_topCell, m_groundCliffZones, m_maxZone);
							applyZone(r_thisCell, r_topCell, m_groundCliffRubbleZones, m_maxZone);
						}
					}

				}
#endif

			}

		}
	}

	//FLATTEN HIERARCHICAL ZONES
	for (i=1; i<m_maxZone; i++) {
		Int zone = m_hierarchicalZones[i];
		m_hierarchicalZones[i] = m_hierarchicalZones[zone];
	}

	//THIS BLOCK IS 20%
	flattenZones(m_groundCliffZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundWaterZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundRubbleZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundCliffRubbleZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundWaterRubbleZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_terrainZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_crusherZones, m_hierarchicalZones, m_maxZone);

#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING)
	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

	if ( updateSamples < 400 ) {
		averageTimeToUpdate = ((averageTimeToUpdate * updateSamples) + timeToUpdate) / (updateSamples + 1.0f);
		updateSamples++;
		DEBUG_LOG(("computing...: %f", averageTimeToUpdate));
	}
	else if ( updateSamples == 400 ) {
		DEBUG_LOG((" =============DONE============= Average time to calculate zones: %f", averageTimeToUpdate));
		DEBUG_LOG(("                                           Percent of baseline : %f", averageTimeToUpdate/0.003335f));
		updateSamples = 777;
	}

#endif
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI == AI_DEBUG_ZONES)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
		RGBColor color;
		memset(&color, 0, sizeof(Color));
		addIcon(nullptr, 0, 0, color);
		for( j=0; j<globalBounds.hi.y; j++ )	{
			for( i=0; i<globalBounds.hi.x; i++ )	{
				Int zone = map[i][j].getZone();
				zone = m_hierarchicalZones[zone];

				color.blue = (zone%3) * 0.5f;
				zone = zone/3;
				color.green = (zone%3) * 0.5f;
				zone = zone/3;
				color.red = (zone%3) * 0.5;
				Coord3D pos;
				pos.x = ((Real)i + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.y = ((Real)j + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, map[i][j].getLayer() ) + 0.5f;
				addIcon(&pos, PATHFIND_CELL_SIZE_F*0.8f, 500, color);
			}
		}
	}
#endif
	m_nextFrameToCalculateZones = 0xffffffff;
}

/**
 * Update zones where a structure has been added or removed.
 * This can be done by just updating the equivalency arrays, without rezoning the map..
 */
void PathfindZoneManager::updateZonesForModify(PathfindCell **map, PathfindLayer layers[], const IRegion2D &structureBounds, const IRegion2D &globalBounds )
{

#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING)
	__int64 startTime64;
	double timeToUpdate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
#endif
#endif
	IRegion2D bounds = structureBounds;
	bounds.hi.x++;
	bounds.hi.y++;
	if (bounds.hi.x > globalBounds.hi.x) {
		bounds.hi.x = globalBounds.hi.x;
	}
	if (bounds.hi.y > globalBounds.hi.y) {
		bounds.hi.y = globalBounds.hi.y;
	}

	Int xBlock, yBlock;
	for (xBlock = 0; xBlock<m_zoneBlockExtent.x; xBlock++) {
		for (yBlock=0; yBlock<m_zoneBlockExtent.y; yBlock++) {
			IRegion2D blockBounds;
			blockBounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			blockBounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			blockBounds.hi.x = blockBounds.lo.x + ZONE_BLOCK_SIZE - 1; // blockBounds are inclusive.
			blockBounds.hi.y = blockBounds.lo.y + ZONE_BLOCK_SIZE - 1; // blockBounds are inclusive.
			if (blockBounds.hi.x > bounds.hi.x) {
				blockBounds.hi.x = bounds.hi.x;
			}
			if (blockBounds.hi.y > bounds.hi.y) {
				blockBounds.hi.y = bounds.hi.y;
			}
			if (blockBounds.lo.x < bounds.lo.x) {
				blockBounds.lo.x = bounds.lo.x;
			}
			if (blockBounds.lo.y < bounds.lo.y) {
				blockBounds.lo.y = bounds.lo.y;
			}
			if (blockBounds.lo.x>blockBounds.hi.x || blockBounds.lo.y>blockBounds.hi.y) {
				continue;
			}
			m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(false);
			Int i, j;
			for( j=blockBounds.lo.y; j<=blockBounds.hi.y; j++ )	{
				for( i=blockBounds.lo.x; i<=blockBounds.hi.x; i++ )	{
					PathfindCell *cell = &map[i][j];
					if (cell->getZone()!=UNINITIALIZED_ZONE) continue;

					if (i>blockBounds.lo.x) {
						if (map[i][j].getType() == map[i-1][j].getType()) {
							cell->setZone(map[i-1][j].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
					}
					if (j>blockBounds.lo.y) {
						if (cell->getType() == map[i][j-1].getType()) {
							cell->setZone(map[i][j-1].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
						if (i<blockBounds.hi.x) {
							if (typesMatch(*cell, map[i+1][j-1]) &&
									typesMatch(*cell, map[i+1][j])) {
								cell->setZone(map[i+1][j-1].getZone());
								if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
							}
						}
					}
				}
			}
			for( j=blockBounds.hi.y; j>=blockBounds.lo.y; j-- )	{
				for( i=blockBounds.hi.x; i>=blockBounds.lo.x; i-- )	{
					PathfindCell *cell = &map[i][j];
					if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
					if (i<blockBounds.hi.x) {
						if (map[i][j].getType() == map[i+1][j].getType()) {
							cell->setZone(map[i+1][j].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
					}
					if (j<blockBounds.hi.y) {
						if (cell->getType() == map[i][j+1].getType()) {
							cell->setZone(map[i][j+1].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
						if (i<blockBounds.hi.x) {
							if (typesMatch(*cell, map[i+1][j+1]) &&
									typesMatch(*cell, map[i+1][j])) {
								cell->setZone(map[i+1][j+1].getZone());
								if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
							}
						}
					}
				}
			}
		}
	}
#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING)
	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));
#endif
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI==AI_DEBUG_ZONES)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
		RGBColor color;
		memset(&color, 0, sizeof(Color));
		addIcon(nullptr, 0, 0, color);
		Int i, j;
		for( j=0; j<globalBounds.hi.y; j++ )	{
			for( i=0; i<globalBounds.hi.x; i++ )	{
				Int zone = map[i][j].getZone();
				//zone = m_terrainZones[zone];
				zone = m_hierarchicalZones[zone];

				color.blue = (zone%3) * 0.5f;
				zone = zone/3;
				color.green = (zone%3) * 0.5f;
				zone = zone/3;
				color.red = (zone%3) * 0.5;
				Coord3D pos;
				pos.x = ((Real)i + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.y = ((Real)j + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, map[i][j].getLayer() ) + 0.5f;
				addIcon(&pos, PATHFIND_CELL_SIZE_F*0.8f, 200, color);
			}
		}
	}
#endif

}

//
// Clear the passable flags.
//
void PathfindZoneManager::clearPassableFlags()
{	Int blockX;
	Int blockY;
	for (blockX = 0; blockX<m_zoneBlockExtent.x; blockX++) {
		for (blockY = 0; blockY<m_zoneBlockExtent.y; blockY++) {
			m_zoneBlocks[blockX][blockY].setPassable(false);
		}
	}
}

//
// Set the passable flags.
//
void PathfindZoneManager::setAllPassable()
{	Int blockX;
	Int blockY;
	for (blockX = 0; blockX<m_zoneBlockExtent.x; blockX++) {
		for (blockY = 0; blockY<m_zoneBlockExtent.y; blockY++) {
			m_zoneBlocks[blockX][blockY].setPassable(true);
		}
	}
}

//
// Set the passable flag for the block at this location.
//
void PathfindZoneManager::setPassable(Int cellX, Int cellY, Bool passable)
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return;
	}
	m_zoneBlocks[blockX][blockY].setPassable(passable);
}

//
// Get the passable flag for the block at this location.
//
Bool PathfindZoneManager::isPassable(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	return m_zoneBlocks[blockX][blockY].isPassable();
}

//
// Get the passable flag for the block at this location.
//
Bool PathfindZoneManager::clipIsPassable(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		return false;
	}
	return m_zoneBlocks[blockX][blockY].isPassable();
}

//
// Set the bridge flag for the block at this location.
//
void PathfindZoneManager::setBridge(Int cellX, Int cellY, Bool bridge)
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		// DEBUG_CRASH(("Invalid block."));  Bridges can be off the playable grid, so don't crash. jba.
		return;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		// DEBUG_CRASH(("Invalid block."));  Bridges can be off the playable grid, so don't crash. jba.
		return;
	}
	m_zoneBlocks[blockX][blockY].setInteractsWithBridge(bridge);
}


//
// Set the bridge flag for the block at this location.
//
Bool PathfindZoneManager::interactsWithBridge(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	return m_zoneBlocks[blockX][blockY].getInteractsWithBridge();
}


//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getBlockZone(LocomotorSurfaceTypeMask acceptableSurfaces, Bool crusher,Int cellX, Int cellY, PathfindCell **map) const
{
	PathfindCell *cell = &(map[cellX][cellY]);
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return 0;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return 0;
	}
	zoneStorageType zone =  m_zoneBlocks[blockX][blockY].getEffectiveZone(acceptableSurfaces, crusher, cell->getZone());
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
	if (zone > m_maxZone) {
#else
	if (zone >= m_maxZone) {
#endif
		DEBUG_CRASH(("Invalid zone."));
		return UNINITIALIZED_ZONE;
	}
	return zone;
}

//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getEffectiveTerrainZone(zoneStorageType zone) const
{
	return m_hierarchicalZones[m_terrainZones[zone]];
}

//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getEffectiveZone( LocomotorSurfaceTypeMask acceptableSurfaces,
																										Bool crusher, zoneStorageType zone) const
{
	//DEBUG_ASSERTCRASH(zone, ("Zone not set"));
	if (zone>m_maxZone) {
		DEBUG_CRASH(("Invalid zone"));
		return (0);
	}
	if (zone>m_maxZone) {
		DEBUG_CRASH(("Invalid zone"));
		return (0);
	}
	if (acceptableSurfaces&LOCOMOTORSURFACE_AIR) return 1; // air is all zone 1.

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground, water & cliff, so all is zone 1.
		return 1;
	}

	if (crusher) {
		zone = m_crusherZones[zone];
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		zone = m_groundCliffRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		zone = m_groundWaterRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		zone = m_groundCliffZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & water, so use the ground water combiner.
		zone = m_groundWaterZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		// Locomotors can go on ground & rubble, so use the ground rubble combiner.
		zone = m_groundRubbleZones[zone];
		return zone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		DEBUG_CRASH(("Cliff water only locomotor sets not supported yet."));
	}
	zone = m_hierarchicalZones[zone];

	return zone;
}
//-------------------- PathfindLayer ----------------------------------------
PathfindLayer::PathfindLayer() : m_blockOfMapCells(nullptr), m_layerCells(nullptr), m_bridge(nullptr),
m_destroyed(FALSE),
m_height(0),
m_width(0),
m_xOrigin(0),
m_yOrigin(0),
m_zone(0)
{
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
}

PathfindLayer::~PathfindLayer()
{
	reset();
}

/**
 * Returns true if the layer is available for use.
 */
void PathfindLayer::reset()
{
	m_bridge = nullptr;
	if (m_layerCells) {
		Int i, j;
		for (i=0; i<m_width; i++) {
			for (j=0; j<m_height; j++) {
				PathfindCell *cell = &m_layerCells[i][j];
				cell->reset();
			}
		}
		delete [] m_layerCells;
		m_layerCells = nullptr;
	}

	delete [] m_blockOfMapCells;
	m_blockOfMapCells = nullptr;

	m_width = 0;
	m_height = 0;
	m_xOrigin = 0;
	m_yOrigin = 0;
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
	m_layer = LAYER_GROUND;
}

/**
 * Returns true if the layer is available for use.
 */
Bool PathfindLayer::isUnused()
{
	// Special case - wall layer is built from not a bridge.  jba.
	if (m_layer == LAYER_WALL && m_width>0) return false;

	if (m_bridge==nullptr) return true;
	return false;
}



/**
 * Draws debug cell info.
 */
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
void PathfindLayer::doDebugIcons() {
	if (isUnused()) return;
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	// render AI debug information
	{
		Coord3D topLeftCorner;
		RGBColor color;
		color.red = color.green = color.blue = 0;
		Coord3D center;
		center.x = (m_xOrigin+m_width/2)*PATHFIND_CELL_SIZE_F;
		center.y = (m_yOrigin+m_height/2)*PATHFIND_CELL_SIZE_F;
		center.z = 0;
		Real bridgeHeight = TheTerrainLogic->getLayerHeight(center.x , center.y, m_layer);
		if (m_layer == LAYER_WALL) {
			bridgeHeight = TheAI->pathfinder()->getWallHeight();
		}
		static Int flash = 0;
		flash--;
		if (flash<1) flash = 20;
		if (flash < 10) return;
		Bool showCells = TheGlobalData->m_debugAI==AI_DEBUG_CELLS;
		// show the pathfind grid
		for( int j=0; j<m_height; j++ )
		{
			topLeftCorner.y = (Real)(j+m_yOrigin) * PATHFIND_CELL_SIZE_F;

			for( int i=0; i<m_width; i++ )
			{
				topLeftCorner.x = (Real)(i+m_xOrigin) * PATHFIND_CELL_SIZE_F;

				color.red = color.green = color.blue = 0;
				Bool empty = false;
				Real size = 0.4f;
				const PathfindCell *cell = &m_layerCells[i][j];
				if (cell)
				{
					if (cell->getConnectLayer()==LAYER_GROUND) {
							color.green = 1;
							color.blue = 1;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_IMPASSABLE) {
							color.red = color.green = color.blue = 1;
							size = 0.2f;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE) {
							color.blue = color.red = 1;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_CLIFF) {
							color.red = 1;
							empty = false;
					}	else {
							size = 0.2f;
					}
				}
				if (showCells) {
					empty = true;
					color.red = color.green = color.blue = 0;
					if (empty && cell) {
						if (cell->getFlags()!=PathfindCell::NO_UNITS) {
							empty = false;
							if (cell->getFlags() == PathfindCell::UNIT_GOAL) {
								color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED) {
								color.green = color.blue = color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_MOVING) {
								color.green = 1;
							}	else {
								color.green = color.red = 1;
							}
						}
					}
				}
				if (!empty) {
					Coord3D loc;
					loc.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F/2.0f;
					loc.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F/2.0f;
					loc.z = bridgeHeight;
					addIcon(&loc, PATHFIND_CELL_SIZE_F*size, 99, color);
				}
			}
		}

	}
}
#endif

/**
 * Sets the bridge & layer number for a layer.
 */
Bool PathfindLayer::init(Bridge *theBridge, PathfindLayerEnum layer)
{
	if (m_bridge!=nullptr) return false;
	m_bridge = theBridge;
	m_layer = layer;
	m_destroyed = false;
	return true;
}

/**
 * Allocates the pathfind cells for the bridge layer.
 */
void PathfindLayer::allocateCells(const IRegion2D *extent)
{
	if (m_bridge == nullptr) return;
	Region2D bridgeBounds = *m_bridge->getBounds();
	Int maxX, maxY;
	m_xOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.x-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_yOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.y-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_width = 0;
	m_height = 0;
	maxX = REAL_TO_INT_CEIL((bridgeBounds.hi.x+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	maxY = REAL_TO_INT_CEIL((bridgeBounds.hi.y+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	// Pad with 1 extra;
	m_xOrigin--;
	m_yOrigin--;
	maxX++;
	maxY++;

	if (m_xOrigin < extent->lo.x) m_xOrigin = extent->lo.x;
	if (m_yOrigin < extent->lo.y) m_yOrigin = extent->lo.y;
	if (maxX > extent->hi.x) maxX = extent->hi.x;
	if (maxY > extent->hi.y) maxY = extent->hi.y;
	if (maxX <= m_xOrigin) return;
	if (maxY <= m_yOrigin) return;
	m_width = maxX - m_xOrigin;
	m_height = maxY - m_yOrigin;

	// Allocate cells.
	// pool[]ify
	m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[m_width*m_height];
	m_layerCells = MSGNEW("PathfindMapCells") PathfindCellP[m_width];
	Int i;
	for (i=0; i<m_width; i++) {
		m_layerCells[i] = &m_blockOfMapCells[i*m_height];
	}
}

/**
 * Allocates the pathfind cells for the wall bridge layer.
 */
void PathfindLayer::allocateCellsForWallLayer(const IRegion2D *extent, ObjectID *wallPieces, Int numPieces)
{
	DEBUG_ASSERTCRASH(m_layer==LAYER_WALL, ("Wrong layer for wall."));
	if (m_layer != LAYER_WALL) return;
	Region2D bridgeBounds;

	Int i;
	Bool first = true;
	for (i=0; i<numPieces; i++) {
		Object *obj = TheGameLogic->findObjectByID(wallPieces[i]);
		Region2D objBounds;
		if (obj==nullptr) continue;
		obj->getGeometryInfo().get2DBounds(*obj->getPosition(), obj->getOrientation(), objBounds);
		if (first) {
			bridgeBounds = objBounds;
			first = false;
		} else {
			if (bridgeBounds.lo.x>objBounds.lo.x) bridgeBounds.lo.x = objBounds.lo.x;
			if (bridgeBounds.lo.y>objBounds.lo.y) bridgeBounds.lo.y = objBounds.lo.y;
			if (bridgeBounds.hi.x<objBounds.hi.x) bridgeBounds.hi.x = objBounds.hi.x;
			if (bridgeBounds.hi.y<objBounds.hi.y) bridgeBounds.hi.y = objBounds.hi.y;
		}
	}

	Int maxX, maxY;
	m_xOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.x-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_yOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.y-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_width = 0;
	m_height = 0;
	maxX = REAL_TO_INT_CEIL((bridgeBounds.hi.x+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	maxY = REAL_TO_INT_CEIL((bridgeBounds.hi.y+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	// Pad with 1 extra;
	m_xOrigin--;
	m_yOrigin--;
	maxX++;
	maxY++;

	if (m_xOrigin < extent->lo.x) m_xOrigin = extent->lo.x;
	if (m_yOrigin < extent->lo.y) m_yOrigin = extent->lo.y;
	if (maxX > extent->hi.x) maxX = extent->hi.x;
	if (maxY > extent->hi.y) maxY = extent->hi.y;
	if (maxX <= m_xOrigin) return;
	if (maxY <= m_yOrigin) return;
	m_width = maxX - m_xOrigin;
	m_height = maxY - m_yOrigin;

	// Allocate cells.
	m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[m_width*m_height];
	m_layerCells = MSGNEW("PathfindMapCells") PathfindCellP[m_width];

	for (i=0; i<m_width; i++) {
		m_layerCells[i] = &m_blockOfMapCells[i*m_height];
	}
}

/**
 * Checks to see if a broken bridge connects 2 zones.
 */
Bool PathfindLayer::connectsZones(PathfindZoneManager *zm, const LocomotorSet& locoSet,
																	Int zone1, Int zone2)
{
	if (!m_destroyed) {
		return false;
	}
	Bool found1 = false;
	Bool found2 = false;
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			if (cell->getConnectLayer()==LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i+m_xOrigin, j+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						zoneStorageType zone = zm->getEffectiveZone(locoSet.getValidSurfaces(),
							true, groundCell->getZone());
						zone = zm->getEffectiveTerrainZone(zone);
						if (zone == zone1) found1 = true;
						if (zone == zone2) found2 = true;
					}
			}
		}
	}
	return found1 && found2;
}

/**
 * Classifies the pathfind cells for the bridge layer.
 */
void PathfindLayer::classifyCells()
{
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setConnectLayer(LAYER_INVALID);
			cell->setLayer(m_layer);
			classifyLayerMapCell(i+m_xOrigin, j+m_yOrigin, cell, m_bridge);
		}
		BridgeInfo info;
		m_bridge->getBridgeInfo(&info);
		Coord3D bridgeDir = info.to;
		bridgeDir.x -= info.from.x;
		bridgeDir.y -= info.from.y;
		bridgeDir.z -= info.from.z;
		bridgeDir.normalize();
		bridgeDir.x *= PATHFIND_CELL_SIZE_F*0.7f;
		bridgeDir.y *= PATHFIND_CELL_SIZE_F*0.7f;

		m_startCell.x = REAL_TO_INT_FLOOR((info.from.x-bridgeDir.x) / PATHFIND_CELL_SIZE_F);
		m_startCell.y = REAL_TO_INT_FLOOR((info.from.y-bridgeDir.y) / PATHFIND_CELL_SIZE_F);
		m_endCell.x = REAL_TO_INT_FLOOR((info.to.x+bridgeDir.x) / PATHFIND_CELL_SIZE_F);
		m_endCell.y = REAL_TO_INT_FLOOR((info.to.y+bridgeDir.y) / PATHFIND_CELL_SIZE_F);
	}
	if (m_destroyed) {
		Int i, j;
		for (i=0; i<m_width; i++) {
			for (j=0; j<m_height; j++) {
				PathfindCell *cell = &m_layerCells[i][j];
				if (cell->getConnectLayer() == LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i+m_xOrigin, j+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						DEBUG_ASSERTCRASH(groundCell->getConnectLayer()==m_layer, ("Should connect to this layer.jba."));
						groundCell->setConnectLayer(LAYER_INVALID); // disconnect it.
					}
				}
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
				cell->setType(PathfindCell::CELL_IMPASSABLE);
#else
				cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
#endif
			}
		}
	}
}

/**
 * Classifies the pathfind cells for the wall bridge layer.
 */
void PathfindLayer::classifyWallCells(ObjectID *wallPieces, Int numPieces)
{
	DEBUG_ASSERTCRASH(m_layer==LAYER_WALL, ("Wrong layer for wall."));
	if (m_layer != LAYER_WALL) return;
	if (m_layerCells == nullptr) return;

	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setConnectLayer(LAYER_INVALID);
			cell->setLayer(m_layer);
			classifyWallMapCell(i+m_xOrigin, j+m_yOrigin, cell, wallPieces, numPieces);
			cell->setPinched(false);
		}
	}
	if (m_destroyed) {
		Int i, j;
		for (i=0; i<m_width; i++) {
			for (j=0; j<m_height; j++) {
				PathfindCell *cell = &m_layerCells[i][j];
				if (cell->getConnectLayer() == LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i+m_xOrigin, j+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						DEBUG_ASSERTCRASH(groundCell->getConnectLayer()==m_layer, ("Should connect to this layer.jba."));
						groundCell->setConnectLayer(LAYER_INVALID); // disconnect it.
					}
				}
				cell->setType(PathfindCell::CELL_IMPASSABLE);
			}
		}
	}

	// Tighten up 1 cell.
	for (i=1; i<m_width-1; i++) {
		for (j=1; j<m_height-1; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			Int k, l;
			for (k=i-1; k<i+2; k++) {
				for (l=j-1; l<j+2; l++) {
					PathfindCell *adjacentCell = &m_layerCells[k][l];
					if (adjacentCell->getType() != PathfindCell::CELL_CLEAR) {
						cell->setPinched(true);
					}
				}
			}
		}
	}
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			if (cell->getPinched() && cell->getType() == PathfindCell::CELL_CLEAR) {
				cell->setType(PathfindCell::CELL_CLIFF);
			}
			cell->setPinched(false);
		}
	}
}

/**
 * Relassifies the pathfind cells for the destroyed bridge layer.
 */
Bool PathfindLayer::setDestroyed(Bool destroyed)
{
	if (destroyed == m_destroyed) return false;

	m_destroyed = destroyed;
	classifyCells();

	return true;
}

/**
 * Copies m_zone into the zone for all the member cells.
 */
void PathfindLayer::applyZone()
{
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setZone(m_zone);
		}
	}
}


/**
 * Return the bridge's object id.
 */
ObjectID PathfindLayer::getBridgeID()
{
	return m_bridge->peekBridgeInfo()->bridgeObjectID;
}

/**
 * Return the cell at the index location.
 */
PathfindCell *PathfindLayer::getCell(Int x, Int y)
{
	DEBUG_ASSERTCRASH(m_layerCells, ("no data in layer, why get cells?"));
	if (m_layerCells==nullptr) {
		return nullptr;
	}
	x -= m_xOrigin;
	y -= m_yOrigin;
	if (x<0 || x>=m_width) return nullptr;
	if (y<0 || y>=m_height) return nullptr;
	PathfindCell *cell = &m_layerCells[x][y];
	if (cell->getType() == PathfindCell::CELL_IMPASSABLE) {
		return nullptr; // Impassable cells are ignored.
	}
	return cell;
}


/**
 * Classify the given map cell as clear, or not, etc.
 */
void PathfindLayer::classifyLayerMapCell( Int i, Int j , PathfindCell *cell, Bridge *theBridge)
{
	Coord3D topLeftCorner, bottomRightCorner;

	topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;


	Int bridgeCount = 0;
	Coord3D pt;
	if (theBridge->isPointOnBridge(&topLeftCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.y = bottomRightCorner.y;
	if (theBridge->isPointOnBridge(&pt) ) {
		bridgeCount++;
	}
	if (theBridge->isPointOnBridge(&bottomRightCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.x = bottomRightCorner.x;
	if (theBridge->isPointOnBridge(&pt) ) {
		bridgeCount++;
	}
	cell->reset();
	cell->setLayer(m_layer);
	cell->setType(PathfindCell::CELL_IMPASSABLE);
	if (bridgeCount == 4) {
		cell->setType(PathfindCell::CELL_CLEAR);
	} else {
		if (bridgeCount!=0) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
			cell->setType(PathfindCell::CELL_CLIFF); // it's off the bridge.
#else
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE); // it's off the bridge.
#endif
		}

		// check against the end lines.

		Region2D cellBounds;
		cellBounds.lo.x = topLeftCorner.x;
		cellBounds.lo.y = topLeftCorner.y;
		cellBounds.hi.x = bottomRightCorner.x;
		cellBounds.hi.y = bottomRightCorner.y;

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
		if (m_bridge->isCellOnEnd(&cellBounds)) {
			cell->setType(PathfindCell::CELL_CLEAR);
		}
		if (m_bridge->isCellOnSide(&cellBounds)) {
			cell->setType(PathfindCell::CELL_CLIFF);
		} else {
			if (m_bridge->isCellEntryPoint(&cellBounds)) {
				cell->setType(PathfindCell::CELL_CLEAR);
				cell->setConnectLayer(LAYER_GROUND);
				PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i, j );
				groundCell->setConnectLayer(cell->getLayer());
			}
		}
#else
		if (m_bridge->isCellOnSide(&cellBounds)) {
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
		} else {
			if (m_bridge->isCellOnEnd(&cellBounds)) {
				cell->setType(PathfindCell::CELL_CLEAR);
			}
			if (m_bridge->isCellEntryPoint(&cellBounds)) {
				cell->setType(PathfindCell::CELL_CLEAR);
				cell->setConnectLayer(LAYER_GROUND);
				PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i, j );
				groundCell->setConnectLayer(cell->getLayer());
			}
		}
#endif
	}
	Coord3D center = topLeftCorner;
	center.x += PATHFIND_CELL_SIZE/2;
	center.y += PATHFIND_CELL_SIZE/2;
	if (cell->getType()!=PathfindCell::CELL_IMPASSABLE) {
		if (!(cell->getConnectLayer()==LAYER_GROUND) ) {
			// Check for bridge clearance.  If the ground isn't 1 pathfind cells below, mark impassable.
			Real groundHeight = TheTerrainLogic->getLayerHeight( center.x, center.y, LAYER_GROUND );
			Real bridgeHeight = theBridge->getBridgeHeight( &center, nullptr );
			if (groundHeight+LAYER_Z_CLOSE_ENOUGH_F > bridgeHeight) {
				PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND,i, j);
				if (!(groundCell->getType()==PathfindCell::CELL_OBSTACLE)) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
					groundCell->setType(PathfindCell::CELL_IMPASSABLE);
#else
					groundCell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
#endif
				}
			}
		}
	}
}


Bool PathfindLayer::isPointOnWall(ObjectID *wallPieces, Int numPieces, const Coord3D *pt)
{
	Int i;
	for (i=0; i<numPieces; i++) {
		Object *obj = TheGameLogic->findObjectByID(wallPieces[i]);
		if (obj==nullptr) continue;
		Real major = obj->getGeometryInfo().getMajorRadius();
		Real minor = (obj->getGeometryInfo().getGeomType() == GEOMETRY_SPHERE) ? obj->getGeometryInfo().getMajorRadius() : obj->getGeometryInfo().getMinorRadius();

		Real c = (Real)Cos(-obj->getOrientation());
		Real s = (Real)Sin(-obj->getOrientation());

		// convert to a delta relative to rect ctr
		Real ptx = pt->x - obj->getPosition()->x;
		Real pty = pt->y - obj->getPosition()->y;

		// inverse-rotate it to the right coord system
		Real ptx_new = (Real)WWMath::Fabs(ptx*c - pty*s);
		Real pty_new = (Real)WWMath::Fabs(ptx*s + pty*c);

		if (ptx_new <= major && pty_new <= minor)
		{
			return true;
		}
	}
	return false;
}


/**
 * Classify the given map cell as clear, or not, etc.
 */
void PathfindLayer::classifyWallMapCell( Int i, Int j , PathfindCell *cell, ObjectID *wallPieces, Int numPieces)
{
	Coord3D topLeftCorner, bottomRightCorner;

	topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;


	Int bridgeCount = 0;
	Coord3D pt;
	if (isPointOnWall(wallPieces, numPieces, &topLeftCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.y = bottomRightCorner.y;
	if (isPointOnWall(wallPieces, numPieces, &pt) ) {
		bridgeCount++;
	}
	if (isPointOnWall(wallPieces, numPieces, &bottomRightCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.x = bottomRightCorner.x;
	if (isPointOnWall(wallPieces, numPieces, &pt) ) {
		bridgeCount++;
	}
	cell->reset();
	cell->setLayer(m_layer);
	cell->setType(PathfindCell::CELL_IMPASSABLE);
	if (bridgeCount == 4) {
		cell->setType(PathfindCell::CELL_CLEAR);
	} else {
		if (bridgeCount!=0) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
			cell->setType(PathfindCell::CELL_CLIFF); // it's off the bridge.
#else
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE); // it's off the bridge.
#endif
		}

	}
}

//----------------------- Pathfinder ---------------------------------------

Pathfinder::Pathfinder() :m_map(nullptr)
{
	debugPath = nullptr;
	m_waterRings = nullptr;
	m_cliffRings = nullptr;
	PathfindCellInfo::allocateCellInfos();
	reset();
}

Pathfinder::~Pathfinder()
{
	PathfindCellInfo::releaseCellInfos();
	freeClearance();
}

void Pathfinder::reset()
{
	frameToShowObstacles = 0;
	DEBUG_LOG(("Pathfind cell is %d bytes, PathfindCellInfo is %d bytes", sizeof(PathfindCell), sizeof(PathfindCellInfo)));

	delete [] m_blockOfMapCells;
	m_blockOfMapCells = nullptr;

	delete [] m_map;
	m_map = nullptr;

	Int i;
	for (i=0; i<=LAYER_LAST; i++) {
		m_layers[i].reset();
	}

	// reset the pathfind grid
	m_extent.lo.x=m_extent.lo.y=m_extent.hi.x=m_extent.hi.y=0;
	m_logicalExtent.lo.x=m_logicalExtent.lo.y=m_logicalExtent.hi.x=m_logicalExtent.hi.y=0;
	m_openList.reset();
	m_closedList.reset();

	m_ignoreObstacleID = INVALID_ID;
	m_isTunneling = false;

	m_moveAlliesDepth = 0;

	// pathfind grid cells have not been classified yet
	m_isMapReady = false;
	m_cumulativeCellsAllocated = 0;
	m_searchCellCap = PATHFIND_SEARCH_CELL_CAP;
	m_searchStartFreeCount = PathfindCellInfo::getFreeCount();
	m_lastSearchHitCap = false;
	freeClearance();	// "clearance": made again when the next map is classified

	debugPathPos.x = 0.0f;
	debugPathPos.y = 0.0f;
	debugPathPos.z = 0.0f;

	deleteInstance(debugPath);
	debugPath = nullptr;

	m_frameToShowObstacles = 0;

	for (m_queuePRHead=0; m_queuePRHead<PATHFIND_QUEUE_LEN; m_queuePRHead++) {
		m_queuedPathfindRequests[m_queuePRHead] = INVALID_ID;
	}
	m_queuePRHead = 0;
	m_queuePRTail = 0;

	m_numWallPieces = 0;
	for (i=0; i<MAX_WALL_PIECES; ++i)
	{
		m_wallPieces[i] = INVALID_ID;
	}

	if (TheAI && TheAI->getAiData()) {
		m_wallHeight = TheAI->getAiData()->m_wallHeight;
	}
	else
	{
		m_wallHeight = 0.0f;
	}
	m_zoneManager.reset();

#if RETAIL_COMPATIBLE_PATHFINDING
	// TheSuperHackers @feature simonx645 16/11/2026 "pool" (PathTuning.h): the search code that returns every record it
	// takes on every exit runs from the start, instead of only after a retail failure case was caught.
	s_useFixedPathfinding = PathTuning::Pool;
	s_forceCleanCells = false;
#endif

#if RTS_ZEROHOUR && RETAIL_COMPATIBLE_CRC
	m_classifyFenceZeroInit = false;
#endif
}

/**
 * Adds a piece of a wall.
 */
void Pathfinder::addWallPiece(Object *wallPiece)
{
	if (m_numWallPieces<MAX_WALL_PIECES-1) {
		m_wallPieces[m_numWallPieces] = wallPiece->getID();
		m_numWallPieces++;
	}
}

/**
 * Removes a piece of a wall
 */
void Pathfinder::removeWallPiece(Object *wallPiece)
{

	// sanity
  if( wallPiece == nullptr )
		return;

	// find entry
	for( Int i = 0; i < m_numWallPieces; ++i )
	{

		// match by id
		if( m_wallPieces[ i ] == wallPiece->getID() )
		{

			// put the last id in the wall piece array here
			m_wallPieces[ i ] = m_wallPieces[ m_numWallPieces - 1 ];

			// we now have one less entry
			m_numWallPieces--;

			// all done
			return;

		}

	}

}

/**
 * Checks if a point is on the wall.
 */
Bool Pathfinder::isPointOnWall(const Coord3D *pos)
{
	if (m_numWallPieces==0) return false;
	if (m_layers[LAYER_WALL].isUnused()) return false;
	PathfindLayerEnum layer = (PathfindLayerEnum)LAYER_WALL;
	PathfindCell *cell = getCell(layer, pos);
	// make sure the layer matches, since getCell can return ground layer cells if the pos is 'off' the bridge/wall
	if (cell && cell->getLayer() == layer) {
		if (cell->getType() == PathfindCell::CELL_CLEAR) {
			return true;
		}
	}
	return false;
}

/**
 * Adds a bridge & returns the layer.
 */
PathfindLayerEnum Pathfinder::addBridge(Bridge *theBridge)
{
	Int layer = LAYER_GROUND+1;
	while (layer<=LAYER_WALL) {
		if (m_layers[layer].isUnused()) {
			if (m_layers[layer].init(theBridge, (PathfindLayerEnum)layer) ) {
				return (PathfindLayerEnum)layer;
			}
			DEBUG_LOG(("WARNING: Bridge failed to init in pathfinder"));
			return LAYER_GROUND; // failed to init, usually cause off of the map.  jba.
		}
		layer++;
	}
	DEBUG_CRASH(("Ran out of bridge layers."));
	return LAYER_GROUND;
}

/**
 * Updates an object's layer, making sure the object is actually on the bridge first.
 */
void Pathfinder::updateLayer(Object *obj, PathfindLayerEnum layer)
{
	if (layer != LAYER_GROUND) {
		if (!TheTerrainLogic->objectInteractsWithBridgeLayer(obj, layer)) {
			layer = LAYER_GROUND;
		}
	}
	//DEBUG_LOG(("Object layer is %d", layer));
	obj->setLayer(layer);
}

/**
 * Classify the cells under the given object
 * If 'insert' is true, object is being added
 * If 'insert' is false, object is being removed
 */
void Pathfinder::classifyFence( Object *obj, Bool insert )
{
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
	m_zoneManager.markZonesDirty();
#endif

	const Coord3D *pos = obj->getPosition();
  Real angle = obj->getOrientation();

 	Real halfsizeX = obj->getTemplate()->getFenceWidth()/2;
 	Real halfsizeY = PATHFIND_CELL_SIZE_F/10.0f;
 	Real fenceOffset = obj->getTemplate()->getFenceXOffset();

 	Real c = (Real)Cos(angle);
 	Real s = (Real)Sin(angle);

 	const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
 	Real ydx = s * STEP_SIZE;
 	Real ydy = -c * STEP_SIZE;
 	Real xdx = c * STEP_SIZE;
 	Real xdy = s * STEP_SIZE;

 	Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
 	Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

 	Real tl_x = pos->x - fenceOffset*c - halfsizeY*s;
 	Real tl_y = pos->y + halfsizeY*c - fenceOffset*s;

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	// TheSuperHackers @fix Mauller 16/06/2025 Fixes uninitialized variables.
#if RETAIL_COMPATIBLE_CRC
	//CRCDEBUG_LOG(("Pathfinder::classifyFence - (%d,%d)", cellBounds.hi.x, cellBounds.hi.y));

	// For retail the values on the stack are often either 0 or larger than the map size.
	// We initialize them to reduce the likelihood of a mismatch.
	if (m_classifyFenceZeroInit)
	{
		cellBounds.hi.x = 0;
		cellBounds.hi.y = 0;
	}
	else
	{
		cellBounds.hi.x = 1000000;
		cellBounds.hi.y = 1000000;
	}
#else
	cellBounds.hi.x = REAL_TO_INT_CEIL((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi.y = REAL_TO_INT_CEIL((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
#endif
	Bool didAnything = false;
#endif // !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)

 	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
 	{
 		Real x = tl_x;
 		Real y = tl_y;
 		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
 		{
 			Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
 			Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);
 			if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
 			{
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
 				if (insert) {
 					ICoord2D pos;
 					pos.x = cx;
 					pos.y = cy;
 					m_map[cx][cy].setTypeAsObstacle( obj, true, pos );
 				}
 				else
 					m_map[cx][cy].removeObstacle(obj);
#else
 				if (insert) {
 					ICoord2D pos;
 					pos.x = cx;
 					pos.y = cy;
					if (m_map[cx][cy].setTypeAsObstacle( obj, true, pos )) {
						didAnything = true;
 						m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					}
 				}
				else {
					if (m_map[cx][cy].removeObstacle(obj)) {
						didAnything = true;
 						m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					}
				}
				if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 				if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 				if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 				if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
#endif
 			}
 		}
 	}
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	if (didAnything) {
		m_zoneManager.markZonesDirty();
		m_zoneManager.updateZonesForModify(m_map, m_layers, cellBounds, m_extent);
	}
#endif
}

/**
 * Classify the cells under the given object
 * If 'insert' is true, object is being added
 * If 'insert' is false, object is being removed
 */
void Pathfinder::classifyObjectFootprint( Object *obj, Bool insert )
{
	if (obj->isKindOf(KINDOF_MINE)) {
		return;  // don't pathfind around mines.
	}

	if (obj->isKindOf(KINDOF_PROJECTILE)) {
		return;  // don't care about projectiles.
	}

	if (obj->isKindOf(KINDOF_BRIDGE_TOWER)) {
		return;  // It is important to not abuse bridge towers.
	}

	if (obj->isKindOf(KINDOF_BRIDGE) && !obj->getTemplate()->isBridge()) {
		// Procedural bridge span.  Its deck is a pathfind layer of its own, and the ground below it
		// is governed by the bridge clearance, not by this object's placeholder box.
		return;
	}

	if (obj->getTemplate()->getFenceWidth() > 0.0f)
	{
		if (!obj->isKindOf(KINDOF_DEFENSIVE_WALL))
		{
			classifyFence(obj, insert);
			return;
		}
	}

	if (!insert) {
		// Just in case, remove the object.  Remove checks that the object has been added before
		// removing, so it's safer to just remove it, as by the time some units "die", they've become
		// lifeless immobile husks of debris, but we still need to remove them.  jba.

#if !RTS_GENERALS
    if ( obj->isKindOf( KINDOF_BLAST_CRATER ) ) // since these footprints are permanent, never remove them
      return;
#endif

		removeUnitFromPathfindMap(obj);
		if (obj->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) {
			if (!m_layers[LAYER_WALL].isUnused()) {
				Int i;
				ObjectID curID = obj->getID();
				for (i=0; i<m_numWallPieces; i++) {
					if (curID == m_wallPieces[i]) {
						m_wallPieces[i]=INVALID_ID;
					}
				}
				// Kill anybody on the wall.
				Object *obj;
				for (obj = TheGameLogic->getFirstObject(); obj; obj=obj->getNextObject()) {
					if (obj->getLayer() == LAYER_WALL) {
						if (m_layers[LAYER_WALL].isPointOnWall(&curID, 1, obj->getPosition()))
						{
							// The object fell off the wall.
							// Destroy it.
							DamageInfo extraDamageInfo;
							extraDamageInfo.in.m_damageType = DAMAGE_FALLING;
							extraDamageInfo.in.m_deathType = DEATH_SPLATTED;
							extraDamageInfo.in.m_sourceID = obj->getID();
							extraDamageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
							obj->attemptDamage(&extraDamageInfo);
						}
					}
				}
				// recalc the wall.
				m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
			}
		}
	}
	if (!obj->isKindOf(KINDOF_STRUCTURE)) {
		return;  // Only path around structures.
	}
	if (obj->isMobile()) {
		return; // mobile aren't obstacles.
	}
	/// For now, all small objects will not be obstacles
	if (obj->getGeometryInfo().getIsSmall()) {
		return;
	}

#if RTS_GENERALS
	if (obj->getHeightAboveTerrain() > PATHFIND_CELL_SIZE_F) {
		return; // Don't add bounds that are up in the air.
	}
#else
	if (obj->getHeightAboveTerrain() > PATHFIND_CELL_SIZE_F && ( ! obj->isKindOf( KINDOF_BLAST_CRATER ) ) )
  {
		return; // Don't add bounds that are up in the air.... unless a blast crater wants to do just that
	}
#endif
	internal_classifyObjectFootprint(obj, insert);
}

void Pathfinder::internal_classifyObjectFootprint( Object *obj, Bool insert )
{
	const Coord3D *pos = obj->getPosition();

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi = cellBounds.lo;
#endif

	switch(obj->getGeometryInfo().getGeomType())
	{
		case GEOMETRY_BOX:
		{
			m_zoneManager.markZonesDirty();
			Real angle = obj->getOrientation();

			Real halfsizeX = obj->getGeometryInfo().getMajorRadius();
			Real halfsizeY = obj->getGeometryInfo().getMinorRadius();

			Real c = (Real)Cos(angle);
			Real s = (Real)Sin(angle);

			const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
			Real ydx = s * STEP_SIZE;
			Real ydy = -c * STEP_SIZE;
			Real xdx = c * STEP_SIZE;
			Real xdy = s * STEP_SIZE;

			Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
			Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

			Real tl_x = pos->x - halfsizeX*c - halfsizeY*s;
			Real tl_y = pos->y + halfsizeY*c - halfsizeX*s;

			for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
			{
				Real x = tl_x;
				Real y = tl_y;
				for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
				{
					Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
					Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
					if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
					{
						if (insert) {
							ICoord2D pos;
							pos.x = cx;
							pos.y = cy;
							m_map[cx][cy].setTypeAsObstacle( obj, false, pos );
						}
						else
							m_map[cx][cy].removeObstacle(obj);
					}
#else
					if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
					{
						if (insert) {
							ICoord2D pos;
							pos.x = cx;
							pos.y = cy;
							if (m_map[cx][cy].setTypeAsObstacle( obj, false, pos )) {
 								m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
							}
						}
						else {
							if (m_map[cx][cy].removeObstacle(obj)) {
 								m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
							}
						}
 						if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 						if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 						if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 						if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
					}
#endif
				}
			}
		}
		break;

		case GEOMETRY_SPHERE:	// not quite right, but close enough
		case GEOMETRY_CYLINDER:
		{
			m_zoneManager.markZonesDirty();
			// fill in all cells that overlap as obstacle cells
			/// @todo This is a very inefficient circle-rasterizer
			ICoord2D topLeft, bottomRight;
			Coord2D center, delta;
			Real radius = obj->getGeometryInfo().getMajorRadius();
			Real r2, size;

			topLeft.x = REAL_TO_INT_FLOOR(0.5f + (pos->x - radius)/PATHFIND_CELL_SIZE_F)-1;
			topLeft.y = REAL_TO_INT_FLOOR(0.5f + (pos->y - radius)/PATHFIND_CELL_SIZE_F)-1;
			size = (radius/PATHFIND_CELL_SIZE_F);
			center.x = (pos->x/PATHFIND_CELL_SIZE_F);
			center.y = (pos->y/PATHFIND_CELL_SIZE_F);

			size += 0.4f;
			r2 = size*size;

			bottomRight.x = topLeft.x + 2*size + 2;
			bottomRight.y = topLeft.y + 2*size + 2;

			for( int j = topLeft.y; j < bottomRight.y; j++ )
			{
				for( int i = topLeft.x; i < bottomRight.x; i++ )
				{
					delta.x = i+0.5f - center.x;
					delta.y = j+0.5f - center.y;

					if (delta.x*delta.x + delta.y*delta.y <= r2)
					{
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
						if (i >= 0 && j >= 0 && i < m_extent.hi.x && j < m_extent.hi.y)
						{
							if (insert)	{
								ICoord2D pos;
								pos.x = i;
								pos.y = j;
								m_map[i][j].setTypeAsObstacle( obj, false, pos );
							}
							else
								m_map[i][j].removeObstacle( obj );
						}
#else
						if (i >= 0 && j >= 0 && i < m_extent.hi.x && j < m_extent.hi.y)
						{
							if (insert) {
								ICoord2D pos;
								pos.x = i;
								pos.y = j;
								if (m_map[i][j].setTypeAsObstacle( obj, false, pos )) {
 									m_map[i][j].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
								}
							}
							else {
								if (m_map[i][j].removeObstacle(obj)) {
 									m_map[i][j].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
								}
							}
 							if (cellBounds.lo.x>i) cellBounds.lo.x = i;
 							if (cellBounds.lo.y>j) cellBounds.lo.y = j;
 							if (cellBounds.hi.x<i) cellBounds.hi.x = i;
 							if (cellBounds.hi.y<j) cellBounds.hi.y = j;
						}
#endif
					}
				}
			}
		}
		break;
	}

#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
	Region2D bounds;
	obj->getGeometryInfo().get2DBounds(*obj->getPosition(), obj->getOrientation(), bounds);
	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR(bounds.lo.x/PATHFIND_CELL_SIZE_F)-1;
	cellBounds.lo.y = REAL_TO_INT_FLOOR(bounds.lo.y/PATHFIND_CELL_SIZE_F)-1;
	cellBounds.hi.x = REAL_TO_INT_CEIL(bounds.hi.x/PATHFIND_CELL_SIZE_F)+1;
	cellBounds.hi.y = REAL_TO_INT_CEIL(bounds.hi.y/PATHFIND_CELL_SIZE_F)+1;
#else
	m_zoneManager.updateZonesForModify(m_map, m_layers, cellBounds, m_extent);

	cellBounds.lo.x -= 2;
	cellBounds.lo.y -= 2;
	cellBounds.hi.x += 2;
	cellBounds.hi.y += 2;
#endif

	Int i, j;

	if (cellBounds.lo.x < m_extent.lo.x) {
		cellBounds.lo.x = m_extent.lo.x;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.hi.x > m_extent.hi.x) {
		cellBounds.hi.x = m_extent.hi.x;
	}
	if (cellBounds.hi.y > m_extent.hi.y) {
		cellBounds.hi.y = m_extent.hi.y;
	}

	if (!insert) {
		for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
		{
			for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
			{
				if (m_map[i][j].getType()==PathfindCell::CELL_IMPASSABLE) {
					m_map[i][j].setType(PathfindCell::CELL_CLEAR);
				}
			}
		}
	}
	// Check for pinched cells, and close them off.

	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			m_map[i][j].setPinched(false);
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Int totalCount = 0;
				Int orthogonalCount = 0;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (j==l)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							totalCount++;
							if ((k==i) || (l==j)) {
								orthogonalCount++;
							}
						}

					}
				}
				// If the total open cells are < 2 or total cells < 4, we are pinched.
				if (orthogonalCount<2 || totalCount<4) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}

#if RETAIL_COMPATIBLE_PATHFINDING
	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (m_map[i][j].getPinched() && (m_map[i][j].getType() == PathfindCell::CELL_CLEAR)) {
				m_map[i][j].setType(PathfindCell::CELL_IMPASSABLE);
				m_map[i][j].setPinched(false);
			}
		}
	}
#endif

	// Expand building bounds 1 cell.
	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Bool objectAdjacent = false;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (l==j)) continue;
						if ((k!=i) && (l!=j)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_OBSTACLE) {
							objectAdjacent = true;
							break;
						}

					}
				}
				if (objectAdjacent) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}
}

/**
 * Classify the given map cell as WATER, CLIFF, etc.
 * Note that this does NOT classify cells as OBSTACLES.
 * OBSTACLE cells are classified only via objects.
 * @todo optimize this - lots of redundant computation
 */
void Pathfinder::classifyMapCell( Int i, Int j , PathfindCell *cell)
{
	Coord3D topLeftCorner, bottomRightCorner;

	Bool hasObstacle =  (cell->getType() == PathfindCell::CELL_OBSTACLE) ;

	topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;

	cell->setPinched(false);
	cell->setWaterLevel(0);

	PathfindCell::CellType type = PathfindCell::CELL_CLEAR;
	if (TheTerrainLogic->isCliffCell(topLeftCorner.x, topLeftCorner.y))
	{
		type = PathfindCell::CELL_CLIFF;
	}

	//
	// If any corners are underwater, this is a water cell
	//
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, bottomRightCorner.y) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, bottomRightCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;

	if (hasObstacle) {
		type =  PathfindCell::CELL_OBSTACLE;
	}
	cell->setType( type );
	cell->releaseInfo();
}

struct Point {
	int x, y;
};

static void calculateWaterLevels(IRegion2D bounds, PathfindCell** map)
{
	std::queue<Point> q;
	constexpr int MAX_LEVEL = 15;

	// Directions: Up, Down, Left, Right
	const int dirs[4][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0} };

	// 1. Initialization Pass
	// Iterate only within the defined region
	for (int i = bounds.lo.x; i < bounds.hi.x; ++i) {
		for (int j = bounds.lo.y; j < bounds.hi.y; ++j) {
			auto& cell = map[i][j];

			if (cell.getType() != PathfindCell::CELL_WATER) {
				// Found LAND/OBSTACLE inside the region
				cell.setWaterLevel(0);
				q.push({ i, j });
			}
			else {
				// Mark as unvisited
				cell.setWaterLevel(-1);
			}
		}
	}

	// 2. BFS Propagation
	while (!q.empty()) {
		Point curr = q.front();
		q.pop();

		int currentDist = map[curr.x][curr.y].getWaterLevel();

		// Optimization: Stop expanding if we hit the max clamp limit
		if (currentDist >= MAX_LEVEL) {
			continue;
		}

		// Check neighbors
		for (const auto& dir : dirs) {
			int nx = curr.x + dir[0];
			int ny = curr.y + dir[1];

			// Bounds Check: Ensure neighbor is strictly within IRegion2D
			if (nx >= bounds.lo.x && nx < bounds.hi.x &&
				ny >= bounds.lo.y && ny < bounds.hi.y) {

				auto& neighbor = map[nx][ny];

				// Only propagate into unvisited (-1) WATER cells
				if (neighbor.getWaterLevel() == -1 &&
					neighbor.getType() == PathfindCell::CELL_WATER) {

					int newDist = currentDist + 1;
					neighbor.setWaterLevel(newDist);
					q.push({ nx, ny });
				}
			}
		}
	}

	// 3. Final Cleanup
	// Handle "Deep Ocean" or isolated water cells that BFS didn't reach 
	// (either because they are > 15 away, or isolated by bounds/obstacles)
	for (int i = bounds.lo.x; i < bounds.hi.x; ++i) {
		for (int j = bounds.lo.y; j < bounds.hi.y; ++j) {
			if (map[i][j].getWaterLevel() == -1) {
				map[i][j].setWaterLevel(MAX_LEVEL);
			}
			//DEBUG_LOG(("Cell %d, %d has waterlevel: %d", i, j, map[i][j].getWaterLevel()));
		}
	}
}

// Chebyshev distance to the nearest water, cliff or map-edge cell, less one, capped at 15
static void calculateGroundClearance(IRegion2D bounds, PathfindCell** map)
{
	constexpr int MAX_CLEARANCE = 15;
	const int dirs[8][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
	const int width = bounds.hi.x - bounds.lo.x + 1;
	const int height = bounds.hi.y - bounds.lo.y + 1;
	if (width <= 0 || height <= 0) {
		return;
	}
	const UnsignedByte UNVISITED = 255;
	std::vector<UnsignedByte> dist(width*height, UNVISITED);
	std::queue<Point> q;

	// water and cliffs seed at distance 0, the border at distance 1 as if the map edge were a source
	for (int i = bounds.lo.x; i <= bounds.hi.x; ++i) {
		for (int j = bounds.lo.y; j <= bounds.hi.y; ++j) {
			PathfindCell::CellType type = map[i][j].getType();
			if (type == PathfindCell::CELL_WATER || type == PathfindCell::CELL_CLIFF) {
				dist[(i-bounds.lo.x)*height + (j-bounds.lo.y)] = 0;
				q.push({ i, j });
			}
		}
	}
	for (int i = bounds.lo.x; i <= bounds.hi.x; ++i) {
		for (int j = bounds.lo.y; j <= bounds.hi.y; ++j) {
			Bool onBorder = (i == bounds.lo.x || i == bounds.hi.x || j == bounds.lo.y || j == bounds.hi.y);
			UnsignedByte& d = dist[(i-bounds.lo.x)*height + (j-bounds.lo.y)];
			if (onBorder && d == UNVISITED) {
				d = 1;
				q.push({ i, j });
			}
		}
	}

	while (!q.empty()) {
		Point curr = q.front();
		q.pop();
		int currentDist = dist[(curr.x-bounds.lo.x)*height + (curr.y-bounds.lo.y)];
		if (currentDist > MAX_CLEARANCE) {
			continue;
		}
		for (const auto& dir : dirs) {
			int nx = curr.x + dir[0];
			int ny = curr.y + dir[1];
			if (nx < bounds.lo.x || nx > bounds.hi.x || ny < bounds.lo.y || ny > bounds.hi.y) {
				continue;
			}
			UnsignedByte& nd = dist[(nx-bounds.lo.x)*height + (ny-bounds.lo.y)];
			if (nd == UNVISITED) {
				nd = currentDist + 1;
				q.push({ nx, ny });
			}
		}
	}

	for (int i = bounds.lo.x; i <= bounds.hi.x; ++i) {
		for (int j = bounds.lo.y; j <= bounds.hi.y; ++j) {
			int d = dist[(i-bounds.lo.x)*height + (j-bounds.lo.y)];
			int clearance = MAX_CLEARANCE;
			if (d == 0) {
				clearance = 0;
			} else if (d != UNVISITED && d - 1 < MAX_CLEARANCE) {
				clearance = d - 1;
			}
			map[i][j].setClearance((UnsignedByte)clearance);
		}
	}
}

static void calculateBridgeHeights(IRegion2D bounds, PathfindCell** map)
{
	if (!TheTerrainLogic) return;

	//DEBUG_LOG(("BRIDGE_HEIGHT: Checking all bridges..."));
	for (Bridge* bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext()) {
		const Region2D* bridgeBounds = bridge->getBounds();
		PathfindLayerEnum layer = bridge->getLayer();

		Int cellLoX = REAL_TO_INT_FLOOR(bridgeBounds->lo.x / PATHFIND_CELL_SIZE_F);
		Int cellLoY = REAL_TO_INT_FLOOR(bridgeBounds->lo.y / PATHFIND_CELL_SIZE_F);
		Int cellHiX = REAL_TO_INT_CEIL(bridgeBounds->hi.x / PATHFIND_CELL_SIZE_F);
		Int cellHiY = REAL_TO_INT_CEIL(bridgeBounds->hi.y / PATHFIND_CELL_SIZE_F);

		if (cellLoX < bounds.lo.x) cellLoX = bounds.lo.x;
		if (cellLoY < bounds.lo.y) cellLoY = bounds.lo.y;
		if (cellHiX > bounds.hi.x) cellHiX = bounds.hi.x;
		if (cellHiY > bounds.hi.y) cellHiY = bounds.hi.y;

		// a thick deck, girders or arches hang below the driving surface and take away room
		TerrainRoadType *bridgeTemplate = TheTerrainRoads->findBridge( bridge->getBridgeTemplateName() );
		Real deckHeight = bridgeTemplate ? bridgeTemplate->getBridgeDeckHeight() : 0.0f;

		for (Int i = cellLoX; i < cellHiX; ++i) {
			for (Int j = cellLoY; j < cellHiY; ++j) {
				Real worldX = ((Real)i + 0.5f) * PATHFIND_CELL_SIZE_F;
				Real worldY = ((Real)j + 0.5f) * PATHFIND_CELL_SIZE_F;

				// Only mark cells whose center is actually under the bridge deck.
				// Cells inside the rectangular bbox but outside the (possibly rotated) deck
				// must NOT be marked, otherwise they form a moat in front of the entry strip
				// and the only reachable connect cells are at the bbox corners.
				Coord3D cellCenter;
				cellCenter.x = worldX;
				cellCenter.y = worldY;
				cellCenter.z = 0.0f;
				if (!bridge->isPointOnBridge(&cellCenter)) {
					continue;
				}

				// the deck plane through the two bridge points is what a unit passing below clears
				Real bridgeZ = TheTerrainLogic->getLayerHeight(worldX, worldY, layer);

				// isUnderwater leaves waterZ untouched where there is no water, so seed both
				Real waterZ = 0.0f, groundZ = 0.0f;
				TheTerrainLogic->isUnderwater(worldX, worldY, &waterZ, &groundZ);
				Real baseZ   = (waterZ > groundZ) ? waterZ : groundZ;

				Real gap = bridgeZ - deckHeight - baseZ;
				if (gap < 0.0f) gap = 0.0f;

				Int encoded = (Int)(gap / 10.0f);
				if (encoded > 15) encoded = 15;

				//DEBUG_LOG(("BRIDGE_HEIGHT: (%d, %d) -> %d", i, j, encoded));
				//DEBUG_LOG(("BRIDGE_HEIGHT: (%d, %d) -> Layer %d", i, j, static_cast<int>(layer)));
				map[i][j].setBridgeHeight((UnsignedByte)encoded);
				map[i][j].setBridgeLayer(layer);
			}
		}
	}
}

/**
 * Set up for a new map.
 */
void Pathfinder::newMap()
{
	m_wallHeight = TheAI->getAiData()->m_wallHeight; // may be updated by map.ini.
	Region3D terrainExtent;
	TheTerrainLogic->getMaximumPathfindExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	Bool dataAllocated = false;
	if (m_extent.hi.x==bounds.hi.x && m_extent.hi.y==bounds.hi.y) {
		if (m_blockOfMapCells != nullptr && m_map!=nullptr) {
			dataAllocated = true;
		}
	}
	// For map load from file, we have to call newMap twice to do sequencing issues.
	// so the second time through, dataAllocated==TRUE, so we skip the allocate.
	if (!dataAllocated) {
		m_extent = bounds;
		DEBUG_ASSERTCRASH(m_map == nullptr, ("Can't reallocate pathfind cells."));
 		m_zoneManager.allocateBlocks(m_extent);
		// Allocate cells.
		m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[(bounds.hi.x+1)*(bounds.hi.y+1)];
		m_map = MSGNEW("PathfindMapCells") PathfindCellP[bounds.hi.x+1];
		Int i;
		for (i=0; i<=bounds.hi.x; i++) {
			m_map[i] = &m_blockOfMapCells[i*(bounds.hi.y+1)];
		}
		for (i=0; i<LAYER_LAST; i++) {
			if (!m_layers[i].isUnused()) {
				m_layers[i].allocateCells(&m_extent);
			}
		}
		if (m_numWallPieces>0) {
			m_layers[LAYER_WALL].init(nullptr, LAYER_WALL);
			m_layers[LAYER_WALL].allocateCellsForWallLayer(&m_extent, m_wallPieces, m_numWallPieces);
		}
	}
	classifyMap();
	// Add existing objects.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		classifyObjectFootprint(obj, true);
	}

	m_isMapReady = true;
}

/**
 * Classify all cells in grid as obstacles, etc.
 */
void Pathfinder::classifyMap()
{

	Int i, j;
	// for now, sample cell corners and classify cell accordingly
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			classifyMapCell( i, j, &m_map[i][j]);
		}
	}
	// Clearance is measured from true cliffs, before the dilation below turns their ring into cliff cells
	calculateGroundClearance(m_extent, m_map);
#if 1
	// Expand all cliff cells one step (mark pinched)
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);
						}

					}
				}
			}
		}
	}
	// Convert pinched to cliff.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getPinched()) {
				if (m_map[i][j].getType()==PathfindCell::CELL_CLEAR) {
					m_map[i][j].setType(PathfindCell::CELL_CLIFF);
				}
			}
		}
	}
	// Add a border of pinched cells to cliffs.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);
						}

					}
				}
			}
		}
	}
#endif
	DEBUG_LOG(("WATER LEVEL: Checking map"));
	// set water depth values for ship navigation
	calculateWaterLevels(m_extent, m_map);

	DEBUG_LOG(("BRIDGE_HEIGHT: Classify map"));
	// set bridge height values and layers to all cells
	calculateBridgeHeights(m_extent, m_map);

	for (i=0; i<LAYER_LAST; i++) {
		if (!m_layers[i].isUnused()) {
			m_layers[i].classifyCells();
		}
	}
	if (!m_layers[LAYER_WALL].isUnused()) {
		m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
	}
	m_zoneManager.calculateZones(m_map, m_layers, m_extent);
	if (PathTuning::Clearance) {
		buildClearance();
	}
}

// TheSuperHackers @feature simonx645 16/11/2026 "clearance" (PathTuning.h): Chebyshev distances to the nearest water and
// cliff cell, by two passes over the map (each cell takes the smallest neighbour seen so far plus one: down and across,
// then back up). Water and cliff cells only change when the map is classified again, which builds these again.
void Pathfinder::freeClearance()
{
	delete[] m_waterRings;
	m_waterRings = nullptr;
	delete[] m_cliffRings;
	m_cliffRings = nullptr;
}

void Pathfinder::buildClearance()
{
	freeClearance();
	const Int width = m_extent.hi.x - m_extent.lo.x + 1;
	const Int height = m_extent.hi.y - m_extent.lo.y + 1;
	if (m_map == nullptr || width <= 0 || height <= 0) {
		return;
	}
	m_waterRings = MSGNEW("PathfindClearance") UnsignedByte[width * height];
	m_cliffRings = MSGNEW("PathfindClearance") UnsignedByte[width * height];
	const UnsignedByte unreached = (UnsignedByte)(PathTuning::CLEARANCE_MAX + 1);
	for (Int x = 0; x < width; x++) {
		for (Int y = 0; y < height; y++) {
			const PathfindCell::CellType type = m_map[x + m_extent.lo.x][y + m_extent.lo.y].getType();
			m_waterRings[x * height + y] = type == PathfindCell::CELL_WATER ? 0 : unreached;
			m_cliffRings[x * height + y] = type == PathfindCell::CELL_CLIFF ? 0 : unreached;
		}
	}
	UnsignedByte *maps[2] = { m_waterRings, m_cliffRings };
	for (Int m = 0; m < 2; m++) {
		UnsignedByte *d = maps[m];
		for (Int x = 0; x < width; x++) {
			for (Int y = 0; y < height; y++) {
				Int best = d[x * height + y];
				if (best == 0) continue;
				if (y > 0) best = min(best, d[x * height + y - 1] + 1);
				if (x > 0) {
					best = min(best, d[(x - 1) * height + y] + 1);
					if (y > 0) best = min(best, d[(x - 1) * height + y - 1] + 1);
					if (y < height - 1) best = min(best, d[(x - 1) * height + y + 1] + 1);
				}
				d[x * height + y] = (UnsignedByte)min(best, (Int)unreached);
			}
		}
		for (Int x = width - 1; x >= 0; x--) {
			for (Int y = height - 1; y >= 0; y--) {
				Int best = d[x * height + y];
				if (best == 0) continue;
				if (y < height - 1) best = min(best, d[x * height + y + 1] + 1);
				if (x < width - 1) {
					best = min(best, d[(x + 1) * height + y] + 1);
					if (y < height - 1) best = min(best, d[(x + 1) * height + y + 1] + 1);
					if (y > 0) best = min(best, d[(x + 1) * height + y - 1] + 1);
				}
				d[x * height + y] = (UnsignedByte)min(best, (Int)unreached);
			}
		}
	}
}

Int Pathfinder::ringShortage( Int cellX, Int cellY, LocomotorSurfaceTypeMask surfaces, Int radius, Int *hardShortage ) const
{
	*hardShortage = 0;
	if (radius <= 0 || m_waterRings == nullptr) {
		return 0;
	}
	// ground units only; their whole locomotor set counts, so an amphibious unit ignores water and a climber cliffs
	if (!(surfaces & LOCOMOTORSURFACE_GROUND) || (surfaces & LOCOMOTORSURFACE_AIR)) {
		return 0;
	}
	if (cellX < m_extent.lo.x || cellY < m_extent.lo.y || cellX > m_extent.hi.x || cellY > m_extent.hi.y) {
		return 0;
	}
	const Int height = m_extent.hi.y - m_extent.lo.y + 1;
	const Int index = (cellX - m_extent.lo.x) * height + (cellY - m_extent.lo.y);
	Int shortage = 0;
	// clearance: the rings between the cell and the nearest water (or cliff) cell; a footprint of radius r hangs
	// r - clearance rings over it
	if (!(surfaces & LOCOMOTORSURFACE_WATER)) {
		const Int rings = m_waterRings[index];
		const Int clearance = rings > 0 ? rings - 1 : 0;
		shortage = max(shortage, radius - clearance);
	}
	if (!(surfaces & LOCOMOTORSURFACE_CLIFF)) {
		// classifyMap widens every cliff by one ring of cells (and pins the next), so the first cliff cell is already a
		// ring of slope: counted from the cliff itself, as water is, the clearance is one more
		const Int clearance = m_cliffRings[index];
		shortage = max(shortage, radius - clearance);
	}
	*hardShortage = shortage;
	// the playable edge, which costs but never refuses: units still drive on and off the map
	if (m_logicalExtent.hi.x > m_logicalExtent.lo.x && m_logicalExtent.hi.y > m_logicalExtent.lo.y) {
		Int edge = 0;
		if (cellX >= m_logicalExtent.lo.x && cellY >= m_logicalExtent.lo.y && cellX <= m_logicalExtent.hi.x && cellY <= m_logicalExtent.hi.y) {
			edge = min(min(cellX - m_logicalExtent.lo.x, m_logicalExtent.hi.x - cellX), min(cellY - m_logicalExtent.lo.y, m_logicalExtent.hi.y - cellY));
		}
		shortage = max(shortage, radius - edge);
	}
	return shortage;
}


/**
 * Force pathfind map recomputation.
 */
void Pathfinder::forceMapRecalculation()
{
	classifyMap();
}

/**
 * Show all cells touched in the last search
 */
void Pathfinder::debugShowSearch(  Bool pathFound  )
{
	if (!TheGlobalData->m_debugAI) {
		return;
	}
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

	// show all explored cells for debugging
	PathfindCell *s;
	RGBColor color;
	color.red = color.blue = color.green = 1;
	if (!pathFound) {
		addIcon(nullptr, 0, 0, color);	 // erase.
	}

	for( s = m_openList.getHead(); s; s=s->getNextOpen() )
	{
		// create objects to show path - they decay
		RGBColor color;
		color.red = color.green = 0;
		color.blue = 1;

		Coord3D pos;
		pos.x = ((Real)s->getXIndex() + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)s->getYIndex() + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, s->getLayer() ) + 0.5f;
		addIcon(&pos, PATHFIND_CELL_SIZE_F*.6f, 200, color);
	}

	for( s = m_closedList.getHead(); s; s=s->getNextOpen() )
	{
		// create objects to show path - they decay
		RGBColor color;
		color.red = color.blue = 1;
		color.green = 0;
		if (!pathFound)	color.blue = 0;

		Int length=200;
		if (!pathFound)
			length *= 2;

		Coord3D pos;
		pos.x = ((Real)s->getXIndex() + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)s->getYIndex() + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, s->getLayer()) + 0.5f;
		addIcon(&pos, PATHFIND_CELL_SIZE_F*.6f, length, color);
	}
#endif
}

Locomotor* Pathfinder::chooseBestLocomotorForPosition(PathfindLayerEnum layer, LocomotorSet* locomotorSet, const Coord3D* pos )
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	PathfindCell* cell = getCell(layer, x, y );
	// off the map? call it CELL_CLEAR...
	PathfindCell::CellType celltype = cell ? cell->getType() : PathfindCell::CELL_CLEAR;

	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(celltype);
	return locomotorSet->findLocomotor(acceptableSurfaces);
}

/*static*/ LocomotorSurfaceTypeMask Pathfinder::validLocomotorSurfacesForCellType(PathfindCell::CellType t)
{
	switch (t)
	{
		case PathfindCell::CELL_CLEAR:
			return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_AIR;

		case PathfindCell::CELL_WATER:
			return LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_AIR;

		case PathfindCell::CELL_CLIFF:
			return LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR;

		case PathfindCell::CELL_RUBBLE:
			return LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_AIR;

		case PathfindCell::CELL_OBSTACLE:
		case PathfindCell::CELL_BRIDGE_IMPASSABLE:
		case PathfindCell::CELL_IMPASSABLE:
			return LOCOMOTORSURFACE_AIR;

		default:
			return NO_SURFACES;
	}
}

//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementTerrain( PathfindLayerEnum layer, const Locomotor* locomotor, const Coord3D *pos)
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);

	PathfindCell *toCell = nullptr;
	toCell = getCell( layer, x, y );

	if (toCell == nullptr)
		return false;
	// Only do terrain, not obstacle cells.  jba.
	if (toCell->getType()==PathfindCell::CELL_OBSTACLE) return true;
	if (toCell->getType()==PathfindCell::CELL_IMPASSABLE) return true;
	if (toCell->getLayer()!=LAYER_GROUND && toCell->getType() == PathfindCell::CELL_CLEAR) {
		return true;
	}
	// check validity of destination cell
	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(toCell->getType());
	if ((locomotor->getLegalSurfaces() & acceptableSurfaces) == 0)
		return false;

	if ((locomotor->getAppearance() == LOCO_SHIP)) {
		if (toCell->getWaterLevel() < locomotor->getRequireWaterLevel()) {
			return false;
		}
	}
	return true;
}

/**
 * validMovementTerrain plus the ground clearance the object's footprint needs
 */
Bool Pathfinder::validMovementTerrainForObject( const Object *obj, const Locomotor* locomotor, const Coord3D *pos)
{
	if (!validMovementTerrain(obj->getLayer(), locomotor, pos)) {
		return false;
	}
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	const PathfindCell *cell = getCell(obj->getLayer(), x, y);
	if (cell == nullptr) {
		return false;
	}
	Int radius;
	Bool centerInCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	LocomotorSurfaceTypeMask surfaces = locomotor->getLegalSurfaces();
#if RTS_ZEROHOUR
	if (obj->getAIUpdateInterface())
	{
		surfaces = obj->getAIUpdateInterface()->getLocomotorSet().getValidSurfaces();
	}
#endif
	return getClearanceShortage(cell, surfaces, radius) < CLEARANCE_HARD_SHORTAGE;
}

/**
 * How many footprint cells hang past the ground clearance of this cell, 0 when the unit does not care
 */
Int Pathfinder::getClearanceShortage( const PathfindCell *cell, LocomotorSurfaceTypeMask surfaces, Int radius ) const
{
	if (radius <= 0 || cell->getLayer() != LAYER_GROUND) {
		return 0;
	}
	if (!(surfaces & LOCOMOTORSURFACE_GROUND) || (surfaces & (LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR))) {
		return 0;
	}
	Int shortage = radius - (Int)cell->getClearance();
	return shortage > 0 ? shortage : 0;
}

//
// Releases the cells on the open & closed lists.
//
void Pathfinder::cleanOpenAndClosedLists() {
	Int count = 0;
	if (!m_openList.empty()) {
		count += PathfindCell::releaseOpenList(m_openList);
		m_openList.reset();
	}

#if RETAIL_COMPATIBLE_PATHFINDING
	// TheSuperHackers @info this is here as the map cells are contained within the pathfinder and cannot be cleaned externally.
	// If the crash mode within PathfindCell::releaseOpenList is hit, it will set s_forceCleanCells to allow the system to cleanly recover.
	if (s_forceCleanCells) {
		forceCleanCells();
		// TheSuperHackers @info cells on the closed list are forcefully cleaned up by this point
		s_forceCleanCells = false;
	}
#endif

	if (!m_closedList.empty()) {
		count += PathfindCell::releaseClosedList(m_closedList);
		m_closedList.reset();
	}

#if RETAIL_COMPATIBLE_PATHFINDING
	// TheSuperHackers @info this is here and performs the same function as the above block, but for when the crash occurs within the closed list.
	// If the crash mode within PathfindCell::releaseClosedList is hit, it will set s_forceCleanCells to allow the system to cleanly recover.
	if (s_forceCleanCells) {
		forceCleanCells();
		s_forceCleanCells = false;
	}
#endif

	m_cumulativeCellsAllocated += count;
	s_pathfindStats.cells += count;
	if ((UnsignedInt)count > s_pathfindStats.maxCellsOneSearch) {
		s_pathfindStats.maxCellsOneSearch = count;
	}
}

/**
 * Sets the cell cap for the search about to start from what the frame has left
 */
void Pathfinder::beginSearchBudget( PathfindSearchType type )
{
	s_pathfindStats.byType[type]++;
	Int remaining = PATHFIND_FRAME_CELL_LIMIT - m_cumulativeCellsAllocated;
	if (remaining < PATHFIND_SEARCH_CELL_MIN) {
		remaining = PATHFIND_SEARCH_CELL_MIN;
	}
	if (remaining > PATHFIND_SEARCH_CELL_CAP) {
		remaining = PATHFIND_SEARCH_CELL_CAP;
	}
	// TheSuperHackers @feature simonx645 16/11/2026 "cap" (GameLogic/PathTuning.h): the same cap for every search,
	// whatever else searched this frame, so a route depends on its own search only.
	if (PathTuning::SearchCap) {
		remaining = PathTuning::SEARCH_CAP_CELLS;
	}
	m_searchCellCap = remaining;
	m_searchStartFreeCount = PathfindCellInfo::getFreeCount();
	m_lastSearchHitCap = false;
	s_pathfindStats.searches++;
}

Bool Pathfinder::isSearchBudgetExhausted()
{
	if (m_searchStartFreeCount - PathfindCellInfo::getFreeCount() < m_searchCellCap)
	{
		return false;
	}
	m_lastSearchHitCap = true;
	return true;
}

void Pathfinder::countStatEvent( PathfindStatEvent event )
{
	s_pathfindStats.events[event]++;
}


//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementPosition( Bool isCrusher, LocomotorSurfaceTypeMask acceptableSurfaces, Int requiredWaterLevel, Short requiredBridgeHeight,
																			 PathfindCell *toCell, PathfindCell *fromCell)
{
	if (toCell == nullptr)
		return false;

	// check if the destination cell is classified as an obstacle,
	// and we happen to be ignoring it
	if (toCell->isObstaclePresent( m_ignoreObstacleID ))
		return true;

	if (isCrusher && toCell->isObstacleFence()) {
		return true;
	}

	// check validity of destination cell
	LocomotorSurfaceTypeMask cellSurfaces = validLocomotorSurfacesForCellType(toCell->getType());
	if ((cellSurfaces & acceptableSurfaces) == 0)
		return false;

	// Check for water depth validity of ships
	if (requiredWaterLevel >0 && toCell->getType() == PathfindCell::CELL_WATER) {
		if (toCell->getWaterLevel() < requiredWaterLevel) {
			return false;
		}
	}

	// Check if cell has a repaired/closed bridge above.
	// Skip layer-transition (connect) cells — those are bridge ramp/entry points where deck meets ground.
	if (toCell->getLayer() == LAYER_GROUND
			&& toCell->getBridgeLayer() > LAYER_GROUND
			&& toCell->getConnectLayer() == LAYER_INVALID
			&& TheAI->pathfinder()->isPathfindLayerPassable(toCell->getBridgeLayer())) {
		// Do a height check
		if (static_cast<Short>(toCell->getBridgeHeight()) < requiredBridgeHeight) {
			return false;
		}
	}

	return true;
}

/**
 * Checks to see if obj can occupy the pathfind cell at x,y.
 * Returns false if there is another unit's goal already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkDestination(const Object *obj, Int cellX, Int cellY, PathfindLayerEnum layer, Int iRadius, Bool centerInCell)
{
	// If obj==nullptr, means we are checking for any ground units present.  jba.
	Int numCellsAbove = iRadius;
	if (centerInCell) numCellsAbove++;
	Bool checkForAircraft = false;
	Int i, j;
	ObjectID ignoreId = INVALID_ID;
	ObjectID objID = INVALID_ID;
	if (obj && obj->getAIUpdateInterface()) {
		ignoreId =  obj->getAIUpdateInterface()->getIgnoredObstacleID();
		checkForAircraft = obj->getAI()->isAircraftThatAdjustsDestination();
		objID = obj->getID();
		const PathfindCell *centerCell = getCell(layer, cellX, cellY);
		if (PathTuning::Clearance) {
			// TheSuperHackers @feature simonx645 16/11/2026 "clearance" (PathTuning.h): no goal the footprint cannot stand
			// on, except where the unit stands now (so a unit too close can still be given a route from there) and inside
			// the structure it is going into
			if (layer == LAYER_GROUND) {
				Int hard;
				ringShortage(cellX, cellY, obj->getAIUpdateInterface()->getLocomotorSet().getValidSurfaces(), iRadius, &hard);
				if (hard >= PathTuning::CLEARANCE_HARD) {
					ICoord2D here;
					worldToCell(obj->getPosition(), &here);
					if (!(here.x == cellX && here.y == cellY) && !(centerCell && centerCell->isObstaclePresent(ignoreId))) {
						return false;
					}
				}
			}
		}
		else if (centerCell && getClearanceShortage(centerCell, obj->getAIUpdateInterface()->getLocomotorSet().getValidSurfaces(), iRadius) >= CLEARANCE_HARD_SHORTAGE) {
			return false;
		}
	}
	for (i=cellX-iRadius; i<cellX+numCellsAbove; i++) {
		for (j=cellY-iRadius; j<cellY+numCellsAbove; j++) {
			PathfindCell	*cell = getCell(layer, i, j);
			if (!cell) {
				return false; // off the map, so can't place here.
			}

			if (checkForAircraft) {
				if (!cell->isAircraftGoal()) {
					continue;
				}
				if (cell->getGoalAircraft() == objID) {
					continue;
				}
				return false;
			}

			if (cell->getType()==PathfindCell::CELL_OBSTACLE) {
				if (cell->isObstaclePresent( ignoreId ))
					continue;
				return false;
			}

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
			if (IS_IMPASSABLE(cell->getType())) {
				return false;
			}
#endif

			if (cell->getFlags() == PathfindCell::NO_UNITS) {
				continue;  // Nobody is here, so it's ok.
			}

			ObjectID goalUnitID = cell->getGoalUnit();
			if (goalUnitID == objID) {
				continue; // we got it.
			}

			if (goalUnitID == ignoreId) {
				continue; // we are ignoring it.
			}

			if (goalUnitID == INVALID_ID) {
				continue;
			}

			if (!obj) {
				return false;
			}
			Object *unit = TheGameLogic->findObjectByID(goalUnitID);
			if (!unit) {
				continue;
			}

			// order matters: we want to know if I consider it to be an ally, not vice versa
			if (obj->getRelationship(unit) == ALLIES) {
				return false; 	// Don't usurp your allies goals.  jba.
			}
			if (cell->getFlags()==PathfindCell::UNIT_PRESENT_FIXED) {
				Bool canCrush = obj->canCrushOrSquish(unit, TEST_CRUSH_OR_SQUISH);
				if (!canCrush) {
					return false; // Don't move to an occupied cell.
				}
			}
		}
	}
	return true;
}

/**
 * Checks to see if obj can move through the pathfind cell at x,y.
 * Returns false if there are other units already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkForMovement(const Object *obj, TCheckMovementInfo &info)
{
	info.allyFixedCount = 0;
	info.allyMoving = false;
	info.allyGoal = false;
	info.enemyFixed = false;

	const Int		maxAlly = 5;
	ObjectID		allies[maxAlly];
	Int					numAlly = 0;

	if (!obj) {
		return true; // not object can move there.
	}

	ObjectID ignoreId = INVALID_ID;
	if (obj->getAIUpdateInterface()) {
		ignoreId =  obj->getAIUpdateInterface()->getIgnoredObstacleID();
	}

	Int numCellsAbove = info.radius;
	if (info.centerInCell) numCellsAbove++;
	Int i, j;
//	Bool isInfantry = obj->isKindOf(KINDOF_INFANTRY);
	for (i=info.cell.x-info.radius; i<info.cell.x+numCellsAbove; i++) {
		for (j=info.cell.y-info.radius; j<info.cell.y+numCellsAbove; j++) {
			PathfindCell	*cell = getCell(info.layer,i, j);
			if (!cell) {
				return false; // off the map, so can't move here.
			}

			PathfindCell::CellFlags flags = cell->getFlags();
			if ((flags == PathfindCell::UNIT_GOAL) || (flags == PathfindCell::UNIT_GOAL_OTHER_MOVING)) {
				info.allyGoal = true;
			} else if (flags == PathfindCell::NO_UNITS) {
				continue;  // Nobody is here, so it's ok.
			}

			ObjectID posUnit = cell->getPosUnit();
			if (posUnit == obj->getID()) {
				continue; // we got it.
			}

			if (posUnit == ignoreId) {
				continue; // we are ignoring this one.
			}

			Bool check = false;
			Object *unit = nullptr;
			if (flags == PathfindCell::UNIT_PRESENT_MOVING || flags == PathfindCell::UNIT_GOAL_OTHER_MOVING) {
				unit = TheGameLogic->findObjectByID(posUnit);
				// order matters: we want to know if I consider it to be an ally, not vice versa
				if (unit && obj->getRelationship(unit) == ALLIES) {
					info.allyMoving = true;
				}
				if (info.considerTransient) {
					check = true;
				}
			}
			if (flags == PathfindCell::UNIT_PRESENT_FIXED) {
				check = true;
				unit = TheGameLogic->findObjectByID(posUnit);
			}
			if (check && unit!=nullptr) {
				if (obj->getAIUpdateInterface() && obj->getAIUpdateInterface()->getIgnoredObstacleID()==unit->getID()) {
					// Don't check if it's the ignored obstacle.
					check = false;
				}
			}
			if (!check || !unit) {
				continue;
			}

#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
			if (obj->isKindOf(KINDOF_INFANTRY) && unit->isKindOf(KINDOF_INFANTRY)) {
				// Infantry can run through infantry.
				continue; //
			}
#endif
			// See if it is an ally.
			// order matters: we want to know if I consider it to be an ally, not vice versa
			if (obj->getRelationship(unit) == ALLIES) {
				if (!unit->getAIUpdateInterface()) {
					return false; // can't path through not-idle units.
				}
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
				if (!unit->getAIUpdateInterface()->isIdle()) {
					return false; // can't path through not-idle units.
				}
#endif
				Bool found = false;
				Int k;
				for (k=0; k<numAlly; k++) {
					if (allies[k] == unit->getID()) {
						found = true;
					}
				}
				if (!found) {
					info.allyFixedCount++;
					if (numAlly < maxAlly) {
						allies[numAlly] = unit->getID();
						numAlly++;
					}
				}
			} else {
				Bool canCrush = obj->canCrushOrSquish( unit, TEST_CRUSH_OR_SQUISH );
				if (!canCrush) {
					info.enemyFixed = true;
				}
			}
		}
	}
	return true;
}

/**
 * Adjusts a coordinate to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
}

/**
 * Adjusts a goal position to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapClosestGoalPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	if (checkDestination(obj, cell.x, cell.y , layer, iRadius, center)) {
		return;
	}

	// Try adjusting by 1.
	Int i,j;
	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			if (checkDestination(obj, i, j, layer, iRadius, center)) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}

	if (iRadius > 0)
		return;

	// Try to find an unoccupied cell.
	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			PathfindCell* newCell = getCell(layer, i, j);
			if (!newCell)
				continue;

			if (newCell->getGoalUnit() == INVALID_ID || newCell->getGoalUnit() == obj->getID()) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}

	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			PathfindCell* newCell = getCell(layer, i, j);
			if (!newCell)
				continue;

			if (newCell->getFlags()!=PathfindCell::UNIT_PRESENT_FIXED) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}
}

/**
 * Returns coordinates of goal.
 *
 */
Bool Pathfinder::goalPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return false; // only consider ai objects.
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell = *ai->getPathfindGoalCell();
	pos->zero();
	if (cell.x<0 || cell.y<0) return false;
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	return true;
}


Bool Pathfinder::checkForAdjust(Object *obj, const LocomotorSet& locomotorSet, Bool isHuman,
																Int cellX, Int cellY, PathfindLayerEnum layer,
																Int iRadius, Bool center, Coord3D *dest, const Coord3D *groupDest)
{
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	if (cellP && cellP->getType() == PathfindCell::CELL_CLIFF) {
		return false;  // no final destinations on cliffs.
	}
	if (isHuman) {
		// check if new cell is in logical map.	(computer can move off logical map)
		if (cellX < m_logicalExtent.lo.x ||
				cellY < m_logicalExtent.lo.y ||
				cellX > m_logicalExtent.hi.x ||
				cellY > m_logicalExtent.hi.y) return false;
	}
	if (checkDestination(obj, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		Bool adjustedPathExists;
		if (obj->isKindOf(KINDOF_AIRCRAFT)) {
			adjustedPathExists = true;
		}	else {
			Bool pathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getRequiredBridgeHeight(), obj->getPosition(), dest);
			if (!pathExists && clientSafeQuickDoesPathExist( locomotorSet, obj->getRequiredBridgeHeight(), dest, &adjustDest)) {
				adjustedPathExists = true;
			} else {
				adjustedPathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getRequiredBridgeHeight(), obj->getPosition(), &adjustDest);
			}
		}
		if ( adjustedPathExists	) {
			if (groupDest) {
				tightenPath(obj, locomotorSet, &adjustDest, groupDest);
				// Check to see if it is a long way to get to the adjusted destination.
				Int cost = checkPathCost(obj, locomotorSet, groupDest, &adjustDest);
				Int dx = IABS(groupDest->x-adjustDest.x);
				Int dy = IABS(groupDest->y-adjustDest.y);
				if (1.4f*(dx+dy)<cost) {
					return false;
				}
			}
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

Bool Pathfinder::checkForLanding(Int cellX, Int cellY, PathfindLayerEnum layer,
																Int iRadius, Bool center, Coord3D *dest)
{
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	switch (cellP->getType())
	{
		case PathfindCell::CELL_CLIFF:
		case PathfindCell::CELL_WATER:
		case PathfindCell::CELL_IMPASSABLE:
			return false;  // no final destinations on cliffs, water, etc.
	}
	if (checkDestination(nullptr, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		*dest = adjustDest;
		return true;
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to land at.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToLandingDestination(Object *obj, Coord3D *dest)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	// If the object is off the map & the goal is off the map, it is a scripted setup, so just
	// go to the dest.
	if (!extent.isInRegionNoZ(*dest)) {
		if (!extent.isInRegionNoZ(*obj->getPosition())) {
			return true;
		}
	}

	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );

	Int limit = MAX_ADJUSTMENT_CELL_COUNT;
	Int i, j;
	i = cell.x;
	j = cell.y;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (checkForLanding(i,j, layer, iRadius, center, dest)) {
		return true;
	}

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
	}
	return false;
}


struct SlotCandidate
{
	ICoord2D cell;
	PathfindLayerEnum layer;
	Int ring;
	Real projection;
	Int order;
};

// Innermost ring first, then the far side of the ring along the travel direction, then scan order
static Bool slotCandidateLess(const SlotCandidate &a, const SlotCandidate &b)
{
	if (a.ring != b.ring) {
		return a.ring < b.ring;
	}
	if (a.projection != b.projection) {
		return a.projection > b.projection;
	}
	return a.order < b.order;
}

// Units nearest the goal along the travel direction first, object id breaks ties
static Bool slotRequestLess(const PathfindSlotRequest &a, const PathfindSlotRequest &b)
{
	if (a.projection != b.projection) {
		return a.projection > b.projection;
	}
	return a.obj->getID() < b.obj->getID();
}

// Slots fill innermost ring first, far side first, and units take them in the same order, so early arrivals park deepest
Bool Pathfinder::assignGroupSlots(std::vector<PathfindSlotRequest>& units, const Coord3D *click, const Coord3D *centroid)
{
	const Int n = units.size();
	if (n < 2 || !m_isMapReady) {
		return false;
	}

	// One member stands in for the group in occupancy checks, so enemy claims are ignored and allied ones respected
	const Object *representative = units[0].obj;
	Bool isHuman = true;
	const Player *player = representative->getControllingPlayer();
	if (player && player->getPlayerType() == PLAYER_COMPUTER) {
		isHuman = false;
	}

	// The lattice step comes from the largest footprint so any slot fits any unit
	Int maxRadius = 0;
	Bool maxCenter = true;
	Int maxSpan = 1;
	for (Int i = 0; i < n; i++) {
		Int radius;
		Bool center;
		getRadiusAndCenter(units[i].obj, radius, center);
		Int span = 2*radius + (center ? 1 : 0);
		if (span > maxSpan) {
			maxSpan = span;
			maxRadius = radius;
			maxCenter = center;
		}
	}
	const Int step = (maxSpan > 1) ? maxSpan + 1 : 1;

	Coord2D dir;
	dir.x = click->x - centroid->x;
	dir.y = click->y - centroid->y;
	if (dir.length() < PATHFIND_CELL_SIZE_F) {
		dir.x = 1.0f;
		dir.y = 0.0f;
	}
	dir.normalize();

	PathfindLayerEnum destLayer = TheTerrainLogic->getLayerForDestination(click);
	ICoord2D clickCell;
	worldToCell(click, &clickCell);

	std::vector<SlotCandidate> slots;
	slots.reserve(n);
	Int scanned = 0;
	const Int scanLimit = 4*n + 64;
	Bool haveRefZone = false;
	zoneStorageType refZone = 0;
	for (Int ring = 0; ring <= PATHFIND_SLOT_MAX_RING; ring++) {
		if ((Int)slots.size() >= n || scanned >= scanLimit) {
			break;
		}
		for (Int i = -ring; i <= ring; i++) {
			for (Int j = -ring; j <= ring; j++) {
				if (IABS(i) != ring && IABS(j) != ring) {
					continue;
				}
				scanned++;
				ICoord2D c;
				c.x = clickCell.x + i*step;
				c.y = clickCell.y + j*step;
				PathfindCell *cell = getCell(destLayer, c.x, c.y);
				if (cell == nullptr || cell->getType() != PathfindCell::CELL_CLEAR) {
					continue;
				}
				if (maxSpan > 1 && cell->getPinched()) {
					continue;
				}
				if (isHuman && (c.x < m_logicalExtent.lo.x || c.x > m_logicalExtent.hi.x || c.y < m_logicalExtent.lo.y || c.y > m_logicalExtent.hi.y)) {
					continue;
				}
				if (getClearanceShortage(cell, LOCOMOTORSURFACE_GROUND, maxRadius) > 0) {
					continue;
				}
				// Off the deck a bridge-layer lookup falls through to the ground cell, so the slot keeps the cell's own layer
				PathfindLayerEnum slotLayer = cell->getLayer();
				if (!checkDestination(representative, c.x, c.y, slotLayer, maxRadius, maxCenter)) {
					continue;
				}
				// Every slot has to sit in the zone of the first one, or units would end up on the far side of a wall
				zoneStorageType zone = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, cell->getZone());
				if (!haveRefZone) {
					refZone = zone;
					haveRefZone = true;
				} else if (zone != refZone) {
					continue;
				}
				Coord3D slotPos;
				adjustCoordToCell(c.x, c.y, maxCenter, slotPos, slotLayer);
				SlotCandidate candidate;
				candidate.cell = c;
				candidate.layer = slotLayer;
				candidate.ring = ring;
				candidate.projection = (slotPos.x - click->x)*dir.x + (slotPos.y - click->y)*dir.y;
				candidate.order = slots.size();
				slots.push_back(candidate);
			}
		}
	}
	if (slots.empty()) {
		return false;
	}
	std::sort(slots.begin(), slots.end(), slotCandidateLess);

	for (Int i = 0; i < n; i++) {
		const Coord3D *pos = units[i].obj->getPosition();
		units[i].projection = (pos->x - click->x)*dir.x + (pos->y - click->y)*dir.y;
		units[i].assigned = false;
	}
	std::sort(units.begin(), units.end(), slotRequestLess);

	Int count = MIN(n, (Int)slots.size());
	for (Int i = 0; i < count; i++) {
		Int radius;
		Bool center;
		getRadiusAndCenter(units[i].obj, radius, center);
		adjustCoordToCell(slots[i].cell.x, slots[i].cell.y, center, units[i].slot, slots[i].layer);
		updateGoal(units[i].obj, &units[i].slot, slots[i].layer);
		units[i].assigned = true;
	}
	return true;
}

/**
 * Find an unoccupied spot for a unit to move to.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest, const Coord3D *groupDest)
{
	if( obj->isKindOf(KINDOF_PROJECTILE) )
	{
		return true; // missiles can go wherever they want to. jba.
	}

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D cellDest = *dest;
	if (!center) {
		cellDest.x += PATHFIND_CELL_SIZE_F/2;
		cellDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &cellDest, &cell );
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (groupDest) {
		layer = TheTerrainLogic->getLayerForDestination(groupDest);
	}

	Int i = cell.x;
	Int j = cell.y;
	// Check the center cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (checkForAdjust(obj, locomotorSet, isHuman, i, j, layer, iRadius, center, dest, groupDest)) {
		return true;
	}
#else
	Coord3D adjustDest = *dest;
	if (checkForAdjust(obj, locomotorSet, isHuman, i, j, layer, iRadius, center, &adjustDest, groupDest)) {
		// TheSuperHackers @bugfix stephanmeesters 15/06/2026 Destination adjustment always snaps to the nearest grid cell
		// even when no adjustment is necessary because there are no obstructions. For single units this adjustment
		// can be skipped in order to provide more accurate movement, which is especially noticeable for chinooks.
		const Bool singleUnit = obj && obj->getGroup() && obj->getGroup()->getCount() == 1;
		const Bool useExactDestination = isHuman && singleUnit;
		if (!useExactDestination) {
			*dest = adjustDest;
		}
		return true;
	}
#endif

	// TheSuperHackers @info Expanding counter-clockwise spiral search around center cell C. Each full lap walks right->up->left->down.
	// After every pair of directions (right+up, then left+down) length of the segment grows by 1.
	//
	//    <------  12
	//    4  3  2  11
	//    5  C  1  10
	//    6  7  8  9
	//
	Int limit = MAX_ADJUSTMENT_CELL_COUNT;
	Int segmentLength = 1;
	const ICoord2D directions[4] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };
	while (limit>0) {
		for (Int dir = 0; dir < 4; dir++) {
			for (Int count = segmentLength; count>0; count--) {
				i+=directions[dir].x;
				j+=directions[dir].y;
				limit--;
				if (checkForAdjust(obj, locomotorSet, isHuman, i, j, layer, iRadius, center, dest, groupDest)) {
					return true;
				}
			}
			if (dir & 1) {
				segmentLength++;
			}
		}
	}

	if (groupDest) {
		// Didn't work, so just do simple adjust.
		return(adjustDestination(obj, locomotorSet, dest, nullptr));
	}
	return false;
}

Bool Pathfinder::checkForTarget(const Object *obj, 	Int cellX, Int cellY, const Weapon *weapon,
																const Object *victim, const Coord3D *victimPos,
																Int iRadius, Bool center,Coord3D *dest)
{
	Coord3D adjustDest;
	if (checkDestination(obj, cellX, cellY, LAYER_GROUND, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, LAYER_GROUND);
		if (weapon->isGoalPosWithinAttackRange( obj, &adjustDest, victim, victimPos ))	{
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to move to that can fire at victim.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustTargetDestination(const Object *obj, const Object *target, const Coord3D *targetPos,
																				 const Weapon *weapon, Coord3D *dest)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &cell )) {
		return false; // outside of bounds.
	}

	Int limit = MAX_ADJUSTMENT_CELL_COUNT;
	Int i, j;
	i = cell.x;
	j = cell.y;
	if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
		return true;
	}

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
	}
	return false;
}

Bool Pathfinder::checkForPossible(Bool isCrusher, Int fromZone,  Bool center, const LocomotorSet& locomotorSet,
																	Int cellX, Int cellY, PathfindLayerEnum layer, Coord3D *dest, Bool startingInObstacle)
{
	PathfindCell *goalCell = getCell(layer, cellX, cellY);
	if (!goalCell) return false;
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
	if (goalCell->getType() == PathfindCell::CELL_OBSTACLE) return false;
#else
	if (IS_IMPASSABLE(goalCell->getType())) return false;
#endif
	Int zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());
	if (startingInObstacle) {
		zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
	}
	if (fromZone==zone2) {
		adjustCoordToCell(cellX, cellY,  center, *dest, layer);
		return true;
	}
	return false;
}

/**
 * Find a pathable spot near the destination.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToPossibleDestination(Object *obj, const LocomotorSet& locomotorSet,
																						 Coord3D *dest)
{
	Int radius;
	Bool center;
	getRadiusAndCenter(obj, radius, center);
	ICoord2D goalCellNdx;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &goalCellNdx )) {
		return false; // outside of bounds.
	}

	// determine goal cell
	PathfindCell *goalCell;
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(dest);

	goalCell = getCell(destinationLayer, goalCellNdx.x, goalCellNdx.y);


	Coord3D from = *obj->getPosition();

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(&from, &startCellNdx);
	PathfindLayerEnum layer = LAYER_GROUND;
	if (obj) {
		layer = obj->getLayer();
	}
	PathfindCell *parentCell = getClippedCell( layer, &from );
	if (parentCell == nullptr) {
		return false;
	}

	Int zone1, zone2;
	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, parentCell->getZone());
	Bool isObstacle = false;
	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE)	{
		isObstacle = true;
	}
	if (isObstacle) {
		zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, zone1);
	}

	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());

	if (zone1 == zone2) {
		if (checkDestination(obj, goalCellNdx.x, goalCellNdx.y, destinationLayer, radius, center)) {
			return true;
		}
	}

	Int limit = MAX_ADJUSTMENT_CELL_COUNT;
	Int i, j;
	i = goalCellNdx.x;
	j = goalCellNdx.y;

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		delta++;
	}
	return false;
}


/**
 * Queues an object to do a pathfind.
 * It will call the object's ai update->doPathfind() during processPathfindQueue().
 */
Bool Pathfinder::queueForPath(ObjectID id)
{
#ifdef DEBUG_LOGGING
	{
		Object *tmpObj = TheGameLogic->findObjectByID(id);
		if (tmpObj) {
			AIUpdateInterface *tmpAI = tmpObj->getAIUpdateInterface();
			if (tmpAI) {
				const Coord3D* pos = tmpAI->friend_getRequestedDestination();
				DEBUG_ASSERTLOG(pos->x != 0.0 && pos->y != 0.0, ("Queueing pathfind to (0, 0), usually a bug. (Unit Name: '%s', Type: '%s')", tmpObj->getName().str(), tmpObj->getTemplate()->getName().str()));
			}
		}
	}
#endif

	/* Check & see if we are already queued. */
	Int slot = m_queuePRHead;
	while (slot != m_queuePRTail) {
		if (m_queuedPathfindRequests[slot] == id) {
			return true;
		}
		slot++;
		if (slot >= PATHFIND_QUEUE_LEN) {
			slot = 0;
		}
	}

	// Tail is the first available slot.
	Int nextSlot = m_queuePRTail+1;
	if (nextSlot >= PATHFIND_QUEUE_LEN) {
		nextSlot = 0;
	}
	if (nextSlot==m_queuePRHead) {
		DEBUG_CRASH(("Ran out of pathfind queue slots."));
		s_pathfindStats.droppedQueue++;
		return false;
	}
	m_queuedPathfindRequests[m_queuePRTail] = id;
	m_queuePRTail = nextSlot;
	return true;
}

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
void Pathfinder::doDebugIcons() {
	const Int FRAMES_TO_SHOW_OBSTACLES = 100;
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	// render AI debug information
	if (TheGlobalData->m_debugAI!=AI_DEBUG_CELLS && TheGlobalData->m_debugAI!=AI_DEBUG_TERRAIN) {
		return;
	}

		RGBColor color;
		color.red = color.green = color.blue = 0;
		addIcon(nullptr, 0, 0, color);	 // clear.
		Coord3D topLeftCorner;
		Bool showCells = TheGlobalData->m_debugAI==AI_DEBUG_CELLS;
		Int i;
		for (i=0; i<=LAYER_LAST; i++) {
			m_layers[i].doDebugIcons();
		}
		if (!showCells)	{
			frameToShowObstacles = TheGameLogic->getFrame()+FRAMES_TO_SHOW_OBSTACLES;
			//return;
		}
		// show the pathfind grid
		for( int j=0; j<getExtent()->y; j++ )
		{
			topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;

			for( int i=0; i<getExtent()->x; i++ )
			{
				topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;

				color.red = color.green = color.blue = 0;
				Bool empty = true;

				const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, i, j );
				if (cell)
				{
					switch (cell->getType())
					{
						case PathfindCell::CELL_CLIFF:
							color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_BRIDGE_IMPASSABLE:
							color.blue = color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_IMPASSABLE:
							color.green = 1;
							empty = false;
							break;

						case PathfindCell::CELL_WATER:
							color.blue = 1;
							empty = false;
							break;

						case PathfindCell::CELL_RUBBLE:
							color.red = 1;
							color.green = 0.5;
							empty = false;
							break;

						case PathfindCell::CELL_OBSTACLE:
							color.red = color.green = 1;
							empty = false;
							break;
						default:
							if (cell->getPinched()) {
								color.blue = color.green = 0.7f;
								empty = false;
							} else if (cell->getClearance() <= 1) {
								// Shore clearance: white touches water, cliff or the map edge, gray is one cell away
								color.red = color.green = color.blue = (cell->getClearance() == 0) ? 1.0f : 0.5f;
								empty = false;
							}
							break;
					}
				}
				if (showCells) {
					empty = true;
					color.red = color.green = color.blue = 0;
					if (empty && cell) {
						if (cell->getFlags()!=PathfindCell::NO_UNITS) {
							empty = false;
							if (cell->getFlags() == PathfindCell::UNIT_GOAL) {
								color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED) {
								color.green = color.blue = color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_MOVING) {
								color.green = 1;
							}	else {
								color.green = color.red = 1;
							}
						}
						if (cell->isAircraftGoal()) {
							empty = false;
							color.red = 0;
							color.green = color.blue = 1;
						}
					}
				}
				if (!empty) {
					Coord3D loc;
					loc.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F/2.0f;
					loc.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F/2.0f;
					loc.z = TheTerrainLogic->getGroundHeight(loc.x , loc.y);
					addIcon(&loc, PATHFIND_CELL_SIZE_F*0.8f, FRAMES_TO_SHOW_OBSTACLES-1, color);
				}
			}

	}
}
#endif


//-------------------------------------------------------------------------------------------------
/**
 * Create an aircraft path.  Just jogs around tall buildings marked with KINDOF_AIRCRAFT_PATH_AROUND.
 */
Path *Pathfinder::getAircraftPath( const Object *obj, const Coord3D *to )
{
	// for now, quick path objects don't pathfind, generally airborne units
	// build a trivial one-node path containing destination, then avoid buildings.
	Path *thePath = newInstance(Path);
	const AIUpdateInterface *ai = obj->getAI();
	ObjectID avoidObject = INVALID_ID;
	if (ai) {
		avoidObject = ai->getBuildingToNotPathAround();
	}

	// If it is an aircraft that circles (like raptors & migs) we need to adjust the destination
	// to one that doesn't clip buildings.
	Bool checkClips = false;
	if (ai && ai->getCurLocomotor()) {
		if (ai->getCurLocomotor()->getAppearance() == LOCO_WINGS) {
			checkClips = true;
		}
	}

	Real radius = 100;
	Coord3D adjDest = *to;
	if (checkClips) {
		circleClipsTallBuilding(obj->getPosition(), to, radius, avoidObject, &adjDest);
	}
	thePath->prependNode(&adjDest, LAYER_GROUND);
	Coord3D pos = *obj->getPosition();
	pos.z = to->z;
	thePath->prependNode( &pos, LAYER_GROUND );
	Int limit = 20;
	PathNode *curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		Coord3D newPos1, newPos2, newPos3;
		if (segmentIntersectsTallBuilding(curNode, curNode->getNext(), avoidObject, &newPos1, &newPos2, &newPos3)) {
			PathNode *newNode3 = newInstance(PathNode);
			newNode3->setPosition( &newPos3 );
			newNode3->setLayer(LAYER_GROUND);
			curNode->append(newNode3);
			PathNode *newNode2 = newInstance(PathNode);
			newNode2->setPosition( &newPos2 );
			newNode2->setLayer(LAYER_GROUND);
			curNode->append(newNode2);
			PathNode *newNode1 = newInstance(PathNode);
			newNode1->setPosition( &newPos1 );
			newNode1->setLayer(LAYER_GROUND);
			curNode->append(newNode1);
			curNode = newNode2;
		}
		curNode = curNode->getNext();
		limit--;
		if (limit<0) break;
	}

	curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		curNode->setNextOptimized(curNode->getNext());
		curNode = curNode->getNext();
	}
	thePath->markOptimized();
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) {
		TheAI->pathfinder()->setDebugPath(thePath);
	}

	return thePath;
}


/**
 * Process some path requests in the pathfind queue.
 */
//DECLARE_PERF_TIMER(processPathfindQueue)
void Pathfinder::processPathfindQueue()
{
	//USE_PERF_TIMER(processPathfindQueue)
	if (!m_isMapReady) {
		return;
	}
#ifdef DEBUG_QPF
#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
	__int64 startTime64;
	double timeToUpdate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
#endif
#endif

	if (m_zoneManager.needToCalculateZones()) {
		m_zoneManager.calculateZones(m_map, m_layers, m_extent);
		if (!PathTuning::BridgeFixes) {
			return;	// "bridges": the queue goes on serving paths in the frame zones are made, within its budget
		}
	}

	// Get the current logical extent.
	Region3D terrainExtent;
	TheTerrainLogic->getExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	m_logicalExtent = bounds;

	m_cumulativeCellsAllocated = 0;	// Number of pathfind cells examined.
	Int pathsFound = 0;
	while (m_cumulativeCellsAllocated < PATHFIND_CELLS_PER_FRAME &&
		m_queuePRTail!=m_queuePRHead) {
		Object *obj = TheGameLogic->findObjectByID(m_queuedPathfindRequests[m_queuePRHead]);
		m_queuedPathfindRequests[m_queuePRHead] = INVALID_ID;
		if (obj) {
			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (ai) {
				ai->doPathfind(this);
				pathsFound++;
			}
		}
		m_queuePRHead = m_queuePRHead+1;
		if (m_queuePRHead >= PATHFIND_QUEUE_LEN) {
			m_queuePRHead = 0;
		}
	}
	if (pathsFound > 0) {
		PROFILER_PLOT("PathfindCells", (double)m_cumulativeCellsAllocated);
		PROFILER_PLOT("PathfindPaths", (double)pathsFound);
	}
#ifdef DEBUG_QPF
	if (pathsFound>0) {
#ifdef DEBUG_LOGGING
		QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
		timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));
		if (timeToUpdate>0.01f)
		{
			DEBUG_LOG(("%d Pathfind queue: %d paths, %d cells --", TheGameLogic->getFrame(), pathsFound, m_cumulativeCellsAllocated));
			DEBUG_LOG(("time %f (%f)", timeToUpdate, (::GetTickCount()-startTimeMS)/1000.0f));
		}
#endif
	}	
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	doDebugIcons();
#endif

	logStatsIfDue();
}

/**
 * Opens pathfinding.txt beside the exe on the first enable and appends on later ones
 */
void Pathfinder::setStatsLogging( Bool enable )
{
	if (enable && s_statsFile == nullptr) {
		static Bool everOpened = false;
		char dir[_MAX_PATH];
		::GetModuleFileName(nullptr, dir, sizeof(dir));
		char *end = strrchr(dir, '\\');
		if (end) {
			*(end + 1) = 0;
		} else {
			dir[0] = 0;
		}
		char filename[_MAX_PATH];
		snprintf(filename, sizeof(filename), "%spathfinding.txt", dir);
		s_statsFile = fopen(filename, everOpened ? "a" : "w");
		everOpened = true;
		if (s_statsFile) {
			fprintf(s_statsFile, "Pathfinding log on at frame %u. One line a second while the pathfinder is busy.\n", TheGameLogic->getFrame());
			fprintf(s_statsFile, "queued: requests waiting  drop: refused by a full queue  srch: searches  cap: stopped at the cell cap  hier: coarse ok/fail\n");
			fprintf(s_statsFile, "cells: cell records used  max: largest search  free: records left  zones: raw count/overflow\n");
			fprintf(s_statsFile, "kind: find/closest/patch/away/attack/safe/ground  zr: zone screen refusals  blk: blocked repaths  fix: shoved off invalid cells\n");
			fflush(s_statsFile);
		}
	}
	if (!enable && s_statsFile) {
		fprintf(s_statsFile, "Pathfinding log off at frame %u.\n", TheGameLogic->getFrame());
		fclose(s_statsFile);
		s_statsFile = nullptr;
	}
	s_statsLogging = enable && s_statsFile != nullptr;
}

#if RTS_ZEROHOUR
void Pathfinder::logMovementEvent(const char *event, const Object *obj, Object *blocker, const Coord3D *goal)
{
	if (!s_statsLogging || s_statsFile == nullptr)
	{
		return;
	}
	const Coord3D *pos = obj->getPosition();
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	fprintf(s_statsFile, "PFMOVE f=%u event=%s id=%u type=%s name=%s pos=%.2f,%.2f heading=%.2f state=%d source=%d goal=%.2f,%.2f",
		TheGameLogic->getFrame(), event, (UnsignedInt)obj->getID(), obj->getTemplate()->getName().str(), obj->getName().str(),
		pos->x, pos->y, obj->getOrientation(), ai->getCurrentStateID(), ai->getLastCommandSource(), goal->x, goal->y);
	if (blocker != nullptr)
	{
		fprintf(s_statsFile, " blocker=%u type=%s relation=%d crusher=%u crushable=%u canCrush=%d",
			(UnsignedInt)blocker->getID(), blocker->getTemplate()->getName().str(), obj->getRelationship(blocker),
			(UnsignedInt)obj->getCrusherLevel(), (UnsignedInt)blocker->getCrushableLevel(), obj->canCrushOrSquish(blocker, TEST_CRUSH_OR_SQUISH));
	}
	fprintf(s_statsFile, "\n");
	fflush(s_statsFile);
}
#endif

/**
 * Once a second, write the pathfinder counters when anything happened
 */
void Pathfinder::logStatsIfDue()
{
	if (!s_statsLogging || s_statsFile == nullptr) {
		return;
	}
	if (TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND != 0) {
		return;
	}
	Int queued = (m_queuePRTail - m_queuePRHead + PATHFIND_QUEUE_LEN) % PATHFIND_QUEUE_LEN;
	PathfindStats &stats = s_pathfindStats;
	if (queued == 0 && stats.droppedQueue == 0 && stats.searches == 0 && stats.cells == 0 && stats.hierOk == 0 && stats.hierFail == 0 && stats.zoneOverflow == 0) {
		return;
	}
	fprintf(s_statsFile, "PFSTAT f=%u queued=%d drop=%u srch=%u cap=%u hier=%u/%u cells=%u max=%u free=%d zones=%u/%u kind=%u/%u/%u/%u/%u/%u/%u zr=%u blk=%u fix=%u\n",
		TheGameLogic->getFrame(), queued, stats.droppedQueue, stats.searches, stats.cappedSearches, stats.hierOk, stats.hierFail, stats.cells, stats.maxCellsOneSearch,
		PathfindCellInfo::getFreeCount(), stats.rawZoneHighWater, stats.zoneOverflow,
		stats.byType[PFS_FIND], stats.byType[PFS_CLOSEST], stats.byType[PFS_PATCH], stats.byType[PFS_AWAY], stats.byType[PFS_ATTACK], stats.byType[PFS_SAFE], stats.byType[PFS_GROUND],
		stats.events[PFE_ZONE_REJECT], stats.events[PFE_BLOCKED_REPATH], stats.events[PFE_INVALID_FIX]);
	fflush(s_statsFile);
	stats.droppedQueue = 0;
	stats.searches = 0;
	stats.cappedSearches = 0;
	stats.hierOk = 0;
	stats.hierFail = 0;
	stats.cells = 0;
	stats.maxCellsOneSearch = 0;
	stats.zoneOverflow = 0;
	memset(stats.byType, 0, sizeof(stats.byType));
	memset(stats.events, 0, sizeof(stats.events));
}


void Pathfinder::checkChangeLayers(PathfindCell *parentCell, Bool limitCells)
{
	if (parentCell->getConnectLayer() == LAYER_INVALID)
		return;

	ICoord2D newCellCoord = { parentCell->getXIndex(), parentCell->getYIndex() };
	PathfindCell *newCell = getCell(parentCell->getConnectLayer(), newCellCoord.x, newCellCoord.y );

	if (!newCell) {
		DEBUG_CRASH(("Couldn't find cell."));
		return;
	}

	// already on one of the lists
	if (newCell->hasInfo() && (newCell->getOpen() || newCell->getClosed())) {
		return;
	}

	if (limitCells && !newCell->hasInfo() && isSearchBudgetExhausted())
	{
		return;
	}
	if (!newCell->allocateInfo(newCellCoord)) {
		// Out of cells for pathing...
		return;
	}
	// compute cost of path thus far
	// keep track of path we're building - point back to cell we moved here from
	newCell->setParentCell(parentCell) ;
	// store cost of this path
	newCell->setCostSoFar(parentCell->getCostSoFar()); // same as parent cost
	newCell->setTotalCost(parentCell->getTotalCost());
	// insert newCell in open list such that open list is sorted, smallest total path cost first
	newCell->putOnSortedOpenList( m_openList );
}

bool Pathfinder::checkCellOutsideExtents(ICoord2D& cell) {
	return 	cell.x < m_logicalExtent.lo.x ||
					cell.x > m_logicalExtent.hi.x ||
					cell.y < m_logicalExtent.lo.y ||
					cell.y > m_logicalExtent.hi.y;
}


struct ExamineCellsStruct
{
	Pathfinder					*thePathfinder;
	const LocomotorSet	*theLoco;
	Bool								centerInCell;
	Bool								isHuman;
	Int									radius;
	const Object				*obj;
	PathfindCell				*goalCell;
};

/*static*/ Int Pathfinder::examineCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	ExamineCellsStruct* d = (ExamineCellsStruct*)userData;
	Bool isCrusher = d->obj ? d->obj->getCrusherLevel() > 0 : false;
	if (d->thePathfinder->m_isTunneling) return 1; // abort.
	if (from && to) {
			if (!d->thePathfinder->validMovementPosition( isCrusher, d->theLoco->getValidSurfaces(), d->theLoco->getRequiredWaterLevel(), d->obj->getRequiredBridgeHeight(), to, from )) {
				return 1;
			}
			if ( (to->getLayer() == LAYER_GROUND) && !d->thePathfinder->m_zoneManager.isPassable(to_x, to_y) ) {
				return 1;
			}

			if (to->getPinched()) {
				return 1; // abort.
			}
			if (PathTuning::Clearance) {
				// "clearance": the line stops where the footprint would hang over; the cells near a shore are left to the
				// search, which prices them
				Int hard;
				if (d->obj && d->radius > 0 && to->getLayer() == LAYER_GROUND &&
					d->thePathfinder->ringShortage(to_x, to_y, d->theLoco->getValidSurfaces(), d->radius, &hard) > 0) {
					return 1; // abort.
				}
			}
			else if (d->thePathfinder->getClearanceShortage(to, d->theLoco->getValidSurfaces(), d->radius) > 0) {
				return 1; // abort.
			}
			if (d->isHuman) {
				// check if new cell is in logical map.	(computer can move off logical map)
				if (to_x < d->thePathfinder->m_logicalExtent.lo.x) return 1; // abort
				if (to_y < d->thePathfinder->m_logicalExtent.lo.y) return 1; // abort
				if (to_x > d->thePathfinder->m_logicalExtent.hi.x) return 1; // abort
				if (to_y > d->thePathfinder->m_logicalExtent.hi.y) return 1; // abort
			}
			TCheckMovementInfo info;
			info.cell.x = to_x;
			info.cell.y = to_y;
			info.layer = from->getLayer();
			info.centerInCell = d->centerInCell;
			info.radius = d->radius;
			info.considerTransient = false;
			info.acceptableSurfaces = d->theLoco->getValidSurfaces();
			if (!d->thePathfinder->checkForMovement(d->obj, info)) {
				return 1; //abort.
			}

			if (info.enemyFixed) {
				return 1; //abort.
			}

			if (info.allyFixedCount) {
				return 1; //abort.
			}

			UnsignedInt newCostSoFar = from->getCostSoFar() + 0.5f*COST_ORTHOGONAL;
			if (to->getType() == PathfindCell::CELL_CLIFF ) {
				return 1;
			}

			ICoord2D newCellCoord;
			newCellCoord.x = to_x;
			newCellCoord.y = to_y;

			if (!to->hasInfo() && pathfinder->isSearchBudgetExhausted())
			{
				return 1;
			}
			if (!to->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return 1;
			}
			to->setBlockedByAlly(false);
			Int costRemaining = 0;
			costRemaining = to->costToGoal( d->goalCell );

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			if ( to->hasInfo() && (to->getOpen() || to->getClosed()) )
			{
				// already on one of the lists - if existing costSoFar is less,
				// the new cell is on a longer path, so skip it
				if (to->getCostSoFar() <= newCostSoFar)
				{
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
					return 0; // keep going.
#else
					// This cell's own probe walks on toward the goal
					return 1;
#endif
				}
			}

			to->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			to->setParentCell(from) ;
			to->setTotalCost(to->getCostSoFar() + costRemaining) ;

			// if to was on closed list, remove it from the list
			if (to->getClosed())
				to->removeFromClosedList( d->thePathfinder->m_closedList );

			// if the to was already on the open list, remove it so it can be re-inserted in order
			if (to->getOpen())
				to->removeFromOpenList( d->thePathfinder->m_openList );

			// insert to in open list such that open list is sorted, smallest total path cost first
			to->putOnSortedOpenList( d->thePathfinder->m_openList );
	}

	return 0;	// keep going
}


Int Pathfinder::examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell, const LocomotorSet& locomotorSet,
																				 Bool isHuman, Bool centerInCell, Int radius, const ICoord2D &startCellNdx,
																				 const Object *obj, Int attackDistance)
{
		const Int freeCellsAtStart = PathfindCellInfo::getFreeCount();
		if (isSearchBudgetExhausted())
		{
			return 0;
		}
	  Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;
		Bool canPathThroughUnits = false;
		if (obj && obj->getAIUpdateInterface()) {
			canPathThroughUnits = obj->getAIUpdateInterface()->canPathThroughUnits();
		}
		Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
		if (attackDistance==NO_ATTACK && !m_isTunneling && !locomotorSet.isDownhillOnly() && goalCell) {
			ExamineCellsStruct info;
			info.thePathfinder = this;
			info.theLoco = &locomotorSet;
			info.centerInCell = centerInCell;
			info.radius = radius;
			info.obj = obj;
			info.isHuman = isHuman;
			info.goalCell = goalCell;
			ICoord2D start, end;
			start.x = parentCell->getXIndex();
			start.y = parentCell->getYIndex();
			end.x = goalCell->getXIndex();
			end.y = goalCell->getYIndex();
			iterateCellsAlongLine(start, end, parentCell->getLayer(), examineCellsCallback, &info);
		}

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] =
		{
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 }
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = { 0 };

		UnsignedInt newCostSoFar = 0;

		// TheSuperHackers @feature simonx645 16/11/2026 "clearance" (PathTuning.h): how far this cell's footprint hangs over
		// water or cliff. A step that would hang CLEARANCE_HARD or more rings over is refused, unless it hangs less than the
		// cell it leaves: a unit already too close may always move away. A step that hangs over at all costs more.
		const Bool ourClearance = PathTuning::Clearance && obj && radius > 0;
		Int parentHard = 0;
		if (ourClearance && parentCell->getLayer() == LAYER_GROUND) {
			ringShortage(parentCell->getXIndex(), parentCell->getYIndex(), locomotorSet.getValidSurfaces(), radius, &parentHard);
		}

		for( int i=0; i<numNeighbors; i++ )
		{
			if (isSearchBudgetExhausted())
			{
				break;
			}
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (!newCell)
				continue;

			Bool notZonePassable = false;
			if ((newCell->getLayer()==LAYER_GROUND) && !m_zoneManager.isPassable(newCellCoord.x, newCellCoord.y)) {
				notZonePassable = true;
			}

			// check if new cell is in logical map.	(computer can move off logical map)
			if (isHuman && checkCellOutsideExtents(newCellCoord))
				continue;

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			if ( newCell->hasInfo() && (newCell->getOpen() || newCell->getClosed()) )
				continue;

			if (i>=firstDiagonal) {
				// make sure one of the adjacent sides is open.
				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
					continue;
				}
			}

			// do the gravity check here
			if ( locomotorSet.isDownhillOnly() )
			{
				Coord3D fromPos;
				fromPos.x = parentCell->getXIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.y = parentCell->getYIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.z = TheTerrainLogic->getGroundHeight(fromPos.x , fromPos.y);

				Coord3D toPos;
				toPos.x = newCellCoord.x * PATHFIND_CELL_SIZE_F ;
				toPos.y = newCellCoord.y * PATHFIND_CELL_SIZE_F ;
				toPos.z = TheTerrainLogic->getGroundHeight(toPos.x , toPos.y);

				if ( fromPos.z < toPos.z )
					continue;
			}

			Bool movementValid = validMovementPosition(isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, newCell, parentCell);
			Bool dozerHack = false;
			if (!movementValid && obj->isKindOf(KINDOF_DOZER) && newCell->getType() == PathfindCell::CELL_OBSTACLE) {
				Object* obstacle = TheGameLogic->findObjectByID(newCell->getObstacleID());
				if (obstacle && !(obj->getRelationship(obstacle) == ENEMIES)) {
					movementValid = true;
					dozerHack = true;
				}
			}

			Int clearanceShortage = 0;
			if (!PathTuning::Clearance) {
				clearanceShortage = getClearanceShortage(newCell, locomotorSet.getValidSurfaces(), radius);
				if (clearanceShortage >= CLEARANCE_HARD_SHORTAGE) {
					movementValid = false;
				}
			}

			if (!movementValid && !m_isTunneling) {
				continue;
			}

			if (ourClearance && newCell->getLayer() == LAYER_GROUND) {
				// "clearance" refuses a step that would hang over further than the cell it leaves; or as far, unless the
				// unit is still near where it started (it may have to drive along the band to get out of a gap it was built
				// or parked in); never the cell a bridge starts at, or one of the structure the unit is going into
				Int hard;
				const Int shortage = ringShortage(newCellCoord.x, newCellCoord.y, locomotorSet.getValidSurfaces(), radius, &hard);
				const Bool nearStart = IABS(newCellCoord.x - startCellNdx.x) <= radius + 2 && IABS(newCellCoord.y - startCellNdx.y) <= radius + 2;
				if (hard >= PathTuning::CLEARANCE_HARD && (hard > parentHard || (hard == parentHard && !nearStart)) &&
					!m_isTunneling && newCell->getConnectLayer() == LAYER_INVALID && !newCell->isObstaclePresent(m_ignoreObstacleID)) {
					continue;
				}
				clearanceShortage = shortage;	// it costs CLEARANCE_SOFT_COST below (contraZH's 40, as ours)
			}

			if (!dozerHack)
				neighborFlags[i] = true;

			TCheckMovementInfo info;
			info.cell = newCellCoord;
			info.layer = parentCell->getLayer();
			info.centerInCell = centerInCell;
			info.radius = radius;
			info.considerTransient = false;
			info.acceptableSurfaces = locomotorSet.getValidSurfaces();
			Int dx = newCellCoord.x-startCellNdx.x;
			Int dy = newCellCoord.y-startCellNdx.y;
			if (dx<0) dx = -dx;
			if (dy<0) dy = -dy;
			if (dx>1+radius) info.considerTransient = false;
			if (dy>1+radius) info.considerTransient = false;
			if (!checkForMovement(obj, info) || info.enemyFixed) {
				if (!m_isTunneling) {
					continue;
				}
				movementValid = false;
			}

			if (movementValid && !newCell->getPinched()) {
				//Note to self - only turn off tunneling after check for movement.jba.
				m_isTunneling = false;
			}

			if (!newCell->hasInfo()) {
				if (!newCell->allocateInfo(newCellCoord)) {
					// Out of cells for pathing...
					return freeCellsAtStart - PathfindCellInfo::getFreeCount();
				}
			}

			newCostSoFar = newCell->costSoFar( parentCell );
			if (info.allyMoving && dx<10 && dy<10) {
				newCostSoFar += 3*COST_DIAGONAL;
			}
			if (clearanceShortage > 0) {
				newCostSoFar += CLEARANCE_SOFT_COST;
			}

			if (newCell->getType() == PathfindCell::CELL_CLIFF && !newCell->getPinched() ) {
				Coord3D fromPos;
				fromPos.x = parentCell->getXIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.y = parentCell->getYIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.z = TheTerrainLogic->getGroundHeight(fromPos.x , fromPos.y);

				Coord3D toPos;
				toPos.x = newCellCoord.x * PATHFIND_CELL_SIZE_F ;
				toPos.y = newCellCoord.y * PATHFIND_CELL_SIZE_F ;
				toPos.z = TheTerrainLogic->getGroundHeight(toPos.x , toPos.y);

				// Painted-impassable cells are flat; a real climb rises about the cliff limit per cell.
				if ( WWMath::Fabs(fromPos.z - toPos.z)<PATHFIND_CELL_SIZE_F*0.5f) {
					newCostSoFar += 7*COST_DIAGONAL;
				}
			} else if (newCell->getPinched()) {
				newCostSoFar += COST_ORTHOGONAL;
			}

			newCell->setBlockedByAlly(false);
			if (info.allyFixedCount>0) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
				newCostSoFar += 3*COST_DIAGONAL*info.allyFixedCount;
#else
				newCostSoFar += 3*COST_DIAGONAL;
#endif
				if (!canPathThroughUnits)
					newCell->setBlockedByAlly(true);
			}

			Int costRemaining = 0;
			if (goalCell) {
				if (attackDistance == NO_ATTACK)  {
					costRemaining = newCell->costToGoal( goalCell );
				}	else {
					dx = newCellCoord.x - goalCell->getXIndex();
					dy = newCellCoord.y - goalCell->getYIndex();
					costRemaining = COST_ORTHOGONAL*WWMath::Sqrt((float)(dx*dx + dy*dy));
					costRemaining -= attackDistance/2;
					if (costRemaining<0)
						costRemaining=0;
					if (info.allyGoal) {
						if (obj->isKindOf(KINDOF_VEHICLE)) {
							newCostSoFar += 3*COST_ORTHOGONAL;
						}	else {
							// Infantry can pass through infantry.
							newCostSoFar += COST_ORTHOGONAL;
						}
					}
				}
			}

			if (notZonePassable) {
				newCostSoFar += 100*COST_ORTHOGONAL;
			}

			if (newCell->getType()==PathfindCell::CELL_OBSTACLE) {
				newCostSoFar += 100*COST_ORTHOGONAL;
			}

			if (m_isTunneling) {
				if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, newCell, parentCell )) {
					newCostSoFar += 10*COST_ORTHOGONAL;
				}
			}

			newCell->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;
			if (m_isTunneling) {
				costRemaining = 0; // find the closest valid cell.
			}
			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				newCell->removeFromOpenList( m_openList );

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			newCell->putOnSortedOpenList( m_openList, !m_isTunneling );
		}
	return freeCellsAtStart - PathfindCellInfo::getFreeCount();
}


/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
													 const Coord3D *rawTo)
{
	beginSearchBudget(PFS_FIND);
	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;
	if (!clientSafeQuickDoesPathExist(locomotorSet, requiredBridgeHeight, from, rawTo)) {
		countStatEvent(PFE_ZONE_REJECT);
		return nullptr;
	}
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	Int radius;
	Bool centerInCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	m_zoneManager.clearPassableFlags();
	Path *hPat = findHierarchicalPath(isHuman, locomotorSet, from, rawTo, false, radius);
	if (hPat) {
		deleteInstance(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	Path *pat = internalFindPath(obj, locomotorSet, from, rawTo);
	if (pat!=nullptr) {
		return pat;
	}

	return nullptr;
}
/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::internalFindPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
													 const Coord3D *rawTo)
{
	//CRCDEBUG_LOG(("Pathfinder::findPath()"));
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("internal find path..."));
#endif

#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif
	Bool centerInCell = true;
	Int radius = 0;
	if (obj) {
		getRadiusAndCenter(obj, radius, centerInCell);
	}

	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug."));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	if (!centerInCell) {
		adjustTo.x += PATHFIND_CELL_SIZE_F/2;
		adjustTo.y += PATHFIND_CELL_SIZE_F/2;
	}

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, to );
	if (goalCell == nullptr) {
		return nullptr;
	}

	ICoord2D cell;
	worldToCell( to, &cell );

	if (!checkDestination(obj, cell.x, cell.y, destinationLayer, radius, centerInCell)) {
		return nullptr;
	}
	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(&clipFrom, &startCellNdx);
	PathfindLayerEnum layer = LAYER_GROUND;
	if (obj) {
		layer = obj->getLayer();
	}
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (parentCell == nullptr) {
		return nullptr;
	}

	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &pos2d);
		if (!parentCell->allocateInfo(pos2d)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}
	//
	// Determine if this pathfind is "tunneling" or not.
	// A tunneling pathfind starts from within an obstacle, and is allowed
	// to ignore obstacle cells until it reaches a cell that is no longer
	// classified as an obstacle.  At that point, the pathfind behaves normally.
	//
	m_isTunneling = parentCell->getType() == PathfindCell::CELL_OBSTACLE;

	Int zone1, zone2;
	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, parentCell->getZone());
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());

	if ( (layer==LAYER_WALL && zone1 == 0) || (destinationLayer==LAYER_WALL && zone2 == 0) ) {
#if RETAIL_COMPATIBLE_PATHFINDING
		if (s_useFixedPathfinding)
#endif
		{
			goalCell->releaseInfo();
			parentCell->releaseInfo();
		}
		return nullptr;
	}

	if (goalCell->isObstaclePresent(m_ignoreObstacleID) || m_isTunneling) {
		// Use terrain zones instead of building zones, since we are moving into or out of a building.
		zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
		zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, zone2);
		zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, zone1);
	}

	//DEBUG_LOG(("Zones %d to %d", zone1, zone2));

	if ( zone1 != zone2) {
		//DEBUG_LOG(("Intense Debug Info - Pathfind Zone screen failed-cannot reach desired location."));
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	// sanity check - if destination is invalid, can't path there
	if (!validMovementPosition( isCrusher, destinationLayer, locomotorSet, requiredBridgeHeight, to ))	{
		m_isTunneling = false;
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	// sanity check - if source is invalid, we have to cheat
	if (!validMovementPosition( isCrusher, layer, locomotorSet, requiredBridgeHeight, from ))	{
		// somehow we got to an impassable location.
		m_isTunneling = true;
	}
	if (!PathTuning::Clearance && getClearanceShortage(parentCell, locomotorSet.getValidSurfaces(), radius) >= CLEARANCE_HARD_SHORTAGE) {
		m_isTunneling = true;	// ("clearance" lets a unit too close move away instead)
	}

	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	Int cellCount = 0;
	PathfindCell *startCell = parentCell;
	PathfindCell *closestCell = nullptr;
	Int closestDistSqr = 0x7fffffff;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_PATHS;
#ifdef INTENSE_DEBUG
			DEBUG_LOG(("internal find path SUCCESS..."));
			Int count = 0;
			if (cellCount>1000 && obj) {
				show = true;
				DEBUG_LOG(("cells %d obj %s %x from (%f,%f) to(%f, %f)", count, obj->getTemplate()->getName().str(), obj, from->x, from->y, to->x, to->y));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big path", false);
			}
#endif
			if (show)
				debugShowSearch(true);

			m_isTunneling = false;
			// construct and return path
			Path *path =  buildActualPath( obj, locomotorSet.getValidSurfaces(), from, goalCell, centerInCell, false, locomotorSet.getRequiredWaterLevel() );
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding)
			{
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
			}
			return path;
		}

		// remember the cell nearest the goal in case the search runs out of budget
		Int dx = goalCell->getXIndex() - parentCell->getXIndex();
		Int dy = goalCell->getYIndex() - parentCell->getYIndex();
		if (dx*dx + dy*dy < closestDistSqr) {
			closestDistSqr = dx*dx + dy*dy;
			closestCell = parentCell;
		}

		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell, true);

		cellCount += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		if (isSearchBudgetExhausted())
		{
			break;
		}
	}

	if (m_lastSearchHitCap) {
		// Hand back a path to the nearest cell reached that the unit may stop on; the caller paths on from there
		PathfindCell *endCell = closestCell;
		Int steps = 0;
		while (endCell && endCell != startCell && steps < 8 &&
			!checkDestination(obj, endCell->getXIndex(), endCell->getYIndex(), endCell->getLayer(), radius, centerInCell)) {
			endCell = endCell->getParentCell();
			steps++;
		}
		if (endCell && endCell != startCell) {
			s_pathfindStats.cappedSearches++;
			m_isTunneling = false;
			Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), from, endCell, centerInCell, false, locomotorSet.getRequiredWaterLevel() );
			cleanOpenAndClosedLists();
			goalCell->releaseInfo();
			return path;
		}
	}

	// failure - goal cannot be reached
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("internal find path FAILURE..."));
#endif
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = WWMath::Sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2);
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}

	if (obj) {
#ifdef DUMP_PERF_STATS
		TheGameLogic->incrementOverallFailedPathfinds();
#endif
#ifdef STATE_MACHINE_DEBUG
		if( obj->getAIUpdateInterface() )
		{
			DEBUG_LOG(("state %s", obj->getAIUpdateInterface()->getCurrentStateName().str()));
		}
#endif
	}
#endif

#ifdef DEBUG_LOGGING
	if (obj)
	{
		Bool valid;
		valid = validMovementPosition( isCrusher, obj->getLayer(), locomotorSet, obj->getRequiredBridgeHeight(), to ) ;

		DEBUG_LOG(("%d Pathfind failed from (%f,%f) to (%f,%f), OV %d --", TheGameLogic->getFrame(), from->x, from->y, to->x, to->y, valid));
		DEBUG_LOG(("Unit '%s', time %f, cells %d", obj->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f,cellCount));
	}
#endif

	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding)
	{
		goalCell->releaseInfo();
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}
	return nullptr;
}

/**
 * Checks to see if there is enough path width at this cell for ground
 * movement.  Returns the width available.
 */
Int Pathfinder::clearCellForDiameter(Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter)
{
	Int radius = pathDiameter/2;
	Int numCellsAbove = radius;
	if (radius==0) numCellsAbove++;
	Int i, j;
	Bool clear = true;
	Bool cutCorners = false;
	if (radius>1) {
		cutCorners = true;
		// We remove the outside corner cells from the check.
	}
	for (i=cellX-radius; i<cellX+numCellsAbove; i++) {
		Bool xMinOrMax = (i==cellX-radius) || (i==cellX+numCellsAbove-1);
		for (j=cellY-radius; j<cellY+numCellsAbove; j++) {
			Bool yMinOrMax = (j==cellY-radius) || (j==cellY+numCellsAbove-1);
			if (xMinOrMax && yMinOrMax && cutCorners) {
				continue; // this is an outside corner cell, and we are cutting corners. jba. :)
			}
			PathfindCell	*cell = getCell(layer, i, j);
			if (cell) {
				if (cell->getType() != PathfindCell::CELL_CLEAR) {
					if (cell->getType() == PathfindCell::CELL_OBSTACLE) {
						if (cell->isObstacleFence()) {
							if (!crusher) {
								clear = false;
							}
						} else {
							clear = false;
						}
					} else {
						clear = false;
					}
				}
				if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED && pathDiameter>=2) {
					Object *obj = TheGameLogic->findObjectByID(cell->getPosUnit());
					if (obj) {
						if (crusher) {
							if (obj->getCrushableLevel()>1) {
								clear = false;
							}
						} else {
							if (obj->getCrushableLevel()>0) {
								clear = false;
							}
						}
					}
				}
			} else {
				return false; // off the map.
			}
			if (!clear) break;
		}
	}
	if (clear) {
		if (radius==0) return 1;
		return 2*radius;
	}
	if (pathDiameter < 2) return 0;
	return clearCellForDiameter(crusher, cellX, cellY, layer, pathDiameter-2);
}

/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildGroundPath(Bool isCrusher, const Coord3D *fromPos, PathfindCell *goalCell, Bool center, Int pathDiameter )
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildActualPath: goalCell == nullptr") );

	Path *path = newInstance(Path);

	prependCells(path, fromPos, goalCell, center);

	// cleanup the path by checking line of sight
	path->optimizeGroundPath( isCrusher, pathDiameter );


#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		PathNode *node = path->getFirstNode();
		for( ; node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			color.red = color.green = 1;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = path->getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}

/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildHierarchicalPath( const Coord3D *fromPos, PathfindCell *goalCell )
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildHierarchicalPath: goalCell == nullptr") );

	Path *path = newInstance(Path);

	prependCells(path, fromPos, goalCell, true);

	// TheSuperHackers @feature simonx645 16/11/2026 "coarse" (GameLogic/PathTuning.h): the corridor is every block the
	// route crosses and every block next to one, along the whole route, so the fine search can go round a unit or a
	// corner near the coarse line.
	if (PathTuning::CoarseFix) {
		ICoord2D blocks;
		m_zoneManager.getExtent(blocks);
		for (PathNode *node = path->getFirstNode(); node; node = node->getNext()) {
			ICoord2D ndx;
			worldToCell(node->getPosition(), &ndx);
			const Int blockX = ndx.x / PathfindZoneManager::ZONE_BLOCK_SIZE;
			const Int blockY = ndx.y / PathfindZoneManager::ZONE_BLOCK_SIZE;
			for (Int x = blockX - 1; x <= blockX + 1; x++) {
				for (Int y = blockY - 1; y <= blockY + 1; y++) {
					if (x >= 0 && y >= 0 && x < blocks.x && y < blocks.y) {
						m_zoneManager.setPassable(x * PathfindZoneManager::ZONE_BLOCK_SIZE, y * PathfindZoneManager::ZONE_BLOCK_SIZE, true);
					}
				}
			}
		}
	}

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	// Expand the hierarchical path around the starting point. jba [8/24/2003]
	// This allows the unit to get around friendly units that may be near it.
	Coord3D pos = *path->getFirstNode()->getPosition();
	Coord3D minPos = pos;
	minPos.x -= PathfindZoneManager::ZONE_BLOCK_SIZE*PATHFIND_CELL_SIZE_F;
	minPos.y -= PathfindZoneManager::ZONE_BLOCK_SIZE*PATHFIND_CELL_SIZE_F;
	Coord3D maxPos = pos;
	maxPos.x += PathfindZoneManager::ZONE_BLOCK_SIZE*PATHFIND_CELL_SIZE_F;
	maxPos.y += PathfindZoneManager::ZONE_BLOCK_SIZE*PATHFIND_CELL_SIZE_F;
	ICoord2D cellNdxMin, cellNdxMax;
	worldToCell(&minPos, &cellNdxMin);
	worldToCell(&maxPos, &cellNdxMax);
	Int i, j;
	for (i=cellNdxMin.x; i<=cellNdxMax.x; i++) {
		for (j=cellNdxMin.y; j<=cellNdxMax.y; j++) {
			m_zoneManager.setPassable(i, j, true);
		}
	}
#endif

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		Int i;
		for (i=0; i<3; i++)
		for( PathNode *node = path->getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			color.red = 1;
			color.green = 0.4f;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos, PATHFIND_CELL_SIZE_F, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}


struct MADStruct
{
	Pathfinder					*thePathfinder;
	Object							*obj;
	ObjectID						ignoreID;
};

/*static*/ Int Pathfinder::moveAlliesDestinationCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	MADStruct* d = (MADStruct*)userData;
	if (to) {
		if (to->getPosUnit()==INVALID_ID) {
			return 0;
		}
		if (to->getPosUnit()==d->obj->getID()) {
			return 0;	// It's us.
		}
		if (to->getPosUnit()==d->ignoreID) {
			return 0;	 // It's the one we are ignoring.
		}
		Object *otherObj = TheGameLogic->findObjectByID(to->getPosUnit());
		if (otherObj==nullptr) return 0;
		if (d->obj->getRelationship(otherObj)!=ALLIES) {
			return 0;  // Only move allies.
		}
		if (otherObj && otherObj->getAI() && !otherObj->getAI()->isMoving()) {
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
			//Kris: Patch 1.01 November 3, 2003
			//Black Lotus exploit fix -- moving while hacking.
			if( otherObj->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || otherObj->getAI()->isBusy() )
			{
				return 0; // Packing or unpacking objects for example
			}
#endif
			//DEBUG_LOG(("Moving ally"));
			otherObj->getAI()->aiMoveAwayFromUnit(d->obj, CMD_FROM_AI);
		}
	}

	return 0;	// keep going
}

void Pathfinder::moveAlliesAwayFromDestination(Object *obj,const Coord3D& destination)
{
	MADStruct info;
	info.obj = obj;
	info.ignoreID = obj->getAI()->getIgnoredObstacleID();
	info.thePathfinder = this;
	PathfindLayerEnum layer = obj->getLayer();
	if (layer==LAYER_GROUND) {
		layer = TheTerrainLogic->getLayerForDestination(&destination);
	}
	iterateCellsAlongLine(*obj->getPosition(), destination, layer, moveAlliesDestinationCallback, &info);

}


struct GroundCellsStruct
{
	Pathfinder					*thePathfinder;
	Bool								centerInCell;
	Int									pathDiameter;
	PathfindCell				*goalCell;
	Bool								crusher;
};

/*static*/ Int Pathfinder::groundCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	GroundCellsStruct* d = (GroundCellsStruct*)userData;
	if (from && to) {
			if (to->hasInfo()) {
				if (to->getOpen() || to->getClosed())
				{
					// already on one of the lists
					return 1; // abort.
				}
			}
			// See how wide the cell is.
			Int clearDiameter = d->thePathfinder->clearCellForDiameter(d->crusher, to_x, to_y, to->getLayer(), d->pathDiameter);
			if (clearDiameter != d->pathDiameter) {
				return 1;
			}
			ICoord2D newCellCoord;
			newCellCoord.x = to_x;
			newCellCoord.y = to_y;
			if (!to->hasInfo() && pathfinder->isSearchBudgetExhausted())
			{
				return 1;
			}
			if (!to->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return 1;
			}

			UnsignedInt newCostSoFar = from->getCostSoFar() + 0.5f*COST_ORTHOGONAL;
			to->setBlockedByAlly(false);

			Int costRemaining = 0;
			costRemaining = to->costToGoal( d->goalCell );
			to->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			to->setParentCell(from) ;
			to->setTotalCost(to->getCostSoFar() + costRemaining) ;

			// insert to in open list such that open list is sorted, smallest total path cost first
			to->putOnSortedOpenList( d->thePathfinder->m_openList );
	}

	return 0;	// keep going
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findGroundPath( const Coord3D *from,
													 const Coord3D *rawTo, Int pathDiameter, Bool crusher)
{
	//CRCDEBUG_LOG(("Pathfinder::findGroundPath()"));
	beginSearchBudget(PFS_GROUND);
#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("Find ground path..."));
#endif
	Bool centerInCell = false;

	m_zoneManager.clearPassableFlags();
	Bool isHuman = true;

	Path *hPat = internal_findHierarchicalPath(isHuman, LOCOMOTORSURFACE_GROUND, from, rawTo, false, false, 0);
	if (hPat) {
		deleteInstance(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug."));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);

	ICoord2D cell;
	worldToCell( to, &cell );

	if (pathDiameter!=clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)) {
		Int offset=1;
		ICoord2D newCell;
		const Int MAX_OFFSET = 8;
		while (offset<MAX_OFFSET) {
			newCell = cell;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			offset++;
			cell = newCell;
		}
		if (offset >= MAX_OFFSET) {
			return nullptr;
		}
	}

	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, cell.x, cell.y );
	if (goalCell == nullptr) {
		return nullptr;
	}
	if (!goalCell->allocateInfo(cell)) {
		return nullptr;
	}

	// determine start cell
	ICoord2D startCellNdx;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (parentCell == nullptr) {
#if RETAIL_COMPATIBLE_PATHFINDING
		if (s_useFixedPathfinding)
#endif
		{
			goalCell->releaseInfo();
		}
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &startCellNdx);
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}


	Int zone1, zone2;
	// m_isCrusher = false;
	zone1 = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, parentCell->getZone());
	zone2 =  m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, goalCell->getZone());

	//DEBUG_LOG(("Zones %d to %d", zone1, zone2));

	if ( zone1 != zone2) {
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}
	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	// TheSuperHackers @fix helmutbuhler This was originally uninitialized and in the loop below.
#if RETAIL_COMPATIBLE_CRC
	UnsignedInt newCostSoFar = 0;
#endif

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	Int cellCount = 0;
	while( !m_openList.empty() )
	{
		if (isSearchBudgetExhausted())
		{
			break;
		}
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal
#ifdef INTENSE_DEBUG
	DEBUG_LOG((" time %d msec %d cells", (::GetTickCount()-startTimeMS), cellCount));
	DEBUG_LOG((" SUCCESS"));
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS;
			if (show)
				debugShowSearch(true);
#endif
			m_isTunneling = false;
			// construct and return path
			Path *path =  buildGroundPath(crusher, from, goalCell, centerInCell, pathDiameter );
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding)
			{
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
			}
			return path;
		}

		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell, true);

		GroundCellsStruct info;
		info.thePathfinder = this;
		info.centerInCell = centerInCell;
		info.pathDiameter = pathDiameter;
		info.goalCell = goalCell;
		info.crusher = crusher;
		ICoord2D start, end;
		start.x = parentCell->getXIndex();
		start.y = parentCell->getYIndex();
		end.x = goalCell->getXIndex();
		end.y = goalCell->getYIndex();
		iterateCellsAlongLine(start, end, parentCell->getLayer(), groundCellsCallback, &info);

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] =
		{
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 }
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = { 0 };

		// TheSuperHackers @fix Mauller 23/05/2025 Fixes uninitialized variable.
#if RETAIL_COMPATIBLE_CRC
		// newCostSoFar defined in outer block.
#else
		UnsignedInt newCostSoFar = 0;
#endif

		for( int i=0; i<numNeighbors; i++ )
		{
			if (isSearchBudgetExhausted())
			{
				break;
			}
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			if ((newCell->getLayer()==LAYER_GROUND) && !m_zoneManager.isPassable(newCellCoord.x, newCellCoord.y)) {
				// check if we are within 3.
				Bool passable = false;
				if (m_zoneManager.clipIsPassable(newCellCoord.x+3, newCellCoord.y+3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x-3, newCellCoord.y+3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x+3, newCellCoord.y-3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x-3, newCellCoord.y-3)) passable = true;
				if (!passable) continue;
			}

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists
					onList = true;
				}
			}
			Int clearDiameter = 0;
			if (newCell!=goalCell) {

				if (i>=firstDiagonal) {
					// make sure one of the adjacent sides is open.
					if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
						continue;
					}
				}

				// See how wide the cell is.
				clearDiameter = clearCellForDiameter(crusher, newCellCoord.x, newCellCoord.y, newCell->getLayer(), pathDiameter);
				if (newCell->getType() != PathfindCell::CELL_CLEAR) {
					continue;
				}
				if (newCell->getPinched()) {
					continue;
				}
				neighborFlags[i] = true;

				if (!newCell->allocateInfo(newCellCoord)) {
					// Out of cells for pathing...
 					continue;
				}
				cellCount++;

#if RETAIL_COMPATIBLE_CRC
				// TheSuperHackers @fix helmutbuhler 11/06/2025 The indentation was wrong on retail here.
				newCostSoFar = newCell->costSoFar( parentCell );
				if (clearDiameter<pathDiameter) {
					int delta = pathDiameter-clearDiameter;
					newCostSoFar += 0.6f*(delta*COST_ORTHOGONAL);
				}
				newCell->setBlockedByAlly(false);
			}
#else
			}
			newCostSoFar = newCell->costSoFar( parentCell );
			if (clearDiameter<pathDiameter) {
				int delta = pathDiameter-clearDiameter;
				newCostSoFar += 0.6f*(delta*COST_ORTHOGONAL);
			}
			newCell->setBlockedByAlly(false);
#endif
			Int costRemaining = 0;
			costRemaining = newCell->costToGoal( goalCell );

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less,
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}
			newCell->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;
			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				newCell->removeFromOpenList( m_openList );

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			newCell->putOnSortedOpenList( m_openList );
		}
	}
	// failure - goal cannot be reached
#ifdef INTENSE_DEBUG
	DEBUG_LOG((" FAILURE"));
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = WWMath::Sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2);
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}
#endif

	DEBUG_LOG(("%d FindGroundPath failed from (%f,%f) to (%f,%f) --", TheGameLogic->getFrame(), from->x, from->y, to->x, to->y));
	DEBUG_LOG(("time %f", (::GetTickCount()-startTimeMS)/1000.0f));

#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding)
	{
		goalCell->releaseInfo();
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}
	return nullptr;
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
// TheSuperHackers @feature simonx645 16/11/2026 "crossings" (GameLogic/PathTuning.h): a 4x4 or 5x5 (radius 2 and up) may
// cross where nothing within radius - 1 cells is off the map, or water or cliff its surfaces cannot use: the footprint
// would otherwise hang a whole row past the shore. Smaller footprints always may. Structures and other obstacles are left
// to the zones and the fine search.
static Bool Coarse_Crossing_Fits( Pathfinder *pathfinder, Int cellX, Int cellY, LocomotorSurfaceTypeMask surfaces, Int radius )
{
	const Int reach = radius - 1;
	for (Int i = cellX - reach; i <= cellX + reach; i++) {
		for (Int j = cellY - reach; j <= cellY + reach; j++) {
			PathfindCell *cell = pathfinder->getCell(LAYER_GROUND, i, j);
			if (!cell) {
				return false;
			}
			const PathfindCell::CellType type = cell->getType();
			if ((type == PathfindCell::CELL_WATER && !(surfaces & LOCOMOTORSURFACE_WATER)) ||
				(type == PathfindCell::CELL_CLIFF && !(surfaces & LOCOMOTORSURFACE_CLIFF))) {
				return false;
			}
		}
	}
	return true;
}

void Pathfinder::processHierarchicalCell( const ICoord2D &scanCell, const ICoord2D &delta, PathfindCell *parentCell,
																				 PathfindCell *goalCell, zoneStorageType parentZone,
																				 zoneStorageType *examinedZones, Int &numExZones,
																				 LocomotorSurfaceTypeMask locomotorSurface, Bool crusher, Int radius, Int &cellCount)
{
	if (scanCell.x<m_extent.lo.x || scanCell.x>m_extent.hi.x ||
		scanCell.y<m_extent.lo.y || scanCell.y>m_extent.hi.y) {
		return;
	}
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
	if (parentZone == PathfindZoneManager::UNINITIALIZED_ZONE) {
		return;
	}
#endif
	if (parentZone == m_zoneManager.getBlockZone(locomotorSurface,
		crusher, scanCell.x, scanCell.y, m_map)) {
		PathfindCell *newCell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
		if (newCell->hasInfo() && (newCell->getOpen() || newCell->getClosed())) return; // already looked at this one.
#else
		// A cell without a record has not been visited yet, so it must stay eligible for allocation below
		if (newCell->hasInfo() && (newCell->getOpen() || newCell->getClosed()))
		{
			return;
		}
#endif

		ICoord2D adjacentCell = scanCell;
		//DEBUG_ASSERTCRASH(parentZone==newCell->getZone(), ("Different zones?"));
		if (parentZone!=newCell->getZone()) return;
		adjacentCell.x += delta.x;
		adjacentCell.y += delta.y;
		if (adjacentCell.x<m_extent.lo.x || adjacentCell.x>m_extent.hi.x ||
			adjacentCell.y<m_extent.lo.y || adjacentCell.y>m_extent.hi.y) {
			return;
		}
		PathfindCell *adjNewCell = getCell(LAYER_GROUND, adjacentCell.x, adjacentCell.y);
		if (adjNewCell->hasInfo() && (adjNewCell->getOpen() || adjNewCell->getClosed())) return; // already looked at this one.
		zoneStorageType parentGlobalZone = m_zoneManager.getEffectiveZone(locomotorSurface, crusher, parentZone);

		/// @todo - somehow out of bounds or bogus newZone.
		zoneStorageType newZone = m_zoneManager.getBlockZone(locomotorSurface,
							crusher, adjacentCell.x, adjacentCell.y, m_map);
		zoneStorageType newGlobalZone = m_zoneManager.getEffectiveZone(locomotorSurface, crusher, newZone);
		if (newGlobalZone != parentGlobalZone) {
			return; // can't step over. jba.
		}
		Int j;
		Bool found=false;
		for (j=0; j<numExZones; j++) {
			if (examinedZones[j] == newZone) {
				found = true;
				break;
			}
		}
		if (found) {
			return;
		}
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
		// Zones ignore footprints, so skip a crossing the fine search refuses for this unit
		if (PathTuning::CoarseFootprint)
		{
			// "crossings": the footprint must fit round both crossing cells
			if (radius >= 2 && (!Coarse_Crossing_Fits(this, scanCell.x, scanCell.y, locomotorSurface, radius) ||
				!Coarse_Crossing_Fits(this, adjacentCell.x, adjacentCell.y, locomotorSurface, radius)))
			{
				return;
			}
		}
		else if (getClearanceShortage(newCell, locomotorSurface, radius) >= CLEARANCE_HARD_SHORTAGE ||
			getClearanceShortage(adjNewCell, locomotorSurface, radius) >= CLEARANCE_HARD_SHORTAGE)
		{
			return;
		}
#endif

		if (!newCell->allocateInfo(scanCell))
		{
			return;
		}
#if RETAIL_COMPATIBLE_PATHFINDING
		if (!s_useFixedPathfinding)
		{
			if (!newCell->getClosed() && !newCell->getOpen()) {
				newCell->putOnClosedList(m_closedList);
			}
		}
		else
#endif
		{
			if (newCell->hasInfo() && !newCell->getClosed() && !newCell->getOpen()) {
				newCell->putOnClosedList(m_closedList);
			}
		}

		adjNewCell->allocateInfo(adjacentCell);
		if( adjNewCell->hasInfo() )
		{

			cellCount++;
			Int curCost = adjNewCell->costToHierGoal(parentCell);
			Int remCost = adjNewCell->costToHierGoal(goalCell);
			if (adjNewCell->getPinched() || newCell->getPinched()) {
				curCost += 2*COST_ORTHOGONAL;
			}	else {
				examinedZones[numExZones] = newZone;
				numExZones++;
			}

			adjNewCell->setCostSoFar(parentCell->getCostSoFar() + curCost);
			adjNewCell->setTotalCost(adjNewCell->getCostSoFar()+remCost);
			adjNewCell->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			adjNewCell->putOnSortedOpenList( m_openList );
		}

	}
}


/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from,
													 const Coord3D *to, Bool crusher, Int radius)
{
	return internal_findHierarchicalPath(isHuman, locomotorSet.getValidSurfaces(), from, to, crusher, FALSE, radius);
}


/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findClosestHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from,
													 const Coord3D *to, Bool crusher, Int radius)
{
	return internal_findHierarchicalPath(isHuman, locomotorSet.getValidSurfaces(), from, to, crusher, TRUE, radius);
}



/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::internal_findHierarchicalPath( Bool isHuman, const LocomotorSurfaceTypeMask locomotorSurface, const Coord3D *from,
													 const Coord3D *rawTo, Bool crusher, Bool closestOK, Int radius)
{
	//CRCDEBUG_LOG(("Pathfinder::findGroundPath()"));
#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif

	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug."));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);

	ICoord2D cell;
	worldToCell( to, &cell );

	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, cell.x, cell.y );
	if (!goalCell) {
		return nullptr;
	}

	if (!goalCell->allocateInfo(cell)) {
		return nullptr;
	}

	// determine start cell
	ICoord2D startCellNdx;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (!parentCell) {
		return nullptr;
	}

	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &startCellNdx);
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}

	Int zone1, zone2;
	// m_isCrusher = false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSurface, false, parentCell->getZone());
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSurface, false, goalCell->getZone());

	if ( zone1 != zone2) {
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	parentCell->startPathfind(goalCell);

	// "closed" list is initially empty
	m_closedList.reset();

	Int cellCount = 0;

	zoneStorageType goalBlockZone;
	ICoord2D goalBlockNdx;
	if (goalCell->getLayer()==LAYER_GROUND) {
		goalBlockZone = m_zoneManager.getBlockZone(locomotorSurface,
			crusher, goalCell->getXIndex(), goalCell->getYIndex(), m_map);

		goalBlockNdx.x = goalCell->getXIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		goalBlockNdx.y = goalCell->getYIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
	}	else {
		goalBlockZone = goalCell->getZone();
		goalBlockNdx.x = -1;
		goalBlockNdx.y = -1;
	}

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	if (parentCell->getLayer()!=LAYER_GROUND) {
		PathfindLayerEnum layer = parentCell->getLayer();
		// We're starting on a bridge, so link to land at the bridge end points.
		ICoord2D ndx;
		ICoord2D toNdx;
		m_layers[layer].getStartCellIndex(&ndx);
		m_layers[layer].getEndCellIndex(&toNdx);
 		PathfindCell *cell = getCell(LAYER_GROUND, toNdx.x, toNdx.y);
		PathfindCell *startCell = getCell(LAYER_GROUND, ndx.x, ndx.y);
		if (cell && startCell) {
			// Close parent cell;
			parentCell->removeFromOpenList(m_openList);
			parentCell->putOnClosedList(m_closedList);
			if (!startCell->allocateInfo(ndx)) {
				// TheSuperHackers @info We need to forcefully cleanup dangling pathfinding cells if this failure condition is hit in retail
				// Retail clients will crash beyond this point, but we attempt to recover by performing a full cleanup then enabling the fixed pathfinding codepath
#if RETAIL_COMPATIBLE_PATHFINDING
				if (!s_useFixedPathfinding) {
					s_useFixedPathfinding = true;
					forceCleanCells();
				}
				else
#endif
				{
					cleanOpenAndClosedLists();
					goalCell->releaseInfo();
				}
				return nullptr;
			}
			startCell->setParentCellHierarchical(parentCell);
			cellCount++;
			Int curCost = startCell->costToHierGoal(parentCell);
			Int remCost = startCell->costToHierGoal(goalCell);
			startCell->setCostSoFar(curCost);
			startCell->setTotalCost(remCost);
			startCell->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			startCell->putOnSortedOpenList( m_openList, false );

			cellCount++;
			if(!cell->allocateInfo(toNdx)) {
				// TheSuperHackers @info We need to forcefully cleanup dangling pathfinding cells if this failure condition is hit in retail
				// Retail clients will crash beyond this point, but we attempt to recover by performing a full cleanup then enabling the fixed pathfinding codepath
#if RETAIL_COMPATIBLE_PATHFINDING
				if (!s_useFixedPathfinding) {
					s_useFixedPathfinding = true;
					forceCleanCells();
				}
				else
#endif
				{
					cleanOpenAndClosedLists();
					goalCell->releaseInfo();
				}
				return nullptr;
			}
			curCost = cell->costToHierGoal(parentCell);
			remCost = cell->costToHierGoal(goalCell);
			cell->setCostSoFar(curCost);
			cell->setTotalCost(remCost);
			cell->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			cell->putOnSortedOpenList( m_openList, false );
		}
	}

	PathfindCell *closestCell = nullptr;
	Real closestDistSqr = sqr(HUGE_DIST);

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.empty() )
	{
		if (cellCount >= PATHFIND_HIER_CELL_CAP) {
			break;
		}
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		zoneStorageType parentZone;
		if (parentCell->getLayer()==LAYER_GROUND) {
			parentZone = m_zoneManager.getBlockZone(locomotorSurface,
				crusher, parentCell->getXIndex(), parentCell->getYIndex(), m_map);
		}	else {
			parentZone = parentCell->getZone();
		}

		Bool reachedGoal = false;

		Int blockX = parentCell->getXIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		Int blockY = parentCell->getYIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		if (parentZone == goalBlockZone) {
			if (goalBlockNdx.x == -1 || (blockX==goalBlockNdx.x && blockY == goalBlockNdx.y)) {
				reachedGoal = true;
			} else {
#ifdef INTENSE_DEBUG
				DEBUG_LOG(("Hmm, got match before correct cell."));
#endif
			}
		}

		ICoord2D zoneBlockExtent;
		m_zoneManager.getExtent(zoneBlockExtent);

		if (!reachedGoal && m_zoneManager.interactsWithBridge(parentCell->getXIndex(), parentCell->getYIndex())) {
			Int i;
			for (i=0; i<=LAYER_LAST; i++) {
				if (m_layers[i].isUnused() || m_layers[i].isDestroyed()) {
					continue;
				}
				ICoord2D ndx;
				ICoord2D toNdx;
				m_layers[i].getStartCellIndex(&ndx);
				m_layers[i].getEndCellIndex(&toNdx);
				if (ndx.x/PathfindZoneManager::ZONE_BLOCK_SIZE != blockX ||
						ndx.y/PathfindZoneManager::ZONE_BLOCK_SIZE != blockY) {
					m_layers[i].getStartCellIndex(&toNdx);
					m_layers[i].getEndCellIndex(&ndx);
				}
				if (ndx.x<0 || ndx.y<0) continue;
				if (toNdx.x<0 || toNdx.y<0) continue;
				if (ndx.x/PathfindZoneManager::ZONE_BLOCK_SIZE == blockX &&
						ndx.y/PathfindZoneManager::ZONE_BLOCK_SIZE == blockY) {
					// Bridge connects to this block.
					Int bridgeZone = m_zoneManager.getBlockZone(locomotorSurface, crusher, ndx.x, ndx.y, m_map);
					if (bridgeZone != parentZone) {
						continue;
					}
					// We have a winner.
					if (m_layers[i].getZone() == goalBlockZone) {
						reachedGoal = true;
						break;
					}
 					PathfindCell *cell = getCell(LAYER_GROUND, toNdx.x, toNdx.y);
					if (!cell)
						continue;

					if (cell->hasInfo() && (cell->getClosed() || cell->getOpen()))
						continue;

					PathfindCell *startCell = getCell(LAYER_GROUND, ndx.x, ndx.y);
					if (!startCell)
						continue;

					if (startCell != parentCell) {
						if(!startCell->allocateInfo(ndx)) {
							// TheSuperHackers @info We need to forcefully cleanup dangling pathfinding cells if this failure condition is hit in retail
							// Retail clients will crash beyond this point, but we attempt to recover by performing a full cleanup then enabling the fixed pathfinding codepath
#if RETAIL_COMPATIBLE_PATHFINDING
							if (!s_useFixedPathfinding) {
								s_useFixedPathfinding = true;
								forceCleanCells();
							}
							else
#endif
							{
								cleanOpenAndClosedLists();
								goalCell->releaseInfo();
							}
							return nullptr;
						}
						startCell->setParentCellHierarchical(parentCell);
						if (!startCell->getClosed() && !startCell->getOpen()) {
							startCell->putOnClosedList(m_closedList);
						}
					}
					if(!cell->allocateInfo(toNdx)) {
						// TheSuperHackers @info We need to forcefully cleanup dangling pathfinding cells if this failure condition is hit in retail
						// Retail clients will crash beyond this point, but we attempt to recover by performing a full cleanup then enabling the fixed pathfinding codepath
#if RETAIL_COMPATIBLE_PATHFINDING
						if (!s_useFixedPathfinding) {
							s_useFixedPathfinding = true;
							forceCleanCells();
						}
						else
#endif
						{
							cleanOpenAndClosedLists();
							goalCell->releaseInfo();
						}
						return nullptr;
					}
					cell->setParentCellHierarchical(startCell);

					cellCount++;
					Int curCost = cell->costToHierGoal(startCell);
					Int remCost = cell->costToHierGoal(goalCell);

					cell->setCostSoFar(startCell->getCostSoFar() + curCost);
					cell->setTotalCost(cell->getCostSoFar()+remCost);
					cell->setParentCellHierarchical(startCell);
					// insert newCell in open list such that open list is sorted, smallest total path cost first
					cell->putOnSortedOpenList( m_openList );

				}
			}
		}

		if (reachedGoal)
		{
			if (parentCell != goalCell) {
				goalCell->setParentCellHierarchical(parentCell);
			}
			// success - found a path to the goal

			m_isTunneling = false;
			s_pathfindStats.hierOk++;
			// construct and return path
			Path *path =  buildHierarchicalPath( from, goalCell );
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_PATHS;
			show |= (TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS);
			if (show)	{
				debugShowSearch(true);
			}
#endif
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding)
			{
				if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
					goalCell->releaseInfo();
				}
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
				goalCell->releaseInfo();
			}
			return path;
		}

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
#if 0
		Bool show = TheGlobalData->m_debugAI==AI_DEBUG_PATHS;
		show |= (TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS);
		if (show)	{
			extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 			RGBColor color;
			color.blue = 1;
			color.red = 1;
			color.green = 0;
			Coord3D pos;
			pos.x = ((blockX+0.5f)*PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F ;
			pos.y = ((blockY+0.5f)*PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F ;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, 5*PATHFIND_CELL_SIZE_F, 300, color);
		}
#endif
#endif
		Real dx = IABS(goalCell->getXIndex()-parentCell->getXIndex());
		Real dy = IABS(goalCell->getYIndex()-parentCell->getYIndex());
		Real distSqr = dx*dx+dy*dy;
		if (distSqr < closestDistSqr) {
			closestCell = parentCell;
			closestDistSqr = distSqr;
		}

		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		Int i;
		zoneStorageType examinedZones[PathfindZoneManager::ZONE_BLOCK_SIZE];
		Int numExZones = 0;
		// Left side.
		if (blockX>0) {
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.x = blockX*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.y = (blockY*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.y += offset;
				ICoord2D delta;
				delta.x = -1; // left side moves -1.
				delta.y = 0;

				PathfindCell *cell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (!cell)
					continue;

				if ( cell->hasInfo() && (cell->getClosed() || cell->getOpen()) ) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface, crusher, scanCell.x, scanCell.y, m_map))
						break;
				}

				if (isHuman && checkCellOutsideExtents(scanCell))
					continue;

				processHierarchicalCell(scanCell, delta, parentCell,
					goalCell, parentZone, examinedZones, numExZones, locomotorSurface, crusher, radius, cellCount);
			}
		}
		// Right side.
		if (blockX<zoneBlockExtent.x-1) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.x = blockX*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE-1;
				scanCell.y = (blockY*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.y += offset;
				ICoord2D delta;
				delta.x = 1; // right side moves +1.
				delta.y = 0;

				PathfindCell *cell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (!cell)
					continue;

				if ( cell->hasInfo() && (cell->getClosed() || cell->getOpen()) ) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface, crusher, scanCell.x, scanCell.y, m_map))
						break;
				}

				if (isHuman && checkCellOutsideExtents(scanCell))
					continue;

				processHierarchicalCell(scanCell, delta, parentCell,
					goalCell, parentZone, examinedZones, numExZones, locomotorSurface, crusher, radius, cellCount);
			}
		}
		// Top side.
		if (blockY>0) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.y = blockY*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.x = (blockX*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.x += offset;
				ICoord2D delta;
				delta.x = 0;
				delta.y = -1;	// Top side moves -1.

				PathfindCell *cell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (!cell)
					continue;

				if ( cell->hasInfo() && (cell->getClosed() || cell->getOpen()) ) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface, crusher, scanCell.x, scanCell.y, m_map))
						break;
				}

				if (isHuman && checkCellOutsideExtents(scanCell))
					continue;

				processHierarchicalCell(scanCell, delta, parentCell,
					goalCell, parentZone, examinedZones, numExZones, locomotorSurface, crusher, radius, cellCount);
			}
		}
		// Bottom side.
		if (blockY<zoneBlockExtent.y-1) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.y = blockY*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE-1;
				scanCell.x = (blockX*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.x += offset;
				ICoord2D delta;
				delta.x = 0;
				delta.y = 1; // Top side moves +1.

				PathfindCell *cell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (!cell)
					continue;

				if ( cell->hasInfo() && (cell->getClosed() || cell->getOpen()) ) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface, crusher, scanCell.x, scanCell.y, m_map))
						break;
				}

				if (isHuman && checkCellOutsideExtents(scanCell))
					continue;

				processHierarchicalCell(scanCell, delta, parentCell,
					goalCell, parentZone, examinedZones, numExZones, locomotorSurface, crusher, radius, cellCount);
			}
		}
	}

	if (closestOK && closestCell) {
		m_isTunneling = false;
		s_pathfindStats.hierOk++;
		// construct and return path
		Path *path =  buildHierarchicalPath( from, closestCell );

#if RETAIL_COMPATIBLE_PATHFINDING
		if (!s_useFixedPathfinding)
		{
			if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
				goalCell->releaseInfo();
			}
			cleanOpenAndClosedLists();
		}
		else
#endif
		{
			cleanOpenAndClosedLists();
			parentCell->releaseInfo();
			goalCell->releaseInfo();
		}
		return path;
	}

	// failure - goal cannot be reached
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = WWMath::Sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2);
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}
#endif

	s_pathfindStats.hierFail++;
	DEBUG_LOG(("%d FindHierarchicalPath failed from (%f,%f) to (%f,%f) --", TheGameLogic->getFrame(), from->x, from->y, to->x, to->y));
	DEBUG_LOG(("time %f", (::GetTickCount()-startTimeMS)/1000.0f));

#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding)
	{
		goalCell->releaseInfo();
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}

	return nullptr;
}


/**
 * Does any broken bridge join from and to?
 * True means that if bridge BridgeID is repaired, there is a land path from to to..
 */
Bool Pathfinder::findBrokenBridge(const LocomotorSet& locoSet,
																	const Coord3D *from, const Coord3D *to, ObjectID *bridgeID)
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);

	Int zone1, zone2;
	*bridgeID = INVALID_ID;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	zone1 = m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, parentCell->getZone());
	zone2 =  m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, goalCell->getZone());
	zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
	zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
	zone1 = m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, zone1);
	zone2 =  m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, zone2);

	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return false;
	}

	// Check broken bridges.
	Int i;
	for (i=0; i<=LAYER_LAST; i++) {
		if (m_layers[i].isDestroyed()) {
			if (m_layers[i].connectsZones(&m_zoneManager, locoSet, zone1, zone2)) {
				*bridgeID = m_layers[i].getBridgeID();
				return true;
			}
		}
	}
	return false;
}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the quick check, only looks at whether the terrain is possible or
 * impossible to path over.  Doesn't take other units into account.
 * False means it is impossible to path.
 * True means it is possible given the terrain, but there may be units in the way.
 */
Bool Pathfinder::clientSafeQuickDoesPathExist( const LocomotorSet& locomotorSet,
																Short requiredBridgeHeight,
																const Coord3D *from,
																const Coord3D *to )
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	if (!validMovementPosition(false, destinationLayer, locomotorSet, requiredBridgeHeight, to)) {
		return false;
	}
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	Int zone1, zone2;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	if (goalCell->getType()==PathfindCell::CELL_CLIFF) {
		return false; // No goals on cliffs.
	}
	Bool doingTerrainZone = false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, parentCell->getZone());

	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE) {
		doingTerrainZone = true;
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
		if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE) {
			// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
			// It is better to return a false positive than a false negative. jba.
			return true;
		}
#endif
	}
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, goalCell->getZone());
	if (goalCell->getType() == PathfindCell::CELL_OBSTACLE) {
		doingTerrainZone = true;
	}
	if (doingTerrainZone) {
		zone1 = parentCell->getZone();
		zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, zone1);
		zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
		zone2 = goalCell->getZone();
		zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
		zone2 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, zone2);
		zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
	}
	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return true;
	}
	return FALSE;  // no path exists

}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the quick check, only looks at whether the terrain is possible or
 * impossible to path over.  Doesn't take other units into account.
 * False means it is impossible to path.
 * True means it is possible given the terrain, but there may be units in the way.
 */
Bool Pathfinder::clientSafeQuickDoesPathExistForUI( const LocomotorSet& locomotorSet,
																const Coord3D *from,
																const Coord3D *to,
																Bool acceptRubble )
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	Int zone1, zone2;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	if (goalCell->getType()==PathfindCell::CELL_CLIFF) {
		return false; // No goals on cliffs.
	}

	LocomotorSurfaceTypeMask surfaces = locomotorSet.getValidSurfaces();
	if (acceptRubble)
	{
		surfaces |= LOCOMOTORSURFACE_RUBBLE;
	}

	zone1 = m_zoneManager.getEffectiveZone(surfaces, false, parentCell->getZone());
	zone2 =  m_zoneManager.getEffectiveZone(surfaces, false, goalCell->getZone());

	if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE ||
			zone2 == PathfindZoneManager::UNINITIALIZED_ZONE) {
		// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
		// It is better to return a false positive than a false negative. jba.
		return true;
	}
	/* Do the effective terrain zone.  This feedback is for the ui, so we won't take structures into account,
		because if they are visible it will be obvious, and if they are stealthed they should be invisible to the
		pathing as well. jba. */
	zone1 = parentCell->getZone();
	zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
	zone1 = m_zoneManager.getEffectiveZone(surfaces, false, zone1);
	zone1 = m_zoneManager.getEffectiveTerrainZone(zone1);
	zone2 = goalCell->getZone();
	zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);
	zone2 = m_zoneManager.getEffectiveZone(surfaces, false, zone2);
	zone2 = m_zoneManager.getEffectiveTerrainZone(zone2);

	if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE) {
		// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
		// It is better to return a false positive than a false negative. jba.
		return true;
	}
	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return true;
	}
	return FALSE;  // no path exists

}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the careful check, looks at whether the terrain, buindings and units are possible or
 * impossible to path over.  Takes other units into account.
 * False means it is impossible to path.
 * True means it is possible to path.
 */
Bool Pathfinder::slowDoesPathExist( Object *obj,
																const Coord3D *from,
																const Coord3D *to,
																ObjectID ignoreObject)
{
	AIUpdateInterface *ai = obj->getAI();
	if (ai==nullptr) {
		return false;
	}
	const LocomotorSet &locoSet = ai->getLocomotorSet();
	m_ignoreObstacleID = ignoreObject;
	Path *path = findPath(obj, locoSet, from, to);
	m_ignoreObstacleID = INVALID_ID;
	Bool found = (path!=nullptr);

	deleteInstance(path);
	path = nullptr;

	return found;
}

void Pathfinder::clip( Coord3D *from, Coord3D *to )
{
	ICoord2D fromCell, toCell;
	ICoord2D clipFromCell, clipToCell;
	fromCell.x = REAL_TO_INT_FLOOR(from->x/PATHFIND_CELL_SIZE);
	fromCell.y = REAL_TO_INT_FLOOR(from->y/PATHFIND_CELL_SIZE);
	toCell.x = REAL_TO_INT_FLOOR(to->x/PATHFIND_CELL_SIZE);
	toCell.y = REAL_TO_INT_FLOOR(to->y/PATHFIND_CELL_SIZE);
	if (ClipLine2D(&fromCell, &toCell, &clipFromCell, &clipToCell,&m_extent)) {
		if (fromCell.x!=clipFromCell.x || fromCell.y != clipFromCell.y) {
			from->x = clipFromCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			from->y = clipFromCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
		if (toCell.x!=clipToCell.x || toCell.y != clipToCell.y) {
			to->x = clipToCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			to->y = clipToCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
	}

}

//Bool Pathfinder::pathDestination( 	Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest,
//		PathfindLayerEnum layer, const Coord3D *groupDest)
//{
//	//CRCDEBUG_LOG(("Pathfinder::pathDestination()"));
//	if (m_isMapReady == false) return false;
//
//	if (!obj) return false;
//
//	Int cellCount = 0;
//
//	Coord3D adjustTo = *groupDest;
//	Coord3D *to = &adjustTo;
//	DEBUG_ASSERTCRASH(m_openList== nullptr && m_closedList == nullptr, ("Dangling lists."));
//	// create unique "mark" values for open and closed cells for this pathfind invocation
//
//	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
//
//	PathfindLayerEnum desiredLayer = TheTerrainLogic->getLayerForDestination(dest);
//	// determine desired
//	PathfindCell *desiredCell = getClippedCell( desiredLayer,  dest );
//	if (desiredCell == nullptr)
//		return FALSE;
//
//	PathfindLayerEnum goalLayer = TheTerrainLogic->getLayerForDestination(to);
//	// determine goal cell
//	PathfindCell *goalCell = getClippedCell( goalLayer,  to );
//	if (goalCell == nullptr)
//		return FALSE;
//
//
//	Bool isHuman = true;
//	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
//		isHuman = false; // computer gets to cheat.
//	}
//	Bool center;
//	Int radius;
//	getRadiusAndCenter(obj, radius, center);
//
//	// determine start cell
//	ICoord2D startCellNdx;
//	worldToCell(dest, &startCellNdx);
//	PathfindCell *parentCell = getCell( layer, startCellNdx.x, startCellNdx.y );
//	if (parentCell == nullptr)
//		return FALSE;
//	ICoord2D pos2d;
//	worldToCell(to, &pos2d);
//	if (!goalCell->allocateInfo(pos2d)) {
//		return FALSE;
//	}
//
//	if (parentCell!=goalCell) {
//		if (!parentCell->allocateInfo(startCellNdx)) {
//#if RETAIL_COMPATIBLE_PATHFINDING
//			if (!s_useFixedPathfinding)	{
//				desiredCell->releaseInfo();
//			}
//#endif
//			goalCell->releaseInfo();
//			return FALSE;
//		}
//	}
//
//	PathfindCell *closestCell = nullptr;
//	Real closestDistanceSqr = FLT_MAX;
//	Coord3D closestPos;
//
//	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), parentCell ) == false) {
//		parentCell->releaseInfo();
//		goalCell->releaseInfo();
//		return FALSE;
//	}
//
//	parentCell->startPathfind(goalCell);
//
//	// initialize "open" list to contain start cell
//	m_openList = parentCell;
//
//	// "closed" list is initially empty
//	m_closedList = nullptr;
//
//	//
//	// Continue search until "open" list is empty, or
//	// until goal is found.
//	//
//	while( m_openList != nullptr )
//	{
//		// take head cell off of open list - it has lowest estimated total path cost
//		parentCell = m_openList;
//		m_openList = parentCell->removeFromOpenList(m_openList);
//
//		Coord3D pos;
//		// put parent cell onto closed list - its evaluation is finished
//		m_closedList = parentCell->putOnClosedList( m_closedList );
//		if (checkForAdjust(obj, locomotorSet, isHuman, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(),
//			radius, center, &pos, groupDest)) {
//			Int dx = IABS(goalCell->getXIndex()-parentCell->getXIndex());
//			Int dy = IABS(goalCell->getYIndex()-parentCell->getYIndex());
//			Real distSqr = dx*dx+dy*dy;
//
//			if (distSqr < closestDistanceSqr) {
//				closestCell = parentCell;
//				closestDistanceSqr = distSqr;
//				closestPos = pos;
//			} else {
//				continue;
//			}
//		} else {
//			continue;
//		}
//
//		if (cellCount > MAX_CELL_COUNT) {
//			continue;
//		}
//		// Check to see if we can change layers in this cell.
//		checkChangeLayers(parentCell);
//
//		// expand search to neighboring orthogonal cells
//		static ICoord2D delta[] =
//		{
//			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
//			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 }
//		};
//		const Int numNeighbors = 8;
//		const Int firstDiagonal = 4;
//		ICoord2D newCellCoord;
//		PathfindCell *newCell;
//		const Int adjacent[5] = {0, 1, 2, 3, 0};
//		Bool neighborFlags[8] = { 0 };
//
//		UnsignedInt newCostSoFar = 0;
//
//		for( int i=0; i<numNeighbors; i++ )
//		{
//			neighborFlags[i] = false;
//			// determine neighbor cell to try
//			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
//			newCellCoord.y = parentCell->getYIndex() + delta[i].y;
//
//			// get the neighboring cell
//			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );
//
//			// check if cell is on the map
//			if (newCell == nullptr)
//				continue;
//
//			// check if this neighbor cell is already on the open (waiting to be tried)
//			// or closed (already tried) lists
//			Bool onList = false;
//			if (newCell->hasInfo()) {
//				if (newCell->getOpen() || newCell->getClosed())
//				{
//					// already on one of the lists
//					onList = true;
//				}
//			}
//			if (i>=firstDiagonal) {
//				// make sure one of the adjacent sides is open.
//				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
//					continue;
//				}
//			}
//
//			if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), newCell, parentCell )) {
//				continue;
//			}
//
//			neighborFlags[i] = true;
//
//			if (!newCell->allocateInfo(newCellCoord)) {
//				// Out of cells for pathing...
//#if RETAIL_COMPATIBLE_PATHFINDING
//				if (s_useFixedPathfinding)
//#endif
//				{
//					cleanOpenAndClosedLists();
//					goalCell->releaseInfo();
//				}
// 				return cellCount;
//			}
//			cellCount++;
//
//			newCostSoFar = newCell->costSoFar( parentCell );
//			newCell->setBlockedByAlly(false);
//
//			// check if this neighbor cell is already on the open (waiting to be tried)
//			// or closed (already tried) lists
//			if (onList)
//			{
//				// already on one of the lists - if existing costSoFar is less,
//				// the new cell is on a longer path, so skip it
//				if (newCell->getCostSoFar() <= newCostSoFar)
//					continue;
//			}
//
//			// keep track of path we're building - point back to cell we moved here from
//			newCell->setParentCell(parentCell) ;
//
//			// store cost of this path
//			newCell->setCostSoFar(newCostSoFar);
//
//			Int costRemaining = 0;
//			if (goalCell) {
//				costRemaining = newCell->costToGoal( goalCell );
//			}
//
//			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;
//
//			// if newCell was on closed list, remove it from the list
//			if (newCell->getClosed())
//				m_closedList = newCell->removeFromClosedList( m_closedList );
//
//			// if the newCell was already on the open list, remove it so it can be re-inserted in order
//			if (newCell->getOpen())
//				m_openList = newCell->removeFromOpenList( m_openList );
//
//			// insert newCell in open list such that open list is sorted, smallest total path cost first
//			m_openList = newCell->putOnSortedOpenList( m_openList );
//		}
//	}
//
//#if defined(RTS_DEBUG)
//	if (closestCell) {
//		debugShowSearch(true);
//		*dest = closestPos;
//	} else {
//		debugShowSearch(true);
//	}
//#endif
//	m_isTunneling = false;
//#if RETAIL_COMPATIBLE_PATHFINDING
//	if (!s_useFixedPathfinding) {
//		cleanOpenAndClosedLists();
//		goalCell->releaseInfo();
//	}
//	else
//#endif
//	{
//		cleanOpenAndClosedLists();
//		parentCell->releaseInfo();
//		goalCell->releaseInfo();
//	}
//
//	return false;
//}

struct TightenPathStruct
{
	Object *obj;
	const LocomotorSet *locomotorSet;
	PathfindLayerEnum layer;
	Int		radius;
	Bool	center;
	Bool	foundNewDest;
	Coord3D orgDestPos;
	Coord3D newDestPos;
};


/*static*/ Int Pathfinder::tightenPathCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	TightenPathStruct* d = (TightenPathStruct*)userData;
	if (from == nullptr || to==nullptr) return 0; // failure
	if (d->layer != to->getLayer()) {
		return 0; // failure
	}

#if RETAIL_COMPATIBLE_CRC
	// TheSuperHackers @bugfix Caball009 27/02/2026 This was originally uninitialized.
	// The uninitialized values that retail uses here are usually close to zero as long as foundNewDest == false, otherwise it uses the new values of newDestPos.
	// newDestPos is zero initialized by the caller, so there is no need to check foundNewDest here.
	Coord3D pos = d->newDestPos;
#else
	Coord3D pos = d->orgDestPos;
#endif

	if (!TheAI->pathfinder()->checkForAdjust(d->obj, *d->locomotorSet, true, to_x, to_y, to->getLayer(), d->radius, d->center, &pos, nullptr))
	{
		return 0; // failure
	}
	d->foundNewDest = true;
	d->newDestPos = pos;

	return 0; // success but continue
}

/* Returns the cost, which is in the same units as coord3d distance. */
void Pathfinder::tightenPath(Object *obj, const LocomotorSet& locomotorSet, Coord3D *from,
		const Coord3D *to)
{
	TightenPathStruct info;

	getRadiusAndCenter(obj, info.radius, info.center);
	info.layer = TheTerrainLogic->getLayerForDestination(from);
	info.obj = obj;
	info.locomotorSet = &locomotorSet;
	info.foundNewDest = false;
	info.orgDestPos = *to;
#if RETAIL_COMPATIBLE_CRC
	info.newDestPos.zero();
#endif
	iterateCellsAlongLine(*from, *to, info.layer, tightenPathCallback, &info);
	if (info.foundNewDest) {
		*from = info.newDestPos;
	}
}


/* Returns the cost, which is in the same units as coord3d distance. */
Int Pathfinder::checkPathCost(Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Coord3D *rawTo)
{
	//CRCDEBUG_LOG(("Pathfinder::checkPathCost()"));
	if (m_isMapReady == false) return 0;
	enum {MAX_COST = 0x7fff0000};
	if (!obj) return MAX_COST;

	Int cellCount = 0;

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;

	PathfindLayerEnum goalLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getClippedCell( goalLayer,  to );
	if (goalCell == nullptr)
		return MAX_COST;


	Bool center;
	Int radius;
	getRadiusAndCenter(obj, radius, center);
	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(from, &startCellNdx);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getCell( fromLayer, from );
	if (parentCell == nullptr)
		return MAX_COST;
	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return MAX_COST;
	}

	if (parentCell!=goalCell) {
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return MAX_COST;
		}
	}

	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, parentCell ) == false) {
		parentCell->releaseInfo();
		goalCell->releaseInfo();
		return MAX_COST;
	}

	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		// put parent cell onto closed list - its evaluation is finished - Retail compatible behaviour
#if RETAIL_COMPATIBLE_PATHFINDING
		if (!s_useFixedPathfinding) {
			parentCell->putOnClosedList( m_closedList );
		}
#endif

		if (parentCell==goalCell) {
			Int cost = parentCell->getTotalCost();
			m_isTunneling = false;
			cleanOpenAndClosedLists();
#if RETAIL_COMPATIBLE_PATHFINDING
			if (s_useFixedPathfinding)
#endif
			{
				parentCell->releaseInfo();
			}
			return cost;
		}

		// put parent cell onto closed list - its evaluation is finished - Fixed behaviour
#if RETAIL_COMPATIBLE_PATHFINDING
		if (s_useFixedPathfinding)
#endif
		{
			parentCell->putOnClosedList( m_closedList );
		}

		if (cellCount > MAX_CELL_COUNT) {
			continue;
		}
		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] =
		{
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 }
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = { 0 };

		UnsignedInt newCostSoFar = 0;

		for( int i=0; i<numNeighbors; i++ )
		{
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists
					onList = true;
				}
			}
			if (i>=firstDiagonal) {
				// make sure one of the adjacent sides is open.
				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
					continue;
				}
			}

			if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, newCell, parentCell )) {
				continue;
			}

			neighborFlags[i] = true;

			if (!newCell->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
#if RETAIL_COMPATIBLE_PATHFINDING
				if (s_useFixedPathfinding)
#endif
				{
					cleanOpenAndClosedLists();
					parentCell->releaseInfo();
					goalCell->releaseInfo();
				}
 				return cellCount;
			}
			cellCount++;

			newCostSoFar = newCell->costSoFar( parentCell );
			newCell->setBlockedByAlly(false);

			// check if this neighbor cell is already on the open (waiting to be tried)
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less,
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}

			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;

			// store cost of this path
			newCell->setCostSoFar(newCostSoFar);

			Int costRemaining = 0;
			if (goalCell) {
				costRemaining = newCell->costToGoal( goalCell );
			}

			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				newCell->removeFromOpenList( m_openList );

#if RETAIL_COMPATIBLE_PATHFINDING
			// TheSuperHacker @info This is here to catch a retail pathfinding crash point and to recover from it
			// A cell has gotten onto the open list without pathfinding info due to a danling m_open pointer on the previous listed cell so we need to force a cleanup
			if (!s_useFixedPathfinding && m_openList.getHead() && !m_openList.getHead()->hasInfo()) {
				s_useFixedPathfinding = true;
				forceCleanCells();
				return MAX_COST;
			}
#endif

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			newCell->putOnSortedOpenList( m_openList );
		}
	}

	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
			goalCell->releaseInfo();
		}
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}
	return MAX_COST;
}


/**
 * Find a short, valid path between the FROM location and a location NEAR the to location.
 * Uses A* algorithm.
 */
Path *Pathfinder::findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
																	Coord3D *rawTo, Bool blocked, Real pathCostMultiplier, Bool moveAllies)
{
	//CRCDEBUG_LOG(("Pathfinder::findClosestPath()"));
	beginSearchBudget(PFS_CLOSEST);
#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	if (locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
		return nullptr;
	}

	if (m_isMapReady == false) return nullptr;

	m_isTunneling = false;

	if (!obj) return nullptr;

	Bool canPathThroughUnits = false;
	if (obj && obj->getAIUpdateInterface()) {
		canPathThroughUnits = obj->getAIUpdateInterface()->canPathThroughUnits();
	}
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);

	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	if (!centerInCell) {
		adjustTo.x += PATHFIND_CELL_SIZE_F/2;
		adjustTo.y += PATHFIND_CELL_SIZE_F/2;
	}
	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;


	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getClippedCell( destinationLayer,  to );
	if (goalCell == nullptr)
		return nullptr;

	if (goalCell->getZone()==0 && destinationLayer==LAYER_WALL) {
		return nullptr;
	}

	Bool goalOnObstacle = false;
	if (m_ignoreObstacleID != INVALID_ID) {
		// Check for object on structure.
		// srj sez: check for obstacle on AIRFIELD... only want to do this for things
		// that are "parked" on the airfield, but not for things hovering over an obstacle
		// (eg, a chinook over a supply dock).
		Object *goalObj = TheGameLogic->findObjectByID(m_ignoreObstacleID);
		if (goalObj) {
			PathfindCell *ignoreCell = getClippedCell(goalObj->getLayer(), goalObj->getPosition());
			if ( (goalCell->getObstacleID()==ignoreCell->getObstacleID()) && (goalCell->getObstacleID() != INVALID_ID) ) {
				Object* newObstacle = TheGameLogic->findObjectByID(goalCell->getObstacleID());
				if (newObstacle != nullptr && newObstacle->isKindOf(KINDOF_FS_AIRFIELD))
				{
					m_ignoreObstacleID = goalCell->getObstacleID();
					goalOnObstacle = true;
				}
				else
				{
					if (m_ignoreObstacleID == goalCell->getObstacleID()) {
						goalOnObstacle = true;
					}
				}
			}
		}
	}

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(from, &startCellNdx);
 	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &clipFrom );
	if (parentCell == nullptr)
		return nullptr;

	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, parentCell ) == false) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}
	TCheckMovementInfo info;
	info.cell = startCellNdx;
	info.layer = obj->getLayer();
	info.centerInCell = centerInCell;
	info.radius = radius;
	info.considerTransient = blocked;
	info.acceptableSurfaces = locomotorSet.getValidSurfaces();
	if (!checkForMovement(obj, info) || info.enemyFixed) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}
	if (!PathTuning::Clearance && getClearanceShortage(parentCell, locomotorSet.getValidSurfaces(), radius) >= CLEARANCE_HARD_SHORTAGE) {
		m_isTunneling = true;	// ("clearance" lets a unit too close move away instead)
	}

	Bool gotHierarchicalPath = false;
	if (m_isTunneling) {
		m_zoneManager.setAllPassable(); // can't optimize.
	}	else {
		m_zoneManager.clearPassableFlags();
		Path *hPat = findClosestHierarchicalPath(isHuman, locomotorSet, from, rawTo, false, radius);
		if (hPat) {
			deleteInstance(hPat);
			gotHierarchicalPath = true;
		}	else {
			m_zoneManager.setAllPassable();
		}
	}
	const Bool startedStuck = m_isTunneling;

	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &pos2d);
		if (!parentCell->allocateInfo(pos2d)) {
#if RETAIL_COMPATIBLE_PATHFINDING
			if (s_useFixedPathfinding)
#endif
			{
				goalCell->releaseInfo();
			}
			return nullptr;
		}
	}
	parentCell->startPathfind(goalCell);

	PathfindCell *closesetCell = nullptr;
	Real closestDistanceSqr = FLT_MAX;
	Real closestDistScreenSqr = FLT_MAX;

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();
	Int count = 0;
	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	Bool foundGoal = false;
	while( !m_openList.empty() )
	{
		Real dx;
		Real dy;
		Real distSqr;
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal
			if (!goalOnObstacle) {
				// See if the goal is a valid destination.  If not, accept closest cell.
				if (closesetCell!=nullptr && !canPathThroughUnits && !checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), radius, centerInCell)) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
					break;
#else
					foundGoal = true;
					// Continue processing the open list to find a possibly closer cell. jba. [8/25/2003]
					continue;
#endif
				}
			}

			Bool show = TheGlobalData->m_debugAI;
#ifdef INTENSE_DEBUG
			Int count = 0;
			PathfindCell *cur;
			for (cur = m_closedList.getHead(); cur; cur=cur->getNextOpen()) {
				count++;
			}
			if (count>1000) {
				show = true;
				DEBUG_LOG(("FCP - cells %d obj %s %x", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big path FCP", false);
			}
#endif
			if (show)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), from, goalCell, centerInCell, blocked, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding) {
				parentCell->releaseInfo();
				goalCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
				goalCell->releaseInfo();
			}

			return path;
		}
		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );
		if (!m_isTunneling && checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), radius, centerInCell)) {
			if (!startedStuck || validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, parentCell )) {
				dx = IABS(goalCell->getXIndex()-parentCell->getXIndex());
				dy = IABS(goalCell->getYIndex()-parentCell->getYIndex());
				distSqr = dx*dx+dy*dy;
				if (distSqr<closestDistScreenSqr) {
					closestDistScreenSqr = distSqr;
				}
				distSqr += (parentCell->getCostSoFar()*(parentCell->getCostSoFar()*COST_TO_DISTANCE_FACTOR_SQR))*pathCostMultiplier;
				if (distSqr < closestDistanceSqr) {
					closesetCell = parentCell;
					closestDistanceSqr = distSqr;
				}
			}
		}

		dx = IABS(goalCell->getXIndex()-parentCell->getXIndex());
		dy = IABS(goalCell->getYIndex()-parentCell->getYIndex());
		distSqr = dx*dx+dy*dy;
		// If we are 2x farther than the closest location already found, don't continue.
		if (distSqr > closestDistScreenSqr*4) {
			Bool skip = false;
			if (!gotHierarchicalPath) {
				skip = true;
			}
			if (count>2000) {
				skip = true;
			}
			if (closestDistScreenSqr < 10*10*PATHFIND_CELL_SIZE_F) {
				skip = true;
			}
			if (skip) {
				continue;
			}
		}
		// If we haven't already found the goal cell, continue examining. [8/25/2003]
		if (!foundGoal) {
			// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell, true);
			count += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		}
		if (isSearchBudgetExhausted())
		{
			break;
		}
	}

	if (closesetCell) {
		// success - found a path to near the goal

		Bool show = TheGlobalData->m_debugAI;

#ifdef INTENSE_DEBUG
		if (count>5000) {
			show = true;
			DEBUG_LOG(("FCP CC cells %d obj %s %x", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
			if( obj->getAIUpdateInterface() )
			{
				DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
			}
#endif

			DEBUG_LOG(("%d Pathfind(findClosestPath) chugged from (%f,%f) to (%f,%f) --", TheGameLogic->getFrame(), from->x, from->y, to->x, to->y));
			DEBUG_LOG(("Unit '%s', time %f", obj->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));
#ifdef INTENSE_DEBUG
			TheScriptEngine->AppendDebugMessage("Big path FCP CC", false);
#endif
		}
#endif
		if (show)
			debugShowSearch(true);

		m_isTunneling = false;
		rawTo->x = closesetCell->getXIndex()*PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F/2.0f;
		rawTo->y = closesetCell->getYIndex()*PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F/2.0f;
		// construct and return path
		Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), from, closesetCell, centerInCell, blocked, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
		if (!s_useFixedPathfinding) {
			goalCell->releaseInfo();
			cleanOpenAndClosedLists();
		}
		else
#endif
		{
			cleanOpenAndClosedLists();
			parentCell->releaseInfo();
			goalCell->releaseInfo();
		}
		return path;
	}

	// failure - goal cannot be reached
#ifdef DEBUG_LOGGING
	Bool valid;
	valid = validMovementPosition( isCrusher, obj->getLayer(), locomotorSet, requiredBridgeHeight, to ) ;

	DEBUG_LOG(("Pathfind(findClosestPath) failed from (%f,%f) to (%f,%f), original valid %d --", TheGameLogic->getFrame(), from->x, from->y, to->x, to->y, valid));
	DEBUG_LOG(("Unit '%s', time %f", obj->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));
#endif
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI)
		debugShowSearch(false);
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		goalCell->releaseInfo();
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}
	return nullptr;
}


void Pathfinder::adjustCoordToCell(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer)
{
	if (centerInCell) {
		pos.x = ((Real)cellX + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)cellY + 0.5f) * PATHFIND_CELL_SIZE_F;
	} else {
		pos.x = ((Real)cellX+0.05) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)cellY+0.05) * PATHFIND_CELL_SIZE_F;
	}
	pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, layer );

	if (TheGlobalData->m_heightAboveTerrainIncludesWater) {
		//Adjust to water surface
		if (Real waterZ = 0; TheTerrainLogic->isUnderwater(pos.x, pos.y, &waterZ)) {
			if (waterZ > pos.z) pos.z = waterZ;
		}
	}
}


/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildActualPath( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, const Coord3D *fromPos,
																	PathfindCell *goalCell, Bool center, Bool blocked, Int requiredWaterLevel)
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildActualPath: goalCell == nullptr") );

	Path *path = newInstance(Path);

	if (goalCell->getPinched() && goalCell->getParentCell() && !goalCell->getParentCell()->getPinched()) {
		goalCell = goalCell->getParentCell();
	}

	prependCells(path, fromPos, goalCell, center);

	// cleanup the path by checking line of sight
	path->optimize(obj, acceptableSurfaces, blocked, requiredWaterLevel);

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		PathNode *node = path->getFirstNode();
		for( ; node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			color.red = color.green = 1;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = path->getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}

/**
 * Work backwards from goal cell to construct final path.
 */
void Pathfinder::prependCells( Path *path, const Coord3D *fromPos,
																	PathfindCell *goalCell, Bool center )
{
	// traverse path cells in REVERSE order, creating path in desired order
	// skip the LAST node, as that will be in the same cell as the unit itself - so use the unit's position
	Coord3D pos;
	PathfindCell *cell, *prevCell = nullptr;
	Bool goalCellNull = (goalCell->getParentCell()==nullptr);
	for( cell = goalCell; cell->getParentCell(); cell = cell->getParentCell() )
	{
		m_zoneManager.setPassable(cell->getXIndex(), cell->getYIndex(), true);
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		if (prevCell && cell->getXIndex()==prevCell->getXIndex() && cell->getYIndex()==prevCell->getYIndex()) {
			// transitioning layers.
			PathfindLayerEnum layer = cell->getLayer();
			if (layer==LAYER_GROUND) {
				layer = prevCell->getLayer();
			}
			DEBUG_ASSERTCRASH(layer!=LAYER_GROUND, ("Should have at 1 non-ground layer. jba"));
			path->getFirstNode()->setLayer(layer);
			continue;
		}

		Bool canOptimize = true;
		if (cell->getType() == PathfindCell::CELL_CLIFF) {
			if (prevCell && prevCell->getType() != PathfindCell::CELL_CLIFF) {
				if (path->getFirstNode()) {
					path->getFirstNode()->setCanOptimize(false);
				}
			}
		}	else {
			if (prevCell && prevCell->getType() == PathfindCell::CELL_CLIFF) {
				canOptimize = false;
			}
		}

		path->prependNode( &pos, cell->getLayer() );
		path->getFirstNode()->setCanOptimize(canOptimize);
		if (cell->isBlockedByAlly()) {
			path->setBlockedByAlly(true);
		}

		//Check if cell is below an opened/destroyed bridge
		PathfindLayerEnum belowBridgeLayer = cell->getUnderDestroyedBridgeLayer();
		if (belowBridgeLayer > LAYER_GROUND) {
			path->setPathBelowBridge(belowBridgeLayer);
		}

		if (prevCell) {
			prevCell->clearParentCell();
		}
		prevCell = cell;
	}

#if RETAIL_COMPATIBLE_PATHFINDING
	// TheSuperHackers @info This pathway is here for retail compatibility, it is to catch when a starting cell has a dangling parent that contains no pathing information
	// Beyond this point a retail client will crash due to a null pointer access within cell->getXIndex()
	// To recover from this we set the cell to the previous cell, which should be the actual starting cell then set to use the fixed pathing and perform a forced cleanup
	if (!s_useFixedPathfinding) {
		if (!cell->hasInfo()) {
			cell = prevCell;

			m_zoneManager.setPassable(cell->getXIndex(), cell->getYIndex(), true);
			if (goalCellNull) {
				// Very short path.
				adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
				path->prependNode( &pos, cell->getLayer() );
			}
			// put actual start position as first node on the path, so it begins right at the unit's feet
			if (fromPos->x != path->getFirstNode()->getPosition()->x || fromPos->y != path->getFirstNode()->getPosition()->y) {
				path->prependNode( fromPos, cell->getLayer() );
			}

			s_useFixedPathfinding = true;
			forceCleanCells();
			return;
		}
	}
#endif

	m_zoneManager.setPassable(cell->getXIndex(), cell->getYIndex(), true);
	if (goalCellNull) {
		// Very short path.
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		path->prependNode( &pos, cell->getLayer() );
	}
	// put actual start position as first node on the path, so it begins right at the unit's feet
	if (fromPos->x != path->getFirstNode()->getPosition()->x || fromPos->y != path->getFirstNode()->getPosition()->y) {
		path->prependNode( fromPos, cell->getLayer() );
	}

}

void Pathfinder::setDebugPath(Path *newDebugpath)
{
	if (TheGlobalData->m_debugAI)
	{
		// copy the path for debugging
		deleteInstance(debugPath);
		debugPath = newInstance(Path);

		for( PathNode *copyNode = newDebugpath->getFirstNode(); copyNode; copyNode = copyNode->getNextOptimized() )
			debugPath->appendNode( copyNode->getPosition(), copyNode->getLayer() );
	}

}

/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Int Pathfinder::iterateCellsAlongLine( const Coord3D& startWorld, const Coord3D& endWorld,
																			PathfindLayerEnum layer, CellAlongLineProc proc, void* userData )
{
	ICoord2D start, end;
	worldToCell( &startWorld, &start );
	worldToCell( &endWorld, &end );
	return iterateCellsAlongLine(start, end, layer, proc, userData);
}
/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Int Pathfinder::iterateCellsAlongLine( const ICoord2D &start, const ICoord2D &end,
																			PathfindLayerEnum layer, CellAlongLineProc proc, void* userData )
{
	Int delta_x = abs(end.x - start.x);			// The difference between the x's
	Int delta_y = abs(end.y - start.y);			// The difference between the y's
	Int x = start.x;												// Start x off at the first pixel
	Int y = start.y;												// Start y off at the first pixel

	Int xinc1, xinc2;
	if (end.x >= start.x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	Int yinc1, yinc2;
	if (end.y >= start.y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}

	Bool checkY = true;
	Int den, num, numadd, numpixels;
	if (delta_x >= delta_y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta_x;
		num = delta_x / 2;
		numadd = delta_y;
		numpixels = delta_x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta_y;
		num = delta_y / 2;
		numadd = delta_x;
		numpixels = delta_y;							// There are more y-values than x-values
	}

	PathfindCell* from = nullptr;
	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PathfindCell* to = getCell( layer, x, y );
		if (to==nullptr) return 0;

		Int ret = (*proc)(this, from, to, x, y, userData);
		if (ret != 0)
			return ret;

		num += numadd;										// Increase the numerator by the top of the fraction
		if (num >= den)										// Check if numerator >= denominator
		{
			num -= den;											// Calculate the new numerator value
			x += xinc1;											// Change the x as appropriate
			y += yinc1;											// Change the y as appropriate
			from = to;
			to = getCell( layer, x, y );
			if (to==nullptr) return 0;
			Int ret = (*proc)(this, from, to, x, y, userData);
			if (ret != 0)
				return ret;
		}
		x += xinc2;												// Change the x as appropriate
		y += yinc2;												// Change the y as appropriate

		from = to;
	}

	return 0;
}

//-----------------------------------------------------------------------------

static ObjectID getSlaverID(const Object* o)
{
	for (BehaviorModule** update = o->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != nullptr)
		{
			return sdu->getSlaverID();
		}
	}

	return INVALID_ID;
}

static ObjectID getContainerID(const Object* o)
{
	const Object* container = o ? o->getContainedBy() : nullptr;
	return container ? container->getID() : INVALID_ID;
}

struct segmentIntersectsStruct
{
	Object *theTallBuilding;
	ObjectID ignoreBuilding;
};

/*static*/ Int Pathfinder::segmentIntersectsBuildingCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	segmentIntersectsStruct* d = (segmentIntersectsStruct*)userData;

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		Object *obj = TheGameLogic->findObjectByID(to->getObstacleID());
		if (obj && obj->isKindOf(KINDOF_AIRCRAFT_PATH_AROUND)) {
			if (obj->getID() == d->ignoreBuilding) {
				return 0;
			}
			d->theTallBuilding = obj;
			return 1;
		}
	}

	return 0;	// keep going
}



struct ViewBlockedStruct
{
	const Object *obj;
	const Object *objOther;
};


/*static*/ Int Pathfinder::lineBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const ViewBlockedStruct* d = (const ViewBlockedStruct*)userData;

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{

		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		// nor does the object we're trying to see!
		if (to->isObstaclePresent(d->objOther->getID()))
			return 0;

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->objOther)))
			return 0;

		// if the obstacle is transparent, ignore it, since this callback is only used for line-of-sight. (srj)
		if (to->isObstacleTransparent())
			return 0;

		return 1;	// bail early
	}

	return 0;	// keep going
}

struct ViewAttackBlockedStruct
{
	const Object *obj;
	const Object *victim;
	const PathfindCell *victimCell;
	Int		skipCount;
};

/*static*/ Int Pathfinder::attackBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	ViewAttackBlockedStruct* d = (ViewAttackBlockedStruct*)userData;

	if (d->skipCount>0) {
		d->skipCount--;
		return 0;
	}
	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		if (d->victim) {
			// nor does the object we're trying to attack!
			if (to->isObstaclePresent(d->victim->getID()))
				return 0;
			// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.
			if (to->isObstaclePresent(getSlaverID(d->victim)))
				return 0;
		}

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		if (to->isObstacleTransparent())
			return 0;
		//Kris: Added the check for victimCell because in China01 -- after the intro, NW of your
		//base is a cream colored building that lies in a negative coord. When you order units to
		//force attack it, it crashes.
		if( d->victimCell && to->isObstaclePresent( d->victimCell->getObstacleID() ) )
		{
			// Victim is inside the bounds of another object.  We don't let this block us,
			// as usually it is on the edge and it looks like we should be able to shoot it. jba.
			return 0;
		}
		return 1;	// bail early
	}

	return 0;	// keep going
}

struct BridgeDeckStruct
{
	Coord3D from;
	Coord3D to;
	Bool above;
	Bool below;
};

// Stops once the sight line has been both above and below a bridge deck.
static Int lineCrossesBridgeDeckCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	BridgeDeckStruct* d = (BridgeDeckStruct*)userData;
	PathfindLayerEnum deck = to->getBridgeLayer();
	if (deck <= LAYER_GROUND)
	{
		return 0;
	}

	Real x = (to_x + 0.5f) * PATHFIND_CELL_SIZE_F;
	Real y = (to_y + 0.5f) * PATHFIND_CELL_SIZE_F;
	Real dx = d->to.x - d->from.x;
	Real dy = d->to.y - d->from.y;
	Real lenSqr = dx*dx + dy*dy;
	Real t = lenSqr > 0.0f ? ((x - d->from.x)*dx + (y - d->from.y)*dy) / lenSqr : 0.0f;
	if (t < 0.0f)
	{
		t = 0.0f;
	}
	else if (t > 1.0f)
	{
		t = 1.0f;
	}
	Real lineZ = d->from.z + (d->to.z - d->from.z) * t;
	if (lineZ > TheTerrainLogic->getLayerHeight(x, y, deck))
	{
		d->above = true;
	}
	else
	{
		d->below = true;
	}
	return (d->above && d->below) ? 1 : 0;
}

// Deck cells sit inside their bridge's bounds, so a line clear of every bounds box crosses no deck.
static Bool lineNearBridge(const Coord3D& from, const Coord3D& to)
{
	const Real loX = (from.x < to.x ? from.x : to.x) - PATHFIND_CELL_SIZE_F;
	const Real hiX = (from.x > to.x ? from.x : to.x) + PATHFIND_CELL_SIZE_F;
	const Real loY = (from.y < to.y ? from.y : to.y) - PATHFIND_CELL_SIZE_F;
	const Real hiY = (from.y > to.y ? from.y : to.y) + PATHFIND_CELL_SIZE_F;
	for (Bridge* bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext())
	{
		const Region2D* bounds = bridge->getBounds();
		if (loX <= bounds->hi.x && hiX >= bounds->lo.x && loY <= bounds->hi.y && hiY >= bounds->lo.y)
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
Bool Pathfinder::isViewBlockedByObstacle(const Object* obj, const Object* objOther)
{
	ViewBlockedStruct info;
	info.obj = obj;
	info.objOther = objOther;
	if (objOther && objOther->isSignificantlyAboveTerrain()) {
		return false; // We don't check los to flying objects.  jba.
	}
#if 1
	return isAttackViewBlockedByObstacle(obj, *obj->getPosition(), objOther, *objOther->getPosition());
#else
	PathfindLayerEnum layer = objOther->getLayer();
	if (layer==LAYER_GROUND) {
		layer = obj->getLayer();
	}
	Int ret = iterateCellsAlongLine(*obj->getPosition(), *objOther->getPosition(),
		layer, lineBlockedByObstacleCallback, &info);
	return ret != 0;
#endif
}


//-----------------------------------------------------------------------------
Bool Pathfinder::isAttackViewBlockedByObstacle(const Object* attacker, const Coord3D& attackerPos, const Object* victim, const Coord3D& victimPos)
{
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - attackerPos is (%g,%g,%g) (%X,%X,%X)",
	//	attackerPos.x, attackerPos.y, attackerPos.z,
	//	AS_INT(attackerPos.x),AS_INT(attackerPos.y),AS_INT(attackerPos.z)));
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - victimPos is (%g,%g,%g) (%X,%X,%X)",
	//	victimPos.x, victimPos.y, victimPos.z,
	//	AS_INT(victimPos.x),AS_INT(victimPos.y),AS_INT(victimPos.z)));
	// Global switch to turn this off in case it doesn't work.
	if (!TheAI->getAiData()->m_attackUsesLineOfSight)
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 1"));
		return false;
	}

	// A bridge deck between the two is a wall from either side.
	if (lineNearBridge(attackerPos, victimPos))
	{
		BridgeDeckStruct deck;
		deck.from = attackerPos;
		deck.from.z += attacker->getGeometryInfo().getMaxHeightAbovePosition();
		deck.to = victimPos;
		if (victim)
		{
			victim->getGeometryInfo().getCenterPosition(victimPos, deck.to);
		}
		deck.above = false;
		deck.below = false;
		if (iterateCellsAlongLine(attackerPos, victimPos, LAYER_GROUND, lineCrossesBridgeDeckCallback, &deck) != 0)
		{
			return true;
		}
	}

	// If the attacker doesn't need line of sight, isn't blocked.
	if (!attacker->isKindOf(KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT))
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 2"));
		return false;
	}

#if RTS_ZEROHOUR
	if (attacker->isCurrentWeaponIgnoringLineOfSight())
	{
		return false;
	}
#endif

// srj sez: this is a good start at taking terrain into account for attacks, but findAttackPath needs to be smartened also
#define LOS_TERRAIN
#ifdef LOS_TERRAIN
	const Weapon* w = attacker->getCurrentWeapon();
	if (attacker->isKindOf(KINDOF_IMMOBILE)) {
		// Don't take terrain blockage into account, since we can't move around it. jba.
		w = nullptr;
	}
	if (w)
	{
#if !RETAIL_COMPATIBLE_CRC
		// TheSuperHackers @bugfix Stubbjax 23/08/2026 Don't consider the attack blocked if it is a contact weapon.
		// This allows weapons such as suicide bombs to be triggered if the unit is blocked by building geometry.
		if (w->isContactWeapon())
			return false;
#endif

		Bool viewBlocked;
		if (victim)
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victim);
		else
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victimPos);

		if (viewBlocked)
		{
			//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 3"));
			return true;
		}
	}
#endif

	ViewAttackBlockedStruct info;
	info.obj = attacker;
	info.victim = victim;
	PathfindLayerEnum layer = LAYER_GROUND;
	if (victim) {
		layer = victim->getLayer();
	}
	info.victimCell = getCell(layer, &victimPos);

	info.skipCount = 0;
	if (attacker->getLayer() != LAYER_GROUND)
	{
		info.skipCount = 3;	/// srj -- someone wanna tell me what this magic number means?
												/// jba - Yes, it means that if someone is on a bridge, or rooftop, they can see
												///      3 pathfind cells out of whatever they are standing on.
												/// srj -- awesome! thank you very much :-)
		if (layer==LAYER_GROUND) {
			layer = attacker->getLayer();
		}
	}

	Int ret = iterateCellsAlongLine(attackerPos, victimPos, layer, attackBlockedByObstacleCallback, &info);
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 4"));
	return ret != 0;
}

static void computeNormalRadialOffset(const Coord3D& from,	Coord3D& insert, const Coord3D& to,
																			Object *obj, Real radius)
{
	Real crossProduct;
	Real dx = to.x - from.x;
	Real dy = to.y -from.y;
	Coord3D objPos = *obj->getPosition();


	Real objDx = objPos.x - from.x;
	Real objDy = objPos.y - from.y;

	crossProduct = dx*objDy - dy*objDx;

	Coord3D fromToNormal;
	fromToNormal.z = 0;
	if (crossProduct>0) {
		fromToNormal.x = dy;
		fromToNormal.y = -dx;
	}	else {
		fromToNormal.x = -dy;
		fromToNormal.y = dx;
	}
	fromToNormal.normalize();
	Real length = radius;
	insert = *obj->getPosition();
	insert.x += fromToNormal.x*length;
	insert.y += fromToNormal.y*length;

}

//-----------------------------------------------------------------------------
Bool Pathfinder::segmentIntersectsTallBuilding(const PathNode *curNode,
										PathNode *nextNode,  ObjectID ignoreBuilding, Coord3D *insertPos1,  Coord3D *insertPos2,  Coord3D *insertPos3 )
{
	segmentIntersectsStruct info;
	info.theTallBuilding = nullptr;
	info.ignoreBuilding = ignoreBuilding;

	Coord3D fromPos = *curNode->getPosition();
	Coord3D toPos = *nextNode->getPosition();

	Int i;
	for (i=0; i<2; i++) {
		Int ret = iterateCellsAlongLine(fromPos, toPos, LAYER_GROUND, segmentIntersectsBuildingCallback, &info);
		if (ret!=0 && info.theTallBuilding) {
			// see if toPos is inside the radius of the tall building.
			Coord3D bldgPos = *info.theTallBuilding->getPosition();
			Coord2D delta;
			Real radius = info.theTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
			delta.x = toPos.x - bldgPos.x;
			delta.y = toPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				toPos.x = bldgPos.x+delta.x;
				toPos.y = bldgPos.y+delta.y;
				nextNode->setPosition(&toPos);
				continue;
			}
			delta.x = fromPos.x - bldgPos.x;
			delta.y = fromPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				fromPos.x = bldgPos.x+delta.x;
				fromPos.y = bldgPos.y+delta.y;
			}


			computeNormalRadialOffset(fromPos, *insertPos2, toPos, info.theTallBuilding, radius);
			computeNormalRadialOffset(fromPos, *insertPos1, *insertPos2, info.theTallBuilding, radius);
			computeNormalRadialOffset(*insertPos2, *insertPos3, toPos, info.theTallBuilding, radius);

			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
Bool Pathfinder::circleClipsTallBuilding(	const Coord3D *from, const Coord3D *to, Real circleRadius, ObjectID ignoreBuilding, Coord3D *adjustTo)
{
	PartitionFilterAcceptByKindOf filterKindof(MAKE_KINDOF_MASK(KINDOF_AIRCRAFT_PATH_AROUND), KINDOFMASK_NONE);
	PartitionFilter *filters[] = { &filterKindof, nullptr };
	Object* tallBuilding = ThePartitionManager->getClosestObject(to, circleRadius, FROM_BOUNDINGSPHERE_2D, filters);
	if (tallBuilding) {
		Real radius = tallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
		computeNormalRadialOffset(*from, *adjustTo, *to, tallBuilding, circleRadius+radius);
		Object* otherTallBuilding = ThePartitionManager->getClosestObject(adjustTo, circleRadius, FROM_BOUNDINGSPHERE_2D, filters);
		if (otherTallBuilding && otherTallBuilding!=tallBuilding) {
			radius = otherTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
			Coord3D tmpTo = *adjustTo;
			computeNormalRadialOffset(*from, *adjustTo, tmpTo, otherTallBuilding, circleRadius+radius);
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------

struct LinePassableStruct
{
	const Object *obj;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	Int radius;
	Bool centerInCell;
	Bool blocked;
	Bool allowPinched;
	Int requiredWaterDepth; //Added for ships to not be able to get too close to coast
};

/*static*/ Int Pathfinder::linePassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const LinePassableStruct* d = (const LinePassableStruct*)userData;

	Short requiredBridgeHeight = d->obj ? d->obj->getRequiredBridgeHeight() : 0;
	Bool isCrusher = d->obj ? d->obj->getCrusherLevel() > 0 : false;
	TCheckMovementInfo info;
	info.cell.x = to_x;
	info.cell.y = to_y;
	info.layer = to->getLayer();
	info.centerInCell = d->centerInCell;
	info.radius = d->radius;
	info.considerTransient = d->blocked;
	info.acceptableSurfaces = d->acceptableSurfaces;
	if (!pathfinder->checkForMovement(d->obj, info))
	{
		return 1;	// bail out
	}

	if (info.allyFixedCount || info.enemyFixed)
	{
		return 1;	// bail out
	}

	if (!d->allowPinched && to->getPinched()) {
		return 1; // bail out.
	}

	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		if (to->getType() == PathfindCell::CELL_CLEAR) {
			return 0;
		}
	}

	if (pathfinder->validMovementPosition( isCrusher, d->acceptableSurfaces, d->requiredWaterDepth, requiredBridgeHeight, to, from ) == false)
	{
		return 1;	// bail out
	}

	if (PathTuning::Clearance)
	{
		// TheSuperHackers @feature simonx645 16/11/2026 "clearance" (PathTuning.h): a straightened route must not cut
		// across the cells the search refused or priced near a shore
		if (d->obj && d->radius > 0 && to->getLayer() == LAYER_GROUND)
		{
			Int hard;
			const Int shortage = pathfinder->ringShortage(to_x, to_y, d->acceptableSurfaces, d->radius, &hard);
			if (hard >= PathTuning::CLEARANCE_HARD || (shortage > 0 && !d->allowPinched))
			{
				return 1;	// bail out
			}
		}
	}
	else
	{
		Int clearanceShortage = pathfinder->getClearanceShortage(to, d->acceptableSurfaces, d->radius);
		if (clearanceShortage >= CLEARANCE_HARD_SHORTAGE || (clearanceShortage > 0 && !d->allowPinched))
		{
			return 1;	// bail out
		}
	}

	if (to->getType() == PathfindCell::CELL_WATER && d->requiredWaterDepth > 0 && to->getWaterLevel() < d->requiredWaterDepth) {
		return 1; // bail out, ship needs deeper water
	}

	return 0;	// keep going
}

//-----------------------------------------------------------------------------

struct GroundPathPassableStruct
{
	Int		diameter;
	Bool	crusher;
};

/*static*/ Int Pathfinder::groundPathPassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const GroundPathPassableStruct* d = (const GroundPathPassableStruct*)userData;

	Int curDiameter = pathfinder->clearCellForDiameter(d->crusher, to_x, to_y, to->getLayer(), d->diameter);
	if (curDiameter==d->diameter) return 0;	//  good to go.
	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		return 0;
	}

	return 1;	// failed.
}

//-----------------------------------------------------------------------------

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isLinePassable( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces,
																PathfindLayerEnum layer, const Coord3D& startWorld,
																const Coord3D& endWorld, Bool blocked,
																Bool allowPinched, Int requiredWaterDepth)
{
	LinePassableStruct info;
	//CRCDEBUG_LOG(("Pathfinder::isLinePassable(): %d %d %d ", m_ignoreObstacleID, m_isMapReady, m_isTunneling));

	info.obj = obj;
	info.acceptableSurfaces = acceptableSurfaces;
	getRadiusAndCenter(obj, info.radius, info.centerInCell);
	info.blocked = blocked;
	info.allowPinched = allowPinched;
	info.requiredWaterDepth = requiredWaterDepth;

	Int ret = iterateCellsAlongLine(startWorld, endWorld, layer, linePassableCallback, (void*)&info);
	return ret == 0;
}

//-----------------------------------------------------------------------------

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isGroundPathPassable( Bool isCrusher, const Coord3D& startWorld, PathfindLayerEnum startLayer,
		const Coord3D& endWorld, Int pathDiameter)
{
	GroundPathPassableStruct info;

	info.diameter = pathDiameter;
	info.crusher = isCrusher;

	Int ret = iterateCellsAlongLine(startWorld, endWorld, startLayer, groundPathPassableCallback, (void*)&info);
	return ret == 0;
}

/**
 * Classify the cells under the bridge
 * If 'repaired' is true, bridge is repaired
 * If 'repaired' is false, bridge has been damaged to be impassable
 */
void Pathfinder::changeBridgeState( PathfindLayerEnum layer, Bool repaired)
{
	if (m_layers[layer].isUnused()) return;
	if (m_layers[layer].setDestroyed(!repaired)) {
		m_zoneManager.markZonesDirtyNow();
	}
}

/**
 * Disconnects the ground cells from the bridge, frees its layer cells and drops the Bridge pointer
 */
void Pathfinder::removeBridge( PathfindLayerEnum layer )
{
	if (layer <= LAYER_GROUND || layer > LAYER_LAST) {
		return;
	}
	if (m_layers[layer].isUnused()) {
		return;
	}
	m_layers[layer].setDestroyed(true);
	m_layers[layer].reset();
	m_zoneManager.markZonesDirtyNow();
}

void Pathfinder::getRadiusAndCenter(const Object *obj, Int &iRadius, Bool &center)
{
	if (!obj)
	{
		center = true;
		iRadius = 0;
		return;
	}
	const Int max_radius = obj->getTemplate() != nullptr ? obj->getTemplate()->getMaxPathFindingCellRadius() : 2;

	Real diameter = 2*obj->getGeometryInfo().getBoundingCircleRadius();
	if (diameter>PATHFIND_CELL_SIZE_F && diameter<2.0f*PATHFIND_CELL_SIZE_F) {
		diameter = 2.0f*PATHFIND_CELL_SIZE_F;
	}
	iRadius = REAL_TO_INT_FLOOR(diameter/PATHFIND_CELL_SIZE_F+0.3f);
	center = false;
	if (iRadius==0) iRadius++;
	if (iRadius&1)
	{
		center = true;
	}
	iRadius /= 2;
	if (iRadius > max_radius)
	{
		iRadius = max_radius;
		center = true;
	}
}

/**
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateGoal( Object *obj, const Coord3D *newGoalPos, PathfindLayerEnum layer)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	if (PathTuning::DeadLeaveGrid && obj->isEffectivelyDead()) {
		removeGoal(obj);	// "dead": no goal is kept for a dead unit
		return;
	}
	if (!ai->isDoingGroundMovement()) {
#if RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING
		Bool isUnmannedHelicopter = false;
#else
		// exception:sniped choppers are on ground
		Bool isUnmannedHelicopter = ( obj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) && obj->isDisabledByType( DISABLED_UNMANNED  ) ) ;
#endif
		if (!isUnmannedHelicopter) {
			updateAircraftGoal(obj, newGoalPos);
			return;
		}
	}

	PathfindLayerEnum originalLayer = obj->getDestinationLayer();

	Bool layerChanged = originalLayer != layer;

	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;
	} else {
		doLayer = true;
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true;
		}
	}

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (!layerChanged && newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	removeGoal(obj);

	obj->setDestinationLayer(layer);
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	Bool warn = true;
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						//Units got stuck close to each other.  jba
					}
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						//Units got stuck close to each other.  jba
					}
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
		}
	}

}

/**
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateAircraftGoal( Object *obj, const Coord3D *newGoalPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	removeGoal(obj);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	if (ai->isDoingGroundMovement()) {
		return;  // shouldn't really happen, but just in case.
	}

	// For now, we are only doing HOVER, and WINGS.
	if (!ai->isAircraftThatAdjustsDestination()) return;

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}

	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cell = getCell(LAYER_GROUND, i, j);
			if (cell) {
				cellNdx.x = i;
				cellNdx.y = j;
				cell->setGoalAircraft(obj->getID(), cellNdx);
			}
		}
	}

}

/**
 * Removes the goal cell for an ai unit.
 * Used for a unit that is going to be moving several times, like following a waypoint path,
 * or intentionally collides with other units (like a car bomb). jba
 */
void Pathfinder::removeGoal( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	if (radius==0) {
		radius++;
	}
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	newCell.x = newCell.y = -1;
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	ICoord2D cellNdx;
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	if (goalCell.x>=0 && goalCell.y>=0) {
		for (i=goalCell.x-radius; i<goalCell.x+numCellsAbove; i++) {
			for (j=goalCell.y-radius; j<goalCell.y+numCellsAbove; j++) {
				PathfindCell	*cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (cell->getGoalUnit()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalUnit(INVALID_ID, cellNdx);
					}
					if (cell->getGoalAircraft()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalAircraft(INVALID_ID, cellNdx);
					}
				}
				if (obj->getDestinationLayer()!=LAYER_GROUND) {
					cell = getCell( obj->getDestinationLayer(), i, j);
					if (cell) {
						if (cell->getGoalUnit()==obj->getID()) {
							cellNdx.x = i;
							cellNdx.y = j;
							cell->setGoalUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/**
 * Updates the position cell for an ai unit.
 */
void Pathfinder::updatePos( Object *obj, const Coord3D *newPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE))
	{
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady)
		return;

	// TheSuperHackers @feature simonx645 16/11/2026 "dead" (PathTuning.h): a dead unit is off the map at once, and a corpse
	// still sliding is not put back on it
	if (PathTuning::DeadLeaveGrid && obj->isEffectivelyDead()) {
		removeUnitFromPathfindMap(obj);
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
		return; // only consider ai objects.

	ICoord2D curCell = *ai->getCurPathfindCell();
	if (!ai->isDoingGroundMovement())
	{
		if (curCell.x>=0 && curCell.y>=0)
		{
			removePos(obj);
		}
		return;
	}

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell)
		numCellsAbove++;
	if (centerInCell)
	{
		newCell.x = REAL_TO_INT_FLOOR(newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newPos->y/PATHFIND_CELL_SIZE_F);
	}
	else
	{
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==curCell.x && newCell.y == curCell.y)
	{
		return;
	}

	PathfindLayerEnum layer = obj->getLayer();
	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;	// just have to do ground
	} else {
		doLayer = true; // have to do the layer
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true; // In this case, have to both layer & ground, as they overlap here.
		}
	}

	ai->setCurPathfindCell(newCell);
	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cellNdx.x = i;
			cellNdx.y = j;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
		}
	}
}

/**
 * Removes the position cell flags for an ai unit.
 */
void Pathfinder::removePos( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady) return;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D curCell = *ai->getCurPathfindCell();
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	PathfindLayerEnum layer = obj->getLayer();

	ICoord2D newCell;
	newCell.x = newCell.y = -1;
	ai->setCurPathfindCell(newCell);

	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/**
 * Removes a mobile unit from the pathfind grid.
 */
void Pathfinder::removeUnitFromPathfindMap(  Object *obj )
{
	removePos(obj);
	removeGoal(obj);
}

Bool Pathfinder::moveAllies(Object *obj, Path *path)
{

#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
extern Bool g_UT_startTiming;
if (g_UT_startTiming) return false;
#endif
	if (!obj->isKindOf(KINDOF_DOZER) && !obj->isKindOf(KINDOF_HARVESTER)) {
		// Harvesters & dozers want a clear path.
		if (!path->getBlockedByAlly()) {
			return FALSE; // Only move units if it is required.
		}
	}
	LatchRestore<Int> recursiveDepth(m_moveAlliesDepth, m_moveAlliesDepth+1);
	if (m_moveAlliesDepth > 2) {
		return false;
	}

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	PathNode *node;
	ObjectID ignoreId = INVALID_ID;
	if (obj->getAIUpdateInterface()) {
		ignoreId = obj->getAIUpdateInterface()->getIgnoredObstacleID();
	}
	for( node = path->getLastNode(); node && node != path->getFirstNode(); node = node->getPrevious() )	{
		ICoord2D curCell;
		worldToCell(node->getPosition(), &curCell);
		Int i, j;
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				PathfindCell	*cell = getCell(node->getLayer(), i, j);
				if (!cell) {
					continue; // Cell is not on the pathfinding grid
				}

				ObjectID unitId = cell->getPosUnit();
				if (unitId==INVALID_ID) {
					continue;
				}

				if (unitId==obj->getID()) {
					continue;	// It's us.
				}

				if (unitId==ignoreId) {
					continue;	 // It's the one we are ignoring.
				}

				Object *otherObj = TheGameLogic->findObjectByID(unitId);
				if (!otherObj) {
					continue;
				}

				if (obj->getRelationship(otherObj)!=ALLIES) {
					continue;  // Only move allies.
				}

				if (obj->isKindOf(KINDOF_INFANTRY) && otherObj->isKindOf(KINDOF_INFANTRY)) {
					continue;  // infantry can walk through other infantry, so just let them.
				}
				if (obj->isKindOf(KINDOF_INFANTRY) && !otherObj->isKindOf(KINDOF_INFANTRY)) {
					// If this is a general clear operation, don't let infantry push vehicles.
					if (!path->getBlockedByAlly()) {
						continue;
					}
				}

				if (!otherObj->getAI() || otherObj->getAI()->isMoving()) {
					continue;
				}

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
				if (otherObj->getAI()->isAttacking()) {
					continue; // Don't move units that are attacking. [8/14/2003]
				}

				//Kris: Patch 1.01 November 3, 2003
				//Black Lotus exploit fix -- moving while hacking.
				if( otherObj->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || otherObj->getAI()->isBusy() ) {
					continue; // Packing or unpacking objects for example
				}
#endif

				//DEBUG_LOG(("Moving ally"));
				otherObj->getAI()->aiMoveAwayFromUnit(obj, CMD_FROM_AI);
			}
		}
	}
	return true;
}


/**
 * Moves an allied unit out of the path of another unit.
 * Uses A* algorithm.
 */
Path *Pathfinder::getMoveAwayFromPath(Object* obj, Object *otherObj,
											Path *pathToAvoid, Object *otherObj2, Path *pathToAvoid2)
{
	if (!m_isMapReady)
		return nullptr; // Should always be ok.

#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;

	Bool otherCenter;
	Int otherRadius;
	getRadiusAndCenter(otherObj, otherRadius, otherCenter);

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;

	m_zoneManager.setAllPassable();

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);

	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));

	// determine start cell
	ICoord2D startCellNdx;
	Coord3D startPos = *obj->getPosition();
	if (!centerInCell) {
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
	}
	worldToCell(&startPos, &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), obj->getPosition() );
	if (!parentCell)
		return nullptr;

	if (!obj->getAIUpdateInterface()) // shouldn't happen, but can't move it without an ai.
		return nullptr; 

	const LocomotorSet& locomotorSet = obj->getAIUpdateInterface()->getLocomotorSet();

	m_isTunneling = false;
	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, parentCell ) == false) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}

	TCheckMovementInfo info;
	info.cell = startCellNdx;
	info.layer = obj->getLayer();
	info.centerInCell = centerInCell;
	info.radius = radius;
	info.considerTransient = false;
	info.acceptableSurfaces = locomotorSet.getValidSurfaces();
	if (!checkForMovement(obj, info) || info.enemyFixed) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}

	beginSearchBudget(PFS_AWAY);
	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind(nullptr);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//


	Real boxHalfWidth = radius*PATHFIND_CELL_SIZE_F - (PATHFIND_CELL_SIZE_F/4.0f);
	if (centerInCell) boxHalfWidth+=PATHFIND_CELL_SIZE_F/2;
	boxHalfWidth += otherRadius*PATHFIND_CELL_SIZE_F;
	if (otherCenter) boxHalfWidth+=PATHFIND_CELL_SIZE_F/2;

	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		Region2D bounds;
		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());
		bounds.lo.x = cellCenter.x-boxHalfWidth;
		bounds.lo.y = cellCenter.y-boxHalfWidth;
		bounds.hi.x = cellCenter.x+boxHalfWidth;
		bounds.hi.y = cellCenter.y+boxHalfWidth;
		PathNode *node;
		Bool overlap = false;

		for( node = pathToAvoid->getFirstNode(); node && node->getNextOptimized(); node = node->getNextOptimized() )	{
			Coord2D start, end;
			start.x = node->getPosition()->x;
			start.y = node->getPosition()->y;
			end.x = node->getNextOptimized()->getPosition()->x;
			end.y = node->getNextOptimized()->getPosition()->y;
			if (LineInRegion(&start, &end, &bounds)) {
				overlap = true;
				break;
			}
		}

		if (!overlap && pathToAvoid2) {
			for( node = pathToAvoid2->getFirstNode(); node && node->getNextOptimized(); node = node->getNextOptimized() )	{
				Coord2D start, end;
				start.x = node->getPosition()->x;
				start.y = node->getPosition()->y;
				end.x = node->getNextOptimized()->getPosition()->x;
				end.y = node->getNextOptimized()->getPosition()->y;
				if (LineInRegion(&start, &end, &bounds)) {
					overlap = true;
					break;
				}
			}
		}
		if (!overlap) {
			if (startCellNdx.x == parentCell->getXIndex() && startCellNdx.y == parentCell->getYIndex()) {
				// we didn't move. Always move at least 1 cell. jba.
				overlap = true;
			}
		}
		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		if (!overlap && checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(),
				parentCell->getLayer(), radius, centerInCell)) {
			// success - found a path to the goal
			if (false && TheGlobalData->m_debugAI)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *newPath = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding) {
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
			}
			return newPath;
		}
		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell, true);

		examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		if (isSearchBudgetExhausted())
		{
			break;
		}
	}

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	debugShowSearch(true);
#endif

	DEBUG_LOG(("%d getMoveAwayFromPath pathfind failed --", TheGameLogic->getFrame()));
	DEBUG_LOG(("Unit '%s', time %f", obj->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));

	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
	}
	return nullptr;
}


/** Patch to the exiting path from the current position, either because we became blocked,
  or because we had to move off the path to avoid other units. */
Path *Pathfinder::patchPath( const Object *obj, const LocomotorSet& locomotorSet,
		Path *originalPath, Bool blocked )
{
	//CRCDEBUG_LOG(("Pathfinder::patchPath()"));
#ifdef DEBUG_LOGGING
	Int startTimeMS = ::GetTickCount();
#endif
	if (originalPath==nullptr) return nullptr;
	beginSearchBudget(PFS_PATCH);
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	m_zoneManager.setAllPassable();

	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));

	enum {CELL_LIMIT = 2000}; // max cells to examine.
	Int cellCount = 0;

	Coord3D currentPosition = *obj->getPosition();

	// determine start cell
	ICoord2D startCellNdx;
	Coord3D startPos = *obj->getPosition();
	if (!centerInCell) {
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
	}
	worldToCell(&startPos, &startCellNdx);
	//worldToCell(obj->getPosition(), &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &currentPosition);
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr; // shouldn't happen, but can't move it without an ai.
	}

	m_isTunneling = false;

	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind( nullptr);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	if (TheGlobalData->m_debugAI)
	{
		RGBColor color;
		color.setFromInt(0);
		addIcon(nullptr, 0,0,color);
	}
#endif

	PathNode *startNode;
	Coord3D goalPos = *originalPath->getLastNode()->getPosition();
	Real goalDeltaSqr = sqr(goalPos.x-currentPosition.x) + sqr(goalPos.y - currentPosition.y);
	for( startNode = originalPath->getLastNode(); startNode != originalPath->getFirstNode(); startNode = startNode->getPrevious() )	{
		ICoord2D cellCoord;
		worldToCell(startNode->getPosition(), &cellCoord);
		TCheckMovementInfo info;
		info.cell = cellCoord;
		info.layer = startNode->getLayer();
		info.centerInCell = centerInCell;
		info.radius = radius;
		info.considerTransient = blocked;
		info.acceptableSurfaces = locomotorSet.getValidSurfaces();
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		if (TheGlobalData->m_debugAI) {
			RGBColor color;
			color.setFromInt(0);
			color.green = 1;
			addIcon(startNode->getPosition(), PATHFIND_CELL_SIZE_F*0.5f, 100, color);
		}
#endif
		Int dx = cellCoord.x-startCellNdx.x;
		Int dy = cellCoord.y-startCellNdx.y;
		if (dx<-2 || dx>2) info.considerTransient = false;
		if (dy<-2 || dy>2) info.considerTransient = false;
		if (!checkForMovement(obj, info)) {
			break;
		}
		if (info.allyFixedCount || info.enemyFixed) {
			break;	// Don't patch through cells that are occupied.
		}
		Real curSqr = sqr(startNode->getPosition()->x-currentPosition.x) + sqr(startNode->getPosition()->y - currentPosition.y);
		if (curSqr < goalDeltaSqr) {
			goalPos = *startNode->getPosition();
			goalDeltaSqr = curSqr;
		}

	}
	if (startNode == originalPath->getLastNode()) {
#if RETAIL_COMPATIBLE_PATHFINDING
		if (!s_useFixedPathfinding) {
			cleanOpenAndClosedLists();
		}
		else
#endif
		{
			parentCell->releaseInfo();
		}
		return nullptr; // no open nodes.
	}
	PathfindCell *candidateGoal;
	candidateGoal = getCell(LAYER_GROUND, &goalPos); // just using for cost estimates.
	ICoord2D goalCellNdx;
	worldToCell(&goalPos, &goalCellNdx);
	if (!candidateGoal->allocateInfo(goalCellNdx)) {
#if RETAIL_COMPATIBLE_PATHFINDING
		if (s_useFixedPathfinding)
#endif
		{
			parentCell->releaseInfo();
		}
		return nullptr;
	}

	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());
		PathNode *matchNode;
		Bool found = false;
		for( matchNode = originalPath->getLastNode(); matchNode != startNode; matchNode = matchNode->getPrevious() )	{
			if (cellCenter.x == matchNode->getPosition()->x && cellCenter.y == matchNode->getPosition()->y)	{
				found = true;
				break;
			}
		}
		if (found ) {
			// success - found a path to the goal
			if ( TheGlobalData->m_debugAI)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *path = newInstance(Path);
			PathNode *node;
			for( node = originalPath->getLastNode(); node != matchNode; node = node->getPrevious() )	{
				path->prependNode(node->getPosition(), node->getLayer());
			}
			prependCells(path, obj->getPosition(), parentCell, centerInCell);

			// cleanup the path by checking line of sight
			path->optimize(obj, locomotorSet.getValidSurfaces(), blocked, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding) {
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
				candidateGoal->releaseInfo();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
				candidateGoal->releaseInfo();
			}

			return path;
		}
		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		if (cellCount < CELL_LIMIT) {
			// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell, true);
			cellCount += examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		}
	}

	DEBUG_LOG(("%d patchPath Pathfind failed --", TheGameLogic->getFrame()));
	DEBUG_LOG(("Unit '%s', time %f", obj->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	if (TheGlobalData->m_debugAI) {
		debugShowSearch(true);
	}
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		if (!candidateGoal->getOpen() && !candidateGoal->getClosed())
		{
			// Not on one of the lists 
			candidateGoal->releaseInfo();
		}
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		candidateGoal->releaseInfo();
	}
	return nullptr;
}


/** True if obj can stand at goalPos (on its current layer) and attack the victim from there.
	* Same checks as the "quick check" in findAttackPath. */
Bool Pathfinder::isValidAttackGoal( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D& goalPos,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon )
{
	if (!m_isMapReady || obj == nullptr || weapon == nullptr)
		return false;

	Bool isCrusher = obj->getCrusherLevel() > 0;
	Short requiredBridgeHeight = obj->getRequiredBridgeHeight();
	Int radius;
	Bool centerInCell;
	getRadiusAndCenter(obj, radius, centerInCell);

	ICoord2D cellNdx;
	worldToCell(&goalPos, &cellNdx);
	PathfindCell *aCell = getCell(obj->getLayer(), cellNdx.x, cellNdx.y);
	if (!aCell)
		return false;

	if (!validMovementPosition(isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, aCell))
		return false;

	if (!checkDestination(obj, cellNdx.x, cellNdx.y, obj->getLayer(), radius, centerInCell))
		return false;

	if (!weapon->isGoalPosWithinAttackRange(obj, &goalPos, victim, victimPos))
		return false;

	if (isAttackViewBlockedByObstacle(obj, goalPos, victim, *victimPos))
		return false;

	return true;
}


/** Find a short, valid path to a location that obj can attack victim from.  */
Path *Pathfinder::findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon )
{
	if (!m_isMapReady)
		return nullptr; // Should always be ok.

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	Short requiredBridgeHeight = obj ? obj->getRequiredBridgeHeight() : 0;
	Int radius;
	Bool centerInCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	beginSearchBudget(PFS_ATTACK);

	// Quick check:  See if moving couple of cells towards the victim will work.
	{
		Coord3D curPos = *obj->getPosition();
		Coord3D goalPos = victim?*victim->getPosition():*victimPos;
		Coord3D delta;
		delta.set(goalPos.x-curPos.x, goalPos.y-curPos.y, 0);
		delta.normalize();
		delta.x *= PATHFIND_CELL_SIZE_F;
		delta.y *= PATHFIND_CELL_SIZE_F;
		Int i;
		for (i=1; i<10; i++) {
			Coord3D testPos = curPos;
			testPos.x += delta.x*i*0.5f;
			testPos.y += delta.y*i*0.5f;

			ICoord2D cellNdx;
			worldToCell(&testPos, &cellNdx);
			PathfindCell *aCell = getCell(obj->getLayer(), cellNdx.x, cellNdx.y);
			if (!aCell) break;

			if (!validMovementPosition(isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, aCell))
				break;

			if (!checkDestination(obj, cellNdx.x, cellNdx.y, obj->getLayer(), radius, centerInCell))
				break;

			if (!weapon->isGoalPosWithinAttackRange(obj, &testPos, victim, victimPos))
				continue;

			if (isAttackViewBlockedByObstacle(obj, testPos, victim, *victimPos))
				continue;

			// return path.
			Path *path = newInstance(Path);
			path->prependNode( &testPos, obj->getLayer() );
			path->prependNode( &curPos, obj->getLayer() );
			path->getFirstNode()->setNextOptimized(path->getFirstNode()->getNext());
			if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) {
				setDebugPath(path);
			}
			return path;
		}
	}

	const Int ATTACK_CELL_LIMIT = 2500; // this is a rather expensive operation, so limit the search.

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	m_zoneManager.clearPassableFlags();
	Path *hPat = findClosestHierarchicalPath(isHuman, locomotorSet, from, victimPos, isCrusher, radius);
	if (hPat) {
		deleteInstance(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	Int cellCount = 0;

	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));

	Int attackDistance = weapon->getAttackDistance(obj, victim, victimPos);
	attackDistance += 3*PATHFIND_CELL_SIZE;

		// determine start cell
	ICoord2D startCellNdx;
	Coord3D objPos = *obj->getPosition();
	// since worldtocell truncates, add.
	if (centerInCell) {
		objPos.x += PATHFIND_CELL_SIZE_F/2.0f;
		objPos.y += PATHFIND_CELL_SIZE_F/2.0f;
	}

	if (!obj->getAIUpdateInterface()) // shouldn't happen, but can't move without an ai.
		return nullptr;

	worldToCell(&objPos, &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &objPos );
	if (!parentCell)
		return nullptr;

	if (!parentCell->allocateInfo(startCellNdx))
		return nullptr;

	const PathfindCell *startCell = parentCell;
	parentCell->startPathfind(nullptr);

	// determine start cell
	ICoord2D victimCellNdx;
	worldToCell(victim ? victim->getPosition() : victimPos, &victimCellNdx);

	// determine goal cell
	PathfindCell *goalCell = getCell( LAYER_GROUND, victimCellNdx.x, victimCellNdx.y );
	if (!goalCell)
		return nullptr;

 	if (!goalCell->allocateInfo(victimCellNdx)) {
		return nullptr;
	}

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

	PathfindCell *closestCell = nullptr;
	Real closestDistanceSqr = FLT_MAX;
	Bool checkLOS = false;
	if (!victim) {
		checkLOS = true;
	}
	if (victim && !victim->isSignificantlyAboveTerrain()) {
		checkLOS = true;
	}

	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());

		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		if (weapon->isGoalPosWithinAttackRange(obj, &cellCenter, victim, victimPos) &&
			checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(),
				parentCell->getLayer(), radius, centerInCell)) {
			// check line of sight.
			Bool viewBlocked = false;
			if (checkLOS)
			{
				viewBlocked = isAttackViewBlockedByObstacle(obj, cellCenter, victim, *victimPos);
			}
			if (startCell == parentCell) {
				// We never want to accept our starting cell.
				// If we could attack from there, we wouldn't be calling
				// FindAttackPath.  Usually happens cause the cell is valid for attack, but
				// a point near the cell center isn't, and that happens to be where the
				// attacker is standing, and it's too close to move to.
				viewBlocked = true;
			} else {
				// If through some unfortunate rounding, we end up moving near ourselves,
				// don't want it.
				Coord3D cellPos;
				adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellPos, parentCell->getLayer());
				Real dx = (cellPos.x - objPos.x);
				Real dy = (cellPos.y - objPos.y);
				if (sqr(dx) + sqr(dy) < sqr(PATHFIND_CELL_SIZE_F*0.5f)) {
					viewBlocked = true;
				}
			}
			if (!viewBlocked)
			{
				// success - found a path to the goal
				Bool show = TheGlobalData->m_debugAI;
	#ifdef INTENSE_DEBUG
				Int count = 0;
				PathfindCell *cur;
				for (cur = m_closedList.getHead(); cur; cur=cur->getNextOpen()) {
					count++;
				}
				if (count>1000) {
					show = true;
					DEBUG_LOG(("FAP cells %d obj %s %x", count, obj->getTemplate()->getName().str(), obj));
	#ifdef STATE_MACHINE_DEBUG
					if( obj->getAIUpdateInterface() )
					{
						DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
					}
	#endif
					TheScriptEngine->AppendDebugMessage("Big Attack path", false);
				}
	#endif
				if (show)
					debugShowSearch(true);

#if !(RTS_GENERALS && RETAIL_COMPATIBLE_PATHFINDING)
				// put parent cell onto closed list - its evaluation is finished
				parentCell->putOnClosedList( m_closedList );

				if (obj->isKindOf(KINDOF_VEHICLE)) {
					// Strip backwards.
					PathfindCell *lastBlocked = nullptr;
					PathfindCell *cur = parentCell;
					Bool useLargeRadius = false;
					Int cellLimit = 12; // Magic number, yes I know - jba.   It is about 4 * size of an average vehicle width (3 cells) [8/15/2003]
					while (cur) {
						cellLimit--;
						if (cellLimit<0) {
							break;
						}
						TCheckMovementInfo info;
						info.cell.x = cur->getXIndex();
						info.cell.y = cur->getYIndex();
						info.layer = cur->getLayer();
						if (useLargeRadius) {
							info.centerInCell = centerInCell;
							info.radius = radius;
						} else {
							info.centerInCell = true;
							info.radius = 0;
						}
						info.considerTransient = false;
						info.acceptableSurfaces = locomotorSet.getValidSurfaces();
						PathfindCell	*cell = getCell(info.layer,info.cell.x,info.cell.y);
						Bool unitIdle = false;
						if (cell) {
							ObjectID posUnit = cell->getPosUnit();
							Object *unit = TheGameLogic->findObjectByID(posUnit);
							if (unit && unit->getAI() && unit->getAI()->isIdle()) {
								unitIdle = true;
							}
						}
						Bool checkMovement = checkForMovement(obj, info);
						Bool blockedByEnemy = info.enemyFixed;
						Bool blockedByAllies = info.allyFixedCount || info.allyGoal;
						if (unitIdle) {
							// If the unit present is idle, it doesn't block allies. [8/18/2003]
							blockedByAllies = false;
						}


						if (!checkMovement || blockedByEnemy || blockedByAllies) {
							lastBlocked = cur;
							useLargeRadius = true;
						} else {
							useLargeRadius = false;
						}
						cur = cur->getParentCell();
					}
					if (lastBlocked) {
						parentCell = lastBlocked;
						if (lastBlocked->getParentCell()) {
							parentCell = lastBlocked->getParentCell();
						}
					}
				}
#endif
				// construct and return path
				Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
				if (!s_useFixedPathfinding) {
					if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
						goalCell->releaseInfo();
					}
					cleanOpenAndClosedLists();
				}
				else
#endif
				{
					cleanOpenAndClosedLists();
					parentCell->releaseInfo();
					goalCell->releaseInfo();
				}
				return path;
			}
		}
		if (checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), radius, centerInCell)) {
			if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), locomotorSet.getRequiredWaterLevel(), requiredBridgeHeight, parentCell )) {
				Real dx = IABS(victimCellNdx.x-parentCell->getXIndex());
				Real dy = IABS(victimCellNdx.y-parentCell->getYIndex());
				Real distSqr = dx*dx+dy*dy;
				if (distSqr < closestDistanceSqr) {
					closestCell = parentCell;
					closestDistanceSqr = distSqr;
				}
			}
		}

		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		if (cellCount < ATTACK_CELL_LIMIT) {
				// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell, true);
			cellCount += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell,
				radius, startCellNdx, obj, attackDistance);
		}

	}

#ifdef INTENSE_DEBUG
	DEBUG_LOG(("obj %s %x", obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
	if( obj->getAIUpdateInterface() )
	{
		DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
	}
#endif
	debugShowSearch(true);
	TheScriptEngine->AppendDebugMessage("Overflowed attack path", false);
#endif
#if 0
	if (closestCell) {
		// construct and return path
		Path *path = buildActualPath( obj, locomotorSet, obj->getPosition(), closestCell, centerInCell, false);
		cleanOpenAndClosedLists();
		return path;
	}
#if defined(RTS_DEBUG)
	DEBUG_LOG(("%d (%d cells) Attack Pathfind failed from (%f,%f) to (%f,%f) --", TheGameLogic->getFrame(), cellCount, from->x, from->y, victim->getPosition()->x, victim->getPosition()->y));
	DEBUG_LOG(("Unit '%s', attacking '%s' time %f", obj->getTemplate()->getName().str(),  victim->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));
#endif
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
			goalCell->releaseInfo();
		}
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
		goalCell->releaseInfo();
	}
	return nullptr;
}

/** Find a short, valid path to a location that is safe from the repulsors.  */
Path *Pathfinder::findSafePath( const Object *obj, const LocomotorSet& locomotorSet,
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius)
{
	//CRCDEBUG_LOG(("Pathfinder::findSafePath()"));
	if (m_isMapReady == false) return nullptr; // Should always be ok.
#if defined(RTS_DEBUG)
//	Int startTimeMS = ::GetTickCount();
#endif

	const Int MAX_CELLS = MAX_SAFE_PATH_CELL_COUNT; // this is a rather expensive operation, so limit the search.
	beginSearchBudget(PFS_SAFE);

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Real repulsorDistSqr = repulsorRadius*repulsorRadius;
	Int cellCount = 0;
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	DEBUG_ASSERTCRASH(m_openList.empty() && m_closedList.empty(), ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	m_zoneManager.setAllPassable();
	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(obj->getPosition(), &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), obj->getPosition() );
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr; // shouldn't happen, but can't move it without an ai.
	}
	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind( nullptr);

	// initialize "open" list to contain start cell
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		m_openList.reset(parentCell);
	}
	else
#endif
	{
		m_openList.reset();
		parentCell->putOnSortedOpenList(m_openList);
	}

	// "closed" list is initially empty
	m_closedList.reset();

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

	Real farthestDistanceSqr = 0;

	while( !m_openList.empty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		parentCell->removeFromOpenList(m_openList);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());

		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		Real dx = cellCenter.x-repulsorPos1->x;
		Real dy = cellCenter.y-repulsorPos1->y;
		Bool ok = false;
		Real distSqr = dx*dx+dy*dy;
		dx = cellCenter.x-repulsorPos2->x;
		dy = cellCenter.y-repulsorPos2->y;
		Real distSqr2 = dx*dx+dy*dy;
		if (distSqr2<distSqr) {
			distSqr = distSqr2;
		}
		if (distSqr>repulsorDistSqr) {
			ok = true;
		}
		if (m_openList.empty() && cellCount>0) {
			ok = true; // exhausted the search space, just take the last cell.
		}
		if (distSqr > farthestDistanceSqr) {
			farthestDistanceSqr = distSqr;
			if (cellCount > MAX_CELLS) {
#ifdef INTENSE_DEBUG
				DEBUG_LOG(("Took intermediate path, dist %f, goal dist %f", WWMath::Sqrt(farthestDistanceSqr), repulsorRadius));
#endif
				ok = true; // Already a big search, just take this one.
			}
		}
		if ( ok &&
			checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(),
				parentCell->getLayer(), radius, centerInCell)) {
			// success - found a path to the goal
			Bool show = TheGlobalData->m_debugAI;
#ifdef INTENSE_DEBUG
			Int count = 0;
			PathfindCell *cur;
			for (cur = m_closedList.getHead(); cur; cur=cur->getNextOpen()) {
				count++;
			}
			if (count>2000) {
				show = true;
				DEBUG_LOG(("cells %d obj %s %x", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big Safe path", false);
			}
#endif
			if (show)
				debugShowSearch(true);
#if defined(RTS_DEBUG)
			//DEBUG_LOG(("Attack path took %d cells, %f sec", cellCount, (::GetTickCount()-startTimeMS)/1000.0f));
#endif
			// construct and return path
			Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false, locomotorSet.getRequiredWaterLevel());
#if RETAIL_COMPATIBLE_PATHFINDING
			if (!s_useFixedPathfinding) {
				parentCell->releaseInfo();
				cleanOpenAndClosedLists();
			}
			else
#endif
			{
				cleanOpenAndClosedLists();
				parentCell->releaseInfo();
			}
			return path;
		}

		// put parent cell onto closed list - its evaluation is finished
		parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell, true);

		cellCount += examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);

	}

#ifdef INTENSE_DEBUG
	DEBUG_LOG(("obj %s %x count %d", obj->getTemplate()->getName().str(), obj, cellCount));
#ifdef STATE_MACHINE_DEBUG
	if( obj->getAIUpdateInterface() )
	{
		DEBUG_LOG(("State %s",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
	}
#endif
	TheScriptEngine->AppendDebugMessage("Overflowed Safe path", false);
#endif
#if 0
#if defined(RTS_DEBUG)
	DEBUG_LOG(("%d (%d cells) Attack Pathfind failed from (%f,%f) to (%f,%f) --", TheGameLogic->getFrame(), cellCount, from->x, from->y, victim->getPosition()->x, victim->getPosition()->y));
	DEBUG_LOG(("Unit '%s', attacking '%s' time %f", obj->getTemplate()->getName().str(),  victim->getTemplate()->getName().str(), (::GetTickCount()-startTimeMS)/1000.0f));
#endif
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
#if RETAIL_COMPATIBLE_PATHFINDING
	if (!s_useFixedPathfinding) {
		cleanOpenAndClosedLists();
	}
	else
#endif
	{
		cleanOpenAndClosedLists();
		parentCell->releaseInfo();
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
void Pathfinder::crc( Xfer *xfer )
{
	CRCDEBUG_LOG(("Pathfinder::crc() on frame %d", TheGameLogic->getFrame()));
	CRCDEBUG_LOG(("beginning CRC: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser( &m_extent, sizeof(IRegion2D) );
	CRCDEBUG_LOG(("m_extent: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferBool( &m_isMapReady );
	CRCDEBUG_LOG(("m_isMapReady: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferBool( &m_isTunneling );
	CRCDEBUG_LOG(("m_isTunneling: %8.8X", ((XferCRC *)xfer)->getCRC()));

	Int obsolete1 = 0;
	xfer->xferInt( &obsolete1 );

	xfer->xferUser(&m_ignoreObstacleID, sizeof(ObjectID));
	CRCDEBUG_LOG(("m_ignoreObstacleID: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser(m_queuedPathfindRequests, sizeof(ObjectID)*PATHFIND_QUEUE_LEN);
	CRCDEBUG_LOG(("m_queuedPathfindRequests: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_queuePRHead);
	CRCDEBUG_LOG(("m_queuePRHead: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_queuePRTail);
	CRCDEBUG_LOG(("m_queuePRTail: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferInt(&m_numWallPieces);
	CRCDEBUG_LOG(("m_numWallPieces: %8.8X", ((XferCRC *)xfer)->getCRC()));

#if RETAIL_COMPATIBLE_CRC
	// TheSuperHackers @fix The original code effectively accessed m_numWallPieces 128 times,
	// because it used &m_wallPieces[MAX_WALL_PIECES] which is out-of-bounds and points to m_numWallPieces.
	static_assert(sizeof(Int) == sizeof(ObjectID), "Type sizes must be equal for correct xfer");

	for (Int i = 0; i < MAX_WALL_PIECES; ++i)
	{
		xfer->xferInt(&m_numWallPieces);
	}
#else
	xfer->xferUser(m_wallPieces, sizeof(m_wallPieces));
#endif

	CRCDEBUG_LOG(("m_wallPieces: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferReal(&m_wallHeight);
	CRCDEBUG_LOG(("m_wallHeight: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_cumulativeCellsAllocated);
	CRCDEBUG_LOG(("m_cumulativeCellsAllocated: %8.8X", ((XferCRC *)xfer)->getCRC()));

	// TheSuperHackers @feature simonx645 16/11/2026 The pathfinding switches (GameLogic/PathTuning.h), only when one is on,
	// so a game without them keeps its CRC.
	if (PathTuning::Any()) {
		UnsignedInt mask = PathTuning::Mask();
		Int weight = PathTuning::CRC_WEIGHT, cap = PathTuning::SEARCH_CAP_CELLS;
		Int queueBase = PathTuning::CRC_QUEUE_BASE, queueMax = PathTuning::CRC_QUEUE_MAX;
		xfer->xferUnsignedInt(&mask);
		xfer->xferInt(&weight);
		xfer->xferInt(&cap);
		xfer->xferInt(&queueBase);
		xfer->xferInt(&queueMax);
		// the footprint knobs only with their own switch, so a game with search-core switches only keeps its CRC
		if (PathTuning::Clearance) {
			Int clearanceHard = PathTuning::CLEARANCE_HARD, clearanceCost = PathTuning::CLEARANCE_COST;
			xfer->xferInt(&clearanceHard);
			xfer->xferInt(&clearanceCost);
		}
		CRCDEBUG_LOG(("PathTuning: %8.8X", ((XferCRC *)xfer)->getCRC()));
	}

}

//-----------------------------------------------------------------------------
void Pathfinder::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

}

//-----------------------------------------------------------------------------
void Pathfinder::loadPostProcess()
{

}

// TheSuperHackers @feature simonx645 16/11/2026 "clearance" (PathTuning.h): the search keeps a unit's own route off the
// shore, but a group move's column waypoints are placed beside the group's route (1.5 cells a column) and were driven to
// as they were, so a column on the water side ran its hull over the water. Such a waypoint moves to the nearest cell,
// ring by ring up to 3, where the footprint hangs over nothing; ties go to the first in the scan (fixed order, so every
// player picks the same). The cell must be one the unit can stand on and in its zone, so a waypoint never jumps a river.
// A waypoint with no such cell stays.
Bool Pathfinder::clearWaypoint( const Object *obj, Coord3D *pos )
{
	const AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
	if (!PathTuning::Clearance || ai == nullptr || m_waterRings == nullptr || !ai->isDoingGroundMovement()) {
		return false;
	}
	if (TheTerrainLogic->getLayerForDestination(pos) != LAYER_GROUND) {
		return false;	// on a bridge deck: the water under it is not in the way
	}
	Int radius;
	Bool center;
	getRadiusAndCenter(obj, radius, center);
	if (radius <= 0) {
		return false;
	}
	const LocomotorSet &locomotorSet = ai->getLocomotorSet();
	const LocomotorSurfaceTypeMask surfaces = locomotorSet.getValidSurfaces();
	Coord3D cellPos = *pos;
	if (!center) {
		cellPos.x += PATHFIND_CELL_SIZE_F/2;
		cellPos.y += PATHFIND_CELL_SIZE_F/2;
	}
	ICoord2D cell;
	worldToCell(&cellPos, &cell);
	Int hard;
	if (ringShortage(cell.x, cell.y, surfaces, radius, &hard) <= 0) {
		return false;
	}
	// TheSuperHackers @feature simonx645 16/11/2026 "lanekeep" (GameLogic/PathTuning.h): only where the search would refuse
	// the step. At 1 ring over, the water-side lane moved onto the other lane and the two closed into one at an inlet
	// tip, halving the corner's throughput (T3b)
	if (PathTuning::LaneKeep && hard < PathTuning::CLEARANCE_HARD) {
		return false;
	}
	const Bool isCrusher = obj->getCrusherLevel() > 0;
	for (Int ring = 1; ring <= 3; ring++) {
		Int bestD = 0x7FFFFFFF;
		Coord3D best;
		for (Int i = -ring; i <= ring; i++) {
			for (Int j = -ring; j <= ring; j++) {
				if (IABS(i) != ring && IABS(j) != ring) {
					continue;
				}
				const Int d = i*i + j*j;
				if (d >= bestD) {
					continue;
				}
				PathfindCell *c = getCell(LAYER_GROUND, cell.x + i, cell.y + j);
				if (c == nullptr || !validMovementPosition(isCrusher, surfaces, locomotorSet.getRequiredWaterLevel(), obj->getRequiredBridgeHeight(), c)) {
					continue;
				}
				if (ringShortage(cell.x + i, cell.y + j, surfaces, radius, &hard) > 0) {
					continue;
				}
				Coord3D at;
				adjustCoordToCell(cell.x + i, cell.y + j, center, at, LAYER_GROUND);
				if (!clientSafeQuickDoesPathExist(locomotorSet, obj->getRequiredBridgeHeight(), obj->getPosition(), &at)) {
					continue;
				}
				bestD = d;
				best = at;
			}
		}
		if (bestD != 0x7FFFFFFF) {
			*pos = best;
			return true;
		}
	}
	return false;
}

// TheSuperHackers @feature simonx645 16/11/2026 "groupgoals" (PathTuning.h): one goal finder, Reforged's flood with
// contraZH's spacing. One breadth-first flood from the click; the members, ranked by when they would get there, take the
// cells as the flood reaches them, so the first to arrive stand deepest and later arrivals stop short of them instead of
// driving through the group (which is what made groups spiral at the end of a move).
// - Ground units whose surfaces are ground only; the rest keep the old goals.
// - Before the first goal, every cell floods, so a click on a building or a lake reaches the nearest open ring; after it,
//   only open ground floods, so the group stays on the clicked side of a cliff or river. Every goal is in the first
//   goal's zone.
// - Within a ring, cells further along the group's way come first. Each cell is offered to the 8 soonest unplaced members
//   it fits: their own footprint (checkDestination), a cell's gap from goals already given for a 4x4 or 5x5, and never
//   more than a cell past the click on the far side from the member's own approach.
// - The flood also carries the widest footprint (1 to 5 cells square) that fits all the way from the click to each cell,
//   and a member takes a cell only if its own footprint got there: a pocket a 5x5 cannot drive into through a 2-cell gap
//   between cliffs is no goal for it, though its own footprint would fit at the bottom of the pocket.
// Integer keys, total-order sorts (key, then ObjectID; ring order, then flood order). The travel direction is a float
// dot product without trig.
static Bool Group_Goal_Before(Int keyA, ObjectID idA, Int keyB, ObjectID idB)
{
	return keyA != keyB ? keyA < keyB : idA < idB;
}

// the footprint size of a radius / center pair: 1, 2 (2x2), 3, 4 or 5
static Int Group_Goal_Size(Int radius, Bool center)
{
	const Int size = center ? 2 * radius + 1 : 2 * radius;
	return size < 1 ? 1 : size;
}

Int Pathfinder::groupGoalFit( Int x, Int y )
{
	// box of size s at (x, y): x - s/2 .. x - s/2 + s - 1 (an even box takes the cell and the ones below it, as an even
	// footprint is laid out). Each box holds the one before it, so grow until a cell fails.
	Int fit = 0;
	for (Int s = 1; s <= 5; s++) {
		const Int lo = s / 2;
		Bool ok = TRUE;
		for (Int i = x - lo; i <= x - lo + s - 1 && ok; i++) {
			for (Int j = y - lo; j <= y - lo + s - 1 && ok; j++) {
				PathfindCell *c = getCell(LAYER_GROUND, i, j);
				if (c == nullptr || !validMovementPosition(false, LOCOMOTORSURFACE_GROUND, 0, 0, c)) {
					ok = FALSE;
				}
			}
		}
		if (!ok) {
			break;
		}
		fit = s;
	}
	return fit;
}

Int Pathfinder::assignGroupGoals( Object **members, Int count, const Coord3D *click, CommandSourceType cmdSource, Coord3D *goals, Bool *placed )
{
	for (Int m = 0; m < count; m++) {
		placed[m] = FALSE;
	}
	if (!m_isMapReady || m_map == nullptr || count <= 0 || count > 512) {
		return 0;
	}
	struct Member { Object *obj; Int index; Int key; Int radius; Bool center; Real awayX, awayY; Bool done; Int inside; };
	Member list[512];
	Int n = 0;
	Real minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f, sumX = 0.0f, sumY = 0.0f;
	for (Int m = 0; m < count; m++) {
		Object *obj = members[m];
		AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
		if (ai == nullptr || !ai->isDoingGroundMovement() || obj->isKindOf(KINDOF_IMMOBILE)) {
			continue;
		}
		const LocomotorSurfaceTypeMask surfaces = ai->getLocomotorSet().getValidSurfaces();
		if (!(surfaces & LOCOMOTORSURFACE_GROUND) || (surfaces & (LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR))) {
			continue;
		}
		const Coord3D *p = obj->getPosition();
		minX = min(minX, p->x); minY = min(minY, p->y); maxX = max(maxX, p->x); maxY = max(maxY, p->y);
		sumX += p->x; sumY += p->y;
		Member &e = list[n++];
		e.obj = obj;
		e.index = m;
		e.done = FALSE;
		getRadiusAndCenter(obj, e.radius, e.center);
		// arrival: straight-line cells to the click over cells a frame, in 1/1024ths
		const Real dx = click->x - p->x, dy = click->y - p->y;
		const Real dist = sqrtf(dx * dx + dy * dy);
		Real speed = 0.0f;
		if (ai->getCurLocomotor()) {
			speed = ai->getCurLocomotor()->getMaxSpeedForCondition(obj->getBodyModule()->getDamageState());
		}
		const Int distCells = REAL_TO_INT_FLOOR(dist / PATHFIND_CELL_SIZE_F * 1024.0f);
		Int speedCells = REAL_TO_INT_FLOOR(speed / PATHFIND_CELL_SIZE_F * 1024.0f);
		if (speedCells < 1) speedCells = 1;
		e.key = (Int)(((Int64)distCells * 1024) / speedCells);
		e.awayX = dist > 0.01f ? dx / dist : 0.0f;	// the member's approach
		e.awayY = dist > 0.01f ? dy / dist : 0.0f;
		// its place in the container it is in, -1 outside
		e.inside = -1;
		const Object *holder = obj->getContainedBy();
		const ContainModuleInterface *contain = holder ? holder->getContain() : nullptr;
		const ContainedItemsList *items = contain ? contain->getContainedItemsList() : nullptr;
		if (holder != nullptr) {
			Int place = 0;
			if (items != nullptr) {
				for (ContainedItemsList::const_iterator it = items->begin(); it != items->end() && *it != obj; ++it) {
					++place;	// not listed: after the listed ones
				}
			}
			e.inside = place;
		}
	}
	if (n == 0) {
		return 0;
	}
	// TheSuperHackers @bugfix simonx645 16/11/2026 A member still inside a container at the order comes out after every
	// member outside, whatever its distance: it ranks after them, in its container's order. It was ranked from the door as
	// if it were out and given a goal behind members that parked before it came out (T11b: a Scorpion stuck 28.7 s behind
	// the parked row).
	{
		Int maxOutside = 0;
		for (Int i = 0; i < n; i++) {
			if (list[i].inside < 0 && list[i].key > maxOutside) maxOutside = list[i].key;
		}
		for (Int i = 0; i < n; i++) {
			if (list[i].inside >= 0) list[i].key = maxOutside + 1 + list[i].inside;
		}
	}
	// a click inside the group keeps the old offsets (and the tighten move)
	if (n > 1 && click->x >= minX && click->x <= maxX && click->y >= minY && click->y <= maxY) {
		return 0;
	}
	// soonest first, ties by ObjectID
	for (Int i = 1; i < n; i++) {
		const Member e = list[i];
		Int j = i - 1;
		while (j >= 0 && Group_Goal_Before(e.key, e.obj->getID(), list[j].key, list[j].obj->getID())) {
			list[j + 1] = list[j];
			j--;
		}
		list[j + 1] = e;
	}
	for (Int i = 0; i < n; i++) {
		removeGoal(list[i].obj);	// the old goals are not in the way of the new ones
	}
	// the group's way, for the order within a ring
	Real wayX = click->x - sumX / n, wayY = click->y - sumY / n;
	const Real wayL = sqrtf(wayX * wayX + wayY * wayY);
	if (wayL > 0.01f) { wayX /= wayL; wayY /= wayL; } else { wayX = 0.0f; wayY = 0.0f; }

	ICoord2D start;
	worldToCell(click, &start);
	if (getCell(LAYER_GROUND, start.x, start.y) == nullptr) {
		return 0;
	}
	const Int width = m_extent.hi.x - m_extent.lo.x + 1;
	const Int height = m_extent.hi.y - m_extent.lo.y + 1;
	UnsignedByte *seen = MSGNEW("PathfindGroupGoals") UnsignedByte[width * height];	// 1 queued, 2 taken from the ring
	memset(seen, 0, width * height);
	// the widest footprint that got to a cell from the click (5: not limited yet, as on the way out of a building or lake
	// before the first goal); and a cell's own fit, worked out once (0: not yet, else fit + 1)
	UnsignedByte *wide = MSGNEW("PathfindGroupGoals") UnsignedByte[width * height];
	UnsignedByte *fitAt = MSGNEW("PathfindGroupGoals") UnsignedByte[width * height];
	memset(fitAt, 0, width * height);
	// the open cells the first flood took from a ring: the second flood passes back over them without using its budget
	UnsignedByte *firstPopped = MSGNEW("PathfindGroupGoals") UnsignedByte[width * height];
	memset(firstPopped, 0, width * height);
	// TheSuperHackers @bugfix simonx645 16/11/2026 The budget is the cells the goals take: per member its box with the gap
	// round it, twice over for the cells past the click that are not for it (at least the old 32). A flat 32 a member ran
	// out with 120 3x3s and 4x4s (T1b: 27 members had no goal).
	Int budget = 400;
	for (Int i = 0; i < n; i++) {
		const Int span = Group_Goal_Size(list[i].radius, list[i].center) + 2;
		budget += max(32, 2 * span * span);
	}
	Int *ring = MSGNEW("PathfindGroupGoals") Int[budget + 9];
	Int *next = MSGNEW("PathfindGroupGoals") Int[budget + 9];
	Int ringCount = 0, nextCount = 0, popped = 0, placedCount = 0;
	Int visited = 0;	// every cell taken from a ring; popped counts only the open cells nobody holds as a goal
	Int refZone = -1;
	struct Given { Int x, y, r; };
	Given *given = MSGNEW("PathfindGroupGoals") Given[n];
	Int givenCount = 0;
	const Int startCell = (start.x - m_extent.lo.x) * height + (start.y - m_extent.lo.y);
	// TheSuperHackers @bugfix simonx645 16/11/2026 Members the flood left without a goal get a second flood from the click,
	// without the "a cell past the click" rule, the goals given so far kept, before the old spiral.
	for (Int pass = 0; pass < 2 && placedCount < n; pass++) {
	const Bool alongRule = pass == 0;
	if (pass == 1) {
		memset(seen, 0, width * height);
		popped = 0;
		visited = 0;
	}
	ringCount = 0;
	ring[ringCount++] = startCell;
	seen[startCell] = 1;
	wide[startCell] = 5;
	while (ringCount > 0 && popped < budget && visited < 4 * budget && placedCount < n) {
		// this ring: further along the way first, then flood order
		for (Int i = 1; i < ringCount; i++) {
			const Int c = ring[i];
			const Real pc = ((c / height + m_extent.lo.x) - start.x) * wayX + ((c % height + m_extent.lo.y) - start.y) * wayY;
			Int j = i - 1;
			while (j >= 0) {
				const Int o = ring[j];
				const Real po = ((o / height + m_extent.lo.x) - start.x) * wayX + ((o % height + m_extent.lo.y) - start.y) * wayY;
				if (po >= pc) break;
				ring[j + 1] = ring[j];
				j--;
			}
			ring[j + 1] = c;
		}
		nextCount = 0;
		for (Int r = 0; r < ringCount && popped < budget && visited < 4 * budget && placedCount < n; r++) {
			const Int c = ring[r];
			seen[c] = 2;
			const Int x = c / height + m_extent.lo.x, y = c % height + m_extent.lo.y;
			PathfindCell *cell = getCell(LAYER_GROUND, x, y);
			// a cell another unit holds as its goal (a group that got there first stands on it) does not use up the
			// budget, so a second group sent to the same click floods past the first; every cell still counts toward a
			// hard cap of four budgets
			visited++;
			const PathfindCell::CellType type = cell->getType();
			ICoord2D here;
			here.x = x;
			here.y = y;
			const Bool open = (type == PathfindCell::CELL_CLEAR || type == PathfindCell::CELL_RUBBLE) &&
				!(cmdSource == CMD_FROM_PLAYER && checkCellOutsideExtents(here));
			// TheSuperHackers @bugfix simonx645 16/11/2026 only open cells use up the budget (a flood out of a lake or a
			// building no longer spends it before the first goal)
			if (open && cell->getGoalUnit() == INVALID_ID) {
				if (pass == 0) {
					popped++;
					firstPopped[c] = 1;
				} else if (!firstPopped[c]) {
					popped++;	// TheSuperHackers @bugfix simonx645 16/11/2026 the second flood spent its whole budget again on
								// the field the first had filled, and placed nothing new (T9: 20 of 100 left over)
				}
			}
			// the widest footprint that gets here: as wide as came in, and no wider than fits here.
			// TheSuperHackers @bugfix simonx645 16/11/2026 What a cell passes on narrows only once a goal is given, and
			// never at the click: the click's own fit (a prop or a cliff fringe within two cells) shut every 5x5 out of the
			// whole flood (T9: no goal for any member). The cell's own fit still limits who takes the cell itself.
			Int cellWide = 0;
			if (open) {
				if (fitAt[c] == 0) {
					fitAt[c] = (UnsignedByte)(groupGoalFit(x, y) + 1);
				}
				const Int fitHere = fitAt[c] - 1;
				// TheSuperHackers @bugfix simonx645 16/11/2026 nor within 2 cells of the click (the widest box's reach): a prop
				// beside the click narrowed every cell round it, and the width only narrows along the flood, so no cell
				// past them got a 5x5 back (T9: all 20 Overlords without a goal, the click's own fit 2)
				const Bool clickArea = IABS(x - start.x) <= 2 && IABS(y - start.y) <= 2;
				if (refZone >= 0 && !clickArea && wide[c] > fitHere) {
					wide[c] = (UnsignedByte)fitHere;
				}
				// the second flood, for what the first left, asks only that the footprint fit at the cell
				cellWide = alongRule ? min((Int)wide[c], fitHere) : fitHere;
			}
			const Bool zoneOk = refZone < 0 || m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, cell->getZone()) == refZone;
			if (open && zoneOk) {
				// offered to the 8 soonest unplaced members it fits.
				// TheSuperHackers @bugfix simonx645 16/11/2026 A member counts toward the 8 only if it passes the cheap tests
				// (along, apart, size): 8 soonest members that could take no cell starved every member behind them.
				Int offered = 0;
				for (Int i = 0; i < n && offered < 8; i++) {
					Member &e = list[i];
					if (e.done) continue;
					// never more than a cell past the click, seen from the member's own approach (first flood only)
					if (alongRule) {
						const Real along = (x - start.x) * e.awayX + (y - start.y) * e.awayY;
						if (along > 1.0f) continue;
					}
					Bool apart = TRUE;
					for (Int g = 0; g < givenCount && apart; g++) {
						const Int gap = (e.radius >= 2 || given[g].r >= 2) ? 1 : 0;
						const Int reach = e.radius + given[g].r + gap;
						if (IABS(x - given[g].x) <= reach && IABS(y - given[g].y) <= reach) apart = FALSE;
					}
					if (!apart) continue;
					if (cellWide < Group_Goal_Size(e.radius, e.center)) continue;	// its footprint cannot get in here
					offered++;
					if (!checkDestination(e.obj, x, y, LAYER_GROUND, e.radius, e.center)) continue;
					Coord3D goal;
					adjustCoordToCell(x, y, e.center, goal, LAYER_GROUND);
					goals[e.index] = goal;
					placed[e.index] = TRUE;
					e.done = TRUE;
					updateGoal(e.obj, &goal, LAYER_GROUND);
					given[givenCount].x = x; given[givenCount].y = y; given[givenCount].r = e.radius;
					givenCount++;
					placedCount++;
					if (refZone < 0) {
						refZone = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, cell->getZone());
					}
					break;
				}
			}
			// flood on: everything until the first goal, open ground after it
			if (!open && refZone >= 0) continue;
			for (Int dy = -1; dy <= 1; dy++) {
				for (Int dx = -1; dx <= 1; dx++) {
					if (dx == 0 && dy == 0) continue;
					const Int nx = x + dx, ny = y + dy;
					if (nx < m_extent.lo.x || ny < m_extent.lo.y || nx > m_extent.hi.x || ny > m_extent.hi.y) continue;
					const Int nc = (nx - m_extent.lo.x) * height + (ny - m_extent.lo.y);
					// what this cell passes on: its own width (a cell that is not open passes on what reached it, so the click on
					// a building or a lake starts unlimited, and a way through a cliff stays as narrow as the open cell before it)
					const UnsignedByte passOn = wide[c];
					if (seen[nc] == 1 && wide[nc] < passOn) {
						wide[nc] = passOn;	// queued from a narrower way in this ring: the wider one counts
					}
					if (seen[nc] || nextCount >= budget) continue;
					seen[nc] = 1;
					wide[nc] = passOn;
					next[nextCount++] = nc;
				}
			}
		}
		Int *swap = ring; ring = next; next = swap;
		ringCount = nextCount;
	}
	}	// the passes
	delete[] given;
	delete[] next;
	delete[] ring;
	delete[] firstPopped;
	delete[] fitAt;
	delete[] wide;
	delete[] seen;
	return placedCount;
}
