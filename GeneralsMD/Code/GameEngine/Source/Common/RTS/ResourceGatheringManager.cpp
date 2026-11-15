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

// FILE: ResourceGatheringManager.cpp ///////////////////////////////////////////////////////////
// The part of a Player's brain that keeps track of all Resource type Objects and makes
// gathering type decisions based on them.
// Author: Graham Smallwood, January, 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ResourceGatheringManager.h"

#include "Common/ActionManager.h"
#include "Common/BuildAssistant.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyCenterDockUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/UpdateModule.h"

static const Real SUPPLY_SOURCE_CLOSE_DIST = 20 * PATHFIND_CELL_SIZE_F;
static const UnsignedInt SUPPLY_SOURCE_STALL_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt SUPPLY_SOURCE_RETRY_FRAMES = 60 * LOGICFRAMES_PER_SECOND;

static Real supplySourceDistanceSquared(const Coord3D *a, const Coord3D *b)
{
	Real dx = a->x - b->x;
	Real dy = a->y - b->y;
	return dx * dx + dy * dy;
}

static Int supplySourceCash(Object *source)
{
	if (!source || source->isEffectivelyDead() || !source->isKindOf(KINDOF_SUPPLY_SOURCE))
	{
		return 0;
	}
	static const NameKeyType key = NAMEKEY("SupplyWarehouseDockUpdate");
	SupplyWarehouseDockUpdate *warehouse = (SupplyWarehouseDockUpdate *)source->findUpdateModule(key);
	return warehouse ? warehouse->getBoxesStored() * TheGlobalData->m_baseValuePerSupplyBox : 0;
}

class PartitionFilterSupplySourceOccupant : public PartitionFilter
{
public:
	PartitionFilterSupplySourceOccupant(const Object *mover, const Object *source) : m_mover(mover), m_source(source) { }
	virtual Bool allow(Object *other) override
	{
		if (other == m_mover || !other->isKindOf(KINDOF_CASH_GENERATOR))
		{
			return false;
		}
		if (m_source && !other->isKindOf(KINDOF_STRUCTURE))
		{
			Player *owner = other->getControllingPlayer();
			ResourceGatheringManager *manager = owner ? owner->getResourceGatheringManager() : nullptr;
			Object *assigned = manager ? manager->getAssignedSupplySource(other) : nullptr;
			if (assigned && assigned != m_source)
			{
				return false;
			}
		}
		// A travelling stash claims its destination, not the sources it passes.
		return other->isKindOf(KINDOF_STRUCTURE) || !other->getAI() || !other->getAI()->isMoving();
	}
#if defined(RTS_DEBUG)
	virtual const char *debugGetName() override { return "PartitionFilterSupplySourceOccupant"; }
#endif
private:
	const Object *m_mover;
	const Object *m_source;
};

Bool ResourceGatheringManager::isAtSupplySource(const Object *mover, const Object *source)
{
	Real radius = source->getGeometryInfo().getBoundingCircleRadius() +
		2 * TheGlobalData->m_SupplyBuildBorder + mover->getGeometryInfo().getBoundingCircleRadius() +
		2 * PATHFIND_CELL_SIZE_F;
	return supplySourceDistanceSquared(mover->getPosition(), source->getPosition()) <= radius * radius;
}

