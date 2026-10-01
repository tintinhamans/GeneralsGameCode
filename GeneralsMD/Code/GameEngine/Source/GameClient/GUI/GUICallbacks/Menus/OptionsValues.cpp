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

#include "PreRTS.h"
#include "GameClient/GUI/GUICallbacks/Menus/OptionsValues.h"

#include "gamespy/ghttp/ghttp.h"

#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "Common/Registry.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/GameClient.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameLogic/GameLogic.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/texturefilter.h"
#include "WWDownload/Registry.h"

#include "../OnlineServices_Init.h"

// Owned by OptionsMenu.cpp; the resolution-change dialog (MainMenu.cpp's DoResolutionDialog())
// reads/writes the same globals, so ApplyDisplayMode() below must not shadow them with its own copy.
extern DisplaySettings oldDispSettings, newDispSettings;
extern Bool dispChanged;

namespace OptionsValues
{

//-------------------------------------------------------------------------------------------------
void ApplyLanguageFilter(OptionPreferences &pref, Bool enabled)
{
	TheWritableGlobalData->m_languageFilterPref = enabled;
	pref["LanguageFilter"] = enabled ? "true" : "false";
}

void ApplySendDelay(OptionPreferences &pref, Bool enabled)
{
	TheWritableGlobalData->m_firewallSendDelay = enabled;
	pref["SendDelay"] = enabled ? "yes" : "no";
}

//-------------------------------------------------------------------------------------------------
void ApplyAntiAliasing(OptionPreferences &pref, Int comboIndex)
{
	comboIndex = clamp((int)OptionPreferences::AntiAliasingMode_OFF, comboIndex, (int)OptionPreferences::AntiAliasingMode_MSAA_8X);
	Int mode = (comboIndex > 0) ? 1 << comboIndex : WW3D::MULTISAMPLE_MODE_NONE;

	TheWritableGlobalData->m_antiAliasLevel = mode;
	AsciiString prefString;
	prefString.format("%d", mode);
	pref["AntiAliasing"] = prefString;
}

// Same ordering as OptionsMenu.cpp's textureFilterModes[].
static const TextureFilterClass::TextureFilterMode s_textureFilterModes[] =
{
	TextureFilterClass::TEXTURE_FILTER_BILINEAR,
	TextureFilterClass::TEXTURE_FILTER_TRILINEAR,
	TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC,
};
static const Int s_anisotropyLevels[] = { 2, 4, 8, 16 };

void ApplyTextureFilter(OptionPreferences &pref, Int comboIndex)
{
	if (comboIndex < 0 || comboIndex >= ARRAY_SIZE(s_textureFilterModes))
		return;

	TheWritableGlobalData->m_textureFilteringMode = s_textureFilterModes[comboIndex];
	pref["TextureFilter"] = TextureFilterClass::TextureFilterModeString[s_textureFilterModes[comboIndex]];

	WW3D::Set_Texture_Filter(TheWritableGlobalData->m_textureFilteringMode);
	TheWritableGlobalData->m_textureFilteringMode = WW3D::Get_Texture_Filter();
}

void ApplyAnisotropy(OptionPreferences &pref, Int comboIndex)
{
	if (comboIndex < 0 || comboIndex >= ARRAY_SIZE(s_anisotropyLevels))
		return;

	TheWritableGlobalData->m_textureAnisotropyLevel = s_anisotropyLevels[comboIndex];
	AsciiString prefString;
	prefString.format("%d", s_anisotropyLevels[comboIndex]);
	pref["AnisotropyLevel"] = prefString;

	WW3D::Set_Anisotropy_Level(TheWritableGlobalData->m_textureAnisotropyLevel);
	TheWritableGlobalData->m_textureAnisotropyLevel = WW3D::Get_Anisotropy_Level();
}

//-------------------------------------------------------------------------------------------------
void ApplyMouseOptions(OptionPreferences &pref, Bool alternateMouse, Bool retaliation, Bool doubleClickAttackMove)
{
	TheWritableGlobalData->m_useAlternateMouse = alternateMouse;
	pref["UseAlternateMouse"] = alternateMouse ? "yes" : "no";

	TheWritableGlobalData->m_clientRetaliationModeEnabled = retaliation;
	pref["Retaliation"] = retaliation ? "yes" : "no";

	TheWritableGlobalData->m_doubleClickAttackMove = doubleClickAttackMove;
	pref["UseDoubleClickAttackMove"] = doubleClickAttackMove ? "yes" : "no";
}

void ApplyScrollSpeedPercent(OptionPreferences &pref, Int percent)
{
	if (percent <= 0)
		return;

	TheWritableGlobalData->m_keyboardScrollFactor = percent / 100.0f;
	AsciiString prefString;
	prefString.format("%d", percent);
	pref["ScrollFactor"] = prefString;
}

//-------------------------------------------------------------------------------------------------
void ApplyMusicVolumePercent(OptionPreferences &pref, Int percent)
{
	AsciiString prefString;
	prefString.format("%d", percent);
	pref["MusicVolume"] = prefString;
	TheAudio->setVolume(percent / 100.0f, (AudioAffect)(AudioAffect_Music | AudioAffect_SystemSetting));
}

void ApplySFXVolumePercent(OptionPreferences &pref, Int percent)
{
	// 2D and 3D sound share one slider; a relative offset from AudioSettings lowers whichever
	// side is set to give ground, matching the .wnd behavior exactly.
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

	AsciiString prefString;
	prefString.format("%d", REAL_TO_INT(sound2DVolume * 100.0f));
	pref["SFXVolume"] = prefString;
	prefString.format("%d", REAL_TO_INT(sound3DVolume * 100.0f));
	pref["SFX3DVolume"] = prefString;
}

void ApplyVoiceVolumePercent(OptionPreferences &pref, Int percent)
{
	AsciiString prefString;
	prefString.format("%d", percent);
	pref["VoiceVolume"] = prefString;
	TheAudio->setVolume(percent / 100.0f, (AudioAffect)(AudioAffect_Speech | AudioAffect_SystemSetting));
}

void ApplyBrightnessPercent(OptionPreferences &pref, Int percent)
{
	// Exact copy of the .wnd SliderGamma math in saveOptions(): generates a value between 0.6 and 2.0.
	Real gammaval = 1.0f;
	if (percent < 50)
	{
		if (percent <= 0)
			gammaval = 0.6f;
		else
			gammaval = 1.0f - (0.4f) * (Real)(50 - percent) / 50.0f;
	}
	else if (percent > 50)
	{
		gammaval = 1.0f + (1.0f) * (Real)(percent - 50) / 50.0f;
	}

	AsciiString prefString;
	prefString.format("%d", percent);
	pref["Gamma"] = prefString;

	if (TheGlobalData->m_displayGamma != gammaval)
	{
		TheWritableGlobalData->m_displayGamma = gammaval;
		TheDisplay->setGamma(TheGlobalData->m_displayGamma, 0.0f, 1.0f, FALSE);
	}
}

//-------------------------------------------------------------------------------------------------
Bool GetCurrentLanguageFilter() { return TheGlobalData->m_languageFilterPref; }
Bool GetCurrentSendDelay() { return TheGlobalData->m_firewallSendDelay; }

Int GetCurrentAntiAliasingIndex()
{
	Int mode = TheGlobalData->m_antiAliasLevel;
	if (mode <= 0)
		return (Int)OptionPreferences::AntiAliasingMode_OFF;
	Int index = 0;
	while ((1 << index) < mode && index < (Int)OptionPreferences::AntiAliasingMode_MSAA_8X)
		++index;
	return index;
}

Int GetCurrentTextureFilterIndex()
{
	for (Int i = 0; i < ARRAY_SIZE(s_textureFilterModes); ++i)
		if (s_textureFilterModes[i] == TheGlobalData->m_textureFilteringMode)
			return i;
	return 0;
}

Int GetCurrentAnisotropyIndex()
{
	for (Int i = 0; i < ARRAY_SIZE(s_anisotropyLevels); ++i)
		if (s_anisotropyLevels[i] == TheGlobalData->m_textureAnisotropyLevel)
			return i;
	return 0;
}

Bool GetCurrentAlternateMouse() { return TheGlobalData->m_useAlternateMouse; }
Bool GetCurrentRetaliation() { return TheGlobalData->m_clientRetaliationModeEnabled; }
Bool GetCurrentDoubleClickAttackMove() { return TheGlobalData->m_doubleClickAttackMove; }
Int GetCurrentScrollSpeedPercent() { return (Int)(TheGlobalData->m_keyboardScrollFactor * 100.0f); }
Int GetCurrentMusicVolumePercent() { return REAL_TO_INT(TheAudio->getVolume(AudioAffect_Music) * 100.0f); }
Int GetCurrentSFXVolumePercent() { return REAL_TO_INT(TheAudio->getVolume(AudioAffect_Sound) * 100.0f); }
Int GetCurrentVoiceVolumePercent() { return REAL_TO_INT(TheAudio->getVolume(AudioAffect_Speech) * 100.0f); }
Int GetCurrentBrightnessPercent(OptionPreferences &pref) { return REAL_TO_INT(pref.getGammaValue()); }

//-------------------------------------------------------------------------------------------------
Bool GetDefaultLanguageFilter() { return TRUE; }
Bool GetDefaultSendDelay() { return FALSE; }
Bool GetDefaultAlternateMouse() { return FALSE; }
Bool GetDefaultRetaliation() { return TRUE; }
Bool GetDefaultDoubleClickAttackMove() { return FALSE; }
Int GetDefaultScrollSpeedPercent() { return (Int)(TheGlobalData->m_keyboardDefaultScrollFactor * 100.0f); }
Int GetDefaultMusicVolumePercent() { return REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f); }
Int GetDefaultSFXVolumePercent()
{
	Real maxVolume = MAX(TheAudio->getAudioSettings()->m_defaultSoundVolume, TheAudio->getAudioSettings()->m_default3DSoundVolume);
	return REAL_TO_INT(maxVolume * 100.0f);
}
Int GetDefaultVoiceVolumePercent() { return REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f); }
Int GetDefaultBrightnessPercent() { return 50; }

