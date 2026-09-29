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

#include "W3DDevice/GameClient/RmlUi/RmlPopupReplayScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cstdio>
#include <vector>
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
		RmlPopupReplayScreen::instance().close();
	}
}

//-------------------------------------------------------------------------------------------------
RmlPopupReplayScreen &RmlPopupReplayScreen::instance()
{
	static RmlPopupReplayScreen s_screen;
	return s_screen;
}

void RmlPopupReplayScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("popupreplay");
	if (constructor)
	{
		if (Rml::StructHandle<RowModel> rowHandle = constructor.RegisterStruct<RowModel>())
		{
			rowHandle.RegisterMember("name", &RowModel::name);
			rowHandle.RegisterMember("date_time", &RowModel::dateTime);
			rowHandle.RegisterMember("version", &RowModel::version);
			rowHandle.RegisterMember("map", &RowModel::map);
			rowHandle.RegisterMember("color_hex", &RowModel::colorHex);
			rowHandle.RegisterMember("map_color_hex", &RowModel::mapColorHex);
			rowHandle.RegisterMember("index", &RowModel::index);
			rowHandle.RegisterMember("selected", &RowModel::selected);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();

		constructor.Bind("rows", &m_rows);
		constructor.Bind("name", &m_name);
		constructor.Bind("name_max", &m_nameMax);
		constructor.Bind("save_disabled", &m_saveDisabled);
		constructor.Bind("show_saved", &m_showSaved);

		constructor.BindEventCallback("select_row", &RmlPopupReplayScreen::onSelectRow, this);
		constructor.BindEventCallback("name_changed", &RmlPopupReplayScreen::onNameChanged, this);
		constructor.BindEventCallback("save", &RmlPopupReplayScreen::onSave, this);
		constructor.BindEventCallback("back", &RmlPopupReplayScreen::onBack, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/PopupReplay.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlPopupReplayScreen::refresh()
{
	const PopupReplayData &data = PopupReplayData::instance();

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
		row.colorHex = rgbToHex(src.m_color);
		row.mapColorHex = rgbToHex(src.m_mapColor);
		row.index = (int)i;
		row.selected = (int)i == data.m_selected;
	}

	// leave the entry alone while it already shows the name, so typing keeps its caret
	const Rml::String name = unicodeToUtf8(data.m_name);
	const bool nameChanged = name != m_name;
	m_name = name;

	m_nameMax = PopupReplayData::MAX_NAME_LENGTH;
	m_saveDisabled = !data.canSave();
	m_showSaved = data.m_showSaved == TRUE;
	m_shownVersion = data.m_version;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("rows");
		if (nameChanged)
			m_modelHandle.DirtyVariable("name");
		m_modelHandle.DirtyVariable("name_max");
		m_modelHandle.DirtyVariable("save_disabled");
		m_modelHandle.DirtyVariable("show_saved");
	}
}

void RmlPopupReplayScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	PopupReplayActions::open(&closeDocument);
	m_name.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("name");
	refresh();

	m_document->Show(Rml::ModalFlag::Modal);

	// the name entry starts with the keyboard focus
	if (Rml::Element *input = m_document->GetElementById("name-input"))
		input->Focus();
}

void RmlPopupReplayScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlPopupReplayScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlPopupReplayScreen::tick()
{
	RmlPopupReplayScreen &screen = instance();
	if (!screen.isVisible())
		return;

	PopupReplayActions::update();
	if (screen.isVisible() && screen.m_shownVersion != PopupReplayData::instance().m_version)
		screen.refresh();
}

//-------------------------------------------------------------------------------------------------
void RmlPopupReplayScreen::onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty())
		return;

	PopupReplayActions::select(args[0].Get<int>());
	refresh();
}

// Fires on every edit; Enter arrives with linebreak set and saves like the .wnd's GEM_EDIT_DONE.
void RmlPopupReplayScreen::onNameChanged(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	PopupReplayActions::setName(utf8ToUnicode(ev.GetParameter<Rml::String>("value", m_name)));

	if (ev.GetParameter<bool>("linebreak", false))
		PopupReplayActions::save();

	refresh();
}

void RmlPopupReplayScreen::onSave(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	PopupReplayActions::setName(utf8ToUnicode(m_name));
	PopupReplayActions::save();
	refresh();
}

void RmlPopupReplayScreen::onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	PopupReplayActions::back();
}

void RmlPopupReplayScreen::back()
{
	PopupReplayActions::back();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlPopupReplayScreen() { RmlPopupReplayScreen::instance().open(); }
void CloseRmlPopupReplayScreen() { RmlPopupReplayScreen::instance().close(); }
