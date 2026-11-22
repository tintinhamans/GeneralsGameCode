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

// Ronin @feature 22/11/2026 DX9: GO's buddy notification (GenOnlineShowNotificationBox()) as an RmlUi card over the RmlUi shell.
// GO creates, words, times and removes its .wnd; this hides it, shows its text and clicks its button. Elsewhere GO's card stays.
#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class ElementDocument; class Event; }

class RmlGOToast
{
public:
	static RmlGOToast &instance();
	static void tick();		// RmlUiManager::update()
	void releaseRml() { m_document = nullptr; m_modelHandle = Rml::DataModelHandle(); }

private:
	RmlGOToast() {}

	void load();
	void show();
	void hide();
	bool isVisible() const;
	void onPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	Rml::String m_text;
};