void ResourceGatheringManager::updateSupplySourceReservations()
{
	UnsignedInt frame = TheGameLogic->getFrame();
	for (SupplySourceReservations::iterator it = m_supplySourceReservations.begin(); it != m_supplySourceReservations.end(); )
	{
		Object *mover = TheGameLogic->findObjectByID(it->first);
		if (!mover || mover->isEffectivelyDead() || !mover->getControllingPlayer() ||
			mover->getControllingPlayer()->getResourceGatheringManager() != this)
		{
			m_supplySourceReservations.erase(it++);
			continue;
		}
		SupplySourceReservation &reservation = it->second;
		Object *source = TheGameLogic->findObjectByID(reservation.sourceID);
		if (supplySourceCash(source) <= 0 || mover->getControllingPlayer()->getRelationship(source->getTeam()) == ENEMIES)
		{
			reservation.sourceID = INVALID_ID;
		}
		else if (isAtSupplySource(mover, source) ||
			supplySourceDistanceSquared(mover->getPosition(), &reservation.lastPosition) >= PATHFIND_CELL_SIZE_F * PATHFIND_CELL_SIZE_F)
		{
			reservation.lastPosition = *mover->getPosition();
			reservation.lastProgressFrame = frame;
		}
		else if (frame - reservation.lastProgressFrame >= SUPPLY_SOURCE_STALL_FRAMES)
		{
			reservation.blockedSourceID = reservation.sourceID;
			reservation.lastFailureFrame = frame;
			reservation.sourceID = INVALID_ID;
		}
		if (!reservation.managesWorkers && reservation.sourceID == INVALID_ID &&
			(reservation.blockedSourceID == INVALID_ID || frame - reservation.lastFailureFrame >= SUPPLY_SOURCE_RETRY_FRAMES))
		{
			m_supplySourceReservations.erase(it++);
			continue;
		}
		++it;
	}
}

Bool ResourceGatheringManager::isSupplySourceReserved(Object *source, const Object *mover, Bool separateSources)
{
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *player = ThePlayerList->getNthPlayer(i);
		ResourceGatheringManager *manager = player ? player->getResourceGatheringManager() : nullptr;
		if (!manager)
		{
			continue;
		}
		manager->updateSupplySourceReservations();
		for (SupplySourceReservations::const_iterator it = manager->m_supplySourceReservations.begin(); it != manager->m_supplySourceReservations.end(); ++it)
		{
			if (mover && it->first == mover->getID())
			{
				continue;
			}
			Object *reservedSource = TheGameLogic->findObjectByID(it->second.sourceID);
			if (reservedSource && separateSources)
			{
				if (reservedSource == source)
				{
					return true;
				}
				continue;
			}
			if (reservedSource)
			{
				Real radius = SUPPLY_SOURCE_CLOSE_DIST + source->getGeometryInfo().getBoundingCircleRadius() +
					reservedSource->getGeometryInfo().getBoundingCircleRadius();
				if (supplySourceDistanceSquared(source->getPosition(), reservedSource->getPosition()) <= radius * radius)
				{
					return true;
				}
			}
		}
	}
	return false;
}

