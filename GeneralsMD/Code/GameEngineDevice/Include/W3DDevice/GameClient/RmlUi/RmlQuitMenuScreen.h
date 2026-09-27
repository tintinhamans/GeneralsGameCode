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

// FILE: RmlQuitMenuScreen.h //////////////////////////////////////////////////
// RmlScreen for Data/UI/QuitMenu.rml, the in-game Escape overlay (QuitMenu.wnd/
// QuitNoSave.wnd). Unlike MainMenu/Options/Credits this is NOT routed through
// RmlUiManager::showScreen()'s single-current-screen swap: it stays up over
// live gameplay (not a shell screen), and RmlOptionsScreen::hide() already
// hardcodes restoring the RmlUi *main menu* when Options closes (see
// ShowMainMenuForOptions), which would be wrong here. So this loads/shows/hides
// its own document directly, the same independent-overlay pattern RmlMessageBox
// uses, and RmlUiScreenRegistry's open/close pair for both .wnd paths just
// forwards into show()/hide() with the right save/load variant.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlQuitMenuScreen
{
public:
	static RmlQuitMenuScreen &instance();

	void open(bool noSaveVariant); ///< QuitMenu.wnd (false) or QuitNoSave.wnd (true)
	void close();
	bool isVisible() const;

private:
	RmlQuitMenuScreen() {}

	void load(Rml::Context *context);
	void refreshButtonState(); ///< labels/enabled state from QuitMenuActions, called on open()

	void onExit(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onRestart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onReturn(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSaveLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	struct Model
	{
		Rml::String restartLabel;
		Rml::String exitLabel;
		bool restartDisabled = false;
		bool showSaveLoad = true;
		bool saveLoadDisabled = false;
		bool optionsDisabled = false;
	} m_model;
};

// Registry entry points for both .wnd paths (see RmlUiManager::init()).
void OpenRmlQuitMenuScreen();
void CloseRmlQuitMenuScreen();
void OpenRmlQuitNoSaveScreen();
void CloseRmlQuitNoSaveScreen();
