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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: OptionPreferences.h
// Author: Matthew D. Campbell, April 2002
// Description: Saving/Loading of option preferences
///////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "WW3D2/ww3d.h"
#include "WW3D2/texturefilter.h"

#include "Common/UserPreferences.h"
#include "GameClient/Color.h"

typedef UnsignedInt CursorCaptureMode;
typedef UnsignedInt ScreenEdgeScrollMode;

// TheSuperHackers @feature How targeted commands (guard, attack move, abilities) are triggered.
// Only affects hotkey activation -- a mouse click on a cameo leaves the cursor over the control
// bar, where there is no world position to cast at, so it always uses the normal two step flow.
enum CastMode CPP_11(: Int)
{
	CastMode_Normal = 0,				///< click the button, then click the world (retail behavior)
	CastMode_QuickCast,					///< hotkey fires immediately at the cursor
	CastMode_QuickCastWithIndicator,	///< as above, but flash the targeting decal where it fired

	CastMode_Count,
	CastMode_Default = CastMode_Normal
};

// TheSuperHackers @feature How remaining time is shown on build queue and cooldown cameos.
// Purely a client side display preference.
enum BuildTimerDisplayMode CPP_11(: Int)
{
	BuildTimerDisplayMode_None = 0,		///< no numbers, just the existing clock sweep (retail behavior)
	BuildTimerDisplayMode_Seconds,		///< always plain seconds, however large
	BuildTimerDisplayMode_Auto,				///< seconds under a minute, M:SS above it

	BuildTimerDisplayMode_Count,
	BuildTimerDisplayMode_Default = BuildTimerDisplayMode_None
};

// Which orders draw a line from each selected unit to its destination.
enum ActionLineMode CPP_11(: Int)
{
	ActionLineMode_Off = 0,
	ActionLineMode_All,
	ActionLineMode_AttackOnly,			///< attack and attack move

	ActionLineMode_Count,
	ActionLineMode_Default = ActionLineMode_Off
};

// TheSuperHackers @feature When health bars are shown above objects. Purely a client side
// display preference -- it never affects game logic, so it is safe in multiplayer and replays.
// TheSuperHackers @feature How big the NewRadar object blips draw. Small is the size the feature
// shipped with; Large is easier on the eye but merges sooner when units are packed together.
enum RadarBlipSize CPP_11(: Int)
{
	RadarBlipSize_Small = 0,	///< 3 pixel units, 5 pixel structures
	RadarBlipSize_Large,			///< 5 pixel units, 7 pixel structures

	RadarBlipSize_Count,
	RadarBlipSize_Default = RadarBlipSize_Large
};

enum HealthBarDisplayMode CPP_11(: Int)
{
	HealthBarDisplayMode_Classic = 0,	///< selected and moused over objects only (retail behavior)
	HealthBarDisplayMode_Damaged,			///< the above, plus anything that is not at full health
	HealthBarDisplayMode_Always,			///< the above, plus every undamaged unit and structure

	HealthBarDisplayMode_Count,
	HealthBarDisplayMode_Default = HealthBarDisplayMode_Classic
};

enum AlliedDecalMode CPP_11(: Int)
{
	AlliedDecalMode_Hidden = 0,	///< only your own power decals are drawn (retail behavior)
	AlliedDecalMode_HouseColor,	///< allied power decals drawn in the ally's player color
	AlliedDecalMode_ArmyColor,	///< allied power decals drawn in the ally's faction color

	AlliedDecalMode_Count,
	AlliedDecalMode_Default = AlliedDecalMode_HouseColor
};

// TheSuperHackers @feature Options.ini: HouseColorMode = Default | Basic
enum HouseColorMode CPP_11(: Int)
{
	HouseColorMode_PlayerColors = 0,	///< every player in their own color (retail behavior)
	HouseColorMode_Basic,						///< blue for you, yellow for allies, red for enemies

	HouseColorMode_Count,
	HouseColorMode_Default = HouseColorMode_PlayerColors
};

#define GRID_HOTKEY_LAYOUT_DEFAULT "QWERTYUIOASDFGHJKL"

//-----------------------------------------------------------------------------
// OptionsPreferences options menu class
//-----------------------------------------------------------------------------
class OptionPreferences : public UserPreferences
{
public:
	OptionPreferences();
	virtual ~OptionPreferences() override;

	enum AntiAliasingMode CPP_11(: Int)
	{
		AntiAliasingMode_OFF = 0,
		AntiAliasingMode_MSAA_2X,
		AntiAliasingMode_MSAA_4X,
		AntiAliasingMode_MSAA_8X,
		AntiAliasingMode_Count
	};

	Bool loadFromIniFile();

