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

// CommandXlat.cpp
// Translate raw input events into tactical commands
// Author: Michael S. Booth, February 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "stdlib.h"				// VC++ wants this here, or gives compile error...

#include "Common/AudioAffect.h"
#include "Common/ActionManager.h"
#include "Common/FramePacer.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameType.h"
#include "Common/GameUtility.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MiscAudio.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
// TheSuperHackers @feature for the max rank part of the combined cheat
#include "GameLogic/RankInfo.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/StatsCollector.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"

#include "GameClient/InGameUI.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
// TheSuperHackers @feature for the health bar display mode cycle
#include "Common/OptionPreferences.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shell.h"
#include "GameClient/ControlBar.h"
#include "GameClient/SelectionInfo.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/TerrainVisual.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"


#include "Common/ThingFactory.h"
#include "GameLogic/Module/ContainModule.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/BuddyThread.h"

#include "WW3D2/ww3d.h"

#if defined(GENERALS_ONLINE)
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#endif

#if defined(RTS_DEBUG)
/*non-static*/ Real TheSkateDistOverride = 0.0f;

void countObjects(Object *obj, void *userData)
{
	Int *numObjects = (Int *)userData;

	DEBUG_LOG(("Looking at obj %d (%s) - isEffectivelyDead()==%d, isDestroyed==%d, numObjects==%d",
		obj->getID(), obj->getTemplate()->getName().str(), obj->isEffectivelyDead(), obj->isDestroyed(), *numObjects));

	if (!obj->isEffectivelyDead() && !obj->isDestroyed() && !obj->isKindOf(KINDOF_INERT))
		++(*numObjects);
}

void printObjects(Object *obj, void *userData)
{
	Bool isDead = obj->isEffectivelyDead() || obj->isDestroyed();
	Bool isInert = obj->isKindOf(KINDOF_INERT);
	AsciiString statusStr = (isDead)?"Dead":(isInert)?"Inert":"Living";

	AsciiString line;
	if (obj->getName().isEmpty())
		line.format("  Obj#%d %s - (%g,%g) %s", obj->getID(), obj->getTemplate()->getName().str(),
			obj->getPosition()->x, obj->getPosition()->y, statusStr.str());
	else
		line.format("  Obj#%d %s (%s) - (%g,%g) %s", obj->getID(), obj->getTemplate()->getName().str(),
			obj->getName().str(), obj->getPosition()->x, obj->getPosition()->y, statusStr.str());
	TheScriptEngine->AppendDebugMessage(line, FALSE);
}

#endif // defined(RTS_DEBUG)


#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)

void giveAllSciences(Player* player)
{
	// cheese festival: do NOT imitate this code. it is for debug purposes only.
	std::vector<AsciiString> v = TheScienceStore->friend_getScienceNames();
	for (size_t i = 0; i < v.size(); ++i)
	{
		ScienceType st = TheScienceStore->getScienceFromInternalName(v[i]);
		if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st))
		{
			player->grantScience(st);
		}
	}
}

// Put text on the Windows clipboard as Unicode text. Windows synthesizes the ANSI form from it
// on demand, so plain text consumers read it too. Returns FALSE when another window holds the
// clipboard or the allocation fails.
static Bool copyTextToClipboard(const AsciiString& text)
{
#ifdef _WIN32
	UnicodeString wide;
	wide.translate(text);

	if (!OpenClipboard(NULL))
		return FALSE;

	Bool copied = FALSE;
	const size_t bytes = ((size_t)wide.getLength() + 1) * sizeof(WideChar);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (hMem != NULL)
	{
		void *dst = GlobalLock(hMem);
		if (dst != NULL)
		{
			memcpy(dst, wide.str(), bytes);
			GlobalUnlock(hMem);
			// the clipboard owns the memory once SetClipboardData succeeds
			copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hMem) != NULL;
		}
		if (!copied)
			GlobalFree(hMem);
	}

	CloseClipboard();
	return copied;
#else
	return FALSE;
#endif
}

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
// TheSuperHackers @feature Grant only the sciences that belong to this player's own general.
//
// giveAllSciences above hands out every grantable science in the game, which on a general with a
// narrow tree means most of what it grants is not even his. The player template names the three
// command sets the promotion UI builds its ranks from, so walking those gives exactly the tree the
// player could actually have purchased, and nothing else.
static void giveOwnFactionSciences(Player* player)
{
	if (player == NULL)
		return;

	const PlayerTemplate* tmpl = player->getPlayerTemplate();
	if (tmpl == NULL)
		return;

	AsciiString setNames[3];
#if RTS_ZEROHOUR
	setNames[0] = tmpl->getCommandSet( PLAYER_COMMANDSET_PURCHASE_RANK1, player );
	setNames[1] = tmpl->getCommandSet( PLAYER_COMMANDSET_PURCHASE_RANK3, player );
	setNames[2] = tmpl->getCommandSet( PLAYER_COMMANDSET_PURCHASE_RANK8, player );
#else
	setNames[0] = tmpl->getPurchaseScienceCommandSetRank1();
	setNames[1] = tmpl->getPurchaseScienceCommandSetRank3();
	setNames[2] = tmpl->getPurchaseScienceCommandSetRank8();
#endif

	for (Int setIndex = 0; setIndex < 3; ++setIndex)
	{
		if (setNames[setIndex].isEmpty())
			continue;

		const CommandSet* set = TheControlBar ? TheControlBar->findCommandSet(setNames[setIndex]) : NULL;
		if (set == NULL)
			continue;

		for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
		{
			const CommandButton* button = set->getCommandButton(i);
			if (button == NULL)
				continue;

			// A purchase button can name more than one science, for the ranks that unlock a pair.
			const ScienceVec& sciences = button->getScienceVec();
			for (size_t sv = 0; sv < sciences.size(); ++sv)
			{
				ScienceType st = sciences[sv];
				if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st))
					player->grantScience(st);
			}
		}
	}
}
#endif

void objectUnderConstruction(Object* obj, void *underConstruction)
{
	if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
	{
		*(Bool*)underConstruction = true;
		return;
	}

	ProductionUpdateInterface *pui = ProductionUpdate::getProductionUpdateInterfaceFromObject(obj);
	if(pui != nullptr && pui->getProductionCount() > 0)
	{
		*(Bool*)underConstruction = true;
		return;
	}
}

Bool hasThingsInProduction(Player* player)
{
	Bool hasThingInProduction = false;
	player->iterateObjects( objectUnderConstruction, &hasThingInProduction );
	return hasThingInProduction;
}

Bool hasThingsInProduction(PlayerType playerType)
{
	for (Int n = 0; n < ThePlayerList->getPlayerCount(); ++n)
	{
		Player* player = ThePlayerList->getNthPlayer(n);
		if (player->getPlayerType() == playerType)
		{
			if (hasThingsInProduction(player))
				return true;
		}
	}
	return false;
}

#endif // defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)


bool changeMaxRenderFps(FpsValueChange change)
{
	UnsignedInt maxRenderFps = TheFramePacer->getFramesPerSecondLimit();
	maxRenderFps = RenderFpsPreset::changeFpsValue(maxRenderFps, change);

	TheFramePacer->setFramesPerSecondLimit(maxRenderFps);
	TheWritableGlobalData->m_useFpsLimit = (maxRenderFps != RenderFpsPreset::UncappedFpsValue);

	UnicodeString message;

	if (TheWritableGlobalData->m_useFpsLimit)
	{
		message = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:SetMaxRenderFps", L"Max Render FPS is %u", maxRenderFps);
	}
	else
	{
		message = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:SetUncappedRenderFps", L"Max Render FPS is uncapped");
	}

	TheInGameUI->messageNoFormat(message);

	return true;
}

bool changeLogicTimeScale(FpsValueChange change)
{
	if (TheNetwork != nullptr)
		return false;

	const UnsignedInt maxRenderFps = TheFramePacer->getFramesPerSecondLimit();
	UnsignedInt maxRenderRemainder = LogicTimeScaleFpsPreset::StepFpsValue;
	maxRenderRemainder -= maxRenderFps % LogicTimeScaleFpsPreset::StepFpsValue;
	maxRenderRemainder %= LogicTimeScaleFpsPreset::StepFpsValue;

	UnsignedInt logicTimeScaleFps = TheFramePacer->getLogicTimeScaleFps();
	// Set the value to the max render fps value plus a bit when time scale is
	// disabled. This ensures that the time scale does not re-enable with a
	// 'surprise' value.
	if (!TheFramePacer->isLogicTimeScaleEnabled())
	{
		logicTimeScaleFps = maxRenderFps + maxRenderRemainder;
	}
	// Ceil the value at the max render fps value plus a bit so that the next fps
	// value decrease would undercut the max render fps at the correct step value.
	// Example: render fps 72 -> logic value ceiled to 75 -> decreased to 70.
	logicTimeScaleFps = min(logicTimeScaleFps, maxRenderFps + maxRenderRemainder);
	logicTimeScaleFps = LogicTimeScaleFpsPreset::changeFpsValue(logicTimeScaleFps, change);

	// Set value before potentially disabling it.
	if (TheFramePacer->isLogicTimeScaleEnabled())
	{
		TheFramePacer->setLogicTimeScaleFps(logicTimeScaleFps);
	}

	TheFramePacer->enableLogicTimeScale(logicTimeScaleFps < maxRenderFps);

	// Set value after potentially enabling it.
	if (TheFramePacer->isLogicTimeScaleEnabled())
	{
		TheFramePacer->setLogicTimeScaleFps(logicTimeScaleFps);
	}

	logicTimeScaleFps = TheFramePacer->getLogicTimeScaleFps();
	const UnsignedInt actualLogicTimeScaleFps = TheFramePacer->getActualLogicTimeScaleFps();
	const Real actualLogicTimeScaleRatio = TheFramePacer->getActualLogicTimeScaleRatio();

	UnicodeString message;

	if (TheFramePacer->isLogicTimeScaleEnabled())
	{
		message = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:SetLogicTimeScaleFps", L"Logic Time Scale FPS is %u (actual %u, ratio %.02f)",
			logicTimeScaleFps, actualLogicTimeScaleFps, actualLogicTimeScaleRatio);
	}
	else
	{
		message = TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:SetUncappedLogicTimeScaleFps", L"Logic Time Scale FPS is uncapped (actual %u, ratio %.02f)",
			actualLogicTimeScaleFps, actualLogicTimeScaleRatio);
	}

	TheInGameUI->messageNoFormat(message);

	return true;
}


static Bool isSystemMessage( const GameMessage *msg );

enum{ DROPPED_MAX_PARTICLE_COUNT = 1000};

