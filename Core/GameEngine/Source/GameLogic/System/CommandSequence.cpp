// FILE: CommandSequence.cpp /////////////////////////////////////////////////////////////////////////////////
// Author: ShigureUi, September 2026
// Desc:   Advanced waypoint system
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Common/ActionManager.h"
#include "Common/BuildAssistant.h"
#include "Common/GameState.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"

#include "GameClient/GameClient.h"
#include "GameClient/TerrainVisual.h"

#include "GameLogic/CommandSequence.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

//-----------------------------------------------------------------------------
//         Public Data
//-----------------------------------------------------------------------------
CommandSequence *TheCommandSequence = nullptr;  ///< the command sequence singleton

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CommandSequence::CommandSequence()
{
}

CommandSequence::~CommandSequence()
{
	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		delete it->second;
	}

	for (IDToDrawableMap::iterator it = m_constructionShadows.begin(); it != m_constructionShadows.end(); it++)
	{
		TheGameClient->destroyDrawable(it->second);
	}
}

void CommandSequence::init()
{
	m_uniqueID = 0;
	m_commandNodeCount = 0;
}

void CommandSequence::reset()
{
	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		delete it->second;
	}

	for (IDToDrawableMap::iterator it = m_constructionShadows.begin(); it != m_constructionShadows.end(); it++)
	{
		TheGameClient->destroyDrawable(it->second);
	}

	m_allNodes.clear();
	m_constructionCount.clear();
	m_constructionShadows.clear();
	m_uniqueID = 0;
	m_commandNodeCount = 0;
}

void CommandSequence::update()
{

}

void CommandSequence::crc(Xfer* xfer)
{
}

void CommandSequence::xfer(Xfer* xfer)
{
	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	xfer->xferUnsignedInt(&m_uniqueID);
	xfer->xferInt(&m_commandNodeCount);
	
	if (xfer->getXferMode() == XFER_LOAD)
	{
		for (Int i = 0; i < m_commandNodeCount; i++)
		{
			UnsignedInt nodeID;
			xfer->xferUnsignedInt(&nodeID);

			CommandNode *cmdNode = new CommandNode();
			m_allNodes.insert(std::make_pair(nodeID, cmdNode));

			xferCommandNode(xfer, cmdNode);
		}

		// xfer construction count
		Int cnt;
		xfer->xferInt(&cnt);
		if (cnt < 0)
			throw SC_INVALID_DATA;

		for (Int i = 0; i < cnt; i++)
		{
			ObjectID objID;
			Int constructionCount;
			xfer->xferObjectID(&objID);
			xfer->xferInt(&constructionCount);
			m_constructionCount.insert(std::make_pair(objID, constructionCount));
		}
	}
	else
	{
		for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
		{
			CommandNode *cmdNode = it->second;
			xfer->xferUnsignedInt(&cmdNode->m_nodeID);

			xferCommandNode(xfer, cmdNode);
		}

		// xfer construction count
		Int cnt = m_constructionCount.size();
		xfer->xferInt(&cnt);
		for (std::map<ObjectID, Int>::iterator it = m_constructionCount.begin(); it != m_constructionCount.end(); it++)
		{
			ObjectID objID = it->first;
			xfer->xferObjectID(&objID);
			xfer->xferInt(&it->second);
		}
	}
}

void CommandSequence::loadPostProcess()
{
	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		CommandNode *cmdNode = it->second;
		if (doesBuildCommandHasShadow(cmdNode))
		{
			Object *obj = TheGameLogic->findObjectByID(cmdNode->m_nextCommand.begin()->first);
			if (obj && obj->getControllingPlayer()->isLocalPlayer())
				createConstructionShadow(cmdNode->m_nodeID, obj);
		}
	}
}

void CommandSequence::xferCommandNode(Xfer *xfer, CommandNode *cmdNode)
{
	xfer->xferInt((Int*)&cmdNode->m_cmdType);

	switch (cmdNode->m_cmdType)
	{
			// no argument
		case COMMAND_DO_EVACUATE:
		case COMMAND_INTERNET_HACK:
		case COMMAND_TOGGLE_OVERCHARGE:
		case COMMAND_TOGGLE_HOLD_FIRE:
		case COMMAND_TOGGLE_DEPLOY:
			break;
			// single pos
		case COMMAND_DO_MOVETO:
		case COMMAND_DO_REVERSE_MOVETO:
		{
			xfer->xferCoord3D(&cmdNode->m_loc);
			break;
		}
		// single target
		case COMMAND_GET_REPAIRED:
		case COMMAND_GET_HEALED:
		case COMMAND_ENTER:
		case COMMAND_DO_EXIT:
		case COMMAND_DO_REPAIR:
		case COMMAND_DOCK:
		case COMMAND_RESUME_CONSTRUCTION:
		{
			xfer->xferObjectID(&cmdNode->m_targetID);
			break;
		}
		// guard pos
		case COMMAND_DO_GUARD_POSITION:
		{
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferInt((Int*) & cmdNode->m_guardMode);
			break;
		}
		// guard target
		case COMMAND_DO_GUARD_OBJECT:
		{
			xfer->xferObjectID(&cmdNode->m_targetID);
			xfer->xferInt((Int*)&cmdNode->m_guardMode);
			break;
		}
		// pos and target
		case COMMAND_DO_COMBATDROP:
		{
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferObjectID(&cmdNode->m_targetID);
			break;
		}
		// pos and maxshots
		case COMMAND_DO_ATTACK_MOVETO:
		{
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferInt(&cmdNode->m_maxShotsToFire);
			break;
		}
		// do weapon at location
		case COMMAND_DO_WEAPON:
		case COMMAND_DO_WEAPON_AT_LOCATION:
		{
			xfer->xferInt((Int*)&cmdNode->m_weaponSlot);
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferInt(&cmdNode->m_maxShotsToFire);
			xfer->xferBool(&cmdNode->m_releaseAfter);
			break;
		}
		// do attack object including do weapon at object
		case COMMAND_DO_ATTACK_OBJECT:
		case COMMAND_DO_FORCE_ATTACK_OBJECT:
		{
			xfer->xferInt((Int*)&cmdNode->m_weaponSlot);
			xfer->xferObjectID(&cmdNode->m_targetID);
			xfer->xferInt(&cmdNode->m_maxShotsToFire);
			break;
		}
		// dozer construct
		case COMMAND_DOZER_CONSTRUCT:
		case COMMAND_DOZER_CONSTRUCT_LINE:
		{
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferUnsignedShort(&cmdNode->m_templateID);
			xfer->xferReal(&cmdNode->m_angle);
			if (cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT_LINE)
				xfer->xferCoord3D(&cmdNode->m_loc2);
			if (cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT)
				xfer->xferUnsignedInt(&cmdNode->m_cashWithdraw);
			break;
		}
		// do special
		case COMMAND_DO_SPECIAL_POWER:
		{
			xfer->xferUnsignedInt(&cmdNode->m_specialPowerID);
			xfer->xferUnsignedInt(&cmdNode->m_specialOptions);
			break;
		}
		// do special at location
		case COMMAND_DO_SPECIAL_POWER_AT_POSITION:
		case COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION:
		{
			xfer->xferUnsignedInt(&cmdNode->m_specialPowerID);
			xfer->xferCoord3D(&cmdNode->m_loc);
			xfer->xferReal(&cmdNode->m_angle);
			xfer->xferObjectID(&cmdNode->m_targetID);
			xfer->xferUnsignedInt(&cmdNode->m_specialOptions);
			break;
		}
		// do special at object
		case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
		{
			xfer->xferUnsignedInt(&cmdNode->m_specialPowerID);
			xfer->xferObjectID(&cmdNode->m_targetID);
			xfer->xferUnsignedInt(&cmdNode->m_specialOptions);
			break;
		}
		// switch weapon
		case COMMAND_SWITCH_WEAPON:
		{
			xfer->xferInt((Int*)&cmdNode->m_weaponSlot);
			break;
		}
	}

	if (xfer->getXferMode() == XFER_LOAD)
	{
		Int cnt;
		ObjectID objID;
		UnsignedInt nextCmd;
		xfer->xferInt(&cnt);
		for (Int i = 0; i < cnt; i++)
		{
			xfer->xferObjectID(&objID);
			xfer->xferUnsignedInt(&nextCmd);

			cmdNode->m_nextCommand.insert(std::make_pair(objID, nextCmd));
		}

		if (isTypicalMovingCommand(cmdNode->m_cmdType))
		{
			Coord3D dest;
			xfer->xferInt(&cnt);
			for (Int i = 0; i < cnt; i++)
			{
				xfer->xferObjectID(&objID);
				xfer->xferCoord3D(&dest);

				cmdNode->m_allocatedDest.insert(std::make_pair(objID, dest));
			}
		}
	}
	else
	{
		Int cnt;
		ObjectID objID;
		cnt = cmdNode->m_nextCommand.size();
		xfer->xferInt(&cnt);

		for (ObjectToNodeMap::iterator it = cmdNode->m_nextCommand.begin(); it != cmdNode->m_nextCommand.end(); it++)
		{
			objID = it->first;
			xfer->xferObjectID(&objID);
			xfer->xferUnsignedInt(&it->second);
		}

		if (isTypicalMovingCommand(cmdNode->m_cmdType))
		{
			cnt = cmdNode->m_allocatedDest.size();
			xfer->xferInt(&cnt);

			for (ObjectToLocMap::iterator it = cmdNode->m_allocatedDest.begin(); it != cmdNode->m_allocatedDest.end(); it++)
			{
				objID = it->first;
				xfer->xferObjectID(&objID);
				xfer->xferCoord3D(&it->second);
			}
		}
	}
}

