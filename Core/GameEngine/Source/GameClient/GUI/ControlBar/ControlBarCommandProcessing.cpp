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

// FILE: ControlBarCommandProcessing.cpp //////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   This file contain just the method responsible for processing the actual command
//				 clicks from the window controls in the UI
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/PlayerTemplate.h"

#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameClient.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MetaEvent.h"
// TheSuperHackers @feature for quick cast
#include "Common/OptionPreferences.h"
#include "Common/Recorder.h"
#include "GameClient/HotKey.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameClient/AnimateWindowManager.h"

// TheSuperHackers @feature How many units a shift click queues or cancels at once.
static const Int SHIFT_CLICK_BATCH_SIZE = 5;

// TheSuperHackers @feature Quick cast (Options.ini: CastMode).
/**
 * Fire a targeted command at the cursor instead of waiting for a second click.
 *
 * Returns TRUE only if the command was actually dispatched. Every rejection path returns
 * FALSE so the caller arms the command normally -- an input is never silently eaten.
 *
 * Deliberately limited to keyboard activation. Clicking a cameo with the mouse leaves the
 * cursor over the control bar, where there is no world position worth targeting.
 */
static Bool tryQuickCast( const CommandButton *commandButton )
{
	if( commandButton == nullptr || TheGlobalData == nullptr )
		return FALSE;

	if( TheGlobalData->m_castMode == CastMode_Normal )
		return FALSE;

	// only from a hotkey -- see the note above
	if( !HotKeyManager::isExecutingHotKey() )
		return FALSE;

	// Hold to aim: on the key down pass we want the normal arming path, which already shows the
	// targeting decal and lets the player move the cursor. The key up pass then fires.
	if( HotKeyManager::isQuickCastAiming() )
		return FALSE;

	if( TheInGameUI == nullptr || TheMouse == nullptr || TheTacticalView == nullptr )
		return FALSE;

	// leave replay playback alone, matching InGameUI::setGUICommand
	if( TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK )
		return FALSE;

	const UnsignedInt options = commandButton->getOptions();

	// Commands that must not be fired blind:
	//   NEED_N_TARGET_POS  needs several deliberate clicks by definition
	//   SINGLE_USE_COMMAND burns the button permanently, so a misfire is unrecoverable
	if( BitIsSet( options, NEED_N_TARGET_POS ) || BitIsSet( options, SINGLE_USE_COMMAND ) )
		return FALSE;

	// Rally points and beacons place a marker wherever the cursor happens to be, which is
	// silent and easy to miss. Structure placement needs a deliberate footprint.
	switch( commandButton->getCommandType() )
	{
		case GUI_COMMAND_SET_RALLY_POINT:
		case GUICOMMANDMODE_PLACE_BEACON:
		case GUI_COMMAND_DOZER_CONSTRUCT:
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT:
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT:
			return FALSE;

		// Superweapons are excluded on purpose: firing one at an unintended spot cannot be
		// undone, and the stray keypress that does it is easy to make.
		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
			return FALSE;

		default:
			break;
	}

	// The cursor has to be over the battlefield, not the command bar or another panel.
	const MouseIO *mouseIO = TheMouse->getMouseStatus();
	if( mouseIO == nullptr )
		return FALSE;

	if( TheWindowManager &&
			TheWindowManager->getWindowUnderCursor( mouseIO->pos.x, mouseIO->pos.y ) != nullptr )
		return FALSE;

	// TheSuperHackers @feature If the ability is still recharging, remember the cast and let
	// InGameUI fire it the moment the logic side says it is ready, rather than throwing the
	// input away. The cooldown itself is untouched -- this only stops the press being wasted.
	if( commandButton->getSpecialPowerTemplate() )
	{
		Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
		Object *source = draw ? draw->getObject() : nullptr;
		if( source )
		{
			SpecialPowerModuleInterface *mod =
				source->getSpecialPowerModule( commandButton->getSpecialPowerTemplate() );
			if( mod && !mod->isReady() )
			{
				TheInGameUI->queueQuickCast( commandButton, mouseIO->pos );
				TheInGameUI->triggerQuickCastHint( commandButton, mouseIO->pos );
				return TRUE;
			}
		}
	}

	// Hand the click to the normal path. Synthesizing the message rather than calling the
	// do*Command helpers directly means quick cast reuses the engine's own validation,
	// voice responses and cleanup, and cannot drift away from normal behaviour.
	//
	// In hold to aim mode the command was already armed on key down; re-arming here is
	// harmless and covers the case where something cleared it while the key was held.
	TheInGameUI->setGUICommand( commandButton );

	// GUICommandTranslator reads the click position from pixelRegion.hi
	IRegion2D clickRegion;
	clickRegion.lo = mouseIO->pos;
	clickRegion.hi = mouseIO->pos;

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_MOUSE_LEFT_CLICK );
	msg->appendPixelRegionArgument( clickRegion );

	// In indicator mode the decal has been visible the whole time the key was held, so let it
	// linger briefly at the point it fired rather than vanishing the instant the key comes up.
	if( TheGlobalData->m_castMode == CastMode_QuickCastWithIndicator )
		TheInGameUI->triggerQuickCastHint( commandButton, mouseIO->pos );

	return TRUE;
}

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
struct SelectObjectsInfo
{
	const ThingTemplate *thingTemplate;
	GameMessage *msg;
};

//-------------------------------------------------------------------------------------------------

static void selectObjectOfType( Object* obj, void* selectObjectsInfo )
{
	SelectObjectsInfo *soInfo = (SelectObjectsInfo*)selectObjectsInfo;

	//Do the templates match?
	if( obj->getTemplate()->isEquivalentTo( soInfo->thingTemplate ) )
	{
		//Okay, then add it to the selected group.
		soInfo->msg->appendObjectIDArgument( obj->getID() );

		Drawable *draw = obj->getDrawable();
		if( draw )
		{
			TheInGameUI->selectDrawable( draw );
		}
	}
}

#if RTS_ZEROHOUR
//-------------------------------------------------------------------------------------------------
/** Producers already auto queueing this unit stop. If there are none, every producer starts. */
//-------------------------------------------------------------------------------------------------
static void toggleAutoQueueUnit( const DrawableList &factories, const ThingTemplate *unitType, Bool canStart )
{
	Bool anyOn = FALSE;
	for( DrawableListCIt it = factories.begin(); it != factories.end(); ++it )
	{
		if( (*it)->getObject()->getProductionUpdateInterface()->isAutoQueueUnit( unitType ) )
		{
			anyOn = TRUE;
			break;
		}
	}

	// only the cameo enforces NEED_UPGRADE, so a greyed one may stop auto queue but never start it
	if( !anyOn && !canStart )
	{
		return;
	}

	AudioEventRTS click( "GUIClick" );
	TheAudio->addAudioEvent( &click );

	for( DrawableListCIt it = factories.begin(); it != factories.end(); ++it )
	{
		Object *factory = (*it)->getObject();
		if( anyOn && !factory->getProductionUpdateInterface()->isAutoQueueUnit( unitType ) )
		{
			continue;
		}

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_AUTO_QUEUE_UNIT );
		msg->appendObjectIDArgument( factory->getID() );
		msg->appendIntegerArgument( anyOn ? 0 : unitType->getTemplateID() );
	}
}
#endif

