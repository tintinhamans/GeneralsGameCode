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

#include "W3DDevice/GameClient/RmlUi/RmlControlBarScreen.h"

#include "Common/UnicodeUtf8.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarActions.h"
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

namespace
{
	Rml::String imageName(const Image *image)
	{
		return image ? Rml::String(image->getName().str()) : Rml::String();
	}

	Rml::String percent(float value)
	{
		char text[32];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f%%", value);
		return Rml::String(text);
	}

	void toSlot(const ControlBarButtonData &button, RmlControlBarScreen::SlotModel &slot)
	{
		slot.shown = button.shown != FALSE;
		slot.enabled = button.enabled != FALSE;
		slot.checked = button.checked != FALSE;
		slot.notReady = button.notReady != FALSE;
		slot.poor = button.alwaysColor != FALSE;
		slot.flash = button.flashing != FALSE;
		slot.icon = imageName(button.image);
		slot.overlay = imageName(button.overlay);
		slot.hasClock = button.clockPercent >= 0;
		if (slot.hasClock)
		{
			// the darkened part: what is still to go for the inverse clock, what is done for the normal one
			const int percentDone = button.clockPercent < 0 ? 0 : (button.clockPercent > 100 ? 100 : button.clockPercent);
			slot.clock = percent((float)(button.clockInverse ? 100 - percentDone : percentDone));
		}
		else
			slot.clock = "0%"; // a data-style needs a value even while the clock is hidden
		slot.key = button.hotkey.str();
		for (size_t i = 0; i < slot.key.size(); ++i)
			slot.key[i] = (char)toupper((unsigned char)slot.key[i]);
		slot.text = unicodeToUtf8(button.text);
		slot.border = button.border;
	}

	const char *contextName(ControlBarContext context)
	{
		switch (context)
		{
			case CB_CONTEXT_COMMAND: return "command";
			case CB_CONTEXT_STRUCTURE_INVENTORY: return "inventory";
			case CB_CONTEXT_BEACON: return "beacon";
			case CB_CONTEXT_UNDER_CONSTRUCTION: return "construction";
			case CB_CONTEXT_MULTI_SELECT: return "multi";
			case CB_CONTEXT_OBSERVER_INFO: return "observer_info";
			case CB_CONTEXT_OBSERVER_LIST: return "observer_list";
			case CB_CONTEXT_OCL_TIMER: return "ocl";
			default: return "none";
		}
	}

	// "America", "China", "GLA": the faction accent class on the body
	Rml::String sideName(const AsciiString &side)
	{
		if (side.compareNoCase("America") == 0) return "usa";
		if (side.compareNoCase("China") == 0) return "china";
		if (side.compareNoCase("GLA") == 0) return "gla";
		return "other";
	}

	// data-cb="c3": command slot 3; q = queue, s = side, x = context button
	bool slotOf(Rml::Element *element, ControlBarButtonId &id)
	{
		for (Rml::Element *e = element; e; e = e->GetParentNode())
		{
			const Rml::Variant *attribute = e->GetAttribute("data-cb");
			if (!attribute)
				continue;
			const Rml::String value = attribute->Get<Rml::String>();
			if (value.size() < 2)
				return false;
			switch (value[0])
			{
				case 'c': id.group = CBB_COMMAND; break;
				case 'q': id.group = CBB_QUEUE; break;
				case 's': id.group = CBB_SIDE; break;
				case 'x': id.group = CBB_CONTEXT; break;
				default: return false;
			}
			id.index = atoi(value.c_str() + 1);
			return true;
		}
		return false;
	}

	bool sameSlot(const ControlBarButtonId &a, const ControlBarButtonId &b)
	{
		return a.group == b.group && a.index == b.index;
	}
}

//-------------------------------------------------------------------------------------------------
RmlControlBarScreen &RmlControlBarScreen::instance()
{
	static RmlControlBarScreen s_screen;
	return s_screen;
}

