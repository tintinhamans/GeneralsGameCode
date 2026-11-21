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

// Ronin @feature 29/10/2026 DX9: see OptionsValues.h. The bodies follow OptionsMenu.cpp's saveOptions() and init line by line,
// after tintinhamans' fork (arctic/go/feat/rmlui); the settings the .wnd only re-saves get real setters here.
#include "WW3D2/dx8todx9.h"

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GUI/GUICallbacks/Menus/OptionsValues.h"

#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "Common/Recorder.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/Online/IOnlineGameHooks.h"

#include "WW3D2/ww3d.h"

// OptionsMenu.cpp's: the resolution dialog (MainMenu.cpp's DoResolutionDialog) reads and resets these.
extern DisplaySettings oldDispSettings, newDispSettings;
extern Bool dispChanged;

namespace OptionsValues
{

static const char *yesNo(Bool b) { return b ? "yes" : "no"; }

static void setInt(OptionPreferences &pref, const char *key, Int value)
{
	AsciiString s;
	s.format("%d", value);
	pref[key] = s;
}

//-------------------------------------------------------------------------------------------------
// Detail
//-------------------------------------------------------------------------------------------------
Int GetDetailCustomLevel() { return (Int)STATIC_GAME_LOD_CUSTOM; }

Int GetCurrentDetailLevel()
{
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->getRecommendedStaticLODLevel());
	return (Int)TheGameLODManager->getStaticLODLevel();
}

DetailPresetValues GetDetailPresetPreview(Int level)
{
	DetailPresetValues values = GetCurrentGraphicsValues();
	if (level < 0 || level >= STATIC_GAME_LOD_CUSTOM)
		return values;
	const StaticGameLODInfo &info = TheGameLODManager->m_staticGameLODInfo[level];
	values.textureResolutionSliderPos = 2 - TheGameLODManager->getLevelTextureReduction((StaticGameLODLevel)level);
	values.particleCap = info.m_maxParticleCount;
	values.shadow3D = info.m_useShadowVolumes;
	values.shadow2D = info.m_useShadowDecals;
	values.cloudShadows = info.m_useCloudMap;
	values.groundLighting = info.m_useLightMap;
	values.smoothWater = info.m_showSoftWaterEdge;
	values.extraAnimations = info.m_useBuildupScaffolds;
	values.noDynamicLod = !info.m_enableDynamicLOD;
	values.heatEffects = info.m_useHeatEffects;
	values.props = info.m_useTrees;
	return values;	// buildingOcclusion: no preset sets it
}

DetailPresetValues GetCurrentGraphicsValues()
{
	DetailPresetValues values;
	values.textureResolutionSliderPos = 2 - WW3D::Get_Texture_Reduction();
	values.particleCap = TheGlobalData->m_maxParticleCount;
	values.shadow3D = TheGlobalData->m_useShadowVolumes;
	values.shadow2D = TheGlobalData->m_useShadowDecals;
	values.cloudShadows = TheGlobalData->m_useCloudMap;
	values.groundLighting = TheGlobalData->m_useLightMap;
	values.smoothWater = TheGlobalData->m_showSoftWaterEdge;
	values.extraAnimations = !TheGlobalData->m_useDrawModuleLOD;
	values.noDynamicLod = !TheGlobalData->m_enableDynamicLOD;
	values.heatEffects = TheGlobalData->m_useHeatEffects;
	values.buildingOcclusion = TheGlobalData->m_enableBehindBuildingMarkers;
	values.props = TheGlobalData->m_useTrees;
	return values;
}

void ApplyDetailLevel(OptionPreferences &pref, Int level)
{
	if (TheGameLODManager->setStaticLODLevel((StaticGameLODLevel)level))
		pref["StaticGameLOD"] = TheGameLODManager->getStaticGameLODLevelName(TheGameLODManager->getStaticLODLevel());
}

