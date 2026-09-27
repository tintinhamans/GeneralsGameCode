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

// FILE: RmlMainMenuScreen.h ///////////////////////////////////////////////////
// RmlScreen for Data/UI/MainMenu.rml. Owns its own panel navigation (main,
// single player, multiplayer, load/replay, difficulty select) as CSS-class
// driven state instead of TheTransitionHandler/.wnd drop-downs; every button's
// terminal effect is a MainMenuActions call. Registered in RmlUiManager::init()
// so Shell::push("Menus/MainMenu.wnd") (from Shell::showShell()) routes here
// exactly like Options does (see RmlUiScreenRegistry.h).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlMainMenuScreen : public RmlScreen
{
public:
	RmlMainMenuScreen();
	virtual ~RmlMainMenuScreen() override;

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // Escape: step back one panel, or no-op already at "main"
	virtual void update() override; // download/patch pump + GameSpy/HTTP think while visible

private:
	void setPanel(const Rml::String &panel);

	void onGoSingle(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setPanel("single"); }
	void onGoMulti(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setPanel("multi"); }
	void onGoLoadReplay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setPanel("loadreplay"); }
	void onBackToMain(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoCredits(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoExit(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	void onGoSkirmish(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectUSA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectGLA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectChina(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectChallenge(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onDiffBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	// Faction hover previews on the "single" panel: mirrors MainMenu.cpp's GBM_MOUSE_ENTERING/
	// LEAVING on ButtonUSA/GLA/China/Challenge (TheTransitionHandler->setGroup("MainMenuFaction*")),
	// simplified to a CSS opacity fade instead of porting the .wnd transition-group animation.
	// Challenge previews the Training art, matching the .wnd's "MainMenuFactionTraining" group name.
	void onPreviewUSA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setHoverFaction("USA"); }
	void onPreviewGLA(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setHoverFaction("GLA"); }
	void onPreviewChina(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setHoverFaction("China"); }
	void onPreviewChallenge(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setHoverFaction("Training"); }
	void onPreviewClear(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { setHoverFaction(""); }
	void setHoverFaction(const Rml::String &faction);

	void onSelectEasy(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectMedium(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelectHard(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	void onGoOnline(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoNetwork(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoLoadGame(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onGoReplay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	void selectFaction(const char *campaignName, const char *factionLabel, bool challenge);
	void startAtDifficulty(int diff);
	void goBackFromDifficulty(); // shared by the diff_back button event and onBack()/Escape

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	bool m_challengePending = false; // true if the pending difficulty pick launches ChallengeMenu.wnd

	struct Model
	{
		Rml::String panel = "main";
		Rml::String selectedFaction; // "", "USA", "GLA", "China", "Training"
		Rml::String hoverFaction; // "", "USA", "GLA", "China", "Training" -- single panel preview only
		Rml::String version;
	} m_model;
};

// Router entry points: Shell::push/pop route "Menus/MainMenu.wnd" here via the registry, and
// OptionsMenu.cpp's HideMainMenuForOptions/ShowMainMenuForOptions use the same pair to cover/
// restore this screen while Options is up (mirrors OpenRmlOptionsScreen/CloseRmlOptionsScreen).
void OpenRmlMainMenuScreen();
void CloseRmlMainMenuScreen();
