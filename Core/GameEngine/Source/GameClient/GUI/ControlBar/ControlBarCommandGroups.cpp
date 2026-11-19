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

// TheSuperHackers @feature Command group row, part of smart selection. One cameo per hotkey
// squad with live members, in its own box at the screen's top left corner, with the squad number
// under each cameo. A click goes through the same meta message the number key sends.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Squad.h"
#include "GameClient/ControlBar.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"

//-------------------------------------------------------------------------------------------------
/** The container owns the cameos, so their clicks land here. */
//-------------------------------------------------------------------------------------------------
static WindowMsgHandledType CommandGroupBarSystem( GameWindow *window, UnsignedInt msg,
																									 WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED )
	{
		return MSG_IGNORED;
	}
	TheControlBar->processCommandGroupClick( (GameWindow *)mData1 );
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
static void CommandGroupBarDraw( GameWindow *window, WinInstanceData *instData )
{
	TheControlBar->drawCommandGroupBar();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::initCommandGroupBar( const ICoord2D &commandButtonSize )
{
	m_commandGroupButtonSize.x = commandButtonSize.x * COMMAND_GROUP_CAMEO_SCALE_PERCENT / 100;
	m_commandGroupButtonSize.y = commandButtonSize.y * COMMAND_GROUP_CAMEO_SCALE_PERCENT / 100;
	if( m_commandGroupButtonSize.x <= 0 || m_commandGroupButtonSize.y <= 0 )
	{
		return;
	}

	m_commandGroupParent = createCameoRow( CommandGroupBarSystem, MAX_COMMAND_GROUP_BUTTONS, FALSE, m_commandGroupButtonSize, m_commandGroupButtons );
	if( m_commandGroupParent == nullptr )
	{
		return;
	}

	// the box draws its frame itself and keeps clicks on it off the world
	m_commandGroupParent->winClearStatus( WIN_STATUS_SEE_THRU );
	m_commandGroupParent->winSetInputFunc( GameWinBlockInput );
	m_commandGroupParent->winSetDrawFunc( CommandGroupBarDraw );

	GameFont *cameoFont = m_commandGroupButtons[ 0 ] ? m_commandGroupButtons[ 0 ]->winGetFont() : nullptr;
	if( cameoFont == nullptr )
	{
		return;
	}
	const Int plate = m_commandGroupButtonSize.y / 2;
	GameFont *font = TheFontLibrary->getFont( cameoFont->nameString, MAX( plate / 2, 8 ), TRUE );
	if( font == nullptr )
	{
		font = cameoFont;
	}
	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS; i++ )
	{
		m_commandGroupNumbers[ i ] = TheDisplayStringManager->newDisplayString();
		if( m_commandGroupNumbers[ i ] )
		{
			m_commandGroupNumbers[ i ]->setFont( font );
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::destroyCommandGroupBar()
{
	if( m_commandGroupParent )
	{
		TheWindowManager->winDestroy( m_commandGroupParent );
	}
	m_commandGroupParent = nullptr;
	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS; i++ )
	{
		m_commandGroupButtons[ i ] = nullptr;
		if( m_commandGroupNumbers[ i ] && TheDisplayStringManager )
		{
			TheDisplayStringManager->freeDisplayString( m_commandGroupNumbers[ i ] );
		}
		m_commandGroupNumbers[ i ] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::resetCommandGroupBar()
{
	m_commandGroupEntries.clear();
	if( isCommandGroupRowShown() )
	{
		m_commandGroupParent->winHide( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::isCommandGroupRowShown() const
{
	return m_commandGroupParent && !m_commandGroupParent->winIsHidden();
}

//-------------------------------------------------------------------------------------------------
/** Every frame: the squads change under the row when members die, which marks nothing dirty,
	* so the entries are rebuilt once per logic frame and the row only repainted when they differ. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateCommandGroupBar()
{
	if( m_commandGroupParent == nullptr )
	{
		return;
	}

	GameWindow *master = m_contextParent[ CP_MASTER ];
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( master == nullptr || master->winIsHidden() || player == nullptr || m_isObserverCommandBar ||
			!TheGlobalData->m_smartSelection || !TheGlobalData->m_smartCommandGroup )
	{
		resetCommandGroupBar();
		return;
	}

	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame != m_commandGroupFrame || m_commandGroupEntries.empty() )
	{
		m_commandGroupFrame = frame;

		// squads in key order, 1 through 9 then 0, each with its most common type
		std::vector<CommandGroupEntry> entries;
		std::map<const ThingTemplate *, Int> typeCounts;
		for( Int slot = 0; slot < MAX_COMMAND_GROUP_BUTTONS; slot++ )
		{
			const Int group = ( slot + 1 ) % MAX_COMMAND_GROUP_BUTTONS;
			Squad *squad = player->getHotkeySquad( group );
			if( squad == nullptr )
			{
				continue;
			}
			const VecObjectPtr &members = squad->getLiveObjects();
			if( members.empty() )
			{
				continue;
			}

			typeCounts.clear();
			for( size_t m = 0; m < members.size(); m++ )
			{
				typeCounts[ members[ m ]->getTemplate()->getReskinRoot() ]++;
			}
			CommandGroupEntry entry;
			entry.group = group;
			entry.thingTemplate = nullptr;
			entry.count = (Int)members.size();
			Int best = 0;
			for( std::map<const ThingTemplate *, Int>::const_iterator it = typeCounts.begin(); it != typeCounts.end(); ++it )
			{
				if( it->second > best )
				{
					best = it->second;
					entry.thingTemplate = it->first;
				}
			}
			entries.push_back( entry );
		}

		if( entries.empty() )
		{
			resetCommandGroupBar();
			return;
		}
		if( entries != m_commandGroupEntries )
		{
			m_commandGroupEntries.swap( entries );
			refreshCommandGroupButtons();
		}
	}

	layoutCommandGroupBar();
	m_commandGroupParent->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
/** Every frame, since the frame inset follows the active scheme. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutCommandGroupBar()
{
	ICoord2D inset;
	getCameoFrameInset( &inset );
	const Int plate = m_commandGroupButtonSize.y / 2;
	const Int stride = m_commandGroupButtonSize.x + CAMEO_ROW_GAP;
	const Int count = (Int)m_commandGroupEntries.size();

	m_commandGroupParent->winSetPosition( COMMAND_GROUP_SCREEN_MARGIN, COMMAND_GROUP_SCREEN_MARGIN );
	m_commandGroupParent->winSetSize( count * stride - CAMEO_ROW_GAP + 2 * inset.x, m_commandGroupButtonSize.y + plate + 2 * inset.y );
	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS; i++ )
	{
		if( m_commandGroupButtons[ i ] )
		{
			m_commandGroupButtons[ i ]->winSetPosition( inset.x + i * stride, inset.y );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The frame behind the cameos, and each squad number centred in the strip under its cameo. */
//-------------------------------------------------------------------------------------------------
void ControlBar::drawCommandGroupBar()
{
	if( m_commandGroupParent == nullptr || m_commandGroupParent->winIsHidden() )
	{
		return;
	}

	ICoord2D inset;
	if( getCameoFrameInset( &inset ) )
	{
		IRegion2D rect;
		ICoord2D size;
		m_commandGroupParent->winGetScreenPosition( &rect.lo.x, &rect.lo.y );
		m_commandGroupParent->winGetSize( &size.x, &size.y );
		rect.hi.x = rect.lo.x + size.x;
		rect.hi.y = rect.lo.y + size.y;
		drawCameoFrame( rect );
	}

	GameWindow *firstButton = m_commandGroupButtons[ 0 ];
	if( firstButton == nullptr )
	{
		return;
	}
	const Color textColor = firstButton->winGetEnabledTextColor();
	const Color dropColor = firstButton->winGetEnabledTextBorderColor();
	const Int plate = m_commandGroupButtonSize.y / 2;
	const size_t entryCount = m_commandGroupEntries.size();
	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS && (size_t)i < entryCount; i++ )
	{
		GameWindow *button = m_commandGroupButtons[ i ];
		DisplayString *number = m_commandGroupNumbers[ i ];
		if( button == nullptr || number == nullptr )
		{
			continue;
		}
		ICoord2D buttonPos, buttonSize, textSize;
		button->winGetScreenPosition( &buttonPos.x, &buttonPos.y );
		button->winGetSize( &buttonSize.x, &buttonSize.y );
		number->getSize( &textSize.x, &textSize.y );
		number->draw( buttonPos.x + ( buttonSize.x - textSize.x ) / 2, buttonPos.y + buttonSize.y + ( plate - textSize.y ) / 2,
			textColor, dropColor );
	}
}

//-------------------------------------------------------------------------------------------------
/** Put the squads on the cameos, with the squad number as the button payload. */
//-------------------------------------------------------------------------------------------------
void ControlBar::refreshCommandGroupButtons()
{
	const size_t entryCount = m_commandGroupEntries.size();

	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS; i++ )
	{
		GameWindow *button = m_commandGroupButtons[ i ];
		if( button == nullptr )
		{
			continue;
		}
		if( (size_t)i >= entryCount )
		{
			button->winHide( TRUE );
			continue;
		}

		const CommandGroupEntry &entry = m_commandGroupEntries[ i ];
		GadgetButtonSetData( button, (void *)(size_t)( entry.group + 1 ) );
		GadgetButtonSetEnabledImage( button, getCameoImage( entry.thingTemplate ) );

		UnicodeString count;
		if( entry.count <= MAX_CAMEO_COUNT_BADGE )
		{
			count.format( L"%d", entry.count );
		}
		GadgetButtonSetText( button, count );
		if( m_commandGroupNumbers[ i ] )
		{
			UnicodeString number;
			number.format( L"%d", entry.group );
			m_commandGroupNumbers[ i ]->setText( number );
		}

		UnicodeString tooltip;
		tooltip.format( L"Group %d: %s", entry.group, entry.thingTemplate->getDisplayName().str() );
		button->winSetTooltip( tooltip );
		button->winHide( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
/** The same meta message the number key sends, so the selection translator does the select
	* and its double tap timer turns a double click into a camera jump. */
//-------------------------------------------------------------------------------------------------
void ControlBar::processCommandGroupClick( GameWindow *button )
{
	const Int group = (Int)(size_t)GadgetButtonGetData( button ) - 1;
	if( group < 0 || group >= MAX_COMMAND_GROUP_BUTTONS )
	{
		return;
	}
	TheMessageStream->appendMessage( (GameMessage::Type)( GameMessage::MSG_META_SELECT_TEAM0 + group ) );
}
