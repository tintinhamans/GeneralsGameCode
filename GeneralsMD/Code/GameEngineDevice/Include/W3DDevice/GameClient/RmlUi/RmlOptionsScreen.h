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

// FILE: RmlOptionsScreen.h ////////////////////////////////////////////////////
// RmlScreen for Data/UI/Options.rml. Loads the document once, binds an RmlUi
// data model over the same OptionsValues:: functions the .wnd OptionsMenu
// callbacks use (see OptionsValues.h), and reads back the model on Accept.
// No option apply/save/default logic is duplicated here; this file only
// owns widget-shaped concerns: which fields the document shows, and turning
// button clicks into calls into the shared logic.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

class OptionPreferences;

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlOptionsScreen : public RmlScreen
{
public:
	RmlOptionsScreen();
	virtual ~RmlOptionsScreen() override;

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // same as Cancel

private:
	void loadCurrentValues();      // pulls OptionsValues::GetCurrent*() into the model
	void loadDefaultValues();      // pulls OptionsValues::GetDefault*() into the model (Defaults button)
	void applyAndSave();           // pushes model fields through OptionsValues::Apply*() (Accept button)

	void onSelectTab(const Rml::String &tab);
	void onAccept(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onDefaults(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectVideo(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { onSelectTab("video"); }
	void onSelectGraphics(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { onSelectTab("graphics"); }
	void onSelectAudio(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { onSelectTab("audio"); }
	void onSelectControls(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { onSelectTab("controls"); }
	void onSelectNetwork(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { onSelectTab("network"); }

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	OptionPreferences *m_pref = nullptr;

	// Data model fields, bound by pointer in load(). Kept as plain members (like the .wnd
	// version's GameWindow state) so Bind() has stable addresses for the document's lifetime.
	struct Model
	{
		Rml::String tab = "video";

		bool languageFilter = true;
		bool sendDelay = false;

		int scrollSpeed = 100;
		int musicVolume = 100;
		int sfxVolume = 100;
		int voiceVolume = 100;

		bool alternateMouse = false;
		bool retaliation = true;
		bool doubleClickAttackMove = false;

		int antiAliasing = 0;
		int textureFilter = 0;
		int anisotropy = 0;
	} m_model;
};

// Router entry points: MainMenu.cpp/QuitMenu.cpp call these instead of TheShell->getOptionsLayout()
// when !TheGlobalData->m_useLegacyMenus. One process-lifetime RmlOptionsScreen is reused across
// opens (mirrors TheShell's own options layout being created once and reused).
void OpenRmlOptionsScreen();
void CloseRmlOptionsScreen();
