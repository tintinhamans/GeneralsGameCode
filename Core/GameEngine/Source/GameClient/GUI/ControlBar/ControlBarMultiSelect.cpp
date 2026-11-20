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

// FILE: ControlBarMultiSelect.cpp ////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context-sensitive GUI for when you select multiple objects.  What we do is show
//				 the commands that you can use between them all
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ThingTemplate.h"
#include "Common/OptionPreferences.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"



//-------------------------------------------------------------------------------------------------
/** Reset the common command data */
//-------------------------------------------------------------------------------------------------
void ControlBar::resetCommonCommandData()
{
	Int i;

	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		m_commonCommands[ i ] = nullptr;
		//Clear out any remnant overlays.
		GadgetButtonDrawOverlayImage( m_commandWindows[ i ], nullptr );
	}

}

//-------------------------------------------------------------------------------------------------
/** add the common commands of this drawable to the common command set */
//-------------------------------------------------------------------------------------------------
void ControlBar::addCommonCommands( Drawable *draw, Bool firstDrawable )
{
	Int i;
	const CommandButton *command;

	// sanity
	if( draw == nullptr )
		return;

	Object* obj = draw->getObject();
	if (!obj)
		return;

	if (obj->isKindOf(KINDOF_IGNORED_IN_GUI)) // ignore these guys
		return;

	// get the command set of this drawable
	const CommandSet *commandSet = findCommandSet( obj->getCommandSetString() );
	if( commandSet == nullptr )
	{

		//
		// if there is no command set for this drawable, none of the selected drawables
		// can possibly have matching commands so we'll get rid of them all
		//
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{

			m_commonCommands[ i ] = nullptr;
			if (m_commandWindows[ i ])
			{
				m_commandWindows[ i ]->winHide( TRUE );
			}
			// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
	// removed from multiplayer branch
			//showCommandMarkers();

		}

		return;

	}


	//
	// easy case, if we're adding the first drawable we simply just add any of the commands
	// in its set that can be multi-select commands to the common command set
	//
	if( firstDrawable == TRUE )
	{

		// just add each command that is classified as a common command
		for (i = 0; i < MAX_COMMANDS_PER_SET; i++)
		{
			// our implementation doesn't necessarily make use of the max possible command buttons
			if (!m_commandWindows[i]) continue;

			// get command
			command = commandSet->getCommandButton(i);

			//sanity
			if (!command)
				continue;

			// Script only command -- don't show it in the UI.
			// ShigureUi 07/09/2026 Since no production command button was allowed when multiselect, we need a check here.
			if (BitIsSet(command->getOptions(), SCRIPT_ONLY))
			{
				m_commandWindows[i]->winHide(TRUE);
				continue;
			}

			// add if present and can be used in a multi select
			if ((BitIsSet(command->getOptions(), OK_FOR_MULTI_SELECT) == TRUE ||
					// ShigureUi 07/09/2026 Allows unit build and upgrade command button OK_FOR_MULTI_SELECT by default.
					command->getCommandType() == GUI_COMMAND_UNIT_BUILD ||
					command->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
					command->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE
				))
			{

				// put it in the common command set
				m_commonCommands[ i ] = command;

				// show and enable this control
				m_commandWindows[ i ]->winHide( FALSE );
				m_commandWindows[ i ]->winEnable( TRUE );

				// set the command into the control
				setControlCommand( m_commandWindows[ i ], command );

			}

		}

	}
	else
	{

		// go through each command one by one
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{

			// our implementation doesn't necessarily make use of the max possible command buttons
			if (! m_commandWindows[ i ]) continue;

			// get the command
			command = commandSet->getCommandButton(i);

			Bool attackMove = (command && command->getCommandType() == GUI_COMMAND_ATTACK_MOVE) ||
												(m_commonCommands[ i ] && m_commonCommands[ i ]->getCommandType() == GUI_COMMAND_ATTACK_MOVE);

			// Kris: When any units have attack move, they all get it. This is to allow
			// combat units to be selected with the odd dozer or pilot and still retain that ability.
			if( attackMove && !m_commonCommands[ i ] )
			{
				// put it in the common command set
				m_commonCommands[ i ] = command;

				// show and enable this control
				m_commandWindows[ i ]->winHide( FALSE );
				m_commandWindows[ i ]->winEnable( TRUE );

				// set the command into the control
				setControlCommand( m_commandWindows[ i ], command );
			}
			else if( command != m_commonCommands[ i ] && !attackMove )
			{
				//
				// if this command does not match the command that is in the common command set then
				// *neither* this command OR the command in the common command set are really common
				// commands, so we will remove the one that has been stored in the common set
				//

				// remove the common command
				m_commonCommands[ i ] = nullptr;

				//
				// hide the window control cause it should have been made visible from a command
				// that was placed in this common 'slot' earlier
				//
				m_commandWindows[ i ]->winHide( TRUE );
			}

		}

	}

	// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
	// removed from multiplayer branch
	//showCommandMarkers();

}


