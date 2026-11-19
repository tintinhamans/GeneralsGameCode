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

// FILE: GlobalData.h /////////////////////////////////////////////////////////////////////////////
// Global data used by both the client and logic
// Author: trolfs, Michae Booth, Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include "Common/AsciiString.h"
#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/Color.h"
#include "GameClient/DisruptionShader.h"
#include "GameClient/StormShader.h"
#include "GameClient/HeadlightShader.h"
#include "GameClient/PlanarMirrorShader.h"
#include "GameClient/TintStatus.h"
#include "Common/STLTypedefs.h"
#include "Common/Money.h"
#include "Common/KindOf.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
struct FieldParse;
enum _TerrainLOD CPP_11(: Int);
class CommandLine;
class GlobalData;
class INI;
class ThingTemplate;
class WeaponBonusSet;
enum BodyDamageType CPP_11(: Int);
enum AIDebugOptions CPP_11(: Int);
typedef UnsignedInt DeathTypeFlags;
//enum DrawableColorTint CPP_11(: Int);

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

constexpr const Int MAX_GLOBAL_LIGHTS = 3;
constexpr const Int SIMULATE_REPLAYS_SEQUENTIAL = -1;

//-------------------------------------------------------------------------------------------------
// Command-line parsing state is stored here instead of in CommandLine because
// the parsing result belongs to the GlobalData instance created during startup.
class CommandLineData
{
	friend class CommandLine;
	friend class GlobalData;

	CommandLineData()
		: m_hasParsedCommandLineForStartup(false)
		, m_hasParsedCommandLineForEngineInit(false)
	{}

	Bool m_hasParsedCommandLineForStartup;
	Bool m_hasParsedCommandLineForEngineInit;
	BoolVector m_parsedArguments;
};

//-------------------------------------------------------------------------------------------------
// A subdual tuning value: a plain number, or a multiple of the unit's max health ("MaxHealth * 2").
// Unset until an INI key writes it, so callers can tell "absent" from an explicit zero.
struct SubdualValue
{
	Real m_flat;
	Real m_maxHealthFactor;
	Bool m_isSet;

	SubdualValue() : m_flat(0.0f), m_maxHealthFactor(0.0f), m_isSet(FALSE) { }

	Real evaluate( Real maxHealth ) const { return m_flat + maxHealth * m_maxHealthFactor; }

	static void parseFromINI( INI* ini, void* instance, void* store, const void* userData );
	static void parseDurationFromINI( INI* ini, void* instance, void* store, const void* userData );
};

//-------------------------------------------------------------------------------------------------
// One GameData SubdualDamageDefaults block; an empty KindOf matches every object not in ForbiddenKindOf
struct SubdualDamageDefaults
{
	KindOfMaskType m_kindOf;
	KindOfMaskType m_forbiddenKindOf;
	SubdualValue m_subdualDamageCap;
	SubdualValue m_subdualDamageHealRate;
	SubdualValue m_subdualDamageHealAmount;
	SubdualValue m_jammingDamageCap;
	SubdualValue m_jammingDamageHealRate;
	SubdualValue m_jammingDamageHealAmount;
	SubdualValue m_frozenDamageCap;
	SubdualValue m_frozenDamageHealRate;
	SubdualValue m_frozenDamageHealAmount;
	SubdualValue m_chronoDamageHealRate;
	SubdualValue m_chronoDamageHealAmount;
};

//-------------------------------------------------------------------------------------------------
/** Global data container class
  *	Defines all global game data used by the system
	* @todo Change this entire system. Otherwise this will end up a huge class containing tons of variables,
	* and will cause re-compilation dependencies throughout the code base.
  * OOPS -- TOO LATE! :) */
//-------------------------------------------------------------------------------------------------
class GlobalData : public SubsystemInterface
{

public:

	GlobalData();
	virtual ~GlobalData() override;

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override { }

	Bool setTimeOfDay( TimeOfDay tod );		///< Use this function to set the Time of day;

	static void getDefaultResolution( Int &width, Int &height );	///< the primary desktop mode, or 800x600 when it is unreadable or smaller
	static void parseGameDataDefinition( INI* ini );
	static void reloadEditedIni();		///< picks up render tuning keys from GameData.ini saved while the game runs, in cheat builds
	void parseCustomDefinition();

	//-----------------------------------------------------------------------------------------------
	struct TerrainLighting
	{
		RGBColor ambient;
		RGBColor diffuse;
		Coord3D lightPos;
	};

	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------

	CommandLineData m_commandLineData;

	AsciiString m_mapName;  ///< hack for now, this whole this is going away
	AsciiString m_moveHintName;
	Bool m_useTrees;
	Bool m_useTreeSway;
	Bool m_useDrawModuleLOD;
	Bool m_useHeatEffects;
	Bool m_useFpsLimit;
	Bool m_dumpAssetUsage;
	Int m_framesPerSecondLimit;
	Int	m_chipSetType;	///<See W3DShaderManager::ChipsetType for options

	// TheSuperHackers @feature helmutbuhler 11/04/2025
	// Run game without graphics, input or audio.
	Bool m_headless;

	Bool m_windowed;

