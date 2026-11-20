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
// FILE: OptionPreferences.cpp
// Author: Matthew D. Campbell, April 2002
// Description: Saving/Loading of option preferences
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"

#include "GameClient/ClientInstance.h"
#include "GameClient/Display.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/Mouse.h"

#include "GameLogic/ScriptEngine.h"

#include "GameNetwork/IPEnumeration.h"

OptionPreferences::OptionPreferences()
{
	loadFromIniFile();
}

OptionPreferences::~OptionPreferences()
{
}

Bool OptionPreferences::loadFromIniFile()
{
	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		AsciiString fname;
		fname.format("Options_Instance%.2u.ini", rts::ClientInstance::getInstanceId());
		return load(fname);
	}

	return load("Options.ini");
}

WW3D::MultiSampleModeEnum OptionPreferences::getAntiAliasing() const
{
	OptionPreferences::const_iterator it = find("AntiAliasing");
	if (it == end())
		return WW3D::MULTISAMPLE_MODE_NONE;

	WW3D::MultiSampleModeEnum level = (WW3D::MultiSampleModeEnum)atoi(it->second.str());
	level = clamp(WW3D::MULTISAMPLE_MODE_NONE, level, WW3D::MULTISAMPLE_MODE_8X);
	level = highestBit(level);

	return level;
}

Int OptionPreferences::getSuperSampling() const
{
	const Int percent = getInt("SuperSampling", 100);
	if (percent >= 200)
	{
		return 200;
	}
	if (percent >= 150)
	{
		return 150;
	}
	return 100;
}

TextureFilterClass::TextureFilterMode OptionPreferences::getTextureFilterMode() const
{
	OptionPreferences::const_iterator it = find("TextureFilter");
	if (it == end())
		return TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC;

	return TextureFilterClass::getTextureFilterMode(it->second.str());
}

TextureFilterClass::AnisotropicFilterMode OptionPreferences::getTextureAnisotropyLevel() const
{
	OptionPreferences::const_iterator it = find("AnisotropyLevel");
	if (it == end())
		return TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_8X;

	TextureFilterClass::AnisotropicFilterMode level = (TextureFilterClass::AnisotropicFilterMode)atoi(it->second.str());
	level = clamp(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_2X, level, TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_16X);
	level = highestBit(level);

	return level;
}

Int OptionPreferences::getCampaignDifficulty()
{
	OptionPreferences::const_iterator it = find("CampaignDifficulty");
	if (it == end())
		return TheScriptEngine->getGlobalDifficulty();

	Int factor = atoi(it->second.str());
	if (factor < DIFFICULTY_EASY)
		factor = DIFFICULTY_EASY;
	if (factor > DIFFICULTY_HARD)
		factor = DIFFICULTY_HARD;

	return factor;
}

void OptionPreferences::setCampaignDifficulty(Int diff)
{
	AsciiString prefString;
	prefString.format("%d", diff);
	(*this)["CampaignDifficulty"] = prefString;
}

UnsignedInt OptionPreferences::getLANIPAddress()
{
	AsciiString selectedIP = (*this)["IPAddress"];
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	while (IPlist)
	{
		if (selectedIP.compareNoCase(IPlist->getIPstring()) == 0)
		{
			return IPlist->getIP();
		}
		IPlist = IPlist->getNext();
	}
	return TheGlobalData->m_defaultIP;
}

void OptionPreferences::setLANIPAddress(AsciiString IP)
{
	(*this)["IPAddress"] = IP;
}

void OptionPreferences::setLANIPAddress(UnsignedInt IP)
{
	AsciiString tmp;
	tmp.format("%d.%d.%d.%d", PRINTF_IP_AS_4_INTS(IP));
	(*this)["IPAddress"] = tmp;
}

UnsignedInt OptionPreferences::getOnlineIPAddress()
{
	AsciiString selectedIP = (*this)["GameSpyIPAddress"];
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	while (IPlist)
	{
		if (selectedIP.compareNoCase(IPlist->getIPstring()) == 0)
		{
			return IPlist->getIP();
		}
		IPlist = IPlist->getNext();
	}
	return TheGlobalData->m_defaultIP;
}

void OptionPreferences::setOnlineIPAddress(AsciiString IP)
{
	(*this)["GameSpyIPAddress"] = IP;
}

void OptionPreferences::setOnlineIPAddress(UnsignedInt IP)
{
	AsciiString tmp;
	tmp.format("%d.%d.%d.%d", PRINTF_IP_AS_4_INTS(IP));
	(*this)["GameSpyIPAddress"] = tmp;
}

