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

// Ronin @feature 22/11/2026 DX9: GO's buddy notification as an RmlUi card. GO's layout stays the truth: shown = its layout is not
// hidden (GenOnlineShowNotificationBox() shows it, GenOnlineDeleteNotificationBox() destroys it when its time is up).
#include "W3DDevice/GameClient/RmlUi/RmlGOToast.h"

#include "Common/NameKeyGenerator.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/WindowLayout.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	GameWindow *notificationButton()
	{
		if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			return nullptr;
		return TheWindowManager->winGetWindowFromId(nullptr,
			TheNameKeyGenerator->nameToKey(AsciiString("PopupBuddyListNotification.wnd:ButtonNotification")));
	}
}

RmlGOToast &RmlGOToast::instance()
{
	static RmlGOToast s_toast;
	return s_toast;
}

// The RmlUi card while the RmlUi main menu or hub is the screen (RmlUi draws under GO's .wnd screens and the match's windows).
void RmlGOToast::tick()
{
	RmlGOToast &toast = instance();
	GameWindow *button = notificationButton();
	// Ronin @bugfix 22/11/2026 DX9: only a layout's top windows carry it (WindowLayout::addWindow): from the button up to its top.
	GameWindow *root = button;
	while (root != nullptr && root->winGetParent() != nullptr)
		root = root->winGetParent();
	WindowLayout *layout = root ? root->winGetLayout() : nullptr;
	const bool goShowing = layout != nullptr && !layout->isHidden();
	const bool rmlShell = RmlUiScreenRegistry::isOpen(AsciiString("Menus/GenOnlineWelcomeMenu.wnd"))
		|| RmlUiScreenRegistry::isOpen(AsciiString("Menus/MainMenu.wnd"));

	if (!goShowing || !rmlShell)
	{
		toast.hide();
		// GO hides its card through the layout, never its root: a hidden root under a shown layout is this one's.
		if (goShowing && root->winIsHidden())
			root->winHide(FALSE);
		return;
	}

	toast.show();
	if (!toast.isVisible())
		return;	// no card document: GO's own stays up
	if (!root->winIsHidden())
		root->winHide(TRUE);

	const Rml::String text = unicodeToUtf8(button->winGetText());
	if (text != toast.m_text)
	{
		toast.m_text = text;
		if (toast.m_modelHandle)
			toast.m_modelHandle.DirtyVariable("text");
	}
}

void RmlGOToast::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;
	Rml::DataModelConstructor constructor = TheRmlUiManager->getContext()->CreateDataModel("toast");
	if (constructor)
	{
		constructor.Bind("text", &m_text);
		constructor.BindEventCallback("pressed", &RmlGOToast::onPressed, this);
		m_modelHandle = constructor.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen("Data/UI/Toast.rml");
}

// Shown without focus: a card must not take the keyboard from a chat line being typed.
void RmlGOToast::show()
{
	load();
	if (!m_document || m_document->IsVisible())
		return;
	m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	m_document->PullToFront();
}

void RmlGOToast::hide()
{
	if (m_document && m_document->IsVisible())
		m_document->Hide();
}

bool RmlGOToast::isVisible() const
{
	return m_document && m_document->IsVisible();
}

// GenOnlinePopupBuddyNotificationSystem() answers its button's GBM_SELECTED.
void RmlGOToast::onPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	GameWindow *button = notificationButton();
	if (button != nullptr)
		TheWindowManager->winSendSystemMsg(button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId());
}
