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
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"

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
/** Height of the number strip under a cameo. */
//-------------------------------------------------------------------------------------------------
static Int commandGroupPlateHeight( Int buttonHeight )
{
	return buttonHeight * 2 / 7;
}

//-------------------------------------------------------------------------------------------------
/** Fill the slot's image with the centred part of the source picture that has the button's
	* aspect ratio, since the button stretches its image over the whole window. */
//-------------------------------------------------------------------------------------------------
const Image *ControlBar::cropCameoImage( Image *&slot, const Image *source, const ICoord2D &buttonSize )
{
	if( source == nullptr || source->getImageWidth() <= 0 || source->getImageHeight() <= 0 ||
			buttonSize.x <= 0 || buttonSize.y <= 0 )
	{
		return source;
	}

	if( slot == nullptr )
	{
		slot = newInstance( Image );
	}
	slot->setName( source->getName() );
	slot->setFilename( source->getFilename() );
	slot->setTextureWidth( source->getTextureSize()->x );
	slot->setTextureHeight( source->getTextureSize()->y );
	slot->setRawTextureData( const_cast<void *>( source->getRawTextureData() ) );
	slot->clearStatus( slot->getStatus() );
	slot->setStatus( source->getStatus() );

	const Real sourceAspect = (Real)source->getImageWidth() / (Real)source->getImageHeight();
	const Real targetAspect = (Real)buttonSize.x / (Real)buttonSize.y;
	Real keepWidth = 1.0f;
	Real keepHeight = 1.0f;
	if( targetAspect < sourceAspect )
	{
		keepWidth = targetAspect / sourceAspect;
	}
	else
	{
		keepHeight = sourceAspect / targetAspect;
	}

	// a rotated image draws V across the screen and U down it
	const Bool rotated = BitIsSet( source->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE );
	const Real keepU = rotated ? keepHeight : keepWidth;
	const Real keepV = rotated ? keepWidth : keepHeight;
	const Region2D *sourceUV = source->getUV();
	const Real marginU = ( sourceUV->hi.x - sourceUV->lo.x ) * ( 1.0f - keepU ) / 2.0f;
	const Real marginV = ( sourceUV->hi.y - sourceUV->lo.y ) * ( 1.0f - keepV ) / 2.0f;
	Region2D uv;
	uv.lo.x = sourceUV->lo.x + marginU;
	uv.hi.x = sourceUV->hi.x - marginU;
	uv.lo.y = sourceUV->lo.y + marginV;
	uv.hi.y = sourceUV->hi.y - marginV;
	slot->setUV( &uv );

	ICoord2D size;
	size.x = REAL_TO_INT( source->getImageWidth() * keepWidth + 0.5f );
	size.y = REAL_TO_INT( source->getImageHeight() * keepHeight + 0.5f );
	slot->setImageSize( &size );
	return slot;
}