void ApplyCustomGraphicsSettings(OptionPreferences &pref, const DetailPresetValues &values)
{
	const Int textureReduction = 2 - values.textureResolutionSliderPos;
	setInt(pref, "TextureReduction", textureReduction);
	TheWritableGlobalData->m_textureReductionFactor = textureReduction;
	TheGameClient->setTextureLOD(textureReduction);

	TheWritableGlobalData->m_useShadowVolumes = values.shadow3D;
	pref["UseShadowVolumes"] = yesNo(values.shadow3D);
	TheWritableGlobalData->m_useShadowDecals = values.shadow2D;
	pref["UseShadowDecals"] = yesNo(values.shadow2D);
	TheWritableGlobalData->m_useCloudMap = values.cloudShadows;
	pref["UseCloudMap"] = yesNo(values.cloudShadows);
	TheWritableGlobalData->m_useLightMap = values.groundLighting;
	pref["UseLightMap"] = yesNo(values.groundLighting);
	TheWritableGlobalData->m_showSoftWaterEdge = values.smoothWater;
	pref["ShowSoftWaterEdge"] = yesNo(values.smoothWater);
	TheWritableGlobalData->m_useDrawModuleLOD = !values.extraAnimations;
	TheWritableGlobalData->m_useTreeSway = values.extraAnimations;	// borrows the same setting
	pref["ExtraAnimations"] = yesNo(values.extraAnimations);
	TheWritableGlobalData->m_enableDynamicLOD = !values.noDynamicLod;
	pref["DynamicLOD"] = yesNo(!values.noDynamicLod);
	TheWritableGlobalData->m_useHeatEffects = values.heatEffects;
	pref["HeatEffects"] = yesNo(values.heatEffects);
	TheWritableGlobalData->m_enableBehindBuildingMarkers = values.buildingOcclusion;
	pref["BuildingOcclusion"] = yesNo(values.buildingOcclusion);
	TheWritableGlobalData->m_useTrees = values.props;
	pref["ShowTrees"] = yesNo(values.props);
	setInt(pref, "MaxParticleCount", values.particleCap);
	TheWritableGlobalData->m_maxParticleCount = values.particleCap;
}

//-------------------------------------------------------------------------------------------------
// Resolution
//-------------------------------------------------------------------------------------------------
Int GetDisplayModeCount() { return TheDisplay->getDisplayModeCount(); }

void GetDisplayMode(Int index, Int &width, Int &height)
{
	Int bitDepth = 0;
	width = height = 0;
	if (index >= 0 && index < TheDisplay->getDisplayModeCount())
		TheDisplay->getDisplayModeDescription(index, &width, &height, &bitDepth);
}

Int GetCurrentDisplayModeIndex()
{
	for (Int i = 0; i < TheDisplay->getDisplayModeCount(); ++i)
	{
		Int w, h, bitDepth;
		TheDisplay->getDisplayModeDescription(i, &w, &h, &bitDepth);
		if ((UnsignedInt)w == TheDisplay->getWidth() && (UnsignedInt)h == TheDisplay->getHeight())
			return i;
	}
	return -1;
}