	WW3D::MultiSampleModeEnum getAntiAliasing() const;
	// Options.ini: SuperSampling = 150 or 200 renders the world that percent of the screen's size each way, 100 for off
	Int getSuperSampling() const;
	TextureFilterClass::TextureFilterMode getTextureFilterMode() const;
	TextureFilterClass::AnisotropicFilterMode getTextureAnisotropyLevel() const;
	UnsignedInt getLANIPAddress();
	UnsignedInt getOnlineIPAddress();
	void setLANIPAddress(AsciiString IP);
	void setOnlineIPAddress(AsciiString IP);
	void setLANIPAddress(UnsignedInt IP);
	void setOnlineIPAddress(UnsignedInt IP);
	Bool getArchiveReplaysEnabled() const;
	Bool getAlternateMouseModeEnabled();
	Bool getRightMouseScrollWithAlternateMouseEnabled() const;
	Bool getRetaliationModeEnabled();
	HealthBarDisplayMode getHealthBarDisplayMode() const;
	AlliedDecalMode getAlliedDecalMode() const;
	HouseColorMode getHouseColorMode() const;
	BuildTimerDisplayMode getBuildTimerDisplayMode() const;
	ActionLineMode getActionLineMode() const;
	CastMode getCastMode() const;
	Bool getSelectionCircleEnabled() const;
	// Options.ini: DefensesRangeCircle = Yes rings the attack range of an armed structure being placed
	Bool getDefensesRangeCircleEnabled() const;
	// Options.ini: QueueBar = Yes shows a button each for idle infantry, vehicles, aircraft and support units
	Bool getQueueBarEnabled() const;
	Bool getObjectDecalsEnabled() const;
	// Options.ini: Bloom = Yes adds a glow around additive particles, BloomStrength (0..1) sets how bright
	Bool getBloomEnabled() const;
	Real getBloomStrength() const;
#if defined(RTS_ZEROHOUR)
	Real getBloomThreshold() const;
	Real getBloomSize() const;
#endif
	Bool getBloomDebugEnabled() const;
	// Options.ini: LaserRef = Yes lights the ground along each laser beam
	Bool getLaserRefEnabled() const;
	// Options.ini: ShadowMap = Yes draws shadows from a sun shadow map instead of volumes and blobs
	Bool getShadowMapEnabled() const;
	// Options.ini: ShadowMapResolution = 4096 sets the shadow map's width and height, 512 to 4096
	Int getShadowMapResolution() const;
	// Options.ini: Specular = Yes adds a per-pixel sun highlight to vehicles and structures
	Bool getSpecularEnabled() const;
	// Options.ini: NormalMaps = Yes adds bump detail to the sun's light on vehicles, structures and terrain
	Bool getNormalMapsEnabled() const;
	// Options.ini: WaterReflections = Yes lets smooth water mirror the terrain, units and buildings
	Bool getWaterReflectionsEnabled() const;
	// Options.ini: ShorelineFoam = Yes edges smooth water with a band of surf where it meets the shore
	Bool getShorelineFoamEnabled() const;
	// Options.ini: SoftParticles = Yes fades smoke and fire where they meet the ground and buildings
	Bool getSoftParticlesEnabled() const;
	// Options.ini: FlameShaders = Yes makes flame weapon fire flicker and glow white-hot at its core
	Bool getFlameShadersEnabled() const;
	// Options.ini: ElectricShaders = Yes makes electric sparks and flares crackle with arcs
	Bool getElectricShadersEnabled() const;
	// Options.ini: LaserShaders = Yes gives laser beams a white-hot core and pulses running along them
	Bool getLaserShadersEnabled() const;
	// Options.ini: CryoShaders = Yes turns cryo beams, streaks and sprites to ice
	Bool getCryoShadersEnabled() const;
#if RTS_ZEROHOUR
	Bool getFluidShadersEnabled() const;
#endif
	// Options.ini: DynamicLights = Yes lets explosions, muzzle flashes and lasers light their surroundings
	Bool getDynamicLightsEnabled() const;
	// Options.ini: PixelLights = Yes draws those lights per pixel where the hardware allows
	Bool getPixelLightsEnabled() const;
	Bool getPathTracingEnabled() const;
	Int getPathTracingMode() const;	///< 0 off, 1 quality, 2 fast
	// Options.ini: AmbientOcclusion = Yes darkens creases and the ground where objects meet it
	Bool getAmbientOcclusionEnabled() const;
	// Options.ini: HeightBlend = Yes blends terrain textures by height where the hardware allows
	Bool getHeightBlendEnabled() const;
	// Options.ini: HQSky = Yes draws cloud shadows that drift softly and change shape where the hardware allows
	Bool getHQSkyEnabled() const;
	// Options.ini: VSync = Yes or No; without it, -1 keeps vsync on in fullscreen and off in a window
	Int getVSyncMode() const;
	// Options.ini: LowLatency = Yes keeps at most one frame queued ahead of the GPU
	Bool getLowLatencyEnabled() const;
	// Options.ini: SmoothUnitMotion = Yes draws models between logic frames when the frame rate is higher
	Bool getSmoothUnitMotionEnabled() const;
	// Options.ini: SpecularDebug = Yes tints what the specular pass covers and shows its highlight 8x in magenta
	Bool getSpecularDebugEnabled() const;
	// Options.ini: NormalMapDebug = Yes shows only the terrain's bump shading, on grey
	Bool getNormalMapDebugEnabled() const;
	// Options.ini: AmbientOcclusionDebug = Yes shows the occlusion alone, in grey
	Bool getAmbientOcclusionDebugEnabled() const;
	// Options.ini: DynamicLightsDebug = Yes shows only the per-pixel dynamic lights, on black
	Bool getDynamicLightsDebugEnabled() const;
	Bool getBorderlessWindowEnabled() const;
	// Options.ini: Windowed = Yes runs in a framed window, as -win does
	Bool getWindowedEnabled() const;
	Bool getEasyMilitaryDragEnabled() const;
	Bool getSmartPipsEnabled() const;
	Bool getNumericalHealthEnabled() const;
	Bool getNewRadarEnabled() const;
	RadarBlipSize getRadarBlipSize() const;
	// TheSuperHackers @feature How long a particle name lingers in the debug overlay after its
	// system is gone, in milliseconds. 0 (and an absent key) keeps the original behaviour, where a
	// name shows only while its system is alive.
	Int getParticleNameLingerMS() const;
	Bool getGridHotkeysEnabled() const;
	AsciiString getGridHotkeyLayout() const;
	Bool getGridHotkeyUnitOrders() const;
	Bool getKeyboardOverlayEnabled() const;
	Color getKeyboardOverlayColor() const;
	Bool getKeyboardOverlayBackdropEnabled() const;
	Color getKeyboardOverlayBackdropColor() const;
	Bool getDoubleClickAttackMoveEnabled();
#if RTS_ZEROHOUR
	Bool getFacingDragEnabled() const;		///< UseFacingDrag (Options "Drag to Face"): missing - on
#endif
	Int getJpegQuality() const;
	Real getScrollFactor();

