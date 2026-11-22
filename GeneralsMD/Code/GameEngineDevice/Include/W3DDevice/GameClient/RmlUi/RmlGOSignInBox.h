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

// Ronin @feature 22/11/2026 DX9: Generals Online's sign-in dialog (GOSignInDialog.wnd: the update check, update, download, errors,
// sign-in) drawn by RmlUi. GO's code runs unchanged: this hides its window, shows what it holds, and presses its buttons.
#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }
class GameWindow;

class RmlGOSignInBox
{
public:
	static RmlGOSignInBox &instance();
	static void tick();		// RmlUiManager::update(): opens with GO's dialog window, follows it, closes when it goes
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void open();
	void close();
	bool isVisible() const;
	void back();			// Escape: the dialog's Cancel (OK on an error) whenever it shows, as signInDialogInput()

private:
	RmlGOSignInBox() {}

	void load();
	void read(GameWindow *root);
	void press(const char *buttonName);

	void onCancel(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onUpdate(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onAnother(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;

	struct Model
	{
		Rml::String title;
		Rml::String status;
		Rml::String message;
		bool showMessage = false;
		bool showProgress = false;
		Rml::String progressStyle = "0%";
		Rml::String sizeText;
		bool showCancel = false;
		bool cancelIsOk = false;		// the error step's OK goes where the answer goes, far right
		Rml::String cancelLabel;
		bool showUpdate = false;
		Rml::String updateLabel;
		bool showAnother = false;
		Rml::String anotherLabel;
		bool busy = false;				// checking or signing in: the streak runs
	} m_model;
};