void RmlControlBarScreen::load(Rml::Context *context)
{
	if (m_document || !context)
		return;

	m_context = context;
	m_model.commands.resize(CONTROL_BAR_VISIBLE_COMMANDS);
	m_model.queue.resize(MAX_BUILD_QUEUE_BUTTONS);
	m_model.upgrades.resize(MAX_RIGHT_HUD_UPGRADE_CAMEOS);
	m_model.contextButtons.resize(CB_CTX_COUNT);
	m_model.sideButtons.resize(CB_SIDE_COUNT);

	Rml::DataModelConstructor constructor = context->CreateDataModel("controlbar");
	if (constructor)
	{
		if (Rml::StructHandle<SlotModel> slot = constructor.RegisterStruct<SlotModel>())
		{
			slot.RegisterMember("shown", &SlotModel::shown);
			slot.RegisterMember("enabled", &SlotModel::enabled);
			slot.RegisterMember("checked", &SlotModel::checked);
			slot.RegisterMember("not_ready", &SlotModel::notReady);
			slot.RegisterMember("poor", &SlotModel::poor);
			slot.RegisterMember("flash", &SlotModel::flash);
			slot.RegisterMember("has_clock", &SlotModel::hasClock);
			slot.RegisterMember("icon", &SlotModel::icon);
			slot.RegisterMember("overlay", &SlotModel::overlay);
			slot.RegisterMember("clock", &SlotModel::clock);
			slot.RegisterMember("key", &SlotModel::key);
			slot.RegisterMember("text", &SlotModel::text);
			slot.RegisterMember("border", &SlotModel::border);
		}
		constructor.RegisterArray<Rml::Vector<SlotModel>>();
		if (Rml::StructHandle<UpgradeModel> upgrade = constructor.RegisterStruct<UpgradeModel>())
		{
			upgrade.RegisterMember("shown", &UpgradeModel::shown);
			upgrade.RegisterMember("owned", &UpgradeModel::owned);
			upgrade.RegisterMember("icon", &UpgradeModel::icon);
		}
		constructor.RegisterArray<Rml::Vector<UpgradeModel>>();

		constructor.Bind("low", &m_model.low);
		constructor.Bind("context", &m_model.context);
		constructor.Bind("side", &m_model.side);
		constructor.Bind("money_shown", &m_model.moneyShown);
		constructor.Bind("money", &m_model.money);
		constructor.Bind("power_shown", &m_model.powerShown);
		constructor.Bind("power_text", &m_model.powerText);
		constructor.Bind("power_state", &m_model.powerState);
		constructor.Bind("power_fill", &m_model.powerFill);
		constructor.Bind("power_needle", &m_model.powerNeedle);
		constructor.Bind("commands_shown", &m_model.commandsShown);
		constructor.Bind("commands", &m_model.commands);
		constructor.Bind("queue_shown", &m_model.queueShown);
		constructor.Bind("queue", &m_model.queue);
		constructor.Bind("portrait_shown", &m_model.portraitShown);
		constructor.Bind("portrait", &m_model.portrait);
		constructor.Bind("portrait_overlay", &m_model.portraitOverlay);
		constructor.Bind("name", &m_model.name);
		constructor.Bind("select_count", &m_model.selectCount);
		constructor.Bind("upgrades", &m_model.upgrades);
		constructor.Bind("context_text", &m_model.contextText);
		constructor.Bind("context_has_percent", &m_model.contextHasPercent);
		constructor.Bind("context_percent", &m_model.contextPercent);
		constructor.Bind("context_buttons", &m_model.contextButtons);
		constructor.Bind("side_buttons", &m_model.sideButtons);
		constructor.Bind("general_lit", &m_model.generalLit);
		constructor.Bind("rank", &m_model.rank);
		constructor.Bind("science_points", &m_model.sciencePoints);
		constructor.Bind("experience", &m_model.experience);

		m_modelHandle = constructor.GetModelHandle();
	}

	m_document = context->LoadDocument("UI/ControlBar.rml");
	if (m_document)
	{
		m_document->AddEventListener(Rml::EventId::Mousedown, this);
		m_document->AddEventListener(Rml::EventId::Mouseup, this);
		m_document->AddEventListener(Rml::EventId::Mousemove, this);
	}
}

void RmlControlBarScreen::shutdown()
{
	if (m_document)
	{
		m_document->RemoveEventListener(Rml::EventId::Mousedown, this);
		m_document->RemoveEventListener(Rml::EventId::Mouseup, this);
		m_document->RemoveEventListener(Rml::EventId::Mousemove, this);
	}
	m_document = nullptr;
	m_context = nullptr;
	m_modelHandle = Rml::DataModelHandle();
	m_hasShown = false;
	m_hovering = false;
	m_pressing = false;
}

bool RmlControlBarScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlControlBarScreen::show()
{
	if (m_document && !m_document->IsVisible())
		m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None); // the game keeps the keyboard
}

void RmlControlBarScreen::hide()
{
	if (m_hovering && TheControlBar)
		ControlBarActions::leave(m_hovered);
	m_hovering = false;
	m_pressing = false;
	if (m_document && m_document->IsVisible())
		m_document->Hide();
}

