/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/STLTypedefs.h"

#if defined(GENONLINE)

class Image;
class MapMetaData;
class ThingTemplate;

AsciiString resolveMapPreviewCategory( const AsciiString &templateName, const ThingTemplate *thingTemplate );

/// Category image, else the retail TecBuilding or Cash image. May return nullptr.
const Image *getMapPreviewCategoryImage( const AsciiString &category, Bool isTech );

UnicodeString getMapPreviewCategoryLabel( const AsciiString &category, Bool isTech );

// GenOnline @feature arcticdolphin 08/11/2026 Typed markers scale with the layout instead of the fixed SUPPLY_TECH_SIZE.
enum { MAP_PREVIEW_MARKER_LAYOUT_SIZE = 11 };

/// Typed marker size in screen pixels, 11 layout px scaled with the screen height.
Int getMapPreviewMarkerSize();

/// Fills m_typedMarkers from the typed objects of the cached map. Returns FALSE to keep the retail position lists.
Bool positionTypedPreviewImages( const MapMetaData *mmd, const ICoord2D &ul, Int smallWidth, Int smallHeight );

Bool getMapPreviewMarkerTooltip( Int x, Int y, UnicodeString &outTooltip );

struct MapPreviewLegendEntry
{
	AsciiString m_category; ///< icon table category name, empty for objects no rule matched
	const Image *m_image; ///< category image, or the retail TecBuilding or Cash image; may be nullptr
	UnicodeString m_label; ///< localized category label
	Int m_count; ///< number of objects of this category on the map
	Bool m_isTech; ///< the category's objects are tech buildings, else supply sources
};
typedef std::vector<MapPreviewLegendEntry> MapPreviewLegend;

/// Returns FALSE and leaves outLegend empty when the map is unknown or its typed objects cannot be read.
Bool getMapPreviewLegend( const AsciiString &mapName, MapPreviewLegend &outLegend );

#endif