Bool ApplyDisplayMode(OptionPreferences &pref, Int index)
{
	oldDispSettings.xRes = TheDisplay->getWidth();
	oldDispSettings.yRes = TheDisplay->getHeight();
	oldDispSettings.bitDepth = TheDisplay->getBitDepth();
	oldDispSettings.windowed = TheDisplay->getWindowed();

	if (index < 0 || index >= TheDisplay->getDisplayModeCount())
		return FALSE;
	Int xres, yres, bitDepth;
	TheDisplay->getDisplayModeDescription(index, &xres, &yres, &bitDepth);
	if (TheGlobalData->m_xResolution == xres && TheGlobalData->m_yResolution == yres)
		return FALSE;
	if (!TheDisplay->setDisplayMode(xres, yres, bitDepth, TheDisplay->getWindowed()))
		return FALSE;

	dispChanged = TRUE;
	TheWritableGlobalData->m_xResolution = xres;
	TheWritableGlobalData->m_yResolution = yres;
	TheHeaderTemplateManager->onResolutionChanged();
	TheMouse->onResolutionChanged();

	newDispSettings.xRes = xres;
	newDispSettings.yRes = yres;
	newDispSettings.bitDepth = bitDepth;
	newDispSettings.windowed = TheDisplay->getWindowed();

	AsciiString prefString;
	prefString.format("%d %d", xres, yres);
	pref["Resolution"] = prefString;

	TheShell->recreateWindowLayouts();
	TheInGameUI->recreateControlBar();
	TheInGameUI->refreshCustomUiResources();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Network
//-------------------------------------------------------------------------------------------------
Int GetIPChoiceCount()
{
	IPEnumeration IPs;
	Int count = 0;
	for (EnumeratedIP *ip = IPs.getAddresses(); ip; ip = ip->getNext())
		++count;
	return count;
}

AsciiString GetIPChoiceLabel(Int index)
{
	IPEnumeration IPs;
	Int i = 0;
	for (EnumeratedIP *ip = IPs.getAddresses(); ip; ip = ip->getNext(), ++i)
		if (i == index)
			return ip->getIPstring();
	return AsciiString::TheEmptyString;
}

UnsignedInt GetIPChoiceIP(Int index)
{
	IPEnumeration IPs;
	Int i = 0;
	for (EnumeratedIP *ip = IPs.getAddresses(); ip; ip = ip->getNext(), ++i)
		if (i == index)
			return ip->getIP();
	return 0;
}

static Int findIPIndex(UnsignedInt wanted)
{
	IPEnumeration IPs;
	Int i = 0;
	for (EnumeratedIP *ip = IPs.getAddresses(); ip; ip = ip->getNext(), ++i)
		if (ip->getIP() == wanted)
			return i;
	return -1;
}

// An address no longer on the machine shows the first one, as the .wnd's init does.
Int GetCurrentLANIPIndex(OptionPreferences &pref)
{
	const Int index = findIPIndex(pref.getLANIPAddress());
	return index >= 0 ? index : 0;
}

Int GetCurrentOnlineIPIndex(OptionPreferences &pref)
{
	const Int index = findIPIndex(pref.getOnlineIPAddress());
	return index >= 0 ? index : 0;
}

void ApplyLANIPChoice(OptionPreferences &pref, UnsignedInt ip)
{
	TheWritableGlobalData->m_defaultIP = ip;
	pref.setLANIPAddress(ip);
}

void ApplyOnlineIPChoice(OptionPreferences &pref, UnsignedInt ip)
{
	pref.setOnlineIPAddress(ip);
}

Int GetCurrentFirewallPortOverride() { return TheGlobalData->m_firewallPortOverride; }

void ApplyFirewallPortOverride(OptionPreferences &pref, Int port)
{
	if (port < 0 || port > 65535)
		port = 0;
	if (TheGlobalData->m_firewallPortOverride == port)
		return;
	TheWritableGlobalData->m_firewallPortOverride = port;
	setInt(pref, "FirewallPortOverride", port);
}

// Unknown makes the firewall helper detect it again at the next login.
void RefreshFirewallBehavior(OptionPreferences &pref)
{
	TheWritableGlobalData->m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
	setInt(pref, "FirewallBehavior", TheGlobalData->m_firewallBehavior);
}

// Ronin @feature 21/11/2026 DX9: logged in through the online provider, as OptionsMenu.cpp; GameSpy's hook is TheGameSpyInfo
Bool IsOptionsRestrictedContext()
{
	return (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL) || GetOnlineGameHooks()->isLoggedIn();
}

//-------------------------------------------------------------------------------------------------
// Audio
//-------------------------------------------------------------------------------------------------
Int GetCurrentMusicVolumePercent(OptionPreferences &pref) { return REAL_TO_INT(pref.getMusicVolume()); }
Int GetCurrentSFXVolumePercent(OptionPreferences &pref) { return REAL_TO_INT(MAX(pref.getSoundVolume(), pref.get3DSoundVolume())); }
Int GetCurrentVoiceVolumePercent(OptionPreferences &pref) { return REAL_TO_INT(pref.getSpeechVolume()); }
Int GetCurrentMoneyVolumePercent(OptionPreferences &pref) { return REAL_TO_INT(pref.getMoneyTransactionVolume()); }

void ApplyMusicVolumePercent(OptionPreferences &pref, Int percent)
{
	setInt(pref, "MusicVolume", percent);
	TheAudio->setVolume(percent / 100.0f, (AudioAffect)(AudioAffect_Music | AudioAffect_SystemSetting));
}

// 2D and 3D share the slider; the audio settings' relative offset lowers one of them.
void ApplySFXVolumePercent(OptionPreferences &pref, Int percent)
{
	Real sound2DVolume = percent / 100.0f;
	Real sound3DVolume = percent / 100.0f;
	Real relative2DVolume = TheAudio->getAudioSettings()->m_relative2DVolume;
	relative2DVolume = MIN(1.0f, MAX(-1.0f, relative2DVolume));
	if (relative2DVolume < 0.0f)
		sound2DVolume *= 1.0f + relative2DVolume;
	else
		sound3DVolume *= 1.0f - relative2DVolume;

	TheAudio->setVolume(sound2DVolume, (AudioAffect)(AudioAffect_Sound | AudioAffect_SystemSetting));
	TheAudio->setVolume(sound3DVolume, (AudioAffect)(AudioAffect_Sound3D | AudioAffect_SystemSetting));
	setInt(pref, "SFXVolume", REAL_TO_INT(sound2DVolume * 100.0f));
	setInt(pref, "SFX3DVolume", REAL_TO_INT(sound3DVolume * 100.0f));
}

void ApplyVoiceVolumePercent(OptionPreferences &pref, Int percent)
{
	setInt(pref, "VoiceVolume", percent);
	TheAudio->setVolume(percent / 100.0f, (AudioAffect)(AudioAffect_Speech | AudioAffect_SystemSetting));
}

void ApplyMoneyVolumePercent(OptionPreferences &pref, Int percent)
{
	setInt(pref, "MoneyTransactionVolume", percent);
	TheAudio->friend_getAudioSettings()->m_preferredMoneyTransactionVolume = percent / 100.0f;
}

Int GetDefaultMusicVolumePercent() { return REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f); }
Int GetDefaultSFXVolumePercent()
{
	return REAL_TO_INT(MAX(TheAudio->getAudioSettings()->m_defaultSoundVolume, TheAudio->getAudioSettings()->m_default3DSoundVolume) * 100.0f);
}
Int GetDefaultVoiceVolumePercent() { return REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f); }

