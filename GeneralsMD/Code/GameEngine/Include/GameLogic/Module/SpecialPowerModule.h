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

// FILE: SpecialPowerModule.h /////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2002
// Desc:	 Special power module interface
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/Module.h"
#include "Common/Science.h"
#include "GameLogic/Module/BehaviorModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;
class SpecialPowerTemplate;
struct FieldParse;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerModuleInterface
{
public:

	virtual Bool isModuleForPower( const SpecialPowerTemplate *specialPowerTemplate ) const = 0;
	virtual Bool isReady() const = 0;
	virtual Bool isReadyForCommand( UnsignedInt commandOptions ) const = 0;
//  This is the althernate way to one-at-a-time BlackLotus' specials; we'll keep it commented her until Dustin decides, or until 12/10/02
//	virtual Bool isBusy() const = 0;
	virtual Real getPercentReady() const = 0;
	virtual UnsignedInt getReadyFrame() const = 0;
	virtual AsciiString getPowerName() const = 0;
	virtual const SpecialPowerTemplate* getSpecialPowerTemplate() const = 0;
	virtual ScienceType getRequiredScience() const = 0;
	virtual void onSpecialPowerCreation() = 0;
	virtual void setReadyFrame( UnsignedInt frame ) = 0;
	virtual void pauseCountdown( Bool pause ) = 0;
	virtual void doSpecialPower( UnsignedInt commandOptions ) = 0;
	virtual void doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions ) = 0;
	virtual void doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions ) = 0;
	virtual void doSpecialPowerUsingWaypoints( const Waypoint *way, UnsignedInt commandOptions ) = 0;
	virtual void markSpecialPowerTriggered( const Coord3D *location ) = 0;
	virtual void startPowerRecharge() = 0;
	// TheSuperHackers @feature StartCooldownOnFirstShot: the firing tracker drives the wait
	virtual Bool isWaitingForShots() const = 0;
	virtual void onShotFired() = 0;
	virtual void updatePendingShots() = 0;
	virtual const AudioEventRTS& getInitiateSound() const = 0;
	virtual Bool startsReady() const = 0;
	virtual Bool isScriptOnly() const = 0;

	//If the special power launches a construction site, we need to know the final product for placement purposes.
	virtual const ThingTemplate* getReferenceThingTemplate() const = 0;
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerModuleData : public BehaviorModuleData
{
public:

	SpecialPowerModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	const SpecialPowerTemplate *m_specialPowerTemplate;		///< pointer to the special power template
	AudioEventRTS			m_initiateSound;
	Bool							m_updateModuleStartsAttack;	///< update module determines when the special power actually starts! If true, update module is required.
	Bool							m_startsPaused; ///< Paused on creation, someone else will have to unpause (like upgrade module, or script)
	Bool							m_startsReady; ///< If true, the special power will be ready immediately, otherwise it will have a reload time on creation.
	Bool							m_scriptedSpecialPowerOnly;
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerModule : public BehaviorModule,
													 public SpecialPowerModuleInterface
{

	MEMORY_POOL_GLUE_ABC( SpecialPowerModule )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SpecialPowerModule, SpecialPowerModuleData )

public:

	SpecialPowerModule( Thing* thing, const ModuleData* moduleData );

	static Int getInterfaceMask() { return MODULEINTERFACE_SPECIAL_POWER; }

	// BehaviorModule
	virtual SpecialPowerModuleInterface* getSpecialPower() override { return this; }

	virtual Bool isModuleForPower( const SpecialPowerTemplate *specialPowerTemplate ) const override;	///< is this module for the specified special power
	virtual Bool isReadyForCommand( UnsignedInt commandOptions ) const override;
	virtual Bool isReady() const override; 						///< is this special power available now
//  This is the althernate way to one-at-a-time BlackLotus' specials; we'll keep it commented her until Dustin decides, or until 12/10/02
//	Bool isBusy() const { return FALSE; }

	virtual Real getPercentReady() const override;		///< get the percent ready (1.0 = ready now, 0.5 = half charged up etc.)

	virtual UnsignedInt getReadyFrame() const override;		///< get the frame at which we are ready
	virtual AsciiString getPowerName() const override;
	void syncReadyFrameToStatusQuo();

	virtual const SpecialPowerTemplate* getSpecialPowerTemplate() const override;
	virtual ScienceType getRequiredScience() const override;

	virtual void onSpecialPowerCreation() override;	// called by a create module to start our countdown
	//
	// The following methods are for use by the scripting engine ONLY
	//

	virtual void setReadyFrame( UnsignedInt frame ) override;
	UnsignedInt getReadyFrame() { return m_availableOnFrame; }// USED BY PLAYER TO KEEP RECHARGE TIMERS IN SYNC
	virtual void pauseCountdown( Bool pause ) override;

	//
	// the following methods should be *EXTENDED* for any special power module implementations
	// and carry out the special power executions
	//
	virtual void doSpecialPower( UnsignedInt commandOptions ) override;
	virtual void doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions ) override;
	virtual void doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions ) override;
	virtual void doSpecialPowerUsingWaypoints( const Waypoint *way, UnsignedInt commandOptions ) override;

	/**
	 Now, there are special powers that require some preliminary processing before the actual
	 special power triggers. When the ini setting "UpdateModuleStartsAttack" is true, then
	 the update module will call the doSpecialPower a second time. This function then resets
	 the power recharge, and tells the scriptengine that the attack has started.

	 A good example of something that uses this is the Black Lotus - capture building hack attack.
	 When the user initiates the attack, the doSpecialPower is called, which triggers the update
	 module. The update module then orders the unit to move within range, and it isn't until the
	 hacker start the physical attack, that the timer is reset and the attack technically begins.
	*/
	virtual void markSpecialPowerTriggered( const Coord3D *location ) override;

	/** start the recharge process for this special power. public because some powers call it repeatedly.
	*/
	virtual void startPowerRecharge() override;

	// TheSuperHackers @feature StartCooldownOnFirstShot. The module has no update of its own,
	// so the object's firing tracker reports the shots and ticks the wait.
	virtual Bool isWaitingForShots() const override { return m_pendingShotsState != PENDING_NONE; }
	virtual void onShotFired() override;
	virtual void updatePendingShots() override;

	virtual const AudioEventRTS& getInitiateSound() const override;

	virtual Bool startsReady() const override;
	virtual Bool isScriptOnly() const override;

	//If the special power launches a construction site, we need to know the final product for placement purposes.
	virtual const ThingTemplate* getReferenceThingTemplate() const override { return nullptr; }

