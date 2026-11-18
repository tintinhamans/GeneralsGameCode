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

// FILE: TornadoUpdate.cpp ///////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_DEATH_NAMES

#include "Common/Xfer.h"
#include "Lib/trig.h"
#include "Common/GlobalData.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/TornadoUpdate.h"

#include <algorithm>

// Where victims orbit when RingRadius is left unset, as a fraction of the grab radius.
static const Real TORNADO_DEFAULT_RING_FRACTION = 0.1f;

// Fraction of the ring inside which the radial direction is too noisy to steer by.
static const Real TORNADO_STEADY_FRACTION = 0.25f;

// How hard a victim is steered toward the speed the tornado wants for it.
static const Real TORNADO_HOVER_DAMPING = 0.35f;

// Frames spent easing into the ceiling, so victims settle instead of slamming into it.
static const Real TORNADO_APPROACH_FRAMES = 12.0f;

// How far a victim is lifted clear of the ground when it is first grabbed.
static const Real TORNADO_LIFTOFF_HEIGHT = 2.0f;

// How long a tornado with no timer, no lifetime and no controller runs before ending itself.
static const UnsignedInt TORNADO_ORPHAN_FRAMES = 30 * LOGICFRAMES_PER_SECOND;

//-------------------------------------------------------------------------------------------------
TornadoUpdateModuleData::TornadoUpdateModuleData()
{
	m_radius = 0.0f;
	m_ringRadius = 0.0f;
	m_pullForce = 0.0f;
	m_liftForce = 0.0f;
	m_spinForce = 0.0f;
	m_yawRate = 0.0f;
	m_maxLiftHeight = 0.0f;
	m_maxVictimSpeed = 0.0f;
	m_massReference = 0.0f;
	m_releaseSpeed = 0.0f;
	m_requiredKindOf.clear();
	m_forbiddenKindOf.clear();
	m_targetsMask = WEAPON_AFFECTS_ALLIES | WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_NEUTRALS;
	m_affectAirborne = FALSE;
	m_ignoreVictimGeometry = FALSE;
	m_rampUpFrames = 0;
	m_fullStrengthFrames = 0;
	m_rampDownFrames = 0;
	m_damagePerSecond = 0.0f;
	m_damageRadius = 0.0f;
	m_damagePulseFrames = 0;
	m_damageType = DAMAGE_EXPLOSION;
	m_deathType = DEATH_EXPLODED;
	m_killObjectWhenDone = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void TornadoUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",				INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_radius ) },
		{ "RingRadius",			INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_ringRadius ) },
		{ "PullForce",			INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_pullForce ) },
		{ "LiftForce",			INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_liftForce ) },
		{ "SpinForce",			INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_spinForce ) },
		{ "YawRate",			INI::parseAngularVelocityReal,			nullptr,					offsetof( TornadoUpdateModuleData, m_yawRate ) },
		{ "MaxLiftHeight",		INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_maxLiftHeight ) },
		{ "MaxVictimSpeed",		INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_maxVictimSpeed ) },
		{ "MassReference",		INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_massReference ) },
		{ "ReleaseSpeed",		INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_releaseSpeed ) },
		{ "RequiredKindOf",		KindOfMaskType::parseFromINI,			nullptr,					offsetof( TornadoUpdateModuleData, m_requiredKindOf ) },
		{ "ForbiddenKindOf",	KindOfMaskType::parseFromINI,			nullptr,					offsetof( TornadoUpdateModuleData, m_forbiddenKindOf ) },
		{ "AffectsTargets",		INI::parseBitString32,					TheWeaponAffectsMaskNames,	offsetof( TornadoUpdateModuleData, m_targetsMask ) },
		{ "AffectAirborne",		INI::parseBool,							nullptr,					offsetof( TornadoUpdateModuleData, m_affectAirborne ) },
		{ "IgnoreVictimGeometry",INI::parseBool,						nullptr,					offsetof( TornadoUpdateModuleData, m_ignoreVictimGeometry ) },
		{ "RampUpTime",			INI::parseDurationUnsignedInt,			nullptr,					offsetof( TornadoUpdateModuleData, m_rampUpFrames ) },
		{ "FullStrengthTime",	INI::parseDurationUnsignedInt,			nullptr,					offsetof( TornadoUpdateModuleData, m_fullStrengthFrames ) },
		{ "RampDownTime",		INI::parseDurationUnsignedInt,			nullptr,					offsetof( TornadoUpdateModuleData, m_rampDownFrames ) },
		{ "DamagePerSecond",	INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_damagePerSecond ) },
		{ "DamageRadius",		INI::parseReal,							nullptr,					offsetof( TornadoUpdateModuleData, m_damageRadius ) },
		{ "DamagePulseDelay",	INI::parseDurationUnsignedInt,			nullptr,					offsetof( TornadoUpdateModuleData, m_damagePulseFrames ) },
		{ "DamageType",			DamageTypeFlags::parseSingleBitFromINI,	nullptr,					offsetof( TornadoUpdateModuleData, m_damageType ) },
		{ "DeathType",			INI::parseIndexList,					TheDeathNames,				offsetof( TornadoUpdateModuleData, m_deathType ) },
		{ "KillObjectWhenDone",	INI::parseBool,							nullptr,					offsetof( TornadoUpdateModuleData, m_killObjectWhenDone ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
TornadoUpdate::TornadoUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_startFrame = TheGameLogic->getFrame();
	m_rampDownStartFrame = 0;
	m_rampDownStartStrength = 0.0f;
	m_nextDamagePulseFrame = m_startFrame;
	m_rampingDown = FALSE;
	m_externallyControlled = FALSE;
	m_rampUpFrames = getTornadoUpdateModuleData()->m_rampUpFrames;
	m_rampDownFrames = getTornadoUpdateModuleData()->m_rampDownFrames;
	m_previousCenter.zero();
	m_hasPreviousCenter = FALSE;

	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
TornadoUpdate::~TornadoUpdate( void )
{
	// The engine may already be tearing down, in which case there is nothing left to release.
	if( TheGameLogic != nullptr )
	{
		releaseAll();
	}
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::setExternallyControlled( UnsignedInt rampFrames )
{
	m_externallyControlled = TRUE;
	m_rampUpFrames = rampFrames;
	m_rampDownFrames = rampFrames;
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::beginRampDown( void )
{
	if( m_rampingDown )
	{
		return;
	}

	UnsignedInt now = TheGameLogic->getFrame();
	m_rampDownStartStrength = computeStrength( now );
	m_rampDownStartFrame = now;
	m_rampingDown = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The weak to strong to weak envelope, scaling forces, spin and damage. */
//-------------------------------------------------------------------------------------------------
Real TornadoUpdate::computeStrength( UnsignedInt now ) const
{
	if( m_rampingDown )
	{
		if( m_rampDownFrames == 0 )
		{
			return 0.0f;
		}
		Real fade = 1.0f - (Real)(now - m_rampDownStartFrame) / (Real)m_rampDownFrames;
		if( fade <= 0.0f )
		{
			return 0.0f;
		}
		return m_rampDownStartStrength * fade;
	}

	if( m_rampUpFrames == 0 )
	{
		return 1.0f;
	}

	UnsignedInt elapsed = now - m_startFrame;
	if( elapsed >= m_rampUpFrames )
	{
		return 1.0f;
	}
	return (Real)elapsed / (Real)m_rampUpFrames;
}

//-------------------------------------------------------------------------------------------------
Bool TornadoUpdate::hasLifetimeUpdate( void ) const
{
	static NameKeyType key_LifetimeUpdate = NAMEKEY( "LifetimeUpdate" );
	return getObject()->findUpdateModule( key_LifetimeUpdate ) != nullptr;
}

//-------------------------------------------------------------------------------------------------
/** The side and KindOf test. */
//-------------------------------------------------------------------------------------------------
Bool TornadoUpdate::canAffect( const Object *obj ) const
{
	const TornadoUpdateModuleData *data = getTornadoUpdateModuleData();

	if( obj == getObject() )
	{
		return FALSE;
	}
	if( data->m_requiredKindOf.any() && !obj->isAnyKindOf( data->m_requiredKindOf ) )
	{
		return FALSE;
	}
	if( obj->isAnyKindOf( data->m_forbiddenKindOf ) )
	{
		return FALSE;
	}
	if( !data->m_affectAirborne && obj->isAirborneTarget() )
	{
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** What the tornado can physically take hold of; damage is limited to the same set. */
//-------------------------------------------------------------------------------------------------
Bool TornadoUpdate::canGrab( const Object *obj ) const
{
	if( obj->getPhysics() == nullptr || obj->getPhysics()->getShockResistanceScale() <= 0.0f )
	{
		return FALSE;
	}
	ObjectID controller = obj->getPhysics()->getTornadoController();
	if( controller != INVALID_ID && controller != getObject()->getID() )
	{
		return FALSE;
	}
	if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_PROJECTILE ) )
	{
		return FALSE;
	}
	if( obj->getContainedBy() != nullptr )
	{
		return FALSE;
	}
	if( obj->isDisabledByType( DISABLED_HELD ) )
	{
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Collect the ids of everything in range we may act on, so acting cannot disturb the query. */
//-------------------------------------------------------------------------------------------------
void TornadoUpdate::gatherTargets( const Coord3D *center, Real radius, ObjectIDVector &out )
{
	const TornadoUpdateModuleData *data = getTornadoUpdateModuleData();
	Object *me = getObject();

	Int targetFlags = 0;
	if( data->m_targetsMask & WEAPON_AFFECTS_ALLIES )
	{
		targetFlags |= PartitionFilterRelationship::ALLOW_ALLIES;
	}
	if( data->m_targetsMask & WEAPON_AFFECTS_ENEMIES )
	{
		targetFlags |= PartitionFilterRelationship::ALLOW_ENEMIES;
	}
	if( data->m_targetsMask & WEAPON_AFFECTS_NEUTRALS )
	{
		targetFlags |= PartitionFilterRelationship::ALLOW_NEUTRAL;
	}

	// Flag tests first; the relationship filter looks up teams for every candidate.
	PartitionFilterAlive filterAlive;
	PartitionFilterSameMapStatus filterMapStatus( me );
	PartitionFilterRelationship relationship( me, targetFlags );
	PartitionFilter *filters[] = { &filterAlive, &filterMapStatus, &relationship, nullptr };

	out.clear();
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( center, radius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( canGrab( obj ) && canAffect( obj ) )
		{
			out.push_back( obj->getID() );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::lockEnemyVictim( Object *obj, UnsignedInt &flags )
{
	if( getObject()->getRelationship( obj ) == ENEMIES )
	{
		if( !obj->testStatus( OBJECT_STATUS_NO_ATTACK ) )
		{
			flags |= RESTORE_NO_ATTACK;
		}
		if( !obj->testStatus( OBJECT_STATUS_UNSELECTABLE ) )
		{
			flags |= RESTORE_UNSELECTABLE;
		}
		obj->setStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_NO_ATTACK, OBJECT_STATUS_UNSELECTABLE ) );
	}
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::captureVictim( Object *obj )
{
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if( ai )
	{
		ai->aiIdle( CMD_FROM_AI );
	}

	PhysicsBehavior *physics = obj->getPhysics();
	Victim victim;
	victim.id = obj->getID();
	victim.flags = 0;
	if( physics->getAllowCollideForce() )
	{
		victim.flags |= RESTORE_COLLIDE_FORCE;
	}
	lockEnemyVictim( obj, victim.flags );
	m_victims.push_back( victim );
	physics->setTornadoController( getObject()->getID() );
	physics->clearAcceleration();
	physics->setAllowBouncing( TRUE );

	// Else applyForce keeps only the sideways part of our pull for the next third of a second.
	physics->clearMotiveForce();

	// Victims crowd one ring, so the collision push-apart fights the orbit; optionally mute it.
	if( getTornadoUpdateModuleData()->m_ignoreVictimGeometry )
	{
		physics->setAllowCollideForce( FALSE );
	}

	// Physics zeroes upward velocity on the ground, so break contact by hand, within the victim's own height.
	physics->setStickToGround( FALSE );
	Real step = __min( TORNADO_LIFTOFF_HEIGHT, obj->getGeometryInfo().getMaxHeightAbovePosition() );
	if( step > 0.0f )
	{
		Coord3D liftOff = *obj->getPosition();
		liftOff.z += step;
		obj->setPosition( &liftOff );

		// Leave the ground cells behind, or others path around bare ground.
		TheAI->pathfinder()->updatePos( obj, &liftOff );
	}

	// A braking object does not integrate its horizontal velocity.
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );

	obj->setModelConditionState( MODELCONDITION_STUNNED_FLAILING );
}

//-------------------------------------------------------------------------------------------------
/** Drag one victim for a frame. Physics undoes most of this every frame, so it is re-applied. */
//-------------------------------------------------------------------------------------------------
void TornadoUpdate::holdVictim( Object *obj, const HoldParams &p )
{
	const TornadoUpdateModuleData *data = getTornadoUpdateModuleData();
	PhysicsBehavior *physics = obj->getPhysics();

	// The stun halts the locomotor, which otherwise re-arms stickToGround every tick.
	physics->setStunned( TRUE );
	physics->setAllowToFall( TRUE );
	physics->setStickToGround( FALSE );

	const Coord3D *pos = obj->getPosition();
	Coord3D radial;
	radial.x = p.center.x - pos->x;
	radial.y = p.center.y - pos->y;
	radial.z = 0.0f;

	Real distance = radial.length();
	if( distance > 0.01f )
	{
		radial.normalize();
	}
	else
	{
		// On the axis any heading will do, but not one our own yaw keeps changing, so use the id.
		Real bearing = deg2rad( (Real)( (UnsignedInt)obj->getID() % 360 ) );
		radial.x = Cos( bearing );
		radial.y = Sin( bearing );
	}

	// Inside the ring the pull reverses; near the axis it eases in, since the direction is noise there.
	Real pullScale = 1.0f;
	if( distance < p.ring )
	{
		pullScale = ( distance / p.ring ) * 2.0f - 1.0f;
		if( distance < p.steady )
		{
			pullScale *= distance / p.steady;
		}
	}

	// The ceiling stays fixed during decay until each victim is released.
	if( pos->z > p.ceiling )
	{
		Coord3D ceilingPos = *pos;
		ceilingPos.z = p.ceiling;
		obj->setPosition( &ceilingPos );
		physics->scrubVelocityZ( 0.0f );
	}

	// A climb rate rather than a push, else the victim keeps accelerating and shoots through the ceiling.
	Real wantedRise = 0.0f;
	if( pos->z < p.ceiling )
	{
		wantedRise = p.riseSpeed * physics->getShockResistanceScale();

		// Ease off over the last stretch so the victim arrives already slow.
		Real remaining = p.ceiling - pos->z;
		if( remaining < wantedRise * TORNADO_APPROACH_FRAMES )
		{
			wantedRise = remaining / TORNADO_APPROACH_FRAMES;
		}
	}

	// Mass is multiplied back in because applyForce divides it out; every victim then rides alike.
	Real mass = physics->getMass();
	const Coord3D *vel = physics->getVelocity();
	Real lift = -TheGlobalData->m_gravity * mass + ( wantedRise - vel->z ) * mass * TORNADO_HOVER_DAMPING;
	if( lift < 0.0f )
	{
		lift = 0.0f;
	}

	// Flown as a target speed, so that air drag, which grows with speed, cannot bleed the orbit away.
	Real scale = p.strength * physics->getShockResistanceScale();
	Coord3D wantedVel;
	wantedVel.x = ( data->m_pullForce * pullScale * radial.x - data->m_spinForce * radial.y ) * scale;
	wantedVel.y = ( data->m_pullForce * pullScale * radial.y + data->m_spinForce * radial.x ) * scale;
	wantedVel.z = 0.0f;
	if( data->m_maxVictimSpeed > 0.0f )
	{
		Real wantedSpeedSqr = sqr( wantedVel.x ) + sqr( wantedVel.y );
		if( wantedSpeedSqr > sqr( data->m_maxVictimSpeed ) )
		{
			wantedVel.scale( data->m_maxVictimSpeed / sqrtf( wantedSpeedSqr ) );
		}
	}

	Coord3D force;
	Real orbitSpeed = ( vel->y - p.velocity.y ) * radial.x - ( vel->x - p.velocity.x ) * radial.y;
	Real centripetal = sqr( orbitSpeed ) / __max( distance, p.ring );
	force.x = ( ( wantedVel.x + p.velocity.x - vel->x ) * TORNADO_HOVER_DAMPING + centripetal * radial.x ) * mass;
	force.y = ( ( wantedVel.y + p.velocity.y - vel->y ) * TORNADO_HOVER_DAMPING + centripetal * radial.y ) * mass;
	force.z = lift;

	// Not applyMotiveForce: an AI holding position scrubs the velocity of motive units every tick.
	physics->applyForce( &force );

	// MassReference only slows the spin-up of heavy victims while the tornado is still building.
	Real massScale = 1.0f;
	if( data->m_massReference > 0.0f && p.strength < 1.0f )
	{
		massScale = __min( 1.0f, data->m_massReference / mass );
		massScale += ( 1.0f - massScale ) * p.strength;
	}
	Real spin = data->m_yawRate * massScale * scale;
	Real phase = (Real)( TheGameLogic->getFrame() - m_startFrame ) * SECONDS_PER_LOGICFRAME_REAL;
	phase += deg2rad( (Real)( (UnsignedInt)obj->getID() % 360 ) );
	physics->setYawRate( spin );
	physics->setPitchRate( spin * 0.25f * Sin( phase * 1.7f ) );
	physics->setRollRate( spin * 0.25f * Cos( phase * 2.3f ) );
}

//-------------------------------------------------------------------------------------------------
/** Let go. Physics clears the stun and fall flags by itself on landing and awards the falling
	* damage, which only counts a steep enough descent - hence scrubbing the horizontal speed. */
//-------------------------------------------------------------------------------------------------
void TornadoUpdate::releaseVictim( Object *obj, UnsignedInt flags )
{
	PhysicsBehavior *physics = obj->getPhysics();
	if( physics == nullptr )
	{
		return;
	}

	if( physics->getTornadoController() != getObject()->getID() )
	{
		return;
	}
	physics->setTornadoController( INVALID_ID );
	if( flags & RESTORE_NO_ATTACK )
	{
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_ATTACK ) );
	}
	if( flags & RESTORE_UNSELECTABLE )
	{
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
	}
	physics->setPitchRate( 0.0f );
	physics->setRollRate( 0.0f );
	physics->setYawRate( 0.0f );
	physics->clearAcceleration();
	physics->scrubVelocity2D( 0.0f );
	Real releaseSpeed = getTornadoUpdateModuleData()->m_releaseSpeed;
	if( releaseSpeed > 0.0f )
	{
		UnsignedInt seed = (UnsignedInt)obj->getID() * 2654435761U ^ m_rampDownStartFrame ^ (UnsignedInt)getObject()->getID();
		Real angle = deg2rad( (Real)( seed % 360 ) );
		Coord3D velocity;
		velocity.x = Cos( angle ) * releaseSpeed;
		velocity.y = Sin( angle ) * releaseSpeed;
		velocity.z = 0.0f;
		physics->addVelocityTo( &velocity );
	}

	if( getTornadoUpdateModuleData()->m_ignoreVictimGeometry )
	{
		physics->setAllowCollideForce( ( flags & RESTORE_COLLIDE_FORCE ) != 0 );
	}

	// We cleared this to lift the victim; an idle unit never runs the locomotor that re-arms it.
	physics->setStickToGround( TRUE );
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::releaseAll( void )
{
	for( VictimVector::iterator it = m_victims.begin(); it != m_victims.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( it->id );
		if( obj )
		{
			releaseVictim( obj, it->flags );
		}
	}
	m_victims.clear();
}

//-------------------------------------------------------------------------------------------------
void TornadoUpdate::doDamagePulse( const Coord3D *center, Real strength )
{
	const TornadoUpdateModuleData *data = getTornadoUpdateModuleData();

	Real pulseSeconds = (Real)data->m_damagePulseFrames / LOGICFRAMES_PER_SECONDS_REAL;

	DamageInfo damageInfo;
	damageInfo.in.m_amount = data->m_damagePerSecond * strength * pulseSeconds;
	damageInfo.in.m_sourceID = getObject()->getID();
	damageInfo.in.m_damageType = data->m_damageType;
	damageInfo.in.m_deathType = data->m_deathType;

	// Without a damage radius of its own the targets are exactly this frame's victims.
	if( data->m_damageRadius > 0.0f )
	{
		gatherTargets( center, data->m_damageRadius, m_scratch );
	}
	else
	{
		m_scratch.clear();
		for( VictimVector::const_iterator it = m_victims.begin(); it != m_victims.end(); ++it )
		{
			m_scratch.push_back( it->id );
		}
	}

	for( ObjectIDVector::const_iterator it = m_scratch.begin(); it != m_scratch.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj == nullptr )
		{
			continue;
		}
		BodyModuleInterface *body = obj->getBodyModule();
		if( body )
		{
			body->attemptDamage( &damageInfo );
		}
	}
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime TornadoUpdate::update( void )
{
	const TornadoUpdateModuleData *data = getTornadoUpdateModuleData();
	Object *me = getObject();
	UnsignedInt now = TheGameLogic->getFrame();

	if( me->isEffectivelyDead() )
	{
		releaseAll();
		return UPDATE_SLEEP_FOREVER;
	}

	if( !m_rampingDown && !m_externallyControlled )
	{
		UnsignedInt fullFrame = m_startFrame + m_rampUpFrames;
		if( data->m_fullStrengthFrames > 0 )
		{
			if( now >= fullFrame + data->m_fullStrengthFrames )
			{
				beginRampDown();
			}
		}
		// FullStrengthTime 0 lasts until something else ends us; if nothing can, stop anyway.
		else if( now >= fullFrame + TORNADO_ORPHAN_FRAMES && !hasLifetimeUpdate() )
		{
			beginRampDown();
		}
	}

	Real strength = computeStrength( now );
	if( m_rampingDown && strength <= 0.0f )
	{
		releaseAll();
		if( data->m_killObjectWhenDone )
		{
			TheGameLogic->destroyObject( me );
		}
		return UPDATE_SLEEP_FOREVER;
	}

	HoldParams p;
	p.center = *me->getPosition();
	p.velocity.zero();
	if( m_hasPreviousCenter )
	{
		p.velocity.x = p.center.x - m_previousCenter.x;
		p.velocity.y = p.center.y - m_previousCenter.y;
	}
	m_previousCenter = p.center;
	m_hasPreviousCenter = TRUE;
	p.strength = strength;
	p.ring = __max( 0.01f, ( data->m_ringRadius > 0.0f ) ? data->m_ringRadius : ( data->m_radius * TORNADO_DEFAULT_RING_FRACTION ) );
	p.steady = p.ring * TORNADO_STEADY_FRACTION;
	p.ceiling = TheTerrainLogic->getGroundHeight( p.center.x, p.center.y ) + data->m_maxLiftHeight;
	p.riseSpeed = data->m_liftForce * strength;
	if( data->m_maxVictimSpeed > 0.0f && p.riseSpeed > data->m_maxVictimSpeed )
	{
		p.riseSpeed = data->m_maxVictimSpeed;
	}

	if( !m_rampingDown && strength > 0.0f )
	{
		gatherTargets( &p.center, data->m_radius, m_scratch );
		std::sort( m_scratch.begin(), m_scratch.end() );
		for( ObjectIDVectorIterator it = m_scratch.begin(); it != m_scratch.end(); ++it )
		{
			Object *obj = TheGameLogic->findObjectByID( *it );
			if( obj && obj->getPhysics()->getTornadoController() == INVALID_ID )
			{
				captureVictim( obj );
			}
		}
	}

	UnsignedInt held = 0;
	for( VictimVector::iterator it = m_victims.begin(); it != m_victims.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( it->id );
		UnsignedInt releaseDelay = 0;
		if( m_rampingDown && m_rampDownFrames > 0 )
		{
			// Mix stable ids so release order survives saving and consumes no random stream.
			UnsignedInt seed = (UnsignedInt)it->id ^ (UnsignedInt)me->getID() * 2654435761U ^ m_rampDownStartFrame;
			seed ^= seed >> 16;
			seed *= 2246822519U;
			seed ^= seed >> 13;
			releaseDelay = 1 + seed % m_rampDownFrames;
		}
		if( obj == nullptr )
		{
			continue;
		}
		if( obj->isEffectivelyDead() || !canGrab( obj ) ||
			( m_rampingDown && now - m_rampDownStartFrame >= releaseDelay ) )
		{
			releaseVictim( obj, it->flags );
			continue;
		}
		holdVictim( obj, p );
		m_victims[held++] = *it;
	}
	m_victims.resize( held );

	if( data->m_damagePulseFrames > 0 && data->m_damagePerSecond > 0.0f && m_nextDamagePulseFrame <= now )
	{
		doDamagePulse( &p.center, strength );
		m_nextDamagePulseFrame = now + data->m_damagePulseFrames;
	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TornadoUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TornadoUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	ObjectIDVector ids;
	for( VictimVector::const_iterator it = m_victims.begin(); it != m_victims.end(); ++it )
	{
		ids.push_back( it->id );
	}
	xfer->xferSTLObjectIDVector( &ids );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_victims.clear();
		for( ObjectIDVector::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			Victim victim;
			victim.id = *it;
			victim.flags = 0;
			if( version < 2 )
			{
				victim.flags = LEGACY_VICTIM;
			}
			m_victims.push_back( victim );
		}
	}
	xfer->xferUnsignedInt( &m_startFrame );
	xfer->xferUnsignedInt( &m_rampDownStartFrame );
	xfer->xferReal( &m_rampDownStartStrength );
	xfer->xferUnsignedInt( &m_nextDamagePulseFrame );
	xfer->xferBool( &m_rampingDown );
	xfer->xferBool( &m_externallyControlled );
	if( version >= 2 )
	{
		xfer->xferUnsignedInt( &m_rampUpFrames );
		xfer->xferUnsignedInt( &m_rampDownFrames );
		xfer->xferCoord3D( &m_previousCenter );
		xfer->xferBool( &m_hasPreviousCenter );
		for( VictimVector::iterator it = m_victims.begin(); it != m_victims.end(); ++it )
		{
			xfer->xferUnsignedInt( &it->flags );
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void TornadoUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

	for( VictimVector::iterator it = m_victims.begin(); it != m_victims.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( it->id );
		if( obj && obj->getPhysics() )
		{
			PhysicsBehavior *physics = obj->getPhysics();
			if( physics->getTornadoController() == INVALID_ID )
			{
				physics->setTornadoController( getObject()->getID() );
			}
			if( it->flags & LEGACY_VICTIM )
			{
				it->flags = 0;
				if( physics->getDefaultAllowCollideForce() )
				{
					it->flags |= RESTORE_COLLIDE_FORCE;
				}
				lockEnemyVictim( obj, it->flags );
			}
		}
	}

}  // end loadPostProcess
