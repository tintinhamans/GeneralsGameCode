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

// FILE: OptionsValues.h //////////////////////////////////////////////////////
// Widget-agnostic options logic shared between the .wnd OptionsMenu callbacks
// and the RmlUi options screen: applying a chosen value to TheGlobalData/
// TheAudio/etc, persisting it to OptionPreferences, and reading the built-in
// default. Neither side of this file touches a GameWindow or an RML element;
// callers own reading their own widget/data-model state in and out.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"

class OptionPreferences;

namespace OptionsValues
{
	// -- Graphics: detail preset -----------------------------------------------------------
	// Mirrors the fields showDetailPreset()/saveOptions()'s Custom block read/write, so a
	// preset preview or a Custom edit means the same thing on both .wnd and RmlUi.
	struct DetailPresetValues
	{
		Int textureResolutionSliderPos = 2; // sliderTextureResolution position (0..2)
		Int particleCap = 0;
		Bool shadow3D = false;
		Bool shadow2D = false;
		Bool cloudShadows = false;
		Bool groundLighting = false;
		Bool smoothWater = false;
		Bool extraAnimations = false;
		Bool noDynamicLod = false;
		Bool heatEffects = false;
		Bool buildingOcclusion = false; // not touched by presets, only by Custom edits (see .cpp)
		Bool props = false;
	};

	Int GetDetailCustomLevel();                    ///< STATIC_GAME_LOD_CUSTOM, the "Custom" combo position
	Int GetCurrentDetailLevel();                   ///< current StaticGameLODLevel as an Int
	AsciiString GetDetailLevelName(Int level);      ///< combo box label for a preset level (Low/Medium/...)

	// Values a preset would show if chosen (does not apply anything); buildingOcclusion is left
	// at its current value since presets never touch it, matching showDetailPreset().
	DetailPresetValues GetDetailPresetPreview(Int level);
	// Values as currently applied (TheGlobalData), for populating the screen when level==Custom.
	DetailPresetValues GetCurrentGraphicsValues();

	void ApplyDetailLevel(OptionPreferences &pref, Int level);                          ///< named preset only
	void ApplyCustomGraphicsSettings(OptionPreferences &pref, const DetailPresetValues &values); ///< Custom only

	// -- Graphics: resolution ----------------------------------------------------------------
	struct DisplayModeValues { Int width = 0; Int height = 0; Int bitDepth = 0; };

	Int GetDisplayModeCount();
	DisplayModeValues GetDisplayMode(Int index);
	Int GetCurrentDisplayModeIndex();

	// Changes the display mode if different from the current one; returns TRUE if it did (the
	// caller must then call DoResolutionDialog() -- see MainMenu.cpp -- exactly once, last,
	// same as the .wnd Accept handler: "MUST NEVER ADD ANOTHER OPTION HERE AT THE END").
	Bool ApplyDisplayMode(OptionPreferences &pref, Int index);

	// -- Network -------------------------------------------------------------------------------
	Int GetIPChoiceCount();
	AsciiString GetIPChoiceLabel(Int index);
	UnsignedInt GetIPChoiceIP(Int index);           ///< the raw IP a combo box would store as item data
	Int GetCurrentLANIPIndex(OptionPreferences &pref);
	Int GetCurrentOnlineIPIndex(OptionPreferences &pref);
	void ApplyLANIPChoice(OptionPreferences &pref, UnsignedInt ip);
	void ApplyOnlineIPChoice(OptionPreferences &pref, UnsignedInt ip);

	AsciiString GetCurrentHTTPProxy();
	void ApplyHTTPProxy(const AsciiString &proxy);

	Int GetCurrentFirewallPortOverride();          ///< 0 if unset
	void ApplyFirewallPortOverride(OptionPreferences &pref, Int port);

	void RefreshFirewallBehavior(OptionPreferences &pref); ///< the Firewall Refresh button

	// TRUE while in a game (not the shell) or connected online: the .wnd version disables
	// LAN/online IP, send delay, firewall refresh/port override, HTTP proxy, detail and
	// resolution combos in this state (see OptionsMenuInit).
	Bool IsOptionsRestrictedContext();


	// -- Apply + persist -----------------------------------------------------------------
	void ApplyLanguageFilter(OptionPreferences &pref, Bool enabled);
	void ApplySendDelay(OptionPreferences &pref, Bool enabled);

	// index is the combo box position, using the same ordering as the .wnd combo boxes.
	void ApplyAntiAliasing(OptionPreferences &pref, Int comboIndex);
	void ApplyTextureFilter(OptionPreferences &pref, Int comboIndex);
	void ApplyAnisotropy(OptionPreferences &pref, Int comboIndex);

	void ApplyMouseOptions(OptionPreferences &pref, Bool alternateMouse, Bool retaliation, Bool doubleClickAttackMove);
	void ApplyScrollSpeedPercent(OptionPreferences &pref, Int percent);

	void ApplyMusicVolumePercent(OptionPreferences &pref, Int percent);
	void ApplySFXVolumePercent(OptionPreferences &pref, Int percent);
	void ApplyVoiceVolumePercent(OptionPreferences &pref, Int percent);

	// -- Current / default values, for populating a fresh screen ------------------------
	Bool GetCurrentLanguageFilter();
	Bool GetCurrentSendDelay();
	Int GetCurrentAntiAliasingIndex();
	Int GetCurrentTextureFilterIndex();
	Int GetCurrentAnisotropyIndex();
	Bool GetCurrentAlternateMouse();
	Bool GetCurrentRetaliation();
	Bool GetCurrentDoubleClickAttackMove();
	Int GetCurrentScrollSpeedPercent();
	Int GetCurrentMusicVolumePercent();
	Int GetCurrentSFXVolumePercent();
	Int GetCurrentVoiceVolumePercent();

	Bool GetDefaultLanguageFilter();
	Bool GetDefaultSendDelay();
	Bool GetDefaultAlternateMouse();
	Bool GetDefaultRetaliation();
	Bool GetDefaultDoubleClickAttackMove();
	Int GetDefaultScrollSpeedPercent();
	Int GetDefaultMusicVolumePercent();
	Int GetDefaultSFXVolumePercent();
	Int GetDefaultVoiceVolumePercent();
}
