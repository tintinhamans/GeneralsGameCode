// FILE: MapData.h /////////////////////////////////////////////////////////////////////////////
// used to store extra map metadata via map.ini, is reset before loading a map
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once


class MapData : public SubsystemInterface
{

public:

	MapData();
	virtual ~MapData() = default;

	virtual void init();
	virtual void reset();
	virtual void update() {};

	

	static void parseMapDataDefinition( INI* ini );
	static MapData* createMapDataSystem(void);

	Real m_HeightmapScale;
	Bool m_enableShips;
	Bool m_aiSpecialPowersRespectFog;
	Bool m_aiSpecialPowersRespectStealth;

	// Height based terrain ambient light (e.g. water depth lighting). Per-map.
	RGBColor m_terrainHeightAmbientLightColor1;
	RGBColor m_terrainHeightAmbientLightColor2;
	Real m_terrainHeightAmbientLightHeightStart;
	Real m_terrainHeightAmbientLightHeight1;
	Real m_terrainHeightAmbientLightHeight2;
	Bool m_terrainHeightAmbientLightAdditive;

private:
	void setDefaults();
	static const FieldParse s_MapDataFieldParseTable[];
};

// singleton
extern MapData* TheWriteableMapData;

inline const MapData* const& TheMapData = TheWriteableMapData;

inline MapData* MapData::createMapDataSystem(void) { return NEW MapData; }
