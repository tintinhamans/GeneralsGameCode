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

// FILE: ScriptActionsCLPUnits.cpp ////////////////////////////////////////////////////////////////
// CLP_AI script actions that move, attack, load, repair or teleport units and teams.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/ScriptCLP.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#include <vector>

namespace
{

//-------------------------------------------------------------------------------------------------
/** Accepts objects the player can see right now. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPSeen : public PartitionFilter
{
public:
	PartitionFilterCLPSeen(const Player *player) : m_player(player) { }
	virtual Bool allow(Object *other) override
	{
		return clpIsSeenByPlayer(other, m_player);
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPSeen"; }
#endif
private:
	const Player *m_player;
};

//-------------------------------------------------------------------------------------------------
/** Accepts objects that are fogged or shrouded for the player, or with underFog FALSE, clear ones. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPFog : public PartitionFilter
{
public:
	PartitionFilterCLPFog(const Player *player, Bool underFog) : m_playerIndex(player->getPlayerIndex()), m_underFog(underFog) { }
	virtual Bool allow(Object *other) override
	{
		Bool fogged = other->getShroudedStatus(m_playerIndex) != OBJECTSHROUD_CLEAR;
		return fogged == m_underFog;
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPFog"; }
#endif
private:
	Int m_playerIndex;
	Bool m_underFog;
};

//-------------------------------------------------------------------------------------------------
/** Accepts buildings with a capture in progress. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPBeingCaptured : public PartitionFilter
{
public:
	virtual Bool allow(Object *other) override
	{
		return TheScriptCLPState && TheScriptCLPState->isBeingCaptured(other);
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPBeingCaptured"; }
#endif
};

//-------------------------------------------------------------------------------------------------
/** Rejects one object and the members of one team; either may be null. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterCLPRejectSelf : public PartitionFilter
{
public:
	PartitionFilterCLPRejectSelf(const Team *team, const Object *obj) : m_team(team), m_obj(obj) { }
	virtual Bool allow(Object *other) override
	{
		return other != m_obj && (!m_team || other->getTeam() != m_team);
	}
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() override { return "PartitionFilterCLPRejectSelf"; }
#endif
private:
	const Team *m_team;
	const Object *m_obj;
};

enum CLPRelationMove
{
	CLP_MOVE_AWAY,
	CLP_MOVE_TOWARDS,
	CLP_KEEP_DISTANCE
};

//-------------------------------------------------------------------------------------------------
Object *clpFirstAIMember(Team *team)
{
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (obj && !obj->isEffectivelyDead() && obj->getAIUpdateInterface())
		{
			return obj;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
AIGroupPtr clpTeamGroup(Team *team)
{
	AIGroupPtr group = TheAI->createGroup();
	if (!group)
	{
		return group;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(group);
#else
	team->getTeamAsAIGroup(group.Peek());
#endif
	return group;
}

//-------------------------------------------------------------------------------------------------
/** Adds the positions of live members matching kindOf (KINDOF_INVALID for any) and objectType (empty for any). */
//-------------------------------------------------------------------------------------------------
void clpSumTeamPositions(Team *team, KindOfType kindOf, const AsciiString &objectType, Coord3D *sum, Int *count)
{
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		if (kindOf != KINDOF_INVALID && !obj->isKindOf(kindOf))
		{
			continue;
		}
		if (objectType.isNotEmpty() && !clpObjectMatchesType(obj, objectType))
		{
			continue;
		}
		sum->add(*obj->getPosition());
		++(*count);
	}
}

