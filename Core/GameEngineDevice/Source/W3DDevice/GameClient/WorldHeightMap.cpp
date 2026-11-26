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

// WorldHeightMap.cpp
// Class to encapsulate height map.
// Author: John Ahlquist, April 2001

#define INSTANTIATE_WELL_KNOWN_KEYS

#include "windows.h"
#include "stdlib.h"
#include <limits.h>
#include "Common/STLTypedefs.h"

#include "Common/DataChunk.h"
//#include "Common/GameFileSystem.h"
#include "Common/FileSystem.h" // for LOAD_TEST_ASSETS
#include "Common/GlobalData.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"

#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"

#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/W3DShadow.h"

// @feature Ronin 10/05/2026 Normal-map N1.5b: DDS asset path for the normal atlas.
// DDSFileClass handles BC1/BC3 decode and BGRA conversion via Copy_Level_To_Surface.
// Note: this codebase's DDSFileClass is DXT-only (asserts on uncompressed BGRA8 DDS),
// so the helper below pre-screens the FourCC and fails soft for non-DXT inputs --
// uncompressed normal maps must ship as TGA, which the existing path already handles.
#include "WW3D2/ddsfile.h"

#include "Common/file.h"
#include "Utility/stringex.h"

// @feature Ronin 05/04/2026 Support HD terrain replacements without changing logical terrain coverage.
#include <algorithm>
#include <vector>

#define K_OBSOLETE_HEIGHT_MAP_VERSION 8

#define PATHFIND_CLIFF_SLOPE_LIMIT_F	9.8f

Int WorldHeightMap::getTerrainTexturePageForTileNdx(Short tileNdx) const
{
	// @feature Ronin 08/04/2026 Query terrain atlas page ownership from packed source tile indices.
	const Short baseNdx = tileNdx >> 2;
	if (baseNdx < 0 || baseNdx >= NUM_SOURCE_TILES) {
		return 0;
	}

	TileData* pTile = m_sourceTiles[baseNdx];
	if (pTile == nullptr) {
		return 0;
	}

	return pTile->m_texturePage;
}

Int WorldHeightMap::getEdgeTexturePageForBlendClass(Int edgeClass) const
{
	// @feature Ronin 08/04/2026 Query edge atlas page ownership from blend edge classes.
	if (edgeClass < 0 || edgeClass >= m_numEdgeTextureClasses) {
		return 0;
	}

	return m_edgeTextureClasses[edgeClass].texturePage;
}

Int WorldHeightMap::getTerrainTexturePageForCell(Int xIndex, Int yIndex) const
{
	// @feature Ronin 08/04/2026 Query base terrain atlas page ownership using draw-local cell coordinates.
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;

	const Int ndx = (yIndex * m_width) + xIndex;
	if (ndx < 0 || ndx >= m_dataSize || m_tileNdxes == nullptr) {
		return 0;
	}

	return getTerrainTexturePageForTileNdx(m_tileNdxes[ndx]);
}

Int WorldHeightMap::getAlphaTexturePageForCell(Int xIndex, Int yIndex) const
{
	// @feature Ronin 08/04/2026 Query alpha terrain atlas page ownership using draw-local cell coordinates.
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;

	const Int ndx = (yIndex * m_width) + xIndex;
	if (ndx < 0 || ndx >= m_dataSize || m_tileNdxes == nullptr) {
		return 0;
	}

	Short tileNdx = m_tileNdxes[ndx];
	const Short blendNdx = (m_blendTileNdxes != nullptr) ? m_blendTileNdxes[ndx] : 0;
	if (blendNdx > 0 && blendNdx < m_numBlendedTiles) {
		tileNdx = m_blendedTiles[blendNdx].blendNdx;
	}

	return getTerrainTexturePageForTileNdx(tileNdx);
}

Int WorldHeightMap::getExtraAlphaTexturePageForCell(Int xIndex, Int yIndex) const
{
	// @feature Ronin 09/04/2026 Query extra-blend terrain atlas page ownership using draw-local cell coordinates.
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;

	const Int ndx = (yIndex * m_width) + xIndex;
	if (ndx < 0 || ndx >= m_dataSize || m_tileNdxes == nullptr) {
		return 0;
	}

	Short tileNdx = m_tileNdxes[ndx];
	const Short blendNdx = (m_extraBlendTileNdxes != nullptr) ? m_extraBlendTileNdxes[ndx] : 0;
	if (blendNdx > 0 && blendNdx < m_numBlendedTiles) {
		tileNdx = m_blendedTiles[blendNdx].blendNdx;
	}

	return getTerrainTexturePageForTileNdx(tileNdx);
}

TextureClass* WorldHeightMap::getTerrainTexture(Int page)
{
	if (page < 0 || page >= m_numTerrainTexturePages) {
		return nullptr;
	}
	if (m_terrainTex == nullptr) {
		getTerrainTexture();
	}
	return m_terrainTextures[page];
}

TextureClass* WorldHeightMap::getAlphaTerrainTexture(Int page)
{
	if (page < 0 || page >= m_numTerrainTexturePages) {
		return nullptr;
	}
	if (m_alphaTerrainTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaTerrainTextures[page];
}

TextureClass* WorldHeightMap::getEdgeTerrainTexture(Int page)
{
	if (page < 0 || page >= m_numEdgeTexturePages) {
		return nullptr;
	}
	if (m_alphaEdgeTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaEdgeTextures[page];
}

// @bugfix Ronin 05/04/2026 Forward declare HD terrain helper before first use in readTexClass().
static Bool getSourceTilePixelExtentForLogicalWidth(InputStream* pStr, Int logicalWidth, Int* tilePixelExtent);


static Int getSupportedTextureSheetWidth(Int tileWidth, Int tileHeight)
{
	Int supportedWidth = WorldHeightMap::getMaxTextureSheetWidthInTiles();
	if (tileWidth < supportedWidth) {
		supportedWidth = tileWidth;
	}
	if (tileHeight < supportedWidth) {
		supportedWidth = tileHeight;
	}
	return supportedWidth;
}

// Add this new method.
/*static*/ Int WorldHeightMap::getMaxTextureSheetWidthInTiles()
{
	// @feature Ronin 04/04/2026 Allow terrain sheets larger than 10x10 up to the atlas-supported width.
	Int maxWidthFromTilePool = 0;
	while ((maxWidthFromTilePool + 1) * (maxWidthFromTilePool + 1) <= NUM_SOURCE_TILES) {
		++maxWidthFromTilePool;
	}

	const Int maxWidthFromTexture = TEXTURE_WIDTH / (TILE_PIXEL_EXTENT + TILE_OFFSET);
	if (maxWidthFromTexture < maxWidthFromTilePool) {
		return maxWidthFromTexture;
	}

	return maxWidthFromTilePool;
}

// -----------------------------------------------------------
static AsciiString validateName(AsciiString n, Int flags)
{

	return n;

}

/* ********* GDIFileStream class ****************************/
class GDIFileStream : public InputStream
{
protected:
	File* m_file;
public:
	GDIFileStream(File* pFile):m_file(pFile) {};
	virtual Int read(void *pData, Int numBytes) override {
			return(m_file->read(pData, numBytes));
	};
};


/* ********* MapObject class ****************************/
/*static*/ MapObject *MapObject::TheMapObjectListPtr = nullptr;
/*static*/ Dict MapObject::TheWorldDict;
static Int s_nextTreeID = 1;

MapObject::MapObject(Coord3D loc, AsciiString name, Real angle, Int flags, const Dict* props,
										 const ThingTemplate *thingTemplate )
{
	m_objectName = validateName( name, flags );
	m_thingTemplate = thingTemplate;
	m_nextMapObject = nullptr;
	m_location = loc;
	m_angle = normalizeAngle(angle);
	m_color = (0xff)<<8; // Bright green.
	m_flags = flags;
	m_renderObj = nullptr;
	m_shadowObj = nullptr;
	m_runtimeFlags = 0;
	m_treeID = (DrawableID)s_nextTreeID++;
	// Note - do NOT set TheKey_objectSelectable on creation - allow it to follow the .ini value unless specified by user action.  jba. [3/20/2003]
	if (props)
	{
		m_properties = *props;
	}
	else
	{
		m_properties.setInt(TheKey_objectInitialHealth, 100);
		m_properties.setBool(TheKey_objectEnabled, true);
		m_properties.setBool(TheKey_objectIndestructible, false);
		m_properties.setBool(TheKey_objectUnsellable, false);
		m_properties.setBool(TheKey_objectPowered, true);
		m_properties.setBool(TheKey_objectRecruitableAI, true);
		m_properties.setBool(TheKey_objectTargetable, false );
	}

	for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
		setBridgeRenderObject( (BridgeTowerType)i, nullptr );

}


MapObject::~MapObject()
{
	setRenderObj(nullptr);
	setShadowObj(nullptr);
	if (m_nextMapObject) {
		MapObject *cur = m_nextMapObject;
		MapObject *next;
		while (cur) {
			next = cur->getNext();
			cur->setNextMap(nullptr); // prevents recursion.
			deleteInstance(cur);
			cur = next;
		}
	}
	for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
		setBridgeRenderObject( (BridgeTowerType)i, nullptr );

}

MapObject *MapObject::duplicate()
{
	MapObject *pObj = newInstance( MapObject)(m_location, m_objectName, m_angle, m_flags, &m_properties, m_thingTemplate);
	pObj->setColor(getColor());
	pObj->m_runtimeFlags = m_runtimeFlags;
	return pObj;
}

void MapObject::setRenderObj(RenderObjClass *pObj)
{
	REF_PTR_SET(m_renderObj, pObj);
}

void MapObject::setBridgeRenderObject( BridgeTowerType type, RenderObjClass* renderObj )
{

	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		REF_PTR_SET( m_bridgeTowers[ type ], renderObj );

}

RenderObjClass* MapObject::getBridgeRenderObject( BridgeTowerType type )
{

	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		return m_bridgeTowers[ type ];
	return nullptr;

}

void MapObject::validate()
{
	verifyValidTeam();
	verifyValidUniqueID();
}

void MapObject::verifyValidTeam()
{
	// if this map object has a valid team, then do nothing.
	// if it has an invalid team, the place it on the default neutral team, (by clearing the
	// existing team name.)
	Bool exists;
	AsciiString teamName = getProperties()->getAsciiString(TheKey_originalOwner, &exists);
	if (exists) {
		Bool valid = false;

		int numSides = TheSidesList->getNumTeams();

		for (int i = 0; i < numSides; ++i) {
			TeamsInfo *teamInfo = TheSidesList->getTeamInfo(i);
			if (!teamInfo) {
				continue;
			}

			Bool itBetter;
			AsciiString testAgainstTeamName = teamInfo->getDict()->getAsciiString(TheKey_teamName, &itBetter);
			if (itBetter) {
				if (testAgainstTeamName.compare(teamName) == 0) {
					valid = true;
				}
			}
		}

		if (!valid) {
			getProperties()->remove(TheKey_originalOwner);
		}
	}
}

void MapObject::verifyValidUniqueID()
{
	Bool exists;
	AsciiString uniqueID = getProperties()->getAsciiString(TheKey_uniqueID, &exists);
	MapObject *obj = MapObject::getFirstMapObject();

	// -1 is the sentinel
	int highestIndex = -1;

	while (obj) {
		if (obj == this) {
			// the first object is THIS OBJECT, cause we've already been added.
			obj = obj->getNext();
			continue;
		}

		if (obj->isWaypoint()) {
			// waypoints throw this off. Sad but true. :-(
			obj = obj->getNext();
			continue;
		}

		Bool iterateExists;
		AsciiString tempStr = obj->getProperties()->getAsciiString(TheKey_uniqueID, &iterateExists);
		const char* lastSpace = tempStr.reverseFind(' ');

		int testIndex = -1;
		if (lastSpace) {
			testIndex = atoi(lastSpace);
		}

		if (testIndex > highestIndex) {
			highestIndex = testIndex;
		}
		break;
	}

	int indexOfThisObject = highestIndex + 1;

	const char* thingName;
	if (getThingTemplate()) {
		thingName = getThingTemplate()->getName().str();
	} else if (isWaypoint()) {
		thingName = getWaypointName().str();
	} else {
		thingName = getName().str();
	}
	const char* pName = thingName;

	while (*thingName) {
		if ((*thingName) == '/') {
			pName = thingName + 1;
		}
		++thingName;
	}

	AsciiString newID;
	if (isWaypoint()) {
		newID.format("%s", pName);
	} else {
		newID.format("%s %d", pName, indexOfThisObject);
	}
	getProperties()->setAsciiString(TheKey_uniqueID, newID);
}

void MapObject::fastAssignAllUniqueIDs()
{
	// here's what we do. Take all of them, push them onto a stack. Then, pop each one, setting its id.
	// should be much faster than what we currently do.

	MapObject *pMapObj = getFirstMapObject();

	std::stack<MapObject*> objStack;
	Int actualNumObjects = 0;

	while (pMapObj) {
		++actualNumObjects;
		objStack.push(pMapObj);
		pMapObj = pMapObj->getNext();
	}

	Int indexOfThisObject = 0;
	while (actualNumObjects) {
		MapObject *obj = objStack.top();


		const char* thingName;
		if (obj->getThingTemplate()) {
			thingName = obj->getThingTemplate()->getName().str();
		} else if (obj->isWaypoint()) {
			thingName = obj->getWaypointName().str();
		} else {
			thingName = obj->getName().str();
		}
		const char* pName = thingName;

		while (*thingName) {
			if ((*thingName) == '/') {
				pName = thingName + 1;
			}
			++thingName;
		}

		AsciiString newID;
		if (obj->isWaypoint()) {
			newID.format("%s", pName);
		} else {
			newID.format("%s %d", pName, indexOfThisObject);
		}

		obj->getProperties()->setAsciiString(TheKey_uniqueID, newID);
		objStack.pop();

		++indexOfThisObject;
		--actualNumObjects;
	}
}



void MapObject::setThingTemplate(const ThingTemplate *thing)
{
	m_thingTemplate = thing;
	if (thing) {
		m_objectName = thing->getName();
	}
}


void MapObject::setName(AsciiString name)
{
	m_objectName = name;
}

WaypointID MapObject::getWaypointID() { return (WaypointID)getProperties()->getInt(TheKey_waypointID); }
AsciiString MapObject::getWaypointName() { return getProperties()->getAsciiString(TheKey_waypointName); }
void MapObject::setWaypointID(Int i) { getProperties()->setInt(TheKey_waypointID, i); }
void MapObject::setWaypointName(AsciiString n) { getProperties()->setAsciiString(TheKey_waypointName, n); }

/*static */ Int MapObject::countMapObjectsWithOwner(const AsciiString& n)
{
	Int count = 0;
	for (MapObject *pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext())
	{
		if (pMapObj->getProperties()->getAsciiString(TheKey_originalOwner) == n)
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
const ThingTemplate *MapObject::getThingTemplate() const
{
	if (m_thingTemplate)
		return (const ThingTemplate*) m_thingTemplate->getFinalOverride();

	return nullptr;
}


/* ********* WorldHeightMap class ****************************/

TileData *WorldHeightMap::m_alphaTiles[NUM_ALPHA_TILES]={0};

// @bugfix Ronin 26/04/2026 See WorldHeightMap.h for rationale.
Int WorldHeightMap::s_instanceCount = 0;

//
// WorldHeightMap destructor .
//
WorldHeightMap::~WorldHeightMap()
{
	delete[](m_data);
	m_data = nullptr;

	delete[](m_tileNdxes);
	m_tileNdxes = nullptr;

	delete[](m_blendTileNdxes);
	m_blendTileNdxes = nullptr;

	delete[](m_extraBlendTileNdxes);
	m_extraBlendTileNdxes = nullptr;

	delete[](m_cliffInfoNdxes);
	m_cliffInfoNdxes = nullptr;

	delete[](m_cellFlipState);
	m_cellFlipState = nullptr;

	delete[](m_seismicUpdateFlag);
	m_seismicUpdateFlag = nullptr;

	delete[](m_seismicZVelocities);
	m_seismicZVelocities = nullptr;

	delete[](m_cellCliffState);
	m_cellCliffState = nullptr;

	int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		REF_PTR_RELEASE(m_sourceTiles[i]);
		REF_PTR_RELEASE(m_edgeTiles[i]);
	}

	// @bugfix Ronin 26/04/2026 Release the shared static alpha-mask tiles when the LAST
	// WorldHeightMap dies. Decrement first so a partially-constructed instance that
	// threw before the matching ++ in the ctor cannot cause an underflow here.
	if (s_instanceCount > 0 && --s_instanceCount == 0) {
		for (Int alphaNdx = 0; alphaNdx < NUM_ALPHA_TILES; ++alphaNdx) {
			REF_PTR_RELEASE(m_alphaTiles[alphaNdx]);
		}
	}

	for (i = 0; i < MAX_TEXTURE_ATLAS_PAGES; ++i) {
		// @feature Ronin 08/04/2026 Release per-page terrain texture storage before releasing legacy page-0 aliases.
		REF_PTR_RELEASE(m_terrainTextures[i]);
		REF_PTR_RELEASE(m_alphaTerrainTextures[i]);
		REF_PTR_RELEASE(m_alphaEdgeTextures[i]);
	}

	// @feature Ronin 27/04/2026 Splat S20-A2a: release per-material weight atlas pages.
	for (Int wpage = 0; wpage < MAX_S20_WEIGHT_ATLAS_PAGES; ++wpage) {
		REF_PTR_RELEASE(m_perMaterialWeightAtlas[wpage]);
	}

	m_numPerMaterialWeightAtlasPages = 0;

	// @feature Ronin 10/05/2026 Normal-map N1: release per-material normal atlas pages.
	// Lifecycle mirrors the weight atlas above; one page per diffuse atlas page.
	for (Int npage = 0; npage < MAX_TEXTURE_ATLAS_PAGES; ++npage) {
		REF_PTR_RELEASE(m_perMaterialNormalAtlas[npage]);
	}
	m_numPerMaterialNormalAtlasPages = 0;


	REF_PTR_RELEASE(m_terrainTex);
	REF_PTR_RELEASE(m_alphaTerrainTex);
	REF_PTR_RELEASE(m_alphaEdgeTex);
}

void WorldHeightMap::freeListOfMapObjects()
{
	if (MapObject::TheMapObjectListPtr)
	{
		deleteInstance(MapObject::TheMapObjectListPtr);
		MapObject::TheMapObjectListPtr = nullptr;
	}
	MapObject::getWorldDict()->clear();
}


/**
 WorldHeightMap - create a new height map for class WorldHeightMap.
 Note that there is 1 m_numBlendedTiles, which is the implied
 transparent tile for non-blended tiles.
*/
WorldHeightMap::WorldHeightMap():
	m_width(0), m_height(0),  m_dataSize(0), m_data(nullptr), m_cellFlipState(nullptr), m_seismicUpdateFlag(nullptr), m_seismicZVelocities(nullptr),
	m_drawOriginX(0), m_drawOriginY(0),
	m_numTextureClasses(0),
	m_drawWidthX(NORMAL_DRAW_WIDTH), m_drawHeightY(NORMAL_DRAW_HEIGHT),
	m_tileNdxes(nullptr), m_blendTileNdxes(nullptr), m_extraBlendTileNdxes(nullptr), m_cliffInfoNdxes(nullptr),
#ifdef EVAL_TILING_MODES
	m_tileMode(TILE_4x4),
#endif
	m_numCliffInfo(1),
	m_terrainTex(nullptr), m_alphaTerrainTex(nullptr), m_numBitmapTiles(0), m_numBlendedTiles(1),
	m_primaryBlendTexelsPerCell(16),
	m_primaryBlendControlTextureDirty(TRUE)
{
	Int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		m_sourceTiles[i] = nullptr;
		m_edgeTiles[i] = nullptr;
	}

	m_alphaEdgeTex = nullptr;
	m_alphaEdgeHeight = 1;

	m_numTerrainTexturePages = 1;
	m_numEdgeTexturePages = 1;
	for (i = 0; i < MAX_TEXTURE_ATLAS_PAGES; ++i) {
		// @feature Ronin 08/04/2026 Initialize per-page terrain texture storage for multi-page atlases.
		m_terrainTextures[i] = nullptr;
		m_alphaTerrainTextures[i] = nullptr;
		m_alphaEdgeTextures[i] = nullptr;
		m_terrainTexturePages[i].textureHeight = 0;
		m_edgeTexturePages[i].textureHeight = 0;
	}
	m_terrainTexturePages[0].textureHeight = m_terrainTexHeight;
	m_edgeTexturePages[0].textureHeight = m_alphaEdgeHeight;

	TheSidesList->validateSides();
	setupAlphaTiles();

	// @bugfix Ronin 26/04/2026 Increment AFTER setupAlphaTiles() succeeds so a throw
	// during construction does not leave the static alpha tiles orphaned with no
	// matching destructor decrement. See ~WorldHeightMap().
	++s_instanceCount;
}

#ifdef EVAL_TILING_MODES
static Bool ParseFunkyTilingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)userData;
	*((Int *)&pThis->m_tileMode) = file.readInt();
	return true;
}
#endif

