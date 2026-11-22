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

// Ronin @feature 22/11/2026 DX9: the Communicator over the hub. Each frame it reads GO's three .wnd lists (buddies, the selected
// buddy's chat, the blocked players); its presses go to the gadgets' owner as GBM_SELECTED / GLM_RIGHT_CLICKED / GEM_EDIT_DONE.
#include "W3DDevice/GameClient/RmlUi/RmlGOCommunicator.h"

#include "Common/NameKeyGenerator.h"
#include "Common/UnicodeString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "W3DDevice/GameClient/RmlUi/RmlChatInput.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cwchar>
#include <functional>
#include <string>

extern void GenOnlineCloseRightClickMenus();	// GenOnlineBuddyOverlay.cpp: a click elsewhere closes GO's right-click menu

namespace
{
	// The registry's key: not a .wnd path, so GameSpyOpenOverlay() still builds GO's .wnd.
	const char *const kCommunicatorKey = "RmlUi/GOCommunicator";
	const char *const kRootName = "WOLBuddyOverlay.wnd:BuddyMenuParent";

	GameWindow *child(GameWindow *root, const char *name)
	{
		return TheWindowManager->winGetWindowFromId(root, TheNameKeyGenerator->nameToKey(AsciiString(name)));
	}

	void addSignature(std::wstring &signature, const UnicodeString &text, Color color)
	{
		signature += text.str();
		signature += L'\x1F';
		signature += (wchar_t)(color & 0xFFFF);
		signature += (wchar_t)((color >> 16) & 0xFFFF);
	}
}

//-------------------------------------------------------------------------------------------------
RmlGOCommunicator &RmlGOCommunicator::instance()
{
	static RmlGOCommunicator s_communicator;
	return s_communicator;
}

// GO's overlay is open and the hub is the screen: the RmlUi window. Anywhere else GO's own window, shown again if this hid it.
void RmlGOCommunicator::tick()
{
	RmlGOCommunicator &communicator = instance();
	GameWindow *root = nullptr;
	if (TheWindowManager != nullptr && RmlUiScreenRegistry::isRegistered(AsciiString("Menus/MainMenu.wnd"))
		&& GameSpyIsOverlayOpen(GSOVERLAY_BUDDY))
		root = child(nullptr, kRootName);
	const bool overHub = RmlUiScreenRegistry::isOpen(AsciiString("Menus/GenOnlineWelcomeMenu.wnd"));

	if (root == nullptr || !overHub)
	{
		if (communicator.isVisible())
			RmlUiScreenRegistry::close(AsciiString(kCommunicatorKey));
		if (root != nullptr && root == communicator.m_hiddenRoot && root->winIsHidden())
			root->winHide(FALSE);
		communicator.m_hiddenRoot = nullptr;
		return;
	}

	if (!communicator.isVisible())
		RmlUiScreenRegistry::open(AsciiString(kCommunicatorKey));
	if (!communicator.isVisible())
		return;	// no window (its document did not load): GO's own stays up
	if (!root->winIsHidden())
		root->winHide(TRUE);	// its gadgets stay, unseen: GO's code still fills and reads them
	communicator.m_hiddenRoot = root;
	communicator.read(root);
}