Bool CommandSequence::hasAnyCommand(Object *obj)
{
	return obj->getCurrentCommand();
}

void CommandSequence::clearObjectCommand(Object *obj)
{
	while (removeCurrentCommand(obj));
	if (obj)
		obj->clearPendingExitList();
}

CommandSequence::CommandNode *CommandSequence::newCommandNode(CommandType cmdType)
{
	m_uniqueID++;
	CommandNode *cmdNode = new CommandNode();
	cmdNode->m_cmdType = cmdType;
	cmdNode->m_nodeID = m_uniqueID;
	m_allNodes.insert(std::make_pair(m_uniqueID, cmdNode));
	m_commandNodeCount++;
	return cmdNode;
}


UnsignedInt CommandSequence::newNoArgumentCommand(CommandType cmdType)
{
	switch (cmdType)
	{
	case COMMAND_DO_EVACUATE:
	case COMMAND_INTERNET_HACK:
	case COMMAND_TOGGLE_OVERCHARGE:
	case COMMAND_TOGGLE_HOLD_FIRE:
	case COMMAND_TOGGLE_DEPLOY:
		break;
	default:
		return 0;
	}
	CommandNode* cmdNode = newCommandNode(cmdType);
	return cmdNode->m_nodeID;
}

//ShigureUi 07/10/2026 note all sanity check needs a recheck
UnsignedInt CommandSequence::newSinglePosCommand(CommandType cmdType, const Coord3D *pos)
{
	switch (cmdType)
	{
	case COMMAND_DO_MOVETO:
	case COMMAND_DO_REVERSE_MOVETO:
		break;
	default:
		return 0;
	}
	CommandNode *cmdNode = newCommandNode(cmdType);
	cmdNode->m_loc = *pos;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newSingleTargetCommand(CommandType cmdType, Object *obj)
{
	switch (cmdType)
	{
	case COMMAND_GET_REPAIRED:
	case COMMAND_GET_HEALED:
	case COMMAND_ENTER:
	case COMMAND_DO_EXIT:
	case COMMAND_DO_REPAIR:
	case COMMAND_DOCK:
	case COMMAND_RESUME_CONSTRUCTION:
		break;
	default:
		return 0;
	}

	CommandNode *cmdNode = newCommandNode(cmdType);
	cmdNode->m_targetID = obj ? obj->getID() : INVALID_ID;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newTargetNPosCommand(CommandType cmdType, Object* obj, const Coord3D* pos)
{
	switch (cmdType)
	{
	case COMMAND_DO_COMBATDROP:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_targetID = obj ? obj->getID() : INVALID_ID;
	cmdNode->m_loc = *pos;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newPosNMaxShotsCommand(CommandType cmdType, const Coord3D *pos, Int maxShotsToFire)
{
	switch (cmdType)
	{
	case COMMAND_DO_ATTACK_MOVETO:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_loc = *pos;
	cmdNode->m_maxShotsToFire = maxShotsToFire;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newGuardCommand(CommandType cmdType, const Coord3D *pos, Object *obj, GuardMode guardMode)
{
	switch (cmdType)
	{
	case COMMAND_DO_GUARD_POSITION:
	case COMMAND_DO_GUARD_OBJECT:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	if (cmdNode->m_cmdType == COMMAND_DO_GUARD_POSITION)
		cmdNode->m_loc = *pos;
	else
		cmdNode->m_targetID = obj->getID();
	cmdNode->m_guardMode = guardMode;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDoWeaponAtLocCommand(CommandType cmdType, WeaponSlotType weaponSlot, const Coord3D *pos, Int maxShotsToFire, Bool releaseAfter)
{
	switch (cmdType)
	{
	case COMMAND_DO_WEAPON:
	case COMMAND_DO_WEAPON_AT_LOCATION:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_weaponSlot = weaponSlot;
	if (pos)
		cmdNode->m_loc = *pos;
	cmdNode->m_maxShotsToFire = maxShotsToFire;
	cmdNode->m_releaseAfter = releaseAfter;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDoWeaponAtObjCommand(CommandType cmdType, WeaponSlotType weaponSlot, Object* obj, Int maxShotsToFire)
{
	switch (cmdType)
	{
	case COMMAND_DO_ATTACK_OBJECT:
	case COMMAND_DO_FORCE_ATTACK_OBJECT:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_weaponSlot = weaponSlot;
	cmdNode->m_targetID = obj->getID();
	cmdNode->m_maxShotsToFire = maxShotsToFire;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDozerConstructCommand(CommandType cmdType, const ThingTemplate *thing, const Coord3D *loc1, const Coord3D* loc2, Real angle)
{
	switch (cmdType)
	{
	case COMMAND_DOZER_CONSTRUCT:
	case COMMAND_DOZER_CONSTRUCT_LINE:
		break;
	default:
		return 0;
	}

	if (cmdType == COMMAND_DOZER_CONSTRUCT && !loc1)
		return 0;
	else if (cmdType == COMMAND_DOZER_CONSTRUCT_LINE && (!loc1 || !loc2))
		return 0;

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_loc = *loc1;
	cmdNode->m_templateID = thing->getTemplateID();
	cmdNode->m_angle = angle;
	if (cmdType == COMMAND_DOZER_CONSTRUCT_LINE)
		cmdNode->m_loc2 = *loc2;
	cmdNode->m_cashWithdraw = 0;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDoSpecialCommand(CommandType cmdType, UnsignedInt specialPowerID, UnsignedInt commandOptions)
{
	switch (cmdType)
	{
	case COMMAND_DO_SPECIAL_POWER:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_specialPowerID = specialPowerID;
	cmdNode->m_specialOptions = commandOptions;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDoSpecialAtLocCommand(CommandType cmdType, UnsignedInt specialPowerID, const Coord3D *location, Real angle, const Object* objectInWay, UnsignedInt commandOptions)
{
	switch (cmdType)
	{
	case COMMAND_DO_SPECIAL_POWER_AT_POSITION:
	case COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_specialPowerID = specialPowerID;
	cmdNode->m_loc = *location;
	cmdNode->m_angle = angle;
	cmdNode->m_targetID = objectInWay == nullptr ? INVALID_ID : objectInWay->getID();
	cmdNode->m_specialOptions = commandOptions;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newDoSpecialAtObjCommand(CommandType cmdType, UnsignedInt specialPowerID, Object* target, UnsignedInt commandOptions)
{
	switch (cmdType)
	{
	case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_specialPowerID = specialPowerID;
	cmdNode->m_targetID = target->getID();
	cmdNode->m_specialOptions = commandOptions;
	return cmdNode->m_nodeID;
}

UnsignedInt CommandSequence::newSwitchWeaponCommand(CommandType cmdType, WeaponSlotType weaponSlot)
{
	switch (cmdType)
	{
	case COMMAND_SWITCH_WEAPON:
		break;
	default:
		return 0;
	}

	CommandNode* cmdNode = newCommandNode(cmdType);
	cmdNode->m_weaponSlot = weaponSlot;
	return cmdNode->m_nodeID;
}

Bool CommandSequence::handleMoveStreak(Object* obj)
{
	if (!obj)
		return false;

	UnsignedInt curStreakID = obj->getLastCommandInMoveStreak();
	CommandNode *curNode = findCommandByID(obj->getCurrentCommand());
	if (!curNode)
	{
		return false;
	}

	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();

	// ShigureUi 07/11/2026 Don't bother do current command, it will be done somewhere else.
	if (!curStreakID && (curNode->m_cmdType == COMMAND_DO_MOVETO || curNode->m_cmdType == COMMAND_DO_REVERSE_MOVETO) && cvs != STATUS_EXIT_VERIFIED)
	{
		obj->setLastCommandInMoveStreak(obj->getCurrentCommand());
		return true;
	}

	CommandNode *curStreak = findCommandByID(curStreakID);
	CommandNode *nextStreak = nextCommand(obj, curStreak);

	if (!nextStreak || (nextStreak->m_cmdType != COMMAND_DO_MOVETO && nextStreak->m_cmdType != COMMAND_DO_REVERSE_MOVETO))
		return false;

	obj->setLastCommandInMoveStreak(nextStreak->m_nodeID);

	AIUpdateInterface* ai = obj->getAI();
	if (ai)
	{
		Coord3D curDest, nextDest;
		ObjectToLocMap::iterator it;

		it = nextStreak->m_allocatedDest.find(obj->getID());
		if (it == nextStreak->m_allocatedDest.end())
			nextDest = nextStreak->m_loc;
		else
			nextDest = it->second;
		
		if (ai->getAIStateType() == AI_FOLLOW_PATH)
		{
			ai->aiFollowPathAppend(&nextDest, CMD_FROM_PLAYER, nextStreak->m_cmdType == COMMAND_DO_REVERSE_MOVETO, nextStreak->m_nodeID);
		}
		else
		{
			it = curStreak->m_allocatedDest.find(obj->getID());
			if (it == curStreak->m_allocatedDest.end())
				curDest = curStreak->m_loc;
			else
				curDest = it->second;

			VecCoord3D path;
			path.push_back(curDest);
			ai->aiFollowPath(&path, nullptr, CMD_FROM_PLAYER, curStreak->m_cmdType == COMMAND_DO_REVERSE_MOVETO, curStreakID);
			ai->aiFollowPathAppend(&nextDest, CMD_FROM_PLAYER, nextStreak->m_cmdType == COMMAND_DO_REVERSE_MOVETO, nextStreak->m_nodeID);
		}
	}
	return true;
}

Bool CommandSequence::includeUnitInCommand(UnsignedInt cmdNodeID, Object *obj, const Coord3D *objDest)
{
	if (!obj)
		return false;

	CommandNode *cmdNode = findCommandByID(cmdNodeID);
	if (cmdNode == nullptr)
	{
		return false;
	}

	UnsignedInt lastCmdNodeID = obj->getLastCommand();
	CommandNode *lastCmdNode = findCommandByID(lastCmdNodeID);

	if (lastCmdNode && isEndCommand(lastCmdNode))
		return false;

	cmdNode->m_nextCommand.insert(std::make_pair(obj->getID(), 0));

	if (lastCmdNode)
	{
		ObjectToNodeMap::iterator it = lastCmdNode->m_nextCommand.find(obj->getID());
		if (it == lastCmdNode->m_nextCommand.end() || it->second)
			// very wrong
			return false;
		it->second = cmdNodeID;

		obj->setLastCommand(cmdNodeID);
	}
	else
	{
		obj->setCurrentCommand(cmdNodeID);
		obj->setLastCommand(cmdNodeID);
		obj->setCommandVerifyStatus(STATUS_FRESH);
	}

	if (cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT || cmdNode->m_cmdType == COMMAND_RESUME_CONSTRUCTION)
		updateConstructionCount(obj, 1);

	return true;
}

Bool CommandSequence::includeUnitInTypicalMovingCommand(UnsignedInt cmdNodeID, Object *obj, Coord3D objDest)
{
	if (!includeUnitInCommand(cmdNodeID, obj, &objDest))
		return false;

	CommandNode *cmdNode = findCommandByID(cmdNodeID);

	if (isTypicalMovingCommand(cmdNode->m_cmdType))
		cmdNode->m_allocatedDest.insert(std::make_pair(obj->getID(), objDest));

	if (obj->getCommandVerifyStatus() != STATUS_UNIT_PENDING)
		handleMoveStreak(obj);

	return true;
}

void CommandSequence::issueCommand(UnsignedInt cmdNodeID, Object *obj, Bool isWaypoint, CommandSourceType cmdSource)
{
	if (!obj)
	{
		return;
	}

	if (!isWaypoint)
	{
		clearObjectCommand(obj);
	}

	Bool runNow = !hasAnyCommand(obj);
	if (!includeUnitInCommand(cmdNodeID, obj))
	{
		return;
	}

	if (runNow)
	{
		// The caller may still add units to this node, so it is not freed here.
		doCurrentCommand(obj, obj->getCommandVerifyStatus(), cmdSource, false);
	}
}

Bool CommandSequence::isTypicalMovingCommand(CommandType cmdType)
{
	switch (cmdType)
	{
		case COMMAND_DO_MOVETO:
		case COMMAND_DO_REVERSE_MOVETO:
		case COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION:
			return true;
		default:
			return false;
	}
}

inline Bool CommandSequence::isEndCommand(CommandNode *cmdNode)
{
	switch (cmdNode->m_cmdType)
	{
	case COMMAND_DO_GUARD_POSITION:
	case COMMAND_INTERNET_HACK:
		return true;
	case COMMAND_DO_WEAPON_AT_LOCATION:
		if (cmdNode->m_maxShotsToFire == NO_MAX_SHOTS_LIMIT)
			return true;
		else
			return false;
	default:
		return false;
	}
}

inline Bool CommandSequence::hasEndCommand(Object *obj)
{
	CommandNode *cmdNode = findCommandByID(obj->getLastCommand());
	if (isEndCommand(cmdNode))
		return true;
	return false;
}

inline Bool CommandSequence::isLocatableCommand(UnsignedInt cmdNodeID)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);
	if (cmdNode == nullptr)
		return false;

	switch (cmdNode->m_cmdType)
	{
		case COMMAND_NONE:
		case COMMAND_DO_WEAPON:
		case COMMAND_DO_SPECIAL_POWER:
		case COMMAND_DO_EXIT:
		case COMMAND_DO_EVACUATE:
		case COMMAND_INTERNET_HACK:
		case COMMAND_SWITCH_WEAPON:
		case COMMAND_TOGGLE_HOLD_FIRE:
		case COMMAND_TOGGLE_DEPLOY:
			return false;
		case COMMAND_DO_GUARD_OBJECT:
		case COMMAND_DO_ATTACK_OBJECT:
		case COMMAND_DO_FORCE_ATTACK_OBJECT:
		case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
		case COMMAND_GET_REPAIRED:
		case COMMAND_GET_HEALED:
		case COMMAND_DO_REPAIR:
		case COMMAND_RESUME_CONSTRUCTION:
		case COMMAND_ENTER:
		case COMMAND_DOCK:
		{
			if (TheGameLogic->findObjectByID(cmdNode->m_targetID))
				return true;
			else
				return false;
		}
		case COMMAND_DO_COMBATDROP:
		{
			if (!cmdNode->m_targetID || TheGameLogic->findObjectByID(cmdNode->m_targetID))
				return true;
			else
				return false;
		}
		default:
			return true;
	}
}

void CommandSequence::cleanUpEmptyCommand(UnsignedInt cmdNodeID)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);

	// A unit that dies while carrying out the command takes the emptied node with it.
	if (cmdNode == nullptr)
	{
		return;
	}

	if (cmdNode->m_nextCommand.empty())
	{
		m_allNodes.erase(cmdNodeID);
		m_commandNodeCount--;
		delete cmdNode;
	}
}

UnsignedInt CommandSequence::getFirstLocatableCommand(Object *obj)
{
	if (!obj)
		return 0;

	ObjectID objID = obj->getID();
	UnsignedInt curCmdID = obj->getCurrentCommand();
	while (curCmdID && !isLocatableCommand(curCmdID))
	{
		curCmdID = findCommandByID(curCmdID)->m_nextCommand.find(objID)->second;
	}

	return curCmdID;
}

UnsignedInt CommandSequence::getNextLocatableCommand(Object* obj, UnsignedInt cmdNodeID)
{
	if (!obj)
		return 0;

	ObjectID objID = obj->getID();
	UnsignedInt curCmdID = cmdNodeID;
	do
	{
		curCmdID = findCommandByID(curCmdID)->m_nextCommand.find(objID)->second;
	}
	while (curCmdID && !isLocatableCommand(curCmdID));

	return curCmdID;
}

Bool CommandSequence::getCommandLoc(UnsignedInt cmdNodeID, Coord3D &loc)
{
	if (!isLocatableCommand(cmdNodeID))
		return false;

	CommandNode *cmdNode = findCommandByID(cmdNodeID);

	Object* obj = nullptr;

	switch (cmdNode->m_cmdType)
	{
		case COMMAND_DO_MOVETO:
		case COMMAND_DO_REVERSE_MOVETO:
		case COMMAND_DO_ATTACK_MOVETO:
		case COMMAND_DO_GUARD_POSITION:
		case COMMAND_DO_WEAPON_AT_LOCATION:
		case COMMAND_DO_SPECIAL_POWER_AT_POSITION:
		case COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION:
		case COMMAND_DOZER_CONSTRUCT:
		case COMMAND_DOZER_CONSTRUCT_LINE:
			loc = cmdNode->m_loc;
			break;
		case COMMAND_DO_GUARD_OBJECT:
		case COMMAND_DO_ATTACK_OBJECT:
		case COMMAND_DO_FORCE_ATTACK_OBJECT:
		case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
		case COMMAND_GET_REPAIRED:
		case COMMAND_GET_HEALED:
		case COMMAND_DO_REPAIR:
		case COMMAND_RESUME_CONSTRUCTION:
		case COMMAND_ENTER:
		case COMMAND_DOCK:
			obj = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (obj == nullptr)
				return false;
			loc = *obj->getPosition();
			break;
		case COMMAND_DO_COMBATDROP:
			if (cmdNode->m_targetID == INVALID_ID)
			{
				loc = cmdNode->m_loc;
			}
			else
			{
				obj = TheGameLogic->findObjectByID(cmdNode->m_targetID);
				if (obj == nullptr)
					return false;
				loc = *obj->getPosition();
			}
			break;
		default:
			return false;
	}
	return true;
}

CommandSequence::CommandNode *CommandSequence::findCommandByID(UnsignedInt cmdNodeID)
{
	//if (m_prevNodeID == cmdNodeID)
	//	return m_prevNode;

	std::map<UnsignedInt, CommandNode*>::iterator it;
	it = m_allNodes.find(cmdNodeID);
	if (it == m_allNodes.end())
		return nullptr;

	return it->second;
}

UnsignedInt CommandSequence::removeCurrentCommand(Object *obj, Bool doCleanUp)
{
	CommandNode *node = findCommandByID(obj->getCurrentCommand());

	if (node == nullptr)
		return 0;

	ObjectToNodeMap::iterator it = node->m_nextCommand.find(obj->getID());

	if (it == node->m_nextCommand.end())
		return 0;

	if (obj->getLastCommandInMoveStreak() == obj->getCurrentCommand())
		obj->setLastCommandInMoveStreak(0);

	if (node->m_cmdType == COMMAND_DOZER_CONSTRUCT || node->m_cmdType == COMMAND_RESUME_CONSTRUCTION)
		updateConstructionCount(obj, -1);

	if (node->m_cmdType == COMMAND_DOZER_CONSTRUCT)
	{
		removeConstructionShadow(obj->getCurrentCommand());

		Money* money = obj->getControllingPlayer()->getMoney();

		money->deposit(node->m_cashWithdraw, true, false);
	}

	UnsignedInt ret = it->second;

	if (ret)
	{
		obj->setCurrentCommand(ret);
		if (obj->getCommandVerifyStatus() != STATUS_FOLLOW_PATH_VERIFIED)
			obj->setCommandVerifyStatus(STATUS_FRESH);
	}
	else
	{
		obj->setCurrentCommand(0);
		obj->setLastCommand(0);
		obj->setCommandVerifyStatus(STATUS_NONE);
	}

	node->m_nextCommand.erase(it);

	if (isTypicalMovingCommand(node->m_cmdType))
	{
		ObjectToLocMap::iterator lit = node->m_allocatedDest.find(obj->getID());
		node->m_allocatedDest.erase(lit);
	}

	if (doCleanUp && node->m_nextCommand.empty())
	{
		IDToNodeMap::iterator iit = m_allNodes.find(node->m_nodeID);
		if (iit != m_allNodes.end())
			m_allNodes.erase(iit);
		delete node;
		m_commandNodeCount--;
	}

	return ret;
}

CommandSequence::CommandNode *CommandSequence::nextCommand(Object *obj, CommandNode *cmdNode)
{
	if (!obj || !cmdNode)
		return nullptr;

	ObjectToNodeMap::iterator it = cmdNode->m_nextCommand.find(obj->getID());

	if (it == cmdNode->m_nextCommand.end())
		return nullptr;

	return findCommandByID(it->second);
}


void CommandSequence::doCurrentCommand(Object *obj, CommandVerifyStatus cvs, CommandSourceType cmdSource, Bool doCleanUp)
{
	CommandNode *cmdNode = findCommandByID(obj->getCurrentCommand());
	if (!cmdNode)
		return;

	prepareCommandForVerification(obj);

	CommandType cmdType = cmdNode->m_cmdType;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();

	Object *target;

	switch (cmdType)
	{
		// AIMoveToState
		case COMMAND_DO_MOVETO:
		case COMMAND_DO_REVERSE_MOVETO:
		{
			Bool isReverse = cmdType == COMMAND_DO_REVERSE_MOVETO;
			Coord3D unitLoc;
			ObjectToLocMap::iterator it = cmdNode->m_allocatedDest.find(obj->getID());
			if (it == cmdNode->m_allocatedDest.end())
				break;
			unitLoc = it->second;

			if (ai)
			{
				ai->handleStealthCombatUnitMove();
				VecCoord3D path;
				path.push_back(unitLoc);
				ai->aiFollowPath(&path, nullptr, cmdSource, isReverse, cmdNode->m_nodeID);
				if (!TheCommandSequence->isStillAwaiting(obj))
				{
					obj->setLastCommandInMoveStreak(cmdNode->m_nodeID);
					if (obj->getCommandVerifyStatus() != STATUS_UNIT_PENDING)
					{
						while (handleMoveStreak(obj));
					}
				}
			}
			break;
		}
		case COMMAND_DO_ATTACK_MOVETO:
		{
			obj->releaseWeaponLock(LOCKED_TEMPORARILY);

			if (ai)
			{
				if (obj->isAbleToAttack())
					ai->aiAttackMoveToPosition(&cmdNode->m_loc, cmdNode->m_maxShotsToFire, cmdSource);
				else
					ai->aiMoveToPosition(&cmdNode->m_loc, cmdSource);
			}
			break;
		}
		case COMMAND_DO_GUARD_POSITION:
		{
			if (ai)
			{
				ai->aiGuardPosition(&cmdNode->m_loc, cmdNode->m_guardMode, cmdSource);
			}
			break;
		}
		case COMMAND_DO_GUARD_OBJECT:
		{
			target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (ai && target)
			{
				ai->aiGuardObject(target, cmdNode->m_guardMode, cmdSource);
			}
			break;
		}
		case COMMAND_DO_WEAPON:
		case COMMAND_DO_WEAPON_AT_LOCATION:
		{
			Coord3D attackPos = cmdType == COMMAND_DO_WEAPON ? *obj->getPosition() : cmdNode->m_loc;
			if (cmdNode->m_weaponSlot == WEAPON_NONE)
			{
				obj->releaseWeaponLock(LOCKED_TEMPORARILY);
			}
			else
			{
				obj->setWeaponLock(cmdNode->m_weaponSlot, LOCKED_TEMPORARILY);
			}

			//This code allows garrisoned buildings to force attack a ground position
			//-----------------------------------------------------------------------
			//Determine if this object is a garrisoned container capable of firing!
			//If so, order everyone inside to attack as well!
			ContainModuleInterface* contain = obj->getContain();
			if (contain && contain->isPassengerAllowedToFire())
			{
				//Loop through each member and order them to attack the same target (if possible)
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
					{
						Object* garrisonedMember = *it;

						if (!contain->isPassengerAllowedToFire(garrisonedMember->getID())) continue;

						CanAttackResult result = garrisonedMember->getAbleToUseWeaponAgainstTarget(ATTACK_NEW_TARGET, nullptr, &attackPos, cmdSource);
						if (result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING)
						{
							AIUpdateInterface* memberAI = garrisonedMember->getAI();
							if (memberAI)
							{
								memberAI->aiAttackPosition(&attackPos, cmdNode->m_maxShotsToFire, cmdSource);
							}
						}
					}
				}
			}

			//Also handle slaves. If we have slaves, then order them to stop too!
			SpawnBehaviorInterface* spawnInterface = obj->getSpawnBehaviorInterface();
			if (spawnInterface && !spawnInterface->doSlavesHaveFreedom())
			{
				spawnInterface->orderSlavesToAttackPosition(&attackPos, cmdNode->m_maxShotsToFire, cmdSource);
			}

			if (ai)
			{
				ai->aiAttackPosition(&attackPos, cmdNode->m_maxShotsToFire, cmdSource);
			}

			if (cmdNode->m_releaseAfter)
			{
				obj->releaseWeaponLock(LOCKED_TEMPORARILY);
			}

			break;
		}
		case COMMAND_DO_ATTACK_OBJECT:
		case COMMAND_DO_FORCE_ATTACK_OBJECT:
		{
			Bool forced = cmdType == COMMAND_DO_FORCE_ATTACK_OBJECT;

			target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!target)
				break;

			if (cmdSource != CMD_FROM_SCRIPT)
			{
				if (cmdNode->m_weaponSlot == WEAPON_NONE)
				{
					obj->releaseWeaponLock(LOCKED_TEMPORARILY);
				}
				else
				{
					obj->setWeaponLock(cmdNode->m_weaponSlot, LOCKED_TEMPORARILY);
				}
			}

			//Determine if this object is a garrisoned container capable of firing!
			//If so, order everyone inside to attack as well!
			ContainModuleInterface* contain = obj->getContain();
			if (contain && contain->isPassengerAllowedToFire())
			{
				//Loop through each member and order them to attack the same target (if possible)
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
					{
						Object* garrisonedMember = *it;

						if (!contain->isPassengerAllowedToFire(garrisonedMember->getID())) continue;

						CanAttackResult result = garrisonedMember->getAbleToAttackSpecificObject(forced ? ATTACK_NEW_TARGET_FORCED : ATTACK_NEW_TARGET, target, cmdSource);
						if (result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING)
						{
							AIUpdateInterface* memberAI = garrisonedMember->getAI();
							if (memberAI)
							{
								if (forced)
									memberAI->aiForceAttackObject(target, cmdNode->m_maxShotsToFire, cmdSource);
								else
									memberAI->aiAttackObject(target, cmdNode->m_maxShotsToFire, cmdSource);
							}
						}
					}
				}
			}

			//Do a check to see if we have a hive object that has slaved objects.
			SpawnBehaviorInterface* spawnInterface = obj->getSpawnBehaviorInterface();
			if (spawnInterface && !spawnInterface->doSlavesHaveFreedom())
			{
				spawnInterface->orderSlavesToAttackTarget(target, cmdNode->m_maxShotsToFire, cmdSource);
			}

			//Order the specific group object to attack!
			if (ai && obj != target)
			{
				if (forced)
					ai->aiForceAttackObject(target, cmdNode->m_maxShotsToFire, cmdSource);
				else
					ai->aiAttackObject(target, cmdNode->m_maxShotsToFire, cmdSource);
			}
			break;
		}
		case COMMAND_DOZER_CONSTRUCT:
		{
			const ThingTemplate* thing = TheThingFactory->findByTemplateID(cmdNode->m_templateID);
			if (!thing)
				break;

			removeConstructionShadow(obj->getCurrentCommand());

			TheBuildAssistant->buildObjectNow(obj, thing, &cmdNode->m_loc, cmdNode->m_angle,
				obj->getControllingPlayer());
			break;
		}
		// unused
		case COMMAND_DOZER_CONSTRUCT_LINE:
		{
			const ThingTemplate* thing = TheThingFactory->findByTemplateID(cmdNode->m_templateID);
			if (!thing)
				break;
			TheBuildAssistant->buildObjectLineNow(obj, thing, &cmdNode->m_loc, &cmdNode->m_loc2, cmdNode->m_angle,
				obj->getControllingPlayer());
			break;
		}
		case COMMAND_DO_SPECIAL_POWER:
		{
			const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID(cmdNode->m_specialPowerID);
			SpecialPowerModuleInterface* mod = obj->getSpecialPowerModule(spTemplate);
			if (!mod || !mod->isReady())
				break;

			mod->doSpecialPower(cmdNode->m_specialOptions);
			obj->friend_setUndetectedDefector(FALSE);// My secret is out

			break;
		}
		case COMMAND_DO_SPECIAL_POWER_AT_POSITION:
		case COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION:
		{
			const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID(cmdNode->m_specialPowerID);
			SpecialPowerModuleInterface *mod = obj->getSpecialPowerModule(spTemplate);
			if (!mod || !mod->isReady())
				break;

			Coord3D unitLoc = cmdNode->m_loc;

			if (cmdType == COMMAND_DO_MOVING_SPECIAL_POWER_AT_POSITION)
			{
				ObjectToLocMap::iterator it = cmdNode->m_allocatedDest.find(obj->getID());
				if (it == cmdNode->m_allocatedDest.end())
					break;

				unitLoc = it->second;
			}

			mod->doSpecialPowerAtLocation(&unitLoc, cmdNode->m_angle, cmdNode->m_specialOptions);

			obj->friend_setUndetectedDefector(FALSE);// My secret is out

			break;
		}
		case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
		{
			Object *target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!target)
				break;

			const SpecialPowerTemplate* spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID(cmdNode->m_specialPowerID);
			SpecialPowerModuleInterface* mod = obj->getSpecialPowerModule(spTemplate);
			
			if (!mod || !mod->isReady())
				break;

			mod->doSpecialPowerAtObject(target, cmdNode->m_specialOptions);
			obj->friend_setUndetectedDefector(FALSE);// My secret is out

			break;
		}
		case COMMAND_DO_EXIT:
		{
			Object *objWantToExit = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!objWantToExit || objWantToExit->getContainedBy() != obj)
				break;

			objWantToExit->releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.

			if (objWantToExit->getControllingPlayer() != obj->getControllingPlayer())
				break;

			// exit whatever objectWantingToExit is INSIDE of
			ai = objWantToExit->getAIUpdateInterface();
			if (ai)
				ai->aiExit(obj, cmdSource);

			break;
		}
		case COMMAND_DO_EVACUATE:
		{
			ContainModuleInterface *contain = obj->getContain();
			if (contain)
			{
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
					{
						Object *member = *it;
						ai = member->getAIUpdateInterface();
						if (ai)
						{
							ai->aiExit(obj, CMD_FROM_PLAYER);
						}
					}
				}
			}

			break;
		}
		case COMMAND_DO_COMBATDROP:
		{
			if (!ai)
				break;

			if (cmdNode->m_targetID == INVALID_ID)
				ai->aiCombatDrop(nullptr, cmdNode->m_loc, cmdSource);
			else
			{
				Object *target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
				if (!target)
					break;

				ai->aiCombatDrop(target, *target->getPosition(), cmdSource);
			}

			break;
		}

		case COMMAND_GET_REPAIRED:
		{
			if (!ai)
				break;

			Object *repairDepot = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!repairDepot)
				break;

			if (TheActionManager->canGetRepairedAt(obj, repairDepot, cmdSource))
				ai->aiGetRepaired(repairDepot, cmdSource);
			else
				break;

			break;
		}

		case COMMAND_GET_HEALED:
		{
			if (!ai)
				break;

			Object* healDepot = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!healDepot)
				break;

			if (TheActionManager->canGetHealedAt(obj, healDepot, cmdSource))
				ai->aiGetHealed(healDepot, cmdSource);
			else
				break;

			break;
		}
		case COMMAND_DO_REPAIR:
		{
			if (!ai)
				break;

			Object *target = TheGameLogic->findObjectByID(cmdNode->m_targetID);

			if (!target)
				break;

			ai->aiRepair(target, cmdSource);

			break;
		}
		case COMMAND_RESUME_CONSTRUCTION:
		{
			Object* target = TheGameLogic->findObjectByID(cmdNode->m_targetID);

			if (!target)
				break;

			if (ai)
			{
				ai->aiResumeConstruction(target, cmdSource);
			}
			break;
		}
		case COMMAND_ENTER:
		{
			if (!ai)
				break;

			Object *target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!target)
				break;

			if (TheActionManager->canEnterObject(obj, target, cmdSource, DONT_CHECK_CAPACITY))
			{
				obj->releaseWeaponLock(LOCKED_TEMPORARILY);
				ai->aiEnter(target, cmdSource);
			}
			else
				break;

			break;
		}
		case COMMAND_DOCK:
		{
			if (!ai)
				break;

			Object* target = TheGameLogic->findObjectByID(cmdNode->m_targetID);
			if (!target)
				break;

			ai->aiDock(target, cmdSource);

			break;
		}
		case COMMAND_INTERNET_HACK:
		{
			if (!ai)
				break;

			ai->aiHackInternet(cmdSource);

			break;
		}
		case COMMAND_TOGGLE_OVERCHARGE:
		{
			OverchargeBehaviorInterface* obi;
			for (BehaviorModule** bmi = obj->getBehaviorModules(); *bmi; ++bmi)
			{
				obi = (*bmi)->getOverchargeBehaviorInterface();
				if (obi)
				{
					obi->toggle();
				}
			}

			break;
		}
		case COMMAND_SWITCH_WEAPON:
		{
			obj->setWeaponLock(cmdNode->m_weaponSlot, LOCKED_PERMANENTLY);

			break;
		}
		case COMMAND_TOGGLE_HOLD_FIRE:
		{
			if (!ai)
				break;

			ai->setHoldingFire(!ai->isHoldingFire());

			break;
		}
		case COMMAND_TOGGLE_DEPLOY:
		{
			DeployStyleAIUpdate* deployAI = ai ? ai->getDeployStyleAIUpdate() : nullptr;
			if (deployAI == nullptr)
				break;

			if (!deployAI->isDeployedOrDeploying())
				deployAI->toggleManualDeploy();
			else
				break;

			break;
		}
		default:
			// shouldn't happen
			break;
	}

	CommandVerifyStatus newCvs = obj->getCommandVerifyStatus();
	if (newCvs == STATUS_AWAITING || newCvs == STATUS_EXIT_VERIFIED)
		removeCurrentCommand(obj, doCleanUp);

	if (cvs == STATUS_EXIT_VERIFIED || newCvs == STATUS_EXIT_VERIFIED)
		obj->setCommandVerifyStatus(STATUS_EXIT_VERIFIED);
}

void CommandSequence::proceedNextCommand(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	while (cvs == STATUS_FRESH || (cvs == STATUS_EXIT_VERIFIED && isCurrentExitCommand(obj)))
	{
		doCurrentCommand(obj, cvs);
		cvs = obj->getCommandVerifyStatus();
	}
}

inline void CommandSequence::prepareCommandForVerification(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_FRESH || cvs == STATUS_UNIT_PENDING)
	{
		obj->setCommandVerifyStatus(STATUS_AWAITING);
	}
}

Bool CommandSequence::isStillAwaiting(Object *obj)
{
	return obj->getCommandVerifyStatus() == STATUS_AWAITING;
}

Bool CommandSequence::dozerPendingTask(Object *obj)
{
	if (obj->getCommandVerifyStatus() == STATUS_AWAITING)
	{
		obj->setCommandVerifyStatus(STATUS_DOZER_PENDING);
		return true;
	}

	return false;
}

Bool CommandSequence::oclPending(Object *obj)
{
	if (obj->getCommandVerifyStatus() == STATUS_AWAITING)
	{
		obj->setCommandVerifyStatus(STATUS_OCL_PENDING);
		return true;
	}

	return false;
}

Bool CommandSequence::verifyObjectCommand(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_AWAITING || cvs == STATUS_DOZER_PENDING)
	{
		obj->setCommandVerifyStatus(STATUS_VERIFIED);
		return true;
	}

	return false;
}

Bool CommandSequence::verifyFollowPathCommand(Object* obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_AWAITING || cvs == STATUS_COMPLETE)
	{
		obj->setCommandVerifyStatus(STATUS_FOLLOW_PATH_VERIFIED);
		return true;
	}

	return false;
}

Bool CommandSequence::unitPendingCommand(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_AWAITING)
	{
		obj->setCommandVerifyStatus(STATUS_UNIT_PENDING);
		return true;
	}

	return false;
}

Bool CommandSequence::unitOnHeld(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_VERIFIED)
	{
		obj->setCommandVerifyStatus(STATUS_ON_HELD);
		return true;
	}

	return false;
}

