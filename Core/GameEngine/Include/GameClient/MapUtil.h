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

// FILE: MapUtil.h /////////////////////////////////////////////////////////
// Author: Matt Campbell, December 2001
// Description: Map utility/convenience routines
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"

#include "Common/STLTypedefs.h"

class GameWindow;
class AsciiString;
struct Coord3D;
struct FileInfo;
class Image;
class DataChunkInput;
struct DataChunkInfo;
// This matches the windows timestamp.
enum { SUPPLY_TECH_SIZE = 15};
typedef std::list <ICoord2D> ICoord2DList;

#if defined(GENONLINE)
// GenOnline @feature arcticdolphin 08/11/2026 Preview marker that carries its icon category.
struct MapPreviewMarker
{
	ICoord2D m_pos; ///< top left of the getMapPreviewMarkerSize() marker, relative to the preview window
	AsciiString m_category; ///< MapPreviewIcons category name, empty when no rule matched
	AsciiString m_templateName;
	Bool m_isTech; ///< KINDOF_TECH_BUILDING, else KINDOF_SUPPLY_SOURCE_ON_PREVIEW
};
typedef std::list<MapPreviewMarker> MapPreviewMarkerList;
#endif
#if defined(GENONLINE)

#endif
class TechAndSupplyImages
{
public:
	ICoord2DList m_techPosList;
	ICoord2DList m_supplyPosList;
#if defined(GENONLINE)
	// GenOnline @feature arcticdolphin 08/11/2026 Typed markers. When set, the two position lists above stay empty.
	MapPreviewMarkerList m_typedMarkers;
#endif
};

struct WinTimeStamp
{
	UnsignedInt m_lowTimeStamp;
	UnsignedInt m_highTimeStamp;
};


class WaypointMap : public std::map<AsciiString, Coord3D>
{
public:
	void update();	///< returns the number of multiplayer start spots found
	Int m_numStartSpots;
};

typedef std::list <Coord3D> Coord3DList;
#if defined(GENONLINE)

#endif
#if defined(GENONLINE)
// GenOnline @feature arcticdolphin 08/11/2026 Typed supply and tech objects of a map for the preview icons and mapcache.json.
struct MapTypedObject
{
	AsciiString m_templateName;
	AsciiString m_category; ///< MapPreviewIcons category name, empty when no rule matched
	Bool m_isTech; ///< KINDOF_TECH_BUILDING, else KINDOF_SUPPLY_SOURCE_ON_PREVIEW
	Coord3D m_pos;
};
typedef std::vector<MapTypedObject> MapTypedObjectVector;

class MapTypedObjects
{
public:
	MapTypedObjects() : m_isKnown(FALSE) {}

	MapTypedObjectVector m_objects;
	Bool m_isKnown; ///< TRUE once read from the map or mapcache.json; an empty list then means the map has none
};

Bool loadMapTypedObjects( const AsciiString &mapPath, MapTypedObjectVector &outObjects );
#endif

class MapMetaData
{
public:
	UnicodeString m_displayName;
	AsciiString m_nameLookupTag;
	Region3D m_extent;
	Int m_numPlayers;

	Bool m_isMultiplayer;
	Bool m_isOfficial;
	Bool m_doesExist; ///< Flag to indicate whether the map physically exists. Should be true.
	UnsignedInt m_filesize;
	UnsignedInt m_CRC;

	WinTimeStamp m_timestamp;

	WaypointMap m_waypoints;
	Coord3DList m_supplyPositions;
	Coord3DList m_techPositions;
	AsciiString m_fileName;
#if defined(GENONLINE)
	// GenOnline @feature arcticdolphin 08/11/2026 Typed objects for the preview icons, not written to MapCache.ini.
	MapTypedObjects m_typedObjects;
#endif
};

// TheSuperHackers @performance xezon 02/11/2025 Simplifies and improves the implementation of MapCache
// to prevent expensive reoccurring redundant map cache reads.

class MapCache : public std::map<AsciiString, MapMetaData>
{
	typedef std::set<AsciiString> MapNameSet;

public:
	MapCache()
		: m_doCreateStandardMapCacheINI(TRUE)
		, m_doLoadStandardMapCacheINI(TRUE)
		, m_doLoadUserMapCacheINI(TRUE)
	{}

	void updateCache();

#if defined(GENONLINE)
	AsciiString getMapDir(bool bCustomMapDebug = false) const;
	AsciiString getUserMapDir(bool bCustomMapDebug = false) const;
#else
	AsciiString getMapDir() const;
	AsciiString getUserMapDir() const;
#endif
	AsciiString getMapExtension() const;

	const MapMetaData *findMap(AsciiString mapName);

	// allow us to create a set of shippable maps to be in mapcache.ini.  For use with -buildMapCache.
	void addShippingMap(AsciiString mapName) { mapName.toLower(); m_allowedMaps.insert(mapName); }

private:
	static AsciiString getCachePath(const AsciiString &mapDir);
	void prepareUnseenMaps(const AsciiString &mapDir);
	Bool clearUnseenMaps(const AsciiString &mapDir);
#if defined(GENONLINE)
	Bool loadMapsFromMapCacheINI(const AsciiString &mapDir); // returns false if the cache file was unreadable
#else
	void loadMapsFromMapCacheINI(const AsciiString &mapDir);
#endif
	Bool loadMapsFromDisk(const AsciiString &mapDir, Bool isOfficial, Bool filterByAllowedMaps = FALSE); // returns true if we needed to (re)parse a map
	Bool addMap(const AsciiString &mapDir, const AsciiString &fname, const AsciiString &lowerFname, FileInfo &fileInfo, Bool isOfficial); ///< returns true if it had to (re)parse the map
	void writeCacheINI(const AsciiString &mapDir);

	static const char *const m_mapCacheName;

	MapNameSet m_allowedMaps;
	Bool m_doCreateStandardMapCacheINI;
	Bool m_doLoadStandardMapCacheINI;
	Bool m_doLoadUserMapCacheINI;
};

extern MapCache *TheMapCache;
extern TechAndSupplyImages TheSupplyAndTechImageLocations;

// TheSuperHackers @refactor xezon 28/11/2025 Refactors the map list population implementation
// by breaking it into smaller pieces to make it more maintainable.

Int populateMapListbox( GameWindow *listbox, Bool useSystemMaps, Bool isMultiplayer, AsciiString mapToSelect = AsciiString::TheEmptyString );		/// Read a list of maps from the run directory and fill in the listbox.  Return the selected index
Int populateMapListboxNoReset( GameWindow *listbox, Bool useSystemMaps, Bool isMultiplayer, AsciiString mapToSelect = AsciiString::TheEmptyString );		/// Read a list of maps from the run directory and fill in the listbox.  Return the selected index
Bool isValidMap( AsciiString mapName, Bool isMultiplayer );						/// Validate a map
Image *getMapPreviewImage( AsciiString mapName );
AsciiString getDefaultMap( Bool isMultiplayer );											/// Find a valid map
AsciiString getDefaultOfficialMap();
Bool isOfficialMap( AsciiString mapName );
Bool parseMapPreviewChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
void findDrawPositions( Int startX, Int startY, Int width, Int height, Region3D extent,
															 ICoord2D *ul, ICoord2D *lr );
Bool WouldMapTransfer( const AsciiString& mapName );
#if defined(GENONLINE)
UnicodeString getMapNameFromPath( const AsciiString& mapPath ); ///< Map file name without folders or the .map extension
#endif