/**
* WorldHeightMap - read a height map from a file.
* Format is  Chunky.
*
*	Input: ChunkInputStream,
*
*/
WorldHeightMap::WorldHeightMap(ChunkInputStream *pStrm, Bool logicalDataOnly):
	m_width(0), m_height(0),  m_dataSize(0), m_data(nullptr), m_cellFlipState(nullptr), m_seismicUpdateFlag(nullptr), m_seismicZVelocities(nullptr),
	m_drawOriginX(0),	m_cellCliffState(nullptr), m_drawOriginY(0),
	m_numTextureClasses(0),
	m_drawWidthX(NORMAL_DRAW_WIDTH), m_drawHeightY(NORMAL_DRAW_HEIGHT),
	m_tileNdxes(nullptr), m_blendTileNdxes(nullptr), m_extraBlendTileNdxes(nullptr), m_cliffInfoNdxes(nullptr),
	m_terrainTexHeight(1), m_alphaTexHeight(1),
#ifdef EVAL_TILING_MODES
	m_tileMode(TILE_4x4),
#endif
	m_numCliffInfo(1),
	m_terrainTex(nullptr), m_alphaTerrainTex(nullptr), m_numBitmapTiles(0), m_numBlendedTiles(1),
	m_primaryBlendTexelsPerCell(4),
	m_primaryBlendControlTextureDirty(TRUE)
{

	int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		m_sourceTiles[i]=nullptr;
		m_edgeTiles[i]=nullptr;
	}

	m_alphaEdgeTex = nullptr;
	m_alphaEdgeHeight = 1;

	m_numTerrainTexturePages = 1;
	m_numEdgeTexturePages = 1;
	for (i = 0; i < MAX_TEXTURE_ATLAS_PAGES; ++i) {
		// @feature Ronin 08/04/2026 Initialize per-page terrain texture storage for multi-page atlases.
		m_terrainTextures[i] = nullptr;
		m_alphaTerrainTextures[i] = nullptr;
		m_alphaEdgeTextures[i] = nullptr;
		m_terrainTexturePages[i].textureHeight = 0;
		m_edgeTexturePages[i].textureHeight = 0;
	}
	m_terrainTexturePages[0].textureHeight = m_terrainTexHeight;
	m_edgeTexturePages[0].textureHeight = m_alphaEdgeHeight;

	DataChunkInput file( pStrm );

	if (logicalDataOnly) {
		file.registerParser( "HeightMapData", AsciiString::TheEmptyString, ParseSizeOnlyInChunk );
		file.registerParser( "WorldInfo", AsciiString::TheEmptyString, ParseWorldDictDataChunk );
		file.registerParser( "ObjectsList", AsciiString::TheEmptyString, ParseObjectsDataChunk );
		freeListOfMapObjects(); // just in case.
		file.registerParser( "PolygonTriggers", AsciiString::TheEmptyString, PolygonTrigger::ParsePolygonTriggersDataChunk );
		PolygonTrigger::deleteTriggers(); // just in case.
		TheSidesList->emptySides();
		file.registerParser("SidesList", AsciiString::TheEmptyString,	SidesList::ParseSidesDataChunk );
	}	else {
		file.registerParser( "HeightMapData", AsciiString::TheEmptyString, ParseHeightMapDataChunk );
		file.registerParser( "BlendTileData", AsciiString::TheEmptyString, ParseBlendTileDataChunk );
#ifdef EVAL_TILING_MODES
		file.registerParser( "FUNKY_TILING", AsciiString::TheEmptyString, ParseFunkyTilingDataChunk );
#endif
		file.registerParser( "GlobalLighting", AsciiString::TheEmptyString, ParseLightingDataChunk );
	}
	if (!file.parse(this)) {

		throw(ERROR_CORRUPT_FILE_FORMAT);
	}
	// patch bad maps.
	if (!logicalDataOnly) {
		for(i=0; i<m_dataSize; i++) {
			if (m_cliffInfoNdxes[i]<0 || m_cliffInfoNdxes[i]>= m_numCliffInfo) {
				m_cliffInfoNdxes[i] = 0;
			}
			if (m_blendTileNdxes[i]<0 || m_blendTileNdxes[i]>= m_numBlendedTiles) {
				m_blendTileNdxes[i] = 0;
			}
			if (m_extraBlendTileNdxes[i]<0 || m_extraBlendTileNdxes[i]>= m_numBlendedTiles) {
				m_extraBlendTileNdxes[i] = 0;
			}
		}
	}

	if (m_drawWidthX > m_width) {
		m_drawWidthX = m_width;
	}
	if (m_drawHeightY > m_height) {
		m_drawHeightY = m_height;
	}

	TheSidesList->validateSides();
	setupAlphaTiles();

	// @bugfix Ronin 26/04/2026 Increment AFTER setupAlphaTiles() succeeds so a throw
	// during construction does not leave the static alpha tiles orphaned with no
	// matching destructor decrement. See ~WorldHeightMap().
	++s_instanceCount;
}

Bool WorldHeightMap::isTerrainFlat(const IRegion2D& bounds) const
{
	DEBUG_ASSERTCRASH(getLogicalBounds().contains(bounds), ("WorldHeightMap::isTerrainFlat must use a region contained by the logical map bounds."));

	const Int width = bounds.width();
	const Int height = bounds.height();
	if (width <= 0 || height <= 0)
		return true;

	const UnsignedByte* firstRow = m_data + (bounds.lo.y + m_borderSize) * m_width + bounds.lo.x + m_borderSize;
	const UnsignedByte referenceHeight = firstRow[0];
	for (Int j = 0; j < height; j++)
	{
		const UnsignedByte* row = firstRow + j * m_width;
		if (memcchr(row, referenceHeight, width) != nullptr)
			return false;
	}

	return true;
}

/** Optimized version of method to get triangle flip state of a terrain cell.  Use this
*	instead of getAlphaUVData() whenever possible.
*/
Bool WorldHeightMap::getFlipState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellFlipState) return false;
	return m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

/** Sets the value of the flip state bit.
*/
void WorldHeightMap::setFlipState(Int xIndex, Int yIndex, Bool value)
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_cellFlipState) return ;
	UnsignedByte *curVal = &m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)];
	if (value) {
		*curVal |= (1<<(xIndex&0x7));
	}	else {
		*curVal &= ~(1<<(xIndex&0x7));
	}
}

/** Clears all flip state bits.
*/
void WorldHeightMap::clearFlipStates() {
	if (m_cellFlipState) {
		memset(m_cellFlipState,0,m_flipStateWidth*m_height);	//clear all flags
	}
}

//////////////////////////////////////////////////////////////////////////////m_SeismicUpdateFlag
Bool WorldHeightMap::getSeismicUpdateFlag(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_seismicUpdateFlag) return false;
	return m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}
void WorldHeightMap::setSeismicUpdateFlag(Int xIndex, Int yIndex, Bool value)
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_seismicUpdateFlag) return ;
	UnsignedByte *curVal = &m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)];
	if (value) {
		*curVal |= (1<<(xIndex&0x7));
	}	else {
		*curVal &= ~(1<<(xIndex&0x7));
	}
}
void WorldHeightMap::clearSeismicUpdateFlags()
{
	if (m_seismicUpdateFlag) {
		memset(m_seismicUpdateFlag,0,m_seismicUpdateWidth*m_height);	//clear all flags
	}
}

///////////////////////////////////////////////m_SeismicZVelocities
Real WorldHeightMap::getSeismicZVelocity(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_seismicZVelocities) return false;
	return m_seismicZVelocities[yIndex*m_width + xIndex];
}
void WorldHeightMap::setSeismicZVelocity(Int xIndex, Int yIndex, Real value)
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_seismicZVelocities) return ;
	m_seismicZVelocities[yIndex*m_width + xIndex] = value;
}
void WorldHeightMap::fillSeismicZVelocities( Real value )
{
	if (!m_seismicZVelocities) return ;
  for (Int idx = 0; idx < m_width*m_height; ++idx)
    m_seismicZVelocities[idx] = value;
}

Real WorldHeightMap::getBilinearSampleSeismicZVelocity( Int x, Int y)
{
	if ( x < 0 || y < 0 ) return 0;
	if ( y >= m_height ) return 0;
	if ( x >= m_width ) return 0;
	if (!m_seismicZVelocities) return 0;

  Real collector = 0.0f;
  Real divisor = 0.0f;

  collector += m_seismicZVelocities[ y * m_width + x ];
  ++divisor;

  if ( y > 0 )
  {
    collector += m_seismicZVelocities[ (y-1) * m_width + x ];//bottom
    ++divisor;

    if( x > 0 )
    {
      collector += m_seismicZVelocities[ (y-1) * m_width + (x-1) ];//lower left
      ++divisor;
    }
    if ( x < m_width-1 )
    {
      collector += m_seismicZVelocities[ (y-1) * m_width + (x+1) ];//lower right
      ++divisor;
    }
  }
  if ( y < m_height-1 )
  {
    collector += m_seismicZVelocities[ (y+1) * m_width + x ];//top
    ++divisor;

    if( x > 0 )
    {
      collector += m_seismicZVelocities[ (y+1) * m_width + (x-1) ];//upper left
      ++divisor;
    }
    if ( x < m_width-1 )
    {
      collector += m_seismicZVelocities[ (y+1) * m_width + (x+1) ];//upper right
      ++divisor;
    }
  }
  if( x > 0 )
  {
    collector += m_seismicZVelocities[ y * m_width + (x-1) ];//left
    ++divisor;
  }
  if ( x < m_width-1 )
  {
    collector += m_seismicZVelocities[ y * m_width + (x+1) ];//right
    ++divisor;
  }

  collector /= divisor;

  return collector;

}

/** Get whether the cell is a cliff cell (impassable to ground vehicles).
*/
Bool WorldHeightMap::getCliffState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellCliffState) return false;
	return m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

//=============================================================================
// setCliffState
//=============================================================================
/** Sets the cliff state for a given cell. */
//=============================================================================
void WorldHeightMap::setCliffState(Int xIndex, Int yIndex, Bool state)
{
	if (xIndex<0 || yIndex<0) return;
	if (yIndex>=m_height) return;
	if (xIndex>=m_width) return;
	if (!m_cellCliffState) return;
	UnsignedByte	flagByte = m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)];
	UnsignedByte flagMask = (1<<(xIndex&0x7));
	if (state) {
		flagByte |= flagMask;
	} else {
		flagByte &= (~flagMask);
	}
	m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] = flagByte;
}

Bool WorldHeightMap::ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Dict d = file.readDict();
	*MapObject::getWorldDict() = d;
	Bool exists;
	Int theWeather = MapObject::getWorldDict()->getInt(TheKey_weather, &exists);
	if (exists) {
		TheWritableGlobalData->m_weather = (Weather) theWeather;
	}
	return true;
}

/**
* WorldHeightMap::ParseLightingDataChunk - read a global lights chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseLightingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
		TheWritableGlobalData->m_timeOfDay = (TimeOfDay)file.readInt();
		Int i;
		GlobalData::TerrainLighting	initLightValues	= { { 0,0,0},{0,0,0},{0,0,-1.0f}};

		// initialize the directions of the lights to not be totally invalid, in case old maps are read
		for (i=0; i<4; i++) {
			for (Int j=0;j<MAX_GLOBAL_LIGHTS; j++) {
				TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j]=initLightValues;
				TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j]=initLightValues;
			}
		}

		for (i=0; i<4; i++) {
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.red = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.green = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.blue = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.red = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.green = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.blue = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.x = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.y = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.z = file.readReal();

			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.red = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.green = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.blue = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.red = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.green = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.blue = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.x = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.y = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.z = file.readReal();

			if (info->version >= K_LIGHTING_VERSION_2) {
				for (Int j=1; j<3; j++)	//added support for 2 extra object lights
				{
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.red = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.green = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.blue = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.red = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.green = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.blue = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.x = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.y = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.z = file.readReal();
				}
			}
			if (info->version >= K_LIGHTING_VERSION_3) {
				for (Int j=1; j<3; j++)	//added support for 2 extra terrain lights
				{
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.red = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.green = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.blue = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.red = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.green = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.blue = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.x = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.y = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.z = file.readReal();
				}
			}
		}
		if (!file.atEndOfChunk()) {
			UnsignedInt shadowColor = file.readInt();
			if (TheW3DShadowManager) {
				TheW3DShadowManager->setShadowColor(shadowColor);
			}
		}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/**
* WorldHeightMap::ParseObjectsDataChunk - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	file.m_currentObject = nullptr;
	file.registerParser( "Object", info->label, ParseObjectDataChunk );
	return (file.parse(userData));
}

Bool WorldHeightMap::isValidTotalMapSize(Int width, Int height, Int borderSize)
{
	if (width <= 0 || width > MAX_MAP_DIMENSION)
		return false;
	if (height <= 0 || height > MAX_MAP_DIMENSION)
		return false;

	if (borderSize < 0 ||
		borderSize > (width - 1) / 2 ||
		borderSize > (height - 1) / 2)
		return false;

	if (width * height > MAX_MAP_AREA)
		return false;

	return true;
}

Bool WorldHeightMap::isValidPlayableMapSize(Int width, Int height, Int borderSize)
{
	if (width <= 0 || height <= 0 || borderSize < 0)
		return false;

	if (borderSize > (INT_MAX - width) / 2 ||
		borderSize > (INT_MAX - height) / 2)
		return false;

	return isValidTotalMapSize(
		width + 2 * borderSize,
		height + 2 * borderSize,
		borderSize);
}

static Bool isValidHeightDataSize(DataChunkInput &file, Int dataSize, Int width, Int height)
{
	return dataSize > 0 && dataSize == width * height && (UnsignedInt)dataSize <= file.getChunkDataSizeLeft();
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)userData;
	return pThis->ParseHeightMapData(file, info, userData);
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}

	if (!isValidTotalMapSize(m_width, m_height, m_borderSize)) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	if (!isValidHeightDataSize(file, m_dataSize, m_width, m_height)) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}

	m_data = MSGNEW("WorldHeightMap_ParseHeightMapData") UnsignedByte[m_dataSize];

	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;
	m_seismicUpdateWidth=numBytesX;
	m_seismicUpdateFlag	= MSGNEW("WorldHeightMap::ParseHeightMapData _ m_seismicUpdateFlag allocated") UnsignedByte[numBytesX*numBytesY];
  clearSeismicUpdateFlags();
  m_seismicZVelocities = MSGNEW("WorldHeightMap_ParseHeightMapData _ zvelocities allocated") Real[m_dataSize];
  fillSeismicZVelocities( 0 );


	file.readArrayOfBytes((char *)m_data, m_dataSize);
	// Resize me.
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseSizeOnlyInChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)userData;
	return pThis->ParseSizeOnly(file, info, userData);
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}

	if (!isValidTotalMapSize(m_width, m_height, m_borderSize)) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	if (!isValidHeightDataSize(file, m_dataSize, m_width, m_height)) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}

	m_data = MSGNEW("WorldHeightMap_ParseSizeOnly") UnsignedByte[m_dataSize];
	file.readArrayOfBytes((char *)m_data, m_dataSize);
	// Resize me.
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
		m_width = newWidth;
		m_height = newHeight;
	}
	return true;
}

/**
* WorldHeightMap::ParseBlendTileDataChunk - read a blend tile info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)userData;
	return pThis->ParseBlendTileData(file, info, userData);
}

/** Function to read in the tiles for a texture class. */
void WorldHeightMap::readTexClass(TXTextureClass* texClass, TileData** tileData)
{
	char resolvedPath[_MAX_PATH];
	resolvedPath[0] = 0;
	File* theFile = nullptr;

	const char* textureKind = "Unknown";
	if (tileData == m_sourceTiles) {
		textureKind = "Base";
	}
	else if (tileData == m_edgeTiles) {
		textureKind = "Edge";
	}

	// get the file from the description in TheTerrainTypes
	TerrainType* terrain = TheTerrainTypes->findTerrain(texClass->name);
	char texturePath[_MAX_PATH];
	if (terrain == nullptr)
	{
#ifdef LOAD_TEST_ASSETS
		snprintf(resolvedPath, ARRAY_SIZE(resolvedPath), "%s", texClass->name.str());
		theFile = TheFileSystem->openFile(resolvedPath, File::READ | File::BINARY);
#endif
	}
	else
	{
		snprintf(texturePath, ARRAY_SIZE(texturePath), "%s%s", TERRAIN_TGA_DIR_PATH, terrain->getTexture().str());
		snprintf(resolvedPath, ARRAY_SIZE(resolvedPath), "%s", texturePath);
		theFile = TheFileSystem->openFile(texturePath, File::READ | File::BINARY);
	}

	if (theFile == nullptr) {
		DEBUG_LOG(("MapTerrainTexture: kind=%s class=%s path=%s logicalWidth=%d numTiles=%d firstTile=%d load=open_failed",
			textureKind,
			texClass->name.str(),
			resolvedPath[0] ? resolvedPath : "<unresolved>",
			texClass->width,
			texClass->numTiles,
			texClass->firstTile));
		return;
	}

	GDIFileStream theStream(theFile);
	InputStream* pStr = &theStream;

	Int sourceTilePixelExtent = TILE_PIXEL_EXTENT;
	texClass->texturePage = 0;
	if (getSourceTilePixelExtentForLogicalWidth(pStr, texClass->width, &sourceTilePixelExtent)) {
		theFile->seek(0, File::START);
		texClass->tilePixelExtent = sourceTilePixelExtent;

		const Bool ok = WorldHeightMap::readTiles(pStr, tileData + texClass->firstTile, texClass->width, sourceTilePixelExtent);
		if (ok) {
			for (Int tileIndex = 0; tileIndex < texClass->numTiles; ++tileIndex) {
				TileData* pTile = tileData[texClass->firstTile + tileIndex];
				if (pTile == nullptr) {
					continue;
				}
				pTile->m_texturePage = 0;
				pTile->m_tileLocationInTexture.x = 0;
				pTile->m_tileLocationInTexture.y = 0;
			}
		}

		DEBUG_LOG(("MapTerrainTexture: kind=%s class=%s path=%s logicalWidth=%d numTiles=%d tilePixelExtent=%d firstTile=%d load=%s",
			textureKind,
			texClass->name.str(),
			resolvedPath[0] ? resolvedPath : "<unresolved>",
			texClass->width,
			texClass->numTiles,
			texClass->tilePixelExtent,
			texClass->firstTile,
			ok ? "ok" : "read_failed"));
	}
	else {
		DEBUG_LOG(("MapTerrainTexture: kind=%s class=%s path=%s logicalWidth=%d numTiles=%d firstTile=%d load=invalid_layout",
			textureKind,
			texClass->name.str(),
			resolvedPath[0] ? resolvedPath : "<unresolved>",
			texClass->width,
			texClass->numTiles,
			texClass->firstTile));
	}

	theFile->close();
}