//-------------------------------------------------------------------------------------------------
// Brightness
//-------------------------------------------------------------------------------------------------
Int GetCurrentBrightnessPercent(OptionPreferences &pref) { return REAL_TO_INT(pref.getGammaValue()); }

// 0.6 at the bottom, 1 at the middle, 2 at the top, as the .wnd's slider.
void ApplyBrightnessPercent(OptionPreferences &pref, Int percent)
{
	Real gamma = 1.0f;
	if (percent < 50)
		gamma = (percent <= 0) ? 0.6f : 1.0f - 0.4f * (Real)(50 - percent) / 50.0f;
	else if (percent > 50)
		gamma = 1.0f + 1.0f * (Real)(percent - 50) / 50.0f;

	setInt(pref, "Gamma", percent);
	if (TheGlobalData->m_displayGamma != gamma)
	{
		TheWritableGlobalData->m_displayGamma = gamma;
		TheDisplay->setGamma(TheGlobalData->m_displayGamma, 0.0f, 1.0f, FALSE);
	}
}

//-------------------------------------------------------------------------------------------------
// Controls
//-------------------------------------------------------------------------------------------------
static Bool prefYes(OptionPreferences &pref, const char *key, Bool whenAbsent)
{
	OptionPreferences::const_iterator it = pref.find(key);
	if (it == pref.end() || it->second.isEmpty())
		return whenAbsent;
	return stricmp(it->second.str(), "yes") == 0;
}

