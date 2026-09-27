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

// FILE: RmlScreen.h //////////////////////////////////////////////////////////
// Minimal interface for a shell screen implemented as an RmlUi document.
// RmlUiManager owns at most one active RmlScreen at a time: it loads the
// document, binds a data model and event listeners once, then shows/hides
// on demand. Later phases route other Shell::push/pop screens through the
// same interface (one RmlScreen subclass per screen, one call to showScreen()
// from wherever the .wnd version currently creates its WindowLayout).
///////////////////////////////////////////////////////////////////////////////

#pragma once

namespace Rml { class Context; }

//-------------------------------------------------------------------------------------------------
class RmlScreen
{
public:
	virtual ~RmlScreen() {}

	// Loads the .rml document and binds data model/events. Safe to call more than once;
	// implementations should no-op if already loaded. Called lazily by RmlUiManager::showScreen().
	virtual void load(Rml::Context *context) = 0;

	virtual void show() = 0;
	virtual void hide() = 0;
	virtual bool isVisible() const = 0;

	// Called when Escape is pressed, or a document element fires the equivalent "back" event.
	// Typical implementation: behave like the .wnd Cancel/Back button.
	virtual void onBack() = 0;
};
