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

// FILE: ScriptActionsCLPAI.cpp ///////////////////////////////////////////////////////////////////
// CLP_AI script actions that change how the skirmish AI builds.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/ScriptCLP.h"
#include "GameLogic/ScriptCLPAI.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"

#include <algorithm>
#include <vector>

ScriptCLPAI *TheScriptCLPAI = nullptr;

static const Real CLP_SEARCH_STEP = 2 * PATHFIND_CELL_SIZE_F;
static const Real CLP_NEAR_RADIUS = 600 * PATHFIND_CELL_SIZE_F;
static const Real CLP_SUPPLY_RADIUS = 300 * PATHFIND_CELL_SIZE_F;
static const Int CLP_SEARCH_BUDGET = 2048;					///< legality tests per placement, keeps one script action from stalling a frame
static const Int CLP_ANGLE_SEARCH_BUDGET = 256;
static const Int CLP_MAX_DEFENSE_ATTEMPTS = 100;
static const Real CLP_SUPPLY_CENTER_CLOSE_DIST = 20 * PATHFIND_CELL_SIZE_F;	///< SUPPLY_CENTER_CLOSE_DIST in AIPlayer.cpp

//-------------------------------------------------------------------------------------------------
/** Angle 361 picks a random diagonal and 362 a random whole degree. Degrees in and out. */
//-------------------------------------------------------------------------------------------------
static Real clpResolveAngle(Real angle)
{
	if( angle == 361.0f )
	{
		switch( GameLogicRandomValue( 0, 3 ) )
		{
			case 0:
				angle = 45.0f;
				break;
			case 1:
				angle = -45.0f;
				break;
			case 2:
				angle = 135.0f;
				break;
			default:
				angle = -135.0f;
				break;
		}
	}
	else if( angle == 362.0f )
	{
		angle = (Real)GameLogicRandomValue( 0, 359 );
	}
	return angle;
}

//-------------------------------------------------------------------------------------------------
/** A template by name, or a random structure from an ObjectTypes list. */
//-------------------------------------------------------------------------------------------------
static const ThingTemplate *clpPickStructureTemplate(const AsciiString &name)
{
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( name, FALSE );
	if( tmpl )
	{
		return tmpl;
	}

	ObjectTypes *types = TheScriptEngine->getObjectTypes( name );
	if( !types )
	{
		return nullptr;
	}

	std::vector<const ThingTemplate *> templates;
	for( size_t i = 0; i < types->getListSize(); ++i )
	{
		const ThingTemplate *t = TheThingFactory->findTemplate( types->getNthInList( i ), FALSE );
		if( t && t->isKindOf( KINDOF_STRUCTURE ) )
		{
			templates.push_back( t );
		}
	}
	if( templates.empty() )
	{
		return nullptr;
	}
	return templates[ GameLogicRandomValue( 0, (Int)templates.size() - 1 ) ];
}

//-------------------------------------------------------------------------------------------------
/** Every template a name stands for, without duplicates. */
//-------------------------------------------------------------------------------------------------
static void clpCollectTemplates(const AsciiString &name, std::vector<const ThingTemplate *> *out)
{
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( name, FALSE );
	if( tmpl )
	{
		out->push_back( tmpl );
		return;
	}

	ObjectTypes *types = TheScriptEngine->getObjectTypes( name );
	if( !types )
	{
		return;
	}
	for( size_t i = 0; i < types->getListSize(); ++i )
	{
		const ThingTemplate *t = TheThingFactory->findTemplate( types->getNthInList( i ), FALSE );
		if( t && std::find( out->begin(), out->end(), t ) == out->end() )
		{
			out->push_back( t );
		}
	}
}

//-------------------------------------------------------------------------------------------------
static void clpRemoveAllBibs()
{
	// isLocationLegalToBuild adds bib feedback; the stock AI clears it the same way.
	if( TheTerrainVisual )
	{
		TheTerrainVisual->removeAllBibs();
	}
}

//-------------------------------------------------------------------------------------------------
/** Cell i of the square ring at Chebyshev distance ring, walked clockwise from the top left corner. */
//-------------------------------------------------------------------------------------------------
static void clpRingCell(Int ring, Int i, Int *dx, Int *dy)
{
	Int side = 2 * ring;
	if( i < side )
	{
		*dx = -ring + i;
		*dy = -ring;
	}
	else if( i < 2 * side )
	{
		*dx = ring;
		*dy = -ring + (i - side);
	}
	else if( i < 3 * side )
	{
		*dx = ring - (i - 2 * side);
		*dy = ring;
	}
	else
	{
		*dx = -ring;
		*dy = ring - (i - 3 * side);
	}
}

//-------------------------------------------------------------------------------------------------
/** Searches square rings outward from center for a spot where tmpl is fully legal to build.
	budget is the number of legality tests left and may be shared by several searches. */
