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

// FILE: RmlCreditsScreen.h ////////////////////////////////////////////////////
// RmlScreen for Data/UI/CreditsMenu.rml. Reuses TheCredits (CreditsManager,
// Core/GameEngine/Credits.h) exactly as CreditsMenu.cpp does -- same
// Data/INI/Credits-driven scroll timing/content -- but renders its displayed
// lines as absolutely-positioned RmlUi elements (SetInnerRML each frame)
// instead of CreditsManager::draw()'s direct TheDisplay calls, since RmlUi
// owns rendering while this screen is up. Registered in RmlUiManager::init()
// so Shell::push("Menus/CreditsMenu.wnd") (from MainMenuActions::openCreditsMenu())
// routes here exactly like MainMenu/Options (see RmlUiScreenRegistry.h).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlCreditsScreen : public RmlScreen
{
public:
	RmlCreditsScreen();
	virtual ~RmlCreditsScreen() override;

	static RmlCreditsScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // Escape: skip credits immediately, same as CreditsMenuInput's KEY_ESC
	virtual void update() override; // TheCredits->update() + resync the rendered lines each frame

private:
	void syncDisplayedLines();
	void onBackClicked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // the Back button: same as Escape

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status
};

// Router entry points: Shell::push/pop route "Menus/CreditsMenu.wnd" here via the registry.
void OpenRmlCreditsScreen();
void CloseRmlCreditsScreen();