/**
* WorldHeightMap::ParseBlendTileData - read a blend tile info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseBlendTileData(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	int i, j;
	Int len = file.readInt();
	if (m_dataSize != len) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	m_tileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_cliffInfoNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_blendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_extraBlendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	// Note - we have one less cell than the width & height. But for paranoia, allocate
	// extra row. jba.
	//
	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;

	m_flipStateWidth=numBytesX;

	m_cellFlipState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	m_cellCliffState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	memset(m_cellFlipState,0,numBytesX*numBytesY);	//clear all flags
	memset(m_cellCliffState,0,numBytesX*numBytesY);	//clear all flags

	file.readArrayOfBytes((char*)m_tileNdxes, m_dataSize*sizeof(Short));
	file.readArrayOfBytes((char*)m_blendTileNdxes, m_dataSize*sizeof(Short));
	if (info->version >= K_BLEND_TILE_VERSION_6) {
		file.readArrayOfBytes((char*)m_extraBlendTileNdxes, m_dataSize*sizeof(Short));
		//Allow clearing of extra blend tiles via ini and resaving of map.
		//Useful for flushing out initial maps made with buggy 3-way blending.
		if (!TheGlobalData->m_use3WayTerrainBlends)
			memset(m_extraBlendTileNdxes,0,m_dataSize*sizeof(Short));
	}
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		file.readArrayOfBytes((char*)m_cliffInfoNdxes, m_dataSize*sizeof(Short));
	}
	if (info->version >= K_BLEND_TILE_VERSION_7) {
		if (info->version==K_BLEND_TILE_VERSION_7) {
			// previous incorrect length that got used to save the file.  jba. [4/3/2003]
			Int byteWidth = (m_width+1)/8;
			UnsignedByte *data = new UnsignedByte[m_height*byteWidth];
			file.readArrayOfBytes((char*)data, m_height*byteWidth);
			for (j=0; j<m_height; j++) {
				for (i=0; i<byteWidth; i++) {
					m_cellCliffState[j*m_flipStateWidth + i] = data[j*byteWidth + i];
				}
			}
			delete[] data;
		} else {
			file.readArrayOfBytes((char*)m_cellCliffState, m_height*m_flipStateWidth);
		}
	} else {
		initCliffFlagsFromHeights();
	}
#if RTS_GENERALS && RETAIL_COMPATIBLE_CRC
	// Retail Generals keeps the cliff bits of maps up to version 7 in rows of (width+1)/8 bytes, which are too short for
	// the last cells of a row. These cells share their bits with the first cells of the next row, and the pathfinder
	// relies on the shared values, so they are copied here.
	if (info->version <= K_BLEND_TILE_VERSION_7) {
		const Int retailRowCells = 8*((m_width+1)/8);
		for (j=0; j<m_height-1; ++j) {
			for (i=retailRowCells; i<m_width-1; ++i) {
				if (info->version == K_BLEND_TILE_VERSION_7) {
					// The file holds the shared bits in the first cells of the next row.
					setCliffState(i, j, getCliffState(i-retailRowCells, j+1));
				} else {
					// The flags are computed column by column, so the last cells of a row are written last.
					setCliffState(i-retailRowCells, j+1, getCliffState(i, j));
				}
			}
		}
	}
	// Retail Generals also reads the cliff bits of version 8 maps in rows of (width+1)/8 bytes, but Zero Hour writes them
	// in rows of (width+7)/8 bytes. When the rows differ in length, the bits are regrouped into the rows retail reads.
	if (info->version == K_BLEND_TILE_VERSION_8 && m_flipStateWidth != (m_width+1)/8) {
		const Int retailByteWidth = (m_width+1)/8;
		UnsignedByte *data = new UnsignedByte[m_height*m_flipStateWidth];
		memcpy(data, m_cellCliffState, m_height*m_flipStateWidth);
		for (j=0; j<m_height-1; ++j) {
			for (i=0; i<m_width-1; ++i) {
				setCliffState(i, j, (data[j*retailByteWidth + (i>>3)] >> (i&7)) & 1);
			}
		}
		delete[] data;
	}
#elif !RETAIL_COMPATIBLE_CRC
	// Version 7 files have no bits for the last cells of a row, so they are computed from the heights.
	if (info->version == K_BLEND_TILE_VERSION_7) {
		const Int retailRowCells = 8*((m_width+1)/8);
		for (j=0; j<m_height-1; ++j) {
			for (i=retailRowCells; i<m_width-1; ++i) {
				setCellCliffFlagFromHeights(i, j);
			}
		}
	}
#endif
	m_numBitmapTiles = file.readInt();
	DEBUG_ASSERTCRASH(m_numBitmapTiles>0 && m_numBitmapTiles<2048, ("Unlikely numBitmapTiles."));
	m_numBlendedTiles = file.readInt();
	DEBUG_ASSERTCRASH(m_numBlendedTiles>0 && m_numBlendedTiles<NUM_BLEND_TILES+1, ("Unlikely numBlendedTiles."));
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		m_numCliffInfo = file.readInt();
	} else {
		m_numCliffInfo = 1;	// cliffInfo[0] is the default info.
	}
// --> file loading here
	m_numTextureClasses = file.readInt();
	DEBUG_ASSERTCRASH(m_numTextureClasses > 0 && m_numTextureClasses < 200, ("Unlikely m_numTextureClasses."));
	for (i = 0; i < m_numTextureClasses; i++) {
		m_textureClasses[i].globalTextureClass = -1;
		m_textureClasses[i].firstTile = file.readInt();
		m_textureClasses[i].numTiles = file.readInt();
		m_textureClasses[i].width = file.readInt();
		m_textureClasses[i].tilePixelExtent = TILE_PIXEL_EXTENT;
		m_textureClasses[i].texturePage = 0;


		// legacy GDF data
		// used to read "m_textureClasses[i].isGDF = file.readInt();"
		/* Int legacy = */ file.readInt();

		m_textureClasses[i].name = file.readAsciiString();
		readTexClass(&m_textureClasses[i], m_sourceTiles);
	}

	m_numEdgeTextureClasses = 0;
	m_numEdgeTiles = 0;
	if (info->version >= K_BLEND_TILE_VERSION_4) {
		m_numEdgeTiles = file.readInt();
		m_numEdgeTextureClasses = file.readInt();
		for (i = 0; i < m_numEdgeTextureClasses; i++) {
			m_edgeTextureClasses[i].globalTextureClass = -1;
			m_edgeTextureClasses[i].firstTile = file.readInt();
			m_edgeTextureClasses[i].numTiles = file.readInt();
			m_edgeTextureClasses[i].width = file.readInt();
			m_edgeTextureClasses[i].tilePixelExtent = TILE_PIXEL_EXTENT;
			m_edgeTextureClasses[i].texturePage = 0;
			m_edgeTextureClasses[i].name = file.readAsciiString();
			readTexClass(&m_edgeTextureClasses[i], m_edgeTiles);
		}
	}
	for (i=1; i<m_numBlendedTiles; i++) {
		Int flag;
		m_blendedTiles[i].blendNdx = file.readInt();
		m_blendedTiles[i].horiz = file.readByte();
		m_blendedTiles[i].vert = file.readByte();
		m_blendedTiles[i].rightDiagonal = file.readByte();
		m_blendedTiles[i].leftDiagonal = file.readByte();
		m_blendedTiles[i].inverted = file.readByte();
		//Allow clearing of extra blend tiles via ini and resaving of map.
		//Useful for flushing out initial maps made with buggy 3-way blending.
		if (!TheGlobalData->m_use3WayTerrainBlends)
			m_blendedTiles[i].inverted &= ~FLIPPED_MASK;	//filter out extra flips from 3-way

		if (info->version >= K_BLEND_TILE_VERSION_3) {
			m_blendedTiles[i].longDiagonal = file.readByte();
		} else {
			m_blendedTiles[i].longDiagonal = false;
		}
		if (info->version >= K_BLEND_TILE_VERSION_4) {
			m_blendedTiles[i].customBlendEdgeClass = file.readInt();
		} else {
			m_blendedTiles[i].customBlendEdgeClass = -1;
		}

		flag = file.readInt();
		DEBUG_ASSERTCRASH(flag==FLAG_VAL, ("Invalid format."));
		if (flag != FLAG_VAL) {
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
	}
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		for (i=1; i<m_numCliffInfo; i++) {
			m_cliffInfo[i].tileIndex = file.readInt();
			m_cliffInfo[i].u0 = file.readReal();
			m_cliffInfo[i].v0 = file.readReal();
			m_cliffInfo[i].u1 = file.readReal();
			m_cliffInfo[i].v1 = file.readReal();
			m_cliffInfo[i].u2 = file.readReal();
			m_cliffInfo[i].v2 = file.readReal();
			m_cliffInfo[i].u3 = file.readReal();
			m_cliffInfo[i].v3 = file.readReal();
			m_cliffInfo[i].flip = file.readByte();
			m_cliffInfo[i].mutant = file.readByte();
		}
	}
	// Resize me.
	if (info->version == K_BLEND_TILE_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_tileNdxes[i*newWidth+j] = m_tileNdxes[2*i*m_width+2*j];
				m_blendTileNdxes[i*newWidth+j] = 0;
				m_extraBlendTileNdxes[i*newWidth+j] = 0;
				m_cliffInfoNdxes[i*newWidth+j] = 0;
			}
		}
		m_numBlendedTiles = 1;
		m_numCliffInfo = 1;
		m_width= newWidth;
		m_height = newHeight;
		m_dataSize = m_width*m_height;
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}


/**
* WorldHeightMap::ParseObjectData - read a object info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)file.m_userData;
	return pThis->ParseObjectData(file, info, userData, info->version >= K_OBJECTS_VERSION_2);
}

/**
* WorldHeightMap::ParseObjectData - read a object info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput
*
*/
Bool WorldHeightMap::ParseObjectData(DataChunkInput &file, DataChunkInfo *info, void *userData, Bool readDict)
{
	MapObject *pPrevious = (MapObject *)file.m_currentObject;

	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();

	Real minZ = -100*MAP_XY_FACTOR;
	Real maxZ = (255*10)*MAP_HEIGHT_SCALE;

	if (info->version <= K_OBJECTS_VERSION_2) {
		loc.z = 0;
	}

	Real angle = file.readReal();
	Int flags = file.readInt();
	AsciiString name = file.readAsciiString();
	Dict d;
	if (readDict)
	{
		d = file.readDict();
	}

	if (loc.z<minZ || loc.z>maxZ) {
		DEBUG_LOG(("Removing object at z height %f", loc.z));
		return true;
	}

	MapObject *pThisOne;

	// create the map object
	pThisOne = newInstance( MapObject )( loc, name, angle, flags, &d,
														TheThingFactory->findTemplate( name, FALSE ) );

//DEBUG_LOG(("obj %s owner %s",name.str(),d.getAsciiString(TheKey_originalOwner).str()));

	if (pThisOne->getProperties()->getType(TheKey_waypointID) == Dict::DICT_INT)
		pThisOne->setIsWaypoint();

	if (pThisOne->getProperties()->getType(TheKey_lightHeightAboveTerrain) == Dict::DICT_REAL)
		pThisOne->setIsLight();

	if (pThisOne->getProperties()->getType(TheKey_scorchType) == Dict::DICT_INT)
		pThisOne->setIsScorch();


	if (pPrevious) {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr != nullptr && pPrevious->getNext() == nullptr, ("Bad linkage."));
		pPrevious->setNextMap(pThisOne);
	}	else {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr == nullptr, ("Bad linkage."));
		MapObject::TheMapObjectListPtr = pThisOne;
	}
	file.m_currentObject = pThisOne;
	return true;
}



// Targa format:  Header

typedef struct {
	UnsignedByte	idLength;
	UnsignedByte	colorMapType; // 0 = rgb, 1 = indexed.
	UnsignedByte	imageType; //0x1 = indexed, 0x2 = rgb, 0x8 = rle.
	UnsignedByte	colorMapInfo[5]; // we ignore, only do rgb.
	Short			xOrigin;
	Short			yOrigin;
	Short			imageWidth;
	Short			imageHeight;
	UnsignedByte	pixelDepth;
	UnsignedByte	flags; //  &0x0F = alpha channel bits, &0x10 is right to left flag,
						   // 0x20 is top to bottom flag.  (0x0? is left to right, bottom to top)
						   // 0x3? is top to bottom, right to left.
} TTargaHeader;

// followed by idLength bytes of ascii data

// followed by pixel data

// followed by optional data.

// Add below TTargaHeader.

