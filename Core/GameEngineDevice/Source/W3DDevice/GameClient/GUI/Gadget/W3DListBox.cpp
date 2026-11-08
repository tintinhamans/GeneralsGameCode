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


// FILE: W3DListBox.cpp ///////////////////////////////////////////////////////
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
// File name: W3DListBox.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:      W3D implementation for the list box control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "W3DDevice/GameClient/W3DGadget.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// kit scroll bar draws, defined with the vertical slider and push button draws
extern void W3DGadgetVerticalSliderKitTrackDraw( GameWindow *window, WinInstanceData *instData );
extern void W3DGadgetVerticalSliderKitThumbDraw( GameWindow *window, WinInstanceData *instData );
extern void W3DGadgetPushButtonKitScrollArrowDraw( GameWindow *window, WinInstanceData *instData );

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// TheSuperHackers @feature arcticdolphin 08/11/2026 Give the list scroll bar the kit draws when the GO kit scroll images exist.
static void assignKitScrollBarDraws( ListboxData *list )
{
	// only scroll bars still on their default draws, so custom draws stay
	if( list == nullptr || list->slider == nullptr || list->slider->winGetDrawFunc() != W3DGadgetVerticalSliderDraw )
		return;
	if( GadgetListBoxHasKitScrollBar() == FALSE )
		return;

	list->slider->winSetDrawFunc( W3DGadgetVerticalSliderKitTrackDraw );

	GameWindow *thumb = list->slider->winGetChild();
	if( thumb && thumb->winGetDrawFunc() == W3DGadgetPushButtonImageDraw )
		thumb->winSetDrawFunc( W3DGadgetVerticalSliderKitThumbDraw );
	if( list->upButton && list->upButton->winGetDrawFunc() == W3DGadgetPushButtonImageDraw )
		list->upButton->winSetDrawFunc( W3DGadgetPushButtonKitScrollArrowDraw );
	if( list->downButton && list->downButton->winGetDrawFunc() == W3DGadgetPushButtonImageDraw )
		list->downButton->winSetDrawFunc( W3DGadgetPushButtonKitScrollArrowDraw );
}

// TheSuperHackers @tweak arcticdolphin 08/11/2026 Kit lists with a layout border draw it at the kit line weight.
// Kit frame of a colour drawn list: the combo field border for drop-downs, the kit line for bordered lists, else 0
static Int getKitListFrame( GameWindow *window )
{
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_IMAGE ) )
		return 0;

	Int comboFrame = GadgetListBoxGetKitComboFrame( window );
	if( comboFrame > 0 )
		return comboFrame;

	Color border = GadgetListBoxGetEnabledBorderColor( window );
	if( border == WIN_COLOR_UNDEFINED )
		return 0;

	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( border, &red, &green, &blue, &alpha );
	if( alpha == 0 )
		return 0;

	return GadgetListBoxGetKitScrollLineWidth();
}

// TheSuperHackers @feature arcticdolphin 08/11/2026 Kit combo drop-down lists line up with and frame like their combo field.
// Kit combo drop-down rows start where the combo field text starts, other rows keep their margin; x is already inside the frame
static Int getListTextOffsetX( GameWindow *window )
{
	Int frame = getKitListFrame( window );
	if( frame <= 0 )
		return GameWindowManager::winScaleLayoutSize( TEXT_X_OFFSET );
	if( GadgetListBoxGetKitComboFrame( window ) <= 0 )
		return GameWindowManager::winScaleLayoutSize( TEXT_X_OFFSET ) - frame;

	// the W3DTextEntry margin, then its start for text drawn from the start or edited text
	GameWindow *editBox = GadgetComboBoxGetEditBox( window->winGetParent() );
	EntryData *e = editBox ? (EntryData *)editBox->winGetUserData() : nullptr;
	Int offset = GameWindowManager::winScaleLayoutSize( 5 );
	if( e && e->drawTextFromStart )
		offset += GameWindowManager::winScaleLayoutSize( 5 );
	else
		offset += GameWindowManager::winScaleLayoutSize( 2 );
	return offset - frame;
}

