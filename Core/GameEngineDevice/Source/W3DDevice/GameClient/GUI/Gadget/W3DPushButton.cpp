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


// FILE: W3DPushButton.cpp ////////////////////////////////////////////////////
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
// File name: W3DPushButton.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:			W3D implementation for the push button control element
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/Gadget.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/Display.h"
// TheSuperHackers @feature for the command bar hotkey overlay
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/HotKey.h"
#include "W3DDevice/GameClient/W3DGameWindow.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DGadget.h"




// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

void W3DGadgetPushButtonImageDrawThree(GameWindow *window, WinInstanceData *instData );
void W3DGadgetPushButtonImageDrawOne(GameWindow *window, WinInstanceData *instData );

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

// TheSuperHackers @feature Countdown text over a cameo (Options.ini: BuildTimerDisplayMode).
// TheSuperHackers @feature The countdown's shared string and the per letter hotkey overlay
// cache stay registered with TheDisplayStringManager between frames, so they must be handed
// back before the manager is torn down - its destructor asserts on any string left behind.
// The W3DDisplayStringManager destructor calls this.
static DisplayString *s_countdownString = nullptr;
static Int s_countdownLastSeconds = -1;
static Int s_countdownLastMode = -1;
static DisplayString *s_letterStrings[ HotKeyManager::CORNER_CODES ] = { nullptr };
static DisplayString *s_autoQueueString = nullptr;

// the plate behind a count badge or corner letter over a cameo
static const Color CAMEO_PLATE_COLOR = GameMakeColor( 0, 0, 0, 160 );

void W3DGadgetPushButtonFreeDisplayStrings( void )
{
	if( s_countdownString != nullptr && TheDisplayStringManager != nullptr )
		TheDisplayStringManager->freeDisplayString( s_countdownString );
	s_countdownString = nullptr;
	s_countdownLastSeconds = -1;
	s_countdownLastMode = -1;

	if( s_autoQueueString != nullptr && TheDisplayStringManager != nullptr )
	{
		TheDisplayStringManager->freeDisplayString( s_autoQueueString );
	}
	s_autoQueueString = nullptr;

	for( Int i = 0; i < ARRAY_SIZE( s_letterStrings ); ++i )
	{
		if( s_letterStrings[ i ] != nullptr && TheDisplayStringManager != nullptr )
			TheDisplayStringManager->freeDisplayString( s_letterStrings[ i ] );
		s_letterStrings[ i ] = nullptr;
	}
}

// drawTextPlate ==============================================================
/** A translucent plate one pixel larger than the text it sits behind. */
//=============================================================================
static void drawTextPlate( Int textX, Int textY, Int width, Int height, Color color )
{
	const Int pad = 1;
	TheDisplay->drawFillRect( textX - pad, textY - pad, width + pad * 2, height + pad * 2, color );
}

// drawButtonCountdown ========================================================
/** Draw the remaining time for whatever this button is counting down -- a queued unit or
	* upgrade, or a special power recharging. Drawn after the clock sweep so the darkening
	* does not swallow the digits. */