void RmlGOCommunicator::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;

	Rml::DataModelConstructor constructor = TheRmlUiManager->getContext()->CreateDataModel("communicator");
	if (constructor)
	{
		Rml::StructHandle<BuddyModel> buddyHandle = constructor.RegisterStruct<BuddyModel>();
		if (buddyHandle)
		{
			buddyHandle.RegisterMember("name", &BuddyModel::name);
			buddyHandle.RegisterMember("name_color", &BuddyModel::nameColor);
			buddyHandle.RegisterMember("status", &BuddyModel::status);
			buddyHandle.RegisterMember("dot", &BuddyModel::dot);
			buddyHandle.RegisterMember("unread", &BuddyModel::unread);
			buddyHandle.RegisterMember("selected", &BuddyModel::selected);
			buddyHandle.RegisterMember("index", &BuddyModel::index);
			buddyHandle.RegisterMember("used", &BuddyModel::used);
		}
		constructor.RegisterArray<Rml::Vector<BuddyModel>>();

		Rml::StructHandle<LineModel> lineHandle = constructor.RegisterStruct<LineModel>();
		if (lineHandle)
		{
			lineHandle.RegisterMember("text", &LineModel::text);
			lineHandle.RegisterMember("color", &LineModel::color);
			lineHandle.RegisterMember("index", &LineModel::index);
			lineHandle.RegisterMember("used", &LineModel::used);
		}
		constructor.RegisterArray<Rml::Vector<LineModel>>();

		constructor.Bind("tab", &m_model.tab);
		constructor.Bind("chat_with", &m_model.chatWith);
		constructor.Bind("chat_status", &m_model.chatStatus);
		constructor.Bind("chat_text", &m_model.chatText);
		constructor.Bind("buddies", &m_model.buddies);
		constructor.Bind("chat", &m_model.chat);
		constructor.Bind("blocked", &m_model.blocked);
		constructor.Bind("blocked_count", &m_model.blockedCount);

		constructor.BindEventCallback("buddy_press", &RmlGOCommunicator::onBuddyPress, this);
		constructor.BindEventCallback("chat_press", &RmlGOCommunicator::onChatPress, this);
		constructor.BindEventCallback("blocked_press", &RmlGOCommunicator::onBlockedPress, this);
		constructor.BindEventCallback("tab_buddies", &RmlGOCommunicator::onTabBuddies, this);
		constructor.BindEventCallback("tab_blocked", &RmlGOCommunicator::onTabBlocked, this);
		constructor.BindEventCallback("close", &RmlGOCommunicator::onClose, this);
		constructor.BindEventCallback("chat_committed", &RmlGOCommunicator::onChatCommitted, this);
		constructor.BindEventCallback("send", &RmlGOCommunicator::onSend, this);
		m_modelHandle = constructor.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen("Data/UI/Communicator.rml");
}

void RmlGOCommunicator::open()
{
	load();
	if (!m_document)
		return;
	// A fresh overlay: read everything again.
	m_buddiesSignature = m_chatSignature = m_blockedSignature = 0;
	if (!m_document->IsVisible())
		m_document->Show();
	m_document->PullToFront();
}

void RmlGOCommunicator::close()
{
	if (m_document && m_document->IsVisible())
		m_document->Hide();
}

bool RmlGOCommunicator::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlGOCommunicator::back()
{
	press("WOLBuddyOverlay.wnd:ButtonHide");
}

//-------------------------------------------------------------------------------------------------
void RmlGOCommunicator::read(GameWindow *root)
{
	GameWindow *ignoreParent = child(root, "WOLBuddyOverlay.wnd:IgnoreParent");
	const Rml::String tab = ignoreParent != nullptr && !ignoreParent->winIsHidden() ? "blocked" : "buddies";
	if (tab != m_model.tab)
	{
		m_model.tab = tab;
		if (m_modelHandle)
			m_modelHandle.DirtyVariable("tab");
	}

	if (GameWindow *buddies = child(root, "WOLBuddyOverlay.wnd:ListboxBuddies"))
		readBuddies(buddies);
	if (GameWindow *chat = child(root, "WOLBuddyOverlay.wnd:ListboxBuddyChat"))
		readChat(chat);
	if (GameWindow *blocked = child(root, "WOLBuddyOverlay.wnd:ListboxIgnore"))
		readBlocked(blocked);
}