//-------------------------------------------------------------------------------------------------
/** Populate the visible command bar with commands that are common to all the objects
	* that are selected in the UI */

//-------------------------------------------------------------------------------------------------
void ControlBar::populateMultiSelect()
{
	Int i;
	Drawable *draw;
	Bool firstDrawable = TRUE;
	Bool portraitSet = FALSE;
	const Image *portrait = nullptr;
	Object *portraitObj = nullptr;

	// first reset the common command data
	resetCommonCommandData();

	// by default, hide all the controls in the command section
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		if (m_commandWindows[ i ])
		{
			m_commandWindows[ i ]->winHide( TRUE );
		}
	}

	// sanity
	DEBUG_ASSERTCRASH( TheInGameUI->getSelectCount() > 1,
										 ("populateMultiSelect: Can't populate multiselect context cause there are only '%d' things selected",
										  TheInGameUI->getSelectCount()) );

	// get the list of drawable IDs from the in game UI
	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();

	// sanity
	DEBUG_ASSERTCRASH( selectedDrawables->empty() == FALSE, ("populateMultiSelect: Drawable list is empty") );

	ObjectVector producerList;
	Bool anyOngoingProduction = false, everyHasPU = true;

	// loop through all the selected drawables
	for( DrawableListCIt it = selectedDrawables->begin();
			 it != selectedDrawables->end(); ++it )
	{

		// get the drawable
		draw = *it;


		if (draw->getObject()->isKindOf(KINDOF_IGNORED_IN_GUI)) // ignore these guys
			continue;

		// TheSuperHackers @feature With a type focused in the smart selection row, only that type
		// contributes, so the bar shows its command set rather than the group's common subset.
		if( !isSmartSelectionFocused( draw->getObject() ) )
		{
			continue;
		}

		//if (draw->getObject() && draw->getObject()->getProductionUpdateInterface())

		//
		// add command for this drawable, note that we also sanity check to make sure the
		// drawable has an object as all interesting drawables that we can select should
		// actually have an object underneath it so that we can do interesting things with
		// it ... otherwise we should have never selected it.
		// NOTE that we're not considering objects that are currently in the process of
		// being sold as those objects can't be issued anymore commands
		//
		if( draw && draw->getObject() &&
				!draw->getObject()->getStatusBits().test( OBJECT_STATUS_SOLD ) )
		{

			// add the common commands of this drawable to the common command set
			addCommonCommands( draw, firstDrawable );

			// ShigureUi 07/09/2026 find producer drawables, we need all of them to be producer.
			ProductionUpdateInterface *pu = draw->getObject()->getProductionUpdateInterface();
			if (pu)
			{
				if (pu->firstProduction())
					anyOngoingProduction = true;
				producerList.push_back(draw->getObject());
			}
			else
				everyHasPU = false;


			// not adding the first drawable anymore
			firstDrawable = FALSE;

			//
			// keep track of the portrait images, if all units selected have the same portrait
			// we will display it in the right HUD, otherwise we won't
			//
			if( portraitSet == FALSE )
			{

				portrait = draw->getTemplate()->getSelectedPortraitImage();
				portraitObj = draw->getObject();
				portraitSet = TRUE;

			}
			else if( draw->getTemplate()->getSelectedPortraitImage() != portrait )
				portrait = nullptr;

		}

		// ShigureUi 13/9/2026 Shows Rally point when multiselect
		ExitInterface* exit = draw->getObject()->getObjectExitInterface();
		if (exit)
		{

			//
			// if a rally point is set, show the rally point, if we don't have it set hide any rally
			// point we might have visible
			//
			showRallyPoint(exit->getRallyPoint());

		}

	}

	// ShigureUi 07/09/2026 check for common buildable production
	for (i = 0; i < MAX_COMMANDS_PER_SET; i++)
	{
		if (m_commonCommands[i] &&
			(m_commonCommands[i]->getCommandType() == GUI_COMMAND_UNIT_BUILD ||
			m_commonCommands[i]->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
			m_commonCommands[i]->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE))
			break;
	}

	if (i == MAX_COMMANDS_PER_SET || !anyOngoingProduction || !everyHasPU)
	// set the portrait image
		setPortraitByObject( portraitObj );
	else
	{
		// ShigureUi 13/9/2026 either portrait or build queue
		populateMultiSelectBuildQueue(&producerList);
	}

}


