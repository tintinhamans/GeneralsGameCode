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

// FILE: RmlBuddyToastScreen.h //////////////////////////////////////////////////
// RmlUi replacement for PopupBuddyListNotification.wnd, the bottom-right "new
// friend request"/"invite sent" toast (see showNotificationBox()/
// deleteNotificationBox() in WOLBuddyOverlay.cpp). Unlike every other RmlScreen
// this isn't opened/closed by a .wnd path through RmlUiScreenRegistry: it's a
// single persistent document, loaded once at RmlUiManager::init() and shown on
// top of whatever screen/popup is currently up (same "renders on top of
// everything else this frame" precedent as RmlUiManager::render() itself).
//
// Ownership: RmlUiManager::init() calls InitRmlBuddyToastScreen() only when
// !m_useLegacyMenus, which connects this screen to BuddyToastSignals after
// BuddyOverlaySession::releaseWidgetToast() has disconnected WOLBuddyOverlay.cpp's
// own static-initializer-connected .wnd presenter (BuddyToastPresenter), so only
// one of them ever shows a toast for as long as RmlUi owns the shell. The -wnd
// command-line flag never calls InitRmlBuddyToastScreen(), so the .wnd toast
// keeps working unchanged there.
//
// tickToast()'s auto-dismiss check needs a per-frame call regardless of which
// screen is current (a toast can be showing over any of them, or over none
// while mid-transition); RmlUiManager::update() -- already called unconditionally
// every frame from W3DDisplay::draw() -- calls BuddyOverlaySession::tickToast()
// for exactly this reason (harmless no-op the rest of the time, and harmless if
// the .wnd presenter is the one currently connected).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Signal.h"
#include "Common/UnicodeString.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlBuddyToastScreen
{
public:
	static RmlBuddyToastScreen &instance();

	void init(Rml::Context *context);
	void shutdown();

private:
	RmlBuddyToastScreen() = default;

	// BuddyToastSignals targets, connected in init().
	void onToastShown(const UnicodeString &text);
	void onToastDismissed();

	void load(Rml::Context *context);
	void onClick(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &); // mirrors PopupBuddyNotificationSystem's GBM_SELECTED

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	SignalConnections m_connections; // BuddyToastSignals, connected from init() to shutdown()
	Rml::DataModelHandle m_modelHandle;

	struct Model
	{
		bool visible = false;
		Rml::String text;
	} m_model;
};

void InitRmlBuddyToastScreen(Rml::Context *context); ///< RmlUiManager::init(), only when !m_useLegacyMenus
void ShutdownRmlBuddyToastScreen();                  ///< RmlUiManager::shutdown()