//=============================================================================
static void drawButtonCountdown( GameWindow *window, Int seconds )
{
	if( seconds < 0 )
		return;

	if( !TheGlobalData || TheGlobalData->m_buildTimerDisplayMode == BuildTimerDisplayMode_None )
		return;

	if( TheDisplayStringManager == nullptr )
		return;

	if( s_countdownString == nullptr )
	{
		s_countdownString = TheDisplayStringManager->newDisplayString();
		if( s_countdownString == nullptr )
			return;

		Int pointSize = 10;
		if( TheGlobalLanguageData )
			pointSize = TheGlobalLanguageData->adjustFontSize( pointSize );
		s_countdownString->setFont( TheFontLibrary->getFont( AsciiString( "Arial" ), pointSize, TRUE ) );
	}

	// only rebuild the sentence when the displayed value actually changes, which at one
	// tick per second is far less often than we are drawn
	if( s_countdownLastSeconds != seconds || s_countdownLastMode != TheGlobalData->m_buildTimerDisplayMode )
	{
		UnicodeString text;
		if( TheGlobalData->m_buildTimerDisplayMode == BuildTimerDisplayMode_Auto && seconds >= 60 )
			text.format( L"%d:%2.2d", seconds / 60, seconds % 60 );
		else
			text.format( L"%d", seconds );

		s_countdownString->setText( text );
		s_countdownLastSeconds = seconds;
		s_countdownLastMode = TheGlobalData->m_buildTimerDisplayMode;
	}

	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	Int width, height;
	s_countdownString->getSize( &width, &height );

	// centered along the bottom, clear of the hotkey badge in the top left
	const Int textX = origin.x + (size.x / 2) - (width / 2);
	const Int textY = origin.y + size.y - height - 2;

	drawTextPlate( textX, textY, width, height, GameMakeColor( 0, 0, 0, 128 ) );

	s_countdownString->draw( textX, textY,
		GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

// drawButtonBar ==============================================================
/** A thin bar stacked up from the bottom of a cameo, row 0 lowest, with a divider between
	* segments when there is more than one. */
//=============================================================================
static void drawButtonBar( GameWindow *window, Int row, Real ratio, Int segments, Color frameColor, Color fillColor )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	const Int inset = 2;
	const Int frameHeight = 5;
	const Int barX = origin.x + inset;
	const Int barY = origin.y + size.y - inset - frameHeight * ( row + 1 );
	const Int innerWidth = size.x - inset * 2 - 2;
	if( innerWidth <= 0 )
		return;

	TheDisplay->beginBatch();
	TheDisplay->drawOpenRect( barX, barY, innerWidth + 2, frameHeight, 1.0f, frameColor );
	TheDisplay->drawFillRect( barX + 1, barY + 1, innerWidth * ratio, frameHeight - 2, fillColor );
	for( Int i = 1; i < segments; i++ )
	{
		TheDisplay->drawFillRect( barX + 1 + innerWidth * i / segments, barY + 1, 1, frameHeight - 2, frameColor );
	}
	TheDisplay->endBatch();
}

// drawButtonHealthBar ========================================================
/** TheSuperHackers @feature Health along the bottom of a cameo, green through yellow to red
	* like the in world bar. */
//=============================================================================
static void drawButtonHealthBar( GameWindow *window, Real ratio )
{
	Real red, green;
	if( ratio >= 0.5f )
	{
		red = 1.0f - ( ratio - 0.5f ) / 0.5f;
		green = 1.0f;
	}
	else
	{
		red = 1.0f;
		green = ratio / 0.5f;
	}

	drawButtonBar( window, 0, ratio, 1,
		GameMakeColor( red * 128, green * 128, 0, 255 ), GameMakeColor( red * 255, green * 255, 0, 255 ) );
}

// drawButtonAmmoBar ==========================================================
/** A light orange clip bar above the health bar. */
//=============================================================================
static void drawButtonAmmoBar( GameWindow *window, Int ammoInClip, Int clipSize )
{
	drawButtonBar( window, 1, (Real)ammoInClip / (Real)clipSize, clipSize <= 6 ? clipSize : 1,
		GameMakeColor( 128, 88, 40, 255 ), GameMakeColor( 255, 176, 80, 255 ) );
}

// drawButtonCornerLetter =====================================================
/** TheSuperHackers @feature One letter in the top left of a cameo, drawn for the hotkey overlay
	* and for a caller's own corner letter. */
//=============================================================================
static void drawButtonCornerLetter( GameWindow *window, Int index, GameFont *font, Bool plate, Color plateColor, Color textColor )
{
	if( TheDisplayStringManager == nullptr || font == nullptr )
		return;

	if( index <= 0 || index >= HotKeyManager::CORNER_CODES )
	{
		return;
	}

	// One display string per letter, so that drawing many cameos in a row does not
	// rebuild sentence geometry over and over. There are only ever a handful of
	// distinct letters on screen, so this stays small.
	DisplayString *letterString = s_letterStrings[ index ];

	if( letterString == nullptr )
	{
		letterString = TheDisplayStringManager->newDisplayString();
		if( letterString == nullptr )
			return;

		// the manager stores keys lowercased, but shortcuts read better as capitals
		UnicodeString text;
		const UnsignedByte key = (UnsignedByte)( index & 0xFF );
		const char *mouseText = HotKeyManager::cornerText( (Char)key );
		if( mouseText )
		{
			text.translate( AsciiString( mouseText ) );
		}
		else
		{
			text.concat( (WideChar)toupper( (Int)key ) );
		}
		UnicodeString label;
		if( index & HotKeyManager::CTRL_CORNER )
		{
			label.concat( L'!' );
		}
		if( index & HotKeyManager::ALT_CORNER )
		{
			label.concat( L'#' );
		}
		if( index & HotKeyManager::SHIFT_CORNER )
		{
			label.concat( L'^' );
		}
		label.concat( text );
		letterString->setText( label );

		s_letterStrings[ index ] = letterString;
	}
	if( letterString->getFont() != font )
		letterString->setFont( font );

	ICoord2D origin;
	window->winGetScreenPosition( &origin.x, &origin.y );

	// tuck it into the top left of the cameo, where no existing decoration lives
	const Int inset = 2;
	const Int textX = origin.x + inset;
	const Int textY = origin.y + inset;

	// Optional plate behind the letter, so it stays readable over busy cameo art.
	if( plate )
	{
		Int width, height;
		letterString->getSize( &width, &height );

		drawTextPlate( textX, textY, width, height, plateColor );
	}

	letterString->draw( textX, textY, textColor, GameMakeColor( 0, 0, 0, 255 ) );
}

// TheSuperHackers @feature Command bar hotkey overlay (Options.ini: KeyboardOverlay).
// drawButtonHotKeyOverlay ====================================================
/** Draw the keyboard hotkey letter over a command bar cameo, so the player can
	* learn the shortcuts without hunting through tooltips.
	*
	* The letter comes from what actually got registered in the hotkey manager, not
	* from the button's label -- colliding hotkeys are dropped at registration, and
	* drawing those would advertise a key that does nothing.
	* A button's own corner letter, such as a power's SPECIAL_POWER key, takes its place. */
//=============================================================================
static void drawButtonHotKeyOverlay( GameWindow *window, Int ownCorner )
{
	if( !TheGlobalData || !TheGlobalData->m_keyboardOverlayEnabled )
		return;

	if( TheHotKeyManager == nullptr )
		return;

	// only cameo style buttons opt into overlay states, so this leaves menu buttons alone
	if( !BitIsSet( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
		return;

	Int corner = ownCorner;
	if( corner == 0 )
	{
		AsciiString hotKey = TheHotKeyManager->getHotKeyForWindow( window );
		if( hotKey.isEmpty() )
			return;

		// Only a single byte printable key or a mouse key can be shown faithfully. A localized mnemonic outside
		// ASCII arrives as a multi byte sequence here, and drawing its first byte would show a
		// wrong or garbled shortcut - better to draw nothing for those.
		corner = (UnsignedByte)HotKeyManager::cornerOf( hotKey );
		if( corner == 0 )
			return;
	}

	Int pointSize = 10;
	if( TheGlobalLanguageData )
		pointSize = TheGlobalLanguageData->adjustFontSize( pointSize );
	GameFont *font = TheFontLibrary->getFont( AsciiString( "Arial" ), pointSize, TRUE );

	drawButtonCornerLetter( window, corner, font, TheGlobalData->m_keyboardOverlayBackdrop,
		TheGlobalData->m_keyboardOverlayBackdropColor, TheGlobalData->m_keyboardOverlayColor );
}

// drawButtonAutoQueue ========================================================
/** A green frame and an AUTO tag on the cameo of the unit its producer auto queues. */
//=============================================================================
static void drawButtonAutoQueue( GameWindow *window )
{
	if( TheDisplayStringManager == nullptr )
	{
		return;
	}

	if( s_autoQueueString == nullptr )
	{
		s_autoQueueString = TheDisplayStringManager->newDisplayString();
		if( s_autoQueueString == nullptr )
		{
			return;
		}

		s_autoQueueString->setText( L"AUTO" );
	}

	// the size follows the resolution, which can change between matches
	Int pointSize = 8;
	if( TheGlobalLanguageData )
	{
		pointSize = TheGlobalLanguageData->adjustFontSize( pointSize );
	}
	GameFont *font = TheFontLibrary->getFont( AsciiString( "Arial" ), pointSize, TRUE );
	if( s_autoQueueString->getFont() != font )
	{
		s_autoQueueString->setFont( font );
	}

	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	const Color green = GameMakeColor( 80, 255, 80, 255 );
	TheDisplay->drawOpenRect( origin.x + 1, origin.y + 1, size.x - 2, size.y - 2, 2.0f, green );

	Int width, height;
	s_autoQueueString->getSize( &width, &height );

	const Int textX = origin.x + size.x - width - 3;
	const Int textY = origin.y + size.y - height - 3;

	drawTextPlate( textX, textY, width, height, CAMEO_PLATE_COLOR );
	s_autoQueueString->draw( textX, textY, green, GameMakeColor( 0, 0, 0, 255 ) );
}

// drawButtonText =============================================================
/** Draw button text to the screen */
//=============================================================================
static void drawButtonText( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size, textPos;
	Int width, height;
	Color textColor, dropColor;
	DisplayString *text = instData->getTextDisplayString();

	// sanity
	if( text == nullptr || text->getTextLength() == 0 )
		return;

	// get window position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// set whether or not we center the wrapped text
	text->setWordWrapCentered( BitIsSet( instData->getStatus(), WIN_STATUS_WRAP_CENTERED ));
	text->setWordWrap(size.x);
	// get the right text color
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		textColor = window->winGetDisabledTextColor();
		dropColor = window->winGetDisabledTextBorderColor();
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		textColor = window->winGetHiliteTextColor();
		dropColor = window->winGetHiliteTextBorderColor();
	}
	else
	{
		textColor = window->winGetEnabledTextColor();
		dropColor = window->winGetEnabledTextBorderColor();
	}

	// set our font to that of our parent if not the same
	if( text->getFont() != window->winGetFont() )
		text->setFont( window->winGetFont() );

	// get text size
	text->getSize( &width, &height );

	// where to draw
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_COUNT_BADGE ) )
	{
		// TheSuperHackers @feature A count over a cameo sits bottom right on a translucent plate
		textPos.x = origin.x + size.x - width - 2;
		textPos.y = origin.y + size.y - height - 1;
		drawTextPlate( textPos.x, textPos.y, width, height, CAMEO_PLATE_COLOR );
	}
	else if( BitIsSet( window->winGetStatus(), WIN_STATUS_COUNT_FOOTER ) )
	{
		// TheSuperHackers @feature A count under a framed icon sits bottom centre, clear of the frame
		textPos.x = origin.x + (size.x / 2) - (width / 2);
		textPos.y = origin.y + size.y - height - MAX( size.y / 32, 1 );
	}
	else if( BitIsSet( window->winGetStatus(), WIN_STATUS_SHORTCUT_BUTTON ) )
	{
		// Oh god... this is a total hack for shortcut buttons to handle rendering text top left corner...
		textPos.x = origin.x + 2;
		textPos.y = origin.y + 0;
	}
	else
	{
		textPos.x = origin.x + (size.x / 2) - (width / 2);
		textPos.y = origin.y + (size.y / 2) - (height / 2);
	}

	// draw it
	text->draw( textPos.x, textPos.y, textColor, dropColor );

}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetPushButtonDraw ====================================================
/** Draw colored pushbutton using standard graphics */
//=============================================================================
void W3DGadgetPushButtonDraw( GameWindow *window, WinInstanceData *instData )
{
	Color color, border;
	ICoord2D origin, size, start, end;

	// get window position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetDisabledSelectedColor( window );
			border		= GadgetButtonGetDisabledSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetDisabledColor( window );
			border		= GadgetButtonGetDisabledBorderColor( window );
		}

	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetHiliteSelectedColor( window );
			border		= GadgetButtonGetHiliteSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetHiliteColor( window );
			border		= GadgetButtonGetHiliteBorderColor( window );
		}

	}
	else
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetEnabledSelectedColor( window );
			border		= GadgetButtonGetEnabledSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetEnabledColor( window );
			border		= GadgetButtonGetEnabledBorderColor( window );
		}

	}

	// compute draw position
	start.x = origin.x;
	start.y = origin.y;
	end.x = start.x + size.x;
	end.y = start.y + size.y;

	// box and border
	if( border != WIN_COLOR_UNDEFINED )
	{

		TheWindowManager->winOpenRect( border, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}

	if( color != WIN_COLOR_UNDEFINED )
	{

		// draw inside border
		start.x++;
		start.y++;
		end.x--;
		end.y--;
		TheWindowManager->winFillRect( color, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}

	// draw the button text
	if( instData->getTextLength() )
		drawButtonText( window, instData );

	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
	}

	PushButtonData *pData = (PushButtonData *)window->winGetUserData();
	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
		}

		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(origin.x, origin.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( origin.x, origin.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			pData->drawClock = NO_CLOCK;
			window->winSetUserData(pData);
		}

		// TheSuperHackers @feature Countdown text, after the clock so the darkening does not
		// swallow the digits. One shot, like the clock above.
		if( pData->countdownSeconds >= 0 )
		{
			drawButtonCountdown( window, pData->countdownSeconds );
			pData->countdownSeconds = -1;
			window->winSetUserData(pData);
		}

		if( pData->healthRatio >= 0.0f )
		{
			drawButtonHealthBar( window, pData->healthRatio );
			pData->healthRatio = -1.0f;
			window->winSetUserData(pData);
		}

		if( pData->ammoClipSize > 0 )
		{
			drawButtonAmmoBar( window, pData->ammoInClip, pData->ammoClipSize );
			pData->ammoClipSize = 0;
			window->winSetUserData(pData);
		}

		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{
			TheDisplay->drawOpenRect(origin.x -1, origin.y - 1, size.x + 2, size.y + 2,1 , pData->colorBorder);
		}
	}

}