// Draw the combo field art over a kit combo drop-down list as its frame and background
static void drawKitComboFrame( GameWindow *window, Int x, Int y, Int width, Int height )
{
	GameWindow *editBox = GadgetComboBoxGetEditBox( window->winGetParent() );
	if( editBox == nullptr )
		return;

	const Image *left = GadgetTextEntryGetEnabledImageLeft( editBox );
	const Image *center = GadgetTextEntryGetEnabledImageCenter( editBox );
	const Image *smallCenter = GadgetTextEntryGetEnabledImageSmallCenter( editBox );
	const Image *right = GadgetTextEntryGetEnabledImageRight( editBox );
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE &&
			GadgetTextEntryGetDisabledImageLeft( editBox ) && GadgetTextEntryGetDisabledImageCenter( editBox ) &&
			GadgetTextEntryGetDisabledImageRight( editBox ) )
	{
		left = GadgetTextEntryGetDisabledImageLeft( editBox );
		center = GadgetTextEntryGetDisabledImageCenter( editBox );
		smallCenter = GadgetTextEntryGetDisabledImageSmallCenter( editBox );
		right = GadgetTextEntryGetDisabledImageRight( editBox );
	}

	// TheSuperHackers @bugfix arcticdolphin 08/11/2026 An open drop-down hides the controls under it; the kit field fill is translucent.
	TheDisplay->drawFillRect( x, y, width, height, GameMakeColor( 5, 7, 15, 255 ) );
	TheWindowManager->winDrawThreePieceImage( left, center, smallCenter, right, x, y, width, height );
}

// drawHiliteBar ==============================================================
/** Draw image for the hilite bar */
//=============================================================================
static void drawHiliteBar( const Image *left, const Image *right,
													 const Image *center, const Image *smallCenter,
													 Int startX, Int startY,
													 Int endX, Int endY )
{
	ICoord2D barWindowSize;  // end point of bar from window origin
	Int xOffset = 0, yOffset = 0;  // incase we want this functionality later



	barWindowSize.x = endX - startX;
	barWindowSize.y = endY - startY;

	//
	// the bar window size will always be at least big enough to accommodate
	// the left and right ends
	//
	if( barWindowSize.x < left->getImageWidth() + right->getImageWidth() )
		barWindowSize.x = left->getImageWidth() + right->getImageWidth();

	TheWindowManager->winDrawThreePieceImage( left, center, smallCenter, right,
																						 startX + xOffset, startY + yOffset, barWindowSize.x, barWindowSize.y );

}

