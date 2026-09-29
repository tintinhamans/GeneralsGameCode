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
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cstdio>
#include <windows.h>

namespace
{
	Rml::String imageName(const Image *image)
	{
		return image ? Rml::String(image->getName().str()) : Rml::String();
	}

	// Medallion positions on the world map: the top left corners of GeneralPosition0..11 in
	// ChallengeMenu.wnd, in its 800x600 space.
	const int kPositions[NUM_GENERALS][2] =
	{
		{ 152, 198 }, { 500, 222 }, { 624, 198 }, { 220, 159 }, { 663, 218 }, { 102, 186 },
		{ 438, 206 }, { 691, 183 }, { 535, 189 }, { 641, 176 }, { 292, 199 }, { 293, 228 },
	};

	// One .wnd pixel in dp: the menu is 600 units tall and the context is 1080dp tall.
	const float kDpPerWndPixel = 1080.0f / 600.0f;

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
			generalHandle.RegisterMember("left", &GeneralModel::left);
			generalHandle.RegisterMember("top", &GeneralModel::top);
			generalHandle.RegisterMember("size", &GeneralModel::size);
			generalHandle.RegisterMember("selected", &GeneralModel::selected);
		}
		constructor.RegisterArray<Rml::Vector<GeneralModel>>();

		constructor.Bind("generals", &m_model.generals);
		constructor.Bind("show_bio", &m_model.showBio);
		constructor.Bind("has_portrait", &m_model.hasPortrait);
		constructor.Bind("portrait_image", &m_model.portraitImage);
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

	// the generals that start disabled have no medallion
	m_model.generals.clear();
	for (int i = 0; i < NUM_GENERALS; ++i)
	{
		const ChallengeMenuData::General &src = data.m_generals[i];
		if (!src.m_enabled)
			continue;

		GeneralModel general;
		general.index = i;
		general.normalImage = imageName(src.m_normal);
		general.hiliteImage = imageName(src.m_hilite);
		general.selectedImage = imageName(src.m_selected);
		general.left = percent(kPositions[i][0] * 100.0f / 800.0f);
		general.top = percent(kPositions[i][1] * 100.0f / 600.0f);

		// the .wnd sizes the button by its image, square
		char size[32];
		_snprintf_s(size, sizeof(size), _TRUNCATE, "%.1fdp", (src.m_normal ? src.m_normal->getImageWidth() : 0) * kDpPerWndPixel);
		general.size = size;

		general.selected = i == data.m_selected;
		m_model.generals.push_back(general);
	}

	m_model.showBio = data.m_bioVisible;
	m_model.hasPortrait = data.m_portrait != nullptr;
	m_model.portraitImage = imageName(data.m_portrait);
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
	refresh();

	m_document->Show(Rml::ModalFlag::Modal);
}

void RmlChallengeMenuScreen::close()
{
	if (m_active)
	{
		m_active = false;

		// Play hides the shell for the game, which the score screen needs the game info of
		ChallengeMenuActions::close(ChallengeMenuData::instance().m_gameStarting);
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

	ChallengeMenuActions::hover(args[0].Get<int>());
	refresh();
}

void RmlChallengeMenuScreen::onUnhover(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	ChallengeMenuActions::unhover(args[0].Get<int>());
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