Bool CommandSequence::verifyExitCommand(Object *objWantToExit, Object *objectContainingExiter)
{
	if (!objectContainingExiter)
	{
		return false;
	}

	CommandNode *cmdNode = findCommandByID(objectContainingExiter->getCurrentCommand());
	if (cmdNode &&
		(((cmdNode->m_cmdType == COMMAND_DO_EXIT && cmdNode->m_targetID == objWantToExit->getID()))
			|| cmdNode->m_cmdType == COMMAND_DO_EVACUATE))
	{
		objectContainingExiter->addToPendingExitList(objWantToExit);
		CommandVerifyStatus cvs = objectContainingExiter->getCommandVerifyStatus();
		if (cvs == STATUS_AWAITING)
		{
			objectContainingExiter->setCommandVerifyStatus(STATUS_EXIT_VERIFIED);
			return true;
		}
	}

	return false;
}

Bool CommandSequence::verifyOCLPendingCommand(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_OCL_PENDING || cvs == STATUS_OCL_COMPLETE)
	{
		obj->setCommandVerifyStatus(STATUS_OCL_VERIFIED);
		return true;
	}

	return false;
}

Bool CommandSequence::terminateDozerPending(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_DOZER_PENDING)
	{
		obj->setCommandVerifyStatus(STATUS_COMPLETE);
		return true;
	}

	return false;
}