Bool ResourceGatheringManager::isSupplySourceAvailable(Object *source, const Player *player, const Object *mover, Bool separateSources)
{
	if (supplySourceCash(source) <= 0 || player->getRelationship(source->getTeam()) == ENEMIES)
	{
		return false;
	}
	PartitionFilterSupplySourceOccupant filterOccupant(mover, separateSources ? source : nullptr);
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *occupants[] = { &filterOccupant, &filterAlive, &filterOnMap, nullptr };
	Real radius = SUPPLY_SOURCE_CLOSE_DIST + source->getGeometryInfo().getBoundingCircleRadius();
	if (ThePartitionManager->getClosestObject(source->getPosition(), radius, FROM_BOUNDINGSPHERE_2D, occupants))
	{
		return false;
	}

	PartitionFilterAcceptByKindOf filterStructure(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterPlayerAffiliation filterAllies(player, ALLOW_ALLIES | ALLOW_ENEMIES, true);
	PartitionFilterPlayer filterOtherPlayer(player, false);
	PartitionFilter *alliedBase[] = { &filterStructure, &filterAllies, &filterOtherPlayer, &filterAlive, &filterOnMap, nullptr };
	if (ThePartitionManager->getClosestObject(source->getPosition(), TheAI->getAiData()->m_supplyCenterSafeRadius,
		FROM_BOUNDINGSPHERE_2D, alliedBase))
	{
		return false;
	}
	return !isSupplySourceReserved(source, mover, separateSources);
}

Object *ResourceGatheringManager::findFreeSupplySource(Object *mover, const Coord3D *position, Int minimumCash)
{
	updateSupplySourceReservations();
	Player *player = mover->getControllingPlayer();
	SupplySourceReservations::iterator existing = m_supplySourceReservations.find(mover->getID());
	if (existing != m_supplySourceReservations.end())
	{
		Object *source = TheGameLogic->findObjectByID(existing->second.sourceID);
		if (source)
		{
			return source;
		}
	}

	Object *bestSource = nullptr;
	Real bestDistanceSquared = 0;
	do
	{
		for (Object *source = TheGameLogic->getFirstObject(); source; source = source->getNextObject())
		{
			Int cash = supplySourceCash(source);
			if (!source->isKindOf(KINDOF_STRUCTURE) || cash <= 0 || cash < minimumCash)
			{
				continue;
			}
			SupplySourceReservations::const_iterator previous = m_supplySourceReservations.find(mover->getID());
			if (previous != m_supplySourceReservations.end() && previous->second.blockedSourceID == source->getID() &&
				TheGameLogic->getFrame() - previous->second.lastFailureFrame < SUPPLY_SOURCE_RETRY_FRAMES)
			{
				continue;
			}
			Real distanceSquared = supplySourceDistanceSquared(source->getPosition(), position);
			if ((bestSource && distanceSquared >= bestDistanceSquared) || !isSupplySourceAvailable(source, player, mover, mover->isKindOf(KINDOF_CASH_GENERATOR) && !mover->isKindOf(KINDOF_STRUCTURE)))
			{
				continue;
			}
			bestSource = source;
			bestDistanceSquared = distanceSquared;
		}
		if (bestSource)
		{
			SupplySourceReservation reservation = m_supplySourceReservations[mover->getID()];
			reservation.sourceID = bestSource->getID();
			reservation.blockedSourceID = INVALID_ID;
			reservation.lastProgressFrame = TheGameLogic->getFrame();
			reservation.lastFailureFrame = 0;
			reservation.lastPosition = *mover->getPosition();
			m_supplySourceReservations[mover->getID()] = reservation;
			break;
		}
		minimumCash /= 2;
	} while (minimumCash > 100);
	return bestSource;
}

void ResourceGatheringManager::manageSupplyTruck(Object *truck)
{
	m_supplySourceReservations[truck->getID()].managesWorkers = true;
}

Object *ResourceGatheringManager::getAssignedSupplySource(Object *mover)
{
	updateSupplySourceReservations();
	SupplySourceReservations::const_iterator it = m_supplySourceReservations.find(mover->getID());
	return it == m_supplySourceReservations.end() ? nullptr : TheGameLogic->findObjectByID(it->second.sourceID);
}

Bool ResourceGatheringManager::hasFreeSupplySource(const Player *player, Int minimumCash)
{
	updateSupplySourceReservations();
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == player && !obj->isEffectivelyDead() &&
			obj->isKindOf(KINDOF_CASH_GENERATOR) && !obj->isKindOf(KINDOF_STRUCTURE) && obj->getAI() &&
			!getAssignedSupplySource(obj))
		{
			return false;
		}
	}
	for (Object *source = TheGameLogic->getFirstObject(); source; source = source->getNextObject())
	{
		if (source->isKindOf(KINDOF_STRUCTURE) && supplySourceCash(source) >= max(1, minimumCash) &&
			isSupplySourceAvailable(source, player, nullptr, true))
		{
			return true;
		}
	}
	return false;
}

Object *ResourceGatheringManager::getWorkerSupplyTruck(Object *worker)
{
	Object *truck = TheGameLogic->findObjectByID(worker->getProducerID());
	if (!truck || truck->isEffectivelyDead() || truck->getControllingPlayer() != worker->getControllingPlayer() ||
		truck->isKindOf(KINDOF_STRUCTURE) || !truck->isKindOf(KINDOF_CASH_GENERATOR))
	{
		return nullptr;
	}
	updateSupplySourceReservations();
	SupplySourceReservations::const_iterator it = m_supplySourceReservations.find(truck->getID());
	return it != m_supplySourceReservations.end() && it->second.managesWorkers ? truck : nullptr;
}

