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

#include "W3DDevice/GameClient/RmlUi/RmlBuddyToastScreen.h"
#include "Common/UnicodeUtf8.h"

#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlayActions.h"
#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlaySession.h"
#include "W3DDevice/GameClient/RmlUi/RmlSocialDock.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

//-------------------------------------------------------------------------------------------------
RmlBuddyToastScreen &RmlBuddyToastScreen::instance()
{
	static RmlBuddyToastScreen s_screen;
	return s_screen;
}

void RmlBuddyToastScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;

	Rml::DataModelConstructor constructor = context->CreateDataModel("buddytoast");
	if (constructor)
	{
		constructor.Bind("visible", &m_model.visible);
		constructor.Bind("text", &m_model.text);
		constructor.BindEventCallback("click", &RmlBuddyToastScreen::onClick, this);
		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/BuddyToast.rml");
	if (m_document)
		m_document->Show(); // stays loaded/shown; the inner .buddy-toast div's data-if="visible"
		                     // controls whether anything actually appears, same as the .wnd's
		                     // create-once/hide-and-reuse noticeLayout.
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyToastScreen::init(Rml::Context *context)
{
	load(context);

	// The .wnd presenter would otherwise show its own popup on top of this one.
	BuddyOverlaySession::releaseWidgetToast();
	m_connections.disconnect();
	m_connections.add( BuddyToastSignals::shown().connect( [this]( const UnicodeString &text ) { onToastShown( text ); } ) );
	m_connections.add( BuddyToastSignals::dismissed().connect( [this]() { onToastDismissed(); } ) );
}

void RmlBuddyToastScreen::shutdown()
{
	m_connections.disconnect();
	m_document = nullptr; // Rml::Shutdown() (RmlUiManager::shutdown()) destroys the document itself
	m_context = nullptr;
	m_modelHandle = Rml::DataModelHandle();
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyToastScreen::onToastShown( const UnicodeString &text )
{
	m_model.text = unicodeToUtf8( text );
	m_model.visible = true;
	if (m_modelHandle)
	{
		m_modelHandle.DirtyVariable("text");
		m_modelHandle.DirtyVariable("visible");
	}
}

void RmlBuddyToastScreen::onToastDismissed()
{
	m_model.visible = false;
	if (m_modelHandle)
		m_modelHandle.DirtyVariable("visible");
}

//-------------------------------------------------------------------------------------------------
void RmlBuddyToastScreen::onClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &)
{
	// Mirrors PopupBuddyNotificationSystem's GBM_SELECTED: click reopens the buddy overlay. The
	// toast itself keeps showing until its own timer expires (tickToast()), same as the .wnd. Over an
	// online screen that is the social dock, opened on the conversation the toast was about.
	RmlSocialDock::instance().noteToastClicked();
	BuddyOverlayActions::open();
}

//-------------------------------------------------------------------------------------------------
void InitRmlBuddyToastScreen(Rml::Context *context)
{
	RmlBuddyToastScreen::instance().init(context);
}

void ShutdownRmlBuddyToastScreen()
{
	RmlBuddyToastScreen::instance().shutdown();
}