static Bool canSelectionSalvage( const Object *targetObj)
{
	if (!targetObj) {
		return FALSE;
	}

	if (!targetObj->isSalvageCrate()) {
		return FALSE;
	}

	const DrawableList *drawList = TheInGameUI->getAllSelectedDrawables();

	for (DrawableListCIt cit = drawList->begin(); cit != drawList->end(); ++cit)
	{
		Drawable *draw = *cit;
		if (!draw)
		{
			continue;
		}


		Object *obj = draw->getObject();
		if (!obj)
		{
			continue;
		}

		if (obj->isKindOf(KINDOF_SALVAGER)) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static CanAttackResult canObjectForceAttack( Object *obj, const Object *victim, const Coord3D *pos )
{
	CanAttackResult result;
	result = obj->isAbleToAttack() ? ATTACKRESULT_POSSIBLE : ATTACKRESULT_NOT_POSSIBLE;
	if( result == ATTACKRESULT_NOT_POSSIBLE )
	{
		return ATTACKRESULT_NOT_POSSIBLE;
	}

	if (victim)
	{
		result = obj->getAbleToAttackSpecificObject(ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );

		//Special case -- objects with spawn weapons have to do different checks. Stinger site with stinger soldiers is
		//the catalyst example.
    if ( obj->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
    {
      if( result != ATTACKRESULT_POSSIBLE && result != ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		  {
			  SpawnBehaviorInterface *spawnInterface = obj->getSpawnBehaviorInterface();
			  if( spawnInterface )
			  {
				  //We found the spawn interface, now get the closest slave to the target.
				  Object *slave = spawnInterface->getClosestSlave( victim->getPosition() );
				  if( slave )
				  {
					  result = slave->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );
				  }
			  }
		  }
      else // oh dear me. The weird case of a garrisoncontainer being a KINDOF_SPAWNS_ARE_THE_WEAPONS... the AmericaBuildingFirebase
      {
        ContainModuleInterface *contain = obj->getContain();
        if ( contain )
        {
          Object *rider = contain->getClosestRider( victim->getPosition() );
          if ( rider )
          {
            result = rider->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );
            if( result != ATTACKRESULT_NOT_POSSIBLE )
              return result;
          }
        }
      }

    }


    return result;
	}
	else
	{
		//Almost every combat unit can force attack a position. The exceptions include stationary units
		//that try to force attack in a location beyond their reach (range, LOS, etc).
		if( pos )
		{
			Object *testObj = obj;
			if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
			{
				SpawnBehaviorInterface *spawnInterface = obj->getSpawnBehaviorInterface();
				if( spawnInterface )
				{
					//We found the spawn interface, now get the closest slave to the target.
					Object *slave = spawnInterface->getClosestSlave( pos );
					if( slave )
					{
						testObj = slave;
					}
				}
        else
        {
    			result = obj->getAbleToUseWeaponAgainstTarget( ATTACK_NEW_TARGET, nullptr, pos, CMD_FROM_PLAYER );
          if( result != ATTACKRESULT_POSSIBLE ) // oh dear me. The weird case of a garrisoncontainer being a KINDOF_SPAWNS_ARE_THE_WEAPONS... the AmericaBuildingFirebase
          {
            ContainModuleInterface *contain = obj->getContain();
            if ( contain )
            {
              Object *rider = contain->getClosestRider( pos );
              if ( rider )
                testObj = rider;
            }
          }
        }
			}
			//Now evaluate the testObj again to see if it is capable of force attacking the pos.
			result = testObj->getAbleToUseWeaponAgainstTarget( ATTACK_NEW_TARGET, nullptr, pos, CMD_FROM_PLAYER );
			return result;
		}
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

//-------------------------------------------------------------------------------------------------
static CanAttackResult canAnyForceAttack(const DrawableList *allSelected, const Object *victim, const Coord3D *pos )
{
	// TheSuperHackers @bugfix WebbontheWeb 06/10/2026 Checks all selected units instead of just first one,
	// so a unit without a weapon doesn't prevent the entire group from force attacking.
	CanAttackResult bestResult = ATTACKRESULT_NOT_POSSIBLE;

	// check to make sure that allSelected can attack obj.
	for (DrawableListCIt cit = allSelected->begin(); cit != allSelected->end(); ++cit)
	{
		Drawable *draw = *cit;
		if (!draw)
		{
			continue;
		}

		Object *obj = draw->getObject();
		if (!obj)
		{
			continue;
		}

		const CanAttackResult result = canObjectForceAttack( obj, victim, pos );
		if (result > bestResult)
		{
			bestResult = result;
			if (bestResult == ATTACKRESULT_POSSIBLE)
			{
				break;
			}
		}
	}

	return bestResult;
}

//-------------------------------------------------------------------------------------------------
void pickAndPlayUnitVoiceResponse( const DrawableList *list, GameMessage::Type msgType, PickAndPlayInfo *info )
{
	if (!list)
	{
		return;
	}

	const AudioEventRTS* soundToPlayPtr = nullptr;

	Object *objectWithSound = nullptr;
	Bool skip = false;

	Object *target = nullptr;
	if( info && info->m_drawTarget )
	{
		target = info->m_drawTarget->getObject();
	}

	//Now, loop through all the drawables (even if you find a match on the first.
	//The innards are responsible for "upgrading" a sound that is played based on
	//priorities. For example, the voice move or voice crush. Voice move gets set
	//when we have a null event -- but if any of the units can crush the specified
	//target, then it changes to VoiceCrush.
	for( DrawableListCIt it = list->begin(); it != list->end(); ++it )
	{
		Object *obj = (*it)->getObject();

		if (obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
			continue;

		// Use the object instead of the drawable to get the thing template from. This way, we get the
		// sounds even if the thing is disguised as something else. (Ala bomb truck.)
		const ThingTemplate *templ = obj->getTemplate();
		if (!templ)
		{
			return;
		}
		// TheSuperHackers @feature With a cameo focused, only its units answer a command; a
		// plain order still goes to the whole selection, so they all answer that
		if (TheControlBar && !TheControlBar->isSmartSelectionFocused(obj))
		{
			switch (msgType)
			{
				// selecting is not a command, so the whole group still speaks
				case GameMessage::MSG_SELECT_TEAM0:
				case GameMessage::MSG_SELECT_TEAM1:
				case GameMessage::MSG_SELECT_TEAM2:
				case GameMessage::MSG_SELECT_TEAM3:
				case GameMessage::MSG_SELECT_TEAM4:
				case GameMessage::MSG_SELECT_TEAM5:
				case GameMessage::MSG_SELECT_TEAM6:
				case GameMessage::MSG_SELECT_TEAM7:
				case GameMessage::MSG_SELECT_TEAM8:
				case GameMessage::MSG_SELECT_TEAM9:
				case GameMessage::MSG_CREATE_SELECTED_GROUP:
					break;
				default:
					if (!GameMessage::isPlainOrder(msgType))
					{
						continue;
					}
					break;
			}
		}

		switch (msgType)
		{
			case GameMessage::MSG_DOCK:
				soundToPlayPtr = templ->getPerUnitSound("VoiceSupply");
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_SELECT_TEAM0:
			case GameMessage::MSG_SELECT_TEAM1:
			case GameMessage::MSG_SELECT_TEAM2:
			case GameMessage::MSG_SELECT_TEAM3:
			case GameMessage::MSG_SELECT_TEAM4:
			case GameMessage::MSG_SELECT_TEAM5:
			case GameMessage::MSG_SELECT_TEAM6:
			case GameMessage::MSG_SELECT_TEAM7:
			case GameMessage::MSG_SELECT_TEAM8:
			case GameMessage::MSG_SELECT_TEAM9:
			case GameMessage::MSG_CREATE_SELECTED_GROUP:
				soundToPlayPtr = templ->getVoiceSelect();
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_EVACUATE:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceUnload" );
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_DO_REPAIR:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceRepair" );
				objectWithSound = obj;
				skip = true;
				break;
#ifdef ALLOW_SURRENDER
			case GameMessage::MSG_PICK_UP_PRISONER:
				soundToPlayPtr = templ->getPerUnitSound( "VoicePickup" );
				objectWithSound = obj;
				skip = true;
				break;
#endif
			case GameMessage::MSG_COMBATDROP_AT_LOCATION:
			case GameMessage::MSG_COMBATDROP_AT_OBJECT:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceCombatDrop" );
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_ENTER:
				if( target && target->isKindOf( KINDOF_HEAL_PAD ) )
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceGetHealed" );
				}
				else if( target && target->isKindOf( KINDOF_STRUCTURE ) )
				{
					if( obj->getRelationship( target ) == ENEMIES )
					{
						//Saboteurs
						soundToPlayPtr = templ->getPerUnitSound( "VoiceEnterHostile" );
					}
					else
					{
						soundToPlayPtr = templ->getPerUnitSound( "VoiceGarrison" );
					}
				}
				// order matters: we want to know if I consider it to be an ally, not vice versa
				else if( target && obj->getRelationship(target) != ALLIES )
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceEnterHostile" );
				}
				else
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceEnter" );
				}
				objectWithSound = obj;
				skip = true;
				break;

			case GameMessage::MSG_DO_MOVETO:
			case GameMessage::MSG_DO_ATTACKMOVETO:
			case GameMessage::MSG_GET_REPAIRED:
			case GameMessage::MSG_GET_HEALED:
			case GameMessage::MSG_DO_SALVAGE:
			{
				AIUpdateInterface *ai = obj->getAI();
				if( ai )
				{
					//This flag determines if the object has started moving yet... if not
					//it's a good initial check.
					Bool isEffectivelyMoving = ai->isMoving() || ai->isWaitingForPath();

					if( TheInGameUI->isInWaypointMode() )
					{
						if( isEffectivelyMoving )
						{
							//Don't want to play the sound unless he's not moving!
							continue;
						}
					}

					//Default to voice move (it'll be selected if we can't find a crush case as we iterate
					//through the rest of the drawables)
					if( !soundToPlayPtr )
					{
						soundToPlayPtr = templ->getVoiceMove();
						objectWithSound = obj;
					}
					if( TheInGameUI->isInForceMoveToMode() && target )
					{
						if( obj->canCrushOrSquish( target ) )
						{
							//Change it to voice crush because we are intentionally trying to crush this!
							soundToPlayPtr = templ->getPerUnitSound( "VoiceCrush" );
							objectWithSound = obj;
							skip = true;
						}
					}
					if (msgType == GameMessage::MSG_DO_SALVAGE)
					{
						const AudioEventRTS *tempSound = templ->getPerUnitSound( "VoiceSalvage" );
						if (TheAudio->isValidAudioEvent(tempSound))
						{
							soundToPlayPtr = tempSound;
							objectWithSound = obj;
							skip = true;
						}
					}
					// Special case for GLA worker to use a different set of move voices when he has received the worker shoes upgrade
					Player *player = obj->getControllingPlayer();
					static const UpgradeTemplate *workerShoeTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_GLAWorkerShoes" );
					if (player && workerShoeTemplate && player->hasUpgradeComplete(workerShoeTemplate))
					{
						if (obj->isKindOf(KINDOF_INFANTRY) && obj->isKindOf(KINDOF_DOZER) && obj->isKindOf(KINDOF_HARVESTER)) // Only Workers fit all 3
						{
							soundToPlayPtr = templ->getPerUnitSound( "VoiceMoveUpgraded" );
							objectWithSound = obj;
							skip = true;
						}
					}
				}
				break;
			}

			case GameMessage::MSG_RESUME_CONSTRUCTION:
			case GameMessage::MSG_DOZER_CONSTRUCT:
			case GameMessage::MSG_DOZER_CONSTRUCT_LINE:
			{
				soundToPlayPtr = templ->getPerUnitSound( "VoiceBuildResponse" );
				objectWithSound = obj;
				skip = true;

				break;
			}

			case GameMessage::MSG_SWITCH_WEAPONS:
			{
				if( info && info->m_weaponSlot )
				{
					switch( *info->m_weaponSlot )
					{
						case PRIMARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoicePrimaryWeaponMode" );
							break;
						case SECONDARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceSecondaryWeaponMode" );
							break;
						case TERTIARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceTertiaryWeaponMode" );
							break;
						case WEAPON_FOUR:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceWeaponModeFour" );
							break;
						case WEAPON_FIVE:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceWeaponModeFive" );
							break;
						case WEAPON_SIX:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceWeaponModeSix" );
							break;
						case WEAPON_SEVEN:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceWeaponModeSeven" );
							break;
						case WEAPON_EIGHT:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceWeaponModeEight" );
							break;
					}
					objectWithSound = obj;
					skip = true;
				}
				break;
			}

			case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
			{
				soundToPlayPtr = templ->getPerUnitSound( "VoiceBombard" );
				objectWithSound = obj;
				skip = true;

				if (TheAudio->isValidAudioEvent(soundToPlayPtr)) {
					break;
				} else {
					// clear out the sound to play, and drop into the attack object logic.
					soundToPlayPtr = nullptr;
					FALLTHROUGH;
				}
			}
			case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
			case GameMessage::MSG_DO_ATTACK_OBJECT:
			case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
			{
				if( !soundToPlayPtr )
				{
					//Low priority sounds -- only do this if uninitialized.
					if( info && info->m_air )
						soundToPlayPtr = templ->getVoiceAttackAir();
					else
						soundToPlayPtr = templ->getVoiceAttack();

					objectWithSound = obj;
				}

				//Look for a specialty weapon fired via command button!
				Bool specialtyWeapon = FALSE;
				if( msgType == GameMessage::MSG_DO_WEAPON_AT_OBJECT )
				{
					specialtyWeapon = TRUE;
				}

				Weapon *weapon = obj->getCurrentWeapon();
				if( info && info->m_weaponSlot )
				{
					weapon = obj->getWeaponInWeaponSlot( *info->m_weaponSlot );
				}
				if( weapon )
				{
					switch( weapon->getDamageType() )
					{
// this stays, even if ALLOW_SURRENDER is not defed, since flashbangs still use 'em
						case DAMAGE_SURRENDER:
							if( target && target->isKindOf( KINDOF_STRUCTURE ) )
							{
								//We are attempting to take over a building with rangers and flashbangs!
								soundToPlayPtr = templ->getPerUnitSound( "VoiceClearBuilding" );
							}
							else
							{
								//Special for subdue attacks.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceSubdue" );
							}
							objectWithSound = obj;
							skip = true;
							break;
						case DAMAGE_DISARM:
							//Special for mine clearing attacks.
							soundToPlayPtr = templ->getPerUnitSound( "VoiceDisarm" );
							objectWithSound = obj;
							skip = true;
							break;
						case DAMAGE_KILLPILOT:
							if( specialtyWeapon )
							{
								//Special for sniping vehicle pilots.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceSnipePilot" );
								objectWithSound = obj;
								skip = true;
							}
							break;
						case DAMAGE_MELEE:
							if( specialtyWeapon )
							{
								//Special for stabbing
								soundToPlayPtr = templ->getPerUnitSound( "VoiceMelee" );
								objectWithSound = obj;
								skip = true;
							}
							break;
					}
				}
				break;
			}

			case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
			{

				if( !soundToPlayPtr )
				{
					//Low priority sounds -- only do this if uninitialized.
					if( info && info->m_air )
						soundToPlayPtr = templ->getVoiceAttackAir();
					else
						soundToPlayPtr = templ->getVoiceAttack();
					objectWithSound = obj;
				}

				//Check for possibility of a higher priority sound!
				Weapon *weapon = obj->getCurrentWeapon();
				if( info && info->m_weaponSlot )
				{
					weapon = obj->getWeaponInWeaponSlot( *info->m_weaponSlot );
				}
				if( weapon )
				{
					switch( weapon->getDamageType() )
					{
// this stays, even if ALLOW_SURRENDER is not defed, since flashbangs still use 'em
						case DAMAGE_SURRENDER:
							break;
						case DAMAGE_DISARM:
							//Special for mine clearing attacks.
							soundToPlayPtr = templ->getPerUnitSound( "VoiceDisarm" );
							objectWithSound = obj;
							break;
						//these are specific to the guicommand based ground attacks, the toxin sprinkler and the firestorm wall thing
						//hence the additional check for it being a non-primary weapon
 						case DAMAGE_FLAME:
							if (weapon->getWeaponSlot() != PRIMARY_WEAPON)
							{
 								soundToPlayPtr = templ->getPerUnitSound( "VoiceFlameLocation" );
 								objectWithSound = obj;
							}
 							break;
 						case DAMAGE_POISON:
							if (weapon->getWeaponSlot() != PRIMARY_WEAPON)
							{
 								soundToPlayPtr = templ->getPerUnitSound( "VoicePoisonLocation" );
 								objectWithSound = obj;
							}
 							break;
						default:
							if( !weapon->getName().compare( "ComancheRocketPodWeapon" ) )
							{
								//Special case for comanche rocket pods.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceFireRocketPods" );
								objectWithSound = obj;
								skip = true;
							}
					}
				}
				break;
			}
			case GameMessage::MSG_DO_GUARD_POSITION:
			case GameMessage::MSG_DO_GUARD_OBJECT:
				soundToPlayPtr = templ->getVoiceGuard();
				objectWithSound = obj;
				skip = true;
				break;

			case GameMessage::MSG_DO_SPECIAL_POWER:
			case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
			case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
			{
				if( info && info->m_specialPowerType != SPECIAL_INVALID )
				{
					SpecialPowerModuleInterface *spmInterface = obj->findSpecialPowerModuleInterface( info->m_specialPowerType );
					if( spmInterface )
					{
						objectWithSound = obj;
						soundToPlayPtr = &spmInterface->getInitiateSound();
						skip = TRUE;
					}
				}
				break;
			}

			case GameMessage::MSG_INTERNET_HACK:
				objectWithSound = obj;
				soundToPlayPtr = templ->getPerUnitSound( "VoiceHackInternet" );
				skip = TRUE;
				break;

			default:
				DEBUG_LOG(("Requested to add voice of message type %d, but don't know how - jkmcd", msgType));
				break;
		}
		if( skip )
		{
			//The unit sound doesn't have any sort of priority or special case, so simply play the first one that comes along.
			break;
		}

	}




	if (!soundToPlayPtr)
		return;

	AudioEventRTS soundToPlay = *soundToPlayPtr;

	// to prevent voice stepping.
	if( objectWithSound )
	{
		soundToPlay.setObjectID( objectWithSound->getID() );
		TheAudio->addAudioEvent(&soundToPlay);

		// This seems really hacky, and MarkL admits that it is. However, we do this so that we
		// can "randomly" pick a different sound the next time, if we have 3 or more sounds. - jkmcd
		soundToPlayPtr->setPlayingAudioIndex( soundToPlay.getPlayingAudioIndex() );

		if( objectWithSound->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
		{
			//Additional sounds for terrorists in cars.
			switch (msgType)
			{
				case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
				case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
				case GameMessage::MSG_DO_ATTACK_OBJECT:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarAttackVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;

				case GameMessage::MSG_DO_MOVETO:
				case GameMessage::MSG_DO_ATTACKMOVETO:
				case GameMessage::MSG_GET_REPAIRED:
				case GameMessage::MSG_GET_HEALED:
				case GameMessage::MSG_DO_SALVAGE:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarMoveVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;

				case GameMessage::MSG_SELECT_TEAM0:
				case GameMessage::MSG_SELECT_TEAM1:
				case GameMessage::MSG_SELECT_TEAM2:
				case GameMessage::MSG_SELECT_TEAM3:
				case GameMessage::MSG_SELECT_TEAM4:
				case GameMessage::MSG_SELECT_TEAM5:
				case GameMessage::MSG_SELECT_TEAM6:
				case GameMessage::MSG_SELECT_TEAM7:
				case GameMessage::MSG_SELECT_TEAM8:
				case GameMessage::MSG_SELECT_TEAM9:
				case GameMessage::MSG_CREATE_SELECTED_GROUP:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarSelectVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;
			}
		}
	}
}


//------------------------------------------------------------------------------------
/**
 * Find a suitable command center to view.
 */

struct CommandCenterLocator
{
	Bool atLeastOne;
	Bool isCommandCenter;
	Int val;
	Coord3D loc;

	CommandCenterLocator() : atLeastOne(false), isCommandCenter(false), val(-1)
	{
		loc.zero();
	}
};

void findCommandCenterOrMostExpensiveBuilding(Object* obj, void* vccl)
{
	CommandCenterLocator *ccl = (CommandCenterLocator*) vccl;

	// here's the deal. We want to get the first Command Center in the list.
	// Barring that, we want the most expensive structure we currently own.
	if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
		ccl->isCommandCenter = true;
		ccl->loc = *obj->getPosition();
	} else if (!ccl->isCommandCenter) {
		if (!obj->isKindOf(KINDOF_STRUCTURE)) {
			return;
		}

		Int costToBuild = obj->getTemplate()->calcCostToBuild(obj->getControllingPlayer());
		if (costToBuild > ccl->val) {
			ccl->val = costToBuild;
			ccl->loc = *obj->getPosition();
		}
	}

	ccl->atLeastOne = true;
}

static void viewCommandCenter()
{
	Player* localPlayer = rts::getObservedOrLocalPlayer();
	if (!localPlayer->isPlayerActive())
		return;

	CommandCenterLocator ccl;
	localPlayer->iterateObjects(findCommandCenterOrMostExpensiveBuilding, &ccl);

	if (ccl.atLeastOne) {
		TheTacticalView->userLookAt(&ccl.loc);
	} else {
		// @todo. Find their starting position and look at that instead?
	}
}



//----------------- Select and View Hero -----------------------------------

struct HeroHolder
{
	Object *hero;
};

void amIAHero(Object* obj, void* heroHolder)
{
	if (((HeroHolder*)heroHolder)->hero != nullptr)
	{
		return;
	}

	if (obj->isKindOf( KINDOF_HERO ))
	{
		((HeroHolder*)heroHolder)->hero = obj;
	}
}



static Object *iNeedAHero()
{
	Player* localPlayer = rts::getObservedOrLocalPlayer();
	if (!localPlayer->isPlayerActive())
		return nullptr;

	HeroHolder heroHolder;
	heroHolder.hero = nullptr;

	localPlayer->iterateObjects(amIAHero, (void*)&heroHolder);

	return heroHolder.hero;

}

//------------------------------------------------------------------------------------
/**
 * Create DO_MOVE_TO messages for each selected object, instructing it to move to the given location.
 */
GameMessage::Type CommandTranslator::issueMoveToLocationCommand( const Coord3D *pos, Drawable *drawableInWay,
																																 CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;
	Object *obj = drawableInWay ? drawableInWay->getObject() : nullptr;

	Bool isForceAttackable = FALSE;
	if (obj) {
		isForceAttackable = obj->isKindOf(KINDOF_FORCEATTACKABLE);
	}

	if (m_teamExists)
	{
		//ShigureUi 01/10/2026 we override existing waypoint message
		//if( TheInGameUI->isInWaypointMode() )
		//{
		//msgType = GameMessage::MSG_ADD_WAYPOINT;
		//}
		//else

		if( TheInGameUI->isInAttackMoveToMode())
		{
			msgType = GameMessage::MSG_DO_ATTACKMOVETO;
		}
		else if( TheInGameUI->isInReverseMoveToMode() )
		{
			msgType = GameMessage::MSG_DO_REVERSE_MOVETO;
		}
		else if( TheInGameUI->isInForceMoveToMode() )
		{
			msgType = GameMessage::MSG_DO_FORCEMOVETO;
		}
		else if( TheInGameUI->isInForceAttackMode() && isForceAttackable )
		{
			msgType = GameMessage::MSG_DO_ATTACK_OBJECT;
		}
		else
		{
			msgType = GameMessage::MSG_DO_MOVETO;
		}
		if( commandType == DO_COMMAND )
		{
			GameMessage *movemsg = TheMessageStream->appendMessage( msgType );
			if (msgType == GameMessage::MSG_DO_ATTACK_OBJECT)
			{
				movemsg->appendObjectIDArgument(obj->getID());
				movemsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}
			else
			{
				movemsg->appendLocationArgument(*pos);
				movemsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}

		}
	}

	// only make sounds if we really did the command messages
	if( commandType == DO_COMMAND )
	{
		PickAndPlayInfo info;
		info.m_drawTarget = drawableInWay;
		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_MOVETO, &info );
	}

	if(TheStatsCollector)
		TheStatsCollector->incrementMoveCount();

	// return the actual msg type used
	return msgType;

}

//------------------------------------------------------------------------------------
// TheSuperHackers @feature Quick cast for the reverse move key (Options.ini: CastMode).
/** Reverse the selection to the terrain under the cursor, the order a click there gives in
	* reverse move mode. It always goes to the ground, never to a unit under the cursor, since the
	* key means "back up to here".
	*
	* Returns FALSE, leaving the armed mode alone, when the cursor is not over the battlefield or
	* nothing selected can take the order, so the caller can arm it for a click instead. */
Bool CommandTranslator::quickCastReverseMove()
{
	if( !m_teamExists || !TheInGameUI->areSelectedObjectsControllable() )
		return FALSE;

	if( TheMouse == nullptr || TheTacticalView == nullptr )
		return FALSE;

	const MouseIO *mouseIO = TheMouse->getMouseStatus();
	if( mouseIO == nullptr )
		return FALSE;

	// the cursor has to be over the battlefield, not the command bar or another panel
	ICoord2D cursor = mouseIO->pos;
	if( TheWindowManager && TheWindowManager->getWindowUnderCursor( cursor.x, cursor.y ) != nullptr )
		return FALSE;

	Coord3D pos;
	if( !TheTacticalView->screenToTerrain( &cursor, &pos ) )
		return FALSE;

	// issueMoveToLocationCommand reads the armed mode to pick the order
	if( !TheInGameUI->isInReverseMoveToMode() )
		TheInGameUI->toggleReverseMoveToMode();

	issueMoveToLocationCommand( &pos, nullptr, DO_COMMAND );
	TheInGameUI->clearArmedMoveMode();
	return TRUE;
}

//------------------------------------------------------------------------------------
/// @todo Play attack command response sound on client-side to hide latency
GameMessage::Type CommandTranslator::createAttackMessage( Drawable *draw,
																													Drawable *other,
																													CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	// the drawable must have an object to be able to attack
	if( draw->getObject() == nullptr )
		return msgType;

	// the target must have an object to be attacked
	if( other->getObject() == nullptr )
		return msgType;

	// insert object attack command message into stream
	msgType = GameMessage::MSG_DO_ATTACK_OBJECT;

	// only make the message if we are really doing a command
	if( commandType == DO_COMMAND )
	{
		GameMessage *attackmsg = TheMessageStream->appendMessage( msgType );

		attackmsg->appendObjectIDArgument( other->getObject()->getID() );	// must pass object IDs to logic
		attackmsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

	}

	// return the message type created
	return msgType;

}

//------------------------------------------------------------------------------------
/**
 * Create DO_ATTACK_GROUND_OBJECT messages for each selected object, instructing it to attack the given enemy.
 * Return TRUE if any attacks actually occurred.
 */
GameMessage::Type CommandTranslator::issueAttackCommand( Drawable *target,
																												 CommandEvaluateType commandType,
																												 GUICommandType command )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if (target == nullptr)
		return msgType;

	// you cannot attack an enemy that has no object representation
	Object *targetObj = target->getObject();
	if( !targetObj )
		return msgType;

	if( m_teamExists )
	{

		//DEBUG_LOG(("issuing team-attack cmd against %s",enemy->getTemplate()->getName().str()));

		// insert team attack command message into stream
		switch( command )
		{
#ifdef ALLOW_SURRENDER
			case GUICOMMANDMODE_PICK_UP_PRISONER:
				msgType = GameMessage::MSG_PICK_UP_PRISONER;
				break;
#endif
			case GUI_COMMAND_NONE:
				msgType = GameMessage::MSG_DO_ATTACK_OBJECT;
				break;
			default:
				DEBUG_CRASH( ("issueAttackCommand was passed in a GUICommandType type that isn't supported yet...") );
				return msgType;
		}

		// only create the message if our command type is DO_COMMAND
		if( commandType == DO_COMMAND )
		{
			GameMessage *attackMsg;

			attackMsg = TheMessageStream->appendMessage( msgType );

			attackMsg->appendObjectIDArgument( targetObj->getID() );	// must pass target object ID to logic
			attackMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			// if we have a stats collector, increment the stats
			if(TheStatsCollector)
				TheStatsCollector->incrementAttackCount();
		}
	}
	else
	{
		DEBUG_LOG(("issuing NON-team-attack cmd against %s",target->getTemplate()->getName().str()));

		// send single attack command for selected drawable
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

		// loop through all the selected drawables
		Drawable *draw;
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			draw = *it;
			msgType = createAttackMessage(draw, target, commandType );
		}
	}

	// only make sounds if the command was for real
	if( commandType == DO_COMMAND )
	{
		PickAndPlayInfo info;
		info.m_air = targetObj->isUsingAirborneLocomotor();
		info.m_drawTarget = target;
		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );
	}

	// return the actual message type created
	return msgType;

}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueSpecialPowerCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos, Object* ignoreSelObj )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( !command || !command->getSpecialPowerTemplate())
	{
		return msgType;
	}

	Drawable* sourceDraw = ignoreSelObj ? ignoreSelObj->getDrawable() : TheInGameUI->getFirstSelectedDrawable();
	ObjectID specificSource = ignoreSelObj ? ignoreSelObj->getID() : INVALID_ID;

	if( BitIsSet( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{
		// OBJECT BASED SPECIAL
		if (!command->isValidObjectTarget(sourceDraw, target))
			return msgType;

		msgType = GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendObjectIDArgument( target->getObject()->getID() );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );
			msg->appendBooleanArgument(specificSource ? false : TheInGameUI->isInWaypointMode());

			// say   something like " I think I'll put some dynamite on that there tank."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		}
	}
	else if( BitIsSet( command->getOptions(), NEED_N_TARGET_POS ) )
	{
		//N LOCATION BASED SPECIAL (chronosphere = 2; count set by NumberOfTargets)
		msgType = GameMessage::MSG_DO_SPECIAL_POWER_AT_MULTIPLE_LOCATIONS;
		if( commandType == DO_COMMAND )
		{
			const SpecialPowerTargetRadiusMode radiusMode = command->getTargetRadiusMode();

			// RADIUS_ANCHORED_AREA: the very first click is an anchor that only defines the constraint
			// area; it is captured but never delivered as a target.
			if( radiusMode == SPTRM_ANCHORED_AREA && !TheInGameUI->hasSpecialPowerAreaAnchor() )
			{
				TheInGameUI->setSpecialPowerAreaAnchor( pos );
				msgType = GameMessage::MSG_INVALID;
			}
			else
			{
				// Clamp the pick into the active constraint area (anchor / first / previous target), then
				// always accept it - out-of-area picks land on the boundary instead of being rejected.
				Coord3D tpos = *pos;
				TheInGameUI->clampToSpecialPowerTargetArea( command, tpos );

				{
					TheInGameUI->addPendingSpecialPowerLocation( &tpos );

					if( TheInGameUI->getPendingSpecialPowerLocationCount() < command->getNumberOfTargets() )
					{
						// Still collecting points - commit nothing yet, stay pending.
						msgType = GameMessage::MSG_INVALID;
					}
					else
					{
						// Final click: emit ONE deterministic message carrying all target points (the
						// anchor, if any, is not sent).
						const std::vector<Coord3D>& targets = TheInGameUI->getPendingSpecialPowerLocations();
						GameMessage *msg = TheMessageStream->appendMessage( msgType );
						msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
						msg->appendIntegerArgument( (Int)targets.size() );
						for( std::vector<Coord3D>::const_iterator it = targets.begin(); it != targets.end(); ++it )
							msg->appendLocationArgument( *it );
						msg->appendIntegerArgument( command->getOptions() );
						msg->appendObjectIDArgument( specificSource );
						TheInGameUI->clearPendingSpecialPowerLocations();

						PickAndPlayInfo info;
						info.m_drawTarget = target;
						info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
						pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );
					}
				}
			}
		}
	}
	else if( BitIsSet( command->getOptions(), NEED_TARGET_POS ) )
	{
		//LOCATION BASED SPECIAL
		msgType = GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendLocationArgument( *pos );
#if !(RTS_GENERALS && RETAIL_COMPATIBLE_NETWORKING)
			msg->appendRealArgument( INVALID_ANGLE ); //We don't use the angle (unless we're using a construction special in PlaceEventTranslator).
#endif
			//Object in way.... some specials care, others don't
			ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			msg->appendObjectIDArgument( targetID );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );
			msg->appendBooleanArgument(specificSource ? false : TheInGameUI->isInWaypointMode());

			// say   something like " I think I'll put a timed charge on the ground, here."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		}
	}
	else
	{
		//NO TARGET SPECIAL
		msgType = GameMessage::MSG_DO_SPECIAL_POWER;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );
			msg->appendBooleanArgument(specificSource ? false : TheInGameUI->isInWaypointMode());

			// say   something like " I think I'll set down my laptop and hack some cash from a bank in the Cayman Islands."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		}
	}

	// N-point (chronosphere) powers commit atomically on the final click and don't use the
	// fire-then-steer overridable-destination model, so skip the shortcut auto-select/steer block -
	// it would deselect/select the firing object, which clears the pending point state and
	// makes the remaining clicks impossible.
	if( command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT && commandType == DO_COMMAND
			&& !BitIsSet( command->getOptions(), NEED_N_TARGET_POS ) )
	{
		Object *obj = sourceDraw->getObject();
		SpecialPowerUpdateInterface *spUpdate = obj->findSpecialPowerWithOverridableDestination();
		if( spUpdate )
		{
			//Deselect the drawables before posting the selection message.
			TheInGameUI->deselectAllDrawables();

			//Because we just launched a special power via shortcut, and the special power accepts input
			//from the player (particle uplink cannon, spectre gunship), simply select the object now.
			//--------------------
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
			// creating a new team so pass in true
			teamMsg->appendBooleanArgument( TRUE );
			teamMsg->appendObjectIDArgument( obj->getID() );

			TheInGameUI->selectDrawable( obj->getDrawable() );
		}
	}



	return msgType;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueCombatDropCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos )
{
	if( !command )
	{
		return GameMessage::MSG_INVALID;
	}

	if( target != nullptr && BitIsSet( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{

		// OBJECT BASED SPECIAL
		if (!command->isValidObjectTarget(TheInGameUI->getFirstSelectedDrawable(), target))
			return GameMessage::MSG_INVALID;

		GameMessage::Type msgType = GameMessage::MSG_COMBATDROP_AT_OBJECT;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg = TheMessageStream->appendMessage( msgType );
			ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			msg->appendObjectIDArgument( targetID );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_COMBATDROP_AT_OBJECT );
		}
		return msgType;
	}
	else if ( BitIsSet( command->getOptions(), NEED_TARGET_POS ) )
	{
		GameMessage::Type msgType = GameMessage::MSG_COMBATDROP_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg = TheMessageStream->appendMessage( msgType );
			msg->appendLocationArgument( *pos );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_COMBATDROP_AT_LOCATION );
		}
		return msgType;
	}
	else
	{
		return GameMessage::MSG_INVALID;
	}
}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueFireWeaponCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( !command )
	{
		return msgType;
	}

	if( BitIsSet( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{
		//OBJECT BASED FIRE WEAPON
		if (!target || !target->getObject())
			return msgType;

		if (!command->isValidObjectTarget(TheInGameUI->getFirstSelectedDrawable(), target))
			return msgType;

		if( BitIsSet( command->getOptions(), ATTACK_OBJECTS_POSITION ) )
		{
			//Actually, you know what.... we want to attack the object's location instead.
			msgType = GameMessage::MSG_DO_WEAPON_AT_LOCATION;
			if( commandType == DO_COMMAND )
			{
				GameMessage *msg;
				msg = TheMessageStream->appendMessage( msgType );
				msg->appendIntegerArgument( command->getWeaponSlot() );
				msg->appendLocationArgument( *pos );
				msg->appendIntegerArgument( command->getMaxShotsToFire() );
				//ShigureUi 04/10/2026 comment it out cuz no one ever use it
				////Object in way.... some location weapons care, others don't
				//ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
				//msg->appendObjectIDArgument( targetID );
				msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}
		}
		else
		{
			msgType = GameMessage::MSG_DO_WEAPON_AT_OBJECT;
			if( commandType == DO_COMMAND )
			{
				GameMessage *msg;
				msg = TheMessageStream->appendMessage( msgType );
				msg->appendIntegerArgument( command->getWeaponSlot() );
				ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
				msg->appendObjectIDArgument( targetID );
				msg->appendIntegerArgument( command->getMaxShotsToFire() );
				msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

				//play a unit specific sound?
				PickAndPlayInfo info;
				WeaponSlotType slot = command->getWeaponSlot();
				info.m_weaponSlot = &slot;
 				pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_WEAPON_AT_OBJECT, &info );
			}
		}
	}
	else if( BitIsSet( command->getOptions(), NEED_TARGET_POS ) )
	{
		//LOCATION BASED FIRE WEAPON
		msgType = GameMessage::MSG_DO_WEAPON_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getWeaponSlot() );
			msg->appendLocationArgument( *pos );
			msg->appendIntegerArgument( command->getMaxShotsToFire() );
			//ShigureUi 04/10/2026 comment it out cuz no one ever use it
			////Object in way.... some location weapons care, others don't
			//ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			//msg->appendObjectIDArgument( targetID );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
		}
	}
	else
	{
		//NO TARGET WEAPON
		msgType = GameMessage::MSG_DO_WEAPON;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			//msg = TheMessageStream->appendMessage( msgType );
			//DEBUG_ASSERTCRASH( (command->getSpecialPowerTemplate()), ("No Special Power Weapon here to 'do' with! ML"));
			//msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendIntegerArgument(command->getWeaponSlot());
			msg->appendIntegerArgument(command->getMaxShotsToFire());
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
		}
	}

	return msgType;
}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::createEnterMessage( Drawable *enter,
																												 CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_ENTER;

	// if we're just evaluating then get out of here without actually doing the action
	if( commandType == EVALUATE_ONLY )
		return msgType;

	if (!enter || !enter->getObject())
		return msgType;

	// sanity
	DEBUG_ASSERTCRASH( commandType == DO_COMMAND, ("createEnterMessage - commandType is not DO_COMMAND") );

	if( m_teamExists )
	{
		PickAndPlayInfo info;
		info.m_drawTarget = enter;
		pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		GameMessage *enterMsg = TheMessageStream->appendMessage( msgType );
		enterMsg->appendObjectIDArgument( INVALID_ID );		// 0 means current "selection team" of this player
		enterMsg->appendObjectIDArgument( enter->getObject()->getID() );
		enterMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
	}
	else
	{
		DEBUG_CRASH(("Shouldn't get here. jkmcd"));
	}

	// return the type of the message used
	return msgType;

}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::createSmartGarrisonMessage( Drawable *target,
																																 CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_DO_SMART_GARRISON;

	// if we're just evaluating then get out of here without actually doing the action
	if( commandType == EVALUATE_ONLY )
		return msgType;

	if (!target || !target->getObject())
		return msgType;

	// sanity
	DEBUG_ASSERTCRASH( commandType == DO_COMMAND, ("createSmartGarrisonMessage - commandType is not DO_COMMAND") );

	if( m_teamExists )
	{
		PickAndPlayInfo info;
		info.m_drawTarget = target;
		// reuse the normal "enter" voice response
		pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_ENTER, &info );

		GameMessage *msg = TheMessageStream->appendMessage( msgType );
		msg->appendObjectIDArgument( target->getObject()->getID() );
	}

	// return the type of the message used
	return msgType;

}

