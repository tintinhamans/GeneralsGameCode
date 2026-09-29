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

#include "W3DDevice/GameClient/RmlUi/RmlLoadScreen.h"

#include "GameClient/LoadScreenData.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1);
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

static Rml::String rgbToHex(UnsignedInt rgb)
{
	char hex[8];
	_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return Rml::String(hex);
}

static Rml::String percentStyle(float percent)
{
	char buf[16];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f%%", percent);
	return Rml::String(buf);
}

//-------------------------------------------------------------------------------------------------
RmlLoadScreen &RmlLoadScreen::instance(Kind kind)
{
	static RmlLoadScreen s_screens[KIND_COUNT] = {
		RmlLoadScreen("UI/MapTransfer.rml", "maptransfer"),
	};
	return s_screens[kind];
}

void RmlLoadScreen::tick()
{
	for (int i = 0; i < KIND_COUNT; ++i)
	{
		RmlLoadScreen &screen = instance((Kind)i);
		if (screen.isVisible() && screen.m_seenVersion != LoadScreenData::instance().m_version)
			screen.refresh();
	}
}

void RmlLoadScreen::load(Rml::Context *context)
{
	if (m_document)
		return;

	m_context = context;
	m_rows.assign(MAX_SLOTS, RowModel());

	Rml::DataModelConstructor constructor = context->CreateDataModel(m_modelName);
	if (constructor)
	{
		if (Rml::StructHandle<RowModel> rowHandle = constructor.RegisterStruct<RowModel>())
		{
			rowHandle.RegisterMember("name", &RowModel::name);
			rowHandle.RegisterMember("status", &RowModel::status);
			rowHandle.RegisterMember("color_hex", &RowModel::colorHex);
			rowHandle.RegisterMember("progress_style", &RowModel::progressStyle);
			rowHandle.RegisterMember("used", &RowModel::used);
			rowHandle.RegisterMember("show_progress", &RowModel::showProgress);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();

		constructor.Bind("rows", &m_rows);
		constructor.Bind("current_file", &m_currentFile);
		constructor.Bind("timeout", &m_timeout);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument(m_documentPath);
}

void RmlLoadScreen::refresh()
{
	const LoadScreenData &data = LoadScreenData::instance();
	m_seenVersion = data.m_version;

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const LoadScreenPlayerRow &src = data.m_rows[i];
		RowModel &row = m_rows[i];
		row.used = i < data.m_rowCount;
		row.name = unicodeToUtf8(src.m_name);
		row.status = unicodeToUtf8(src.m_status);
		row.colorHex = rgbToHex(src.m_color);
		row.progressStyle = percentStyle((float)src.m_progress);
		row.showProgress = src.m_showProgress == TRUE;

	}

	m_currentFile = unicodeToUtf8(data.m_currentFile);
	m_timeout = unicodeToUtf8(data.m_timeout);

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("rows");
		m_modelHandle.DirtyVariable("current_file");
		m_modelHandle.DirtyVariable("timeout");
	}
}

void RmlLoadScreen::open()
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	refresh();
	m_document->Show();
}

void RmlLoadScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlLoadScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlMapTransferScreen() { RmlLoadScreen::instance(RmlLoadScreen::KIND_MAP_TRANSFER).open(); }
void CloseRmlMapTransferScreen() { RmlLoadScreen::instance(RmlLoadScreen::KIND_MAP_TRANSFER).close(); }