//-------------------------------------------------------------------------------------------------
static Bool clpFindBuildPosition(const Coord3D *center, const ThingTemplate *tmpl, Real angle, Player *player, Real maxRadius, Int *budget, Coord3D *outPos)
{
	const UnsignedInt flags = BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP;
	Int maxRing = REAL_TO_INT_FLOOR( maxRadius / CLP_SEARCH_STEP );
	for( Int ring = 0; ring <= maxRing; ++ring )
	{
		Int cells = ring == 0 ? 1 : 8 * ring;
		for( Int i = 0; i < cells; ++i )
		{
			if( *budget <= 0 )
			{
				return FALSE;
			}
			--(*budget);

			Int dx = 0;
			Int dy = 0;
			clpRingCell( ring, i, &dx, &dy );
			Coord3D pos = *center;
			pos.x += dx * CLP_SEARCH_STEP;
			pos.y += dy * CLP_SEARCH_STEP;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			if( TheBuildAssistant->isLocationLegalToBuild( &pos, tmpl, angle, flags, nullptr, player ) == LBC_OK )
			{
				*outPos = pos;
				return TRUE;
			}
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** The location itself when nothing overlaps it, like the stock AI, else the nearest fully legal spot. */
//-------------------------------------------------------------------------------------------------
static Bool clpPlaceNear(const Coord3D *location, const ThingTemplate *tmpl, Real angle, Player *player, Real maxRadius, Int *budget, Coord3D *outPos)
{
	Coord3D center = *location;
	center.z = TheTerrainLogic->getGroundHeight( center.x, center.y );
	--(*budget);
	if( TheBuildAssistant->isLocationLegalToBuild( &center, tmpl, angle, BuildAssistant::NO_OBJECT_OVERLAP, nullptr, player ) == LBC_OK )
	{
		*outPos = center;
		return TRUE;
	}
	return clpFindBuildPosition( &center, tmpl, angle, player, maxRadius, budget, outPos );
}

//-------------------------------------------------------------------------------------------------
/** True if the player's build list still has an unbuilt supply center near pos. */
//-------------------------------------------------------------------------------------------------
static Bool clpHasPendingSupplyCenter(Player *player, const Coord3D *pos, Real radius)
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->getObjectID() != INVALID_ID || !info->isBuildable() )
		{
			continue;
		}
		if( !info->isPriorityBuild() && !info->isAutomaticBuild() )
		{
			continue;
		}
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->getTemplateName(), FALSE );
		if( !tmpl || !tmpl->isKindOf( KINDOF_CASH_GENERATOR ) )
		{
			continue;
		}
		Real dx = info->getLocation()->x - pos->x;
		Real dy = info->getLocation()->y - pos->y;
		if( dx * dx + dy * dy <= radius * radius )
		{
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Stock getPlayerSuperweaponValue, with objects of objectType worth five times as much. */
//-------------------------------------------------------------------------------------------------
static Int clpSuperweaponValueType(const Coord3D *center, Int playerNdx, Real radius, const AsciiString &objectType, Bool includeMilitaryUnits, const Player *viewer)
{
	if( radius < 4 * PATHFIND_CELL_SIZE_F )
	{
		radius = 4 * PATHFIND_CELL_SIZE_F;
	}
	Real cash = 0;
	Real radSqr = sqr( radius );

	Player *pPlayer = ThePlayerList->getNthPlayer( playerNdx );
	if( pPlayer == nullptr )
	{
		return 0;
	}
	for( Player::PlayerTeamList::const_iterator it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it )
	{
		for( DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team )
			{
				continue;
			}
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *pObj = objIter.cur();
				if( !pObj )
				{
					continue;
				}
				if( !clpIsFairAITarget( pObj, viewer ) )
				{
					continue;
				}

				Bool applyNegValue = FALSE;
				if( !includeMilitaryUnits )
				{
					if( pObj->isKindOf( KINDOF_FS_BASE_DEFENSE ) || pObj->isKindOf( KINDOF_TECH_BASE_DEFENSE ) )
					{
						applyNegValue = TRUE;
					}
					else if( pObj->isKindOf( KINDOF_VEHICLE ) || pObj->isKindOf( KINDOF_INFANTRY ) )
					{
						if( !pObj->isKindOf( KINDOF_DOZER ) && !pObj->isKindOf( KINDOF_HARVESTER ) )
						{
							applyNegValue = TRUE;
						}
					}
				}
				else if( pObj->isKindOf( KINDOF_AIRCRAFT ) )
				{
					if( pObj->isSignificantlyAboveTerrain() )
					{
						continue;
					}
				}

				const Coord3D *pos = pObj->getPosition();
				Real dx = center->x - pos->x;
				Real dy = center->y - pos->y;
				if( dx * dx + dy * dy < radSqr )
				{
					Real dist = sqrt( dx * dx + dy * dy );
					Real factor = 1.0f - (dist / (2 * radius));
					Real value = pObj->getTemplate()->calcCostToBuild( pPlayer );
					if( pObj->isKindOf( KINDOF_COMMANDCENTER ) )
					{
						if( !includeMilitaryUnits )
						{
							value = value * 5.0f;
						}
						else
						{
							value = value / 10;
						}
					}
					if( pObj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
					{
						if( !includeMilitaryUnits )
						{
							value = value * 5.0f;
						}
						else
						{
							value = value / 10;
						}
					}
					if( clpObjectMatchesType( pObj, objectType ) )
					{
						value = value * 5.0f;
					}
					if( applyNegValue )
					{
						cash -= factor * value * 5.0f;
					}
					else
					{
						cash += factor * value;
					}
				}
			}
		}
	}
	return (Int)cash;
}

//-------------------------------------------------------------------------------------------------
static Bool clpGetStartPosition(Player *player, Coord3D *pos)
{
	AsciiString name;
	name.format( "Player_%d_Start", player->getMpStartIndex() + 1 );
	Waypoint *way = TheTerrainLogic->getWaypointByName( name );
	if( !way )
	{
		return FALSE;
	}
	*pos = *way->getLocation();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Queues buildingType near obj for the player's AI. angle is in degrees and may be 361 or 362. */
//-------------------------------------------------------------------------------------------------
static void clpBuildNearObject(Player *player, const AsciiString &buildingType, const Object *obj, Real angle)
{
	AIPlayer *ai = player->getAIPlayer();
	if( !ai || !obj )
	{
		return;
	}
	angle = clpResolveAngle( angle );
	const ThingTemplate *tmpl = clpPickStructureTemplate( buildingType );
	if( !tmpl )
	{
		return;
	}
	ai->clpBuildNearPosition( tmpl, obj->getPosition(), angle * (PI / 180.0f) );
}

//-------------------------------------------------------------------------------------------------
/** A random entry of the player's base defense list, or empty if it is empty or not buildable. */
//-------------------------------------------------------------------------------------------------
static AsciiString clpPickBaseDefense(Player *player)
{
	const std::vector<AsciiString> &defenses = TheScriptCLPAI->getBaseDefenses( player->getPlayerIndex() );
	if( defenses.empty() )
	{
		return AsciiString::TheEmptyString;
	}
	const AsciiString &name = defenses[ GameLogicRandomValue( 0, (Int)defenses.size() - 1 ) ];
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( name, FALSE );
	if( !tmpl || !player->canBuild( tmpl ) )
	{
		return AsciiString::TheEmptyString;
	}
	return name;
}

//-------------------------------------------------------------------------------------------------
static AISkirmishPlayer *clpGetSkirmishAI(Player *player)
{
	AIPlayer *ai = player ? player->getAIPlayer() : nullptr;
	if( !ai || !ai->isSkirmishAI() )
	{
		return nullptr;
	}
	return static_cast<AISkirmishPlayer *>( ai );
}

//-------------------------------------------------------------------------------------------------
// Player
//-------------------------------------------------------------------------------------------------
void Player::clpPrependToBuildList(BuildListInfo *info)
{
	info->setNextBuildList( m_pBuildList );
	m_pBuildList = info;
}

//-------------------------------------------------------------------------------------------------
// AIPlayer
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::clpTrainUnit(const ThingTemplate *thing)
{
	if( !thing )
	{
		return FALSE;
	}

	// The factory queue keeps only the template, so the order is done once training starts.
	WorkOrder *order = newInstance( WorkOrder );
	order->m_thing = thing;
	order->m_required = true;
	Bool success = startTraining( order, TRUE, AsciiString( "ScriptBuildUnit" ) );
	deleteInstance( order );
	return success;
}

//-------------------------------------------------------------------------------------------------
void AIPlayer::clpBuildNearPosition(const ThingTemplate *tmpl, const Coord3D *location, Real angle)
{
	if( !tmpl || !location )
	{
		return;
	}

	Int budget = CLP_SEARCH_BUDGET;
	Coord3D pos;
	Bool found = clpPlaceNear( location, tmpl, angle, m_player, CLP_NEAR_RADIUS, &budget, &pos );
	clpRemoveAllBibs();
	if( found )
	{
		pos.z = 0;
		m_player->addToPriorityBuildList( tmpl->getName(), &pos, angle );
	}
}

//-------------------------------------------------------------------------------------------------
/** Tries the team first, then every other instance of its prototype. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::clpBuildNearestTeam(const ThingTemplate *tmpl, Team *team, Real angle)
{
	if( !tmpl || !team )
	{
		return;
	}

	Int budget = CLP_SEARCH_BUDGET;
	Coord3D pos;
	Bool found = FALSE;
	const Coord3D *location = team->getEstimateTeamPosition();
	if( location )
	{
		found = clpPlaceNear( location, tmpl, angle, m_player, CLP_NEAR_RADIUS, &budget, &pos );
	}

	const TeamPrototype *proto = team->getPrototype();
	if( proto )
	{
		for( DLINK_ITERATOR<Team> iter = proto->iterate_TeamInstanceList(); !iter.done() && !found && budget > 0; iter.advance() )
		{
			Team *instance = iter.cur();
			if( !instance || instance == team )
			{
				continue;
			}
			location = instance->getEstimateTeamPosition();
			if( location )
			{
				found = clpPlaceNear( location, tmpl, angle, m_player, CLP_NEAR_RADIUS, &budget, &pos );
			}
		}
	}

	clpRemoveAllBibs();
	if( found )
	{
		pos.z = 0;
		m_player->addToPriorityBuildList( tmpl->getName(), &pos, angle );
	}
}

//-------------------------------------------------------------------------------------------------
/** Stock buildBySupplies with a script angle. Supply centers face their dock and skip docks that
	already have one queued. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::clpBuildBySupplies(Int minimumCash, const ThingTemplate *tTemplate, Real angle)
{
	if( !tTemplate )
	{
		return;
	}

	Object *bestSupplyWarehouse = clpFindSupplyCenter( minimumCash );
	Bool isCashGenerator = tTemplate->isKindOf( KINDOF_CASH_GENERATOR );
	if( !isCashGenerator )
	{
		Object *curWarehouse = TheGameLogic->findObjectByID( m_curWarehouseID );
		if( curWarehouse )
		{
			bestSupplyWarehouse = curWarehouse;
		}
	}
	if( !bestSupplyWarehouse )
	{
		return;
	}

	const Coord3D *warehousePos = bestSupplyWarehouse->getPosition();
	Coord3D location = *warehousePos;
	Coord2D offset;
	offset.x = location.x - m_baseCenter.x;
	offset.y = location.y - m_baseCenter.y;
	offset.normalize();
	Real radius = 3 * PATHFIND_CELL_SIZE_F;
	if( !isCashGenerator )
	{
		// Probably a defense, so build towards the enemy.
		Player *enemy = TheScriptEngine->getSkirmishEnemyPlayer();
		if( enemy )
		{
			Region2D bounds;
			getPlayerStructureBounds( &bounds, enemy->getPlayerIndex() );
			offset.x = location.x - (bounds.lo.x + bounds.hi.x) * 0.5f;
			offset.y = location.y - (bounds.lo.y + bounds.hi.y) * 0.5f;
			offset.normalize();
		}
		radius = bestSupplyWarehouse->getGeometryInfo().getBoundingCircleRadius();
	}
	location.x -= offset.x * radius;
	location.y -= offset.y * radius;
	location.z = TheTerrainLogic->getGroundHeight( location.x, location.y );

	Bool valid = FALSE;
	Coord3D newPos = location;
	if( isCashGenerator )
	{
		static const Real angleOffsetsDeg[] = { 0.0f, 15.0f, -15.0f, 30.0f, -30.0f, 45.0f, -45.0f, 90.0f, -90.0f };
		Real desiredAngle = normalizeAngle( (Real)WWMath::Atan2f( warehousePos->y - location.y, warehousePos->x - location.x ) );
		for( Int i = 0; i < ARRAY_SIZE( angleOffsetsDeg ) && !valid; ++i )
		{
			Real testAngle = normalizeAngle( desiredAngle + angleOffsetsDeg[ i ] * (PI / 180.0f) );
			Int budget = CLP_ANGLE_SEARCH_BUDGET;
			if( clpFindBuildPosition( &location, tTemplate, testAngle, m_player, CLP_SUPPLY_RADIUS, &budget, &newPos ) )
			{
				angle = testAngle;
				valid = TRUE;
			}
		}
	}
	else
	{
		Int budget = CLP_SEARCH_BUDGET;
		valid = clpPlaceNear( &location, tTemplate, angle, m_player, CLP_SUPPLY_RADIUS, &budget, &newPos );
	}
	clpRemoveAllBibs();

	if( valid )
	{
		newPos.z = 0;
		m_player->addToPriorityBuildList( tTemplate->getName(), &newPos, angle );
	}
	else
	{
		AsciiString debugMessage;
		debugMessage.format( " %s - buildBySupplies unable to place near dock at (%.2f,%.2f).", tTemplate->getName().str(), warehousePos->x, warehousePos->y );
		TheScriptEngine->AppendDebugMessage( debugMessage, false );
	}
	m_curWarehouseID = bestSupplyWarehouse->getID();
}

//-------------------------------------------------------------------------------------------------
/** Stock findSupplyCenter that also skips docks with a supply center still waiting in the build list. */
//-------------------------------------------------------------------------------------------------
Object *AIPlayer::clpFindSupplyCenter(Int minimumCash)
{
	Object *bestSupplyWarehouse = nullptr;
	Real bestDistSqr = 0;
	Coord3D enemyCenter;
	enemyCenter.zero();
	Region2D bounds;
	Player *enemy = getAiEnemy();
	if( enemy )
	{
		getPlayerStructureBounds( &bounds, enemy->getPlayerIndex() );
		enemyCenter.set( (bounds.lo.x + bounds.hi.x) * 0.5f, (bounds.lo.y + bounds.hi.y) * 0.5f, 0 );
	}

	static const NameKeyType key_warehouseUpdate = NAMEKEY( "SupplyWarehouseDockUpdate" );
	do
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( !obj->isKindOf( KINDOF_STRUCTURE ) || !obj->isKindOf( KINDOF_SUPPLY_SOURCE ) )
			{
				continue;
			}
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate *)obj->findUpdateModule( key_warehouseUpdate );
			if( !warehouseModule )
			{
				continue;
			}
			Int availableCash = warehouseModule->getBoxesStored() * TheGlobalData->m_baseValuePerSupplyBox;
			if( availableCash < minimumCash )
			{
				continue;
			}
			if( m_player->getRelationship( obj->getTeam() ) == ENEMIES )
			{
				continue;
			}

			Coord3D center = *obj->getPosition();
			Real radius = CLP_SUPPLY_CENTER_CLOSE_DIST + obj->getGeometryInfo().getBoundingCircleRadius();

			PartitionFilterAcceptByKindOf f1( MAKE_KINDOF_MASK( KINDOF_CASH_GENERATOR ), KINDOFMASK_NONE );
			PartitionFilterPlayer f2( m_player, true );
			PartitionFilterOnMap filterMapStatus;
			PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, nullptr };
			if( ThePartitionManager->getClosestObject( &center, radius, FROM_BOUNDINGSPHERE_2D, filters ) )
			{
				continue;
			}
			if( clpHasPendingSupplyCenter( m_player, &center, radius ) )
			{
				continue;
			}

			Real dx = obj->getPosition()->x - m_baseCenter.x;
			Real dy = obj->getPosition()->y - m_baseCenter.y;
			Real distSqr = dx * dx + dy * dy;
			if( enemy )
			{
				dx = obj->getPosition()->x - enemyCenter.x;
				dy = obj->getPosition()->y - enemyCenter.y;
				if( distSqr * 0.4 > (dx * dx + dy * dy) * 0.6f )
				{
					continue;
				}
			}

			if( bestSupplyWarehouse == nullptr || bestDistSqr > distSqr )
			{
				bestSupplyWarehouse = obj;
				bestDistSqr = distSqr;
			}
		}
		if( bestSupplyWarehouse )
		{
			break;
		}
		minimumCash /= 2;
	} while( minimumCash > 100 );

	return bestSupplyWarehouse;
}