Bool CommandSequence::terminateOCLPending(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_OCL_PENDING)
	{
		obj->setCommandVerifyStatus(STATUS_COMPLETE);
		return true;
	}

	return false;
}

Bool CommandSequence::terminateVerifiedCommand(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_VERIFIED)
	{
		obj->setCommandVerifyStatus(STATUS_COMPLETE);
		return true;
	}

	return false;
}

Bool CommandSequence::terminateVerifiedFollowPathCommand(Object* obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_FOLLOW_PATH_VERIFIED)
	{
		obj->setCommandVerifyStatus(STATUS_COMPLETE);
		return true;
	}

	return false;
}

Bool CommandSequence::confirmExitCommand(Object *objWantToExit, Object *objectContainingExiter)
{
	// The container can be destroyed while a passenger still waits to exit it.
	if (!objectContainingExiter)
	{
		return false;
	}

	CommandVerifyStatus cvs = objectContainingExiter->getCommandVerifyStatus();
	if (cvs == STATUS_EXIT_VERIFIED)
	{
		objectContainingExiter->removeFromPendingExitList(objWantToExit);
		return true;
	}

	return false;
}

Bool CommandSequence::terminateOCLVerifiedCommand(Object* obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();
	if (cvs == STATUS_OCL_VERIFIED)
	{
		obj->setCommandVerifyStatus(STATUS_OCL_COMPLETE);
		return true;
	}

	return false;
}

