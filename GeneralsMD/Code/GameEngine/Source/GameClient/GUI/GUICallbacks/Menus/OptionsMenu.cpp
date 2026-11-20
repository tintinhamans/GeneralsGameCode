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

// FILE: OptionsMenu.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: options menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "gamespy/ghttp/ghttp.h"

#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/OptionPreferences.h"
#include "Common/GameLOD.h"
#include "Common/FramePacer.h"
#include "Common/GameUtility.h"
#include "Common/Recorder.h"
#include "Common/Registry.h"
#include "Common/version.h"

#include "GameClient/ClientInstance.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GUIUtil.h"
#include "GameClient/Display.h"
#include "GameClient/ControlBar.h"
#include "GameClient/IMEManager.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GlobalLanguage.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "WWDownload/Registry.h"
#include "GameClient/MessageBox.h"

#include "WW3D2/ww3d.h"
#include "WW3D2/texturefilter.h"

#include "GameClient/Image.h"

#include <algorithm>
#include <vector>

#include "OptionsMenuTabs.inl"

// This is for non-RC builds only!!!
#define VERBOSE_VERSION L"Release"



static NameKeyType		comboBoxOnlineIPID	= NAMEKEY_INVALID;
static GameWindow *		comboBoxOnlineIP		= nullptr;

static NameKeyType		comboBoxLANIPID	= NAMEKEY_INVALID;
static GameWindow *		comboBoxLANIP		= nullptr;

static NameKeyType    comboBoxAntiAliasingID   = NAMEKEY_INVALID;
static GameWindow *   comboBoxAntiAliasing     = nullptr;

static NameKeyType    comboBoxResolutionID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxResolution       = nullptr;

static NameKeyType    comboBoxDetailID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxDetail        = nullptr;

static NameKeyType		checkAlternateMouseID	= NAMEKEY_INVALID;
static GameWindow *		checkAlternateMouse		= nullptr;

static NameKeyType		checkRetaliationID	= NAMEKEY_INVALID;
static GameWindow *		checkRetaliation		= nullptr;

static NameKeyType		checkDoubleClickAttackMoveID	= NAMEKEY_INVALID;
static GameWindow *		checkDoubleClickAttackMove		= nullptr;
static GameWindow *		checkFacingDrag						= nullptr;

static NameKeyType		sliderScrollSpeedID	= NAMEKEY_INVALID;
static GameWindow *		sliderScrollSpeed		= nullptr;

static NameKeyType		checkMaxCameraHeightID	= NAMEKEY_INVALID;
static GameWindow *		checkMaxCameraHeight		= nullptr;
static NameKeyType		textEntryMaxCameraHeightID	= NAMEKEY_INVALID;
static GameWindow *		textEntryMaxCameraHeight		= nullptr;

static NameKeyType		checkBorderlessWindowID	= NAMEKEY_INVALID;
static GameWindow *		checkBorderlessWindow		= nullptr;
static NameKeyType		checkWindowedID	= NAMEKEY_INVALID;
static GameWindow *		checkWindowed		= nullptr;

static NameKeyType    checkLanguageFilterID = NAMEKEY_INVALID;
static GameWindow *   checkLanguageFilter   = nullptr;

static NameKeyType		checkUseCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkUseCamera			= nullptr;

static NameKeyType		checkSaveCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkSaveCamera			= nullptr;

static NameKeyType		checkDrawAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkDrawAnchor			= nullptr;

static NameKeyType		checkMoveAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkMoveAnchor			= nullptr;

static NameKeyType		buttonFirewallRefreshID	= NAMEKEY_INVALID;
static GameWindow *		buttonFirewallRefresh		= nullptr;
//
//static NameKeyType    checkAudioHardwareID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioHardware   = nullptr;
//
//static NameKeyType    checkAudioSurroundID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioSurround   = nullptr;
////volume controls
//
static NameKeyType    sliderMusicVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderMusicVolume   = nullptr;

static NameKeyType    sliderSFXVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderSFXVolume   = nullptr;

static NameKeyType    sliderVoiceVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderVoiceVolume   = nullptr;

static NameKeyType    sliderGammaID = NAMEKEY_INVALID;
static GameWindow *   sliderGamma = nullptr;

//Advanced Options Screen
static NameKeyType    WinAdvancedDisplayID      = NAMEKEY_INVALID;
static GameWindow *   WinAdvancedDisplay				= nullptr;

static NameKeyType    ButtonAdvancedAcceptID      = NAMEKEY_INVALID;
static GameWindow *   ButtonAdvancedAccept				= nullptr;

static NameKeyType    ButtonAdvancedCancelID      = NAMEKEY_INVALID;
static GameWindow *   ButtonAdvancedCancel				= nullptr;

//Game Options Screen
static NameKeyType    WinGameOptionsID            = NAMEKEY_INVALID;
static GameWindow *   WinGameOptions              = nullptr;

static NameKeyType    ButtonGameOptionsID         = NAMEKEY_INVALID;
static GameWindow *   ButtonGameOptions           = nullptr;

static NameKeyType    ButtonGameOptionsAcceptID   = NAMEKEY_INVALID;
static GameWindow *   ButtonGameOptionsAccept     = nullptr;

static NameKeyType    ButtonGameOptionsCancelID   = NAMEKEY_INVALID;
static GameWindow *   ButtonGameOptionsCancel     = nullptr;

//Shaders Screen
static GameWindow *   WinShaders                  = nullptr;
static NameKeyType    ButtonShadersID             = NAMEKEY_INVALID;
static NameKeyType    ButtonShadersAcceptID       = NAMEKEY_INVALID;
static NameKeyType    ButtonShadersCancelID       = NAMEKEY_INVALID;
static GameWindow *   comboBoxShadows             = nullptr;
static GameWindow *   comboBoxSuperSampling       = nullptr;

//Hotkeys Screen
static NameKeyType    ButtonHotkeysID             = NAMEKEY_INVALID;
static NameKeyType    comboBoxSuperSamplingID     = NAMEKEY_INVALID;

static GameWindow *   buttonMainAccept            = nullptr;
static GameWindow *   buttonMainBack              = nullptr;
static GameWindow *   buttonMainDefaults          = nullptr;

static GameWindow *   comboBoxHealthBars          = nullptr;
static GameWindow *   comboBoxAlliedDecals        = nullptr;
static GameWindow *   comboBoxHouseColors         = nullptr;
static GameWindow *   comboBoxBuildTimers         = nullptr;
static GameWindow *   comboBoxActionLines         = nullptr;
static GameWindow *   comboBoxCastMode            = nullptr;
static GameWindow *   comboBoxTextureFilter       = nullptr;
static GameWindow *   comboBoxAnisotropy          = nullptr;
static GameWindow *   comboBoxRenderFPSLimit      = nullptr;
static GameWindow *   checkNumericalHealth        = nullptr;
static GameWindow *   checkSmartPips              = nullptr;
static GameWindow *   checkSelectionCircle        = nullptr;
static GameWindow *   checkDefensesRangeCircle    = nullptr;
static GameWindow *   checkQueueBar               = nullptr;
static GameWindow *   checkVeterancyLevel         = nullptr;
static GameWindow *   checkObjectDecals           = nullptr;
static GameWindow *   checkEasyMilitaryDrag       = nullptr;
static GameWindow *   checkSmartSelection         = nullptr;
static GameWindow *   checkSmartSelectionUseMouse = nullptr;
static GameWindow *   checkSmartCommandGroup      = nullptr;
static GameWindow *   checkBuildQueueBar          = nullptr;
static GameWindow *   checkNewRadar               = nullptr;
static GameWindow *   checkLargeBlips             = nullptr;
static NameKeyType    checkGridHotkeysID          = NAMEKEY_INVALID;
static GameWindow *   checkGridHotkeys            = nullptr;
static GameWindow *   comboBoxGridHotkeyLayout    = nullptr;
static GameWindow *   checkGridHotkeyUnitOrders   = nullptr;
static GameWindow *   checkKeyboardOverlay        = nullptr;
static NameKeyType    checkKeyboardOverlayBackdropID = NAMEKEY_INVALID;
static GameWindow *   checkKeyboardOverlayBackdrop = nullptr;
static GameWindow *   textEntryKeyboardOverlayRed   = nullptr;
static GameWindow *   textEntryKeyboardOverlayGreen = nullptr;
static GameWindow *   textEntryKeyboardOverlayBlue  = nullptr;
static GameWindow *   textEntryKeyboardOverlayBackdropRed     = nullptr;
static GameWindow *   textEntryKeyboardOverlayBackdropGreen   = nullptr;
static GameWindow *   textEntryKeyboardOverlayBackdropBlue    = nullptr;
static GameWindow *   textEntryKeyboardOverlayBackdropOpacity = nullptr;
static NameKeyType    checkBloomID                = NAMEKEY_INVALID;
static GameWindow *   checkBloom                  = nullptr;
static GameWindow *   textEntryBloomStrength      = nullptr;
static GameWindow *   textEntryBloomThreshold     = nullptr;
static GameWindow *   textEntryBloomSize          = nullptr;
static GameWindow *   checkBloomDebug             = nullptr;
static GameWindow *   checkLaserRef               = nullptr;
static GameWindow *   checkShadowMap              = nullptr;
static NameKeyType    checkSpecularID             = NAMEKEY_INVALID;
static GameWindow *   checkSpecular               = nullptr;
static GameWindow *   checkSpecularDebug          = nullptr;
static NameKeyType    checkNormalMapsID           = NAMEKEY_INVALID;
static GameWindow *   checkNormalMaps             = nullptr;
static GameWindow *   checkNormalMapDebug         = nullptr;
static GameWindow *   checkWaterReflections       = nullptr;
static GameWindow *   checkShorelineFoam          = nullptr;
static NameKeyType    checkDynamicLightsID        = NAMEKEY_INVALID;
static GameWindow *   checkDynamicLights          = nullptr;
static NameKeyType    checkPixelLightsID          = NAMEKEY_INVALID;
static GameWindow *   checkPixelLights            = nullptr;
static GameWindow *   checkPathTracing            = nullptr;
static GameWindow *   comboBoxPathTracing         = nullptr;
static GameWindow *   checkDynamicLightsDebug     = nullptr;
static GameWindow *   checkSoftParticles          = nullptr;
static NameKeyType    checkAmbientOcclusionID     = NAMEKEY_INVALID;
static GameWindow *   checkAmbientOcclusion       = nullptr;
static GameWindow *   checkAmbientOcclusionDebug  = nullptr;
static GameWindow *   checkHeightBlend            = nullptr;
static GameWindow *   checkHQSky                  = nullptr;
static GameWindow *   checkVSync                  = nullptr;
static GameWindow *   checkLowLatency             = nullptr;
static GameWindow *   checkSmoothUnitMotion       = nullptr;

// Options.ini spellings, indexed by the matching enum and combo box position
static const char *const HealthBarModeNames[] = { "Classic", "Damaged", "Always" };
static const char *const BuildTimerModeNames[] = { "None", "Seconds", "Auto" };
static const char *const CastModeNames[] = { "Normal", "QuickCast", "QuickCastWithIndicator" };
static const Int AnisotropyLevels[] = { 2, 4, 8, 16 };
static const Int ShadowMapResolutions[] = { 512, 1024, 2048, 4096 };
static const Int SuperSamplingPercents[] = { 100, 150, 200, WW3D::SUPER_SAMPLING_16K };
static const char *const PathTracingModeNames[] = { "Off", "Quality", "Fast" };
static const Int RenderFPSLimits[] = { 30, 60, 120, 240, 0 };
static_assert( ARRAY_SIZE(HealthBarModeNames) == HealthBarDisplayMode_Count, "HealthBarModeNames out of date" );
static const char *const AlliedDecalModeNames[] = { "Hidden", "House", "Army" };
static_assert( ARRAY_SIZE(AlliedDecalModeNames) == AlliedDecalMode_Count, "AlliedDecalModeNames out of date" );
static const char *const HouseColorModeNames[] = { "Default", "Basic" };
static_assert( ARRAY_SIZE(HouseColorModeNames) == HouseColorMode_Count, "HouseColorModeNames out of date" );
static_assert( ARRAY_SIZE(BuildTimerModeNames) == BuildTimerDisplayMode_Count, "BuildTimerModeNames out of date" );
static const char *const ActionLineModeNames[] = { "Off", "All", "AttackOnly" };
static_assert( ARRAY_SIZE(ActionLineModeNames) == ActionLineMode_Count, "ActionLineModeNames out of date" );
static_assert( ARRAY_SIZE(CastModeNames) == CastMode_Count, "CastModeNames out of date" );
static const char *const GridHotkeyLayouts[] = { GRID_HOTKEY_LAYOUT_DEFAULT,"ASDFGHJKLZXCVBNM,.", "QWERTYUIOZXCVBNM,." };
static const wchar_t *const GridHotkeyLayoutNames[] = { L"QWER / ASDF", L"ASDF / ZXCV", L"QWER / ZXCV" };
static_assert( ARRAY_SIZE(GridHotkeyLayoutNames) == ARRAY_SIZE(GridHotkeyLayouts), "GridHotkeyLayoutNames out of date" );

static NameKeyType    sliderTextureResolutionID = NAMEKEY_INVALID;
static GameWindow *   sliderTextureResolution = nullptr;

static NameKeyType    sliderParticleCapID = NAMEKEY_INVALID;
static GameWindow *   sliderParticleCap = nullptr;

static NameKeyType    check3DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check3DShadows   = nullptr;

static NameKeyType    check2DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check2DShadows   = nullptr;

static NameKeyType    checkCloudShadowsID = NAMEKEY_INVALID;
static GameWindow *   checkCloudShadows   = nullptr;

static NameKeyType    checkGroundLightingID = NAMEKEY_INVALID;
static GameWindow *   checkGroundLighting   = nullptr;

static NameKeyType    checkSmoothWaterID = NAMEKEY_INVALID;
static GameWindow *   checkSmoothWater   = nullptr;

static NameKeyType    checkBuildingOcclusionID = NAMEKEY_INVALID;
static GameWindow *   checkBuildingOcclusion   = nullptr;

static NameKeyType    checkPropsID = NAMEKEY_INVALID;
static GameWindow *   checkProps   = nullptr;

static NameKeyType    checkExtraAnimationsID = NAMEKEY_INVALID;
static GameWindow *   checkExtraAnimations   = nullptr;

static NameKeyType    checkNoDynamicLodID = NAMEKEY_INVALID;
static GameWindow *   checkNoDynamicLod   = nullptr;

static NameKeyType    checkUnlockFpsID = NAMEKEY_INVALID;
static GameWindow *   checkUnlockFps   = nullptr;

static NameKeyType    checkHeatEffectsID = NAMEKEY_INVALID;
static GameWindow *   checkHeatEffects   = nullptr;

/*

static NameKeyType    radioHighID = NAMEKEY_INVALID;
static GameWindow *   radioHigh   = nullptr;
static NameKeyType    radioMediumID = NAMEKEY_INVALID;
static GameWindow *   radioMedium   = nullptr;
static NameKeyType    radioLowID = NAMEKEY_INVALID;
static GameWindow *   radioLow   = nullptr;

*/

DisplaySettings oldDispSettings, newDispSettings;
Bool dispChanged = FALSE;
extern Int timer;
extern void DoResolutionDialog();
static Bool ignoreSelected = FALSE;
WindowLayout *OptionsLayout = nullptr;

static OptionPreferences *pref = nullptr;

// Shows a camera limit in the text entry; the entry only takes input while the checkbox is on
static void showMaxCameraHeightEntry( Bool useCustom, Int height )
{
	if (textEntryMaxCameraHeight)
	{
		UnicodeString shown;
		shown.format( L"%d", height );
		GadgetTextEntrySetText( textEntryMaxCameraHeight, shown );
		textEntryMaxCameraHeight->winEnable( useCustom );
	}
}

// Setting the checkbox re-sends GBM_SELECTED, so the click handler must not call this
static void showMaxCameraHeight( Bool useCustom, Int height )
{
	if (checkMaxCameraHeight)
	{
		GadgetCheckBoxSetChecked( checkMaxCameraHeight, useCustom );
	}
	showMaxCameraHeightEntry( useCustom, height );
}

static void enableWindow( GameWindow *window, Bool on )
{
	if (window)
	{
		window->winEnable( on );
	}
}

static void setCheck( GameWindow *check, Bool on )
{
	if (check)
	{
		GadgetCheckBoxSetChecked( check, on );
	}
}

static Bool getCheck( GameWindow *check, Bool fallback )
{
	if (!check)
	{
		return fallback;
	}
	return GadgetCheckBoxIsChecked( check );
}

static void setComboPos( GameWindow *combo, Int pos )
{
	if (combo)
	{
		GadgetComboBoxSetSelectedPos( combo, pos );
	}
}

static Int getComboPos( GameWindow *combo, Int fallback )
{
	if (!combo)
	{
		return fallback;
	}
	Int pos = -1;
	GadgetComboBoxGetSelectedPos( combo, &pos );
	return pos < 0 ? fallback : pos;
}

static void setEntryAscii( GameWindow *entry, const AsciiString &text )
{
	if (entry)
	{
		UnicodeString shown;
		shown.translate( text );
		GadgetTextEntrySetText( entry, shown );
	}
}