//-------------------------------------------------------------------------------------------------
/** Stock computeSuperweaponTarget scored by clpSuperweaponValueType. */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::clpComputeSuperweaponTargetType(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius, const AsciiString &objectType)
{
	Region2D bounds;
	getPlayerStructureBounds( &bounds, playerNdx );
	if( bounds.hi.x == 0 && bounds.lo.x == 0 && bounds.hi.y == 0 && bounds.lo.y == 0 )
	{
		// No buildings, so scan the whole map for the leftovers.
		Region3D bounds3D;
		TheTerrainLogic->getExtent( &bounds3D );
		bounds.hi.x = bounds3D.hi.x;
		bounds.lo.x = bounds3D.lo.x;
		bounds.hi.y = bounds3D.hi.y;
		bounds.lo.y = bounds3D.lo.y;
	}

	if( weaponRadius < 1.0f )
	{
		weaponRadius = 1.0f;
	}

	bounds.lo.x += weaponRadius;
	bounds.hi.x -= weaponRadius;
	if( bounds.hi.x < bounds.lo.x )
	{
		bounds.hi.x = bounds.lo.x = (bounds.hi.x + bounds.lo.x) / 2.0f;
	}
	if( bounds.hi.y < bounds.lo.y )
	{
		bounds.hi.y = bounds.lo.y = (bounds.hi.y + bounds.lo.y) / 2.0f;
	}

	Int xCount = REAL_TO_INT_CEIL( bounds.width() / weaponRadius ) + 1;
	Int yCount = REAL_TO_INT_CEIL( bounds.height() / weaponRadius ) + 1;
	if( xCount > 10 )
	{
		xCount = 10;
	}
	if( yCount > 10 )
	{
		yCount = 10;
	}

	// Sneak attacks want rich, poorly defended spots.
	Bool targetMilitaryUnits = power->getSpecialPowerType() != SPECIAL_SNEAK_ATTACK;

	Int xDelta, yDelta, xStart, yStart;
	switch( GameLogicRandomValue( 1, 4 ) )
	{
		case 1:
			xDelta = 1;
			yDelta = 1;
			xStart = 0;
			yStart = 0;
			break;
		case 2:
			xDelta = -1;
			yDelta = 1;
			xStart = xCount;
			yStart = 0;
			break;
		case 3:
			xDelta = 1;
			yDelta = -1;
			xStart = 0;
			yStart = yCount;
			break;
		default:
			xDelta = -1;
			yDelta = -1;
			xStart = xCount;
			yStart = yCount;
			break;
	}

	Bool success = FALSE;
	Int cash = -1;
	Coord3D pos;
	Coord3D bestPos;
	bestPos.zero();
	Int xIndex = xStart;
	for( Int x = 0; x < xCount; x++, xIndex += xDelta )
	{
		Int yIndex = yStart;
		for( Int y = 0; y < yCount; y++, yIndex += yDelta )
		{
			pos.x = bounds.lo.x + (bounds.width() * xIndex) / xCount;
			pos.y = bounds.lo.y + (bounds.height() * yIndex) / yCount;
			pos.z = 0;
			Int curCash = clpSuperweaponValueType( &pos, playerNdx, 2 * weaponRadius, objectType, targetMilitaryUnits, m_player );
			if( curCash > cash )
			{
				cash = curCash;
				bestPos = pos;
				success = TRUE;
			}
		}
	}
	if( !success )
	{
		return FALSE;
	}

	if( cash <= 0 && clpAIUsesTargetingRestrictions( m_player ) )
	{
		return FALSE;
	}

	// Fine tune with a smaller radius on an 11x11 grid around the best spot.
	Coord3D veryBestPos = bestPos;
	cash = -1;
	Int count = 0;
	for( Int x = 0; x < 11; x++ )
	{
		for( Int y = 0; y < 11; y++ )
		{
			pos.x = bestPos.x + (x - 5) * (weaponRadius / 10);
			pos.y = bestPos.y + (y - 5) * (weaponRadius / 10);
			pos.z = 0;
			Int curCash = clpSuperweaponValueType( &pos, playerNdx, weaponRadius, objectType, targetMilitaryUnits, m_player );
			if( curCash > cash )
			{
				cash = curCash;
				veryBestPos = pos;
				count = 1;
			}
			else if( curCash == cash )
			{
				veryBestPos.x += pos.x;
				veryBestPos.y += pos.y;
				count++;
			}
		}
	}
	if( count > 1 )
	{
		veryBestPos.x /= count;
		veryBestPos.y /= count;
	}
	veryBestPos.z = TheTerrainLogic->getGroundHeight( veryBestPos.x, veryBestPos.y );
	*retPos = veryBestPos;
	return cash > -1;
}

