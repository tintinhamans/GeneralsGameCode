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

// FILE: GameWindowGlobal.cpp /////////////////////////////////////////////////
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
// File name: GameWindowGlobal.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:      These are some global functions that every game using this
//						window system must implement for their current technology
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/Image.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameFont.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

static Int scaledImageWidth( const Image *image, Int height )
{
	Int imageHeight = image->getImageHeight();
	Int width = image->getImageWidth();

	if( imageHeight > 0 && height != imageHeight )
		width = (width * height + imageHeight / 2) / imageHeight;

	if( width < 1 )
		width = 1;

	return width;
}

static void fitImageAspect( const Image *image, Int *startX, Int *startY, Int *endX, Int *endY )
{
	Int imageWidth = image->getImageWidth();
	Int imageHeight = image->getImageHeight();
	Int width = *endX - *startX;
	Int height = *endY - *startY;
	if( imageWidth <= 0 || imageHeight <= 0 || width <= 0 || height <= 0 )
		return;

	if( width * imageHeight > height * imageWidth )
	{
		Int fitWidth = (height * imageWidth + imageHeight / 2) / imageHeight;
		*startX += (width - fitWidth) / 2;
		*endX = *startX + fitWidth;
	}
	else
	{
		Int fitHeight = (width * imageHeight + imageWidth / 2) / imageWidth;
		*startY += (height - fitHeight) / 2;
		*endY = *startY + fitHeight;
	}
}

// GenOnline @feature arcticdolphin 08/11/2026 Kit images keep their border rows at a fixed thickness at any bar height.
// Image pixels the interior band starts inside the border, so it never samples the line.
static const Real BORDER_INTERIOR_INSET = 0.5f;

struct BarBands
{
	Int y;
	Int height;
	Int borderHeight;
	Real layoutScale;
	Real borderScale;
	IRegion2D *outer;
};

static Bool hasBorderRows( const Image *image )
{
	Real border = image->getBorder();

	return border > 0.0f && image->getImageHeight() > 2.0f * (border + BORDER_INTERIOR_INSET);
}

static Int layoutImageWidth( const Image *image, Real layoutScale )
{
	Int width = REAL_TO_INT_FLOOR( image->getImageWidth() * layoutScale + 0.5f );

	if( width < 1 )
		width = 1;

	return width;
}

static Bool intersectRegion( IRegion2D *out, const IRegion2D *region, const IRegion2D *outer )
{
	*out = *region;

	if( outer != nullptr )
	{
		if( outer->lo.x > out->lo.x )
			out->lo.x = outer->lo.x;
		if( outer->lo.y > out->lo.y )
			out->lo.y = outer->lo.y;
		if( outer->hi.x < out->hi.x )
			out->hi.x = outer->hi.x;
		if( outer->hi.y < out->hi.y )
			out->hi.y = outer->hi.y;
	}

	return out->lo.x < out->hi.x && out->lo.y < out->hi.y;
}

static void drawImageInRegion( const Image *image, Int startX, Int startY, Int endX, Int endY,
															 const IRegion2D *region, const IRegion2D *outer, Color color )
{
	IRegion2D clip;

	if( !intersectRegion( &clip, region, outer ) )
		return;

	TheDisplay->setClipRegion( &clip );
	TheDisplay->drawImage( image, startX, startY, endX, endY, color );
}

static void restoreClip( IRegion2D *outer )
{
	if( outer != nullptr )
		TheDisplay->setClipRegion( outer );
	else
		TheDisplay->enableClipping( FALSE );
}

