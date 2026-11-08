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
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/Mouse.h"
#include "W3DDevice/GameClient/W3DGadget.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

enum KitSliderImage
{
	KIT_SLIDER_TRACK_LEFT,
	KIT_SLIDER_TRACK_CENTER,
	KIT_SLIDER_TRACK_RIGHT,
	KIT_SLIDER_FILL_LEFT,
	KIT_SLIDER_FILL_CENTER,
	KIT_SLIDER_FILL_RIGHT,
	KIT_SLIDER_THUMB,
	KIT_SLIDER_THUMB_HILITE,
	KIT_SLIDER_THUMB_DISABLED,

	KIT_SLIDER_IMAGE_COUNT
};

// PRIVATE DATA ///////////////////////////////////////////////////////////////

static const char *const s_kitSliderImageNames[KIT_SLIDER_IMAGE_COUNT] =
{
	"GOSliderTrackLeft",
	"GOSliderTrackCenter",
	"GOSliderTrackRight",
	"GOSliderFillLeft",
	"GOSliderFillCenter",
	"GOSliderFillRight",
	"GOSliderThumb",
	"GOSliderThumbHilite",
	"GOSliderThumbDisabled"
};

static const Image *s_kitSliderImages[KIT_SLIDER_IMAGE_COUNT];

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

// Look up the kit slider images, caching each one once found; FALSE if any is missing
static Bool findKitSliderImages()
{
	Int i;
	for( i = 0; i < KIT_SLIDER_IMAGE_COUNT; ++i )
	{
		if( s_kitSliderImages[i] == nullptr )
			s_kitSliderImages[i] = TheMappedImageCollection->findImageByName( s_kitSliderImageNames[i] );

		if( s_kitSliderImages[i] == nullptr )
			return FALSE;
	}
	return TRUE;
}

// TheSuperHackers @bugfix arcticdolphin 08/11/2026 TRUE while the mouse holds the slider or its thumb.
static Bool isKitSliderGrabbed( GameWindow *slider )
{
	GameWindow *grab = TheWindowManager->winGetGrabWindow();
	return grab != nullptr && ( grab == slider || grab->winGetParent() == slider );
}

// TheSuperHackers @bugfix arcticdolphin 08/11/2026 The kit thumb is drawn by the slider, so skip the layout's retail thumb hilite image.
static void kitSliderThumbDraw( GameWindow *window, WinInstanceData *instData )
{
}

// Show "<label>: <value>" for the slider under the mouse, or for the slider owning the thumb
static void kitSliderTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	GameWindow *slider = window;
	if( BitIsSet( window->winGetStyle(), GWS_HORZ_SLIDER ) == FALSE && window->winGetParent() != nullptr )
	{
		slider = window->winGetParent();
	}

	SliderData *s = (SliderData *)slider->winGetUserData();
	if( s == nullptr )
	{
		return;
	}

	// TheSuperHackers @bugfix arcticdolphin 08/11/2026 Only the value while dragging, the label and value on hover, shown at once while dragging.
	WinInstanceData *sliderData = slider->winGetInstanceData();
	const Bool grabbed = isKitSliderGrabbed( slider );
	UnicodeString label = sliderData->getTooltipText();
	UnicodeString text;
	if( label.isEmpty() == FALSE && grabbed == FALSE )
	{
		text.format( L"%ls: %d", label.str(), s->position );
	}
	else
	{
		text.format( L"%d", s->position );
	}

	Int delay = sliderData->m_tooltipDelay;
	if( grabbed )
	{
		delay = 0;
	}

	TheMouse->setCursorTooltip( text, delay );
}