// drawListBoxText ============================================================
/** Draw the text for a listbox */
//=============================================================================
static void drawListBoxText( GameWindow *window, WinInstanceData *instData,
														 Int x, Int y, Int width, Int height,
														 Bool useImages )
{
	Int drawY;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	Int i;
	Bool selected;
	Int listLineHeight;
	Color textColor;
//	W3DGameWindow *w3dWindow = static_cast<W3DGameWindow *>(window);
	IRegion2D clipRegion;
	ICoord2D start, end;

	//
	// save the clipping information region cause we're going to use it here
	// in drawing the text
	//
//	TheWindowManager->winGetClipRegion( &clipRegion );

	// set clip region to inside the outline box.
//	TheWindowManager->winSetClipRegion( x, y, width, height );

	// set the text clip region to the outline of the listbox
	clipRegion.lo.x = x + 1;
	clipRegion.lo.y = y -3;
	clipRegion.hi.x = x + width - 1;
	clipRegion.hi.y = y + height - 1;

	drawY = y - list->displayPos;
	// TheSuperHackers @tweak arcticdolphin 08/11/2026 Compact kit combo drop-down rows draw their text from above the row by the trimmed blank top.
	const Int textTrim = GadgetListBoxGetKitComboRowTrim( window );

	for( i = 0; ; i++ )
	{

		if( i > 0 )
			if( list->listData[(i - 1)].listHeight >
					(list->displayPos + list->displayHeight) )
				break;

		if( i == list->endPos )
			break;

		if( list->listData[i].listHeight < list->displayPos )
		{
			drawY += (list->listData[i].height + 1);
			continue;
		}

		listLineHeight = list->listData[i].height + 1;
		//textColor =  list->listData[i].textColor;
		selected = FALSE;

		int rowDrawY = drawY;
		ApplyListBoxRowAnimation(window, i, listLineHeight, rowDrawY);

		if (list->multiSelect)
		{
			Int j = 0;

			while( list->selections[j] >= 0 )
			{
				if( i == list->selections[j] )
				{
					selected = TRUE;
					break;
				}

				j++;
			}
		}
		else
		{
			if( i == list->selectPos )
				selected = TRUE;
		}

		// this item is selected, draw the selection color or image
		if( selected )
		{

			if( useImages )
			{
				const Image *left, *right, *center, *smallCenter;

				if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
				{

					left				= GadgetListBoxGetDisabledSelectedItemImageLeft( window );
					right				= GadgetListBoxGetDisabledSelectedItemImageRight( window );
					center			= GadgetListBoxGetDisabledSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetDisabledSelectedItemImageSmallCenter( window );

				}
				else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
				{

					left				= GadgetListBoxGetHiliteSelectedItemImageLeft( window );
					right				= GadgetListBoxGetHiliteSelectedItemImageRight( window );
					center			= GadgetListBoxGetHiliteSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetHiliteSelectedItemImageSmallCenter( window );

				}
				else
				{

					left				= GadgetListBoxGetEnabledSelectedItemImageLeft( window );
					right				= GadgetListBoxGetEnabledSelectedItemImageRight( window );
					center			= GadgetListBoxGetEnabledSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetEnabledSelectedItemImageSmallCenter( window );

				}

				// draw select image across area

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x;
				start.y = rowDrawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( left && right && center && smallCenter )
					drawHiliteBar( left, right, center, smallCenter, start.x + 1, start.y, end.x , end.y );

			}
			else
			{
				Color selectColor = WIN_COLOR_UNDEFINED,
							selectBorder = WIN_COLOR_UNDEFINED;

				if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
				{
					selectColor  = GadgetListBoxGetDisabledSelectedItemColor( window );
					selectBorder = GadgetListBoxGetDisabledSelectedItemBorderColor( window );
				}
				else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
				{
					selectColor  = GadgetListBoxGetHiliteSelectedItemColor( window );
					selectBorder = GadgetListBoxGetHiliteSelectedItemBorderColor( window );
				}
				else
				{
					selectColor  = GadgetListBoxGetEnabledSelectedItemColor( window );
					selectBorder = GadgetListBoxGetEnabledSelectedItemBorderColor( window );
				}

				// draw border

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x;
				start.y = rowDrawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( selectBorder != WIN_COLOR_UNDEFINED )
					TheWindowManager->winOpenRect( selectBorder,
																				 WIN_DRAW_LINE_WIDTH,
																				 start.x, start.y,
																				 end.x, end.y );

				// draw filled inner rect

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x + 1;
				start.y = rowDrawY + 1;
				end.x = start.x + width - 2;
				end.y = start.y + listLineHeight - 2;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( selectColor != WIN_COLOR_UNDEFINED )
					TheWindowManager->winFillRect( selectColor,
																				 WIN_DRAW_LINE_WIDTH,
																				 start.x, start.y,
																				 end.x, end.y );

			}

		}




		Color dropColor = TheWindowManager->winMakeColor( 0, 0, 0, 255 );
		DisplayString *string;
		// TheSuperHackers @tweak arcticdolphin 08/11/2026 The text margin scales with the screen height.
		const Int textOffsetX = getListTextOffsetX( window );

		ListEntryCell *cells = list->listData[i].cell;
		Int columnX = x;
		IRegion2D columnRegion;
		if( cells )
		{
			// loop through all the cells
			for( Int j = 0; j < list->columns; j++ )
			{
				// setup the Clip Region size

				columnRegion.lo.x = columnX;
				columnRegion.lo.y = rowDrawY - textTrim;
				if(list->columns == 1 && list->slider && list->slider->winIsHidden())
					columnRegion.hi.x = columnX + width-3;
				else
					columnRegion.hi.x = columnX + list->columnWidth[j];
				columnRegion.hi.y = rowDrawY + list->listData[i].height;
				if(columnRegion.lo.y < clipRegion.lo.y )
					columnRegion.lo.y = clipRegion.lo.y;
				if( columnRegion.hi.y > clipRegion.hi.y )
					columnRegion.hi.y = clipRegion.hi.y;

				// Display the Text Case;
				if(cells[j].cellType == LISTBOX_TEXT)
				{
					textColor = cells[j].color;
					string = (DisplayString *)cells[j].data;
					if( BitIsSet( window->winGetStatus(), WIN_STATUS_ONE_LINE ) == TRUE )
					{
						string->setWordWrap(0);
						// make sure the font of the text is the same as the windows
						if( string->getFont() != window->winGetFont() )
							string->setFont( window->winGetFont() );

						// draw this text after setting the clip region for it
						string->setClipRegion( &columnRegion );
						string->draw( columnX + textOffsetX,
													rowDrawY - textTrim,
													textColor,
													dropColor );

					}
					else
					{

						// make sure the font of the text is the same as the windows
						if( string->getFont() != window->winGetFont() )
							string->setFont( window->winGetFont() );

						// set clip region and draw
						string->setClipRegion( &columnRegion );
						string->draw( columnX + textOffsetX,
													rowDrawY - textTrim,
													textColor,
													dropColor );
					}
				}
				else if(cells[j].cellType == LISTBOX_IMAGE && cells[j].data)
				{
					Int width, height;
					if (cells[j].width > 0)
						width = cells[j].width;
					else
						width = list->columnWidth[j];
					if(cells[j].height > 0)
						height = cells[j].height;
					else
						height = list->listData[i].height;
					if(j == 0)
						width--;
					Int offsetX,offsetY;
					if(width < list->columnWidth[j])
						offsetX = columnX + ((list->columnWidth[j] - width) / 2);
					else
						offsetX = columnX;
					if(height < list->listData[i].height)
						offsetY = rowDrawY + ((list->listData[i].height - height) / 2);
					else
						offsetY = rowDrawY;

					offsetY++;
					if(offsetX <x+1)
						offsetX = x+1;
					TheDisplay->setClipRegion( &columnRegion );
					TheWindowManager->winDrawImage( (const Image *)cells[j].data,
																offsetX, offsetY,
																offsetX + width, offsetY + height,cells[j].color );

				}
				columnX = columnX + list->columnWidth[j];
			}
		}


		drawY += listLineHeight;
		TheDisplay->enableClipping(FALSE);
	}

//	TheWindowManager->winSetClipRegion( clipRegion.lo.x, clipRegion.lo.y,
//																			clipRegion.hi.x, clipRegion.hi.y );

}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetListBoxDraw =======================================================
/** Draw colored list box using standard graphics */
//=============================================================================
void W3DGadgetListBoxDraw( GameWindow *window, WinInstanceData *instData )
{
	Int width, height, fontHeight, x, y;
	Color background, border, titleColor, titleBorder;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	ICoord2D size;
	DisplayString *title = instData->getTextDisplayString();

	// get window position and size
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &size.x, &size.y );

	// scroll bar parts draw after the list box
	assignKitScrollBarDraws( list );

	// get font height
	fontHeight = TheWindowManager->winFontHeight( instData->getFont() );

	// alias width and height from size
	width = size.x;
	height = size.y;

	// get the right colors
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		background		= GadgetListBoxGetDisabledColor( window );
		border				= GadgetListBoxGetDisabledBorderColor( window );
		titleColor		= window->winGetDisabledTextColor();
		titleBorder		= window->winGetDisabledTextBorderColor();
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		background		= GadgetListBoxGetHiliteColor( window );
		border				= GadgetListBoxGetHiliteBorderColor( window );
		titleColor		= window->winGetHiliteTextColor();
		titleBorder		= window->winGetHiliteTextBorderColor();
	}
	else
	{
		background		= GadgetListBoxGetEnabledColor( window );
		border				= GadgetListBoxGetEnabledBorderColor( window );
		titleColor		= window->winGetEnabledTextColor();
		titleBorder		= window->winGetEnabledTextBorderColor();
	}

	// Draw the title
	if( title && title->getTextLength() )
	{

		// set the font of this text to that of the window if not already
		if( title->getFont() != window->winGetFont() )
			title->setFont( window->winGetFont() );

		// draw the text
		title->draw( x + 1, y, titleColor, titleBorder );

		y += fontHeight + 1;
		height -= fontHeight + 1;

	}

	// TheSuperHackers @feature arcticdolphin 08/11/2026 Kit combo drop-down lists wear the combo field frame, joined to the field.
	const Int comboFrame = GadgetListBoxGetKitComboFrame( window );
	if( comboFrame > 0 )
	{
		drawKitComboFrame( window, x, y, width, height );
		border = WIN_COLOR_UNDEFINED;
		background = WIN_COLOR_UNDEFINED;
	}

	// TheSuperHackers @tweak arcticdolphin 08/11/2026 Other kit lists keep their background and draw their border at the kit line weight.
	const Int listFrame = getKitListFrame( window );
	const Bool kitBorder = comboFrame <= 0 && listFrame > 0;
	if( kitBorder )
		border = WIN_COLOR_UNDEFINED;

	// draw the back border
	if( border != WIN_COLOR_UNDEFINED )
		TheWindowManager->winOpenRect( border, WIN_DRAW_LINE_WIDTH,
																	 x, y, x + width, y + height );

	// draw background
	if( background != WIN_COLOR_UNDEFINED )
		TheWindowManager->winFillRect( background, WIN_DRAW_LINE_WIDTH,
																	 x + 1, y + 1,
																	 x + width - 1, y + height - 1 );

	if( kitBorder )
		GadgetListBoxDrawKitLines( x, y, width, height, TRUE, TRUE, TRUE, TRUE );

	// If ScrollBar was requested ... adjust width.
	if( list->slider  && !list->slider->winIsHidden())
	{
		ICoord2D sliderSize;

		list->slider->winGetSize( &sliderSize.x, &sliderSize.y );
		// TheSuperHackers @tweak arcticdolphin 08/11/2026 Keep the text clear of the scaled scroll bar.
		width -= (sliderSize.x + GameWindowManager::winScaleLayoutSize( 3 ));

	}

	// rows and selection bars stay inside the kit frame
	if( listFrame > 0 )
	{
		x += listFrame;
		width -= listFrame;
		if( list->slider == nullptr || list->slider->winIsHidden() )
			width -= listFrame;
	}

	// draw the text
	drawListBoxText( window, instData, x, y + 4 , width, height-4-listFrame, TRUE );



}