//-------------------------------------------------------------------------------------------------
// AISkirmishPlayer
//-------------------------------------------------------------------------------------------------
/** Stock buildAIBaseDefenseStructure on baseOwner's defense circle, scaled by percent and turned by
	rotation degrees. The structure goes into this player's build list and baseOwner's slots advance. */
//-------------------------------------------------------------------------------------------------
void AISkirmishPlayer::clpBuildBaseDefense(const AsciiString &thingName, Bool flank, Real rotation, Real percent, AISkirmishPlayer *baseOwner)
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate( thingName, FALSE );
	if( !tTemplate || !baseOwner )
	{
		return;
	}

	Real placeAngle = clpResolveAngle( rotation ) * (PI / 180.0f);
	Int startIndex = baseOwner->m_player->getMpStartIndex() + 1;
	for( Int attempt = 0; attempt < CLP_MAX_DEFENSE_ATTEMPTS; ++attempt )
	{
		AsciiString pathLabel;
		if( !flank )
		{
			pathLabel.format( "%s%d", SKIRMISH_CENTER, startIndex );
		}
		else if( baseOwner->m_curFlankBaseDefense & 1 )
		{
			pathLabel.format( "%s%d", SKIRMISH_FLANK, startIndex );
		}
		else
		{
			pathLabel.format( "%s%d", SKIRMISH_BACKDOOR, startIndex );
		}

		Coord3D goalPos = baseOwner->m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if( way )
		{
			goalPos = *way->getLocation();
		}
		else
		{
			if( flank )
			{
				return;
			}
			Region2D bounds;
			getPlayerStructureBounds( &bounds, baseOwner->getMyEnemyPlayerIndex() );
			goalPos.x = bounds.lo.x + bounds.width() / 2;
			goalPos.y = bounds.lo.y + bounds.height() / 2;
		}

		Real defenseDistance = baseOwner->m_baseRadius + TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;
		Real baseCircumference = 2 * PI * defenseDistance;
		if( baseCircumference < 1.0f )
		{
			return;
		}

		Coord2D offset;
		offset.x = goalPos.x - baseOwner->m_baseCenter.x;
		offset.y = goalPos.y - baseOwner->m_baseCenter.y;
		offset.normalize();
		offset.x *= defenseDistance;
		offset.y *= defenseDistance;

		Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		Real angleOffset = 2 * PI * (structureRadius * 4 / baseCircumference);

		Real angle;
		if( flank )
		{
			Int selector = baseOwner->m_curFlankBaseDefense >> 1;
			if( baseOwner->m_curFlankBaseDefense & 1 )
			{
				if( selector & 1 )
				{
					baseOwner->m_curLeftFlankRightDefenseAngle -= angleOffset;
					angle = baseOwner->m_curLeftFlankRightDefenseAngle;
				}
				else
				{
					angle = baseOwner->m_curLeftFlankLeftDefenseAngle;
					baseOwner->m_curLeftFlankLeftDefenseAngle += angleOffset;
				}
			}
			else
			{
				if( selector & 1 )
				{
					baseOwner->m_curRightFlankRightDefenseAngle -= angleOffset;
					angle = baseOwner->m_curRightFlankRightDefenseAngle;
				}
				else
				{
					angle = baseOwner->m_curRightFlankLeftDefenseAngle;
					baseOwner->m_curRightFlankLeftDefenseAngle += angleOffset;
				}
			}
		}
		else
		{
			if( baseOwner->m_curFrontBaseDefense & 1 )
			{
				baseOwner->m_curFrontRightDefenseAngle -= angleOffset;
				angle = baseOwner->m_curFrontRightDefenseAngle;
			}
			else
			{
				angle = baseOwner->m_curFrontLeftDefenseAngle;
				baseOwner->m_curFrontLeftDefenseAngle += angleOffset;
			}
		}

		if( angle > PI / 3 )
		{
			break;
		}
		Real s = WWMath::Sinf( angle );
		Real c = WWMath::Cosf( angle );

		Coord3D buildPos = baseOwner->m_baseCenter;
		buildPos.x += (offset.x * c - offset.y * s) * (percent / 100.0f);
		buildPos.y += (offset.y * c + offset.x * s) * (percent / 100.0f);

		Bool canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild( &buildPos, tTemplate, placeAngle,
			BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player );
		clpRemoveAllBibs();
		if( flank )
		{
			baseOwner->m_curFlankBaseDefense++;
		}
		else
		{
			baseOwner->m_curFrontBaseDefense++;
		}
		if( canBuild )
		{
			m_player->addToPriorityBuildList( thingName, &buildPos, placeAngle );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Stock buildSpecificAIBuilding, except the entry closest to an idle dozer wins instead of the first. */
//-------------------------------------------------------------------------------------------------
void AISkirmishPlayer::clpBuildSpecificBuildingNearDozer(const AsciiString &thingName)
{
	std::vector<const Object *> dozers;
	for( Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it )
	{
		for( DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team )
			{
				continue;
			}
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( !obj || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER ) )
				{
					continue;
				}
				const AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if( ai && ai->isIdle() )
				{
					dozers.push_back( obj );
				}
			}
		}
	}

	Bool found = FALSE;
	BuildListInfo *closestInfo = nullptr;
	Real closestDistSqr = 0;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if( info->getTemplateName() != thingName )
		{
			continue;
		}
		if( !TheThingFactory->findTemplate( thingName, FALSE ) )
		{
			DEBUG_LOG(( "*** ERROR - Build list building '%s' doesn't exist.", thingName.str() ));
			continue;
		}
		found = TRUE;
		if( TheGameLogic->findObjectByID( info->getObjectID() ) || info->isPriorityBuild() )
		{
			continue;
		}

		Real distSqr = FLT_MAX;
		for( size_t i = 0; i < dozers.size(); ++i )
		{
			Real dx = dozers[ i ]->getPosition()->x - info->getLocation()->x;
			Real dy = dozers[ i ]->getPosition()->y - info->getLocation()->y;
			distSqr = std::min( distSqr, dx * dx + dy * dy );
		}
		if( !closestInfo || distSqr < closestDistSqr )
		{
			closestInfo = info;
			closestDistSqr = distSqr;
		}
	}

	if( closestInfo )
	{
		closestInfo->markPriorityBuild();
		m_buildDelay = 0;
		AsciiString buildingStr = "Queueing building '";
		buildingStr.concat( thingName );
		buildingStr.concat( "' for construction." );
		TheScriptEngine->AppendDebugMessage( buildingStr, false );
	}
	else if( found )
	{
		AsciiString buildingStr = "Warning - all instances of building '";
		buildingStr.concat( thingName );
		buildingStr.concat( "' are already built or queued for build, not queueing." );
		TheScriptEngine->AppendDebugMessage( buildingStr, false );
	}
	else
	{
		AsciiString buildingStr = "Error - could not find building '";
		buildingStr.concat( thingName );
		buildingStr.concat( "' in the building template list." );
		TheScriptEngine->AppendDebugMessage( buildingStr, false );
	}
}