static Bool isPowerOfTwo(Int value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

static Bool readTgaHeader(InputStream* pStr, TTargaHeader* hdr)
{
	if (pStr == nullptr || hdr == nullptr) {
		return false;
	}

	return pStr->read(hdr, sizeof(*hdr)) == sizeof(*hdr);
}

static Bool getSourceTilePixelExtentForLogicalWidth(InputStream* pStr, Int logicalWidth, Int* tilePixelExtent)
{
	TTargaHeader hdr;
	if (!readTgaHeader(pStr, &hdr)) {
		return false;
	}

	if (hdr.colorMapType != 0) {
		return false;
	}
	if (hdr.imageType != 0x2 && hdr.imageType != 0xA) {
		return false;
	}
	if (hdr.pixelDepth < 24 || hdr.pixelDepth > 32) {
		return false;
	}
	if (logicalWidth <= 0) {
		return false;
	}
	if ((hdr.imageWidth % logicalWidth) != 0 || (hdr.imageHeight % logicalWidth) != 0) {
		return false;
	}

	const Int pixelExtentX = hdr.imageWidth / logicalWidth;
	const Int pixelExtentY = hdr.imageHeight / logicalWidth;
	if (pixelExtentX != pixelExtentY) {
		return false;
	}
	if (!isPowerOfTwo(pixelExtentX)) {
		return false;
	}

	*tilePixelExtent = pixelExtentX;
	return true;
}

// @feature Ronin 10/05/2026 Normal-map N1.5b: decode <basename>_NRM.dds (BC1/BC3) into a
// sub-rect of an already-locked atlas page. DDSFileClass handles the DXT decode and
// BGRA conversion (we ask for WW3D_FORMAT_A8R8G8B8 which in the locked rect is byte
// order B,G,R,A -- identical to the TGA helper convention).
//
// Pre-screens the FourCC because DDSFileClass in this codebase ASSERTS on non-DXT
// pixel formats (uncompressed BGRA8 DDS would trip the assert). Uncompressed normal
// maps must ship as TGA; this is documented in the design doc and in normal art is a
// non-issue (production normal maps ship DXT5).
//
// Strict size check: image must be square and exactly `expectedExtent` on a side
// (matches the diffuse rectangle, same contract as the TGA helper). Fail-soft on any
// error; does not touch the destination rect on failure so the flat-default fill or
// TGA fallback can take over.
static Bool loadNormalDdsIntoLockedRect(
	const char* path,
	Int destX, Int destY, Int expectedExtent,
	UnsignedByte* lockBits, Int lockPitch,
	Int pageW, Int pageH)
{
	if (path == nullptr || lockBits == nullptr || expectedExtent <= 0) {
		return false;
	}
	if (destX < 0 || destY < 0 ||
		destX + expectedExtent > pageW ||
		destY + expectedExtent > pageH) {
		return false;
	}

	// Pre-screen header so DDSFileClass's WWASSERT on non-DXT formats never fires.
	// We do a tiny independent open via TheFileSystem (consistent with the TGA helper
	// right below). This also gives us a clean miss for files that aren't there.
	{
		File* probe = TheFileSystem->openFile(path, File::READ | File::BINARY);
		if (probe == nullptr) {
			return false;
		}
		// DDS header: 4-byte magic 'DDS ' + 124-byte LegacyDDSURFACEDESC2.
		// FourCC lives at offset 84 from the start of the SurfaceDesc, i.e. offset 88
		// from the start of the file. Read enough to reach it.
		UnsignedByte hdr[92];
		const Int got = probe->read(hdr, sizeof(hdr));
		probe->close();
		if (got < (Int)sizeof(hdr)) {
			return false;
		}
		if (hdr[0] != 'D' || hdr[1] != 'D' || hdr[2] != 'S' || hdr[3] != ' ') {
			return false;
		}
		const UnsignedByte* fourCC = &hdr[88];
		const Bool isDxt1 = (fourCC[0] == 'D' && fourCC[1] == 'X' && fourCC[2] == 'T' && fourCC[3] == '1');
		const Bool isDxt3 = (fourCC[0] == 'D' && fourCC[1] == 'X' && fourCC[2] == 'T' && fourCC[3] == '3');
		const Bool isDxt5 = (fourCC[0] == 'D' && fourCC[1] == 'X' && fourCC[2] == 'T' && fourCC[3] == '5');
		if (!(isDxt1 || isDxt3 || isDxt5)) {
			return false;
		}
	}

	// reduction_factor=0: never sub-sample; we need level 0 at native resolution so it
	// lines up with the diffuse rectangle byte-for-byte.
	DDSFileClass dds(path, 0);
	if (!dds.Load() || !dds.Is_Available()) {
		return false;
	}

	if ((Int)dds.Get_Width(0) != expectedExtent ||
		(Int)dds.Get_Height(0) != expectedExtent) {
		return false;
	}

	// Sub-rect blit. DDSFileClass::Copy_Level_To_Surface with dest_format =
	// WW3D_FORMAT_A8R8G8B8 decompresses the DXT block to A8R8G8B8 (locked-rect byte
	// order = B,G,R,A) and writes into dest_surface + y*dest_pitch + x*4. By passing
	// destPtr = lockBits + destY*lockPitch + destX*4 and dest_pitch = lockPitch, the
	// output lands at the (destX, destY, expectedExtent, expectedExtent) sub-rect of
	// the locked atlas page -- no temp buffer, no row-by-row copy.
	UnsignedByte* destPtr = lockBits + destY * lockPitch + destX * 4;
	dds.Copy_Level_To_Surface(
		/*level*/        0,
		/*dest_format*/  WW3D_FORMAT_A8R8G8B8,
		/*dest_width*/   (unsigned)expectedExtent,
		/*dest_height*/  (unsigned)expectedExtent,
		/*dest_surface*/ destPtr,
		/*dest_pitch*/   (unsigned)lockPitch);

	return true;
}



// @feature Ronin 10/05/2026 Normal-map N1.5: decode <basename>_NRM.tga directly into a
// sub-rect of an already-locked atlas page. Supports uncompressed (0x02) and RLE (0x0A)
// 24/32-bit BGR(A). Honors the TGA top-down flag (bit 0x20). Strict size check: image
// must be square and exactly `expectedExtent` on a side (matches the diffuse rectangle).
// On any failure (open, header, format, size, bounds) returns false WITHOUT touching
// the destination rect, so the flat-normal default written by the caller stays in place.
// Locked-rect convention matches the rest of N1: byte order is BGRA, A reserved for
// height/AO. The TGA pixel byte order at pixelDepth 24/32 is also BGR(A) so we copy
// channels straight through with no swizzle.
static Bool loadNormalTgaIntoLockedRect(
	const char* path,
	Int destX, Int destY, Int expectedExtent,
	UnsignedByte* lockBits, Int lockPitch,
	Int pageW, Int pageH)
{
	if (path == nullptr || lockBits == nullptr || expectedExtent <= 0) {
		return false;
	}
	if (destX < 0 || destY < 0 ||
		destX + expectedExtent > pageW ||
		destY + expectedExtent > pageH) {
		return false;
	}

	File* f = TheFileSystem->openFile(path, File::READ | File::BINARY);
	if (f == nullptr) {
		return false;
	}
	GDIFileStream stream(f);
	InputStream* pStr = &stream;

	TTargaHeader hdr;
	if (!readTgaHeader(pStr, &hdr)) { f->close(); return false; }
	if (hdr.colorMapType != 0) { f->close(); return false; }
	if (hdr.imageType != 0x2 && hdr.imageType != 0xA) { f->close(); return false; }
	if (hdr.pixelDepth < 24 || hdr.pixelDepth > 32) { f->close(); return false; }
	if (hdr.imageWidth != expectedExtent || hdr.imageHeight != expectedExtent) {
		f->close();
		return false;
	}

	// Skip optional image ID block.
	if (hdr.idLength > 0) {
		UnsignedByte skip[256];
		pStr->read(skip, hdr.idLength);
	}

	const Bool compressed = (hdr.imageType & 0x08) != 0;
	const Int bpp = (hdr.pixelDepth + 7) / 8;
	const Bool topDown = (hdr.flags & 0x20) != 0;

	UnsignedByte buf[4] = { 0,0,0,0 };
	Int repeatCount = 0;
	Bool running = false;

	for (Int row = 0; row < expectedExtent; ++row) {
		const Int dstY = topDown ? (destY + row) : (destY + (expectedExtent - 1 - row));
		UnsignedByte* dstRow = lockBits + dstY * lockPitch + destX * 4;

		for (Int col = 0; col < expectedExtent; ++col) {
			if (compressed && repeatCount == 0) {
				UnsignedByte flag;
				pStr->read(&flag, 1);
				repeatCount = (flag & 0x7f) + 1;
				running = (flag & 0x80) != 0;
				if (running) {
					pStr->read(buf, bpp);
				}
			}
			if (compressed) {
				--repeatCount;
			}
			if (!running) {
				pStr->read(buf, bpp);
			}

			// Source bytes at pixelDepth 24/32 are BGR(A) on disk -- straight copy.
			dstRow[col * 4 + 0] = buf[0]; // B
			dstRow[col * 4 + 1] = buf[1]; // G
			dstRow[col * 4 + 2] = buf[2]; // R
			dstRow[col * 4 + 3] = (bpp == 4) ? buf[3] : 255;
		}
	}

	f->close();
	return true;
}

/// Count how many tiles come in from a targa file.
Int WorldHeightMap::countTiles(InputStream *pStr, Bool *halfTile)
{
	TTargaHeader hdr;
	if (halfTile) {
		*halfTile = false;
	}
	Int len = pStr->read(&hdr, sizeof(hdr));
	if (len != sizeof(hdr)) return(0);
	Int tileWidth = hdr.imageWidth / TILE_PIXEL_EXTENT;
	Int tileHeight = hdr.imageHeight / TILE_PIXEL_EXTENT;

	if (hdr.colorMapType != 0) {
		return(0); // we don't do indexed at this time. jba.
	}
	if (hdr.imageType != 0x2 && hdr.imageType != 0xA) {
		return(0); // we don't do indexed at this time. jba.
	}

	if (hdr.pixelDepth < 24) return(false);
	if (hdr.pixelDepth > 32) return(false);

	const Int supportedWidth = getSupportedTextureSheetWidth(tileWidth, tileHeight);
	if (supportedWidth >= 1) {
		return supportedWidth * supportedWidth;
	}

	if (halfTile && hdr.imageHeight == TILE_PIXEL_EXTENT / 2 && hdr.imageWidth == TILE_PIXEL_EXTENT / 2) {
		*halfTile = true;
		return 1;
	}
	return(0);
}
/*Break down a .tga file into a collection of tiles.  numRows * numRows total tiles.*/
Bool WorldHeightMap::readTiles(InputStream* pStr, TileData** tiles, Int numRows, Int tilePixelExtent)
{
	TTargaHeader hdr;
	pStr->read(&hdr, sizeof(hdr));

	Int tileWidth = hdr.imageWidth / tilePixelExtent;
	Int tileHeight = hdr.imageHeight / tilePixelExtent;

	if (hdr.imageHeight == tilePixelExtent / 2) {
		tileHeight = 1;
	}
	if (hdr.imageWidth == tilePixelExtent / 2) {
		tileWidth = 1;
	}

	if (tileWidth < numRows || tileHeight < numRows) {
		return false;
	}

	Bool compressed = false;
	if (hdr.imageType & 0x08) {
		compressed = true;
	}

	int row = 0;
	int column = 0;
	int bytesPerPixel = (hdr.pixelDepth + 7) / 8;
	if (bytesPerPixel < 3) return false;
	if (bytesPerPixel > 4) return false;

	int i;
	for (i = 0; i < numRows * numRows; i++) {
		if (tiles[i] == nullptr) {
			tiles[i] = MSGNEW("WorldHeightMap_readTiles") TileData(tilePixelExtent);
		}
	}

	UnsignedByte buf[4];
	int repeatCount = 0;
	Bool running = false;
	for (row = 0; row < numRows * tilePixelExtent; row++) {
		for (column = 0; column < hdr.imageWidth; column++) {
			UnsignedByte r, g, b, a;
			if (row < hdr.imageHeight) {
				if (compressed && repeatCount == 0) {
					UnsignedByte flag;
					pStr->read(&flag, 1);
					repeatCount = flag & 0x7f;
					repeatCount++;
					if (flag & 0x80) {
						running = true;
						pStr->read(buf, bytesPerPixel);
					}
					else {
						running = false;
					}
				}
				if (compressed) repeatCount--;
				if (!running) {
					pStr->read(buf, bytesPerPixel);
				}
				r = buf[2];
				g = buf[1];
				b = buf[0];
				if (bytesPerPixel == 4) {
					a = buf[3];
				}
				else {
					a = 255;
				}
			}
			else {
				r = g = b = a = 0;
			}

			if (column >= (numRows * tilePixelExtent)) continue;

			const Int tileNdx = (column / tilePixelExtent) + numRows * (row / tilePixelExtent);
			const Int pixelNdx = (column % tilePixelExtent) + tilePixelExtent * (row % tilePixelExtent);

			UnsignedByte* pixel = tiles[tileNdx]->getDataPtr();
			pixel += pixelNdx * TILE_BYTES_PER_PIXEL;
			*pixel++ = b;
			*pixel++ = g;
			*pixel++ = r;
			*pixel = a;
		}
		DEBUG_ASSERTCRASH(repeatCount == 0, ("Invalid tga."));
	}

	for (i = 0; i < numRows * numRows; i++) {
		tiles[i]->updateMips();
	}
	return true;
}

namespace
{
	struct TextureAtlasPackPageState
	{
		Int cursorX;
		Int cursorY;
		Int rowHeight;

		TextureAtlasPackPageState() :
			cursorX(0),
			cursorY(0),
			rowHeight(0)
		{
		}
	};
}


/** updateTileTexturePositions - assigns each tile a location in the texture.
*/
Int WorldHeightMap::updateTileTexturePositions(Int* edgeHeight)
{
	Int i;

	m_numTerrainTexturePages = 1;
	m_numEdgeTexturePages = 1;
	for (i = 0; i < MAX_TEXTURE_ATLAS_PAGES; ++i) {
		// @bugfix Ronin 08/04/2026 Reset only atlas layout metadata here; runtime texture objects are owned elsewhere.
		m_terrainTexturePages[i].textureHeight = 0;
		m_edgeTexturePages[i].textureHeight = 0;
	}

	for (i = 0; i < m_numBitmapTiles; i++) {
		if (m_sourceTiles[i]) {
			m_sourceTiles[i]->m_texturePage = 0;
			m_sourceTiles[i]->m_tileLocationInTexture.x = 0;
			m_sourceTiles[i]->m_tileLocationInTexture.y = 0;
		}
	}

	for (i = 0; i < m_numEdgeTiles; i++) {
		if (m_edgeTiles[i]) {
			m_edgeTiles[i]->m_texturePage = 0;
			m_edgeTiles[i]->m_tileLocationInTexture.x = 0;
			m_edgeTiles[i]->m_tileLocationInTexture.y = 0;
		}
	}

	auto packTextureClassesIntoPages =
		[&](TXTextureClass* classes,
			Int numClasses,
			TileData** tiles,
			TTextureAtlasPageInfo* outPages,
			Int* outNumPages,
			Int maxPages) -> Int
		{
			if (outNumPages) {
				*outNumPages = 1;
			}
			if (maxPages < 1) {
				return 0;
			}

			std::vector<Bool> packed(numClasses, false);
			std::vector<TextureAtlasPackPageState> pages(maxPages);
			Int usedPages = 1;

			for (Int packedCount = 0; packedCount < numClasses; ++packedCount) {
				Int bestClass = -1;
				Int bestPixelSize = -1;

				for (Int texClass = 0; texClass < numClasses; ++texClass) {
					if (packed[texClass]) {
						continue;
					}

					const Int classPixelSize = classes[texClass].width * classes[texClass].tilePixelExtent + TILE_OFFSET;
					if (classPixelSize > bestPixelSize) {
						bestPixelSize = classPixelSize;
						bestClass = texClass;
					}
				}

				if (bestClass < 0) {
					break;
				}

				packed[bestClass] = true;
				TXTextureClass& textureClass = classes[bestClass];
				textureClass.texturePage = 0;
				textureClass.positionInTexture.x = 0;
				textureClass.positionInTexture.y = 0;

				const Int tilePixelExtent = textureClass.tilePixelExtent;
				const Int classPixelSize = textureClass.width * tilePixelExtent + TILE_OFFSET;

				if (classPixelSize > TEXTURE_WIDTH) {
					continue;
				}

				Bool placed = false;
				for (Int page = 0; page < maxPages; ++page) {
					TextureAtlasPackPageState& state = pages[page];

					if (state.cursorX != 0 && state.cursorX + classPixelSize > TEXTURE_WIDTH) {
						state.cursorX = 0;
						state.cursorY += state.rowHeight;
						state.rowHeight = 0;
					}

					if (state.cursorY + classPixelSize > TEXTURE_WIDTH) {
						continue;
					}

					if (usedPages < page + 1) {
						usedPages = page + 1;
					}

					const Int xOrigin = state.cursorX + TILE_OFFSET / 2;
					const Int yOrigin = state.cursorY + TILE_OFFSET / 2;
					textureClass.texturePage = page;
					textureClass.positionInTexture.x = xOrigin;
					textureClass.positionInTexture.y = yOrigin;

					const Int classHeight = yOrigin + textureClass.width * tilePixelExtent + TILE_OFFSET / 2;
					if (outPages[page].textureHeight < classHeight) {
						outPages[page].textureHeight = classHeight;
					}

					for (Int classX = 0; classX < textureClass.width; ++classX) {
						for (Int classY = 0; classY < textureClass.width; ++classY) {
							const Int baseNdx = textureClass.firstTile + classX + classY * textureClass.width;
							if (tiles[baseNdx] == nullptr) {
								continue;
							}

							const Int x = xOrigin + classX * tilePixelExtent;
							const Int y = yOrigin + (textureClass.width - classY - 1) * tilePixelExtent;
							tiles[baseNdx]->m_texturePage = page;
							tiles[baseNdx]->m_tileLocationInTexture.x = x;
							tiles[baseNdx]->m_tileLocationInTexture.y = y;
						}
					}

					state.cursorX += classPixelSize;
					if (state.rowHeight < classPixelSize) {
						state.rowHeight = classPixelSize;
					}

					placed = true;
					break;
				}

				if (!placed) {
					textureClass.texturePage = 0;
					textureClass.positionInTexture.x = 0;
					textureClass.positionInTexture.y = 0;
				}
			}

			if (outNumPages) {
				*outNumPages = usedPages;
			}

			Int maxPackedHeight = 0;
			for (Int page = 0; page < usedPages; ++page) {
				if (outPages[page].textureHeight > maxPackedHeight) {
					maxPackedHeight = outPages[page].textureHeight;
				}
			}

			return maxPackedHeight;
		};

	// Compatibility mode: layout is page-aware, but runtime still consumes one page.
	const Int kCompatibilityMaxAtlasPages = MAX_TEXTURE_ATLAS_PAGES;

	const Int maxHeight = packTextureClassesIntoPages(
		m_textureClasses,
		m_numTextureClasses,
		m_sourceTiles,
		m_terrainTexturePages,
		&m_numTerrainTexturePages,
		kCompatibilityMaxAtlasPages);

	const Int maxEdgeHeight = packTextureClassesIntoPages(
		m_edgeTextureClasses,
		m_numEdgeTextureClasses,
		m_edgeTiles,
		m_edgeTexturePages,
		&m_numEdgeTexturePages,
		kCompatibilityMaxAtlasPages);

	if (edgeHeight) {
		*edgeHeight = maxEdgeHeight;
	}

	return maxHeight;
}

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
*/
void WorldHeightMap::getUVForNdx(Int tileNdx, float* minU, float* minV, float* maxU, float* maxV)
{
	Short baseNdx = tileNdx >> 2;
	if (m_sourceTiles[baseNdx] == nullptr) {
		*minU = *minV = *maxU = *maxV = 0.0f;
		return;
	}

	TileData* pTile = m_sourceTiles[baseNdx];
	const Int tilePixelExtent = pTile->getPixelExtent();
	ICoord2D pos = pTile->m_tileLocationInTexture;
	const Int texturePage = pTile->m_texturePage;
	Int textureHeight = getTerrainTextureHeightForPage(texturePage);
	if (textureHeight <= 0) {
		textureHeight = m_terrainTexHeight;
	}

	*minU = (Real)pos.x;
	*minV = (Real)pos.y;
	*maxU = *minU + tilePixelExtent;
	*maxV = *minV + tilePixelExtent;

	*minU /= TEXTURE_WIDTH;
	*minV /= textureHeight;
	*maxU /= TEXTURE_WIDTH;
	*maxV /= textureHeight;

	// @feature Ronin 05/04/2026 Split the actual source tile extent into quadrants instead of always assuming 64x64.
	const Real midX = (*minU + *maxU) / 2.0f;
	const Real midY = (*minV + *maxV) / 2.0f;
	if (tileNdx & 2) {
		*maxV = midY;
	}
	else {
		*minV = midY;
	}
	if (tileNdx & 1) {
		*minU = midX;
	}
	else {
		*maxU = midX;
	}
}

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
*/
void WorldHeightMap::getUVForBlend(Int edgeClass, Region2D* range)
{
	ICoord2D pos = m_edgeTextureClasses[edgeClass].positionInTexture;
	Int width = m_edgeTextureClasses[edgeClass].width;
	Int tilePixelExtent = m_edgeTextureClasses[edgeClass].tilePixelExtent;
	const Int texturePage = m_edgeTextureClasses[edgeClass].texturePage;
	Int textureHeight = getEdgeTextureHeightForPage(texturePage);
	if (textureHeight <= 0) {
		textureHeight = m_alphaEdgeHeight;
	}

	range->lo.x = (Real)pos.x / TEXTURE_WIDTH;
	range->lo.y = (Real)pos.y / textureHeight;
	range->hi.x = ((Real)pos.x + width * tilePixelExtent) / TEXTURE_WIDTH;
	range->hi.y = ((Real)pos.y + width * tilePixelExtent) / textureHeight;

}

/// Get whether something is cliff indexed with the offset that HeightMapRenderObjClass uses built in.
Bool WorldHeightMap::isCliffMappedTexture(Int x, Int y) {
	Int ndx = x+m_drawOriginX+m_width*(y+m_drawOriginY);
	if (ndx>=0 && ndx<m_dataSize) {
		return m_cliffInfoNdxes[ndx] != 0;
	}
	return false;
};

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
		xIndex and yIndex are the integer coordinates into the height map.
		U and V are the texture coordinates for the 4 corners of a height map cell.
*/
Bool WorldHeightMap::getUVData(Int xIndex, Int yIndex, float U[4], float V[4])
{
#define dont_SHOW_THE_TEXTURE_FOR_DEBUG 1
#if SHOW_THE_TEXTURE_FOR_DEBUG
		// This is debug code that just shows the generated texture laid on the terrain.
		// For debugging ;) jba.
		xIndex += m_drawOriginX;
		yIndex += m_drawOriginY;
		float nU= xIndex;
		float xU = xIndex+1;
		float nV = 48-yIndex-1;
		float xV = 48-yIndex;
		float k = 48;
		nU /= k;
		xU /= k;
		k = k*m_terrainTexHeight/TEXTURE_WIDTH;
		nV /= k;
		xV /= k;
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		return(true);
#else
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;
	Int ndx = (yIndex*m_width)+xIndex;
	if ((ndx<m_dataSize) && m_tileNdxes) {
		Short tileNdx = m_tileNdxes[ndx];
		return getUVForTileIndex(ndx, tileNdx, U, V);
	}
	return false;
#endif
}

/** getUVForTileIndex - Gets the texture coordinates to use.  See getTerrainTexture.
		ndx is the index into the linear height array.
		tileNdx is the index into the texture tiles array.
		U and V are the texture coordinates for the 4 corners of a height map cell.
*/

Bool WorldHeightMap::getUVForTileIndex(Int ndx, Short tileNdx, float U[4], float V[4])
{
	Real nU, nV, xU, xV;
	nU = nV = xU = xV = 0.0f;

	if ((ndx < m_dataSize) && m_tileNdxes) {
		getUVForNdx(tileNdx, &nU, &nV, &xU, &xV);
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		if (TheGlobalData && !TheGlobalData->m_adjustCliffTextures) {
			return false;
		}
		if (nU == 0.0f) {
			return false; // missing texture.
		}
		if (m_cliffInfoNdxes[ndx]) {
			TCliffInfo info = m_cliffInfo[m_cliffInfoNdxes[ndx]];
			Bool tilesMatch = false;
			Int ndx1 = tileNdx >> 2;
			Int ndx2 = info.tileIndex >> 2;
			Int i;
			for (i = 0; i < this->m_numTextureClasses; i++) {
				if (ndx1 >= m_textureClasses[i].firstTile && ndx1 < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles) {
					tilesMatch = ndx2 >= m_textureClasses[i].firstTile && ndx2 < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles;
					break;
				}
			}
			if (tilesMatch) {
				const Int classPixelExtent = m_textureClasses[i].width * m_textureClasses[i].tilePixelExtent;
				const Int texturePage = m_textureClasses[i].texturePage;
				Int textureHeight = getTerrainTextureHeightForPage(texturePage);
				if (textureHeight <= 0) {
					textureHeight = m_terrainTexHeight;
				}

				Real minU = m_textureClasses[i].positionInTexture.x;
				Real maxV = m_textureClasses[i].positionInTexture.y + classPixelExtent;
				minU /= TEXTURE_WIDTH;
				maxV /= textureHeight;
				Real vFactor = (Real)TEXTURE_WIDTH / textureHeight;
				U[0] = info.u0 + minU;
				U[1] = info.u1 + minU;
				U[2] = info.u2 + minU;
				U[3] = info.u3 + minU;
				V[0] = info.v0 * vFactor + maxV;
				V[1] = info.v1 * vFactor + maxV;
				V[2] = info.v2 * vFactor + maxV;
				V[3] = info.v3 * vFactor + maxV;
				return info.flip;
			}
		}

		// TheSuperHackers @info xezon 11/12/2025 The old uv adjustment for cliffs produces bad uv tiles on steep terrain
		// and is also not helping performance. But we cannot just remove it, because it is required to render smooth
		// steep diagonal slopes.
#define DO_OLD_UV
#ifdef DO_OLD_UV
// old uv adjustment for cliffs
		static Real STRETCH_LIMIT = 1.5f;	 // If it is stretching less than this, don't adjust.
		static Real TILE_LIMIT = 4.0;			// Our tiles are currently 4 cells wide & tall, so dont'
		// adjust to more than 4.0.

		static Real TALL_STRETCH_LIMIT = 2.0f;
		static Real DIAMOND_STRETCH_LIMIT = 2.4f;
		static Real HEIGHT_SCALE = MAP_HEIGHT_SCALE / MAP_XY_FACTOR;

		Real nU, nV, xU, xV;
		nU = nV = xU = xV = 0.0f;

		getUVForNdx(tileNdx, &nU, &nV, &xU, &xV);
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		if (TheGlobalData && !TheGlobalData->m_adjustCliffTextures) {
			return false;
		}
		if (nU == 0.0f) {
			return false; // missing texture.
		}
		// check for excessive heights.
		if (ndx < this->m_dataSize - m_width - 1) {
			Int h0 = m_data[ndx];
			Int h1 = m_data[ndx + 1];
			Int h2 = m_data[ndx + m_width + 1];
			Int h3 = m_data[ndx + m_width];
			Int minH, maxH;
			minH = maxH = h0;
			if (minH > h1) minH = h1;
			if (maxH < h1) maxH = h1;
			if (minH > h2) minH = h2;
			if (maxH < h2) maxH = h2;
			if (minH > h3) minH = h3;
			if (maxH < h3) maxH = h3;
			Int deltaH = maxH - minH;
			Int below = 0;
			Int above = 0;
			Int belowLimit = minH + (2 * deltaH + 1) / 3;
			Int aboveLimit = minH + (deltaH + 1) / 3;
			if (h0 < belowLimit) below++;
			if (h1 < belowLimit) below++;
			if (h2 < belowLimit) below++;
			if (h3 < belowLimit) below++;
			if (h0 > aboveLimit) above++;
			if (h1 > aboveLimit) above++;
			if (h2 > aboveLimit) above++;
			if (h3 > aboveLimit) above++;
			if (deltaH * HEIGHT_SCALE < STRETCH_LIMIT) {
				return false;
			}

			Short baseNdx = tileNdx >> 2;
			Short texClass;
			for (texClass = 0; texClass < m_numTextureClasses; texClass++) {
				if (m_textureClasses[texClass].firstTile < 0) {
					continue;
				}
				if (baseNdx >= m_textureClasses[texClass].firstTile &&
					baseNdx < m_textureClasses[texClass].firstTile + m_textureClasses[texClass].numTiles) {
					break;
				}
			}
			if (texClass >= m_numTextureClasses) return false;

			const Int classPixelExtent = m_textureClasses[texClass].width * m_textureClasses[texClass].tilePixelExtent;
			Real nUb, nVb, xUb, xVb;
			nUb = m_textureClasses[texClass].positionInTexture.x;
			nVb = m_textureClasses[texClass].positionInTexture.y;
			xUb = nUb + classPixelExtent;
			xVb = nVb + classPixelExtent;
			const Int texturePage = m_textureClasses[texClass].texturePage;
			Int textureHeight = getTerrainTextureHeightForPage(texturePage);
			if (textureHeight <= 0) {
				textureHeight = m_terrainTexHeight;
			}

			nUb /= TEXTURE_WIDTH;
			nVb /= textureHeight;
			xUb /= TEXTURE_WIDTH;
			xVb /= textureHeight;

			Real divisor = TILE_LIMIT / (deltaH * HEIGHT_SCALE);
			if (divisor > TILE_LIMIT) divisor = TILE_LIMIT;
			if (divisor < 1.0f) divisor = 1.0f;
			Real deltaV = (xVb - nVb);

			if (above != 1 && below != 1 && (above != 2 || below != 2)) {
				if (deltaH * HEIGHT_SCALE < DIAMOND_STRETCH_LIMIT) {
					return false;
				}
			}

			if (below == 1 || above > below) {
				if (h0 == minH) {
					V[0] = nV + deltaV / divisor;
				}
				else if (h1 == minH) {
					V[1] = nV + deltaV / divisor;
				}
				else if (h2 == minH) {
					V[2] = xV - deltaV / divisor;
				}
				else if (h3 == minH) {
					V[3] = xV - deltaV / divisor;
				}
			}
			else if (above == 1 || below > above) {
				if (h0 == maxH) {
					V[0] = nV + deltaV / divisor;
				}
				else if (h1 == maxH) {
					V[1] = nV + deltaV / divisor;
				}
				else if (h2 == maxH) {
					V[2] = xV - deltaV / divisor;
				}
				else if (h3 == maxH) {
					V[3] = xV - deltaV / divisor;
				}
			}
			else {
				if (deltaH * HEIGHT_SCALE < TALL_STRETCH_LIMIT) {
					return false;
				}

				Real dx = (h3 - h2) * HEIGHT_SCALE;
				dx = sqrt(1 + dx * dx);
				Real dy = (h3 - h0) * HEIGHT_SCALE;
				dy = sqrt(1 + dy * dy);
				if (dx < STRETCH_LIMIT) dx = 1.0f;
				if (dy < STRETCH_LIMIT) dy = 1.0f;
				if (dx > TILE_LIMIT) dx = TILE_LIMIT;
				if (dy > TILE_LIMIT) dy = TILE_LIMIT;
				dx *= xU - nU;
				dy *= xV - nV;
				U[0] = nU; U[1] = nU + dx; U[2] = nU + dx; U[3] = nU;
				V[0] = nV + dy; V[1] = nV + dy; V[2] = nV; V[3] = nV;

				dx = (h1 - h0) * HEIGHT_SCALE;
				dx = sqrt(1 + dx * dx);
				dy = (h2 - h1) * HEIGHT_SCALE;
				dy = sqrt(1 + dy * dy);
				if (dx < STRETCH_LIMIT) dx = 1.0f;
				if (dy < STRETCH_LIMIT) dy = 1.0f;
				if (dx > TILE_LIMIT) dx = TILE_LIMIT;
				if (dy > TILE_LIMIT) dy = TILE_LIMIT;
				dx *= xU - nU;
				dy *= xV - nV;
				U[1] = U[0] + dx;
				V[1] = V[3] + dy;
			}

			Real adjU = 0;
			Real adjV = 0;
			Int i;
			for (i = 0; i < 4; i++) {
				if (nVb - V[i] > adjV) adjV = nVb - V[i];
			}
			for (i = 0; i < 4; i++) {
				V[i] += adjV;
			}
			adjV = 0;
			for (i = 0; i < 4; i++) {
				if (U[i] - xUb > adjU) adjU = U[i] - xUb;
				if (V[i] - xVb > adjV) adjV = V[i] - xVb;
			}
			for (i = 0; i < 4; i++) {
				U[i] -= adjU;
				V[i] -= adjV;
			}
		}
		return true;
#endif
	}
	return false;
}

///@todo: Are the different "if" cases mutually exclusive?  If so, should add else statements.
Bool WorldHeightMap::getExtraAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *needFlip, Bool *cliff)
{
	Int ndx = (yIndex*m_width)+xIndex;
	*needFlip = FALSE;
	*cliff = FALSE;

	if ( (ndx>=0) && (ndx<m_dataSize) && m_tileNdxes) {
		Short blendNdx = m_extraBlendTileNdxes[ndx];
		if (blendNdx == 0) {
			return FALSE;
		} else {
			*cliff = getUVForTileIndex(ndx, m_blendedTiles[blendNdx].blendNdx, U, V);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			if (m_blendedTiles[blendNdx].horiz) {
				// Horizontals don't need flipping unless forced because of 3way blend
				// and a diagonal in base blend layer.
				*needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[3] = 255;
				} else {
					alpha[1] = alpha[2] = 255;
				}
			}
			if (m_blendedTiles[blendNdx].vert) {
				// Verticals don't need flipping unless forced because of 3way blend
				// and a diagonal in base blend layer.
				*needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[1] = 255;
				} else {
					alpha[2] = alpha[3]  = 255;
				}
			}
			if (m_blendedTiles[blendNdx].rightDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[1] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				} else {
					// Uninverted right diagonals need flipping.
					*needFlip = TRUE;
					alpha[2] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].leftDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					// Inverted left diagonals need flipping.
					*needFlip = TRUE;
					alpha[0] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				} else {
					alpha[3] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].customBlendEdgeClass>=0) {
				alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
				// No alpha blend, so never need to flip.
				*needFlip = FALSE;
			}
		}
	}

	return TRUE;
}

