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

// FILE: W3DProgressBar.cpp ///////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DProgressBar.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:      W3D implementation for the progress bar GUI control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/LoadScreen.h"
#include "W3DDevice/GameClient/W3DGadget.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetProgressBarDraw ===================================================
/** Draw colored Progress Bar using standard graphics */
//=============================================================================
void W3DGadgetProgressBarDraw( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size, start, end;
	Color backColor, backBorder, barColor, barBorder;
	Int progress = (Int)(intptr_t)window->winGetUserData();

	// get window size and position
  window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get the right colors to use
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		backColor		= GadgetProgressBarGetDisabledColor( window );
		backBorder	= GadgetProgressBarGetDisabledBorderColor( window );
		barColor		= GadgetProgressBarGetDisabledBarColor( window );
		barBorder		= GadgetProgressBarGetDisabledBarBorderColor( window );
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		backColor		= GadgetProgressBarGetHiliteColor( window );
		backBorder	= GadgetProgressBarGetHiliteBorderColor( window );
		barColor		= GadgetProgressBarGetHiliteBarColor( window );
		barBorder		= GadgetProgressBarGetHiliteBarBorderColor( window );
	}
	else
	{
		backColor		= GadgetProgressBarGetEnabledColor( window );
		backBorder	= GadgetProgressBarGetEnabledBorderColor( window );
		barColor		= GadgetProgressBarGetEnabledBarColor( window );
		barBorder		= GadgetProgressBarGetEnabledBarBorderColor( window );
	}

	// draw background border
	if( backBorder != WIN_COLOR_UNDEFINED )
	{

		start.x = origin.x;
		start.y = origin.y;
		end.x = start.x + size.x;
		end.y = start.y + size.y;
		TheWindowManager->winOpenRect( backBorder, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}

	// draw background fill
	if( backColor != WIN_COLOR_UNDEFINED )
	{

		start.x = origin.x + 1;
		start.y = origin.y + 1;
		end.x = start.x + size.x - 2;
		end.y = start.y + size.y - 2;
		TheWindowManager->winFillRect( backColor, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}

	// draw the progress so far
	if( progress )
	{

		// draw bar border
		if( barBorder != WIN_COLOR_UNDEFINED )
		{

			start.x = origin.x;
			start.y = origin.y;
			end.x = start.x + (size.x * progress) / 100;
			end.y = start.y + size.y;
			if(end.x- start.x > 1  )
			{
				TheWindowManager->winOpenRect( barBorder, WIN_DRAW_LINE_WIDTH,
																		 start.x, start.y, end.x, end.y );
			}

		}

		// draw bar fill
		if( barColor != WIN_COLOR_UNDEFINED )
		{

			start.x = origin.x + 1;
			start.y = origin.y + 1;
			end.x = start.x + (size.x * progress) / 100 - 2;
			end.y = start.y + size.y - 2;
//			TheWindowManager->winOpenRect( barColor, WIN_DRAW_LINE_WIDTH,
//																		 start.x, start.y, end.x, end.y );

			if(end.x- start.x > 1  )
			{
				TheWindowManager->winFillRect( barColor,WIN_DRAW_LINE_WIDTH,
																		 start.x, start.y, end.x, end.y );

				TheWindowManager->winDrawLine(GameMakeColor(255,255,255,255),WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, start.y);
				TheWindowManager->winDrawLine(GameMakeColor(200,200,200,255),WIN_DRAW_LINE_WIDTH, start.x, start.y, start.x, end.y);
			}

		}

	}



}

// W3DGadgetProgressBarImageDraw ==============================================
/** Draw Progress Bar with user supplied images */
//=============================================================================
void W3DGadgetProgressBarImageDrawA( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	const Image *barCenter, *barRight, *left, *right, *center;
	Int progress = (Int)(intptr_t)window->winGetUserData();
	Int xOffset, yOffset;
	Int i;
	// get window size and position
  window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get offset
	xOffset = instData->m_imageOffset.x;
	yOffset = instData->m_imageOffset.y;

	barCenter				= GadgetProgressBarGetEnabledBarImageCenter( window );
	barRight				= GadgetProgressBarGetEnabledBarImageRight( window );
	left						= GadgetProgressBarGetEnabledImageLeft( window );
	right						= GadgetProgressBarGetEnabledImageRight( window );
	center					= GadgetProgressBarGetEnabledImageCenter( window );

	if(!barCenter || !barRight || !left || !right || !center)
		return;

	Int width = barCenter->getImageWidth();
//	Int height = barCenter->getImageHeight();

	Int drawWidth = (size.x * progress) / 100;
	Int pieces = drawWidth / width;
	Int x = origin.x;
	for( i = 0; i < pieces; i ++)
	{

				TheWindowManager->winDrawImage( barCenter,
																				x, origin.y,
																				x + width, origin.y + size.y );
				x += width;
	}


}

void W3DGadgetProgressBarImageDraw( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size, start, end;
	const Image *backLeft, *backRight, *backCenter,
				 *barRight, *barCenter;//*backSmallCenter,*barLeft,, *barSmallCenter;
	Int progress = (Int)(intptr_t)window->winGetUserData();
	Int xOffset, yOffset;
	Int i;

	// get window size and position
  window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get offset
	xOffset = instData->m_imageOffset.x;
	yOffset = instData->m_imageOffset.y;

	// get the right images to use
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		backLeft				= GadgetProgressBarGetDisabledImageLeft( window );
		//barLeft					= GadgetProgressBarGetDisabledBarImageLeft( window );
		backRight				= GadgetProgressBarGetDisabledImageRight( window );
		barRight				= GadgetProgressBarGetDisabledBarImageRight( window );
		backCenter			= GadgetProgressBarGetDisabledImageCenter( window );
		barCenter				= GadgetProgressBarGetDisabledBarImageCenter( window );
		//backSmallCenter	= GadgetProgressBarGetDisabledImageSmallCenter( window );
		//barSmallCenter	= GadgetProgressBarGetDisabledBarImageSmallCenter( window );

	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{

		backLeft				= GadgetProgressBarGetHiliteImageLeft( window );
		//barLeft					= GadgetProgressBarGetHiliteBarImageLeft( window );
		backRight				= GadgetProgressBarGetHiliteImageRight( window );
		barRight				= GadgetProgressBarGetHiliteBarImageRight( window );
		backCenter			= GadgetProgressBarGetHiliteImageCenter( window );
		barCenter				= GadgetProgressBarGetHiliteBarImageCenter( window );
		//backSmallCenter	= GadgetProgressBarGetHiliteImageSmallCenter( window );
		//barSmallCenter	= GadgetProgressBarGetHiliteBarImageSmallCenter( window );

	}
	else
	{

		backLeft				= GadgetProgressBarGetEnabledImageLeft( window );
		//barLeft					= GadgetProgressBarGetEnabledBarImageLeft( window );
		backRight				= GadgetProgressBarGetEnabledImageRight( window );
		barRight				= GadgetProgressBarGetEnabledBarImageRight( window );
		backCenter			= GadgetProgressBarGetEnabledImageCenter( window );
		barCenter				= GadgetProgressBarGetEnabledBarImageCenter( window );
		//backSmallCenter	= GadgetProgressBarGetEnabledImageSmallCenter( window );
		//barSmallCenter	= GadgetProgressBarGetEnabledBarImageSmallCenter( window );

	}

	// sanity
	if( backLeft == nullptr || backRight == nullptr ||
			backCenter == nullptr ||
			barRight == nullptr)
			// backSmallCenter == nullptr ||barLeft == nullptr ||barCenter == nullptr || barSmallCenter == nullptr )
		return;

	TheWindowManager->winDrawThreePieceImage( backLeft, backCenter, nullptr, backRight,
																						 origin.x + xOffset, origin.y + yOffset, size.x, size.y );


	ICoord2D barWindowSize;  // end point of bar from window origin

	barWindowSize.x = ((size.x - 20) * progress) / 100;
	barWindowSize.y = size.y;

	// keep the bar tiles at their own aspect for the fill height
	Int pieces;
	Int fillHeight = size.y - 10;
	Int barCenterWidth = barCenter->getImageWidth();
	Int barRightWidth = barRight->getImageWidth();
	if( fillHeight > 0 && barCenter->getImageHeight() > 0 && fillHeight != barCenter->getImageHeight() )
		barCenterWidth = (barCenterWidth * fillHeight + barCenter->getImageHeight() / 2) / barCenter->getImageHeight();
	if( fillHeight > 0 && barRight->getImageHeight() > 0 && fillHeight != barRight->getImageHeight() )
		barRightWidth = (barRightWidth * fillHeight + barRight->getImageHeight() / 2) / barRight->getImageHeight();
	if( barCenterWidth < 1 )
		barCenterWidth = 1;
	if( barRightWidth < 1 )
		barRightWidth = 1;

	pieces = barWindowSize.x / barCenterWidth;

	// TheSuperHackers @feature arcticdolphin 09/11/2026 A bar in the GO bar art carries the radar van; driving inside the bar it rides the track ahead of the fill, so the fill stops at its rear.
	const Int fillEnd = origin.x + 10 + barCenterWidth * pieces;
	const Bool goVan = isGoLoadBarImage( barCenter );
	IRegion2D vanFill;
	vanFill.lo.x = origin.x + 10;
	vanFill.lo.y = origin.y + yOffset + 5;
	vanFill.hi.x = origin.x + size.x - 10;
	vanFill.hi.y = vanFill.lo.y + size.y - 10;
	IRegion2D vanRoad = vanFill;
	if( isGoLoadBarImage( backLeft ) )
	{
		vanRoad.lo.x = origin.x + xOffset;
		vanRoad.lo.y = origin.y + yOffset;
		vanRoad.hi.x = vanRoad.lo.x + size.x;
		vanRoad.hi.y = vanRoad.lo.y + size.y;
	}
	if( goVan )
	{
		const Int fillStop = goLoadBarVanFillEnd( window, vanRoad, vanFill, fillEnd );
		if( fillStop < fillEnd )
		{
			pieces = ( fillStop - vanFill.lo.x ) / barCenterWidth;
			if( pieces < 0 )
			{
				pieces = 0;
			}
		}
	}

 	// draw the pieces
	start.x = origin.x +10;
	start.y = origin.y + yOffset +5;
	end.y = start.y + size.y - 10;
	for( i = 0; i < pieces; i++ )
	{

		end.x = start.x + barCenterWidth;
		TheWindowManager->winDrawImage( barCenter,
																		start.x, start.y,
																		end.x, end.y );
		start.x += barCenterWidth;

	}
	start.x = origin.x + 10 + barCenterWidth * pieces;
	//pieces = (size.x - barWindowSize.x -20) / barRight->getImageWidth();
	pieces = ((size.x - 20) / barCenterWidth) - pieces;
	for( i = 0; i < pieces; i++ )
	{

		end.x = start.x + barRightWidth;
		TheWindowManager->winDrawImage( barRight,
																		start.x, start.y,
																		end.x, end.y );
		start.x += barRightWidth;

	}

	// TheSuperHackers @feature arcticdolphin 09/11/2026 The van over the fill and track; no in-match bar uses the GO bar art.
	if( goVan )
	{
		drawGoLoadBarVan( window, progress, vanRoad, vanFill, fillEnd );
	}

}