// ShigureUi 08/09/2026 copied from populateBuildQueue() and modified
void ControlBar::populateMultiSelectBuildQueue(ObjectVector *producerList)
{
	/// @todo srj -- remove hard-coding here, please
	static const CommandButton* cancelUnitCommand = findCommandButton("Command_CancelUnitCreate");
	/// @todo srj -- remove hard-coding here, please
	static const CommandButton* cancelUpgradeCommand = findCommandButton("Command_CancelUpgradeCreate");
	static NameKeyType buildQueueIDs[MAX_BUILD_QUEUE_BUTTONS];
	static Bool idsInitialized = FALSE;
	Int i;

	// reset the build queue data
	resetBuildQueueData();

	// get name key ids for the build queue buttons
	if (idsInitialized == FALSE)
	{
		AsciiString buttonName;

		for (i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++)
		{

			buttonName.format("ControlBar.wnd:ButtonQueue%02d", i + 1);
			buildQueueIDs[i] = TheNameKeyGenerator->nameToKey(buttonName);

		}

		idsInitialized = TRUE;

	}

	// get window pointers to all the buttons for the build queue
	for (i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++)
	{

		// get window commented out cause I believe we already set this.  We'll see in a few minutes
		m_queueData[i].control = TheWindowManager->winGetWindowFromId(m_contextParent[CP_BUILD_QUEUE],
			buildQueueIDs[i]);

		// disable window by default
		m_queueData[i].control->winEnable(FALSE);

		//Clear the status because this button doesn't use it -- and if it's set, it'll
		//become invisible meaning the image that was there will be showed.
		m_queueData[i].control->winClearStatus(WIN_STATUS_USE_OVERLAY_STATES);

		// an empty slot takes no right click
		m_queueData[i].control->winClearStatus(WIN_STATUS_RIGHT_CLICK_ON_PRESS);

		// set the text of the window to nothing by default
		GadgetButtonSetText(m_queueData[i].control, L"");

		//Clear any potential veterancy rank, or else we'll see it when it's empty!
		GadgetButtonDrawOverlayImage(m_queueData[i].control, nullptr);

	}

	ProductionUpdateInterface *pu;

	std::vector<ProductionUpdateInterface*> allPU;
	std::vector<const ProductionEntry*> productionPointer;
	std::vector<Real> currentBuildTime;
	const ProductionEntry* pe;
	Player* thePlayer = nullptr;
	int producerCount = producerList->size();

	allPU.resize(producerCount);
	productionPointer.resize(producerCount);
	currentBuildTime.resize(producerCount);

	m_displayedQueueSignature.clear();
	m_displayedQueueSignature.reserve(producerCount);

	// ShigureUi 13/9/2026 calculate all first production's finish time
	for (i = 0; i < producerCount; i++)
	{
		pu = (*producerList)[i]->getProductionUpdateInterface();
		if (!pu)
		{
			// a partial signature could match a later partial pass and suppress a real repaint
			m_displayedQueueSignature.clear();
			return;  // sanity
		}

		if (!thePlayer)
			thePlayer = (*producerList)[i]->getControllingPlayer();
		allPU[i] = pu;
		productionPointer[i] = pu->firstProduction();

		QueueSignature signature;
		signature.producer = (*producerList)[i];
		signature.head = productionPointer[i] ? productionPointer[i]->getProductionID() : PRODUCTIONID_INVALID;
		signature.count = pu->getProductionCount();
		signature.isDisabled = (*producerList)[i]->isDisabled();
		m_displayedQueueSignature.push_back(signature);
		currentBuildTime[i] = 1e9;

		if (productionPointer[i])
		{
			pe = productionPointer[i];
			Int totalFrames = 0;

			if (pe->getProductionType() == PRODUCTION_UNIT)
			{
				if (pe->getProductionObject())
					totalFrames = pe->getProductionObject()->calcTimeToBuild(thePlayer);
			}
			else if (pe->getProductionUpgrade())
			{
				totalFrames = pe->getProductionUpgrade()->calcTimeToBuild(thePlayer);
			}

			currentBuildTime[i] = (100.0f - pe->getPercentComplete()) / 100.f * totalFrames;

			if ((*producerList)[i]->isDisabled())
				currentBuildTime[i] += 1e7;
		}
	}

	Int windowIndex = 0;
	const Image* image;
	Real minFinishTime;
	Int minTimeOwner;

	// ShigureUi 13/9/2026 find nine least estimated finished time production
	while (windowIndex < MAX_BUILD_QUEUE_BUTTONS)
	{

		minFinishTime = 1e8;
		minTimeOwner = -1;

		for (i = 0; i < producerCount; i++)
		{
			if (currentBuildTime[i] < minFinishTime)
			{
				minFinishTime = currentBuildTime[i];
				minTimeOwner = i;
			}
		}

		if (minTimeOwner == -1)
			break;

		// ShigureUi 08/09/2026 draw the button which gonna be newest
		pe = productionPointer[minTimeOwner];

		// set the command into the queue button
		if (pe->getProductionType() == PRODUCTION_UNIT)
		{

			// set the control command
			setControlCommand(m_queueData[windowIndex].control, cancelUnitCommand);
			m_queueData[windowIndex].type = PRODUCTION_UNIT;
			m_queueData[windowIndex].productionID = pe->getProductionID();

			// set the images
			m_queueData[windowIndex].control->winEnable(TRUE);
			m_queueData[windowIndex].control->winSetStatus(WIN_STATUS_USE_OVERLAY_STATES);
			image = pe->getProductionObject()->getButtonImage();
			GadgetButtonSetEnabledImage(m_queueData[windowIndex].control, image);

			//No longer used.
			//image = TheMappedImageCollection->findImageByName( production->getProductionObject()->getInventoryImageName( INV_IMAGE_HILITE ) );
			//GadgetButtonSetHiliteSelectedImage( m_queueData[ windowIndex ].control, image );
			//image = TheMappedImageCollection->findImageByName( production->getProductionObject()->getInventoryImageName( INV_IMAGE_PUSHED ) );
			//GadgetButtonSetHiliteImage( m_queueData[ windowIndex ].control, image );

			//Show the veterancy rank of the object being constructed in the queue
			const Image* image = calculateVeterancyOverlayForThing(pe->getProductionObject());
			GadgetButtonDrawOverlayImage(m_queueData[windowIndex].control, image);
			//
			// note we're not setting a disabled image into the queue button ... when there is
			// nothing in the queue we set the button to disabled, we want to leave the disabled
			// queue button graphic we already have in place
			//
	//		image = TheMappedImageCollection->findImageByName( production->getProductionObject()->getInventoryImageName( INV_IMAGE_DISABLED ) );
	//		GadgetButtonSetDisabledImage( m_queueData[ windowIndex ].control, image );

		}
		else
		{
			const UpgradeTemplate* ut = pe->getProductionUpgrade();

			// set the control command
			setControlCommand(m_queueData[windowIndex].control, cancelUpgradeCommand);
			m_queueData[windowIndex].type = PRODUCTION_UPGRADE;
			m_queueData[windowIndex].upgradeToResearch = pe->getProductionUpgrade();

			// set the images
			m_queueData[windowIndex].control->winEnable(TRUE);
			m_queueData[windowIndex].control->winSetStatus(WIN_STATUS_USE_OVERLAY_STATES);
			image = ut->getButtonImage();
			GadgetButtonSetEnabledImage(m_queueData[windowIndex].control, image);

			//No longer used
			//image = TheMappedImageCollection->findImageByName( ut->getQueueImageName( UpgradeTemplate::UPGRADE_HILITE ) );
			//GadgetButtonSetHiliteSelectedImage( m_queueData[ windowIndex ].control, image );
			//image = TheMappedImageCollection->findImageByName( ut->getQueueImageName( UpgradeTemplate::UPGRADE_PUSHED ) );
			//GadgetButtonSetHiliteImage( m_queueData[ windowIndex ].control, image );
			//
			// note we're not setting a disabled image into the queue button ... when there is
			// nothing in the queue we set the button to disabled, we want to leave the disabled
			// queue button graphic we already have in place
			//
	//		image = TheMappedImageCollection->findImageByName( ut->getQueueImageName( UpgradeTemplate::UPGRADE_DISABLED ) );
	//		GadgetButtonSetDisabledImage( m_queueData[ windowIndex ].control, image );

		}

		m_queueData[windowIndex].producer = (*producerList)[minTimeOwner];

		productionPointer[minTimeOwner] = allPU[minTimeOwner]->nextProduction(productionPointer[minTimeOwner]);

		// ShigureUi 13/9/2026 if no production set it to 1e9, otherwise calculate next production time
		if (!productionPointer[minTimeOwner])
			currentBuildTime[minTimeOwner] = 1e9;
		else
		{
			pe = productionPointer[minTimeOwner];
			if (pe->getProductionType() == PRODUCTION_UNIT)
			{
				if (pe->getProductionObject())
					currentBuildTime[minTimeOwner] += pe->getProductionObject()->calcTimeToBuild(thePlayer);
			}
			else if (pe->getProductionUpgrade())
			{
				currentBuildTime[minTimeOwner] += pe->getProductionUpgrade()->calcTimeToBuild(thePlayer);
			}
		}

		// we have filled up this window now
		windowIndex++;

	}

	//
	// save the count of things being produced in the build queue, when it changes we will
	// repopulate the queue to visually show the change
	// no longer used for multiselect
	// m_displayedQueueCount = 0;

}