static void drawBarPiece( const Image *image, Int startX, Int endX, const IRegion2D *region,
													const BarBands &bands, Color color )
{
	Int endY = bands.y + bands.height;

	if( bands.borderHeight == 0 )
	{
		if( region != nullptr )
			drawImageInRegion( image, startX, bands.y, endX, endY, region, bands.outer, color );
		else
			TheDisplay->drawImage( image, startX, bands.y, endX, endY, color );
		return;
	}

	Int imageHeight = image->getImageHeight();
	// GenOnline @tweak arcticdolphin 08/11/2026 Border rows fill their band exactly, so every kit outline is the same whole pixel width.
	Int scaledHeight = REAL_TO_INT_FLOOR( imageHeight * bands.borderHeight / image->getBorder() + 0.5f );
	Real inner = image->getBorder() + BORDER_INTERIOR_INSET;
	Real interiorScale = (Real)(bands.height - 2 * bands.borderHeight) / (imageHeight - 2.0f * inner);
	Real interiorTop = bands.y + bands.borderHeight - inner * interiorScale;
	IRegion2D band;

	band.lo.x = region != nullptr ? region->lo.x : startX;
	band.hi.x = region != nullptr ? region->hi.x : endX;

	band.lo.y = bands.y;
	band.hi.y = bands.y + bands.borderHeight;
	drawImageInRegion( image, startX, bands.y, endX, bands.y + scaledHeight, &band, bands.outer, color );

	band.lo.y = bands.y + bands.borderHeight;
	band.hi.y = endY - bands.borderHeight;
	drawImageInRegion( image, startX, REAL_TO_INT_FLOOR( interiorTop + 0.5f ), endX,
										 REAL_TO_INT_FLOOR( interiorTop + imageHeight * interiorScale + 0.5f ), &band, bands.outer, color );

	band.lo.y = endY - bands.borderHeight;
	band.hi.y = endY;
	drawImageInRegion( image, startX, endY - scaledHeight, endX, endY, &band, bands.outer, color );
}

// GenOnline @tweak arcticdolphin 08/11/2026 Single kit images with a border keep it at the kit line weight on every side.
// One axis of a nine-slice: where the image is drawn and the screen span it is clipped to, per slice.
static void nineSliceAxis( Int start, Int end, Int imageSize, Real border, Int lineSize,
													 Int drawStart[3], Int drawEnd[3], Int spanStart[3], Int spanEnd[3] )
{
	Real edgeScale = (Real)lineSize / border;
	Int edgeSize = REAL_TO_INT_FLOOR( imageSize * edgeScale + 0.5f );
	Real inner = border + BORDER_INTERIOR_INSET;
	Real interiorScale = (Real)(end - start - 2 * lineSize) / (imageSize - 2.0f * inner);
	Real interiorStart = start + lineSize - inner * interiorScale;

	drawStart[0] = start;
	drawEnd[0] = start + edgeSize;
	spanStart[0] = start;
	spanEnd[0] = start + lineSize;

	drawStart[1] = REAL_TO_INT_FLOOR( interiorStart + 0.5f );
	drawEnd[1] = REAL_TO_INT_FLOOR( interiorStart + imageSize * interiorScale + 0.5f );
	spanStart[1] = start + lineSize;
	spanEnd[1] = end - lineSize;

	drawStart[2] = end - edgeSize;
	drawEnd[2] = end;
	spanStart[2] = end - lineSize;
	spanEnd[2] = end;
}