//-------------------------------------------------------------------------------------------------
// Graphics: detail preset
//-------------------------------------------------------------------------------------------------
Int GetDetailCustomLevel() { return (Int)STATIC_GAME_LOD_CUSTOM; }
Int GetCurrentDetailLevel() { return (Int)TheGameLODManager->getStaticLODLevel(); }

AsciiString GetDetailLevelName(Int level)
{
	return AsciiString(TheGameLODManager->getStaticGameLODLevelName((StaticGameLODLevel)level));
}

DetailPresetValues GetDetailPresetPreview(Int level)
{
	DetailPresetValues values;
	if (level < 0 || level >= STATIC_GAME_LOD_CUSTOM)
		return values;

	StaticGameLODInfo info = TheGameLODManager->getStaticLODPreview((StaticGameLODLevel)level);
	values.textureResolutionSliderPos = 2 - info.m_textureReduction;
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
	// buildingOcclusion: presets never set it (see showDetailPreset()); leave at current value.
	values.buildingOcclusion = TheGlobalData->m_enableBehindBuildingMarkers;
	return values;
}

DetailPresetValues GetCurrentGraphicsValues()
{
	DetailPresetValues values;
	values.textureResolutionSliderPos = 2 - TheGlobalData->m_textureReductionFactor;
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
	const Bool changed = TheGameLODManager->setStaticLODLevel((StaticGameLODLevel)level);
	if (changed)
		pref["StaticGameLOD"] = TheGameLODManager->getStaticGameLODLevelName(TheGameLODManager->getStaticLODLevel());
}