void ResourceGatheringManager::maintainSupplyWorkers(Object *truck, const ThingTemplate *workerType, Int workerCount)
{
	Object *source = getAssignedSupplySource(truck);
	ProductionUpdateInterface *production = truck->getProductionUpdateInterface();
	if (!source || !isAtSupplySource(truck, source) || !production || !workerType || workerCount <= 0)
	{
		return;
	}
	Int count = 0;
	for (const ProductionEntry *entry = production->firstProduction(); entry; entry = production->nextProduction(entry))
	{
		if (entry->getProductionType() == PRODUCTION_UNIT && entry->getProductionObject()->isEquivalentTo(workerType))
		{
			count += entry->getProductionQuantityRemaining();
		}
	}
	for (Object *worker = TheGameLogic->getFirstObject(); worker; worker = worker->getNextObject())
	{
		if (worker->isEffectivelyDead() || worker->getControllingPlayer() != truck->getControllingPlayer() ||
			!worker->getTemplate()->isEquivalentTo(workerType))
		{
			continue;
		}
		if (worker->getProducerID() == truck->getID())
		{
			++count;
		}
	}
	for (Object *worker = TheGameLogic->getFirstObject(); worker && count < workerCount; worker = worker->getNextObject())
	{
		if (!worker->isEffectivelyDead() && worker->getControllingPlayer() == truck->getControllingPlayer() &&
			worker->getTemplate()->isEquivalentTo(workerType) && worker->getProducerID() != truck->getID() && isAtSupplySource(worker, source))
		{
			Object *producer = TheGameLogic->findObjectByID(worker->getProducerID());
			if (!producer || producer->isEffectivelyDead() || producer->getControllingPlayer() != worker->getControllingPlayer())
			{
				worker->setProducer(truck);
				++count;
			}
		}
	}
	if (count < workerCount && production->canQueueCreateUnit(workerType) == CANMAKE_OK)
	{
		production->queueCreateUnit(workerType, production->requestUniqueUnitID());
	}
}

ResourceGatheringManager::ResourceGatheringManager()
{
}

ResourceGatheringManager::~ResourceGatheringManager()
{
	m_supplyWarehouses.erase( m_supplyWarehouses.begin(), m_supplyWarehouses.end() );
	m_supplyCenters.erase( m_supplyCenters.begin(), m_supplyCenters.end() );
}

void ResourceGatheringManager::addSupplyCenter( Object *newCenter )
{
	if( newCenter == nullptr )
		return;

	m_supplyCenters.push_back( newCenter->getID() );
}

void ResourceGatheringManager::removeSupplyCenter( Object *oldCenter )
{
	if( oldCenter == nullptr )
		return;

	ObjectID targetID = oldCenter->getID();

	objectIDListIterator iterator = m_supplyCenters.begin();
	while( iterator != m_supplyCenters.end() )
	{
		if( targetID == *iterator )
		{
			iterator = m_supplyCenters.erase( iterator );
		}
		else
			iterator++;
	}
}

void ResourceGatheringManager::addSupplyWarehouse( Object *newWarehouse )
{
	if( newWarehouse == nullptr )
		return;

	m_supplyWarehouses.push_back( newWarehouse->getID() );
}

void ResourceGatheringManager::removeSupplyWarehouse( Object *oldWarehouse )
{
	if( oldWarehouse == nullptr )
		return;

	ObjectID targetID = oldWarehouse->getID();

	objectIDListIterator iterator = m_supplyWarehouses.begin();
	while( iterator != m_supplyWarehouses.end() )
	{
		if( targetID == *iterator )
		{
			iterator = m_supplyWarehouses.erase( iterator );
		}
		else
			iterator++;
	}
}

