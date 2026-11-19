//////////////////////////////////////////
// FILE: MapData.cpp
// Store some extra metadata about the current map
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/MapData.h"

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
MapData* TheWriteableMapData = NULL;				///< The current map data singleton

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ const FieldParse MapData::s_MapDataFieldParseTable[] =
{
	{ "HeightMapScale",									INI::parseReal,				NULL,			offsetof( MapData, m_HeightmapScale) },
	{ "EnableShips",									  INI::parseBool,       NULL,     offsetof( MapData, m_enableShips) },
	{ "AISpecialPowersRespectFog", INI::parseBool, NULL, offsetof( MapData, m_aiSpecialPowersRespectFog) },
	{ "AISpecialPowersRespectStealth", INI::parseBool, NULL, offsetof( MapData, m_aiSpecialPowersRespectStealth) },
	{ "TerrainHeightAmbientLightColor1",	INI::parseRGBColor,		NULL,			offsetof( MapData, m_terrainHeightAmbientLightColor1) },
	{ "TerrainHeightAmbientLightColor2",	INI::parseRGBColor,		NULL,			offsetof( MapData, m_terrainHeightAmbientLightColor2) },
	{ "TerrainHeightAmbientLightStart",		INI::parseReal,				NULL,			offsetof( MapData, m_terrainHeightAmbientLightHeightStart) },
	{ "TerrainHeightAmbientLightHeight1",	INI::parseReal,				NULL,			offsetof( MapData, m_terrainHeightAmbientLightHeight1) },
	{ "TerrainHeightAmbientLightHeight2",	INI::parseReal,				NULL,			offsetof( MapData, m_terrainHeightAmbientLightHeight2) },
	{ "TerrainHeightAmbientLightAdditive",INI::parseBool,				NULL,			offsetof( MapData, m_terrainHeightAmbientLightAdditive) },
	{ nullptr, nullptr, nullptr, 0 }  // keep this last

};

void MapData::setDefaults()
{
	m_HeightmapScale = 1.0f;
	m_enableShips = false;
	m_aiSpecialPowersRespectFog = true;
	m_aiSpecialPowersRespectStealth = true;

	m_terrainHeightAmbientLightColor1.red = 0;
	m_terrainHeightAmbientLightColor1.green = 0;
	m_terrainHeightAmbientLightColor1.blue = 0;

	m_terrainHeightAmbientLightColor2.red = 0;
	m_terrainHeightAmbientLightColor2.green = 0;
	m_terrainHeightAmbientLightColor2.blue = 0;

	m_terrainHeightAmbientLightHeightStart = -1;
	m_terrainHeightAmbientLightHeight1 = -1;
	m_terrainHeightAmbientLightHeight2 = -1;

	m_terrainHeightAmbientLightAdditive = false;
}

MapData::MapData() : SubsystemInterface()
{
	setDefaults();
}

void MapData::init() {
	setDefaults();
}

void MapData::reset() {
	setDefaults();
}

void MapData::parseMapDataDefinition(INI* ini) {
	ini->initFromINI(TheWriteableMapData, s_MapDataFieldParseTable);
}
