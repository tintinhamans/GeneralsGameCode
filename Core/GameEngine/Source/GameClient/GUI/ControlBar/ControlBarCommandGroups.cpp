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
// squad with live members, on the command bar frame under the smart selection row. A click
// goes through the same meta message the number key sends.

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
#include "GameClient/GadgetPushButton.h"
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
//-------------------------------------------------------------------------------------------------
void ControlBar::initCommandGroupBar()
{
	m_commandGroupParent = createCameoRow( CommandGroupBarSystem, MAX_COMMAND_GROUP_BUTTONS, FALSE, m_commandGroupButtons );
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

	// the bar's parent window starts well above its visible frame, so the layout anchors to the
	// command grid, whose top sits at the frame
	const CameoRowLayout layout = getCameoRowLayout();
	m_commandGroupParent->winSetPosition( layout.cameoLeft, layout.restBottom - m_smartSelectionButtonSize.y );
	m_commandGroupParent->winHide( FALSE );
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
		GadgetButtonSetCornerLetter( button, (Char)( '0' + entry.group ) );

		UnicodeString tooltip;
		tooltip.format( L"Group %d: %s", entry.group, entry.thingTemplate->getDisplayName().str() );
		button->winSetTooltip( tooltip );
		button->winHide( FALSE );
	}

	m_commandGroupParent->winSetSize( MAX( getCameoRowWidth( (Int)entryCount ), 1 ), m_smartSelectionButtonSize.y );
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
