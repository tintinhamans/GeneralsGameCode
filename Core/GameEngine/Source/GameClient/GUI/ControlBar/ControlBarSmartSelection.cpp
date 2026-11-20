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

// TheSuperHackers @feature Smart selection (Options.ini: SmartSelection). A row of small
// cameos above the command bar. A mixed selection gets one cameo per type with a count; a
// selection of one type gets one cameo per object. A cameo for a single object shows its health
// bar. Left click and Tab focus a cameo, so the whole group stays selected but the bar shows the
// command set of that cameo's type, or of its one object, instead of the group's common subset.
// Right click clears the focus on the focused cameo, and drops the cameo's units from the
// selection on any other. Double click (or Ctrl+Shift click with SmartSelectionUseMouse = No)
// keeps only them.
//
// The row is built in code rather than from ControlBar.wnd, which ships in the game data.
// The container is a top level window because the hit test only descends into a top level
// window that contains the point, so a child sitting above its parent's rect is never found.
// It is SEE_THRU so the gaps between cameos still reach the world.
//
// The cameos copy the command buttons' font and text colors from the layout, and wear the active
// control bar scheme's build border. Under a Pro scheme the bar's own frame rises around the row:
// one housing, cut from the faction's command bar art and scaled like it, whose thin left frame
// continues the bar's left stone line and whose panel runs down to the bar's top stone line, which
// then divides the row from the header strip. The bar draws the part above its top edge before
// its art, so the money tab stays in front, and the band down to the stone line after its art,
// around the tab. The row is a top level window created after the bar, so its cameos draw
// over both. Once the row would reach the money tab it lifts above it. An
// invisible blocker under the row in the hit test takes the housing's rect down to the bar's top
// edge, so clicks on the frame and panel stay off the world. Without that art the row has no
// housing and no blocker, and keeps the CAMEO_ROW_GAP spacing.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/WinInstanceData.h"

//-------------------------------------------------------------------------------------------------
/** The container owns the cameos, so their clicks land here. */
//-------------------------------------------------------------------------------------------------
static WindowMsgHandledType SmartSelectionBarSystem( GameWindow *window, UnsignedInt msg,
																										 WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED && msg != GBM_SELECTED_RIGHT )
	{
		return MSG_IGNORED;
	}
	TheControlBar->processSmartSelectionClick( (GameWindow *)mData1, msg == GBM_SELECTED_RIGHT );
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** A check like button ignores the right button up, which bubbles here and would otherwise
	* reach the world as a deselect. Swallow it over a cameo only: in a gap the right button must
	* still pass, or a camera scroll started there never ends. */
//-------------------------------------------------------------------------------------------------
static WindowMsgHandledType CameoRowInput( GameWindow *window, UnsignedInt msg,
																					 WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GWM_RIGHT_DOWN && msg != GWM_RIGHT_UP )
	{
		return MSG_IGNORED;
	}
	GameWindow *child = window->winPointInChild( LOLONGTOSHORT( mData1 ), HILONGTOSHORT( mData1 ) );
	return ( child && child != window ) ? MSG_HANDLED : MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
/** The active control bar scheme, or null. Read on every use because it follows the viewed player. */
//-------------------------------------------------------------------------------------------------
static const ControlBarScheme *getActiveScheme()
{
	ControlBarSchemeManager *schemeManager = TheControlBar ? TheControlBar->getControlBarSchemeManager() : nullptr;
	return schemeManager ? schemeManager->getCurrentScheme() : nullptr;
}

// the image push button draw that CameoDraw wraps
static GameWinDrawFunc s_cameoBaseDraw = nullptr;

//-------------------------------------------------------------------------------------------------
static void CameoDraw( GameWindow *window, WinInstanceData *instData )
{
	const ControlBarScheme *scheme = getActiveScheme();
	GadgetButtonSetBorder( window, scheme ? scheme->m_borderBuildColor : (Color)GAME_COLOR_UNDEFINED, scheme != nullptr );
	if( s_cameoBaseDraw )
	{
		s_cameoBaseDraw( window, instData );
	}
}

//-------------------------------------------------------------------------------------------------
/** A rectangle of the Pro command bar art, in pixels of its 1920 by 512 mapped image. */
//-------------------------------------------------------------------------------------------------
struct FrameSlice
{
	Int left;
	Int top;
	Int width;
	Int height;
};

// the bar's stone line with two pixels of its outer bevel, and its panel, at the same pixels in
// all three factions
static const FrameSlice FRAME_TOP_LEFT = { 505, 332, 7, 6 };
static const FrameSlice FRAME_TOP_RIGHT = { 1406, 332, 7, 6 };
static const FrameSlice FRAME_TOP = { 836, 332, 20, 6 };
static const FrameSlice FRAME_LEFT = { 505, 346, 7, 20 };
static const FrameSlice FRAME_RIGHT = { 1406, 346, 7, 20 };
static const FrameSlice FRAME_FILL = { 524, 356, 20, 20 };
static const Int FRAME_THICKNESS_X = 7;
static const Int FRAME_THICKNESS_Y = 6;
static const Int FRAME_PADDING = 2;
static const Int FRAME_ART_RIGHT = 1413;
static const Int FRAME_ART_BOTTOM = 376;

// the bar's outer top left corner, which the CommandWindow's screen position marks
static const Int BAR_LEFT = 500;
static const Int BAR_TOP = 326;
// the outer edge of the bar's left stone line, where the span a full row's housing centres in starts
static const Int HOUSING_LEFT = 505;
// the outer edge of the bar's right stone line, where that span ends
static const Int HOUSING_RIGHT = 1413;
// the bar's top stone line, which the housing's sides and panel run down to
static const Int BAR_STONE_TOP = 334;
// the money tab, the only art above the bar's top edge
static const Int MONEY_TAB_LEFT = 862;
static const Int MONEY_TAB_RIGHT = 1056;
static const Int MONEY_TAB_TOP = 302;

static const char *const PRO_COMMAND_BAR_IMAGES[] = { "AmericaProCommandBar", "ChinaProCommandBar", "GlaProCommandBar" };

// one image retargeted for every slice; the display keys its texture cache by image and filename
static Image *s_frameSlice = nullptr;

//-------------------------------------------------------------------------------------------------
/** The active scheme's Pro command bar part, with the screen pixels per art pixel it is drawn
	* at, or null for a stock or observer bar. */