void ApplyCustomGraphicsSettings(OptionPreferences &pref, const DetailPresetValues &values)
{
	Int textureReduction = 2 - values.textureResolutionSliderPos;
	AsciiString prefString;
	prefString.format("%d", textureReduction);
	pref["TextureReduction"] = prefString;
	TheWritableGlobalData->m_textureReductionFactor = textureReduction;
	TheGameClient->setTextureLOD(textureReduction);

	TheWritableGlobalData->m_useShadowVolumes = values.shadow3D;
	pref["UseShadowVolumes"] = values.shadow3D ? "yes" : "no";

	TheWritableGlobalData->m_useShadowDecals = values.shadow2D;
	pref["UseShadowDecals"] = values.shadow2D ? "yes" : "no";

	TheWritableGlobalData->m_useCloudMap = values.cloudShadows;
	pref["UseCloudMap"] = values.cloudShadows ? "yes" : "no";

	TheWritableGlobalData->m_useLightMap = values.groundLighting;
	pref["UseLightMap"] = values.groundLighting ? "yes" : "no";

	TheWritableGlobalData->m_showSoftWaterEdge = values.smoothWater;
	pref["ShowSoftWaterEdge"] = values.smoothWater ? "yes" : "no";

	TheWritableGlobalData->m_useDrawModuleLOD = !values.extraAnimations;
	TheWritableGlobalData->m_useTreeSway = !TheWritableGlobalData->m_useDrawModuleLOD; // borrows the same setting
	pref["ExtraAnimations"] = TheGlobalData->m_useDrawModuleLOD ? "no" : "yes";

	TheWritableGlobalData->m_enableDynamicLOD = !values.noDynamicLod;
	pref["DynamicLOD"] = TheGlobalData->m_enableDynamicLOD ? "yes" : "no";

	TheWritableGlobalData->m_useHeatEffects = values.heatEffects;
	pref["HeatEffects"] = values.heatEffects ? "yes" : "no";

	TheWritableGlobalData->m_enableBehindBuildingMarkers = values.buildingOcclusion;
	pref["BuildingOcclusion"] = values.buildingOcclusion ? "yes" : "no";

	TheWritableGlobalData->m_useTrees = values.props;
	pref["ShowTrees"] = values.props ? "yes" : "no";

	prefString.format("%d", values.particleCap);
	pref["MaxParticleCount"] = prefString;
	TheWritableGlobalData->m_maxParticleCount = values.particleCap;
}