// GenOnlineUpdateBuddyInfo()'s rows: a friend's name opens with a filled or empty circle and may end with " [unread]"; the second
// column is the status (Online - presence, Offline, Recently Played With, a request...).
void RmlGOCommunicator::readBuddies(GameWindow *listbox)
{
	const Int count = GadgetListBoxGetNumEntries(listbox);
	const bool hasStatus = GadgetListBoxGetNumColumns(listbox) > 1;
	Int selected = -1;
	GadgetListBoxGetSelected(listbox, &selected);

	std::wstring signature;
	for (Int row = 0; row < count; ++row)
	{
		Color nameColor = 0;
		Color statusColor = 0;
		addSignature(signature, GadgetListBoxGetTextAndColor(listbox, &nameColor, row, 0), nameColor);
		if (hasStatus)
			addSignature(signature, GadgetListBoxGetTextAndColor(listbox, &statusColor, row, 1), statusColor);
	}
	signature += (wchar_t)(selected + 1);
	const size_t hash = std::hash<std::wstring>()(signature);
	if (hash == m_buddiesSignature)
		return;
	m_buddiesSignature = hash;

	m_model.chatWith.clear();
	m_model.chatStatus.clear();
	m_buddyRows.beginUpdate();
	for (Int row = 0; row < count; ++row)
	{
		Color nameColor = 0;
		std::wstring name = GadgetListBoxGetTextAndColor(listbox, &nameColor, row, 0).str();
		BuddyModel &buddy = m_buddyRows.next();
		if (name.size() >= 2 && name[1] == L' ' && (name[0] == 0x25CF || name[0] == 0x25CC))
		{
			buddy.dot = name[0] == 0x25CF ? "on" : "off";
			name.erase(0, 2);
		}
		const size_t unread = name.rfind(L" [");
		if (unread != std::wstring::npos && !name.empty() && name.back() == L']')
		{
			buddy.unread = _wtoi(name.c_str() + unread + 2);
			name.erase(unread);
		}
		buddy.name = unicodeToUtf8(UnicodeString(name.c_str()));
		buddy.nameColor = RmlChatColorHex(nameColor);
		buddy.status = hasStatus ? unicodeToUtf8(GadgetListBoxGetText(listbox, row, 1)) : Rml::String();
		buddy.selected = row == selected;
		buddy.index = row;
		if (buddy.selected)
		{
			m_model.chatWith = buddy.name;
			m_model.chatStatus = buddy.status;
		}
	}
	m_buddyRows.endUpdate();

	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("buddies");
		m_modelHandle.DirtyVariable("chat_with");
		m_modelHandle.DirtyVariable("chat_status");
	}
}

// AddBuddyChatLine()'s rows: the conversation shown is the selected buddy's. Lines come at the end, or the box starts over.
void RmlGOCommunicator::readChat(GameWindow *listbox)
{
	const Int count = GadgetListBoxGetNumEntries(listbox);
	std::wstring signature;
	signature += (wchar_t)(count & 0xFFFF);
	if (count > 0)
	{
		Color color = 0;
		addSignature(signature, GadgetListBoxGetTextAndColor(listbox, &color, 0, 0), color);
		addSignature(signature, GadgetListBoxGetTextAndColor(listbox, &color, count - 1, 0), color);
	}
	const size_t hash = std::hash<std::wstring>()(signature);
	if (hash == m_chatSignature)
		return;
	m_chatSignature = hash;

	m_chatRows.beginUpdate();
	for (Int row = 0; row < count; ++row)
	{
		Color color = 0;
		const UnicodeString text = GadgetListBoxGetTextAndColor(listbox, &color, row, 0);
		LineModel &line = m_chatRows.next();
		line.text = unicodeToUtf8(text);
		line.color = RmlChatColorHex(color);
		line.index = row;
	}
	m_chatRows.endUpdate();
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat");
}

void RmlGOCommunicator::readBlocked(GameWindow *listbox)
{
	const Int count = GadgetListBoxGetNumEntries(listbox);
	Int selected = -1;
	GadgetListBoxGetSelected(listbox, &selected);
	std::wstring signature;
	for (Int row = 0; row < count; ++row)
	{
		Color color = 0;
		addSignature(signature, GadgetListBoxGetTextAndColor(listbox, &color, row, 0), color);
	}
	signature += (wchar_t)(selected + 1);
	const size_t hash = std::hash<std::wstring>()(signature);
	if (hash == m_blockedSignature)
		return;
	m_blockedSignature = hash;

	m_blockedRows.beginUpdate();
	for (Int row = 0; row < count; ++row)
	{
		BuddyModel &player = m_blockedRows.next();
		player.name = unicodeToUtf8(GadgetListBoxGetText(listbox, row, 0));
		player.selected = row == selected;
		player.index = row;
	}
	m_blockedRows.endUpdate();
	m_model.blockedCount = count;
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("blocked");
		m_modelHandle.DirtyVariable("blocked_count");
	}
}