/** getAlphaUVData - Gets the texture coordinates to use with the alpha texture.
		xIndex and yIndex are the integer coordinates into the height map.
		U and V are the texture coordinates for the 4 corners of a height map cell.
		flip is set if we need to flip the diagonal across the cell to make the
		alpha coordinates blend properly.  Filling a square with 2 triangles is not symmetrical :)
*/
void WorldHeightMap::getAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *flip)
{
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;
	Int ndx = (yIndex*m_width)+xIndex;
	Bool stretchedForCliff = false;
	Bool needFlip = false;

	if ((ndx<m_dataSize) && m_tileNdxes) {
		Short blendNdx = m_blendTileNdxes[ndx];
		if (blendNdx == 0) {
			stretchedForCliff = getUVForTileIndex(ndx, m_tileNdxes[ndx], U, V);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			// No alpha blend, so never need to flip.
			needFlip = false;
		} else {
			stretchedForCliff = getUVForTileIndex(ndx, m_blendedTiles[blendNdx].blendNdx, U, V);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			if (m_blendedTiles[blendNdx].horiz) {
				// Horizontals don't need flipping unless forced because of 3way blend.
				needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[3] = 255;
				} else {
					alpha[1] = alpha[2] = 255;
				}
			}
			if (m_blendedTiles[blendNdx].vert) {
				// Verticals don't need flipping unless forced because of 3way blend.
				needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[1] = 255;
				} else {
					alpha[2] = alpha[3]  = 255;
				}
			}
			if (m_blendedTiles[blendNdx].rightDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[1] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				} else {
					// Uninverted right diagonals need flipping.
					needFlip = true;
					alpha[2] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].leftDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					// Inverted left diagonals need flipping.
					needFlip = true;
					alpha[0] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				} else {
					alpha[3] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].customBlendEdgeClass>=0) {
				alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
				// No alpha blend, so never need to flip.
				needFlip = false;
			}
		}
	}
	if (stretchedForCliff) {
		// If we had to stretch for clif, check heights.
		Int p0=getHeight(xIndex, yIndex);
		Int p1=getHeight(xIndex+1, yIndex);
		Int p2=getHeight(xIndex+1, yIndex+1);
		Int p3=getHeight(xIndex, yIndex+1);
		Int dz1 = abs(p0-p2);
		Int dz2 = abs(p1-p3);
		needFlip = dz1>dz2;
	}
#ifdef FLIP_TRIANGLES
	*flip = needFlip;
#endif
}


// =============================================================================
// @feature Ronin 26/04/2026 Splat S20-A1: per-material weight texture bake.
// =============================================================================
//
// Encoding: one byte-channel per active material, packed as a planar [active][y][x]
// 3D buffer (m_perMaterialWeightBytes, stride m_perMaterialWeightPitch). Sum of all
// channels per texel == 255 (representing 1.0) by construction.
//
// Algorithm:
//   1. Scan m_tileNdxes / m_blendTileNdxes / m_extraBlendTileNdxes to discover the
//      active material set (deduplicated local class indices).
//   2. Allocate width*height bytes per active material.
//   3. For each cell, compute the 4 corner weights for each material that the cell
//      contributes to (base/blend1/blend2), bilerp across the cell's texelsPerCell
//      block.
//   4. Per-channel separable box blur with radius `blurRadiusCells * texelsPerCell`
//      (independent per channel).
//   5. Renormalize so sum-of-channels-per-texel == 255 (correct for box-blur drift).
//
// What this does:
//   - Builds per-material CPU weight planes for the live terrain splat path.
//   - Feeds the GPU atlas upload performed by ensurePerMaterialWeightAtlasTextures().
// =============================================================================

namespace
{
	// File-local helper: separable box blur on a single byte channel buffer of size
	// validW * validH at row-pitch `pitch`. Operates on a tightly-packed single-channel
	// buffer (one byte per texel, no stride). Used by buildPerMaterialWeightTextures
	// Step 4 to smooth each active material weight plane independently.
	static void splat_blurSingleChannel(
		UnsignedByte* buf, Int pitch, Int validW, Int validH, Int radius)
	{
		if (buf == nullptr || validW <= 0 || validH <= 0 || radius <= 0) return;
		const Int kernelSize = 2 * radius + 1;
		UnsignedByte* scratch =
			MSGNEW("WorldHeightMap_S20A1_blurScratch") UnsignedByte[validW * validH];
		if (scratch == nullptr) return;

		// Horizontal pass: buf -> scratch (tightly packed, validW per row in scratch).
		for (Int y = 0; y < validH; ++y) {
			UnsignedByte* srcRow = buf + y * pitch;
			UnsignedByte* dstRow = scratch + y * validW;
			Int sum = 0;
			for (Int k = -radius; k <= radius; ++k) {
				Int sx = k; if (sx < 0) sx = 0; else if (sx >= validW) sx = validW - 1;
				sum += srcRow[sx];
			}
			dstRow[0] = (UnsignedByte)(sum / kernelSize);
			for (Int x = 1; x < validW; ++x) {
				Int xOut = x + radius;     if (xOut >= validW) xOut = validW - 1;
				Int xIn = x - radius - 1; if (xIn < 0)       xIn = 0;
				sum += srcRow[xOut] - srcRow[xIn];
				dstRow[x] = (UnsignedByte)(sum / kernelSize);
			}
		}

		// Vertical pass: scratch -> buf.
		for (Int x = 0; x < validW; ++x) {
			Int sum = 0;
			for (Int k = -radius; k <= radius; ++k) {
				Int sy = k; if (sy < 0) sy = 0; else if (sy >= validH) sy = validH - 1;
				sum += scratch[sy * validW + x];
			}
			buf[0 * pitch + x] = (UnsignedByte)(sum / kernelSize);
			for (Int y = 1; y < validH; ++y) {
				Int yOut = y + radius;     if (yOut >= validH) yOut = validH - 1;
				Int yIn = y - radius - 1; if (yIn < 0)       yIn = 0;
				sum += scratch[yOut * validW + x] - scratch[yIn * validW + x];
				buf[y * pitch + x] = (UnsignedByte)(sum / kernelSize);
			}
		}

		delete[] scratch;
	}
}