// Draw a track, a fill up to the thumb and the thumb from the kit images; FALSE if the kit is absent
static Bool drawKitSliderTrack( GameWindow *window, WinInstanceData *instData,
																const ICoord2D &origin, const ICoord2D &size )
{
	GameWindow *thumb = window->winGetChild();
	if( thumb == nullptr || findKitSliderImages() == FALSE )
		return FALSE;

	// TheSuperHackers @bugfix arcticdolphin 08/11/2026 Stop the thumb child drawing the retail hover arrow over the kit marker.
	if( thumb->winGetDrawFunc() != kitSliderThumbDraw )
		thumb->winSetDrawFunc( kitSliderThumbDraw );

	Bool grabbed = isKitSliderGrabbed( window );

	ICoord2D thumbPos, thumbSize;
	thumb->winGetScreenPosition( &thumbPos.x, &thumbPos.y );
	thumb->winGetSize( &thumbSize.x, &thumbSize.y );
	Int thumbCX = thumbPos.x + thumbSize.x / 2;
	Int thumbCY = thumbPos.y + thumbSize.y / 2;

	Real yScale = INT_TO_REAL( TheDisplay->getHeight() ) / 600.0f;
	Bool enabled = BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED );

	// track spans the travel of the thumb centre
	Int trackHeight = REAL_TO_INT( 8.0f * yScale + 0.5f );
	if( trackHeight < 6 )
		trackHeight = 6;
	Int trackY = thumbCY - trackHeight / 2;
	Int trackStart = origin.x + thumbSize.x / 2;
	Int trackEnd = origin.x + size.x - thumbSize.x / 2;

	if( trackEnd - trackStart >= 1 )
	{
		TheWindowManager->winDrawThreePieceImage( s_kitSliderImages[KIT_SLIDER_TRACK_LEFT],
																							s_kitSliderImages[KIT_SLIDER_TRACK_CENTER],
																							nullptr,
																							s_kitSliderImages[KIT_SLIDER_TRACK_RIGHT],
																							trackStart, trackY, trackEnd - trackStart, trackHeight );
	}

	// fill from the track start to the thumb centre
	Int fillEnd = thumbCX;
	if( fillEnd > trackEnd )
		fillEnd = trackEnd;
	if( enabled && fillEnd - trackStart >= 1 )
	{
		TheWindowManager->winDrawThreePieceImage( s_kitSliderImages[KIT_SLIDER_FILL_LEFT],
																							s_kitSliderImages[KIT_SLIDER_FILL_CENTER],
																							nullptr,
																							s_kitSliderImages[KIT_SLIDER_FILL_RIGHT],
																							trackStart, trackY, fillEnd - trackStart, trackHeight );
	}

	// TheSuperHackers @tweak arcticdolphin 08/11/2026 The marker is a rounded pill 4 units wide and 1.75 track heights tall, centred on the value.
	Int thumbWidth = REAL_TO_INT( 4.0f * yScale + 0.5f );
	if( thumbWidth < 3 )
		thumbWidth = 3;
	Int thumbHeight = (trackHeight * 7 + 2) / 4;
	Int thumbX = thumbCX - thumbWidth / 2;
	Int thumbY = thumbCY - thumbHeight / 2;

	const Image *thumbImage = s_kitSliderImages[KIT_SLIDER_THUMB];
	if( enabled == FALSE )
		thumbImage = s_kitSliderImages[KIT_SLIDER_THUMB_DISABLED];
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) ||
					 BitIsSet( thumb->winGetInstanceData()->getState(), WIN_STATE_HILITED ) ||
					 grabbed )
		thumbImage = s_kitSliderImages[KIT_SLIDER_THUMB_HILITE];

	TheWindowManager->winDrawImage( thumbImage, thumbX, thumbY,
																	thumbX + thumbWidth, thumbY + thumbHeight );

	// value tooltip, unless a menu already set a tooltip callback
	if( window->winGetTooltipFunc() == nullptr )
		window->winSetTooltipFunc( kitSliderTooltip );
	if( thumb->winGetTooltipFunc() == nullptr )
		thumb->winSetTooltipFunc( kitSliderTooltip );

	// TheSuperHackers @bugfix arcticdolphin 08/11/2026 The window manager sets no tooltip while a window is held, so keep the value up while dragging.
	if( grabbed && window->winGetTooltipFunc() == kitSliderTooltip )
		kitSliderTooltip( window, instData, 0 );

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetHorizontalSliderDraw ==============================================
/** Draw colored horizontal slider using standard graphics */
//=============================================================================
void W3DGadgetHorizontalSliderDraw( GameWindow *window, WinInstanceData *instData )
{
	Color backBorder, backColor;
	ICoord2D origin, size, start, end;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// TheSuperHackers @bugfix arcticdolphin 08/11/2026 Sliders without image status get the kit track too.
	if( drawKitSliderTrack( window, instData, origin, size ) )
		return;

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

// W3DGadgetHorizontalSliderImageDraw =========================================
/** Draw horizontal slider with user supplied images */
//=============================================================================
void W3DGadgetHorizontalSliderImageDraw( GameWindow *window,
																				 WinInstanceData *instData )
{
	const Image *fillSquare, *blankSquare, *highlightSquare;
	ICoord2D origin, size, start, end, highlightOffset;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// TheSuperHackers @feature arcticdolphin 08/11/2026 Draw a track and thumb with a value tooltip when the GO kit slider images exist.
	if( drawKitSliderTrack( window, instData, origin, size ) )
		return;

	highlightSquare = GadgetSliderGetHiliteImageLeft( window );
	blankSquare	= GadgetSliderGetDisabledImageRight( window );
	fillSquare = GadgetSliderGetDisabledImageLeft( window );

	SliderData *s = (SliderData *)window->winGetUserData();

#if !defined(GENONLINE_WIDESCREEN)
	Real xMulti = INT_TO_REAL(TheDisplay->getWidth()) / DEFAULT_DISPLAY_WIDTH;
#else
	Real xMulti = INT_TO_REAL(TheDisplay->getWidth()) / GENONLINE_WIDESCREEN_X_SCALE;
#endif

	// figure out how many boxes we draw for this slider
	Int numBoxes = 0;
	Int numSelectedBoxes = 0;
	Int numHighlightBoxes = 0;
	Int boxWidth = fillSquare->getImageWidth()* xMulti;
	Int boxPadding = 2;
	start.x = origin.x;
	end.x	= start.x + boxWidth;
	Real selectedPercent = (s->position - s->minVal)/INT_TO_REAL((s->maxVal - s->minVal));
	Int maxSelectedX = origin.x + REAL_TO_INT(selectedPercent * size.x);
	while(end.x < origin.x + size.x )
	{
		if (start.x <= maxSelectedX && end.x < origin.x + size.x && s->position != s->minVal)
			++numSelectedBoxes;
		start.x = end.x + boxPadding;
		end.x	= start.x + boxWidth;
		++numBoxes;
	}
	numHighlightBoxes = numBoxes + 1;
	Int distanceCovered = end.x - origin.x - boxWidth;
	highlightOffset.x = -(boxWidth + boxPadding)/2;
	highlightOffset.y = boxWidth/3;
	Int blankness = size.x - distanceCovered;
	origin.x += blankness/2;

	Int i;
	if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		ICoord2D backgroundStart, backgroundEnd;
		backgroundStart.y = origin.y + highlightOffset.y;
		backgroundEnd.y = backgroundStart.y + boxWidth + boxPadding;
		for (i=0; i<numHighlightBoxes; ++i)
		{
			backgroundStart.x = origin.x + highlightOffset.x + i*(boxWidth+boxPadding);
			backgroundEnd.x = backgroundStart.x + boxWidth + boxPadding;
			TheWindowManager->winDrawImage( highlightSquare,
																		backgroundStart.x, backgroundStart.y,
																		backgroundEnd.x, backgroundEnd.y );
		}
	}

	start.y = origin.y;
	end.y = start.y + boxWidth;
	for (i=0; i<numSelectedBoxes; ++i)
	{
		start.x = origin.x + i*(boxWidth+boxPadding);
		end.x = start.x + boxWidth;
		TheWindowManager->winDrawImage( fillSquare,
																	start.x, start.y,
																	end.x, end.y );
	}
	for (i=numSelectedBoxes; i<numBoxes; ++i)
	{
		start.x = origin.x + i*(boxWidth+boxPadding);
		end.x = start.x + boxWidth;
		TheWindowManager->winDrawImage( blankSquare,
																	start.x, start.y,
																	end.x, end.y );
	}
}