//-------------------------------------------------------------------------------------------------
// A button's GBM_SELECTED to its owner, as GadgetPushButton / GadgetRadioButton send it.
void RmlGOCommunicator::press(const char *buttonName)
{
	GameWindow *root = TheWindowManager ? child(nullptr, kRootName) : nullptr;
	GameWindow *button = root ? child(root, buttonName) : nullptr;
	if (button == nullptr)
		return;
	TheWindowManager->winSendSystemMsg(button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId());
}

// A row's left press selects it (GadgetListBoxSetSelected sends GLM_SELECTED: GO shows that conversation); a right press is the
// listbox's GLM_RIGHT_CLICKED with the row and the pointer, and GO opens its menu there.
void RmlGOCommunicator::listPress(const char *listboxName, int row, Rml::Event &ev)
{
	GameWindow *root = TheWindowManager ? child(nullptr, kRootName) : nullptr;
	GameWindow *listbox = root ? child(root, listboxName) : nullptr;
	if (listbox == nullptr || row < 0 || row >= GadgetListBoxGetNumEntries(listbox))
		return;

	GenOnlineCloseRightClickMenus();
	const int button = ev.GetParameter<int>("button", 0);
	if (button == 1)
	{
		RightClickStruct rc;
		rc.mouseX = ev.GetParameter<int>("mouse_x", 0);
		rc.mouseY = ev.GetParameter<int>("mouse_y", 0);
		rc.pos = row;
		TheWindowManager->winSendSystemMsg(listbox->winGetOwner(), GLM_RIGHT_CLICKED, (WindowMsgData)listbox, (WindowMsgData)&rc);
	}
	else if (button == 0 && strcmp(listboxName, "WOLBuddyOverlay.wnd:ListboxBuddyChat") != 0)
	{
		GadgetListBoxSetSelected(listbox, row);
	}
}

// The chat line to the .wnd's entry, then its GEM_EDIT_DONE: GO sends it to the selected buddy (or asks to pick one).
void RmlGOCommunicator::sendLine(Rml::Event &ev)
{
	GameWindow *entry = TheWindowManager ? child(nullptr, "WOLBuddyOverlay.wnd:TextEntryChat") : nullptr;
	if (entry != nullptr && !m_model.chatText.empty())
	{
		GadgetTextEntrySetText(entry, utf8ToUnicode(m_model.chatText));
		TheWindowManager->winSendSystemMsg(entry->winGetOwner(), GEM_EDIT_DONE, (WindowMsgData)entry, 0);
	}
	m_model.chatText.clear();
	RmlClearChatInput(ev);
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("chat_text");
}

void RmlGOCommunicator::onBuddyPress(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (!args.empty())
		listPress("WOLBuddyOverlay.wnd:ListboxBuddies", args[0].Get<int>(), ev);
}

void RmlGOCommunicator::onChatPress(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (!args.empty())
		listPress("WOLBuddyOverlay.wnd:ListboxBuddyChat", args[0].Get<int>(), ev);
}

void RmlGOCommunicator::onBlockedPress(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args)
{
	if (!args.empty())
		listPress("WOLBuddyOverlay.wnd:ListboxIgnore", args[0].Get<int>(), ev);
}

void RmlGOCommunicator::onTabBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("WOLBuddyOverlay.wnd:RadioButtonBuddies");
}

void RmlGOCommunicator::onTabBlocked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("WOLBuddyOverlay.wnd:RadioButtonIgnore");
}

void RmlGOCommunicator::onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	press("WOLBuddyOverlay.wnd:ButtonHide");
}

// Enter in the entry (the change event's linebreak), as the LAN lobby's chat.
void RmlGOCommunicator::onChatCommitted(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	if (!ev.GetParameter<bool>("linebreak", false))
		return;
	m_model.chatText = ev.GetParameter<Rml::String>("value", m_model.chatText);
	sendLine(ev);
}

void RmlGOCommunicator::onSend(Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &)
{
	sendLine(ev);
}