	// Frameless window at the chosen resolution; runs the device windowed even without -win
	Bool m_borderlessWindow;
	Int m_xResolution;
	Int m_yResolution;
	Int m_maxShellScreens;  ///< this many shells layouts can be loaded at once
	Bool m_useCloudMap;
	Int  m_use3WayTerrainBlends;	///< 0 is none, 1 is normal, 2 is debug.
	Bool m_useLightMap;
	Bool m_bilinearTerrainTex;
	Bool m_trilinearTerrainTex;
	Bool m_multiPassTerrain;
	Bool m_adjustCliffTextures;
	Bool m_stretchTerrain; // TheSuperHackers @info Legacy, unused
	Bool m_useHalfHeightMap; // TheSuperHackers @info Legacy, unused
	Bool m_drawEntireTerrain;
	_TerrainLOD m_terrainLOD;
	Bool m_enableDynamicLOD;
	Bool m_enableStaticLOD;
	Int m_terrainLODTargetTimeMS;
	Bool m_useAlternateMouse;
	Bool m_useRightMouseScrollWithAlternateMouse; // TheSuperHackers @feature User option for RMB scroll in Alternate Mouse mode.
	Bool m_clientRetaliationModeEnabled;
	Bool m_guardRequiresVisibility;
	// TheSuperHackers @feature Client side health bar display preference, from Options.ini.
	// Holds a HealthBarDisplayMode; stored as Int so this widely included header does not
	// have to pull in OptionPreferences.h.
	Int m_healthBarDisplayMode;
	Int m_alliedDecalMode;          ///< Options.ini AlliedDecalMode: how allied power decals are drawn
	Int m_houseColorMode;           ///< TheSuperHackers @feature Options.ini HouseColorMode, a HouseColorMode
	// TheSuperHackers @feature Countdown numbers on build queue and cooldown cameos.
	// Holds a BuildTimerDisplayMode; stored as Int to avoid pulling OptionPreferences.h in here.
	Int m_buildTimerDisplayMode;
	// TheSuperHackers @feature Hotkey activation behaviour for targeted commands.
	// Holds a CastMode; stored as Int to avoid pulling OptionPreferences.h in here.
	Int m_castMode;
	// TheSuperHackers @feature Draw a green hexagon under selected objects.
	Bool m_selectionCircleEnabled;
	// TheSuperHackers @feature Ring the attack range of an armed structure while it is being placed.
	Bool m_defensesRangeCircle;
	// Show a button each for idle infantry, vehicles, aircraft and support units.
	Bool m_queueBar;
	// TheSuperHackers @feature Draw the decals objects ask for with DisplayDecal.
	Bool m_objectDecalsEnabled;
	// TheSuperHackers @feature Keep ammo and passenger pips on screen when there is something
	// to report, rather than only on selection or hover.
	Bool m_smartPips;
	// TheSuperHackers @feature Print hit points beside the health bar.
	Bool m_numericalHealth;
	// TheSuperHackers @feature Outline the radar blips and the shoreline, at double radar
	// resolution. Client side only; the radar never feeds game logic.
	Bool m_newRadar;
	// TheSuperHackers @feature How big the NewRadar object blips draw.
	// Holds a RadarBlipSize; stored as Int to avoid pulling OptionPreferences.h in here.
	Int m_radarBlipSize;
	// TheSuperHackers @feature How long a particle name lingers in the debug name overlay after
	// its system is gone, in milliseconds. 0 shows names only while the system is alive.
	Int m_particleNameLingerMS;
	// TheSuperHackers @feature Command bar keys by slot position instead of by string file.
	Bool m_gridHotkeysEnabled;
	AsciiString m_gridHotkeyLayout;
	// Unit orders keep their string file letter and swap grid keys with the slot that had it.
	Bool m_gridHotkeyUnitOrders;
	// TheSuperHackers @feature Show each command bar cameo's hotkey letter on the cameo.
	Bool m_keyboardOverlayEnabled;
	Color m_keyboardOverlayColor;
	Bool m_keyboardOverlayBackdrop;
	Color m_keyboardOverlayBackdropColor;
	// TheSuperHackers @feature Leave builders out of drag selections.
	Bool m_easyMilitaryDrag;
	// TheSuperHackers @feature Per type cameos with counts above the command bar.
	Bool m_smartSelection;
	Bool m_smartSelectionUseMouse;
	Bool m_smartCommandGroup;
	Int m_actionLineMode;						///< holds an ActionLineMode
	Bool m_doubleClickAttackMove;
	Bool m_facingDrag;
	Bool m_rightMouseAlwaysScrolls;
	Int m_jpegQuality; // TheSuperHackers @feature Quality for JPEG screenshots.
	Bool m_useWaterPlane;
	Bool m_useCloudPlane;
	Bool m_useShadowVolumes;
	Bool m_useShadowDecals;
	Real m_shadowMapMinSunElevation;	///< GameData ShadowMapMinSunElevation: lowest sun, in degrees, the shadow map uses
	Bool m_shadowsAlwaysOn;	///< GameData ShadowsAlwaysOn: objects without a Shadow still cast into the shadow map
	Real m_unitSpecularIntensity;	///< GameData UnitSpecularIntensity: brightness of the sun highlight on vehicles and structures, 0 for none
	Real m_unitSpecularPower;		///< GameData UnitSpecularPower: tightness of that highlight, higher is smaller and sharper
	Real m_unitBumpHeight;			///< GameData UnitBumpHeight: rise, in world units, of full brightness on textures without a normal map, 0 for flat
	Real m_roadBumpHeight;			///< GameData RoadBumpHeight: the same for road and bridge textures without a normal map
	Real m_unitNormalMapStrength;	///< GameData UnitNormalMapStrength: scales the tilt of authored _nrm normal maps
	Real m_terrainNormalMapStrength;	///< GameData TerrainNormalMapStrength: scales the tilt of terrain _nrm normal maps
	Real m_terrainGlintIntensity;	///< GameData TerrainGlintIntensity: brightness of the sun's glint on the ground, 0 for none
	Real m_terrainGlintGloss;		///< GameData TerrainGlintGloss: tightness of that glint, higher is smaller and sharper
	Real m_terrainGlintAlbedo;		///< GameData TerrainGlintAlbedo: how far the glint follows the ground's brightness, 0 not at all to 1 fully
	Real m_unitEmissiveIntensity;		///< GameData UnitEmissiveIntensity: brightness of _emi glow masks on vehicles and structures by day, 0 for none
	Real m_unitEmissiveNightIntensity;	///< GameData UnitEmissiveNightIntensity: the same at night
	Real m_pathTracingLightMaxRadius;
	Real m_pathTracingLightBrightness;
	Real m_softParticleDistance;		///< GameData SoftParticleDistance: how far in front of a surface a particle starts to fade, 0 for hard edges
	Real m_flameWarp;					///< GameData FlameWarp and the keys below: flame shading and heat haze defaults, see FlameShaderTuning
	Real m_flameHeat;
	Real m_flameFlicker;
	Real m_flameBreakup;
	Real m_flameNoiseSize;
	Real m_flameRise;
	Real m_fluidWarp;
	Real m_fluidNoiseSize;
	Real m_fluidSpeed;
	Real m_fluidBody;
	Real m_fluidBubbles;
	Real m_fluidBubbleSize;
	Real m_fluidEdge;
	Real m_fluidGlow;
	Real m_hazeBend;
	Real m_hazeSize;
	Real m_hazeLift;
	Real m_hazeNoiseSize;
	Real m_hazeRise;
	Real m_hazeMask;
	std::vector<AsciiString> m_electricParticleTextures;	///< GameData ElectricParticleTextures: particle textures that get the electric shader when their system is Auto
	Real m_electricArcs;				///< GameData ElectricArcs and the keys below: electric shader settings
	Real m_electricArcSharpness;
	Real m_electricNoiseSize;
	Real m_electricJitter;
	Real m_electricFlicker;
	Real m_electricRate;
	Real m_electricParticleScale;		///< GameData ElectricParticleScale: how much larger or smaller electric-shaded particles draw, 1 unchanged
	std::vector<AsciiString> m_laserParticleTextures;	///< GameData LaserParticleTextures: streak textures that get the laser shader when their system is Auto
	Real m_laserCore;					///< GameData LaserCore and the keys below: laser shader settings
	Real m_laserCoreWidth;
	Real m_laserShimmer;
	Real m_laserPulse;
	Real m_laserPulseSize;
	Real m_laserPulseSpeed;
	Bool m_laserDebug;					///< GameData LaserDebug: shaded beams darken the scene instead of lighting it, to show the shader's shape
	std::vector<AsciiString> m_cryoParticleTextures;	///< GameData CryoParticleTextures: particle textures that get the cryo shader when their system is Auto
	RGBColor m_cryoTint;				///< GameData CryoTint and the keys below: cryo shader settings
	Real m_cryoTintStrength;
	Real m_cryoCore;
	Real m_cryoCoreWidth;
	Real m_cryoFrost;
	Real m_cryoFrostSize;
	Real m_cryoFrostSpeed;
	Real m_cryoShards;
	Real m_cryoShardSize;
	Real m_cryoGlints;
	Real m_cryoGlintSize;
	Real m_cryoGlintRate;
	Real m_cryoParticleScale;			///< GameData CryoParticleScale: how much larger or smaller cryo-shaded particles draw, 1 unchanged
	DisruptionShaderTuning m_disruptionTuning;	///< GameData DisruptionRingStrength and the keys beside it: disruption shader defaults
	HeadlightShaderTuning m_headlightTuning;	///< GameData HeadlightShader and the Headlight keys: headlights drawn in place of HEADLIGHT meshes
	KindOfMaskType m_headlightForbiddenKindOf;	///< GameData HeadlightShaderForbiddenKindOf: objects of these kinds keep their HEADLIGHT meshes
	Bool m_rotrHack;	///< GameData RotrHack: additive HEADLIGHT meshes keep their own look, for Rise of the Reds' lit windows
	Bool m_planarMirrorShader;	///< GameData PlanarMirrorShader: whether models' PlanarMirror meshes reflect, read at launch
	PlanarMirrorShaderTuning m_planarMirrorTuning;	///< GameData PlanarMirrorReflectivity and the keys beside it: what mirrors take where their model sets nothing
	StormShaderInfo m_stormTuning[StormShaderInfo::TYPE_COUNT];	///< GameData SandStorm and SnowStorm keys: what each storm type takes where its entry sets nothing
	Bool m_distanceFog;
	Int m_distanceFogPreset;
	RGBColor m_distanceFogColor;
	Real m_distanceFogStart;
	Real m_distanceFogEnd;
	Real m_distanceFogStrength;
	Real m_distanceFogSunInfluence;
	Real m_ambientOcclusionRadius;		///< GameData AmbientOcclusionRadius: how far, in world units, geometry darkens what is near it
	Real m_ambientOcclusionStrength;	///< GameData AmbientOcclusionStrength: how dark the occlusion gets, 0 for none
	Real m_groundNoiseStrength;			///< GameData GroundNoiseStrength and the keys below: the noise that stands in for the light map, see W3DGroundNoise
	Real m_groundNoiseSize;
	Real m_groundNoiseTint;
	Real m_groundNoiseBrightness;
	Real m_terrainHeightBlendStrength;	///< GameData TerrainHeightBlendStrength: how far the taller texture pushes into the other's side of a blend
	Real m_terrainHeightBlendSharpness;	///< GameData TerrainHeightBlendSharpness: how narrow the blend's edge is, 1 as wide as the legacy blend
	Real m_skyCloudSize;				///< GameData SkyCloudSize and the keys below: the HQ sky's cloud shadows, see W3DSkyClouds
	Real m_skyCloudCoverage;
	Real m_skyCloudSoftness;
	Real m_skyCloudShadowStrength;
	RGBColor m_skyCloudShadowTint;
	Real m_skyCloudWindSpeed;
	Real m_skyCloudWindAngle;
	Real m_skyCloudChurn;
	Real m_skyCloudBillow;
	Real m_skyCloudDetail;
	AsciiString m_colorLut;			///< GameData ColorLut and the keys below: the colour grade over the 3D scene, see W3DColorLut
	Real m_colorLutStrength;
	Real m_colorLutBrightness;
	Real m_colorLutContrast;
	Real m_colorLutSaturation;
	RGBColor m_colorLutTint;
	Real m_colorLutChroma;
	Real m_colorLutLuma;
	Real m_colorLutVibrance;
	Real m_colorLutTechnicolor;
	Real m_colorLutBlackPoint;
	Real m_colorLutWhitePoint;
	Real m_colorLutGamma;
	Real m_colorLutOutputBlack;
	Real m_colorLutOutputWhite;
	Real m_colorLutVignette;
	Real m_colorLutVignetteRadius;
	Real m_colorLutGrain;
	Real m_colorLutDither;
	Int  m_textureReductionFactor;	//how much to cut texture resolution: 2 is half, 3 is quarter, etc.
	Bool m_enableBehindBuildingMarkers;
	Real m_waterPositionX;
	Real m_waterPositionY;
	Real m_waterPositionZ;
	Real m_waterExtentX;
	Real m_waterExtentY;
	Int	m_waterType;
	Bool m_showSoftWaterEdge;
	Bool m_usingWaterTrackEditor;
	Bool m_isWorldBuilder;

