/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// TheSuperHackers @feature simonx645 16/11/2026 Pathfinding test runner (-pathScenario / -pathBench), see PathScenario.h.
//
// FILE FORMAT (one item per line; # or ; starts a comment; a value with spaces goes in double quotes)
//
//   map "Maps\Iron Dragon\Iron Dragon.map"     the map (as the map list names it)
//   seed 1234                                   the game's random seed (default 1)
//   end_frame 9000                              ends the run at this logic frame (default 108000 = one hour)
//   player <slot 1-8> <human|observer|easy|medium|hard> <faction|random> <start 1-8|random> [team <n>] [ai on|off]
//                                               slot 1 is the local player (human or observer); "ai off" makes a computer
//                                               player hold (no AI thinking; its units still defend themselves)
//
// Commands, each with a trigger in front:
//   <frame> <command>                           at that logic frame
//   when arrived <group> <percent> +<n> <command>    n frames after that share of the group arrived
//   when near <group> <x> <y> <radius> +<n> <command> n frames after a unit of the group came within radius cells
//   when after <tag> +<n> <command>              n frames after the command tagged <tag> ran
// Any command may end with "tag <name>". Coordinates are cells (world / 10); facing is in degrees, 0 = east.
//
//   spawn <group> <template> <count> <player slot> <x> <y> <spacing> <facing>   a block of units, rows of ceil(sqrt(count))
//   order <group> move|attackmove|guard|reversemove <x> <y>                     as the owning player's messages
//   order <group> stop|evacuate                                                 (evacuate: the group's containers empty)
//   order <group> enter <target group>                                          into the target group's first live unit
//   order <group> gather <x> <y>                                                dock at the supply source nearest x, y
//   kill <group>                                                                the group's live units, damaged to 0
//                                                                               (a structure leaves its rubble)
//   bridge <x> <y> kill                                                         the nearest bridge, damaged to 0 (a bridge
//                                                                               with an ImmortalBody, as GenericBridge, stays:
//                                                                               the run says so)
//   bridge <x> <y> delete                                                       the nearest bridge removed from the map, as
//                                                                               TerrainLogic deletes one; what is on it falls
//   bridge <x> <y> repair <dozer template> <player slot> <from x> <from y>      a dozer is spawned (group "dozer")
//                                                                               and ordered to repair the nearest tower
//   phase <name>                                                                a new block of group rows
// spawn places structures too (as a script does): put a building in its own group to kill or enter it.
//   end                                                                         the run ends
//
// Bench files (-pathBench) take map, seed and player lines, and:
//   template <name>                             one or more; each pair is searched for each template
//   pair <sx> <sy> <gx> <gy>                    a pair, in cells
//   pairs <count> [bands <edge> <edge> ...]     random passable pairs from the seed, spread over the distance bands
//                                               (default bands 0 50 150 300 100000)

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/PathScenario.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PathProfile.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameNetwork/GameInfo.h"
#include "Lib/trig.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace PathScenario
{
	// ---------------------------------------------------------------------------------------------------------------
	// The file
	const Int MAX_TOKENS = 16;
	const Int MAX_COMMANDS = 512;
	const Int MAX_TAGS = 64;
	const Int MAX_TEMPLATES = 8;
	const Int MAX_PAIRS = 4096;
	const Int MAX_GROUP_IDS = 4096;
	const Real CELL = PATHFIND_CELL_SIZE_F;
	const Int COST_ORTHOGONAL = 10;	///< as the pathfinder's own step costs
	const Int COST_DIAGONAL = 14;

	enum Trigger { AT_FRAME, WHEN_ARRIVED, WHEN_NEAR, WHEN_AFTER };

	struct Command
	{
		Int trigger;
		UnsignedInt frame;		///< AT_FRAME: the frame; others: the frame it fires (0 until its condition is met)
		char group[48];
		Real percent, x, y, radius;
		Int delay;
		char after[32];
		char tag[32];
		char tok[MAX_TOKENS][64];
		Int count;
		Bool armed, done;
	};

	struct Slot
	{
		Bool used;
		Int type;			///< 0 human, 1 observer, 2 easy, 3 medium, 4 hard
		char faction[48];
		Int start;			///< -1 random
		Int team;
		Bool aiOff;
	};

	char ScenarioFile[512] = "";
	char BenchFile[512] = "";
	char MapName[300] = "";
	Int Seed = 1;
	UnsignedInt EndFrame = 108000;
	Slot Slots[8];
	Command Commands[MAX_COMMANDS];
	Int CommandCount = 0;
	struct TagRun { char name[32]; UnsignedInt frame; };
	TagRun Tags[MAX_TAGS];
	Int TagCount = 0;
	char Templates[MAX_TEMPLATES][64];
	Int TemplateCount = 0;
	struct Pair { Int sx, sy, gx, gy; };
	Pair Pairs[MAX_PAIRS];
	Int PairCount = 0;
	Int RandomPairs = 0;
	Int Bands[16] = { 0, 50, 150, 300, 100000 };
	Int BandCount = 5;
	Bool Started = FALSE;
	Bool FirstFrameDone = FALSE;
	Bool Ended = FALSE;

	struct GroupIds { char name[48]; ObjectID ids[512]; Int count; };
	GroupIds GroupList[32];
	Int GroupListCount = 0;

	void Set_Scenario(const char * file)
	{
		if (file != nullptr) strncpy(ScenarioFile, file, sizeof(ScenarioFile) - 1);
	}

	void Set_Bench(const char * file)
	{
		if (file != nullptr) strncpy(BenchFile, file, sizeof(BenchFile) - 1);
	}

	Bool Active()
	{
		return ScenarioFile[0] != 0 || BenchFile[0] != 0;
	}

	Bool Done()
	{
		return Ended;
	}

	void Say(const char * format, const char * a = "", Int b = 0)
	{
		char line[600];
		snprintf(line, sizeof(line), format, a, b);
		printf("pathScenario: %s\n", line);
		fflush(stdout);
		DEBUG_LOG(("pathScenario: %s", line));
	}

	// Splits a line into tokens; double quotes keep spaces. Returns the count.
	Int Tokens(char * line, char out[][64], Int maxTokens)
	{
		Int n = 0;
		char * p = line;
		while (*p != 0 && n < maxTokens)
		{
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
			if (*p == 0 || *p == '#' || *p == ';')
				break;
			Int len = 0;
			if (*p == '"')
			{
				++p;
				while (*p != 0 && *p != '"' && len < 63) out[n][len++] = *p++;
				if (*p == '"') ++p;
			}
			else
				while (*p != 0 && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && len < 63) out[n][len++] = *p++;
			out[n][len] = 0;
			++n;
		}
		return n;
	}

	Bool Is_Number(const char * s)
	{
		if (*s == 0) return FALSE;
		for (; *s; ++s) if (*s < '0' || *s > '9') return FALSE;
		return TRUE;
	}

	Int Slot_Type(const char * s)
	{
		if (_stricmp(s, "human") == 0) return 0;
		if (_stricmp(s, "observer") == 0) return 1;
		if (_stricmp(s, "easy") == 0) return 2;
		if (_stricmp(s, "medium") == 0) return 3;
		return 4;	// hard
	}

	Bool Read(const char * file)
	{
		FILE * f = fopen(file, "rt");
		if (f == nullptr)
		{
			Say("cannot open %s", file);
			return FALSE;
		}
		char line[1024];
		char tok[MAX_TOKENS + 8][64];
		while (fgets(line, sizeof(line), f) != nullptr)
		{
			const Int n = Tokens(line, tok, MAX_TOKENS + 8);
			if (n == 0)
				continue;
			if (_stricmp(tok[0], "map") == 0 && n > 1) { strncpy(MapName, tok[1], sizeof(MapName) - 1); continue; }
			if (_stricmp(tok[0], "seed") == 0 && n > 1) { Seed = atoi(tok[1]); continue; }
			if (_stricmp(tok[0], "end_frame") == 0 && n > 1) { EndFrame = (UnsignedInt)atoi(tok[1]); continue; }
			if (_stricmp(tok[0], "template") == 0 && n > 1 && TemplateCount < MAX_TEMPLATES) { strncpy(Templates[TemplateCount++], tok[1], 63); continue; }
			if (_stricmp(tok[0], "pair") == 0 && n > 4 && PairCount < MAX_PAIRS)
			{
				Pair & p = Pairs[PairCount++];
				p.sx = atoi(tok[1]); p.sy = atoi(tok[2]); p.gx = atoi(tok[3]); p.gy = atoi(tok[4]);
				continue;
			}
			if (_stricmp(tok[0], "pairs") == 0 && n > 1)
			{
				RandomPairs = atoi(tok[1]);
				if (n > 2 && _stricmp(tok[2], "bands") == 0)
				{
					BandCount = 0;
					for (Int i = 3; i < n && BandCount < 16; ++i)
						Bands[BandCount++] = atoi(tok[i]);
				}
				continue;
			}
			if (_stricmp(tok[0], "player") == 0 && n > 4)
			{
				const Int s = atoi(tok[1]) - 1;
				if (s < 0 || s >= 8) continue;
				Slot & sl = Slots[s];
				sl.used = TRUE;
				sl.type = Slot_Type(tok[2]);
				strncpy(sl.faction, tok[3], sizeof(sl.faction) - 1);
				sl.start = _stricmp(tok[4], "random") == 0 ? -1 : atoi(tok[4]) - 1;
				sl.team = -1;
				for (Int i = 5; i + 1 < n; ++i)
				{
					if (_stricmp(tok[i], "team") == 0) sl.team = atoi(tok[i + 1]) - 1;
					if (_stricmp(tok[i], "ai") == 0) sl.aiOff = _stricmp(tok[i + 1], "off") == 0;
				}
				continue;
			}
			// a command with its trigger
			if (CommandCount >= MAX_COMMANDS)
				continue;
			Command & c = Commands[CommandCount];
			memset(&c, 0, sizeof(c));
			Int at = 0;
			if (Is_Number(tok[0]))
			{
				c.trigger = AT_FRAME;
				c.frame = (UnsignedInt)atoi(tok[0]);
				at = 1;
			}
			else if (_stricmp(tok[0], "when") == 0 && n > 2)
			{
				if (_stricmp(tok[1], "arrived") == 0 && n > 5)
				{
					c.trigger = WHEN_ARRIVED;
					strncpy(c.group, tok[2], sizeof(c.group) - 1);
					c.percent = (Real)atof(tok[3]);
					c.delay = atoi(tok[4] + (tok[4][0] == '+' ? 1 : 0));
					at = 5;
				}
				else if (_stricmp(tok[1], "near") == 0 && n > 7)
				{
					c.trigger = WHEN_NEAR;
					strncpy(c.group, tok[2], sizeof(c.group) - 1);
					c.x = (Real)atof(tok[3]); c.y = (Real)atof(tok[4]); c.radius = (Real)atof(tok[5]);
					c.delay = atoi(tok[6] + (tok[6][0] == '+' ? 1 : 0));
					at = 7;
				}
				else if (_stricmp(tok[1], "after") == 0 && n > 4)
				{
					c.trigger = WHEN_AFTER;
					strncpy(c.after, tok[2], sizeof(c.after) - 1);
					c.delay = atoi(tok[3] + (tok[3][0] == '+' ? 1 : 0));
					at = 4;
				}
				else
					continue;
			}
			else
				continue;
			for (Int i = at; i < n && c.count < MAX_TOKENS; ++i)
			{
				if (_stricmp(tok[i], "tag") == 0 && i + 1 < n)
				{
					strncpy(c.tag, tok[i + 1], sizeof(c.tag) - 1);
					++i;
					continue;
				}
				strncpy(c.tok[c.count++], tok[i], 63);
			}
			if (c.count > 0)
				++CommandCount;
		}
		fclose(f);
		return TRUE;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The start: a skirmish set up as the skirmish menu does it (SkirmishGameOptionsMenu), started as reallyDoStart does
	Bool Start(Bool viaCommandList)
	{
		const char * file = ScenarioFile[0] != 0 ? ScenarioFile : BenchFile;
		if (!Read(file))
			return FALSE;
		if (MapName[0] == 0)
		{
			Say("%s names no map", file);
			return FALSE;
		}
		if (!Slots[0].used)
		{
			Slots[0].used = TRUE;	// a local player is needed: an observer by default
			Slots[0].type = 1;
			strcpy(Slots[0].faction, "observer");
			Slots[0].start = -1;
			Slots[0].team = -1;
		}
		if (Slots[0].type >= 2)
		{
			// Slot 1 is the local player, and a skirmish needs one (a human or an observer): with a computer player there it
			// had none and the game failed at once. It runs as a human of that faction, which has no AI anyway.
			Say("player 1 is the local player: it runs as a human of its faction (no AI)");
			Slots[0].type = 0;
		}
		if (!PathProfile::Enabled)
		{
			// the counters' files go beside the scenario file
			char base[512];
			strncpy(base, file, sizeof(base) - 1);
			base[sizeof(base) - 1] = 0;
			char * dot = strrchr(base, '.');
			if (dot != nullptr && strrchr(base, '\\') < dot)
				*dot = 0;
			PathProfile::Set_Output(base);
		}
		if (ScenarioFile[0] != 0)
			PathProfile::Groups_On();

		if (TheSkirmishGameInfo == nullptr)
			TheSkirmishGameInfo = NEW SkirmishGameInfo;
		else if (TheSkirmishGameInfo->isInGame())
			TheSkirmishGameInfo->endGame();
		TheSkirmishGameInfo->init();
		TheSkirmishGameInfo->clearSlotList();
		TheSkirmishGameInfo->reset();
		TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
		TheSkirmishGameInfo->enterGame();
		for (Int s = 0; s < MAX_SLOTS && s < 8; ++s)
		{
			GameSlot slot;
			const Slot & sl = Slots[s];
			if (!sl.used)
			{
				slot.setState(SLOT_CLOSED);
				TheSkirmishGameInfo->setSlot(s, slot);
				continue;
			}
			static const SlotState states[] = { SLOT_PLAYER, SLOT_PLAYER, SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI };
			if (sl.type <= 1)
				slot.setState(SLOT_PLAYER, UnicodeString(L"Scenario"));
			else
				slot.setState(states[sl.type]);
			Int faction = PLAYERTEMPLATE_RANDOM;
			if (sl.type == 1 || _stricmp(sl.faction, "observer") == 0)
				faction = PLAYERTEMPLATE_OBSERVER;
			else if (_stricmp(sl.faction, "random") != 0 && ThePlayerTemplateStore != nullptr)
			{
				const Int num = ThePlayerTemplateStore->getTemplateNumByName(AsciiString(sl.faction));
				if (num >= 0)
					faction = num;
				else
					Say("unknown faction %s: random", sl.faction);
			}
			slot.setPlayerTemplate(faction);
			slot.setStartPos(sl.start);
			slot.setTeamNumber(sl.team);
			slot.setColor(s);
			TheSkirmishGameInfo->setSlot(s, slot);
		}
		TheSkirmishGameInfo->setSeed(Seed);
		if (TheMultiplayerSettings != nullptr)
			TheSkirmishGameInfo->setStartingCash(TheMultiplayerSettings->getDefaultStartingMoney());
		TheSkirmishGameInfo->setSuperweaponRestriction(0);
		AsciiString map(MapName);
		TheSkirmishGameInfo->setMap(map);
		AsciiString lower = map;
		lower.toLower();
		const MapMetaData * md = TheMapCache != nullptr ? TheMapCache->findMap(lower) : nullptr;
		TheSkirmishGameInfo->setMapCRC(md != nullptr ? md->m_CRC : 0);
		TheSkirmishGameInfo->setMapSize(md != nullptr ? md->m_filesize : 0);
		if (md == nullptr)
			Say("map %s is not in the map list", MapName);

		TheWritableGlobalData->m_mapName = map;
		TheWritableGlobalData->m_pendingFile = map;
		TheWritableGlobalData->m_shellMapOn = FALSE;
		TheWritableGlobalData->m_playIntro = FALSE;
		TheSkirmishGameInfo->startGame(0);
		InitRandom(Seed);

		GameMessage * msg = newInstance(GameMessage)(GameMessage::MSG_NEW_GAME);
		msg->appendIntegerArgument(GAME_SKIRMISH);
		msg->appendIntegerArgument(DIFFICULTY_NORMAL);
		msg->appendIntegerArgument(0);
		msg->appendIntegerArgument(viaCommandList ? 1000 : 0);	// headless: unthrottled; windowed: the usual limit
		TheCommandList->appendMessage(msg);
		Started = TRUE;
		Say("started %s", file);
		return TRUE;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Commands
	GroupIds * Group(const char * name, Bool make)
	{
		for (Int g = 0; g < GroupListCount; ++g)
			if (strcmp(GroupList[g].name, name) == 0)
				return &GroupList[g];
		if (!make || GroupListCount >= 32)
			return nullptr;
		GroupIds & g = GroupList[GroupListCount++];
		memset(&g, 0, sizeof(g));
		strncpy(g.name, name, sizeof(g.name) - 1);
		return &g;
	}

	Player * Slot_Player(Int slot)
	{
		return ThePlayerList != nullptr ? ThePlayerList->getPlayerFromSlotIndex(slot - 1) : nullptr;
	}

	Object * Spawn_One(const ThingTemplate * tmpl, Player * owner, Real wx, Real wy, Real angle)
	{
		Object * obj = TheThingFactory->newObject(tmpl, owner->getDefaultTeam());
		if (obj == nullptr)
			return nullptr;
		Coord3D pos;
		pos.x = wx;
		pos.y = wy;
		pos.z = TheTerrainLogic->getGroundHeight(wx, wy);
		obj->setOrientation(angle);
		obj->setPosition(&pos);
		return obj;
	}

	void Spawn(const Command & c, UnsignedInt frame)
	{
		if (c.count < 9)
			return;
		const ThingTemplate * tmpl = TheThingFactory->findTemplate(AsciiString(c.tok[2]));
		Player * owner = Slot_Player(atoi(c.tok[4]));
		if (tmpl == nullptr || owner == nullptr)
		{
			Say("spawn: unknown template or player in %s", c.tok[2]);
			return;
		}
		const Int count = atoi(c.tok[3]);
		const Real cx = (Real)atof(c.tok[5]) * CELL + CELL / 2.0f, cy = (Real)atof(c.tok[6]) * CELL + CELL / 2.0f;
		const Real spacing = (Real)atof(c.tok[7]) * CELL;
		const Real angle = (Real)atof(c.tok[8]) * PI / 180.0f;
		const Int cols = (Int)ceilf(sqrtf((Real)count));
		const Int rows = (count + cols - 1) / cols;
		GroupIds * g = Group(c.tok[1], TRUE);
		const Int profileGroup = PathProfile::Group_Index(c.tok[1]);
		for (Int i = 0; i < count; ++i)
		{
			const Int col = i % cols, row = i / cols;
			const Real ox = ((Real)col - (Real)(cols - 1) / 2.0f) * spacing, oy = ((Real)row - (Real)(rows - 1) / 2.0f) * spacing;
			Object * obj = Spawn_One(tmpl, owner, cx + ox, cy + oy, angle);
			if (obj == nullptr)
				continue;
			if (g != nullptr && g->count < 512)
				g->ids[g->count++] = obj->getID();
			PathProfile::Group_Add(profileGroup, obj);
		}
	}

	// The group's alive units, selected by their player (the select message a box-select sends), then the order.
	Bool Select(GroupIds * g, Int * playerIndex)
	{
		GameMessage * select = nullptr;
		for (Int i = 0; i < g->count; ++i)
		{
			Object * obj = TheGameLogic->findObjectByID(g->ids[i]);
			if (obj == nullptr || obj->isEffectivelyDead())
				continue;
			if (select == nullptr)
			{
				Player * p = obj->getControllingPlayer();
				if (p == nullptr)
					return FALSE;
				*playerIndex = p->getPlayerIndex();
				select = newInstance(GameMessage)(GameMessage::MSG_CREATE_SELECTED_GROUP);
				select->friend_setPlayerIndex(*playerIndex);
				select->appendBooleanArgument(TRUE);
			}
			select->appendObjectIDArgument(g->ids[i]);
		}
		if (select == nullptr)
			return FALSE;
		TheCommandList->appendMessage(select);
		return TRUE;
	}

	Object * First_Alive(GroupIds * g);
	Object * Nearest(KindOfType kind, Real wx, Real wy);

	void Order(const Command & c, UnsignedInt frame)
	{
		if (c.count < 3)
			return;
		// the order and its target are checked before anything is sent, so a bad line sends nothing
		static const char * const known[] = { "move", "attackmove", "guard", "reversemove", "stop", "evacuate", "enter", "gather" };
		Bool knownOrder = FALSE;
		for (Int k = 0; k < (Int)(sizeof(known) / sizeof(known[0])); ++k)
			if (_stricmp(c.tok[2], known[k]) == 0)
				knownOrder = TRUE;
		if (!knownOrder)
		{
			Say("order: unknown order %s", c.tok[2]);
			return;
		}
		Object * target = nullptr;
		if (_stricmp(c.tok[2], "enter") == 0 || _stricmp(c.tok[2], "gather") == 0)
		{
			if (_stricmp(c.tok[2], "enter") == 0)
				target = c.count > 3 ? First_Alive(Group(c.tok[3], FALSE)) : nullptr;
			else if (c.count > 4)
				target = Nearest(KINDOF_SUPPLY_SOURCE, (Real)atof(c.tok[3]) * CELL + CELL / 2.0f, (Real)atof(c.tok[4]) * CELL + CELL / 2.0f);
			if (target == nullptr)
			{
				Say("order %s: no target", c.tok[2]);
				return;
			}
		}
		GroupIds * g = Group(c.tok[1], FALSE);
		Int playerIndex = 0;
		if (g == nullptr || !Select(g, &playerIndex))
		{
			Say("order: group %s has no unit", c.tok[1]);
			return;
		}
		Coord3D at;
		at.x = c.count > 4 ? (Real)atof(c.tok[3]) * CELL + CELL / 2.0f : 0.0f;
		at.y = c.count > 4 ? (Real)atof(c.tok[4]) * CELL + CELL / 2.0f : 0.0f;
		at.z = TheTerrainLogic->getGroundHeight(at.x, at.y);
		Int kind = PathProfile::ORDER_MOVE;
		GameMessage * msg = nullptr;
		if (target != nullptr)
		{
			const Bool enter = _stricmp(c.tok[2], "enter") == 0;
			kind = enter ? PathProfile::ORDER_ENTER : PathProfile::ORDER_GATHER;
			msg = newInstance(GameMessage)(enter ? GameMessage::MSG_ENTER : GameMessage::MSG_DOCK);
			if (enter)
				msg->appendObjectIDArgument(INVALID_ID);	// the selected group, as a click sends it
			msg->appendObjectIDArgument(target->getID());
			at = *target->getPosition();
		}
		else if (_stricmp(c.tok[2], "reversemove") == 0)
		{
			kind = PathProfile::ORDER_REVERSE;
			msg = newInstance(GameMessage)(GameMessage::MSG_DO_REVERSE_MOVETO);
			msg->appendLocationArgument(at);
		}
		else if (_stricmp(c.tok[2], "evacuate") == 0)
		{
			kind = PathProfile::ORDER_EVACUATE;
			msg = newInstance(GameMessage)(GameMessage::MSG_EVACUATE);
		}
		else if (_stricmp(c.tok[2], "move") == 0)
		{
			msg = newInstance(GameMessage)(GameMessage::MSG_DO_MOVETO);
			msg->appendLocationArgument(at);
		}
		else if (_stricmp(c.tok[2], "attackmove") == 0)
		{
			kind = PathProfile::ORDER_ATTACKMOVE;
			msg = newInstance(GameMessage)(GameMessage::MSG_DO_ATTACKMOVETO);
			msg->appendLocationArgument(at);
		}
		else if (_stricmp(c.tok[2], "guard") == 0)
		{
			kind = PathProfile::ORDER_GUARD;
			msg = newInstance(GameMessage)(GameMessage::MSG_DO_GUARD_POSITION);
			msg->appendLocationArgument(at);
			msg->appendIntegerArgument(GUARDMODE_NORMAL);
		}
		else if (_stricmp(c.tok[2], "stop") == 0)
		{
			kind = PathProfile::ORDER_STOP;
			msg = newInstance(GameMessage)(GameMessage::MSG_DO_STOP);
		}
		else
			return;	// not reached: checked above
		msg->friend_setPlayerIndex(playerIndex);
		TheCommandList->appendMessage(msg);
		PathProfile::Group_Order(PathProfile::Group_Index(c.tok[1]), kind, at.x, at.y, frame);
	}

	Object * First_Alive(GroupIds * g)
	{
		if (g == nullptr)
			return nullptr;
		for (Int i = 0; i < g->count; ++i)
		{
			Object * obj = TheGameLogic->findObjectByID(g->ids[i]);
			if (obj != nullptr && !obj->isEffectivelyDead())
				return obj;
		}
		return nullptr;
	}

	void Kill_Object(Object * obj)
	{
		DamageInfo damage;
		damage.in.m_damageType = DAMAGE_UNRESISTABLE;
		damage.in.m_deathType = DEATH_NORMAL;
		damage.in.m_sourceID = INVALID_ID;
		BodyModuleInterface * body = obj->getBodyModule();
		damage.in.m_amount = body != nullptr ? body->getMaxHealth() * 10.0f : 1.0e9f;
		obj->attemptDamage(&damage);
	}

	void Kill(const Command & c, UnsignedInt frame)
	{
		GroupIds * g = c.count > 1 ? Group(c.tok[1], FALSE) : nullptr;
		if (g == nullptr)
		{
			Say("kill: no group %s", c.count > 1 ? c.tok[1] : "");
			return;
		}
		for (Int i = 0; i < g->count; ++i)
		{
			Object * obj = TheGameLogic->findObjectByID(g->ids[i]);
			if (obj != nullptr && !obj->isEffectivelyDead())
				Kill_Object(obj);
		}
		PathProfile::Event("kill", frame);	// rubble or a cleared footprint: re-route times count from here
	}

	Object * Nearest(KindOfType kind, Real wx, Real wy)
	{
		Object * best = nullptr;
		Real bestD = 0.0f;
		for (Object * obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
		{
			if (!obj->isKindOf(kind))
				continue;
			const Coord3D * p = obj->getPosition();
			const Real d = (p->x - wx) * (p->x - wx) + (p->y - wy) * (p->y - wy);
			if (best == nullptr || d < bestD)
			{
				best = obj;
				bestD = d;
			}
		}
		return best;
	}

	void Bridge(const Command & c, UnsignedInt frame)
	{
		if (c.count < 4)
			return;
		const Real wx = (Real)atof(c.tok[1]) * CELL + CELL / 2.0f, wy = (Real)atof(c.tok[2]) * CELL + CELL / 2.0f;
		if (_stricmp(c.tok[3], "kill") == 0)
		{
			Object * bridge = Nearest(KINDOF_BRIDGE, wx, wy);
			if (bridge == nullptr)
			{
				Say("bridge kill: no bridge on the map");
				return;
			}
			Kill_Object(bridge);
			if (bridge->getBodyModule() != nullptr && bridge->getBodyModule()->getDamageState() != BODY_RUBBLE)
				Say("bridge kill: %s took no harm (its body cannot be destroyed); use bridge <x> <y> delete",
					bridge->getTemplate()->getName().str());
			PathProfile::Event("bridge_kill", frame);
			return;
		}
		if (_stricmp(c.tok[3], "delete") == 0)
		{
			Object * bridge = Nearest(KINDOF_BRIDGE, wx, wy);
			::Bridge * logic = nullptr;	// the class (Bridge here is this file's command)
			for (::Bridge * b = bridge != nullptr ? TheTerrainLogic->getFirstBridge() : nullptr; b != nullptr; b = b->getNext())
				if (b->peekBridgeInfo()->bridgeObjectID == bridge->getID())
					logic = b;
			if (logic == nullptr)
			{
				Say("bridge delete: no bridge on the map");
				return;
			}
			// what stands on its layer falls, as when a bridge's damage state turns to rubble
			const PathfindLayerEnum layer = logic->getLayer();
			for (Object * o = TheGameLogic->getFirstObject(); o != nullptr; o = o->getNextObject())
			{
				if (o != bridge && o->getLayer() == layer)
				{
					DamageInfo fall;
					fall.in.m_damageType = DAMAGE_FALLING;
					fall.in.m_deathType = DEATH_SPLATTED;
					fall.in.m_sourceID = o->getID();
					fall.in.m_amount = HUGE_DAMAGE_AMOUNT;
					o->attemptDamage(&fall);
				}
			}
			TheTerrainLogic->deleteBridge(logic);
			PathProfile::Event("bridge_kill", frame);
			return;
		}
		if (_stricmp(c.tok[3], "repair") == 0 && c.count >= 8)
		{
			const ThingTemplate * tmpl = TheThingFactory->findTemplate(AsciiString(c.tok[4]));
			Player * owner = Slot_Player(atoi(c.tok[5]));
			Object * tower = Nearest(KINDOF_BRIDGE_TOWER, wx, wy);
			if (tmpl == nullptr || owner == nullptr || tower == nullptr)
			{
				Say("bridge repair: no dozer template, player or bridge tower (%s)", c.tok[4]);
				return;
			}
			Object * dozer = Spawn_One(tmpl, owner, (Real)atof(c.tok[6]) * CELL + CELL / 2.0f, (Real)atof(c.tok[7]) * CELL + CELL / 2.0f, 0.0f);
			if (dozer == nullptr)
				return;
			GroupIds * g = Group("dozer", TRUE);
			if (g != nullptr && g->count < 512)
				g->ids[g->count++] = dozer->getID();
			const Int profileGroup = PathProfile::Group_Index("dozer");
			PathProfile::Group_Add(profileGroup, dozer);
			GameMessage * select = newInstance(GameMessage)(GameMessage::MSG_CREATE_SELECTED_GROUP);
			select->friend_setPlayerIndex(owner->getPlayerIndex());
			select->appendBooleanArgument(TRUE);
			select->appendObjectIDArgument(dozer->getID());
			TheCommandList->appendMessage(select);
			GameMessage * repair = newInstance(GameMessage)(GameMessage::MSG_DO_REPAIR);
			repair->friend_setPlayerIndex(owner->getPlayerIndex());
			repair->appendObjectIDArgument(tower->getID());
			TheCommandList->appendMessage(repair);
			PathProfile::Group_Order(profileGroup, PathProfile::ORDER_REPAIR, tower->getPosition()->x, tower->getPosition()->y, frame);
			PathProfile::Event("bridge_repair", frame);
		}
	}

	// The match's records are written now; GameLogic skips PathProfile::End_Frame from here on (Done()), so the frame
	// that ran the end opens no second match.
	void Finish()
	{
		if (Ended)
			return;
		Ended = TRUE;
		PathProfile::End_Match();
		if (TheGameEngine != nullptr)
			TheGameEngine->setQuitting(TRUE);
		Say("ended");
	}

	void Run_Bench(UnsignedInt frame);

	void Execute(Command & c, UnsignedInt frame)
	{
		c.done = TRUE;
		if (c.tag[0] != 0 && TagCount < MAX_TAGS)
		{
			strncpy(Tags[TagCount].name, c.tag, sizeof(Tags[TagCount].name) - 1);
			Tags[TagCount].frame = frame;
			++TagCount;
		}
		const char * what = c.tok[0];
		if (_stricmp(what, "spawn") == 0) Spawn(c, frame);
		else if (_stricmp(what, "order") == 0) Order(c, frame);
		else if (_stricmp(what, "bridge") == 0) Bridge(c, frame);
		else if (_stricmp(what, "kill") == 0) Kill(c, frame);
		else if (_stricmp(what, "phase") == 0 && c.count > 1) PathProfile::Phase(c.tok[1], frame);
		else if (_stricmp(what, "end") == 0) Finish();
		else Say("unknown command %s", what);
	}

	void Frame(UnsignedInt frame)
	{
		if (!Started || Ended || TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isLoadingMap())
			return;
		if (!FirstFrameDone)
		{
			FirstFrameDone = TRUE;
			for (Int s = 0; s < 8; ++s)
			{
				if (!Slots[s].used || !Slots[s].aiOff)
					continue;
				Player * p = Slot_Player(s + 1);
				if (p != nullptr)
					p->setPlayerType(PLAYER_HUMAN, FALSE);	// "ai off": the player holds; its units still defend themselves
			}
		}
		if (BenchFile[0] != 0 && ScenarioFile[0] == 0)
		{
			if (frame >= 2)
			{
				Run_Bench(frame);
				Finish();
			}
			return;
		}
		for (Int i = 0; i < CommandCount && !Ended; ++i)
		{
			Command & c = Commands[i];
			if (c.done)
				continue;
			if (c.trigger == AT_FRAME)
			{
				if (frame >= c.frame)
					Execute(c, frame);
				continue;
			}
			if (!c.armed)
			{
				Bool met = FALSE;
				if (c.trigger == WHEN_ARRIVED)
					met = PathProfile::Group_Arrived(PathProfile::Group_Index(c.group)) * 100.0f >= c.percent;
				else if (c.trigger == WHEN_NEAR)
					met = PathProfile::Group_Near(PathProfile::Group_Index(c.group), c.x * CELL + CELL / 2.0f, c.y * CELL + CELL / 2.0f, c.radius * CELL);
				else
					for (Int t = 0; t < TagCount && !met; ++t)
						met = strcmp(Tags[t].name, c.after) == 0;
				if (met)
				{
					c.armed = TRUE;
					c.frame = frame + (UnsignedInt)(c.delay > 0 ? c.delay : 0);
				}
			}
			if (c.armed && frame >= c.frame)
				Execute(c, frame);
		}
		if (!Ended && frame >= EndFrame)
			Finish();
	}

	Int Run_Headless()
	{
		if (!Start(TRUE))
			return 1;
		UnsignedInt guard = 0;
		const UnsignedInt limit = EndFrame + 1000;
		while (!Ended && TheGameEngine != nullptr && !TheGameEngine->getQuitting())
		{
			TheGameLogic->UPDATE();
			if (++guard > limit)
			{
				Say("stopped: past end_frame without an end");
				Finish();
				break;
			}
		}
		if (!Ended)
			Finish();
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The bench: findPath for each pair and template, and an exact A* (8 directions, 10 / 14, a diagonal needs one open
	// side, the cell rule validMovementPosition: as the pathfinder's own search, without its turn and pinch costs or
	// other units, and without its footprint clearance costs) for the shortest route the grid allows.
	Int GridW = 0, GridH = 0;
	Int BenchWater = 0;		///< the searching unit's required water level and bridge height (the cell rule's other inputs)
	Short BenchBridge = 0;
	Int * GCost = nullptr;
	UnsignedByte * Closed = nullptr;
	struct HeapItem { Int f; Int g; Int idx; };
	HeapItem * Heap = nullptr;
	Int HeapCap = 0, HeapN = 0;

	void Heap_Push(Int f, Int g, Int idx)
	{
		if (HeapN >= HeapCap)
		{
			const Int cap = HeapCap > 0 ? HeapCap * 2 : 65536;
			HeapItem * h = (HeapItem *)realloc(Heap, sizeof(HeapItem) * cap);
			if (h == nullptr) return;
			Heap = h;
			HeapCap = cap;
		}
		Int i = HeapN++;
		while (i > 0)
		{
			const Int parent = (i - 1) / 2;
			if (Heap[parent].f < f || (Heap[parent].f == f && Heap[parent].g >= g)) break;
			Heap[i] = Heap[parent];
			i = parent;
		}
		Heap[i].f = f; Heap[i].g = g; Heap[i].idx = idx;
	}

	HeapItem Heap_Pop()
	{
		HeapItem top = Heap[0];
		const HeapItem last = Heap[--HeapN];
		Int i = 0;
		for (;;)
		{
			Int c = 2 * i + 1;
			if (c >= HeapN) break;
			if (c + 1 < HeapN && (Heap[c + 1].f < Heap[c].f || (Heap[c + 1].f == Heap[c].f && Heap[c + 1].g > Heap[c].g))) ++c;
			if (last.f < Heap[c].f || (last.f == Heap[c].f && last.g >= Heap[c].g)) break;
			Heap[i] = Heap[c];
			i = c;
		}
		if (HeapN > 0) Heap[i] = last;
		return top;
	}

	Bool Passable(Pathfinder * pf, LocomotorSurfaceTypeMask surfaces, Int x, Int y)
	{
		if (x < 0 || y < 0 || x >= GridW || y >= GridH)
			return FALSE;
		PathfindCell * c = pf->getCell(LAYER_GROUND, x, y);
		return c != nullptr && pf->validMovementPosition(FALSE, surfaces, BenchWater, BenchBridge, c);
	}

	Int Octile(Int dx, Int dy)
	{
		dx = abs(dx); dy = abs(dy);
		return dx > dy ? COST_DIAGONAL * dy + COST_ORTHOGONAL * (dx - dy) : COST_DIAGONAL * dx + COST_ORTHOGONAL * (dy - dx);
	}

	Int Exact(Pathfinder * pf, LocomotorSurfaceTypeMask surfaces, Int sx, Int sy, Int gx, Int gy)
	{
		if (!Passable(pf, surfaces, sx, sy) || !Passable(pf, surfaces, gx, gy))
			return -1;
		const Int cells = GridW * GridH;
		for (Int i = 0; i < cells; ++i) GCost[i] = 0x7FFFFFFF;
		memset(Closed, 0, cells);
		HeapN = 0;
		const Int start = sy * GridW + sx, goal = gy * GridW + gx;
		GCost[start] = 0;
		Heap_Push(Octile(gx - sx, gy - sy), 0, start);
		static const Int dx[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
		static const Int dy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
		static const Int sideA[4] = { 0, 1, 2, 3 }, sideB[4] = { 1, 2, 3, 0 };
		while (HeapN > 0)
		{
			const HeapItem it = Heap_Pop();
			if (Closed[it.idx] || it.g != GCost[it.idx])
				continue;
			if (it.idx == goal)
				return it.g;
			Closed[it.idx] = 1;
			const Int x = it.idx % GridW, y = it.idx / GridW;
			Bool open[4] = { FALSE, FALSE, FALSE, FALSE };
			for (Int d = 0; d < 8; ++d)
			{
				if (d >= 4 && !open[sideA[d - 4]] && !open[sideB[d - 4]])
					continue;	// a diagonal needs one of its two sides open
				const Int nx = x + dx[d], ny = y + dy[d];
				if (!Passable(pf, surfaces, nx, ny))
					continue;
				if (d < 4)
					open[d] = TRUE;
				const Int n = ny * GridW + nx;
				if (Closed[n])
					continue;
				const Int g = it.g + (d < 4 ? COST_ORTHOGONAL : COST_DIAGONAL);
				if (g < GCost[n])
				{
					GCost[n] = g;
					Heap_Push(g + Octile(gx - nx, gy - ny), g, n);
				}
			}
		}
		return -1;
	}

	UnsignedInt Rng = 1;
	UnsignedInt Next()
	{
		Rng ^= Rng << 13; Rng ^= Rng >> 17; Rng ^= Rng << 5;
		return Rng;
	}

	void Run_Bench(UnsignedInt frame)
	{
		Pathfinder * pf = TheAI != nullptr ? TheAI->pathfinder() : nullptr;
		Player * owner = Slot_Player(1);
		if (pf == nullptr || owner == nullptr || TemplateCount == 0)
		{
			Say("bench: no pathfinder, player 1 or template");
			return;
		}
		Region3D extent;
		TheTerrainLogic->getExtent(&extent);
		GridW = REAL_TO_INT_FLOOR(extent.hi.x / CELL);
		GridH = REAL_TO_INT_FLOOR(extent.hi.y / CELL);
		if (GridW <= 0 || GridH <= 0)
			return;
		GCost = (Int *)malloc(sizeof(Int) * GridW * GridH);
		Closed = (UnsignedByte *)malloc(GridW * GridH);
		if (GCost == nullptr || Closed == nullptr)
			return;
		// one unit per template, moved to each pair's start
		Object * units[MAX_TEMPLATES] = {};
		for (Int t = 0; t < TemplateCount; ++t)
		{
			const ThingTemplate * tmpl = TheThingFactory->findTemplate(AsciiString(Templates[t]));
			if (tmpl == nullptr)
			{
				Say("bench: unknown template %s", Templates[t]);
				continue;
			}
			units[t] = Spawn_One(tmpl, owner, (Real)GridW * CELL / 2.0f, (Real)GridH * CELL / 2.0f, 0.0f);
		}
		// random pairs, passable for the first template, spread over the distance bands
		if (RandomPairs > 0 && units[0] != nullptr && units[0]->getAI() != nullptr)
		{
			const LocomotorSurfaceTypeMask surfaces = units[0]->getAI()->getLocomotorSet().getValidSurfaces();
			BenchWater = units[0]->getAI()->getLocomotorSet().getRequiredWaterLevel();
			BenchBridge = units[0]->getRequiredBridgeHeight();
			Rng = (UnsignedInt)Seed * 2654435761u + 1u;
			const Int bands = BandCount > 1 ? BandCount - 1 : 1;
			for (Int k = 0; k < RandomPairs && PairCount < MAX_PAIRS; ++k)
			{
				const Int b = k % bands;
				const Real lo = (Real)Bands[b], hi = (Real)(BandCount > 1 ? Bands[b + 1] : 100000);
				for (Int attempt = 0; attempt < 400; ++attempt)
				{
					const Int sx = (Int)(Next() % (UnsignedInt)GridW), sy = (Int)(Next() % (UnsignedInt)GridH);
					if (!Passable(pf, surfaces, sx, sy))
						continue;
					const Real a = (Real)(Next() % 36000) / 36000.0f * 2.0f * PI;
					const Real span = (hi > (Real)(GridW + GridH) ? (Real)(GridW + GridH) : hi) - lo;
					const Real d = lo + (span > 0.0f ? (Real)(Next() % 10000) / 10000.0f * span : 0.0f);
					const Int gx = sx + REAL_TO_INT(Cos(a) * d), gy = sy + REAL_TO_INT(Sin(a) * d);
					if (!Passable(pf, surfaces, gx, gy))
						continue;
					Pair & p = Pairs[PairCount++];
					p.sx = sx; p.sy = sy; p.gx = gx; p.gy = gy;
					break;
				}
			}
		}
		char path[600];
		snprintf(path, sizeof(path), "%s.bench.csv", PathProfile::Output_Base());
		FILE * out = fopen(path, "wt");
		if (out == nullptr)
			return;
		fprintf(out, "map,template,footprint,pair,sx,sy,gx,gy,straight_cells,result,cells,ms,route_cells,nodes,optimal_cells,ratio\n");
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		for (Int t = 0; t < TemplateCount; ++t)
		{
			Object * unit = units[t];
			if (unit == nullptr || unit->getAI() == nullptr)
				continue;
			const LocomotorSurfaceTypeMask surfaces = unit->getAI()->getLocomotorSet().getValidSurfaces();
			BenchWater = unit->getAI()->getLocomotorSet().getRequiredWaterLevel();
			BenchBridge = unit->getRequiredBridgeHeight();
			Int footN = 1;
			{
				Real diameter = 2.0f * unit->getGeometryInfo().getBoundingCircleRadius();
				if (diameter > CELL && diameter < 2.0f * CELL) diameter = 2.0f * CELL;
				footN = REAL_TO_INT_FLOOR(diameter / CELL + 0.3f);
				if (footN < 1) footN = 1;
				if (footN > 5) footN = 5;
			}
			for (Int i = 0; i < PairCount; ++i)
			{
				const Pair & p = Pairs[i];
				Coord3D from, to;
				from.x = p.sx * CELL + CELL / 2.0f; from.y = p.sy * CELL + CELL / 2.0f; from.z = TheTerrainLogic->getGroundHeight(from.x, from.y);
				to.x = p.gx * CELL + CELL / 2.0f; to.y = p.gy * CELL + CELL / 2.0f; to.z = TheTerrainLogic->getGroundHeight(to.x, to.y);
				unit->setPosition(&from);
				Int cells = 0;
				LARGE_INTEGER t0, t1;
				QueryPerformanceCounter(&t0);
				Path * route = pf->benchFindPath(unit, &from, &to, &cells);
				QueryPerformanceCounter(&t1);
				const PathProfile::Result result = PathProfile::Search::Classify(route, &to, FALSE);
				Real length = 0.0f;
				Int nodes = 0;
				if (route != nullptr)
				{
					const PathNode * prev = nullptr;
					for (const PathNode * n = route->getFirstNode(); n != nullptr; n = n->getNext())
					{
						if (prev != nullptr)
						{
							const Real dx = n->getPosition()->x - prev->getPosition()->x, dy = n->getPosition()->y - prev->getPosition()->y;
							length += sqrtf(dx * dx + dy * dy);
						}
						prev = n;
						++nodes;
					}
					deleteInstance(route);
				}
				const Int optimal = Exact(pf, surfaces, p.sx, p.sy, p.gx, p.gy);
				const Real optimalCells = optimal >= 0 ? (Real)optimal / (Real)COST_ORTHOGONAL : -1.0f;
				const Real straight = sqrtf((Real)((p.gx - p.sx) * (p.gx - p.sx) + (p.gy - p.sy) * (p.gy - p.sy)));
				static const char * const results[] = { "found", "partial", "failed", "out_of_pool" };
				fprintf(out, "\"%s\",%s,%d,%d,%d,%d,%d,%d,%.1f,%s,%d,%.4f,%.2f,%d,%.2f,%.4f\n", MapName, Templates[t], footN, i, p.sx, p.sy,
					p.gx, p.gy, straight, results[result], cells,
					(double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart, length / CELL, nodes, optimalCells,
					optimalCells > 0.0f && route != nullptr ? (length / CELL) / optimalCells : -1.0f);
			}
		}
		fclose(out);
		free(GCost); GCost = nullptr;
		free(Closed); Closed = nullptr;
		free(Heap); Heap = nullptr; HeapCap = HeapN = 0;
		Say("bench written: %s", path);
	}
}