// W3DGadgetPushButtonImageDraw ===============================================
/** Draw pushbutton with user supplied images */
//=============================================================================
void W3DGadgetPushButtonImageDraw( GameWindow *window,
																	 WinInstanceData *instData )
{
	// if we return nullptr then we'll call the one picture drawing code, if we return a value
	// then we'll call the 3 picture drawing code
	if( GadgetButtonGetMiddleEnabledImage( window ) )
	{
		if( BitIsSet( instData->getState(), WIN_STATUS_USE_OVERLAY_STATES ) )
		{
			ICoord2D size, start;
			// get window position
			window->winGetScreenPosition( &start.x, &start.y );
			window->winGetSize( &size.x, &size.y );
			// offset position by image offset
			start.x += instData->m_imageOffset.x;
			start.y += instData->m_imageOffset.y;

			DEBUG_CRASH( ("Button at %d,%d is attempting to render with W3DGadgetPushButtonImageDrawThree(), but is using overlay states! Forcing the code to use W3DGadgetPushButtonImageDrawOne() instead.", start.x, start.y ) );
			W3DGadgetPushButtonImageDrawOne( window, instData );
		}
		else
		{
			W3DGadgetPushButtonImageDrawThree( window, instData );
		}
	}
	else
	{
		W3DGadgetPushButtonImageDrawOne( window, instData );
	}
}

