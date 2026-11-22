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

// Ronin @feature 22/11/2026 DX9: GO's Communicator (GenOnlineBuddyOverlay.wnd) as a movable RmlUi window over the hub. GO fills and
// answers its .wnd; this hides it, shows its lists and sends its gadgets' input to their owner. Elsewhere GO's window stays.
#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <cstddef>

namespace Rml { class ElementDocument; class Event; }
class GameWindow;

class RmlGOCommunicator
{
public:
	static RmlGOCommunicator &instance();
	static void tick();		// RmlUiManager::update(): follows GO's buddy overlay over the hub
	void releaseRml() { m_document = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back();			// Escape: the .wnd's Hide button, as its ESC

private:
	RmlGOCommunicator() : m_buddyRows(m_model.buddies), m_chatRows(m_model.chat), m_blockedRows(m_model.blocked) {}

	void load();
	void read(GameWindow *root);
	void readBuddies(GameWindow *listbox);
	void readChat(GameWindow *listbox);
	void readBlocked(GameWindow *listbox);
	void press(const char *buttonName);
	void listPress(const char *listboxName, int row, Rml::Event &ev);
	void sendLine(Rml::Event &ev);

	void onBuddyPress(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatPress(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBlockedPress(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onTabBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onTabBlocked(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onClose(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onChatCommitted(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSend(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	GameWindow *m_hiddenRoot = nullptr;	// the .wnd this hid, shown again if the hub goes while the overlay stays
	size_t m_buddiesSignature = 0;			// the lists are rebuilt only when what GO shows changed
	size_t m_chatSignature = 0;
	size_t m_blockedSignature = 0;

	struct BuddyModel
	{
		Rml::String name;
		Rml::String nameColor;
		Rml::String status;
		Rml::String dot;		// on, off, or empty for a non-friend row
		int unread = 0;
		bool selected = false;
		int index = 0;			// the .wnd listbox row
		bool used = true;
	};

	struct LineModel
	{
		Rml::String text;
		Rml::String color;
		int index = 0;
		bool used = true;
	};

	struct Model
	{
		Rml::String tab = "buddies";
		Rml::String chatWith;
		Rml::String chatStatus;
		Rml::String chatText;
		Rml::Vector<BuddyModel> buddies;
		Rml::Vector<LineModel> chat;
		Rml::Vector<BuddyModel> blocked;
		int blockedCount = 0;
	} m_model;

	RmlGrowOnlyList<BuddyModel> m_buddyRows;
	RmlGrowOnlyList<LineModel> m_chatRows;
	RmlGrowOnlyList<BuddyModel> m_blockedRows;
};
