/*
**	Command & Conquer Generals Zero Hour(tm)
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

// Protocol reference: docs/stats-exporter.md
#include "Common/StatsExporterLobby.h"
#include "Common/StatsExporterLive.h"

#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// utf8_to_local

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "StatsExporter.pb.h"

namespace
{
	typedef statsexporter::ResponseStatus Status;

	const Status kOk = statsexporter::RESPONSE_STATUS_OK;
	const Status kBadRequest = statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	const Status kNotFound = statsexporter::RESPONSE_STATUS_NOT_FOUND;
	const Status kUnavailable = statsexporter::RESPONSE_STATUS_UNAVAILABLE;
	const Status kNotAllowed = statsexporter::RESPONSE_STATUS_NOT_ALLOWED;

	const Int kMinGameSpeed = 15;
	const Int kMaxGameSpeed = 61;

	// What one request works on: the screen's description plus the kind of lobby it is.
	struct Target
	{
		StatsLobbyScreen screen;
		statsexporter::LobbyKind kind;
		std::string note;
	};

	Status refuse(std::string& message, const char* reason)
	{
		message = reason;
		return kNotAllowed;
	}

	Status badRequest(std::string& message, const char* reason)
	{
		message = reason;
		return kBadRequest;
	}

	// A control the user could operate right now.
	Bool usable(GameWindow* window)
	{
		return window != NULL && !window->winIsHidden() && window->winGetEnabled();
	}

	Int comboItemWithData(GameWindow* combo, Int data)
	{
		const Int count = GadgetComboBoxGetLength(combo);
		for (Int i = 0; i < count; ++i)
		{
			if ((Int)GadgetComboBoxGetItemData(combo, i) == data)
			{
				return i;
			}
		}
		return -1;
	}

	// The same press a mouse click on the button sends to its owner.
	void pressButton(GameWindow* button)
	{
		TheWindowManager->winSendSystemMsg(button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId());
	}

	std::string lowered(const std::string& text)
	{
		std::string out = text;
		for (size_t i = 0; i < out.size(); ++i)
		{
			out[i] = (char)tolower((unsigned char)out[i]);
		}
		return out;
	}

	// Lower case with one kind of slash: how map cache keys compare.
	std::string pathKey(const std::string& text)
	{
		std::string out = lowered(text);
		for (size_t i = 0; i < out.size(); ++i)
		{
			if (out[i] == '/')
			{
				out[i] = '\\';
			}
		}
		return out;
	}

	// The map cache key for a map name as LobbyState exports it (user data folder stripped) or as the game spells it.
	Bool findMapKey(const std::string& wanted, AsciiString& key, const MapMetaData*& found)
	{
		if (TheMapCache == NULL || wanted.empty())
		{
			return FALSE;
		}
		const std::string wantedKey = pathKey(utf8_to_local(wanted));
		const std::string userData = (TheGlobalData != NULL) ? pathKey(TheGlobalData->getPath_UserData().str()) : std::string();
		for (MapCache::iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
		{
			const std::string candidate = pathKey(it->first.str());
			const Bool same = candidate == wantedKey
				|| (!userData.empty() && candidate.size() == userData.size() + wantedKey.size()
					&& candidate.compare(0, userData.size(), userData) == 0 && candidate.compare(userData.size(), std::string::npos, wantedKey) == 0);
			if (same)
			{
				key = it->first;
				found = &it->second;
				return TRUE;
			}
		}
		return FALSE;
	}

	Status openScreen(Target& target, std::string& message)
	{
		Bool open = FALSE;
		switch (StatsExporterLiveCurrentScreen())
		{
			case statsexporter::UI_SCREEN_SKIRMISH_SETUP:
				target.kind = statsexporter::LOBBY_KIND_SKIRMISH;
				open = SkirmishGameOptionsMenuStatsLobby(target.screen);
				break;
			case statsexporter::UI_SCREEN_LAN_GAME_SETUP:
				target.kind = statsexporter::LOBBY_KIND_LAN;
				open = LanGameOptionsMenuStatsLobby(target.screen);
				break;
			case statsexporter::UI_SCREEN_ONLINE_GAME_SETUP:
				target.kind = statsexporter::LOBBY_KIND_ONLINE;
				open = WOLGameSetupMenuStatsLobby(target.screen);
				break;
			default:
				break;
		}
		if (!open || target.screen.game == NULL || target.screen.game->isGameInProgress())
		{
			message = "the user is not in a game setup lobby";
			return kUnavailable;
		}
		if (target.screen.blocked != NULL)
		{
			message = target.screen.blocked;
			return kNotAllowed;
		}
		return kOk;
	}

	statsexporter::LobbyChangeRoute routeFor(const Target& target)
	{
		switch (target.kind)
		{
			case statsexporter::LOBBY_KIND_ONLINE:
				return statsexporter::LOBBY_CHANGE_REQUESTED_FROM_SERVICE;
			case statsexporter::LOBBY_KIND_LAN:
				return target.screen.isHost ? statsexporter::LOBBY_CHANGE_APPLIED : statsexporter::LOBBY_CHANGE_REQUESTED_FROM_HOST;
			default:
				return statsexporter::LOBBY_CHANGE_APPLIED;
		}
	}

	// ---- slot group ----------------------------------------------------------------------------------------------------

	// The Open, Closed and AI entries of a slot's player dropdown; the dropdown's entry numbers are the SlotState values.
	Status changeSlotState(const Target& target, Int slot, SlotState state, Bool kick, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (!screen.isHost)
		{
			return refuse(message, "only the host changes what a slot holds");
		}
		if (slot == screen.localSlot)
		{
			return refuse(message, "the host's own slot cannot be changed");
		}
		GameSlot* gameSlot = screen.game->getSlot(slot);
		if (kick != gameSlot->isHuman())
		{
			return refuse(message, kick ? "no human holds that slot" : "a human holds that slot: use kick");
		}
		GameWindow* combo = screen.comboPlayer[slot];
		if (!usable(combo))
		{
			return refuse(message, "the slot's dropdown is not available");
		}
		if (GadgetComboBoxGetLength(combo) <= (Int)state)
		{
			message = "the slot's dropdown does not offer that entry";
			return kUnavailable;
		}
		if (gameSlot->getState() == state)
		{
			return kOk;
		}

		GadgetComboBoxSetSelectedPos(combo, (Int)state);
		if (gameSlot->getState() != state)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeFaction(const Target& target, Int slot, const std::string& side, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		GameSlot* gameSlot = screen.game->getSlot(slot);
		GameWindow* combo = screen.comboTemplate[slot];
		if (!usable(combo))
		{
			return refuse(message, "that slot's faction dropdown is not available to you");
		}

		const std::string wanted = lowered(side);
		Int wantedTemplate = -1;
		Bool special = FALSE;
		Int index = -1;
		Bool nameKnown = FALSE;
		if (wanted == "random")
		{
			wantedTemplate = PLAYERTEMPLATE_RANDOM;
			special = TRUE;
			nameKnown = TRUE;
		}
		else if (wanted == "observer")
		{
			wantedTemplate = PLAYERTEMPLATE_OBSERVER;
			special = TRUE;
			nameKnown = TRUE;
		}
		else
		{
			for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
			{
				const PlayerTemplate* candidate = ThePlayerTemplateStore->getNthPlayerTemplate(i);
				if (candidate != NULL && !candidate->getStartingBuilding().isEmpty() && lowered(candidate->getSide().str()) == wanted)
				{
					nameKnown = TRUE;
				}
			}
		}
		if (!nameKnown)
		{
			message = "no such side";
			return kNotFound;
		}

		const Int count = GadgetComboBoxGetLength(combo);
		for (Int i = 0; i < count && index < 0; ++i)
		{
			const Int data = (Int)GadgetComboBoxGetItemData(combo, i);
			if (special)
			{
				if (data == wantedTemplate)
				{
					index = i;
				}
			}
			else
			{
				const PlayerTemplate* candidate = (data >= 0) ? ThePlayerTemplateStore->getNthPlayerTemplate(data) : NULL;
				if (candidate != NULL && lowered(candidate->getSide().str()) == wanted)
				{
					index = i;
					wantedTemplate = data;
				}
			}
		}
		if (index < 0)
		{
			return refuse(message, wanted == "observer" ? "this lobby does not allow observers" : "the lobby does not offer that side");
		}
		if (gameSlot->getPlayerTemplate() == wantedTemplate)
		{
			return kOk;
		}

		GadgetComboBoxSetSelectedPos(combo, index);
		if (gameSlot->getPlayerTemplate() != wantedTemplate)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeColor(const Target& target, Int slot, UnsignedInt argb, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		GameSlot* gameSlot = screen.game->getSlot(slot);
		GameWindow* combo = screen.comboColor[slot];
		if (!usable(combo))
		{
			return refuse(message, "that slot's colour dropdown is not available to you");
		}

		Int wantedColor = -1;
		if (argb != 0)
		{
			wantedColor = -2;
			for (Int c = 0; c < TheMultiplayerSettings->getNumColors(); ++c)
			{
				const MultiplayerColorDefinition* definition = TheMultiplayerSettings->getColor(c);
				if (definition != NULL && (UnsignedInt)definition->getColor() == argb)
				{
					wantedColor = c;
					break;
				}
			}
			if (wantedColor == -2)
			{
				return badRequest(message, "not one of the lobby colours");
			}
		}

		const Int index = comboItemWithData(combo, wantedColor);
		if (index < 0)
		{
			return refuse(message, "the colour is held by another slot");
		}
		if (gameSlot->getColor() == wantedColor)
		{
			return kOk;
		}

		GadgetComboBoxSetSelectedPos(combo, index);
		if (target.kind != statsexporter::LOBBY_KIND_ONLINE && gameSlot->getColor() != wantedColor)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeTeam(const Target& target, Int slot, Int team, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (team < -1 || team >= MAX_SLOTS / 2)
		{
			return badRequest(message, "team must be -1 to 3");
		}
		GameSlot* gameSlot = screen.game->getSlot(slot);
		GameWindow* combo = screen.comboTeam[slot];
		if (!usable(combo))
		{
			return refuse(message, "that slot's team dropdown is not available to you");
		}
		const Int index = comboItemWithData(combo, team);
		if (index < 0)
		{
			return refuse(message, "the team dropdown does not offer that team");
		}
		if (gameSlot->getTeamNumber() == team)
		{
			return kOk;
		}

		GadgetComboBoxSetSelectedPos(combo, index);
		if (target.kind != statsexporter::LOBBY_KIND_ONLINE && gameSlot->getTeamNumber() != team)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	// What a click on a start position button does for a slot of the user's own, or an AI slot of the host.
	Status changeStartPosition(const Target& target, Int slot, Int position, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (position < -1 || position >= MAX_SLOTS)
		{
			return badRequest(message, "start_position must be -1 to 7");
		}
		const MapMetaData* map = (TheMapCache != NULL) ? TheMapCache->findMap(screen.game->getMap()) : NULL;
		if (map != NULL && position >= map->m_numPlayers)
		{
			return badRequest(message, "the map has fewer start positions");
		}

		GameSlot* gameSlot = screen.game->getSlot(slot);
		if (!(slot == screen.localSlot || (screen.isHost && gameSlot->isAI())))
		{
			return refuse(message, "only your own slot, or an AI slot as the host, can be placed");
		}
		if (slot == screen.localSlot && gameSlot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
		{
			return refuse(message, "an observer has no start position");
		}

		const Int current = gameSlot->getStartPos();
		const Int buttonIndex = (position >= 0) ? position : current;
		if (buttonIndex < 0)
		{
			return kOk;
		}
		if (screen.moveStartPosition == NULL || screen.buttonStartPosition == NULL || !usable(screen.buttonStartPosition[buttonIndex]))
		{
			return refuse(message, "the start position is not selectable right now");
		}
		for (Int i = 0; position >= 0 && i < MAX_SLOTS; ++i)
		{
			if (i != slot && screen.game->getSlot(i)->getStartPos() == position)
			{
				return refuse(message, "another slot holds that start position");
			}
		}
		if (current == position)
		{
			return kOk;
		}

		screen.moveStartPosition(slot, position);
		if (target.kind != statsexporter::LOBBY_KIND_ONLINE && gameSlot->getStartPos() != position)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	// ---- lobby-wide ----------------------------------------------------------------------------------------------------

	Status changeMap(const Target& target, const std::string& mapName, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (!screen.isHost)
		{
			return refuse(message, "only the host picks the map");
		}
		if (!usable(screen.buttonSelectMap) || screen.applyMap == NULL)
		{
			return refuse(message, "the Select Map button is locked right now");
		}
		if (mapName.empty())
		{
			return badRequest(message, "map_name is empty");
		}

		AsciiString key;
		const MapMetaData* map = NULL;
		if (!findMapKey(mapName, key, map))
		{
			message = "no such map in the map cache";
			return kNotFound;
		}
		if (!map->m_isMultiplayer || map->m_displayName.isEmpty())
		{
			return refuse(message, "the map list offers multiplayer maps only");
		}
		AsciiString current = screen.game->getMap();
		current.toLower();
		if (current == key)
		{
			return kOk;
		}

		screen.applyMap(key);
		AsciiString now = screen.game->getMap();
		now.toLower();
		if (now != key)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeStartingCash(const Target& target, UnsignedInt cash, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (!screen.isHost || !usable(screen.comboStartingCash))
		{
			return refuse(message, "only the host changes the starting cash");
		}
		const Int index = comboItemWithData(screen.comboStartingCash, (Int)cash);
		if (index < 0)
		{
			std::string allowed = "starting_cash must be one of:";
			const Int count = GadgetComboBoxGetLength(screen.comboStartingCash);
			for (Int i = 0; i < count; ++i)
			{
				char text[24];
				sprintf(text, " %d", (Int)GadgetComboBoxGetItemData(screen.comboStartingCash, i));
				allowed += text;
			}
			message = allowed;
			return kBadRequest;
		}
		if ((UnsignedInt)screen.game->getStartingCash().countMoney() == cash)
		{
			return kOk;
		}

		GadgetComboBoxSetSelectedPos(screen.comboStartingCash, index);
		if (target.kind != statsexporter::LOBBY_KIND_ONLINE && (UnsignedInt)screen.game->getStartingCash().countMoney() != cash)
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeSuperweaponLimit(const Target& target, Bool limited, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (!screen.isHost || !usable(screen.checkLimitSuperweapons))
		{
			return refuse(message, "only the host changes the superweapon limit");
		}
		if ((GadgetCheckBoxIsChecked(screen.checkLimitSuperweapons) != FALSE) == (limited != FALSE))
		{
			return kOk;
		}

		GadgetCheckBoxSetChecked(screen.checkLimitSuperweapons, limited);
		if (target.kind != statsexporter::LOBBY_KIND_ONLINE && (screen.game->getSuperweaponRestriction() != 0) != (limited != FALSE))
		{
			return refuse(message, "the screen did not accept the change");
		}
		return kOk;
	}

	Status changeGameSpeed(const Target& target, UnsignedInt fps, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (screen.sliderGameSpeed == NULL || screen.setGameSpeed == NULL)
		{
			return refuse(message, "only the skirmish screen has a game speed slider");
		}
		if (fps < (UnsignedInt)kMinGameSpeed || fps > (UnsignedInt)kMaxGameSpeed)
		{
			return badRequest(message, "game_speed_fps must be 15 to 61");
		}
		if (!usable(screen.sliderGameSpeed))
		{
			return refuse(message, "the game speed slider is locked");
		}
		screen.setGameSpeed((Int)fps);
		return kOk;
	}

	// The host's Start button or a client's Accept button.
	Status pressStartOrAccept(Target& target, Bool start, std::string& message)
	{
		const StatsLobbyScreen& screen = target.screen;
		if (start && !screen.isHost)
		{
			return refuse(message, "only the host starts the game: a client accepts");
		}
		if (!start && screen.isHost)
		{
			return refuse(message, "the host has no accept button: use start");
		}
		if (!usable(screen.buttonStart))
		{
			return refuse(message, start ? "the Start button is disabled right now" : "the Accept button is disabled right now (already accepted, or the lobby is locked)");
		}

		pressButton(screen.buttonStart);
		if (start && target.kind != statsexporter::LOBBY_KIND_SKIRMISH)
		{
			target.note = "pressed Start: the game starts once every human has accepted, otherwise the players are only told the host wants to start";
		}
		return kOk;
	}

	Status runAction(Target& target, const statsexporter::LobbyControlQuery& query, std::string& message)
	{
		const Int slot = (Int)query.lobby_slot();
		const Bool slotAction = query.action_case() >= statsexporter::LobbyControlQuery::kSetSlotState
			&& query.action_case() <= statsexporter::LobbyControlQuery::kStartPosition;
		if (slotAction && (slot < 0 || slot >= MAX_SLOTS || target.screen.game->getSlot(slot) == NULL))
		{
			return badRequest(message, "lobby_slot must be 0 to 7");
		}

		switch (query.action_case())
		{
			case statsexporter::LobbyControlQuery::kSetSlotState:
			{
				SlotState state = SLOT_OPEN;
				switch (query.set_slot_state())
				{
					case statsexporter::LOBBY_SLOT_OPEN:
						state = SLOT_OPEN;
						break;
					case statsexporter::LOBBY_SLOT_CLOSED:
						state = SLOT_CLOSED;
						break;
					case statsexporter::LOBBY_SLOT_AI_EASY:
						state = SLOT_EASY_AI;
						break;
					case statsexporter::LOBBY_SLOT_AI_MEDIUM:
						state = SLOT_MED_AI;
						break;
					case statsexporter::LOBBY_SLOT_AI_BRUTAL:
						state = SLOT_BRUTAL_AI;
						break;
					default:
						return badRequest(message, "state must be OPEN, CLOSED or an AI level");
				}
				return changeSlotState(target, slot, state, FALSE, message);
			}
			case statsexporter::LobbyControlQuery::kKick:
				return changeSlotState(target, slot, query.kick().close_slot() ? SLOT_CLOSED : SLOT_OPEN, TRUE, message);
			case statsexporter::LobbyControlQuery::kFaction:
				return changeFaction(target, slot, query.faction(), message);
			case statsexporter::LobbyControlQuery::kColor:
				return changeColor(target, slot, query.color(), message);
			case statsexporter::LobbyControlQuery::kTeam:
				return changeTeam(target, slot, query.team(), message);
			case statsexporter::LobbyControlQuery::kStartPosition:
				return changeStartPosition(target, slot, query.start_position(), message);
			case statsexporter::LobbyControlQuery::kMapName:
				return changeMap(target, query.map_name(), message);
			case statsexporter::LobbyControlQuery::kStartingCash:
				return changeStartingCash(target, query.starting_cash(), message);
			case statsexporter::LobbyControlQuery::kLimitSuperweapons:
				return changeSuperweaponLimit(target, query.limit_superweapons() ? TRUE : FALSE, message);
			case statsexporter::LobbyControlQuery::kGameSpeedFps:
				return changeGameSpeed(target, query.game_speed_fps(), message);
			case statsexporter::LobbyControlQuery::kAccept:
				return pressStartOrAccept(target, FALSE, message);
			case statsexporter::LobbyControlQuery::kStart:
				return pressStartOrAccept(target, TRUE, message);
			default:
				return badRequest(message, "no action set");
		}
	}
}

int StatsExporterLobbyControl(const statsexporter::LobbyControlQuery& query, std::string& result, std::string& message)
{
	if (query.action_case() == statsexporter::LobbyControlQuery::ACTION_NOT_SET)
	{
		message = "no action set";
		return kBadRequest;
	}

	Target target;
	const Status gate = openScreen(target, message);
	if (gate != kOk)
	{
		return gate;
	}

	const Status status = runAction(target, query, message);
	if (status != kOk)
	{
		return status;
	}

	statsexporter::LobbyControlResult out;
	out.set_kind(target.kind);
	out.set_route(routeFor(target));
	out.set_note(target.note);
	out.SerializeToString(&result);
	return kOk;
}