static void setEntryInt( GameWindow *entry, Int value )
{
	if (entry)
	{
		UnicodeString shown;
		shown.format( L"%d", value );
		GadgetTextEntrySetText( entry, shown );
	}
}

static AsciiString getEntryAscii( GameWindow *entry, const AsciiString &fallback )
{
	if (!entry)
	{
		return fallback;
	}
	AsciiString text;
	text.translate( GadgetTextEntryGetText( entry ) );
	return text;
}

// An empty field means the caller's fallback, so a cleared colour channel never reads as 0 by accident
static Int getEntryInt( GameWindow *entry, Int minVal, Int maxVal, Int fallback )
{
	if (!entry)
	{
		return fallback;
	}
	AsciiString text;
	text.translate( GadgetTextEntryGetText( entry ) );
	if (text.isEmpty())
	{
		return fallback;
	}
	return clamp( minVal, atoi( text.str() ), maxVal );
}

static void showColorEntries( Color color, GameWindow *red, GameWindow *green, GameWindow *blue, GameWindow *alpha )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( color, &r, &g, &b, &a );
	setEntryInt( red, r );
	setEntryInt( green, g );
	setEntryInt( blue, b );
	setEntryInt( alpha, a );
}

// Writes the channels as <prefix>Red/Green/Blue/Opacity and returns the colour they make
static Color saveColorEntries( const char *prefix, Color fallback, GameWindow *red, GameWindow *green, GameWindow *blue, GameWindow *alpha )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( fallback, &r, &g, &b, &a );
	const Int channels[4] =
	{
		getEntryInt( red, 0, 255, r ),
		getEntryInt( green, 0, 255, g ),
		getEntryInt( blue, 0, 255, b ),
		getEntryInt( alpha, 0, 255, a ),
	};
	static const char *const names[4] = { "Red", "Green", "Blue", "Opacity" };
	for (Int i = 0; i < 4; ++i)
	{
		if (i == 3 && alpha == nullptr)
		{
			break;
		}
		AsciiString key, value;
		key.format( "%s%s", prefix, names[i] );
		value.format( "%d", channels[i] );
		(*pref)[key] = value;
	}
	return GameMakeColor( channels[0], channels[1], channels[2], channels[3] );
}

// The plain yes/no settings, read, shown, saved and defaulted from one place
struct BoolOption
{
	GameWindow **check;
	const char *prefKey;
	Bool (OptionPreferences::*read)() const;
	Bool GlobalData::*field;
	Bool defaultValue;
};

static const BoolOption BoolOptions[] =
{
	{ &checkNumericalHealth, "NumericalHealth", &OptionPreferences::getNumericalHealthEnabled, &GlobalData::m_numericalHealth, FALSE },
	{ &checkSmartPips, "SmartPips", &OptionPreferences::getSmartPipsEnabled, &GlobalData::m_smartPips, FALSE },
	{ &checkSelectionCircle, "SelectionCircle", &OptionPreferences::getSelectionCircleEnabled, &GlobalData::m_selectionCircleEnabled, FALSE },
	{ &checkDefensesRangeCircle, "DefensesRangeCircle", &OptionPreferences::getDefensesRangeCircleEnabled, &GlobalData::m_defensesRangeCircle, FALSE },
	{ &checkQueueBar, "QueueBar", &OptionPreferences::getQueueBarEnabled, &GlobalData::m_queueBar, FALSE },
	{ &checkObjectDecals, "ObjectDecals", &OptionPreferences::getObjectDecalsEnabled, &GlobalData::m_objectDecalsEnabled, TRUE },
	{ &checkEasyMilitaryDrag, "EasyMilitaryDrag", &OptionPreferences::getEasyMilitaryDragEnabled, &GlobalData::m_easyMilitaryDrag, FALSE },
	{ &checkFacingDrag, "UseFacingDrag", &OptionPreferences::getFacingDragEnabled, &GlobalData::m_facingDrag, TRUE },
	{ &checkSmartSelection, "SmartSelection", &OptionPreferences::getSmartSelectionEnabled, &GlobalData::m_smartSelection, TRUE },
	{ &checkSmartSelectionUseMouse, "SmartSelectionUseMouse", &OptionPreferences::getSmartSelectionUseMouse, &GlobalData::m_smartSelectionUseMouse, TRUE },
	{ &checkSmartCommandGroup, "SmartCommandGroup", &OptionPreferences::getSmartCommandGroupEnabled, &GlobalData::m_smartCommandGroup, TRUE },
	{ &checkBuildQueueBar, "BuildQueueBar", &OptionPreferences::getBuildQueueBarEnabled, &GlobalData::m_buildQueueBar, TRUE },
	// the radar caches this when it is created, so it waits for the next launch
	{ &checkNewRadar, "NewRadar", &OptionPreferences::getNewRadarEnabled, &GlobalData::m_newRadar, FALSE },
	{ &checkGridHotkeys, "GridHotkeys", &OptionPreferences::getGridHotkeysEnabled, &GlobalData::m_gridHotkeysEnabled, FALSE },
	{ &checkGridHotkeyUnitOrders, "GridHotkeyUnitOrders", &OptionPreferences::getGridHotkeyUnitOrders, &GlobalData::m_gridHotkeyUnitOrders, TRUE },
	{ &checkKeyboardOverlay, "KeyboardOverlay", &OptionPreferences::getKeyboardOverlayEnabled, &GlobalData::m_keyboardOverlayEnabled, TRUE },
	{ &checkKeyboardOverlayBackdrop, "KeyboardOverlayBackdrop", &OptionPreferences::getKeyboardOverlayBackdropEnabled, &GlobalData::m_keyboardOverlayBackdrop, TRUE },
	{ &checkBloom, "Bloom", &OptionPreferences::getBloomEnabled, &GlobalData::m_useBloom, TRUE },
	{ &checkBloomDebug, "BloomDebug", &OptionPreferences::getBloomDebugEnabled, &GlobalData::m_bloomDebug, FALSE },
	{ &checkLaserRef, "LaserRef", &OptionPreferences::getLaserRefEnabled, &GlobalData::m_laserRef, TRUE },
	{ &checkShadowMap, "ShadowMap", &OptionPreferences::getShadowMapEnabled, &GlobalData::m_useShadowMap, TRUE },
	{ &checkSpecular, "Specular", &OptionPreferences::getSpecularEnabled, &GlobalData::m_useSpecular, TRUE },
	{ &checkSpecularDebug, "SpecularDebug", &OptionPreferences::getSpecularDebugEnabled, &GlobalData::m_specularDebug, FALSE },
	{ &checkNormalMaps, "NormalMaps", &OptionPreferences::getNormalMapsEnabled, &GlobalData::m_useNormalMaps, FALSE },
	{ &checkNormalMapDebug, "NormalMapDebug", &OptionPreferences::getNormalMapDebugEnabled, &GlobalData::m_normalMapDebug, FALSE },
	{ &checkWaterReflections, "WaterReflections", &OptionPreferences::getWaterReflectionsEnabled, &GlobalData::m_waterReflections, TRUE },
	{ &checkShorelineFoam, "ShorelineFoam", &OptionPreferences::getShorelineFoamEnabled, &GlobalData::m_shorelineFoam, TRUE },
	{ &checkDynamicLights, "DynamicLights", &OptionPreferences::getDynamicLightsEnabled, &GlobalData::m_useDynamicLights, TRUE },
	{ &checkPixelLights, "PixelLights", &OptionPreferences::getPixelLightsEnabled, &GlobalData::m_usePixelLights, TRUE },
	{ &checkPathTracing, "PathTracing", &OptionPreferences::getPathTracingEnabled, &GlobalData::m_usePathTracing, FALSE },
	{ &checkDynamicLightsDebug, "DynamicLightsDebug", &OptionPreferences::getDynamicLightsDebugEnabled, &GlobalData::m_dynamicLightsDebug, FALSE },
	{ &checkSoftParticles, "SoftParticles", &OptionPreferences::getSoftParticlesEnabled, &GlobalData::m_useSoftParticles, TRUE },
	{ &checkAmbientOcclusion, "AmbientOcclusion", &OptionPreferences::getAmbientOcclusionEnabled, &GlobalData::m_useAmbientOcclusion, FALSE },
	{ &checkAmbientOcclusionDebug, "AmbientOcclusionDebug", &OptionPreferences::getAmbientOcclusionDebugEnabled, &GlobalData::m_ambientOcclusionDebug, FALSE },
	{ &checkHeightBlend, "HeightBlend", &OptionPreferences::getHeightBlendEnabled, &GlobalData::m_useHeightBlend, TRUE },
	{ &checkHQSky, "HQSky", &OptionPreferences::getHQSkyEnabled, &GlobalData::m_useHQSky, TRUE },
	{ &checkLowLatency, "LowLatency", &OptionPreferences::getLowLatencyEnabled, &GlobalData::m_lowLatency, FALSE },
	{ &checkSmoothUnitMotion, "SmoothUnitMotion", &OptionPreferences::getSmoothUnitMotionEnabled, &GlobalData::m_smoothUnitMotion, TRUE },
};

// the strength is stored as 0..1 but edited as a percentage
static Int bloomPercent( Real strength )
{
	return REAL_TO_INT( strength * 100.0f + 0.5f );
}

// A layout the combo does not offer shows as the first one
static Int gridLayoutIndex( const AsciiString& layout )
{
	for (Int i = 0; i < ARRAY_SIZE(GridHotkeyLayouts); ++i)
	{
		if (layout.compareNoCase( GridHotkeyLayouts[i] ) == 0)
		{
			return i;
		}
	}
	return 0;
}

static Int levelIndex( const Int *levels, Int count, Int level )
{
	for (Int i = 0; i < count; ++i)
	{
		if (levels[i] == level)
		{
			return i;
		}
	}
	return 0;
}

// A tabbed layout spreads the Game Options and Shaders controls over its pages
static Bool hasGameOptions()
{
	return WinGameOptions != nullptr || OptionsTabs::isOn();
}

// Fields that only matter while their checkbox is on stay greyed out otherwise
static void updateGameOptionsEnables()
{
	const Bool canChangeDisplay = (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame()) && !TheGameSpyInfo;
	enableWindow( comboBoxResolution, canChangeDisplay && !getCheck( checkBorderlessWindow, FALSE ) );

	const Bool grid = getCheck( checkGridHotkeys, FALSE );
	enableWindow( comboBoxGridHotkeyLayout, grid );
	enableWindow( checkGridHotkeyUnitOrders, grid );

	const Bool backdrop = getCheck( checkKeyboardOverlayBackdrop, FALSE );
	enableWindow( textEntryKeyboardOverlayBackdropRed, backdrop );
	enableWindow( textEntryKeyboardOverlayBackdropGreen, backdrop );
	enableWindow( textEntryKeyboardOverlayBackdropBlue, backdrop );
	enableWindow( textEntryKeyboardOverlayBackdropOpacity, backdrop );

	const Bool bloom = getCheck( checkBloom, TRUE );
	enableWindow( textEntryBloomStrength, bloom );
#if defined(BUILD_WITH_D3D9)
	enableWindow( textEntryBloomThreshold, bloom );
#else
	enableWindow( textEntryBloomThreshold, FALSE );
#endif
	enableWindow( textEntryBloomSize, bloom );
	enableWindow( checkBloomDebug, bloom );

	enableWindow( checkPixelLights, getCheck( checkDynamicLights, TRUE ) );
#if defined(BUILD_WITH_D3D9)
	enableWindow( checkPathTracing, getCheck( checkDynamicLights, TRUE ) && getCheck( checkPixelLights, TRUE ) );
	enableWindow( comboBoxPathTracing, getCheck( checkDynamicLights, TRUE ) && getCheck( checkPixelLights, TRUE ) );
#else
	enableWindow( checkPathTracing, FALSE );
	enableWindow( comboBoxPathTracing, FALSE );
#endif
	enableWindow( checkDynamicLightsDebug, getCheck( checkDynamicLights, TRUE ) && getCheck( checkPixelLights, TRUE ) );
	// the specular pass also draws normal maps, so its debug tint shows while either is on
	enableWindow( checkSpecularDebug, getCheck( checkSpecular, TRUE ) || getCheck( checkNormalMaps, FALSE ) );
	enableWindow( checkNormalMapDebug, getCheck( checkNormalMaps, FALSE ) );
	enableWindow( checkHQSky, getCheck( checkCloudShadows, TRUE ) );

	// the scene depth it reads is multisampled, and so unreadable, with MSAA on; supersampling takes MSAA's place and keeps it readable
	Int antiAliasing = 0;
	if (comboBoxAntiAliasing)
	{
		GadgetComboBoxGetSelectedPos( comboBoxAntiAliasing, &antiAliasing );
	}
	const Bool superSampling = comboBoxSuperSampling && getComboPos( comboBoxSuperSampling, 0 ) > 0;
	enableWindow( comboBoxAntiAliasing, !superSampling );
	enableWindow( checkAmbientOcclusion, antiAliasing <= 0 || superSampling );
	enableWindow( checkAmbientOcclusionDebug, (antiAliasing <= 0 || superSampling) && getCheck( checkAmbientOcclusion, TRUE ) );
}

static void populateGameOptions()
{
	if (!hasGameOptions())
	{
		return;
	}

	setComboPos( comboBoxHealthBars, pref->getHealthBarDisplayMode() );
	setComboPos( comboBoxAlliedDecals, pref->getAlliedDecalMode() );
	setComboPos( comboBoxHouseColors, pref->getHouseColorMode() );
	setComboPos( comboBoxBuildTimers, pref->getBuildTimerDisplayMode() );
	setComboPos( comboBoxActionLines, pref->getActionLineMode() );
	setComboPos( comboBoxCastMode, pref->getCastMode() );
	setComboPos( comboBoxTextureFilter, pref->getTextureFilterMode() );
	setComboPos( comboBoxAnisotropy, levelIndex( AnisotropyLevels, ARRAY_SIZE(AnisotropyLevels), pref->getTextureAnisotropyLevel() ) );

	for (Int i = 0; i < ARRAY_SIZE(BoolOptions); ++i)
	{
		setCheck( *BoolOptions[i].check, (pref->*BoolOptions[i].read)() );
	}
	setCheck( checkLargeBlips, pref->getRadarBlipSize() == RadarBlipSize_Large );
	setCheck( checkVeterancyLevel, pref->getBool( "VeterancyLevel", FALSE ) );

	setComboPos( comboBoxGridHotkeyLayout, gridLayoutIndex( pref->getGridHotkeyLayout() ) );

	showColorEntries( pref->getKeyboardOverlayColor(), textEntryKeyboardOverlayRed, textEntryKeyboardOverlayGreen, textEntryKeyboardOverlayBlue, nullptr );
	showColorEntries( pref->getKeyboardOverlayBackdropColor(), textEntryKeyboardOverlayBackdropRed, textEntryKeyboardOverlayBackdropGreen, textEntryKeyboardOverlayBackdropBlue, textEntryKeyboardOverlayBackdropOpacity );
	setEntryInt( textEntryBloomStrength, bloomPercent( pref->getBloomStrength() ) );
	setEntryInt( textEntryBloomThreshold, bloomPercent( pref->getBloomThreshold() ) );
	setEntryInt( textEntryBloomSize, bloomPercent( pref->getBloomSize() ) );

	updateGameOptionsEnables();
}

// Back restores whichever checkboxes the layout puts on the Shaders page
static void populateShaders()
{
	setComboPos( comboBoxShadows, levelIndex( ShadowMapResolutions, ARRAY_SIZE(ShadowMapResolutions), pref->getShadowMapResolution() ) );
	// The device's own setting, because 16K is never saved and the file would show the entry before it
	setComboPos( comboBoxSuperSampling, levelIndex( SuperSamplingPercents, ARRAY_SIZE(SuperSamplingPercents), WW3D::Get_Super_Sampling() ) );
	setComboPos( comboBoxPathTracing, pref->getPathTracingMode() );
	for (Int i = 0; i < ARRAY_SIZE(BoolOptions); ++i)
	{
		GameWindow *check = *BoolOptions[i].check;
		if (check && WinShaders && check->winGetParent() == WinShaders)
		{
			setCheck( check, (pref->*BoolOptions[i].read)() );
		}
	}
	setEntryInt( textEntryBloomStrength, bloomPercent( pref->getBloomStrength() ) );
	setEntryInt( textEntryBloomThreshold, bloomPercent( pref->getBloomThreshold() ) );
	setEntryInt( textEntryBloomSize, bloomPercent( pref->getBloomSize() ) );
	updateGameOptionsEnables();
}

