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

// Ronin @feature 22/11/2026 DX9: GO's right-click menus over the RmlUi Communicator. GO keeps its .wnd menu as the window
// manager's lone window; this reads that window's shown buttons and presses the picked one (GBM_SELECTED to its owner).
#include "W3DDevice/GameClient/RmlUi/RmlGOContextMenu.h"
#include "W3DDevice/GameClient/RmlUi/RmlGOCommunicator.h"

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
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cstdio>

extern void GenOnlineCloseRightClickMenus();	// GenOnlineBuddyOverlay.cpp: closes GO's open right-click menus

namespace
{
	const char *const kContextMenuKey = "RmlUi/GOContextMenu";

	// GO's right-click menu, if the lone window is one: GenOnlineRCBuddiesMenu, -RCBuddyRequestMenu, -RCNonBuddiesMenu, -RCMessageMenu...
	GameWindow *goMenu()
	{
		GameWindow *lone = TheWindowManager ? TheWindowManager->winGetLoneWindow() : nullptr;
		WindowLayout *layout = lone ? lone->winGetLayout() : nullptr;
		if (layout == nullptr || !layout->getFilename().startsWith("Menus/GenOnlineRC"))
			return nullptr;
		return lone;
	}
}

//-------------------------------------------------------------------------------------------------
RmlGOContextMenu &RmlGOContextMenu::instance()
{
	static RmlGOContextMenu s_menu;
	return s_menu;
}

void RmlGOContextMenu::tick()
{
	RmlGOContextMenu &menu = instance();
	GameWindow *window = RmlGOCommunicator::instance().isVisible() ? goMenu() : nullptr;
	if (window == nullptr)
	{
		if (menu.isVisible())
			RmlUiScreenRegistry::close(AsciiString(kContextMenuKey));
		menu.m_menu = nullptr;
		return;
	}

	if (!menu.isVisible())
		RmlUiScreenRegistry::open(AsciiString(kContextMenuKey));
	if (!menu.isVisible())
		return;	// no menu document: GO's own stays up
	if (!window->winIsHidden())
		window->winHide(TRUE);
	if (window != menu.m_menu)
	{
		menu.m_menu = window;
		menu.read(window);
	}
}

void RmlGOContextMenu::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;

	Rml::DataModelConstructor constructor = TheRmlUiManager->getContext()->CreateDataModel("contextmenu");
	if (constructor)
	{
		Rml::StructHandle<ItemModel> itemHandle = constructor.RegisterStruct<ItemModel>();
		if (itemHandle)
		{
			itemHandle.RegisterMember("label", &ItemModel::label);
			itemHandle.RegisterMember("index", &ItemModel::index);
			itemHandle.RegisterMember("used", &ItemModel::used);
		}
		constructor.RegisterArray<Rml::Vector<ItemModel>>();
		constructor.Bind("items", &m_model.items);
		constructor.BindEventCallback("pick", &RmlGOContextMenu::onPick, this);
		constructor.BindEventCallback("inside", &RmlGOContextMenu::onInside, this);
		constructor.BindEventCallback("outside", &RmlGOContextMenu::onOutside, this);
		m_modelHandle = constructor.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen("Data/UI/ContextMenu.rml");
}

void RmlGOContextMenu::open()
{
	load();
	if (!m_document)
		return;
	m_menu = nullptr;
	if (!m_document->IsVisible())
		m_document->Show();
	m_document->PullToFront();
}

void RmlGOContextMenu::close()
{
	if (m_document && m_document->IsVisible())
		m_document->Hide();
}

bool RmlGOContextMenu::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlGOContextMenu::back()
{
	GenOnlineCloseRightClickMenus();
}

// The menu's shown push buttons, top to bottom (GO hides the ones that do not apply, Report or Delete on a message).
void RmlGOContextMenu::buttons(GameWindow *menu, std::vector<GameWindow *> &out)
{
	out.clear();
	for (GameWindow *child = menu->winGetChild(); child != nullptr; child = child->winGetNext())
	{
		if (!child->winIsHidden() && BitIsSet(child->winGetStyle(), GWS_PUSH_BUTTON))
			out.push_back(child);
	}
	std::stable_sort(out.begin(), out.end(), [](GameWindow *a, GameWindow *b)
		{
			Int ax = 0, ay = 0, bx = 0, by = 0;
			a->winGetScreenPosition(&ax, &ay);
			b->winGetScreenPosition(&bx, &by);
			return ay < by;
		});
}

// Its rows, and the menu where GO put it (at the pointer, kept on screen).
void RmlGOContextMenu::read(GameWindow *menu)
{
	std::vector<GameWindow *> shown;
	buttons(menu, shown);
	m_itemRows.beginUpdate();
	for (size_t i = 0; i < shown.size(); ++i)
	{
		ItemModel &item = m_itemRows.next();
		item.label = unicodeToUtf8(shown[i]->winGetText());
		item.index = (int)i;
	}
	m_itemRows.endUpdate();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("items");

	Int x = 0, y = 0;
	menu->winGetScreenPosition(&x, &y);
	if (Rml::Element *box = m_document ? m_document->GetElementById("ctx_menu") : nullptr)
	{
		char value[32];
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%dpx", x);
		box->SetProperty("left", value);
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%dpx", y);
		box->SetProperty("top", value);
	}
}

// The picked row's button, pressed as the .wnd button would: GO acts and closes its menu, and this one follows.
void RmlGOContextMenu::onPick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args)
{
	GameWindow *menu = goMenu();
	if (menu == nullptr || args.empty())
		return;
	std::vector<GameWindow *> shown;
	buttons(menu, shown);
	const int index = args[0].Get<int>();
	if (index < 0 || index >= (int)shown.size())
		return;
	GameWindow *button = shown[index];
	TheWindowManager->winSendSystemMsg(button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId());
}

// A press on the menu reaches it before the layer behind it, so the layer knows to leave the menu open.
void RmlGOContextMenu::onInside(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	m_pressInside = true;
}

void RmlGOContextMenu::onOutside(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (m_pressInside)
	{
		m_pressInside = false;
		return;
	}
	GenOnlineCloseRightClickMenus();
}
