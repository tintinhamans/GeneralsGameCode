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

// FILE: MapPreviewIcons.h ////////////////////////////////////////////////////////////////////////
// Desc: Typed map preview icons. The data table in
//       Data\GeneralsOnline\MapPreviewIcons*.ini maps object templates to icon categories.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/STLTypedefs.h"

#if defined(GENONLINE)

class Image;
class MapMetaData;
class ThingTemplate;

// TheSuperHackers @feature arcticdolphin 08/11/2026 Icon categories and typed preview markers for the map preview.

/// Category name of the last table rule that matches the object, or empty when none matches.
AsciiString resolveMapPreviewCategory( const AsciiString &templateName, const ThingTemplate *thingTemplate );

/// Category image, else the retail "TecBuilding" or "Cash" image; may return nullptr.
const Image *getMapPreviewCategoryImage( const AsciiString &category, Bool isTech );

/// Localized category label, else the retail tech building or supply dock tooltip.
UnicodeString getMapPreviewCategoryLabel( const AsciiString &category, Bool isTech );

/// Fills TheSupplyAndTechImageLocations.m_typedMarkers from the typed objects of the cached map, parsing the map
/// once when it has none yet. Returns FALSE to keep the retail position lists.
Bool positionTypedPreviewImages( const MapMetaData *mmd, const ICoord2D &ul, Int smallWidth, Int smallHeight );

/// Tooltip of the typed marker at x, y relative to the preview window; returns FALSE when no typed marker is there.
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

/// Fills outLegend with the distinct icon categories on the map, in icon table order, one entry each with its count.
/// mapName is a MapCache path in any case. Parses the map once when it has no typed objects yet.
/// Returns FALSE and leaves outLegend empty when the map is unknown or its typed objects cannot be read.
Bool getMapPreviewLegend( const AsciiString &mapName, MapPreviewLegend &outLegend );

/// One line legend text such as "Supply dock x2, Oil derrick x4"; empty when getMapPreviewLegend fails.
UnicodeString getMapPreviewLegendText( const AsciiString &mapName );

#endif