static void setGameOptionsDefaults()
{
	if (!hasGameOptions())
	{
		return;
	}

	setComboPos( comboBoxHealthBars, HealthBarDisplayMode_Default );
	setComboPos( comboBoxAlliedDecals, AlliedDecalMode_Default );
	setComboPos( comboBoxHouseColors, HouseColorMode_Default );
	setComboPos( comboBoxBuildTimers, BuildTimerDisplayMode_Default );
	setComboPos( comboBoxActionLines, ActionLineMode_Default );
	setComboPos( comboBoxCastMode, CastMode_Default );
	setComboPos( comboBoxTextureFilter, TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC );
	setComboPos( comboBoxAnisotropy, levelIndex( AnisotropyLevels, ARRAY_SIZE(AnisotropyLevels), TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_8X ) );

	for (Int i = 0; i < ARRAY_SIZE(BoolOptions); ++i)
	{
		setCheck( *BoolOptions[i].check, BoolOptions[i].defaultValue );
	}
	setCheck( checkLargeBlips, RadarBlipSize_Default == RadarBlipSize_Large );
	setCheck( checkVeterancyLevel, FALSE );

	setComboPos( comboBoxGridHotkeyLayout, gridLayoutIndex( GRID_HOTKEY_LAYOUT_DEFAULT ) );

	showColorEntries( GameMakeColor( 255, 255, 255, 255 ), textEntryKeyboardOverlayRed, textEntryKeyboardOverlayGreen, textEntryKeyboardOverlayBlue, nullptr );
	showColorEntries( GameMakeColor( 0, 0, 0, 128 ), textEntryKeyboardOverlayBackdropRed, textEntryKeyboardOverlayBackdropGreen, textEntryKeyboardOverlayBackdropBlue, textEntryKeyboardOverlayBackdropOpacity );
	setEntryInt( textEntryBloomStrength, 50 );
	setEntryInt( textEntryBloomThreshold, 25 );
	setEntryInt( textEntryBloomSize, 100 );

	updateGameOptionsEnables();
}

static void setDefaults()
{
	constexpr const Bool ModifyDisplaySettings = FALSE;

	//-------------------------------------------------------------------------------------------------
	// provider type
//	GadgetCheckBoxSetChecked(checkAudioHardware, FALSE);

	//-------------------------------------------------------------------------------------------------
	// speaker type
//	GadgetCheckBoxSetChecked(checkAudioSurround, FALSE);

	//-------------------------------------------------------------------------------------------------
	// language filter
	GadgetCheckBoxSetChecked( checkLanguageFilter, TRUE );

	if constexpr (ModifyDisplaySettings)
	{
	//-------------------------------------------------------------------------------------------------
	// LOD
	GadgetComboBoxSetSelectedPos(comboBoxDetail, (Int)TheGameLODManager->getRecommendedStaticLODLevel());

	//-------------------------------------------------------------------------------------------------
	// Resolution
	// The mode a fresh start picks, else 800x600.
	if ((TheGameLogic->isInGame() == FALSE || TheGameLogic->isInShellGame() == TRUE) && !TheGameSpyInfo) {
		Int defaultWidth, defaultHeight;
		GlobalData::getDefaultResolution( defaultWidth, defaultHeight );
		Int numResolutions = TheDisplay->getDisplayModeCount();
		Int defaultResIndex=0;
		for( Int i = 0; i < numResolutions; ++i )
		{	Int xres,yres,bitDepth;
			TheDisplay->getDisplayModeDescription(i,&xres,&yres,&bitDepth);
			if (xres == defaultWidth && yres == defaultHeight)
			{	defaultResIndex=i;
				break;
			}
			if (xres == DEFAULT_DISPLAY_WIDTH && yres == DEFAULT_DISPLAY_HEIGHT)
			{
				defaultResIndex=i;
			}
		}
		GadgetComboBoxSetSelectedPos( comboBoxResolution, defaultResIndex );
	}
	}

	//-------------------------------------------------------------------------------------------------
	// Mouse Mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, FALSE);
	GadgetCheckBoxSetChecked(checkRetaliation, TRUE );
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, FALSE );
	setCheck( checkBorderlessWindow, FALSE );
	setCheck( checkWindowed, FALSE );
	updateGameOptionsEnables();

	//-------------------------------------------------------------------------------------------------
//	// scroll speed val
	Int scrollPos = (Int)(TheGlobalData->m_keyboardDefaultScrollFactor*100.0f);
	GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );

	showMaxCameraHeight( FALSE, (Int)TheGlobalData->m_defaultMaxCameraHeight );
	setGameOptionsDefaults();
	setComboPos( comboBoxShadows, ARRAY_SIZE(ShadowMapResolutions) - 1 );
	setComboPos( comboBoxPathTracing, 0 );


	Int valMin, valMax;

	//-------------------------------------------------------------------------------------------------
	// slider music volume
	GadgetSliderGetMinMax(sliderMusicVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderMusicVolume,REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	GadgetSliderGetMinMax(sliderSFXVolume,&valMin, &valMax);
	Real maxVolume = MAX( TheAudio->getAudioSettings()->m_defaultSoundVolume, TheAudio->getAudioSettings()->m_default3DSoundVolume );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume * 100.0f ) );

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	GadgetSliderGetMinMax(sliderVoiceVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderVoiceVolume, REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	GadgetSliderGetMinMax(sliderGamma,&valMin, &valMax);
 	GadgetSliderSetPosition(sliderGamma, ((valMax - valMin) / 2 + valMin));

	if constexpr (ModifyDisplaySettings)
	{
	if ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE))
	{
		//-------------------------------------------------------------------------------------------------
		// Texture resolution slider
		//
		Int	txtFact=TheGameLODManager->getRecommendedTextureReduction();

		GadgetSliderSetPosition( sliderTextureResolution, 2-txtFact);

		//-------------------------------------------------------------------------------------------------
 		// 3D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

		//-------------------------------------------------------------------------------------------------
 		// 2D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

		//-------------------------------------------------------------------------------------------------
 		// Cloud Shadows checkbox
		//
		GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

		//-------------------------------------------------------------------------------------------------
 		// Ground Lighting (lightmap) checkbox
		//
		GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

		//-------------------------------------------------------------------------------------------------
 		// Smooth Water Border checkbox
		//
		GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

		//-------------------------------------------------------------------------------------------------
 		// Extra Animations (tree sway and buildups) checkbox
		//
		GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);

		//-------------------------------------------------------------------------------------------------
 		// DisableDynamicLOD
		//
		GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

		//-------------------------------------------------------------------------------------------------
 		// Disable FPS Limit
		//
		GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

		//-------------------------------------------------------------------------------------------------
 		// Heat Effects
		//
		GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

		setCheck( checkVSync, WW3D::Is_VSync_On() );

		//-------------------------------------------------------------------------------------------------
 		// Building Occlusion checkbox
		//
		GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

		//-------------------------------------------------------------------------------------------------
 		// Particle Cap slider
		//
		GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount);

		//-------------------------------------------------------------------------------------------------
 		// Trees and Shrubs
		//
		GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	}
	}
}