//-------------------------------------------------------------------------------------------------
/** Update logic for the multi select context sensitive GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextMultiSelect()
{
	Drawable *draw;
	Object *obj;
	const CommandButton *command;
	GameWindow *win;
	Int objectsThatCanDoCommand[ MAX_COMMANDS_PER_SET ];
	Int i;

	// zero the array that counts how many objects can do each command
	memset( objectsThatCanDoCommand, 0, sizeof( objectsThatCanDoCommand ) );

	// sanity
	DEBUG_ASSERTCRASH( TheInGameUI->getSelectCount() > 1,
										 ("updateContextMultiSelect: TheInGameUI only has '%d' things selected",
										  TheInGameUI->getSelectCount()) );

	// get the list of drawable IDs from the in game UI
	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();
	Bool anyProductionExist = false, everyHasPU = true;
	ProductionUpdateInterface* pu;
	ObjectVector producerList;
	int producerCount = 0;

	// sanity
	DEBUG_ASSERTCRASH( selectedDrawables->empty() == FALSE, ("populateMultiSelect: Drawable list is empty") );

	// loop through all the selected drawable IDs
	for( DrawableListCIt it = selectedDrawables->begin();
			 it != selectedDrawables->end(); ++it )
	{

		// get the drawable from the ID
		draw = *it;

		if (draw->getObject()->isKindOf(KINDOF_IGNORED_IN_GUI)) // ignore these guys
			continue;

		// get the object
		obj = draw->getObject();

		// sanity
		if (obj == nullptr)
			continue;

		// TheSuperHackers @feature Only the focused type judges availability, or another type
		// that cannot do a command would hide or grey it out.
		if( !isSmartSelectionFocused( obj ) )
		{
			continue;
		}

		//ShigureUi 07/09/2026 check if there is any production exists in order to populate build queue
		pu = obj->getProductionUpdateInterface();

		if (pu)
		{
			producerList.push_back(obj);
			producerCount++;
			if (pu->firstProduction())
				anyProductionExist = true;
		}
		else
			everyHasPU = false;

		// for each of the visible command windows make sure the object can execute the command
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{

			// get the control window
			win = m_commandWindows[ i ];

			// our implementation doesn't necessarily make use of the max possible command buttons
			if (!win) continue;

			// don't consider hidden windows
			if( win->winIsHidden() == TRUE )
				continue;

			// get the command
			command = (const CommandButton *)GadgetButtonGetData(win);
			if( command == nullptr )
				continue;

			// can we do the command
			CommandAvailability availability = getCommandAvailability( command, obj, win );

			win->winClearStatus( WIN_STATUS_NOT_READY );
			win->winClearStatus( WIN_STATUS_ALWAYS_COLOR );

			// enable/disable the window control
			switch( availability )
			{
				case COMMAND_HIDDEN:
					win->winHide( TRUE );
					break;
				case COMMAND_RESTRICTED:
					win->winEnable( FALSE );
					break;
				case COMMAND_NOT_READY:
					win->winEnable( FALSE );
					win->winSetStatus( WIN_STATUS_NOT_READY );
					break;
				case COMMAND_CANT_AFFORD:
					win->winEnable( FALSE );
					win->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
					break;
				default:
					win->winEnable( TRUE );
					break;
			}

			//If button is a CHECK_LIKE, then update it's status now.
			if( BitIsSet( command->getOptions(), CHECK_LIKE ) )
			{
				GadgetCheckLikeButtonSetVisualCheck( win, availability == COMMAND_ACTIVE );
			}

			if( availability == COMMAND_AVAILABLE || availability == COMMAND_ACTIVE )
					objectsThatCanDoCommand[ i ]++;

		}

	}

	//
	// for each command, if any objects can do the command we enable the window, otherwise
	// we disable it
	//

	bool canShareBuildQueue = false;

	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		// our implementation doesn't necessarily make use of the max possible command buttons
		if (! m_commandWindows[ i ]) continue;

		// don't consider hidden commands
		if( m_commandWindows[ i ]->winIsHidden() == TRUE )
			continue;

		// don't consider slots that don't have commands
		if( m_commonCommands[ i ] == nullptr )
			continue;

		// check the count of objects that can do the command and enable/disable the control,
		if( objectsThatCanDoCommand[ i ] > 0 )
			m_commandWindows[ i ]->winEnable( TRUE );
		else
			m_commandWindows[ i ]->winEnable( FALSE );

		//Determine by the production type of this button, whether or not the created object
		//will have a veterancy rank
		if (m_commonCommands[ i ]->getCommandType() != GUI_COMMAND_EXIT_CONTAINER)
		{
			//Already handled for contained members -- see ControlBar::populateButtonProc()
			const Image* image = calculateVeterancyOverlayForThing(m_commonCommands[ i ]->getThingTemplate());
			GadgetButtonDrawOverlayImage(m_commandWindows[ i ], image);
		}

#if RTS_ZEROHOUR
		if (m_commonCommands[ i ]->getCommandType() == GUI_COMMAND_UNIT_BUILD)
		{
			for (size_t producer = 0; producer < producerList.size(); producer++)
			{
				if (producerList[ producer ]->getProductionUpdateInterface()->isAutoQueueUnit(m_commonCommands[ i ]->getThingTemplate()))
				{
					GadgetButtonDrawAutoQueue(m_commandWindows[ i ]);
					break;
				}
			}
		}
#endif


		// ShigureUi 07/09/2026 check if there is any command button is build/upgrade and available to all.
		if (everyHasPU &&
			  (m_commonCommands[ i ]->getCommandType() == GUI_COMMAND_UNIT_BUILD ||
				m_commonCommands[ i ]->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
				m_commonCommands[ i ]->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE
				))
			canShareBuildQueue = true;
	}

	//ShigureUi 07/09/2026 copied from updateContextCommand() and modified

	if (m_contextParent[CP_BUILD_QUEUE]->winIsHidden() == TRUE)
	{

		if (anyProductionExist && canShareBuildQueue)
		{

			// don't show the portrait image
			setPortraitByObject(nullptr);

			// show the build queue
			m_contextParent[CP_BUILD_QUEUE]->winHide(FALSE);
			populateMultiSelectBuildQueue(&producerList);

		}

	}
	else
	{

		if (!anyProductionExist || !canShareBuildQueue)
		{

			// hide the build queue
			m_contextParent[CP_BUILD_QUEUE]->winHide(TRUE);

			// show the portrait image
			setPortraitByObject(obj);

		}

	}

	// update a visible production queue
	if (m_contextParent[CP_BUILD_QUEUE]->winIsHidden() == FALSE)
	{

		// when the build queue is enabled, the selected portrait cannot be shown
		setPortraitByObject(nullptr);

		//
		// when showing a production queue, when the production count changes of the producer
		// object (the thing we have selected for the control bar) we will repopulate the
		// windows to visually show the new production linup
		//

		if (anyProductionExist)
		{
			// Compare each producer's head and count, not their sum: an entry gained at one
			// producer and lost at another leaves the sum alone and the queue unrepainted.
			Bool queueChanged = (Int)m_displayedQueueSignature.size() != producerCount;

			for (i = 0; !queueChanged && i < producerCount; i++)
			{
				pu = producerList[i]->getProductionUpdateInterface();
				if (!pu)
				{
					// the populate pass bails on this too, so its signature can never match
					queueChanged = TRUE;
					break;
				}

				const ProductionEntry *head = pu->firstProduction();
				QueueSignature signature;
				signature.producer = producerList[i];
				signature.head = head ? head->getProductionID() : PRODUCTIONID_INVALID;
				signature.count = pu->getProductionCount();
				signature.isDisabled = producerList[i]->isDisabled();
				queueChanged = signature != m_displayedQueueSignature[i];
			}

			// update the whole queue as necessary
			if (queueChanged)
				populateMultiSelectBuildQueue(&producerList);

			//
			// update the build percentage on the first thing (the thing that's being built)
			// in the queue
			//
			// ShigureUi 08/09/2026 for multiselect, check every build queue windows to see if they're any production facility's first production.

			const ProductionEntry* pe;

			for (i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++)
			{
				if (!m_queueData[i].control->winGetEnabled())
					break;

				if (!m_queueData[i].producer)
					break;

				pu = m_queueData[i].producer->getProductionUpdateInterface();

				if (!pu)
					break;
				pe = pu->firstProduction();

				// this producer's queue may have drained without changing the summed count
				if (!pe)
					continue;

				if (m_queueData[i].type == pe->getProductionType() &&
					((m_queueData[i].type == PRODUCTION_UNIT && m_queueData[i].productionID == pe->getProductionID()) ||
						(m_queueData[i].type == PRODUCTION_UPGRADE && m_queueData[i].upgradeToResearch == pe->getProductionUpgrade()))
					)
				{
					GameWindow* win = m_queueData[i].control;
					//				UnicodeString text;
					//
					//				text.format( L"%.0f%%", produce->getPercentComplete() );
					//				GadgetButtonSetText( win, text );

					GadgetButtonDrawInverseClock(win, pe->getPercentComplete(), m_buildUpClockColor);

					// TheSuperHackers @feature Remaining build time on the head queue slot.
					if (TheGlobalData->m_buildTimerDisplayMode != BuildTimerDisplayMode_None)
					{
						Int totalFrames = 0;
						if (pe->getProductionType() == PRODUCTION_UNIT)
						{
							if (pe->getProductionObject())
								totalFrames = pe->getProductionObject()->calcTimeToBuild(obj->getControllingPlayer());
						}
						else if (pe->getProductionUpgrade())
						{
							totalFrames = pe->getProductionUpgrade()->calcTimeToBuild(obj->getControllingPlayer());
						}

						if (totalFrames > 0)
						{
							Real remainingReal = totalFrames * (100.0f - pe->getPercentComplete()) / 100.0f;
							Int remainingFrames = (remainingReal > 0.0f) ? REAL_TO_INT_CEIL(remainingReal) : 0;
							// integer ceiling -- see formatBuildTimeForTooltip for why not the float form
							GadgetButtonDrawCountdown(win,
								(remainingFrames + LOGICFRAMES_PER_SECOND - 1) / LOGICFRAMES_PER_SECOND);
						}
					}
				}
			}
		}

  }

		// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
		// removed from multiplayer branch
		//showCommandMarkers();

}