//-------------------------------------------------------------------------------------------------
// ShigureUi 19/09/2026 calculate estimated production finished time of a production update
//-------------------------------------------------------------------------------------------------
Real ControlBar::calcEstimatedProductionFinishedTime(Object *obj)
{
	if (!obj || !obj->isLocallyControlled())
		return -1.0;

	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if (!pu)
		return -1.0;

	Player* player = obj->getControllingPlayer();

	Real curFinishTime = 0.0;
	Int totalFrames;

	const ProductionEntry *pe = pu->firstProduction();

	if (pe)
	{
		totalFrames = 0;
		if (pe->getProductionType() == PRODUCTION_UNIT)
		{
			if (pe->getProductionObject())
				totalFrames = pe->getProductionObject()->calcTimeToBuild(player);
		}
		else if (pe->getProductionUpgrade())
		{
			totalFrames = pe->getProductionUpgrade()->calcTimeToBuild(player);
		}
		curFinishTime = (100.0f - pe->getPercentComplete()) / 100.f * totalFrames;

		while ((pe = pu->nextProduction(pe)) != nullptr)
		{
			if (pe->getProductionType() == PRODUCTION_UNIT)
			{
				if (pe->getProductionObject())
					curFinishTime += pe->getProductionObject()->calcTimeToBuild(player);
			}
			else if (pe->getProductionUpgrade())
			{
				curFinishTime += pe->getProductionUpgrade()->calcTimeToBuild(player);
			}
		}
	}

	//ShigureUi 20/09/2026 find this object's cache
	std::multimap<ObjectID, BuildQueueCacheNode>::iterator it;
	std::pair<std::multimap<ObjectID, BuildQueueCacheNode>::iterator, std::multimap<ObjectID, BuildQueueCacheNode>::iterator> pr;
	pr = m_multiSelectQueueCache.equal_range(obj->getID());

	for (it = pr.first; it != pr.second; it++)
	{
		const BuildQueueCacheNode &bqsn = (*it).second;
		if (bqsn.m_type == PRODUCTION_UNIT)
			curFinishTime += bqsn.m_objectToProduce->calcTimeToBuild(player);
		else
			curFinishTime += bqsn.m_upgradeToResearch->calcTimeToBuild(player);
	}

	return curFinishTime;
}


//-------------------------------------------------------------------------------------------------
// ShigureUi 20/09/2026 calculate how many slots left in a production update
//-------------------------------------------------------------------------------------------------
UnsignedInt ControlBar::calcBuildQueueRoomLeft(Object* obj, const ThingTemplate *thing, const UpgradeTemplate *upgrade)
{
	if (!obj || !obj->isLocallyControlled())
		return 0;

	ProductionUpdateInterface* pu = obj->getProductionUpdateInterface();
	if (!pu)
		return 0;

	Player* player = obj->getControllingPlayer();

	//ShigureUi 20/09/2026 find this object's cache
	std::multimap<ObjectID, BuildQueueCacheNode>::iterator it;
	std::pair<std::multimap<ObjectID, BuildQueueCacheNode>::iterator, std::multimap<ObjectID, BuildQueueCacheNode>::iterator> pr;
	pr = m_multiSelectQueueCache.equal_range(obj->getID());

	// how many more this producer takes before its queue, or its parking, full
	Int roomLeft = pu->getMaxQueueEntries() - pu->getProductionCount();
	if (roomLeft < 0)
		roomLeft = 0;

	if (thing)
	{

		for (BehaviorModule** i = obj->getBehaviorModules(); *i; ++i)
		{
			ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
			if (pp != nullptr)
			{
				if (pp->shouldReserveDoorWhenQueued(thing))
				{
					while (roomLeft && !pp->hasAvailableSpaceFor(thing, roomLeft))
					{
						roomLeft--;
					}

					for (it = pr.first; it != pr.second; it++)
					{
						const BuildQueueCacheNode &bqsn = (*it).second;
						if (roomLeft && bqsn.m_type == PRODUCTION_UNIT && bqsn.m_objectToProduce == thing)
							roomLeft--;
					}
				}
				break;
			}
		}
	}
	else if (upgrade)
	{
		roomLeft = min(1, roomLeft);
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			// ShigureUi 28/09/2026 UINT_MAX means already built or is building not queue full 0 means queue full
			if (player->hasUpgradeComplete(upgrade) || player->hasUpgradeInProduction(upgrade))
				roomLeft = UINT_MAX;

			// ShigureUi 20/09/2026 search for upgrade
			for (it = pr.first; it != pr.second; it++)
			{
				const BuildQueueCacheNode& bqsn = (*it).second;
				if (bqsn.m_type == PRODUCTION_UPGRADE && bqsn.m_upgradeToResearch == upgrade)
				{
					roomLeft = UINT_MAX;
					break;
				}
			}
		}
		else
		{
			if (obj->hasUpgrade(upgrade) || !obj->affectedByUpgrade(upgrade) || pu->isUpgradeInQueue(upgrade))
				roomLeft = UINT_MAX;

			// ShigureUi 20/09/2026 search for upgrade
			for (it = pr.first; it != pr.second; it++)
			{
				const BuildQueueCacheNode& bqsn = (*it).second;
				if (bqsn.m_type == PRODUCTION_UPGRADE && bqsn.m_upgradeToResearch == upgrade)
				{
					roomLeft = UINT_MAX;
					break;
				}
			}
		}
	}
	else
		roomLeft = 0;

	return roomLeft;
}

//-------------------------------------------------------------------------------------------------
// ShigureUi 20/09/2026 calculate build limit but not more than SHIFT_CLICK_BATCH_SIZE
//-------------------------------------------------------------------------------------------------
UnsignedInt ControlBar::calcBuildLimitLeft(Player *player, const ThingTemplate *thing)
{
	if (!thing || !player)
		return 0;

	//ShigureUi 20/09/2026 find this frame cache and remove others
	std::multimap<ObjectID, BuildQueueCacheNode>::iterator it;
	std::pair<std::multimap<ObjectID, BuildQueueCacheNode>::iterator, std::multimap<ObjectID, BuildQueueCacheNode>::iterator> pr;
	pr = std::make_pair(m_multiSelectQueueCache.begin(), m_multiSelectQueueCache.end());

	UnsignedInt limitLeft = SHIFT_CLICK_BATCH_SIZE;

	if (thing->getMaxSimultaneousOfType())
	{
		while (limitLeft && !player->canBuildMoreOfType(thing, limitLeft))
			limitLeft--;

		// ShigureUi 20/09/2026 build limit is global so scan all
		for (it = pr.first; it != pr.second; it++)
		{
			const BuildQueueCacheNode& bqsn = (*it).second;
			if (bqsn.m_type == PRODUCTION_UNIT && bqsn.m_objectToProduce == thing && limitLeft)
				limitLeft--;
		}
	}

	return limitLeft;
}

//-------------------------------------------------------------------------------------------------
/** Process a button transition message from the window system that should be for one of
	* our GUI commands */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processCommandTransitionUI( GameWindow *control, GadgetGameMessage gadgetMessage )
{
	// sanity, we won't process messages if we have no source object
	if( m_currContext != CB_CONTEXT_MULTI_SELECT &&
			(m_currentSelectedDrawable == nullptr ||
			 m_currentSelectedDrawable->getObject() == nullptr) )
	{

		if( m_currContext != CB_CONTEXT_NONE &&
				m_currContext != CB_CONTEXT_OBSERVER_INFO &&
				m_currContext != CB_CONTEXT_OBSERVER_LIST)
			switchToContext( CB_CONTEXT_NONE, nullptr );
		return CBC_COMMAND_NOT_USED;

	}

	return CBC_COMMAND_USED;

}

#if RTS_ZEROHOUR
//-------------------------------------------------------------------------------------------------
static Bool isPowerHotkeyButton( const CommandButton *button )
{
	if( button == nullptr )
	{
		return FALSE;
	}
	if( button->getCommandType() != GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT &&
			button->getCommandType() != GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT )
	{
		return FALSE;
	}
	return !BitIsSet( button->getOptions(), SCRIPT_ONLY ) && button->getButtonImage() != nullptr &&
		button->getSpecialPowerTemplate() != nullptr;
}