	// Options.ini: UseCustomMaxCameraHeight = Yes lets MaxCameraHeight (210..1000) replace GameData's limit
	enum { MaxCameraHeightMin = 210, MaxCameraHeightMax = 1000 };
	Bool getUseCustomMaxCameraHeight() const;
	Real getMaxCameraHeight() const;
	Bool getDrawScrollAnchor();
	Bool getMoveScrollAnchor();
	Bool getCursorCaptureEnabledInWindowedGame() const;
	Bool getCursorCaptureEnabledInWindowedMenu() const;
	Bool getCursorCaptureEnabledInFullscreenGame() const;
	Bool getCursorCaptureEnabledInFullscreenMenu() const;
	CursorCaptureMode getCursorCaptureMode() const;
	Bool getScreenEdgeScrollEnabledInWindowedApp() const;
	Bool getScreenEdgeScrollEnabledInFullscreenApp() const;
	ScreenEdgeScrollMode getScreenEdgeScrollMode() const;
	Int getFirewallBehavior();
	Short getFirewallPortAllocationDelta();
	UnsignedShort getFirewallPortOverride();
	Bool getFirewallNeedToRefresh();
	Bool usesSystemMapDir();
	AsciiString getPreferred3DProvider();
	AsciiString getSpeakerType();
	Real getSoundVolume();
	Real get3DSoundVolume();
	Real getSpeechVolume();
	Real getMusicVolume();
	Real getMoneyTransactionVolume() const;
	Bool saveCameraInReplays();
	Bool useCameraInReplays();
	Bool getPlayerObserverEnabled() const;
	Int getStaticGameDetail();
	Int getIdealStaticGameDetail();
	Real getGammaValue();
	Int getTextureReduction();
	void getResolution(Int *xres, Int *yres);
	Bool get3DShadowsEnabled();
	Bool get2DShadowsEnabled();
	Bool getCloudShadowsEnabled();
	Bool getLightmapEnabled();
	Bool getSmoothWaterEnabled();
	Bool getTreesEnabled();
	Bool getExtraAnimationsDisabled();
	Bool getUseHeatEffects();
	Bool getDynamicLODEnabled();
	Bool getFPSLimitEnabled();
#if RTS_ZEROHOUR
	Int getRenderFPSLimit() const;
#endif
	Bool getBuildingOcclusionEnabled();
	Int getParticleCap();

	Int getCampaignDifficulty();
	void setCampaignDifficulty(Int diff);

	Int getNetworkLatencyFontSize();
	Int getRenderFpsFontSize();
	Int getSystemTimeFontSize();
	Int getGameTimeFontSize();
	Int getPlayerInfoListFontSize();

	Real getResolutionFontAdjustment();

	Bool getShowMoneyPerMinute() const;
#if defined(GENERALS_ONLINE)
	Int getObserverNotificationFontSize() const;
	Bool getObserverNotificationSpecialPowerUsage() const;
	Bool getObserverNotificationSpecialPowerPurchase() const;
	Bool getObserverNotificationMilestone() const;
#endif

	Bool getSmartSelectionEnabled() const;
	Bool getSmartSelectionUseMouse() const;
	Bool getSmartCommandGroupEnabled() const;

	Real getGameWindowTransitionSpeedMultiplier() const;

private:
	// TheSuperHackers @feature Read one 0-255 colour channel, clamped, with a fallback.
	UnsignedByte getColorChannel(const char *keyName, UnsignedByte defaultValue) const;
};