//-------------------------------------------------------------------------------------------------
// Graphics: resolution
//-------------------------------------------------------------------------------------------------
Int GetDisplayModeCount() { return TheDisplay->getDisplayModeCount(); }

DisplayModeValues GetDisplayMode(Int index)
{
	DisplayModeValues mode;
	if (index < 0 || index >= TheDisplay->getDisplayModeCount())
		return mode;
	TheDisplay->getDisplayModeDescription(index, &mode.width, &mode.height, &mode.bitDepth);
	return mode;
}

Int GetCurrentDisplayModeIndex()
{
	UnsignedInt width = TheDisplay->getWidth();
	UnsignedInt height = TheDisplay->getHeight();
	for (Int i = 0; i < TheDisplay->getDisplayModeCount(); ++i)
	{
		Int w, h, bpp;
		TheDisplay->getDisplayModeDescription(i, &w, &h, &bpp);
		if ((UnsignedInt)w == width && (UnsignedInt)h == height)
			return i;
	}
	return -1;
}

// TheSuperHackers @info Resolution must be applied last -- see saveOptions()'s original comment:
// changing it can recreate the shell and destroy whatever layout/screen called this.
Bool ApplyDisplayMode(OptionPreferences &pref, Int index)
{
	if (index < 0 || index >= TheDisplay->getDisplayModeCount())
		return FALSE;

	Int xres, yres, bitDepth;
	oldDispSettings.xRes = TheDisplay->getWidth();
	oldDispSettings.yRes = TheDisplay->getHeight();
	oldDispSettings.bitDepth = TheDisplay->getBitDepth();
	oldDispSettings.windowed = TheDisplay->getWindowed();

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

static Int findIPIndex(UnsignedInt wantedIP)
{
	IPEnumeration IPs;
	Int i = 0;
	for (EnumeratedIP *ip = IPs.getAddresses(); ip; ip = ip->getNext(), ++i)
		if (ip->getIP() == wantedIP)
			return i;
	return -1;
}

Int GetCurrentLANIPIndex(OptionPreferences &pref)
{
	Int index = findIPIndex(pref.getLANIPAddress());
	return index >= 0 ? index : 0;
}

Int GetCurrentOnlineIPIndex(OptionPreferences &pref)
{
	Int index = findIPIndex(pref.getOnlineIPAddress());
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

AsciiString GetCurrentHTTPProxy()
{
	std::string proxy;
	GetStringFromRegistry("", "Proxy", proxy);
	return AsciiString(proxy.c_str());
}

void ApplyHTTPProxy(const AsciiString &proxy)
{
	SetStringInRegistry("", "Proxy", proxy.str());
	ghttpSetProxy(proxy.str());
}

// The GameTextLanguages code of the installed game (its registry language), or null if it has none.
static const char *GetInstalledTextLanguageCode()
{
	static const struct { const char *registry; const char *code; } installed[] =
	{
		{ "english", "us" }, { "german", "de" }, { "french", "fr" }, { "spanish", "es" }, { "italian", "it" },
		{ "korean", "ko" }, { "chinese", "zh" }, { "brazilian", "bp" }, { "polish", "pl" },
		{ "russian", "ru" }, { "ukrainian", "uk" }, { "arabic", "ar" },
	};
	const AsciiString registry = GetRegistryLanguage();
	for (size_t i = 0; i < ARRAY_SIZE(installed); ++i)
	{
		if (registry.compareNoCase(installed[i].registry) == 0)
			return installed[i].code;
	}
	return nullptr;
}

Int GetInstalledTextLanguageChoice()
{
	const char *code = GetInstalledTextLanguageCode();
	for (Int i = 0; code && i < GameTextLanguageCount; ++i)
	{
		if (stricmp(code, GameTextLanguages[i].code) == 0)
			return i + 1;
	}
	return 0;
}

Int GetTextLanguageChoiceCount()
{
	return GameTextLanguageCount + 1;
}

UnicodeString GetTextLanguageChoiceName(Int index)
{
	if (index <= 0 || index > GameTextLanguageCount)
		return UnicodeString::TheEmptyString;
	UnicodeString name(GameTextLanguages[index - 1].nativeName);
	if (index != GetInstalledTextLanguageChoice())
		return name;
	// "<name> (installed)": the installed language's entry, which follows the installed game.
	UnicodeString label = TheGameText->fetch("GO:GUI:LanguageNameInstalled");
	const WideChar *marker = wcsstr(label.str(), L"%s");
	if (!marker)
		return name;
	UnicodeString result;
	result.set(label.str(), (Int)(marker - label.str()));
	result.concat(name);
	result.concat(marker + 2);
	return result;
}

Int GetCurrentTextLanguageChoice()
{
	// What settings.json asks for, which is what the next start loads (not necessarily what this
	// session loaded, if it was changed since or its files were missing). Empty or the installed
	// language's own code is the installed language.
	const std::string &code = NGMP_OnlineServicesManager::Settings.UI_GetLanguage();
	for (Int i = 0; i < GameTextLanguageCount; ++i)
	{
		if (stricmp(code.c_str(), GameTextLanguages[i].code) == 0)
		{
			const Int choice = i + 1;
			return choice == GetInstalledTextLanguageChoice() ? GetInstalledTextLanguageChoice() : choice;
		}
	}
	return GetInstalledTextLanguageChoice();
}

void ApplyTextLanguageChoice(Int index)
{
	if (index == GetCurrentTextLanguageChoice())
		return;
	// The installed language (or the entry for an unknown one) stores nothing: it follows the install.
	const bool known = index > 0 && index <= GameTextLanguageCount && index != GetInstalledTextLanguageChoice();
	NGMP_OnlineServicesManager::Settings.UI_SetLanguage(known ? GameTextLanguages[index - 1].code : "");
}

static const char *const InterfaceWidths[] = { "full", "21:9", "16:9" };

Int GetCurrentInterfaceWidth()
{
	const std::string &width = NGMP_OnlineServicesManager::Settings.UI_GetInterfaceWidth();
	for (Int i = 1; i < (Int)ARRAY_SIZE(InterfaceWidths); ++i)
	{
		if (width == InterfaceWidths[i])
			return i;
	}
	return 0;
}

void ApplyInterfaceWidth(Int index)
{
	if (index < 0 || index >= (Int)ARRAY_SIZE(InterfaceWidths) || index == GetCurrentInterfaceWidth())
		return;
	NGMP_OnlineServicesManager::Settings.UI_SetInterfaceWidth(InterfaceWidths[index]);
}

Int GetCurrentFirewallPortOverride() { return TheGlobalData->m_firewallPortOverride; }

void ApplyFirewallPortOverride(OptionPreferences &pref, Int port)
{
	if (port < 0 || port > 65535)
		port = 0;
	if (TheGlobalData->m_firewallPortOverride == port)
		return;
	TheWritableGlobalData->m_firewallPortOverride = port;
	AsciiString prefString;
	prefString.format("%d", port);
	pref["FirewallPortOverride"] = prefString;
}

void RefreshFirewallBehavior(OptionPreferences &pref)
{
	// Setting the behavior to unknown forces the firewall helper to re-detect it the next
	// time we log into GameSpy/WOL/whatever.
	TheWritableGlobalData->m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
	AsciiString prefString;
	prefString.format("%d", TheGlobalData->m_firewallBehavior);
	pref["FirewallBehavior"] = prefString;
}

Bool IsOptionsRestrictedContext()
{
	return (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL)
		|| NGMP_OnlineServicesManager::GetInstance() != nullptr;
}

} // namespace OptionsValues