static Bool drawBorderedImage( const Image *image, Int startX, Int startY, Int endX, Int endY, Color color )
{
	Real border = image->getBorder();
	if( border <= 0.0f || TheDisplay == nullptr )
		return FALSE;

	Int imageWidth = image->getImageWidth();
	Int imageHeight = image->getImageHeight();
	if( imageWidth <= 2.0f * (border + BORDER_INTERIOR_INSET) || imageHeight <= 2.0f * (border + BORDER_INTERIOR_INSET) )
		return FALSE;

	Int lineSize = REAL_TO_INT_CEIL( border * TheDisplay->getHeight() / 600.0f - 0.01f );
	if( lineSize < 1 )
		lineSize = 1;
	if( endX - startX < 2 * lineSize + 1 || endY - startY < 2 * lineSize + 1 )
		return FALSE;

	IRegion2D callerClip;
	IRegion2D *outer = nullptr;
	if( TheDisplay->isClippingEnabled() )
	{
		if( TheDisplay->getClipRegion( &callerClip ) == FALSE )
			return FALSE;
		outer = &callerClip;
	}

	Int drawX0[3], drawX1[3], spanX0[3], spanX1[3];
	Int drawY0[3], drawY1[3], spanY0[3], spanY1[3];
	nineSliceAxis( startX, endX, imageWidth, border, lineSize, drawX0, drawX1, spanX0, spanX1 );
	nineSliceAxis( startY, endY, imageHeight, border, lineSize, drawY0, drawY1, spanY0, spanY1 );

	Int row, column;
	for( row = 0; row < 3; ++row )
	{
		for( column = 0; column < 3; ++column )
		{
			IRegion2D slice;
			slice.lo.x = spanX0[column];
			slice.hi.x = spanX1[column];
			slice.lo.y = spanY0[row];
			slice.hi.y = spanY1[row];
			drawImageInRegion( image, drawX0[column], drawY0[row], drawX1[column], drawY1[row], &slice, outer, color );
		}
	}

	restoreClip( outer );
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// GameWindowManager::winDrawImage ============================================
/** draw image, coord are in screen and should be kepth within
	* that box specified */
//=============================================================================
void GameWindowManager::winDrawImage( const Image *image, Int startX, Int startY,
																			Int endX, Int endY, Color color )
{

	// GenOnline @bugfix arcticdolphin 08/11/2026 Stop KEEP_ASPECT images such as the logo stretching on wide screens.
	if( image != nullptr && BitIsSet( image->getStatus(), IMAGE_STATUS_KEEP_ASPECT ) )
		fitImageAspect( image, &startX, &startY, &endX, &endY );

	if( image != nullptr && drawBorderedImage( image, startX, startY, endX, endY, color ) )
		return;

	TheDisplay->drawImage( image, startX, startY, endX, endY, color );

}

//=============================================================================
// GenOnline @bugfix arcticdolphin 08/11/2026 Stop end caps and tiles stretching when 800x600 layouts scale to wider screens.
void GameWindowManager::winDrawThreePieceImage( const Image *left, const Image *center,
																								const Image *smallCenter, const Image *right,
																								Int x, Int y, Int width, Int height,
																								Color color )
{

	if( left == nullptr || center == nullptr || right == nullptr )
		return;

	// GenOnline @feature arcticdolphin 08/11/2026 Bars whose pieces all have border rows keep them at a fixed thickness.
	IRegion2D callerClip;
	BarBands bands;
	bands.y = y;
	bands.height = height;
	bands.borderHeight = 0;
	bands.layoutScale = (Real)TheDisplay->getHeight() / 600.0f;
	bands.borderScale = bands.layoutScale;
	bands.outer = nullptr;

	Bool canRestoreClip = TRUE;
	if( TheDisplay->isClippingEnabled() )
	{
		if( TheDisplay->getClipRegion( &callerClip ) )
			bands.outer = &callerClip;
		else
			canRestoreClip = FALSE;
	}

	if( canRestoreClip && hasBorderRows( left ) && hasBorderRows( center ) && hasBorderRows( right ) )
	{
		// ceil keeps the whole line inside the band, so rows and caps show the same thickness
		Int borderHeight = REAL_TO_INT_CEIL( left->getBorder() * bands.layoutScale - 0.01f );
		if( borderHeight < 1 )
			borderHeight = 1;

		if( height >= 2 * borderHeight + 1 )
		{
			bands.borderHeight = borderHeight;
			bands.borderScale = (Real)borderHeight / left->getBorder();
		}
	}

	Bool bordered = bands.borderHeight > 0;
	Int leftWidth = bordered ? layoutImageWidth( left, bands.borderScale ) : scaledImageWidth( left, height );
	Int rightWidth = bordered ? layoutImageWidth( right, bands.borderScale ) : scaledImageWidth( right, height );
	Int endX = x + width;
	Int endY = y + height;

	if( leftWidth + rightWidth >= width )
	{
		Int midX = x + width / 2;
		if( bordered )
		{
			IRegion2D half;
			half.lo.y = y;
			half.hi.y = endY;
			half.lo.x = x;
			half.hi.x = midX;
			drawBarPiece( left, x, x + leftWidth, &half, bands, color );
			half.lo.x = midX;
			half.hi.x = endX;
			drawBarPiece( right, endX - rightWidth, endX, &half, bands, color );
			restoreClip( bands.outer );
			return;
		}
		winDrawImage( left, x, y, midX, endY, color );
		winDrawImage( right, midX, y, endX, endY, color );
		return;
	}

	Int gapStart = x + leftWidth;
	Int gapEnd = endX - rightWidth;
	Int tileWidth = bordered ? layoutImageWidth( center, bands.borderScale ) : scaledImageWidth( center, height );
	Int pieces = (gapEnd - gapStart) / tileWidth;
	Int tileX = gapStart;
	Int i;

	for( i = 0; i < pieces; ++i )
	{
		drawBarPiece( center, tileX, tileX + tileWidth, nullptr, bands, color );
		tileX += tileWidth;
	}

	if( tileX < gapEnd )
	{
		IRegion2D clip;
		clip.lo.x = tileX;
		clip.lo.y = y;
		clip.hi.x = gapEnd;
		clip.hi.y = endY;

		// a small tile without border rows would break the bands, so the clipped center tile fills in
		if( smallCenter != nullptr && bordered && !hasBorderRows( smallCenter ) )
			smallCenter = nullptr;

		if( smallCenter != nullptr )
		{
			Int smallWidth = bordered ? layoutImageWidth( smallCenter, bands.borderScale ) : scaledImageWidth( smallCenter, height );
			while( tileX < gapEnd )
			{
				drawBarPiece( smallCenter, tileX, tileX + smallWidth, &clip, bands, color );
				tileX += smallWidth;
			}
		}
		else
		{
			drawBarPiece( center, tileX, tileX + tileWidth, &clip, bands, color );
		}

		restoreClip( bands.outer );
	}

	drawBarPiece( left, x, gapStart, nullptr, bands, color );
	drawBarPiece( right, gapEnd, endX, nullptr, bands, color );

	if( bordered )
		restoreClip( bands.outer );

}

// GameWindowManager::winFillRect =============================================
/** draw filled rect, coords are absolute screen coords */
//=============================================================================
void GameWindowManager::winFillRect( Color color, Real width,
																		 Int startX, Int startY,
																		 Int endX, Int endY )
{

	TheDisplay->drawFillRect( startX, startY,
														endX - startX, endY - startY,
														color );

}

// GameWindowManager::winOpenRect =============================================
/** draw rect outline, coords are absolute screen coords */
//=============================================================================
void GameWindowManager::winOpenRect( Color color, Real width,
																		 Int startX, Int startY,
																		 Int endX, Int endY )
{

	TheDisplay->drawOpenRect( startX, startY,
														endX - startX, endY - startY,
														width, color );

}

// GameWindowManager::winDrawLine =============================================
/** draw line, coords are absolute screen coords */
//=============================================================================
void GameWindowManager::winDrawLine( Color color, Real width,
																		 Int startX, Int startY,
																		 Int endX, Int endY )
{

	TheDisplay->drawLine( startX, startY, endX, endY, width, color );

}

// GameWindowManager::winFindImage ============================================
/** Given an image name, return an image loc to that image information.
	* You may reorganize the image loc structure however you like to suit
	* your individual project */
//=============================================================================
const Image *GameWindowManager::winFindImage( const char *name )
{

	assert( TheMappedImageCollection );
	if( TheMappedImageCollection )
		return TheMappedImageCollection->findImageByName( name );

	return nullptr;

}

// GameWindowManager::winMakeColor ============================================
/** Given RGBA, make a color, you can change color representation for your
	* individual project needs */
//=============================================================================
Color GameWindowManager::winMakeColor( UnsignedByte red,
																			 UnsignedByte green,
																			 UnsignedByte blue,
																			 UnsignedByte alpha )
{

	return GameMakeColor( red, green, blue, alpha );

}

// GameWindowManager::winFormatText ===========================================
/** draw text to the screen */
//=============================================================================
void GameWindowManager::winFormatText( GameFont *font, UnicodeString text, Color color,
																			 Int x, Int y, Int width, Int height )
{

	/// @todo make all display string rendering go through here!

}

// GameWindowManager::winGetTextSize ==========================================
/** get the extent size of text */
//=============================================================================
void GameWindowManager::winGetTextSize( GameFont *font, UnicodeString text,
																				Int *width, Int *height, Int maxWidth )
{

	/// @todo make display string size stuff go through here

	if( width )
		*width = 0;

	if( height )
		*height = 0;

}

// GameWindowManager::winFontHeight ===========================================
/** Return the font height in pixels */
//=============================================================================
Int GameWindowManager::winFontHeight( GameFont *font )
{

	if (font == nullptr)
		return 0;

	return font->height;

}

// GameWindowManager::winIsDigit ==============================================
/** You implementation of whether or not character is a digit */
//=============================================================================
Int GameWindowManager::winIsDigit( Int c )
{

	return iswdigit( c );

}

// GameWindowManager::winIsAscii ==============================================
/** You implementation of whether or not character is ascii */
//=============================================================================
Int GameWindowManager::winIsAscii( Int c )
{

	return iswascii( c );

}

// GameWindowManager::winIsAlNum ==============================================
/** Your implementation of whether or not character is alpha numeric */
//=============================================================================
Int GameWindowManager::winIsAlNum( Int c )
{

	return iswalnum( c );

}

// GameWindowManager::winFindFont =============================================
/** Get a font */
//=============================================================================
GameFont *GameWindowManager::winFindFont( AsciiString fontName,
																					Int pointSize,
																					Bool bold )
{

	assert( TheFontLibrary );
	if( TheFontLibrary )
		return TheFontLibrary->getFont( fontName, pointSize, bold );

	return nullptr;

}

