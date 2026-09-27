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

#include "W3DDevice/GameClient/RmlUi/RmlOptionsScreen.h"

#include "Common/OptionPreferences.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <functional>

extern void DoResolutionDialog(); // MainMenu.cpp; same resolution-change confirm dialog as the .wnd Accept handler

//-------------------------------------------------------------------------------------------------
RmlOptionsScreen::RmlOptionsScreen()
{
}

RmlOptionsScreen::~RmlOptionsScreen()
{
	if (m_context && m_document)
	{
		m_context->UnloadDocument(m_document);
		m_context->RemoveDataModel("options");
	}
	delete m_pref;
}

void RmlOptionsScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;
	m_pref = NEW OptionPreferences;

	Rml::DataModelConstructor constructor = context->CreateDataModel("options");
	if (constructor)
	{
		constructor.Bind("tab", &m_model.tab);
		constructor.Bind("language_filter", &m_model.languageFilter);
		constructor.Bind("send_delay", &m_model.sendDelay);
		constructor.Bind("scroll_speed", &m_model.scrollSpeed);
		constructor.Bind("music_volume", &m_model.musicVolume);
		constructor.Bind("sfx_volume", &m_model.sfxVolume);
		constructor.Bind("voice_volume", &m_model.voiceVolume);
		constructor.Bind("alternate_mouse", &m_model.alternateMouse);
		constructor.Bind("retaliation", &m_model.retaliation);
		constructor.Bind("double_click_attack_move", &m_model.doubleClickAttackMove);
		constructor.Bind("anti_aliasing", &m_model.antiAliasing);
		constructor.Bind("texture_filter", &m_model.textureFilter);
		constructor.Bind("anisotropy", &m_model.anisotropy);

		constructor.Bind("resolution_index", &m_model.resolutionIndex);
		constructor.Bind("detail_level", &m_model.detailLevel);
		constructor.Bind("texture_resolution", &m_model.textureResolutionSliderPos);
		constructor.Bind("particle_cap", &m_model.particleCap);
		constructor.Bind("shadow_3d", &m_model.shadow3D);
		constructor.Bind("shadow_2d", &m_model.shadow2D);
		constructor.Bind("cloud_shadows", &m_model.cloudShadows);
		constructor.Bind("ground_lighting", &m_model.groundLighting);
		constructor.Bind("smooth_water", &m_model.smoothWater);
		constructor.Bind("extra_animations", &m_model.extraAnimations);
		constructor.Bind("no_dynamic_lod", &m_model.noDynamicLod);
		constructor.Bind("heat_effects", &m_model.heatEffects);
		constructor.Bind("building_occlusion", &m_model.buildingOcclusion);
		constructor.Bind("props", &m_model.props);

		constructor.Bind("lan_ip_index", &m_model.lanIPIndex);
		constructor.Bind("online_ip_index", &m_model.onlineIPIndex);
		constructor.Bind("http_proxy", &m_model.httpProxy);
		constructor.Bind("firewall_port_override", &m_model.firewallPortOverride);

		constructor.Bind("restricted", &m_model.restricted);
		constructor.Bind("detail_controls_disabled", &m_model.detailControlsDisabled);

		constructor.BindEventCallback("select_video", &RmlOptionsScreen::onSelectVideo, this);
		constructor.BindEventCallback("select_graphics", &RmlOptionsScreen::onSelectGraphics, this);
		constructor.BindEventCallback("select_audio", &RmlOptionsScreen::onSelectAudio, this);
		constructor.BindEventCallback("select_controls", &RmlOptionsScreen::onSelectControls, this);
		constructor.BindEventCallback("select_network", &RmlOptionsScreen::onSelectNetwork, this);
		constructor.BindEventCallback("accept", &RmlOptionsScreen::onAccept, this);
		constructor.BindEventCallback("cancel", &RmlOptionsScreen::onCancel, this);
		constructor.BindEventCallback("defaults", &RmlOptionsScreen::onDefaults, this);
		constructor.BindEventCallback("detail_level_changed", &RmlOptionsScreen::onDetailLevelChanged, this);
		constructor.BindEventCallback("detail_control_changed", &RmlOptionsScreen::onDetailControlChanged, this);
		constructor.BindEventCallback("firewall_refresh", &RmlOptionsScreen::onFirewallRefresh, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/Options.rml");
}

void RmlOptionsScreen::show()
{
	if (!m_document)
		return;

	// Remember what showScreen() swapped out to show us (the shell main menu, Credits, or nullptr
	// if nothing was current -- e.g. opened from the in-game quit menu overlay, which never goes
	// through showScreen). hide() restores exactly this instead of hardcoding the main menu.
	if (TheRmlUiManager)
	{
		RmlScreen *previous = TheRmlUiManager->getPreviousScreen();
		m_screenToRestore = (previous != this) ? previous : nullptr;
	}

	loadCurrentValues();
	populateSelectOptions();
	HideMainMenuForOptions(); // no-op if MainMenu.wnd isn't the current shell screen

	// Modal: blocks the shell behind it, matching the .wnd version hiding the main menu and
	// disabling in-game input while Options is up (see Shell::getOptionsLayout call sites).
	m_document->Show(Rml::ModalFlag::Modal);
}

void RmlOptionsScreen::hide()
{
	if (m_document)
		m_document->Hide();

	// Restore exactly what Options covered: the shell screen it swapped out (main menu, Credits),
	// or nothing if it was opened as an overlay over live gameplay (the in-game quit menu, which
	// stays up on its own and must never be replaced by the main menu here).
	if (m_screenToRestore && TheRmlUiManager)
		TheRmlUiManager->showScreen(m_screenToRestore);
	m_screenToRestore = nullptr;
}

bool RmlOptionsScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOptionsScreen::onBack()
{
	// Escape behaves like Cancel: OptionsMenu.cpp's buttonBack handler (GBM_SELECTED) never
	// calls saveOptions() -- it only deletes its OptionPreferences and destroys the layout.
	// Nothing in this menu applies live before Accept either (GSM_SLIDER_TRACK/GCM_SELECTED
	// only flip the detail combo to Custom, a UI-only state), so discarding here is exact,
	// not an approximation.
	hide();
	if (TheShell)
		TheShell->destroyOptionsLayout();
}

//-------------------------------------------------------------------------------------------------
void RmlOptionsScreen::loadDetailPresetValues(const OptionsValues::DetailPresetValues &values)
{
	m_model.textureResolutionSliderPos = values.textureResolutionSliderPos;
	m_model.particleCap = values.particleCap;
	m_model.shadow3D = values.shadow3D;
	m_model.shadow2D = values.shadow2D;
	m_model.cloudShadows = values.cloudShadows;
	m_model.groundLighting = values.groundLighting;
	m_model.smoothWater = values.smoothWater;
	m_model.extraAnimations = values.extraAnimations;
	m_model.noDynamicLod = values.noDynamicLod;
	m_model.heatEffects = values.heatEffects;
	m_model.buildingOcclusion = values.buildingOcclusion;
	m_model.props = values.props;
}

void RmlOptionsScreen::updateDetailControlsDisabled()
{
	// "Without a changeable preset, only an existing Custom detail can be edited" (OptionsMenuInit).
	m_model.detailControlsDisabled = m_model.restricted && m_model.detailLevel != OptionsValues::GetDetailCustomLevel();
}

void RmlOptionsScreen::loadCurrentValues()
{
	m_model.languageFilter = OptionsValues::GetCurrentLanguageFilter();
	m_model.sendDelay = OptionsValues::GetCurrentSendDelay();
	m_model.scrollSpeed = OptionsValues::GetCurrentScrollSpeedPercent();
	m_model.musicVolume = OptionsValues::GetCurrentMusicVolumePercent();
	m_model.sfxVolume = OptionsValues::GetCurrentSFXVolumePercent();
	m_model.voiceVolume = OptionsValues::GetCurrentVoiceVolumePercent();
	m_model.alternateMouse = OptionsValues::GetCurrentAlternateMouse();
	m_model.retaliation = OptionsValues::GetCurrentRetaliation();
	m_model.doubleClickAttackMove = OptionsValues::GetCurrentDoubleClickAttackMove();
	m_model.antiAliasing = OptionsValues::GetCurrentAntiAliasingIndex();
	m_model.textureFilter = OptionsValues::GetCurrentTextureFilterIndex();
	m_model.anisotropy = OptionsValues::GetCurrentAnisotropyIndex();

	m_model.resolutionIndex = OptionsValues::GetCurrentDisplayModeIndex();
	m_model.detailLevel = OptionsValues::GetCurrentDetailLevel();
	m_applyingDetailPreset = true; // loading straight from TheGlobalData is not "editing"
	loadDetailPresetValues(OptionsValues::GetCurrentGraphicsValues());
	m_applyingDetailPreset = false;

	m_model.lanIPIndex = OptionsValues::GetCurrentLANIPIndex(*m_pref);
	m_model.onlineIPIndex = OptionsValues::GetCurrentOnlineIPIndex(*m_pref);
	m_model.httpProxy = OptionsValues::GetCurrentHTTPProxy().str();
	Int port = OptionsValues::GetCurrentFirewallPortOverride();
	AsciiString portStr;
	if (port != 0)
		portStr.format("%d", port);
	m_model.firewallPortOverride = portStr.str();

	m_model.restricted = OptionsValues::IsOptionsRestrictedContext();
	updateDetailControlsDisabled();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlOptionsScreen::loadDefaultValues()
{
	m_model.languageFilter = OptionsValues::GetDefaultLanguageFilter();
	m_model.sendDelay = OptionsValues::GetDefaultSendDelay();
	m_model.scrollSpeed = OptionsValues::GetDefaultScrollSpeedPercent();
	m_model.musicVolume = OptionsValues::GetDefaultMusicVolumePercent();
	m_model.sfxVolume = OptionsValues::GetDefaultSFXVolumePercent();
	m_model.voiceVolume = OptionsValues::GetDefaultVoiceVolumePercent();
	m_model.alternateMouse = OptionsValues::GetDefaultAlternateMouse();
	m_model.retaliation = OptionsValues::GetDefaultRetaliation();
	m_model.doubleClickAttackMove = OptionsValues::GetDefaultDoubleClickAttackMove();
	// Anti-aliasing/texture filter/anisotropy/resolution/detail have no shared "default" (the
	// .wnd version's setDefaults() never resets these either -- see its ModifyDisplaySettings
	// compile-time gate), so they are left exactly as currently applied.

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

bool RmlOptionsScreen::applyAndSave()
{
	OptionsValues::ApplyLanguageFilter(*m_pref, m_model.languageFilter);

	// Every gate below mirrors a winGetEnabled() check the .wnd Accept handler makes; restricted
	// (in-game/online) disables the same set of controls there (see OptionsMenuInit).
	if (!m_model.restricted)
		OptionsValues::ApplySendDelay(*m_pref, m_model.sendDelay);

	OptionsValues::ApplyScrollSpeedPercent(*m_pref, m_model.scrollSpeed);
	OptionsValues::ApplyMusicVolumePercent(*m_pref, m_model.musicVolume);
	OptionsValues::ApplySFXVolumePercent(*m_pref, m_model.sfxVolume);
	OptionsValues::ApplyVoiceVolumePercent(*m_pref, m_model.voiceVolume);
	OptionsValues::ApplyMouseOptions(*m_pref, m_model.alternateMouse, m_model.retaliation, m_model.doubleClickAttackMove);
	OptionsValues::ApplyAntiAliasing(*m_pref, m_model.antiAliasing);
	OptionsValues::ApplyTextureFilter(*m_pref, m_model.textureFilter);
	OptionsValues::ApplyAnisotropy(*m_pref, m_model.anisotropy);

	// Custom detail values apply whenever the level is Custom, regardless of restriction --
	// matches saveOptions()'s "if (index == STATIC_GAME_LOD_CUSTOM)" block, which is not gated
	// on comboBoxDetail's enabled state.
	if (m_model.detailLevel == OptionsValues::GetDetailCustomLevel())
	{
		OptionsValues::DetailPresetValues values;
		values.textureResolutionSliderPos = m_model.textureResolutionSliderPos;
		values.particleCap = m_model.particleCap;
		values.shadow3D = m_model.shadow3D;
		values.shadow2D = m_model.shadow2D;
		values.cloudShadows = m_model.cloudShadows;
		values.groundLighting = m_model.groundLighting;
		values.smoothWater = m_model.smoothWater;
		values.extraAnimations = m_model.extraAnimations;
		values.noDynamicLod = m_model.noDynamicLod;
		values.heatEffects = m_model.heatEffects;
		values.buildingOcclusion = m_model.buildingOcclusion;
		values.props = m_model.props;
		OptionsValues::ApplyCustomGraphicsSettings(*m_pref, values);
	}

	if (!m_model.restricted)
		OptionsValues::ApplyDetailLevel(*m_pref, m_model.detailLevel);

	if (!m_model.restricted)
	{
		OptionsValues::ApplyLANIPChoice(*m_pref, OptionsValues::GetIPChoiceIP(m_model.lanIPIndex));
		OptionsValues::ApplyOnlineIPChoice(*m_pref, OptionsValues::GetIPChoiceIP(m_model.onlineIPIndex));
		OptionsValues::ApplyHTTPProxy(AsciiString(m_model.httpProxy.c_str()));
		OptionsValues::ApplyFirewallPortOverride(*m_pref, atoi(m_model.firewallPortOverride.c_str()));
	}

	// Resolution must be applied dead last, before the write: it can recreate the shell (see
	// ApplyDisplayMode()'s comment), and its pref entry still needs to make it into this write().
	// The caller (onAccept) shows the resolution confirm dialog only after hiding this screen and
	// destroying the options layout, exactly matching the .wnd Accept handler's own order.
	bool resolutionChanged = false;
	if (!m_model.restricted)
		resolutionChanged = OptionsValues::ApplyDisplayMode(*m_pref, m_model.resolutionIndex);

	m_pref->write();

	return resolutionChanged;
}

//-------------------------------------------------------------------------------------------------
void RmlOptionsScreen::populateSelectOptions()
{
	auto fillSelect = [this](const char *id, int count, int selected, const std::function<Rml::String(int)> &label)
	{
		Rml::Element *element = m_document->GetElementById(id);
		if (!element)
			return;
		Rml::String html;
		for (int i = 0; i < count; ++i)
		{
			html += "<option value=\"" + std::to_string(i) + "\">" + label(i) + "</option>";
		}
		element->SetInnerRML(html);
	};

	fillSelect("select_resolution", OptionsValues::GetDisplayModeCount(), m_model.resolutionIndex,
		[](int i)
		{
			OptionsValues::DisplayModeValues mode = OptionsValues::GetDisplayMode(i);
			return std::to_string(mode.width) + " x " + std::to_string(mode.height);
		});

	const int customLevel = OptionsValues::GetDetailCustomLevel();
	fillSelect("select_detail", customLevel + 1, m_model.detailLevel,
		[customLevel](int i)
		{
			return i == customLevel ? Rml::String("Custom") : Rml::String(OptionsValues::GetDetailLevelName(i).str());
		});

	fillSelect("select_lan_ip", OptionsValues::GetIPChoiceCount(), m_model.lanIPIndex,
		[](int i) { return Rml::String(OptionsValues::GetIPChoiceLabel(i).str()); });

	fillSelect("select_online_ip", OptionsValues::GetIPChoiceCount(), m_model.onlineIPIndex,
		[](int i) { return Rml::String(OptionsValues::GetIPChoiceLabel(i).str()); });
}

//-------------------------------------------------------------------------------------------------
void RmlOptionsScreen::onSelectTab(const Rml::String &tab)
{
	m_model.tab = tab;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("tab");
}

void RmlOptionsScreen::onAccept(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	const bool resolutionChanged = applyAndSave();
	hide();
	if (TheShell)
		TheShell->destroyOptionsLayout(); // symmetric with the .wnd Accept button (see MainMenu.cpp)

	// Same order as the .wnd Accept handler: destroy the layout, then show the confirm dialog.
	if (resolutionChanged)
		DoResolutionDialog();
}

void RmlOptionsScreen::onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	hide();
	if (TheShell)
		TheShell->destroyOptionsLayout();
}

void RmlOptionsScreen::onDefaults(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	loadDefaultValues();
}

void RmlOptionsScreen::onDetailLevelChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors GCM_SELECTED's showDetailPreset(): choosing a named preset previews its values
	// without itself counting as an edit; choosing Custom directly leaves values untouched.
	const int customLevel = OptionsValues::GetDetailCustomLevel();
	if (m_model.detailLevel >= 0 && m_model.detailLevel < customLevel)
	{
		m_applyingDetailPreset = true;
		loadDetailPresetValues(OptionsValues::GetDetailPresetPreview(m_model.detailLevel));
		m_applyingDetailPreset = false;
	}

	updateDetailControlsDisabled();
	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlOptionsScreen::onDetailControlChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors switchDetailToCustom(): editing any detail-preset control switches the combo to
	// Custom, unless this edit is itself the result of applying a preset preview above, or the
	// controls are disabled (restricted, non-Custom -- can't be edited at all in that state).
	if (m_applyingDetailPreset || m_model.detailControlsDisabled)
		return;
	if (m_model.detailLevel == OptionsValues::GetDetailCustomLevel())
		return;

	m_model.detailLevel = OptionsValues::GetDetailCustomLevel();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("detail_level");
}

void RmlOptionsScreen::onFirewallRefresh(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	OptionsValues::RefreshFirewallBehavior(*m_pref);
}

//-------------------------------------------------------------------------------------------------
void OpenRmlOptionsScreen()
{
	static RmlOptionsScreen s_screen;
	if (TheRmlUiManager)
		TheRmlUiManager->showScreen(&s_screen);
}

void CloseRmlOptionsScreen()
{
	if (TheRmlUiManager)
		TheRmlUiManager->hideCurrentScreen();
}