//-------------------------------------------------------------------------------------------------
Bool clpAveragePosition(const Coord3D &sum, Int count, Coord3D *center)
{
	if (count <= 0)
	{
		return FALSE;
	}
	center->x = sum.x / count;
	center->y = sum.y / count;
	center->z = sum.z / count;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool clpTeamCenter(Team *team, Coord3D *center)
{
	Coord3D sum;
	sum.zero();
	Int count = 0;
	clpSumTeamPositions(team, KINDOF_INVALID, AsciiString::TheEmptyString, &sum, &count);
	return clpAveragePosition(sum, count, center);
}

//-------------------------------------------------------------------------------------------------
void clpTeamMoveTo(Team *team, const Coord3D *pos)
{
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiMoveToPosition(pos, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
void clpTeamMoveToObject(Team *team, Object *target)
{
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiMoveToObject(target, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** Selects an object with the given relation to player, skipping excludeTeam and excludeObj. */
//-------------------------------------------------------------------------------------------------
Object *clpSelectByRelation(const Coord3D *pos, const Player *player, Int relationType, const Team *excludeTeam,
	const Object *excludeObj, const AsciiString &objectType, Int selectionMode)
{
	if (!player)
	{
		return nullptr;
	}

	UnsignedInt flags;
	switch (relationType)
	{
		case ENEMIES: flags = ALLOW_ENEMIES; break;
		case NEUTRAL: flags = ALLOW_NEUTRAL; break;
		case ALLIES: flags = ALLOW_ALLIES; break;
		default: return nullptr;
	}

	PartitionFilterPlayerAffiliation filterRelation(player, flags, TRUE);
	PartitionFilterCLPRejectSelf filterSelf(excludeTeam, excludeObj);
	PartitionFilterPlayer filterNotOwn(player, FALSE);
	PartitionFilter *filters[4];
	Int count = 0;
	filters[count++] = &filterRelation;
	filters[count++] = &filterSelf;

	// The affiliation filter accepts the player's own objects for every relation.
	if (relationType != ALLIES)
	{
		filters[count++] = &filterNotOwn;
	}
	filters[count] = nullptr;

	return clpSelectObject(pos, filters, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
/** Destination of a relation move from pos with respect to threat; FALSE if there is none. */
//-------------------------------------------------------------------------------------------------
Bool clpRelationDestination(CLPRelationMove move, const Coord3D &pos, const Coord3D &threat, Real feet, Coord3D *dest)
{
	Coord3D dir;
	dir.x = pos.x - threat.x;
	dir.y = pos.y - threat.y;
	dir.z = 0.0f;
	Real len = (Real)sqrt(dir.x * dir.x + dir.y * dir.y);
	if (len < 0.01f)
	{
		if (move == CLP_MOVE_TOWARDS)
		{
			return FALSE;
		}

		// Stacked on the threat, so any fixed direction will do.
		dir.x = 1.0f;
		dir.y = 0.0f;
	}
	else
	{
		dir.x /= len;
		dir.y /= len;
	}

	switch (move)
	{
		case CLP_MOVE_AWAY:
			dest->set(pos.x + dir.x * feet, pos.y + dir.y * feet, pos.z);
			break;
		case CLP_MOVE_TOWARDS:
			dest->set(pos.x - dir.x * feet, pos.y - dir.y * feet, pos.z);
			break;
		case CLP_KEEP_DISTANCE:
			dest->set(threat.x + dir.x * feet, threat.y + dir.y * feet, threat.z);
			break;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void clpTeamRelationMove(CLPRelationMove move, const AsciiString &teamName, Real feet, Int relationType,
	const AsciiString &objectType, Int selectionMode)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	Coord3D teamPos = *teamObj->getPosition();
	Object *bestObj = clpSelectByRelation(&teamPos, teamObj->getControllingPlayer(), relationType, team, nullptr, objectType, selectionMode);
	if (!bestObj)
	{
		return;
	}

	Coord3D dest;
	if (clpRelationDestination(move, teamPos, *bestObj->getPosition(), feet, &dest))
	{
		clpTeamMoveTo(team, &dest);
	}
}

//-------------------------------------------------------------------------------------------------
void clpUnitRelationMove(CLPRelationMove move, const AsciiString &unitName, Real feet, Int relationType,
	const AsciiString &objectType, Int selectionMode)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj || obj->isEffectivelyDead())
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}

	Coord3D objPos = *obj->getPosition();
	Object *bestObj = clpSelectByRelation(&objPos, obj->getControllingPlayer(), relationType, nullptr, obj, objectType, selectionMode);
	if (!bestObj)
	{
		return;
	}

	Coord3D dest;
	if (clpRelationDestination(move, objPos, *bestObj->getPosition(), feet, &dest))
	{
		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiMoveToPosition(&dest, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** Attacks or attack-moves the team at an enemy, optionally inside an area and seen by the team's player. */
//-------------------------------------------------------------------------------------------------
void clpTeamAttack(const AsciiString &teamName, const AsciiString &objectType, const AsciiString *areaName,
	Bool seenOnly, Bool attackMove, Int selectionMode)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	PolygonTrigger *area = nullptr;
	if (areaName)
	{
		area = TheScriptEngine->getQualifiedTriggerAreaByName(*areaName);
		if (!area)
		{
			return;
		}
	}

	PartitionFilterPlayerAffiliation filterEnemies(player, ALLOW_ENEMIES, TRUE);
	PartitionFilterPlayer filterNotOwn(player, FALSE);
	PartitionFilterPolygonTrigger filterArea(area);
	PartitionFilterCLPSeen filterSeen(player);
	PartitionFilter *filters[5];
	Int count = 0;
	filters[count++] = &filterEnemies;
	filters[count++] = &filterNotOwn;
	if (area)
	{
		filters[count++] = &filterArea;
	}
	if (seenOnly)
	{
		filters[count++] = &filterSeen;
	}
	filters[count] = nullptr;

	Coord3D teamPos = *teamObj->getPosition();
	Object *bestObj = clpSelectObject(&teamPos, filters, objectType, selectionMode);
	if (!bestObj)
	{
		return;
	}

	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	if (attackMove)
	{
		theGroup->groupAttackMoveToPosition(bestObj->getPosition(), NO_MAX_SHOTS_LIMIT, false, CMD_FROM_SCRIPT);
	}
	else
	{
		theGroup->groupAttackObject(WEAPON_NONE, bestObj, NO_MAX_SHOTS_LIMIT, false, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
void clpTeamMoveFog(const AsciiString &teamName, const AsciiString &objectType, const AsciiString *areaName,
	Bool underFog, Int selectionMode)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	PolygonTrigger *area = nullptr;
	if (areaName)
	{
		area = TheScriptEngine->getQualifiedTriggerAreaByName(*areaName);
		if (!area)
		{
			return;
		}
	}

	PartitionFilterCLPFog filterFog(player, underFog);
	PartitionFilterPolygonTrigger filterArea(area);
	PartitionFilter *filters[3];
	Int count = 0;
	filters[count++] = &filterFog;
	if (area)
	{
		filters[count++] = &filterArea;
	}
	filters[count] = nullptr;

	Coord3D teamPos = *teamObj->getPosition();
	Object *bestObj = clpSelectObject(&teamPos, filters, objectType, selectionMode);
	if (bestObj)
	{
		clpTeamMoveToObject(team, bestObj);
	}
}

//-------------------------------------------------------------------------------------------------
/** Moves the team to the closest own or allied building being captured. */
//-------------------------------------------------------------------------------------------------
void clpTeamMoveCaptured(const AsciiString &teamName, const AsciiString &objectType)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	// The affiliation filter also accepts the player's own objects.
	PartitionFilterPlayerAffiliation filterAllies(player, ALLOW_ALLIES, TRUE);
	PartitionFilterCLPBeingCaptured filterCaptured;
	PartitionFilter *filters[] = { &filterAllies, &filterCaptured, nullptr };

	Coord3D teamPos = *teamObj->getPosition();
	Object *bestObj = clpSelectObject(&teamPos, filters, objectType, Parameter::CLOSEST);
	if (!bestObj)
	{
		return;
	}

	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(bestObj->getPosition(), false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void clpTeleport(Object *obj, const Coord3D *pos)
{
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai)
	{
		ai->clearWaypointQueue();
		ai->aiIdle(CMD_FROM_SCRIPT);
	}
	Coord3D dest = *pos;
	dest.z = TheTerrainLogic->getGroundHeight(dest.x, dest.y);
	obj->setPosition(&dest);
}

//-------------------------------------------------------------------------------------------------
Bool clpCanTeleport(const Object *obj)
{
	return obj && !obj->isEffectivelyDead() && !obj->getContainedBy() && !obj->isKindOf(KINDOF_STRUCTURE);
}

//-------------------------------------------------------------------------------------------------
Object *clpClosestOwned(const Coord3D *pos, const Player *player, KindOfType kindOf)
{
	PartitionFilterPlayer filterPlayer(player, TRUE);
	PartitionFilterAcceptByKindOf filterKind(MAKE_KINDOF_MASK(kindOf), KINDOFMASK_NONE);
	PartitionFilter *filters[] = { &filterPlayer, &filterKind, nullptr };
	return clpSelectObject(pos, filters, AsciiString::TheEmptyString, Parameter::CLOSEST);
}

//-------------------------------------------------------------------------------------------------
/** Docks obj at its player's closest airfield, repair pad or heal pad, by its kind. */
//-------------------------------------------------------------------------------------------------
void clpDockForRepair(Object *obj, Object *airfield, Object *repairPad, Object *healPad)
{
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}

	// Aircraft are also vehicles, so test them first.
	if (obj->isKindOf(KINDOF_AIRCRAFT) && airfield)
	{
		ai->aiDock(airfield, CMD_FROM_SCRIPT);
	}
	else if (obj->isKindOf(KINDOF_VEHICLE) && repairPad)
	{
		ai->aiDock(repairPad, CMD_FROM_SCRIPT);
	}
	else if (obj->isKindOf(KINDOF_INFANTRY) && healPad)
	{
		ai->aiDock(healPad, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
void clpTeamSendToRepair(const AsciiString &teamName, const AsciiString &objectType, Bool sendBackFullHP)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	const Coord3D *teamPos = teamObj->getPosition();
	Object *airfield = clpClosestOwned(teamPos, player, KINDOF_FS_AIRFIELD);
	Object *repairPad = clpClosestOwned(teamPos, player, KINDOF_REPAIR_PAD);
	Object *healPad = clpClosestOwned(teamPos, player, KINDOF_HEAL_PAD);

	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		if (objectType.isNotEmpty() && !clpObjectMatchesType(obj, objectType))
		{
			continue;
		}
		BodyModuleInterface *body = obj->getBodyModule();
		if (!body)
		{
			continue;
		}
		Bool fullHP = body->getMaxHealth() - body->getHealth() < 0.1f;
		if (fullHP && !sendBackFullHP)
		{
			continue;
		}
		clpDockForRepair(obj, airfield, repairPad, healPad);
	}
}

//-------------------------------------------------------------------------------------------------
/** Spreads the team's infantry over its transports round robin, within each transport's free slots. */
//-------------------------------------------------------------------------------------------------
void clpLoadTransportsEvenly(const AsciiString &teamName, const AsciiString &transportType)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}

	std::vector<Object *> transports;
	std::vector<Int> freeSlots;
	std::vector<Object *> units;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		if (obj->isKindOf(KINDOF_TRANSPORT))
		{
			if (transportType.isNotEmpty() && !clpObjectMatchesType(obj, transportType))
			{
				continue;
			}
			ContainModuleInterface *contain = obj->getContain();
			if (!contain)
			{
				continue;
			}
			Int free = contain->getContainMax() - (Int)contain->getContainCount() - contain->getExtraSlotsInUse();
			if (free > 0)
			{
				transports.push_back(obj);
				freeSlots.push_back(free);
			}
		}
		else if (obj->isKindOf(KINDOF_INFANTRY) && !obj->getContainedBy() && obj->getAIUpdateInterface())
		{
			units.push_back(obj);
		}
	}

	if (transports.empty())
	{
		return;
	}

	size_t next = 0;
	for (size_t i = 0; i < units.size(); ++i)
	{
		Object *unit = units[i];
		Int slots = unit->getTransportSlotCount();
		if (slots <= 0)
		{
			continue;
		}
		for (size_t attempt = 0; attempt < transports.size(); ++attempt)
		{
			size_t t = (next + attempt) % transports.size();
			if (freeSlots[t] < slots || !transports[t]->getContain()->isValidContainerFor(unit, FALSE))
			{
				continue;
			}
			freeSlots[t] -= slots;
			unit->getAIUpdateInterface()->aiEnter(transports[t], CMD_FROM_SCRIPT);
			next = (t + 1) % transports.size();
			break;
		}
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveRelative(const AsciiString& teamName, Coord3D* coords)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team || !coords)
	{
		return;
	}

	Coord3D center;
	if (!clpTeamCenter(team, &center))
	{
		return;
	}
	Coord3D dest;
	dest.set(center.x + coords->x, center.y + coords->y, center.z);

	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(&dest, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveRelative(const AsciiString& unitName, Coord3D* coords)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj || !coords || obj->isEffectivelyDead())
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}

	const Coord3D *pos = obj->getPosition();
	Coord3D dest;
	dest.set(pos->x + coords->x, pos->y + coords->y, pos->z);
	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiMoveToPosition(&dest, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveNearestBelongingToPlayer(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& playerName, Int selectionMode)
{
	Player *targetPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!targetPlayer)
	{
		return;
	}
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Object *teamObj = clpFirstAIMember(team);
	if (!teamObj)
	{
		return;
	}

	PartitionFilterPlayer filterPlayer(targetPlayer, TRUE);
	PartitionFilter *filters[] = { &filterPlayer, nullptr };
	Coord3D teamPos = *teamObj->getPosition();
	Object *bestObj = clpSelectObject(&teamPos, filters, objectType, selectionMode);
	if (bestObj)
	{
		clpTeamMoveToObject(team, bestObj);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveNearestBelongingToPlayer(const AsciiString& unitName, const AsciiString& objectType, const AsciiString& playerName, Int selectionMode)
{
	Player *targetPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!targetPlayer)
	{
		return;
	}
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj || obj->isEffectivelyDead())
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}

	PartitionFilterPlayer filterPlayer(targetPlayer, TRUE);
	PartitionFilterCLPRejectSelf filterSelf(nullptr, obj);
	PartitionFilter *filters[] = { &filterPlayer, &filterSelf, nullptr };
	Object *bestObj = clpSelectObject(obj->getPosition(), filters, objectType, selectionMode);
	if (!bestObj)
	{
		return;
	}
	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiMoveToObject(bestObj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveAwayFromRelation(const AsciiString& teamName, Real feet, Int relationType, Int selectionMode)
{
	clpTeamRelationMove(CLP_MOVE_AWAY, teamName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveAwayFromRelationType(const AsciiString& teamName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpTeamRelationMove(CLP_MOVE_AWAY, teamName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveAwayFromRelation(const AsciiString& unitName, Real feet, Int relationType, Int selectionMode)
{
	clpUnitRelationMove(CLP_MOVE_AWAY, unitName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveAwayFromRelationType(const AsciiString& unitName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpUnitRelationMove(CLP_MOVE_AWAY, unitName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveTowardsRelation(const AsciiString& teamName, Real feet, Int relationType, Int selectionMode)
{
	clpTeamRelationMove(CLP_MOVE_TOWARDS, teamName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveTowardsRelationType(const AsciiString& teamName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpTeamRelationMove(CLP_MOVE_TOWARDS, teamName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveTowardsRelation(const AsciiString& unitName, Real feet, Int relationType, Int selectionMode)
{
	clpUnitRelationMove(CLP_MOVE_TOWARDS, unitName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitMoveTowardsRelationType(const AsciiString& unitName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpUnitRelationMove(CLP_MOVE_TOWARDS, unitName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamKeepDistanceFromRelation(const AsciiString& teamName, Real feet, Int relationType, Int selectionMode)
{
	clpTeamRelationMove(CLP_KEEP_DISTANCE, teamName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamKeepDistanceFromRelationType(const AsciiString& teamName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpTeamRelationMove(CLP_KEEP_DISTANCE, teamName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitKeepDistanceFromRelation(const AsciiString& unitName, Real feet, Int relationType, Int selectionMode)
{
	clpUnitRelationMove(CLP_KEEP_DISTANCE, unitName, feet, relationType, AsciiString::TheEmptyString, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitKeepDistanceFromRelationType(const AsciiString& unitName, Real feet, Int relationType, const AsciiString& objectType, Int selectionMode)
{
	clpUnitRelationMove(CLP_KEEP_DISTANCE, unitName, feet, relationType, objectType, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMeet(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}

	Coord3D center;
	if (!clpTeamCenter(team, &center))
	{
		return;
	}
	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMeetKindOf(const AsciiString& teamName, Int kindOf)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team || kindOf < KINDOF_FIRST || kindOf >= KINDOF_COUNT)
	{
		return;
	}

	Coord3D sum;
	sum.zero();
	Int count = 0;
	clpSumTeamPositions(team, (KindOfType)kindOf, AsciiString::TheEmptyString, &sum, &count);
	Coord3D center;
	if (!clpAveragePosition(sum, count, &center))
	{
		return;
	}
	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMeetType(const AsciiString& teamName, const AsciiString& objectType)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team || objectType.isEmpty())
	{
		return;
	}

	Coord3D sum;
	sum.zero();
	Int count = 0;
	clpSumTeamPositions(team, KINDOF_INVALID, objectType, &sum, &count);
	Coord3D center;
	if (!clpAveragePosition(sum, count, &center))
	{
		return;
	}
	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMeetTeam(const AsciiString& teamNameA, const AsciiString& teamNameB)
{
	Team *teamA = TheScriptEngine->getTeamNamed(teamNameA);
	Team *teamB = TheScriptEngine->getTeamNamed(teamNameB);
	if (!teamA || !teamB)
	{
		return;
	}

	Coord3D sum;
	sum.zero();
	Int count = 0;
	clpSumTeamPositions(teamA, KINDOF_INVALID, AsciiString::TheEmptyString, &sum, &count);
	if (teamB != teamA)
	{
		clpSumTeamPositions(teamB, KINDOF_INVALID, AsciiString::TheEmptyString, &sum, &count);
	}
	Coord3D center;
	if (!clpAveragePosition(sum, count, &center))
	{
		return;
	}

	AIGroupPtr theGroupA = clpTeamGroup(teamA);
	if (theGroupA)
	{
		theGroupA->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
	}
	if (teamB == teamA)
	{
		return;
	}
	AIGroupPtr theGroupB = clpTeamGroup(teamB);
	if (theGroupB)
	{
		theGroupB->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveToTeam(const AsciiString& teamNameA, const AsciiString& teamNameB)
{
	Team *teamA = TheScriptEngine->getTeamNamed(teamNameA);
	Team *teamB = TheScriptEngine->getTeamNamed(teamNameB);
	if (!teamA || !teamB)
	{
		return;
	}

	Coord3D center;
	if (!clpTeamCenter(teamB, &center))
	{
		return;
	}
	AIGroupPtr theGroup = clpTeamGroup(teamA);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupMoveToPosition(&center, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackType(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, nullptr, FALSE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackTypeArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerParm, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, &pTriggerParm, FALSE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackSeenUnit(const AsciiString& teamName, Int selectionMode)
{
	clpTeamAttack(teamName, AsciiString::TheEmptyString, nullptr, TRUE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackSeenType(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, nullptr, TRUE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackSeenArea(const AsciiString& teamName, const AsciiString& pTriggerParm, Int selectionMode)
{
	clpTeamAttack(teamName, AsciiString::TheEmptyString, &pTriggerParm, TRUE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackSeenTypeArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerParm, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, &pTriggerParm, TRUE, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveLocation(const AsciiString& teamName, const AsciiString& waypointName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way)
	{
		return;
	}

	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupAttackMoveToPosition(way->getLocation(), NO_MAX_SHOTS_LIMIT, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveArea(const AsciiString& teamName, const AsciiString& pTriggerArea, Int selectionMode)
{
	clpTeamAttack(teamName, AsciiString::TheEmptyString, &pTriggerArea, FALSE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveType(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, nullptr, FALSE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveTypeArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerArea, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, &pTriggerArea, FALSE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveSeenUnit(const AsciiString& teamName, Int selectionMode)
{
	clpTeamAttack(teamName, AsciiString::TheEmptyString, nullptr, TRUE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveSeenType(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, nullptr, TRUE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveSeenArea(const AsciiString& teamName, const AsciiString& pTriggerParm, Int selectionMode)
{
	clpTeamAttack(teamName, AsciiString::TheEmptyString, &pTriggerParm, TRUE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMoveSeenTypeArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerParm, Int selectionMode)
{
	clpTeamAttack(teamName, objectType, &pTriggerParm, TRUE, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackMovePath(const AsciiString& teamName, const AsciiString& waypointPath)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}

	Coord3D center;
	if (!clpTeamCenter(team, &center))
	{
		return;
	}
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath(&center, waypointPath);
	if (!way)
	{
		return;
	}

	AIGroupPtr theGroup = clpTeamGroup(team);
	if (!theGroup)
	{
		return;
	}
	theGroup->groupAttackMoveToPosition(way->getLocation(), NO_MAX_SHOTS_LIMIT, false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveCaptured(const AsciiString& teamName)
{
	clpTeamMoveCaptured(teamName, AsciiString::TheEmptyString);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveCapturedType(const AsciiString& teamName, const AsciiString objectType)
{
	if (objectType.isEmpty())
	{
		return;
	}
	clpTeamMoveCaptured(teamName, objectType);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveNearestUnderFog(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamMoveFog(teamName, objectType, nullptr, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveNearestUnderFogInArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerArea, Int selectionMode)
{
	clpTeamMoveFog(teamName, objectType, &pTriggerArea, TRUE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveNearestNotUnderFog(const AsciiString& teamName, const AsciiString& objectType, Int selectionMode)
{
	clpTeamMoveFog(teamName, objectType, nullptr, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveNearestNotUnderFogInArea(const AsciiString& teamName, const AsciiString& objectType, const AsciiString& pTriggerArea, Int selectionMode)
{
	clpTeamMoveFog(teamName, objectType, &pTriggerArea, FALSE, selectionMode);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitTeleportLocation(const AsciiString& unitName, const AsciiString& waypointName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!clpCanTeleport(obj))
	{
		return;
	}
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way)
	{
		return;
	}
	clpTeleport(obj, way->getLocation());
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamTeleportLocation(const AsciiString& teamName, const AsciiString& waypointName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way)
	{
		return;
	}

	std::vector<Object *> objs;
	Real spacing = 0.0f;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!clpCanTeleport(obj))
		{
			continue;
		}
		objs.push_back(obj);
		Real size = 2.5f * obj->getGeometryInfo().getMajorRadius();
		if (size > spacing)
		{
			spacing = size;
		}
	}
	if (objs.empty())
	{
		return;
	}

	// Lay the members out on a square grid centred on the waypoint.
	Int side = 1;
	while (side * side < (Int)objs.size())
	{
		++side;
	}
	const Coord3D *center = way->getLocation();
	Real half = (side - 1) * 0.5f;
	for (size_t i = 0; i < objs.size(); ++i)
	{
		Coord3D pos;
		pos.set(center->x + ((Int)i % side - half) * spacing, center->y + ((Int)i / side - half) * spacing, center->z);
		clpTeleport(objs[i], &pos);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityOnType(const AsciiString& teamName, const AsciiString& ability, const AsciiString& objectType)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	const CommandButton *commandButton = TheControlBar->findCommandButton(ability);
	if (!commandButton)
	{
		return;
	}

	std::vector<Object *> users;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_INERT))
		{
			continue;
		}
		if (clpObjectMatchesType(obj, objectType))
		{
			users.push_back(obj);
		}
	}
	if (users.empty())
	{
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup)
	{
		return;
	}
	for (size_t i = 0; i < users.size(); ++i)
	{
		theGroup->add(users[i]);
	}
	theGroup->groupDoCommandButton(commandButton, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityOnTeam(const AsciiString& teamName, const AsciiString& ability, const AsciiString& targetTeam)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	Team *targets = TheScriptEngine->getTeamNamed(targetTeam);
	if (!team || !targets)
	{
		return;
	}
	const CommandButton *commandButton = TheControlBar->findCommandButton(ability);
	if (!commandButton)
	{
		return;
	}

	std::vector<Object *> users;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (obj && !obj->isEffectivelyDead())
		{
			users.push_back(obj);
		}
	}

	for (size_t i = 0; i < users.size(); ++i)
	{
		Object *user = users[i];
		const Coord3D *userPos = user->getPosition();
		Object *bestTarget = nullptr;
		Real bestDistSqr = 0.0f;
		for (DLINK_ITERATOR<Object> iter = targets->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *target = iter.cur();
			if (!target || target->isEffectivelyDead())
			{
				continue;
			}
			if (!commandButton->isValidToUseOn(user, target, nullptr, CMD_FROM_SCRIPT))
			{
				continue;
			}
			const Coord3D *targetPos = target->getPosition();
			Real dx = targetPos->x - userPos->x;
			Real dy = targetPos->y - userPos->y;
			Real distSqr = dx * dx + dy * dy;
			if (!bestTarget || distSqr < bestDistSqr)
			{
				bestTarget = target;
				bestDistSqr = distSqr;
			}
		}
		if (bestTarget)
		{
			user->doCommandButtonAtObject(commandButton, bestTarget, CMD_FROM_SCRIPT);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamHuntWithCommandButtonType(const AsciiString& teamName, const AsciiString& ability, const AsciiString& objectType)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}
	const CommandButton *commandButton = TheControlBar->findCommandButton(ability);
	if (!commandButton)
	{
		return;
	}

	Bool canHunt = FALSE;
	switch (commandButton->getCommandType())
	{
		case GUI_COMMAND_SPECIAL_POWER:
			canHunt = commandButton->getSpecialPowerTemplate() && BitIsSet(commandButton->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET);
			break;
		case GUI_COMMAND_SWITCH_WEAPON:
		case GUI_COMMAND_FIRE_WEAPON:
		case GUICOMMANDMODE_HIJACK_VEHICLE:
		case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
		case GUICOMMANDMODE_SABOTAGE_BUILDING:
			canHunt = TRUE;
			break;
		default:
			break;
	}
	if (!canHunt)
	{
		AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
		msg.concat(ability);
		TheScriptEngine->AppendDebugMessage(msg, false);
		return;
	}

	static NameKeyType key_CommandButtonHuntUpdate = NAMEKEY("CommandButtonHuntUpdate");
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead() || !obj->getAIUpdateInterface())
		{
			continue;
		}
		if (!clpObjectMatchesType(obj, objectType))
		{
			continue;
		}

		Bool foundCommand = FALSE;
		const CommandSet *commandSet = TheControlBar->findCommandSet(obj->getCommandSetString());
		if (commandSet)
		{
			for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
			{
				if (commandSet->getCommandButton(i) == commandButton)
				{
					foundCommand = TRUE;
					break;
				}
			}
		}
		if (!foundCommand)
		{
			AsciiString msg = "Error - Team hunt with command button - unit type '";
			msg.concat(obj->getTemplate()->getName().str());
			msg.concat("' is not valid for ability ");
			msg.concat(ability);
			TheScriptEngine->AppendDebugMessage(msg, false);
			continue;
		}

		CommandButtonHuntUpdate *huntUpdate = (CommandButtonHuntUpdate *)obj->findUpdateModule(key_CommandButtonHuntUpdate);
		if (huntUpdate)
		{
			huntUpdate->setCommandButton(ability);
		}
		else
		{
			AsciiString msg = "Error - Team hunt with command button - unit type '";
			msg.concat(obj->getTemplate()->getName().str());
			msg.concat("' requires CommandButtonHuntUpdate in .ini definition to hunt with ");
			msg.concat(ability);
			TheScriptEngine->AppendDebugMessage(msg, false);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEvacuateDestroyedPercent(const AsciiString& teamName, Real value)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}

	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj || obj->isEffectivelyDead())
		{
			continue;
		}
		BodyModuleInterface *body = obj->getBodyModule();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!body || !ai)
		{
			continue;
		}
		if (body->getMaxHealth() * (value / 100.0f) > body->getHealth())
		{
			obj->leaveGroup();
			ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			ai->aiEvacuate(FALSE, CMD_FROM_SCRIPT);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSendToRepair(const AsciiString& teamName, Bool sendBackFullHP)
{
	clpTeamSendToRepair(teamName, AsciiString::TheEmptyString, sendBackFullHP);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitSendToRepair(const AsciiString& unitName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj || obj->isEffectivelyDead() || !obj->getAIUpdateInterface())
	{
		return;
	}
	Player *player = obj->getControllingPlayer();
	if (!player)
	{
		return;
	}

	const Coord3D *pos = obj->getPosition();
	Object *airfield = clpClosestOwned(pos, player, KINDOF_FS_AIRFIELD);
	Object *repairPad = clpClosestOwned(pos, player, KINDOF_REPAIR_PAD);
	Object *healPad = clpClosestOwned(pos, player, KINDOF_HEAL_PAD);
	clpDockForRepair(obj, airfield, repairPad, healPad);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSendToRepairType(const AsciiString& teamName, const AsciiString& objectType, Bool sendBackFullHP)
{
	if (objectType.isEmpty())
	{
		return;
	}
	clpTeamSendToRepair(teamName, objectType, sendBackFullHP);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamLoadAllTransportsEvenly(const AsciiString& teamName)
{
	clpLoadTransportsEvenly(teamName, AsciiString::TheEmptyString);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamLoadAllTransportsEvenlyType(const AsciiString& teamName, const AsciiString& objectType)
{
	if (objectType.isEmpty())
	{
		return;
	}
	clpLoadTransportsEvenly(teamName, objectType);
}
