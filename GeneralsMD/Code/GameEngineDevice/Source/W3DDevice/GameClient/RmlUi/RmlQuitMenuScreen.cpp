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

#include "W3DDevice/GameClient/RmlUi/RmlQuitMenuScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GUI/GUICallbacks/Menus/MainMenuActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/QuitMenuActions.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

namespace
{
	// ButtonExit's Yes callback: quit first, then destroy the menu, as the .wnd exitQuitMenu() does.
	// GameLogic::quit() re-opens the quit menu instead of quitting when canOpenQuitMenu() is true,
	// and destroyQuitMenu() clears the quit-menu-visible flag that keeps it false.
	// GameWinMsgBoxFunc is a plain void(*)(); can't bind the instance, so these are free
	// functions the same way RmlMainMenuScreen.cpp's quitConfirmedCallback() is.
	void quitConfirmedCallback()
	{
		QuitMenuActions::exit();
		destroyQuitMenu();
	}

	// ButtonRestart's confirmation Yes callback (both the restart and surrender titles use it; the
	// dispatch between the two happens inside QuitMenuActions::confirmRestartOrSurrender()).
	void restartConfirmedCallback()
	{
		destroyQuitMenu();
		QuitMenuActions::confirmRestartOrSurrender();
	}
}

//-------------------------------------------------------------------------------------------------
RmlQuitMenuScreen &RmlQuitMenuScreen::instance()
{
	static RmlQuitMenuScreen s_screen;
	return s_screen;
}

void RmlQuitMenuScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("quitmenu");
	if (constructor)
	{
		constructor.Bind("restart_label", &m_model.restartLabel);
		constructor.Bind("exit_label", &m_model.exitLabel);
		constructor.Bind("restart_disabled", &m_model.restartDisabled);
		constructor.Bind("show_saveload", &m_model.showSaveLoad);
		constructor.Bind("saveload_disabled", &m_model.saveLoadDisabled);
		constructor.Bind("options_disabled", &m_model.optionsDisabled);

		constructor.BindEventCallback("exit", &RmlQuitMenuScreen::onExit, this);
		constructor.BindEventCallback("restart", &RmlQuitMenuScreen::onRestart, this);
		constructor.BindEventCallback("return_to_game", &RmlQuitMenuScreen::onReturn, this);
		constructor.BindEventCallback("options", &RmlQuitMenuScreen::onOptions, this);
		constructor.BindEventCallback("save_load", &RmlQuitMenuScreen::onSaveLoad, this);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/QuitMenu.rml");
}

void RmlQuitMenuScreen::refreshButtonState()
{
	m_model.restartLabel = TheGameText ? unicodeToUtf8(TheGameText->fetch(QuitMenuActions::restartLabelKey())) : Rml::String();
	m_model.exitLabel = TheGameText ? unicodeToUtf8(TheGameText->fetch(QuitMenuActions::exitLabelKey())) : Rml::String();
	m_model.restartDisabled = !QuitMenuActions::isRestartEnabled();
	m_model.saveLoadDisabled = QuitMenuActions::isCinematicInputDisabled();
	m_model.optionsDisabled = QuitMenuActions::isCinematicInputDisabled();

	if (m_modelHandle)
		m_modelHandle.DirtyAllVariables();
}

void RmlQuitMenuScreen::open(bool noSaveVariant)
{
	if (!TheRmlUiManager)
		return;

	load(TheRmlUiManager->getContext());
	if (!m_document)
		return;

	m_model.showSaveLoad = !noSaveVariant;
	refreshButtonState();

	m_document->Show();
}

void RmlQuitMenuScreen::close()
{
	if (m_document)
		m_document->Hide();
}

bool RmlQuitMenuScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlQuitMenuScreen::onExit(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"), &quitConfirmedCallback, nullptr);
}

void RmlQuitMenuScreen::onRestart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	if (TheGameLogic->isInMultiplayerGame())
		MessageBoxYesNo(TheGameText->fetch("GUI:SurrenderConfirmationTitle"), TheGameText->fetch("GUI:SurrenderConfirmation"), &restartConfirmedCallback, nullptr);
	else
		MessageBoxYesNo(TheGameText->fetch("GUI:RestartConfirmationTitle"), TheGameText->fetch("GUI:RestartConfirmation"), &restartConfirmedCallback, nullptr);
}

void RmlQuitMenuScreen::onReturn(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	ToggleQuitMenu(); // hides this menu; see QuitMenu.cpp
}

void RmlQuitMenuScreen::onOptions(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	MainMenuActions::openOptions();
}

void RmlQuitMenuScreen::onSaveLoad(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	openQuitMenuSaveLoad();
}

void RmlQuitMenuScreen::back()
{
	ToggleQuitMenu(); // hides this menu; see QuitMenu.cpp
}

//-------------------------------------------------------------------------------------------------
void OpenRmlQuitMenuScreen()
{
	RmlQuitMenuScreen::instance().open(false);
}

void CloseRmlQuitMenuScreen()
{
	RmlQuitMenuScreen::instance().close();
}

void OpenRmlQuitNoSaveScreen()
{
	RmlQuitMenuScreen::instance().open(true);
}

void CloseRmlQuitNoSaveScreen()
{
	RmlQuitMenuScreen::instance().close();
}
