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

// FILE: .cpp /////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:
//
// File name: .cpp
//
// Created:
//
// Desc:
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "W3DDevice/GameClient/W3DGadget.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

// Draw top, center and bottom pieces down a column, the ends at the column width and their own aspect
static void drawVerticalThreePieceImage( const Image *top, const Image *center, const Image *bottom,
																				 Int x, Int y, Int width, Int height )
{
	if( top == nullptr || center == nullptr || bottom == nullptr || width <= 0 || height <= 0 )
		return;

	Int topHeight = width;
	if( top->getImageWidth() > 0 )
		topHeight = (top->getImageHeight() * width + top->getImageWidth() / 2) / top->getImageWidth();
	Int bottomHeight = width;
	if( bottom->getImageWidth() > 0 )
		bottomHeight = (bottom->getImageHeight() * width + bottom->getImageWidth() / 2) / bottom->getImageWidth();

	// too short for both ends, split the height between them
	if( topHeight + bottomHeight > height )
	{
		topHeight = height / 2;
		bottomHeight = height - topHeight;
	}

	TheWindowManager->winDrawImage( top, x, y, x + width, y + topHeight );
	if( height - topHeight - bottomHeight > 0 )
		TheWindowManager->winDrawImage( center, x, y + topHeight, x + width, y + height - bottomHeight );
	TheWindowManager->winDrawImage( bottom, x, y + height - bottomHeight, x + width, y + height );
}

// TheSuperHackers @tweak arcticdolphin 08/11/2026 The kit pill fills its channel, like the kit slider fill fills its track, at 4 of the 14 column units.
static const Int KIT_SCROLL_CHANNEL_WIDTH = 4;