static void saveOptions()
{
	Int index;
	Int val;
	//-------------------------------------------------------------------------------------------------
//	// provider type
//	Bool isChecked = GadgetCheckBoxIsChecked(checkAudioHardware);
//	TheAudio->setHardwareAccelerated(isChecked);
//	(*pref)["3DAudioProvider"] = TheAudio->getProviderName(TheAudio->getSelectedProvider());
//
	//-------------------------------------------------------------------------------------------------
//	// speaker type
	//	isChecked = GadgetCheckBoxIsChecked(checkAudioSurround);
	//	TheAudio->setSpeakerSurround(isChecked);
	//	(*pref)["SpeakerType"] = TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType());
	//
	//-------------------------------------------------------------------------------------------------
	// language filter
	if( GadgetCheckBoxIsChecked( checkLanguageFilter ) )
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, true);
			TheWritableGlobalData->m_languageFilterPref = true;
			(*pref)["LanguageFilter"] = "true";
	}
	else
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, false);
			TheWritableGlobalData->m_languageFilterPref = false;
			(*pref)["LanguageFilter"] = "false";
	}


	//-------------------------------------------------------------------------------------------------
	// Custom game detail settings.
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if (index == STATIC_GAME_LOD_CUSTOM)
	{
 		//-------------------------------------------------------------------------------------------------
 		// Texture resolution slider
		{
		 		val = 2 - GadgetSliderGetPosition(sliderTextureResolution);

				AsciiString prefString;
				prefString.format("%d",val);
				(*pref)["TextureReduction"] = prefString;

				TheWritableGlobalData->m_textureReductionFactor = val;
				TheGameClient->setTextureLOD(val);
		}

		TheWritableGlobalData->m_useShadowVolumes = GadgetCheckBoxIsChecked( check3DShadows );
		(*pref)["UseShadowVolumes"] = TheWritableGlobalData->m_useShadowVolumes ? "yes" : "no";

		TheWritableGlobalData->m_useShadowDecals = GadgetCheckBoxIsChecked( check2DShadows );
		(*pref)["UseShadowDecals"] = TheWritableGlobalData->m_useShadowDecals ? "yes" : "no";

		TheWritableGlobalData->m_useCloudMap = GadgetCheckBoxIsChecked( checkCloudShadows );
		(*pref)["UseCloudMap"] = TheGlobalData->m_useCloudMap ? "yes" : "no";

		TheWritableGlobalData->m_useLightMap = GadgetCheckBoxIsChecked( checkGroundLighting );
		(*pref)["UseLightMap"] = TheGlobalData->m_useLightMap ? "yes" : "no";

		TheWritableGlobalData->m_showSoftWaterEdge = GadgetCheckBoxIsChecked( checkSmoothWater );
		(*pref)["ShowSoftWaterEdge"] = TheGlobalData->m_showSoftWaterEdge ? "yes" : "no";

		TheWritableGlobalData->m_useDrawModuleLOD = !GadgetCheckBoxIsChecked( checkExtraAnimations );
		TheWritableGlobalData->m_useTreeSway = !TheWritableGlobalData->m_useDrawModuleLOD;	//borrow same setting.
		(*pref)["ExtraAnimations"] = TheGlobalData->m_useDrawModuleLOD ? "no" : "yes";

		TheWritableGlobalData->m_enableDynamicLOD = !GadgetCheckBoxIsChecked( checkNoDynamicLod );
		(*pref)["DynamicLOD"] = TheGlobalData->m_enableDynamicLOD ? "yes" : "no";

		TheWritableGlobalData->m_useHeatEffects = GadgetCheckBoxIsChecked( checkHeatEffects );
		(*pref)["HeatEffects"] = TheGlobalData->m_useHeatEffects ? "yes" : "no";

		// Never write this out
		//TheWritableGlobalData->m_useFpsLimit = !GadgetCheckBoxIsChecked( checkUnlockFps );
		//(*pref)["FPSLimit"] = TheGlobalData->m_useFpsLimit ? "yes" : "no";

		TheWritableGlobalData->m_enableBehindBuildingMarkers = GadgetCheckBoxIsChecked( checkBuildingOcclusion );
		(*pref)["BuildingOcclusion"] = TheWritableGlobalData->m_enableBehindBuildingMarkers ? "yes" : "no";

		TheWritableGlobalData->m_useTrees = GadgetCheckBoxIsChecked( checkProps);
		(*pref)["ShowTrees"] = TheWritableGlobalData->m_useTrees ? "yes" : "no";

 		//-------------------------------------------------------------------------------------------------
		// Particle Cap slider
		{
				AsciiString prefString;

		 		val = GadgetSliderGetPosition(sliderParticleCap);

				prefString.format("%d",val);
				(*pref)["MaxParticleCount"] = prefString;

				TheWritableGlobalData->m_maxParticleCount = val;
		}
	}

	// Written only once changed, so an untouched Options.ini keeps vsync on in fullscreen and off in a window.
	if (checkVSync != nullptr && GadgetCheckBoxIsChecked( checkVSync ) != (Bool)WW3D::Is_VSync_On())
	{
		TheWritableGlobalData->m_vsync = GadgetCheckBoxIsChecked( checkVSync ) ? 1 : 0;
		(*pref)["VSync"] = TheGlobalData->m_vsync ? "yes" : "no";
		WW3D::Set_VSync_Mode( TheGlobalData->m_vsync );
	}

	//-------------------------------------------------------------------------------------------------
	// LOD
	if (comboBoxDetail && comboBoxDetail->winGetEnabled())
	{
		GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );

		// A match keeps the frame rate limit its game mode set, which the detail presets would overwrite
		const Bool fpsLimit = TheGlobalData->m_useFpsLimit;
		const Bool levelChanged = TheGameLODManager->setStaticLODLevel((StaticGameLODLevel)index);
		if (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL)
		{
			TheWritableGlobalData->m_useFpsLimit = fpsLimit;
		}

		if (levelChanged)
			(*pref)["StaticGameLOD"] = TheGameLODManager->getStaticGameLODLevelName(TheGameLODManager->getStaticLODLevel());
	}

	if (comboBoxRenderFPSLimit)
	{
		GadgetComboBoxGetSelectedPos( comboBoxRenderFPSLimit, &index );
		if (index >= 0 && index < ARRAY_SIZE(RenderFPSLimits))
		{
			const Int fps = RenderFPSLimits[index];
			AsciiString value;
			value.format("%d", fps);
			(*pref)["RenderFPSLimit"] = value;
			TheFramePacer->setRenderFPSLimit(fps);
		}
	}

	//-------------------------------------------------------------------------------------------------
	// IP address
	if (comboBoxLANIP && comboBoxLANIP->winGetEnabled())
	{
		UnsignedInt ip;
		GadgetComboBoxGetSelectedPos(comboBoxLANIP, &index);
		if (index>=0 && TheGlobalData)
		{
			ip = (UnsignedInt)GadgetComboBoxGetItemData(comboBoxLANIP, index);
			TheWritableGlobalData->m_defaultIP = ip;
			pref->setLANIPAddress(ip);
		}
	}

	if (comboBoxOnlineIP && comboBoxOnlineIP->winGetEnabled())
	{
		UnsignedInt ip;
		GadgetComboBoxGetSelectedPos(comboBoxOnlineIP, &index);
		if (index>=0)
		{
			ip = (UnsignedInt)GadgetComboBoxGetItemData(comboBoxOnlineIP, index);
			pref->setOnlineIPAddress(ip);
		}
	}

	//-------------------------------------------------------------------------------------------------
	// Firewall Port Override
	GameWindow *textEntryFirewallPortOverride = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("OptionsMenu.wnd:TextEntryFirewallPortOverride"));
	if (textEntryFirewallPortOverride && textEntryFirewallPortOverride->winGetEnabled())
	{
		UnicodeString uStr = GadgetTextEntryGetText(textEntryFirewallPortOverride);
		AsciiString aStr;
		aStr.translate(uStr);
		Int portOverride = atoi(aStr.str());
		if (portOverride < 0 || portOverride > 65535)
			portOverride = 0;
		if (TheGlobalData->m_firewallPortOverride != portOverride)
		{	TheWritableGlobalData->m_firewallPortOverride = portOverride;
		    aStr.format("%d", portOverride);
			(*pref)["FirewallPortOverride"] = aStr;
		}
	}

	//-------------------------------------------------------------------------------------------------
	// antialiasing
  GadgetComboBoxGetSelectedPos(comboBoxAntiAliasing, &index);
  if( index >= 0 )
  {
		Int mode = WW3D::MULTISAMPLE_MODE_NONE;

		// TheSuperHackers @info We are converting comboBox entry position to MultiSampleModeEnum values
		index = clamp((int)OptionPreferences::AntiAliasingMode_OFF, index, (int)OptionPreferences::AntiAliasingMode_MSAA_8X);
		mode = (index > 0) ? 1 << index : 0;

		// The device is rebuilt with the new sample count, which falls back where the card lacks it
		if (mode != (Int)WW3D::Get_MSAA_Mode())
		{
			WW3D::Set_MSAA_Mode( (WW3D::MultiSampleModeEnum)mode );
			WW3D::Set_Render_Device( -1, -1, -1, -1, -1, false, true, true );
			mode = (Int)WW3D::Get_MSAA_Mode();
		}

		TheWritableGlobalData->m_antiAliasLevel = mode;
    AsciiString prefString;
		prefString.format("%d", mode);
		(*pref)["AntiAliasing"] = prefString;
  }

	//-------------------------------------------------------------------------------------------------
	// texture filter mode
	val = getComboPos( comboBoxTextureFilter, pref->getTextureFilterMode() );
	if (val >= 0)
	{
		val = clamp((int)TextureFilterClass::TEXTURE_FILTER_NONE, val, (int)TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC);

		// W3DDisplay only pushes this at init, so a menu change has to reach the renderer here
		WW3D::Set_Texture_Filter( val );
		TheWritableGlobalData->m_textureFilteringMode = val;
		AsciiString prefString;
		prefString = TextureFilterClass::TextureFilterModeString[val];
		(*pref)["TextureFilter"] = prefString;
	}

	//-------------------------------------------------------------------------------------------------
	// anisotropy level
	val = comboBoxAnisotropy ? AnisotropyLevels[getComboPos( comboBoxAnisotropy, 0 )] : pref->getTextureAnisotropyLevel();
	if (val >= 0)
	{
		val = clamp((int)TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_2X, val, (int)TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC_16X);

		WW3D::Set_Anisotropy_Level( val );
		TheWritableGlobalData->m_textureAnisotropyLevel = val;
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["AnisotropyLevel"] = prefString;
	}

	//-------------------------------------------------------------------------------------------------
	// mouse mode
	TheWritableGlobalData->m_useAlternateMouse = GadgetCheckBoxIsChecked(checkAlternateMouse);
	(*pref)["UseAlternateMouse"] = TheWritableGlobalData->m_useAlternateMouse ? "yes" : "no";

	// TheSuperHackers @todo Add check box ?
	{
		Bool useRightMouseScrollWithAlternateMouse = pref->getRightMouseScrollWithAlternateMouseEnabled();
		(*pref)["UseRightMouseScrollWithAlternateMouse"] = useRightMouseScrollWithAlternateMouse ? "yes" : "no";
		TheWritableGlobalData->m_useRightMouseScrollWithAlternateMouse = useRightMouseScrollWithAlternateMouse;
	}

	TheWritableGlobalData->m_clientRetaliationModeEnabled = GadgetCheckBoxIsChecked(checkRetaliation);
	(*pref)["Retaliation"] = TheWritableGlobalData->m_clientRetaliationModeEnabled? "yes" : "no";

	TheWritableGlobalData->m_doubleClickAttackMove = GadgetCheckBoxIsChecked( checkDoubleClickAttackMove );
	(*pref)["UseDoubleClickAttackMove"] = TheWritableGlobalData->m_doubleClickAttackMove ? "yes" : "no";

	// TheSuperHackers @todo Add combo box ?
	{
		CursorCaptureMode mode = pref->getCursorCaptureMode();
		(*pref)["CursorCaptureEnabledInWindowedGame"] = (mode & CursorCaptureMode_EnabledInWindowedGame) ? "yes" : "no";
		(*pref)["CursorCaptureEnabledInWindowedMenu"] = (mode & CursorCaptureMode_EnabledInWindowedMenu) ? "yes" : "no";
		(*pref)["CursorCaptureEnabledInFullscreenGame"] = (mode & CursorCaptureMode_EnabledInFullscreenGame) ? "yes" : "no";
		(*pref)["CursorCaptureEnabledInFullscreenMenu"] = (mode & CursorCaptureMode_EnabledInFullscreenMenu) ? "yes" : "no";
		TheMouse->setCursorCaptureMode(mode);
	}

	// TheSuperHackers @todo Add combo box ?
	{
		ScreenEdgeScrollMode mode = pref->getScreenEdgeScrollMode();
		(*pref)["ScreenEdgeScrollEnabledInWindowedApp"] = (mode & ScreenEdgeScrollMode_EnabledInWindowedApp) ? "yes" : "no";
		(*pref)["ScreenEdgeScrollEnabledInFullscreenApp"] = (mode & ScreenEdgeScrollMode_EnabledInFullscreenApp) ? "yes" : "no";
		TheLookAtTranslator->setScreenEdgeScrollMode(mode);
	}

	// TheSuperHackers @todo Add checkbox ?
	{
		Bool enabled = pref->getPlayerObserverEnabled();
		(*pref)["PlayerObserverEnabled"] = enabled ? "yes" : "no";
		TheWritableGlobalData->m_enablePlayerObserver = enabled;
	}

	// TheSuperHackers @todo Add checkbox ?
	{
		Bool enabled = pref->getArchiveReplaysEnabled();
		(*pref)["ArchiveReplays"] = enabled ? "yes" : "no";
		TheRecorder->setArchiveEnabled(enabled);
	}

	//-------------------------------------------------------------------------------------------------
	// scroll speed val
	val = GadgetSliderGetPosition(sliderScrollSpeed);
	if(val > 0)
	{
		TheWritableGlobalData->m_keyboardScrollFactor = val/100.0f;
		DEBUG_LOG(("Scroll Speed val %d, keyboard scroll factor %f", val, TheGlobalData->m_keyboardScrollFactor));
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["ScrollFactor"] = prefString;
	}

	//-------------------------------------------------------------------------------------------------
	// max camera height
	if (checkMaxCameraHeight && textEntryMaxCameraHeight)
	{
		const Bool useCustom = GadgetCheckBoxIsChecked( checkMaxCameraHeight );
		(*pref)["UseCustomMaxCameraHeight"] = useCustom ? "yes" : "no";
		if (useCustom)
		{
			const Int height = getEntryInt( textEntryMaxCameraHeight, OptionPreferences::MaxCameraHeightMin, OptionPreferences::MaxCameraHeightMax, (Int)pref->getMaxCameraHeight() );
			AsciiString prefString;
			prefString.format( "%d", height );
			(*pref)["MaxCameraHeight"] = prefString;
		}
		showMaxCameraHeight( useCustom, (Int)pref->getMaxCameraHeight() );

		// LAN and online games play the host's limit until they end
		const GameMode mode = TheGameLogic->getGameMode();
		if (mode != GAME_LAN && mode != GAME_INTERNET)
		{
			const Real height = pref->getMaxCameraHeight();
			TheWritableGlobalData->m_maxCameraHeight = height;
			if (TheTacticalView && TheGameLogic->isInGame() && mode != GAME_SHELL)
			{
				TheTacticalView->setMaxHeightAboveGround( height );
				TheTacticalView->setHeightAboveGround( TheTacticalView->getHeightAboveGround() );
			}
		}
	}

	//-------------------------------------------------------------------------------------------------
	// game options
	if (hasGameOptions())
	{
		Int idx = clamp( 0, getComboPos( comboBoxHealthBars, pref->getHealthBarDisplayMode() ), (Int)HealthBarDisplayMode_Count - 1 );
		(*pref)["HealthBarDisplayMode"] = HealthBarModeNames[idx];
		TheWritableGlobalData->m_healthBarDisplayMode = idx;

		idx = clamp( 0, getComboPos( comboBoxAlliedDecals, pref->getAlliedDecalMode() ), (Int)AlliedDecalMode_Count - 1 );
		(*pref)["AlliedDecalMode"] = AlliedDecalModeNames[idx];
		TheWritableGlobalData->m_alliedDecalMode = idx;

		// TheSuperHackers @feature A changed HouseColorMode recolors what is already on the map.
		idx = clamp( 0, getComboPos( comboBoxHouseColors, pref->getHouseColorMode() ), (Int)HouseColorMode_Count - 1 );
		(*pref)["HouseColorMode"] = HouseColorModeNames[idx];
		if( TheGlobalData->m_houseColorMode != idx )
		{
			TheWritableGlobalData->m_houseColorMode = idx;
			rts::refreshHouseColors();
		}

		idx = clamp( 0, getComboPos( comboBoxBuildTimers, pref->getBuildTimerDisplayMode() ), (Int)BuildTimerDisplayMode_Count - 1 );
		(*pref)["BuildTimerDisplayMode"] = BuildTimerModeNames[idx];
		TheWritableGlobalData->m_buildTimerDisplayMode = idx;

		idx = clamp( 0, getComboPos( comboBoxActionLines, pref->getActionLineMode() ), (Int)ActionLineMode_Count - 1 );
		(*pref)["ActionLines"] = ActionLineModeNames[idx];
		TheWritableGlobalData->m_actionLineMode = idx;

		idx = clamp( 0, getComboPos( comboBoxCastMode, pref->getCastMode() ), (Int)CastMode_Count - 1 );
		(*pref)["CastMode"] = CastModeNames[idx];
		TheWritableGlobalData->m_castMode = idx;

		const Int savedPathTracing = pref->getPathTracingMode();
		for (Int i = 0; i < ARRAY_SIZE(BoolOptions); ++i)
		{
			const BoolOption &option = BoolOptions[i];
			const Bool on = getCheck( *option.check, (pref->*option.read)() );
			(*pref)[option.prefKey] = on ? "yes" : "no";
			TheWritableGlobalData->*option.field = on;
		}

		// path tracing keeps its mode in the one key. A layout with only the checkbox turns on the mode last saved.
		{
			Int mode = TheGlobalData->m_usePathTracing ? max( savedPathTracing, 1 ) : 0;
			if (comboBoxPathTracing)
			{
				mode = clamp( 0, getComboPos( comboBoxPathTracing, savedPathTracing ), (Int)ARRAY_SIZE(PathTracingModeNames) - 1 );
			}
			(*pref)["PathTracing"] = PathTracingModeNames[mode];
			TheWritableGlobalData->m_usePathTracing = mode != 0;
			TheWritableGlobalData->m_pathTracingFast = mode == 2;
		}
		WW3D::Set_Low_Latency( TheGlobalData->m_lowLatency != FALSE );
		const Bool veterancyLevel = getCheck( checkVeterancyLevel, pref->getBool( "VeterancyLevel", FALSE ) );
		pref->setBool( "VeterancyLevel", veterancyLevel );
		if( TheControlBar )
		{
			TheControlBar->setPortraitVeterancyEnabled( veterancyLevel );
		}

		// the radar caches this when it is created, so it waits for the next launch
		const Bool large = getCheck( checkLargeBlips, pref->getRadarBlipSize() == RadarBlipSize_Large );
		(*pref)["BlipSize"] = large ? "Large" : "Small";
		TheWritableGlobalData->m_radarBlipSize = large ? RadarBlipSize_Large : RadarBlipSize_Small;

		// a hand-written layout stays until the player picks a preset
		const AsciiString storedLayout = pref->getGridHotkeyLayout();
		const Int storedIdx = gridLayoutIndex( storedLayout );
		idx = clamp( 0, getComboPos( comboBoxGridHotkeyLayout, storedIdx ), (Int)ARRAY_SIZE(GridHotkeyLayouts) - 1 );
		if (comboBoxGridHotkeyLayout && (idx != storedIdx || storedLayout.compareNoCase( GridHotkeyLayouts[storedIdx] ) == 0))
		{
			(*pref)["GridHotkeyLayout"] = GridHotkeyLayouts[idx];
			TheWritableGlobalData->m_gridHotkeyLayout = GridHotkeyLayouts[idx];
		}

		// The command bar bakes the hotkeys in when it fills, so make it fill again
		if (TheControlBar)
		{
			TheControlBar->markUIDirty();
		}

		TheWritableGlobalData->m_keyboardOverlayColor = saveColorEntries( "KeyboardOverlay", GameMakeColor( 255, 255, 255, 255 ),
			textEntryKeyboardOverlayRed, textEntryKeyboardOverlayGreen, textEntryKeyboardOverlayBlue, nullptr );
		TheWritableGlobalData->m_keyboardOverlayBackdropColor = saveColorEntries( "KeyboardOverlayBackdrop", GameMakeColor( 0, 0, 0, 128 ),
			textEntryKeyboardOverlayBackdropRed, textEntryKeyboardOverlayBackdropGreen, textEntryKeyboardOverlayBackdropBlue, textEntryKeyboardOverlayBackdropOpacity );

		const Int percent = getEntryInt( textEntryBloomStrength, 0, 100, bloomPercent( pref->getBloomStrength() ) );
		AsciiString prefString;
		prefString.format( "%.2f", percent / 100.0f );
		(*pref)["BloomStrength"] = prefString;
		TheWritableGlobalData->m_bloomStrength = percent / 100.0f;

		const Int threshold = getEntryInt( textEntryBloomThreshold, 0, 100, bloomPercent( pref->getBloomThreshold() ) );
		prefString.format( "%.2f", threshold / 100.0f );
		(*pref)["BloomThreshold"] = prefString;
		TheWritableGlobalData->m_bloomThreshold = threshold / 100.0f;

		const Int size = getEntryInt( textEntryBloomSize, 25, 400, bloomPercent( pref->getBloomSize() ) );
		prefString.format( "%.2f", size / 100.0f );
		(*pref)["BloomSize"] = prefString;
		TheWritableGlobalData->m_bloomSize = size / 100.0f;
	}

	//-------------------------------------------------------------------------------------------------
	// shadow map resolution, which the shadow map takes up on its next frame
	if (comboBoxShadows)
	{
		const Int resolution = ShadowMapResolutions[getComboPos( comboBoxShadows, ARRAY_SIZE(ShadowMapResolutions) - 1 )];
		AsciiString prefString;
		prefString.format( "%d", resolution );
		(*pref)["ShadowMapResolution"] = prefString;
		TheWritableGlobalData->m_shadowMapResolution = resolution;
	}

	//-------------------------------------------------------------------------------------------------
	// supersampling, which rebuilds the device like a sample count change and falls back where the target cannot be made
	if (comboBoxSuperSampling)
	{
		Int percent = SuperSamplingPercents[getComboPos( comboBoxSuperSampling, 0 )];
		// 16K lasts for this run alone, so choosing it leaves the file's percent as it is
		const Bool saved = percent != WW3D::SUPER_SAMPLING_16K;
		if (percent != WW3D::Get_Super_Sampling())
		{
			WW3D::Set_Super_Sampling( percent );
			WW3D::Set_Render_Device( -1, -1, -1, -1, -1, false, true, true );
			percent = WW3D::Get_Super_Sampling();
			TheWritableGlobalData->m_antiAliasLevel = (UnsignedInt)WW3D::Get_MSAA_Mode();
		}
		if (saved)
		{
			AsciiString prefString;
			prefString.format( "%d", percent );
			(*pref)["SuperSampling"] = prefString;
		}
		TheWritableGlobalData->m_superSampling = percent;
	}

	//-------------------------------------------------------------------------------------------------
	// draw scroll anchor
	{
		if( TheInGameUI->getDrawRMBScrollAnchor() )
				(*pref)["DrawScrollAnchor"] = "yes";
		else
				(*pref)["DrawScrollAnchor"] = "no";
	}

	//-------------------------------------------------------------------------------------------------
	// move scroll anchor
	{
		if( TheInGameUI->getMoveRMBScrollAnchor() )
				(*pref)["MoveScrollAnchor"] = "yes";
		else
				(*pref)["MoveScrollAnchor"] = "no";
	}

	//-------------------------------------------------------------------------------------------------
	// slider music volume
	val = GadgetSliderGetPosition(sliderMusicVolume);
	if(val != -1)
	{
    AsciiString prefString;
    prefString.format("%d", val);
    (*pref)["MusicVolume"] = prefString;
    TheAudio->setVolume(val / 100.0f, (AudioAffect) (AudioAffect_Music | AudioAffect_SystemSetting));
	}

	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	val = GadgetSliderGetPosition(sliderSFXVolume);
	if(val != -1)
	{
		//Both 2D and 3D sound effects are sharing the same slider. However, there is a
		//relative slider that gets applied to one of these values to lower that sound volume.
		Real sound2DVolume = val / 100.0f;
		Real sound3DVolume = val / 100.0f;
		Real relative2DVolume = TheAudio->getAudioSettings()->m_relative2DVolume;
		relative2DVolume = MIN( 1.0f, MAX( -1.0, relative2DVolume ) );
		if( relative2DVolume < 0.0f )
		{
			//Lower the 2D volume
			sound2DVolume *= 1.0f + relative2DVolume;
		}
		else
		{
			//Lower the 3D volume
			sound3DVolume *= 1.0f - relative2DVolume;
		}

		//Apply the sound volumes in the audio system now.
    TheAudio->setVolume( sound2DVolume, (AudioAffect) (AudioAffect_Sound | AudioAffect_SystemSetting) );
		TheAudio->setVolume( sound3DVolume, (AudioAffect) (AudioAffect_Sound3D | AudioAffect_SystemSetting) );

		//Save the settings in the options.ini.
    AsciiString prefString;
    prefString.format("%d", REAL_TO_INT( sound2DVolume * 100.0f ) );
    (*pref)["SFXVolume"] = prefString;
    prefString.format("%d", REAL_TO_INT( sound3DVolume * 100.0f ) );
		(*pref)["SFX3DVolume"] = prefString;
	}

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	val = GadgetSliderGetPosition(sliderVoiceVolume);
	if(val != -1)
	{
    AsciiString prefString;
    prefString.format("%d", val);
    (*pref)["VoiceVolume"] = prefString;
    TheAudio->setVolume(val / 100.0f, (AudioAffect) (AudioAffect_Speech | AudioAffect_SystemSetting));
	}

	//-------------------------------------------------------------------------------------------------
	// Money tick volume
	// TheSuperHackers @todo Add options slider ?
	{
		val = pref->getMoneyTransactionVolume();
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["MoneyTransactionVolume"] = prefString;
		TheAudio->friend_getAudioSettings()->m_preferredMoneyTransactionVolume = val / 100.0f;
	}

 	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	val = GadgetSliderGetPosition(sliderGamma);
 	if(val != -1)
 	{
		Real gammaval=1.0f;
		//generate a value between 0.6 and 2.0.
		if (val < 50)
		{	//darker gamma
			if (val <= 0)
				gammaval = 0.6f;
			else
				gammaval=1.0f-(0.4f) * (Real)(50-val)/50.0f;
		}
		else
		if (val > 50)
			gammaval=1.0f+(1.0f) * (Real)(val-50)/50.0f;

 		AsciiString prefString;
 		prefString.format("%d", val);
 		(*pref)["Gamma"] = prefString;

		if (TheGlobalData->m_displayGamma != gammaval)
		{	TheWritableGlobalData->m_displayGamma = gammaval;
			TheDisplay->setGamma(TheGlobalData->m_displayGamma,0.0f, 1.0f, FALSE);
		}
 	}

	//-------------------------------------------------------------------------------------------------
	// Set Network Latency Font Size
	val = pref->getNetworkLatencyFontSize();
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["NetworkLatencyFontSize"] = prefString;
		TheInGameUI->refreshNetworkLatencyResources();
	}

	//-------------------------------------------------------------------------------------------------
	// Set Render FPS Font Size
	val = pref->getRenderFpsFontSize();
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["RenderFpsFontSize"] = prefString;
		TheInGameUI->refreshRenderFpsResources();
	}

	//-------------------------------------------------------------------------------------------------
	// Set System Time Font Size
	val = pref->getSystemTimeFontSize(); // TheSuperHackers @todo replace with options input when applicable
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["SystemTimeFontSize"] = prefString;
		TheInGameUI->refreshSystemTimeResources();
	}

	//-------------------------------------------------------------------------------------------------
	// Set Game Time Font Size
	val = pref->getGameTimeFontSize(); // TheSuperHackers @todo replace with options input when applicable
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["GameTimeFontSize"] = prefString;
		TheInGameUI->refreshGameTimeResources();
	}

	//-------------------------------------------------------------------------------------------------
	// Set Player Info List Font Size
	val = pref->getPlayerInfoListFontSize();
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["PlayerInfoListFontSize"] = prefString;
		TheInGameUI->refreshPlayerInfoListResources();
	}

