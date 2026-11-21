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

// FILE: ScriptCLPState.cpp ///////////////////////////////////////////////////////////////////////
// Runtime state for the CLP_AI script actions and conditions ported from CookieLandProjects/CLP_AI.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/MapData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/ScriptCLP.h"

ScriptCLPState *TheScriptCLPState = nullptr;

// Frames an all clear stays true after a team stops seeing enemies.
static const Int ALL_CLEAR_FRAMES = 10 * LOGICFRAMES_PER_SECOND;

// Frames kill and loss events stay readable.
static const UnsignedInt EVENT_FRAMES = 5 * 60 * LOGICFRAMES_PER_SECOND;

static const ScriptCLPState::EventList s_noEvents;

//-------------------------------------------------------------------------------------------------
/** Accepts objects whose template is in an ObjectTypes list. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPObjectTypes : public PartitionFilter
{
public:
	PartitionFilterCLPObjectTypes(const ObjectTypes *types) : m_types(types) { }
	virtual Bool allow(Object *other) override
	{
		return m_types->isInSet(other->getTemplate());
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPObjectTypes"; }
#endif
private:
	const ObjectTypes *m_types;
};

//-------------------------------------------------------------------------------------------------
/** Accepts objects equivalent to one template. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPThing : public PartitionFilter
{
public:
	PartitionFilterCLPThing(const ThingTemplate *thing) : m_thing(thing) { }
	virtual Bool allow(Object *other) override
	{
		return m_thing->isEquivalentTo(other->getTemplate());
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPThing"; }
#endif
private:
	const ThingTemplate *m_thing;
};

//-------------------------------------------------------------------------------------------------
Object *clpSelectObject(const Coord3D *pos, PartitionFilter **filters, const AsciiString &objectType, Int selectionMode)
{
	if (!pos)
	{
		return nullptr;
	}

	const ThingTemplate *thing = nullptr;
	const ObjectTypes *types = nullptr;
	if (objectType.isNotEmpty())
	{
		types = TheScriptEngine->getObjectTypes(objectType);
		if (!types)
		{
			thing = TheThingFactory->findTemplate(objectType, FALSE);
			if (!thing)
			{
				return nullptr;
			}
		}
	}

	enum { MAX_FILTERS = 16 };
	PartitionFilter *allFilters[MAX_FILTERS];
	Int count = 0;
	if (filters)
	{
		for (Int i = 0; filters[i] && count < MAX_FILTERS - 4; ++i)
		{
			allFilters[count++] = filters[i];
		}
	}

	PartitionFilterOnMap filterOnMap;
	PartitionFilterAlive filterAlive;
	PartitionFilterCLPThing filterThing(thing);
	PartitionFilterCLPObjectTypes filterTypes(types);
	allFilters[count++] = &filterOnMap;
	allFilters[count++] = &filterAlive;
	if (thing)
	{
		allFilters[count++] = &filterThing;
	}
	else if (types)
	{
		allFilters[count++] = &filterTypes;
	}
	allFilters[count] = nullptr;

	switch (selectionMode)
	{
		case Parameter::CLOSEST:
			return ThePartitionManager->getClosestObject(pos, HUGE_DIST, FROM_CENTER_2D, allFilters);

		case Parameter::FARTHEST:
		{
			ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(pos, HUGE_DIST, FROM_CENTER_2D, allFilters, ITER_SORTED_FAR_TO_NEAR);
			MemoryPoolObjectHolder hold(iter);
			return iter->first();
		}

		case Parameter::RANDOM:
		case Parameter::YOUNGEST:
		case Parameter::OLDEST:
		{
			ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(pos, HUGE_DIST, FROM_CENTER_2D, allFilters, ITER_SORTED_NEAR_TO_FAR);
			MemoryPoolObjectHolder hold(iter);
			std::vector<Object *> candidates;
			Object *best = nullptr;
			for (Object *obj = iter->first(); obj; obj = iter->next())
			{
				// Object IDs are handed out in creation order.
				if (selectionMode == Parameter::RANDOM)
				{
					candidates.push_back(obj);
				}
				else if (!best || (selectionMode == Parameter::YOUNGEST) == (obj->getID() > best->getID()))
				{
					best = obj;
				}
			}
			if (selectionMode == Parameter::RANDOM && !candidates.empty())
			{
				best = candidates[GameLogicRandomValue(0, (Int)candidates.size() - 1)];
			}
			return best;
		}

		default:
			DEBUG_CRASH(("clpSelectObject: invalid selection mode %d", selectionMode));
			return nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
Bool clpObjectMatchesType(const Object *obj, const AsciiString &objectType)
{
	if (!obj)
	{
		return FALSE;
	}
	const ObjectTypes *types = TheScriptEngine->getObjectTypes(objectType);
	if (types)
	{
		return types->isInSet(obj->getTemplate());
	}
	const ThingTemplate *thing = TheThingFactory->findTemplate(objectType, FALSE);
	return thing && thing->isEquivalentTo(obj->getTemplate());
}

//-------------------------------------------------------------------------------------------------
Bool clpIsSeenByPlayer(const Object *obj, const Player *player)
{
	if (!obj || !player)
	{
		return FALSE;
	}
	if (obj->getShroudedStatus(player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
	{
		return FALSE;
	}
	if (obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED)
		&& player->getRelationship(obj->getTeam()) != ALLIES)
	{
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool clpAIPlaysFair(const Player *player)
{
	return player && player->getPlayerType() == PLAYER_COMPUTER;
}

//-------------------------------------------------------------------------------------------------
Bool clpAIUsesTargetingRestrictions(const Player *player)
{
	return clpAIPlaysFair(player) &&
		(TheMapData->m_aiSpecialPowersRespectFog || TheMapData->m_aiSpecialPowersRespectStealth);
}

//-------------------------------------------------------------------------------------------------
Bool clpIsFairAITarget(const Object *obj, const Player *player)
{
	if (!clpAIUsesTargetingRestrictions(player))
	{
		return TRUE;
	}
	if (!obj)
	{
		return FALSE;
	}
	if (TheMapData->m_aiSpecialPowersRespectFog)
	{
		ObjectShroudStatus status = obj->getShroudedStatus(player->getPlayerIndex());
		if (status == OBJECTSHROUD_SHROUDED)
		{
			return FALSE;
		}

		// A fogged structure stays targetable through the ghost a human would see.
		if (status == OBJECTSHROUD_FOGGED && !obj->isKindOf(KINDOF_STRUCTURE))
		{
			return FALSE;
		}
	}
	if (TheMapData->m_aiSpecialPowersRespectStealth &&
		obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED)
		&& player->getRelationship(obj->getTeam()) != ALLIES)
	{
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
ScriptCLPState::ScriptCLPState()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
ScriptCLPState::~ScriptCLPState()
{
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::reset()
{
	m_kdRatios.clear();
	m_beingCaptured.clear();
	m_objectCrushes.clear();
	m_teamTracks.clear();
	m_teamLosses.clear();
	m_eventSeq = 0;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_playerCrushes[i] = -1;
		m_playerKills[i].clear();
		m_playerLosses[i].clear();
	}
}

//-------------------------------------------------------------------------------------------------
static void pruneEvents(ScriptCLPState::EventList &events, UnsignedInt oldestFrame)
{
	size_t keep = 0;
	while (keep < events.size() && events[keep].frame < oldestFrame)
	{
		++keep;
	}
	if (keep > 0)
	{
		events.erase(events.begin(), events.begin() + keep);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::update()
{
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt oldestFrame = now > EVENT_FRAMES ? now - EVENT_FRAMES : 0;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		pruneEvents(m_playerKills[i], oldestFrame);
		pruneEvents(m_playerLosses[i], oldestFrame);
	}
	for (TeamLossMap::iterator it = m_teamLosses.begin(); it != m_teamLosses.end(); )
	{
		pruneEvents(it->second, oldestFrame);
		if (it->second.empty())
		{
			m_teamLosses.erase(it++);
		}
		else
		{
			++it;
		}
	}

#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
	// Frame counts in scripts are 30 Hz frames, as authored in the map.
	if (!TheGameLogic->HasLegacyFrameAdvanced())
	{
		return;
	}
#endif

	for (TeamTrackMap::iterator it = m_teamTracks.begin(); it != m_teamTracks.end(); )
	{
		Team *team = TheTeamFactory->findTeamByID(it->first);
		if (!team)
		{
			m_teamTracks.erase(it++);
			continue;
		}
		updateTeamTrack(team, &it->second);
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
ScriptCLPState::KDRatio *ScriptCLPState::findKDRatio(const AsciiString &name, Bool create)
{
	KDRatioMap::iterator it = m_kdRatios.find(name);
	if (it != m_kdRatios.end())
	{
		return &it->second;
	}
	if (!create)
	{
		return nullptr;
	}
	KDRatio &ratio = m_kdRatios[name];
	ratio.kills = 0.0f;
	ratio.deaths = 0.0f;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		ratio.readKills[i] = 0;
		ratio.readLosses[i] = 0;
	}
	return &ratio;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::addEvent(EventList &events, const Object *obj)
{
	Event event;
	event.seq = ++m_eventSeq;
	event.thing = obj->getTemplate();
	event.pos = *obj->getPosition();
	event.frame = TheGameLogic->getFrame();
	events.push_back(event);
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::onObjectDied(const Object *victim)
{
	m_beingCaptured.erase(victim->getID());
	m_objectCrushes.erase(victim->getID());

	const Player *player = victim->getControllingPlayer();
	if (player)
	{
		addEvent(m_playerLosses[player->getPlayerIndex()], victim);
	}
	const Team *team = victim->getTeam();
	if (!team)
	{
		return;
	}
	addEvent(m_teamLosses[team->getID()], victim);

	TeamTrackMap::iterator it = m_teamTracks.find(team->getID());
	if (it != m_teamTracks.end() && victim->getBodyModule())
	{
		it->second.deadMaxHealth += victim->getBodyModule()->getMaxHealth();
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::onObjectKilled(const Player *killer, const Object *victim)
{
	if (killer)
	{
		addEvent(m_playerKills[killer->getPlayerIndex()], victim);
	}
}

//-------------------------------------------------------------------------------------------------
const ScriptCLPState::EventList &ScriptCLPState::getPlayerKills(Int playerIndex) const
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
	{
		return s_noEvents;
	}
	return m_playerKills[playerIndex];
}

//-------------------------------------------------------------------------------------------------
const ScriptCLPState::EventList &ScriptCLPState::getPlayerLosses(Int playerIndex) const
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
	{
		return s_noEvents;
	}
	return m_playerLosses[playerIndex];
}

//-------------------------------------------------------------------------------------------------
const ScriptCLPState::EventList &ScriptCLPState::getTeamLosses(TeamID teamID) const
{
	TeamLossMap::const_iterator it = m_teamLosses.find(teamID);
	if (it == m_teamLosses.end())
	{
		return s_noEvents;
	}
	return it->second;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt ScriptCLPState::readEvents(Condition *pCondition)
{
	// The custom frame marks that the condition has a read position in its custom data.
	UnsignedInt since = m_eventSeq;
	if (pCondition->getCustomFrame() != 0)
	{
		since = (UnsignedInt)pCondition->getCustomData();
	}
	pCondition->setCustomFrame(1);
	pCondition->setCustomData((Int)m_eventSeq);
	return since;
}

//-------------------------------------------------------------------------------------------------
Bool ScriptCLPState::isBeingCaptured(const Object *obj) const
{
	return obj && m_beingCaptured.find(obj->getID()) != m_beingCaptured.end();
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::setBeingCaptured(const Object *obj, Bool beingCaptured)
{
	if (!obj)
	{
		return;
	}
	if (beingCaptured)
	{
		m_beingCaptured[obj->getID()] = TRUE;
	}
	else
	{
		m_beingCaptured.erase(obj->getID());
	}
}

//-------------------------------------------------------------------------------------------------
Bool ScriptCLPState::getCrushesInfantry(const Object *obj) const
{
	ObjectFlagMap::const_iterator it = m_objectCrushes.find(obj->getID());
	if (it != m_objectCrushes.end())
	{
		return it->second;
	}
	const Player *player = obj->getControllingPlayer();
	if (player && m_playerCrushes[player->getPlayerIndex()] >= 0)
	{
		return m_playerCrushes[player->getPlayerIndex()] != 0;
	}
	return TheAI->getAiData()->m_aiCrushesInfantry;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::setPlayerCrushesInfantry(Int playerIndex, Bool crushes)
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT)
	{
		m_playerCrushes[playerIndex] = crushes ? 1 : 0;
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::setObjectCrushesInfantry(const Object *obj, Bool crushes)
{
	if (obj)
	{
		m_objectCrushes[obj->getID()] = crushes;
	}
}

//-------------------------------------------------------------------------------------------------
ScriptCLPState::TeamTrack *ScriptCLPState::trackTeam(Team *team)
{
	TeamTrackMap::iterator it = m_teamTracks.find(team->getID());
	if (it != m_teamTracks.end())
	{
		return &it->second;
	}
	TeamTrack &track = m_teamTracks[team->getID()];
	track.idleFrames = 0;
	track.allClearFrames = 0;
	track.sawEnemy = FALSE;
	track.deadMaxHealth = 0.0f;
	updateTeamTrack(team, &track);
	return &track;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::updateTeamTrack(Team *team, TeamTrack *track)
{
	if (team->isIdle())
	{
		track->idleFrames++;
	}
	else
	{
		track->idleFrames = 0;
	}

	if (track->allClearFrames > 0)
	{
		track->allClearFrames--;
	}

	Bool anyAlive = FALSE;
	Bool seesEnemy = FALSE;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *member = iter.cur();
		if (member->isEffectivelyDead())
		{
			continue;
		}
		anyAlive = TRUE;

		PartitionFilterRelationship filterEnemies(member, PartitionFilterRelationship::ALLOW_ENEMIES);
		PartitionFilterAlive filterAlive;
		PartitionFilterSameMapStatus filterMapStatus(member);
		PartitionFilter *filters[] = { &filterEnemies, &filterAlive, &filterMapStatus, nullptr };
		if (ThePartitionManager->getClosestObject(member, member->getVisionRange(), FROM_CENTER_2D, filters))
		{
			seesEnemy = TRUE;
			break;
		}
	}
	if (anyAlive && track->sawEnemy && !seesEnemy)
	{
		track->allClearFrames = ALL_CLEAR_FRAMES;
	}
	track->sawEnemy = seesEnemy;
}

//-------------------------------------------------------------------------------------------------
Int ScriptCLPState::getTeamIdleFrames(Team *team)
{
	return team ? trackTeam(team)->idleFrames : 0;
}

//-------------------------------------------------------------------------------------------------
Bool ScriptCLPState::isTeamAllClear(Team *team)
{
	return team && trackTeam(team)->allClearFrames > 0;
}

//-------------------------------------------------------------------------------------------------
Real ScriptCLPState::getTeamHealthPercent(Team *team)
{
	if (!team)
	{
		return 0.0f;
	}
	TeamTrack *track = trackTeam(team);
	Real health = 0.0f;
	Real maxHealth = track->deadMaxHealth;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		const Object *member = iter.cur();
		if (member->isEffectivelyDead() || !member->getBodyModule())
		{
			continue;
		}
		health += member->getBodyModule()->getHealth();
		maxHealth += member->getBodyModule()->getMaxHealth();
	}
	if (maxHealth <= 0.0f)
	{
		return 0.0f;
	}
	return 100.0f * health / maxHealth;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::crc( Xfer *xfer )
{
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void ScriptCLPState::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// kills to deaths ratios
	UnsignedInt count = (UnsignedInt)m_kdRatios.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_SAVE )
	{
		for( KDRatioMap::iterator it = m_kdRatios.begin(); it != m_kdRatios.end(); ++it )
		{
			AsciiString name = it->first;
			xfer->xferAsciiString( &name );
			xfer->xferReal( &it->second.kills );
			xfer->xferReal( &it->second.deaths );
		}
	}
	else
	{
		m_kdRatios.clear();
		for( UnsignedInt i = 0; i < count; ++i )
		{
			AsciiString name;
			KDRatio ratio;
			xfer->xferAsciiString( &name );
			xfer->xferReal( &ratio.kills );
			xfer->xferReal( &ratio.deaths );
			m_kdRatios[name] = ratio;
		}
	}

	// object flags
	ObjectFlagMap *flagMaps[] = { &m_beingCaptured, &m_objectCrushes };
	for( Int m = 0; m < ARRAY_SIZE(flagMaps); ++m )
	{
		ObjectFlagMap &flags = *flagMaps[m];
		count = (UnsignedInt)flags.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_SAVE )
		{
			for( ObjectFlagMap::iterator it = flags.begin(); it != flags.end(); ++it )
			{
				ObjectID id = it->first;
				xfer->xferObjectID( &id );
				xfer->xferBool( &it->second );
			}
		}
		else
		{
			flags.clear();
			for( UnsignedInt i = 0; i < count; ++i )
			{
				ObjectID id;
				Bool value;
				xfer->xferObjectID( &id );
				xfer->xferBool( &value );
				flags[id] = value;
			}
		}
	}

	// player crush overrides
	count = MAX_PLAYER_COUNT;
	xfer->xferUnsignedInt( &count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		Int value = i < MAX_PLAYER_COUNT ? m_playerCrushes[i] : -1;
		xfer->xferInt( &value );
		if( i < MAX_PLAYER_COUNT )
		{
			m_playerCrushes[i] = value;
		}
	}

	// team tracking
	count = (UnsignedInt)m_teamTracks.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_SAVE )
	{
		for( TeamTrackMap::iterator it = m_teamTracks.begin(); it != m_teamTracks.end(); ++it )
		{
			TeamID id = it->first;
			xfer->xferUnsignedInt( &id );
			xfer->xferInt( &it->second.idleFrames );
			xfer->xferInt( &it->second.allClearFrames );
			xfer->xferBool( &it->second.sawEnemy );
			xfer->xferReal( &it->second.deadMaxHealth );
		}
	}
	else
	{
		m_teamTracks.clear();
		for( UnsignedInt i = 0; i < count; ++i )
		{
			TeamID id;
			TeamTrack track;
			xfer->xferUnsignedInt( &id );
			xfer->xferInt( &track.idleFrames );
			xfer->xferInt( &track.allClearFrames );
			xfer->xferBool( &track.sawEnemy );
			xfer->xferReal( &track.deadMaxHealth );
			m_teamTracks[id] = track;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPState::loadPostProcess()
{
	// Drop flags of objects that no longer exist.
	ObjectFlagMap *flagMaps[] = { &m_beingCaptured, &m_objectCrushes };
	for( Int m = 0; m < ARRAY_SIZE(flagMaps); ++m )
	{
		ObjectFlagMap &flags = *flagMaps[m];
		for( ObjectFlagMap::iterator it = flags.begin(); it != flags.end(); )
		{
			if( TheGameLogic->findObjectByID( it->first ) )
			{
				++it;
			}
			else
			{
				flags.erase( it++ );
			}
		}
	}
}