//-------------------------------------------------------------------------------------------------
const CommandButton *ControlBar::getPowerHotkeyButton( const CommandSet *set, Int number, Int *slot ) const
{
	if( set == nullptr || number < 1 )
	{
		return nullptr;
	}
	Int count = 0;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( !isPowerHotkeyButton( button ) )
		{
			continue;
		}
		if( ++count == number )
		{
			if( slot )
			{
				*slot = i;
			}
			return button;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const CommandSet *ControlBar::findPowerHotkeySet( const PlayerTemplate *side, Player *player )
{
	if( side == nullptr )
	{
		return nullptr;
	}
	const AsciiString barName = side->getCommandSet( PLAYER_COMMANDSET_SHORTCUT, player );
	if( barName.isEmpty() )
	{
		return nullptr;
	}
	const CommandSet *bar = findCommandSet( barName );
	if( bar == nullptr )
	{
		return nullptr;
	}
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = bar->getCommandButton( i );
		if( button == nullptr || button->getCommandType() != GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		{
			continue;
		}
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing == nullptr )
		{
			continue;
		}
		Object *holder = player ? player->findAnyExistingObjectWithThingTemplate( thing ) : nullptr;
		const AsciiString& name = holder ? holder->getCommandSetString() : thing->friend_getCommandSetString();
		const CommandSet *held = findCommandSet( name );
		if( getPowerHotkeyButton( held, 1 ) )
		{
			return held;
		}
	}

	// a mod without a holder object keeps its powers on the shortcut bar itself
	return getPowerHotkeyButton( bar, 1 ) ? bar : nullptr;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::firePowerHotkey( Int number )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( player == nullptr || !player->isPlayerActive() || player->getPlayerTemplate() == nullptr )
	{
		return;
	}
	if( TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK )
	{
		return;
	}
	const CommandButton *button = getPowerHotkeyButton( findPowerHotkeySet( player->getPlayerTemplate(), player ), number );
	if( button == nullptr )
	{
		return;
	}

	Bool usable = TRUE;
	const SpecialPowerTemplate *power = button->getSpecialPowerTemplate();
	if( BitIsSet( button->getOptions(), NEED_SPECIAL_POWER_SCIENCE ) &&
			power->getRequiredScience() != SCIENCE_INVALID && !player->hasScience( power->getRequiredScience() ) )
	{
		usable = FALSE;
	}
	if( usable )
	{
		usable = getCommandAvailability( button, nullptr, nullptr ) == COMMAND_AVAILABLE;
	}

	if( !usable )
	{
		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
		{
			TheAudio->addAudioEvent( &disabledClick );
		}
		return;
	}

	processCommandUI( nullptr, GBM_SELECTED, button );

	AudioEventRTS buttonClick( "GUIClick" );
	if( TheAudio )
	{
		TheAudio->addAudioEvent( &buttonClick );
	}
}

//-------------------------------------------------------------------------------------------------
Int ControlBar::getPowerHotkeyCorner( const CommandButton *button )
{
	if( !isPowerHotkeyButton( button ) )
	{
		return 0;
	}
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( player == nullptr || player->getPlayerTemplate() == nullptr || TheMetaMap == nullptr )
	{
		return 0;
	}
	const CommandSet *set = findPowerHotkeySet( player->getPlayerTemplate(), player );
	for( Int meta = GameMessage::MSG_META_SPECIAL_POWER_1; meta <= GameMessage::MSG_META_SPECIAL_POWER_16; ++meta )
	{
		const CommandButton *numbered = getPowerHotkeyButton( set, meta - GameMessage::MSG_META_SPECIAL_POWER_1 + 1 );
		if( numbered == nullptr )
		{
			return 0;
		}
		if( numbered != button )
		{
			continue;
		}

		// getMetaMapRec would add a record for an unmapped command
		const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec();
		while( rec && rec->m_meta != meta )
		{
			rec = rec->m_next;
		}
		if( rec == nullptr || rec->m_usableIn == COMMANDUSABLE_NONE || ( rec->m_key == MK_NONE && rec->m_modState == NONE ) )
		{
			return 0;
		}
		return HotKeyManager::chordCorner( rec->m_key, rec->m_modState );
	}
	return 0;
}
#endif

//-------------------------------------------------------------------------------------------------
/** Process a button selected message from the window system that should be for one of
	* our GUI commands */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processCommandUI( GameWindow *control,
																							GadgetGameMessage gadgetMessage,
																							const CommandButton *windowless )
{
	// get the command pointer from the control user data we put in the button
	// a windowless command is one of the shortcut power types, which need no selection and no button
	const CommandButton *commandButton = control ? (const CommandButton *)GadgetButtonGetData(control) : windowless;
	if( !commandButton )
	{
		DEBUG_CRASH( ("ControlBar::processCommandUI() -- Button activated has no data. Ignoring...") );
		return CBC_COMMAND_NOT_USED;
	}

	// sanity, we won't process messages if we have no source object,
	// unless we're CB_CONTEXT_PURCHASE_SCIENCE or GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT
	if( m_currContext != CB_CONTEXT_MULTI_SELECT &&
			commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE &&
			commandButton->getCommandType() != GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT &&
			commandButton->getCommandType() != GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT &&
			commandButton->getCommandType() != GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE &&
			(m_currentSelectedDrawable == nullptr || m_currentSelectedDrawable->getObject() == nullptr) )
	{

		if( m_currContext != CB_CONTEXT_NONE )
			switchToContext( CB_CONTEXT_NONE, nullptr );
		return CBC_COMMAND_NOT_USED;

	}

	// sanity
	if( control == nullptr && windowless == nullptr )
		return CBC_COMMAND_NOT_USED;

	// the context sensitive gui only is only made of buttons ... sanity
	if( control && control->winGetInputFunc() != GadgetPushButtonInput )
		return CBC_COMMAND_NOT_USED;


	if( commandButton == nullptr )
		return CBC_COMMAND_NOT_USED;

	// if the button is flashing, tell it to stop flashing
	commandButton->setFlashCount(0);
	setFlash( FALSE );

	if( commandButton->getCommandType() != GUI_COMMAND_EXIT_CONTAINER )
	{
		// Only stamp a real image. A command with no ButtonImage - the generic cancel
		// commands on the build queue, whose cameo populateBuildQueue stamps afterwards -
		// must not have its image nulled by the click, or any click that ends without a
		// repopulate leaves that cameo blank. Same guard setControlCommand uses.
		if( control && commandButton->getButtonImage() )
		{
			GadgetButtonSetEnabledImage( control, commandButton->getButtonImage() );
		}
	}

	//
	// get the object that is driving the context sensitive UI if we're not in a multi
	// select context
	//
	Object *obj = nullptr;
	const DrawableList* selected = TheInGameUI->getAllSelectedDrawables();
	DrawableList factories;
	Drawable* draw;

	// ShigureUi 13/9/2026 prepare producer list for unit build and upgrade
	if (commandButton->getCommandType() == GUI_COMMAND_UNIT_BUILD ||
		commandButton->getCommandType() == GUI_COMMAND_CANCEL_UNIT_BUILD ||
		commandButton->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
		commandButton->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE ||
		commandButton->getCommandType() == GUI_COMMAND_CANCEL_UPGRADE)
	{
		for (DrawableListCIt it = selected->begin();
			it != selected->end(); ++it)
		{

			// get the drawable
			draw = *it;


			if (draw->getObject()->isKindOf(KINDOF_IGNORED_IN_GUI)) // ignore these guys
				continue;

			// TheSuperHackers @feature With a type focused in the smart selection row, only that type
			// contributes, so the bar shows its command set rather than the group's common subset.
			if (!isSmartSelectionFocused(draw->getObject()))
			{
				continue;
			}

			// a right click on a disabled producer can still stop its auto queue
			if (draw && draw->getObject() &&
				!draw->getObject()->getStatusBits().test(OBJECT_STATUS_SOLD) &&
				!draw->getObject()->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) &&
				(gadgetMessage == GBM_SELECTED_RIGHT || !draw->getObject()->isDisabled()))
			{
				factories.push_back(draw);
			}
		}

		//sanity
		for (DrawableListCIt it = factories.begin();
			it != factories.end(); ++it)
			if (!(*it)->getObject()->getProductionUpdateInterface() || !(*it)->getObject()->isLocallyControlled())
			{
				factories.clear();
				break;
			}
	}

	// ShigureUi 13/9/2026 otherwise give them the object
	if( m_currContext != CB_CONTEXT_MULTI_SELECT &&
			commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE &&
			commandButton->getCommandType() != GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT &&
			commandButton->getCommandType() != GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT &&
			commandButton->getCommandType() != GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		obj = m_currentSelectedDrawable->getObject();

	//@todo Kris -- Special case code so convoy trucks can detonate nuke trucks -- if other things need this,
	//rethink it.
	if( obj && BitIsSet( commandButton->getOptions(), SINGLE_USE_COMMAND ) )
	{
		/** @todo Added obj check because Single Use and Multi Select crash when used together, but with this check
			* they just won't work.  When the "rethinking" occurs, this can get fixed.  Right now it is unused.
			* Convoy Truck needs Multi Select so Single Use is turned off, and noone else has it.
		*/

		//Make sure the command button is marked as used if it's a single use command. That way
		//we can never press the button again. This was added specifically for nuke convoy trucks.
		//When you click to detonate the nuke, it takes a few seconds to detonate in order to play
		//a sound. But we want to disable the button after the first click.
		obj->markSingleUseCommandUsed(); //Yeah, an object can only use one single use command...
	}

	TheInGameUI->placeBuildAvailable( nullptr, nullptr );

	//Play any available unit specific sound for button
	Player *player = ThePlayerList->getLocalPlayer();
	if( player )
	{
		AudioEventRTS sound = *commandButton->getUnitSpecificSound();
		sound.setPlayerIndex( player->getPlayerIndex() );
		TheAudio->addAudioEvent( &sound );
	}

	if( BitIsSet( commandButton->getOptions(), COMMAND_OPTION_NEED_TARGET ) )
	{
		if (commandButton->getOptions() & USES_MINE_CLEARING_WEAPONSET)
		{
			TheMessageStream->appendMessage( GameMessage::MSG_SET_MINE_CLEARING_DETAIL );
		}

		//June 06, 2002 -- Major change
		//I've added support for specific context sensitive commands which need targets just like
		//other options may need. When we need a target, the user must move the cursor to a position
		//where he wants the GUI command to take place. Older commands such as napalm strikes or daisy
		//cutter drops simply needed the user to click anywhere he desired.
		//
		//Now, we have new commands that will only work when the user clicks on valid targets to interact
		//with. For example, the terrorist can jack a car and convert it into a carbomb, but he has to
		//click on a valid car. In this case the doCommandOrHint code will determine if the mode is valid
		//or not and the cursor modes will be set appropriately.

		// TheSuperHackers @feature Quick cast fires the command at the cursor instead of waiting for
		// a second click. If it declines -- wrong mode, unsafe command, cursor not over the
		// battlefield -- fall through and arm normally, so nothing is ever silently swallowed.
		if( tryQuickCast( commandButton ) )
			return CBC_COMMAND_USED;

		TheInGameUI->setGUICommand( commandButton );
	}
#if RTS_ZEROHOUR
	// TheSuperHackers @fix In hold to aim mode the key down pass exists only to arm targeted
	// commands so the decal shows while aiming. Everything else must act on the key up pass
	// alone - otherwise a production hotkey queues two units per press and a toggle undoes
	// itself, one per key transition.
	else if( HotKeyManager::isQuickCastAiming() )
	{
		// swallow the key down half; the key up pass executes normally
	}
#endif
	else switch( commandButton->getCommandType() )
	{

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_DOZER_CONSTRUCT:
		{

			//Kris: September 27, 2002
			//Make sure we have enough CASH to build it WHEN we click the button to build it,
			//before actually previewing the purchase, otherwise, cancel altogether.
			const ThingTemplate *whatToBuild = commandButton->getThingTemplate();
			CanMakeType cmt = CANMAKE_NO_PREREQ, curCmt;
			Bool hasCmt = false;
			Int rating;

			for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
			{
				curCmt = TheBuildAssistant->canMakeUnit((*it)->getObject(), whatToBuild);
				rating = rateCanMakeType(curCmt);

				if (rating < 0)
				{
					hasCmt = true;
					cmt = curCmt;
					break;
				}

				if (!hasCmt)
				{
					hasCmt = true;
					cmt = curCmt;
				}
				else if (rating < rateCanMakeType(cmt))
					cmt = curCmt;
			}

			if (!hasCmt)
				break;
			
			if (cmt == CANMAKE_NO_MONEY)
			{
				TheEva->setShouldPlay(EVA_InsufficientFunds);
				TheInGameUI->message( "GUI:NotEnoughMoneyToBuild" );
				break;
			}
			else if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message( "GUI:ProductionQueueFull" );
				break;
			}
			else if (cmt == CANMAKE_PARKING_PLACES_FULL)
			{
				TheInGameUI->message( "GUI:ParkingPlacesFull" );
				break;
			}
			else if( cmt == CANMAKE_MAXED_OUT_FOR_PLAYER )
			{
				TheInGameUI->message( "GUI:UnitMaxedOut" );
				break;
			}

			// tell the UI that we want to build something so we get a building at the cursor
			TheInGameUI->placeBuildAvailable( commandButton->getThingTemplate(), selected);

			break;

		}


		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT:
		{
			//Determine the object that would construct it.
			const SpecialPowerTemplate *spTemplate = commandButton->getSpecialPowerTemplate();
			DEBUG_ASSERTCRASH(spTemplate != nullptr, ("Special Power Button is missing Special Power template"));

			SpecialPowerType spType = spTemplate->getSpecialPowerType();
			Object* obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( spType );
			if( !obj )
				break;
			Drawable *draw = obj->getDrawable();

			const ThingTemplate *whatToBuild = commandButton->getThingTemplate();

			CanMakeType cmt = TheBuildAssistant->canMakeUnit( obj, whatToBuild );
			if (cmt == CANMAKE_NO_MONEY)
			{
				TheEva->setShouldPlay(EVA_InsufficientFunds);
				TheInGameUI->message( "GUI:NotEnoughMoneyToBuild" );
				break;
			}
			else if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message( "GUI:ProductionQueueFull" );
				break;
			}
			else if (cmt == CANMAKE_PARKING_PLACES_FULL)
			{
				TheInGameUI->message( "GUI:ParkingPlacesFull" );
				break;
			}
			else if( cmt == CANMAKE_MAXED_OUT_FOR_PLAYER )
			{
				TheInGameUI->message( "GUI:UnitMaxedOut" );
				break;
			}

			DrawableList drawList;
			drawList.push_back(draw);
			// tell the UI that we want to build something so we get a building at the cursor
			TheInGameUI->placeBuildAvailable( commandButton->getThingTemplate(), &drawList);

			ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
			if( pu )
			{
				pu->setSpecialPowerConstructionCommandButton( commandButton );
			}

			break;
		}
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT:
		{
			// sanity
			if( m_currentSelectedDrawable == nullptr )
				break;

			const ThingTemplate *whatToBuild = commandButton->getThingTemplate();

			CanMakeType cmt = TheBuildAssistant->canMakeUnit( obj, whatToBuild );
			if (cmt == CANMAKE_NO_MONEY)
			{
				TheEva->setShouldPlay(EVA_InsufficientFunds);
				TheInGameUI->message( "GUI:NotEnoughMoneyToBuild" );
				break;
			}
			else if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message( "GUI:ProductionQueueFull" );
				break;
			}
			else if (cmt == CANMAKE_PARKING_PLACES_FULL)
			{
				TheInGameUI->message( "GUI:ParkingPlacesFull" );
				break;
			}
			else if( cmt == CANMAKE_MAXED_OUT_FOR_PLAYER )
			{
				TheInGameUI->message( "GUI:UnitMaxedOut" );
				break;
			}


			DrawableList drawList;
			drawList.push_back(m_currentSelectedDrawable);
			// tell the UI that we want to build something so we get a building at the cursor
			TheInGameUI->placeBuildAvailable( commandButton->getThingTemplate(), &drawList);

			ProductionUpdateInterface* pu = obj->getProductionUpdateInterface();
			if( pu )
			{
				pu->setSpecialPowerConstructionCommandButton( commandButton );
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
		{

			// get the object we have selected
			Object *building = obj;
			if( building == nullptr )
				break;

			// sanity check, the building must be under our control to cancel construction
			if( !building->isLocallyControlled() )
				break;

			// do the message
			TheMessageStream->appendMessage( GameMessage::MSG_DOZER_CANCEL_CONSTRUCT );

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_UNIT_BUILD:
		{
			const ThingTemplate *whatToBuild = commandButton->getThingTemplate();

			if( factories.size() == 0)
				break;

			// sanity, we must have something to build
			DEBUG_ASSERTCRASH( whatToBuild, ("Undefined BUILD command for object '%s'",
												 commandButton->getThingTemplate()->getName().str()) );

#if RTS_ZEROHOUR
			if( gadgetMessage == GBM_SELECTED_RIGHT )
			{
				toggleAutoQueueUnit( factories, whatToBuild, control->winGetEnabled() );
				break;
			}
#endif

			Real curFinishTime;
			Object *curFactory;
			CanMakeType cmt = CANMAKE_FACTORY_IS_DISABLED, curCmt;

			// TheSuperHackers @feature Shift queues a batch instead of a single unit.
			Int unitsToQueue = 1;
			if (TheKeyboard && TheKeyboard->isShift())
				unitsToQueue = SHIFT_CLICK_BATCH_SIZE;

			// every producer that can take the unit, so a batch can be spread across them
			ObjectVector okFactories;
			std::vector<Real> finishTimes;
			std::vector<Int> roomToLeft;

			// ShigureUi 13/9/2026 find best producer, compare them estimated finish time
			for (DrawableListCIt it = factories.begin(); it != factories.end(); it++)
			{
				curFactory = (*it)->getObject();
				curFinishTime = calcEstimatedProductionFinishedTime(curFactory);

				if (curFinishTime < 0)
				{
					cmt = CANMAKE_FACTORY_IS_DISABLED;
					break;
				}

				curCmt = TheBuildAssistant->canMakeUnit(curFactory, whatToBuild);

				if (it == factories.begin())
				{
					cmt = curCmt;
					// ShigureUi 13/9/2026 if these CANMAKE type then no hope, no need to check others 
					if (cmt == CANMAKE_NO_MONEY || cmt == CANMAKE_NO_PREREQ || cmt == CANMAKE_MAXED_OUT_FOR_PLAYER)
						break;
				}

				// ShigureUi 13/9/2026 record all canmake items
				if (curCmt == CANMAKE_OK)
				{
					cmt = CANMAKE_OK;
					okFactories.push_back(curFactory);
					finishTimes.push_back(curFinishTime);

					// how many more this producer takes before its queue, or its parking, fills
					Int room = calcBuildQueueRoomLeft(curFactory, whatToBuild, nullptr);
					roomToLeft.push_back(room);
				}
				// ShigureUi 13/9/2026 queue full is better than parking places full, update if possible
				else if (curCmt == CANMAKE_QUEUE_FULL && cmt == CANMAKE_PARKING_PLACES_FULL)
						cmt = CANMAKE_QUEUE_FULL;
			}


			if (cmt == CANMAKE_NO_MONEY)
			{
				TheEva->setShouldPlay(EVA_InsufficientFunds);
				TheInGameUI->message( "GUI:NotEnoughMoneyToBuild" );
				break;
			}
			else if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message( "GUI:ProductionQueueFull" );
				break;
			}
			else if (cmt == CANMAKE_PARKING_PLACES_FULL)
			{
				TheInGameUI->message( "GUI:ParkingPlacesFull" );
				break;
			}
			else if( cmt == CANMAKE_MAXED_OUT_FOR_PLAYER )
			{
				TheInGameUI->message( "GUI:UnitMaxedOut" );
				break;
			}
			else if (cmt != CANMAKE_OK)
			{
				DEBUG_CRASH( ("Cannot create '%s' because the factory object '%s' returns false for canMakeUnit",
																whatToBuild->getName().str(),
																(*factories.begin())->getObject()->getTemplate()->getName().str()) );
				break;
			}

			// the batch stops where the money or the per type limit runs out; canMakeUnit
			// already vouched for the first unit
			const Int unitCost = whatToBuild->calcCostToBuild(player);
			if (unitCost > 0)
			{
				unitsToQueue = MIN(unitsToQueue, (Int)(player->getMoney()->countMoney() / unitCost));
			}

			unitsToQueue = MIN(unitsToQueue, calcBuildLimitLeft(player, whatToBuild));

			const Int unitFrames = whatToBuild->calcTimeToBuild(player);
			for( Int queued = 0; queued < unitsToQueue; )
			{
				if (finishTimes.empty())
				{
					break;
				}
				// the producer that would finish first takes the unit and is charged for it
				const size_t best = std::min_element(finishTimes.begin(), finishTimes.end()) - finishTimes.begin();
				if (roomToLeft[best] < 1)
				{
					okFactories.erase(okFactories.begin() + best);
					finishTimes.erase(finishTimes.begin() + best);
					roomToLeft.erase(roomToLeft.begin() + best);
					continue;
				}
				Object *factory = okFactories[best];
				finishTimes[best] += unitFrames;
				roomToLeft[best]--;

				// get a new production id to assign to this
				ProductionID productionID = factory->getProductionUpdateInterface()->requestUniqueUnitID();

				// create a message to build this thing
				// ShigureUi 13/9/2026 Add a new factory objectID argument, otherwise message processor needs to find best producer again
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UNIT_CREATE );
				msg->appendIntegerArgument( whatToBuild->getTemplateID() );
				msg->appendObjectIDArgument( factory->getID() );
				msg->appendIntegerArgument( productionID );

				// ShigureUi 20/09/2026 Send it to cache
				BuildQueueCacheNode bqcn;
				bqcn.m_type = PRODUCTION_UNIT;
				bqcn.m_objectToProduce = whatToBuild;
				bqcn.m_productionID = productionID;
				m_multiSelectQueueCache.insert(std::make_pair(factory->getID(), bqcn));

				queued++;
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_CANCEL_UNIT_BUILD:
		{
			Int i;

			// find out which index (i) in the queue represents the button clicked
			for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
				if( m_queueData[ i ].control == control )
					break;

			// sanity, control not found
			if( i == MAX_BUILD_QUEUE_BUTTONS )
			{

				DEBUG_CRASH( ("Control not found in build queue data") );
				break;

			}

			// sanity
			if( m_queueData[ i ].type != PRODUCTION_UNIT )
				break;

			// the the production ID to cancel
			ProductionID productionIDToCancel = m_queueData[ i ].productionID;

			// A right click moves the entry one place earlier and never cancels.
			// ShigureUi 13/9/2026 only work when there's no multiselect
			if( gadgetMessage == GBM_SELECTED_RIGHT )
			{
				if( TheGlobalData->m_queueReorder && factories.size() == 1 && !factories.front()->getObject()->isDisabled() && i > 0 )
				{
					GameMessage *moveMsg = TheMessageStream->appendMessage( GameMessage::MSG_MOVE_UNIT_CREATE_EARLIER );
					moveMsg->appendIntegerArgument( productionIDToCancel );
				}
				break;
			}

			ProductionUpdateInterface *pu;
			const ProductionEntry *pe;
			const ThingTemplate *typeToCancel = nullptr;

			Object *curFactory = nullptr;

			// ShigureUi 13/9/2026 Find the production and save the type
			for (DrawableListCIt it = factories.begin(); it != factories.end(); it++)
			{
				Object *factory = (*it)->getObject();
				if (!factory || !factory->isLocallyControlled())
					break;
				pu = factory->getProductionUpdateInterface();
				if (m_queueData[i].producer == factory)
				{
					curFactory = factory;
					for (pe = pu->firstProduction(); pe; pe = pu->nextProduction(pe))
						if (pe->getProductionType() == PRODUCTION_UNIT && pe->getProductionID() == productionIDToCancel)
						{
							typeToCancel = pe->getProductionObject();
							break;
						}
					break;
				}
			}

			// TheSuperHackers @feature Shift cancels every queued unit of the clicked entry's type
			// ShigureUi 13/9/2026 instead of decide here, cancel all needs to add new templateID argument and send them to the message processor
			// When ultiselect cancel the production alone also needs producer ID
			Bool cancelAll = TheKeyboard && TheKeyboard->isShift();
			if (cancelAll ? !typeToCancel : !curFactory)
				break;

			GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CANCEL_UNIT_CREATE);
			if (cancelAll)
			{
				msg->appendBooleanArgument(true);
				msg->appendIntegerArgument(typeToCancel->getTemplateID());
			}
			else
			{
				msg->appendBooleanArgument(false);
				msg->appendIntegerArgument(productionIDToCancel);
				msg->appendObjectIDArgument(curFactory->getID());
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_PLAYER_UPGRADE:
		{
			const UpgradeTemplate* upgradeT = commandButton->getUpgradeTemplate();
			DEBUG_ASSERTCRASH(upgradeT, ("Undefined upgrade '%s' in player upgrade command", "UNKNOWN"));

			// sanity
			if (factories.size() == 0 || upgradeT == nullptr)
				break;

			// make sure the player can really make this
			if (TheUpgradeCenter->canAffordUpgrade(ThePlayerList->getLocalPlayer(), upgradeT, TRUE) == FALSE)
			{
				break;
			}

			Real minFinishTime = 1e9, curFinishTime;
			Object *bestFactory = nullptr, * curFactory;
			CanMakeType cmt = CANMAKE_NO_PREREQ, curCmt;
			UnsignedInt roomLeft;

			// ShigureUi 13/9/2026 Find best producer for the upgrade
			for (DrawableListCIt it = factories.begin(); it != factories.end(); it++)
			{
				curFactory = (*it)->getObject();
				curFinishTime = calcEstimatedProductionFinishedTime(curFactory);

				if (curFinishTime < 0)
				{
					cmt = CANMAKE_NO_PREREQ;
					break;
				}

				roomLeft = calcBuildQueueRoomLeft(curFactory, nullptr, upgradeT);

				curCmt = CANMAKE_NO_PREREQ;
				if (roomLeft == 1)
					curCmt = CANMAKE_OK;
				else if (roomLeft == 0)
					curCmt = CANMAKE_QUEUE_FULL;

				// ShigureUi 13/9/2026 update if better
				if (curCmt == CANMAKE_OK)
				{
					if (curFinishTime < minFinishTime || cmt != CANMAKE_OK)
					{
						cmt = CANMAKE_OK;
						minFinishTime = curFinishTime;
						bestFactory = curFactory;
					}
				}
				else if (curCmt == CANMAKE_QUEUE_FULL && cmt == CANMAKE_NO_PREREQ)
					cmt = CANMAKE_QUEUE_FULL;

				if (minFinishTime == 0.0)
					break;
			}

			if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message("GUI:ProductionQueueFull");
				break;
			}
			else if (cmt != CANMAKE_OK)
			{
				break;
			}

			// send the message
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UPGRADE );
			msg->appendObjectIDArgument( bestFactory->getID() );
			msg->appendIntegerArgument( upgradeT->getUpgradeNameKey() );

			// ShigureUi 20/09/2026 Send it to cache
			BuildQueueCacheNode bqcn;
			bqcn.m_type = PRODUCTION_UPGRADE;
			bqcn.m_upgradeToResearch = upgradeT;
			m_multiSelectQueueCache.insert(std::make_pair(bestFactory->getID(), bqcn));

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_OBJECT_UPGRADE:
		{
			const UpgradeTemplate *upgradeT = commandButton->getUpgradeTemplate();
			DEBUG_ASSERTCRASH( upgradeT, ("Undefined upgrade '%s' in object upgrade command", "UNKNOWN") );

			// sanity
			if (factories.size() == 0 || upgradeT == nullptr)
				break;

			//Make sure the player can really make this
			if( TheUpgradeCenter->canAffordUpgrade( ThePlayerList->getLocalPlayer(), upgradeT, TRUE ) == FALSE )
			{
				//Kris: Disabled because we can get a valid reason for not being able to afford the upgrade!
				//TheInGameUI->message( "upgrade unsupported in commandprocessing." );
				break;
			}

			Real minFinishTime[SHIFT_CLICK_BATCH_SIZE], curFinishTime;
			Object *bestFactories[SHIFT_CLICK_BATCH_SIZE], *curFactory;
			Int i, j;
			CanMakeType cmt = CANMAKE_QUEUE_FULL, curCmt;
			UnsignedInt roomLeft;

			for (i = 0; i < SHIFT_CLICK_BATCH_SIZE; i++)
			{
				minFinishTime[i] = 1e9;
				bestFactories[i] = nullptr;
			}

			// TheSuperHackers @feature Shift queues upgrade on a batch of units instead of single.
			Int upgradeToQueue = 1;
			if (TheKeyboard && TheKeyboard->isShift())
				upgradeToQueue = SHIFT_CLICK_BATCH_SIZE;

			// ShigureUi 13/9/2026 Find 5 best producer, or 1
			for (DrawableListCIt it = factories.begin(); it != factories.end(); it++)
			{
				curFactory = (*it)->getObject();
				curFinishTime = 0.0;

				curFinishTime = calcEstimatedProductionFinishedTime(curFactory);

				if (curFinishTime < 0)
				{
					cmt = CANMAKE_NO_PREREQ;
					break;
				}
	
				roomLeft = calcBuildQueueRoomLeft(curFactory, nullptr, upgradeT);

				curCmt = CANMAKE_NO_PREREQ;
				if (roomLeft == 1)
					curCmt = CANMAKE_OK;
				else if (roomLeft == 0)
					curCmt = CANMAKE_QUEUE_FULL;

				// ShigureUi 13/9/2026 find best location to insert then update best 5
				if (curCmt == CANMAKE_OK)
				{
					cmt = CANMAKE_OK;
					for (i = 0; i < upgradeToQueue; i++)
						if (minFinishTime[i] > curFinishTime)
						{
							for (j = upgradeToQueue - 1; j > i; j--)
							{
								minFinishTime[j] = minFinishTime[j - 1];
								bestFactories[j] = bestFactories[j - 1];
							}
							minFinishTime[i] = curFinishTime;
							bestFactories[i] = curFactory;
							break;
						}
				}
				else if (curCmt == CANMAKE_QUEUE_FULL && cmt == CANMAKE_NO_PREREQ)
					cmt = CANMAKE_QUEUE_FULL;

				for (i = 0; i < upgradeToQueue; i++)
					if (minFinishTime[i] != 0.0)
						break;
				if (i == upgradeToQueue)
					break;
			}

			if (cmt == CANMAKE_QUEUE_FULL)
			{
				TheInGameUI->message("GUI:ProductionQueueFull");
				break;
			}
			else if (cmt != CANMAKE_OK)
			{
				break;
			}

			GameMessage *msg;

			// ShigureUi 13/9/2026 Add new factory objectID argument for identify
			for (i = 0; i < upgradeToQueue && bestFactories[i]; i++)
			{
				// send the message
				msg = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UPGRADE);
				msg->appendObjectIDArgument(bestFactories[i]->getID());
				msg->appendIntegerArgument(upgradeT->getUpgradeNameKey());

				// ShigureUi 20/09/2026 Send it to cache
				BuildQueueCacheNode bqcn;
				bqcn.m_type = PRODUCTION_UPGRADE;
				bqcn.m_upgradeToResearch = upgradeT;
				m_multiSelectQueueCache.insert(std::make_pair(bestFactories[i]->getID(), bqcn));
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_CANCEL_UPGRADE:
		{
			Int i;

			// find out which index (i) in the queue represents the button clicked
			for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
				if( m_queueData[ i ].control == control )
					break;

			// sanity, control not found
			if( i == MAX_BUILD_QUEUE_BUTTONS )
			{

				DEBUG_CRASH( ("Control not found in build queue data") );
				break;

			}

			// sanity
			if( m_queueData[ i ].type != PRODUCTION_UPGRADE )
				break;

			// get the upgrade to cancel
			const UpgradeTemplate *upgradeT = m_queueData[ i ].upgradeToResearch;

			// sanity
			if (upgradeT == nullptr)
				break;

			ProductionUpdateInterface *pu;
			const ProductionEntry *pe;

			Object *curFactory = nullptr;

			// ShigureUi 13/9/2026 Find the production and get the type
			for (DrawableListCIt it = factories.begin(); it != factories.end(); it++)
			{
				Object *factory = (*it)->getObject();
				if (!factory || !factory->isLocallyControlled())
					break;
				pu = factory->getProductionUpdateInterface();
				if (m_queueData[i].producer == factory)
				{
					curFactory = factory;
					for (pe = pu->firstProduction(); pe; pe = pu->nextProduction(pe))
						if (pe->getProductionType() == PRODUCTION_UPGRADE && pe->getProductionUpgrade() == upgradeT)
							break;
					break;
				}
			}

			// sanity
			if (curFactory == nullptr)
				break;

			// A right click moves the entry one place earlier and never cancels.
			// ShigureUi 13/9/2026 only if there is only 1 producer
			if (gadgetMessage == GBM_SELECTED_RIGHT)
			{
				if (TheGlobalData->m_queueReorder && factories.size() == 1 && i > 0 && curFactory->isLocallyControlled() && !curFactory->isDisabled())
				{
					GameMessage *moveMsg = TheMessageStream->appendMessage(GameMessage::MSG_MOVE_UPGRADE_EARLIER);
					moveMsg->appendIntegerArgument(upgradeT->getUpgradeNameKey());
				}
				break;
			}

			// TheSuperHackers @feature Shift cancel all of this upgrade on selected units instead of that production.
			// ShigureUi 13/9/2026 need a argument to tell cancel all or the only one
			if (TheKeyboard && TheKeyboard->isShift())
			{
				// send the message
				GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CANCEL_UPGRADE);
				msg->appendBooleanArgument(true);
				msg->appendIntegerArgument(upgradeT->getUpgradeNameKey());

			}
			else
			{
				// send the message
				// ShigureUi 13/9/2026 the only one need an extra producer objectID argument
				GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CANCEL_UPGRADE);
				msg->appendBooleanArgument(false);
				msg->appendIntegerArgument(upgradeT->getUpgradeNameKey());
				msg->appendObjectIDArgument(curFactory->getID());
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_ATTACK_MOVE:
			TheMessageStream->appendMessage(GameMessage::MSG_META_TOGGLE_ATTACKMOVE);
			break;

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_STOP:
		{
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_DO_STOP);
			break;
		}

		case GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE:
		{
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			if( !localPlayer )
			{
				break;
			}

			const ThingTemplate *thing = commandButton->getThingTemplate();
			if( !thing )
			{
				break;
			}

			//deselect other units
			TheInGameUI->deselectAllDrawables();

			// create a new group.
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

			//New group or add to group? Passed in value is true if we are creating a new group.
			teamMsg->appendBooleanArgument( TRUE );

			//Iterate through the player's entire team and select each member that matches the template.
			SelectObjectsInfo info;
			info.thingTemplate = thing;
			info.msg = teamMsg;
			localPlayer->iterateObjects( selectObjectOfType, (void*)&info );

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_WAYPOINTS:
			break;

		//-------------------------------------------------------------------------------------------------
		case GUI_COMMAND_EXIT_CONTAINER:
		{
			Int i;
			// TheSuperHackers @fix Caball009/Mauller 23/05/2025 Fix uninitialized variable and control lookup behaviour to prevent a buffer-overflow when the control container is empty
			ObjectID objID = INVALID_ID;

			//
			// find the object ID that wants to exit by scanning through the transport data and looking
			// for the matching control button
			//
			for (i = 0; i < MAX_COMMANDS_PER_SET; i++)
			{
				if (m_containData[i].control == control)
				{
					objID = m_containData[ i ].objectID;
					break;
				}
			}

			if (objID == INVALID_ID)
				break;

			// get the actual object
			Object *objWantingExit = TheGameLogic->findObjectByID( objID );

			// if the control container returns an object ID but the object is not found, remove the control entry and exit
			if( objWantingExit == nullptr )
			{

				//
				// remove from inventory data to avoid future matches ... the inventory update
				// cycle of the UI will repopulate any buttons as the contents of objects
				// change so this is only an edge case that will be visually corrected next frame
				//
				m_containData[ i ].control = nullptr;
				m_containData[ i ].objectID = INVALID_ID;
				break;  // exit case

			}

      //what if container is subdued... assert a logic failure, perhaps?

			// send message to exit
			GameMessage *exitMsg = TheMessageStream->appendMessage( GameMessage::MSG_EXIT );
			exitMsg->appendObjectIDArgument( objWantingExit->getID() ); // 0 is the thing inside coming out
			exitMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			// contraZH: shift click exits every passenger of the clicked unit's type
			if (TheKeyboard && TheKeyboard->isShift())
			{
				const ThingTemplate *exitType = objWantingExit->getTemplate();
				for (i = 0; i < MAX_COMMANDS_PER_SET; i++)
				{
					if (m_containData[i].control == control || m_containData[i].objectID == INVALID_ID)
					{
						continue;
					}

					Object *other = TheGameLogic->findObjectByID( m_containData[i].objectID );

					// if the control container returns an object ID but the object is not found, remove the control entry and exit
					if (other == nullptr)
					{

						//
						// remove from inventory data to avoid future matches ... the inventory update
						// cycle of the UI will repopulate any buttons as the contents of objects
						// change so this is only an edge case that will be visually corrected next frame
						//
						m_containData[i].control = nullptr;
						m_containData[i].objectID = INVALID_ID;
						continue;  // exit case

					}

					if (!other->getTemplate()->isEquivalentTo(exitType))
					{
						continue;
					}

					exitMsg = TheMessageStream->appendMessage( GameMessage::MSG_EXIT );
					exitMsg->appendObjectIDArgument( other->getID() );
					exitMsg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
				}
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_EVACUATE:
		{
			// Cancel GUI command mode.
			TheInGameUI->setGUICommand( nullptr );

			if (BitIsSet(commandButton->getOptions(), NEED_TARGET_POS) == FALSE) {
				pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_EVACUATE );
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_EVACUATE );
				msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_EVACUATE_TO_WORK:
		{
			TheInGameUI->setGUICommand( nullptr );
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_EVACUATE );
			TheMessageStream->appendMessage( GameMessage::MSG_EVACUATE_TO_WORK );
			break;
		}

		// --------------------------------------------------------------------------------------------
		// TheSuperHackers @feature Fill the selected containers from nearby idle infantry. The UI only
		// appends the message; the logic side decides who actually boards, so peers stay in sync.
		case GUI_COMMAND_AUTO_FILL:
		{
			TheInGameUI->setGUICommand( nullptr );
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_AUTO_FILL );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
		{
			TheMessageStream->appendMessage( GameMessage::MSG_EXECUTE_RAILED_TRANSPORT );
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GUI_COMMAND_HACK_INTERNET:
		{
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_INTERNET_HACK );
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_INTERNET_HACK );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_SET_RALLY_POINT:
		{
			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_SELL:
		{

			// command needs no additional data, send the message
			TheMessageStream->appendMessage( GameMessage::MSG_SELL );
			break;

		}

		// --------------------------------------------------------------------------------------------
		case GUI_COMMAND_TOGGLE_OVERCHARGE:
		{

			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TOGGLE_OVERCHARGE );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;

		}

		// TheSuperHackers @feature Hold Fire stance. Only send the message here -- the stance itself
		// is flipped on the logic side, or clients would desync.
		// --------------------------------------------------------------------------------------------
		case GUI_COMMAND_HOLD_FIRE:
		{

			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TOGGLE_HOLD_FIRE );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;

		}

		// Deploy button. As with Hold Fire, only send the message -- the deploy itself happens on the
		// logic side, or clients would desync.
		// --------------------------------------------------------------------------------------------
		case GUI_COMMAND_TOGGLE_DEPLOY:
		{

			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TOGGLE_DEPLOY );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;

		}

		//Same as above
		case GUI_COMMAND_TOGGLE_TUNNEL_AUTO_POP:
		{

			TheMessageStream->appendMessage(GameMessage::MSG_TOGGLE_TUNNEL_AUTO_POP);
			break;

		}

#ifdef ALLOW_SURRENDER
		// ------------------------------------------------------------------------------------------------
		case GUI_COMMAND_POW_RETURN_TO_PRISON:
		{

			TheMessageStream->appendMessage( GameMessage::MSG_RETURN_TO_PRISON );
			break;

		}
#endif

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_BEACON_DELETE:
		{

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
		case GUI_COMMAND_COMBATDROP:
		{
			DEBUG_CRASH(("hmm, should never occur"));
		}
		break;

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_SWITCH_WEAPON:
		{
				// command needs no additional data, send the message
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SWITCH_WEAPONS );

				//Play mode change acknowledgement
				PickAndPlayInfo info;
				WeaponSlotType slot = commandButton->getWeaponSlot();
				info.m_weaponSlot = &slot;
				pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_SWITCH_WEAPONS, &info );

				msg->appendIntegerArgument( commandButton->getWeaponSlot() );
				msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
				break;
		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_FIRE_WEAPON:
		{
			// command needs no additional data, send the message
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_WEAPON );
			msg->appendIntegerArgument( commandButton->getWeaponSlot() );
			msg->appendIntegerArgument( commandButton->getMaxShotsToFire() );
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_TOGGLE_FIRE_WEAPON:
		{
			// Only send the intent -- start or stop is decided on the logic side, or clients desync.
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TOGGLE_FIRE_WEAPON );
			msg->appendIntegerArgument( commandButton->getWeaponSlot() );
			msg->appendIntegerArgument( commandButton->getMaxShotsToFire() );
			msg->appendIntegerArgument(TheInGameUI->isInWaypointMode());

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
		{
			const SpecialPowerTemplate *spTemplate = commandButton->getSpecialPowerTemplate();
			SpecialPowerType spType = spTemplate->getSpecialPowerType();

			Object* obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( spType );
			if( !obj )
				break;

			// command needs no additional data, send the message
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SPECIAL_POWER );
			msg->appendIntegerArgument( spTemplate->getID() );
			msg->appendIntegerArgument( commandButton->getOptions() );
			msg->appendObjectIDArgument( obj->getID() );
			msg->appendBooleanArgument(false);
			break;

		}

		case GUI_COMMAND_SPECIAL_POWER:
		{
			// command needs no additional data, send the message
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SPECIAL_POWER );
			msg->appendIntegerArgument( commandButton->getSpecialPowerTemplate()->getID() );
			msg->appendIntegerArgument( commandButton->getOptions() );
			msg->appendObjectIDArgument( INVALID_ID );	// no specific source
			msg->appendBooleanArgument(TheInGameUI->isInWaypointMode());
			break;

		}

		//---------------------------------------------------------------------------------------------
		case GUI_COMMAND_PURCHASE_SCIENCE:
		{

			// loop through all the sciences on the button and select the one we don't have

			ScienceType	st = SCIENCE_INVALID;
			Player *player = ThePlayerList->getLocalPlayer();
			for(size_t i = 0; i < commandButton->getScienceVec().size(); ++i)
			{
				st = commandButton->getScienceVec()[ i ];
				if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
				{
					break;
				}
			}

			if( st == SCIENCE_INVALID)
			{
				switchToContext( CB_CONTEXT_NONE, nullptr );
				break;
			}


			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_PURCHASE_SCIENCE );
			msg->appendIntegerArgument( st );

			markUIDirty();

			break;

		}

		//---------------------------------------------------------------------------------------------
		default:

			DEBUG_CRASH( ("Unknown command '%d'", commandButton->getCommandType()) );
			return CBC_COMMAND_NOT_USED;

	}

	return CBC_COMMAND_USED;

}