	Int m_featherWater;

	// these are for WATER_TYPE_3 vertex animated water
	enum { MAX_WATER_GRID_SETTINGS = 4 };
	AsciiString m_vertexWaterAvailableMaps[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampLow[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampHi[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAngle[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterXPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterYPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterZPosition[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterXGridCells[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterYGridCells[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterGridSize[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationA[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationB[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationC[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationRange[ MAX_WATER_GRID_SETTINGS ];

	Real m_downwindAngle;
	Real m_skyBoxPositionZ;
	Real m_drawSkyBox;
	Real m_skyBoxScale;
	Real m_viewportHeightScale; // The height scale of the tactical view ranging 0..1. Used to hide the world behind the Control Bar.
	Real m_cameraPitch;
	Real m_cameraYaw;
#if PRESERVE_RETAIL_SCRIPTED_CAMERA
	Real m_cameraHeight;
#endif
	Real m_maxCameraHeight;
	Real m_defaultMaxCameraHeight; ///< MaxCameraHeight as parsed from INI, before Options.ini overrides it
	Real m_minCameraHeight;
	Real m_terrainHeightAtEdgeOfMap;
	Real m_unitDamagedThresh;
	Real m_unitReallyDamagedThresh;
	Real m_groundStiffness;
	Real m_structureStiffness;
	Real m_gravity;	// acceleration due to gravity, in dist/frame^2
	Real m_stealthFriendlyOpacity;
	UnsignedInt m_defaultOcclusionDelay;	///<time to delay building occlusion after object is created.

	Bool m_preloadAssets;
	Bool m_preloadEverything;			///< Preload everything, everywhere (for debugging only)
	Bool m_preloadReport;					///< dump a log of all W3D assets that are being preloaded.

	Real m_partitionCellSize;

	Coord3D m_ammoPipWorldOffset;
	Coord3D m_containerPipWorldOffset;
	Coord2D m_ammoPipScreenOffset;
	Coord2D m_containerPipScreenOffset;
	Real m_ammoPipScaleFactor;
	Real m_containerPipScaleFactor;

	Real m_progressBarYOffset;

	UnsignedInt m_historicDamageLimit;

	//Settings for terrain tracks left by vehicles with treads or wheels
	Int m_maxTerrainTracks; ///<maximum number of objects allowed to generate tracks.
	Int m_maxTankTrackEdges;	///<maximum length of tank track
	Int m_maxTankTrackOpaqueEdges;	///<maximum length of tank track before it starts fading.
	Int m_maxTankTrackFadeDelay;	///<maximum amount of time a tank track segment remains visible.

	AsciiString m_levelGainAnimationName; ///< The animation to play when a level is gained.
	Real m_levelGainAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_levelGainAnimationZRisePerSecond;					///< rise animation up while playing

	AsciiString m_getHealedAnimationName; ///< The animation to play when emergency repair does its thing.
	Real m_getHealedAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_getHealedAnimationZRisePerSecond;					///< rise animation up while playing

	TimeOfDay	m_timeOfDay;
	Weather m_weather;
	Bool m_makeTrackMarks;
	Bool m_hideGarrisonFlags;
	Bool m_forceModelsToFollowTimeOfDay;
	Bool m_forceModelsToFollowWeather;

	TerrainLighting	m_terrainLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];
	TerrainLighting	m_terrainObjectsLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];

	//Settings for each global light
	RGBColor m_terrainAmbient[MAX_GLOBAL_LIGHTS];
	RGBColor m_terrainDiffuse[MAX_GLOBAL_LIGHTS];
	Coord3D m_terrainLightPos[MAX_GLOBAL_LIGHTS];

	Real m_infantryLightScale[TIME_OF_DAY_COUNT];
	Real m_scriptOverrideInfantryLightScale;

	Real m_soloPlayerHealthBonusForDifficulty[PLAYERTYPE_COUNT][DIFFICULTY_COUNT];

	Int m_maxVisibleTranslucentObjects;
	Int m_maxVisibleOccluderObjects;
	Int m_maxVisibleOccludeeObjects;
	Int m_maxVisibleNonOccluderOrOccludeeObjects;
	Real m_occludedLuminanceScale;

	Int m_numGlobalLights;	//number of active global lights
	Int m_maxRoadSegments;
	Int m_maxRoadVertex;
	Int m_maxRoadIndex;
	Int m_maxRoadTypes;

	Bool m_audioOn;
	Bool m_musicOn;
	Bool m_soundsOn;
	Bool m_sounds3DOn;
	Bool m_speechOn;
	Bool m_videoOn;
	Bool m_disableCameraMovement;

	Bool m_useFX;									///< If false, don't render effects
	Bool m_showClientPhysics;
	Bool m_showTerrainNormals;

	UnsignedInt m_noDraw;					///< Used to disable drawing, to profile game logic code.
	AIDebugOptions m_debugAI;			///< Used to display AI debug information
	Bool m_debugSupplyCenterPlacement; ///< Dumps to log everywhere it thinks about placing a supply center
	Bool m_debugAIObstacles;			///< Used to display AI obstacle debug information
	Bool m_showObjectHealth;			///< debug display object health
	Bool m_scriptDebug;						///< Should we attempt to load the script debugger window (.DLL)
	Bool m_particleEdit;					///< Should we attempt to load the particle editor (.DLL)
	Bool m_displayDebug;					///< Used to display display debug info
	Bool m_winCursors;						///< Should we force use of windows cursors?
	Bool m_constantDebugUpdate;		///< should we update the debug stats constantly, vs every 2 seconds?
	Bool m_showTeamDot;						///< Shows the little colored team dot representing which team you are controlling.

#ifdef DUMP_PERF_STATS
	Bool m_dumpPerformanceStatistics;
  Bool  m_dumpStatsAtInterval;///< should I automatically dump stats every N frames
  Int   m_statsInterval;       ///< if so, how many is N?
#endif

	Bool m_forceBenchmark;	///<forces running of CPU detection benchmark, even on known cpu's.

	Int m_fixedSeed;							///< fixed random seed for game logic (less than 0 to disable)

	Real m_particleScale;					///< Global size modifier for particle effects.

	AsciiString m_autoFireParticleSmallPrefix;
	AsciiString m_autoFireParticleSmallSystem;
	Int m_autoFireParticleSmallMax;
	AsciiString m_autoFireParticleMediumPrefix;
	AsciiString m_autoFireParticleMediumSystem;
	Int m_autoFireParticleMediumMax;
	AsciiString m_autoFireParticleLargePrefix;
	AsciiString m_autoFireParticleLargeSystem;
	Int m_autoFireParticleLargeMax;
	AsciiString m_autoSmokeParticleSmallPrefix;
	AsciiString m_autoSmokeParticleSmallSystem;
	Int m_autoSmokeParticleSmallMax;
	AsciiString m_autoSmokeParticleMediumPrefix;
	AsciiString m_autoSmokeParticleMediumSystem;
	Int m_autoSmokeParticleMediumMax;
	AsciiString m_autoSmokeParticleLargePrefix;
	AsciiString m_autoSmokeParticleLargeSystem;
	Int m_autoSmokeParticleLargeMax;
	AsciiString m_autoAflameParticlePrefix;
	AsciiString m_autoAflameParticleSystem;
	Int m_autoAflameParticleMax;

	// Latency insertion, packet loss for network debugging
	Int m_netMinPlayers;					///< Min players needed to start a net game

	Bool m_enableSingleplayerChatWindow;	///< Allow the in-game chat window in singleplayer/skirmish (for chat commands)

	UnsignedInt m_defaultIP;			///< preferred IP address for LAN
	UnsignedInt m_firewallBehavior;	///< Last detected firewall behavior
	UnsignedInt m_firewallPortOverride;	///< User-specified port to be used
	Short m_firewallPortAllocationDelta; ///< the port allocation delta last detected.

	Int m_baseValuePerSupplyBox;
	Real m_BuildSpeed;
	Real m_MinDistFromEdgeOfMapForBuild;
	Real m_SupplyBuildBorder;
	Real m_allowedHeightVariationForBuilding;  ///< how "flat" is still flat enough to build on
	Real m_allowedHeightVariationForBuildingShipyard;  ///< how "flat" is still flat enough to build on
	Real m_MinLowEnergyProductionSpeed;
	Real m_MaxLowEnergyProductionSpeed;
	Real m_LowEnergyPenaltyModifier;
	Real m_MultipleFactory;
	Real m_RefundPercent;

	Real m_commandCenterHealRange;		///< radius in which close by ally things are healed
	Real m_commandCenterHealAmount;   ///< health per logic frame close by things are healed
	Int m_maxLineBuildObjects;				///< line style builds can be no longer than this
	Int m_maxTunnelCapacity;					///< Max people in Player's tunnel network
	Real m_horizontalScrollSpeedFactor;	///< Factor applied to the game screen scrolling speed.
	Real m_verticalScrollSpeedFactor;		///< Separated because of our aspect ratio
	Real m_scrollAmountCutoff;				///< Scroll speed to not adjust camera height
	Real m_cameraAdjustSpeed;					///< Rate at which we adjust camera height
	Bool m_enforceMaxCameraHeight;		///< Enforce max camera height while scrolling?
	Bool m_buildMapCache;
	AsciiString m_initialFile;				///< If this is specified, load a specific map from the command-line
	AsciiString m_pendingFile;				///< If this is specified, use this map at the next game start
	AsciiString m_loadSaveGame;				///< If this is specified, load a save game file from the command-line
	AsciiString m_loadReplayGame;			///< If this is specified, play a replay file from the command-line

	std::vector<AsciiString> m_simulateReplays; ///< If not empty, simulate this list of replays and exit.
	Int m_simulateReplayJobs; ///< Maximum number of processes to use for simulation, or SIMULATE_REPLAYS_SEQUENTIAL for sequential simulation
#if defined(GENERALS_ONLINE)
	Bool m_exportStats; ///< Write game stats JSON next to each simulated replay
	AsciiString m_statsUrl; ///< If not empty, POST the compressed stats JSON here
#endif

	Int m_maxParticleCount;						///< maximum number of particles that can exist
	Int m_maxFieldParticleCount;			///< maximum number of field-type particles that can exist (roughly)
	WeaponBonusSet* m_weaponBonusSet;
	Real m_healthBonus[LEVEL_COUNT];			///< global bonuses to health for veterancy.
	Real m_defaultStructureRubbleHeight;	///< for rubbled structures, compress height to this if none specified

	AsciiString m_shellMapName;				///< Holds the shell map name
	Bool m_shellMapOn;								///< User can set the shell map not to load
	Bool m_playIntro;									///< Flag to say if we're to play the intro or not
	Bool m_playSizzle;								///< Flag to say whether we play the sizzle movie after the logo movie.

	Bool m_loadScreenRender;						///< flag to disallow rendering of almost everything during a loadscreen

	Real m_keyboardScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	Real m_keyboardDefaultScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	Bool m_drawScrollAnchor;					///< Set that the scroll anchor should be enabled
	Bool m_moveScrollAnchor;					///< set that the scroll anchor should move

	Bool m_animateWindows;						///< Should we animate window transitions?

	Bool m_incrementalAGPBuf;

	UnsignedInt m_iniCRC;							///< CRC of important INI files
	UnsignedInt m_exeCRC;							///< CRC of the executable

	BodyDamageType m_movementPenaltyDamageState;	///< at this body damage state, we have movement penalties

	Int m_groupSelectMinSelectSize;		// min number of units to treat as group select for audio feedback
	Real m_groupSelectVolumeBase;			// base volume for group select sound
	Real m_groupSelectVolumeIncrement;// increment to volume for selecting more units
	Int m_maxUnitSelectSounds;				// max number of select sounds to play per selection

	Real m_selectionFlashSaturationFactor; /// how colorful should the selection flash be? 0-4
	Bool m_selectionFlashHouseColor ;  /// skip the house color and just use white.

	Real m_cameraAudibleRadius;				///< If the camera is being used as the position of audio, then how far can we hear?
	Real m_groupMoveClickToGatherFactor; /** if you take all the selected units and calculate the smallest possible rectangle
																			 that contains them all, and click within that, all the selected units will break
																			 formation and gather at the point the user clicked (if the value is 1.0). If it's 0.0,
																			 units will always keep their formation. If it's <1.0, then the user must click a
																			 smaller area within the rectangle to order the gather. */

	UnsignedInt m_antiAliasLevel;          ///< value of selected antialias level in the game options
	Int m_superSampling;                   ///< Options.ini SuperSampling: the world's render size as a percent of the screen's, 100 to 200; 16K lives here for one run and is never saved
	UnsignedInt m_textureFilteringMode;       ///< value related to TextureFilterClass::TextureFilterModeEnum
	UnsignedInt m_textureAnisotropyLevel;     ///< value related to TextureFilterClass::AnisotropicFilterMode

	Bool m_languageFilterPref;        ///< Bool if user wants to filter language
	Bool m_loadScreenDemo;						///< Bool if true, run the loadscreen demo movie
	Bool m_disableRender;							///< if true, no rendering!

	Bool m_saveCameraInReplay;
	Bool m_useCameraInReplay;

	// TheSuperHackers @feature xezon 09/09/2025 Enable the shroud and everything related for observing individual players in any game mode.
	// Enabling this does have a performance cost because the ghost object manager must keep track of all relevant objects for all players.
	Bool m_enablePlayerObserver;

	// TheSuperHackers @feature L3-M 05/09/2025 allow the network latency counter and render fps counter font size to be set, a size of zero disables them
	Int m_networkLatencyFontSize;
	Int m_renderFpsFontSize;
	// TheSuperHackers @feature Mauller 21/06/2025 allow the system time and game time font size to be set, a size of zero disables them
	Int m_systemTimeFontSize;
	Int m_gameTimeFontSize;
	// TheSuperHackers @feature L3-M 05/11/2025 allow the player info list font size to be set, a size of zero disables it
	Int m_playerInfoListFontSize;

	// TheSuperHackers @feature L3-M 21/08/2025 toggle the money per minute display, false shows only the original current money
	Bool m_showMoneyPerMinute;
	Bool m_allowMoneyPerMinuteForPlayer;

#if defined(GENERALS_ONLINE)
	// Observer notification feed; a font size of zero disables it
	Int m_observerNotificationFontSize;
	Bool m_observerNotificationSpecialPowerUsage;
	Bool m_observerNotificationSpecialPowerPurchase;
	Bool m_observerNotificationMilestone;
#endif

	// TheSuperHackers @feature bobtista 28/06/2026 user-configurable speed multiplier for game window transitions
	Real m_gameWindowTransitionSpeedMultiplier;

	Real m_shakeSubtleIntensity;			///< Intensity for shaking a camera with SHAKE_SUBTLE
	Real m_shakeNormalIntensity;			///< Intensity for shaking a camera with SHAKE_NORMAL
	Real m_shakeStrongIntensity;			///< Intensity for shaking a camera with SHAKE_STRONG
	Real m_shakeSevereIntensity;			///< Intensity for shaking a camera with SHAKE_SEVERE
	Real m_shakeCineExtremeIntensity; ///< Intensity for shaking a camera with SHAKE_CINE_EXTREME
	Real m_shakeCineInsaneIntensity;  ///< Intensity for shaking a camera with SHAKE_CINE_INSANE
	Real m_maxShakeIntensity;					///< The maximum shake intensity we can have
	Real m_maxShakeRange;							///< The maximum shake range we can have

	Real m_sellPercentage;						///< when objects are sold, you get this much of the cost it would take to build it back
	Real m_baseRegenHealthPercentPerSecond;	///< auto healing for bases
	UnsignedInt m_baseRegenDelay;			///< delay in frames we must be damage free before we can auto heal

#ifdef ALLOW_SURRENDER
	Real m_prisonBountyMultiplier;		///< the cost of the unit is multiplied by this and given to the player when prisoners are returned to the a prison with KINDOF_COLLECTS_PRISON_BOUNTY
	Color m_prisonBountyTextColor;		///< color of the text that displays the money acquired at the prison
#endif

	Color m_hotKeyTextColor;					///< standard color for all hotkeys.

  //THis is put on ice until later - M Lorenzen
  //	Int m_cheaterHasBeenSpiedIfMyLowestBitIsTrue; ///< says it all.. this lives near other "colors" cause it is masquerading as one

	AsciiString m_specialPowerViewObjectName;	///< Created when certain special powers are fired so players can watch.

	Real m_objectPlacementOpacity; ///< Sets the opacity of build preview objects.
	Bool m_objectPlacementShadows; ///< Enables or disables shadows of build preview objects.

	std::vector<AsciiString> m_standardPublicBones;

	Real m_standardMinefieldDensity;
	Real m_standardMinefieldDistance;


	Bool  m_showMetrics;								///< whether or not to show the metrics.
	Money m_defaultStartingCash;				///< The amount of cash a player starts with by default.

	Bool m_debugShowGraphicalFramerate;		///< Whether or not to show the graphical framerate bar.

	Int m_powerBarBase;										///< Logarithmic base for the power bar scale
	Real m_powerBarIntervals;							///< how many logarithmic intervals the width will be divided into
	Int m_powerBarYellowRange;						///< Red if consumption exceeds production, yellow if consumption this close but under, green if further under
	Real m_displayGamma;									///<display gamma that's adjusted with "brightness" control on options screen.

	UnsignedInt m_unlookPersistDuration;	///< How long after unlook until the sighting info executes the undo

	Bool m_shouldUpdateTGAToDDS;					///< Should we attempt to update old TGAs to DDS stuff on loadup?

	UnsignedInt m_doubleClickTimeMS;	///< What is the maximum amount of time that can separate two clicks in order
																		///< for us to generate a double click message?

	RGBColor m_shroudColor;						///< What color should the shroud be?  Remember, this is a lighting multiply, not an add
	UnsignedByte m_clearAlpha;				///< 255 means perfect visibility
	UnsignedByte m_fogAlpha;					///< 127 means fog is half as obscuring as shroud
	UnsignedByte m_shroudAlpha;				///< 0 makes this opaque, but they may get fancy

	// network timing values.
	UnsignedInt m_networkFPSHistoryLength;		      	///< The number of fps history entries
	UnsignedInt m_networkLatencyHistoryLength;      	///< The number of ping history entries.
	UnsignedInt m_networkCushionHistoryLength;      	///< The number of cushion values to keep.
	UnsignedInt m_networkRunAheadMetricsTime;	      	///< The number of miliseconds between run ahead metrics things
	UnsignedInt m_networkKeepAliveDelay;			      	///< The number of seconds between when the connections to each player send a keep-alive packet.
	UnsignedInt m_networkRunAheadSlack;				      	///< The amount of slack in the run ahead value. This is the percentage of the calculated run ahead that is added.
	UnsignedInt m_networkDisconnectTime;			      	///< The number of milliseconds between when the game gets stuck on a frame for a network stall and when the disconnect dialog comes up.
	UnsignedInt m_networkPlayerTimeoutTime;		      	///< The number of milliseconds between when a player's last keep alive command was received and when they are considered disconnected from the game.
	UnsignedInt	m_networkDisconnectScreenNotifyTime;  ///< The number of milliseconds between when the disconnect screen comes up and when the other players are notified that we are on the disconnect screen.

	Real				m_keyboardCameraRotateSpeed;    ///< How fast the camera rotates when rotated via keyboard controls.
  Int					m_playStats;									///< Int whether we want to log play stats or not, if <= 0 then we don't log

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	Bool m_specialPowerUsesDelay ;
#endif
  Bool m_TiVOFastMode;            ///< When true, the client speeds up the framerate... set by HOTKEY!
  Bool m_queueReorder;            ///< Ctrl+click moves a build queue entry one position earlier; off unless GameData enables it
  Bool m_noOccupantFriendlyFire;  ///< spares the container a passenger is riding in from its own splash; off unless GameData enables it
  Bool m_batchParticles;          ///< draws same-looking particle systems in one batch; off unless GameData enables it
  Bool m_particleAtlas;           ///< packs particle textures into shared premultiplied pages so mixed systems batch together; off unless GameData enables it
  Bool m_skipTranslucencySort;    ///< skips the per-triangle translucency sorter; off unless GameData or the LOD level enables it
  Bool m_backToFront;             ///< with the sorter off, draws whole particle systems far to near; off unless GameData enables it
  Bool m_useBloom;                ///< Options.ini Bloom: glow around additive particles
  Real m_bloomStrength;           ///< Options.ini BloomStrength: glow brightness, 0 to 1
  Real m_bloomThreshold;          ///< Options.ini BloomThreshold: minimum source brightness, 0 to 1
  Real m_bloomSize;               ///< Options.ini BloomSize: blur radius multiplier, 0.25 to 4
  Bool m_bloomDebug;              ///< Options.ini BloomDebug: show the glow buffer instead of the scene
  Bool m_laserRef;                ///< Options.ini LaserRef: lasers light the ground along the beam
  Bool m_useShadowMap;            ///< Options.ini ShadowMap: shadows come from a sun shadow map where the hardware allows
  Int m_shadowMapResolution;      ///< Options.ini ShadowMapResolution: the shadow map's width and height in texels
  Bool m_useSpecular;             ///< Options.ini Specular: per-pixel sun highlight on vehicles and structures, and the sun's glint on the ground, where the hardware allows
  Bool m_useNormalMaps;           ///< Options.ini NormalMaps: bump detail in the sun's light on vehicles, structures and terrain where the hardware allows
  Bool m_waterReflections;        ///< Options.ini WaterReflections: smooth water mirrors the terrain, units and buildings
  Bool m_shorelineFoam;           ///< Options.ini ShorelineFoam: smooth water gathers a band of surf where it meets the shore
  Bool m_useSoftParticles;        ///< Options.ini SoftParticles: smoke and fire fade where they meet the ground and buildings
  Bool m_useFlameShaders;         ///< Options.ini FlameShaders: flame weapon fire flickers and glows white-hot at its core
  Bool m_useElectricShaders;      ///< Options.ini ElectricShaders: electric sparks and flares crackle with arcs
  Bool m_useLaserShaders;         ///< Options.ini LaserShaders: laser beams get a white-hot core and pulses running along them
  Bool m_useFluidShaders;         ///< Options.ini FluidShaders enables fluid particle shading
  Bool m_useCryoShaders;          ///< Options.ini CryoShaders: cryo beams, streaks and sprites turn to ice
  Bool m_useDynamicLights;        ///< Options.ini DynamicLights: explosions, muzzle flashes and lasers light their surroundings
  Bool m_usePixelLights;          ///< Options.ini PixelLights: those lights are drawn per pixel where the hardware allows
  Bool m_usePathTracing;          ///< Options.ini PathTracing enables light from particles and emissive meshes
  Bool m_pathTracingFast;         ///< Options.ini PathTracing = Fast adds those lights in one screen-space draw, softer than the per-surface passes
  Bool m_useAmbientOcclusion;     ///< Options.ini AmbientOcclusion: creases and the ground beneath objects darken where the hardware allows
  Bool m_useHeightBlend;          ///< Options.ini HeightBlend: terrain textures blend by height where the hardware allows
  Bool m_useHQSky;                ///< Options.ini HQSky: cloud shadows drift softly and change shape where the hardware allows
  Int m_vsync;                    ///< Options.ini VSync: 1 on, 0 off, -1 on in fullscreen and off in a window
  Bool m_lowLatency;              ///< Options.ini LowLatency: at most one frame queued ahead of the GPU
  Bool m_smoothUnitMotion;        ///< Options.ini SmoothUnitMotion: models draw between logic frames above the logic rate
  Bool m_specularDebug;           ///< Options.ini SpecularDebug: show what the specular pass covers and its highlight in magenta
  Bool m_normalMapDebug;          ///< Options.ini NormalMapDebug: show only the terrain's bump shading, on grey
  Bool m_ambientOcclusionDebug;   ///< Options.ini AmbientOcclusionDebug: show only the occlusion, in grey
  Bool m_dynamicLightsDebug;      ///< Options.ini DynamicLightsDebug: show only the per-pixel dynamic lights, on black
  Color m_laserGlowColor;         ///< GameData LaserGroundGlowColor: black takes the beam color
  Real m_laserGlowIntensity;      ///< GameData LaserGroundGlowIntensity: how strongly the color is added
  Real m_laserGlowRadius;         ///< GameData LaserGroundGlowRadius: how far the light reaches from the beam, 0 derives it from the beam width
  Real m_laserGlowFalloff;        ///< GameData LaserGroundGlowFalloff: power the light fades by with distance, Direct3D 9 only
  Real m_laserGlowWrap;           ///< GameData LaserGroundGlowWrap: how much ground facing away from the beam still lights, Direct3D 9 only
  Bool m_laserGlowDebug;          ///< GameData LaserGroundGlowDebug: the glow darkens the ground instead of lighting it, Direct3D 9 only
  Bool m_laserGlowOverlap;        ///< GameData LaserGroundGlowOverlap: overlapping glows combine in one pass instead of compounding, Direct3D 9 only

#if defined(RTS_DEBUG) || ENABLE_CONFIGURABLE_SHROUD
	Bool m_shroudOn;
#endif

#if defined(RTS_DEBUG)
	Bool m_wireframe;
	Bool m_stateMachineDebug;
	Bool m_useCameraConstraints;
	Bool m_fogOfWarOn;
	Bool m_jabberOn;
	Bool m_munkeeOn;
	Bool m_allowUnselectableSelection;			///< Are we allowed to select things that are unselectable?
	Bool m_disableCameraFade;								///< if true, script commands affecting camera are disabled
	Bool m_disableScriptedInputDisabling;		///< if true, script commands can't disable input
	Bool m_disableMilitaryCaption;					///< if true, military briefings go fast
	Int m_benchmarkTimer;										///< how long to play the game in benchmark mode?
  Bool m_checkForLeaks;
	Bool m_vTune;
	Bool m_debugCamera;						///< Used to display Camera debug information
	Bool m_debugVisibility;						///< Should we actively debug the visibility
	Int m_debugVisibilityTileCount;		///< How many tiles we should show when debugging visibility
	Real m_debugVisibilityTileWidth;	///< How wide should these tiles be?
	Int m_debugVisibilityTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugThreatMap;						///< Should we actively debug the threat map
	UnsignedInt m_maxDebugThreat;			///< This value (and any values greater) will appear full RED.
	Int m_debugThreatMapTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugCashValueMap;					///< Should we actively debug the threat map
	UnsignedInt m_maxDebugValue;			///< This value (and any values greater) will appear full GREEN.
	Int m_debugCashValueMapTileDuration;	///< How long should these tiles stay around, in frames?
	RGBColor m_debugVisibilityTargettableColor;	///< What color should the targettable cells be?
	RGBColor m_debugVisibilityDeshroudColor;			///< What color should the deshrouding cells be?
	RGBColor m_debugVisibilityGapColor;					///< What color should the gap generator cells be?
	Bool m_debugProjectilePath;						///< Should we actively debug the bezier paths on projectiles
	Real m_debugProjectileTileWidth;			///< How wide should these tiles be?
	Int m_debugProjectileTileDuration;		///< How long should these tiles stay around, in frames?
	RGBColor m_debugProjectileTileColor;	///< What color should these tiles be?
	Bool m_showCollisionExtents;	///< Used to display collision extents
  Bool m_showAudioLocations;    ///< Used to display audio markers and ambient sound radii
	Bool m_saveStats;
	Bool m_saveAllStats;
	Bool m_useLocalMOTD;
	AsciiString m_baseStatsDir;
	AsciiString m_MOTDPath;
	Int m_latencyAverage;					///< Average latency to insert
	Int m_latencyAmplitude;				///< Amplitude of sinusoidal modulation of latency
	Int m_latencyPeriod;					///< Period of sinusoidal modulation of latency
	Int m_latencyNoise;						///< Max amplitude of jitter to throw in
	Int m_packetLoss;							///< Percent of packets to drop
	Bool m_extraLogging;					///< More expensive debug logging to catch crashes.
#endif

#ifdef DEBUG_CRASHING
	Bool m_debugIgnoreAsserts;						///< Ignore all asserts.
#endif

#ifdef DEBUG_STACKTRACE
	Bool m_debugIgnoreStackTrace;					///< No stacktraces for asserts.
#endif

	Bool				m_isBreakableMovie;							///< if we enter a breakable movie, set this flag
	Bool				m_breakTheMovie;								///< The user has hit escape!
	AsciiString m_modDir;
	AsciiString m_modBIG;

	//-allAdvice feature
	//Bool m_allAdvice;

	DrawableColorTint	m_colorTintTypes[TINT_STATUS_COUNT];
	Bool	m_colorTintTypes2; // [TINT_STATUS_COUNT] ;

	AsciiString m_jammingOverlayTexture;	///< empty disables the jamming overlay entirely
	Real m_jammingOverlayScrollU;
	Real m_jammingOverlayScrollV;
	Real m_jammingOverlayScale;				///< UV tiling; >1 repeats the texture more densely
	RGBColor m_jammingOverlayColor;			///< tint multiplied into the texture
	Bool m_jammingOverlayAdditive;

	AsciiString m_frozenOverlayTexture;	///< empty disables the frozen overlay entirely
	Real m_frozenOverlayScrollU;
	Real m_frozenOverlayScrollV;
	Real m_frozenOverlayScale;
	RGBColor m_frozenOverlayColor;
	Bool m_frozenOverlayAdditive;

	Bool m_useOldMoveSpeed;

	Real m_chronoDamageDisableThreshold;
	UnsignedInt m_chronoDamageHealRate;
	Real m_chronoDamageHealAmount;

	std::vector<SubdualDamageDefaults> m_subdualDamageDefaults;	///< later blocks win over earlier ones

	/// the last block matching the template's KindOf that sets the given field, or nullptr
	const SubdualValue* findSubdualDefault( const ThingTemplate* tmpl, SubdualValue SubdualDamageDefaults::*field ) const;

	Real m_chronoDisableAlphaStart;
	Real m_chronoDisableAlphaEnd;

	// TintStatus m_chronoTintStatusType;
	AsciiString m_chronoDisableParticleSystemLarge;
	AsciiString m_chronoDisableParticleSystemMedium;
	AsciiString m_chronoDisableParticleSystemSmall;

	//AudioEventRTS m_chronoDisableSoundLoop;

	DeathTypeFlags m_defaultExcludedDeathTypes;
	Bool m_heightAboveTerrainIncludesWater;
	Bool m_radiusDecalsAboveWater;	///< if true, radius decals (cursors) render over water instead of under it
	Bool m_hideScorchmarksAboveGround;
	Bool m_weaponScatterOnWaterSurfaceDefault;	///< default for WeaponTemplate ScatterOnWaterSurface when not set per-weapon
	Bool m_reverseMoveIgnoreAngleThreshold;	///< if true, a manual REVERSE_MOVE order reverses regardless of heading; if false, only when the goal is behind us
	Real m_smartGarrisonRange;	///< radius searched for additional transports by the Smart Garrison command
	Real m_transportLoadSpeedPenalty;	///< default fraction of speed a container loses at a full load
	Real m_transportLoadTurnRatePenalty;	///< likewise for turn rate
	Real m_transportLoadAccelerationPenalty;	///< likewise for acceleration
	Real m_transportLoadLiftPenalty;	///< likewise for lift
	KindOfMaskType m_transportLoadPenaltyKindOf;	///< default: only occupants with one of these kind of bits count toward the load
	KindOfMaskType m_transportLoadPenaltyForbidKindOf;	///< default: occupants with any of these kind of bits do not count toward the load

	// the trailing '\' is included!
  const AsciiString &getPath_UserData() const { return m_userDataDir; }

private:

	static UnsignedInt generateExeCRC();

	static const FieldParse s_GlobalDataFieldParseTable[];

	// this is private, since we read the info from Windows and cache it for
	// future use. No one is allowed to change it, ever. (srj)
	AsciiString m_userDataDir;
	AsciiString BuildUserDataPathFromRegistry();

	static GlobalData *m_theOriginal;		///< the original global data instance (no overrides)
	GlobalData *m_next;									///< next instance (for overrides)
	virtual GlobalData *newOverride();		/** create a new override, copy data from previous
																			override, and return it */

#if defined(_MSC_VER) && _MSC_VER < 1300
	GlobalData(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); }
	GlobalData& operator=(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); return *this; }
#else
	GlobalData(const GlobalData& that) = delete;
	GlobalData& operator=(const GlobalData& that) = default;
#endif

	static void setColorTintEntry(DrawableColorTint* arr, int index, RGBColor color, RGBColor colorInfantry, UnsignedInt attackFrames, UnsignedInt decayFrames);
	static void parseTintStatusType(INI* ini, void* instance, void* store, const void* userData);
	static void parseSubdualDamageDefaults(INI* ini, void* instance, void* store, const void* userData);

};

// singleton
extern GlobalData* TheWritableGlobalData;

// use TheGlobalData for all read-only accesses
#if __cplusplus >= 201703L
inline const GlobalData* const& TheGlobalData = TheWritableGlobalData;
#else
#define TheGlobalData ((const GlobalData*)TheWritableGlobalData)
#endif