Bool CommandSequence::isCurrentExitCommand(Object *obj)
{
	CommandNode *cmdNode = findCommandByID(obj->getCurrentCommand());

	if (cmdNode && (cmdNode->m_cmdType == COMMAND_DO_EXIT || cmdNode->m_cmdType == COMMAND_DO_EVACUATE))
		return true;
	return false;
}

// unused
Bool CommandSequence::isExitCommandComplete(Object *obj)
{
	CommandVerifyStatus cvs = obj->getCommandVerifyStatus();

	if (cvs == STATUS_EXIT_VERIFIED && obj->isPendingExitListEmpty())
		return true;
	return false;
}

void CommandSequence::updateConstructionCount(Object *obj, Int delta)
{
	ObjectID objID = obj->getID();
	std::map<ObjectID, Int>::iterator it = m_constructionCount.find(objID);

	if (it == m_constructionCount.end())
	{
		if (delta > 0)
			m_constructionCount.insert(std::make_pair(objID, delta));
	}
	else
	{
		it->second += delta;
		if (it->second <= 0)
			m_constructionCount.erase(it);
	}
}

Int CommandSequence::getConstructionCount(Object *obj)
{
	ObjectID objID = obj->getID();
	std::map<ObjectID, Int>::iterator it = m_constructionCount.find(objID);

	if (it == m_constructionCount.end())
	{
		return 0;
	}
	else
	{
		return it->second;
	}
}