//-------------------------------------------------------------------------------------------------
// ShigureUi 19/09/2026 remove unit from cache message is being processed
//-------------------------------------------------------------------------------------------------
void ControlBar::removeUnitFromBuildQueueCache(ObjectID objID, ProductionID productionID)
{
	//ShigureUi 20/09/2026 find this object's cache
	std::multimap<ObjectID, BuildQueueCacheNode>::iterator it;
	std::pair<std::multimap<ObjectID, BuildQueueCacheNode>::iterator, std::multimap<ObjectID, BuildQueueCacheNode>::iterator> pr;
	pr = m_multiSelectQueueCache.equal_range(objID);

	for (it = pr.first; it != pr.second; it++)
	{
		const BuildQueueCacheNode& bqcn = (*it).second;
		if (bqcn.m_type == PRODUCTION_UNIT && bqcn.m_productionID == productionID)
		{
			m_multiSelectQueueCache.erase(it);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// ShigureUi 19/09/2026 remove upgrade from cache message is being processed
//-------------------------------------------------------------------------------------------------
void ControlBar::removeUpgradeFromBuildQueueCache(ObjectID objID, const UpgradeTemplate *upgrade)
{
	//ShigureUi 20/09/2026 find this object's cache
	std::multimap<ObjectID, BuildQueueCacheNode>::iterator it;
	std::pair<std::multimap<ObjectID, BuildQueueCacheNode>::iterator, std::multimap<ObjectID, BuildQueueCacheNode>::iterator> pr;
	pr = m_multiSelectQueueCache.equal_range(objID);

	for (it = pr.first; it != pr.second; it++)
	{
		const BuildQueueCacheNode& bqcn = (*it).second;
		if (bqcn.m_type == PRODUCTION_UPGRADE && bqcn.m_upgradeToResearch == upgrade)
		{
			m_multiSelectQueueCache.erase(it);
			break;
		}
	}
}
