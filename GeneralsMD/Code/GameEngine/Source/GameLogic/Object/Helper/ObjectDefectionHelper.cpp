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

// FILE: ObjectDefectionHelper.cpp ////////////////////////////////////////////////////////////////
// Author: Colin Day, Steven Johnson - September 2002
// Desc:   Object helper module
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/MiscAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ObjectDefectionHelper.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameLogic/Module/AIUpdate.h"


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ObjectDefectionHelper::~ObjectDefectionHelper()
{
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime ObjectDefectionHelper::update()
{
	Object* obj = getObject();
	Drawable* draw = obj->getDrawable();
	updateVehicleControl();

	// if we are here, we should be an undetected defector, but
	// just in case someone has changed us behind our backs...
	if (!obj->getIsUndetectedDefector())
	{
		return isVehicleControlled() ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER;
	}

	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_defectionDetectionEnd)
	{
		obj->friend_setUndetectedDefector(FALSE);

		// timer has reached zero, so we flash white once!!!! -- lorenzen
		if (draw && m_doDefectorFX)
		{
			RGBColor white = {1,1,1};
			if (draw)
				draw->flashAsSelected( &white ); //Whew! that's easier, now, isn't it!


			AudioEventRTS defectorVulnerableSound = TheAudio->getMiscAudio()->m_defectorTimerDingSound;
			defectorVulnerableSound.setObjectID( obj->getID() );
			TheAudio->addAudioEvent(&defectorVulnerableSound);
		}
		return isVehicleControlled() ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER;	// hey, we're done.
	}

	// dead or attacking... our cover is blown.
	if( obj->isEffectivelyDead() || obj->getStatusBits().test( OBJECT_STATUS_IS_FIRING_WEAPON ) )
	{
		// PLEASE NOTE:
		// checking the is_attacking statusbit above, only handles weapon related attacks...
		// I also set m_undetectedDefector = FALSE in the groupDoSpecialPower[...]() functions;
		obj->friend_setUndetectedDefector(FALSE);
		return isVehicleControlled() ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER;	// hey, we're done.
	}

	if (draw && m_doDefectorFX) // skip fx if merely 'invulnerable'
	{
		Bool lastPhase = ( ((Int)m_defectionDetectionFlashPhase) & 1 );// were we in a flashy phase last frame?
		UnsignedInt timeLeft = m_defectionDetectionEnd - now;
		m_defectionDetectionFlashPhase += 0.5f * ( 1.0f - ((Real)timeLeft / DEFECTION_DETECTION_TIME_MAX ) );
		Bool thisPhase = ( ((Int)m_defectionDetectionFlashPhase) & 1 );// are we in a flashy phase this frame?

		if ( lastPhase && ( ! thisPhase ) )
		{
			draw->flashAsSelected(); //Whew! that's easier, now, isn't it!

			AudioEventRTS defectorTimerSound = TheAudio->getMiscAudio()->m_defectorTimerTickSound;
			defectorTimerSound.setObjectID( obj->getID() );
			TheAudio->addAudioEvent(&defectorTimerSound);

		}
	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ObjectDefectionHelper::startVehicleControl( Object *source, UnsignedInt duration, UnsignedInt sustain, Bool continuous )
{
	Object *obj = getObject();
	if( !source || isVehicleControlled() || obj->isContained() || obj->isEffectivelyDead()
		|| !obj->getTeam() || !source->getTeam() || obj->getRelationship( source ) != ENEMIES
		|| (!continuous && duration == 0) )
	{
		return;
	}

	UnsignedInt returnTeamID = obj->getTeam()->getID();
	Int returnPlayerIndex = obj->getControllingPlayer()->getPlayerIndex();
	Team *controlTeam = source->getControllingPlayer()->getDefaultTeam();
	obj->defect( controlTeam, 0 );
	if( obj->getTeam() != controlTeam )
	{
		return;
	}

	m_controlSourceID = source->getID();
	m_controlTeamID = controlTeam->getID();
	m_returnTeamID = returnTeamID;
	m_returnPlayerIndex = returnPlayerIndex;
	m_controlStart = TheGameLogic->getFrame();
	m_controlEnd = m_controlStart + duration;
	m_controlSustain = sustain;
	m_controlContinuous = continuous;
	setWakeFrame( obj, UPDATE_SLEEP_NONE );
}

void ObjectDefectionHelper::returnVehicleControl()
{
	Object *obj = getObject();
	m_controlSourceID = INVALID_ID;
	m_controlContinuous = FALSE;
	if( !obj->getTeam() || obj->getTeam()->getID() != m_controlTeamID || obj->isEffectivelyDead() )
	{
		return;
	}

	Team *team = TheTeamFactory->findTeamByID( m_returnTeamID );
	if( !team )
	{
		Player *player = ThePlayerList->getNthPlayer( m_returnPlayerIndex );
		team = player ? player->getDefaultTeam() : ThePlayerList->getNeutralPlayer()->getDefaultTeam();
	}
	obj->setTeam( team );
	obj->resetTargetingOnSelf();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if( ai )
	{
		ai->aiIdle( CMD_FROM_AI );
	}
}

void ObjectDefectionHelper::stopVehicleControl( ObjectID sourceID )
{
	if( !isVehicleControlledBy( sourceID ) || !m_controlContinuous )
	{
		return;
	}
	m_controlContinuous = FALSE;
	m_controlStart = TheGameLogic->getFrame();
	m_controlEnd = m_controlStart + m_controlSustain;
	if( m_controlSustain == 0 )
	{
		returnVehicleControl();
	}
}

void ObjectDefectionHelper::updateVehicleControl()
{
	if( !isVehicleControlled() )
	{
		return;
	}
	Object *obj = getObject();
	if( obj->isEffectivelyDead() || !obj->getTeam() || obj->getTeam()->getID() != m_controlTeamID )
	{
		m_controlSourceID = INVALID_ID;
		m_controlContinuous = FALSE;
		return;
	}
	if( m_controlContinuous )
	{
		Object *source = TheGameLogic->findObjectByID( m_controlSourceID );
		SpecialAbilityUpdate *ability = source ? source->findSpecialAbilityUpdate( SPECIAL_BLACKLOTUS_CONTROL_VEHICLE_HACK ) : nullptr;
		if( !source || source->getControllingPlayer() != obj->getControllingPlayer()
			|| !ability || !ability->isControllingVehicle( obj->getID() ) || obj->isContained() )
		{
			stopVehicleControl( m_controlSourceID );
		}
	}
	if( isVehicleControlled() && !m_controlContinuous && TheGameLogic->getFrame() >= m_controlEnd )
	{
		returnVehicleControl();
	}
}

Bool ObjectDefectionHelper::getVehicleControlProgress( Real& progress ) const
{
	if( !isVehicleControlled() || !getObject()->getTeam() || getObject()->getTeam()->getID() != m_controlTeamID )
	{
		return FALSE;
	}
	UnsignedInt now = TheGameLogic->getFrame();
	progress = m_controlContinuous ? 1.0f : (now >= m_controlEnd || m_controlEnd == m_controlStart ? 0.0f
		: (Real)(m_controlEnd - now) / (Real)(m_controlEnd - m_controlStart));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
void ObjectDefectionHelper::startDefectionTimer(UnsignedInt numFrames, Bool withDefectorFX)
{
	Object* obj = getObject();

	if (!obj->getIsUndetectedDefector())
	{
		setWakeFrame(obj, isVehicleControlled() ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER);
		return;
	}

	UnsignedInt now = TheGameLogic->getFrame();
	m_defectionDetectionStart = now;
	m_defectionDetectionEnd = now + numFrames;
	m_defectionDetectionFlashPhase = 0.0f;
	m_doDefectorFX = withDefectorFX;

	setWakeFrame(obj, UPDATE_SLEEP_NONE);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ObjectDefectionHelper::crc( Xfer *xfer )
{

	// object helper crc
	ObjectHelper::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ObjectDefectionHelper::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// object helper base class
	ObjectHelper::xfer( xfer );

	// detection start
	xfer->xferUnsignedInt( &m_defectionDetectionStart );

	// detection end
	xfer->xferUnsignedInt( &m_defectionDetectionEnd );

	// flash phase
	xfer->xferReal( &m_defectionDetectionFlashPhase );

	// do defector fx
	xfer->xferBool( &m_doDefectorFX );

	if( version >= 2 )
	{
		xfer->xferObjectID( &m_controlSourceID );
		xfer->xferUnsignedInt( &m_controlTeamID );
		xfer->xferUnsignedInt( &m_returnTeamID );
		xfer->xferInt( &m_returnPlayerIndex );
		xfer->xferUnsignedInt( &m_controlStart );
		xfer->xferUnsignedInt( &m_controlEnd );
		xfer->xferUnsignedInt( &m_controlSustain );
		xfer->xferBool( &m_controlContinuous );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ObjectDefectionHelper::loadPostProcess()
{

	// object helper base class
	ObjectHelper::loadPostProcess();
	if( isVehicleControlled() )
	{
		setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
	}

}
