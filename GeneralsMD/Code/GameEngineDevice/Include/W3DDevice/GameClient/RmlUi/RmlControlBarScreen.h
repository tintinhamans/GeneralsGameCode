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

// FILE: RmlControlBarScreen.h ////////////////////////////////////////////////
// RmlUi view of the in-game control bar (ControlBar.wnd and its family). The ControlBar keeps driving its
// .wnd windows, headless while this view is routed (see ControlBarData.h); every frame this copies
// ControlBar::fillData() into a data model, dirtying only what changed, and hands the pointer on its
// buttons to ControlBarActions, which gives it to the .wnd buttons.
//
// Shown while ControlBarParent is shown, so everything that shows, hides or collapses the bar (game start,
// scripts, the HUD toggle, observers) drives it. An overlay over the battlefield: the body takes no
// pointer events, only its panels do, and it never takes the keyboard.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/ControlBarData.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class Element; class ElementDocument; }

//-------------------------------------------------------------------------------------------------
// <radar data-world-middle/>: the engine draws the radar (or the radar movie) in this element's box, in its place in the
// document, through the headless LeftHUD window it keeps on that box; see ControlBarActions::placeRadar().
class RmlRadarElement : public Rml::Element
{
public:
	explicit RmlRadarElement(const Rml::String &tag) : Rml::Element(tag) {}

protected:
	virtual void OnRender() override;
};

//-------------------------------------------------------------------------------------------------
class RmlControlBarScreen : public Rml::EventListener
{
public:
	static RmlControlBarScreen &instance();

	static void tick(); ///< RmlUiManager::update(), before the context updates
	void shutdown();	///< RmlUiManager::shutdown(): the context and the document go away
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); } ///< after Rml::Shutdown() freed them

	bool isVisible() const;
	bool onKey(unsigned char key, unsigned short state); ///< Escape in the beacon's text; see RmlUiManager::init()

	struct SlotModel
	{
		bool shown = false;
		bool enabled = false;
		bool checked = false;
		bool notReady = false;
		bool poor = false;			///< can't afford: dimmed but not greyed
		bool flash = false;
		bool hasClock = false;
		Rml::String icon;
		Rml::String overlay;
		Rml::String clock = "0%";	///< height of the darkened part, "37%"
		Rml::String key;				///< hotkey letter
		Rml::String text;
		int border = 0;					///< CommandButtonMappedBorderType
	};

	struct ObserverPlayerModel
	{
		bool shown = false;
		Rml::String icon;
		Rml::String name;
		Rml::String color = "#FFFFFF";
	};

	struct UpgradeModel
	{
		bool shown = false;
		bool owned = false;
		Rml::String icon;
	};

private:
	RmlControlBarScreen() {}

	void load(Rml::Context *context);
	void show();
	void hide();
	void refresh(const ControlBarData &data, bool all);
	void trackHover();
	void onBeaconChange(Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &);
	Rml::Element *beaconEntry() const;

	virtual void ProcessEvent(Rml::Event &event) override;

	struct Model
	{
		bool low = false;
		bool observer = false; // the observer bar: the neutral look
		Rml::String context;
		Rml::String side;
		bool moneyShown = false;
		Rml::String money;
		bool powerShown = false;
		Rml::String powerText;
		int powerState = 0;
		Rml::String powerFill;
		Rml::String powerNeedle;
		bool commandsShown = false;
		Rml::Vector<SlotModel> commands;
		bool queueShown = false;
		Rml::Vector<SlotModel> queue;
		bool portraitShown = false;
		Rml::String portrait;
		Rml::String portraitOverlay;
		Rml::String name;
		int selectCount = 0;
		Rml::Vector<UpgradeModel> upgrades;
		Rml::String contextText;
		bool contextHasPercent = false;
		Rml::String contextPercent;
		Rml::Vector<SlotModel> contextButtons;
		Rml::Vector<SlotModel> sideButtons;
		bool generalLit = false;
		int rank = 0;
		int sciencePoints = 0;
		Rml::String experience;
		bool hasRadar = false;
		bool radarAlert = false;
		bool cameoMovie = false;
		bool tooltipShown = false;
		Rml::String tooltipName;
		Rml::String tooltipKey;
		Rml::String tooltipCost;
		Rml::String tooltipDescription;
		bool beaconEditable = false;
		Rml::String beaconText;
		bool shortcutsShown = false;
		Rml::Vector<SlotModel> shortcuts;
		bool scienceShown = false;
		Rml::String scienceTitle;
		Rml::Vector<SlotModel> sciences;
		bool observerListShown = false;
		Rml::Vector<ObserverPlayerModel> observerPlayers;
		bool observerInfoShown = false;
		Rml::String observerName;
		Rml::String observerColor = "#FFFFFF";
		Rml::String observerFlag;
		Rml::String observerUnits;
		Rml::String observerBuildings;
		Rml::String observerKills;
		Rml::String observerLosses;
		Rml::Vector<SlotModel> observerButtons;
		bool replay = false;
	};

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	Model m_model;

	ControlBarData m_data;		///< this frame's snapshot
	ControlBarData m_shown;		///< what the model holds
	bool m_hasShown = false;

	// the pointer on a button, as the window manager tracks it for the .wnd buttons
	bool m_hovering = false;
	ControlBarButtonId m_hovered;
	bool m_pressing = false;
	bool m_pressRight = false;
	ControlBarButtonId m_pressed;

	// the pointer on the radar: LeftHUDInput hears it enter, leave, move and press
	bool m_onRadar = false;
	bool m_onSciencePanel = false;
	bool m_beaconFocused = false; ///< the beacon's entry got the focus when the beacon context came up
	int m_radarButton = -1; ///< held since a press on the radar; 0 left, 1 right
};
