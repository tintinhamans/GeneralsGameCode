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
#include "GameClient/GUI/GUICallbacks/Menus/OptionsValues.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

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

		constructor.BindEventCallback("select_video", &RmlOptionsScreen::onSelectVideo, this);
		constructor.BindEventCallback("select_graphics", &RmlOptionsScreen::onSelectGraphics, this);
		constructor.BindEventCallback("select_audio", &RmlOptionsScreen::onSelectAudio, this);
		constructor.BindEventCallback("select_controls", &RmlOptionsScreen::onSelectControls, this);
		constructor.BindEventCallback("select_network", &RmlOptionsScreen::onSelectNetwork, this);
		constructor.BindEventCallback("accept", &RmlOptionsScreen::onAccept, this);
		constructor.BindEventCallback("cancel", &RmlOptionsScreen::onCancel, this);
		constructor.BindEventCallback("defaults", &RmlOptionsScreen::onDefaults, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/Options.rml");
}

void RmlOptionsScreen::show()
{
	if (!m_document)
		return;

	loadCurrentValues();
	HideMainMenuForOptions(); // no-op if MainMenu.wnd isn't the current shell screen

	// Modal: blocks the shell behind it, matching the .wnd version hiding the main menu and
	// disabling in-game input while Options is up (see Shell::getOptionsLayout call sites).
	m_document->Show(Rml::ModalFlag::Modal);
}

void RmlOptionsScreen::hide()
{
	if (m_document)
		m_document->Hide();
	ShowMainMenuForOptions();
}

bool RmlOptionsScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOptionsScreen::onBack()
{
	// Escape behaves like Cancel: discard the model's in-progress edits, nothing applied/saved.
	hide();
	if (TheShell)
		TheShell->destroyOptionsLayout();
}

//-------------------------------------------------------------------------------------------------
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
	// Anti-aliasing/texture filter/anisotropy have no shared "default" defined yet (the .wnd
	// version doesn't reset these on Defaults either -- see setDefaults()'s ModifyDisplaySettings
	// gate), so leave them as currently applied.

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlOptionsScreen::applyAndSave()
{
	OptionsValues::ApplyLanguageFilter(*m_pref, m_model.languageFilter);
	OptionsValues::ApplySendDelay(*m_pref, m_model.sendDelay);
	OptionsValues::ApplyScrollSpeedPercent(*m_pref, m_model.scrollSpeed);
	OptionsValues::ApplyMusicVolumePercent(*m_pref, m_model.musicVolume);
	OptionsValues::ApplySFXVolumePercent(*m_pref, m_model.sfxVolume);
	OptionsValues::ApplyVoiceVolumePercent(*m_pref, m_model.voiceVolume);
	OptionsValues::ApplyMouseOptions(*m_pref, m_model.alternateMouse, m_model.retaliation, m_model.doubleClickAttackMove);
	OptionsValues::ApplyAntiAliasing(*m_pref, m_model.antiAliasing);
	OptionsValues::ApplyTextureFilter(*m_pref, m_model.textureFilter);
	OptionsValues::ApplyAnisotropy(*m_pref, m_model.anisotropy);

	m_pref->write();
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
	applyAndSave();
	hide();
	if (TheShell)
		TheShell->destroyOptionsLayout(); // symmetric with the .wnd Accept button (see MainMenu.cpp)
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