#if defined(GENERALS_ONLINE)
	//-------------------------------------------------------------------------------------------------
	// Set Observer Notification Font Size
	val = pref->getObserverNotificationFontSize();
	if (val >= 0)
	{
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["ObserverNotificationFontSize"] = prefString;
		TheInGameUI->refreshObserverNotificationResources();
	}
#endif

	//-------------------------------------------------------------------------------------------------
	// Set User Font Scaling Percentage
	val = pref->getResolutionFontAdjustment() * 100.0f; // TheSuperHackers @todo replace with options input when applicable
	if (val >= 0 || val == -100)
	{
		AsciiString prefString;
		prefString.format("%d", REAL_TO_INT( val ) );
		(*pref)["ResolutionFontAdjustment"] = prefString;
		TheGlobalLanguageData->m_userResolutionFontSizeAdjustment = (Real)val / 100.0f;
	}

	//-------------------------------------------------------------------------------------------------
	// Set Money Per Minute
	{
		Bool show = pref->getShowMoneyPerMinute();
		AsciiString prefString;
		prefString = show ? "yes" : "no";
		(*pref)["ShowMoneyPerMinute"] = prefString;
		TheWritableGlobalData->m_showMoneyPerMinute = show;
	}

	//-------------------------------------------------------------------------------------------------
	// Set Game Window Transition Speed Multiplier
	{
		Real speed = pref->getGameWindowTransitionSpeedMultiplier();
		AsciiString prefString;
		prefString.format("%g", speed);
		(*pref)["GameWindowTransitionSpeedMultiplier"] = prefString;
		TheWritableGlobalData->m_gameWindowTransitionSpeedMultiplier = speed;
	}

	//-------------------------------------------------------------------------------------------------
	// Set JPEG screenshot quality
	{
		Int quality = pref->getJpegQuality();
		AsciiString prefString;
		prefString.format("%d", quality);
		(*pref)["JpegQuality"] = prefString;
		TheWritableGlobalData->m_jpegQuality = quality;
	}

	//-------------------------------------------------------------------------------------------------
	// Resolution
	//
	// TheSuperHackers @bugfix xezon 12/06/2025 Now performs the resolution change at the very end of
	// processing all the options. This is necessary, because recreating the Shell will destroy the
	// Options Menu and therefore prevent any further ui gadget interactions afterwards.

	GadgetComboBoxGetSelectedPos( comboBoxResolution, &index );
	Int xres, yres, bitDepth;

	oldDispSettings.xRes = TheDisplay->getWidth();
	oldDispSettings.yRes = TheDisplay->getHeight();
	oldDispSettings.bitDepth = TheDisplay->getBitDepth();
	oldDispSettings.windowed = TheDisplay->getWindowed();
	oldDispSettings.framed = TheGlobalData->m_windowed;

	if (comboBoxResolution && (comboBoxResolution->winGetEnabled() || (checkBorderlessWindow && checkBorderlessWindow->winGetEnabled())) && index < TheDisplay->getDisplayModeCount() && index >= 0)
	{
		TheDisplay->getDisplayModeDescription(index,&xres,&yres,&bitDepth);

		const Bool framed = getCheck( checkWindowed, TheGlobalData->m_windowed );
		const Bool borderless = !framed && getCheck( checkBorderlessWindow, TheGlobalData->m_borderlessWindow );
		const Bool windowed = framed || borderless;

		if (TheGlobalData->m_xResolution != xres || TheGlobalData->m_yResolution != yres || windowed != TheDisplay->getWindowed() || framed != TheGlobalData->m_windowed)
		{
			// the window frame follows m_windowed, so it must change before the mode does
			TheWritableGlobalData->m_windowed = framed;
			if (TheDisplay->setDisplayMode(xres,yres,bitDepth,windowed))
			{
				dispChanged = TRUE;
				TheWritableGlobalData->m_xResolution = xres;
				TheWritableGlobalData->m_yResolution = yres;
				TheWritableGlobalData->m_borderlessWindow = borderless;
				(*pref)["BorderlessWindow"] = borderless ? "yes" : "no";
				(*pref)["Windowed"] = framed ? "yes" : "no";

				TheHeaderTemplateManager->onResolutionChanged();
				TheMouse->onResolutionChanged();

				//Save new settings for a dialog box confirmation after options are accepted
				newDispSettings.xRes = xres;
				newDispSettings.yRes = yres;
				newDispSettings.bitDepth = bitDepth;
				newDispSettings.windowed = TheDisplay->getWindowed();
				newDispSettings.framed = framed;

				AsciiString prefString;
				prefString.format("%d %d", xres, yres );
				(*pref)["Resolution"] = prefString;

				// The control bar goes first so its full screen roots stay below the rebuilt shell, as at launch
				TheInGameUI->recreateControlBar();
				TheShell->recreateWindowLayouts();
				TheInGameUI->refreshCustomUiResources();
			}
			else
			{
				TheWritableGlobalData->m_windowed = oldDispSettings.framed;
			}
		}
	}

	// MUST NEVER ADD ANOTHER OPTION HERE AT THE END !
}

static void DestroyOptionsLayout() {

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_CLOSED);

	TheShell->destroyOptionsLayout();
	OptionsLayout = nullptr;
}

static void showAdvancedOptions()
{
	// a tabbed layout keeps the custom detail toggles on a page of their own
	if (OptionsTabs::isOn())
	{
		OptionsTabs::showPageOf( check3DShadows );
		return;
	}
	WinAdvancedDisplay->winHide(FALSE);
}