void CommandSequence::createConstructionShadow(UnsignedInt cmdNodeID, Object *sourceObject)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);
	if (!sourceObject || !cmdNode || cmdNode->m_cmdType != COMMAND_DOZER_CONSTRUCT)
		return;

	if (!TheRecorder->isPlaybackMode() && !sourceObject->getControllingPlayer()->isLocalPlayer())
		return;

	IDToDrawableMap::iterator it = m_constructionShadows.find(cmdNodeID);

	const ThingTemplate* build = TheThingFactory->findByTemplateID(cmdNode->m_templateID);

	if (!build)
		return;

	if (it == m_constructionShadows.end())
	{
		Drawable* draw;

		UnsignedInt drawableStatus = DRAWABLE_STATUS_NO_STATE_PARTICLES;
		drawableStatus |= TheGlobalData->m_objectPlacementShadows ? DRAWABLE_STATUS_SHADOWS : 0;
		draw = TheThingFactory->newDrawable(build, drawableStatus);

		if (sourceObject)
		{
			if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
				draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerNightColor());
			else
				draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerColor());
		}

		// set the drawable position
		draw->setPosition(&cmdNode->m_loc);

		// set the angle in the icon we just created
		draw->setOrientation(cmdNode->m_angle);

		// set the build icon attached to the cursor to be "see-thru"
		draw->setDrawableOpacity(TheGlobalData->m_objectPlacementOpacity);

		m_constructionShadows.insert(std::make_pair(cmdNodeID, draw));
	}
}