Bool WorldHeightMap::buildPerMaterialWeightTextures(
	Int texelsPerCell, Int blurRadiusCells)
{
	if (m_tileNdxes == nullptr || m_blendTileNdxes == nullptr) return FALSE;
	if (texelsPerCell <= 0) texelsPerCell = 4;
	if (blurRadiusCells < 0) blurRadiusCells = 1;

	const Int cellsX = m_width - 1;
	const Int cellsY = m_height - 1;
	if (cellsX <= 0 || cellsY <= 0) return FALSE;

	const Int W = cellsX * texelsPerCell;
	const Int H = cellsY * texelsPerCell;

	// ---- Step 1: discover active material set. ---------------------------------
	auto getLocalClass = [this](Short tileNdx) -> Int {
		const Short baseNdx = tileNdx >> 2;
		for (Int i = 0; i < m_numTextureClasses; ++i) {
			if (m_textureClasses[i].firstTile < 0) continue;
			if (baseNdx >= m_textureClasses[i].firstTile &&
				baseNdx < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles)
				return i;
		}
		return 0;
		};

	// Ronin @bugfix 14/09/2026 DX9: §19e.3. The WEIGHTABLE set — base classes plus the blends step 3 writes, never custom edges — is
	// what gets weight channels; m_numActiveMaterials is set by the colouring at the end of this block.
	{
		Bool weightable[NUM_TEXTURE_CLASSES] = { FALSE };
		for (Int i = 0; i < m_dataSize; ++i) {
			const Int bc = getLocalClass(m_tileNdxes[i]);
			if (bc >= 0 && bc < NUM_TEXTURE_CLASSES) weightable[bc] = TRUE;
			const Short bp = m_blendTileNdxes[i];
			if (bp > 0 && bp < m_numBlendedTiles && m_blendedTiles[bp].customBlendEdgeClass < 0) {
				const Int c1 = getLocalClass(m_blendedTiles[bp].blendNdx);
				if (c1 >= 0 && c1 < NUM_TEXTURE_CLASSES) weightable[c1] = TRUE;
			}
			if (m_extraBlendTileNdxes != nullptr) {
				const Short be = m_extraBlendTileNdxes[i];
				if (be > 0 && be < m_numBlendedTiles && m_blendedTiles[be].customBlendEdgeClass < 0) {
					const Int c2 = getLocalClass(m_blendedTiles[be].blendNdx);
					if (c2 >= 0 && c2 < NUM_TEXTURE_CLASSES) weightable[c2] = TRUE;
				}
			}
		}
		m_splatWeightableClasses = 0;
		for (Int i = 0; i < NUM_TEXTURE_CLASSES; ++i) {
			if (weightable[i]) ++m_splatWeightableClasses;
		}
		m_splatDroppedCells = 0;

		// Ronin @bugfix 14/09/2026 DX9: §19e.3. Materials meeting inside one terrain draw tile (32 cells + a 4-cell apron for blur,
		// bilinear and the coarsest weight mip), coloured into weight channels. APRON assumes blur 1 cell and MIP_LEVELS_3 weights.
		const Int APRON = 4;
		const Int WIN   = VERTEX_BUFFER_TILE_LENGTH + 2 * APRON;
		Int compact[NUM_TEXTURE_CLASSES];
		Int numCompact = 0;
		for (Int i = 0; i < NUM_TEXTURE_CLASSES; ++i)
			compact[i] = (weightable[i] && numCompact < 64) ? numCompact++ : -1;
		auto bitOf = [&](Int classIdx) -> UnsignedInt64 {
			if (classIdx < 0 || classIdx >= NUM_TEXTURE_CLASSES || compact[classIdx] < 0) return 0;
			return (UnsignedInt64)1 << compact[classIdx];
			};

		const Int cw = m_width;
		const Int ch = m_height;
		std::vector<UnsignedInt64> cellMask(cw * ch, 0);
		for (Int i = 0; i < m_dataSize && i < cw * ch; ++i) {
			UnsignedInt64 m = bitOf(getLocalClass(m_tileNdxes[i]));
			const Short bp = m_blendTileNdxes[i];
			if (bp > 0 && bp < m_numBlendedTiles && m_blendedTiles[bp].customBlendEdgeClass < 0)
				m |= bitOf(getLocalClass(m_blendedTiles[bp].blendNdx));
			if (m_extraBlendTileNdxes != nullptr) {
				const Short be = m_extraBlendTileNdxes[i];
				if (be > 0 && be < m_numBlendedTiles && m_blendedTiles[be].customBlendEdgeClass < 0)
					m |= bitOf(getLocalClass(m_blendedTiles[be].blendNdx));
			}
			cellMask[i] = m;
		}

		// Every tile origin, not just today's grid: the draw window's origin moves with the camera.
		std::vector<UnsignedInt64> rowOr(cw * ch, 0);
		for (Int y = 0; y < ch; ++y) {
			for (Int x0 = 0; x0 < cw; ++x0) {
				const Int xa = (x0 - APRON > 0) ? (x0 - APRON) : 0;
				const Int xb = (x0 - APRON + WIN < cw) ? (x0 - APRON + WIN) : cw;
				UnsignedInt64 m = 0;
				for (Int x = xa; x < xb; ++x) m |= cellMask[y * cw + x];
				rowOr[y * cw + x0] = m;
			}
		}
		UnsignedInt64 conflict[64] = { 0 };
		m_splatMaxPerTile = 0;
		for (Int y0 = 0; y0 < ch; ++y0) {
			const Int ya = (y0 - APRON > 0) ? (y0 - APRON) : 0;
			const Int yb = (y0 - APRON + WIN < ch) ? (y0 - APRON + WIN) : ch;
			for (Int x0 = 0; x0 < cw; ++x0) {
				UnsignedInt64 m = 0;
				for (Int y = ya; y < yb; ++y) m |= rowOr[y * cw + x0];
				Int n = 0;
				for (Int b = 0; b < numCompact; ++b) {
					if (m & ((UnsignedInt64)1 << b)) {
						conflict[b] |= m;
						++n;
					}
				}
				if (n > m_splatMaxPerTile) m_splatMaxPerTile = n;
			}
		}

		Int colour[64];
		m_splatChannelsNeeded = 0;
		for (Int b = 0; b < numCompact; ++b) {
			UnsignedInt64 taken = 0;
			for (Int o = 0; o < b; ++o) {
				if (conflict[b] & ((UnsignedInt64)1 << o))
					taken |= (UnsignedInt64)1 << colour[o];
			}
			Int c = 0;
			while (c < 63 && (taken & ((UnsignedInt64)1 << c))) ++c;
			colour[b] = c;
			if (c + 1 > m_splatChannelsNeeded) m_splatChannelsNeeded = c + 1;
		}

		// Ronin @bugfix 14/09/2026 DX9: §19e.3. CHANNEL REUSE is live — the colouring above IS the channel assignment: materials that
		// ever meet inside one draw tile (+apron) never share a channel, so a per-tile table says which one a channel means.
		m_splatNumCompact = numCompact;
		for (Int i = 0; i < NUM_TEXTURE_CLASSES; ++i) {
			if (compact[i] >= 0) m_splatCompactClass[compact[i]] = i;
		}
		m_numActiveMaterials = 0;
		for (Int b = 0; b < numCompact; ++b) {
			m_splatCompactChannel[b] = (colour[b] < SPLAT_MAX_ACTIVE_MATERIALS) ? colour[b] : -1;
			if (m_splatCompactChannel[b] >= 0) {
				if (m_splatCompactChannel[b] + 1 > m_numActiveMaterials) m_numActiveMaterials = m_splatCompactChannel[b] + 1;
				m_activeMaterialIndices[m_splatCompactChannel[b]] = m_splatCompactClass[b];	// a representative, for the debug dumps
			}
		}

		// Per cell: every material within APRON cells. A draw tile ORs these over its cells to build its table.
		std::vector<UnsignedInt64> rowDil(cw * ch, 0);
		for (Int y = 0; y < ch; ++y) {
			for (Int x = 0; x < cw; ++x) {
				const Int xa = (x - APRON > 0) ? (x - APRON) : 0;
				const Int xb = (x + APRON + 1 < cw) ? (x + APRON + 1) : cw;
				UnsignedInt64 m = 0;
				for (Int xx = xa; xx < xb; ++xx) m |= cellMask[y * cw + xx];
				rowDil[y * cw + x] = m;
			}
		}
		m_splatCellMask.assign(cw * ch, 0);
		for (Int y = 0; y < ch; ++y) {
			const Int ya = (y - APRON > 0) ? (y - APRON) : 0;
			const Int yb = (y + APRON + 1 < ch) ? (y + APRON + 1) : ch;
			for (Int x = 0; x < cw; ++x) {
				UnsignedInt64 m = 0;
				for (Int yy = ya; yy < yb; ++yy) m |= rowDil[yy * cw + x];
				m_splatCellMask[y * cw + x] = m;
			}
		}
		// Ronin @bugfix 14/09/2026 DX9: §19e.3. GLOBAL serial — a new map object can land at the old one's address and restart a
		// per-object count, and the draw's tile cache would then keep the previous map's tables.
		static Int s_splatBakeSerialCounter = 0;
		m_splatBakeSerial = ++s_splatBakeSerialCounter;
	}

	if (m_numActiveMaterials == 0) return FALSE;

	// @bugfix Ronin 26/04/2026 Splat S20-A1: classToActive must be indexed by class index
	// (0..NUM_TEXTURE_CLASSES-1), NOT by slot (0..SPLAT_MAX_ACTIVE_MATERIALS-1). The
	// previous SPLAT_MAX_ACTIVE_MATERIALS-sized array crashed on maps with > 16 texture
	// classes: getLocalClass() returns the raw class index from m_textureClasses (can be
	// 17, 18, ... on the desert map), the cell loop did classToActive[17] which read
	// past the stack array -> returned garbage int -> plane(garbage)[p] wrote out of
	// bounds. Sizing the lookup by NUM_TEXTURE_CLASSES makes every legal class index
	// safe to look up; classes outside the active set still map to -1 and skip.
	Int classToActive[NUM_TEXTURE_CLASSES];
	for (Int i = 0; i < NUM_TEXTURE_CLASSES; ++i) classToActive[i] = -1;
	// Ronin @bugfix 14/09/2026 DX9: §19e.3. Class -> its CHANNEL. Several classes share a channel, never inside one draw tile.
	for (Int b = 0; b < m_splatNumCompact; ++b) {
		const Int classIdx = m_splatCompactClass[b];
		if (classIdx >= 0 && classIdx < NUM_TEXTURE_CLASSES) {
			classToActive[classIdx] = m_splatCompactChannel[b];
		}
	}

	// Defensive lookup helper -- belts & braces against future getLocalClass() changes
	// returning a class index that is somehow outside [0, NUM_TEXTURE_CLASSES).
	auto activeSlotOf = [&](Int classIdx) -> Int {
		if (classIdx < 0 || classIdx >= NUM_TEXTURE_CLASSES) return -1;
		return classToActive[classIdx];
		};

	// ---- Step 2: allocate the planar buffer. ------------------------------------
	const Int pitch = W; // tight pack; A2 will adapt to D3D row pitch when uploading.
	const Int planeBytes = pitch * H;
	const Int totalBytes = planeBytes * m_numActiveMaterials;

	delete[] m_perMaterialWeightBytes;
	m_perMaterialWeightBytes = MSGNEW("WorldHeightMap_S20A1_weights") UnsignedByte[totalBytes];
	if (m_perMaterialWeightBytes == nullptr) return FALSE;
	memset(m_perMaterialWeightBytes, 0, totalBytes);

	m_perMaterialWeightWidth = W;
	m_perMaterialWeightHeight = H;
	m_perMaterialWeightPitch = pitch;

	auto plane = [&](Int activeIdx) -> UnsignedByte* {
		return m_perMaterialWeightBytes + activeIdx * planeBytes;
		};

	// ---- Step 3: per-cell weight rasterization. --------------------------------
	// For each cell:
	//   - baseClass gets weight (1 - smoothA1) * (1 - smoothA2)  bilerped across cell
	//   - blend1Class gets weight smoothA1 * (1 - smoothA2)
	//   - blend2Class gets weight smoothA2
	// where smoothA1/smoothA2 are the corner-alpha bilerps that S1/Pass3 already
	// compute. Sum at each texel == 1.0 by construction.
	//
	// The 4-corner alpha decode below mirrors the blend-info decode used in
	// getAlphaUVData / getExtraAlphaUVData, so the weight bake matches the existing
	// alpha pipeline byte-for-byte at texelsPerCell before blur. (After blur, the
	// per-material channels diverge from the legacy single-control path -- that's the point.)
	for (Int cy = 0; cy < cellsY; ++cy) {
		for (Int cx = 0; cx < cellsX; ++cx) {
			const Int ndx = cy * m_width + cx;
			if (ndx < 0 || ndx >= m_dataSize) continue;

			const Int baseClass = getLocalClass(m_tileNdxes[ndx]);
			Int blend1Class = baseClass;
			Int blend2Class = baseClass;

			UnsignedByte cornerA1[4] = { 0,0,0,0 };
			UnsignedByte cornerA2[4] = { 0,0,0,0 };

			const Short bp = m_blendTileNdxes[ndx];
			if (bp > 0 && bp < m_numBlendedTiles &&
				m_blendedTiles[bp].customBlendEdgeClass < 0) {
				const TBlendTileInfo& info = m_blendedTiles[bp];
				blend1Class = getLocalClass(info.blendNdx);
				// Corner-alpha decode for blend1 -- mirrors the blend-info decode in getAlphaUVData.
				if (info.horiz) {
					if (info.inverted & INVERTED_MASK) { cornerA1[0] = cornerA1[3] = 255; }
					else { cornerA1[1] = cornerA1[2] = 255; }
				}
				if (info.vert) {
					if (info.inverted & INVERTED_MASK) { cornerA1[0] = cornerA1[1] = 255; }
					else { cornerA1[2] = cornerA1[3] = 255; }
				}
				if (info.rightDiagonal) {
					if (info.inverted & INVERTED_MASK) {
						cornerA1[1] = 255;
						if (info.longDiagonal) { cornerA1[0] = 255; cornerA1[2] = 255; }
					}
					else {
						cornerA1[2] = 255;
						if (info.longDiagonal) { cornerA1[1] = 255; cornerA1[3] = 255; }
					}
				}
				if (info.leftDiagonal) {
					if (info.inverted & INVERTED_MASK) {
						cornerA1[0] = 255;
						if (info.longDiagonal) { cornerA1[1] = 255; cornerA1[3] = 255; }
					}
					else {
						cornerA1[3] = 255;
						if (info.longDiagonal) { cornerA1[0] = 255; cornerA1[2] = 255; }
					}
				}
			}

			if (m_extraBlendTileNdxes != nullptr) {
				const Short be = m_extraBlendTileNdxes[ndx];
				if (be > 0 && be < m_numBlendedTiles &&
					m_blendedTiles[be].customBlendEdgeClass < 0) {
					const TBlendTileInfo& info = m_blendedTiles[be];
					blend2Class = getLocalClass(info.blendNdx);
					// @bugfix Ronin 29/04/2026 Splat S20-A1: 3-way corner alpha decode.
					// Decode mirrors the cornerA1 block above (and the blend-info decode in
					// getExtraAlphaUVData) verbatim, writing cornerA2.
					if (info.horiz) {
						if (info.inverted & INVERTED_MASK) { cornerA2[0] = cornerA2[3] = 255; }
						else { cornerA2[1] = cornerA2[2] = 255; }
					}
					if (info.vert) {
						if (info.inverted & INVERTED_MASK) { cornerA2[0] = cornerA2[1] = 255; }
						else { cornerA2[2] = cornerA2[3] = 255; }
					}
					if (info.rightDiagonal) {
						if (info.inverted & INVERTED_MASK) {
							cornerA2[1] = 255;
							if (info.longDiagonal) { cornerA2[0] = 255; cornerA2[2] = 255; }
						}
						else {
							cornerA2[2] = 255;
							if (info.longDiagonal) { cornerA2[1] = 255; cornerA2[3] = 255; }
						}
					}
					if (info.leftDiagonal) {
						if (info.inverted & INVERTED_MASK) {
							cornerA2[0] = 255;
							if (info.longDiagonal) { cornerA2[1] = 255; cornerA2[3] = 255; }
						}
						else {
							cornerA2[3] = 255;
							if (info.longDiagonal) { cornerA2[0] = 255; cornerA2[2] = 255; }
						}
					}
				}
			}

			const Int slotBase = activeSlotOf(baseClass);
			const Int slotBlend1 = activeSlotOf(blend1Class);
			const Int slotBlend2 = activeSlotOf(blend2Class);
			// Ronin @diagnostic 14/09/2026 DX9: §19e.3. A cell whose base class has no slot gets zero weight in every channel.
			if (slotBase < 0) { ++m_splatDroppedCells; continue; }

			const float A1[4] = { cornerA1[0] / 255.0f, cornerA1[1] / 255.0f, cornerA1[2] / 255.0f, cornerA1[3] / 255.0f };
			const float A2[4] = { cornerA2[0] / 255.0f, cornerA2[1] / 255.0f, cornerA2[2] / 255.0f, cornerA2[3] / 255.0f };

			const Int rowBase = cy * texelsPerCell;
			const Int colBase = cx * texelsPerCell;
			for (Int ty = 0; ty < texelsPerCell; ++ty) {
				const float v = (ty + 0.5f) / (float)texelsPerCell;
				for (Int tx = 0; tx < texelsPerCell; ++tx) {
					const float u = (tx + 0.5f) / (float)texelsPerCell;
					const float a1 =
						(A1[0] + (A1[1] - A1[0]) * u) +
						((A1[3] + (A1[2] - A1[3]) * u) - (A1[0] + (A1[1] - A1[0]) * u)) * v;
					const float a2 =
						(A2[0] + (A2[1] - A2[0]) * u) +
						((A2[3] + (A2[2] - A2[3]) * u) - (A2[0] + (A2[1] - A2[0]) * u)) * v;

					const float wBase = (1.0f - a1) * (1.0f - a2);
					const float wBlend1 = a1 * (1.0f - a2);
					const float wBlend2 = a2;

					const Int x = colBase + tx;
					const Int y = rowBase + ty;
					const Int p = y * pitch + x;

					plane(slotBase)[p] = (UnsignedByte)(wBase * 255.0f + 0.5f);
					if (slotBlend1 >= 0 && slotBlend1 != slotBase)
						plane(slotBlend1)[p] = (UnsignedByte)(wBlend1 * 255.0f + 0.5f);
					if (slotBlend2 >= 0 && slotBlend2 != slotBase && slotBlend2 != slotBlend1)
						plane(slotBlend2)[p] = (UnsignedByte)(wBlend2 * 255.0f + 0.5f);
				}
			}
		}
	}

	// ---- Step 4: per-channel separable box blur. -------------------------------
	const Int blurRadius = blurRadiusCells * texelsPerCell;
	if (blurRadius > 0) {
		for (Int s = 0; s < m_numActiveMaterials; ++s) {
			splat_blurSingleChannel(plane(s), pitch, W, H, blurRadius);
		}
	}

	// ---- Step 5: renormalize per-texel sum to 255. -----------------------------
	// Box blur preserves the sum if every channel is blurred with identical kernels
	// and the original sum was constant -- which is true here. But integer rounding
	// drift accumulates ~1-2 LSBs per texel, so renormalize defensively.
	for (Int y = 0; y < H; ++y) {
		for (Int x = 0; x < W; ++x) {
			Int sum = 0;
			for (Int s = 0; s < m_numActiveMaterials; ++s) {
				sum += plane(s)[y * pitch + x];
			}
			if (sum <= 0) continue;
			// Scale so sum -> 255. Most-saturated channel gets the rounding remainder.
			Int newSum = 0;
			Int maxSlot = 0; Int maxVal = 0;
			for (Int s = 0; s < m_numActiveMaterials; ++s) {
				const Int v = plane(s)[y * pitch + x];
				const Int scaled = (v * 255 + sum / 2) / sum;
				plane(s)[y * pitch + x] = (UnsignedByte)scaled;
				newSum += scaled;
				if (scaled > maxVal) { maxVal = scaled; maxSlot = s; }
			}
			const Int delta = 255 - newSum;
			plane(maxSlot)[y * pitch + x] =
				(UnsignedByte)((Int)plane(maxSlot)[y * pitch + x] + delta);
		}
	}

	// Ronin @bugfix 14/09/2026 DX9: §19e.3. Channel reuse: materials share channels, so report both — and any cell left without one.
	DEBUG_LOG(("[S20-A1] Built %d weight channels for %d materials (max %d per draw tile, %d cells dropped) at %dx%d (texelsPerCell=%d, blur=%d cells).\n",
		m_numActiveMaterials, m_splatWeightableClasses, m_splatMaxPerTile, m_splatDroppedCells, W, H, texelsPerCell, blurRadiusCells));
	return TRUE;
}

// @feature Ronin 27/04/2026 Splat S20-A2a: allocate / upload per-material weight atlas
// pages from the A1 planar buffer.
//
// Layout per page (BGRA8, D3DFMT_A8R8G8B8, little-endian byte order in the locked rect):
//   byte 0 (B) = weight of active slot (page*4 + 0)
//   byte 1 (G) = weight of active slot (page*4 + 1)   (or 0 if past numActive)
//   byte 2 (R) = weight of active slot (page*4 + 2)   (or 0)
//   byte 3 (A) = weight of active slot (page*4 + 3)   (or 0)
//
// POW2-padded; padding rows/cols are zeroed (any sampling outside the active region
// returns zero weight for every slot, which the PS will treat as "no contribution").
//
// Recreates pages if width/height/numPages changed. Idempotent: safe to call after every
// successful buildPerMaterialWeightTextures().
//
// A2-a does NOT bind these to any sampler. A2-c wires them into the new PS.
// See docs/Terrain_Splat_Map_Design.md S20 A2-a.
Bool WorldHeightMap::ensurePerMaterialWeightAtlasTextures()
{
	if (m_perMaterialWeightBytes == nullptr || m_numActiveMaterials <= 0) {
		return FALSE;
	}
	if (m_perMaterialWeightWidth <= 0 || m_perMaterialWeightHeight <= 0) {
		return FALSE;
	}

	const Int requiredPages =
		(m_numActiveMaterials + 3) / 4;
	if (requiredPages <= 0 || requiredPages > MAX_S20_WEIGHT_ATLAS_PAGES) {
		DEBUG_LOG(("[S20-A2a] requiredPages=%d out of range (numActive=%d, max=%d)",
			requiredPages, m_numActiveMaterials, (Int)MAX_S20_WEIGHT_ATLAS_PAGES));
		return FALSE;
	}

	Int pow2W = 1; while (pow2W < m_perMaterialWeightWidth)  pow2W *= 2;
	Int pow2H = 1; while (pow2H < m_perMaterialWeightHeight) pow2H *= 2;

	// Recreate pages if size or count changed (or first call).
	const Bool sizeChanged =
		(pow2W != m_perMaterialWeightAtlasWidth) ||
		(pow2H != m_perMaterialWeightAtlasHeight) ||
		(requiredPages != m_numPerMaterialWeightAtlasPages);

	if (sizeChanged) {
		for (Int p = 0; p < MAX_S20_WEIGHT_ATLAS_PAGES; ++p) {
			REF_PTR_RELEASE(m_perMaterialWeightAtlas[p]);
		}
		m_numPerMaterialWeightAtlasPages = requiredPages;
		m_perMaterialWeightAtlasWidth = pow2W;
		m_perMaterialWeightAtlasHeight = pow2H;

		for (Int p = 0; p < requiredPages; ++p) {
			m_perMaterialWeightAtlas[p] = MSGNEW("WorldHeightMap_S20A2a_weightAtlas")
				TextureClass(pow2W, pow2H, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_3);
			if (m_perMaterialWeightAtlas[p] == nullptr) {
				DEBUG_LOG(("[S20-A2a] failed to allocate weight atlas page %d (%dx%d)",
					p, pow2W, pow2H));
				return FALSE;
			}
		}
	}

	const Int srcW = m_perMaterialWeightWidth;
	const Int srcH = m_perMaterialWeightHeight;
	const Int srcPitch = m_perMaterialWeightPitch;
	const Int planeBytes = srcPitch * srcH;

	auto srcPlane = [this, planeBytes](Int activeIdx) -> const UnsignedByte* {
		return m_perMaterialWeightBytes + activeIdx * planeBytes;
		};

	for (Int page = 0; page < requiredPages; ++page) {
		TextureClass* tex = m_perMaterialWeightAtlas[page];
		if (tex == nullptr || tex->Peek_D3D_Texture() == nullptr) {
			continue;
		}

		IDirect3DSurface8* surf = nullptr;
		DX8_ErrorCode(tex->Peek_Lockable_Texture()->GetSurfaceLevel(0, &surf));	// Ronin @feature 08/11/2026 DX9Ex: the lockable copy
		if (surf == nullptr) {
			continue;
		}

		D3DSURFACE_DESC desc;
		DX8_ErrorCode(surf->GetDesc(&desc));
		if (desc.Format != D3DFMT_A8R8G8B8) {
			surf->Release();
			DEBUG_LOG(("[S20-A2a] page %d: unexpected format 0x%x, skipping", page, desc.Format));
			continue;
		}

		D3DLOCKED_RECT lr;
		DX8_ErrorCode(surf->LockRect(&lr, nullptr, 0));

		// Per-page channel sources (null = past numActive -> writes 0).
		const Int slotB = page * 4 + 0;
		const Int slotG = page * 4 + 1;
		const Int slotR = page * 4 + 2;
		const Int slotA = page * 4 + 3;
		const UnsignedByte* pB = (slotB < m_numActiveMaterials) ? srcPlane(slotB) : nullptr;
		const UnsignedByte* pG = (slotG < m_numActiveMaterials) ? srcPlane(slotG) : nullptr;
		const UnsignedByte* pR = (slotR < m_numActiveMaterials) ? srcPlane(slotR) : nullptr;
		const UnsignedByte* pA = (slotA < m_numActiveMaterials) ? srcPlane(slotA) : nullptr;

		for (Int y = 0; y < (Int)desc.Height; ++y) {
			UnsignedByte* dstRow = (UnsignedByte*)lr.pBits + y * lr.Pitch;
			const Bool inSrcY = (y < srcH);
			for (Int x = 0; x < (Int)desc.Width; ++x) {
				const Bool inSrc = inSrcY && (x < srcW);
				const Int srcOff = inSrc ? (y * srcPitch + x) : 0;
				dstRow[x * 4 + 0] = (inSrc && pB) ? pB[srcOff] : 0;
				dstRow[x * 4 + 1] = (inSrc && pG) ? pG[srcOff] : 0;
				dstRow[x * 4 + 2] = (inSrc && pR) ? pR[srcOff] : 0;
				dstRow[x * 4 + 3] = (inSrc && pA) ? pA[srcOff] : 0;
			}
		}

		surf->UnlockRect();
		surf->Release();

		// Box-filter mips. Weight channels are smooth scalars per-channel, so the
		// standard box filter produces correct lower mips (unlike the discrete-ID
		// channels in the legacy splat control texture).
		DX8_ErrorCode(D3DXFilterTexture(tex->Peek_Lockable_Texture(), nullptr, 0, D3DX_FILTER_BOX));	// Ronin @feature 08/11/2026 DX9Ex: the lockable copy
	}

	DEBUG_LOG(("[S20-A2a] allocated %d weight atlas page(s) at %dx%d (active=%d, src=%dx%d)",
		requiredPages, pow2W, pow2H, m_numActiveMaterials, srcW, srcH));

	return TRUE;
}