//-------------------------------------------------------------------------------------------------
// Copies what changed since the last frame into the model and dirties only those variables.
void RmlControlBarScreen::refresh(const ControlBarData &data, bool all)
{
	if (!m_modelHandle)
		return;

	const ControlBarData &old = m_shown;

	if (all || data.stage != old.stage)
	{
		m_model.low = data.stage == CONTROL_BAR_STAGE_LOW;
		m_modelHandle.DirtyVariable("low");
	}
	if (all || data.context != old.context)
	{
		m_model.context = contextName(data.context);
		m_modelHandle.DirtyVariable("context");
	}
	if (all || data.faction != old.faction)
	{
		m_model.side = sideName(data.faction);
		m_modelHandle.DirtyVariable("side");
	}
	if (all || data.moneyShown != old.moneyShown || data.money != old.money)
	{
		m_model.moneyShown = data.moneyShown != FALSE;
		m_model.money = unicodeToUtf8(data.money);
		m_modelHandle.DirtyVariable("money_shown");
		m_modelHandle.DirtyVariable("money");
	}
	if (all || data.powerShown != old.powerShown || data.powerProduction != old.powerProduction || data.powerConsumption != old.powerConsumption
		|| data.powerState != old.powerState || data.powerFill != old.powerFill || data.powerNeedle != old.powerNeedle)
	{
		char text[48];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%d / %d", data.powerConsumption, data.powerProduction);
		m_model.powerShown = data.powerShown != FALSE;
		m_model.powerText = text;
		m_model.powerState = data.powerState;
		m_model.powerFill = percent(data.powerFill * 100.0f);
		m_model.powerNeedle = percent(data.powerNeedle * 100.0f);
		m_modelHandle.DirtyVariable("power_shown");
		m_modelHandle.DirtyVariable("power_text");
		m_modelHandle.DirtyVariable("power_state");
		m_modelHandle.DirtyVariable("power_fill");
		m_modelHandle.DirtyVariable("power_needle");
	}

	if (all || data.commandsShown != old.commandsShown)
	{
		m_model.commandsShown = data.commandsShown != FALSE;
		m_modelHandle.DirtyVariable("commands_shown");
	}
	bool dirty = all;
	for (int i = 0; i < CONTROL_BAR_VISIBLE_COMMANDS; ++i)
	{
		if (all || data.commands[i] != old.commands[i])
		{
			toSlot(data.commands[i], m_model.commands[i]);
			dirty = true;
		}
	}
	if (dirty)
		m_modelHandle.DirtyVariable("commands");

	if (all || data.queueShown != old.queueShown)
	{
		m_model.queueShown = data.queueShown != FALSE;
		m_modelHandle.DirtyVariable("queue_shown");
	}
	dirty = all;
	for (int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i)
	{
		if (all || data.queue[i] != old.queue[i])
		{
			toSlot(data.queue[i], m_model.queue[i]);
			dirty = true;
		}
	}
	if (dirty)
		m_modelHandle.DirtyVariable("queue");

	if (all || data.portraitShown != old.portraitShown || data.portrait != old.portrait || data.portraitOverlay != old.portraitOverlay || data.name != old.name)
	{
		m_model.portraitShown = data.portraitShown != FALSE;
		m_model.portrait = imageName(data.portrait);
		m_model.portraitOverlay = imageName(data.portraitOverlay);
		m_model.name = unicodeToUtf8(data.name);
		m_modelHandle.DirtyVariable("portrait_shown");
		m_modelHandle.DirtyVariable("portrait");
		m_modelHandle.DirtyVariable("portrait_overlay");
		m_modelHandle.DirtyVariable("name");
	}
	dirty = all;
	for (int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i)
	{
		const ControlBarUpgradeData &upgrade = data.upgrades[i];
		const ControlBarUpgradeData &was = old.upgrades[i];
		if (all || upgrade.shown != was.shown || upgrade.owned != was.owned || upgrade.image != was.image)
		{
			m_model.upgrades[i].shown = upgrade.shown != FALSE;
			m_model.upgrades[i].owned = upgrade.owned != FALSE;
			m_model.upgrades[i].icon = imageName(upgrade.image);
			dirty = true;
		}
	}
	if (dirty)
		m_modelHandle.DirtyVariable("upgrades");
	if (all || data.selectCount != old.selectCount)
	{
		m_model.selectCount = data.selectCount;
		m_modelHandle.DirtyVariable("select_count");
	}

	if (all || data.contextText != old.contextText || data.contextPercent != old.contextPercent)
	{
		m_model.contextText = unicodeToUtf8(data.contextText);
		m_model.contextHasPercent = data.contextPercent >= 0;
		m_model.contextPercent = percent((float)(data.contextPercent < 0 ? 0 : data.contextPercent));
		m_modelHandle.DirtyVariable("context_text");
		m_modelHandle.DirtyVariable("context_has_percent");
		m_modelHandle.DirtyVariable("context_percent");
	}
	dirty = all;
	for (int i = 0; i < CB_CTX_COUNT; ++i)
	{
		if (all || data.contextButtons[i] != old.contextButtons[i])
		{
			toSlot(data.contextButtons[i], m_model.contextButtons[i]);
			dirty = true;
		}
	}
	if (dirty)
		m_modelHandle.DirtyVariable("context_buttons");

	dirty = all;
	for (int i = 0; i < CB_SIDE_COUNT; ++i)
	{
		if (all || data.sideButtons[i] != old.sideButtons[i])
		{
			toSlot(data.sideButtons[i], m_model.sideButtons[i]);
			dirty = true;
		}
	}
	if (dirty)
		m_modelHandle.DirtyVariable("side_buttons");

	if (all || data.generalLit != old.generalLit || data.rank != old.rank || data.sciencePoints != old.sciencePoints || data.experience != old.experience)
	{
		m_model.generalLit = data.generalLit != FALSE;
		m_model.rank = data.rank;
		m_model.sciencePoints = data.sciencePoints;
		m_model.experience = percent(data.experience * 100.0f);
		m_modelHandle.DirtyVariable("general_lit");
		m_modelHandle.DirtyVariable("rank");
		m_modelHandle.DirtyVariable("science_points");
		m_modelHandle.DirtyVariable("experience");
	}

	m_shown = data;
	m_hasShown = true;
}