ControlValues GetCurrentControls(OptionPreferences &pref)
{
	ControlValues v;
	v.scrollSpeed = (Int)(TheGlobalData->m_keyboardScrollFactor * 100.0f);
	v.alternateMouse = TheGlobalData->m_useAlternateMouse;
	v.rightMouseScrollWithAlternate = pref.getRightMouseScrollWithAlternateMouseEnabled();
	v.retaliation = TheGlobalData->m_clientRetaliationModeEnabled;
	v.doubleClickAttackMove = TheGlobalData->m_doubleClickAttackMove;
	v.drawScrollAnchor = prefYes(pref, "DrawScrollAnchor", TheInGameUI->getDrawRMBScrollAnchor());
	v.moveScrollAnchor = prefYes(pref, "MoveScrollAnchor", TheInGameUI->getMoveRMBScrollAnchor());
	v.edgeScrollWindowed = pref.getScreenEdgeScrollEnabledInWindowedApp();
	v.edgeScrollFullscreen = pref.getScreenEdgeScrollEnabledInFullscreenApp();
	v.captureWindowedGame = pref.getCursorCaptureEnabledInWindowedGame();
	v.captureWindowedMenu = pref.getCursorCaptureEnabledInWindowedMenu();
	v.captureFullscreenGame = pref.getCursorCaptureEnabledInFullscreenGame();
	v.captureFullscreenMenu = pref.getCursorCaptureEnabledInFullscreenMenu();
	return v;
}

void SetDefaultControls(ControlValues &v)
{
	v.alternateMouse = FALSE;
	v.retaliation = TRUE;
	v.doubleClickAttackMove = FALSE;
	v.scrollSpeed = (Int)(TheGlobalData->m_keyboardDefaultScrollFactor * 100.0f);
}

void ApplyControls(OptionPreferences &pref, const ControlValues &v, Int scrollSpeedLoaded)
{
	TheWritableGlobalData->m_useAlternateMouse = v.alternateMouse;
	pref["UseAlternateMouse"] = yesNo(v.alternateMouse);
	TheWritableGlobalData->m_useRightMouseScrollWithAlternateMouse = v.rightMouseScrollWithAlternate;
	pref["UseRightMouseScrollWithAlternateMouse"] = yesNo(v.rightMouseScrollWithAlternate);
	TheWritableGlobalData->m_clientRetaliationModeEnabled = v.retaliation;
	pref["Retaliation"] = yesNo(v.retaliation);
	TheWritableGlobalData->m_doubleClickAttackMove = v.doubleClickAttackMove;
	pref["UseDoubleClickAttackMove"] = yesNo(v.doubleClickAttackMove);

	if (v.scrollSpeed != scrollSpeedLoaded && v.scrollSpeed > 0)
	{
		TheWritableGlobalData->m_keyboardScrollFactor = v.scrollSpeed / 100.0f;
		setInt(pref, "ScrollFactor", v.scrollSpeed);
	}

	TheInGameUI->setDrawRMBScrollAnchor(v.drawScrollAnchor);
	TheWritableGlobalData->m_drawScrollAnchor = v.drawScrollAnchor;
	pref["DrawScrollAnchor"] = yesNo(v.drawScrollAnchor);
	TheInGameUI->setMoveRMBScrollAnchor(v.moveScrollAnchor);
	TheWritableGlobalData->m_moveScrollAnchor = v.moveScrollAnchor;
	pref["MoveScrollAnchor"] = yesNo(v.moveScrollAnchor);

	ScreenEdgeScrollMode edge = 0;
	if (v.edgeScrollWindowed) edge |= ScreenEdgeScrollMode_EnabledInWindowedApp;
	if (v.edgeScrollFullscreen) edge |= ScreenEdgeScrollMode_EnabledInFullscreenApp;
	pref["ScreenEdgeScrollEnabledInWindowedApp"] = yesNo(v.edgeScrollWindowed);
	pref["ScreenEdgeScrollEnabledInFullscreenApp"] = yesNo(v.edgeScrollFullscreen);
	TheLookAtTranslator->setScreenEdgeScrollMode(edge);

	CursorCaptureMode capture = 0;
	if (v.captureWindowedGame) capture |= CursorCaptureMode_EnabledInWindowedGame;
	if (v.captureWindowedMenu) capture |= CursorCaptureMode_EnabledInWindowedMenu;
	if (v.captureFullscreenGame) capture |= CursorCaptureMode_EnabledInFullscreenGame;
	if (v.captureFullscreenMenu) capture |= CursorCaptureMode_EnabledInFullscreenMenu;
	pref["CursorCaptureEnabledInWindowedGame"] = yesNo(v.captureWindowedGame);
	pref["CursorCaptureEnabledInWindowedMenu"] = yesNo(v.captureWindowedMenu);
	pref["CursorCaptureEnabledInFullscreenGame"] = yesNo(v.captureFullscreenGame);
	pref["CursorCaptureEnabledInFullscreenMenu"] = yesNo(v.captureFullscreenMenu);
	TheMouse->setCursorCaptureMode(capture);
}