// =============================================================================
// @feature Ronin 10/05/2026 Normal-map N1: per-material normal atlas allocation.
// =============================================================================
//
// Mirrors the diffuse atlas layout exactly -- one A8R8G8B8 page per diffuse atlas
// page, same dimensions, same per-source-tile rectangles. Consequence: the existing
// getSplatAtlasRegionsForActiveSet[Page]() tables address this atlas without any
// modification when the PS binds it on s12..s15 (in N2).
//
// v1 scope (this commit): allocate pages, fill every texel with the flat-normal
// default (128,128,255,255 = tangent-space (0,0,1)). Asset discovery
// (<basename>_NRM.dds / .tga) and per-source-tile pixel copy is the next
// sub-commit; until that lands, every source tile contributes the flat default and
// no perturbation is visible at runtime even after N3 lighting ships.
//
// Idempotent. Releases stale pages on size/count change. Safe to call from
// ensureSplatTextures() on the same dirty flag the weight atlas uses.
//
// See docs/Terrain_Normal_Map_Design.md N1.
// =============================================================================
Bool WorldHeightMap::ensurePerMaterialNormalAtlasTextures()
{
	if (m_numTerrainTexturePages <= 0) {
		return FALSE;
	}

	Int allocatedPages = 0;
	Int loadedTiles = 0;
	Int flatDefaultTiles = 0;

	for (Int page = 0; page < m_numTerrainTexturePages && page < MAX_TEXTURE_ATLAS_PAGES; ++page) {
		// Match the corresponding diffuse atlas page dimensions byte-for-byte so the
		// shared region table addresses both atlases.
		TextureClass* diffuse = m_terrainTextures[page];
		if (diffuse == nullptr || diffuse->Peek_D3D_Texture() == nullptr) {
			continue;
		}

		IDirect3DSurface8* diffSurf = nullptr;
		DX8_ErrorCode(diffuse->Peek_D3D_Texture()->GetSurfaceLevel(0, &diffSurf));
		if (diffSurf == nullptr) {
			continue;
		}

		D3DSURFACE_DESC diffDesc;
		DX8_ErrorCode(diffSurf->GetDesc(&diffDesc));
		diffSurf->Release();

		const Int pageW = (Int)diffDesc.Width;
		const Int pageH = (Int)diffDesc.Height;

		REF_PTR_RELEASE(m_perMaterialNormalAtlas[page]);
		m_perMaterialNormalAtlas[page] = MSGNEW("WorldHeightMap_N1_normalAtlas")
			TextureClass(pageW, pageH, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_3);
		if (m_perMaterialNormalAtlas[page] == nullptr) {
			DEBUG_LOG(("[NRM] failed to allocate normal atlas page %d (%dx%d)",
				page, pageW, pageH));
			continue;
		}

		TextureClass* tex = m_perMaterialNormalAtlas[page];
		if (tex->Peek_D3D_Texture() == nullptr) {
			REF_PTR_RELEASE(m_perMaterialNormalAtlas[page]);
			continue;
		}

		IDirect3DSurface8* surf = nullptr;
		DX8_ErrorCode(tex->Peek_Lockable_Texture()->GetSurfaceLevel(0, &surf));	// Ronin @feature 08/11/2026 DX9Ex: the lockable copy
		if (surf == nullptr) {
			REF_PTR_RELEASE(m_perMaterialNormalAtlas[page]);
			continue;
		}

		D3DSURFACE_DESC desc;
		DX8_ErrorCode(surf->GetDesc(&desc));
		if (desc.Format != D3DFMT_A8R8G8B8) {
			surf->Release();
			REF_PTR_RELEASE(m_perMaterialNormalAtlas[page]);
			DEBUG_LOG(("[NRM] page %d: unexpected format 0x%x, skipping", page, desc.Format));
			continue;
		}

		D3DLOCKED_RECT lr;
		DX8_ErrorCode(surf->LockRect(&lr, nullptr, 0));

		// Fill the entire page with flat-normal default (128,128,255,255).
		// Layout matches the weight atlas convention (BGRA in the locked rect):
		//   byte 0 (B) = Z = 255   byte 1 (G) = Y = 128
		//   byte 2 (R) = X = 128   byte 3 (A) = 255 (reserved for height/AO)
		// Sampled in the PS this decodes to tangent-space (0,0,1) -- no perturbation.
		for (Int y = 0; y < (Int)desc.Height; ++y) {
			UnsignedByte* dstRow = (UnsignedByte*)lr.pBits + y * lr.Pitch;
			for (Int x = 0; x < (Int)desc.Width; ++x) {
				dstRow[x * 4 + 0] = 255;
				dstRow[x * 4 + 1] = 128;
				dstRow[x * 4 + 2] = 128;
				dstRow[x * 4 + 3] = 255;
			}
		}

		// @feature Ronin 10/05/2026 Normal-map N1.5: per-source-class asset load.
		// For each texture class living on this page, try <basename>_NRM.tga and copy
		// pixels into the class's atlas rectangle. Misses leave the flat-default fill
		// in place. DDS support (<basename>_NRM.dds, BC1/BC3 + uncompressed BGRA) is
		// the next sub-commit; intentionally not implemented here to keep the diff
		// surface-area small for verification.
		for (Int i = 0; i < m_numTextureClasses; ++i) {
			if (m_textureClasses[i].texturePage != page) {
				continue;
			}

			const TXTextureClass& tc = m_textureClasses[i];
			const Int tileExtent = (tc.tilePixelExtent > 0) ? tc.tilePixelExtent : TILE_PIXEL_EXTENT;
			const Int classExtent = tc.width * tileExtent;
			const Int destX = tc.positionInTexture.x;
			const Int destY = tc.positionInTexture.y;

			// Resolve the diffuse texture filename via TheTerrainTypes (mirrors readTexClass).
			TerrainType* terrain = TheTerrainTypes->findTerrain(tc.name);
			if (terrain == nullptr) {
				++flatDefaultTiles;
				continue;
			}

			// Strip the diffuse extension. Conservative: only strip the last '.' if it
			// occurs after the last path separator, so paths without an extension just
			// get the suffix appended.
			const char* diffuseName = terrain->getTexture().str();
			char baseName[_MAX_PATH];
			snprintf(baseName, ARRAY_SIZE(baseName), "%s", diffuseName);
			char* lastDot = strrchr(baseName, '.');
			char* lastSep = strrchr(baseName, '\\');
			char* lastFwd = strrchr(baseName, '/');
			if (lastFwd > lastSep) lastSep = lastFwd;
			if (lastDot != nullptr && (lastSep == nullptr || lastDot > lastSep)) {
				*lastDot = '\0';
			}

			// @feature Ronin 10/05/2026 Normal-map N1.5b: DDS preferred, TGA fallback,
			// flat default if neither is found. DDS is preferred because (a) BC1/BC3 are
			// 4-8x smaller on disk than 24/32-bit TGA and (b) authoring tools that bake
			// normal maps overwhelmingly emit DDS. The TGA branch is retained so legacy
			// art that ships TGA-only continues to work.
			char ddsPath[_MAX_PATH];
			snprintf(ddsPath, ARRAY_SIZE(ddsPath), "%s%s_NRM.dds", TERRAIN_TGA_DIR_PATH, baseName);

			Bool loaded = loadNormalDdsIntoLockedRect(
				ddsPath, destX, destY, classExtent,
				(UnsignedByte*)lr.pBits, lr.Pitch,
				(Int)desc.Width, (Int)desc.Height);

			const char* loadedFromPath = ddsPath;
			const char* loadedFromKind = "dds";

			if (!loaded) {
				char nrmPath[_MAX_PATH];
				snprintf(nrmPath, ARRAY_SIZE(nrmPath), "%s%s_NRM.tga", TERRAIN_TGA_DIR_PATH, baseName);

				loaded = loadNormalTgaIntoLockedRect(
					nrmPath, destX, destY, classExtent,
					(UnsignedByte*)lr.pBits, lr.Pitch,
					(Int)desc.Width, (Int)desc.Height);

				loadedFromPath = nrmPath;
				loadedFromKind = "tga";
			}

			if (loaded) {
				// @feature Ronin 12/05/2026 Normal-map N6 (P1): detect near-zero alpha
				// channel. Some authoring tools write A=0 when no height data is
				// intended, which would make POM apply maximum displacement for every
				// pixel of that material -- visually very wrong. If the A-channel mean
				// across the loaded tile region is < 8/255, the author almost certainly
				// left A at the tool's default of 0 rather than the engine's expected
				// default of 255 (= full surface = no displacement). Force A=255
				// throughout the tile rect, log once, and continue.
				{
					Int alphaSum = 0;
					Int pixelCount = 0;
					for (Int ay = destY; ay < destY + classExtent && ay < (Int)desc.Height; ++ay) {
						const UnsignedByte* row =
							(const UnsignedByte*)lr.pBits + ay * lr.Pitch;
						for (Int ax = destX; ax < destX + classExtent && ax < (Int)desc.Width; ++ax) {
							alphaSum += row[ax * 4 + 3];
							++pixelCount;
						}
					}
					const Int alphaMean = (pixelCount > 0) ? (alphaSum / pixelCount) : 255;
					if (alphaMean < 8) {
						DEBUG_LOG(("[NRM] class=%s: A-channel mean=%d (<8/255) -- alpha authored as 0 "
							"(tool default); forcing A=255 to suppress unwanted POM displacement\n",
							tc.name.str(), alphaMean));
						for (Int ay = destY; ay < destY + classExtent && ay < (Int)desc.Height; ++ay) {
							UnsignedByte* row =
								(UnsignedByte*)lr.pBits + ay * lr.Pitch;
							for (Int ax = destX; ax < destX + classExtent && ax < (Int)desc.Width; ++ax) {
								row[ax * 4 + 3] = 255;
							}
						}
					}
				}
				++loadedTiles;
				DEBUG_LOG(("[NRM] loaded class=%s kind=%s path=%s page=%d rect=%d,%d ext=%d",
					tc.name.str(), loadedFromKind, loadedFromPath, page, destX, destY, classExtent));
			}
			else {
				++flatDefaultTiles;
			}
		}

		surf->UnlockRect();
		surf->Release();

		// Box-filter mips. Tangent normals do not box-filter perfectly (high-frequency
		// detail collapses toward (0,0,1) at coarse mips); follow-up polish is a
		// normal-aware mip filter. Acceptable for v1 since every page is currently the
		// flat default anyway.
		DX8_ErrorCode(D3DXFilterTexture(tex->Peek_Lockable_Texture(), nullptr, 0, D3DX_FILTER_BOX));	// Ronin @feature 08/11/2026 DX9Ex: the lockable copy

		++allocatedPages;
	}

	m_numPerMaterialNormalAtlasPages = allocatedPages;

	DEBUG_LOG(("[NRM] %d normal atlas page(s) allocated, %d source tile(s) loaded with normal art, %d using flat default\n",
		allocatedPages, loadedTiles, flatDefaultTiles));

	return allocatedPages > 0;
}

// @feature Ronin 10/05/2026 Normal-map N1: page accessor for the live render path.
TextureClass* WorldHeightMap::getPerMaterialNormalAtlasPage(Int page) const
{
	if (page < 0 || page >= m_numPerMaterialNormalAtlasPages) {
		return nullptr;
	}
	return m_perMaterialNormalAtlas[page];
}

// @feature Ronin 27/04/2026 Splat S20-A2b: per-active-slot variant of getSplatAtlasRegions.
// Reuses the same UV math (1 cell == 1 quadrant of 1 tile == tilePixelExtent/2 atlas pixels;
// super-tile world period = width * 2 * MAP_XY_FACTOR) but emits entries indexed by
// active slot (m_activeMaterialIndices[s]) instead of by m_textureClasses index. This is
// the table the new per-material splat PS will iterate over -- regionA[s] / regionB[s]
// align byte-for-byte with the weight stored in atlas page (s/4) channel (s%4).
//
// Defensive zero-fill on every slot up to SPLAT_MAX_ACTIVE_MATERIALS so unused tail slots
// produce a known reproducible (0,0) atlas read in the PS rather than uninitialized garbage.
// See docs/Terrain_Splat_Map_Design.md S20 A2-b.
Int WorldHeightMap::getSplatAtlasRegionsForActiveSet(float* outRegionA, float* outRegionB)
{
	if (outRegionA == nullptr || outRegionB == nullptr) {
		return 0;
	}

	// Zero-fill all SPLAT_MAX_ACTIVE_MATERIALS slots so unused tail entries are well-defined.
	for (Int i = 0; i < SPLAT_MAX_ACTIVE_MATERIALS * 4; ++i) {
		outRegionA[i] = 0.0f;
		outRegionB[i] = 0.0f;
	}

	if (m_numActiveMaterials <= 0) {
		return 0;
	}

	const Int atlasW = TEXTURE_WIDTH;
	Int atlasH = getTerrainTextureHeightForPage(0);
	if (atlasH <= 0) {
		atlasH = m_terrainTexHeight;
	}
	if (atlasH <= 0) {
		return 0;
	}

	const float invAtlasW = 1.0f / (float)atlasW;
	const float invAtlasH = 1.0f / (float)atlasH;

	const Int n = (m_numActiveMaterials < SPLAT_MAX_ACTIVE_MATERIALS)
		? m_numActiveMaterials : SPLAT_MAX_ACTIVE_MATERIALS;

	for (Int s = 0; s < n; ++s) {
		const Int classIdx = m_activeMaterialIndices[s];
		if (classIdx < 0 || classIdx >= m_numTextureClasses) {
			continue;   // leave slot at zero-fill; PS will read (0,0)
		}

		const TXTextureClass& tc = m_textureClasses[classIdx];
		const Int width = (tc.width > 0) ? tc.width : 1;
		const Int pixExt = (tc.tilePixelExtent > 0) ? tc.tilePixelExtent : TILE_PIXEL_EXTENT;

		const float originU = (float)tc.positionInTexture.x * invAtlasW;
		const float originV = (float)tc.positionInTexture.y * invAtlasH;
		const float extentU = (float)(width * pixExt) * invAtlasW;
		const float extentV = (float)(width * pixExt) * invAtlasH;

		// Each tile of the super-tile spans 2 world cells (quadrant pick), so the full
		// super-tile (width tiles) spans 2*width cells = 2*width*MAP_XY_FACTOR world units.
		const float worldPeriod = (float)(width * 2) * MAP_XY_FACTOR;
		const float invPeriod = (worldPeriod > 0.0f) ? (1.0f / worldPeriod) : 0.0f;

		outRegionA[s * 4 + 0] = originU;
		outRegionA[s * 4 + 1] = originV;
		outRegionA[s * 4 + 2] = extentU;
		outRegionA[s * 4 + 3] = extentV;

		outRegionB[s * 4 + 0] = invPeriod;
		outRegionB[s * 4 + 1] = invPeriod;
		outRegionB[s * 4 + 2] = 0.0f;
		outRegionB[s * 4 + 3] = 0.0f;
	}
	return n;
}

// @feature Ronin 03/05/2026 Splat S20 multi-atlas: page-aware per-active-slot atlas
// region table for the live per-material terrain shader. Slots whose material lives on
// `terrainPage` get normal region data plus enableMask=1; all other active slots stay
// zero-filled with enableMask=0 so HeightMap.cpp can render one terrain atlas page at a
// time and accumulate the result across multi-page maps.
Int WorldHeightMap::getSplatAtlasRegionsForActiveSetPage(
	Int terrainPage, float* outRegionA, float* outRegionB, float* outSlotEnableMask)
{
	if (outRegionA == nullptr || outRegionB == nullptr || outSlotEnableMask == nullptr) {
		return 0;
	}

	for (Int i = 0; i < SPLAT_MAX_ACTIVE_MATERIALS * 4; ++i) {
		outRegionA[i] = 0.0f;
		outRegionB[i] = 0.0f;
	}
	for (Int i = 0; i < SPLAT_MAX_ACTIVE_MATERIALS; ++i) {
		outSlotEnableMask[i] = 0.0f;
	}

	if (terrainPage < 0 || terrainPage >= m_numTerrainTexturePages) {
		return 0;
	}
	if (m_numActiveMaterials <= 0) {
		return 0;
	}

	const Int atlasW = TEXTURE_WIDTH;
	Int atlasH = getTerrainTextureHeightForPage(terrainPage);
	if (atlasH <= 0) {
		return 0;
	}

	const float invAtlasW = 1.0f / (float)atlasW;
	const float invAtlasH = 1.0f / (float)atlasH;

	const Int n = (m_numActiveMaterials < SPLAT_MAX_ACTIVE_MATERIALS)
		? m_numActiveMaterials : SPLAT_MAX_ACTIVE_MATERIALS;

	for (Int s = 0; s < n; ++s) {
		const Int classIdx = m_activeMaterialIndices[s];
		if (classIdx < 0 || classIdx >= m_numTextureClasses) {
			continue;
		}

		const TXTextureClass& tc = m_textureClasses[classIdx];
		if (tc.texturePage != terrainPage) {
			continue;
		}

		const Int width = (tc.width > 0) ? tc.width : 1;
		const Int pixExt = (tc.tilePixelExtent > 0) ? tc.tilePixelExtent : TILE_PIXEL_EXTENT;

		const float originU = (float)tc.positionInTexture.x * invAtlasW;
		const float originV = (float)tc.positionInTexture.y * invAtlasH;
		const float extentU = (float)(width * pixExt) * invAtlasW;
		const float extentV = (float)(width * pixExt) * invAtlasH;

		const float worldPeriod = (float)(width * 2) * MAP_XY_FACTOR;
		const float invPeriod = (worldPeriod > 0.0f) ? (1.0f / worldPeriod) : 0.0f;

		outRegionA[s * 4 + 0] = originU;
		outRegionA[s * 4 + 1] = originV;
		outRegionA[s * 4 + 2] = extentU;
		outRegionA[s * 4 + 3] = extentV;

		outRegionB[s * 4 + 0] = invPeriod;
		outRegionB[s * 4 + 1] = invPeriod;
		outRegionB[s * 4 + 2] = 0.0f;
		outRegionB[s * 4 + 3] = 0.0f;

		outSlotEnableMask[s] = 1.0f;
	}

	return n;
}

// Ronin @bugfix 14/09/2026 DX9: §19e.3. Region table for ONE draw tile under channel reuse: each material around the tile fills
// the slot of its channel. terrainPage < 0 = single-page atlas; otherwise only materials on that page are enabled.
Int WorldHeightMap::buildSplatRegionTable(UnsignedInt64 presentMask, Int terrainPage,
	float* outRegionA, float* outRegionB, float* outSlotEnableMask) const
{
	for (Int i = 0; i < SPLAT_MAX_ACTIVE_MATERIALS * 4; ++i) {
		outRegionA[i] = 0.0f;
		outRegionB[i] = 0.0f;
	}
	for (Int i = 0; i < SPLAT_MAX_ACTIVE_MATERIALS; ++i) {
		outSlotEnableMask[i] = 0.0f;
	}

	Int atlasH = getTerrainTextureHeightForPage((terrainPage >= 0) ? terrainPage : 0);
	if (atlasH <= 0 && terrainPage < 0) atlasH = m_terrainTexHeight;
	if (atlasH <= 0) return 0;
	const float invAtlasW = 1.0f / (float)TEXTURE_WIDTH;
	const float invAtlasH = 1.0f / (float)atlasH;

	Int enabled = 0;
	for (Int b = 0; b < m_splatNumCompact; ++b) {
		if ((presentMask & ((UnsignedInt64)1 << b)) == 0) continue;
		const Int slot     = m_splatCompactChannel[b];
		const Int classIdx = m_splatCompactClass[b];
		if (slot < 0 || slot >= SPLAT_MAX_ACTIVE_MATERIALS) continue;
		if (classIdx < 0 || classIdx >= m_numTextureClasses) continue;

		const TXTextureClass& tc = m_textureClasses[classIdx];
		if (terrainPage >= 0 && tc.texturePage != terrainPage) continue;

		const Int width  = (tc.width > 0) ? tc.width : 1;
		const Int pixExt = (tc.tilePixelExtent > 0) ? tc.tilePixelExtent : TILE_PIXEL_EXTENT;
		const float worldPeriod = (float)(width * 2) * MAP_XY_FACTOR;

		outRegionA[slot * 4 + 0] = (float)tc.positionInTexture.x * invAtlasW;
		outRegionA[slot * 4 + 1] = (float)tc.positionInTexture.y * invAtlasH;
		outRegionA[slot * 4 + 2] = (float)(width * pixExt) * invAtlasW;
		outRegionA[slot * 4 + 3] = (float)(width * pixExt) * invAtlasH;
		outRegionB[slot * 4 + 0] = (worldPeriod > 0.0f) ? (1.0f / worldPeriod) : 0.0f;
		outRegionB[slot * 4 + 1] = outRegionB[slot * 4 + 0];
		outSlotEnableMask[slot] = 1.0f;
		++enabled;
	}
	return enabled;
}

// @feature Ronin 06/05/2026 Splat S20-A3: mark the per-material weight atlas as dirty.
void WorldHeightMap::invalidateSplatTextures()
{
	m_primaryBlendControlTextureDirty = TRUE;
}

// @feature Ronin 06/05/2026 Splat S20-A3: lazily (re)build the per-material weight atlas.
// Idempotent: cheap when m_primaryBlendControlTextureDirty == FALSE.
void WorldHeightMap::ensureSplatTextures()
{
	if (m_tileNdxes == nullptr || m_blendTileNdxes == nullptr) {
		return;
	}
	if (m_perMaterialWeightBytes != nullptr && !m_primaryBlendControlTextureDirty) {
		return;
	}

	const Int texelsPerCell = (m_primaryBlendTexelsPerCell > 0) ? m_primaryBlendTexelsPerCell : 4;
	const Int cellsX = m_width - 1;
	const Int cellsY = m_height - 1;
	if (cellsX <= 0 || cellsY <= 0) {
		return;
	}

	buildPerMaterialWeightTextures(/*texelsPerCell*/ texelsPerCell, /*blurRadiusCells*/ 1);
	ensurePerMaterialWeightAtlasTextures();

	// @feature Ronin 10/05/2026 Normal-map N1: build the per-material normal atlas in
	// lockstep with the weight atlas. v1 fills every page with the flat-normal default;
	// per-source-tile asset load (<basename>_NRM.dds / .tga) lands in the next commit.
	ensurePerMaterialNormalAtlasTextures();


	m_primaryBlendControlTextureDirty = FALSE;
}

