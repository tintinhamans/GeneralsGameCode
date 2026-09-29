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

// FILE: RmlChatInput.h //////////////////////////////////////////////////////
// Shared by the chat screens' commit handlers (Enter in the entry, or the send button).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

// Empties the chat entry now instead of leaving it to the data model. The data view only writes a
// cleared model back to the element on the next update, so a second commit event in the same frame
// would read the old text out of the element again and send the line twice. Works from the
// entry's own "change" event or from a sibling send button's click (the entry is the document's
// input.chat-input).
inline void RmlClearChatInput(Rml::Event &ev)
{
	Rml::Element *element = ev.GetCurrentElement();
	if (element && element->GetTagName() != "input")
	{
		Rml::ElementDocument *document = element->GetOwnerDocument();
		element = document ? document->QuerySelector("input.chat-input") : nullptr;
	}
	if (Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element))
		control->SetValue(Rml::String());
}
