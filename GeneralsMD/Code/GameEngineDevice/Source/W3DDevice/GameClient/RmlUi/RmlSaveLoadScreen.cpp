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

#include "W3DDevice/GameClient/RmlUi/RmlSaveLoadScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadData.h"
#include "GameClient/TransitionSounds.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

#include <cstdio>
#include <vector>
#include <windows.h>

extern Bool DontShowMainMenu; // WOLLobbyMenu.cpp; see RmlSaveLoadScreen::open()
extern Bool ReplayWasPressed; // ScoreScreen.cpp; ditto

namespace
{
	Rml::String rgbToHex(UnsignedInt rgb)
	{
		char hex[8];
		_snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02X%02X%02X", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		return Rml::String(hex);
	}

	// A load took over: the .wnd pops without its shutdown transition then.
	void closeDocument()
	{
		RmlSaveLoadScreen::instance().close(false);
	}
}

//-------------------------------------------------------------------------------------------------
RmlSaveLoadScreen &RmlSaveLoadScreen::instance()
{
	static RmlSaveLoadScreen s_screen;
	return s_screen;
}

void RmlSaveLoadScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("saveload");
	if (constructor)
	{
		if (Rml::StructHandle<RowModel> rowHandle = constructor.RegisterStruct<RowModel>())
		{
			rowHandle.RegisterMember("name", &RowModel::name);
			rowHandle.RegisterMember("time", &RowModel::time);
			rowHandle.RegisterMember("date", &RowModel::date);
			rowHandle.RegisterMember("color_hex", &RowModel::colorHex);
			rowHandle.RegisterMember("map", &RowModel::map);
			rowHandle.RegisterMember("campaign", &RowModel::campaign);
			rowHandle.RegisterMember("mission", &RowModel::mission);
			rowHandle.RegisterMember("is_mission_save", &RowModel::isMissionSave);
			rowHandle.RegisterMember("is_new", &RowModel::isNew);
			rowHandle.RegisterMember("index", &RowModel::index);
			rowHandle.RegisterMember("selected", &RowModel::selected);
		}
		constructor.RegisterArray<Rml::Vector<RowModel>>();

		constructor.Bind("rows", &m_rows);
		constructor.Bind("has_selection", &m_hasSelection);
		constructor.Bind("sel", &m_selected);
		constructor.Bind("description", &m_description);
		constructor.Bind("description_max", &m_descriptionMax);
		constructor.Bind("is_popup", &m_isPopup);
		constructor.Bind("list_locked", &m_listLocked);
		constructor.Bind("save_disabled", &m_saveDisabled);
		constructor.Bind("load_disabled", &m_loadDisabled);
		constructor.Bind("delete_disabled", &m_deleteDisabled);
		constructor.Bind("back_disabled", &m_backDisabled);
		constructor.Bind("show_load_confirm", &m_showLoadConfirm);
		constructor.Bind("show_overwrite_confirm", &m_showOverwriteConfirm);
		constructor.Bind("show_save_desc", &m_showSaveDesc);
		constructor.Bind("show_delete_confirm", &m_showDeleteConfirm);

		constructor.BindEventCallback("select_row", &RmlSaveLoadScreen::onSelectRow, this);
		constructor.BindEventCallback("activate_row", &RmlSaveLoadScreen::onActivateRow, this);
		constructor.BindEventCallback("save", &RmlSaveLoadScreen::onSave, this);
		constructor.BindEventCallback("load", &RmlSaveLoadScreen::onLoad, this);
		constructor.BindEventCallback("delete", &RmlSaveLoadScreen::onDelete, this);
		constructor.BindEventCallback("back", &RmlSaveLoadScreen::onBack, this);
		constructor.BindEventCallback("key_down", &RmlSaveLoadScreen::onKeyDown, this);
		constructor.BindEventCallback("confirm_load", &RmlSaveLoadScreen::onConfirmLoad, this);
		constructor.BindEventCallback("cancel_load", &RmlSaveLoadScreen::onCancelLoad, this);
		constructor.BindEventCallback("confirm_overwrite", &RmlSaveLoadScreen::onConfirmOverwrite, this);
		constructor.BindEventCallback("cancel_overwrite", &RmlSaveLoadScreen::onCancelOverwrite, this);
		constructor.BindEventCallback("confirm_save_desc", &RmlSaveLoadScreen::onConfirmSaveDesc, this);
		constructor.BindEventCallback("cancel_save_desc", &RmlSaveLoadScreen::onCancelSaveDesc, this);
		constructor.BindEventCallback("confirm_delete", &RmlSaveLoadScreen::onConfirmDelete, this);
		constructor.BindEventCallback("cancel_delete", &RmlSaveLoadScreen::onCancelDelete, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/SaveLoad.rml");
}

//-------------------------------------------------------------------------------------------------
void RmlSaveLoadScreen::refresh()
{
	const SaveLoadData &data = SaveLoadData::instance();

	m_rows.resize(data.m_rows.size());
	for (size_t i = 0; i < data.m_rows.size(); ++i)
	{
		const SaveLoadRow &src = data.m_rows[i];
		RowModel &row = m_rows[i];
		row.name = unicodeToUtf8(src.m_name);
		row.time = unicodeToUtf8(src.m_time);
		row.date = unicodeToUtf8(src.m_date);
		row.colorHex = rgbToHex(src.m_color);
		row.map = unicodeToUtf8(src.m_mapLabel);
		row.campaign = unicodeToUtf8(src.m_campaign);
		row.mission = src.m_missionNumber;
		row.isMissionSave = src.m_isMissionSave == TRUE;
		row.isNew = src.m_info == nullptr;
		row.index = (int)i;
		row.selected = (int)i == data.m_selected;
	}

	m_hasSelection = data.m_selected >= 0 && data.m_selected < (int)m_rows.size();
	m_selected = m_hasSelection ? m_rows[data.m_selected] : RowModel();

	m_isPopup = data.m_isPopup == TRUE;
	m_listLocked = !data.isListEnabled();
	m_saveDisabled = !data.areButtonsEnabled() || !data.canSave();
	m_loadDisabled = !data.areButtonsEnabled() || !data.canLoad();
	m_deleteDisabled = !data.areButtonsEnabled() || !data.canDelete();
	m_backDisabled = !data.areButtonsEnabled();
	m_showLoadConfirm = data.m_dialog == SaveLoadData::DIALOG_LOAD_CONFIRM;
	m_showOverwriteConfirm = data.m_dialog == SaveLoadData::DIALOG_OVERWRITE_CONFIRM;
	m_showSaveDesc = data.m_dialog == SaveLoadData::DIALOG_SAVE_DESC;
	m_showDeleteConfirm = data.m_dialog == SaveLoadData::DIALOG_DELETE_CONFIRM;
	m_descriptionMax = SaveLoadData::MAX_DESCRIPTION_LENGTH;

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("rows");
		m_modelHandle.DirtyVariable("has_selection");
		m_modelHandle.DirtyVariable("sel");
		m_modelHandle.DirtyVariable("description_max");
		m_modelHandle.DirtyVariable("is_popup");
		m_modelHandle.DirtyVariable("list_locked");
		m_modelHandle.DirtyVariable("save_disabled");
		m_modelHandle.DirtyVariable("load_disabled");
		m_modelHandle.DirtyVariable("delete_disabled");
		m_modelHandle.DirtyVariable("back_disabled");
		m_modelHandle.DirtyVariable("show_load_confirm");
		m_modelHandle.DirtyVariable("show_overwrite_confirm");
		m_modelHandle.DirtyVariable("show_save_desc");
		m_modelHandle.DirtyVariable("show_delete_confirm");
	}
}