//-------------------------------------------------------------------------------------------------
// Interface and replays
//-------------------------------------------------------------------------------------------------
InterfaceValues GetCurrentInterface(OptionPreferences &pref)
{
	InterfaceValues v;
	OptionPreferences::const_iterator it = pref.find("LanguageFilter");
	v.languageFilter = it != pref.end() && it->second == "true";
	v.showMoneyPerMinute = pref.getShowMoneyPerMinute();
	v.playerObserver = pref.getPlayerObserverEnabled();
	v.archiveReplays = pref.getArchiveReplaysEnabled();
	v.saveCameraInReplays = pref.saveCameraInReplays();
	v.useCameraInReplays = pref.useCameraInReplays();
	v.renderFpsFontSize = pref.getRenderFpsFontSize();
	v.systemTimeFontSize = pref.getSystemTimeFontSize();
	v.gameTimeFontSize = pref.getGameTimeFontSize();
	v.networkLatencyFontSize = pref.getNetworkLatencyFontSize();
	v.playerInfoListFontSize = pref.getPlayerInfoListFontSize();
	return v;
}

void ApplyInterface(OptionPreferences &pref, const InterfaceValues &v)
{
	TheWritableGlobalData->m_languageFilterPref = v.languageFilter;
	pref["LanguageFilter"] = v.languageFilter ? "true" : "false";
	TheWritableGlobalData->m_showMoneyPerMinute = v.showMoneyPerMinute;
	pref["ShowMoneyPerMinute"] = yesNo(v.showMoneyPerMinute);
	TheWritableGlobalData->m_enablePlayerObserver = v.playerObserver;
	pref["PlayerObserverEnabled"] = yesNo(v.playerObserver);
	TheRecorder->setArchiveEnabled(v.archiveReplays);
	pref["ArchiveReplays"] = yesNo(v.archiveReplays);
	TheWritableGlobalData->m_saveCameraInReplay = v.saveCameraInReplays;
	pref["SaveCameraInReplays"] = yesNo(v.saveCameraInReplays);
	TheWritableGlobalData->m_useCameraInReplay = v.useCameraInReplays;
	pref["UseCameraInReplays"] = yesNo(v.useCameraInReplays);

	// The overlays read their size from the global data: set it, then rebuild the font.
	TheWritableGlobalData->m_renderFpsFontSize = v.renderFpsFontSize;
	setInt(pref, "RenderFpsFontSize", v.renderFpsFontSize);
	TheInGameUI->refreshRenderFpsResources();
	TheWritableGlobalData->m_systemTimeFontSize = v.systemTimeFontSize;
	setInt(pref, "SystemTimeFontSize", v.systemTimeFontSize);
	TheInGameUI->refreshSystemTimeResources();
	TheWritableGlobalData->m_gameTimeFontSize = v.gameTimeFontSize;
	setInt(pref, "GameTimeFontSize", v.gameTimeFontSize);
	TheInGameUI->refreshGameTimeResources();
	TheWritableGlobalData->m_networkLatencyFontSize = v.networkLatencyFontSize;
	setInt(pref, "NetworkLatencyFontSize", v.networkLatencyFontSize);
	TheInGameUI->refreshNetworkLatencyResources();
	TheWritableGlobalData->m_playerInfoListFontSize = v.playerInfoListFontSize;
	setInt(pref, "PlayerInfoListFontSize", v.playerInfoListFontSize);
	TheInGameUI->refreshPlayerInfoListResources();
}

} // namespace OptionsValues
