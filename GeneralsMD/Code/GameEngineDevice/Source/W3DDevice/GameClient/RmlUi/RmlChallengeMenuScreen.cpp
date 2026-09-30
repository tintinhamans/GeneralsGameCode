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

#include "W3DDevice/GameClient/RmlUi/RmlChallengeMenuScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/Image.h"
#include "GameClient/TransitionSounds.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <windows.h>

namespace
{
	Rml::String imageName(const Image *image)
	{
		return image ? Rml::String(image->getName().str()) : Rml::String();
	}

	// The theatre is the map area drawn contain-fit on the screen (.ch-theatre in ChallengeMenu.rcss):
	// pins, wires and roster tiles are placed in percent of it, so they stay on the map at any aspect.
	// The roster runs west to east by home region, alternating between two rows, so no two wires cross
	// and a lower row tile's wire drops through the gap between the two upper tiles beside it.
	const float kSlotFirstX = 11.5f; // centre of the westmost tile
	const float kSlotSpan = 44.0f; // westmost to eastmost tile centre
	const float kSlotStepMax = 5.5f; // between neighbouring slots (two steps between tiles in one row)
	const float kRowTop[2] = { 44.0f, 64.0f };
	const float kRailTop = 37.0f; // first bus line under the pins
	const float kRailStep = 0.6f; // every slot has its own bus line, so two lit wires never merge

	Rml::String percent(float value)
	{
		char text[32];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.3f%%", value);
		return Rml::String(text);
	}
}

//-------------------------------------------------------------------------------------------------
RmlChallengeMenuScreen &RmlChallengeMenuScreen::instance()
{
	static RmlChallengeMenuScreen s_screen;
	return s_screen;
}

void RmlChallengeMenuScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("challengemenu");
	if (constructor)
	{
		if (Rml::StructHandle<GeneralModel> generalHandle = constructor.RegisterStruct<GeneralModel>())
		{
			generalHandle.RegisterMember("index", &GeneralModel::index);
			generalHandle.RegisterMember("normal_image", &GeneralModel::normalImage);
			generalHandle.RegisterMember("hilite_image", &GeneralModel::hiliteImage);
			generalHandle.RegisterMember("selected_image", &GeneralModel::selectedImage);
			generalHandle.RegisterMember("selected", &GeneralModel::selected);
			generalHandle.RegisterMember("hovered", &GeneralModel::hovered);
			generalHandle.RegisterMember("pin_left", &GeneralModel::pinLeft);
			generalHandle.RegisterMember("pin_top", &GeneralModel::pinTop);
			generalHandle.RegisterMember("tile_left", &GeneralModel::tileLeft);
			generalHandle.RegisterMember("tile_top", &GeneralModel::tileTop);
			generalHandle.RegisterMember("drop_top", &GeneralModel::dropTop);
			generalHandle.RegisterMember("drop_height", &GeneralModel::dropHeight);
			generalHandle.RegisterMember("rail_left", &GeneralModel::railLeft);
			generalHandle.RegisterMember("rail_top", &GeneralModel::railTop);
			generalHandle.RegisterMember("rail_width", &GeneralModel::railWidth);
			generalHandle.RegisterMember("feed_top", &GeneralModel::feedTop);
			generalHandle.RegisterMember("feed_height", &GeneralModel::feedHeight);
			generalHandle.RegisterMember("portrait_image", &GeneralModel::portraitImage);
			generalHandle.RegisterMember("name", &GeneralModel::name);
		}
		constructor.RegisterArray<Rml::Vector<GeneralModel>>();

		constructor.Bind("generals", &m_model.generals);
		constructor.Bind("show_bio", &m_model.showBio);
		constructor.Bind("has_portrait", &m_model.hasPortrait);
		constructor.Bind("portrait_image", &m_model.portraitImage);
		constructor.Bind("portrait_large_image", &m_model.portraitLargeImage);
		constructor.Bind("bio_name", &m_model.bioName);
		constructor.Bind("bio_rank", &m_model.bioRank);
		constructor.Bind("bio_branch", &m_model.bioBranch);
		constructor.Bind("bio_strategy", &m_model.bioStrategy);
		constructor.Bind("show_play", &m_model.showPlay);

		constructor.BindEventCallback("hover", &RmlChallengeMenuScreen::onHover, this);
		constructor.BindEventCallback("unhover", &RmlChallengeMenuScreen::onUnhover, this);
		constructor.BindEventCallback("select", &RmlChallengeMenuScreen::onSelect, this);
		constructor.BindEventCallback("play", &RmlChallengeMenuScreen::onPlay, this);
		constructor.BindEventCallback("back", &RmlChallengeMenuScreen::onBack, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/ChallengeMenu.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlChallengeMenuScreen::refresh()
{
	const ChallengeMenuData &data = ChallengeMenuData::instance();

	// the generals that start disabled have no medallion; the rest go west to east by home region
	int order[NUM_GENERALS];
	int count = 0;
	for (int i = 0; i < NUM_GENERALS; ++i)
		if (data.m_generals[i].m_enabled)
			order[count++] = i;
	std::stable_sort(order, order + count, [&data](int a, int b) { return data.m_generals[a].m_mapX < data.m_generals[b].m_mapX; });

	const float step = count > 1 ? std::min(kSlotStepMax, kSlotSpan / (count - 1)) : 0.0f;

	m_model.generals.clear();
	for (int slot = 0; slot < count; ++slot)
	{
		const int i = order[slot];
		const ChallengeMenuData::General &src = data.m_generals[i];

		GeneralModel general;
		general.index = i;
		general.normalImage = imageName(src.m_normal);
		general.hiliteImage = imageName(src.m_hilite);
		general.selectedImage = imageName(src.m_selected);
		general.selected = i == data.m_selected;
		general.hovered = i == m_hovered;
		general.portraitImage = imageName(src.m_portrait);
		general.name = unicodeToUtf8(src.m_name);

		const float pinX = src.m_mapX * 100.0f;
		const float pinY = src.m_mapY * 100.0f;
		const float tileX = kSlotFirstX + slot * step;
		const float tileTop = kRowTop[slot % 2];
		const float railY = kRailTop + slot * kRailStep;
		general.pinLeft = percent(pinX);
		general.pinTop = percent(pinY);
		general.tileLeft = percent(tileX);
		general.tileTop = percent(tileTop);
		general.dropTop = percent(std::min(pinY, railY));
		general.dropHeight = percent(std::fabs(railY - pinY));
		general.railLeft = percent(std::min(pinX, tileX));
		general.railTop = percent(railY);
		general.railWidth = percent(std::fabs(tileX - pinX));
		general.feedTop = percent(railY);
		general.feedHeight = percent(tileTop - railY);
		m_model.generals.push_back(general);
	}

	m_model.showBio = data.m_bioVisible;
	m_model.hasPortrait = data.m_portrait != nullptr;
	m_model.portraitImage = imageName(data.m_portrait);
	m_model.portraitLargeImage = imageName(data.m_portraitLarge ? data.m_portraitLarge : data.m_portrait);
	m_model.bioName = unicodeToUtf8(data.m_bioShown[0]);
	m_model.bioRank = unicodeToUtf8(data.m_bioShown[1]);
	m_model.bioBranch = unicodeToUtf8(data.m_bioShown[2]);
	m_model.bioStrategy = unicodeToUtf8(data.m_bioShown[3]);
	m_model.showPlay = data.m_selected != -1;
	m_shownVersion = data.m_version;

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlChallengeMenuScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	ChallengeMenuActions::open();
	m_active = true;
	m_hovered = -1;
	refresh();

	m_document->Show(Rml::ModalFlag::Modal);

	// ChallengeMenuUpdate()'s entrance group, and ChallengeMenuShutdown()'s reverse below.
	TransitionSounds::play("ChallengeMenuFade");
}

void RmlChallengeMenuScreen::close()
{
	if (m_active)
	{
		m_active = false;

		// Play hides the shell for the game, which the score screen needs the game info of
		const Bool gameStarting = ChallengeMenuData::instance().m_gameStarting;
		ChallengeMenuActions::close(gameStarting);
		if (!gameStarting)
			TransitionSounds::play("ChallengeMenuFade", TRUE);
	}

	if (m_document)
		m_document->Hide();
}

bool RmlChallengeMenuScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlChallengeMenuScreen::tick()
{
	RmlChallengeMenuScreen &screen = instance();
	if (!screen.isVisible() || !screen.m_active)
		return;

	ChallengeMenuActions::update();
	if (screen.m_shownVersion != ChallengeMenuData::instance().m_version)
		screen.refresh();
}

//-------------------------------------------------------------------------------------------------
void RmlChallengeMenuScreen::onHover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	m_hovered = args[0].Get<int>();
	ChallengeMenuActions::hover(m_hovered);
	refresh();
}

void RmlChallengeMenuScreen::onUnhover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	const int general = args[0].Get<int>();
	if (m_hovered == general)
		m_hovered = -1;
	ChallengeMenuActions::unhover(general);
	refresh();
}

void RmlChallengeMenuScreen::onSelect(Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &args)
{
	// like GWM_LEFT_DOWN: the medallions choose on press
	if (args.empty() || event.GetParameter<int>("button", 0) != 0)
		return;

	ChallengeMenuActions::select(args[0].Get<int>());
	refresh();
}

void RmlChallengeMenuScreen::onPlay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ChallengeMenuActions::play();
	refresh();
}

void RmlChallengeMenuScreen::onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ChallengeMenuActions::back();
}

void RmlChallengeMenuScreen::back()
{
	ChallengeMenuActions::back();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlChallengeMenuScreen() { RmlChallengeMenuScreen::instance().open(); }
void CloseRmlChallengeMenuScreen() { RmlChallengeMenuScreen::instance().close(); }