void WorldHeightMap::setTextureLOD(Int lod)
{
	if (m_terrainTex)
		m_terrainTex->setLOD(lod);
}

TextureClass *WorldHeightMap::getTerrainTexture()
{
	if (m_terrainTex == nullptr) {
		updateTileTexturePositions(nullptr);

		for (Int page = 0; page < MAX_TEXTURE_ATLAS_PAGES; ++page) {
			// @feature Ronin 08/04/2026 Rebuild all per-page terrain atlas textures after layout changes.
			REF_PTR_RELEASE(m_terrainTextures[page]);
			REF_PTR_RELEASE(m_alphaTerrainTextures[page]);
			REF_PTR_RELEASE(m_alphaEdgeTextures[page]);
		}

		for (Int page = 0; page < m_numTerrainTexturePages; ++page) {
			Int pow2Height = 1;
			while (pow2Height < m_terrainTexturePages[page].textureHeight) {
				pow2Height *= 2;
			}

			m_terrainTextures[page] = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height);
			m_terrainTexturePages[page].textureHeight = m_terrainTextures[page]->update(this, page);

			m_alphaTerrainTextures[page] = MSGNEW("WorldHeightMap_getTerrainTexture") AlphaTerrainTextureClass(m_terrainTextures[page]);

			if (page == 0) {
				REF_PTR_RELEASE(m_terrainTex);
				REF_PTR_SET(m_terrainTex, m_terrainTextures[page]);
				m_terrainTexHeight = m_terrainTexturePages[page].textureHeight;

				REF_PTR_RELEASE(m_alphaTerrainTex);
				REF_PTR_SET(m_alphaTerrainTex, m_alphaTerrainTextures[page]);

				char buf[64];
				sprintf(buf, "Base tex height %d", pow2Height);
				DEBUG_LOG((buf));
			}
		}

		for (Int page = 0; page < m_numEdgeTexturePages; ++page) {
			Int pow2Height = 1;
			while (pow2Height < m_edgeTexturePages[page].textureHeight) {
				pow2Height *= 2;
			}

			m_alphaEdgeTextures[page] = MSGNEW("WorldHeightMap_getTerrainTexture") AlphaEdgeTextureClass(pow2Height);
			m_edgeTexturePages[page].textureHeight = m_alphaEdgeTextures[page]->update(this, page);

			if (page == 0) {
				REF_PTR_RELEASE(m_alphaEdgeTex);
				REF_PTR_SET(m_alphaEdgeTex, m_alphaEdgeTextures[page]);
				m_alphaEdgeHeight = m_edgeTexturePages[page].textureHeight;
			}
		}

		//Generate lookup table for determining triangle order in each terrain cell.
		//Not the best place to put this but getAlphaUVData() requires a valid terrain
		//texture to return valid values.

		for (Int y=0; y<(m_height-1); y++)
			for (Int x=0; x<(m_width-1); x++)
			{
				UnsignedByte alpha[4];
				float UA[4], VA[4];
				Bool flipForBlend;

				getAlphaUVData(x, y, UA, VA, alpha, &flipForBlend);

				m_cellFlipState[y*m_flipStateWidth+(x>>3)] |= flipForBlend << (x & 0x7);
				DEBUG_ASSERTCRASH ((y*m_flipStateWidth+(x>>3)) < (m_flipStateWidth * m_height), ("Bad range"));
			}
	}

	return m_terrainTex;
}

TextureClass *WorldHeightMap::getAlphaTerrainTexture()
{
	if (m_alphaTerrainTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaTerrainTex;
}

TextureClass *WorldHeightMap::getEdgeTerrainTexture()
{
	if (m_alphaEdgeTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaEdgeTex;
}

TerrainTextureClass *WorldHeightMap::getFlatTexture(Int xCell, Int yCell, Int cellWidth, Int pixelsPerCell)
{
	if (WW3D::Get_Texture_Reduction()) {
		if (WW3D::Get_Texture_Reduction()>1) {
			pixelsPerCell /= 4;
		} else {
			pixelsPerCell /= 2;
		}
	}
	Int pow2Height = 1;
	while (pow2Height<cellWidth*pixelsPerCell) {
		pow2Height *=2;
	}
	TerrainTextureClass *newTexture = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height, pow2Height);
	newTexture->updateFlat(this, xCell, yCell, cellWidth, pixelsPerCell);
	return newTexture;
}

Region2D WorldHeightMap::getDrawRegion2D()
{
	// Get region in heightmap space
	const Int loX = getDrawOrgX() - getBorderSizeInline();
	const Int loY = getDrawOrgY() - getBorderSizeInline();
	const Int hiX = loX + getDrawWidth();
	const Int hiY = loY + getDrawHeight();

	// Convert to world space
	Region2D region;
	region.lo.x = loX * MAP_XY_FACTOR;
	region.lo.y = loY * MAP_XY_FACTOR;
	region.hi.x = hiX * MAP_XY_FACTOR;
	region.hi.y = hiY * MAP_XY_FACTOR;

	return region;
}

WorldHeightMap::DrawArea WorldHeightMap::createDrawArea(Int xOrg, Int yOrg)
{
	DrawArea area;
	area.sizeX = std::min(m_drawWidthX, m_width);
	area.sizeY = std::min(m_drawHeightY, m_height);
	area.originX = clamp(0, xOrg, m_width - area.sizeX);
	area.originY = clamp(0, yOrg, m_height - area.sizeY);

	return area;
}

Bool WorldHeightMap::setDrawArea(const DrawArea& area)
{
	Bool anythingDifferent =
		m_drawOriginX != area.originX ||
		m_drawOriginY != area.originY ||
		m_drawWidthX != area.sizeX ||
		m_drawHeightY != area.sizeY;

	if (anythingDifferent) {
		m_drawOriginX = area.originX;
		m_drawOriginY = area.originY;
		m_drawWidthX = area.sizeX;
		m_drawHeightY = area.sizeY;


		// @feature Ronin 06/05/2026 (S20-A3) Splat data is whole-map
		// absolute and indexed in world XY; draw-window panning never invalidates it.
		return(true);
	}
	return false;
}

Bool WorldHeightMap::setDrawOrg(Int xOrg, Int yOrg)
{
	return setDrawArea(createDrawArea(xOrg, yOrg));
}


/** Gets global texture class. */
Int WorldHeightMap::getTextureClass(Int xIndex, Int yIndex, Bool baseClass)
{
	Int ndx = (yIndex*m_width)+xIndex;
	DEBUG_ASSERTCRASH((ndx>=0 && ndx<this->m_dataSize),("oops"));
	if (ndx<0 || ndx >= this->m_dataSize) return(-1);
	Int textureNdx = m_tileNdxes[ndx];
	if (!baseClass && (m_blendTileNdxes[ndx] != 0 || m_extraBlendTileNdxes[ndx] != 0)) {
		return(-1);  // blended, so not of the original class.
	}
	return getTextureClassFromNdx(textureNdx);
}


/** Sets all the cliff flags in map based on height. */
void WorldHeightMap::initCliffFlagsFromHeights()
{
	Int xIndex, yIndex;

	for (xIndex=0; xIndex<m_width-1; xIndex++) {
		for (yIndex=0; yIndex<m_height-1; yIndex++) {
			setCellCliffFlagFromHeights(xIndex, yIndex);
		}
	}
}

/** Sets the cliff flag for a cell based on height. */
void WorldHeightMap::setCellCliffFlagFromHeights(Int xIndex, Int yIndex)
{
	Real height1 = getHeight(xIndex, yIndex)*MAP_HEIGHT_SCALE;
	Real height2 = getHeight(xIndex+1, yIndex)*MAP_HEIGHT_SCALE;
	Real height3 = getHeight(xIndex, yIndex+1)*MAP_HEIGHT_SCALE;
	Real height4 = getHeight(xIndex+1, yIndex+1)*MAP_HEIGHT_SCALE;
	Real minZ = height1;
	if (minZ > height2) minZ = height2;
	if (minZ > height3) minZ = height3;
	if (minZ > height4) minZ = height4;
	Real maxZ = height1;
	if (maxZ < height2) maxZ = height2;
	if (maxZ < height3) maxZ = height3;
	if (maxZ < height4) maxZ = height4;
	const Real cliffRange = PATHFIND_CLIFF_SLOPE_LIMIT_F;
	Bool isCliff = (maxZ-minZ > cliffRange);
	setCliffState(xIndex, yIndex, isCliff);

}

/** Gets global texture class. */
Int WorldHeightMap::getTextureClassFromNdx(Int tileNdx)
{
	Int i;
	tileNdx = tileNdx>>2;
	for (i=0; i<m_numTextureClasses; i++) {
		if (m_textureClasses[i].firstTile<0) {
			continue;
		}
		// see if the blend tile is in a texture class, and get the right tile for xIndex, yIndex.
		if (tileNdx >= m_textureClasses[i].firstTile &&
			tileNdx < m_textureClasses[i].firstTile+m_textureClasses[i].numTiles) {
			return(m_textureClasses[i].globalTextureClass);
		}
	}
	return(-1);
}

TXTextureClass WorldHeightMap::getTextureFromIndex( Int textureIndex )
{
	return m_textureClasses[textureIndex];
}

void WorldHeightMap::getTerrainColorAt(Real x, Real y, RGBColor *pColor)
{
	Int xIndex = REAL_TO_INT_FLOOR(x/MAP_XY_FACTOR);
	Int yIndex = REAL_TO_INT_FLOOR(y/MAP_XY_FACTOR);
	xIndex += m_borderSize;
	yIndex += m_borderSize;
	pColor->red = pColor->green = pColor->blue = 0;
	if (xIndex<0) xIndex = 0;
	if (yIndex<0) yIndex = 0;
	if (xIndex >= m_width) xIndex = m_width-1;
	if (yIndex >= m_height) yIndex = m_height-1;
	Int ndx = (yIndex*m_width)+xIndex;
	if (ndx<0 || ndx >= this->m_dataSize) return;
	Int tileNdx = m_tileNdxes[ndx];
	tileNdx = tileNdx>>2;	 // We pack 4 grids into a tile.

	TileData *pTile = getSourceTile(tileNdx);
	if (pTile) {
		// pTile contains the bitmap data for 4 squares.
		// Get the data mipped down to one pixel for the tile.
		UnsignedByte *pData = pTile->getRGBDataForWidth(1);
		// Data is in microsoft bgra format.
		pColor->red = pData[2]/255.0;
		pColor->green = pData[1]/255.0;
		pColor->blue = pData[0]/255.0;
	}
}

AsciiString WorldHeightMap::getTerrainNameAt(Real x, Real y)
{
	Int xIndex = REAL_TO_INT_FLOOR(x/MAP_XY_FACTOR);
	Int yIndex = REAL_TO_INT_FLOOR(y/MAP_XY_FACTOR);
	xIndex += m_borderSize;
	yIndex += m_borderSize;
	if (xIndex<0) xIndex = 0;
	if (yIndex<0) yIndex = 0;
	if (xIndex >= m_width) xIndex = m_width-1;
	if (yIndex >= m_height) yIndex = m_height-1;
	Int ndx = (yIndex*m_width)+xIndex;
	if (ndx<0 || ndx >= this->m_dataSize) return AsciiString::TheEmptyString;
	Int tileNdx = m_tileNdxes[ndx];
	tileNdx = tileNdx>>2;	 // We pack 4 grids into a tile.

	Int i;
	for (i=0; i<this->m_numTextureClasses; i++) {
		if (tileNdx >= m_textureClasses[i].firstTile && tileNdx < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles) {
			return(m_textureClasses[i].name);
		}
	}
	return AsciiString::TheEmptyString;
}


static std::vector<UnsignedByte> s_buffer;
static std::vector<UnsignedByte> s_blendBuffer;

UnsignedByte* WorldHeightMap::getPointerToTileData(Int xIndex, Int yIndex, Int width)
{
	Int ndx = (yIndex * m_width) + xIndex;
	if (yIndex < 0 || xIndex < 0 || xIndex >= m_width || yIndex >= m_height) {
		return nullptr;
	}
	if (ndx < 0 || ndx >= m_dataSize) {
		return nullptr;
	}

	const Int requiredLen = width * width * TILE_BYTES_PER_PIXEL;
	if ((Int)s_buffer.size() < requiredLen) {
		s_buffer.resize(requiredLen);
	}
	if ((Int)s_blendBuffer.size() < requiredLen) {
		s_blendBuffer.resize(requiredLen);
	}

	TBlendTileInfo* pBlend = nullptr;
	Short tileNdx = m_tileNdxes[ndx];
	if (getRawTileData(tileNdx, width, s_buffer.data(), requiredLen)) {
		Short blendTileNdx = m_blendTileNdxes[ndx];
		if (blendTileNdx > 0 && blendTileNdx < NUM_BLEND_TILES) {
			pBlend = &m_blendedTiles[blendTileNdx];
			if (getRawTileData(pBlend->blendNdx, width, s_blendBuffer.data(), requiredLen)) {
				UnsignedByte* pAlpha = getRGBAlphaDataForWidth(width, pBlend);
				pAlpha += 3;  // skip over the rgb to the a.
				Int i, limit;
				limit = width * width;
				UnsignedByte* pBlendData = s_blendBuffer.data();
				UnsignedByte* pDestData = s_buffer.data();
				for (i = 0; i < limit; i++) {
					Int r, g, b, a;
					b = *pBlendData++;
					g = *pBlendData++;
					r = *pBlendData++; pBlendData++;
					a = *pAlpha; pAlpha += 4;
					*pDestData++ = ((b * a) / 255) + (((*pDestData) * (255 - a)) / 255);
					*pDestData++ = ((g * a) / 255) + (((*pDestData) * (255 - a)) / 255);
					*pDestData++ = ((r * a) / 255) + (((*pDestData) * (255 - a)) / 255);
					*pDestData++ = 255;
				}
			}
		}
		return s_buffer.data();
	}

	return nullptr;
}

#define K_HORIZ 0
#define K_VERT 1
#define K_LDIAG 2
#define K_RDIAG 3
#define K_LLDIAG 4
#define K_LRDIAG 5
#define K_DIR_MOD 0x05
#define K_INV 6

static Int getAlphaMaskTileNdxForBlendInfo(const TBlendTileInfo* pBlend)
{
	if (pBlend == nullptr) {
		return -1;
	}

	Int alphaTileNdx = 0;
	if (pBlend->horiz) {
		alphaTileNdx = K_HORIZ;
	}
	else if (pBlend->vert) {
		alphaTileNdx = K_VERT;
	}
	else if (pBlend->rightDiagonal) {
		alphaTileNdx = pBlend->longDiagonal ? K_LRDIAG : K_RDIAG;
	}
	else if (pBlend->leftDiagonal) {
		alphaTileNdx = pBlend->longDiagonal ? K_LLDIAG : K_LDIAG;
	}

	if (pBlend->inverted & INVERTED_MASK) {
		alphaTileNdx += K_INV;
	}

	if (alphaTileNdx < 0 || alphaTileNdx >= NUM_ALPHA_TILES) {
		return -1;
	}

	return alphaTileNdx;
}

// @feature Ronin 13/04/2026 Accept const blend descriptions for control-texture generation helpers.(Probably to never be used)
UnsignedByte* WorldHeightMap::getRGBAlphaDataForWidth(Int width, const TBlendTileInfo* pBlend)
{
	// @bugfix Ronin 15/04/2026 Defend against shared alpha-mask tiles being null after another WorldHeightMap instance tears them down.
	if (pBlend == nullptr || width <= 0) {
		return nullptr;
	}

	const Int alphaTileNdx = getAlphaMaskTileNdxForBlendInfo(pBlend);
	if (alphaTileNdx < 0 || alphaTileNdx >= NUM_ALPHA_TILES) {
		return nullptr;
	}

	if (m_alphaTiles[alphaTileNdx] == nullptr) {
		setupAlphaTiles();
	}

	TileData* pAlphaTile = m_alphaTiles[alphaTileNdx];
	if (pAlphaTile == nullptr) {
		return nullptr;
	}

	if (!pAlphaTile->hasRGBDataForWidth(width)) {
		return nullptr;
	}

	return pAlphaTile->getRGBDataForWidth(width);
}

void WorldHeightMap::setupAlphaTiles()
{
	TBlendTileInfo blendInfo;
	if (m_alphaTiles[0] != nullptr) return;
	Int k;
	for (k=0; k<NUM_ALPHA_TILES; k++) {
		memset(&blendInfo, 0, sizeof(blendInfo));
		Int baseK = k;
		if (k>=K_INV) {
			blendInfo.inverted = true;
			baseK -= K_INV;
		}
		switch(baseK) {
			case K_HORIZ : blendInfo.horiz = true; break;
			case K_VERT : blendInfo.vert = true; break;
			case K_LDIAG : blendInfo.leftDiagonal = true; break;
			case K_RDIAG : blendInfo.rightDiagonal = true; break;
			case K_LLDIAG : blendInfo.leftDiagonal = true; blendInfo.longDiagonal = true; break;
			case K_LRDIAG : blendInfo.rightDiagonal = true; blendInfo.longDiagonal = true; break;
		}
		m_alphaTiles[k] = new TileData;
		TileData *pTile = m_alphaTiles[k];

		Int i, j;
		UnsignedByte *pDest = pTile->getDataPtr();
		for (j=0; j<TILE_PIXEL_EXTENT; j++) {
			for (i=0; i<TILE_PIXEL_EXTENT; i++) {
				Int h = i;
				Int v = j;
				Int alpha = 255;  // 0 - 255.
				if (blendInfo.horiz) {
					if (!blendInfo.inverted) h = TILE_PIXEL_EXTENT-h-1;
					alpha = (alpha*h)/(TILE_PIXEL_EXTENT-1);
				} else if (blendInfo.vert) {
					if (!blendInfo.inverted) v = TILE_PIXEL_EXTENT-v-1;
					alpha = (alpha*v)/(TILE_PIXEL_EXTENT-1);
				} else if (blendInfo.rightDiagonal) {
					h = TILE_PIXEL_EXTENT-h-1;
					if (!blendInfo.inverted) v = TILE_PIXEL_EXTENT-v-1;
					v += h;				// angled
					if (blendInfo.longDiagonal) {
						v -= TILE_PIXEL_EXTENT;
					}
					alpha = (alpha*v)/(TILE_PIXEL_EXTENT-1);
				} else if (blendInfo.leftDiagonal) {
					if (!blendInfo.inverted) v = TILE_PIXEL_EXTENT-v-1;
					v += h;				// angled
					if (blendInfo.longDiagonal) {
						v -= TILE_PIXEL_EXTENT;
					}
					alpha = (alpha*v)/(TILE_PIXEL_EXTENT-1);
				}

				if (alpha > 255) alpha = 255;
				if (alpha<0) alpha = 0;
				alpha = 255-alpha;

				pDest += 3; // skip blue, green & red bytes.
				*pDest = alpha;		// alpha.
				//*pDest = 255;
				pDest++;
			}
		}
		pTile->updateMips();
	}
}


Bool  WorldHeightMap::getRawTileData(Short tileNdx, Int width,
																				 UnsignedByte *buffer, Int bufLen)
{
	TileData *pSrc = nullptr;
	if (tileNdx/4 < NUM_SOURCE_TILES) {
		pSrc = m_sourceTiles[tileNdx/4];
	}
	if (bufLen < (width*width*TILE_BYTES_PER_PIXEL)) {
		return(false);
	}
	if (pSrc && pSrc->hasRGBDataForWidth(2*width)) {
		Int j;
		UnsignedByte *pSrcData = pSrc->getRGBDataForWidth(2*width);
		Int xOffset=0;
		Int yOffset=0;
		if (tileNdx & 1) xOffset = width;
		if (tileNdx & 2) yOffset = width;
		for (j=0; j<width; j++) {
			UnsignedByte *pDestData = buffer;
			pDestData += j*(width)*TILE_BYTES_PER_PIXEL;
			UnsignedByte *pSrc = pSrcData;
			pSrc += (j+yOffset)*width*TILE_BYTES_PER_PIXEL*2;
			pSrc += xOffset*TILE_BYTES_PER_PIXEL;
			memcpy(pDestData, pSrc, width*TILE_BYTES_PER_PIXEL);
		}
		return(true);
	}
	return(false);
}