void W3DGadgetPushButtonImageDrawOne( GameWindow *window,
																	 WinInstanceData *instData )
{
	const Image *image = nullptr;
	ICoord2D size, start, end;

	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	image = GadgetButtonGetEnabledImage( window );

	if( !BitIsSet( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
	{
		//Certain buttons have the option to specify specific images for
		//altered states. If they do, then we won't render the auto-overlay versions.
		if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
		{

			if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetDisabledSelectedImage( window );
			else
				image			= GadgetButtonGetDisabledImage( window );

		}
		else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
		{

			if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetHiliteSelectedImage( window );
			else
				image			= GadgetButtonGetHiliteImage( window );

		}
		else
		{

			if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetHiliteSelectedImage( window );
		}
	}


	// draw the image
	if( image )
	{

		// get window position
		window->winGetScreenPosition( &start.x, &start.y );
		window->winGetSize( &size.x, &size.y );


		// offset position by image offset
		start.x += instData->m_imageOffset.x;
		start.y += instData->m_imageOffset.y;

		// find end point
		end.x = start.x + size.x;
		end.y = start.y + size.y;

		Display::DrawImageMode	drawMode=Display::DRAW_IMAGE_ALPHA;
		Int colorMultiplier = 0xffffffff;

		if(BitIsSet( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
		{
			//we're using a new drawing system which does "grayscale" disabled buttons using original color artwork.
			if( !BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) )
			{
				if( !BitIsSet( window->winGetStatus(), WIN_STATUS_NOT_READY ) )
				{
					//The button is disabled -- but if the button isn't "ready", we don't want to do this because
					//we want to show the button in color with just the clock overlay.
					if( !BitIsSet( window->winGetStatus(), WIN_STATUS_ALWAYS_COLOR ) )
					{
						drawMode=Display::DRAW_IMAGE_GRAYSCALE;
					}
					else
					{
						colorMultiplier = 0xff909090; //RGB values are 144/255 (90) -- Alpha is opaque (ff) --> ff909090;
					}
				}
			}
		}
		TheDisplay->drawImage( image, start.x, start.y, end.x, end.y, colorMultiplier, drawMode );
	}

	// draw the button text
	if( instData->getTextLength() )
		drawButtonText( window, instData );

	// get window position
	window->winGetScreenPosition( &start.x, &start.y );
	window->winGetSize( &size.x, &size.y );


	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, start.x, start.y, start.x + size.x, start.y + size.y );
	}
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();

	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, start.x, start.y, start.x + size.x, start.y + size.y );
		}

		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			pData->drawClock = NO_CLOCK;
			window->winSetUserData(pData);
		}

		// TheSuperHackers @feature Countdown text, after the clock so the darkening does not
		// swallow the digits. One shot, like the clock above.
		if( pData->countdownSeconds >= 0 )
		{
			drawButtonCountdown( window, pData->countdownSeconds );
			pData->countdownSeconds = -1;
			window->winSetUserData(pData);
		}

		if( pData->healthRatio >= 0.0f )
		{
			drawButtonHealthBar( window, pData->healthRatio );
			pData->healthRatio = -1.0f;
			window->winSetUserData(pData);
		}

		if( pData->ammoClipSize > 0 )
		{
			drawButtonAmmoBar( window, pData->ammoInClip, pData->ammoClipSize );
			pData->ammoClipSize = 0;
			window->winSetUserData(pData);
		}

		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{

			TheDisplay->drawOpenRect(start.x - 1, start.y - 1, size.x + 2, size.y + 2, 1, pData->colorBorder);

		}
	}

	//Now render overlays that pertain to the correct state.

	if( BitIsSet( window->winGetStatus(), WIN_STATUS_FLASHING ) )
	{
		//Handle cameo flashing (let the flashing stack with overlay states)
		static const Image *hilitedOverlayIcon = TheMappedImageCollection->findImageByName( "Cameo_push" );
		TheDisplay->drawImage( hilitedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
	}

	if( BitIsSet( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
	{
		image = nullptr;
		static const Image *pushedOverlayIcon	= TheMappedImageCollection->findImageByName( "Cameo_push" );
		static const Image *hilitedOverlayIcon = TheMappedImageCollection->findImageByName( "Cameo_hilited" );
		if( pushedOverlayIcon && hilitedOverlayIcon )
		{
			if(BitIsSet(window->winGetStatus(), WIN_STATUS_ENABLED))
			{
				if (BitIsSet( instData->getState(), WIN_STATE_HILITED ))
				{
					if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
					{
						//The button is hilited and pushed
						TheDisplay->drawImage( pushedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
					}
					else
					{
						//The button is hilited
						TheDisplay->drawImage( hilitedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
					}
				}
  			else if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
 				{
 					//The button appears to be pushed -- CHECK_LIKE buttons that are on.
 					TheDisplay->drawImage( pushedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
  			}
			}
		}
	}

	// TheSuperHackers @feature Draw the corner letter last, so it stays readable on top of the
	// hilite and pushed overlays. A caller's own letter takes the corner over the hotkey; a cameo
	// draws it in the overlay style, any other button in its own font to scale with the badge text.
	if( pData && pData->cornerLetter != 0 && !BitIsSet( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
	{
		drawButtonCornerLetter( window, pData->cornerLetter, window->winGetFont(), TRUE, CAMEO_PLATE_COLOR, GameMakeColor( 255, 255, 255, 255 ) );
	}
	else
	{
		drawButtonHotKeyOverlay( window, pData ? pData->cornerLetter : 0 );
	}

	if( pData && pData->autoQueue )
	{
		drawButtonAutoQueue( window );
		pData->autoQueue = FALSE;
	}
}


void W3DGadgetPushButtonImageDrawThree(GameWindow *window, WinInstanceData *instData )
{

	const Image *leftImage, *rightImage, *centerImage;
	ICoord2D origin, size, start, end;
	Int xOffset, yOffset;
	Int i;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get image offset
	xOffset = instData->m_imageOffset.x;
	yOffset = instData->m_imageOffset.y;


	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftDisabledSelectedImage( window );
			rightImage				= GadgetButtonGetRightDisabledSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleDisabledSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftDisabledImage( window );
			rightImage				= GadgetButtonGetRightDisabledImage( window );
			centerImage				= GadgetButtonGetMiddleDisabledImage( window );

		}

	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftHiliteSelectedImage( window );
			rightImage				= GadgetButtonGetRightHiliteSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleHiliteSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftHiliteImage( window );
			rightImage				= GadgetButtonGetRightHiliteImage( window );
			centerImage				= GadgetButtonGetMiddleHiliteImage( window );

		}

	}
	else
	{

		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftEnabledSelectedImage( window );
			rightImage				= GadgetButtonGetRightEnabledSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleEnabledSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftEnabledImage( window );
			rightImage				= GadgetButtonGetRightEnabledImage( window );
			centerImage				= GadgetButtonGetMiddleEnabledImage( window );

		}

	}

	// sanity, we need to have these images to make it look right
	if( leftImage == nullptr || rightImage == nullptr ||
			centerImage == nullptr )
		return;

	// get image sizes for the ends
	ICoord2D leftSize, rightSize;
	leftSize.x = leftImage->getImageWidth();
	leftSize.y = leftImage->getImageHeight();
	rightSize.x = rightImage->getImageWidth();
	rightSize.y = rightImage->getImageHeight();

	// get two key points used in the end drawing
	ICoord2D leftEnd, rightStart;
	leftEnd.x = origin.x + leftSize.x + xOffset;
	leftEnd.y = origin.y + size.y + yOffset;
	rightStart.x = origin.x + size.x - rightSize.x + xOffset;
	rightStart.y = origin.y + yOffset;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = rightStart.x - leftEnd.x;

	if( centerWidth <= 0)
	{
		// draw left end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end.y = leftEnd.y;
		end.x = origin.x + xOffset + size.x/2;
		TheWindowManager->winDrawImage(leftImage, start.x, start.y, end.x, end.y);

		// draw right end
		start.y = rightStart.y;
		start.x = end.x;
		end.x = origin.x + size.x;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(rightImage, start.x, start.y, end.x, end.y);
	}
	else
	{

		// how many whole repeating pieces will fit in that width
		pieces = centerWidth / centerImage->getImageWidth();

		// draw the pieces
		start.x = leftEnd.x;
		start.y = origin.y + yOffset;
		end.y = start.y + size.y + yOffset; //centerImage->getImageHeight() + yOffset;
		for( i = 0; i < pieces; i++ )
		{

			end.x = start.x + centerImage->getImageWidth();
			TheWindowManager->winDrawImage( centerImage,
																			start.x, start.y,
																			end.x, end.y );
			start.x += centerImage->getImageWidth();

		}

		// we will draw the image but clip the parts we don't want to show
		IRegion2D reg;
		reg.lo.x = start.x;
		reg.lo.y = start.y;
		reg.hi.x = rightStart.x;
		reg.hi.y = end.y;
		centerWidth = rightStart.x - start.x;
		if( centerWidth > 0)
		{
			TheDisplay->setClipRegion(&reg);
			end.x = start.x + centerImage->getImageWidth();
			TheWindowManager->winDrawImage( centerImage,
																			start.x, start.y,
																			end.x, end.y );
			TheDisplay->enableClipping(FALSE);
		}

		// draw left end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end = leftEnd;
		TheWindowManager->winDrawImage(leftImage, start.x, start.y, end.x, end.y);

		// draw right end
		start = rightStart;
		end.x = start.x + rightSize.x;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(rightImage, start.x, start.y, end.x, end.y);
	}

	// draw the button text
	if( instData->getTextLength() )
		drawButtonText( window, instData );

	// get window position
	window->winGetScreenPosition( &start.x, &start.y );
	window->winGetSize( &size.x, &size.y );


	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, start.x, start.y, start.x + size.x, start.y + size.y );
	}
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();

	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
		}

		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			pData->drawClock = NO_CLOCK;
			window->winSetUserData(pData);
		}

		// TheSuperHackers @feature Countdown text, after the clock so the darkening does not
		// swallow the digits. One shot, like the clock above.
		if( pData->countdownSeconds >= 0 )
		{
			drawButtonCountdown( window, pData->countdownSeconds );
			pData->countdownSeconds = -1;
			window->winSetUserData(pData);
		}

		if( pData->healthRatio >= 0.0f )
		{
			drawButtonHealthBar( window, pData->healthRatio );
			pData->healthRatio = -1.0f;
			window->winSetUserData(pData);
		}

		if( pData->ammoClipSize > 0 )
		{
			drawButtonAmmoBar( window, pData->ammoInClip, pData->ammoClipSize );
			pData->ammoClipSize = 0;
			window->winSetUserData(pData);
		}

		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{
			TheDisplay->drawOpenRect(start.x - 1, start.y - 1, size.x + 2, size.y + 2, 1, pData->colorBorder);
		}
	}
}