void RmlSaveLoadScreen::open(bool isPopup)
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	// SaveLoad.wnd only loads, the popup over the game saves and loads.
	SaveLoadActions::open(isPopup ? SLLT_SAVE_AND_LOAD : SLLT_LOAD_ONLY, isPopup ? TRUE : FALSE, &closeDocument);
	SaveLoadData::instance().m_open = TRUE;

	m_description.clear();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("description");
	refresh();

	m_document->Show(Rml::ModalFlag::Modal);

	// SaveLoadMenuUpdate()'s entrance group, which only the shell's SaveLoad.wnd plays (and not
	// straight out of a game), and SaveLoadMenuShutdown()'s reverse in close().
	if (!isPopup && !DontShowMainMenu && !ReplayWasPressed)
	{
		TransitionSounds::stop("MainMenuDefaultMenuLogoFade");
		TransitionSounds::play("SaveLoadMenuFade");
	}
}

void RmlSaveLoadScreen::close(bool reverseTransition)
{
	if (reverseTransition && !m_isPopup && isVisible())
		TransitionSounds::play("SaveLoadMenuFade", TRUE);

	SaveLoadData::instance().m_open = FALSE;
	if (m_document)
		m_document->Hide();
}

bool RmlSaveLoadScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

//-------------------------------------------------------------------------------------------------
void RmlSaveLoadScreen::onSelectRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty() || !SaveLoadData::instance().isListEnabled())
		return;

	SaveLoadActions::select(args[0].Get<int>());
	refresh();
}