//-------------------------------------------------------------------------------------------------
static const ControlBarSchemeImage *findFramePart( Coord2D *scale )
{
	const ControlBarScheme *scheme = getActiveScheme();
	if( scheme == nullptr || TheDisplay == nullptr || scheme->m_ScreenCreationRes.x <= 0 || scheme->m_ScreenCreationRes.y <= 0 )
	{
		return nullptr;
	}

	for( Int layer = 0; layer < MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS; layer++ )
	{
		const ControlBarScheme::ControlBarSchemeImageList &parts = scheme->m_layer[ layer ];
		for( ControlBarScheme::ControlBarSchemeImageList::const_iterator it = parts.begin(); it != parts.end(); ++it )
		{
			const ControlBarSchemeImage *part = *it;
			const Image *image = part ? part->m_image : nullptr;
			// the slices are cut by plain UV, which a rotated or raw texture image does not have
			if( image == nullptr || image->getStatus() != IMAGE_STATUS_NONE ||
					image->getImageWidth() < FRAME_ART_RIGHT || image->getImageHeight() < FRAME_ART_BOTTOM )
			{
				continue;
			}

			Bool pro = FALSE;
			for( Int i = 0; i < (Int)ARRAY_SIZE( PRO_COMMAND_BAR_IMAGES ); i++ )
			{
				if( image->getName().compareNoCase( PRO_COMMAND_BAR_IMAGES[ i ] ) == 0 )
				{
					pro = TRUE;
					break;
				}
			}
			if( !pro )
			{
				continue;
			}

			// the same factors ControlBarScheme::drawBackground applies to the part
			scale->x = part->m_size.x * ( TheDisplay->getWidth() / (Real)scheme->m_ScreenCreationRes.x ) / image->getImageWidth();
			scale->y = part->m_size.y * ( TheDisplay->getHeight() / (Real)scheme->m_ScreenCreationRes.y ) / image->getImageHeight();
			return part;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
static Int scaleArt( Int artPixels, Real scale )
{
	const Real pixels = artPixels * scale;
	return pixels < 0.0f ? -REAL_TO_INT( 0.5f - pixels ) : REAL_TO_INT( pixels + 0.5f );
}

//-------------------------------------------------------------------------------------------------
/** Tile a slice over a screen rect at the art's own scale, cropping the last tile by UV. Only the
	* tiles inside the clip are visited, still counted from the rect's own origin, so the pattern
	* lines up however the rect is clipped. */
//-------------------------------------------------------------------------------------------------
static void drawFrameTiles( const Image *source, const FrameSlice &slice, const Coord2D &scale,
														Int x0, Int y0, Int x1, Int y1, Bool flip, const IRegion2D &clip )
{
	const Real tileWidth = slice.width * scale.x;
	const Real tileHeight = slice.height * scale.y;
	if( tileWidth < 1.0f || tileHeight < 1.0f )
	{
		return;
	}

	const Region2D *sourceUV = source->getUV();
	const Real uPerPixel = ( sourceUV->hi.x - sourceUV->lo.x ) / source->getImageWidth();
	const Real vPerPixel = ( sourceUV->hi.y - sourceUV->lo.y ) / source->getImageHeight();

	// one tile back from the clip edge, since rounding can start a tile a pixel late
	const Int firstRow = clip.lo.y > y0 ? MAX( REAL_TO_INT( ( clip.lo.y - y0 ) / tileHeight ) - 1, 0 ) : 0;
	const Int firstColumn = clip.lo.x > x0 ? MAX( REAL_TO_INT( ( clip.lo.x - x0 ) / tileWidth ) - 1, 0 ) : 0;
	const Int lastY = MIN( y1, clip.hi.y );
	const Int lastX = MIN( x1, clip.hi.x );

	for( Int row = firstRow; y0 + REAL_TO_INT( row * tileHeight + 0.5f ) < lastY; row++ )
	{
		const Int top = y0 + REAL_TO_INT( row * tileHeight + 0.5f );
		const Int fullBottom = y0 + REAL_TO_INT( ( row + 1 ) * tileHeight + 0.5f );
		const Int bottom = MIN( fullBottom, y1 );
		const Real shareY = (Real)( bottom - top ) / (Real)( fullBottom - top );

		Region2D uv;
		if( flip )
		{
			uv.lo.y = sourceUV->lo.y + ( slice.top + slice.height ) * vPerPixel;
			uv.hi.y = uv.lo.y - slice.height * shareY * vPerPixel;
		}
		else
		{
			uv.lo.y = sourceUV->lo.y + slice.top * vPerPixel;
			uv.hi.y = uv.lo.y + slice.height * shareY * vPerPixel;
		}

		for( Int column = firstColumn; x0 + REAL_TO_INT( column * tileWidth + 0.5f ) < lastX; column++ )
		{
			const Int left = x0 + REAL_TO_INT( column * tileWidth + 0.5f );
			const Int fullRight = x0 + REAL_TO_INT( ( column + 1 ) * tileWidth + 0.5f );
			const Int right = MIN( fullRight, x1 );
			const Real shareX = (Real)( right - left ) / (Real)( fullRight - left );

			uv.lo.x = sourceUV->lo.x + slice.left * uPerPixel;
			uv.hi.x = uv.lo.x + slice.width * shareX * uPerPixel;
			s_frameSlice->setUV( &uv );
			TheDisplay->drawImage( s_frameSlice, left, top, right, bottom );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Under the Pro art, art pixels map to the screen from the bar's corner at the CommandWindow. */
//-------------------------------------------------------------------------------------------------
ControlBar::CameoRowLayout ControlBar::getCameoRowLayout() const
{
	CameoRowLayout layout;
	GameWindow *commandWindow = m_contextParent[ CP_COMMAND ] ? m_contextParent[ CP_COMMAND ] : m_contextParent[ CP_MASTER ];
	ICoord2D commandPos;
	commandPos.x = 0;
	commandPos.y = 0;
	if( commandWindow )
	{
		commandWindow->winGetScreenPosition( &commandPos.x, &commandPos.y );
	}

	Coord2D scale;
	layout.housed = commandWindow != nullptr && findFramePart( &scale ) != nullptr;
	if( !layout.housed )
	{
		layout.cameoLeft = commandPos.x;
		layout.restBottom = commandPos.y - CAMEO_ROW_GAP;
		layout.liftedBottom = layout.restBottom;
		layout.tabLeft = commandPos.x;
		layout.side = 0;
		return layout;
	}

	layout.side = scaleArt( FRAME_PADDING + FRAME_THICKNESS_X, scale.x );
	// a full row's housing sits centred between the bar's stone lines
	const Int fullHousing = getCameoRowWidth( MAX_SMART_SELECTION_BUTTONS ) + 2 * layout.side;
	const Int inset = MAX( ( scaleArt( HOUSING_RIGHT - HOUSING_LEFT, scale.x ) - fullHousing ) / 2, 0 );
	layout.cameoLeft = commandPos.x + scaleArt( HOUSING_LEFT - BAR_LEFT, scale.x ) + inset + layout.side;
	layout.restBottom = commandPos.y + scaleArt( BAR_STONE_TOP - FRAME_PADDING - BAR_TOP, scale.y );
	layout.liftedBottom = commandPos.y + scaleArt( MONEY_TAB_TOP - FRAME_PADDING - BAR_TOP, scale.y );
	layout.tabLeft = commandPos.x + scaleArt( MONEY_TAB_LEFT - BAR_LEFT, scale.x );
	return layout;
}

//-------------------------------------------------------------------------------------------------
/** Cameo bottom of the smart selection row. Under the Pro art the row lifts over the money tab
	* once it would reach it, so it never covers the money. */
//-------------------------------------------------------------------------------------------------
Int ControlBar::getCameoStackBottom( const CameoRowLayout &layout ) const
{
	if( !layout.housed )
	{
		return layout.restBottom;
	}

	Int widest = m_smartSelectionGroups.empty() ? 0 : getCameoRowWidth( (Int)m_smartSelectionGroups.size() );
	return layout.cameoLeft + widest + layout.side > layout.tabLeft ? layout.liftedBottom : layout.restBottom;
}

//-------------------------------------------------------------------------------------------------
/** The housing's outer rect around the shown smart selection row, or FALSE when no housing is drawn.
	* It is closed, ending under the cameos with a bottom edge, when that fits above the bar's top
	* edge, and otherwise ends at the bar's top edge. The picture and the input blocker both use it. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::getCameoRowHousing( IRegion2D *housing, Bool *closed ) const
{
	GameWindow *commandWindow = m_contextParent[ CP_COMMAND ] ? m_contextParent[ CP_COMMAND ] : m_contextParent[ CP_MASTER ];
	Coord2D scale;
	if( commandWindow == nullptr || findFramePart( &scale ) == nullptr )
	{
		return FALSE;
	}

	if( m_smartSelectionGroups.empty() || m_smartSelectionParent == nullptr || m_smartSelectionParent->winIsHidden() )
	{
		return FALSE;
	}
	ICoord2D rowPos, rowSize;
	m_smartSelectionParent->winGetScreenPosition( &rowPos.x, &rowPos.y );
	m_smartSelectionParent->winGetSize( &rowSize.x, &rowSize.y );
	IRegion2D cameos;
	cameos.lo = rowPos;
	cameos.hi.x = rowPos.x + rowSize.x;
	cameos.hi.y = rowPos.y + rowSize.y;

	ICoord2D commandPos;
	commandWindow->winGetScreenPosition( &commandPos.x, &commandPos.y );
	housing->lo.x = cameos.lo.x - scaleArt( FRAME_PADDING + FRAME_THICKNESS_X, scale.x );
	housing->lo.y = cameos.lo.y - scaleArt( FRAME_THICKNESS_Y + FRAME_PADDING, scale.y );
	housing->hi.x = cameos.hi.x + scaleArt( FRAME_PADDING + FRAME_THICKNESS_X, scale.x );
	const Int closedBottom = cameos.hi.y + scaleArt( FRAME_PADDING + FRAME_THICKNESS_Y, scale.y );
	*closed = closedBottom <= commandPos.y;
	housing->hi.y = *closed ? closedBottom : commandPos.y;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Every piece of the housing whose outer rect runs from x0, y0 to x1, y1, the fill at the art's
	* scale and the edges and corners at frameScale. A closed housing gets a bottom edge of the top
	* edge's slices upside down; the clip decides which part of it lands. */
//-------------------------------------------------------------------------------------------------
static void drawHousingPieces( const Image *source, const Coord2D &scale, const Coord2D &frameScale,
															 Int x0, Int y0, Int x1, Int y1, Bool closed, IRegion2D clip )
{
	const Int borderX = scaleArt( FRAME_THICKNESS_X, frameScale.x );
	const Int borderY = scaleArt( FRAME_THICKNESS_Y, frameScale.y );
	const Int sideBottom = closed ? y1 - borderY : y1;
	TheDisplay->setClipRegion( &clip );
	drawFrameTiles( source, FRAME_FILL, scale, x0 + borderX, y0 + borderY, x1 - borderX, sideBottom, FALSE, clip );
	drawFrameTiles( source, FRAME_TOP, frameScale, x0 + borderX, y0, x1 - borderX, y0 + borderY, FALSE, clip );
	drawFrameTiles( source, FRAME_LEFT, frameScale, x0, y0 + borderY, x0 + borderX, sideBottom, FALSE, clip );
	drawFrameTiles( source, FRAME_RIGHT, frameScale, x1 - borderX, y0 + borderY, x1, sideBottom, FALSE, clip );
	drawFrameTiles( source, FRAME_TOP_LEFT, frameScale, x0, y0, x0 + borderX, y0 + borderY, FALSE, clip );
	drawFrameTiles( source, FRAME_TOP_RIGHT, frameScale, x1 - borderX, y0, x1, y0 + borderY, FALSE, clip );
	if( closed )
	{
		drawFrameTiles( source, FRAME_TOP, frameScale, x0 + borderX, sideBottom, x1 - borderX, y1, TRUE, clip );
		drawFrameTiles( source, FRAME_TOP_LEFT, frameScale, x0, sideBottom, x0 + borderX, y1, TRUE, clip );
		drawFrameTiles( source, FRAME_TOP_RIGHT, frameScale, x1 - borderX, sideBottom, x1, y1, TRUE, clip );
	}
}

//-------------------------------------------------------------------------------------------------
/** The edge and corner scale at framePercent of the art's. */
//-------------------------------------------------------------------------------------------------
static Coord2D getFrameScale( const Coord2D &scale, Int framePercent )
{
	Coord2D frameScale;
	frameScale.x = scale.x * ( framePercent / 100.0f );
	frameScale.y = scale.y * ( framePercent / 100.0f );
	return frameScale;
}

//-------------------------------------------------------------------------------------------------
static void prepareFrameSlice( const Image *source )
{
	if( s_frameSlice == nullptr )
	{
		s_frameSlice = newInstance( Image );
	}
	s_frameSlice->setFilename( source->getFilename() );
	s_frameSlice->setTextureWidth( source->getTextureSize()->x );
	s_frameSlice->setTextureHeight( source->getTextureSize()->y );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::getCameoFrameInset( ICoord2D *inset, Int framePercent ) const
{
	Coord2D scale;
	if( findFramePart( &scale ) == nullptr )
	{
		inset->x = 0;
		inset->y = 0;
		return FALSE;
	}
	const Coord2D frameScale = getFrameScale( scale, framePercent );
	inset->x = scaleArt( FRAME_THICKNESS_X, frameScale.x ) + scaleArt( FRAME_PADDING, scale.x );
	inset->y = scaleArt( FRAME_THICKNESS_Y, frameScale.y ) + scaleArt( FRAME_PADDING, scale.y );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A closed housing over the rect, drawn whole, its frame at framePercent of the art's thickness. */
//-------------------------------------------------------------------------------------------------
void ControlBar::drawCameoFrame( const IRegion2D &rect, Int framePercent )
{
	Coord2D scale;
	const ControlBarSchemeImage *part = findFramePart( &scale );
	if( part == nullptr )
	{
		return;
	}

	prepareFrameSlice( part->m_image );
	TheDisplay->beginBatch();
	drawHousingPieces( part->m_image, scale, getFrameScale( scale, framePercent ), rect.lo.x, rect.lo.y, rect.hi.x, rect.hi.y, TRUE, rect );
	TheDisplay->endBatch();
	TheDisplay->enableClipping( FALSE );
}

//-------------------------------------------------------------------------------------------------
/** An open housing runs from above the top row down to the bar's top stone line. Before the bar's
	* art it draws the part above the bar's top edge, so the money tab covers it; over the bar it draws
	* the band that hides the bar's dark top bevel, around the tab. A closed housing draws whole
	* before the bar's art and nothing over the bar. */
//-------------------------------------------------------------------------------------------------
void ControlBar::drawCameoRowHousing( Bool overBar )
{
	IRegion2D housing;
	Bool closed;
	Coord2D scale;
	const ControlBarSchemeImage *part = findFramePart( &scale );
	if( part == nullptr || !getCameoRowHousing( &housing, &closed ) )
	{
		return;
	}
	if( closed && overBar )
	{
		return;
	}
	if( closed )
	{
		drawCameoFrame( housing, 100 );
		return;
	}

	const Image *source = part->m_image;
	prepareFrameSlice( source );

	const Int x0 = housing.lo.x;
	const Int y0 = housing.lo.y;
	const Int x1 = housing.hi.x;
	const Int barTop = housing.hi.y;
	const Int y1 = barTop + scaleArt( BAR_STONE_TOP - BAR_TOP, scale.y );

	IRegion2D clip;
	TheDisplay->beginBatch();
	if( !overBar )
	{
		clip = housing;
		drawHousingPieces( source, scale, scale, x0, y0, x1, y1, FALSE, clip );
	}
	else
	{
		ICoord2D commandPos;
		GameWindow *commandWindow = m_contextParent[ CP_COMMAND ] ? m_contextParent[ CP_COMMAND ] : m_contextParent[ CP_MASTER ];
		commandWindow->winGetScreenPosition( &commandPos.x, &commandPos.y );
		const Int tabLeft = commandPos.x + scaleArt( MONEY_TAB_LEFT - BAR_LEFT, scale.x );
		const Int tabRight = commandPos.x + scaleArt( MONEY_TAB_RIGHT - BAR_LEFT, scale.x );
		clip.lo.y = barTop;
		clip.hi.y = barTop + scaleArt( BAR_STONE_TOP - BAR_TOP, scale.y );

		clip.lo.x = x0;
		clip.hi.x = MIN( x1, tabLeft );
		if( clip.lo.x < clip.hi.x )
		{
			drawHousingPieces( source, scale, scale, x0, y0, x1, y1, FALSE, clip );
		}

		clip.lo.x = MAX( x0, tabRight );
		clip.hi.x = x1;
		if( clip.lo.x < clip.hi.x )
		{
			drawHousingPieces( source, scale, scale, x0, y0, x1, y1, FALSE, clip );
		}
	}
	TheDisplay->endBatch();
	TheDisplay->enableClipping( FALSE );
}

//-------------------------------------------------------------------------------------------------
static void CameoRowBlockerDraw( GameWindow *window, WinInstanceData *instData )
{
}

//-------------------------------------------------------------------------------------------------
/** A lifted stack's blocker covers the money display, whose tooltip it passes on. */
//-------------------------------------------------------------------------------------------------
static void CameoRowBlockerTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	static const NameKeyType moneyKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );
	GameWindow *money = TheWindowManager->winGetWindowFromId( nullptr, moneyKey );
	if( money == nullptr || money->winIsHidden() || money->winGetTooltipFunc() == nullptr )
	{
		return;
	}
	if( money->winPointInWindow( LOLONGTOSHORT( mouse ), HILONGTOSHORT( mouse ) ) )
	{
		money->winGetTooltipFunc()( money, money->winGetInstanceData(), mouse );
	}
}

//-------------------------------------------------------------------------------------------------
/** Every frame, after the row is placed: the blocker takes the housing's rect, or hides. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateCameoRowBlocker()
{
	if( m_cameoRowBlocker == nullptr )
	{
		return;
	}

	IRegion2D housing;
	Bool closed;
	if( !getCameoRowHousing( &housing, &closed ) || housing.hi.x <= housing.lo.x || housing.hi.y <= housing.lo.y )
	{
		if( !m_cameoRowBlocker->winIsHidden() )
		{
			m_cameoRowBlocker->winHide( TRUE );
		}
		return;
	}
	m_cameoRowBlocker->winSetPosition( housing.lo.x, housing.lo.y );
	m_cameoRowBlocker->winSetSize( housing.hi.x - housing.lo.x, housing.hi.y - housing.lo.y );
	m_cameoRowBlocker->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
/** A selected object that gets a cameo. Mob members ride along with their nexus and stay out
	* of the row, like they stay out of the command bar. */
//-------------------------------------------------------------------------------------------------
static Object *getSmartSelectionObject( Drawable *draw )
{
	Object *obj = draw->getObject();
	if( obj == nullptr || !obj->isLocallyControlled() )
	{
		return nullptr;
	}
	if( obj->isKindOf( KINDOF_IGNORED_IN_GUI ) || obj->getStatusBits().test( OBJECT_STATUS_SOLD ) )
	{
		return nullptr;
	}
	return obj;
}

//-------------------------------------------------------------------------------------------------
/** A hidden, enabled top level window. The bar's own ABOVE bit decides which hit test pass finds
	* it, so the cameo rows and their blocker must match it. */
//-------------------------------------------------------------------------------------------------
UnsignedInt ControlBar::getCameoRowStatus() const
{
	UnsignedInt status = WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN;
	if( m_contextParent[ CP_MASTER ] && BitIsSet( m_contextParent[ CP_MASTER ]->winGetStatus(), WIN_STATUS_ABOVE ) )
	{
		status |= WIN_STATUS_ABOVE;
	}
	return status;
}

//-------------------------------------------------------------------------------------------------
/** A top level row of small cameos, hidden, with one based slots in the button data so an
	* unset payload never reads as slot zero. */
//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::createCameoRow( GameWinSystemFunc systemFunc, Int slotCount, Bool rightClick, const ICoord2D &buttonSize, GameWindow **buttons )
{
	const Int stride = buttonSize.x + CAMEO_ROW_GAP;

	const UnsignedInt parentStatus = getCameoRowStatus() | WIN_STATUS_SEE_THRU;
	GameWindow *parent = TheWindowManager->winCreate( nullptr, parentStatus, 0, 0,
		slotCount * stride - CAMEO_ROW_GAP, buttonSize.y, systemFunc );
	if( parent == nullptr )
	{
		return nullptr;
	}
	parent->winSetInputFunc( CameoRowInput );

	GameWindow *commandButton = m_commandWindows[ 0 ];
	GameFont *font = commandButton ? commandButton->winGetFont() : nullptr;
	if( font == nullptr )
	{
		Int pointSize = MIN( MAX( buttonSize.y / 3, 8 ), 10 );
		if( TheGlobalLanguageData )
		{
			pointSize = TheGlobalLanguageData->adjustFontSize( pointSize );
		}
		// the badges read fine at seven tenths of the command bar's text size
		pointSize = MAX( pointSize * 7 / 10, 6 );
		font = TheFontLibrary->getFont( AsciiString( "Arial" ), pointSize, TRUE );
	}

	const Color textColor = GameMakeColor( 255, 255, 255, 255 );
	const Color dropColor = GameMakeColor( 0, 0, 0, 255 );
	const UnsignedInt rightClickStatus = rightClick ? WIN_STATUS_RIGHT_CLICK : 0;

	for( Int i = 0; i < slotCount; i++ )
	{
		WinInstanceData instData;
		instData.init();
		instData.m_style = GWS_PUSH_BUTTON | GWS_MOUSE_TRACK;
		// the button input reads the right click flag off the instance data, not the window
		instData.m_status = rightClickStatus;

		GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent,
			WIN_STATUS_ENABLED | WIN_STATUS_IMAGE | WIN_STATUS_USE_OVERLAY_STATES |
			WIN_STATUS_COUNT_BADGE | WIN_STATUS_HIDDEN | rightClickStatus,
			i * stride, 0, buttonSize.x, buttonSize.y,
			&instData, font, FALSE );
		if( button == nullptr )
		{
			continue;
		}

		GadgetButtonSetData( button, (void *)(size_t)( i + 1 ) );
		button->winSetFont( font );
		if( commandButton )
		{
			button->winSetEnabledTextColors( commandButton->winGetEnabledTextColor(), commandButton->winGetEnabledTextBorderColor() );
			button->winSetHiliteTextColors( commandButton->winGetHiliteTextColor(), commandButton->winGetHiliteTextBorderColor() );
			button->winSetDisabledTextColors( commandButton->winGetDisabledTextColor(), commandButton->winGetDisabledTextBorderColor() );
		}
		else
		{
			button->winSetEnabledTextColors( textColor, dropColor );
			button->winSetHiliteTextColors( textColor, dropColor );
			button->winSetDisabledTextColors( textColor, dropColor );
		}
		s_cameoBaseDraw = button->winGetDrawFunc();
		button->winSetDrawFunc( CameoDraw );
		GadgetButtonSetAltSound( button, "GUICommandBarClick" );

		buttons[ i ] = button;
	}
	return parent;
}

//-------------------------------------------------------------------------------------------------
/** The cameo image of a type, falling back to its portrait. */
//-------------------------------------------------------------------------------------------------
const Image *ControlBar::getCameoImage( const ThingTemplate *thingTemplate )
{
	const Image *image = thingTemplate->getButtonImage();
	return image ? image : thingTemplate->getSelectedPortraitImage();
}

//-------------------------------------------------------------------------------------------------
Int ControlBar::getCameoRowWidth( Int cameoCount ) const
{
	return cameoCount * ( m_smartSelectionButtonSize.x + CAMEO_ROW_GAP ) - CAMEO_ROW_GAP;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::initSmartSelectionBar( const ICoord2D &commandButtonSize )
{
	m_smartSelectionButtonSize.x = commandButtonSize.x * CAMEO_SCALE_PERCENT / 100;
	m_smartSelectionButtonSize.y = commandButtonSize.y * CAMEO_SCALE_PERCENT / 100;
	if( m_smartSelectionButtonSize.x <= 0 || m_smartSelectionButtonSize.y <= 0 )
	{
		return;
	}

	// created before the rows, which then come ahead of it in the hit test and draw after it
	m_cameoRowBlocker = TheWindowManager->winCreate( nullptr, getCameoRowStatus(), 0, 0, 1, 1, nullptr );
	if( m_cameoRowBlocker )
	{
		m_cameoRowBlocker->winSetInputFunc( GameWinBlockInput );
		m_cameoRowBlocker->winSetDrawFunc( CameoRowBlockerDraw );
		m_cameoRowBlocker->winSetTooltipFunc( CameoRowBlockerTooltip );
	}

	m_smartSelectionParent = createCameoRow( SmartSelectionBarSystem, MAX_SMART_SELECTION_BUTTONS, TRUE, m_smartSelectionButtonSize, m_smartSelectionButtons );
	for( Int i = 0; i < MAX_SMART_SELECTION_BUTTONS; i++ )
	{
		if( m_smartSelectionButtons[ i ] )
		{
			GadgetButtonEnableCheckLike( m_smartSelectionButtons[ i ], TRUE, FALSE );
		}
	}

	initCommandGroupBar( commandButtonSize );
	initBuildQueueBar();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::destroySmartSelectionBar()
{
	if( m_smartSelectionParent )
	{
		TheWindowManager->winDestroy( m_smartSelectionParent );
	}
	m_smartSelectionParent = nullptr;
	for( Int i = 0; i < MAX_SMART_SELECTION_BUTTONS; i++ )
	{
		m_smartSelectionButtons[ i ] = nullptr;
	}
	destroyCommandGroupBar();
	destroyBuildQueueBar();
	if( m_cameoRowBlocker )
	{
		TheWindowManager->winDestroy( m_cameoRowBlocker );
	}
	m_cameoRowBlocker = nullptr;
	deleteInstance( s_frameSlice );
	s_frameSlice = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::resetSmartSelection()
{
	m_smartSelectionGroups.clear();
	m_smartSelectionActive = -1;
	updateFocusGroup();
	// the row hides without a refresh pass, so the rank overlays are cleared here
	for( Int i = 0; i < MAX_SMART_SELECTION_BUTTONS; i++ )
	{
		GadgetButtonDrawOverlayImage( m_smartSelectionButtons[ i ], nullptr );
	}
	if( m_smartSelectionParent && !m_smartSelectionParent->winIsHidden() )
	{
		m_smartSelectionParent->winHide( TRUE );
	}
	resetCommandGroupBar();
}

//-------------------------------------------------------------------------------------------------
/** The type whose command set the bar should show, or null for the group's common set. */
//-------------------------------------------------------------------------------------------------
const ThingTemplate *ControlBar::getSmartSelectionFocusTemplate() const
{
	return m_smartSelectionActive < 0 ? nullptr : m_smartSelectionGroups[ m_smartSelectionActive ].thingTemplate;
}

//-------------------------------------------------------------------------------------------------
/** The one object the bar should show, when the focused cameo stands for a single object. */
//-------------------------------------------------------------------------------------------------
ObjectID ControlBar::getSmartSelectionFocusObject() const
{
	return m_smartSelectionActive < 0 ? INVALID_ID : m_smartSelectionGroups[ m_smartSelectionActive ].objectID;
}

//-------------------------------------------------------------------------------------------------
/** The drawable of the one focused object, which drives the bar's context in place of the
	* multi select context, or null. */
//-------------------------------------------------------------------------------------------------
Drawable *ControlBar::getSmartSelectionFocusDrawable() const
{
	const ObjectID focusObject = getSmartSelectionFocusObject();
	if( focusObject == INVALID_ID )
	{
		return nullptr;
	}
	Object *obj = TheGameLogic->findObjectByID( focusObject );
	return obj ? obj->getDrawable() : nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Whether the object takes part in the command bar while a cameo is focused. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::isSmartSelectionFocused( const Object *obj ) const
{
	if (m_smartSelectionActive < 0)
		return true;

	if (!obj)
		return false;

	Int count = m_smartSelectionGroups[m_smartSelectionActive].count;

	//sanity
	if (count < 1)
		return true;

	//ShigureUi 14/09/2026 If focus on single
	if (count == 1 && obj->getID() == m_smartSelectionGroups[m_smartSelectionActive].objectID)
		return true;

	//ShigureUi 14/09/2026 If focus on a type
	if (count > 1 && obj->getTemplate()->getReskinRoot() == m_smartSelectionGroups[m_smartSelectionActive].thingTemplate)
		return true;
	
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Whether the cameo is pushed in: the focused one, or every one of the focused type. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::isIndexSmartSelectionFocused( Int groupIndex ) const
{
	if( m_smartSelectionActive < 0 )
	{
		return FALSE;
	}
	const SmartSelectionGroup &focus = m_smartSelectionGroups[ m_smartSelectionActive ];
	return focus.objectID != INVALID_ID ? groupIndex == m_smartSelectionActive : m_smartSelectionGroups[ groupIndex ].thingTemplate == focus.thingTemplate;
}

//-------------------------------------------------------------------------------------------------
/** Runs whenever the UI is marked dirty, which is far more often than the selection changes,
	* so the entries are rebuilt into a local and the row only repainted when they differ. */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateSmartSelection()
{
	if( m_smartSelectionParent == nullptr )
	{
		return;
	}
	if( !TheGlobalData->m_smartSelection )
	{
		resetSmartSelection();
		return;
	}

	// one cameo per type, then a selection of a single type spreads into one cameo per object
	std::vector<SmartSelectionGroup> groups;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *obj = getSmartSelectionObject( *it );
		if( obj == nullptr )
		{
			continue;
		}

		const ThingTemplate *thingTemplate = obj->getTemplate()->getReskinRoot();
		size_t g = 0;
		for( ; g < groups.size(); g++ )
		{
			if( groups[ g ].thingTemplate == thingTemplate )
			{
				break;
			}
		}
		if( g == groups.size() )
		{
			if( g >= MAX_SMART_SELECTION_BUTTONS )
			{
				continue;
			}
			SmartSelectionGroup group;
			group.thingTemplate = thingTemplate;
			group.count = 0;
			group.objectID = obj->getID();
			groups.push_back( group );
		}
		groups[ g ].count++;
		if( groups[ g ].count > 1 )
		{
			groups[ g ].objectID = INVALID_ID;
		}
	}
	if( groups.size() == 1 && groups[ 0 ].count > 1 )
	{
		SmartSelectionGroup group = groups[ 0 ];
		group.count = 1;
		groups.clear();
		for( DrawableListCIt it = selected->begin(); it != selected->end() && groups.size() < MAX_SMART_SELECTION_BUTTONS; ++it )
		{
			Object *obj = getSmartSelectionObject( *it );
			if( obj )
			{
				group.objectID = obj->getID();
				groups.push_back( group );
			}
		}
	}

	Bool same = groups.size() == m_smartSelectionGroups.size();
	for( size_t g = 0; same && g < groups.size(); g++ )
	{
		same = groups[ g ].thingTemplate == m_smartSelectionGroups[ g ].thingTemplate &&
					 groups[ g ].count == m_smartSelectionGroups[ g ].count &&
					 groups[ g ].objectID == m_smartSelectionGroups[ g ].objectID;
	}
	if( same )
	{
		return;
	}

	// The focus survives the rebuild if its object, or else its type, is still in the
	// selection. It cannot survive into a selection the bar drives from one drawable, whose own
	// command set is shown and where the focus would silently filter the next multi selection.
	SmartSelectionGroup focus;
	focus.thingTemplate = nullptr;
	focus.objectID = INVALID_ID;
	if( m_smartSelectionActive >= 0 && TheInGameUI->getSelectCount() > 1 )
	{
		focus = m_smartSelectionGroups[ m_smartSelectionActive ];
	}
	m_smartSelectionGroups.swap( groups );
	m_smartSelectionActive = -1;
	for( size_t g = 0; focus.thingTemplate && g < m_smartSelectionGroups.size(); g++ )
	{
		const SmartSelectionGroup &group = m_smartSelectionGroups[ g ];
		if( focus.objectID != INVALID_ID ? group.objectID == focus.objectID : group.thingTemplate == focus.thingTemplate )
		{
			m_smartSelectionActive = (Int)g;
			break;
		}
	}
	// the focus resolves to a new set of objects here, even when its index is unchanged
	updateFocusGroup();

	refreshSmartSelectionButtons();
}

//-------------------------------------------------------------------------------------------------
/** Every frame: follow the command bar, which slides in on show and hides on its own schedule. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateSmartSelection()
{
	if( m_smartSelectionParent == nullptr )
	{
		return;
	}

	updateCommandGroupBar();
	updateBuildQueueBar();

	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( master == nullptr || master->winIsHidden() || m_smartSelectionGroups.empty() )
	{
		if( !m_smartSelectionParent->winIsHidden() )
		{
			m_smartSelectionParent->winHide( TRUE );
		}
		updateCameoRowBlocker();
		return;
	}

	// the bar's parent window starts well above its visible frame, so the layout anchors to the
	// command grid, whose top sits at the frame
	const CameoRowLayout layout = getCameoRowLayout();
	const Int cameoHeight = m_smartSelectionButtonSize.y;
	Int rowY = getCameoStackBottom( layout ) - cameoHeight;

	// the money display rises out of the frame, so a bare row long enough to reach its housing
	// lifts above it instead of running into it
	const Int rowRight = layout.cameoLeft + getCameoRowWidth( (Int)m_smartSelectionGroups.size() );
	if( !layout.housed && m_smartSelectionMoneyWindow && !m_smartSelectionMoneyWindow->winIsHidden() )
	{
		ICoord2D moneyPos;
		m_smartSelectionMoneyWindow->winGetScreenPosition( &moneyPos.x, &moneyPos.y );
		// the housing slopes out about a cameo's width left of the money text
		const Int housingMargin = m_smartSelectionButtonSize.x;
		const Int liftedY = moneyPos.y - CAMEO_ROW_GAP - cameoHeight;
		if( rowRight > moneyPos.x - housingMargin && liftedY < rowY )
		{
			rowY = liftedY;
		}
	}

	m_smartSelectionParent->winSetPosition( layout.cameoLeft, rowY );
	m_smartSelectionParent->winHide( FALSE );
	updateCameoRowBlocker();

	// a cameo for a type wears the best rank among its members, so one pass over the selection
	// feeds every cameo, and a rank gained under an unchanged selection still lands
	VeterancyLevel bestLevel[ MAX_SMART_SELECTION_BUTTONS ];
	for( size_t g = 0; g < m_smartSelectionGroups.size(); g++ )
	{
		bestLevel[ g ] = LEVEL_INVALID;
	}
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *obj = getSmartSelectionObject( *it );
		if( obj == nullptr )
		{
			continue;
		}
		const VeterancyLevel level = obj->getVeterancyLevel();
		for( size_t g = 0; g < m_smartSelectionGroups.size(); g++ )
		{
			const SmartSelectionGroup &group = m_smartSelectionGroups[ g ];
			const Bool inGroup = group.objectID != INVALID_ID ? obj->getID() == group.objectID : obj->getTemplate()->getReskinRoot() == group.thingTemplate;
			if( inGroup && ( bestLevel[ g ] == LEVEL_INVALID || level > bestLevel[ g ] ) )
			{
				bestLevel[ g ] = level;
			}
		}
	}

	// the bars are one shot on the button, so a lone member's health and clip go on every frame
	for( size_t g = 0; g < m_smartSelectionGroups.size(); g++ )
	{
		GameWindow *button = m_smartSelectionButtons[ g ];
		if( button == nullptr )
		{
			continue;
		}

		// the overlay stays on the button until replaced, so an unranked cameo clears it
		GadgetButtonDrawOverlayImage( button, calculateVeterancyOverlayForLevel( bestLevel[ g ] ) );

		if( m_smartSelectionGroups[ g ].objectID == INVALID_ID )
		{
			continue;
		}
		const Object *obj = TheGameLogic->findObjectByID( m_smartSelectionGroups[ g ].objectID );
		if( obj == nullptr )
		{
			continue;
		}
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body->getMaxHealth() > 0.0f )
		{
			GadgetButtonDrawHealthBar( button, body->getHealth() / body->getMaxHealth() );
		}
		Int clipSize, ammoInClip;
		if( obj->getAmmoPipShowingInfo( clipSize, ammoInClip ) )
		{
			GadgetButtonDrawAmmoBar( button, ammoInClip, clipSize );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Put the groups on the cameos, with the focused one, or every one of its type, pushed in. */
//-------------------------------------------------------------------------------------------------
void ControlBar::refreshSmartSelectionButtons()
{
	const size_t groupCount = m_smartSelectionGroups.size();

	for( Int i = 0; i < MAX_SMART_SELECTION_BUTTONS; i++ )
	{
		GameWindow *button = m_smartSelectionButtons[ i ];
		if( button == nullptr )
		{
			continue;
		}
		if( (size_t)i >= groupCount )
		{
			GadgetButtonDrawOverlayImage( button, nullptr );
			button->winHide( TRUE );
			continue;
		}

		const SmartSelectionGroup &group = m_smartSelectionGroups[ i ];
		GadgetButtonSetEnabledImage( button, getCameoImage( group.thingTemplate ) );

		UnicodeString count;
		if( group.objectID == INVALID_ID && group.count <= MAX_CAMEO_COUNT_BADGE )
		{
			count.format( L"%d", group.count );
		}
		GadgetButtonSetText( button, count );
		button->winSetTooltip( group.thingTemplate->getDisplayName() );
		GadgetCheckLikeButtonSetVisualCheck( button, isIndexSmartSelectionFocused( i ) );
		button->winHide( FALSE );
	}

	m_smartSelectionParent->winSetSize( MAX( getCameoRowWidth( (Int)m_smartSelectionGroups.size() ), 1 ), m_smartSelectionButtonSize.y );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::processSmartSelectionClick( GameWindow *button, Bool rightClick )
{
	const Int groupIndex = (Int)(size_t)GadgetButtonGetData( button ) - 1;
	if( groupIndex < 0 || (size_t)groupIndex >= m_smartSelectionGroups.size() )
	{
		return;
	}

	if( rightClick )
	{
		if (isIndexSmartSelectionFocused(groupIndex))
		{
			smartSelectionFocus( -1 );
		}
		else
		{
			smartSelectionRemove( groupIndex, FALSE );
		}
		return;
	}

	Bool keepOnly;
	if( TheGlobalData->m_smartSelectionUseMouse )
	{
		// The window layer folds a double click into a plain press, so it is found here as a
		// second press on the same cameo within the system double click time.
		const UnsignedInt now = timeGetTime();
		keepOnly = groupIndex == m_smartSelectionLastClickSlot && now - m_smartSelectionLastClickTime <= GetDoubleClickTime();
		m_smartSelectionLastClickSlot = keepOnly ? -1 : groupIndex;
		m_smartSelectionLastClickTime = now;
	}
	else
	{
		// Ctrl and Shift together, because Shift+Tab cycles the row: a click landing while that
		// Shift is still held must not throw the rest of the selection away.
		keepOnly = TheKeyboard && TheKeyboard->isCtrl() && TheKeyboard->isShift();
	}
	if( keepOnly )
	{
		smartSelectionRemove( groupIndex, TRUE );
	}
	else
	{
		smartSelectionFocus( groupIndex );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Step the focus to the next cameo that is not pushed in. */
//-------------------------------------------------------------------------------------------------
void ControlBar::smartSelectionCycle( Int direction )
{
	const Int groupCount = (Int)m_smartSelectionGroups.size();
	if( groupCount < 2 || direction == 0 )
	{
		return;
	}

	Int next = m_smartSelectionActive;
	for( Int step = 0; step < groupCount; step++ )
	{
		if( next < 0 )
		{
			next = direction > 0 ? 0 : groupCount - 1;
		}
		else
		{
			next = ( next + direction + groupCount ) % groupCount;
		}
		if( !isIndexSmartSelectionFocused( next ) )
		{
			smartSelectionFocus( next );
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Show one cameo's command set, or the common set again for -1. Nothing about the selection
	* changes; the dirty flag makes the context repopulate around the new focus. */
//-------------------------------------------------------------------------------------------------
void ControlBar::smartSelectionFocus( Int groupIndex )
{
	if (m_smartSelectionActive != groupIndex)
	{
		m_smartSelectionActive = groupIndex;
		updateFocusGroup();
	}
	refreshSmartSelectionButtons();
	markUIDirty();
}

//-------------------------------------------------------------------------------------------------
/** Drop a cameo's units from the selection, or every other unit for keepGroup. A cameo stands
	* for one object or for a whole type. Client side deselect plus one remove message, the same
	* shape as a shift click on a selected unit. The selection change rebuilds the row. */
//-------------------------------------------------------------------------------------------------
void ControlBar::smartSelectionRemove( Int groupIndex, Bool keepGroup )
{
	const SmartSelectionGroup &group = m_smartSelectionGroups[ groupIndex ];

	// gathered first, since deselecting walks the very list being read
	std::vector<Drawable *> members;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *obj = getSmartSelectionObject( *it );
		if( obj == nullptr )
		{
			continue;
		}
		const Bool inGroup = group.objectID != INVALID_ID ? obj->getID() == group.objectID : obj->getTemplate()->getReskinRoot() == group.thingTemplate;
		if( inGroup != keepGroup )
		{
			members.push_back( *it );
		}
	}
	if( members.empty() )
	{
		return;
	}

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_REMOVE_FROM_SELECTED_GROUP );
	for( size_t i = 0; i < members.size(); i++ )
	{
		msg->appendObjectIDArgument( members[ i ]->getObject()->getID() );
		TheInGameUI->deselectDrawable( members[ i ] );
	}

	// what is kept is one type, which shows its own command set, so it needs no focus either
	if( groupIndex == m_smartSelectionActive || keepGroup )
	{
		m_smartSelectionActive = -1;
		updateFocusGroup();
	}
}

//-------------------------------------------------------------------------------------------------
/** The logic side sends a command to every unit in the player's group that can do it, so a
	* command off the focused card would leak to any other unit with a matching one. The group
	* the command acts on goes ahead of it instead. The client selection is untouched. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateFocusGroup()
{
	const CommandButton* command = TheInGameUI->getGUICommand();

	// ShigureUi 15/09/2026 clear any pending GUICommannd (need target ofc) unless it doesn't act on focus group
	if (command && BitIsSet(command->getOptions(), COMMAND_OPTION_NEED_TARGET))
	{
		switch (command->getCommandType())
		{
			case GUI_COMMAND_ATTACK_MOVE:
			case GUI_COMMAND_GUARD:
			case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:	
			case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
			case GUI_COMMAND_REVERSE_MOVE:
				break;
			default:
				TheInGameUI->setGUICommand(nullptr);
				break;
		}
	}

	TheInGameUI->placeBuildAvailable(nullptr, nullptr);

	// Every shape of the focus lands in this list, and an empty one clears the logic side.
	// Returning without a message would leave it holding the previous focus, which then
	// filters the next command.
	std::vector<ObjectID> focusGroup;

	// the bar is that object's own, so anything out of it goes to that object alone
	const ObjectID focusObject = getSmartSelectionFocusObject();
	const ThingTemplate *focus = getSmartSelectionFocusTemplate();

	if( focusObject != INVALID_ID )
	{
		focusGroup.push_back( focusObject );
	}
	else if( focus )
	{
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			Object *obj = ( *it )->getObject();
			if( obj && obj->getTemplate()->getReskinRoot() == focus )
			{
				focusGroup.push_back( obj->getID() );
			}
		}
	}

	// the message is networked, and the callers run far more often than the focus changes
	if( focusGroup == m_sentFocusGroup )
	{
		return;
	}
	m_sentFocusGroup.swap( focusGroup );

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_UPDATE_FOCUSED_GROUP );
	for( size_t i = 0; i < m_sentFocusGroup.size(); i++ )
	{
		msg->appendObjectIDArgument( m_sentFocusGroup[ i ] );
	}
}
