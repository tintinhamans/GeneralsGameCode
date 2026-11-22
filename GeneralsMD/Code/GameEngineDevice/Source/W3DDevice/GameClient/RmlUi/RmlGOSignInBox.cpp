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

// Ronin @feature 22/11/2026 DX9: GO's sign-in dialog in RmlUi. GenOnlineLoginMenu.cpp keeps building and driving its .wnd; this
// hides that window, shows its texts, buttons and download bar each frame, and sends its buttons' presses to the dialog's root.
#include "W3DDevice/GameClient/RmlUi/RmlGOSignInBox.h"

#include "Common/NameKeyGenerator.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>

namespace
{
	// The registry's key: not a .wnd path, so GO's own winCreateLayout() of the dialog is never routed.
	const char *const kSignInBoxKey = "RmlUi/GOSignInBox";

	GameWindow *child(GameWindow *root, const char *name)
	{
		return TheWindowManager->winGetWindowFromId(root, TheNameKeyGenerator->nameToKey(AsciiString(name)));
	}

	bool shown(GameWindow *window)
	{
		return window != nullptr && !window->winIsHidden();
	}

	template <typename T>
	void set(Rml::DataModelHandle &handle, const char *name, T &field, const T &value)
	{
		if (field == value)
			return;
		field = value;
		if (handle)
			handle.DirtyVariable(name);
	}
}

//-------------------------------------------------------------------------------------------------
RmlGOSignInBox &RmlGOSignInBox::instance()
{
	static RmlGOSignInBox s_box;
	return s_box;
}

// GOSignInDialogOpen() makes the root; GOSignInDialogDestroy() takes it. Only with the RmlUi shell (ModernUI) up.
void RmlGOSignInBox::tick()
{
	RmlGOSignInBox &box = instance();
	GameWindow *root = nullptr;
	if (TheWindowManager != nullptr && RmlUiScreenRegistry::isRegistered(AsciiString("Menus/MainMenu.wnd")))
		root = child(nullptr, "GOSignInDialog.wnd:GOSignInDialogRoot");

	if (root == nullptr)
	{
		if (box.isVisible())
			RmlUiScreenRegistry::close(AsciiString(kSignInBoxKey));
		return;
	}

	if (!box.isVisible())
		RmlUiScreenRegistry::open(AsciiString(kSignInBoxKey));
	if (!box.isVisible())
		return;	// no box (its document did not load): GO's own window stays up
	if (!root->winIsHidden())
		root->winHide(TRUE);	// its windows stay, unseen: GO's code still reads and writes them
	box.read(root);
}

void RmlGOSignInBox::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;

	Rml::DataModelConstructor constructor = TheRmlUiManager->getContext()->CreateDataModel("signinbox");
	if (constructor)
	{
		constructor.Bind("title", &m_model.title);
		constructor.Bind("status", &m_model.status);
		constructor.Bind("message", &m_model.message);
		constructor.Bind("show_message", &m_model.showMessage);
		constructor.Bind("show_progress", &m_model.showProgress);
		constructor.Bind("progress_style", &m_model.progressStyle);
		constructor.Bind("size_text", &m_model.sizeText);
		constructor.Bind("show_cancel", &m_model.showCancel);
		constructor.Bind("cancel_is_ok", &m_model.cancelIsOk);
		constructor.Bind("cancel_label", &m_model.cancelLabel);
		constructor.Bind("show_update", &m_model.showUpdate);
		constructor.Bind("update_label", &m_model.updateLabel);
		constructor.Bind("show_another", &m_model.showAnother);
		constructor.Bind("another_label", &m_model.anotherLabel);
		constructor.Bind("busy", &m_model.busy);

		constructor.BindEventCallback("cancel", &RmlGOSignInBox::onCancel, this);
		constructor.BindEventCallback("update", &RmlGOSignInBox::onUpdate, this);
		constructor.BindEventCallback("another", &RmlGOSignInBox::onAnother, this);
		m_modelHandle = constructor.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen("Data/UI/SignInBox.rml");
}

void RmlGOSignInBox::open()
{
	load();
	if (!m_document)
		return;
	if (!m_document->IsVisible())
		m_document->Show(Rml::ModalFlag::Modal);
	m_document->PullToFront();
}

void RmlGOSignInBox::close()
{
	if (m_document && m_document->IsVisible())
		m_document->Hide();
}