void RmlSaveLoadScreen::onActivateRow(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	if (args.empty() || !SaveLoadData::instance().isListEnabled())
		return;

	SaveLoadActions::activate(args[0].Get<int>());
	refresh();
}

void RmlSaveLoadScreen::onSave(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::save();

	// the entry starts on the default name
	if (SaveLoadData::instance().m_dialog == SaveLoadData::DIALOG_SAVE_DESC)
	{
		m_description = unicodeToUtf8(SaveLoadData::instance().m_description);
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("description");
	}
	refresh();
}

void RmlSaveLoadScreen::onLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::load();
	refresh();
}

void RmlSaveLoadScreen::onDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::remove();
	refresh();
}

void RmlSaveLoadScreen::onBack(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::back();
}

void RmlSaveLoadScreen::onKeyDown(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	if (ev.GetParameter<int>("key_identifier", 0) != Rml::Input::KI_ESCAPE)
		return;

	SaveLoadActions::escape();
	refresh();
}

void RmlSaveLoadScreen::onConfirmLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::confirmLoad();
	refresh();
}

void RmlSaveLoadScreen::onCancelLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::cancelLoad();
	refresh();
}

void RmlSaveLoadScreen::onConfirmOverwrite(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::confirmOverwrite();
	refresh();
}

void RmlSaveLoadScreen::onCancelOverwrite(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::cancelOverwrite();
	refresh();
}

void RmlSaveLoadScreen::onConfirmSaveDesc(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadData::instance().m_description = utf8ToUnicode(m_description);
	SaveLoadActions::confirmSaveDesc();
	refresh();
}

void RmlSaveLoadScreen::onCancelSaveDesc(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::cancelSaveDesc();
	refresh();
}

void RmlSaveLoadScreen::onConfirmDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::confirmDelete();
	refresh();
}

void RmlSaveLoadScreen::onCancelDelete(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	SaveLoadActions::cancelDelete();
	refresh();
}

void RmlSaveLoadScreen::back()
{
	SaveLoadActions::escape();
	refresh();
}

//-------------------------------------------------------------------------------------------------
void OpenRmlSaveLoadScreen() { RmlSaveLoadScreen::instance().open(false); }
void CloseRmlSaveLoadScreen() { RmlSaveLoadScreen::instance().close(); }
void OpenRmlPopupSaveLoadScreen() { RmlSaveLoadScreen::instance().open(true); }
void CloseRmlPopupSaveLoadScreen() { RmlSaveLoadScreen::instance().close(); }