//====================================================================================
CommandTranslator::CommandTranslator() :
	m_objective(0),
	m_teamExists(false),
	m_reverseMoveAiming(false),
	m_rightMouseDownTimeMs(0),
	m_rightMouseUpTimeMs(0)
{
	m_rightMouseDownAnchor.zero();
	m_rightMouseUpAnchor.zero();
}

//====================================================================================
CommandTranslator::~CommandTranslator()
{
}



//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::evaluateForceAttack( Drawable *draw, const Coord3D *pos, CommandEvaluateType type )
{
	// evaluateForceAttack is used to determine whether or not we can force attack the
	// given target, and if we can, to issue the appropriate command.

	GameMessage::Type retVal = GameMessage::MSG_INVALID;
	if( !draw && !pos )
	{
		return retVal;
	}

	const DrawableList *allSelected = TheInGameUI->getAllSelectedDrawables();

	if( draw )
	{
		Object *obj = draw ? draw->getObject() : nullptr;
		if( !obj )
		{
			return retVal;
		}

		CanAttackResult result = canAnyForceAttack( allSelected, obj, pos );

		if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			retVal = GameMessage::MSG_DO_FORCE_ATTACK_OBJECT;

			if( type == DO_COMMAND )
			{
				pickAndPlayUnitVoiceResponse( allSelected, retVal );
				GameMessage *newMsg = TheMessageStream->appendMessage( retVal );
				newMsg->appendObjectIDArgument( obj->getID() );
				newMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}
			else if( type == DO_HINT )
			{
				retVal = GameMessage::MSG_DO_FORCE_ATTACK_OBJECT_HINT;
				// Don't need the message back, cause there is nothing to append to it.
				TheMessageStream->appendMessage( retVal );
			}
		}
		else if( result == ATTACKRESULT_INVALID_SHOT && type == DO_HINT )
		{
			retVal = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
			TheMessageStream->appendMessage( retVal );
		}
	}
	else if( pos )
	{
		CanAttackResult result = canAnyForceAttack( allSelected, nullptr, pos );

		if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			retVal = GameMessage::MSG_DO_FORCE_ATTACK_GROUND;

			if( type == DO_COMMAND )
			{
				pickAndPlayUnitVoiceResponse( allSelected, retVal );
				GameMessage *newMsg = TheMessageStream->appendMessage( retVal );
				newMsg->appendLocationArgument( *pos );
				newMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}
			else if( type == DO_HINT )
			{
				retVal = GameMessage::MSG_DO_FORCE_ATTACK_GROUND_HINT;
				// Don't need the message back, cause there is nothing to append to it.
				TheMessageStream->appendMessage( retVal );
			}
		}
		else if( result == ATTACKRESULT_INVALID_SHOT && type == DO_HINT )
		{
			retVal = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
			TheMessageStream->appendMessage( retVal );
		}
	}

	return retVal;
}

void CommandTranslator::resolveContextTarget( Drawable *&draw, Object *&obj, Drawable *&drawableInWay )
{
	//This piece of code is used to prevent interaction with unselectable objects or masked objects. When we
	//call this function, we typically pass in both a position and a drawable (if applicable), so if the
	//drawable is invalid... then convert it to a position to be evaluated instead.
	//Added: shrubberies are the exception for interactions...
	//Removed: GS Took out ObjectStatusUnselectable, since that status only prevents selection, not everything
	if ( obj == nullptr ||
		( obj->getStatusBits().test( OBJECT_STATUS_MASKED ) &&
		!obj->isKindOf(KINDOF_SHRUBBERY) &&
		!obj->isKindOf(KINDOF_FORCEATTACKABLE) )
	)
	{
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate
		//a position interaction.
		draw = nullptr;
		obj = nullptr;
	}

	// If the thing is a mine, and is locally controlled, then we should issue a moveto to its location.
	if (obj && obj->isLocallyControlled() && obj->isKindOf(KINDOF_MINE))
	{
		draw = nullptr;
		obj = nullptr;
	}

	if ( TheInGameUI->isInForceMoveToMode() )
	{
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate
		//a position interaction.
		draw = nullptr;
		obj = nullptr;
	}
	else if (TheInGameUI->isInForceAttackMode() )
	{
		// setting the drawableInWay to draw will allow us to force attack in the issue move command
		// if there is a location to which we should attack.
		drawableInWay = draw;
	}
}