// W3DGadgetListBoxImageDraw ==================================================
/** Draw list box with user supplied images */
//=============================================================================
void W3DGadgetListBoxImageDraw( GameWindow *window, WinInstanceData *instData )
{
	Int width, height, x, y;
	const Image *image;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	ICoord2D size;
	Color titleColor, titleBorder;
	DisplayString *title = instData->getTextDisplayString();

	// get window position and size
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &size.x, &size.y );

	// save off width and height so we can change them
	width = size.x;
	height = size.y;

	// scroll bar parts draw after the list box
	assignKitScrollBarDraws( list );

	// If ScrollBar was requested ... adjust width.
	if( list->slider )
	{
		ICoord2D sliderSize;

		list->slider->winGetSize( &sliderSize.x, &sliderSize.y );
		width -= sliderSize.x;

	}

	// get the image
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		image				= GadgetListBoxGetDisabledImage( window );
		titleColor	= window->winGetDisabledTextColor();
		titleBorder = window->winGetDisabledTextBorderColor();
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		image				= GadgetListBoxGetHiliteImage( window );
		titleColor	= window->winGetHiliteTextColor();
		titleBorder = window->winGetHiliteTextBorderColor();
	}
	else
	{
		image				= GadgetListBoxGetEnabledImage( window );
		titleColor	= window->winGetEnabledTextColor();
		titleBorder = window->winGetEnabledTextBorderColor();
	}

	// draw the back image
	if( image )
	{
		ICoord2D start, end;

		start.x = x + instData->m_imageOffset.x;
		start.y = y + instData->m_imageOffset.y;
		end.x = start.x + width;
		end.y = start.y + height;
		TheWindowManager->winDrawImage( image,
																		start.x, start.y,
																		end.x, end.y );

	}

	// Draw the title
	if( title && title->getTextLength() )
	{

		// set font to font of the window if not already
		if( title->getFont() != window->winGetFont() )
			title->setFont( window->winGetFont() );

		// draw the text
		title->draw( x + 1, y, titleColor, titleBorder );

		y += TheWindowManager->winFontHeight( instData->getFont() );
		height -= TheWindowManager->winFontHeight( instData->getFont() ) + 1;

	}

	// draw the listbox text
	drawListBoxText( window, instData, x, y+4, width, height-4, TRUE );



}