protected:

	Bool canActivateForCommand( UnsignedInt commandOptions ) const;
	Bool initiateIntentToDoSpecialPower( const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions );
	void triggerSpecialPower( const Coord3D *location );
	void createViewObject( const Coord3D *location );
	void resolveSpecialPower();
	void aboutToDoSpecialPower( const Coord3D *location );

	void handleTargetDesignator(const Coord3D* location);

	/// start the cooldown right now, whatever the template asked for
	void beginCooldownNow();
	/// give the power back, for a use whose shots never happened
	void refundUnfiredPower();
	/// collect a cost that was deferred to the first shot
	void payPendingCost();
	/// forget any wait in progress
	void clearPendingShots();
	/// TRUE when this use should hold its cooldown until the ordered shots are away
	Bool shouldWaitForShots() const;
	/// the firing tracker ticks the wait, and it sleeps whenever nothing is shooting.
	/// FALSE when this object has no tracker at all, so no wait can be watched.
	Bool wakeFiringTrackerForWait();

	/// how long a dropped attack must stay dropped before it counts as a cancel
	enum { PENDING_CANCEL_SETTLE_FRAMES = LOGICFRAMES_PER_SECOND / 2 };

	enum
	{
		PENDING_NONE = 0,									///< no shots are being waited on
		PENDING_WAITING_FOR_FIRST_SHOT,		///< used, but nothing has fired yet
		PENDING_FIRING										///< firing; the cooldown starts when the burst ends
	};

	UnsignedInt m_availableOnFrame;			///< on this frame, this special power is available
	Int m_pausedCount;									///< Reference count of sources pausing me
	UnsignedInt m_pausedOnFrame;
	Real m_pausedPercent;
	Int m_pendingCost;									///< cost still owed, collected on the first shot
	Int m_pendingShotsState;						///< one of the PENDING_ values above
	UnsignedInt m_pendingTimeoutFrame;	///< start the cooldown anyway once this frame passes
	UnsignedInt m_pendingStartFrame;		///< frame the wait began, so the cameo can count down from it
	UnsignedInt m_pendingSettleFrame;		///< zero until the caster takes the attack up, then the frame
																			///< a dropped attack becomes a cancel

};
