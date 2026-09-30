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

// FILE: RmlChallengeMenuScreen.h /////////////////////////////////////////////
// RmlUi view of Menus/ChallengeMenu.wnd (main menu > Single Player > Generals Challenge). All the
// rules live in ChallengeMenuActions/ChallengeMenuData; this mirrors ChallengeMenuData into a data
// model after every action and once per frame (the bio is typed out over time).
//
// Not an RmlScreen swapped in by showScreen(): Shell::push's placeholder layout opens it through the
// registry, and closing it (Back, or the game starting) goes through the same placeholder.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlChallengeMenuScreen
{
public:
	static RmlChallengeMenuScreen &instance();
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back(); ///< Escape: same as KEY_ESC in ChallengeMenu.cpp

	// Per-frame pump while visible; see RmlUiManager::update().
	static void tick();

private:
	RmlChallengeMenuScreen() {}

	void load(Rml::Context *context);
	void refresh();

	void onHover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onUnhover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSelect(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onPlay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	struct GeneralModel
	{
		int index = 0;
		Rml::String normalImage;
		Rml::String hiliteImage;
		Rml::String selectedImage;
		bool selected = false;
		bool hovered = false; // under the cursor, on its tile or its pin
		// Placement in percent of the map theatre: the pin at the general's home region, the roster
		// tile, and the wire from pin to tile (down to its bus line, along it, down into the tile).
		Rml::String pinLeft, pinTop;
		Rml::String tileLeft, tileTop;
		Rml::String dropTop, dropHeight;
		Rml::String railLeft, railTop, railWidth;
		Rml::String feedTop, feedHeight;
		Rml::String portraitImage; // small bio portrait
		Rml::String name; // bio name
	};

	struct Model
	{
		Rml::Vector<GeneralModel> generals;
		bool showBio = false;
		bool hasPortrait = false;
		Rml::String portraitImage;
		Rml::String portraitLargeImage;
		Rml::String bioName, bioRank, bioBranch, bioStrategy;
		bool showPlay = false;
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq; // .hq-header status

	Model m_model;
	unsigned int m_shownVersion = 0;
	int m_hovered = -1; ///< general under the cursor, -1 for none
	bool m_active = false; ///< opened and not closed yet
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlChallengeMenuScreen();
void CloseRmlChallengeMenuScreen();