void CommandSequence::removeConstructionShadow(UnsignedInt cmdNodeID)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);
	if (!cmdNode || cmdNode->m_cmdType != COMMAND_DOZER_CONSTRUCT)
		return;

	IDToDrawableMap::iterator it = m_constructionShadows.find(cmdNodeID);

	if (it != m_constructionShadows.end())
	{
		TheGameClient->destroyDrawable(it->second);
		m_constructionShadows.erase(it);
	}
}

Bool CommandSequence::isShawdowInTheWay(const Coord3D *worldPos, const ThingTemplate *build, Real angle, Player *thePlayer)
{
	Real myFactoryExitWidth = build->getFactoryExitWidth();
	Real myExtraWidth = build->getFactoryExtraBibWidth();

	Bool checkMyExit = false;
	Coord3D myExitPos;
	GeometryInfo myBounds = build->getTemplateGeometryInfo();
	myBounds.setMajorRadius(myBounds.getMajorRadius() + myExtraWidth);
	if (myBounds.getGeomType() != GEOMETRY_BOX) {
		myBounds.set(GEOMETRY_BOX, false, 40, myBounds.getMajorRadius(), myBounds.getMajorRadius());
	}
	else {
		myBounds.setMinorRadius(myBounds.getMinorRadius() + myExtraWidth);
	}
	GeometryInfo myGeom = build->getTemplateGeometryInfo();
	if (myGeom.getGeomType() != GEOMETRY_BOX) {
		myGeom.setMinorRadius(myGeom.getMajorRadius());
	}
	myGeom.setMajorRadius(myFactoryExitWidth / 2.0f);
	if (myFactoryExitWidth > 0) {
		myExitPos = *worldPos;
		checkMyExit = true;
		Real c = (Real)WWMath::Cos(angle);
		Real s = (Real)WWMath::Sin(angle);
		Real offset = build->getTemplateGeometryInfo().getMajorRadius() + myFactoryExitWidth / 2.0f;
		myExitPos.x += c * offset;
		myExitPos.y += s * offset;
	}

	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		CommandNode *cmdNode = it->second;

		if (!doesBuildCommandHasShadow(cmdNode))
			continue;

		Object *obj = TheGameLogic->findObjectByID(cmdNode->m_nextCommand.begin()->first);
		Bool isCurrentPlayerCommand =obj && obj->getControllingPlayer() == thePlayer;
		Drawable *draw = nullptr;

		if (!isCurrentPlayerCommand)
			continue;
		else
		{
			IDToDrawableMap::iterator dit = m_constructionShadows.find(it->first);
			draw = dit == m_constructionShadows.end() ? nullptr : dit->second;
		}

		const ThingTemplate *thing = TheThingFactory->findByTemplateID(cmdNode->m_templateID);

		if (!thing)
			continue;

		Real themFactoryExitWidth = thing->getFactoryExitWidth();
		Real hisExtraWidth = thing->getFactoryExtraBibWidth();

		Bool checkHisExit = false;
		Coord3D hisExitPos;
		GeometryInfo hisBounds = thing->getTemplateGeometryInfo();
		hisBounds.setMajorRadius(hisBounds.getMajorRadius() + hisExtraWidth);
		if (hisBounds.getGeomType() != GEOMETRY_BOX) {
			hisBounds.set(GEOMETRY_BOX, false, 40, hisBounds.getMajorRadius(), hisBounds.getMajorRadius());
		}
		else {
			hisBounds.setMinorRadius(hisBounds.getMinorRadius() + hisExtraWidth);
		}
		GeometryInfo hisGeom = thing->getTemplateGeometryInfo();
		hisGeom.setMajorRadius(themFactoryExitWidth / 2.0f);
		if (hisGeom.getGeomType() != GEOMETRY_BOX) {
			hisGeom.setMinorRadius(thing->getTemplateGeometryInfo().getMajorRadius());
		}
		if (themFactoryExitWidth > 0) {
			hisExitPos = cmdNode->m_loc;
			checkHisExit = true;
			Real c = (Real)WWMath::Cos(cmdNode->m_angle);
			Real s = (Real)WWMath::Sin(cmdNode->m_angle);
			Real offset = thing->getTemplateGeometryInfo().getMajorRadius() + themFactoryExitWidth / 2.0f;
			hisExitPos.x += c * offset;
			hisExitPos.y += s * offset;
		}
		if (ThePartitionManager->geomCollidesWithGeom(&cmdNode->m_loc, hisBounds, cmdNode->m_angle,
			worldPos, myBounds, angle)
			|| (checkMyExit && ThePartitionManager->geomCollidesWithGeom(&cmdNode->m_loc, hisBounds, cmdNode->m_angle,
				&myExitPos, myGeom, angle))
			|| (checkHisExit && ThePartitionManager->geomCollidesWithGeom(&hisExitPos, hisGeom, cmdNode->m_angle,
				worldPos, myBounds, angle))
			|| (checkMyExit && checkHisExit && ThePartitionManager->geomCollidesWithGeom(&hisExitPos, hisGeom, cmdNode->m_angle,
				&myExitPos, myGeom, angle))) {
			if (draw)
				TheTerrainVisual->addFactionBibDrawable(draw, true);
			return true;
		}
	}

	return false;
}