void CommandTranslator::resolveGuiCommandTarget( const CommandButton *command, Drawable *&draw, Object *&obj )
{
	if( obj && obj->isKindOf( KINDOF_SHRUBBERY ) && !BitIsSet( command->getOptions(), ALLOW_SHRUBBERY_TARGET ) )
	{
		//If our object is a shrubbery, and we don't allow targeting it... then null it out.
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate
		//a position interaction.
		draw = nullptr;
		obj = nullptr;
	}

	if( obj && obj->isKindOf( KINDOF_MINE ) && !BitIsSet( command->getOptions(), ALLOW_MINE_TARGET ) )
	{
		//If our object is a mine, and we don't allow targeting it... then null it out.
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate
		//a position interaction.
		draw = nullptr;
		obj = nullptr;
	}

	//Kris: September 27, 2002
	//Added relationship tests to make sure we're not attempting a context-command on a restricted relationship.
	//This case prevents rebels from using tranq darts on allies.
	if( obj && BitIsSet( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{
		Relationship relationship = ThePlayerList->getLocalPlayer()->getRelationship( obj->getTeam() );

		switch( relationship )
		{
			case ALLIES:
				if( !BitIsSet( command->getOptions(), NEED_TARGET_ALLY_OBJECT ) )
				{
					draw = nullptr;
					obj = nullptr;
				}
				break;
			case ENEMIES:
				if( !BitIsSet( command->getOptions(), NEED_TARGET_ENEMY_OBJECT ) )
				{
					draw = nullptr;
					obj = nullptr;
				}
				break;
			case NEUTRAL:
				if( !BitIsSet( command->getOptions(), NEED_TARGET_NEUTRAL_OBJECT ) )
				{
					draw = nullptr;
					obj = nullptr;
				}
				break;
		}
	}
}

GameMessage::Type CommandTranslator::handleWaypointModeCommand( const Coord3D *pos, Drawable *draw, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	//Override any *other* commands with waypoint commands.
	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{
		if( TheTerrainLogic )
		{
			msgType = issueMoveToLocationCommand( pos, draw, type );
		}
	}
	else
	{
		msgType = GameMessage::MSG_ADD_WAYPOINT_HINT;
		GameMessage* hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendLocationArgument( *pos );
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleGuiCommand( const CommandButton *command, Drawable *draw, Object *obj, const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	resolveGuiCommandTarget( command, draw, obj );

	Bool currentlyValid = FALSE;
	ObjectID objectID = obj ? obj->getID() : INVALID_ID;
	switch( command->getCommandType() )
	{
		//Kris: June 06, 2002
		//This is a GUI command button that triggers a mode. In any of these modes, only one specific action
		//can occur. If the mouse isn't over a valid target, then the conditions aren't met and the code will
		//cause an invalid version of the cursor to be shown -- and should the user click, the action won't take place.
		case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB, obj, InGameUI::SELECTION_ANY );
			break;
		case GUICOMMANDMODE_HIJACK_VEHICLE:
			currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_HIJACK_VEHICLE, obj, InGameUI::SELECTION_ANY );
			break;
		case GUICOMMANDMODE_SABOTAGE_BUILDING:
			currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SABOTAGE_BUILDING, obj, InGameUI::SELECTION_ANY );
			break;
#ifdef ALLOW_SURRENDER
		case GUICOMMANDMODE_PICK_UP_PRISONER:
			currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_PICK_UP_PRISONER, obj, InGameUI::SELECTION_ANY );
			break;
#endif
		case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
		{
			Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
			if( unit )
				currentlyValid = TheInGameUI->canSelectedObjectsDoSpecialPower( command, obj, pos, InGameUI::SELECTION_ANY, command->getOptions(), unit );
			else
				currentlyValid = false;
			break;
		}
		case GUI_COMMAND_SPECIAL_POWER:
			currentlyValid = TheInGameUI->canSelectedObjectsDoSpecialPower( command, obj, pos, InGameUI::SELECTION_ANY, command->getOptions(), nullptr );
			break;
		case GUI_COMMAND_FIRE_WEAPON:
			currentlyValid = TheInGameUI->canSelectedObjectsEffectivelyUseWeapon( command, obj, pos, InGameUI::SELECTION_ANY );
			break;
		case GUI_COMMAND_COMBATDROP:
			currentlyValid = !obj ? TRUE : TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_COMBATDROP_INTO, obj, InGameUI::SELECTION_ANY );
			break;
	}

	if( currentlyValid )
	{
		if( type == DO_COMMAND || type == EVALUATE_ONLY )
		{
			switch( command->getCommandType() )
			{
				case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
				case GUICOMMANDMODE_HIJACK_VEHICLE:
				case GUICOMMANDMODE_SABOTAGE_BUILDING:
					msgType = createEnterMessage( draw, type );
					break;
#ifdef ALLOW_SURRENDER
				case GUICOMMANDMODE_PICK_UP_PRISONER:
					msgType = issueAttackCommand( draw, type, command->getCommandType() );
					break;
#endif
				case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
				{
					Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
					if( unit )
						msgType = issueSpecialPowerCommand( command, type, draw, pos, unit );
					break;
				}
				case GUI_COMMAND_SPECIAL_POWER://lorenzen
					msgType = issueSpecialPowerCommand( command, type, draw, pos, nullptr );
					break;
				case GUI_COMMAND_FIRE_WEAPON:
					msgType = issueFireWeaponCommand( command, type, draw, pos );
					break;
				case GUI_COMMAND_COMBATDROP:
					msgType = issueCombatDropCommand( command, type, draw, pos );
					break;
			}

			// null out the GUI command if we're actually doing something.
			// Exception: an N-point (chronosphere) power keeps the command pending between
			// clicks so the remaining clicks (or a right-click cancel) can still be handled.
			if( type == DO_COMMAND && !TheInGameUI->hasPendingSpecialPowerLocations() )
			{
				TheInGameUI->setGUICommand( nullptr );
			}

		}
		else
		{
			msgType = GameMessage::MSG_VALID_GUICOMMAND_HINT;
			GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
			hintMessage->appendObjectIDArgument( objectID );
		}
	}
	else	// not currently valid
	{
		msgType = GameMessage::MSG_INVALID_GUICOMMAND_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( objectID );
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleSpecialPowerConstructCommand( const CommandButton *command, Drawable *draw, const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	//We're using the build placement interface to determine where to build our special power item.
	//Because of that, we only care about DO_COMMAND. The context evaluation and hint feedback system
	//is already taken care of. But what we need to do is trigger the special power to actually build
	//the object and reset the timer.
	if( type == DO_COMMAND )
	{
		switch( command->getCommandType() )
		{
			case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
			{
				Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
				if( unit )
					msgType = issueSpecialPowerCommand( command, type, draw, pos, unit );
				break;
			}
			case GUI_COMMAND_SPECIAL_POWER://lorenzen
				msgType = issueSpecialPowerCommand( command, type, draw, pos, nullptr );
				break;
		}
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleSpecialPowerOverrideDestinationCommand( const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// do the command
		msgType = GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION;
		if( type == DO_COMMAND )
		{
			GameMessage *gameMsg = TheMessageStream->appendMessage( msgType );

			gameMsg->appendLocationArgument( *pos );
			gameMsg->appendIntegerArgument( SPECIAL_INVALID );
			gameMsg->appendObjectIDArgument( INVALID_ID );	// no specific source

		}

	}
	else
	{

		// generate a hint message
		msgType = GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION_HINT;
		TheMessageStream->appendMessage( msgType );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleResumeConstructionCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// do the command
		msgType = GameMessage::MSG_RESUME_CONSTRUCTION;
		if( type == DO_COMMAND )
		{
			GameMessage *resumeMsg = TheMessageStream->appendMessage( msgType );

			resumeMsg->appendObjectIDArgument( obj->getID() );
			resumeMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_RESUME_CONSTRUCTION );

		}

	}
	else
	{

		// generate a hint message
		msgType = GameMessage::MSG_RESUME_CONSTRUCTION_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleDockAtCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	//
	// The actual logic is simply to AIUpdate::dock with the target, the hint is the
	// only part that needs to be more specific.
	//
	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// Give the dock command
		msgType = GameMessage::MSG_DOCK;
		if( type == DO_COMMAND )
		{
			GameMessage *dockMsg = TheMessageStream->appendMessage( msgType );

			dockMsg->appendObjectIDArgument( obj->getID() );
			dockMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			// only make sounds if we really did the command messages
			pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DOCK);
		}

	}
	else
	{

		// make the hint
		msgType = GameMessage::MSG_DOCK_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleRepairObjectCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// do the command
		msgType = GameMessage::MSG_DO_REPAIR;
		if( type == DO_COMMAND )
		{
			GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

			healMsg->appendObjectIDArgument( obj->getID() );
			healMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_REPAIR );

		}

	}
	else
	{

		// generate a hint message
		msgType = GameMessage::MSG_DO_REPAIR_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleGetRepairedAtCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// do the command
		msgType = GameMessage::MSG_GET_REPAIRED;
		if( type == DO_COMMAND )
		{
			GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

			healMsg->appendObjectIDArgument( obj->getID() );
			healMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());


			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_GET_REPAIRED );


		}

	}
	else
	{

		// generate a hint message
		msgType = GameMessage::MSG_GET_REPAIRED_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleGetHealedAtCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// do the command
		msgType = GameMessage::MSG_GET_HEALED;
		if( type == DO_COMMAND )
		{
			GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

			healMsg->appendObjectIDArgument( obj->getID() );
			healMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_GET_HEALED );

		}

	}
	else
	{

		// generate hint message
		msgType = GameMessage::MSG_GET_HEALED_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType);
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleHijackVehicleCommand( Drawable *draw, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// Now, this just tricks the AI  into making the hijacker run towards the target vehicle
		// I must add a test to keep him from actually entering an enemy vehicle (contained)... Lorenzen
		msgType = createEnterMessage( draw, type );

	}
	else
	{

		msgType = GameMessage::MSG_HIJACK_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( draw->getObject()->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleConvertObjectToCarBombCommand( Drawable *draw, Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// issue the command (convert to carbomb is nearly identical to enter)
		msgType = createEnterMessage( draw, type );

	}
	else
	{

		msgType = GameMessage::MSG_CONVERT_TO_CARBOMB_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleSabotageBuildingCommand( Drawable *draw, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{
		msgType = createEnterMessage( draw, type );
	}
	else
	{
		msgType = GameMessage::MSG_SABOTAGE_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( draw->getObject()->getID() );
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleSalvageCommand( Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	GameMessage *msg;

	if( type == DO_COMMAND || type == EVALUATE_ONLY ) {
		msgType = GameMessage::MSG_DO_SALVAGE;
		if (type == DO_COMMAND) {
			msg = TheMessageStream->appendMessage(msgType);
			msg->appendLocationArgument(*obj->getPosition());
			pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), msgType);
		}

	} else {
		msgType = GameMessage::MSG_DO_SALVAGE_HINT;
		msg = TheMessageStream->appendMessage(msgType);
		msg->appendLocationArgument(*obj->getPosition());
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleEnterObjectCommand( Drawable *draw, Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// issue the command
		msgType = createEnterMessage( draw, type );

	}
	else
	{

		msgType = GameMessage::MSG_ENTER_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleAttackObjectCommand( Drawable *draw, Object *obj, CommandEvaluateType type, GameMessage::Type hintType )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// issue the attack order
		msgType = issueAttackCommand( draw, type );

	}
	else
	{

		// Generate an Attack hint
		msgType = hintType;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleCaptureBuildingCommand( Drawable *draw, Object *obj, const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	//@TODO: Kris
	//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
	Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
	const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
	if( set )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			// get command button
			const CommandButton *command = set->getCommandButton(i);
			if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
			{
				SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
				if( type == DO_COMMAND || type == EVALUATE_ONLY )
				{
					if( spType == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING ||
							spType == SPECIAL_INFANTRY_CAPTURE_BUILDING )
					{
						//Issue the capture building command
						msgType = issueSpecialPowerCommand( command, type, draw, pos, nullptr );
						break;
					}
				}
				else if( spType == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING )
				{
					//Issue the black lotus hack hint for capturing a building.
					msgType = GameMessage::MSG_HACK_HINT;
					GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
					hintMessage->appendObjectIDArgument( obj->getID() );
				}
				else if( spType == SPECIAL_INFANTRY_CAPTURE_BUILDING )
				{
					//Issue the infantry hint for capturing a building
					msgType = GameMessage::MSG_CAPTUREBUILDING_HINT;
					GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
					hintMessage->appendObjectIDArgument( obj->getID() );
				}
			}
		}
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleHackCommand( Drawable *draw, Object *obj, const Coord3D *pos, CommandEvaluateType type, SpecialPowerType hackPower )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{
		//@TODO: Kris
		//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
		Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
		const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
		if( set )
		{
			for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				// get command button
				const CommandButton *command = set->getCommandButton(i);
				if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
				{
					SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
					if( spType == hackPower )
					{
						msgType = issueSpecialPowerCommand( command, type, draw, pos, nullptr );
						break;
					}
				}
			}
		}
	}
	else
	{
		msgType = GameMessage::MSG_HACK_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );
	}

	return msgType;
}

#ifdef ALLOW_SURRENDER
GameMessage::Type CommandTranslator::handlePickUpPrisonerCommand( Drawable *draw, Object *obj, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{

		// issue the command
		msgType = issueAttackCommand( draw, type, GUICOMMANDMODE_PICK_UP_PRISONER );

	}
	else
	{

		msgType = GameMessage::MSG_PICK_UP_PRISONER_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendObjectIDArgument( obj->getID() );

	}

	return msgType;
}
#endif

GameMessage::Type CommandTranslator::handleSetRallyPointCommand( const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_SET_RALLY_POINT;

	if (type == DO_COMMAND) {
		const DrawableList *allSelectedDrawables = TheInGameUI->getAllSelectedDrawables();

		for (DrawableList::const_iterator it = allSelectedDrawables->begin(); it != allSelectedDrawables->end(); ++it) {
			Drawable *draw = (*it);
			if (draw && draw->getObject()) {
				GameMessage *newMsg = TheMessageStream->appendMessage(msgType);
				newMsg->appendObjectIDArgument(draw->getObject()->getID());
				newMsg->appendLocationArgument(*pos);
			}
		}
	} else if (type == DO_HINT) {
		msgType = GameMessage::MSG_SET_RALLY_POINT_HINT;
		GameMessage *hintMessage = TheMessageStream->appendMessage(msgType);
		hintMessage->appendLocationArgument(*pos);
	}

	return msgType;
}

GameMessage::Type CommandTranslator::handleInvalidShotCommand( const Coord3D *pos )
{
	GameMessage::Type msgType = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
	GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
	hintMessage->appendLocationArgument( *pos );

	return msgType;
}

GameMessage::Type CommandTranslator::handleDefaultMoveCommand( Drawable *draw, Drawable *drawableInWay, const Coord3D *pos, CommandEvaluateType type )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	//
	// NOTE: If you change this command evaluation function in what it will do
	// if there is nothing picked ... you might want to edit the logic of the
	// selection translator in that you can only select objects if there is
	// no "interesting" command to do with the picked drawable ... which is determined
	// by what we return in this function by default
	//

	//Before we issue a move order or hint, check to see if we can even move there!
	Bool validQuickPath = FALSE;
	// Make sure to only to the check if the shroud is CLEARED.  If it is fogged or shrouded, SKIP THE CHECK.  jba [3/11/2003]
	if( ThePartitionManager->getShroudStatusForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex(), pos ) != CELLSHROUD_CLEAR )
	{
		//If it's in the shroud, pretend we can move there -- skip the check.
		validQuickPath = TRUE;
	}
	else
	{
		//Can we path there?
		const DrawableList *allSelectedDrawables = TheInGameUI->getAllSelectedDrawables();
		for( DrawableList::const_iterator it = allSelectedDrawables->begin(); it != allSelectedDrawables->end(); ++it )
		{
			Object *obj = (*it) ? (*it)->getObject() : nullptr;
			AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
			if( ai )
			{
				if ( ai->isQuickPathAvailable( pos ) )
				{
					validQuickPath = TRUE;
					break;
				}
				// Wait! there are some units that CAN moveTo positions that Quickpath will reject,
				// namely, Colonel Burton and the CombatBike. Both have CLIFF locomotors.
				// We must detect whether the position is valid for these, before just invalidating the cursor,
				// out of hand.
				if ( ai->hasLocomotorForSurface( LOCOMOTORSURFACE_CLIFF ) )
				{
					if ( TheTerrainLogic->isCliffCell( pos->x, pos->y ) )
					{
						validQuickPath = TRUE;// yeah, not really quick, but you know...
						break;
					}
				}
			}


		}
	}

	if( type == DO_COMMAND || type == EVALUATE_ONLY )
	{
		// issue command
		// Note: If draw is valid, then its one of ours and we don't have something more specific
		// to do. Therefore, lets not issue a move command, and instead we'll return that there
		// wasn't a command for us to perform.

		if ( draw == nullptr )
			msgType = issueMoveToLocationCommand( pos, drawableInWay, type );
	}
	else
	{
		if( !validQuickPath )
		{
			msgType = GameMessage::MSG_DO_INVALID_HINT;
		}
		else if( TheInGameUI->isInWaypointMode() )
		{
			//Waypoint mode
			msgType = GameMessage::MSG_ADD_WAYPOINT_HINT;
		}
		else if( TheInGameUI->isInAttackMoveToMode() )
		{
			//THIS CODE WILL NEVER EVER GET CALLED! -- it's a context command now (READ: rip code out)
			//Attack move
			msgType = GameMessage::MSG_DO_ATTACKMOVETO_HINT;
		}
		else if( TheInGameUI->isInReverseMoveToMode() )
		{
			msgType = GameMessage::MSG_DO_REVERSE_MOVETO_HINT;
		}
		else
		{
			//Normal and forced move.
			msgType = GameMessage::MSG_DO_MOVETO_HINT;
		}
		GameMessage *hintMessage = TheMessageStream->appendMessage( msgType );
		hintMessage->appendLocationArgument( *pos );

	}

	return msgType;
}

// ------------------------------------------------------------------------------------------------
/** This method and the order of operations in the check here, determine what command would
	* actually happen (if type parameter == DO_COMMAND) if the user clicked on the drawable
	* 'draw'.  If type == DO_HINT, then the user hasn't actually clicked, but has moused over
	* the drawable 'draw' and we want to generate a hint message as to what the actual
	* command would be if clicked
	* NOTE: draw can be null, in which case we give a hint for the location */
// ------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::evaluateContextCommand( Drawable *draw,
																														 const Coord3D *pos,
																														 CommandEvaluateType type,
																														 Int modifiers )
{
	Object *obj = draw ? draw->getObject() : nullptr;
	Drawable *drawableInWay = draw;
	resolveContextTarget( draw, obj, drawableInWay );

	// Then we should determine if the game currently prefers selection events. If it does, then return
	// the invalid message.
	if( obj && obj->isLocallyControlled() && TheInGameUI->isInPreferSelectionMode() )
	{
		return GameMessage::MSG_INVALID;
	}

	// Kris: Now that we can select non-controllable units/structures, don't allow any actions to be performed.
	const CommandButton *command = TheInGameUI->getGUICommand();

	if( command && command->getCommandType() == GUICOMMANDMODE_PLACE_BEACON )
	{
		GameMessage::Type msgType = GameMessage::MSG_VALID_GUICOMMAND_HINT;
		TheMessageStream->appendMessage( msgType );
		return msgType;
	}

	const Bool canPerformActions = TheInGameUI->areSelectedObjectsControllable()
		|| ( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT );

	if( !canPerformActions )
	{
		return GameMessage::MSG_INVALID;
	}

	// ShigureUi 13/10/2026 comment it out for new waypoint system
	/*
	// TheSuperHackers @feature Smart Garrison: holding ALT while hovering a transport the selection
	// can enter overrides waypoint mode and distributes the units across the target plus nearby
	// transports.
	if( draw && obj && BitIsSet( modifiers, KEY_STATE_ALT )
			&& TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_ENTER_OBJECT, obj, InGameUI::SELECTION_ANY, true ) )
	{
		if( type == DO_COMMAND || type == EVALUATE_ONLY )
		{
			return createSmartGarrisonMessage( draw, type );
		}

		const GameMessage::Type msgType = GameMessage::MSG_SMART_GARRISON_HINT;
		TheMessageStream->appendMessage( msgType );
		return msgType;
	}*/

	//ShigureUi 08/10/2026 no more handle waypoint mode
	//if( TheInGameUI->isInWaypointMode() )
	//{
	//	return handleWaypointModeCommand( pos, draw, type );
	//}

	CanAttackResult result = ATTACKRESULT_NOT_POSSIBLE;

	if(command &&
		(command->isContextCommand()
			|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER
			|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT))
	{
		return handleGuiCommand( command, draw, obj, pos, type );
	}
	else if( command && (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT
					 || command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT) )
	{
		return handleSpecialPowerConstructCommand( command, draw, pos, type );
	}
	else if( TheInGameUI->canSelectedObjectsOverrideSpecialPowerDestination( pos, InGameUI::SELECTION_ANY, SPECIAL_INVALID ) )
	{
		return handleSpecialPowerOverrideDestinationCommand( pos, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_RESUME_CONSTRUCTION, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleResumeConstructionCommand( obj, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DOCK_AT,
																										obj,
																										InGameUI::SELECTION_ALL ) )
	{
		return handleDockAtCommand( obj, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_REPAIR_OBJECT, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleRepairObjectCommand( obj, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_GET_REPAIRED_AT, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleGetRepairedAtCommand( obj, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_GET_HEALED_AT, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleGetHealedAtCommand( obj, type );
	}
	else if( draw && draw->getObject() && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_HIJACK_VEHICLE,
																										draw->getObject(),
																										InGameUI::SELECTION_ANY ) )
	{
		return handleHijackVehicleCommand( draw, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleConvertObjectToCarBombCommand( draw, obj, type );
	}
	else if( draw && draw->getObject() && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SABOTAGE_BUILDING,
																										draw->getObject(),
																										InGameUI::SELECTION_ANY ) )
	{
		return handleSabotageBuildingCommand( draw, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() && canSelectionSalvage(obj) )
	{
		return handleSalvageCommand( obj, type );
	}
	else if( draw && !TheInGameUI->isInForceAttackMode() &&
					 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_ENTER_OBJECT, obj, InGameUI::SELECTION_ANY, true ) )
	{
		return handleEnterObjectCommand( draw, obj, type );
	}
	else if( draw && (result = TheInGameUI->getCanSelectedObjectsAttack( InGameUI::ACTIONTYPE_ATTACK_OBJECT, obj, InGameUI::SELECTION_ANY, TheInGameUI->isInForceAttackMode() )) == ATTACKRESULT_POSSIBLE )
	{
		return handleAttackObjectCommand( draw, obj, type, GameMessage::MSG_DO_ATTACK_OBJECT_HINT );
	}
	else if( draw && result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
	{
		return handleAttackObjectCommand( draw, obj, type, GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT );
	}
	else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CAPTURE_BUILDING, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleCaptureBuildingCommand( draw, obj, pos, type );
	}
	else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleHackCommand( draw, obj, pos, type, SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK );
	}
	else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_STEAL_CASH_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleHackCommand( draw, obj, pos, type, SPECIAL_BLACKLOTUS_STEAL_CASH_HACK );
	}
	else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
	{
		return handleHackCommand( draw, obj, pos, type, SPECIAL_HACKER_DISABLE_BUILDING );
	}
#ifdef ALLOW_SURRENDER
	else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_PICK_UP_PRISONER, obj, InGameUI::SELECTION_ANY ) )
	{
		return handlePickUpPrisonerCommand( draw, obj, type );
	}
#endif
	else if ( !draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SET_RALLY_POINT, nullptr, InGameUI::SELECTION_ALL, FALSE ))
	{
		return handleSetRallyPointCommand( pos, type );
	}
	else if( draw && result == ATTACKRESULT_INVALID_SHOT )
	{
		return handleInvalidShotCommand( pos );
	}
	else
	{
		return handleDefaultMoveCommand( draw, drawableInWay, pos, type );
	}
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
//====================================================================================
/**
 * The Command Translator translates mouse events into object command messages
 * such as move_to, attack, etc.
 */
GameMessageDisposition CommandTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessage::Type t = msg->getType();
	GameMessageDisposition disp = KEEP_MESSAGE;
	// We want to always be able to get to the options menu even during no input times and a clear game data message should always go through
	if (t != GameMessage::MSG_META_OPTIONS && t != GameMessage::MSG_CLEAR_GAME_DATA &&
			!TheInGameUI->getInputEnabled() && !isSystemMessage(msg) && !GameMessage::isDebugCheatHotkey(t))
	{
		return DESTROY_MESSAGE;
	}

#if defined(RTS_DEBUG)
	ExtentModType extentModType = EXTENTMOD_INVALID;
	Real extentModAmount = 0.0f;