static Bool isCustomDetailControl( GameWindow *control )
{
	GameWindow *const controls[] =
	{
		check3DShadows, check2DShadows, checkCloudShadows, checkGroundLighting, checkSmoothWater, checkExtraAnimations,
		checkNoDynamicLod, checkHeatEffects, checkBuildingOcclusion, checkProps, sliderTextureResolution, sliderParticleCap,
	};
	for (Int i = 0; i < ARRAY_SIZE(controls); ++i)
	{
		if (control != nullptr && controls[i] == control)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// A tabbed layout shows the custom detail toggles all the time, so changing one picks the Custom preset
static void selectCustomDetailFor( GameWindow *control )
{
	if (OptionsTabs::isOn() && comboBoxDetail && isCustomDetailControl( control ))
	{
		ignoreSelected = TRUE;
		GadgetComboBoxSetSelectedPos( comboBoxDetail, STATIC_GAME_LOD_CUSTOM );
		ignoreSelected = FALSE;
	}
}

static void acceptAdvancedOptions()
{
	WinAdvancedDisplay->winHide(TRUE);
}

static void cancelAdvancedOptions()
{
	//restore the detail selection back to initial state
	GadgetComboBoxSetSelectedPos(comboBoxDetail, (Int)TheGameLODManager->getStaticLODLevel());

	WinAdvancedDisplay->winHide(TRUE);
}

// The panel is not modal, so the buttons outside its rect are switched off while it is up
static void enableMainButtons( Bool enable )
{
	enableWindow( buttonMainAccept, enable );
	enableWindow( buttonMainBack, enable );
	enableWindow( buttonMainDefaults, enable );
}

static void showPanel( GameWindow *panel )
{
	if (panel)
	{
		panel->winHide( FALSE );
		enableMainButtons( FALSE );
	}
}

static void hidePanel( GameWindow *panel )
{
	if (panel)
	{
		panel->winHide( TRUE );
		enableMainButtons( TRUE );
	}
}

static Bool isPanelOpen( GameWindow *panel )
{
	return panel && !panel->winIsHidden();
}

static void cancelPanel( GameWindow *panel, void (*populate)() )
{
	ignoreSelected = TRUE;
	populate();
	ignoreSelected = FALSE;
	hidePanel( panel );
}

static GameWindow *findOptionsWindow( const char *name )
{
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( name ) );
}

static GameWindow *findOptionsWindow( const char *name, NameKeyType &id )
{
	id = TheNameKeyGenerator->nameToKey( name );
	return TheWindowManager->winGetWindowFromId( nullptr, id );
}

// The debug view checkboxes are developer aids, so only a cheats build shows them. The layout
// still holds them; a build without cheats hides the box and forgets it, and the option then
// reads and saves as off.
static GameWindow *findDebugViewCheck( const char *name )
{
	GameWindow *check = findOptionsWindow( name );
#if !(defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
	if (check)
	{
		check->winHide( TRUE );
		check = nullptr;
	}
#endif
	return check;
}

static void setLabelText( const char *name, const char *label )
{
	GameWindow *window = findOptionsWindow( name );
	if (window)
	{
		GadgetStaticTextSetText( window, TheGameText->fetch( label ) );
	}
}

static void setCheckText( GameWindow *check, const char *label, const char *tip )
{
	if (check)
	{
		GadgetCheckBoxSetText( check, TheGameText->fetch( label ) );
		check->winSetTooltip( TheGameText->fetch( tip ) );
	}
}

static GameWindow *createCheckBelow( GameWindow *anchor, const char *name, const char *labelKey, const wchar_t *label, const char *tipKey, const wchar_t *tip )
{
	GameWindow *check = findOptionsWindow( name );
	if (!check && WinGameOptions && anchor && anchor->winGetParent() == WinGameOptions)
	{
		Int x, y, width, height, panelWidth, panelHeight;
		anchor->winGetPosition( &x, &y );
		anchor->winGetSize( &width, &height );
		WinGameOptions->winGetSize( &panelWidth, &panelHeight );
		const Int step = max(height * 21 / 20, 1);
		check = CreateCheckBoxLike( anchor, WinGameOptions, x, y + step,
			TheGameText->fetchOrSubstitute( labelKey, label ), name );
		if (check)
		{
			for (GameWindow *child = WinGameOptions->winGetChild(); child; child = child->winGetNext())
			{
				Int childX, childY;
				child->winGetPosition( &childX, &childY );
				if (child != check && childY >= y + height)
				{
					child->winSetPosition( childX, childY + step );
				}
			}
			WinGameOptions->winSetSize( panelWidth, panelHeight + step );
		}
	}
	if (check)
	{
		GadgetCheckBoxSetText( check, TheGameText->fetchOrSubstitute( labelKey, label ) );
		check->winSetTooltip( TheGameText->fetchOrSubstitute( tipKey, tip ) );
	}
	return check;
}

static GameWindow *createFacingDragCheck()
{
	return createCheckBelow( checkSmartCommandGroup, "OptionsMenu.wnd:CheckFacingDrag", "GUI:FacingDrag", L"Drag to Face",
		"TOOLTIP:FacingDrag", L"Hold both mouse buttons and drag to set the selected units' destination and facing." );
}

static GameWindow *createPathTracingCheck()
{
	GameWindow *check = findOptionsWindow( "OptionsMenu.wnd:CheckPathTracing" );
	GameWindow *anchor = checkAmbientOcclusion;
	if (check == nullptr && WinShaders != nullptr && anchor != nullptr && anchor->winGetParent() == WinShaders)
	{
		Int x, y, width, height, panelWidth, panelHeight;
		anchor->winGetPosition( &x, &y );
		anchor->winGetSize( &width, &height );
		WinShaders->winGetSize( &panelWidth, &panelHeight );
		const Int step = max(height * 23 / 20, 1);
		for (y += step; y + height < panelHeight && check == nullptr; y += step)
		{
			Bool free = TRUE;
			for (GameWindow *child = WinShaders->winGetChild(); child != nullptr; child = child->winGetNext())
			{
				Int cx, cy, cw, ch;
				child->winGetPosition( &cx, &cy );
				child->winGetSize( &cw, &ch );
				if (ch <= height * 2 && x < cx + cw && x + width > cx && y < cy + ch && y + height > cy)
				{
					free = FALSE;
					break;
				}
			}
			if (free)
			{
				WinInstanceData data;
				const WinInstanceData *look = anchor->winGetInstanceData();
				data.m_id = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckPathTracing" );
				data.m_style = look->m_style;
				data.m_enabledText = look->m_enabledText;
				data.m_disabledText = look->m_disabledText;
				data.m_hiliteText = look->m_hiliteText;
				for (Int i = 0; i < MAX_DRAW_DATA; i++)
				{
					data.m_enabledDrawData[i] = look->m_enabledDrawData[i];
					data.m_disabledDrawData[i] = look->m_disabledDrawData[i];
					data.m_hiliteDrawData[i] = look->m_hiliteDrawData[i];
				}
				check = TheWindowManager->gogoGadgetCheckbox( WinShaders, anchor->winGetStatus() & ~WIN_STATUS_HIDDEN,
					x, y, width, height, &data, anchor->winGetFont(), FALSE );
			}
		}
	}
	if (check != nullptr)
	{
		GadgetCheckBoxSetText( check, TheGameText->fetch( "GUI:PathTracing" ) );
		check->winSetTooltip( TheGameText->fetch( "TOOLTIP:PathTracing" ) );
	}
	return check;
}

static void setTooltip( GameWindow *window, const char *tip )
{
	if (window)
	{
		window->winSetTooltip( TheGameText->fetch( tip ) );
	}
}

// Entry i reads label labelPrefix<i>; the layout's MAXDISPLAY sizes the list, as it does for the Resolution combo
static void addComboEntries( GameWindow *combo, const char *labelPrefix, Int count )
{
	if (!combo)
	{
		return;
	}
	const Color white = GameMakeColor( 255, 255, 255, 255 );
	GadgetComboBoxReset( combo );
	for (Int i = 0; i < count; ++i)
	{
		AsciiString label;
		label.format( "%s%d", labelPrefix, i );
		GadgetComboBoxAddEntry( combo, TheGameText->fetch( label.str() ), white );
	}
}

// Finds the panel controls, gives them their words and fills the combo boxes
static void initGameOptionsWindows()
{
	WinGameOptions = findOptionsWindow( "OptionsMenu.wnd:WinGameOptions", WinGameOptionsID );
	ButtonGameOptions = findOptionsWindow( "OptionsMenu.wnd:ButtonGameOptions", ButtonGameOptionsID );

	// A layout without the panel keeps the old menu, minus the button that would open it
	if (!hasGameOptions())
	{
		if (ButtonGameOptions)
		{
			ButtonGameOptions->winHide( TRUE );
		}
		return;
	}

	ButtonGameOptionsAccept = findOptionsWindow( "OptionsMenu.wnd:ButtonGameOptionsAccept", ButtonGameOptionsAcceptID );
	ButtonGameOptionsCancel = findOptionsWindow( "OptionsMenu.wnd:ButtonGameOptionsBack", ButtonGameOptionsCancelID );
	buttonMainAccept = findOptionsWindow( "OptionsMenu.wnd:ButtonAccept" );
	buttonMainBack = findOptionsWindow( "OptionsMenu.wnd:ButtonBack" );
	buttonMainDefaults = findOptionsWindow( "OptionsMenu.wnd:ButtonDefaults" );

	comboBoxHealthBars = findOptionsWindow( "OptionsMenu.wnd:ComboBoxHealthBars" );
	comboBoxAlliedDecals = findOptionsWindow( "OptionsMenu.wnd:ComboBoxAlliedDecals" );
	comboBoxHouseColors = findOptionsWindow( "OptionsMenu.wnd:ComboBoxHouseColors" );
	comboBoxBuildTimers = findOptionsWindow( "OptionsMenu.wnd:ComboBoxBuildTimers" );
	comboBoxActionLines = findOptionsWindow( "OptionsMenu.wnd:ComboBoxActionLines" );
	comboBoxCastMode = findOptionsWindow( "OptionsMenu.wnd:ComboBoxCastMode" );
	comboBoxTextureFilter = findOptionsWindow( "OptionsMenu.wnd:ComboBoxTextureFilter" );
	comboBoxAnisotropy = findOptionsWindow( "OptionsMenu.wnd:ComboBoxAnisotropy" );
	checkNumericalHealth = findOptionsWindow( "OptionsMenu.wnd:CheckNumericalHealth" );
	checkSmartPips = findOptionsWindow( "OptionsMenu.wnd:CheckSmartPips" );
	checkSelectionCircle = findOptionsWindow( "OptionsMenu.wnd:CheckSelectionCircle" );
	checkDefensesRangeCircle = findOptionsWindow( "OptionsMenu.wnd:CheckDefensesRangeCircle" );
	checkQueueBar = findOptionsWindow( "OptionsMenu.wnd:CheckQueueBar" );
	checkVeterancyLevel = findOptionsWindow( "OptionsMenu.wnd:CheckVeterancyLevel" );
	checkObjectDecals = findOptionsWindow( "OptionsMenu.wnd:CheckObjectDecals" );
	checkEasyMilitaryDrag = findOptionsWindow( "OptionsMenu.wnd:CheckEasyMilitaryDrag" );
	checkSmartSelection = findOptionsWindow( "OptionsMenu.wnd:CheckSmartSelection" );
	checkSmartSelectionUseMouse = findOptionsWindow( "OptionsMenu.wnd:CheckSmartSelectionUseMouse" );
	checkSmartCommandGroup = findOptionsWindow( "OptionsMenu.wnd:CheckSmartCommandGroup" );
	checkFacingDrag = createFacingDragCheck();
	checkBuildQueueBar = createCheckBelow( checkSmartCommandGroup, "OptionsMenu.wnd:CheckBuildQueueBar", "GUI:BuildQueueBar", L"Build Queue Bar",
		"TOOLTIP:BuildQueueBar", L"Show every queued unit and upgrade as cameos above the radar. Click one to select its producer." );
	checkNewRadar = findOptionsWindow( "OptionsMenu.wnd:CheckNewRadar" );
	checkLargeBlips = findOptionsWindow( "OptionsMenu.wnd:CheckLargeBlips" );
	checkGridHotkeys = findOptionsWindow( "OptionsMenu.wnd:CheckGridHotkeys", checkGridHotkeysID );
	comboBoxGridHotkeyLayout = findOptionsWindow( "OptionsMenu.wnd:ComboBoxGridHotkeyLayout" );
	checkGridHotkeyUnitOrders = findOptionsWindow( "OptionsMenu.wnd:CheckGridHotkeyUnitOrders" );
	checkKeyboardOverlay = findOptionsWindow( "OptionsMenu.wnd:CheckKeyboardOverlay" );
	checkKeyboardOverlayBackdrop = findOptionsWindow( "OptionsMenu.wnd:CheckKeyboardOverlayBackdrop", checkKeyboardOverlayBackdropID );
	textEntryKeyboardOverlayRed = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayRed" );
	textEntryKeyboardOverlayGreen = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayGreen" );
	textEntryKeyboardOverlayBlue = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayBlue" );
	textEntryKeyboardOverlayBackdropRed = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayBackdropRed" );
	textEntryKeyboardOverlayBackdropGreen = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayBackdropGreen" );
	textEntryKeyboardOverlayBackdropBlue = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayBackdropBlue" );
	textEntryKeyboardOverlayBackdropOpacity = findOptionsWindow( "OptionsMenu.wnd:TextEntryKeyboardOverlayBackdropOpacity" );
	checkBloom = findOptionsWindow( "OptionsMenu.wnd:CheckBloom", checkBloomID );
	textEntryBloomStrength = findOptionsWindow( "OptionsMenu.wnd:TextEntryBloomStrength" );
	textEntryBloomThreshold = findOptionsWindow( "OptionsMenu.wnd:TextEntryBloomThreshold" );
	textEntryBloomSize = findOptionsWindow( "OptionsMenu.wnd:TextEntryBloomSize" );
	checkBloomDebug = findDebugViewCheck( "OptionsMenu.wnd:CheckBloomDebug" );
	checkLaserRef = findOptionsWindow( "OptionsMenu.wnd:CheckLaserRef" );
	checkShadowMap = findOptionsWindow( "OptionsMenu.wnd:CheckShadowMap" );
	checkSpecular = findOptionsWindow( "OptionsMenu.wnd:CheckSpecular", checkSpecularID );
	checkSpecularDebug = findDebugViewCheck( "OptionsMenu.wnd:CheckSpecularDebug" );
	checkNormalMaps = findOptionsWindow( "OptionsMenu.wnd:CheckNormalMaps", checkNormalMapsID );
	checkNormalMapDebug = findDebugViewCheck( "OptionsMenu.wnd:CheckNormalMapDebug" );
	checkWaterReflections = findOptionsWindow( "OptionsMenu.wnd:CheckWaterReflections" );
	checkDynamicLights = findOptionsWindow( "OptionsMenu.wnd:CheckDynamicLights", checkDynamicLightsID );
	checkPixelLights = findOptionsWindow( "OptionsMenu.wnd:CheckPixelLights", checkPixelLightsID );
	checkDynamicLightsDebug = findDebugViewCheck( "OptionsMenu.wnd:CheckDynamicLightsDebug" );
	checkSoftParticles = findOptionsWindow( "OptionsMenu.wnd:CheckSoftParticles" );
	checkAmbientOcclusion = findOptionsWindow( "OptionsMenu.wnd:CheckAmbientOcclusion", checkAmbientOcclusionID );
	checkAmbientOcclusionDebug = findDebugViewCheck( "OptionsMenu.wnd:CheckAmbientOcclusionDebug" );
	checkHeightBlend = findOptionsWindow( "OptionsMenu.wnd:CheckHeightBlend" );
	checkHQSky = findOptionsWindow( "OptionsMenu.wnd:CheckHQSky" );
	checkShorelineFoam = findOptionsWindow( "OptionsMenu.wnd:CheckShorelineFoam" );
	checkVSync = findOptionsWindow( "OptionsMenu.wnd:CheckVSync" );
	checkLowLatency = findOptionsWindow( "OptionsMenu.wnd:CheckLowLatency" );
	checkSmoothUnitMotion = findOptionsWindow( "OptionsMenu.wnd:CheckSmoothUnitMotion" );

	if (ButtonGameOptions)
	{
		GadgetButtonSetText( ButtonGameOptions, TheGameText->fetch( "GUI:GameOptions" ) );
	}
	setLabelText( "OptionsMenu.wnd:GameOptionsTitle", "GUI:GameOptions" );
	setLabelText( "OptionsMenu.wnd:DisplayGroupLabel", "GUI:GameOptionsDisplay" );
	setLabelText( "OptionsMenu.wnd:HealthBarsLabel", "GUI:HealthBars" );
	setLabelText( "OptionsMenu.wnd:AlliedDecalsLabel", "GUI:AlliedDecals" );
	setLabelText( "OptionsMenu.wnd:HouseColorsLabel", "GUI:HouseColors" );
	setLabelText( "OptionsMenu.wnd:BuildTimersLabel", "GUI:BuildTimers" );
	setLabelText( "OptionsMenu.wnd:ActionLinesLabel", "GUI:ActionLines" );
	setLabelText( "OptionsMenu.wnd:InputGroupLabel", "GUI:GameOptionsInput" );
	setLabelText( "OptionsMenu.wnd:CastModeLabel", "GUI:CastMode" );
	setLabelText( "OptionsMenu.wnd:RadarGroupLabel", "GUI:GameOptionsRadar" );
	setLabelText( "OptionsMenu.wnd:GraphicsGroupLabel", "GUI:GameOptionsGraphics" );
	setLabelText( "OptionsMenu.wnd:TextureFilterLabel", "GUI:TextureFilter" );
	setLabelText( "OptionsMenu.wnd:AnisotropyLabel", "GUI:Anisotropy" );
	setLabelText( "OptionsMenu.wnd:CameraGroupLabel", "GUI:GameOptionsCamera" );
	setLabelText( "OptionsMenu.wnd:GridHotkeysGroupLabel", "GUI:GameOptionsGridHotkeys" );
	setLabelText( "OptionsMenu.wnd:GridHotkeyLayoutLabel", "GUI:GridHotkeyLayout" );
	setLabelText( "OptionsMenu.wnd:KeyboardOverlayGroupLabel", "GUI:GameOptionsKeyboardOverlay" );
	setLabelText( "OptionsMenu.wnd:KeyboardOverlayColorLabel", "GUI:KeyboardOverlayColor" );
	setLabelText( "OptionsMenu.wnd:KeyboardOverlayBackdropColorLabel", "GUI:KeyboardOverlayBackdropColor" );
	setLabelText( "OptionsMenu.wnd:KeyboardOverlayBackdropOpacityLabel", "GUI:KeyboardOverlayBackdropOpacity" );
	setLabelText( "OptionsMenu.wnd:BloomGroupLabel", "GUI:GameOptionsBloom" );
	setLabelText( "OptionsMenu.wnd:BloomStrengthLabel", "GUI:BloomStrength" );
	setLabelText( "OptionsMenu.wnd:BloomThresholdLabel", "GUI:BloomThreshold" );
	setLabelText( "OptionsMenu.wnd:BloomSizeLabel", "GUI:BloomSize" );

	setCheckText( checkNumericalHealth, "GUI:NumericalHealth", "TOOLTIP:NumericalHealth" );
	setCheckText( checkSmartPips, "GUI:SmartPips", "TOOLTIP:SmartPips" );
	setCheckText( checkSelectionCircle, "GUI:SelectionCircle", "TOOLTIP:SelectionCircle" );
	setCheckText( checkDefensesRangeCircle, "GUI:DefensesRangeCircle", "TOOLTIP:DefensesRangeCircle" );
	setCheckText( checkQueueBar, "GUI:QueueBar", "TOOLTIP:QueueBar" );
	setCheckText( checkVeterancyLevel, "GUI:VeterancyLevel", "TOOLTIP:VeterancyLevel" );
	setCheckText( checkObjectDecals, "GUI:ObjectDecals", "TOOLTIP:ObjectDecals" );
	setCheckText( checkEasyMilitaryDrag, "GUI:EasyMilitaryDrag", "TOOLTIP:EasyMilitaryDrag" );
	setCheckText( checkSmartSelection, "GUI:SmartSelection", "TOOLTIP:SmartSelection" );
	setCheckText( checkSmartSelectionUseMouse, "GUI:SmartSelectionUseMouse", "TOOLTIP:SmartSelectionUseMouse" );
	setCheckText( checkSmartCommandGroup, "GUI:SmartCommandGroup", "TOOLTIP:SmartCommandGroup" );
	setCheckText( checkNewRadar, "GUI:NewRadar", "TOOLTIP:NewRadar" );
	setCheckText( checkLargeBlips, "GUI:LargeBlips", "TOOLTIP:LargeBlips" );
	setCheckText( checkGridHotkeys, "GUI:GridHotkeys", "TOOLTIP:GridHotkeys" );
	if (checkGridHotkeyUnitOrders)
	{
		GadgetCheckBoxSetText( checkGridHotkeyUnitOrders, TheGameText->FETCH_OR_SUBSTITUTE( "GUI:GridHotkeyUnitOrders", L"Unit orders keep their keys" ) );
		checkGridHotkeyUnitOrders->winSetTooltip( TheGameText->FETCH_OR_SUBSTITUTE( "TOOLTIP:GridHotkeyUnitOrders",
			L"Stop, guard, attack move and evacuate keep their own letters instead of their slot's grid key" ) );
	}
	setCheckText( checkKeyboardOverlay, "GUI:KeyboardOverlay", "TOOLTIP:KeyboardOverlay" );
	setCheckText( checkKeyboardOverlayBackdrop, "GUI:KeyboardOverlayBackdrop", "TOOLTIP:KeyboardOverlayBackdrop" );
	setCheckText( checkBloom, "GUI:Bloom", "TOOLTIP:Bloom" );
	setCheckText( checkBloomDebug, "GUI:BloomDebug", "TOOLTIP:BloomDebug" );
	setTooltip( textEntryBloomStrength, "TOOLTIP:BloomStrength" );
	setTooltip( textEntryBloomThreshold, "TOOLTIP:BloomThreshold" );
	setTooltip( textEntryBloomSize, "TOOLTIP:BloomSize" );
	setCheckText( checkLaserRef, "GUI:LaserRef", "TOOLTIP:LaserRef" );
	setCheckText( checkSpecular, "GUI:Specular", "TOOLTIP:Specular" );
	setCheckText( checkSpecularDebug, "GUI:SpecularDebug", "TOOLTIP:SpecularDebug" );
	setCheckText( checkNormalMaps, "GUI:NormalMaps", "TOOLTIP:NormalMaps" );
	setCheckText( checkNormalMapDebug, "GUI:NormalMapDebug", "TOOLTIP:NormalMapDebug" );
	setCheckText( checkWaterReflections, "GUI:WaterReflections", "TOOLTIP:WaterReflections" );
	setCheckText( checkShorelineFoam, "GUI:ShorelineFoam", "TOOLTIP:ShorelineFoam" );
	setCheckText( checkDynamicLights, "GUI:DynamicLights", "TOOLTIP:DynamicLights" );
	setCheckText( checkPixelLights, "GUI:PixelLights", "TOOLTIP:PixelLights" );
	setCheckText( checkDynamicLightsDebug, "GUI:DynamicLightsDebug", "TOOLTIP:DynamicLightsDebug" );
	setCheckText( checkLowLatency, "GUI:LowLatency", "TOOLTIP:LowLatency" );
	setCheckText( checkSmoothUnitMotion, "GUI:SmoothUnitMotion", "TOOLTIP:SmoothUnitMotion" );
	setCheckText( checkVSync, "GUI:VSync", "TOOLTIP:VSync" );
	setCheckText( checkAmbientOcclusion, "GUI:AmbientOcclusion", "TOOLTIP:AmbientOcclusion" );
	setCheckText( checkAmbientOcclusionDebug, "GUI:AmbientOcclusionDebug", "TOOLTIP:AmbientOcclusionDebug" );
	setCheckText( checkHeightBlend, "GUI:HeightBlend", "TOOLTIP:HeightBlend" );
	setCheckText( checkHQSky, "GUI:HQSky", "TOOLTIP:HQSky" );
	setCheckText( checkSoftParticles, "GUI:SoftParticles", "TOOLTIP:SoftParticles" );
	setCheckText( checkShadowMap, "GUI:ShadowMap", "TOOLTIP:ShadowMap" );

	setTooltip( comboBoxHealthBars, "TOOLTIP:HealthBars" );
	setTooltip( comboBoxAlliedDecals, "TOOLTIP:AlliedDecals" );
	setTooltip( comboBoxHouseColors, "TOOLTIP:HouseColors" );
	setTooltip( comboBoxBuildTimers, "TOOLTIP:BuildTimers" );
	setTooltip( comboBoxActionLines, "TOOLTIP:ActionLines" );
	setTooltip( comboBoxCastMode, "TOOLTIP:CastMode" );
	setTooltip( comboBoxTextureFilter, "TOOLTIP:TextureFilter" );
	setTooltip( comboBoxAnisotropy, "TOOLTIP:Anisotropy" );
	if (comboBoxGridHotkeyLayout)
	{
		comboBoxGridHotkeyLayout->winSetTooltip( TheGameText->FETCH_OR_SUBSTITUTE( "TOOLTIP:GridHotkeyRows",
			L"The keyboard rows that key the command bar's top and bottom rows" ) );
	}
	setTooltip( textEntryKeyboardOverlayRed, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayGreen, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayBlue, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayBackdropRed, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayBackdropGreen, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayBackdropBlue, "TOOLTIP:ColorChannel" );
	setTooltip( textEntryKeyboardOverlayBackdropOpacity, "TOOLTIP:ColorChannel" );

	addComboEntries( comboBoxHealthBars, "GUI:HealthBars", HealthBarDisplayMode_Count );
	addComboEntries( comboBoxAlliedDecals, "GUI:AlliedDecals", AlliedDecalMode_Count );
	addComboEntries( comboBoxHouseColors, "GUI:HouseColors", HouseColorMode_Count );
	addComboEntries( comboBoxBuildTimers, "GUI:BuildTimers", BuildTimerDisplayMode_Count );
	addComboEntries( comboBoxActionLines, "GUI:ActionLines", ActionLineMode_Count );
	addComboEntries( comboBoxCastMode, "GUI:CastMode", CastMode_Count );
	addComboEntries( comboBoxTextureFilter, "GUI:TextureFilter", TextureFilterClass::TEXTURE_FILTER_COUNT );
	addComboEntries( comboBoxAnisotropy, "GUI:Anisotropy", ARRAY_SIZE(AnisotropyLevels) );
	if (comboBoxGridHotkeyLayout)
	{
		GadgetComboBoxReset( comboBoxGridHotkeyLayout );
		for (Int i = 0; i < ARRAY_SIZE(GridHotkeyLayoutNames); ++i)
		{
			GadgetComboBoxAddEntry( comboBoxGridHotkeyLayout, UnicodeString( GridHotkeyLayoutNames[i] ), GameMakeColor( 255, 255, 255, 255 ) );
		}
	}
}

// Finds the Shaders panel, which a layout without it leaves out along with its button
static void initShadersWindows()
{
	WinShaders = findOptionsWindow( "OptionsMenu.wnd:WinShaders" );

	// A layout with the combo offers both modes, and one without it gets the checkbox made here.
	comboBoxPathTracing = findOptionsWindow( "OptionsMenu.wnd:ComboBoxPathTracing" );
	checkPathTracing = comboBoxPathTracing ? nullptr : createPathTracingCheck();
	GameWindow *button = findOptionsWindow( "OptionsMenu.wnd:ButtonShaders", ButtonShadersID );
	if (!WinShaders && !OptionsTabs::isOn())
	{
		return;
	}

	findOptionsWindow( "OptionsMenu.wnd:ButtonShadersAccept", ButtonShadersAcceptID );
	findOptionsWindow( "OptionsMenu.wnd:ButtonShadersBack", ButtonShadersCancelID );
	comboBoxShadows = findOptionsWindow( "OptionsMenu.wnd:ComboBoxShadows" );
	comboBoxSuperSampling = findOptionsWindow( "OptionsMenu.wnd:ComboBoxSuperSampling", comboBoxSuperSamplingID );

	// The layout hides the button, so an exe without this panel never shows it.
	if (button)
	{
		GadgetButtonSetText( button, TheGameText->fetch( "GUI:Shaders" ) );
		button->winHide( FALSE );
	}
	setLabelText( "OptionsMenu.wnd:ShadersTitle", "GUI:Shaders" );
	setLabelText( "OptionsMenu.wnd:ShadowsLabel", "GUI:ShadowMapResolution" );
	setTooltip( comboBoxShadows, "TOOLTIP:ShadowMapResolution" );
	addComboEntries( comboBoxShadows, "GUI:ShadowMapResolution", ARRAY_SIZE(ShadowMapResolutions) );

	setLabelText( "OptionsMenu.wnd:SuperSamplingLabel", "GUI:SuperSampling" );
	setTooltip( comboBoxSuperSampling, "TOOLTIP:SuperSampling" );
	addComboEntries( comboBoxSuperSampling, "GUI:SuperSampling", ARRAY_SIZE(SuperSamplingPercents) );

	setLabelText( "OptionsMenu.wnd:PathTracingLabel", "GUI:PathTracing" );
	setTooltip( comboBoxPathTracing, "TOOLTIP:PathTracing" );
	addComboEntries( comboBoxPathTracing, "GUI:PathTracing", ARRAY_SIZE(PathTracingModeNames) );

	// not every layout ships the panel hidden
	if (WinShaders)
	{
		WinShaders->winHide( TRUE );
	}
}

// The Hotkeys button: the layout's own, hidden for older exes, or one made beside Shaders
static void initHotkeysButton()
{
	ButtonHotkeysID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonHotkeys" );
	GameWindow *button = HotkeysMenuOptionsButton();
	if (button)
	{
		GadgetButtonSetText( button, HotkeysMenuText( "GUI:Hotkeys" ) );
		button->winSetTooltip( HotkeysMenuText( "TOOLTIP:Hotkeys" ) );
		button->winHide( FALSE );
	}
}

// TheSuperHackers @tweak Now prints additional version information in the version label.
static void initLabelVersion()
{
	NameKeyType versionID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:LabelVersion" );
	GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( nullptr, versionID );

	if (labelVersion)
	{
		if (TheVersion && TheGlobalData)
		{
			UnicodeString text = TheVersion->getUnicodeProductVersionHashString();
			GadgetStaticTextSetText( labelVersion, text );
		}
		else
		{
			labelVersion->winHide( TRUE );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the options menu */
//-------------------------------------------------------------------------------------------------
void OptionsMenuInit( WindowLayout *layout, void *userData )
{
	ignoreSelected = TRUE;
	if (TheGameEngine->getQuitting())
		return;
	OptionsLayout = layout;
	if (!pref)
	{
		pref = NEW OptionPreferences;
	}

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_OPENED);

	OptionsTabs::init();

	comboBoxLANIPID				 = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxIP" );
	comboBoxLANIP					 = TheWindowManager->winGetWindowFromId( nullptr,  comboBoxLANIPID);
	comboBoxOnlineIPID		 = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxOnlineIP" );
	comboBoxOnlineIP			 = TheWindowManager->winGetWindowFromId( nullptr,  comboBoxOnlineIPID);
	checkAlternateMouseID  = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckAlternateMouse" );
	checkAlternateMouse	   = TheWindowManager->winGetWindowFromId( nullptr, checkAlternateMouseID);
	checkRetaliationID		 = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:Retaliation" );
	checkRetaliation	     = TheWindowManager->winGetWindowFromId( nullptr, checkRetaliationID);
	checkDoubleClickAttackMoveID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckDoubleClickAttackMove" );
	checkDoubleClickAttackMove   = TheWindowManager->winGetWindowFromId( nullptr, checkDoubleClickAttackMoveID );
	sliderScrollSpeedID	   = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderScrollSpeed" );
	sliderScrollSpeed		   = TheWindowManager->winGetWindowFromId( nullptr,  sliderScrollSpeedID);
	// The layout may predate these controls, so they stay optional
	checkMaxCameraHeight = findOptionsWindow( "OptionsMenu.wnd:CheckMaxCameraHeight", checkMaxCameraHeightID );
	textEntryMaxCameraHeight = findOptionsWindow( "OptionsMenu.wnd:TextEntryMaxCameraHeight", textEntryMaxCameraHeightID );
	if (checkMaxCameraHeight)
	{
		GadgetCheckBoxSetText( checkMaxCameraHeight, TheGameText->fetch( "GUI:MaxCameraHeight" ) );
		checkMaxCameraHeight->winSetTooltip( TheGameText->fetch( "TOOLTIP:CheckMaxCameraHeight" ) );
	}
	if (textEntryMaxCameraHeight)
	{
		textEntryMaxCameraHeight->winSetTooltip( TheGameText->fetch( "TOOLTIP:MaxCameraHeight" ) );
	}
	checkBorderlessWindow = findOptionsWindow( "OptionsMenu.wnd:CheckBorderlessWindow", checkBorderlessWindowID );
	setCheckText( checkBorderlessWindow, "GUI:BorderlessWindow", "TOOLTIP:BorderlessWindow" );
	checkWindowed = findOptionsWindow( "OptionsMenu.wnd:CheckWindowed", checkWindowedID );
	setCheckText( checkWindowed, "GUI:Windowed", "TOOLTIP:Windowed" );
	comboBoxAntiAliasingID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxAntiAliasing" );
	comboBoxAntiAliasing   = TheWindowManager->winGetWindowFromId( nullptr, comboBoxAntiAliasingID );
	comboBoxResolutionID   = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxResolution" );
	comboBoxResolution     = TheWindowManager->winGetWindowFromId( nullptr, comboBoxResolutionID );
	comboBoxDetailID			 = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxDetail" );
	comboBoxDetail		   = TheWindowManager->winGetWindowFromId( nullptr, comboBoxDetailID );

	checkLanguageFilterID  = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckLanguageFilter" );
	checkLanguageFilter    = TheWindowManager->winGetWindowFromId( nullptr, checkLanguageFilterID );
	buttonFirewallRefreshID	= TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonFirewallRefresh" );
	buttonFirewallRefresh		= TheWindowManager->winGetWindowFromId( nullptr, buttonFirewallRefreshID);

#if ENABLE_GUI_HACKS
	// TheSuperHackers @tweak 26/07/2026 The Send Delay feature was obsoleted because it only worked around
	// a source port remapping bug in early 2000s Netgear firewalls. Hide the obsoleted UI element accordingly.
	GameWindow *checkSendDelay = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("OptionsMenu.wnd:CheckSendDelay"));
	if (checkSendDelay)
		checkSendDelay->winHide(TRUE);
#endif

	checkDrawAnchorID       = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckBoxDrawAnchor" );
	checkDrawAnchor				 = TheWindowManager->winGetWindowFromId( nullptr, checkDrawAnchorID);
	checkMoveAnchorID       = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckBoxMoveAnchor" );
	checkMoveAnchor				 = TheWindowManager->winGetWindowFromId( nullptr, checkMoveAnchorID);

	// Replay camera
	checkSaveCameraID      = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckBoxSaveCamera" );
	checkSaveCamera        = TheWindowManager->winGetWindowFromId( nullptr, checkSaveCameraID );
	checkUseCameraID       = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckBoxUseCamera" );
	checkUseCamera         = TheWindowManager->winGetWindowFromId( nullptr, checkUseCameraID );

//	// Speakers and 3-D Audio
//	checkAudioSurroundID   = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckAudioSurround" );
//	checkAudioSurround     = TheWindowManager->winGetWindowFromId( nullptr, checkAudioSurroundID );
//	checkAudioHardwareID   = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckAudioHardware" );
//	checkAudioHardware     = TheWindowManager->winGetWindowFromId( nullptr, checkAudioHardwareID );
//
	// Volume Controls
	sliderMusicVolumeID    = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderMusicVolume" );
	sliderMusicVolume      = TheWindowManager->winGetWindowFromId( nullptr, sliderMusicVolumeID );
	sliderSFXVolumeID      = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderSFXVolume" );
	sliderSFXVolume        = TheWindowManager->winGetWindowFromId( nullptr, sliderSFXVolumeID );
	sliderVoiceVolumeID    = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderVoiceVolume" );
	sliderVoiceVolume      = TheWindowManager->winGetWindowFromId( nullptr, sliderVoiceVolumeID );
 	sliderGammaID    = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderGamma" );
 	sliderGamma      = TheWindowManager->winGetWindowFromId( nullptr, sliderGammaID );

//	checkBoxLowTextureDetailID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckLowTextureDetail" );
//	checkBoxLowTextureDetail      = TheWindowManager->winGetWindowFromId( nullptr, checkBoxLowTextureDetailID );

	WinAdvancedDisplayID		= TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:WinAdvancedDisplayOptions" );
	WinAdvancedDisplay      = TheWindowManager->winGetWindowFromId( nullptr, WinAdvancedDisplayID );

	ButtonAdvancedAcceptID		= TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonAdvanceAccept" );
	ButtonAdvancedAccept      = TheWindowManager->winGetWindowFromId( nullptr, ButtonAdvancedAcceptID );

	ButtonAdvancedCancelID		= TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonAdvanceBack" );
	ButtonAdvancedCancel      = TheWindowManager->winGetWindowFromId( nullptr, ButtonAdvancedCancelID );

	initGameOptionsWindows();
	initShadersWindows();
	initHotkeysButton();

	sliderTextureResolutionID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:LowResSlider" );
	sliderTextureResolution = TheWindowManager->winGetWindowFromId( nullptr, sliderTextureResolutionID );

	check3DShadowsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:Check3DShadows" );
	check3DShadows   = TheWindowManager->winGetWindowFromId( nullptr, check3DShadowsID);

	check2DShadowsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:Check2DShadows" );
	check2DShadows   = TheWindowManager->winGetWindowFromId( nullptr, check2DShadowsID);

	checkCloudShadowsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckCloudShadows" );
	checkCloudShadows   = TheWindowManager->winGetWindowFromId( nullptr, checkCloudShadowsID);

	checkGroundLightingID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckGroundLighting" );
	checkGroundLighting   = TheWindowManager->winGetWindowFromId( nullptr, checkGroundLightingID);

	checkSmoothWaterID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckSmoothWater" );
	checkSmoothWater   = TheWindowManager->winGetWindowFromId( nullptr, checkSmoothWaterID);

	checkExtraAnimationsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckExtraAnimations" );
	checkExtraAnimations   = TheWindowManager->winGetWindowFromId( nullptr, checkExtraAnimationsID);

	checkNoDynamicLodID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckNoDynamicLOD" );
	checkNoDynamicLod   = TheWindowManager->winGetWindowFromId( nullptr, checkNoDynamicLodID);

	checkHeatEffectsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckHeatEffects" );
	checkHeatEffects   = TheWindowManager->winGetWindowFromId( nullptr, checkHeatEffectsID);

	checkUnlockFpsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckUnlockFPS" );
	checkUnlockFps   = TheWindowManager->winGetWindowFromId( nullptr, checkUnlockFpsID);
	comboBoxRenderFPSLimit = findOptionsWindow( "OptionsMenu.wnd:ComboBoxRenderFPSLimit" );
	setLabelText( "OptionsMenu.wnd:RenderFPSLimitLabel", "GUI:RenderFPSLimit" );
	setTooltip( comboBoxRenderFPSLimit, "TOOLTIP:RenderFPSLimit" );
	addComboEntries( comboBoxRenderFPSLimit, "GUI:RenderFPSLimit", ARRAY_SIZE(RenderFPSLimits) );
	if (comboBoxRenderFPSLimit)
	{
		GadgetComboBoxSetMaxDisplay( comboBoxRenderFPSLimit, ARRAY_SIZE(RenderFPSLimits) );
		Int fps = TheFramePacer->getRenderFPSLimit();
		if (fps < 0)
		{
			fps = TheFramePacer->getActualFramesPerSecondLimit();
		}
		Int initialRenderFPSLimitPos = ARRAY_SIZE(RenderFPSLimits) - 1;
		for (Int i = 0; i < ARRAY_SIZE(RenderFPSLimits); ++i)
		{
			if (RenderFPSLimits[i] == fps)
			{
				initialRenderFPSLimitPos = i;
				break;
			}
		}
		setComboPos( comboBoxRenderFPSLimit, initialRenderFPSLimitPos );
	}

	checkBuildingOcclusionID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckBehindBuilding" );
	checkBuildingOcclusion   = TheWindowManager->winGetWindowFromId( nullptr, checkBuildingOcclusionID);

	checkPropsID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:CheckShowProps" );
	checkProps   = TheWindowManager->winGetWindowFromId( nullptr, checkPropsID);

	sliderParticleCapID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ParticleCapSlider" );
  sliderParticleCap = TheWindowManager->winGetWindowFromId( nullptr, sliderParticleCapID );

	if (WinAdvancedDisplay)
	{
		WinAdvancedDisplay->winHide(TRUE);
	}
	if (WinGameOptions)
	{
		WinGameOptions->winHide(TRUE);
	}

	Color color =  GameMakeColor(255,255,255,255);

	initLabelVersion();

	// Choose an IP address, then initialize the IP combo box
	UnsignedInt selectedIP = pref->getLANIPAddress();

	UnicodeString str;
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	Int index;
	Int selectedIndex = -1;
	Int count = 0;
	GadgetComboBoxReset(comboBoxLANIP);
	while (IPlist)
	{
		count++;
		str.translate(IPlist->getIPstring());
		index = GadgetComboBoxAddEntry(comboBoxLANIP, str, color);
		GadgetComboBoxSetItemData(comboBoxLANIP, index, (void *)(IPlist->getIP()));
		if (selectedIP == IPlist->getIP())
		{
			selectedIndex = index;
		}
		IPlist = IPlist->getNext();
	}
	if (selectedIndex >= 0)
	{
		GadgetComboBoxSetSelectedPos(comboBoxLANIP, selectedIndex);
	}
	else
	{
		GadgetComboBoxSetSelectedPos(comboBoxLANIP, 0);
		if (IPs.getAddresses())
		{
			pref->setLANIPAddress(IPs.getAddresses()->getIPstring());
		}
	}

	// And now the GameSpy one
	if (comboBoxOnlineIP)
	{
		UnsignedInt selectedIP = pref->getOnlineIPAddress();
		UnicodeString str;
		IPEnumeration IPs;
		EnumeratedIP *IPlist = IPs.getAddresses();
		Int index;
		Int selectedIndex = -1;
		Int count = 0;
		GadgetComboBoxReset(comboBoxOnlineIP);
		while (IPlist)
		{
			count++;
			str.translate(IPlist->getIPstring());
			index = GadgetComboBoxAddEntry(comboBoxOnlineIP, str, color);
			GadgetComboBoxSetItemData(comboBoxOnlineIP, index, (void *)(IPlist->getIP()));
			if (selectedIP == IPlist->getIP())
			{
				selectedIndex = index;
			}
			IPlist = IPlist->getNext();
		}
		if (selectedIndex >= 0)
		{
			GadgetComboBoxSetSelectedPos(comboBoxOnlineIP, selectedIndex);
		}
		else
		{
			GadgetComboBoxSetSelectedPos(comboBoxOnlineIP, 0);
			if (IPs.getAddresses())
			{
				pref->setOnlineIPAddress(IPs.getAddresses()->getIPstring());
			}
		}
	}

#if ENABLE_GUI_HACKS
	// TheSuperHackers @tweak 26/07/2026 The http proxy feature was obsoleted because it did nothing for the UDP game traffic or match sockets.
	// Hide the relevant obsoleted UI elements accordingly.
	NameKeyType textEntryHTTPProxyID = TheNameKeyGenerator->nameToKey("OptionsMenu.wnd:TextEntryHTTPProxy");
	GameWindow *textEntryHTTPProxy = TheWindowManager->winGetWindowFromId(nullptr, textEntryHTTPProxyID);
	if (textEntryHTTPProxy)
		textEntryHTTPProxy->winHide(TRUE);

	NameKeyType staticTextHTTPProxyID = TheNameKeyGenerator->nameToKey("OptionsMenu.wnd:StaticTextHTTPProxy");
	GameWindow *staticTextHTTPProxy = TheWindowManager->winGetWindowFromId(nullptr, staticTextHTTPProxyID);
	if (staticTextHTTPProxy)
		staticTextHTTPProxy->winHide(TRUE);
#endif

	// Firewall Port Override
	GameWindow *textEntryFirewallPortOverride = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("OptionsMenu.wnd:TextEntryFirewallPortOverride"));
	if (textEntryFirewallPortOverride)
	{
			UnicodeString uStr;
			if (TheGlobalData->m_firewallPortOverride != 0)
			{	AsciiString aStr;
				aStr.format("%d",TheGlobalData->m_firewallPortOverride);
				uStr.translate(aStr);
			}
			GadgetTextEntrySetText(textEntryFirewallPortOverride,uStr);
	}

	// populate anti aliasing modes
	AsciiString selectedAliasingMode = (*pref)["AntiAliasing"];
	addComboEntries( comboBoxAntiAliasing, "GUI:AntiAliasing", OptionPreferences::AntiAliasingMode_Count );
	setLabelText( "OptionsMenu.wnd:AntiAliasingLabel", "GUI:AntiAliasing" );
	setTooltip( comboBoxAntiAliasing, "TOOLTIP:AntiAliasing" );
	Int val = atoi(selectedAliasingMode.str());
	Int pos = 0;

	// TheSuperHackers @info We are converting from human readable value to comboBox entry position
	val = highestBit(val);

	if (val == WW3D::MULTISAMPLE_MODE_NONE)
		pos = OptionPreferences::AntiAliasingMode_OFF;
	else if (val == WW3D::MULTISAMPLE_MODE_2X)
		pos = OptionPreferences::AntiAliasingMode_MSAA_2X;
	else if (val == WW3D::MULTISAMPLE_MODE_4X)
		pos = OptionPreferences::AntiAliasingMode_MSAA_4X;
	else if (val == WW3D::MULTISAMPLE_MODE_8X)
		pos = OptionPreferences::AntiAliasingMode_MSAA_8X;

	if (val < 0 || val > WW3D::MULTISAMPLE_MODE_8X)
	{
		TheWritableGlobalData->m_antiAliasLevel = pos = 0;
	}
	GadgetComboBoxSetSelectedPos(comboBoxAntiAliasing, pos);

	// get resolution from saved preferences file
	AsciiString selectedResolution = (*pref) ["Resolution"];
	Int selectedXRes=DEFAULT_DISPLAY_WIDTH;
	Int selectedYRes=DEFAULT_DISPLAY_HEIGHT;
	Int selectedResIndex=-1;
	if (!selectedResolution.isEmpty())
	{	//try to parse 2 integers out of string
		if (sscanf(selectedResolution.str(),"%d%d", &selectedXRes, &selectedYRes) != 2)
		{
			selectedXRes=DEFAULT_DISPLAY_WIDTH;
			selectedYRes=DEFAULT_DISPLAY_HEIGHT;
		}
	}

	// populate resolution modes
	GadgetComboBoxReset(comboBoxResolution);
	Int numResolutions = TheDisplay->getDisplayModeCount();
	UnsignedInt displayWidth = TheDisplay->getWidth();
	UnsignedInt displayHeight = TheDisplay->getHeight();

	for( Int i = 0; i < numResolutions; ++i )
	{	Int xres,yres,bitDepth;
		TheDisplay->getDisplayModeDescription(i,&xres,&yres,&bitDepth);
		str.format(L"%d x %d",xres,yres);
		GadgetComboBoxAddEntry( comboBoxResolution, str, color);
		// TheSuperHackers @bugfix xezon 12/06/2025 Now makes a selection with the active display resolution
		// instead of the resolution read from the Option Preferences, because the active display resolution
		// is the most relevant to make a selection with and the Option Preferences could be wrong.
		if ( xres == displayWidth && yres == displayHeight )
			selectedResIndex=i;
	}

	if (selectedResIndex == -1)
	{
		// TheSuperHackers @bugfix xezon 08/06/2025 Now adds the current resolution instead of defaulting to 800 x 600.
		// This avoids force changing the resolution when the user has set a custom resolution in the Option Preferences.
		Int xres = displayWidth;
		Int yres = displayHeight;
		str.format(L"%d x %d",xres,yres);
		GadgetComboBoxAddEntry( comboBoxResolution, str, color );
		selectedResIndex = GadgetComboBoxGetLength( comboBoxResolution ) - 1;
	}

	GadgetComboBoxSetSelectedPos( comboBoxResolution, selectedResIndex );

	// set the display detail
	// TheSuperHackers @tweak xezon 24/09/2025 The Detail Combo Box now has the same value order as StaticGameLODLevel for simplicity.
	// TheSuperHackers @feature xezon 24/09/2025 The Detail Combo Box now has a new options for "Very High".
	GadgetComboBoxReset(comboBoxDetail);
#if ENABLE_GUI_HACKS
	// TheSuperHackers @tweak xezon 24/09/2025 Show max 4 rows because with the original layout it cannot possibly show 5.
	GadgetComboBoxSetMaxDisplay(comboBoxDetail, 4);
#endif
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Low"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Medium"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:High"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->FETCH_OR_SUBSTITUTE("GUI:VeryHigh", L"Very High"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Custom"), color);
	static_assert(STATIC_GAME_LOD_COUNT == 5, "Wrong combo box count");

	//Check if level was never set and default to setting most suitable for system.
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
	{
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->getRecommendedStaticLODLevel());
	}

	GadgetComboBoxSetSelectedPos(comboBoxDetail, (Int)TheGameLODManager->getStaticLODLevel());

	GadgetSliderSetPosition( sliderTextureResolution, 2-WW3D::Get_Texture_Reduction());

	GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

	GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

	GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

	GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

	GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

	GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);

	GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

	GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

	setCheck( checkVSync, WW3D::Is_VSync_On() );

	GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

	GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

	GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	//checkProps->winEnable(false);	//gray out the option for now.

	GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount );

	//set language filter
	AsciiString languageFilter = (*pref)["LanguageFilter"];
	if (languageFilter == "true" )
	{
		GadgetCheckBoxSetChecked( checkLanguageFilter, true);
		TheWritableGlobalData->m_languageFilterPref = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkLanguageFilter, false);
		TheWritableGlobalData->m_languageFilterPref = false;
	}

	//set replay camera
	if (pref->saveCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, true);
		TheWritableGlobalData->m_saveCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, false);
		TheWritableGlobalData->m_saveCameraInReplay = false;
	}
	if (pref->useCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkUseCamera, true);
		TheWritableGlobalData->m_useCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkUseCamera, false);
		TheWritableGlobalData->m_useCameraInReplay = false;
	}

	//set scroll options
	AsciiString test = (*pref)["DrawScrollAnchor"];
	DEBUG_LOG(("DrawScrollAnchor == [%s]", test.str()));
	if (test == "yes" || (test.isEmpty() && TheInGameUI->getDrawRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, true);
		TheInGameUI->setDrawRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, false);
		TheInGameUI->setDrawRMBScrollAnchor(false);
	}
	test = (*pref)["MoveScrollAnchor"];
	DEBUG_LOG(("MoveScrollAnchor == [%s]", test.str()));
	if (test == "yes" || (test.isEmpty() && TheInGameUI->getMoveRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, true);
		TheInGameUI->setMoveRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, false);
		TheInGameUI->setMoveRMBScrollAnchor(false);
	}