UnsignedInt CommandSequence::getConstructionCashWithdraw(UnsignedInt cmdNodeID)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);

	if (cmdNode && cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT)
		return cmdNode->m_cashWithdraw;

	return 0;
}

void CommandSequence::setConstructionCashWithdraw(UnsignedInt cmdNodeID, UnsignedInt cashWithdraw)
{
	CommandNode *cmdNode = findCommandByID(cmdNodeID);

	if (cmdNode && cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT)
		cmdNode->m_cashWithdraw = cashWithdraw;
}

Bool CommandSequence::doesBuildCommandHasShadow(const CommandNode *cmdNode)
{
	if (cmdNode->m_cmdType == COMMAND_DOZER_CONSTRUCT)
	{
		if (cmdNode->m_nextCommand.empty())
		{
			return false;
		}

		Object *builder = TheGameLogic->findObjectByID(cmdNode->m_nextCommand.begin()->first);
		if (!builder)
			return false;

		if (builder->getCurrentCommand() == cmdNode->m_nodeID)
		{
			CommandVerifyStatus cvs = builder->getCommandVerifyStatus();
			if (cvs != STATUS_FRESH && cvs != STATUS_EXIT_VERIFIED)
				return false;
		}

		return true;
	}

	return false;
}

UnsignedInt CommandSequence::getConstructionShadowCount(const ThingTemplate *build, const Player *player)
{
	UnsignedInt ret = 0;
	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		CommandNode *cmdNode = it->second;
		if (!doesBuildCommandHasShadow(cmdNode))
		{
			continue;
		}

		const Object *builder = TheGameLogic->findObjectByID(cmdNode->m_nextCommand.begin()->first);
		if (builder == nullptr || builder->getControllingPlayer() != player)
		{
			continue;
		}

		if (build->isEquivalentTo(TheThingFactory->findByTemplateID(cmdNode->m_templateID)))
		{
			ret++;
		}
	}

	return ret;
}

void CommandSequence::transferAttack(ObjectID fromID, ObjectID toID)
{
	for (IDToNodeMap::iterator it = m_allNodes.begin(); it != m_allNodes.end(); it++)
	{
		CommandNode *cmdNode = it->second;
		switch (cmdNode->m_cmdType)
		{
			case COMMAND_DO_GUARD_OBJECT:
			case COMMAND_DO_ATTACK_OBJECT:
			case COMMAND_DO_FORCE_ATTACK_OBJECT:
			case COMMAND_DO_SPECIAL_POWER_AT_OBJECT:
			{
				if (cmdNode->m_targetID == fromID)
					cmdNode->m_targetID = toID;
				break;
			}
			default:
				break;
		}
	}
}