static Real computeRelativeCost( Object *queryObject, Object *destObject, Real *pureDistanceSquared )
{
	/** @todo This gets filled with Pathfinding computations, analysis of Boxes remaining,
			Threat calculations, paths of other trucks, and other fancy stuff.
	*/

	//A good score is a very small number.

	if( queryObject == nullptr  ||  destObject == nullptr )
		return FLT_MAX;

	if( !TheActionManager->canTransferSuppliesAt(queryObject, destObject) )
		return FLT_MAX;// Handles emptiness and alliances

	DockUpdateInterface *dockInterface = destObject->getDockUpdateInterface();
	if( !dockInterface->isClearToApproach( queryObject ) )
		return FLT_MAX;

	// since we don't care about the distance as a distance per se, but rather as
	// a goodness-factor, save some time by getting the dist-sqr (srj)
	Real distSquared = ThePartitionManager->getDistanceSquared(queryObject, destObject, FROM_CENTER_3D);

	// I need the distance, but I don't want to count on the coincidence that
	// the abstract 'cost' this function returns happens to be just the distance, since it could
	// become more complicated
	if( pureDistanceSquared )
		*pureDistanceSquared = distSquared;

	return distSquared;
}

Object *ResourceGatheringManager::findBestSupplyWarehouse( Object *queryObject )
{
	Object *truck = queryObject ? getWorkerSupplyTruck(queryObject) : nullptr;
	if (truck)
	{
		Object *source = getAssignedSupplySource(truck);
		return source && TheActionManager->canTransferSuppliesAt(queryObject, source) ? source : nullptr;
	}

	Object *bestWarehouse = nullptr;
	Real maxDistanceSquared = 100000;

	if( ( queryObject == nullptr ) || ( queryObject->getAI() == nullptr ) )
		return nullptr;

	SupplyTruckAIInterface *supplyTruckAI = queryObject->getAI()->getSupplyTruckAIInterface();
	if( supplyTruckAI )
	{
		// Check for a dock override being set.
		ObjectID dockID = supplyTruckAI->getPreferredDockID();
		Object *dock = TheGameLogic->findObjectByID(dockID);
		if( dock )
		{
			static const NameKeyType key_warehouseUpdate = NAMEKEY("SupplyWarehouseDockUpdate");
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate*)dock->findUpdateModule( key_warehouseUpdate );
			//If remotely okay, let User win.
			if( warehouseModule && computeRelativeCost( queryObject, dock, nullptr ) != FLT_MAX )
				return dock;
		}
		// Please note, there is not a separate Warehouse and Center memory by Design.  Because
		// we lack a UI way to click Warehouse and drag to center to set up a specific path, the
		// practical realization has been made that you do not want separate memory.

		// Design wants a harvester to give up and return to base if it is "too far" to the warehouse.
		// Note, the "PreferedDock" will override this, and there is no distance max on Centers.
		maxDistanceSquared = supplyTruckAI->getWarehouseScanDistance() * supplyTruckAI->getWarehouseScanDistance();
	}

	//Otherwise, search for a good one.
	Real bestCost = FLT_MAX;

	objectIDListIterator iterator = m_supplyWarehouses.begin();
	while( iterator != m_supplyWarehouses.end() )
	{
		ObjectID currentID = *iterator;
		Object *currentWarehouse =TheGameLogic->findObjectByID(currentID);

		if( currentWarehouse == nullptr )
		{
			iterator = m_supplyWarehouses.erase( iterator );
		}
		else
		{
			Real distanceSquared;
			Real currentCost = computeRelativeCost( queryObject, currentWarehouse, &distanceSquared );
			if( (currentCost < bestCost) && (distanceSquared < maxDistanceSquared) )
			{
				bestWarehouse = currentWarehouse;
				bestCost = currentCost;
			}

			iterator++;
		}
	}

	return bestWarehouse;
}