//	// Audio Init shiznat
//	GadgetCheckBoxSetChecked(checkAudioHardware, TheAudio->getHardwareAccelerated());
//	GadgetCheckBoxSetChecked(checkAudioSurround, TheAudio->getSpeakerSurround());

	// set the mouse mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, TheGlobalData->m_useAlternateMouse);
	GadgetCheckBoxSetChecked(checkRetaliation, TheGlobalData->m_clientRetaliationModeEnabled);
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, TheGlobalData->m_doubleClickAttackMove );
	setCheck( checkBorderlessWindow, TheGlobalData->m_borderlessWindow && !TheGlobalData->m_windowed );
	setCheck( checkWindowed, TheGlobalData->m_windowed );
	updateGameOptionsEnables();

	// set scroll speed slider
	// TheSuperHackers @tweak xezon 11/07/2025 No longer sets the slider position if the user setting
	// is set beyond the slider limits. This gives the user more freedom to customize the scroll
	// speed. The slider value remains 0.
	Int scrollPos = (Int)(TheGlobalData->m_keyboardScrollFactor*100.0f);
	Int scrollMin, scrollMax;
	GadgetSliderGetMinMax( sliderScrollSpeed, &scrollMin, &scrollMax );
	if (scrollPos >= scrollMin && scrollPos <= scrollMax)
	{
		GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );
	}
	DEBUG_LOG(("Scroll Speed %d", scrollPos));

	showMaxCameraHeight( pref->getUseCustomMaxCameraHeight(), (Int)pref->getMaxCameraHeight() );
	populateGameOptions();
	populateShaders();

 	// set volume sliders

	// set music volume slider
	GadgetSliderSetPosition( sliderMusicVolume, REAL_TO_INT(pref->getMusicVolume()) );

	//set SFX volume slider
	Real maxVolume = MAX( pref->getSoundVolume(), pref->get3DSoundVolume() );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume ) );

	//set voice volume slider
	GadgetSliderSetPosition( sliderVoiceVolume, REAL_TO_INT(pref->getSpeechVolume()) );

	// set the gamma slider
 	GadgetSliderSetPosition( sliderGamma, REAL_TO_INT(pref->getGammaValue()) );

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:OptionsMenuParent" );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );

	if( (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL) || TheGameSpyInfo )
	{
		// disable controls that you can't change the options for in game
		comboBoxLANIP->winEnable(FALSE);

		if (comboBoxOnlineIP)
			comboBoxOnlineIP->winEnable(FALSE);

		buttonFirewallRefresh->winEnable(FALSE);

		// Trees are placed and draw modules chosen as the map loads, so these wait for the next one
		if (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL)
		{
			enableWindow( checkProps, FALSE );
			enableWindow( checkExtraAnimations, FALSE );
		}

		if (comboBoxResolution)
			comboBoxResolution->winEnable(FALSE);

		enableWindow( checkBorderlessWindow, FALSE );
		enableWindow( checkWindowed, FALSE );

		if (textEntryFirewallPortOverride)
			textEntryFirewallPortOverride->winEnable(FALSE);

		const GameMode mode = TheGameLogic->getGameMode();
		if (mode == GAME_LAN || mode == GAME_INTERNET)
		{
			enableWindow( checkMaxCameraHeight, FALSE );
			enableWindow( textEntryMaxCameraHeight, FALSE );
		}

//		if (checkAudioSurround)
//			checkAudioSurround->winEnable(FALSE);
//
//		if (checkAudioHardware)
//			checkAudioHardware->winEnable(FALSE);
	}


	OptionsTabs::start();

	TheWindowManager->winSetModal(parent);
	ignoreSelected = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuShutdown( WindowLayout *layout, void *userData )
{
/* moved into the back button stuff
	if (pref)
	{
		pref->write();
		delete pref;
		pref = nullptr;
	}

	comboBoxIP = nullptr;

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
*/

}