//-------------------------------------------------------------------------------------------------
/** Give the buttons' text, which is the count badge, its colour and a larger font. */
//-------------------------------------------------------------------------------------------------
void ControlBar::styleCameoCountBadges( GameWindow **buttons, Int count )
{
	const Color badgeColor = GameMakeColor( 255, 210, 40, 255 );
	for( Int i = 0; i < count; i++ )
	{
		GameWindow *button = buttons[ i ];
		if( button )
		{
			button->winSetEnabledTextColors( badgeColor, button->winGetEnabledTextBorderColor() );
			button->winSetHiliteTextColors( badgeColor, button->winGetHiliteTextBorderColor() );
			button->winSetDisabledTextColors( badgeColor, button->winGetDisabledTextBorderColor() );
		}
	}

	GameFont *cameoFont = buttons[ 0 ] ? buttons[ 0 ]->winGetFont() : nullptr;
	if( cameoFont == nullptr )
	{
		return;
	}
	const Int countPoint = MAX( cameoFont->pointSize * 3 / 2, cameoFont->pointSize + 2 );
	GameFont *countFont = TheFontLibrary->getFont( cameoFont->nameString, countPoint, cameoFont->bold );
	if( countFont == nullptr )
	{
		countFont = cameoFont;
	}
	for( Int i = 0; i < count; i++ )
	{
		if( buttons[ i ] )
		{
			buttons[ i ]->winSetFont( countFont );
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::initCommandGroupBar( const ICoord2D &commandButtonSize )
{
	ICoord2D base;
	base.x = commandButtonSize.x * COMMAND_GROUP_CAMEO_SCALE_PERCENT / 100;
	base.y = commandButtonSize.y * COMMAND_GROUP_CAMEO_SCALE_PERCENT / 100;
	m_commandGroupButtonSize.x = base.x;
	m_commandGroupButtonSize.y = base.y * 7 / 6;
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

	// the squad numbers keep the row's text colours, the member count badges get their own
	if( m_commandGroupButtons[ 0 ] )
	{
		m_commandGroupNumberColor = m_commandGroupButtons[ 0 ]->winGetEnabledTextColor();
		m_commandGroupNumberDropColor = m_commandGroupButtons[ 0 ]->winGetEnabledTextBorderColor();
	}
	GameFont *cameoFont = m_commandGroupButtons[ 0 ] ? m_commandGroupButtons[ 0 ]->winGetFont() : nullptr;
	styleCameoCountBadges( m_commandGroupButtons, MAX_COMMAND_GROUP_BUTTONS );
	if( cameoFont == nullptr )
	{
		return;
	}

	const Int plate = commandGroupPlateHeight( m_commandGroupButtonSize.y );
	GameFont *font = TheFontLibrary->getFont( cameoFont->nameString, MAX( plate * 9 / 10, 6 ), TRUE );
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
	// destroyed windows are freed later, so they must not keep pointers to the slot images
	for( Int i = 0; i < MAX_COMMAND_GROUP_BUTTONS; i++ )
	{
		if( m_commandGroupButtons[ i ] )
		{
			GadgetButtonSetEnabledImage( m_commandGroupButtons[ i ], nullptr );
		}
	}
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
		deleteInstance( m_commandGroupImages[ i ] );
		m_commandGroupImages[ i ] = nullptr;
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
Int ControlBar::getCommandGroupBarBottom() const
{
	if( !isCommandGroupRowShown() )
	{
		return 0;
	}
	ICoord2D pos, size;
	m_commandGroupParent->winGetScreenPosition( &pos.x, &pos.y );
	m_commandGroupParent->winGetSize( &size.x, &size.y );
	return pos.y + size.y;
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
	getCameoFrameInset( &inset, COMMAND_GROUP_FRAME_PERCENT );
	const Int plate = commandGroupPlateHeight( m_commandGroupButtonSize.y );
	const Int stride = m_commandGroupButtonSize.x + CAMEO_ROW_GAP;
	const Int count = (Int)m_commandGroupEntries.size();
	const Int hudBottom = TheInGameUI ? TheInGameUI->getHudAnchorBottom() : 0;

	m_commandGroupParent->winSetPosition( COMMAND_GROUP_SCREEN_MARGIN, MAX( COMMAND_GROUP_SCREEN_MARGIN, hudBottom + COMMAND_GROUP_SCREEN_MARGIN ) );
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
	if( getCameoFrameInset( &inset, COMMAND_GROUP_FRAME_PERCENT ) )
	{
		IRegion2D rect;
		ICoord2D size;
		m_commandGroupParent->winGetScreenPosition( &rect.lo.x, &rect.lo.y );
		m_commandGroupParent->winGetSize( &size.x, &size.y );
		rect.hi.x = rect.lo.x + size.x;
		rect.hi.y = rect.lo.y + size.y;
		drawCameoFrame( rect, COMMAND_GROUP_FRAME_PERCENT );
	}

	const Int plate = commandGroupPlateHeight( m_commandGroupButtonSize.y );
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
			m_commandGroupNumberColor, m_commandGroupNumberDropColor );
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
		GadgetButtonSetEnabledImage( button, cropCameoImage( m_commandGroupImages[ i ], getCameoImage( entry.thingTemplate ), m_commandGroupButtonSize ) );

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