Object *ResourceGatheringManager::findBestSupplyCenter( Object *queryObject )
{
	Object *truck = queryObject ? getWorkerSupplyTruck(queryObject) : nullptr;
	if (truck)
	{
		return TheActionManager->canTransferSuppliesAt(queryObject, truck) ? truck : nullptr;
	}

	Object *bestCenter = nullptr;

	if( ( queryObject == nullptr ) || ( queryObject->getAI() == nullptr ) )
		return nullptr;

	SupplyTruckAIInterface *supplyTruckAI = queryObject->getAI()->getSupplyTruckAIInterface();
	if( supplyTruckAI )
	{
		// Check for a dock override being set.
		ObjectID dockID = supplyTruckAI->getPreferredDockID();
		Object *dock = TheGameLogic->findObjectByID(dockID);
		if( dock )
		{
			static const NameKeyType key_centerUpdate = NAMEKEY("SupplyCenterDockUpdate");
			SupplyCenterDockUpdate *centerModule = (SupplyCenterDockUpdate*)dock->findUpdateModule( key_centerUpdate );
			//If remotely okay, let User win.
			if( centerModule && computeRelativeCost( queryObject, dock, nullptr ) != FLT_MAX )
				return dock;
		}
		// Please note, there is not a separate Warehouse and Center memory by Design.  Because
		// we lack a UI way to click Warehouse and drag to center to set up a specific path, the
		// practical realization has been made that you do not want separate memory.
	}

	//Otherwise, search for a good one.
	Real bestCost = FLT_MAX;

	objectIDListIterator iterator = m_supplyCenters.begin();
	while( iterator != m_supplyCenters.end() )
	{
		ObjectID currentID = *iterator;
		Object *currentCenter =TheGameLogic->findObjectByID(currentID);

		if( currentCenter == nullptr )
		{
			iterator = m_supplyCenters.erase( iterator );
		}
		else
		{
			Real currentCost = computeRelativeCost( queryObject, currentCenter, nullptr );
			if( currentCost < bestCost )
			{
				bestCenter = currentCenter;
				bestCost = currentCost;
			}

			iterator++;
		}
	}

	return bestCenter;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ResourceGatheringManager::crc( Xfer *xfer )
{
	xferSupplySourceReservations(xfer);
}

void ResourceGatheringManager::xferSupplySourceReservations(Xfer *xfer, XferVersion version)
{
	UnsignedInt count = (UnsignedInt)m_supplySourceReservations.size();
	xfer->xferUnsignedInt(&count);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_supplySourceReservations.clear();
		for (UnsignedInt i = 0; i < count; ++i)
		{
			ObjectID moverID;
			SupplySourceReservation reservation;
			xfer->xferObjectID(&moverID);
			xfer->xferObjectID(&reservation.sourceID);
			xfer->xferObjectID(&reservation.blockedSourceID);
			xfer->xferUnsignedInt(&reservation.lastProgressFrame);
			xfer->xferUnsignedInt(&reservation.lastFailureFrame);
			xfer->xferCoord3D(&reservation.lastPosition);
			if (version >= 3)
			{
				xfer->xferBool(&reservation.managesWorkers);
			}
			m_supplySourceReservations[moverID] = reservation;
		}
	}
	else
	{
		for (SupplySourceReservations::iterator it = m_supplySourceReservations.begin(); it != m_supplySourceReservations.end(); ++it)
		{
			ObjectID moverID = it->first;
			xfer->xferObjectID(&moverID);
			xfer->xferObjectID(&it->second.sourceID);
			xfer->xferObjectID(&it->second.blockedSourceID);
			xfer->xferUnsignedInt(&it->second.lastProgressFrame);
			xfer->xferUnsignedInt(&it->second.lastFailureFrame);
			xfer->xferCoord3D(&it->second.lastPosition);
			xfer->xferBool(&it->second.managesWorkers);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ResourceGatheringManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// supply warehouses
	xfer->xferSTLObjectIDList( &m_supplyWarehouses );

	// supply centers
	xfer->xferSTLObjectIDList( &m_supplyCenters );
	if (version >= 2)
	{
		xferSupplySourceReservations(xfer, version);
	}
	else
	{
		m_supplySourceReservations.clear();
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ResourceGatheringManager::loadPostProcess()
{

}