Bool OptionPreferences::getArchiveReplaysEnabled() const
{
	OptionPreferences::const_iterator it = find("ArchiveReplays");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getAlternateMouseModeEnabled()
{
	OptionPreferences::const_iterator it = find("UseAlternateMouse");
	if (it == end())
		return TheGlobalData->m_useAlternateMouse;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Health bar display mode, read from Options.ini as
// HealthBarDisplayMode = Classic | Damaged | Always (a plain index also works).
HealthBarDisplayMode OptionPreferences::getHealthBarDisplayMode(void) const
{
	OptionPreferences::const_iterator it = find("HealthBarDisplayMode");
	if (it == end())
		return HealthBarDisplayMode_Default;

	if (stricmp(it->second.str(), "Always") == 0)
		return HealthBarDisplayMode_Always;
	if (stricmp(it->second.str(), "Damaged") == 0)
		return HealthBarDisplayMode_Damaged;
	if (stricmp(it->second.str(), "Classic") == 0)
		return HealthBarDisplayMode_Classic;

	// also accept the raw index, so the value round trips if it is ever written numerically
	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < HealthBarDisplayMode_Count)
		return (HealthBarDisplayMode)mode;

	return HealthBarDisplayMode_Default;
}

// Options.ini: AlliedDecalMode = Hidden | House | Army (a plain index also works).
AlliedDecalMode OptionPreferences::getAlliedDecalMode(void) const
{
	OptionPreferences::const_iterator it = find("AlliedDecalMode");
	if (it == end())
		return AlliedDecalMode_Default;

	if (stricmp(it->second.str(), "Army") == 0)
		return AlliedDecalMode_ArmyColor;
	if (stricmp(it->second.str(), "House") == 0)
		return AlliedDecalMode_HouseColor;
	if (stricmp(it->second.str(), "Hidden") == 0)
		return AlliedDecalMode_Hidden;

	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < AlliedDecalMode_Count)
		return (AlliedDecalMode)mode;

	return AlliedDecalMode_Default;
}

// TheSuperHackers @feature Options.ini: HouseColorMode = Default | Basic (a plain index also works).
HouseColorMode OptionPreferences::getHouseColorMode(void) const
{
	OptionPreferences::const_iterator it = find("HouseColorMode");
	if (it == end())
		return HouseColorMode_Default;

	if (stricmp(it->second.str(), "Basic") == 0)
		return HouseColorMode_Basic;
	if (stricmp(it->second.str(), "Default") == 0)
		return HouseColorMode_PlayerColors;

	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < HouseColorMode_Count)
		return (HouseColorMode)mode;

	return HouseColorMode_Default;
}