// The kit channel, centred between the column separator and the list frame
static void getKitChannel( Int columnX, Int columnWidth, Int &channelX, Int &channelWidth )
{
	Int separator = GadgetListBoxGetKitScrollLineWidth();
	if( separator > columnWidth / 2 )
		separator = 0;
	channelWidth = GameWindowManager::winScaleLayoutSize( KIT_SCROLL_CHANNEL_WIDTH );
	if( channelWidth > columnWidth - 2 * separator )
		channelWidth = columnWidth - 2 * separator;
	if( channelWidth < 2 )
		channelWidth = 2;
	// TheSuperHackers @tweak arcticdolphin 08/11/2026 The list frame on the right is as thick as the separator.
	channelX = columnX + separator + (columnWidth - 2 * separator - channelWidth) / 2;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetVerticalSliderDraw ================================================
/** Draw colored vertical slider using standard graphics */
//=============================================================================
void W3DGadgetVerticalSliderDraw( GameWindow *window,
																	WinInstanceData *instData )
{
	Color backBorder, backColor;
	ICoord2D origin, size, start, end;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get the right colors
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		backBorder		= GadgetSliderGetDisabledBorderColor( window );
		backColor			= GadgetSliderGetDisabledColor( window );

	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{

		backBorder		= GadgetSliderGetHiliteBorderColor( window );
		backColor			= GadgetSliderGetHiliteColor( window );

	}
	else
	{

		backBorder		= GadgetSliderGetEnabledBorderColor( window );
		backColor			= GadgetSliderGetEnabledColor( window );

	}

	// draw background border and rect over whole control
	if( backBorder != WIN_COLOR_UNDEFINED )
	{

		start.x = origin.x;
		start.y = origin.y;
		end.x = start.x + size.x;
		end.y = start.y + size.y;
		TheWindowManager->winOpenRect( backBorder, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}
	if( backColor != WIN_COLOR_UNDEFINED )
	{

		start.x = origin.x + 1;
		start.y = origin.y + 1;
		end.x = start.x + size.x - 2;
		end.y = start.y + size.y - 2;
		TheWindowManager->winFillRect( backColor, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}



}

// W3DGadgetVerticalSliderImageDraw ===========================================
/** Draw vertical slider with user supplied images */
//=============================================================================
void W3DGadgetVerticalSliderImageDraw( GameWindow *window,
																			 WinInstanceData *instData )
{
	const Image *topImage, *bottomImage, *centerImage, *smallCenterImage;
	ICoord2D origin, size, start, end;
	Int xOffset, yOffset;
	Int i;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get image offset
	xOffset = instData->m_imageOffset.x;
	yOffset = instData->m_imageOffset.y;

	// get the right images
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		topImage					= GadgetSliderGetDisabledImageTop( window );
		bottomImage				= GadgetSliderGetDisabledImageBottom( window );
		centerImage				= GadgetSliderGetDisabledImageCenter( window );
		smallCenterImage	= GadgetSliderGetDisabledImageSmallCenter( window );

	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{

		topImage					= GadgetSliderGetHiliteImageTop( window );
		bottomImage				= GadgetSliderGetHiliteImageBottom( window );
		centerImage				= GadgetSliderGetHiliteImageCenter( window );
		smallCenterImage	= GadgetSliderGetHiliteImageSmallCenter( window );

	}
	else
	{

		topImage					= GadgetSliderGetEnabledImageTop( window );
		bottomImage				= GadgetSliderGetEnabledImageBottom( window );
		centerImage				= GadgetSliderGetEnabledImageCenter( window );
		smallCenterImage	= GadgetSliderGetEnabledImageSmallCenter( window );

	}

	// sanity, we need to have these images to make it look right
	if( topImage == nullptr || bottomImage == nullptr ||
			centerImage == nullptr || smallCenterImage == nullptr )
		return;

	// get image sizes for the ends
	ICoord2D topSize, bottomSize;
	topSize.x = topImage->getImageWidth();
	topSize.y = topImage->getImageHeight();
	bottomSize.x = bottomImage->getImageWidth();
	bottomSize.y = bottomImage->getImageHeight();

	if(topSize.y + bottomSize.y >= size.y)
	{
		// scale each end to the slider width, at most half the slider height
		Int halfHeight = size.y / 2;
		ICoord2D topDraw, bottomDraw;
		topDraw = topSize;
		bottomDraw = bottomSize;
		if( topSize.x > 0 && topSize.y > 0 )
		{
			topDraw.x = size.x;
			topDraw.y = (topSize.y * size.x + topSize.x / 2) / topSize.x;
			if( topDraw.y > halfHeight )
			{
				topDraw.y = halfHeight;
				topDraw.x = (topSize.x * halfHeight + topSize.y / 2) / topSize.y;
			}
		}
		if( bottomSize.x > 0 && bottomSize.y > 0 )
		{
			bottomDraw.x = size.x;
			bottomDraw.y = (bottomSize.y * size.x + bottomSize.x / 2) / bottomSize.x;
			if( bottomDraw.y > halfHeight )
			{
				bottomDraw.y = halfHeight;
				bottomDraw.x = (bottomSize.x * halfHeight + bottomSize.y / 2) / bottomSize.y;
			}
		}

		// draw top end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end.x = start.x + topDraw.x;
		end.y = start.y + topDraw.y;
		TheWindowManager->winDrawImage(topImage, start.x, start.y, end.x, end.y);

		// draw bottom end
		start.y = origin.y + yOffset + size.y - bottomDraw.y;
		end.x = start.x + bottomDraw.x;
		end.y = origin.y + yOffset + size.y;
		TheWindowManager->winDrawImage(bottomImage, start.x, start.y, end.x, end.y);
	}
	else
	{

		// get two key points used in the end drawing
		ICoord2D topEnd, bottomStart;
		topEnd.x = origin.x + topSize.x + xOffset;
		topEnd.y = origin.y + topSize.y + yOffset;
		bottomStart.x = origin.x + xOffset;
		bottomStart.y = origin.y + size.y - bottomSize.y + yOffset;

		// draw the center repeating bar
		Int centerHeight, pieces;

		// get size we have to draw our repeating center in
		centerHeight = bottomStart.y - topEnd.y;

		// how many whole repeating pieces will fit in that size
		pieces = centerHeight / centerImage->getImageHeight();

		// draw the pieces
		start.x = origin.x + xOffset;
		start.y = topEnd.y;
		end.x = start.x + centerImage->getImageWidth();
		end.y = start.y + centerImage->getImageHeight();
		for( i = 0; i < pieces; i++ )
		{

			TheWindowManager->winDrawImage( centerImage,
																			start.x, start.y,
																			end.x, end.y );
			start.y += centerImage->getImageHeight();
			end.y += centerImage->getImageHeight();

		}

		//
		// how many small repeating pieces will fit in the gap from where the
		// center repeating bar stopped and the bottom image, draw them
		// and overlapping underneath where the bottom end will go
		//
		centerHeight = bottomStart.y - start.y;
		pieces = centerHeight / smallCenterImage->getImageHeight() + 1;
		end.y = start.y + smallCenterImage->getImageHeight();
		for( i = 0; i < pieces; i++ )
		{

			TheWindowManager->winDrawImage( smallCenterImage,
																			start.x, start.y,
																			end.x, end.y );
			start.y += smallCenterImage->getImageHeight();
			end.y += smallCenterImage->getImageHeight();

		}

		// draw top end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end = topEnd;
		TheWindowManager->winDrawImage(topImage, start.x, start.y, end.x, end.y);

		// draw bottom end
		start = bottomStart;
		end.x = start.x + bottomSize.x;
		end.y = start.y + bottomSize.y;
		TheWindowManager->winDrawImage(bottomImage, start.x, start.y, end.x, end.y);
	}


}

// W3DGadgetVerticalSliderKitTrackDraw ========================================
/** Draw a list box scroll track as a rounded kit channel */
//=============================================================================
// TheSuperHackers @feature arcticdolphin 08/11/2026 Draw the list scroll track as a rounded channel when the GO kit scroll images exist.
void W3DGadgetVerticalSliderKitTrackDraw( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	Int channelX, channelWidth;
	getKitChannel( origin.x, size.x, channelX, channelWidth );
	drawVerticalThreePieceImage( GadgetListBoxGetKitScrollImage( KIT_SCROLL_TRACK_TOP ),
															 GadgetListBoxGetKitScrollImage( KIT_SCROLL_TRACK_CENTER ),
															 GadgetListBoxGetKitScrollImage( KIT_SCROLL_TRACK_BOTTOM ),
															 channelX, origin.y, channelWidth, size.y );

	// TheSuperHackers @tweak arcticdolphin 08/11/2026 The column separator runs between the arrow cells, which draw their own part.
	GameWindow *listbox = window->winGetParent();
	if( listbox == nullptr )
		return;

	Int top = origin.y;
	Int bottom = origin.y + size.y;
	GameWindow *upButton = GadgetListBoxGetUpButton( listbox );
	GameWindow *downButton = GadgetListBoxGetDownButton( listbox );
	if( upButton )
	{
		ICoord2D upPos, upSize;
		upButton->winGetScreenPosition( &upPos.x, &upPos.y );
		upButton->winGetSize( &upSize.x, &upSize.y );
		top = upPos.y + upSize.y;
	}
	if( downButton )
	{
		ICoord2D downPos;
		downButton->winGetScreenPosition( &downPos.x, &downPos.y );
		bottom = downPos.y;
	}
	if( bottom > top )
		GadgetListBoxDrawKitLines( origin.x, top, size.x, bottom - top, TRUE, FALSE, FALSE, FALSE );
}

// W3DGadgetVerticalSliderKitThumbDraw ========================================
/** Draw a list box scroll thumb as a kit pill filling the channel */
//=============================================================================
void W3DGadgetVerticalSliderKitThumbDraw( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// disabled with its slider, brighter while hovered or pressed
	GameWindow *slider = window->winGetParent();
	Bool enabled = BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) &&
								 ( slider == nullptr || BitIsSet( slider->winGetStatus(), WIN_STATUS_ENABLED ) );
	KitScrollImage top = KIT_SCROLL_THUMB_TOP;
	if( enabled == FALSE )
		top = KIT_SCROLL_THUMB_DISABLED_TOP;
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) || BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		top = KIT_SCROLL_THUMB_HILITE_TOP;

	Int channelX, channelWidth;
	getKitChannel( origin.x, size.x, channelX, channelWidth );
	drawVerticalThreePieceImage( GadgetListBoxGetKitScrollImage( top ),
															 GadgetListBoxGetKitScrollImage( (KitScrollImage)(top + 1) ),
															 GadgetListBoxGetKitScrollImage( (KitScrollImage)(top + 2) ),
															 channelX, origin.y, channelWidth, size.y );
}