bool RmlGOSignInBox::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlGOSignInBox::back()
{
	if (m_model.showCancel)
		press("GOSignInDialog.wnd:ButtonCancel");
}

// showSignInDialogStep() and showSignInState() write these windows; their text and shown state are the box's.
void RmlGOSignInBox::read(GameWindow *root)
{
	GameWindow *title = child(root, "GOSignInDialog.wnd:StaticTextTitle");
	GameWindow *status = child(root, "GOSignInDialog.wnd:StaticTextSignInStatus");
	GameWindow *message = child(root, "GOSignInDialog.wnd:StaticTextMessage");
	GameWindow *progress = child(root, "GOSignInDialog.wnd:ProgressBarDownload");
	GameWindow *size = child(root, "GOSignInDialog.wnd:StaticTextDownloadSize");
	GameWindow *cancel = child(root, "GOSignInDialog.wnd:ButtonCancel");
	GameWindow *update = child(root, "GOSignInDialog.wnd:ButtonUpdate");
	GameWindow *another = child(root, "GOSignInDialog.wnd:ButtonUseAnotherAccount");

	set(m_modelHandle, "title", m_model.title, title ? unicodeToUtf8(GadgetStaticTextGetText(title)) : Rml::String());
	set(m_modelHandle, "status", m_model.status, shown(status) ? unicodeToUtf8(GadgetStaticTextGetText(status)) : Rml::String());
	set(m_modelHandle, "message", m_model.message, message ? unicodeToUtf8(GadgetStaticTextGetText(message)) : Rml::String());
	set(m_modelHandle, "show_message", m_model.showMessage, shown(message));

	const bool showProgress = shown(progress);
	set(m_modelHandle, "show_progress", m_model.showProgress, showProgress);
	if (showProgress)
	{
		Int percent = (Int)(intptr_t)progress->winGetUserData();	// GPM_SET_PROGRESS keeps it there
		if (percent < 0)
			percent = 0;
		if (percent > 100)
			percent = 100;
		char style[16];
		_snprintf_s(style, sizeof(style), _TRUNCATE, "%d%%", percent);
		set(m_modelHandle, "progress_style", m_model.progressStyle, Rml::String(style));
		set(m_modelHandle, "size_text", m_model.sizeText, size ? unicodeToUtf8(GadgetStaticTextGetText(size)) : Rml::String());
	}

	const Rml::String cancelLabel = cancel ? unicodeToUtf8(cancel->winGetText()) : Rml::String();
	set(m_modelHandle, "show_cancel", m_model.showCancel, shown(cancel));
	set(m_modelHandle, "cancel_label", m_model.cancelLabel, cancelLabel);
	set(m_modelHandle, "cancel_is_ok", m_model.cancelIsOk,
		cancelLabel == unicodeToUtf8(TheGameText->fetch("GUI:Ok")) || cancelLabel == "OK");
	set(m_modelHandle, "show_update", m_model.showUpdate, shown(update));
	set(m_modelHandle, "update_label", m_model.updateLabel, update ? unicodeToUtf8(update->winGetText()) : Rml::String());
	set(m_modelHandle, "show_another", m_model.showAnother, shown(another));
	set(m_modelHandle, "another_label", m_model.anotherLabel, another ? unicodeToUtf8(another->winGetText()) : Rml::String());

	// Checking for updates or signing in: nothing to answer yet but Cancel.
	set(m_modelHandle, "busy", m_model.busy, !m_model.cancelIsOk && !m_model.showUpdate && !showProgress);
}

// The press the .wnd's button would have made: GBM_SELECTED to the root's signInDialogSystem(). It may close the dialog.
void RmlGOSignInBox::press(const char *buttonName)
{
	GameWindow *root = TheWindowManager ? child(nullptr, "GOSignInDialog.wnd:GOSignInDialogRoot") : nullptr;
	GameWindow *button = root ? child(root, buttonName) : nullptr;
	if (button == nullptr || button->winIsHidden())
		return;
	TheWindowManager->winSendSystemMsg(root, GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId());
}

void RmlGOSignInBox::onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("GOSignInDialog.wnd:ButtonCancel");
}

void RmlGOSignInBox::onUpdate(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("GOSignInDialog.wnd:ButtonUpdate");
}

void RmlGOSignInBox::onAnother(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("GOSignInDialog.wnd:ButtonUseAnotherAccount");
}