#endif

	switch (t)
	{
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_MATCHING_UNITS:
		{

			TheInGameUI->selectUnitsMatchingCurrentSelection();

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SMART_SELECTION_NEXT_TYPE:
		{
			TheControlBar->smartSelectionCycle( 1 );
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SMART_SELECTION_PREV_TYPE:
		{
			TheControlBar->smartSelectionCycle( -1 );
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_NEXT_UNIT:
		{
			/* because list is prepended, iterate through backwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == nullptr )
				break;

			// if nothing is selected
			Drawable *temp;

			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the last drawable
				for( temp = TheGameClient->firstDrawable(); temp->getNextDrawable() != nullptr; temp = temp->getNextDrawable() )
				{
				}
				// temp is the last drawable
				for( temp; temp != nullptr; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && !object->isKindOf( KINDOF_NO_SELECT ) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );

						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->userLookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = nullptr;
				Bool hack = FALSE;
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the previous selectable drawable
					temp = selectedDrawable->getPrevDrawable();
					//temp = selectedDrawable;
					for( ; temp != selectedDrawable; temp = temp->getPrevDrawable() )
					{
						if( hack == TRUE )
						{
							temp = temp->getNextDrawable();
							hack = FALSE;
						}
						// if temp is null, set it to the last drawable and loop back to selected drawable
						if( temp == nullptr )
						{
							for(temp = selectedDrawable; temp->getNextDrawable() != nullptr; temp = temp->getNextDrawable() )
							{
							}
							hack = TRUE;
						}
						// else search for a previous selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() && !tempObject->isKindOf( KINDOF_NO_SELECT ) )
							{
								newDrawable = temp;
								break;
								//temp = selectedDrawable; // same as break
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != nullptr )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						// select the unit
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->userLookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_PREV_UNIT:
		{
			/* because list is prepended, iterate through forwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == nullptr )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the first drawable
				temp = TheGameClient->firstDrawable();
				for( temp; temp != nullptr; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && !object->isKindOf( KINDOF_NO_SELECT ) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );

						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->userLookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = nullptr;
				TheGameClient->getDrawableList();
				Bool hack = FALSE; // takes care of when for loop skips firstdrawable
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the next selectable drawable
					temp = selectedDrawable->getNextDrawable();
					//temp = selectedDrawable;
					for( temp; temp != selectedDrawable; temp = temp->getNextDrawable() )
					{
						if( hack == TRUE )
						{
							temp = TheGameClient->firstDrawable();
							hack = FALSE;
							if( temp == selectedDrawable )
							{
								break;
							}
						}
						// if temp is null, set it to the first drawable and loop forward to selected drawable
						if( temp == nullptr )
						{
							temp = TheGameClient->firstDrawable();
							hack = TRUE;
							const Object *tempObject = temp->getObject();
							// must take case of this case here or else the loop will break without getting newDrawable
							if( tempObject && temp->getNextDrawable() == selectedDrawable && !temp->isSelected()
								&& tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() && !tempObject->isKindOf( KINDOF_NO_SELECT ) )
							{
								newDrawable = temp;
								break;
							}
						}
						// else search for a next selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && !temp->isSelected() && tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() )
							{
								newDrawable = temp;
								break;
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != nullptr )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						// select the unit
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->userLookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;

		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_NEXT_WORKER:
		{
			/* because list is prepended, iterate through backwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == nullptr )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the last drawable
				for( temp = TheGameClient->firstDrawable(); temp->getNextDrawable() != nullptr; temp = temp->getNextDrawable() )
				{
				}
				// temp is the last drawable
				for( temp; temp != nullptr; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					// make sure you select only workers
					else if( object && object->isLocallyControlled() && !object->isContained() && object->isKindOf(KINDOF_DOZER) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( object->getID() );
						TheInGameUI->selectDrawable( temp );

						// play the units sound
						AudioEventRTS soundEvent = *temp->getTemplate()->getVoiceSelect();
						soundEvent.setObjectID(object->getID());
						TheAudio->addAudioEvent( &soundEvent );

						// center on the unit
						TheTacticalView->userLookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = nullptr;
				Bool hack = FALSE;
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the previous selectable drawable
					temp = selectedDrawable->getPrevDrawable();
					//temp = selectedDrawable;
					for( ; temp != selectedDrawable; temp = temp->getPrevDrawable() )
					{
						if( hack == TRUE )
						{
							temp = temp->getNextDrawable();
							hack = FALSE;
						}
						// if temp is null, set it to the last drawable and loop back to selected drawable
						if( temp == nullptr )
						{
							for(temp = selectedDrawable; temp->getNextDrawable() != nullptr; temp = temp->getNextDrawable() )
							{
							}
							hack = TRUE;
						}
						// else search for a previous selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && tempObject->isLocallyControlled() && !tempObject->isContained() && tempObject->isKindOf( KINDOF_DOZER ) )
							{
								newDrawable = temp;
								break;
								//temp = selectedDrawable; // same as break
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != nullptr )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						// select the unit
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->userLookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_PREV_WORKER:
		{
			/* because list is prepended, iterate through forwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == nullptr )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the first drawable
				temp = TheGameClient->firstDrawable();
				for( temp; temp != nullptr; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && object->isKindOf( KINDOF_DOZER ))
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );

						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->userLookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = nullptr;
				TheGameClient->getDrawableList();
				Bool hack = FALSE; // takes care of when for loop skips firstdrawable
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the next selectable drawable
					temp = selectedDrawable->getNextDrawable();
					//temp = selectedDrawable;
					for( temp; temp != selectedDrawable; temp = temp->getNextDrawable() )
					{
						if( hack == TRUE )
						{
							temp = TheGameClient->firstDrawable();
							hack = FALSE;
							if( temp == selectedDrawable )
							{
								break;
							}
						}
						// if temp is null, set it to the first drawable and loop forward to selected drawable
						if( temp == nullptr )
						{
							temp = TheGameClient->firstDrawable();
							hack = TRUE;
							const Object *tempObject = temp->getObject();
							// must take case of this case here or else the loop will break without getting newDrawable
							if( tempObject && temp->getNextDrawable() == selectedDrawable && !temp->isSelected()
								&& tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() )
							{
								newDrawable = temp;
								break;
							}
						}
						// else search for a next selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && !temp->isSelected() && tempObject->isMobile()
								  && tempObject->isLocallyControlled() && !tempObject->isContained() && tempObject->isKindOf( KINDOF_DOZER ) )
							{
								newDrawable = temp;
								break;
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != nullptr )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						// select the unit
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->userLookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;

		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER:
		{
			TheInGameUI->selectNextIdleWorker();

			disp = DESTROY_MESSAGE;
			break;
		}

#if RTS_ZEROHOUR
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SPECIAL_POWER_1:
		case GameMessage::MSG_META_SPECIAL_POWER_2:
		case GameMessage::MSG_META_SPECIAL_POWER_3:
		case GameMessage::MSG_META_SPECIAL_POWER_4:
		case GameMessage::MSG_META_SPECIAL_POWER_5:
		case GameMessage::MSG_META_SPECIAL_POWER_6:
		case GameMessage::MSG_META_SPECIAL_POWER_7:
		case GameMessage::MSG_META_SPECIAL_POWER_8:
		case GameMessage::MSG_META_SPECIAL_POWER_9:
		case GameMessage::MSG_META_SPECIAL_POWER_10:
		case GameMessage::MSG_META_SPECIAL_POWER_11:
		case GameMessage::MSG_META_SPECIAL_POWER_12:
		case GameMessage::MSG_META_SPECIAL_POWER_13:
		case GameMessage::MSG_META_SPECIAL_POWER_14:
		case GameMessage::MSG_META_SPECIAL_POWER_15:
		case GameMessage::MSG_META_SPECIAL_POWER_16:
		{
			TheControlBar->firePowerHotkey( t - GameMessage::MSG_META_SPECIAL_POWER_1 + 1 );
			disp = DESTROY_MESSAGE;
			break;
		}
#endif

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_HERO:
		{
			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == nullptr )
				break;

			Object *hero = iNeedAHero();

			if ( hero == nullptr )
				break;

			if ( hero->isContained() )
				hero = hero->getContainedBy();

			Drawable *heroDraw = hero->getDrawable();

			if ( heroDraw == nullptr )
				break;

			//deselect other units
			TheInGameUI->deselectAllDrawables();

			// create a new group.
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

			//New group so pass in value true
			teamMsg->appendBooleanArgument( TRUE );
			teamMsg->appendObjectIDArgument( hero->getID() );

			// select the unit
			TheInGameUI->selectDrawable( heroDraw );

			// center on the unit
			TheTacticalView->userLookAt(heroDraw->getPosition());

			disp = DESTROY_MESSAGE;
			break;
		}
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_COMMAND_CENTER:
			viewCommandCenter();
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_LAST_RADAR_EVENT:
		{
			// You don't need radar for the space bar. That's silly.
			Coord3D lastEvent;

			if( TheRadar->getLastEventLoc( &lastEvent ) )
			{
				TheTacticalView->userLookAt( &lastEvent );
			}

			disp = DESTROY_MESSAGE;
			break;

		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_ALL:
		case GameMessage::MSG_META_SELECT_ALL_AIRCRAFT:
		{
			KindOfMaskType requiredKindofs;
			KindOfMaskType disqualifyingKindofs;
			disqualifyingKindofs.set(KINDOF_DOZER);
			disqualifyingKindofs.set(KINDOF_HARVESTER);
			disqualifyingKindofs.set(KINDOF_IGNORES_SELECT_ALL);
			Bool selectAircraft = FALSE;

			if( t == GameMessage::MSG_META_SELECT_ALL_AIRCRAFT )
			{
				requiredKindofs.set(KINDOF_AIRCRAFT);
				selectAircraft = TRUE;
			}

			//Kris: Patch 1.03. We need to deselect all the units if any of the units we have selected
			//are incompatible with the select all type we are triggering. This is a fix for the SCUDSTORM
			//exploit.
			const DrawableList *drawList = TheInGameUI->getAllSelectedDrawables();
			Drawable *draw;
			for( DrawableListCIt it = drawList->begin(); it != drawList->end(); ++it )
			{
				draw = *it;
				if( selectAircraft && (draw->isAnyKindOf( disqualifyingKindofs ) || !draw->isKindOf( KINDOF_AIRCRAFT )) )
				{
					TheInGameUI->deselectAllDrawables();
					break;
				}
				else if( !selectAircraft && (draw->isAnyKindOf( disqualifyingKindofs ) || draw->isKindOf( KINDOF_STRUCTURE )) )
				{
					TheInGameUI->deselectAllDrawables();
					break;
				}
			}

			TheInGameUI->selectAllUnitsByType(requiredKindofs, disqualifyingKindofs);

			disp = DESTROY_MESSAGE;
			break;





/*
			TheInGameUI->deselectAllDrawables();

			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
			// creating a new team so pass in true
			teamMsg->appendBooleanArgument( TRUE );

			// just loop through all the drawables in the world
			Drawable *draw = TheGameClient->firstDrawable();

			while( draw )
			{
				const Object *object = draw->getObject();

				//Only select the object if it is locally controlled and not contained by anything.
				KindOfMaskType disqualifyingKindofs;
				disqualifyingKindofs.set(KINDOF_DOZER);
				disqualifyingKindofs.set(KINDOF_HARVESTER);
				disqualifyingKindofs.set(KINDOF_IGNORES_SELECT_ALL);
				if( object
					&& object->isMobile()
					&& object->isLocallyControlled()
					&& !object->isContained()
					&& !object->isAnyKindOf( disqualifyingKindofs )
					&& !object->isEffectivelyDead()
					&& object->isMassSelectable()
					)
				{
					// enforce optional unit cap
					if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
					{
						if ( !TheInGameUI->getDisplayedMaxWarning() )
						{
							TheInGameUI->setDisplayedMaxWarning( TRUE );
							TheInGameUI->message("GUI:MaxSelectionSize", TheInGameUI->getMaxSelectCount());
						}
					}
					else
					{
						TheInGameUI->selectDrawable(draw);
						teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
						TheInGameUI->setDisplayedMaxWarning( FALSE );
					}
				}

				draw = draw->getNextDrawable();
			}
			if( TheInGameUI->getSelectCount() )
			{
				TheInGameUI->message("GUI:SelectedAcrossMap");
			}

			disp = DESTROY_MESSAGE;
			break;
*/

		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SCATTER:
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_DO_SCATTER);
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_STOP:
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_DO_STOP);

			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CREATE_FORMATION:
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_CREATE_FORMATION);

			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEPLOY:
			#ifdef RTS_DEBUG
			DEBUG_CRASH(("unimplemented meta command MSG_META_DEPLOY !"));
			#endif
			/// @todo srj implement me
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_FOLLOW:
			#ifdef RTS_DEBUG
			DEBUG_CRASH(("unimplemented meta command MSG_META_FOLLOW !"));
			#endif
			/// @todo srj implement me
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		/* MDC - no such thing as chat to players right now - not until we have a diplomacy screen
		case GameMessage::MSG_META_CHAT_PLAYERS:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				ToggleInGameChat();
				SetInGameChatType( INGAME_CHAT_PLAYERS );
			}
			disp = DESTROY_MESSAGE;
			break;
		*/

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CHAT_ALLIES:
			// Chat is available in multiplayer, and in singleplayer/skirmish only when EnableSingleplayerChatwindow is set (for chat commands).
			if (TheGameLogic->isInMultiplayerGame() || TheGlobalData->m_enableSingleplayerChatWindow)
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				if ((localPlayer && localPlayer->isPlayerActive()) || !TheGlobalData->m_netMinPlayers)
				{
					ToggleInGameChat();
					SetInGameChatType( INGAME_CHAT_ALLIES );
				}
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CHAT_EVERYONE:
			// Chat is available in multiplayer, and in singleplayer/skirmish only when EnableSingleplayerChatwindow is set (for chat commands).
			if (TheGameLogic->isInMultiplayerGame() || TheGlobalData->m_enableSingleplayerChatWindow)
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				// TheSuperHackers @tweak skyaero 19/07/2025 Observers can now chat
				if (localPlayer || !TheGlobalData->m_netMinPlayers)
				{
					ToggleInGameChat();
					SetInGameChatType( INGAME_CHAT_EVERYONE );
				}
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DIPLOMACY:
			if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
			{
				ToggleDiplomacy( FALSE );
			}
			else if( TheShell && TheShell->isShellActive() && TheGameSpyBuddyMessageQueue)
				GameSpyToggleOverlay(GSOVERLAY_BUDDY);
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_PLACE_BEACON:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive() &&
				(TheGlobalData->m_netMinPlayers==0 || TheGameInfo->isMultiPlayer()))
			{
				Int count;
				const ThingTemplate *thing = TheThingFactory->findTemplate( ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getBeaconTemplate() );
				ThePlayerList->getLocalPlayer()->countObjectsByThingTemplate( 1, &thing, false, &count );
				DEBUG_LOG(("MSG_META_PLACE_BEACON - Player already has %d beacons active", count));
				if (count < TheMultiplayerSettings->getMaxBeaconsPerPlayer())
				{
					const CommandButton *commandButton = TheControlBar->findCommandButton( "Command_PlaceBeacon" );
					TheInGameUI->setGUICommand( commandButton );
				}
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_REMOVE_BEACON:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				TheMessageStream->appendMessage( GameMessage::MSG_REMOVE_BEACON );
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:

			ToggleQuitMenu();
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_INCREASE_MAX_RENDER_FPS:
		{
			if (changeMaxRenderFps(FpsValueChange_Increase))
			{
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DECREASE_MAX_RENDER_FPS:
		{
			if (changeMaxRenderFps(FpsValueChange_Decrease))
			{
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_INCREASE_LOGIC_TIME_SCALE:
		{
			if (changeLogicTimeScale(FpsValueChange_Increase))
			{
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DECREASE_LOGIC_TIME_SCALE:
		{
			if (changeLogicTimeScale(FpsValueChange_Decrease))
			{
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_LOWER_DETAILS:
		{
			if (TheGlobalData)
			{
				static Bool isLowDetails = FALSE;
				static Bool oldShadowVolumsValue = TRUE;
				static Bool oldLightMapValue = TRUE;
				static Bool oldCloudMap = TRUE;
				static Bool oldBehindBuildingMarkers = TRUE;
				static Int oldMaxParticleCount = 0;
				if(isLowDetails)
				{
					TheWritableGlobalData->m_useShadowVolumes = oldShadowVolumsValue;
					TheWritableGlobalData->m_useLightMap = oldLightMapValue;
					TheWritableGlobalData->m_useCloudMap = oldCloudMap;
					TheWritableGlobalData->m_maxParticleCount = oldMaxParticleCount;
					TheGameLogic->setShowBehindBuildingMarkers(oldBehindBuildingMarkers);
					if(TheInGameUI)
						TheInGameUI->message("GUI:ReturnGraphicsToPreviousSettings");
				}
				else
				{
					oldShadowVolumsValue = TheGlobalData->m_useShadowVolumes;
					TheWritableGlobalData->m_useShadowVolumes = FALSE;

					oldLightMapValue = TheGlobalData->m_useLightMap;
					TheWritableGlobalData->m_useLightMap = FALSE;

					oldCloudMap = TheGlobalData->m_useCloudMap;
					TheWritableGlobalData->m_useCloudMap = FALSE;

					oldBehindBuildingMarkers = TheGameLogic->getShowBehindBuildingMarkers();
					TheGameLogic->setShowBehindBuildingMarkers(FALSE);

					oldMaxParticleCount = TheGlobalData->m_maxParticleCount;
					TheWritableGlobalData->m_maxParticleCount = DROPPED_MAX_PARTICLE_COUNT;

					if(TheInGameUI)
						TheInGameUI->message("GUI:DetailsSetToLowest");
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_CONTROL_BAR:
		{
			if(TheShell->isShellActive())
			{
				WindowLayout *win = TheShell->top();
				if(win)
				{
					win->hide(!win->isHidden());
				}

			}
			else
			{
				Bool hide = false;
				if (TheWindowManager)
				{
					Int id = (Int)TheNameKeyGenerator->nameToKey("ControlBar.wnd:ControlBarParent");
					GameWindow *window = TheWindowManager->winGetWindowFromId(nullptr, id);

					if (window)
						hide = !window->winIsHidden();
				}

				ToggleControlBar();
#if defined(GENERALS_ONLINE)
				TheInGameUI->toggleObserverOverlay();
#endif
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_PLAYER_OBSERVER:
		{
			if (Player *lookAtPlayer = TheControlBar->getObserverLookAtPlayer())
			{
				if (Player *observedPlayer = TheControlBar->getObservedPlayer())
				{
					// Set no observed player.
					rts::changeObservedPlayer(nullptr);
					// But keep the look-at player.
					TheControlBar->setObserverLookAtPlayer(lookAtPlayer);
				}
				else
				{
					// Set observed player to look-at player.
					rts::changeObservedPlayer(lookAtPlayer);
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		// TheSuperHackers @feature Cycle the health bar display mode in game, so the player can
		// switch between Classic, Damaged and Always without leaving to edit Options.ini. Purely a
		// client side display setting -- nothing here reaches the simulation, so it is safe in
		// multiplayer and replays. The change is not written back to Options.ini, so it lasts for
		// the session only and the configured mode returns next launch.
		case GameMessage::MSG_META_CYCLE_HEALTH_BAR_MODE:
		{
			if( TheWritableGlobalData )
			{
				Int mode = TheGlobalData->m_healthBarDisplayMode + 1;
				if( mode >= HealthBarDisplayMode_Count || mode < 0 )
					mode = HealthBarDisplayMode_Classic;

				TheWritableGlobalData->m_healthBarDisplayMode = mode;

				switch( mode )
				{
					case HealthBarDisplayMode_Damaged:
						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
								"GUI:HealthBarModeDamaged", L"Health bars: damaged only" ) );
						break;
					case HealthBarDisplayMode_Always:
						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
								"GUI:HealthBarModeAlways", L"Health bars: always" ) );
						break;
					default:
						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
								"GUI:HealthBarModeClassic", L"Health bars: classic" ) );
						break;
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		// TheSuperHackers @feature Cycle HouseColorMode in game. Client side only, like the health
		// bar cycle above, and likewise not written back to Options.ini.
		case GameMessage::MSG_META_CYCLE_HOUSE_COLOR_MODE:
		{
#if RTS_ZEROHOUR
			if( TheWritableGlobalData )
			{
				Int mode = TheGlobalData->m_houseColorMode + 1;
				if( mode >= HouseColorMode_Count || mode < 0 )
					mode = HouseColorMode_PlayerColors;

				TheWritableGlobalData->m_houseColorMode = mode;
				rts::refreshHouseColors();

				if( mode == HouseColorMode_Basic )
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:HouseColorModeBasic", L"House colors: basic" ) );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:HouseColorModeDefault", L"House colors: default" ) );
			}
#endif
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_ATTACKMOVE:
			TheInGameUI->toggleAttackMoveToMode();
			break;

		case GameMessage::MSG_META_TOGGLE_REVERSEMOVE:
		{
			// TheSuperHackers @feature The reverse move key follows CastMode like a command bar
			// hotkey. QuickCast reverses to the cursor on the press. QuickCastWithIndicator arms
			// on the press, so the reverse move cursor shows while the key is held, and fires on
			// the release (MSG_RAW_KEY_UP below). In Normal, or a QuickCast press with nowhere to
			// fire, the press toggles the mode for the next click as before.
#if RTS_ZEROHOUR
			const Int castMode = TheGlobalData ? TheGlobalData->m_castMode : CastMode_Normal;
#else
			const Int castMode = CastMode_Normal;
#endif
			if( castMode == CastMode_QuickCastWithIndicator )
			{
				if( !TheInGameUI->isInReverseMoveToMode() )
					TheInGameUI->toggleReverseMoveToMode();
				m_reverseMoveAiming = TRUE;
			}
			else if( castMode != CastMode_QuickCast || !quickCastReverseMove() )
			{
				TheInGameUI->toggleReverseMoveToMode();
			}
			break;
		}

		case GameMessage::MSG_RAW_KEY_UP:
		{
			// TheSuperHackers @feature Release half of hold to aim reverse move. The meta event
			// only fires on the press, so watch for whatever key TOGGLE_REVERSEMOVE is bound to
			// coming up. The message is kept, nothing here consumes the key.
			if( !m_reverseMoveAiming )
				break;

			const Int key = msg->getArgument( 0 )->integer;
			Bool isReverseMoveKey = FALSE;
			for( const MetaMapRec *map = TheMetaMap->getFirstMetaMapRec(); map; map = map->m_next )
			{
				if( map->m_meta == GameMessage::MSG_META_TOGGLE_REVERSEMOVE && (Int)map->m_key == key )
					isReverseMoveKey = TRUE;
			}
			if( !isReverseMoveKey )
				break;

			m_reverseMoveAiming = FALSE;

			// A click while the key was held has already given the order and dropped the mode.
			// Letting go with nowhere to fire, over a panel say, calls the move off. A press always
			// arms in this mode, so this is the only way the key itself can cancel it.
			if( TheInGameUI->isInReverseMoveToMode() && !quickCastReverseMove() )
				TheInGameUI->clearArmedMoveMode();
			break;
		}

		case GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_LEFT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraRotatingLeft(), ("Setting rotate camera left, but it's already set!"));
			TheInGameUI->setCameraRotateLeft( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ROTATE_LEFT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraRotatingLeft(), ("Clearing rotate camera left, but it's already clear!"));
			TheInGameUI->setCameraRotateLeft( false );
			break;
		case GameMessage::MSG_META_ALT_CAMERA_ROTATE_LEFT:
			if (TheTacticalView->isCameraMovementFinished())
				TheTacticalView->rotateCamera(-1.0f / 8.0f, 500, 100, 400);
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_RIGHT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraRotatingRight(), ("Setting rotate camera right, but it's already set!"));
			TheInGameUI->setCameraRotateRight( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ROTATE_RIGHT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraRotatingRight(), ("Clearing rotate camera right, but it's already clear!"));
			TheInGameUI->setCameraRotateRight( false );
			break;
		case GameMessage::MSG_META_ALT_CAMERA_ROTATE_RIGHT:
			if (TheTacticalView->isCameraMovementFinished())
				TheTacticalView->rotateCamera(1.0f / 8.0f, 500, 100, 400);
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_IN:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraZoomingIn(), ("Setting zoom camera in, but it's already set!"));
			TheInGameUI->setCameraZoomIn( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ZOOM_IN:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraZoomingIn(), ("Clearing zoom camera in, but it's already clear!"));
			TheInGameUI->setCameraZoomIn( false );
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_OUT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraZoomingOut(), ("Setting zoom camera out, but it's already set!"));
			TheInGameUI->setCameraZoomOut( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ZOOM_OUT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraZoomingOut(), ("Clearing zoom camera out, but it's already clear!"));
			TheInGameUI->setCameraZoomOut( false );
			break;
		case GameMessage::MSG_META_CAMERA_RESET:
			TheInGameUI->resetCamera();
			break;
		case GameMessage::MSG_META_TOGGLE_CAMERA_TRACKING_DRAWABLE:
			TheInGameUI->setCameraTrackingDrawable( true );
			break;
        //--------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY:
		{
			if( TheGlobalData )
			{
				if (TheFramePacer->isFastForwardAvailable())
				{
					TheWritableGlobalData->m_TiVOFastMode = 1 - TheGlobalData->m_TiVOFastMode;
					TheInGameUI->messageNoFormat( TheGlobalData->m_TiVOFastMode
						? TheGameText->FETCH_OR_SUBSTITUTE("GUI:FF_ON", L"Fast Forward is on")
						: TheGameText->FETCH_OR_SUBSTITUTE("GUI:FF_OFF", L"Fast Forward is off")
					);
				}
			}

			disp = DESTROY_MESSAGE;
			break;

		}
		case GameMessage::MSG_META_TOGGLE_PAUSE:
		case GameMessage::MSG_META_TOGGLE_PAUSE_ALT:
		{
			if (!TheGameLogic->isInMultiplayerGame())
			{
				if (TheGameLogic->isGamePaused())
				{
					TheGameLogic->setGamePaused(FALSE);
				}
				else
				{
					Bool pause = TRUE;
					Bool pauseMusic = FALSE;
					Bool pauseInput = FALSE;
					TheGameLogic->setGamePaused(pause, pauseMusic, pauseInput);
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}
		case GameMessage::MSG_META_STEP_FRAME:
		case GameMessage::MSG_META_STEP_FRAME_ALT:
		{
			if (!TheGameLogic->isInMultiplayerGame())
			{
				TheGameLogic->setGamePaused(FALSE);
				TheGameLogic->setGamePausedInFrame(TheGameLogic->getFrame() + 1, TRUE);
				disp = DESTROY_MESSAGE;
			}
			break;
		}

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)//may be defined in GameCommon.h
    case GameMessage::MSG_CHEAT_RUNSCRIPT1:
    case GameMessage::MSG_CHEAT_RUNSCRIPT2:
    case GameMessage::MSG_CHEAT_RUNSCRIPT3:
    case GameMessage::MSG_CHEAT_RUNSCRIPT4:
    case GameMessage::MSG_CHEAT_RUNSCRIPT5:
    case GameMessage::MSG_CHEAT_RUNSCRIPT6:
    case GameMessage::MSG_CHEAT_RUNSCRIPT7:
    case GameMessage::MSG_CHEAT_RUNSCRIPT8:
    case GameMessage::MSG_CHEAT_RUNSCRIPT9:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if( TheScriptEngine )
				{
					Int script = t - GameMessage::MSG_CHEAT_RUNSCRIPT1 + 1;
					AsciiString scriptName;
					scriptName.format("KEY_F%d", script);
					TheScriptEngine->runScript(scriptName);
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugRunScript", L"Run script %d", script) );
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_TOGGLE_SPECIAL_POWER_DELAYS:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if( TheGlobalData )
				{

					TheWritableGlobalData->m_specialPowerUsesDelay = 1 - TheGlobalData->m_specialPowerUsesDelay;

					if (TheGlobalData->m_specialPowerUsesDelay)
						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSpecialPowerDelaysOn", L"Special Power (Superweapon) Delay is ON") );
					else
						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSpecialPowerDelaysOff", L"Special Power (Superweapon) Delay is OFF") );

				}

				disp = DESTROY_MESSAGE;
			}
			break;

		}
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_SWITCH_TEAMS:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if (TheGameLogic->isInGame())
				{
					Int idx;
					for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
					{
						if (ThePlayerList->getNthPlayer(i) == ThePlayerList->getLocalPlayer())
						{
							idx = i;
							break;
						}
					}
					Int idxOrig = idx;
					do
					{
						++idx;
						if (idx >= ThePlayerList->getPlayerCount())
							idx = 0;

						if (idx == idxOrig)
							break;

					} while (ThePlayerList->getNthPlayer(idx) == ThePlayerList->getNeutralPlayer());

					Player* player = ThePlayerList->getNthPlayer(idx);
					rts::changeLocalPlayer(player);
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_KILL_SELECTION:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				// THIS CALLS THE STANDARD DEBUG MESSAGE, WHICH IS CALLED:
				TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_SELECTION );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_INSTANT_BUILD:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				// Doesn't make a valid network message
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				localPlayer->toggleInstantBuild();

				if (localPlayer->buildsInstantly())
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugInstantBuildOn", L"Instant Build is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugInstantBuildOff", L"Instant Build is OFF") );

				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_ADD_CASH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Money *money = localPlayer->getMoney();
				money->deposit( 10000 );
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAddCash", L"Add Cash") );
			}
			break;
		}
    case GameMessage::MSG_CHEAT_GIVE_ALL_SCIENCES:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *player = ThePlayerList->getLocalPlayer();
				if (player)
				{
					giveAllSciences(player);
				}
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugGiveAllSciences", L"Granting all sciences!" ) );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_GIVE_SCIENCEPURCHASEPOINTS:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *player = ThePlayerList->getLocalPlayer();
				if (player)
					player->addSciencePurchasePoints(1);

				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugGiveSciencePurchasePoint", L"Adding a SciencePurchasePoint" ) );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
		case GameMessage::MSG_CHEAT_SHOW_HEALTH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				TheWritableGlobalData->m_showObjectHealth = 1 - TheGlobalData->m_showObjectHealth;

				if (TheGlobalData->m_showObjectHealth)
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectHealthOn", L"Object Health is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectHealthOff", L"Object Health is OFF") );
			}
			break;

		}
		case GameMessage::MSG_CHEAT_TOGGLE_MESSAGE_TEXT:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{

				// toggle the message state
				TheInGameUI->toggleMessages();

				// when messages get turned on, display a message
				if( TheInGameUI->isMessagesOn() )
					TheInGameUI->message("GUI:MessagesOn");

				disp = DESTROY_MESSAGE;
			}
			break;

		}


		// TheSuperHackers @feature One cheat that does the four things usually wanted together at the
		// start of a test: instant build, a pile of cash, this general's own sciences, and the map
		// revealed. Single player only, like the individual cheats it stands in for -- none of them
		// are network messages, so firing them on one client only would desync the game.
		case GameMessage::MSG_CHEAT_KITCHEN_SINK:
		{
			if( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();

				// One state drives the whole cheat, so everything flips together.
				Bool enable = !localPlayer->buildsInstantly();

				for( Int n = 0; n < ThePlayerList->getPlayerCount(); ++n )
				{
					Player* player = ThePlayerList->getNthPlayer(n);
					if( player->getPlayerType() == PLAYER_HUMAN )
						player->enableInstantBuild(enable);
				}

				// Rank goes first, and this ordering matters. Lowering a rank calls resetRank, which
				// wipes purchased sciences -- granting them before setting the rank would hand them
				// over and then immediately take them away again.
				const Int maxRank = TheRankInfoStore ? TheRankInfoStore->getRankLevelCount() : 1;
				localPlayer->setRankLevel( enable ? maxRank : 1 );

				if( enable )
				{
					if( Money *money = localPlayer->getMoney() )
						money->deposit( 999999 );

					// only this general's own tree, not every science in the game
					giveOwnFactionSciences( localPlayer );

					ThePartitionManager->revealMapForPlayerPermanently( localPlayer->getPlayerIndex() );
				}

				TheInGameUI->messageNoFormat( enable
					? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugKitchenSinkOn",
							L"Cash, sciences, max rank, map revealed. Instant Build is ON")
					: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugKitchenSinkOff",
							L"Rank reset. Instant Build is OFF") );

				disp = DESTROY_MESSAGE;
			}
			break;
		}

		// TheSuperHackers @feature Free the camera from its zoom limit, so the view can pull much
		// further out than normal. The engine already has this as a debug command, but that one
		// lives inside an #if defined(RTS_DEBUG) block and is compiled out of a cheats enabled
		// release build, where only _ALLOW_DEBUG_CHEATS_IN_RELEASE is set -- so it was unreachable
		// in practice. This sits with the other cheats instead, where it actually builds.
		//
		// Purely a client side view setting: it changes nothing in the simulation, so unlike the
		// other cheats here it is safe in multiplayer and needs no isInMultiplayerGame guard.
		case GameMessage::MSG_CHEAT_TOGGLE_ZOOM_LOCK:
		{
			if( TheTacticalView )
			{
				TheTacticalView->setZoomLimited( !TheTacticalView->isZoomLimited() );

				TheInGameUI->messageNoFormat( TheTacticalView->isZoomLimited()
					? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraZoomLimitOn", L"Camera Zoom Limit is ON")
					: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraZoomLimitOff", L"Camera Zoom Limit is OFF") );
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle rendering off and on. Retail has this as a debug command,
		// but the handler lives inside an #if defined(RTS_DEBUG) block and so is compiled out of a
		// cheats enabled release build, where only _ALLOW_DEBUG_CHEATS_IN_RELEASE is set -- the same
		// trap that made the combined cheat unreachable. Moved here, where it actually builds.
		//
		// Everything it touches is already unguarded: m_disableRender, its initialiser, and the
		// W3DDisplay check that reads it. Only the handler was missing.
		//
		// Client side only -- it stops the frame being drawn and posts nothing to the logic, so the
		// simulation keeps running and this is multiplayer and replay safe.
		case GameMessage::MSG_META_DEMO_TOGGLE_RENDER:
		{
			TheWritableGlobalData->m_disableRender = !TheGlobalData->m_disableRender;
			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle the object name overlay: the template (INI) name drawn
		// above every object on screen, selected or not. A modding aid -- it answers "what is this
		// thing actually called" without leaving the game.
		//
		// The drawing lives in Drawable::drawDebugNameOverlay; this only flips the flag. Client side
		// only, posting nothing to the logic, so it is replay and multiplayer safe.
		case GameMessage::MSG_CHEAT_SHOW_OBJECT_NAME:
		{
			TheInGameUI->toggleObjectNameOverlay();

			switch( TheInGameUI->getObjectNameOverlayMode() )
			{
				case InGameUI::OBJECT_NAME_OVERLAY_NAME:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:DebugObjectNameOverlayOn", L"Object Names are ON") );
					break;
				case InGameUI::OBJECT_NAME_OVERLAY_SUBOBJECTS:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:DebugObjectNameOverlaySubObjects", L"Object Names and Sub Objects are ON") );
					break;
				default:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:DebugObjectNameOverlayOff", L"Object Names are OFF") );
					break;
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle the particle name overlay: the names of the particle systems
		// running on each object, stacked above it. Useful for tracking down which FX an effect
		// actually is before editing it.
		//
		// Note this scans every live particle system once per drawn object, so it is heavier than the
		// object name overlay and is best switched on only while looking for something.
		case GameMessage::MSG_CHEAT_SHOW_PARTICLE_NAMES:
		{
			TheInGameUI->toggleParticleNameOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isParticleNameOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugParticleNameOverlayOn", L"Particle Names are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugParticleNameOverlayOff", L"Particle Names are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle the CommandSet overlay: the name of the CommandSet an object
		// uses. A modding aid for the other half of the question the name overlay answers -- which
		// button set this thing actually gets, which for ZH is not always the one its template
		// names, since a module can override it at runtime.
		//
		// Independent of the object name overlay: on its own it takes the name's line, and with the
		// name also on it is drawn to the right of it.
		case GameMessage::MSG_CHEAT_SHOW_COMMAND_SET:
		{
			TheInGameUI->toggleCommandSetOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isCommandSetOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCommandSetOverlayOn", L"Command Sets are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCommandSetOverlayOff", L"Command Sets are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle the WeaponSet overlay: the weapon set flags an object
		// currently has, drawn under the command set. A weapon set is a set of flags rather than one
		// name -- veterancy, player upgrades, rider slots and so on -- so every flag that is set is
		// listed, which is what makes it useful for working out which WeaponSet block an INI actually
		// selected.
		//
		// Independent of the other overlays: it shows only while toggled on.
		case GameMessage::MSG_CHEAT_SHOW_WEAPON_SET:
		{
			TheInGameUI->toggleWeaponSetOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isWeaponSetOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWeaponSetOverlayOn", L"Weapon Sets are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWeaponSetOverlayOff", L"Weapon Sets are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// TheSuperHackers @feature Toggle the armor overlay: the Armor line of the ArmorSet the object
		// currently matches, drawn under the weapons. Which ArmorSet is live depends on the conditions
		// the object meets (veterancy, upgrades, second life), so reading the armor back is the direct
		// way to see which block the engine picked.
		case GameMessage::MSG_CHEAT_SHOW_ARMOR_SET:
		{
			TheInGameUI->toggleArmorSetOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isArmorSetOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugArmorSetOverlayOn", L"Armor is ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugArmorSetOverlayOff", L"Armor is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle the model name overlay, one callout per draw module, pointing at that module's model.
		case GameMessage::MSG_CHEAT_SHOW_MODEL_NAMES:
		{
			TheInGameUI->toggleModelNameOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isModelNameOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugModelNameOverlayOn", L"Model Names are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugModelNameOverlayOff", L"Model Names are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle the window name overlay, drawn over the GUI.
		case GameMessage::MSG_CHEAT_SHOW_WINDOW_NAMES:
		{
			TheInGameUI->toggleWindowNameOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isWindowNameOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWindowNameOverlayOn", L"Window Names are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWindowNameOverlayOff", L"Window Names are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Copy the template (INI) name of the selected objects to the clipboard, so it can be pasted
		// into an INI, a search or a chat instead of being retyped from the Ctrl+[ overlay. One name
		// per line, duplicates folded, so a selected squad copies its type once.
		//
		// Client side only -- it reads the selection and posts nothing to the logic, so it is
		// replay and multiplayer safe.
		case GameMessage::MSG_CHEAT_COPY_OBJECT_NAME:
		{
			std::vector<AsciiString> names;
			const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
			for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
			{
				const Drawable *draw = *it;
				if (draw == NULL || draw->getTemplate() == NULL)
					continue;

				const AsciiString &name = draw->getTemplate()->getName();
				if (std::find(names.begin(), names.end(), name) == names.end())
					names.push_back(name);
			}

			if (names.empty())
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
						"GUI:DebugCopyObjectNameNone", L"Nothing selected to copy") );
			}
			else
			{
				AsciiString text;
				for (size_t i = 0; i < names.size(); ++i)
				{
					if (i > 0)
						text.concat("\r\n");
					text.concat(names[i]);
				}

				if (copyTextToClipboard(text))
				{
					if (names.size() == 1)
						TheInGameUI->message( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT(
								"GUI:DebugCopyObjectNameOne", L"Copied %hs", names[0].str()) );
					else
						TheInGameUI->message( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT(
								"GUI:DebugCopyObjectNameMany", L"Copied %d object names", (Int)names.size()) );
				}
				else
				{
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(
							"GUI:DebugCopyObjectNameFailed", L"Could not open the clipboard") );
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle the laser name overlay, drawn at the middle of each beam.
		case GameMessage::MSG_CHEAT_SHOW_LASER_NAME:
		{
			TheInGameUI->toggleLaserNameOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isLaserNameOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugLaserNameOverlayOn", L"Laser Names are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugLaserNameOverlayOff", L"Laser Names are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle the laser beam block overlay, stacked under the laser name.
		case GameMessage::MSG_CHEAT_SHOW_LASER_BEAM_BLOCK:
		{
			TheInGameUI->toggleLaserBeamBlockOverlay();

			TheInGameUI->messageNoFormat( TheInGameUI->isLaserBeamBlockOverlayOn()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugLaserBeamBlockOverlayOn", L"Laser Beam Blocks are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugLaserBeamBlockOverlayOff", L"Laser Beam Blocks are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Cycle the camera cheat: default -> free camera -> chase the selected object -> default.
		// Purely a view change on this client, like the overlays above, so it carries no
		// multiplayer guard. The selection is resolved here and handed to the view so the view
		// stays independent of the UI.
		case GameMessage::MSG_CHEAT_CYCLE_CAMERA_MODE:
		{
			ObjectID selectedID = INVALID_ID;
			for( Drawable *draw = TheGameClient->firstDrawable(); draw; draw = draw->getNextDrawable() )
			{
				if( draw->isSelected() && draw->getObject() )
				{
					selectedID = draw->getObject()->getID();
					break;
				}
			}

			TheTacticalView->cycleCameraMode( selectedID );

			const char *stateKey;
			const WideChar *stateText;
			if( !TheTacticalView->isCameraCheatModeActive() )
			{
				stateKey = "GUI:DebugCameraDefault";
				stateText = L"Camera: Default";
			}
			else if( TheTacticalView->isCameraChaseModeActive() )
			{
				stateKey = "GUI:DebugCameraChase";
				stateText = L"Camera: Chase";
			}
			else if( TheTacticalView->isCameraPerspectiveModeActive() )
			{
				stateKey = "GUI:DebugCameraPerspective";
				stateText = L"Camera: Perspective";
			}
			else if( TheTacticalView->isCameraOrthoModeActive() )
			{
				stateKey = "GUI:DebugCameraOrtho";
				stateText = L"Camera: Orthographic";
			}
			else
			{
				stateKey = "GUI:DebugCameraFree";
				stateText = L"Camera: Free";
			}
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE(stateKey, stateText) );

			disp = DESTROY_MESSAGE;
			break;
		}

		// Cycle the skybox through the preset texture sets: the one the map started with, then
		// each shipped set. Purely a texture swap on this client.
		case GameMessage::MSG_CHEAT_CYCLE_SKYBOX:
		{
			const UnsignedInt preset = TheTerrainVisual ? TheTerrainVisual->cycleSkyboxPreset() : 0;

			switch (preset)
			{
				case 1:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSkyboxMorning", L"Skybox: Morning") );
					break;
				case 2:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSkyboxMoon", L"Skybox: Moon") );
					break;
				default:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSkyboxMapDefault", L"Skybox: Map Default") );
					break;
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		// Cycle the terrain draw mode: normal, hidden over black, hidden over green -- a green
		// screen for capturing units. Purely a draw change on this client.
		case GameMessage::MSG_CHEAT_CYCLE_TERRAIN_MODE:
		{
			const UnsignedInt mode = TheTerrainVisual ? TheTerrainVisual->cycleTerrainHideMode() : 0;

			switch (mode)
			{
				case 1:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugTerrainBlack", L"Terrain: Black") );
					break;
				case 2:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugTerrainGreen", L"Terrain: Green Screen") );
					break;
				default:
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugTerrainNormal", L"Terrain: Normal") );
					break;
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle back face culling for everything drawn on this client.
		case GameMessage::MSG_CHEAT_TOGGLE_FACE_CULLING:
		{
			if (TheDisplay->toggleFaceCulling())
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFaceCullingOn", L"Face Culling: On") );
			}
			else
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFaceCullingOff", L"Face Culling: Off (Double Sided)") );
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		case GameMessage::MSG_CHEAT_PATHFIND_LOG_ON:
		case GameMessage::MSG_CHEAT_PATHFIND_LOG_OFF:
		{
			const Bool enable = (t == GameMessage::MSG_CHEAT_PATHFIND_LOG_ON);
			if (TheAI && TheAI->pathfinder())
			{
				TheAI->pathfinder()->setStatsLogging(enable);
			}
			if (enable)
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:PathfindLogOn", L"Pathfinding log: On (pathfinding.txt)") );
			}
			else
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:PathfindLogOff", L"Pathfinding log: Off") );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		// Toggle the overlay listing each frame's draw calls by shader.
		case GameMessage::MSG_CHEAT_SHOW_SHADER_DRAWS:
		{
			TheInGameUI->messageNoFormat( TheDisplay->toggleShaderDrawCounts()
				? TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugShaderDrawsOn", L"Shader Draws are ON")
				: TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugShaderDrawsOff", L"Shader Draws are OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

#if RTS_ZEROHOUR
		case GameMessage::MSG_CHEAT_TOGGLE_SHADER_PANEL:
		{
			TheInGameUI->toggleShaderDebugPanel();
			disp = DESTROY_MESSAGE;
			break;
		}
#endif

#endif

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_AI_DEBUG:
		{
			// The terrain view also switches off column moves in AIGroup, so it stays out of multiplayer
			if (!TheGameLogic->isInMultiplayerGame())
			{
				static const char *const modeNames[AI_DEBUG_END] = { "Off", "Paths", "Terrain", "Cells", "Ground paths", "Zones" };
				TheWritableGlobalData->m_debugAI = (AIDebugOptions)((Int)TheGlobalData->m_debugAI + 1);
				if (TheGlobalData->m_debugAI >= AI_DEBUG_END)
				{
					TheWritableGlobalData->m_debugAI=AI_DEBUG_NONE;
				}

				UnicodeString line;
				line.format(L"Level %d, %hs", TheGlobalData->m_debugAI, modeNames[TheGlobalData->m_debugAI]);
				if (TheGlobalData->m_debugAI != AI_DEBUG_NONE)
				{
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugAiDebugOn", L"Debug AI Mode is %s", line.str()) );
				}
				else
				{
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAiDebugOff", L"Debug AI Mode is OFF") );
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}
#endif

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_FORCEMOVE:
			DEBUG_ASSERTCRASH(!TheInGameUI->isInForceMoveToMode(), ("forceMoveToMode mismatch"));
			TheInGameUI->setForceMoveMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_FORCEMOVE:
			DEBUG_ASSERTCRASH(TheInGameUI->isInForceMoveToMode(), ("forceMoveToMode mismatch"));
			TheInGameUI->setForceMoveMode( false );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_WAYPOINTS:
//			DEBUG_ASSERTCRASH( !TheInGameUI->isInWaypointMode(), ("Setting m_waypointMode but it's already set!") );
			TheInGameUI->setWaypointMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_PREFER_SELECTION:
			TheInGameUI->setPreferSelectionMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_PREFER_SELECTION:
			TheInGameUI->setPreferSelectionMode( false );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_WAYPOINTS:
//			DEBUG_ASSERTCRASH( TheInGameUI->isInWaypointMode(), ("Clearing m_waypointMode but it's already clear!") );
			TheInGameUI->setWaypointMode( false );
			break;


		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_FORCEATTACK:
			TheInGameUI->setForceAttackMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_FORCEATTACK:
			TheInGameUI->setForceAttackMode( false );
			break;

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_ALL_CHEER:
		{
			if ( TheGameLogic->isInMultiplayerGame() )
			{
				TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_allCheerSound);
				disp = DESTROY_MESSAGE;
				TheMessageStream->appendMessage( GameMessage::MSG_DO_CHEER );
			}
			break;
		}

		// --------------------------------------------------------------------------------------------
#if defined(GENERALS_ONLINE)
		// GO's keys (F10, F5 and Insert by default), rebindable in the Hotkeys screen's GO section
		case GameMessage::MSG_META_TOGGLE_NETWORK_STATS:
			NGMP_OnlineServicesManager::ToggleAdvancedNetworkStats();
			disp = DESTROY_MESSAGE;
			break;

		case GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY:
		case GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY_ALT:
			GameSpyToggleOverlay( GSOVERLAY_BUDDY );
			disp = DESTROY_MESSAGE;
			break;

		case GameMessage::MSG_META_TAKE_SCREENSHOT_ALT:
#endif
		case GameMessage::MSG_META_TAKE_SCREENSHOT:
		{
			if (TheDisplay)
				TheDisplay->takeScreenShot(SCREENSHOT_JPEG, TheGlobalData->m_jpegQuality);
			disp = DESTROY_MESSAGE;
			break;
		}

		case GameMessage::MSG_META_TAKE_SCREENSHOT_PNG:
		{
			if (TheDisplay)
				TheDisplay->takeScreenShot(SCREENSHOT_PNG);
			disp = DESTROY_MESSAGE;
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_SELECTED_GROUP:
		case GameMessage::MSG_SELECT_TEAM0:
		case GameMessage::MSG_SELECT_TEAM1:
		case GameMessage::MSG_SELECT_TEAM2:
		case GameMessage::MSG_SELECT_TEAM3:
		case GameMessage::MSG_SELECT_TEAM4:
		case GameMessage::MSG_SELECT_TEAM5:
		case GameMessage::MSG_SELECT_TEAM6:
		case GameMessage::MSG_SELECT_TEAM7:
		case GameMessage::MSG_SELECT_TEAM8:
		case GameMessage::MSG_SELECT_TEAM9:
		{
			// weed out unit responses for things we don't own.
			DrawableList listOfUnits = *TheInGameUI->getAllSelectedDrawables();
			for (DrawableListIt it = listOfUnits.begin(); it != listOfUnits.end(); /* empty */) {
				Drawable *draw = *it;
				if (draw->getObject() && draw->getObject()->isLocallyControlled()) {
					// This thing can emit a unit response.
					++it;
					continue;
				} else {
					// remove it.
					it = listOfUnits.erase(it);
				}
			}
			if (!listOfUnits.empty()) {
				pickAndPlayUnitVoiceResponse( &listOfUnits, GameMessage::MSG_CREATE_SELECTED_GROUP );
			}

			m_teamExists = true;
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND:
		case GameMessage::MSG_ADD_TEAM0:
		case GameMessage::MSG_ADD_TEAM1:
		case GameMessage::MSG_ADD_TEAM2:
		case GameMessage::MSG_ADD_TEAM3:
		case GameMessage::MSG_ADD_TEAM4:
		case GameMessage::MSG_ADD_TEAM5:
		case GameMessage::MSG_ADD_TEAM6:
		case GameMessage::MSG_ADD_TEAM7:
		case GameMessage::MSG_ADD_TEAM8:
		case GameMessage::MSG_ADD_TEAM9:
		{
			m_teamExists = true;
			break;
		}


		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_DESTROY_SELECTED_GROUP:
		{
			m_teamExists = false;
			break;
		}

		// --------------------------------------------------------------------------------------------
		// An object is mouseover'd.  A hint of a command may be generated if something is selected
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT:
		{
			const CommandButton *command = TheInGameUI->getGUICommand();
			if( TheInGameUI->getSelectCount() > 0
					|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT) ) // If something is selected
			{
				/// @todo This as well as the one in GameMessage::MSG_DRAWABLE_PICKED below should possibly have a generalized CanAttack instead of simply checking isEnemyOf
				Drawable *draw = TheGameClient->findDrawableByID( msg->getArgument( 0 )->drawableID );
				if( draw )
				{

					//
					// given the drawable that we have moused over, pretend that we have actually
					// "picked" that drawable, depending on what we have selected, our mode,
					// and what "picked" is we would do a command ... but here we only will generate
					// the hint message, *not* the actual command
					//
					if (TheInGameUI->isInForceAttackMode()) {
						evaluateForceAttack(draw, draw->getPosition(), DO_HINT );
					} else {
						evaluateContextCommand( draw, draw->getPosition(), DO_HINT, TheKeyboard->getModifierFlags() );
					}

					// Do not eat this message, as it itself has another purpose in HintSpy

				}
			}
		}
		break;

		//-----------------------------------------------------------------------------
		// Terrain is mouseover'd.  A hint of a command may be generated if something is selected
		//
		case GameMessage::MSG_MOUSEOVER_LOCATION_HINT:
		{
			Coord3D position = msg->getArgument( 0 )->location;

			//
			// This message occurs whenever the mouse cursor moves off of a drawable, in which
			// case we want to turn off the pick hint.
			//
			if (TheInGameUI->isInForceAttackMode()) {
				evaluateForceAttack( nullptr, &position, DO_HINT );
			} else {
				evaluateContextCommand( nullptr, &position, DO_HINT, TheKeyboard->getModifierFlags() );
			}

			// Do not eat this message, as it will do something itself at HintSpy
			break;

		}

		//-----------------------------------------------------------------------------
		// TheSuperHackers @bugfix Treat the raw double click event identical to the raw button down event
		// because it implicitly is a raw button down event as well. Failing to do so would mess with the
		// button timings in later events on button up.
		case GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			// There are two ways in which we can ignore this as a deselect:
			// 1) 2-D position on screen
			// 2) Time has exceeded the time which we allow for this to be a click.
			m_rightMouseDownAnchor = msg->getArgument( 0 )->pixel;
			m_rightMouseDownTimeMs = (UnsignedInt) msg->getArgument( 2 )->integer;

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			// register this event for determining if the click was fast or short enough not to be a drag
			m_rightMouseUpAnchor = msg->getArgument( 0 )->pixel;
			m_rightMouseUpTimeMs = (UnsignedInt) msg->getArgument( 2 )->integer;

			//Kris: July 7, 2003. Added this code to deselect build placement mode when right clicked. This fixes
			//a bug where you couldn't cancel the sneak attack mode via right click. This only happened when you
			//didn't have anything selected which is possible via the shortcut bar. Normally, it would get deselected
			//via the deselect drawable code.
			if( TheMouse->isClick(
				m_rightMouseDownTimeMs, m_rightMouseUpTimeMs,
				m_rightMouseDownAnchor, m_rightMouseUpAnchor) )
			{
				TheInGameUI->placeBuildAvailable( nullptr, nullptr );
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK:
		{
			if( TheGlobalData->m_useAlternateMouse && TheGlobalData->m_doubleClickAttackMove )
			{
				// create the message and append arguments for a guard location
				Coord3D pos;
				if( !TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos ) )
					break;

				GameMessage *newMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_GUARD_POSITION );
				newMsg->appendLocationArgument(pos);
				newMsg->appendIntegerArgument(GUARDMODE_NORMAL);
				newMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

				ThePlayerList->getLocalPlayer()->getAcademyStats()->recordDoubleClickAttackMoveOrderGiven();

        TheInGameUI->triggerDoubleClickAttackMoveGuardHint();

				break;
			}
			FALLTHROUGH; //intentional fall through
		}
		case GameMessage::MSG_MOUSE_RIGHT_CLICK:
		{
			// right click is only actioned here if we're in alternate mouse mode
			if (TheGlobalData->m_useAlternateMouse
				&& TheMouse->isClick(
					m_rightMouseDownTimeMs, m_rightMouseUpTimeMs,
					m_rightMouseDownAnchor, m_rightMouseUpAnchor))
			{
				// NOTE: RIGHT_CLICK is not transmitted if AREA_SELECTION or DRAWABLE_PICKED occurs.
				// If we see this msg, no object was clicked on, therefore clicked on ground.
				// Issue move command to all currently selected objects.

				// sanity
				if( TheTacticalView == nullptr )
					break;

				// translate from screen coordinates to terrain coords
				Coord3D pos;
				if( !TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos ) )
					break;

				const CommandButton *command = TheInGameUI->getGUICommand();
				Bool isPoint = (msg->getArgument(0)->pixelRegion.height() == 0 && msg->getArgument(0)->pixelRegion.width() == 0);
				Bool controllable = TheInGameUI->areSelectedObjectsControllable()
														|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT);
				if (isPoint && controllable)
				{
					UnsignedInt pickType = getPickTypesForContext( TheInGameUI->isInForceAttackMode() );
					Drawable *draw = TheTacticalView->pickDrawable(&msg->getArgument(0)->pixelRegion.lo,
																													TheInGameUI->isInForceAttackMode(),
																													(PickType) pickType);

					// TheSuperHackers @bugfix Stubbjax 07/08/2025 Prevent dead units blocking positional context commands
					Object* obj = draw ? draw->getObject() : nullptr;
					if (!obj || (obj->isEffectivelyDead() && !obj->isKindOf(KINDOF_ALWAYS_SELECTABLE)))
					{
						draw = nullptr;
					}

					if (TheInGameUI->isInForceAttackMode()) {
						evaluateForceAttack( draw, &pos, DO_COMMAND );
					} else {
						evaluateContextCommand( draw, &pos, DO_COMMAND, TheKeyboard->getModifierFlags() );
					}

					disp = DESTROY_MESSAGE;
					TheInGameUI->clearArmedMoveMode();
				}
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK:
		{
			if( !TheGlobalData->m_useAlternateMouse && TheGlobalData->m_doubleClickAttackMove )
			{
				// create the message and append arguments for a guard location
				Coord3D pos;
				if( !TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos ) )
					break;

				GameMessage *newMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_GUARD_POSITION );
				newMsg->appendLocationArgument(pos);
				newMsg->appendIntegerArgument(GUARDMODE_NORMAL);
				newMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

				ThePlayerList->getLocalPlayer()->getAcademyStats()->recordDoubleClickAttackMoveOrderGiven();

        TheInGameUI->triggerDoubleClickAttackMoveGuardHint();

				break;
			}
			FALLTHROUGH; //intentional fall through
		}
		case GameMessage::MSG_MOUSE_LEFT_CLICK:
		{
			// NOTE: LEFT_CLICK is not transmitted if AREA_SELECTION or DRAWABLE_PICKED occurs.
			// If we see this msg, no object was clicked on, therefore clicked on ground.
			// Issue move command to all currently selected objects.

			// sanity
			if( TheTacticalView == nullptr )
				break;

			// translate from screen coordinates to terrain coords
			Coord3D pos;
			if( !TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos ) )
				break;

			const CommandButton *command = TheInGameUI->getGUICommand();
			// maintain this as the list of GUI button initiated commands that fire with left click in alt mouse mode
  			Bool isFiringGUICommand = (command	&& (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER
  												|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT
 												|| command->getCommandType() == GUI_COMMAND_FIRE_WEAPON
												|| command->getCommandType() == GUI_COMMAND_COMBATDROP
												|| command->getCommandType() == GUICOMMANDMODE_HIJACK_VEHICLE
												|| command->getCommandType() == GUICOMMANDMODE_CONVERT_TO_CARBOMB));

			// in alternate mouse mode, this left click is only actioned here if we're firing a gui command
			if ((TheGlobalData->m_useAlternateMouse) && (! isFiringGUICommand))
				break;

			Bool isPoint = (msg->getArgument(0)->pixelRegion.height() == 0 && msg->getArgument(0)->pixelRegion.width() == 0);
			Bool controllable = TheInGameUI->areSelectedObjectsControllable()
													|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT);
			if (isPoint && controllable)
			{
				UnsignedInt pickType = getPickTypesForContext( TheInGameUI->isInForceAttackMode() );
				Drawable *draw = TheTacticalView->pickDrawable(&msg->getArgument(0)->pixelRegion.lo,
																												TheInGameUI->isInForceAttackMode(),
																												(PickType) pickType);

				// TheSuperHackers @bugfix Stubbjax 07/08/2025 Prevent dead units blocking positional context commands
				Object* obj = draw ? draw->getObject() : nullptr;
				if (!obj || (obj->isEffectivelyDead() && !obj->isKindOf(KINDOF_ALWAYS_SELECTABLE)))
				{
					draw = nullptr;
				}

				if (TheInGameUI->isInForceAttackMode()) {
					evaluateForceAttack( draw, &pos, DO_COMMAND );
				} else {
					evaluateContextCommand( draw, &pos, DO_COMMAND, TheKeyboard->getModifierFlags() );
				}

				disp = DESTROY_MESSAGE;
				TheInGameUI->clearArmedMoveMode();

				//issueMoveToLocationCommand( &pos, draw, DO_COMMAND );
			}
			break;

		}

		case GameMessage::MSG_META_DEMO_INSTANT_QUIT:
		{
			TheGameLogic->quit(TRUE);
			disp = DESTROY_MESSAGE;
			break;
		}

#if defined(RTS_DEBUG)
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_SWITCH_TEAMS:
		{
			if (TheGameLogic->isInGame())
			{
				Int idx;
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					if (ThePlayerList->getNthPlayer(i) == ThePlayerList->getLocalPlayer())
					{
						idx = i;
						break;
					}
				}
				Int idxOrig = idx;
				do
				{

					++idx;
					if (idx >= ThePlayerList->getPlayerCount())
						idx = 0;

					if (idx == idxOrig)
					{
						break;
					}

				} while (ThePlayerList->getNthPlayer(idx) == ThePlayerList->getNeutralPlayer());

				Player* player = ThePlayerList->getNthPlayer(idx);
				rts::changeLocalPlayer(player);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_SWITCH_TEAMS_BETWEEN_CHINA_USA:
		{
			Player *p = ThePlayerList->getLocalPlayer();
			AsciiString side;
			side.set(p->getSide());

			if(side.compareNoCase("America") == 0)
			{
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					Player *pt = ThePlayerList->getNthPlayer(i);
					if(pt->getSide().compareNoCase("China") == 0)
					{
						rts::changeLocalPlayer(pt);
						break;
					}
				}
			}

			if(side.compareNoCase("China") == 0)
			{
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					Player *pt = ThePlayerList->getNthPlayer(i);
					if(pt->getSide().compareNoCase("America") == 0)
					{
						rts::changeLocalPlayer(pt);
						break;
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOD_DECREASE:
		{
			const Int level = clamp(0, WW3D::Get_Texture_Reduction() - 1, 4);
			TheGameClient->setTextureLOD(level);
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugDecreaseLOD", L"Decrease LOD") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOD_INCREASE:
		{
			const Int level = clamp(0, WW3D::Get_Texture_Reduction() + 1, 4);
			TheGameClient->setTextureLOD(level);
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugIncreaseLOD", L"Increase LOD") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_HELP:
		{
			// for demo, they don't want the help menu showing up.  I left this as a block comment because we
			// might want to add back in the help menu at some point in time.
/*
			if (TheWindowManager && TheNameKeyGenerator)
			{
				GameWindow *motd = TheWindowManager->winGetWindowFromId(nullptr, (Int)TheNameKeyGenerator->nameToKey("MOTD.wnd:MOTD"));
				if (motd)
					motd->winHide(!motd->winIsHidden());
			}*/

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_BEHIND_BUILDINGS:
		{
			if (TheGlobalData)
			{

				if(TheGameLogic->getShowBehindBuildingMarkers())
				{
					TheGameLogic->setShowBehindBuildingMarkers(FALSE);
					TheInGameUI->message("GUI:ShowBehindBuildings");
				}
				else
				{
					TheGameLogic->setShowBehindBuildingMarkers(TRUE);
					TheInGameUI->message("GUI:HideBehindBuildings");
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_LETTERBOX:
		{
			if(TheShell->isShellActive())
			{
				WindowLayout *win = TheShell->top();
				if(win)
				{
					win->hide(!win->isHidden());
				}

			}
			else
			{
				Bool hide = false;
				if (TheWindowManager)
				{
					Int id = (Int)TheNameKeyGenerator->nameToKey("ControlBar.wnd:ControlBarParent");
					GameWindow *window = TheWindowManager->winGetWindowFromId(nullptr, id);

					if (window)
						hide = !window->winIsHidden();
				}

				if (hide)
					HideControlBar();
				else
					ShowControlBar(FALSE);
				TheDisplay->toggleLetterBox();
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MESSAGE_TEXT:
		{

			// toggle the message state
			TheInGameUI->toggleMessages();

			// when messages get turned on, display a message
			if( TheInGameUI->isMessagesOn() )
				TheInGameUI->message("GUI:MessagesOn");

			disp = DESTROY_MESSAGE;
			break;

		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MOTION_BLUR_ZOOM:
		{	Int mode;
			if ((mode=TheTacticalView->getViewFilterType()) == FT_VIEW_MOTION_BLUR_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
			}
			else
			{
				static Bool saturate = false;
				FilterModes mode = FM_VIEW_MB_IN_AND_OUT_ALPHA;
				if (saturate) {
					mode = FM_VIEW_MB_IN_AND_OUT_SATURATE;
				}
				if (TheTacticalView->getCameraLock()!=0) {
					mode = FM_VIEW_MB_PAN_ALPHA;
				}
				saturate = !saturate;
				Coord3D curpos = TheTacticalView->getPosition();
				curpos.x += 200;
				curpos.y += 200;
				TheTacticalView->setViewFilterPos(&curpos);

				TheTacticalView->setViewFilterMode(mode);
				TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_BW_VIEW:
		{   //We're not testing BW mode anymore, so use this message for toggling wireframe mode.
			static Int mode=0;
			if (mode == 0)
			{	//First turn on wireframe
				TheTacticalView->set3DWireFrameMode(TRUE);
				mode++;
				disp = DESTROY_MESSAGE;
				break;
			}
			if (mode == 1)
			{
				if ((TheTacticalView->getViewFilterType()) == FT_VIEW_CROSSFADE)
				{	//mode already set, turn it off
					TheTacticalView->setViewFilterMode(FM_NULL_MODE);
					TheTacticalView->setViewFilter(FT_NULL_FILTER);
					TheTacticalView->setFadeParameters(0,-1);
				}
				else
				{
					TheScriptEngine->doFreezeTime();

					//TheTacticalView->setViewFilterMode(FM_VIEW_CROSSFADE_CIRCLE);
					TheTacticalView->setViewFilterMode(FM_VIEW_CROSSFADE_FB_MASK);
					TheTacticalView->setViewFilter(FT_VIEW_CROSSFADE);
					TheTacticalView->setFadeParameters(60,-1);
					TheTacticalView->set3DWireFrameMode(FALSE);
					mode++;
				}
			}
			else
			if (mode == 2)
			{
				TheScriptEngine->doUnfreezeTime();
				mode=0;	//reset everything
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_RED_VIEW:
		{
			if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				TheTacticalView->setFadeParameters(30,-1);
			}
			else
			{
				TheTacticalView->setViewFilterMode(FM_VIEW_BW_RED_AND_WHITE);
				TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
				TheTacticalView->setFadeParameters(30,1);
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_GREEN_VIEW:
		{
			if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				TheTacticalView->setFadeParameters(30,-1);
			}
			else
			{
				TheTacticalView->setViewFilterMode(FM_VIEW_BW_GREEN_AND_WHITE);
				TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
				TheTacticalView->setFadeParameters(30,1);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_CYCLE_LOD_LEVEL:
		{
			static Int level=DYNAMIC_GAME_LOD_VERY_HIGH;

			level--;

			if (level < DYNAMIC_GAME_LOD_LOW)
				level = DYNAMIC_GAME_LOD_VERY_HIGH;

			TheGameLODManager->setDynamicLODLevel((DynamicGameLODLevel)level);

			const char* lodLevelName = TheGameLODManager->getDynamicGameLODLevelName((DynamicGameLODLevel)level);
			TheInGameUI->message( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugDynamicLODLevel", L"Dynamic Game Detail %hs", lodLevelName) );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DUMP_ASSETS:
		{
			TheDisplay->dumpModelAssets("UsedMapAssets.txt");
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SHADOW_VOLUMES:
		{
			TheWritableGlobalData->m_useShadowVolumes = !TheGlobalData->m_useShadowVolumes;
			TheWritableGlobalData->m_useShadowDecals = !TheGlobalData->m_useShadowDecals;

			if (TheWritableGlobalData->m_useShadowVolumes)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugShadowVolumesOn", L"Shadow Volumes is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugShadowVolumesOff", L"Shadow Volumes is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_FOGOFWAR:
		{
			TheWritableGlobalData->m_fogOfWarOn = !TheGlobalData->m_fogOfWarOn;

			if (TheWritableGlobalData->m_fogOfWarOn)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFogOfWarOn", L"Fog of War is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFogOfWarOff", L"Fog of War is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_SELECTION:
		{
			TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_SELECTION );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_ALL_ENEMIES:
		{
			for (Drawable *d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
			{
				Object* obj = d->getObject();
				if (obj && obj->getControllingPlayer() && obj->getControllingPlayer()->getRelationship(ThePlayerList->getLocalPlayer()->getDefaultTeam()) == ENEMIES)
				{
					obj->kill();
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_GIVE_VETERANCY:
		case GameMessage::MSG_META_DEBUG_TAKE_VETERANCY:
		{
			if (TheGameLogic->isInMultiplayerGame())
				break;

			const DrawableList *list = TheInGameUI->getAllSelectedDrawables();
			for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
			{
				Drawable *pDraw = *it;
				if (!pDraw)
					continue;

				Object *pObject = pDraw->getObject();
				if (!pObject)
					continue;

				ExperienceTracker *et = pObject->getExperienceTracker();
				if (!et || !et->isTrainable())
					continue;

				VeterancyLevel oldVet = et->getVeterancyLevel();
				VeterancyLevel newVet = oldVet;

				if (t == GameMessage::MSG_META_DEBUG_GIVE_VETERANCY)
				{
					if (oldVet < LEVEL_LAST)
					{
						newVet = (VeterancyLevel)((Int)oldVet + 1);
					}
				}
				else
				{
					if (oldVet > LEVEL_FIRST)
					{
						newVet = (VeterancyLevel)((Int)oldVet - 1);
					}
				}

				et->setVeterancyLevel(newVet);
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_INCR_ANIM_SKATE_SPEED:
		{
			TheSkateDistOverride += 0.25f;
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugAnimSkateSpeed", L"Skate Distance Override is now %f", TheSkateDistOverride ) );
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DECR_ANIM_SKATE_SPEED:
		{
			TheSkateDistOverride -= 0.25f;
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugAnimSkateSpeed", L"Skate Distance Override is now %f", TheSkateDistOverride ) );
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_CYCLE_EXTENT_TYPE:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_TYPE;
			if (!extentModAmount) extentModAmount = 1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = 1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = -1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = 1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = -1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = 1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = -1.0f;
			FALLTHROUGH;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
		{
			const DrawableList *list = TheInGameUI->getAllSelectedDrawables();
			for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
			{
				Drawable *pDraw = *it;
				if (pDraw)
				{
					Object *pObject = pDraw->getObject();
					if (pObject)
					{
						const GeometryInfo oldGeometry = pObject->getGeometryInfo();

						GeometryInfo newGeometry = oldGeometry;
						newGeometry.tweakExtents(extentModType, extentModAmount);

						TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugExtent",
							L"Extent %hs --> %hs   %d %g", oldGeometry.getDescriptiveString().str(), newGeometry.getDescriptiveString().str(), extentModType, extentModAmount ) );

						DEBUG_LOG(("Extent %s --> %s   %d %g", oldGeometry.getDescriptiveString().str(), newGeometry.getDescriptiveString().str(), extentModType, extentModAmount));

						pObject->setGeometryInfo( newGeometry );
					}
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_SELECTION:
		{
			ObjectID id = INVALID_ID;
			Drawable *d = nullptr;
			for (d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
			{
				if (d->isSelected() && d->getObject())
				{
					id = d->getObject()->getID();
					break;
				}
			}
			if (id != 0 && TheTacticalView->getCameraLock() == id)
			{
				if (TheTacticalView->getCameraLockDrawable())
				{
					id = INVALID_ID;	// toggle it
					d=nullptr;
					TheTacticalView->forceRedraw();	//reset camera to normal
				}
			}
			else
			{
				d = nullptr;
			}
			TheTacticalView->userSetCameraLock(id);
			TheTacticalView->userSetCameraLockDrawable(d);
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SOUND:
		{
			if (TheAudio->isOn(AudioAffect_Sound))
			{
				TheDisplay->stopMovie();
				TheInGameUI->stopMovie();
				TheAudio->setOn(false, AudioAffect_All);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSoundOff", L"Sound is OFF") );
			}
			else
			{
				TheAudio->setOn(true, AudioAffect_All);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSoundOn", L"Sound is ON") );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_TRACKMARKS:
		{
			TheWritableGlobalData->m_makeTrackMarks = !TheGlobalData->m_makeTrackMarks;

			if (TheGlobalData->m_makeTrackMarks)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugTrackMarksOn", L"Track Marks is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugTrackMarksOff", L"Track Marks is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_WATERPLANE:
		{
			TheWritableGlobalData->m_useWaterPlane = !TheGlobalData->m_useWaterPlane;

			if (TheGlobalData->m_useWaterPlane)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWaterPlaneOn", L"Water Plane is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWaterPlaneOff", L"Water Plane is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TIME_OF_DAY:
		{
			TimeOfDay tod = TimeOfDay((Int) TheGlobalData->m_timeOfDay + 1);
			if (tod < TIME_OF_DAY_FIRST || tod >= TIME_OF_DAY_COUNT)
				tod = TIME_OF_DAY_FIRST;
			if (TheGameClient->switchTimeOfDay(tod))
			{
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugTimeOfDay", L"Time of Day set to %hs", TimeOfDayNames[tod]) );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_REMOVE_PREREQ:
		{
			// Doesn't make a valid network message
			// TheSuperHackers @info In multiplayer, all clients need to enable this cheat at the same time, otherwise game will mismatch
			Bool enable = !ThePlayerList->getLocalPlayer()->ignoresPrereqs();

			for (Int n = 0; n < ThePlayerList->getPlayerCount(); ++n)
			{
				Player* player = ThePlayerList->getNthPlayer(n);
				if (player->getPlayerType() == PLAYER_HUMAN)
					player->enableIgnorePrereqs(enable);
			}

			if (enable)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugIgnorePrereqOn", L"Ignore Prerequisites is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugIgnorePrereqOff", L"Ignore Prerequisites is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_INSTANT_BUILD:
		{
			// Doesn't make a valid network message
			// TheSuperHackers @info In multiplayer, all clients need to enable this cheat at the same time, otherwise game will mismatch
			if (!TheGameLogic->isInMultiplayerGame() || !hasThingsInProduction(PLAYER_HUMAN))
			{
				Bool enable = !ThePlayerList->getLocalPlayer()->buildsInstantly();

				for (Int n = 0; n < ThePlayerList->getPlayerCount(); ++n)
				{
					Player* player = ThePlayerList->getNthPlayer(n);
					if (player->getPlayerType() == PLAYER_HUMAN)
						player->enableInstantBuild(enable);
				}

				if (enable)
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugInstantBuildOn", L"Instant Build is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugInstantBuildOff", L"Instant Build is OFF") );

				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_FREE_BUILD:
		{
			// Doesn't make a valid network message
			// TheSuperHackers @info In multiplayer, all clients need to enable this cheat at the same time, otherwise game will mismatch
			Bool enable = !ThePlayerList->getLocalPlayer()->buildsForFree();

			for (Int n = 0; n < ThePlayerList->getPlayerCount(); ++n)
			{
				Player* player = ThePlayerList->getNthPlayer(n);
				if (player->getPlayerType() == PLAYER_HUMAN)
					player->enableFreeBuild(enable);
			}

			if (enable)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFreeBuildOn", L"Free Build is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugFreeBuildOff", L"Free Build is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_ADD_CASH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Money *money = localPlayer->getMoney();
				money->deposit( 10000 );
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAddCash", L"Add Cash") );
			}
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_RUNSCRIPT1:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT2:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT3:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT4:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT5:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT6:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT7:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT8:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT9:
		{
			if( TheScriptEngine )
			{
				Int script = t - GameMessage::MSG_META_DEMO_RUNSCRIPT1 + 1;
				AsciiString scriptName;
				scriptName.format("KEY_F%d", script);
				TheScriptEngine->runScript(scriptName);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugRunScript", L"Run script %d", script) );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_AREA_SELECTION:
		{
			for( int i=1; i<msg->getArgumentCount(); i++ )
			{
				Object *obj = TheGameLogic->findObjectByID( msg->getArgument( i )->objectID );
				if( obj )
				{
					obj->kill();
				}
			}
		}
		break;

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE1:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE2:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE3:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE4:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE5:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE6:
		case GameMessage::MSG_META_DEMO_NEXT_OBJECTIVE_MOVIE:
		{
			if (TheGameLogic->isInGame())
			{
				if (t == GameMessage::MSG_META_DEMO_NEXT_OBJECTIVE_MOVIE)
				{
					m_objective++;
					if (m_objective > 6)
						m_objective = 1;
				}
				else
				{
					m_objective = t - GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE1 + 1;
				}
				AsciiString name;
				name.format("DemoObjective%02d", m_objective);
				TheInGameUI->playMovie(name);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugObjectiveMove", L"Objective Movie %d", m_objective) );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MILITARY_SUBTITLES:
		{

			TheInGameUI->militarySubtitle("MSG:Testing", 10000);  // use some innocuous string
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MUSIC:
		{
			if (TheAudio->isMusicPlaying())
			{
				TheAudio->stopAudio(AudioAffect_Music);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugMusicOn", L"Music is OFF") );
			} else
			{
				TheAudio->resumeAudio(AudioAffect_Music);
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugMusicOff", L"Music is ON") );
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_MUSIC_NEXT_TRACK:
		{
			AsciiString trackName = TheAudio->nextMusicTrack();
			TheInGameUI->message( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugMusicTrack", L"Playing Track: %hs", trackName.str()) );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_MUSIC_PREV_TRACK:
		{
			AsciiString trackName = TheAudio->prevMusicTrack();
			TheInGameUI->message( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugMusicTrack", L"Playing Track: %hs", trackName.str()) );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
#ifdef PERF_TIMERS
		case GameMessage::MSG_META_DEMO_TOGGLE_METRICS:
		{
			TheWritableGlobalData->m_showMetrics = !TheGlobalData->m_showMetrics;
			if (TheGlobalData->m_showMetrics) {
				TheDisplay->setDebugDisplayCallback(StatMetricsDisplay);
			} else {
				TheDisplay->setDebugDisplayCallback(nullptr);
			}
			break;
		}
#endif // #ifdef PERF_TIMERS

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SUPPLY_CENTER_PLACEMENT:
		{
			TheWritableGlobalData->m_debugSupplyCenterPlacement = !TheWritableGlobalData->m_debugSupplyCenterPlacement;

			if (TheGlobalData->m_debugSupplyCenterPlacement)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSupplyCenterPlacementOn", L"Log SupplyCenter Placement is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSupplyCenterPlacementOff", L"Log SupplyCenter Placement is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_SCIENCEPURCHASEPOINTS:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->addSciencePurchasePoints(1);

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugGiveSciencePurchasePoint", L"Adding a SciencePurchasePoint") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_ALL_SCIENCES:
		{
			// TheSuperHackers @info In multiplayer, all clients need to enable this cheat at the same time, otherwise game will mismatch
			for (Int n = 0; n < ThePlayerList->getPlayerCount(); ++n)
			{
				Player* player = ThePlayerList->getNthPlayer(n);
				if (player->getPlayerType() == PLAYER_HUMAN)
					giveAllSciences(player);
			}

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugGiveAllSciences", L"Granting all sciences!") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_RANKLEVEL:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->setRankLevel(player->getRankLevel() + 1);

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAddRankLevel", L"Adding a Rank Level") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TAKE_RANKLEVEL:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->setRankLevel(player->getRankLevel() - 1);

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSubRankLevel", L"Subtracting a Rank Level") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_CAMERA_DEBUG:
		{
			TheWritableGlobalData->m_debugCamera = !TheGlobalData->m_debugCamera;

			if (TheTacticalView->isZoomLimited())
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraModeOn", L"Debug Camera Mode is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraModeOff", L"Debug Camera Mode is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_NO_DRAW:
		{
			const Bool isZero = TheGlobalData->m_noDraw == 0u;
			TheWritableGlobalData->m_noDraw = isZero ? ~0u : 0u;
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DUMP_PLAYER_OBJECTS:
		case GameMessage::MSG_META_DEBUG_DUMP_ALL_PLAYER_OBJECTS:
		{
			AsciiString line;
			line.format("*******************************");
			TheScriptEngine->AppendDebugMessage(line, FALSE);
			line.format("Dumping player object counts");
			TheScriptEngine->AppendDebugMessage(line, FALSE);
			for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
			{
				Player *pPlayer = ThePlayerList->getNthPlayer(i);
				if (!pPlayer || !pPlayer->getPlayerTemplate() || !pPlayer->getPlayerTemplate()->isPlayableSide())
					continue;

				Int numObjects = 0;
				pPlayer->iterateObjects( countObjects, &numObjects );
				line.format("Player %d (%ls) has %d non-dead objects", i, pPlayer->getPlayerDisplayName().str(), numObjects);
				TheScriptEngine->AppendDebugMessage(line, FALSE);

				if (numObjects && (numObjects <= 5 || t == GameMessage::MSG_META_DEBUG_DUMP_ALL_PLAYER_OBJECTS))
				{
					line = "Objects are:";
					TheScriptEngine->AppendDebugMessage(line, FALSE);
					pPlayer->iterateObjects( printObjects, nullptr );
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_TOGGLE_NETWORK:
		{
			if (TheNetwork != nullptr) {
				TheNetwork->toggleNetworkOn();
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_PARTICLEDEBUG:
		{
			if (TheDisplay->getDebugDisplayCallback() == ParticleSystemDebugDisplay)
				TheDisplay->setDebugDisplayCallback(nullptr);
			else
				TheDisplay->setDebugDisplayCallback(ParticleSystemDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_VISIONDEBUG:
		{
			TheWritableGlobalData->m_debugVisibility = !TheGlobalData->m_debugVisibility;

			if (TheGlobalData->m_debugVisibility)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugVisionModeOn", L"Debug Vision Mode is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugVisionModeOff", L"Debug Vision Mode is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_PROJECTILEDEBUG:
		{
			TheWritableGlobalData->m_debugProjectilePath = !TheGlobalData->m_debugProjectilePath;

			if (TheGlobalData->m_debugProjectilePath)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugProjectilePathModeOn", L"Debug Projectile Path Mode is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugProjectilePathModeOff", L"Debug Projectile Path Mode is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_THREATDEBUG:
		{
			TheWritableGlobalData->m_debugThreatMap = !TheGlobalData->m_debugThreatMap;
			if (TheGlobalData->m_debugThreatMap) {
				TheWritableGlobalData->m_debugCashValueMap = false;
			}

			if (TheGlobalData->m_debugThreatMap)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugThreadMapOn", L"Debug Threat Map is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugThreadMapOff", L"Debug Threat Map is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_CASHMAPDEBUG:
		{
			TheWritableGlobalData->m_debugCashValueMap = !TheGlobalData->m_debugCashValueMap ;
			if (TheGlobalData->m_debugCashValueMap) {
				TheWritableGlobalData->m_debugThreatMap = false;
			}

			if (TheGlobalData->m_debugCashValueMap)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCashValueMapOn", L"Debug Cash Value Map is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCashValueMapOff", L"Debug Cash Value Map is OFF") );

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_GRAPHICALFRAMERATEBAR:
		{
			TheWritableGlobalData->m_debugShowGraphicalFramerate = !TheGlobalData->m_debugShowGraphicalFramerate;
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SHOW_EXTENTS:
			TheWritableGlobalData->m_showCollisionExtents = 1 - TheGlobalData->m_showCollisionExtents;

			if (TheGlobalData->m_showCollisionExtents)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectExtentsOn", L"Show Object Extents is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectExtentsOff", L"Show Object Extents is OFF") );

			break;

    //------------------------------------------------------------------------------- DEMO MESSAGES
    //-----------------------------------------------------------------------------------------
    case GameMessage::MSG_META_DEBUG_SHOW_AUDIO_LOCATIONS:
      TheWritableGlobalData->m_showAudioLocations = 1 - TheGlobalData->m_showAudioLocations;

			if (TheGlobalData->m_showAudioLocations)
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAudioLocationsOn", L"Show Audio Locations is ON") );
			else
				TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugAudioLocationsOff", L"Show Audio Locations is OFF") );

      break;

    //------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SHOW_HEALTH:
		{

			TheWritableGlobalData->m_showObjectHealth = 1 - TheGlobalData->m_showObjectHealth;

				if (TheGlobalData->m_showObjectHealth)
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectHealthOn", L"Object Health is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugObjectHealthOff", L"Object Health is OFF") );
			break;

		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PLAY_CAMEO_MOVIE:
		{
			if (TheGameLogic->isInGame())
			{
				if(TheInGameUI->cameoVideoBuffer() == nullptr)
					TheInGameUI->playCameoMovie("CameoMovie");
				else
					TheInGameUI->stopCameoMovie();
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_ZOOM_LOCK:
		{

			if( TheTacticalView )
			{

				TheTacticalView->setZoomLimited( !TheTacticalView->isZoomLimited() );

				if (TheTacticalView->isZoomLimited())
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraZoomLimitOn", L"Camera Zoom Limit is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugCameraZoomLimitOff", L"Camera Zoom Limit is OFF") );

			}

			disp = DESTROY_MESSAGE;
			break;

		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SPECIAL_POWER_DELAYS:
		{

			if( TheGlobalData )
			{

				TheWritableGlobalData->m_specialPowerUsesDelay = 1 - TheGlobalData->m_specialPowerUsesDelay;

				if (TheGlobalData->m_specialPowerUsesDelay)
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSpecialPowerDelaysOn", L"Special Power (Superweapon) Delay is ON") );
				else
					TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugSpecialPowerDelaysOff", L"Special Power (Superweapon) Delay is OFF") );

			}

			disp = DESTROY_MESSAGE;
			break;

		}

#ifdef ALLOW_SURRENDER
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TEST_SURRENDER:
		{
			GameMessage* msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SURRENDER );
			msg->appendObjectIDArgument(INVALID_ID);			// zero means "surrender to anyone"
			msg->appendBooleanArgument(true);
			disp = DESTROY_MESSAGE;
			break;
		}
#endif

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_BATTLE_CRY:
		{
			TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_battleCrySound);

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_AVI:
		{
			if (TheDisplay)
				TheDisplay->toggleMovieCapture();
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_VTUNE_ON:
		{
			TheScriptEngine->setEnableVTune(true);
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugVTuneOff", L"VTune Gathering is ON") );
			disp = DESTROY_MESSAGE;
			break;
		}


		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_VTUNE_OFF:
		{
			TheScriptEngine->setEnableVTune(false);
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugVTuneOff", L"VTune Gathering is OFF") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_TOGGLE_FEATHER_WATER:
		{
			--TheWritableGlobalData->m_featherWater;
			if (TheGlobalData->m_featherWater < 0)
				TheWritableGlobalData->m_featherWater = 5;

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_WIN:
		{
			TheScriptEngine->debugVictory();

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE("GUI:DebugWin", L"Instant Win") );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_DEBUG_STATS:
		{
			if (TheDisplay->getDebugDisplayCallback() == StatDebugDisplay)
				TheDisplay->setDebugDisplayCallback(nullptr);
			else
				TheDisplay->setDebugDisplayCallback(StatDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SLEEPY_UPDATE_PERFORMANCE:
		{
			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugIncreaseAnimSkateSpeed",
				L"Number of Sleepy Modules: %d", TheGameLogic->getNumberSleepyUpdates() ) );
			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_OBJECT_ID_PERFORMANCE:
		{
			static __int64 startTime64;
			static __int64 endTime64,freq64;
			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			Int numberLookups = 10000;
			Int testindex = 1;
			for( ; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			double timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugObjectIdPerformance",
				L"Time to run %d ObjectID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() ) );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 100000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugObjectIdPerformance",
				L"Time to run %d ObjectID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() ) );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 1000000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugObjectIdPerformance",
				L"Time to run %d ObjectID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() ) );

			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DRAWABLE_ID_PERFORMANCE:
		{
			static __int64 startTime64;
			static __int64 endTime64,freq64;
			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			Int numberLookups = 10000;
			Int testindex = 1;
			for( ; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			double timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugDrawableIdPerformance",
				L"Time to run %d DrawableID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() ) );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 100000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugDrawableIdPerformance",
				L"Time to run %d DrawableID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() ) );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 1000000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugDrawableIdPerformance",
				L"Time to run %d DrawableID lookups is %f. Next index is %d", numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() ) );

			break;
		}

		//--------------------------------------------------------------------------- END DEMO MESSAGES
		//--------------------------------------------------------------------------- END DEMO MESSAGES
		//--------------------------------------------------------------------------- END DEMO MESSAGES
#endif // #if defined(RTS_DEBUG)

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_TOGGLE_AUDIODEBUG:
		{
			if (TheDisplay->getDebugDisplayCallback() == AudioDebugDisplay)
				TheDisplay->setDebugDisplayCallback(nullptr);
			else
				TheDisplay->setDebugDisplayCallback(AudioDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}

#endif//defined(RTS_DEBUG)

#ifdef DUMP_PERF_STATS
		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PERFORM_STATISTICAL_DUMP:
			//Dump performance statistics for this frame.
			TheWritableGlobalData->m_dumpPerformanceStatistics = TRUE;

			TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:DebugPerformStatisticalDump",
				L"Statistics dump made on frame: %d", TheGameLogic->getFrame() ) );
			break;
#endif // DUMP_PERF_STATS


	}


	return disp;

}

static Bool isSystemMessage( const GameMessage *msg )
{
	if (!msg) {
		return false;
	}
	GameMessage::Type msgType = msg->getType();

	switch (msgType)
	{
		case GameMessage::MSG_DESTROY_SELECTED_GROUP:
		case GameMessage::MSG_LOGIC_CRC:
		case GameMessage::MSG_SET_REPLAY_CAMERA:
		case GameMessage::MSG_FRAME_TICK:
		case GameMessage::MSG_META_DEMO_INSTANT_QUIT:
			return TRUE;
	}
	return FALSE;
}