//-------------------------------------------------------------------------------------------------
// The .wnd buttons hear the pointer entering and leaving them; RmlUi's hover says which one it is on (it
// loses its hover once the pointer is over the world; see RmlUiManager::update()).
void RmlControlBarScreen::trackHover()
{
	ControlBarButtonId id = { CBB_COMMAND, -1 };
	Rml::Element *hover = m_context ? m_context->GetHoverElement() : nullptr;
	const bool onSlot = hover && hover->GetOwnerDocument() == m_document && slotOf(hover, id);

	if (m_hovering && (!onSlot || !sameSlot(id, m_hovered)))
	{
		ControlBarActions::leave(m_hovered);
		m_hovering = false;
	}
	if (onSlot && !m_hovering)
	{
		m_hovered = id;
		m_hovering = true;
		ControlBarActions::enter(id);
	}
}

void RmlControlBarScreen::ProcessEvent(Rml::Event &event)
{
	if (!TheControlBar)
		return;

	switch (event.GetId())
	{
		case Rml::EventId::Mousemove:
			// over a panel the pointer is the plain arrow, like over the .wnd bar (InGameUI's underWindow)
			if (TheMouse)
				TheMouse->setCursor(Mouse::ARROW);
			break;

		case Rml::EventId::Mousedown:
		{
			const int button = event.GetParameter<int>("button", 0);
			if (button != 0 && button != 1)
				break;
			trackHover();
			ControlBarButtonId id;
			if (slotOf(event.GetTargetElement(), id))
			{
				m_pressing = true;
				m_pressed = id;
				m_pressRight = button == 1;
				ControlBarActions::press(id, m_pressRight);
			}
			break;
		}

		case Rml::EventId::Mouseup:
		{
			const int button = event.GetParameter<int>("button", 0);
			if (button != 0 && button != 1)
				break;
			const bool right = button == 1;
			ControlBarButtonId id;
			if (m_pressing && m_pressRight == right && slotOf(event.GetTargetElement(), id) && sameSlot(id, m_pressed))
				ControlBarActions::release(id, right);
			if (m_pressRight == right)
				m_pressing = false;
			if (!right)
				ControlBarActions::panelRelease();
			break;
		}

		default:
			break;
	}
}

//-------------------------------------------------------------------------------------------------
void RmlControlBarScreen::tick()
{
	RmlControlBarScreen &screen = instance();

	// The view is routed while the control bar's windows are headless (ControlBar::init()).
	if (!TheControlBar || !TheControlBar->isHeadless() || !TheRmlUiManager)
	{
		screen.hide();
		return;
	}

	TheControlBar->fillData(screen.m_data);
	if (!screen.m_data.visible)
	{
		screen.hide();
		return;
	}

	screen.load(TheRmlUiManager->getContext());
	if (!screen.m_document)
		return;

	screen.refresh(screen.m_data, !screen.m_hasShown);
	screen.show();
	screen.trackHover();
}
