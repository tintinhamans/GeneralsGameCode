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

// TheSuperHackers @feature Build queue bar. One cameo per unit type or upgrade the local player
// has queued anywhere, with a count badge and the build clock, in a box above the radar. A click
// selects an object that has it queued, cycling through them on repeated clicks.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"

//-------------------------------------------------------------------------------------------------
/** The container owns the cameos, so their clicks land here. */
//-------------------------------------------------------------------------------------------------
static WindowMsgHandledType BuildQueueBarSystem( GameWindow *window, UnsignedInt msg,
																								 WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED )
	{
		return MSG_IGNORED;
	}
	TheControlBar->processBuildQueueClick( (GameWindow *)mData1 );
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
static void BuildQueueBarDraw( GameWindow *window, WinInstanceData *instData )
{
	TheControlBar->drawBuildQueueBar();
}

//-------------------------------------------------------------------------------------------------
static void collectProducer( Object *obj, void *userData )
{
	if( obj->isEffectivelyDead() || obj->getProductionUpdateInterface() == nullptr )
	{
		return;
	}
	((std::vector<Object *> *)userData)->push_back( obj );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::initBuildQueueBar()
{
	if( m_commandGroupButtonSize.x <= 0 || m_commandGroupButtonSize.y <= 0 )
	{
		return;
	}

	m_buildQueueParent = createCameoRow( BuildQueueBarSystem, MAX_BUILD_QUEUE_BAR_BUTTONS, FALSE, m_commandGroupButtonSize, m_buildQueueButtons );
	if( m_buildQueueParent == nullptr )
	{
		return;
	}

	// the box draws its frame itself and keeps clicks on it off the world
	m_buildQueueParent->winClearStatus( WIN_STATUS_SEE_THRU );
	m_buildQueueParent->winSetInputFunc( GameWinBlockInput );
	m_buildQueueParent->winSetDrawFunc( BuildQueueBarDraw );
	styleCameoCountBadges( m_buildQueueButtons, MAX_BUILD_QUEUE_BAR_BUTTONS );

	m_buildQueueRadarWindow = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarProBlockInputRadar" ) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::destroyBuildQueueBar()
{
	// destroyed windows are freed later, so they must not keep pointers to the slot images
	for( Int i = 0; i < MAX_BUILD_QUEUE_BAR_BUTTONS; i++ )
	{
		if( m_buildQueueButtons[ i ] )
		{
			GadgetButtonSetEnabledImage( m_buildQueueButtons[ i ], nullptr );
		}
	}
	if( m_buildQueueParent )
	{
		TheWindowManager->winDestroy( m_buildQueueParent );
	}
	m_buildQueueParent = nullptr;
	for( Int i = 0; i < MAX_BUILD_QUEUE_BAR_BUTTONS; i++ )
	{
		m_buildQueueButtons[ i ] = nullptr;
		deleteInstance( m_buildQueueImages[ i ] );
		m_buildQueueImages[ i ] = nullptr;
	}
	m_buildQueueRadarWindow = nullptr;
	m_buildQueueEntries.clear();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::resetBuildQueueBar()
{
	m_buildQueueEntries.clear();
	// no logic frame matches, so the next update rebuilds at once
	m_buildQueueFrame = ~0u;
	if( m_buildQueueParent && !m_buildQueueParent->winIsHidden() )
	{
		m_buildQueueParent->winHide( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
/** Every frame, since queues change without marking the UI dirty. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateBuildQueueBar()
{
	if( m_buildQueueParent == nullptr )
	{
		return;
	}

	GameWindow *master = m_contextParent[ CP_MASTER ];
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( master == nullptr || master->winIsHidden() || player == nullptr || m_isObserverCommandBar ||
			!TheGlobalData->m_buildQueueBar )
	{
		resetBuildQueueBar();
		return;
	}

	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame != m_buildQueueFrame )
	{
		m_buildQueueFrame = frame;

		std::vector<Object *> producers;
		player->iterateObjects( collectProducer, &producers );

		std::vector<BuildQueueEntry> entries;
		for( size_t p = 0; p < producers.size(); p++ )
		{
			Object *obj = producers[ p ];
			const ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
			const ProductionEntry *head = pu->firstProduction();
			for( const ProductionEntry *production = head; production; production = pu->nextProduction( production ) )
			{
				const ThingTemplate *thingTemplate = nullptr;
				const UpgradeTemplate *upgradeTemplate = nullptr;
				if( production->getProductionType() == PRODUCTION_UNIT )
				{
					thingTemplate = production->getProductionObject();
				}
				else if( production->getProductionType() == PRODUCTION_UPGRADE )
				{
					upgradeTemplate = production->getProductionUpgrade();
				}
				if( thingTemplate == nullptr && upgradeTemplate == nullptr )
				{
					continue;
				}

				size_t e = 0;
				for( ; e < entries.size(); e++ )
				{
					if( entries[ e ].thingTemplate == thingTemplate && entries[ e ].upgradeTemplate == upgradeTemplate )
					{
						break;
					}
				}
				if( e == entries.size() )
				{
					if( e >= MAX_BUILD_QUEUE_BAR_BUTTONS )
					{
						continue;
					}
					BuildQueueEntry entry;
					entry.thingTemplate = thingTemplate;
					entry.upgradeTemplate = upgradeTemplate;
					entry.count = 0;
					entry.building = FALSE;
					entry.percent = 0;
					entries.push_back( entry );
				}

				BuildQueueEntry &entry = entries[ e ];
				entry.count++;
				if( entry.producers.empty() || entry.producers.back() != obj->getID() )
				{
					entry.producers.push_back( obj->getID() );
				}
				if( production == head )
				{
					entry.building = TRUE;
					entry.percent = MAX( entry.percent, (Int)production->getPercentComplete() );
				}
			}
		}

		Bool same = entries.size() == m_buildQueueEntries.size();
		for( size_t e = 0; same && e < entries.size(); e++ )
		{
			same = entries[ e ].thingTemplate == m_buildQueueEntries[ e ].thingTemplate &&
						 entries[ e ].upgradeTemplate == m_buildQueueEntries[ e ].upgradeTemplate &&
						 entries[ e ].count == m_buildQueueEntries[ e ].count &&
						 entries[ e ].producers == m_buildQueueEntries[ e ].producers;
		}
		m_buildQueueEntries.swap( entries );
		if( !same )
		{
			refreshBuildQueueButtons();
		}
	}

	if( m_buildQueueEntries.empty() )
	{
		if( !m_buildQueueParent->winIsHidden() )
		{
			m_buildQueueParent->winHide( TRUE );
		}
		return;
	}

	layoutBuildQueueBar();
	m_buildQueueParent->winHide( FALSE );

	UnsignedByte r, g, b, a;
	GameGetColorComponents( m_buildUpClockColor, &r, &g, &b, &a );
	const Color clockColor = GameMakeColor( r, g, b, a * BUILD_QUEUE_CLOCK_ALPHA_PERCENT / 100 );

	// the button drops its clock after each draw, so it is set again every frame
	const size_t entryCount = m_buildQueueEntries.size();
	for( Int i = 0; i < MAX_BUILD_QUEUE_BAR_BUTTONS && (size_t)i < entryCount; i++ )
	{
		if( m_buildQueueButtons[ i ] && m_buildQueueEntries[ i ].building )
		{
			GadgetButtonDrawInverseClock( m_buildQueueButtons[ i ], m_buildQueueEntries[ i ].percent, clockColor );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Every frame, since the frame inset follows the active scheme and the radar can move. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutBuildQueueBar()
{
	ICoord2D inset;
	getCameoFrameInset( &inset, COMMAND_GROUP_FRAME_PERCENT );
	const Int stride = m_commandGroupButtonSize.x + CAMEO_ROW_GAP;
	const Int count = (Int)m_buildQueueEntries.size();
	const Int width = count * stride - CAMEO_ROW_GAP + 2 * inset.x;
	const Int height = m_commandGroupButtonSize.y + 2 * inset.y;

	ICoord2D pos;
	if( m_buildQueueRadarWindow && !m_buildQueueRadarWindow->winIsHidden() )
	{
		m_buildQueueRadarWindow->winGetScreenPosition( &pos.x, &pos.y );
		pos.x += COMMAND_GROUP_SCREEN_MARGIN;
	}
	else
	{
		m_contextParent[ CP_MASTER ]->winGetScreenPosition( &pos.x, &pos.y );
		pos.x = COMMAND_GROUP_SCREEN_MARGIN;
	}
	pos.y = MAX( pos.y - height - COMMAND_GROUP_SCREEN_MARGIN, 0 );

	m_buildQueueParent->winSetPosition( pos.x, pos.y );
	m_buildQueueParent->winSetSize( width, height );
	for( Int i = 0; i < MAX_BUILD_QUEUE_BAR_BUTTONS; i++ )
	{
		if( m_buildQueueButtons[ i ] )
		{
			m_buildQueueButtons[ i ]->winSetPosition( inset.x + i * stride, inset.y );
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::drawBuildQueueBar()
{
	if( m_buildQueueParent == nullptr || m_buildQueueParent->winIsHidden() )
	{
		return;
	}

	drawCameoBoxFrame( m_buildQueueParent );
}

//-------------------------------------------------------------------------------------------------
/** Put the queued kinds on the cameos, with the slot as the button payload. */
//-------------------------------------------------------------------------------------------------
void ControlBar::refreshBuildQueueButtons()
{
	const size_t entryCount = m_buildQueueEntries.size();

	for( Int i = 0; i < MAX_BUILD_QUEUE_BAR_BUTTONS; i++ )
	{
		GameWindow *button = m_buildQueueButtons[ i ];
		if( button == nullptr )
		{
			continue;
		}
		if( (size_t)i >= entryCount )
		{
			button->winHide( TRUE );
			continue;
		}

		const BuildQueueEntry &entry = m_buildQueueEntries[ i ];
		GadgetButtonSetData( button, (void *)(size_t)( i + 1 ) );
		const Image *source = entry.thingTemplate ? getCameoImage( entry.thingTemplate ) : entry.upgradeTemplate->getButtonImage();
		GadgetButtonSetEnabledImage( button, cropCameoImage( m_buildQueueImages[ i ], source, m_commandGroupButtonSize ) );

		UnicodeString count;
		if( entry.count > 1 && entry.count <= MAX_CAMEO_COUNT_BADGE )
		{
			count.format( L"%d", entry.count );
		}
		GadgetButtonSetText( button, count );

		if( entry.thingTemplate )
		{
			button->winSetTooltip( entry.thingTemplate->getDisplayName() );
		}
		else
		{
			button->winSetTooltip( TheGameText->fetch( entry.upgradeTemplate->getDisplayNameLabel().str() ) );
		}
		button->winHide( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
/** Select the producer after the selected one, so repeated clicks cycle through them. */
//-------------------------------------------------------------------------------------------------
void ControlBar::processBuildQueueClick( GameWindow *button )
{
	const Int slot = (Int)(size_t)GadgetButtonGetData( button ) - 1;
	if( slot < 0 || (size_t)slot >= m_buildQueueEntries.size() )
	{
		return;
	}
	const std::vector<ObjectID> &producers = m_buildQueueEntries[ slot ].producers;
	const size_t count = producers.size();

	size_t start = 0;
	if( TheInGameUI->getSelectCount() == 1 )
	{
		Drawable *selected = TheInGameUI->getFirstSelectedDrawable();
		Object *selectedObj = selected ? selected->getObject() : nullptr;
		if( selectedObj != nullptr )
		{
			for( size_t i = 0; i < count; ++i )
			{
				if( producers[ i ] == selectedObj->getID() )
				{
					start = ( i + 1 ) % count;
					break;
				}
			}
		}
	}

	for( size_t n = 0; n < count; ++n )
	{
		Object *obj = TheGameLogic->findObjectByID( producers[ ( start + n ) % count ] );
		if( obj != nullptr && obj->getDrawable() != nullptr && obj->isSelectable() )
		{
			TheInGameUI->deselectAllDrawables();
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
			teamMsg->appendBooleanArgument( TRUE );
			teamMsg->appendObjectIDArgument( obj->getID() );
			TheInGameUI->selectDrawable( obj->getDrawable() );
			return;
		}
	}
}