//-------------------------------------------------------------------------------------------------
// ScriptActions
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerBuildUnit(const AsciiString& unitName, const AsciiString& playerName)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	if( !player )
	{
		return;
	}
	const ThingTemplate *unitTemplate = TheThingFactory->findTemplate( unitName, FALSE );
	if( !unitTemplate )
	{
		return;
	}
	if( !unitTemplate->isKindOf( KINDOF_INFANTRY ) && !unitTemplate->isKindOf( KINDOF_VEHICLE ) && !unitTemplate->isKindOf( KINDOF_AIRCRAFT ) )
	{
		return;
	}
	AIPlayer *ai = player->getAIPlayer();
	if( !ai )
	{
		return;
	}
	ai->clpTrainUnit( unitTemplate );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestTeamAngle(const AsciiString& playerName, const AsciiString& buildingType, const AsciiString& teamName, Real angle)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !player || !team )
	{
		return;
	}
	AIPlayer *ai = player->getAIPlayer();
	if( !ai )
	{
		return;
	}
	angle = clpResolveAngle( angle );
	const ThingTemplate *tmpl = clpPickStructureTemplate( buildingType );
	if( !tmpl )
	{
		return;
	}
	ai->clpBuildNearestTeam( tmpl, team, angle * (PI / 180.0f) );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildSupplyCenterAngle(const AsciiString& player, const AsciiString& buildingType, Int cash, Real angle)
{
	Player *thePlayer = TheScriptEngine->getPlayerFromAsciiString( player );
	if( !thePlayer )
	{
		return;
	}
	AIPlayer *ai = thePlayer->getAIPlayer();
	if( !ai )
	{
		return;
	}
	angle = clpResolveAngle( angle );
	const ThingTemplate *tmpl = clpPickStructureTemplate( buildingType );
	if( !tmpl )
	{
		return;
	}
	ai->clpBuildBySupplies( cash, tmpl, angle * (PI / 180.0f) );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestTypeAngle(const AsciiString& playerName, const AsciiString& buildingType, const AsciiString& objectType, Real angle, Int selectionMode)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	Coord3D startPos;
	if( !player || !clpGetStartPosition( player, &startPos ) )
	{
		return;
	}
	Object *bestObj = clpSelectObject( &startPos, nullptr, objectType, selectionMode );
	clpBuildNearObject( player, buildingType, bestObj, angle );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestKindOfAngle(const AsciiString& playerName, const AsciiString& buildingType, Int kindOf, Real angle, Int selectionMode)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	Coord3D startPos;
	if( !player || kindOf < KINDOF_FIRST || kindOf >= KINDOF_COUNT || !clpGetStartPosition( player, &startPos ) )
	{
		return;
	}
	PartitionFilterAcceptByKindOf filterKindOf( MAKE_KINDOF_MASK( (KindOfType)kindOf ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filterKindOf, nullptr };
	Object *bestObj = clpSelectObject( &startPos, filters, AsciiString::TheEmptyString, selectionMode );
	clpBuildNearObject( player, buildingType, bestObj, angle );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestTypeAngleArea(const AsciiString& playerName, const AsciiString& buildingType, const AsciiString& objectType, const AsciiString& triggerArea, Real angle, Int selectionMode)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	PolygonTrigger *trigger = TheScriptEngine->getQualifiedTriggerAreaByName( triggerArea );
	Coord3D startPos;
	if( !player || !trigger || !clpGetStartPosition( player, &startPos ) )
	{
		return;
	}
	PartitionFilterPolygonTrigger filterArea( trigger );
	PartitionFilter *filters[] = { &filterArea, nullptr };
	Object *bestObj = clpSelectObject( &startPos, filters, objectType, selectionMode );
	clpBuildNearObject( player, buildingType, bestObj, angle );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestKindOfAngleArea(const AsciiString& playerName, const AsciiString& buildingType, Int kindOf, const AsciiString& triggerArea, Real angle, Int selectionMode)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	PolygonTrigger *trigger = TheScriptEngine->getQualifiedTriggerAreaByName( triggerArea );
	Coord3D startPos;
	if( !player || !trigger || kindOf < KINDOF_FIRST || kindOf >= KINDOF_COUNT || !clpGetStartPosition( player, &startPos ) )
	{
		return;
	}
	PartitionFilterAcceptByKindOf filterKindOf( MAKE_KINDOF_MASK( (KindOfType)kindOf ), KINDOFMASK_NONE );
	PartitionFilterPolygonTrigger filterArea( trigger );
	PartitionFilter *filters[] = { &filterKindOf, &filterArea, nullptr };
	Object *bestObj = clpSelectObject( &startPos, filters, AsciiString::TheEmptyString, selectionMode );
	clpBuildNearObject( player, buildingType, bestObj, angle );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishFireSpecialPowerAtMostCostType(const AsciiString& player, const AsciiString& specialPower, const AsciiString& objectType)
{
	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if( enemyPlayer == nullptr )
	{
		return;
	}
	Int enemyNdx = enemyPlayer->getPlayerIndex();

	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate( specialPower );
	if( power == nullptr )
	{
		return;
	}
	Real radius = 50.0f;
	if( power->getRadiusCursorRadius() > radius )
	{
		radius = power->getRadiusCursorRadius();
	}

	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString( player );
	if( pPlayer == nullptr )
	{
		return;
	}
	AIPlayer *ai = pPlayer->getAIPlayer();
	if( !ai )
	{
		return;
	}

	std::vector<const ThingTemplate *> targets;
	clpCollectTemplates( objectType, &targets );
	static const NameKeyType key_cannonUpdate = NAMEKEY( "ParticleUplinkCannonUpdate" );

	for( Player::PlayerTeamList::const_iterator it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it )
	{
		for( DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team )
			{
				continue;
			}

			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *pObj = objIter.cur();
				if( !pObj )
				{
					continue;
				}

				SpecialPowerModuleInterface *mod = pObj->getSpecialPowerModule( power );
				if( !mod )
				{
					continue;
				}
				if( !mod->isReady() )
				{
					continue;
				}

				Coord3D location;
				Bool locationFound = ai->clpComputeSuperweaponTargetType( power, &location, enemyNdx, radius, objectType );
				if( locationFound && power->getSpecialPowerType() == SPECIAL_SNEAK_ATTACK )
				{
					const ThingTemplate *sneakAttackTemplate = mod->getReferenceThingTemplate();
					if( sneakAttackTemplate )
					{
						locationFound = pPlayer->calcClosestConstructionZoneLocation( sneakAttackTemplate, &location );
					}
				}

				if( locationFound && location.lengthSqr() > 0.0f )
				{
					if( pObj->findUpdateModule( key_cannonUpdate ) )
					{
						if( targets.empty() )
						{
							TheScriptCLPAI->clearCannonTargets( pObj );
						}
						else
						{
							TheScriptCLPAI->setCannonTargets( pObj, &location, targets );
						}
					}
					mod->doSpecialPowerAtLocation( &location, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
				}
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIPlayerBuildDefenseStructureFromVector(Bool flank, Real rotation, Real percent)
{
	Player *player = TheScriptEngine->getCurrentPlayer();
	AISkirmishPlayer *skirmishAI = clpGetSkirmishAI( player );
	if( !skirmishAI )
	{
		TheScriptEngine->AppendDebugMessage( "Error : Solo ai doesn't support buildAIBaseDefense.", false );
		return;
	}
	AsciiString defense = clpPickBaseDefense( player );
	if( defense.isNotEmpty() )
	{
		skirmishAI->clpBuildBaseDefense( defense, flank, rotation, percent, skirmishAI );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIPlayerBuildDefenseStructureFromVectorAtPlayer(Bool flank, Real rotation, Real percent, const AsciiString& playerName)
{
	Player *player = TheScriptEngine->getCurrentPlayer();
	AISkirmishPlayer *skirmishAI = clpGetSkirmishAI( player );
	AISkirmishPlayer *targetAI = clpGetSkirmishAI( TheScriptEngine->getPlayerFromAsciiString( playerName ) );
	if( !skirmishAI || !targetAI )
	{
		TheScriptEngine->AppendDebugMessage( "Error : buildAIBaseDefense at a player needs two skirmish ai players.", false );
		return;
	}
	AsciiString defense = clpPickBaseDefense( player );
	if( defense.isNotEmpty() )
	{
		skirmishAI->clpBuildBaseDefense( defense, flank, rotation, percent, targetAI );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIPlayerAddBaseDefenseStructure(const AsciiString& objectType)
{
	Player *player = TheScriptEngine->getCurrentPlayer();
	if( !player )
	{
		return;
	}
	std::vector<const ThingTemplate *> templates;
	clpCollectTemplates( objectType, &templates );
	for( size_t i = 0; i < templates.size(); ++i )
	{
		TheScriptCLPAI->addBaseDefense( player->getPlayerIndex(), templates[ i ]->getName() );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIPlayerRemoveBaseDefenseStructure(const AsciiString& objectType)
{
	Player *player = TheScriptEngine->getCurrentPlayer();
	if( !player )
	{
		return;
	}
	std::vector<const ThingTemplate *> templates;
	clpCollectTemplates( objectType, &templates );
	for( size_t i = 0; i < templates.size(); ++i )
	{
		TheScriptCLPAI->removeBaseDefense( player->getPlayerIndex(), templates[ i ]->getName() );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishUseParticleCannonTeleportMode(const AsciiString& playerName, Bool teleportMode)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString( playerName );
	if( !player || !player->getAIPlayer() )
	{
		return;
	}
	TheScriptCLPAI->setCannonTeleportMode( player->getPlayerIndex(), teleportMode );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetAIDataClosestBuildingToDozerPriority(Bool isUsing)
{
	Player *player = TheScriptEngine->getCurrentPlayer();
	if( !player )
	{
		return;
	}
	TheScriptCLPAI->setClosestDozerPriority( player->getPlayerIndex(), isUsing );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIClearBuildList(const AsciiString& player)
{
	Player *thePlayer = TheScriptEngine->getPlayerFromAsciiString( player );
	if( thePlayer )
	{
		thePlayer->setBuildList( nullptr );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAIAddToBuildList(const AsciiString& player, const AsciiString& object, Coord3D* coords, Real angle, BOOLEAN initbuilt, INT rebuildTimes, BOOLEAN exactPositionOnly)
{
	Player *thePlayer = TheScriptEngine->getPlayerFromAsciiString( player );
	if( !thePlayer || !coords )
	{
		return;
	}
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( object, FALSE );
	if( !tmpl )
	{
		AsciiString msg = "AI_ADD_TO_BUILDLIST - template not found: ";
		msg.concat( object );
		TheScriptEngine->AppendDebugMessage( msg, false );
		return;
	}

	Real bAngle = angle * (PI / 180.0f);
	BuildListInfo *info = newInstance( BuildListInfo );
	info->setTemplateName( tmpl->getName() );
	info->setLocation( *coords );
	info->setAngle( bAngle );
	info->setExactPositionOnly( exactPositionOnly );
	// -1 wraps to UNLIMITED_REBUILDS.
	info->setNumRebuilds( (UnsignedInt)rebuildTimes );
	info->setObjectTimestamp( TheGameLogic->getFrame() );

	if( initbuilt )
	{
		Team *team = thePlayer->getDefaultTeam();
		Object *obj = team ? TheThingFactory->newObject( tmpl, team ) : nullptr;
		if( !obj )
		{
			deleteInstance( info );
			return;
		}
		Coord3D finalPos = *coords;
		finalPos.z = TheTerrainLogic->getGroundHeight( coords->x, coords->y );
		obj->setOrientation( bAngle );
		obj->setPosition( &finalPos );
		if( obj->isKindOf( KINDOF_BLAST_CRATER ) )
		{
			TheTerrainLogic->createCraterInTerrain( obj );
			TheAI->pathfinder()->addObjectToPathfindMap( obj );
		}
		info->setObjectID( obj->getID() );
		info->setLocation( *obj->getPosition() );
		info->setAngle( obj->getOrientation() );
	}

	thePlayer->clpPrependToBuildList( info );
}

//-------------------------------------------------------------------------------------------------
// ScriptCLPAI
//-------------------------------------------------------------------------------------------------
ScriptCLPAI::ScriptCLPAI()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
ScriptCLPAI::~ScriptCLPAI()
{
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::reset()
{
	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
	{
		m_baseDefenses[ i ].clear();
		m_closestDozerPriority[ i ] = FALSE;
		m_cannonTeleport[ i ] = FALSE;
	}
	m_cannonTracks.clear();
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::addBaseDefense(Int playerIndex, const AsciiString &templateName)
{
	if( playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT )
	{
		return;
	}
	std::vector<AsciiString> &defenses = m_baseDefenses[ playerIndex ];
	if( std::find( defenses.begin(), defenses.end(), templateName ) == defenses.end() )
	{
		defenses.push_back( templateName );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::removeBaseDefense(Int playerIndex, const AsciiString &templateName)
{
	if( playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT )
	{
		return;
	}
	std::vector<AsciiString> &defenses = m_baseDefenses[ playerIndex ];
	defenses.erase( std::remove( defenses.begin(), defenses.end(), templateName ), defenses.end() );
}

//-------------------------------------------------------------------------------------------------
const std::vector<AsciiString> &ScriptCLPAI::getBaseDefenses(Int playerIndex) const
{
	static const std::vector<AsciiString> s_none;
	if( playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT )
	{
		return s_none;
	}
	return m_baseDefenses[ playerIndex ];
}

//-------------------------------------------------------------------------------------------------
Bool ScriptCLPAI::getClosestDozerPriority(Int playerIndex) const
{
	if( playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT )
	{
		return FALSE;
	}
	return m_closestDozerPriority[ playerIndex ];
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::setClosestDozerPriority(Int playerIndex, Bool enable)
{
	if( playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT )
	{
		m_closestDozerPriority[ playerIndex ] = enable;
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::setCannonTeleportMode(Int playerIndex, Bool teleport)
{
	if( playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT )
	{
		m_cannonTeleport[ playerIndex ] = teleport;
	}
}

//-------------------------------------------------------------------------------------------------
/** Arms the cannon's next firing, which must start this frame, to chase targets from firePos. */
//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::setCannonTargets(const Object *cannon, const Coord3D *firePos, const std::vector<const ThingTemplate *> &targets)
{
	CannonTrackMap::iterator it = m_cannonTracks.begin();
	while( it != m_cannonTracks.end() )
	{
		if( TheGameLogic->findObjectByID( it->first ) == nullptr )
		{
			m_cannonTracks.erase( it++ );
		}
		else
		{
			++it;
		}
	}

	CannonTrack &track = m_cannonTracks[ cannon->getID() ];
	track.startFrame = std::max( TheGameLogic->getFrame(), (UnsignedInt)1 );
	track.started = FALSE;
	track.victimID = INVALID_ID;
	track.victimPos = *firePos;
	track.targets = targets;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::clearCannonTargets(const Object *cannon)
{
	m_cannonTracks.erase( cannon->getID() );
}

//-------------------------------------------------------------------------------------------------
Bool ScriptCLPAI::steerParticleCannon(const Object *cannon, UnsignedInt startAttackFrame, Real fastDrivingSpeed, Coord3D *beamPos)
{
	if( m_cannonTracks.empty() || !cannon )
	{
		return FALSE;
	}
	CannonTrackMap::iterator it = m_cannonTracks.find( cannon->getID() );
	if( it == m_cannonTracks.end() )
	{
		return FALSE;
	}
	if( it->second.startFrame != startAttackFrame )
	{
		// The cannon fired again without this script, so the targets are stale.
		m_cannonTracks.erase( it );
		return FALSE;
	}

	CannonTrack &track = it->second;
	if( !track.started )
	{
		*beamPos = track.victimPos;
		track.started = TRUE;
	}

	Object *target = nullptr;
	if( track.victimID != INVALID_ID )
	{
		target = TheGameLogic->findObjectByID( track.victimID );
		if( !target || target->isEffectivelyDead() )
		{
			target = nullptr;
			track.victimID = INVALID_ID;
		}
	}

	if( !target )
	{
		Real closestDistSqr = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || obj->getRelationship( cannon ) != ENEMIES )
			{
				continue;
			}
			Bool isTarget = FALSE;
			for( size_t i = 0; i < track.targets.size(); ++i )
			{
				if( obj->getTemplate()->isEquivalentTo( track.targets[ i ] ) )
				{
					isTarget = TRUE;
					break;
				}
			}
			if( !isTarget )
			{
				continue;
			}
			Real dx = obj->getPosition()->x - beamPos->x;
			Real dy = obj->getPosition()->y - beamPos->y;
			Real distSqr = dx * dx + dy * dy;
			if( !target || distSqr < closestDistSqr )
			{
				target = obj;
				closestDistSqr = distSqr;
			}
		}
		if( target )
		{
			track.victimID = target->getID();
		}
	}

	if( target )
	{
		track.victimPos = *target->getPosition();
	}

	const Player *owner = cannon->getControllingPlayer();
	Int ownerIndex = owner ? owner->getPlayerIndex() : -1;
	if( ownerIndex >= 0 && ownerIndex < MAX_PLAYER_COUNT && m_cannonTeleport[ ownerIndex ] )
	{
		*beamPos = track.victimPos;
		return TRUE;
	}

	Coord3D direction = track.victimPos;
	direction.sub( *beamPos );
	Real distance = direction.length();
	Real speed = fastDrivingSpeed / LOGICFRAMES_PER_SECOND;
	if( distance <= speed )
	{
		// Arrived, so pick the closest target again next frame.
		*beamPos = track.victimPos;
		track.victimID = INVALID_ID;
	}
	else
	{
		direction.scale( speed / distance );
		beamPos->add( direction );
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::crc( Xfer *xfer )
{
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// per player settings
	UnsignedInt playerCount = MAX_PLAYER_COUNT;
	xfer->xferUnsignedInt( &playerCount );
	for( UnsignedInt i = 0; i < playerCount; ++i )
	{
		std::vector<AsciiString> defenses;
		Bool dozerPriority = FALSE;
		Bool teleport = FALSE;
		if( i < MAX_PLAYER_COUNT )
		{
			defenses = m_baseDefenses[ i ];
			dozerPriority = m_closestDozerPriority[ i ];
			teleport = m_cannonTeleport[ i ];
		}

		UnsignedInt count = (UnsignedInt)defenses.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
		{
			defenses.resize( count );
		}
		for( UnsignedInt d = 0; d < count; ++d )
		{
			xfer->xferAsciiString( &defenses[ d ] );
		}
		xfer->xferBool( &dozerPriority );
		xfer->xferBool( &teleport );

		if( i < MAX_PLAYER_COUNT )
		{
			m_baseDefenses[ i ] = defenses;
			m_closestDozerPriority[ i ] = dozerPriority;
			m_cannonTeleport[ i ] = teleport;
		}
	}

	// particle cannon tracks
	UnsignedInt count = (UnsignedInt)m_cannonTracks.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_SAVE )
	{
		for( CannonTrackMap::iterator it = m_cannonTracks.begin(); it != m_cannonTracks.end(); ++it )
		{
			ObjectID id = it->first;
			CannonTrack &track = it->second;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedInt( &track.startFrame );
			xfer->xferBool( &track.started );
			xfer->xferObjectID( &track.victimID );
			xfer->xferCoord3D( &track.victimPos );
			UnsignedInt targetCount = (UnsignedInt)track.targets.size();
			xfer->xferUnsignedInt( &targetCount );
			for( UnsignedInt t = 0; t < targetCount; ++t )
			{
				AsciiString name = track.targets[ t ]->getName();
				xfer->xferAsciiString( &name );
			}
		}
	}
	else
	{
		m_cannonTracks.clear();
		for( UnsignedInt i = 0; i < count; ++i )
		{
			ObjectID id;
			CannonTrack track;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedInt( &track.startFrame );
			xfer->xferBool( &track.started );
			xfer->xferObjectID( &track.victimID );
			xfer->xferCoord3D( &track.victimPos );
			UnsignedInt targetCount = 0;
			xfer->xferUnsignedInt( &targetCount );
			for( UnsignedInt t = 0; t < targetCount; ++t )
			{
				AsciiString name;
				xfer->xferAsciiString( &name );
				const ThingTemplate *tmpl = TheThingFactory->findTemplate( name, FALSE );
				if( tmpl )
				{
					track.targets.push_back( tmpl );
				}
			}
			m_cannonTracks[ id ] = track;
		}
	}

	// Build list entries placed only at their exact position, by index. BuildListInfo keeps its
	// save format, and the Players block loads before this one.
	UnsignedInt listCount = (UnsignedInt)ThePlayerList->getPlayerCount();
	xfer->xferUnsignedInt( &listCount );
	for( UnsignedInt i = 0; i < listCount; ++i )
	{
		Player *player = (Int)i < ThePlayerList->getPlayerCount() ? ThePlayerList->getNthPlayer( (Int)i ) : nullptr;
		std::vector<UnsignedInt> exactIndices;
		if( xfer->getXferMode() == XFER_SAVE && player )
		{
			UnsignedInt index = 0;
			for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext(), ++index )
			{
				if( info->isExactPositionOnly() )
				{
					exactIndices.push_back( index );
				}
			}
		}

		UnsignedInt exactCount = (UnsignedInt)exactIndices.size();
		xfer->xferUnsignedInt( &exactCount );
		if( xfer->getXferMode() == XFER_LOAD )
		{
			exactIndices.resize( exactCount );
		}
		for( UnsignedInt e = 0; e < exactCount; ++e )
		{
			xfer->xferUnsignedInt( &exactIndices[ e ] );
		}

		if( xfer->getXferMode() == XFER_LOAD && player )
		{
			UnsignedInt index = 0;
			size_t next = 0;
			for( BuildListInfo *info = player->getBuildList(); info && next < exactIndices.size(); info = info->getNext(), ++index )
			{
				if( exactIndices[ next ] == index )
				{
					info->setExactPositionOnly( TRUE );
					++next;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptCLPAI::loadPostProcess()
{
}