// TheSuperHackers @feature Draw each command bar cameo's keyboard hotkey over the cameo.
// Options.ini: KeyboardOverlay = Yes
Bool OptionPreferences::getKeyboardOverlayEnabled(void) const
{
	OptionPreferences::const_iterator it = find("KeyboardOverlay");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getRightMouseScrollWithAlternateMouseEnabled() const
{
	OptionPreferences::const_iterator it = find("UseRightMouseScrollWithAlternateMouse");
	if (it == end())
		return TheGlobalData->m_useRightMouseScrollWithAlternateMouse;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Grid hotkeys assign a command bar key by slot position rather than
// by whatever letter the string file marked with an ampersand.
// Options.ini: GridHotkeys = Yes
Bool OptionPreferences::getGridHotkeysEnabled(void) const
{
	OptionPreferences::const_iterator it = find("GridHotkeys");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Grid keys in reading order, presets in OptionsMenu.cpp. Options.ini: GridHotkeyLayout = QWERTYUIOASDFGHJKL
AsciiString OptionPreferences::getGridHotkeyLayout(void) const
{
	OptionPreferences::const_iterator it = find("GridHotkeyLayout");
	if (it == end() || it->second.isEmpty())
		return AsciiString(GRID_HOTKEY_LAYOUT_DEFAULT);

	return it->second;
}

// TheSuperHackers @feature Unit orders keep their own letter, not a grid key. Options.ini: GridHotkeyUnitOrders = Yes
Bool OptionPreferences::getGridHotkeyUnitOrders(void) const
{
	OptionPreferences::const_iterator it = find("GridHotkeyUnitOrders");
	if (it == end())
		return TRUE;

	return stricmp(it->second.str(), "yes") == 0;
}

// TheSuperHackers @feature Options.ini: SelectionCircle = Yes draws a green hexagon under
// every selected object.
Bool OptionPreferences::getSelectionCircleEnabled(void) const
{
	OptionPreferences::const_iterator it = find("SelectionCircle");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Options.ini: DefensesRangeCircle = Yes rings the attack range of an
// armed structure while it is being placed.
Bool OptionPreferences::getDefensesRangeCircleEnabled(void) const
{
	return getBool("DefensesRangeCircle", FALSE);
}

Bool OptionPreferences::getQueueBarEnabled(void) const
{
	return getBool("QueueBar", FALSE);
}

// TheSuperHackers @feature Options.ini: ObjectDecals = No suppresses the decals objects ask for
// with DisplayDecal. On by default, since the templates opted in.
Bool OptionPreferences::getObjectDecalsEnabled(void) const
{
	// the only option here that defaults on, which is exactly what getBool's default expresses
	return getBool("ObjectDecals", TRUE);
}

Bool OptionPreferences::getBloomEnabled(void) const
{
	return getBool("Bloom", TRUE);
}

Real OptionPreferences::getBloomStrength(void) const
{
	const Real strength = getReal("BloomStrength", 0.5f);
	return clamp(0.0f, strength, 1.0f);
}

#if defined(RTS_ZEROHOUR)
Real OptionPreferences::getBloomThreshold(void) const
{
	return clamp(0.0f, getReal("BloomThreshold", 0.25f), 1.0f);
}

Real OptionPreferences::getBloomSize(void) const
{
	return clamp(0.25f, getReal("BloomSize", 1.0f), 4.0f);
}

#endif

// The debug views are developer aids: a build without cheats never turns one on, whatever Options.ini says
static Bool debugViewEnabled(const UserPreferences &pref, const char *key)
{
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	return pref.getBool(key, FALSE);
#else
	return FALSE;
#endif
}

Bool OptionPreferences::getBloomDebugEnabled(void) const
{
	return debugViewEnabled(*this, "BloomDebug");
}

Bool OptionPreferences::getLaserRefEnabled(void) const
{
	return getBool("LaserRef", TRUE);
}

Bool OptionPreferences::getShadowMapEnabled(void) const
{
	return getBool("ShadowMap", TRUE);
}

// Snapped up to the next size the Options menu offers.
Int OptionPreferences::getShadowMapResolution(void) const
{
	const Int resolution = getInt("ShadowMapResolution", 4096);
	Int size = 512;
	while (size < 4096 && size < resolution)
	{
		size *= 2;
	}
	return size;
}

Bool OptionPreferences::getSpecularEnabled(void) const
{
	return getBool("Specular", TRUE);
}

Bool OptionPreferences::getNormalMapsEnabled(void) const
{
	return getBool("NormalMaps", FALSE);
}

Bool OptionPreferences::getWaterReflectionsEnabled(void) const
{
	return getBool("WaterReflections", TRUE);
}

Bool OptionPreferences::getShorelineFoamEnabled(void) const
{
	return getBool("ShorelineFoam", TRUE);
}

Bool OptionPreferences::getSoftParticlesEnabled(void) const
{
	return getBool("SoftParticles", TRUE);
}

Bool OptionPreferences::getFlameShadersEnabled(void) const
{
	return getBool("FlameShaders", TRUE);
}

Bool OptionPreferences::getElectricShadersEnabled(void) const
{
	return getBool("ElectricShaders", TRUE);
}

Bool OptionPreferences::getLaserShadersEnabled(void) const
{
	return getBool("LaserShaders", TRUE);
}

#if RTS_ZEROHOUR
Bool OptionPreferences::getFluidShadersEnabled(void) const
{
	return getBool("FluidShaders", TRUE);
}
#endif

Bool OptionPreferences::getCryoShadersEnabled(void) const
{
	return getBool("CryoShaders", TRUE);
}

Bool OptionPreferences::getDynamicLightsEnabled(void) const
{
	return getBool("DynamicLights", TRUE);
}

Bool OptionPreferences::getPixelLightsEnabled(void) const
{
	return getBool("PixelLights", TRUE);
}

Bool OptionPreferences::getPathTracingEnabled(void) const
{
	return getPathTracingMode() != 0;
}

// Off, Quality or Fast. The yes and no of older files read as Quality and Off.
Int OptionPreferences::getPathTracingMode(void) const
{
	OptionPreferences::const_iterator it = find("PathTracing");
	if (it == end())
	{
		return 0;
	}
	if (stricmp(it->second.str(), "Fast") == 0)
	{
		return 2;
	}
	if (stricmp(it->second.str(), "Quality") == 0)
	{
		return 1;
	}
	return getBool("PathTracing", FALSE) ? 1 : 0;
}

Bool OptionPreferences::getAmbientOcclusionEnabled(void) const
{
	return getBool("AmbientOcclusion", FALSE);
}

Bool OptionPreferences::getHeightBlendEnabled(void) const
{
	return getBool("HeightBlend", TRUE);
}

Bool OptionPreferences::getHQSkyEnabled(void) const
{
	return getBool("HQSky", TRUE);
}

Int OptionPreferences::getVSyncMode(void) const
{
	if (find("VSync") == end())
	{
		return -1;
	}
	return getBool("VSync", FALSE) ? 1 : 0;
}

Bool OptionPreferences::getLowLatencyEnabled(void) const
{
	return getBool("LowLatency", FALSE);
}

Bool OptionPreferences::getSmoothUnitMotionEnabled(void) const
{
	return getBool("SmoothUnitMotion", TRUE);
}

Bool OptionPreferences::getSpecularDebugEnabled(void) const
{
	return debugViewEnabled(*this, "SpecularDebug");
}

Bool OptionPreferences::getNormalMapDebugEnabled(void) const
{
	return debugViewEnabled(*this, "NormalMapDebug");
}

Bool OptionPreferences::getAmbientOcclusionDebugEnabled(void) const
{
	return debugViewEnabled(*this, "AmbientOcclusionDebug");
}

Bool OptionPreferences::getDynamicLightsDebugEnabled(void) const
{
	return debugViewEnabled(*this, "DynamicLightsDebug");
}

Bool OptionPreferences::getBorderlessWindowEnabled(void) const
{
	return getBool("BorderlessWindow", FALSE);
}

Bool OptionPreferences::getWindowedEnabled(void) const
{
	return getBool("Windowed", FALSE);
}

// TheSuperHackers @feature Options.ini: NumericalHealth = Yes prints the hit points beside the
// health bar. Follows HealthBarDisplayMode, so the number appears exactly where a bar does.
Bool OptionPreferences::getNumericalHealthEnabled(void) const
{
	OptionPreferences::const_iterator it = find("NumericalHealth");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Options.ini: ParticleNameLingerMS = 3000 keeps a particle name in the
// debug overlay for that many milliseconds after its system has gone. Many effects are one shot
// bursts that die within a frame or two, so without this their names flash past unreadably.
//
// Absent or 0 keeps the original behaviour: a name shows only while its system is alive.
Int OptionPreferences::getParticleNameLingerMS(void) const
{
	OptionPreferences::const_iterator it = find("ParticleNameLingerMS");
	if (it == end())
		return 0;	// key absent, so keep the lifetime driven behaviour

	Int ms = atoi(it->second.str());
	if (ms < 0)
		ms = 0;

	return ms;
}

// TheSuperHackers @feature Options.ini: SmartPips = Yes keeps ammo and passenger pips on screen
// instead of showing them only while the unit is selected or moused over. Own units only -- not
// allies, not enemies. Nothing is drawn when there is nothing to report: no shots left, or no
// one aboard. So the pips read as "still loaded" and "carrying someone" at a glance.
Bool OptionPreferences::getSmartPipsEnabled(void) const
{
	OptionPreferences::const_iterator it = find("SmartPips");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Options.ini: NewRadar = Yes outlines the radar blips and the shoreline,
// and doubles the radar resolution so there is room to draw those outlines. Purely presentational.
Bool OptionPreferences::getNewRadarEnabled(void) const
{
	OptionPreferences::const_iterator it = find("NewRadar");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Options.ini: BlipSize = Large sets how big the NewRadar object blips
// draw. Small is the size the feature shipped with, Large is easier to read at a glance but runs
// together sooner when units are packed in. Ignored unless NewRadar is on.
RadarBlipSize OptionPreferences::getRadarBlipSize(void) const
{
	OptionPreferences::const_iterator it = find("BlipSize");
	if (it == end())
		return RadarBlipSize_Default;

	if (stricmp(it->second.str(), "Large") == 0)
		return RadarBlipSize_Large;
	if (stricmp(it->second.str(), "Small") == 0)
		return RadarBlipSize_Small;

	// also accept the raw index, so the value round trips if it is ever written numerically
	Int size = atoi(it->second.str());
	if (size >= 0 && size < RadarBlipSize_Count)
		return (RadarBlipSize)size;

	return RadarBlipSize_Default;
}

// TheSuperHackers @feature Options.ini: EasyMilitaryDrag = Yes leaves builders out of a drag
// selection, so boxing over a base picks up the army without dragging them along. Covers
// KINDOF_DOZER and KINDOF_IGNORES_SELECT_ALL, the same kinds Select All already disqualifies.
Bool OptionPreferences::getEasyMilitaryDragEnabled(void) const
{
	OptionPreferences::const_iterator it = find("EasyMilitaryDrag");
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

// TheSuperHackers @feature Options.ini: SmartSelection = Yes shows a row of cameos above the
// command bar, one per selected unit type with a count, that narrows the selection on click
// and cycles it with Tab. On by default.
Bool OptionPreferences::getSmartSelectionEnabled(void) const
{
	return getBool("SmartSelection", TRUE);
}

// TheSuperHackers @feature Options.ini: SmartSelectionUseMouse = Yes keeps only a cameo's units
// on double click, No on Ctrl+Shift click. On by default.
Bool OptionPreferences::getSmartSelectionUseMouse(void) const
{
	return getBool("SmartSelectionUseMouse", TRUE);
}

// TheSuperHackers @feature Options.ini: SmartCommandGroup = Yes shows a row of cameos for the
// command groups under the smart selection row. On by default.
Bool OptionPreferences::getSmartCommandGroupEnabled(void) const
{
	return getBool("SmartCommandGroup", TRUE);
}

// TheSuperHackers @feature Options.ini: BuildQueueBar = Yes shows the queued units and upgrades as cameos above the radar.
Bool OptionPreferences::getBuildQueueBarEnabled(void) const
{
	return getBool("BuildQueueBar", TRUE);
}

// TheSuperHackers @feature Options.ini: CastMode = Normal | QuickCast | QuickCastWithIndicator
CastMode OptionPreferences::getCastMode(void) const
{
	OptionPreferences::const_iterator it = find("CastMode");
	if (it == end())
		return CastMode_Default;

	if (stricmp(it->second.str(), "QuickCastWithIndicator") == 0)
		return CastMode_QuickCastWithIndicator;
	if (stricmp(it->second.str(), "QuickCast") == 0)
		return CastMode_QuickCast;
	if (stricmp(it->second.str(), "Normal") == 0)
		return CastMode_Normal;

	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < CastMode_Count)
		return (CastMode)mode;

	return CastMode_Default;
}

// TheSuperHackers @feature Countdown numbers on build queue and cooldown cameos, read from
// Options.ini as BuildTimerDisplayMode = None | Seconds | Auto (a plain index also works).
BuildTimerDisplayMode OptionPreferences::getBuildTimerDisplayMode(void) const
{
	OptionPreferences::const_iterator it = find("BuildTimerDisplayMode");
	if (it == end())
		return BuildTimerDisplayMode_Default;

	if (stricmp(it->second.str(), "Auto") == 0)
		return BuildTimerDisplayMode_Auto;
	if (stricmp(it->second.str(), "Seconds") == 0)
		return BuildTimerDisplayMode_Seconds;
	if (stricmp(it->second.str(), "None") == 0)
		return BuildTimerDisplayMode_None;

	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < BuildTimerDisplayMode_Count)
		return (BuildTimerDisplayMode)mode;

	return BuildTimerDisplayMode_Default;
}

// Options.ini as ActionLines = Off | All | AttackOnly (a plain index also works).
ActionLineMode OptionPreferences::getActionLineMode(void) const
{
	OptionPreferences::const_iterator it = find("ActionLines");
	if (it == end())
		return ActionLineMode_Default;

	if (stricmp(it->second.str(), "All") == 0)
		return ActionLineMode_All;
	if (stricmp(it->second.str(), "AttackOnly") == 0)
		return ActionLineMode_AttackOnly;
	if (stricmp(it->second.str(), "Off") == 0)
		return ActionLineMode_Off;

	Int mode = atoi(it->second.str());
	if (mode >= 0 && mode < ActionLineMode_Count)
		return (ActionLineMode)mode;

	return ActionLineMode_Default;
}

UnsignedByte OptionPreferences::getColorChannel(const char *keyName, UnsignedByte defaultValue) const
{
	OptionPreferences::const_iterator it = find(AsciiString(keyName));
	if (it == end())
		return defaultValue;

	Int value = atoi(it->second.str());
	if (value < 0)
		value = 0;
	else if (value > 255)
		value = 255;

	return (UnsignedByte)value;
}

// TheSuperHackers @feature Colour of the command bar hotkey overlay letters.
// Options.ini: KeyboardOverlayRed / KeyboardOverlayGreen / KeyboardOverlayBlue,
// each 0-255. Defaults to white. An out of range or non numeric channel falls
// back to full brightness rather than silently drawing an invisible letter.
Color OptionPreferences::getKeyboardOverlayColor(void) const
{
	return GameMakeColor(
		getColorChannel("KeyboardOverlayRed", 255),
		getColorChannel("KeyboardOverlayGreen", 255),
		getColorChannel("KeyboardOverlayBlue", 255),
		255);
}

// TheSuperHackers @feature Translucent plate drawn behind the hotkey letter.
Bool OptionPreferences::getKeyboardOverlayBackdropEnabled(void) const
{
	OptionPreferences::const_iterator it = find("KeyboardOverlayBackdrop");
	if (it == end())
		return TRUE;	// on by default -- it is what makes the letter readable

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Color OptionPreferences::getKeyboardOverlayBackdropColor(void) const
{
	// black at 50% opacity, matching the darkened plate look
	return GameMakeColor(
		getColorChannel("KeyboardOverlayBackdropRed", 0),
		getColorChannel("KeyboardOverlayBackdropGreen", 0),
		getColorChannel("KeyboardOverlayBackdropBlue", 0),
		getColorChannel("KeyboardOverlayBackdropOpacity", 128));
}

Bool OptionPreferences::getRetaliationModeEnabled()
{
	OptionPreferences::const_iterator it = find("Retaliation");
	if (it == end())
		return TheGlobalData->m_clientRetaliationModeEnabled;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getDoubleClickAttackMoveEnabled()
{
	OptionPreferences::const_iterator it = find("UseDoubleClickAttackMove");
	if( it == end() )
		return TheGlobalData->m_doubleClickAttackMove;

	if( stricmp( it->second.str(), "yes" ) == 0 )
		return TRUE;

	return FALSE;
}

// drag-to-face, on unless the player turned it off
#if RTS_ZEROHOUR
Bool OptionPreferences::getFacingDragEnabled() const
{
	OptionPreferences::const_iterator it = find( "UseFacingDrag" );
	return it == end() || stricmp( it->second.str(), "yes" ) == 0;
}
#endif

Int OptionPreferences::getJpegQuality() const
{
	OptionPreferences::const_iterator it = find("JpegQuality");
	if (it == end())
		return DEFAULT_JPEG_QUALITY;

	// TheSuperHackers @info bobtista 14/07/2026 Clamp the quality to 50-95: above 95 the file
	// size increases significantly with no visible benefit, below 50 the image degrades visibly.
	const Int quality = atoi(it->second.str());
	return clamp(50, quality, 95);
}

Real OptionPreferences::getScrollFactor()
{
	OptionPreferences::const_iterator it = find("ScrollFactor");
	if (it == end())
		return TheGlobalData->m_keyboardDefaultScrollFactor;

	Int factor = atoi(it->second.str());

	// TheSuperHackers @tweak xezon 11/07/2025
	// No longer caps the upper limit to 100, because the options setting can go beyond that.
	// No longer caps the lower limit to 0, because that would mean standstill.
	if (factor < 1)
		factor = 1;

	return factor/100.0f;
}

Bool OptionPreferences::getUseCustomMaxCameraHeight() const
{
	return getBool("UseCustomMaxCameraHeight", FALSE);
}

Real OptionPreferences::getMaxCameraHeight() const
{
	if (!getUseCustomMaxCameraHeight())
	{
		return TheGlobalData->m_defaultMaxCameraHeight;
	}
	const Int height = getInt("MaxCameraHeight", 0);
	if (height <= 0)
	{
		return TheGlobalData->m_defaultMaxCameraHeight;
	}
	return (Real)clamp((Int)MaxCameraHeightMin, height, (Int)MaxCameraHeightMax);
}

Bool OptionPreferences::getDrawScrollAnchor()
{
	OptionPreferences::const_iterator it = find("DrawScrollAnchor");
	// TheSuperHackers @info this default is based on the same variable within InGameUi.ini
	if (it == end())
		return FALSE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getMoveScrollAnchor()
{
	OptionPreferences::const_iterator it = find("MoveScrollAnchor");
	// TheSuperHackers @info this default is based on the same variable within InGameUi.ini
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getCursorCaptureEnabledInWindowedGame() const
{
	OptionPreferences::const_iterator it = find("CursorCaptureEnabledInWindowedGame");
	if (it == end())
		return (CursorCaptureMode_Default & CursorCaptureMode_EnabledInWindowedGame) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

Bool OptionPreferences::getCursorCaptureEnabledInWindowedMenu() const
{
	OptionPreferences::const_iterator it = find("CursorCaptureEnabledInWindowedMenu");
	if (it == end())
		return (CursorCaptureMode_Default & CursorCaptureMode_EnabledInWindowedMenu) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

Bool OptionPreferences::getCursorCaptureEnabledInFullscreenGame() const
{
	OptionPreferences::const_iterator it = find("CursorCaptureEnabledInFullscreenGame");
	if (it == end())
		return (CursorCaptureMode_Default & CursorCaptureMode_EnabledInFullscreenGame) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

Bool OptionPreferences::getCursorCaptureEnabledInFullscreenMenu() const
{
	OptionPreferences::const_iterator it = find("CursorCaptureEnabledInFullscreenMenu");
	if (it == end())
		return (CursorCaptureMode_Default & CursorCaptureMode_EnabledInFullscreenMenu) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

CursorCaptureMode OptionPreferences::getCursorCaptureMode() const
{
	CursorCaptureMode mode = 0;
	mode |= getCursorCaptureEnabledInWindowedGame() ? CursorCaptureMode_EnabledInWindowedGame : 0;
	mode |= getCursorCaptureEnabledInWindowedMenu() ? CursorCaptureMode_EnabledInWindowedMenu : 0;
	mode |= getCursorCaptureEnabledInFullscreenGame() ? CursorCaptureMode_EnabledInFullscreenGame : 0;
	mode |= getCursorCaptureEnabledInFullscreenMenu() ? CursorCaptureMode_EnabledInFullscreenMenu : 0;
	return mode;
}

Bool OptionPreferences::getScreenEdgeScrollEnabledInWindowedApp() const
{
	OptionPreferences::const_iterator it = find("ScreenEdgeScrollEnabledInWindowedApp");
	if (it == end())
		return (ScreenEdgeScrollMode_Default & ScreenEdgeScrollMode_EnabledInWindowedApp) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

Bool OptionPreferences::getScreenEdgeScrollEnabledInFullscreenApp() const
{
	OptionPreferences::const_iterator it = find("ScreenEdgeScrollEnabledInFullscreenApp");
	if (it == end())
		return (ScreenEdgeScrollMode_Default & ScreenEdgeScrollMode_EnabledInFullscreenApp) != 0;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

ScreenEdgeScrollMode OptionPreferences::getScreenEdgeScrollMode() const
{
	ScreenEdgeScrollMode mode = 0;
	mode |= getScreenEdgeScrollEnabledInWindowedApp() ? ScreenEdgeScrollMode_EnabledInWindowedApp : 0;
	mode |= getScreenEdgeScrollEnabledInFullscreenApp() ? ScreenEdgeScrollMode_EnabledInFullscreenApp : 0;
	return mode;
}

Bool OptionPreferences::usesSystemMapDir()
{
	OptionPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::saveCameraInReplays()
{
	OptionPreferences::const_iterator it = find("SaveCameraInReplays");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::useCameraInReplays()
{
	OptionPreferences::const_iterator it = find("UseCameraInReplays");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getPlayerObserverEnabled() const
{
	OptionPreferences::const_iterator it = find("PlayerObserverEnabled");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0)
		return TRUE;

	return FALSE;
}

Int OptionPreferences::getIdealStaticGameDetail()
{
	OptionPreferences::const_iterator it = find("IdealStaticGameLOD");
	if (it == end())
		return STATIC_GAME_LOD_UNKNOWN;

	return TheGameLODManager->getStaticGameLODIndex(it->second);
}

Int OptionPreferences::getStaticGameDetail()
{
	OptionPreferences::const_iterator it = find("StaticGameLOD");
	if (it == end())
		return TheGameLODManager->getStaticLODLevel();

	return TheGameLODManager->getStaticGameLODIndex(it->second);
}

Int OptionPreferences::getFirewallBehavior()
{
	OptionPreferences::const_iterator it = find("FirewallBehavior");
	if (it == end())
		return TheGlobalData->m_firewallBehavior;

	Int behavior = atoi(it->second.str());
	if (behavior < 0)
	{
		behavior = 0;
	}
	return behavior;
}

Short OptionPreferences::getFirewallPortAllocationDelta()
{
	OptionPreferences::const_iterator it = find("FirewallPortAllocationDelta");
	if (it == end()) {
		return TheGlobalData->m_firewallPortAllocationDelta;
	}

	Short delta = atoi(it->second.str());
	return delta;
}

UnsignedShort OptionPreferences::getFirewallPortOverride()
{
	OptionPreferences::const_iterator it = find("FirewallPortOverride");
	if (it == end()) {
		return TheGlobalData->m_firewallPortOverride;
	}

	Int portOverride = atoi(it->second.str());
	if (portOverride < 0 || portOverride > 65535)
		portOverride = 0;
	return portOverride;
}

Bool OptionPreferences::getFirewallNeedToRefresh()
{
	OptionPreferences::const_iterator it = find("FirewallNeedToRefresh");
	if (it == end()) {
		return FALSE;
	}

	Bool retval = FALSE;
	AsciiString str = it->second;
	if (str.compareNoCase("TRUE") == 0) {
		retval = TRUE;
	}
	return retval;
}

AsciiString OptionPreferences::getPreferred3DProvider()
{
	OptionPreferences::const_iterator it = find("3DAudioProvider");
	if (it == end())
		return TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS];
	return it->second;
}

AsciiString OptionPreferences::getSpeakerType()
{
	OptionPreferences::const_iterator it = find("SpeakerType");
	if (it == end())
		return TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getAudioSettings()->m_defaultSpeakerType2D);
	return it->second;
}

Real OptionPreferences::getSoundVolume()
{
	OptionPreferences::const_iterator it = find("SFXVolume");
	if (it == end())
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative < 0 )
		{
			Real scale = 1.0f + relative;
			return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f;
	}

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Real OptionPreferences::get3DSoundVolume()
{
	OptionPreferences::const_iterator it = find("SFX3DVolume");
	if (it == end())
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative > 0 )
		{
			Real scale = 1.0f - relative;
			return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f;
	}

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Real OptionPreferences::getSpeechVolume()
{
	OptionPreferences::const_iterator it = find("VoiceVolume");
	if (it == end())
		return TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f;

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Bool OptionPreferences::getCloudShadowsEnabled()
{
	OptionPreferences::const_iterator it = find("UseCloudMap");
	if (it == end())
		return TheGlobalData->m_useCloudMap;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getLightmapEnabled()
{
	OptionPreferences::const_iterator it = find("UseLightMap");
	if (it == end())
		return TheGlobalData->m_useLightMap;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getSmoothWaterEnabled()
{
	OptionPreferences::const_iterator it = find("ShowSoftWaterEdge");
	if (it == end())
		return TheGlobalData->m_showSoftWaterEdge;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getTreesEnabled()
{
	OptionPreferences::const_iterator it = find("ShowTrees");
	if (it == end())
		return TheGlobalData->m_useTrees;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getExtraAnimationsDisabled()
{
	OptionPreferences::const_iterator it = find("ExtraAnimations");
	if (it == end())
		return TheGlobalData->m_useDrawModuleLOD;

	if (stricmp(it->second.str(), "yes") == 0) {
		return FALSE;	//we are enabling extra animations, so disabled LOD
	}
	return TRUE;
}

Bool OptionPreferences::getUseHeatEffects()
{
	OptionPreferences::const_iterator it = find("HeatEffects");
	if (it == end())
		return TheGlobalData->m_useHeatEffects;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getDynamicLODEnabled()
{
	OptionPreferences::const_iterator it = find("DynamicLOD");
	if (it == end())
		return TheGlobalData->m_enableDynamicLOD;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getFPSLimitEnabled()
{
	OptionPreferences::const_iterator it = find("FPSLimit");
	if (it == end())
		return TheGlobalData->m_useFpsLimit;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

#if RTS_ZEROHOUR
Int OptionPreferences::getRenderFPSLimit() const
{
	const Int fps = getInt("RenderFPSLimit", 240);
	switch (fps)
	{
		case 0:
		case 30:
		case 60:
		case 120:
		case 240:
			return fps;
		default:
			return 240;
	}
}
#endif

Bool OptionPreferences::get3DShadowsEnabled()
{
	OptionPreferences::const_iterator it = find("UseShadowVolumes");
	if (it == end())
		return TheGlobalData->m_useShadowVolumes;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::get2DShadowsEnabled()
{
	OptionPreferences::const_iterator it = find("UseShadowDecals");
	if (it == end())
		return TheGlobalData->m_useShadowDecals;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getBuildingOcclusionEnabled()
{
	OptionPreferences::const_iterator it = find("BuildingOcclusion");
	if (it == end())
		return TheGlobalData->m_enableBehindBuildingMarkers;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Int OptionPreferences::getParticleCap()
{
	OptionPreferences::const_iterator it = find("MaxParticleCount");
	if (it == end())
		return TheGlobalData->m_maxParticleCount;

	Int factor = (Int) atoi(it->second.str());
	if (factor < 100)	//clamp to at least 100 particles.
		factor = 100;

	return factor;
}

Int OptionPreferences::getTextureReduction()
{
	OptionPreferences::const_iterator it = find("TextureReduction");
	if (it == end())
		return -1;	//unknown texture reduction

	Int factor = (Int) atoi(it->second.str());
	if (factor > 2)	//clamp it.
		factor=2;
	return factor;
}

Real OptionPreferences::getGammaValue()
{
	OptionPreferences::const_iterator it = find("Gamma");
	if (it == end())
		return 50.0f;

	Real gamma = (Real) atoi(it->second.str());
	return gamma;
}

void OptionPreferences::getResolution(Int *xres, Int *yres)
{
	*xres = TheGlobalData->m_xResolution;
	*yres = TheGlobalData->m_yResolution;

	OptionPreferences::const_iterator it = find("Resolution");
	if (it == end())
		return;

	Int selectedXRes,selectedYRes;
	if (sscanf(it->second.str(),"%d%d", &selectedXRes, &selectedYRes) != 2)
		return;

	*xres=selectedXRes;
	*yres=selectedYRes;
}

Real OptionPreferences::getMusicVolume()
{
	OptionPreferences::const_iterator it = find("MusicVolume");
	if (it == end())
		return TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f;

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Real OptionPreferences::getMoneyTransactionVolume() const
{
	OptionPreferences::const_iterator it = find("MoneyTransactionVolume");
	if (it == end())
		return TheAudio->getAudioSettings()->m_defaultMoneyTransactionVolume * 100.0f;

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
		volume = 0.0f;

	return volume;
}

Int OptionPreferences::getNetworkLatencyFontSize()
{
	OptionPreferences::const_iterator it = find("NetworkLatencyFontSize");
	if (it == end())
		return 8;

	Int fontSize = atoi(it->second.str());
	if (fontSize < 0)
	{
		fontSize = 0;
	}
	return fontSize;
}

Int OptionPreferences::getRenderFpsFontSize()
{
	OptionPreferences::const_iterator it = find("RenderFpsFontSize");
	if (it == end())
		return 8;

	Int fontSize = atoi(it->second.str());
	if (fontSize < 0)
	{
		fontSize = 0;
	}
	return fontSize;
}

Int OptionPreferences::getSystemTimeFontSize()
{
	OptionPreferences::const_iterator it = find("SystemTimeFontSize");
	if (it == end())
		return 8;

	Int fontSize = atoi(it->second.str());
	if (fontSize < 0)
	{
		fontSize = 0;
	}
	return fontSize;
}

Int OptionPreferences::getGameTimeFontSize()
{
	OptionPreferences::const_iterator it = find("GameTimeFontSize");
	if (it == end())
		return 8;

	Int fontSize = atoi(it->second.str());
	if (fontSize < 0)
	{
		fontSize = 0;
	}
	return fontSize;
}

Int OptionPreferences::getPlayerInfoListFontSize()
{
	OptionPreferences::const_iterator it = find("PlayerInfoListFontSize");
	if (it == end())
		return 8;

	Int fontSize = atoi(it->second.str());
	if (fontSize < 0)
	{
		fontSize = 0;
	}
	return fontSize;
}

Real OptionPreferences::getResolutionFontAdjustment()
{
	OptionPreferences::const_iterator it = find("ResolutionFontAdjustment");
	if (it == end())
		return -1.0f;

	Real fontScale = (Real)atof(it->second.str()) / 100.0f;
	if (fontScale < 0.0f)
	{
		fontScale = -1.0f;
	}
	return fontScale;
}

Bool OptionPreferences::getShowMoneyPerMinute() const
{
	OptionPreferences::const_iterator it = find("ShowMoneyPerMinute");
	if (it == end())
		return TheGlobalData->m_showMoneyPerMinute;

	if (stricmp(it->second.str(), "yes") == 0)
	{
		return TRUE;
	}
	return FALSE;
}

#if defined(GENERALS_ONLINE)
Int OptionPreferences::getObserverNotificationFontSize() const
{
	OptionPreferences::const_iterator it = find("ObserverNotificationFontSize");
	if (it == end())
		return TheGlobalData->m_observerNotificationFontSize;

	return clamp(0, atoi(it->second.str()), 15);
}

static Bool getYesNoPreference(const OptionPreferences &prefs, const char *key, Bool defaultValue)
{
	OptionPreferences::const_iterator it = prefs.find(key);
	if (it == prefs.end())
		return defaultValue;

	return stricmp(it->second.str(), "yes") == 0;
}

Bool OptionPreferences::getObserverNotificationSpecialPowerUsage() const
{
	return getYesNoPreference(*this, "ObserverNotificationSpecialPowerUsage", TheGlobalData->m_observerNotificationSpecialPowerUsage);
}

Bool OptionPreferences::getObserverNotificationSpecialPowerPurchase() const
{
	return getYesNoPreference(*this, "ObserverNotificationSpecialPowerPurchase", TheGlobalData->m_observerNotificationSpecialPowerPurchase);
}

Bool OptionPreferences::getObserverNotificationMilestone() const
{
	return getYesNoPreference(*this, "ObserverNotificationMilestone", TheGlobalData->m_observerNotificationMilestone);
}
#endif

Real OptionPreferences::getGameWindowTransitionSpeedMultiplier() const
{
	OptionPreferences::const_iterator it = find("GameWindowTransitionSpeedMultiplier");
	if (it == end())
		return 1.0f;

	Real speed = (Real) atof(it->second.str());
	return clamp(1.0f, speed, 1000.0f);
}
