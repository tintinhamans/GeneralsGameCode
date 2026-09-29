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

#include "W3DDevice/GameClient/RmlUi/RmlReplayMenuScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>
#include <windows.h>

namespace
{
	Rml::String rgbToHex(UnsignedInt rgb)
	{
		char hex[8];
		_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		return Rml::String(hex);
	}

	void closeDocument()
	{
		RmlReplayMenuScreen::instance().close();
	}
}

//-------------------------------------------------------------------------------------------------
RmlReplayMenuScreen &RmlReplayMenuScreen::instance()
{
	static RmlReplayMenuScreen s_screen;
	return s_screen;
}

void RmlReplayMenuScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("replaymenu");
	if (constructor)
	{
		if (Rml::StructHandle<RowModel> rowHandle = constructor.RegisterStruct<RowModel>())
		{
			rowHandle.RegisterMember("name", &RowModel::name);
			rowHandle.RegisterMember("date_time", &RowModel::dateTime);
			rowHandle.RegisterMember("version", &RowModel::version);
			rowHandle.RegisterMember("map", &RowModel::map);
			rowHandle.RegisterMember("tooltip", &RowModel::tooltip);
			rowHandle.RegisterMember("color_hex", &RowModel::colorHex);
			rowHandle.RegisterMember("map_color_hex", &RowModel::mapColorHex);
			rowHandle.RegisterMember("index", &RowModel::index);
			rowHandle.RegisterMember("selected", &RowModel::selected);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();

		constructor.Bind("rows", &m_rows);

		constructor.BindEventCallback("select_row", &RmlReplayMenuScreen::onSelectRow, this);
		constructor.BindEventCallback("activate_row", &RmlReplayMenuScreen::onActivateRow, this);
		constructor.BindEventCallback("load", &RmlReplayMenuScreen::onLoad, this);
		constructor.BindEventCallback("delete", &RmlReplayMenuScreen::onDelete, this);
		constructor.BindEventCallback("copy", &RmlReplayMenuScreen::onCopy, this);
		constructor.BindEventCallback("back", &RmlReplayMenuScreen::onBack, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/ReplayMenu.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlReplayMenuScreen::refresh()
{
	const ReplayMenuData &data = ReplayMenuData::instance();

	m_rows.resize(data.m_rows.size());
	for (size_t i = 0; i < data.m_rows.size(); ++i)
	{
		const ReplayRow &src = data.m_rows[i];
		RowModel &row = m_rows[i];
		row.name = unicodeToUtf8(src.m_name);

		// the .wnd shows time and date in one column
		UnicodeString dateTime;
		dateTime.format(L"%s %s", src.m_time.str(), src.m_date.str());
		row.dateTime = unicodeToUtf8(dateTime);

		row.version = unicodeToUtf8(src.m_version);
		row.map = unicodeToUtf8(src.m_map);
		row.tooltip = unicodeToUtf8(src.m_tooltip);
		row.colorHex = rgbToHex(src.m_color);
		row.mapColorHex = rgbToHex(src.m_mapColor);
		row.index = (int)i;
		row.selected = (int)i == data.m_selected;
	}
	m_shownVersion = data.m_version;

	if (m_modelHandle)
		m_modelHandle.DirtyVariable("rows");
}

void RmlReplayMenuScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	ReplayMenuActions::open(&closeDocument);
	refresh();

	m_document->Show(Rml::ModalFlag::Modal);
}

void RmlReplayMenuScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlReplayMenuScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlReplayMenuScreen::tick()
{
	RmlReplayMenuScreen &screen = instance();
	if (!screen.isVisible())
		return;

	ReplayMenuActions::update();
	if (screen.m_shownVersion != ReplayMenuData::instance().m_version)
		screen.refresh();
}

//-------------------------------------------------------------------------------------------------
void RmlReplayMenuScreen::onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	ReplayMenuActions::select(args[0].Get<int>());
	refresh();
}

void RmlReplayMenuScreen::onActivateRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	ReplayMenuActions::activate(args[0].Get<int>());
	refresh();
}

void RmlReplayMenuScreen::onLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ReplayMenuActions::load();
	refresh();
}

void RmlReplayMenuScreen::onDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ReplayMenuActions::remove();
}

void RmlReplayMenuScreen::onCopy(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ReplayMenuActions::copy();
}

void RmlReplayMenuScreen::onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ReplayMenuActions::back();
}

void RmlReplayMenuScreen::back()
{
	ReplayMenuActions::back();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlReplayMenuScreen() { RmlReplayMenuScreen::instance().open(); }
void CloseRmlReplayMenuScreen() { RmlReplayMenuScreen::instance().close(); }