//-------------------------------------------------------------------------------------------------
/** options menu update method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuUpdate( WindowLayout *layout, void *userData )
{

}

//-------------------------------------------------------------------------------------------------
/** Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			if (OptionsTabs::onKey( key, state, window ))
			{
				return MSG_HANDLED;
			}

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{
						if (isPanelOpen( WinGameOptions ))
						{
							cancelPanel( WinGameOptions, populateGameOptions );
							return MSG_HANDLED;
						}
						if (isPanelOpen( WinShaders ))
						{
							cancelPanel( WinShaders, populateShaders );
							return MSG_HANDLED;
						}

						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonBack" );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;

}

//-------------------------------------------------------------------------------------------------
/** options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuSystem( GameWindow *window, UnsignedInt msg,
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonBack = NAMEKEY_INVALID;
	static NameKeyType buttonDefaults = NAMEKEY_INVALID;
	static NameKeyType buttonAccept = NAMEKEY_INVALID;
	static NameKeyType buttonReplayMenu = NAMEKEY_INVALID;
	static NameKeyType buttonKeyboardOptionsMenu = NAMEKEY_INVALID;

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			// get ids for our children controls
			buttonBack = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonBack" );
			buttonDefaults = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonDefaults" );
			buttonAccept = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonAccept" );
			buttonKeyboardOptionsMenu = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonKeyboardOptions" );

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if(ignoreSelected)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == comboBoxAntiAliasingID || controlID == comboBoxSuperSamplingID)
				{
					updateGameOptionsEnables();
					break;
				}

				if (controlID == comboBoxDetailID)
				{
					Int index;
					GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
					if(index != STATIC_GAME_LOD_CUSTOM)
						break;

					showAdvancedOptions();
				}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			if(ignoreSelected)
				break;
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if (OptionsTabs::onButtonSelected( control ))
			{
				break;
			}
			selectCustomDetailFor( control );

			if( controlID == buttonBack )
			{
				// go back one screen
				//TheShell->pop();

				delete pref;
				pref = nullptr;

				comboBoxLANIP = nullptr;
				comboBoxOnlineIP = nullptr;
				OptionsTabs::shutdown();

				if(GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS))
					GameSpyCloseOverlay(GSOVERLAY_OPTIONS);
				else
				{
					DestroyOptionsLayout();
				}

			}
			else if (controlID == buttonAccept )
			{
				saveOptions();

				if (pref)
				{
					pref->write();
					delete pref;
					pref = nullptr;
				}

				comboBoxLANIP = nullptr;
				comboBoxOnlineIP = nullptr;
				OptionsTabs::shutdown();

				if(!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
					destroyQuitMenu(); // if we're in a game, the change res then enter the same kind of game, we nee the quit menu to be gone.


				if(GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS))
					GameSpyCloseOverlay(GSOVERLAY_OPTIONS);
				else
				{
					DestroyOptionsLayout();
					if (dispChanged)
					{
						DoResolutionDialog();
					}
				}

			}
			else if (controlID == buttonDefaults )
			{
				setDefaults();
			}
			else if (controlID == ButtonGameOptionsID )
			{
				showPanel( WinGameOptions );
			}
			else if (controlID == ButtonGameOptionsAcceptID )
			{
				hidePanel( WinGameOptions );
			}
			else if (controlID == ButtonGameOptionsCancelID )
			{
				cancelPanel( WinGameOptions, populateGameOptions );
			}
			else if (controlID == ButtonShadersID )
			{
				showPanel( WinShaders );
			}
			else if (controlID == ButtonHotkeysID )
			{
				HotkeysMenuOpen();
			}
			else if (controlID == ButtonShadersAcceptID )
			{
				hidePanel( WinShaders );
			}
			else if (controlID == ButtonShadersCancelID )
			{
				cancelPanel( WinShaders, populateShaders );
			}
			else if (controlID == checkGridHotkeysID || controlID == checkKeyboardOverlayBackdropID || controlID == checkBloomID ||
				controlID == checkDynamicLightsID || controlID == checkPixelLightsID || controlID == checkCloudShadowsID ||
				controlID == checkSpecularID || controlID == checkNormalMapsID || controlID == checkAmbientOcclusionID )
			{
				updateGameOptionsEnables();
			}
			else if (controlID == ButtonAdvancedAcceptID )
			{
				acceptAdvancedOptions();

			}
			else if (controlID == ButtonAdvancedCancelID )
			{
				cancelAdvancedOptions();
			}
			else if ( controlID == buttonKeyboardOptionsMenu )
			{
				TheShell->push( "Menus/KeyboardOptionsMenu.wnd" );
			}
			else if (controlID == checkWindowedID)
			{
				if (GadgetCheckBoxIsChecked( control ))
				{
					setCheck( checkBorderlessWindow, FALSE );
				}
				updateGameOptionsEnables();
			}
			else if (controlID == checkBorderlessWindowID)
			{
				if (GadgetCheckBoxIsChecked( control ))
				{
					setCheck( checkWindowed, FALSE );
				}
				updateGameOptionsEnables();
			}
			else if(controlID == checkMaxCameraHeightID )
			{
				const Bool useCustom = GadgetCheckBoxIsChecked( control );
				showMaxCameraHeightEntry( useCustom, useCustom ? (Int)pref->getMaxCameraHeight() : (Int)TheGlobalData->m_defaultMaxCameraHeight );
			}
			else if(controlID == checkDrawAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setDrawRMBScrollAnchor(true);
          	(*pref)["DrawScrollAnchor"] = "yes";
        }
				else
        {
          	TheInGameUI->setDrawRMBScrollAnchor(false);
          	(*pref)["DrawScrollAnchor"] = "no";
        }
      }
			else if(controlID == checkMoveAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setMoveRMBScrollAnchor(true);
          	(*pref)["MoveScrollAnchor"] = "yes";
        }
				else
        {
          	TheInGameUI->setMoveRMBScrollAnchor(false);
          	(*pref)["MoveScrollAnchor"] = "no";
        }
      }
			else if(controlID == checkSaveCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_saveCameraInReplay = true;
          	(*pref)["SaveCameraInReplays"] = "yes";
        }
				else
        {
          	TheWritableGlobalData->m_saveCameraInReplay = false;
          	(*pref)["SaveCameraInReplays"] = "no";
        }
      }
			else if(controlID == checkUseCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_useCameraInReplay = true;
          	(*pref)["UseCameraInReplays"] = "yes";
        }
				else
        {
          	TheWritableGlobalData->m_useCameraInReplay = false;
          	(*pref)["UseCameraInReplays"] = "no";
        }
      }
			else if (controlID == buttonFirewallRefreshID)
			{
				// setting the behavior to unknown will force the firewall helper to detect the firewall behavior
				// the next time we log into gamespy/WOL/whatever.
				char num[16];
				num[0] = 0;
				TheWritableGlobalData->m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
				itoa(TheGlobalData->m_firewallBehavior, num, 10);
				AsciiString numstr;
				numstr = num;
				(*pref)["FirewallBehavior"] = numstr;
			}
			break;

		}

		//---------------------------------------------------------------------------------------------
		case GSM_SLIDER_TRACK:
		{
			if (!ignoreSelected)
			{
				selectCustomDetailFor( (GameWindow *)mData1 );
			}
			return MSG_IGNORED;
		}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
