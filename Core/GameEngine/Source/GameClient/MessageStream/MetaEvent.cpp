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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: MetaEvent.cpp ////////////////////////////////////////////////////////////////////////////
// Created:    Colin Day, September 2001
// Desc:       Translating keystrokes into event command messages
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameUtility.h"
#include "Common/INI.h"
#include "Common/FramePacer.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"

#include "GameClient/Drawable.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ParticleSys.h"	// for ParticleSystemDebugDisplay
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/DebugDisplay.h"	// for AudioDebugDisplay
#include "GameClient/GameText.h"
#include "GameClient/MetaEvent.h"

#include "GameLogic/GameLogic.h" // for TheGameLogic->getFrame()


#define dont_DUMP_ALL_KEYS_TO_LOG


#ifdef DUMP_ALL_KEYS_TO_LOG
#include "GameClient/Keyboard.h"
#endif

// TheSuperHackers @feature for grid hotkeys yielding to command bar buttons
#include "Common/GlobalData.h"
// TheSuperHackers @feature for the grid hotkey exclusion list
#include "Common/OptionPreferences.h"
#include "GameClient/HotKey.h"
#include "GameClient/Keyboard.h"

MetaMap *TheMetaMap = nullptr;



// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

static const LookupListRec GameMessageMetaTypeNames[] =
{
	{ "SAVE_VIEW1",																GameMessage::MSG_META_SAVE_VIEW1 },
	{ "SAVE_VIEW2",																GameMessage::MSG_META_SAVE_VIEW2 },
	{ "SAVE_VIEW3",																GameMessage::MSG_META_SAVE_VIEW3 },
	{ "SAVE_VIEW4",																GameMessage::MSG_META_SAVE_VIEW4 },
	{ "SAVE_VIEW5",																GameMessage::MSG_META_SAVE_VIEW5 },
	{ "SAVE_VIEW6",																GameMessage::MSG_META_SAVE_VIEW6 },
	{ "SAVE_VIEW7",																GameMessage::MSG_META_SAVE_VIEW7 },
	{ "SAVE_VIEW8",																GameMessage::MSG_META_SAVE_VIEW8 },
	{ "VIEW_VIEW1",																GameMessage::MSG_META_VIEW_VIEW1 },
	{ "VIEW_VIEW2",																GameMessage::MSG_META_VIEW_VIEW2 },
	{ "VIEW_VIEW3",																GameMessage::MSG_META_VIEW_VIEW3 },
	{ "VIEW_VIEW4",																GameMessage::MSG_META_VIEW_VIEW4 },
	{ "VIEW_VIEW5",																GameMessage::MSG_META_VIEW_VIEW5 },
	{ "VIEW_VIEW6",																GameMessage::MSG_META_VIEW_VIEW6 },
	{ "VIEW_VIEW7",																GameMessage::MSG_META_VIEW_VIEW7 },
	{ "VIEW_VIEW8",																GameMessage::MSG_META_VIEW_VIEW8 },
	{ "CREATE_TEAM0",															GameMessage::MSG_META_CREATE_TEAM0 },
	{ "CREATE_TEAM1",															GameMessage::MSG_META_CREATE_TEAM1 },
	{ "CREATE_TEAM2",															GameMessage::MSG_META_CREATE_TEAM2 },
	{ "CREATE_TEAM3",															GameMessage::MSG_META_CREATE_TEAM3 },
	{ "CREATE_TEAM4",															GameMessage::MSG_META_CREATE_TEAM4 },
	{ "CREATE_TEAM5",															GameMessage::MSG_META_CREATE_TEAM5 },
	{ "CREATE_TEAM6",															GameMessage::MSG_META_CREATE_TEAM6 },
	{ "CREATE_TEAM7",															GameMessage::MSG_META_CREATE_TEAM7 },
	{ "CREATE_TEAM8",															GameMessage::MSG_META_CREATE_TEAM8 },
	{ "CREATE_TEAM9",															GameMessage::MSG_META_CREATE_TEAM9 },
	{ "SELECT_TEAM0",															GameMessage::MSG_META_SELECT_TEAM0 },
	{ "SELECT_TEAM1",															GameMessage::MSG_META_SELECT_TEAM1 },
	{ "SELECT_TEAM2",															GameMessage::MSG_META_SELECT_TEAM2 },
	{ "SELECT_TEAM3",															GameMessage::MSG_META_SELECT_TEAM3 },
	{ "SELECT_TEAM4",															GameMessage::MSG_META_SELECT_TEAM4 },
	{ "SELECT_TEAM5",															GameMessage::MSG_META_SELECT_TEAM5 },
	{ "SELECT_TEAM6",															GameMessage::MSG_META_SELECT_TEAM6 },
	{ "SELECT_TEAM7",															GameMessage::MSG_META_SELECT_TEAM7 },
	{ "SELECT_TEAM8",															GameMessage::MSG_META_SELECT_TEAM8 },
	{ "SELECT_TEAM9",															GameMessage::MSG_META_SELECT_TEAM9 },
	{ "ADD_TEAM0",																GameMessage::MSG_META_ADD_TEAM0 },
	{ "ADD_TEAM1",																GameMessage::MSG_META_ADD_TEAM1 },
	{ "ADD_TEAM2",																GameMessage::MSG_META_ADD_TEAM2 },
	{ "ADD_TEAM3",																GameMessage::MSG_META_ADD_TEAM3 },
	{ "ADD_TEAM4",																GameMessage::MSG_META_ADD_TEAM4 },
	{ "ADD_TEAM5",																GameMessage::MSG_META_ADD_TEAM5 },
	{ "ADD_TEAM6",																GameMessage::MSG_META_ADD_TEAM6 },
	{ "ADD_TEAM7",																GameMessage::MSG_META_ADD_TEAM7 },
	{ "ADD_TEAM8",																GameMessage::MSG_META_ADD_TEAM8 },
	{ "ADD_TEAM9",																GameMessage::MSG_META_ADD_TEAM9 },
	{ "VIEW_TEAM0",																GameMessage::MSG_META_VIEW_TEAM0 },
	{ "VIEW_TEAM1",																GameMessage::MSG_META_VIEW_TEAM1 },
	{ "VIEW_TEAM2",																GameMessage::MSG_META_VIEW_TEAM2 },
	{ "VIEW_TEAM3",																GameMessage::MSG_META_VIEW_TEAM3 },
	{ "VIEW_TEAM4",																GameMessage::MSG_META_VIEW_TEAM4 },
	{ "VIEW_TEAM5",																GameMessage::MSG_META_VIEW_TEAM5 },
	{ "VIEW_TEAM6",																GameMessage::MSG_META_VIEW_TEAM6 },
	{ "VIEW_TEAM7",																GameMessage::MSG_META_VIEW_TEAM7 },
	{ "VIEW_TEAM8",																GameMessage::MSG_META_VIEW_TEAM8 },
	{ "VIEW_TEAM9",																GameMessage::MSG_META_VIEW_TEAM9 },
	{ "AUTO_CREATE_TEAMS",												GameMessage::MSG_META_AUTO_CREATE_TEAMS },
	{ "SELECT_MATCHING_UNITS",										GameMessage::MSG_META_SELECT_MATCHING_UNITS },
	{ "SMART_SELECTION_NEXT_TYPE",								GameMessage::MSG_META_SMART_SELECTION_NEXT_TYPE },
	{ "SMART_SELECTION_PREV_TYPE",								GameMessage::MSG_META_SMART_SELECTION_PREV_TYPE },
	{ "SELECT_NEXT_UNIT",													GameMessage::MSG_META_SELECT_NEXT_UNIT },
	{ "SELECT_PREV_UNIT",													GameMessage::MSG_META_SELECT_PREV_UNIT },
	{ "SELECT_NEXT_WORKER",												GameMessage::MSG_META_SELECT_NEXT_WORKER },
	{ "SELECT_PREV_WORKER",												GameMessage::MSG_META_SELECT_PREV_WORKER },
	{ "SELECT_NEXT_IDLE_WORKER",											    GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER },
	{ "SELECT_HERO",												      GameMessage::MSG_META_SELECT_HERO },
	{ "SELECT_ALL",																GameMessage::MSG_META_SELECT_ALL },
	{ "SELECT_ALL_AIRCRAFT",											GameMessage::MSG_META_SELECT_ALL_AIRCRAFT },
	{ "VIEW_COMMAND_CENTER",											GameMessage::MSG_META_VIEW_COMMAND_CENTER },
	{ "VIEW_LAST_RADAR_EVENT",										GameMessage::MSG_META_VIEW_LAST_RADAR_EVENT },
	{ "SCATTER",																	GameMessage::MSG_META_SCATTER },
	{ "STOP",																			GameMessage::MSG_META_STOP },
	{ "DEPLOY",																		GameMessage::MSG_META_DEPLOY },
	{ "CREATE_FORMATION",													GameMessage::MSG_META_CREATE_FORMATION },
	{ "FOLLOW",																		GameMessage::MSG_META_FOLLOW },
	{ "CHAT_PLAYERS",															GameMessage::MSG_META_CHAT_PLAYERS },
	{ "CHAT_ALLIES",															GameMessage::MSG_META_CHAT_ALLIES },
	{ "CHAT_EVERYONE",														GameMessage::MSG_META_CHAT_EVERYONE },
	{ "DIPLOMACY",																GameMessage::MSG_META_DIPLOMACY },
	{ "PLACE_BEACON",															GameMessage::MSG_META_PLACE_BEACON },
	{ "DELETE_BEACON",														GameMessage::MSG_META_REMOVE_BEACON },
	{ "OPTIONS",																	GameMessage::MSG_META_OPTIONS },
	{ "INCREASE_MAX_RENDER_FPS",									GameMessage::MSG_META_INCREASE_MAX_RENDER_FPS },
	{ "DECREASE_MAX_RENDER_FPS",									GameMessage::MSG_META_DECREASE_MAX_RENDER_FPS },
	{ "INCREASE_LOGIC_TIME_SCALE",								GameMessage::MSG_META_INCREASE_LOGIC_TIME_SCALE },
	{ "DECREASE_LOGIC_TIME_SCALE",								GameMessage::MSG_META_DECREASE_LOGIC_TIME_SCALE },
	{ "TOGGLE_LOWER_DETAILS",											GameMessage::MSG_META_TOGGLE_LOWER_DETAILS },
	{ "TOGGLE_CONTROL_BAR",												GameMessage::MSG_META_TOGGLE_CONTROL_BAR },
	{ "TOGGLE_PLAYER_OBSERVER",										GameMessage::MSG_META_TOGGLE_PLAYER_OBSERVER },
	{ "CYCLE_HEALTH_BAR_MODE",										GameMessage::MSG_META_CYCLE_HEALTH_BAR_MODE },
	{ "CYCLE_HOUSE_COLOR_MODE",										GameMessage::MSG_META_CYCLE_HOUSE_COLOR_MODE },
	{ "TOGGLE_REVERSEMOVE",												GameMessage::MSG_META_TOGGLE_REVERSEMOVE },
	{ "BEGIN_PATH_BUILD",													GameMessage::MSG_META_BEGIN_PATH_BUILD },
	{ "END_PATH_BUILD",														GameMessage::MSG_META_END_PATH_BUILD },
#if RTS_ZEROHOUR
	{ "BEGIN_FACING_DRAG", GameMessage::MSG_META_BEGIN_FACING_DRAG },
	{ "END_FACING_DRAG", GameMessage::MSG_META_END_FACING_DRAG },
	{ "SPECIAL_POWER_1", GameMessage::MSG_META_SPECIAL_POWER_1 },
	{ "SPECIAL_POWER_2", GameMessage::MSG_META_SPECIAL_POWER_2 },
	{ "SPECIAL_POWER_3", GameMessage::MSG_META_SPECIAL_POWER_3 },
	{ "SPECIAL_POWER_4", GameMessage::MSG_META_SPECIAL_POWER_4 },
	{ "SPECIAL_POWER_5", GameMessage::MSG_META_SPECIAL_POWER_5 },
	{ "SPECIAL_POWER_6", GameMessage::MSG_META_SPECIAL_POWER_6 },
	{ "SPECIAL_POWER_7", GameMessage::MSG_META_SPECIAL_POWER_7 },
	{ "SPECIAL_POWER_8", GameMessage::MSG_META_SPECIAL_POWER_8 },
	{ "SPECIAL_POWER_9", GameMessage::MSG_META_SPECIAL_POWER_9 },
	{ "SPECIAL_POWER_10", GameMessage::MSG_META_SPECIAL_POWER_10 },
	{ "SPECIAL_POWER_11", GameMessage::MSG_META_SPECIAL_POWER_11 },
	{ "SPECIAL_POWER_12", GameMessage::MSG_META_SPECIAL_POWER_12 },
	{ "SPECIAL_POWER_13", GameMessage::MSG_META_SPECIAL_POWER_13 },
	{ "SPECIAL_POWER_14", GameMessage::MSG_META_SPECIAL_POWER_14 },
	{ "SPECIAL_POWER_15", GameMessage::MSG_META_SPECIAL_POWER_15 },
	{ "SPECIAL_POWER_16", GameMessage::MSG_META_SPECIAL_POWER_16 },
#endif
#if defined(GENERALS_ONLINE)
	{ "TOGGLE_NETWORK_STATS",											GameMessage::MSG_META_TOGGLE_NETWORK_STATS },
	{ "TAKE_SCREENSHOT_ALT",											GameMessage::MSG_META_TAKE_SCREENSHOT_ALT },
	{ "TOGGLE_BUDDY_OVERLAY",											GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY },
	{ "TOGGLE_BUDDY_OVERLAY_ALT",									GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY_ALT },
#endif
	{ "BEGIN_FORCEATTACK",												GameMessage::MSG_META_BEGIN_FORCEATTACK },
	{ "END_FORCEATTACK",													GameMessage::MSG_META_END_FORCEATTACK },
	{ "BEGIN_FORCEMOVE",													GameMessage::MSG_META_BEGIN_FORCEMOVE },
	{ "END_FORCEMOVE",														GameMessage::MSG_META_END_FORCEMOVE },
	{ "BEGIN_WAYPOINTS",													GameMessage::MSG_META_BEGIN_WAYPOINTS },
	{ "END_WAYPOINTS",														GameMessage::MSG_META_END_WAYPOINTS },
	{ "BEGIN_PREFER_SELECTION",										GameMessage::MSG_META_BEGIN_PREFER_SELECTION },
	{ "END_PREFER_SELECTION",											GameMessage::MSG_META_END_PREFER_SELECTION },

	{ "TAKE_SCREENSHOT",													GameMessage::MSG_META_TAKE_SCREENSHOT },
	{ "TAKE_SCREENSHOT_PNG",											GameMessage::MSG_META_TAKE_SCREENSHOT_PNG },
	{ "ALL_CHEER",																GameMessage::MSG_META_ALL_CHEER },

	{ "BEGIN_CAMERA_ROTATE_LEFT",									GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_LEFT },
	{ "END_CAMERA_ROTATE_LEFT",										GameMessage::MSG_META_END_CAMERA_ROTATE_LEFT },
	{ "BEGIN_CAMERA_ROTATE_RIGHT",								GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_RIGHT },
	{ "END_CAMERA_ROTATE_RIGHT",									GameMessage::MSG_META_END_CAMERA_ROTATE_RIGHT },
	{ "BEGIN_CAMERA_ZOOM_IN",											GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_IN },
	{ "END_CAMERA_ZOOM_IN",												GameMessage::MSG_META_END_CAMERA_ZOOM_IN },
	{ "BEGIN_CAMERA_ZOOM_OUT",										GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_OUT },
	{ "END_CAMERA_ZOOM_OUT",											GameMessage::MSG_META_END_CAMERA_ZOOM_OUT },
	{ "CAMERA_RESET",															GameMessage::MSG_META_CAMERA_RESET },
	{ "TOGGLE_CAMERA_TRACKING_DRAWABLE",					GameMessage::MSG_META_TOGGLE_CAMERA_TRACKING_DRAWABLE },
	{ "TOGGLE_FAST_FORWARD_REPLAY",								GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY },
	{ "TOGGLE_PAUSE",															GameMessage::MSG_META_TOGGLE_PAUSE },
	{ "TOGGLE_PAUSE_ALT",													GameMessage::MSG_META_TOGGLE_PAUSE_ALT },
	{ "STEP_FRAME",																GameMessage::MSG_META_STEP_FRAME },
	{ "STEP_FRAME_ALT",														GameMessage::MSG_META_STEP_FRAME_ALT },
	{ "DEMO_INSTANT_QUIT",												GameMessage::MSG_META_DEMO_INSTANT_QUIT },

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)//may be defined in GameCommon.h
	{ "CHEAT_RUNSCRIPT1",								        	GameMessage::MSG_CHEAT_RUNSCRIPT1 },
	{ "CHEAT_RUNSCRIPT2",								        	GameMessage::MSG_CHEAT_RUNSCRIPT2 },
	{ "CHEAT_RUNSCRIPT3",								        	GameMessage::MSG_CHEAT_RUNSCRIPT3 },
	{ "CHEAT_RUNSCRIPT4",								        	GameMessage::MSG_CHEAT_RUNSCRIPT4 },
	{ "CHEAT_RUNSCRIPT5",								        	GameMessage::MSG_CHEAT_RUNSCRIPT5 },
	{ "CHEAT_RUNSCRIPT6",								        	GameMessage::MSG_CHEAT_RUNSCRIPT6 },
	{ "CHEAT_RUNSCRIPT7",								        	GameMessage::MSG_CHEAT_RUNSCRIPT7 },
	{ "CHEAT_RUNSCRIPT8",								        	GameMessage::MSG_CHEAT_RUNSCRIPT8 },
	{ "CHEAT_RUNSCRIPT9",								        	GameMessage::MSG_CHEAT_RUNSCRIPT9 },
	{ "CHEAT_TOGGLE_SPECIAL_POWER_DELAYS",	      GameMessage::MSG_CHEAT_TOGGLE_SPECIAL_POWER_DELAYS },
  { "CHEAT_SWITCH_TEAMS",							        	GameMessage::MSG_CHEAT_SWITCH_TEAMS },
	{ "CHEAT_KILL_SELECTION",						        	GameMessage::MSG_CHEAT_KILL_SELECTION },
	{ "CHEAT_TOGGLE_HAND_OF_GOD_MODE",		        GameMessage::MSG_CHEAT_TOGGLE_HAND_OF_GOD_MODE },
	{ "CHEAT_INSTANT_BUILD",							        GameMessage::MSG_CHEAT_INSTANT_BUILD },
	{ "CHEAT_DESHROUD",									          GameMessage::MSG_CHEAT_DESHROUD },
		// TheSuperHackers @feature Default binding for the combined cheat. Shift+Ctrl+` is free in
		// the engine, in the demo command map and in Contra's own, and it does not collide with
		// the Ctrl+` health bar cycle because the modifier sets differ. Only exists in builds
		// where cheats are compiled in.
	{ "CHEAT_KITCHEN_SINK",									          GameMessage::MSG_CHEAT_KITCHEN_SINK },
	{ "CHEAT_TOGGLE_ZOOM_LOCK",									          GameMessage::MSG_CHEAT_TOGGLE_ZOOM_LOCK },
	{ "CHEAT_SHOW_OBJECT_NAME",									          GameMessage::MSG_CHEAT_SHOW_OBJECT_NAME },
	{ "CHEAT_SHOW_PARTICLE_NAMES",								  GameMessage::MSG_CHEAT_SHOW_PARTICLE_NAMES },
	{ "CHEAT_SHOW_COMMAND_SET",									  GameMessage::MSG_CHEAT_SHOW_COMMAND_SET },
	{ "CHEAT_SHOW_WEAPON_SET",									  GameMessage::MSG_CHEAT_SHOW_WEAPON_SET },
	{ "CHEAT_SHOW_ARMOR_SET",									  GameMessage::MSG_CHEAT_SHOW_ARMOR_SET },
	{ "CHEAT_SHOW_LASER_NAME",									  GameMessage::MSG_CHEAT_SHOW_LASER_NAME },
	{ "CHEAT_SHOW_LASER_BEAM_BLOCK",							  GameMessage::MSG_CHEAT_SHOW_LASER_BEAM_BLOCK },
	{ "CHEAT_CYCLE_CAMERA_MODE",							  GameMessage::MSG_CHEAT_CYCLE_CAMERA_MODE },
	{ "CHEAT_CYCLE_SKYBOX",									  GameMessage::MSG_CHEAT_CYCLE_SKYBOX },
	{ "CHEAT_CYCLE_TERRAIN_MODE",							  GameMessage::MSG_CHEAT_CYCLE_TERRAIN_MODE },
	{ "CHEAT_TOGGLE_FACE_CULLING",							  GameMessage::MSG_CHEAT_TOGGLE_FACE_CULLING },
	{ "CHEAT_SHOW_MODEL_NAMES",									  GameMessage::MSG_CHEAT_SHOW_MODEL_NAMES },
	{ "CHEAT_SHOW_WINDOW_NAMES",								  GameMessage::MSG_CHEAT_SHOW_WINDOW_NAMES },
	{ "CHEAT_SHOW_SHADER_DRAWS",								  GameMessage::MSG_CHEAT_SHOW_SHADER_DRAWS },
#if RTS_ZEROHOUR
	{ "CHEAT_TOGGLE_SHADER_PANEL", GameMessage::MSG_CHEAT_TOGGLE_SHADER_PANEL },
#endif
	{ "CHEAT_COPY_OBJECT_NAME",								  GameMessage::MSG_CHEAT_COPY_OBJECT_NAME },
	{ "CHEAT_PATHFIND_LOG_ON",									  GameMessage::MSG_CHEAT_PATHFIND_LOG_ON },
	{ "CHEAT_PATHFIND_LOG_OFF",									  GameMessage::MSG_CHEAT_PATHFIND_LOG_OFF },
	{ "CHEAT_ADD_CASH",									          GameMessage::MSG_CHEAT_ADD_CASH },
	{ "CHEAT_GIVE_ALL_SCIENCES",					        GameMessage::MSG_CHEAT_GIVE_ALL_SCIENCES },
  { "CHEAT_GIVE_SCIENCEPURCHASEPOINTS",        	GameMessage::MSG_CHEAT_GIVE_SCIENCEPURCHASEPOINTS },
  { "CHEAT_SHOW_HEALTH",                        GameMessage::MSG_CHEAT_SHOW_HEALTH },
  { "CHEAT_TOGGLE_MESSAGE_TEXT",                GameMessage::MSG_CHEAT_TOGGLE_MESSAGE_TEXT },
	// TheSuperHackers @feature Bindable in cheat builds too, not just debug ones.
	{ "DEMO_TOGGLE_RENDER",												GameMessage::MSG_META_DEMO_TOGGLE_RENDER },

#endif

#if defined(RTS_DEBUG)
	{ "HELP",																			GameMessage::MSG_META_HELP },

	{ "DEMO_TOGGLE_BEHIND_BUILDINGS",							GameMessage::MSG_META_DEMO_TOGGLE_BEHIND_BUILDINGS },
	{ "DEMO_LOD_DECREASE",												GameMessage::MSG_META_DEMO_LOD_DECREASE },
	{ "DEMO_LOD_INCREASE",												GameMessage::MSG_META_DEMO_LOD_INCREASE },
	{ "DEMO_TOGGLE_LETTERBOX",										GameMessage::MSG_META_DEMO_TOGGLE_LETTERBOX },
	{ "DEMO_TOGGLE_MESSAGE_TEXT",									GameMessage::MSG_META_DEMO_TOGGLE_MESSAGE_TEXT },

	{ "DEMO_GIVE_ALL_SCIENCES",										GameMessage::MSG_META_DEMO_GIVE_ALL_SCIENCES },
	{ "DEMO_GIVE_RANKLEVEL",											GameMessage::MSG_META_DEMO_GIVE_RANKLEVEL },
	{ "DEMO_TAKE_RANKLEVEL",											GameMessage::MSG_META_DEMO_TAKE_RANKLEVEL },
	{ "DEMO_GIVE_SCIENCEPURCHASEPOINTS",					GameMessage::MSG_META_DEMO_GIVE_SCIENCEPURCHASEPOINTS },
	{ "DEMO_SWITCH_TEAMS",												GameMessage::MSG_META_DEMO_SWITCH_TEAMS },
	{ "DEMO_SWITCH_TEAMS_CHINA_USA",							GameMessage::MSG_META_DEMO_SWITCH_TEAMS_BETWEEN_CHINA_USA },
	{ "DEMO_TOGGLE_CASHMAPDEBUG",									GameMessage::MSG_META_DEMO_TOGGLE_CASHMAPDEBUG },
	{ "DEMO_TOGGLE_GRAPHICALFRAMERATEBAR",				GameMessage::MSG_META_DEMO_TOGGLE_GRAPHICALFRAMERATEBAR },
	{ "DEMO_TOGGLE_PARTICLEDEBUG",								GameMessage::MSG_META_DEMO_TOGGLE_PARTICLEDEBUG },
	{ "DEMO_TOGGLE_THREATDEBUG",									GameMessage::MSG_META_DEMO_TOGGLE_THREATDEBUG },
	{ "DEMO_TOGGLE_VISIONDEBUG",									GameMessage::MSG_META_DEMO_TOGGLE_VISIONDEBUG },
	{ "DEMO_TOGGLE_PROJECTILEDEBUG",							GameMessage::MSG_META_DEMO_TOGGLE_PROJECTILEDEBUG },
	{ "DEMO_LOD_DECREASE",												GameMessage::MSG_META_DEMO_LOD_DECREASE },
	{ "DEMO_LOD_INCREASE",												GameMessage::MSG_META_DEMO_LOD_INCREASE },
	{ "DEMO_TOGGLE_SHADOW_VOLUMES",								GameMessage::MSG_META_DEMO_TOGGLE_SHADOW_VOLUMES },
	{ "DEMO_TOGGLE_FOGOFWAR",											GameMessage::MSG_META_DEMO_TOGGLE_FOGOFWAR },
	{ "DEMO_KILL_ALL_ENEMIES",										GameMessage::MSG_META_DEMO_KILL_ALL_ENEMIES },
	{ "DEMO_KILL_SELECTION",											GameMessage::MSG_META_DEMO_KILL_SELECTION },
	{ "DEMO_TOGGLE_HURT_ME_MODE",									GameMessage::MSG_META_DEMO_TOGGLE_HURT_ME_MODE },
	{ "DEMO_TOGGLE_HAND_OF_GOD_MODE",							GameMessage::MSG_META_DEMO_TOGGLE_HAND_OF_GOD_MODE },
	{ "DEMO_DEBUG_SELECTION",											GameMessage::MSG_META_DEMO_DEBUG_SELECTION },
	{ "DEMO_LOCK_CAMERA_TO_SELECTION",						GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_SELECTION },
	{ "DEMO_TOGGLE_SOUND",												GameMessage::MSG_META_DEMO_TOGGLE_SOUND },
	{ "DEMO_TOGGLE_TRACKMARKS",										GameMessage::MSG_META_DEMO_TOGGLE_TRACKMARKS },
	{ "DEMO_TOGGLE_WATERPLANE",										GameMessage::MSG_META_DEMO_TOGGLE_WATERPLANE },
	{ "DEMO_TIME_OF_DAY",													GameMessage::MSG_META_DEMO_TIME_OF_DAY },
	{ "DEMO_TOGGLE_MILITARY_SUBTITLES",						GameMessage::MSG_META_DEMO_TOGGLE_MILITARY_SUBTITLES },
	{ "DEMO_TOGGLE_MUSIC",												GameMessage::MSG_META_DEMO_TOGGLE_MUSIC },
	{ "DEMO_MUSIC_NEXT_TRACK",										GameMessage::MSG_META_DEMO_MUSIC_NEXT_TRACK },
	{ "DEMO_MUSIC_PREV_TRACK",										GameMessage::MSG_META_DEMO_MUSIC_PREV_TRACK },
	{ "DEMO_NEXT_OBJECTIVE_MOVIE",								GameMessage::MSG_META_DEMO_NEXT_OBJECTIVE_MOVIE },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE1",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE1 },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE2",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE2 },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE3",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE3 },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE4",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE4 },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE5",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE5 },
	{ "DEMO_PLAY_OBJECTIVE_MOVIE6",								GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE6 },
	{ "DEMO_BEGIN_ADJUST_PITCH",									GameMessage::MSG_META_DEMO_BEGIN_ADJUST_PITCH },
	{ "DEMO_END_ADJUST_PITCH",										GameMessage::MSG_META_DEMO_END_ADJUST_PITCH },
	{ "DEMO_BEGIN_ADJUST_DEFAULTPITCH",						GameMessage::MSG_META_DEMO_BEGIN_ADJUST_DEFAULTPITCH },
	{ "DEMO_END_ADJUST_DEFAULTPITCH",							GameMessage::MSG_META_DEMO_END_ADJUST_DEFAULTPITCH },
	{ "DEMO_BEGIN_ADJUST_FOV",										GameMessage::MSG_META_DEMO_BEGIN_ADJUST_FOV },
	{ "DEMO_END_ADJUST_FOV",											GameMessage::MSG_META_DEMO_END_ADJUST_FOV },
	{ "DEMO_LOCK_CAMERA_TO_PLANES",								GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_PLANES },
	{ "DEMO_REMOVE_PREREQ",												GameMessage::MSG_META_DEMO_REMOVE_PREREQ },
	{ "DEMO_RUNSCRIPT1",													GameMessage::MSG_META_DEMO_RUNSCRIPT1 },
	{ "DEMO_RUNSCRIPT2",													GameMessage::MSG_META_DEMO_RUNSCRIPT2 },
	{ "DEMO_RUNSCRIPT3",													GameMessage::MSG_META_DEMO_RUNSCRIPT3 },
	{ "DEMO_RUNSCRIPT4",													GameMessage::MSG_META_DEMO_RUNSCRIPT4 },
	{ "DEMO_RUNSCRIPT5",													GameMessage::MSG_META_DEMO_RUNSCRIPT5 },
	{ "DEMO_RUNSCRIPT6",													GameMessage::MSG_META_DEMO_RUNSCRIPT6 },
	{ "DEMO_RUNSCRIPT7",													GameMessage::MSG_META_DEMO_RUNSCRIPT7 },
	{ "DEMO_RUNSCRIPT8",													GameMessage::MSG_META_DEMO_RUNSCRIPT8 },
	{ "DEMO_RUNSCRIPT9",													GameMessage::MSG_META_DEMO_RUNSCRIPT9 },
	{ "DEMO_ADDCASH",															GameMessage::MSG_META_DEMO_ADD_CASH },
	{ "DEMO_TOGGLE_BW_VIEW",											GameMessage::MSG_META_DEMO_TOGGLE_BW_VIEW },
	{ "DEMO_TOGGLE_RED_VIEW",											GameMessage::MSG_META_DEMO_TOGGLE_RED_VIEW },
	{ "DEMO_TOGGLE_GREEN_VIEW",										GameMessage::MSG_META_DEMO_TOGGLE_GREEN_VIEW },
	{ "DEMO_TOGGLE_MOTION_BLUR_ZOOM",							GameMessage::MSG_META_DEMO_TOGGLE_MOTION_BLUR_ZOOM },
	{ "DEMO_SHOW_EXTENTS",												GameMessage::MSG_META_DEBUG_SHOW_EXTENTS },
  { "DEMO_SHOW_AUDIO_LOCATIONS",								GameMessage::MSG_META_DEBUG_SHOW_AUDIO_LOCATIONS },
  { "DEMO_SHOW_HEALTH",													GameMessage::MSG_META_DEBUG_SHOW_HEALTH },
	{ "DEMO_GIVE_VETERANCY",											GameMessage::MSG_META_DEBUG_GIVE_VETERANCY },
	{ "DEMO_TAKE_VETERANCY",											GameMessage::MSG_META_DEBUG_TAKE_VETERANCY },
	{ "DEMO_BATTLE_CRY",													GameMessage::MSG_META_DEMO_BATTLE_CRY },
#ifdef ALLOW_SURRENDER
	{ "DEMO_TEST_SURRENDER",											GameMessage::MSG_META_DEMO_TEST_SURRENDER },
#endif
	{ "DEMO_TOGGLE_AVI",													GameMessage::MSG_META_DEMO_TOGGLE_AVI },
	{ "DEMO_PLAY_CAMEO_MOVIE",										GameMessage::MSG_META_DEMO_PLAY_CAMEO_MOVIE },
	{ "DEMO_TOGGLE_ZOOM_LOCK",										GameMessage::MSG_META_DEMO_TOGGLE_ZOOM_LOCK },
	{ "DEMO_TOGGLE_SPECIAL_POWER_DELAYS",					GameMessage::MSG_META_DEMO_TOGGLE_SPECIAL_POWER_DELAYS },

	{ "DEMO_TOGGLE_METRICS",											GameMessage::MSG_META_DEMO_TOGGLE_METRICS},
	{ "DEMO_DESHROUD",														GameMessage::MSG_META_DEMO_DESHROUD },
	{ "DEMO_ENSHROUD",														GameMessage::MSG_META_DEMO_ENSHROUD },
	{ "DEMO_TOGGLE_AI_DEBUG",											GameMessage::MSG_META_DEMO_TOGGLE_AI_DEBUG },
	{ "DEMO_TOGGLE_SUPPLY_CENTER_PLACEMENT",			GameMessage::MSG_META_DEMO_TOGGLE_SUPPLY_CENTER_PLACEMENT },
	{ "DEMO_TOGGLE_NO_DRAW",											GameMessage::MSG_NO_DRAW },
	{ "DEMO_CYCLE_LOD_LEVEL",											GameMessage::MSG_META_DEMO_CYCLE_LOD_LEVEL },
	{ "DEMO_DUMP_ASSETS",													GameMessage::MSG_META_DEBUG_DUMP_ASSETS},

	{ "DEMO_INSTANT_BUILD",												GameMessage::MSG_META_DEMO_INSTANT_BUILD },
	{ "DEMO_TOGGLE_CAMERA_DEBUG",									GameMessage::MSG_META_DEMO_TOGGLE_CAMERA_DEBUG },

	/// Begin VTUNE
	{ "DEMO_VTUNE_ON",														GameMessage::MSG_META_DEBUG_VTUNE_ON },
	{ "DEMO_VTUNE_OFF",														GameMessage::MSG_META_DEBUG_VTUNE_OFF },
	/// End VTUNE


	//lorenzen's feather water
	{ "DEMO_TOGGLE_FEATHER_WATER",								GameMessage::MSG_META_DEBUG_TOGGLE_FEATHER_WATER },

	{ "DEMO_INCR_ANIM_SKATE_SPEED",								GameMessage::MSG_META_DEBUG_INCR_ANIM_SKATE_SPEED },
	{ "DEMO_DECR_ANIM_SKATE_SPEED",								GameMessage::MSG_META_DEBUG_DECR_ANIM_SKATE_SPEED },
	{ "DEMO_CYCLE_EXTENT_TYPE",										GameMessage::MSG_META_DEBUG_CYCLE_EXTENT_TYPE },
	{ "DEMO_INCR_EXTENT_MAJOR",										GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR },
	{ "DEMO_DECR_EXTENT_MAJOR",										GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR },
	{ "DEMO_INCR_EXTENT_MAJOR_LARGE",							GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR_BIG },
	{ "DEMO_DECR_EXTENT_MAJOR_LARGE",							GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR_BIG },
	{ "DEMO_INCR_EXTENT_MINOR",										GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR },
	{ "DEMO_DECR_EXTENT_MINOR",										GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR },
	{ "DEMO_INCR_EXTENT_MINOR_LARGE",							GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR_BIG },
	{ "DEMO_DECR_EXTENT_MINOR_LARGE",							GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR_BIG },
	{ "DEMO_INCR_EXTENT_HEIGHT",									GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT },
	{ "DEMO_DECR_EXTENT_HEIGHT",									GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT },
	{ "DEMO_INCR_EXTENT_HEIGHT_LARGE",						GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT_BIG },
	{ "DEMO_DECR_EXTENT_HEIGHT_LARGE",						GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT_BIG },
	{ "DEMO_TOGGLE_NETWORK",											GameMessage::MSG_META_DEBUG_TOGGLE_NETWORK },
	{ "DEBUG_DUMP_PLAYER_OBJECTS",								GameMessage::MSG_META_DEBUG_DUMP_PLAYER_OBJECTS },
	{ "DEBUG_DUMP_ALL_PLAYER_OBJECTS",						GameMessage::MSG_META_DEBUG_DUMP_ALL_PLAYER_OBJECTS },
	{ "DEMO_WIN",																	GameMessage::MSG_META_DEBUG_WIN },
	{ "DEMO_TOGGLE_DEBUG_STATS",									GameMessage::MSG_META_DEMO_TOGGLE_DEBUG_STATS },
	{ "DEBUG_OBJECT_ID_PERFORMANCE",							GameMessage::MSG_META_DEBUG_OBJECT_ID_PERFORMANCE },
	{ "DEBUG_DRAWABLE_ID_PERFORMANCE",						GameMessage::MSG_META_DEBUG_DRAWABLE_ID_PERFORMANCE },
	{ "DEBUG_SLEEPY_UPDATE_PERFORMANCE",					GameMessage::MSG_META_DEBUG_SLEEPY_UPDATE_PERFORMANCE },
#endif // defined(RTS_DEBUG)


#if defined(RTS_DEBUG)
	{ "DEMO_TOGGLE_AUDIODEBUG",										GameMessage::MSG_META_DEMO_TOGGLE_AUDIODEBUG },
#endif//defined(RTS_DEBUG)
#ifdef DUMP_PERF_STATS
	{ "DEMO_PERFORM_STATISTICAL_DUMP",						GameMessage::MSG_META_DEMO_PERFORM_STATISTICAL_DUMP },
#endif//DUMP_PERF_STATS


	{ nullptr, 0	}
};


///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
static const FieldParse TheMetaMapFieldParseTable[] =
{

	{ "Key",								INI::parseLookupList,						KeyNames, offsetof( MetaMapRec, m_key ) },
	{ "Transition",					INI::parseLookupList,						TransitionNames, offsetof( MetaMapRec, m_transition ) },
	{ "Modifiers",					INI::parseLookupList,						ModifierNames, offsetof( MetaMapRec, m_modState ) },
	{ "UseableIn",					INI::parseBitString32,					TheCommandUsableInNames, offsetof( MetaMapRec, m_usableIn ) },
	{ "Category",						INI::parseLookupList,						CategoryListName, offsetof( MetaMapRec, m_category ) },
	{ "Description",				INI::parseAndTranslateLabel,		nullptr, offsetof( MetaMapRec, m_description ) },
	{ "DisplayName",				INI::parseAndTranslateLabel,		nullptr, offsetof( MetaMapRec, m_displayName ) },

	{ nullptr,									nullptr,														nullptr, 0 }

};

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
MetaEventTranslator::MetaEventTranslator()
{
	for (Int i = 0; i < NUM_MOUSE_BUTTONS; ++i) {
		m_nextUpShouldCreateDoubleClick[i] = FALSE;
	}
	m_middleDragged = FALSE;
	m_middleDownTime = 0;
	m_wheelSpin = 0.0f;
}

//-------------------------------------------------------------------------------------------------
MetaEventTranslator::~MetaEventTranslator()
{
}

//-------------------------------------------------------------------------------------------------
static const char * findGameMessageNameByType(GameMessage::Type type)
{
	const char *name = MetaMap::findMetaTypeName(type);
	if (name)
	{
		return name;
	}

	DEBUG_CRASH(("MetaTypeName %d not found -- did you remember to add it to GameMessageMetaTypeNames[] ?", (Int)type));
	return "???";
}

//-------------------------------------------------------------------------------------------------
static Bool isMessageUsable(CommandUsableInType usableIn, GameMessage::Type type)
{
	// We will ignore all commands if the game client has not yet incremented to frame 1.
	// It prevents the user from doing commands during a map load, which throws the input
	// system into whack because there isn't a client frame for the input event, and in
	// the case of a command that pauses the game, like the quit menu, the client frame
	// will never get beyond 0 and we lose the ability to process any input.
	if (TheGameClient->getFrame() == 0)
		return false;

	const Bool usableInShell = (usableIn & COMMANDUSABLE_SHELL);
	const Bool usableInGame = (usableIn & COMMANDUSABLE_GAME);
	const Bool usableAsObserver = (usableIn & COMMANDUSABLE_OBSERVER);
	const Bool isShellActive = TheShell && TheShell->isShellActive();
	const Bool isObserving = !ThePlayerList->getLocalPlayer()->isPlayerActive();

	if (usableInShell && isShellActive)
		return true;

	if (usableInGame && !isShellActive)
		return true;

	if (usableAsObserver && isObserving)
		return true;

	// The shell map runs with the shell active, so a game-only cheat key would never fire there.
	if ((usableInGame || usableAsObserver) && GameMessage::isDebugCheatHotkey(type) && TheGameLogic->isInShellGame())
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MetaEventTranslator::isDebugCheatKey(Int systemKey, Int systemKeyState)
{
#if RTS_ZEROHOUR && (defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
	if (TheMetaMap != nullptr)
	{
		const MappableKeyType keyType = getActionKeyType(systemKey);
		const MappableKeyModState keyModState = getKeyModState(systemKeyState);
		for (const MetaMapRec *map = TheMetaMap->getFirstMetaMapRec(); map; map = map->m_next)
		{
			if (keyType != MK_NONE && map->m_key == keyType && map->m_modState == keyModState &&
				isMessageUsable(map->m_usableIn, map->m_meta))
			{
				return GameMessage::isDebugCheatHotkey(map->m_meta);
			}
		}
	}
#endif
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
GameMessageDisposition MetaEventTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	const GameMessage::Type t = msg->getType();

	if (t == GameMessage::MSG_RAW_KEY_DOWN || t == GameMessage::MSG_RAW_KEY_UP)
	{
		onKeyEvent(msg, disp);
	}
	else if (t == GameMessage::MSG_RAW_MOUSE_WHEEL)
	{
		// only a wheel no window used reaches here, and one that fires a binding does not zoom
		const Real spin = msg->getArgument(1)->real;
		if ((spin > 0.0f) != (m_wheelSpin > 0.0f))
		{
			m_wheelSpin = 0.0f;
		}
		m_wheelSpin += spin;

		// touchpads and smooth wheels send fractions of a notch; a binding fires once per whole notch
		if (fabs(m_wheelSpin) >= 1.0f)
		{
			const Bool up = m_wheelSpin > 0.0f;
			m_wheelSpin -= up ? 1.0f : -1.0f;
			if (onMouseKeyPressed(up ? MK_WHEELUP : MK_WHEELDOWN, msg->getArgument(2)->integer))
			{
				disp = DESTROY_MESSAGE;
			}
		}
	}
	else if (t > GameMessage::MSG_RAW_MOUSE_BEGIN && t < GameMessage::MSG_RAW_MOUSE_END )
	{
		onMouseEvent(msg);
	}

	return disp;
}

//-------------------------------------------------------------------------------------------------
void MetaEventTranslator::onMouseEvent(const GameMessage *msg)
{
	Int index = 3;
	switch (msg->getType())
	{
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			--index;
			m_mouseDownPosition[index] = msg->getArgument(0)->pixel;
			m_nextUpShouldCreateDoubleClick[index] = FALSE;
			if (index == 1)
			{
				m_middleDragged = FALSE;
				m_middleDownTime = msg->getArgument(2)->integer;
			}
			break;
		}

		case GameMessage::MSG_RAW_MOUSE_MIDDLE_DRAG:
		{
			// a rotate that comes back to where it started is still a drag
			if (!TheMouse->isClick(0, 0, m_mouseDownPosition[1], msg->getArgument(0)->pixel))
			{
				m_middleDragged = TRUE;
			}
			break;
		}

		case GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_DOUBLE_CLICK:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
		{
			--index;
			m_nextUpShouldCreateDoubleClick[index] = TRUE;
			break;
		}

		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
			--index;
			FALLTHROUGH;
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			--index;

			constexpr const GameMessage::Type SingleClickMessages[3] =
			{
				GameMessage::MSG_MOUSE_LEFT_CLICK,
				GameMessage::MSG_MOUSE_MIDDLE_CLICK,
				GameMessage::MSG_MOUSE_RIGHT_CLICK,
			};
			constexpr const GameMessage::Type DoubleClickMessages[3] =
			{
				GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK,
				GameMessage::MSG_MOUSE_MIDDLE_DOUBLE_CLICK,
				GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK,
			};

			const ICoord2D location = msg->getArgument(0)->pixel;
			const GameMessage::Type messageType = m_nextUpShouldCreateDoubleClick[index] ? DoubleClickMessages[index] : SingleClickMessages[index];
			GameMessage *newMessage = TheMessageStream->insertMessage(messageType, const_cast<GameMessage*>(msg));

			IRegion2D pixelRegion;
			buildRegion( &m_mouseDownPosition[index], &location, &pixelRegion );
			if (abs(pixelRegion.hi.x - pixelRegion.lo.x) < TheMouse->m_dragTolerance &&
					abs(pixelRegion.hi.y - pixelRegion.lo.y) < TheMouse->m_dragTolerance)
			{
				pixelRegion.hi.x = pixelRegion.lo.x;
				pixelRegion.hi.y = pixelRegion.lo.y;
			}

			newMessage->appendPixelRegionArgument( pixelRegion );

			// append the modifier keys to the message.
			newMessage->appendIntegerArgument( msg->getArgument(1)->integer );

			// a middle click that did not drag the camera fires what is bound to the middle button
			if (index == 1 && !m_middleDragged &&
					TheMouse->isClick(m_middleDownTime, msg->getArgument(2)->integer, m_mouseDownPosition[index], location))
			{
				if (onMouseKeyPressed(MK_MOUSE3, msg->getArgument(1)->integer) && TheLookAtTranslator)
				{
					TheLookAtTranslator->consumeMiddleClick();
				}
			}

			// append the time to the message.
			//newMessage->appendIntegerArgument( msg->getArgument(2)->integer );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MetaEventTranslator::onKeyEvent(const GameMessage *msg, GameMessageDisposition &disp)
{
	const Int systemKey = msg->getArgument(0)->integer;
	const Int systemKeyState = msg->getArgument(1)->integer;

	const MappableKeyType keyType = getActionKeyType(systemKey);
	const MappableKeyModState keyModState = getKeyModState(systemKeyState);

	const Bool modStateRemoved = (keyType == MK_NONE) && (msg->getType() == GameMessage::MSG_RAW_KEY_UP);

	if (modStateRemoved)
	{
		onKeyModStateRemoved(disp, keyModState);
	}
	else
	{
		onKeyPressed(disp, systemKeyState, keyType, keyModState);
	}
}

//-------------------------------------------------------------------------------------------------
void MetaEventTranslator::onKeyModStateRemoved(GameMessageDisposition &disp, MappableKeyModState keyModState)
{
	// TheSuperHackers @fix The key handler now ignores the order in which modifier keys are released.
	// This avoids frustrating experiences where a wrong button release order would skip an important key event.

	for (Int keyDownIndex = 0; keyDownIndex < ARRAY_SIZE(m_keyDownInfos); ++keyDownIndex)
	{
		const MappableKeyType keyDown = (MappableKeyType)keyDownIndex;
		KeyDownInfo &keyDownInfo = m_keyDownInfos[keyDownIndex];

		if (!keyDownInfo.isKeyDown())
			continue;

		for (UnsignedInt modStateIndex = 0; modStateIndex < KeyDownInfo::getMaxKeyModStateCount(); ++modStateIndex)
		{
			const MappableKeyModState keyDownModState = keyDownInfo.getKeyModState(modStateIndex);

			if (keyDownModState == NONE)
				continue;

			if (BitsAreSet(keyModState, keyDownModState))
				continue;

			// Forget that this key and mod state are pressed.
			keyDownInfo.clearKeyModState(modStateIndex);

			for (const MetaMapRec *map = TheMetaMap->getFirstMetaMapRec(); map; map = map->m_next)
			{
				if (!isMessageUsable(map->m_usableIn, map->m_meta))
					continue;

				const Bool isMatchingKeyCombo = map->m_key == keyDown && map->m_modState == keyDownModState;
				const Bool isTransitionUp = map->m_transition == UP;

				if (!(isMatchingKeyCombo && isTransitionUp))
					continue;

				TheMessageStream->appendMessage(map->m_meta);
				disp = DESTROY_MESSAGE;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MetaEventTranslator::onKeyPressed(GameMessageDisposition &disp, Int systemKeyState, MappableKeyType keyType, MappableKeyModState keyModState)
{
	// TheSuperHackers @info The regular key handler only triggers events when the mapped key is pressed,
	// not when the modifier (CTRL, ALT, SHIFT) is pressed, unless the key is MK_NONE.

	for (const MetaMapRec *map = TheMetaMap->getFirstMetaMapRec(); map; map = map->m_next)
	{
		if (!isMessageUsable(map->m_usableIn, map->m_meta))
			continue;

		Bool isMatchingKeyCombo = map->m_key == keyType && map->m_modState == keyModState;
#if RTS_ZEROHOUR
		const Bool facingDragRelease = map->m_meta == GameMessage::MSG_META_END_FACING_DRAG && map->m_key == keyType
			&& (systemKeyState & KEY_STATE_UP) != 0 && m_keyDownInfos[keyType].hasKeyModState(map->m_modState);
		isMatchingKeyCombo = isMatchingKeyCombo || facingDragRelease;
#endif
		const Bool isMatchingTransitionUp = map->m_transition == UP && (systemKeyState & KEY_STATE_UP) != 0;
		const Bool isMatchingTransitionDown = map->m_transition == DOWN && (systemKeyState & KEY_STATE_DOWN) != 0;
		//const Bool isMatchingTransitionDoubleDown = map->m_transition == DOUBLEDOWN && (systemKeyState & KEY_STATE_DOWN) && m_lastKeyDown == key;

		if (isMatchingKeyCombo && (isMatchingTransitionUp || isMatchingTransitionDown /*|| isMatchingTransitionDoubleDown*/))
		{
			if( systemKeyState & KEY_STATE_AUTOREPEAT )
			{
				// if it's an autorepeat of a "known" key, don't generate the meta-event,
				// but DO eat the keystroke so no one else can mess with it
				//DEBUG_LOG(("Frame %d: MetaEventTranslator::translateGameMessage() auto-repeat: %s", TheGameLogic->getFrame(), findGameMessageNameByType(map->m_meta)));
			}
			else
			{
				// TheSuperHackers @feature With grid hotkeys on, a command bar button that has
				// claimed this key wins over the meta event bound to the same letter. Checked
				// here rather than by reordering translators, because the order is fixed before
				// Options.ini is ever read. The meta event still fires whenever no button wants
				// the key, so select all and friends keep working the rest of the time.
				// Only ever yield for an unmodified key. A command bar hotkey is a bare letter, or
				// Shift plus one for batching, so anything carrying Ctrl or Alt cannot be a cameo
				// press and must keep its meta event. Without this a Ctrl combo whose base letter
				// happens to sit on a visible button is swallowed: the meta event steps aside and
				// HotKeyTranslator then rejects the modifiers, so the key does nothing at all.
				if( (keyModState & ~SHIFT) == 0 &&
#if RTS_ZEROHOUR
						!facingDragRelease &&
#endif
						TheGlobalData && TheGlobalData->m_gridHotkeysEnabled && TheHotKeyManager )
				{
					WideChar wKey = TheKeyboard->getPrintableKey( (KeyDefType)keyType, 0 );
					UnicodeString uKey;
					uKey.concat( wKey );
					AsciiString aKey;
					aKey.translate( uKey );
					const char *mouseKey = HotKeyManager::mouseKeyName( keyType );
					if( mouseKey )
					{
						aKey = mouseKey;
					}

					if( aKey.isNotEmpty() && TheHotKeyManager->isHotKeyClaimed( aKey ) )
					{
						disp = KEEP_MESSAGE;	// let HotKeyTranslator have it
						break;
					}
				}

				// THIS IS A GREASY HACK... MESSAGE SHOULD BE HANDLED IN A TRANSLATOR, BUT DURING CINEMATICS THE TRANSLATOR IS DISABLED
				if( map->m_meta ==  GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY)
				{
				if( TheGlobalData && TheFramePacer->isFastForwardAvailable())
					{
						if ( TheWritableGlobalData )
							TheWritableGlobalData->m_TiVOFastMode = 1 - TheGlobalData->m_TiVOFastMode;

						if ( TheInGameUI )
							TheInGameUI->messageNoFormat( TheGlobalData->m_TiVOFastMode
								? TheGameText->FETCH_OR_SUBSTITUTE("GUI:FF_ON", L"Fast Forward is on")
								: TheGameText->FETCH_OR_SUBSTITUTE("GUI:FF_OFF", L"Fast Forward is off")
							);
					}
					disp = KEEP_MESSAGE; // cause for goodness sake, this key gets used a lot by non-replay hotkeys
					break;
				}

#if RTS_ZEROHOUR
				if (map->m_meta == GameMessage::MSG_META_BEGIN_FACING_DRAG)
				{
					m_keyDownInfos[keyType].setKeyModState(map->m_modState);
				}
				else if (map->m_meta == GameMessage::MSG_META_END_FACING_DRAG)
				{
					m_keyDownInfos[keyType].clearKeyModState(map->m_modState);
				}
#endif
				/*GameMessage *metaMsg =*/ TheMessageStream->appendMessage(map->m_meta);
				//DEBUG_LOG(("Frame %d: MetaEventTranslator::translateGameMessage() normal: %s", TheGameLogic->getFrame(), findGameMessageNameByType(map->m_meta)));
			}

			disp = DESTROY_MESSAGE;
			break;
		}
	}

	if (systemKeyState & KEY_STATE_DOWN)
	{
#ifdef DUMP_ALL_KEYS_TO_LOG
		WideChar Wkey = TheKeyboard->getPrintableKey(keyType, 0);
		UnicodeString uKey;
		uKey.set(&Wkey);
		AsciiString aKey;
		aKey.translate(uKey);
		DEBUG_LOG(("^%s ", aKey.str()));
#endif

		if (keyModState != NONE)
		{
			// Remember that this key and mod state are pressed.
			m_keyDownInfos[keyType].setKeyModState(keyModState);
		}
	}
	else
	{
		if (keyModState != NONE)
		{
			DEBUG_ASSERTCRASH(keyType != MK_NONE, ("Key is expected to be not MK_NONE"));

			// Forget that this key and mod state are pressed.
			m_keyDownInfos[keyType].clearKeyModState(keyModState);
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool MetaEventTranslator::onMouseKeyPressed(MappableKeyType keyType, Int systemModifiers)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	const MappableKeyModState keyModState = getKeyModState(systemModifiers);
	onKeyPressed(disp, KEY_STATE_DOWN | systemModifiers, keyType, keyModState);
	onKeyPressed(disp, KEY_STATE_UP | systemModifiers, keyType, keyModState);
	if (disp == DESTROY_MESSAGE)
	{
		return TRUE;
	}

	// a cameo on this input, by the rules HotKeyTranslator keeps for keys
	if ((keyModState & ~SHIFT) != 0 || TheHotKeyManager == nullptr)
	{
		return FALSE;
	}
	return TheHotKeyManager->executeHotKey(AsciiString(HotKeyManager::mouseKeyName(keyType)));
}

//-------------------------------------------------------------------------------------------------
MappableKeyType MetaEventTranslator::getActionKeyType(Int systemKey)
{
	switch (systemKey)
	{
	case KEY_LCTRL:
	case KEY_RCTRL:
	case KEY_LSHIFT:
	case KEY_RSHIFT:
	case KEY_LALT:
	case KEY_RALT:
		return MK_NONE;
	default:
		return (MappableKeyType)systemKey;
	}
}

//-------------------------------------------------------------------------------------------------
MappableKeyModState MetaEventTranslator::getKeyModState(Int systemKeyState)
{
	// for our purposes here, we don't care to distinguish between right and left keys,
	// so just fudge a little to simplify things.
	Int keyModState = 0;

	if( systemKeyState & KEY_STATE_CONTROL )
	{
		keyModState |= CTRL;
	}

	if( systemKeyState & KEY_STATE_SHIFT )
	{
		keyModState |= SHIFT;
	}

	if( systemKeyState & KEY_STATE_ALT )
	{
		keyModState |= ALT;
	}

	return (MappableKeyModState)keyModState;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
MetaMap::MetaMap() :
	m_metaMaps(nullptr)
{
}

//-------------------------------------------------------------------------------------------------
MetaMap::~MetaMap()
{
	while (m_metaMaps)
	{
		MetaMapRec *next = m_metaMaps->m_next;
		deleteInstance(m_metaMaps);
		m_metaMaps = next;
	}
}

//-------------------------------------------------------------------------------------------------
GameMessage::Type MetaMap::findGameMessageMetaType(const char* name)
{
	for (const LookupListRec* metaNames = GameMessageMetaTypeNames; metaNames->name; metaNames++)
		if (stricmp(metaNames->name, name) == 0)
			return (GameMessage::Type)metaNames->value;

	DEBUG_CRASH(("MetaTypeName %s not found -- did you remember to add it to GameMessageMetaTypeNames[] ?", name));
	return GameMessage::MSG_INVALID;
}

//-------------------------------------------------------------------------------------------------
static MetaMapRec *newMetaMapRec(GameMessage::Type t)
{
	MetaMapRec *m = newInstance(MetaMapRec);
	m->m_meta = t;
	m->m_key = MK_NONE;
	m->m_transition = DOWN;
	m->m_modState = NONE;
	m->m_usableIn = COMMANDUSABLE_NONE;
	m->m_category = CATEGORY_MISC;
	m->m_description.clear();
	m->m_displayName.clear();
	m->m_next = nullptr;
	return m;
}

//-------------------------------------------------------------------------------------------------
MetaMapRec *MetaMap::getMetaMapRec(GameMessage::Type t)
{
	for (MetaMapRec *map = m_metaMaps; map; map = map->m_next)
	{
		if (map->m_meta == t)
			return map;
	}

	// not found.. create a new one.
	MetaMapRec *m = newMetaMapRec(t);
	m->m_next = m_metaMaps;
	m_metaMaps = m;

	return m;
}

//-------------------------------------------------------------------------------------------------
// The translator fires only the first record that matches a key, so a record added here leaves
// a key that any other record holds to that one.
MetaMapRec *MetaMap::appendMetaMapRec(GameMessage::Type t)
{
	MetaMapRec *m = newMetaMapRec(t);
	MetaMapRec **tail = &m_metaMaps;
	while (*tail)
		tail = &(*tail)->m_next;
	*tail = m;

	return m;
}

//-------------------------------------------------------------------------------------------------
/*static */ void MetaMap::parseMetaMap(INI* ini)
{
	// read and ignore the meta-map name
	const char *c = ini->getNextToken();

	GameMessage::Type t = TheMetaMap->findGameMessageMetaType(c);
	if (t == GameMessage::MSG_INVALID)
		throw INI_INVALID_DATA;

	MetaMapRec *map = TheMetaMap->getMetaMapRec(t);
	if (map == nullptr)
		throw INI_INVALID_DATA;

	ini->initFromINI(map, TheMetaMapFieldParseTable);
}

//-------------------------------------------------------------------------------------------------
void MetaMap::generateMetaMap()
{
	// TheSuperHackers @info A default mapping for MSG_META_SELECT_ALL_AIRCRAFT would be useful for Generals
	// but is not recommended, because it will cause key mapping conflicts with original game languages.

	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_INCREASE_MAX_RENDER_FPS);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPPLUS;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_DECREASE_MAX_RENDER_FPS);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPMINUS;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_INCREASE_LOGIC_TIME_SCALE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPPLUS;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_DECREASE_LOGIC_TIME_SCALE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPMINUS;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_TOGGLE_PLAYER_OBSERVER);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_M;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_OBSERVER;
		}
	}
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	{
		// TheSuperHackers @feature Free the camera from its zoom limit. Ctrl+Shift+Z is unbound in
		// the engine, in the demo command map and in Contra's own.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_TOGGLE_ZOOM_LOCK);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_Z;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Default binding for the combined cheat. Shift+Ctrl+` is free in
		// the engine, in the demo command map and in Contra's own, and it does not collide with
		// the Ctrl+` health bar cycle because the modifier sets differ. Only exists in builds
		// where cheats are compiled in.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_KITCHEN_SINK);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_TICK;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// TheSuperHackers @feature Print the template name of the selected objects. Ctrl+[ is unbound
		// in the engine defaults, in Contra's CommandMap.ini and in the demo map, so it takes a key
		// nothing else wants. Observer usable too, since reading names is useful while spectating.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_OBJECT_NAME);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_LBRACKET;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Print the particle systems running on the selected objects.
		// Ctrl+] sits next to the object name key and is likewise unbound everywhere.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_PARTICLE_NAMES);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_RBRACKET;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Show the CommandSet each object uses. Ctrl+' continues the run of
		// bracket and quote keys the other overlays took, and is likewise unbound in the engine
		// defaults, in Contra's CommandMap.ini and in the demo map.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_COMMAND_SET);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_APOSTROPHE;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Show the WeaponSet flags an object currently has. Ctrl+; continues
		// the run of punctuation keys the other overlays took, and is likewise unbound in the engine
		// defaults, in Contra's CommandMap.ini and in the demo map.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_WEAPON_SET);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_SEMICOLON;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Show the Armor an object currently uses. Ctrl+- is unbound in the
		// engine defaults and in the demo map.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_ARMOR_SET);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_MINUS;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Show the W3D model each draw module has loaded.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_MODEL_NAMES);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_SLASH;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Outline and name every visible GUI window.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_WINDOW_NAMES);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_SLASH;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Ctrl+, is free in Contra's CommandMap.ini; RTS_DEBUG builds give it to DEMO_BEGIN_ADJUST_DEFAULTPITCH below instead.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_LASER_NAME);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_COMMA;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Show the W3DLaserDraw module tags of each laser beam.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_LASER_BEAM_BLOCK);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_PERIOD;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Cycle the camera cheat: default -> free camera -> chase the selected object -> default.
		// Plain Delete is unbound in the engine defaults, in Contra's CommandMap.ini and in the
		// demo map, and a focused text entry field consumes it in WindowXlat before it gets here.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_CYCLE_CAMERA_MODE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_DEL;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Cycle the skybox through the preset texture sets shipped with the game. Ctrl+Home,
		// because the shipped CommandMap.ini already binds Ctrl+D (DEMO_DEBUG_SELECTION) and
		// the meta translator fires every record matching a keystroke.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_CYCLE_SKYBOX);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_HOME;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Cycle the terrain: normal, hidden over black, hidden over green for chroma keying.
		// Ctrl+End, because the shipped CommandMap.ini already binds Ctrl+X
		// (DEMO_TOGGLE_HURT_ME_MODE) and the meta translator fires every matching record.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_CYCLE_TERRAIN_MODE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_END;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Toggle back face culling. Records made here sit ahead of the INI ones and only the first match fires, so this shadows Contra's Ctrl+F1 SAVE_VIEW1.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_TOGGLE_FACE_CULLING);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_F1;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// Cycle the pathfinding debug overlay. Ctrl+A is retail's debug map binding and is free in Contra's maps.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_DEMO_TOGGLE_AI_DEBUG);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_A;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Pathfinder counter log on the bare keypad plus and minus, which no map binds without a modifier.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_PATHFIND_LOG_ON);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPPLUS;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
		map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_PATHFIND_LOG_OFF);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_KPMINUS;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Shadows CHEAT_RUNSCRIPT1 on Shift+Ctrl+F1 in Contra's CommandMapDemo.ini, the same way as face culling above.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_SHOW_SHADER_DRAWS);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_F1;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
#if RTS_ZEROHOUR
	{
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_TOGGLE_SHADER_PANEL);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_TAB;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
#endif
	{
		// Copy the template names of the selected objects to the clipboard. Alt+V is unbound in
		// the engine defaults, in Contra's CommandMap.ini and in the demo map, which already puts
		// its own cheats on Alt+F, Alt+G, Alt+H and Alt+T. Observer usable too, like the overlays.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_CHEAT_COPY_OBJECT_NAME);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_V;
			map->m_transition = DOWN;
			map->m_modState = ALT;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
#endif
	{
		// TheSuperHackers @feature Cycle the health bar display mode. Ctrl+` is unbound in
		// retail and in the debug and demo command maps, so this takes a key nothing else wants.
		// Note the tick key is layout dependent -- it is ` on a US keyboard but prints something
		// else elsewhere -- however the binding is by scancode, so the same physical key works.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_CYCLE_HEALTH_BAR_MODE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_TICK;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			// in game and while observing, but not in the menus, where health bars mean nothing
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Ctrl+Shift+G cycles HouseColorMode. Free in retail and in
		// Contra's CommandMap.ini and CommandMapDemo.ini, whose only G binding is Alt+G.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_CYCLE_HOUSE_COLOR_MODE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_G;
			map->m_transition = DOWN;
			map->m_modState = SHIFT_CTRL;
			map->m_usableIn = (CommandUsableInType)(COMMANDUSABLE_GAME | COMMANDUSABLE_OBSERVER);
		}
	}
	{
		// TheSuperHackers @feature Tab and Shift+Tab walk the smart selection row. Tab is unbound
		// in retail and in Contra's CommandMap.ini, so the default only fills an empty slot.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_SMART_SELECTION_NEXT_TYPE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_TAB;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
		map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_SMART_SELECTION_PREV_TYPE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_TAB;
			map->m_transition = DOWN;
			map->m_modState = SHIFT;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// TheSuperHackers @feature Plain = is unbound in retail and in Contra's command maps, which use only Ctrl+=.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_AUTO_CREATE_TEAMS);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_EQUAL;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME;
			map->m_category = CATEGORY_TEAM;
			map->m_description = TheGameText->FETCH_OR_SUBSTITUTE("GUI:AutoCreateTeamsDescription", L"Give each of the first ten selected objects its own team, 1 to 9 then 0");
			map->m_displayName = TheGameText->FETCH_OR_SUBSTITUTE("GUI:AutoCreateTeams", L"Auto Create Teams");
		}
	}
	{
		// Caps Lock arms a reverse move for the next terrain click. No cameo or stock binding
		// wants the key, so nothing can shadow it.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_TOGGLE_REVERSEMOVE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_CAPS;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Is mostly useful for Generals.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY);
		Bool useDefaultKey = map->m_key == MK_NONE;
#if RTS_ZEROHOUR && (defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
		useDefaultKey = useDefaultKey || (map->m_key == MK_F && map->m_modState == NONE);
#endif
		if (useDefaultKey)
		{
#if RTS_ZEROHOUR && (defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
			map->m_key = MK_KPSTAR;
#else
			map->m_key = MK_F;
#endif
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_GAME; // @todo COMMANDUSABLE_OBSERVER
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_TOGGLE_PAUSE);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_P;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_OBSERVER;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_TOGGLE_PAUSE_ALT);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_P;
			map->m_transition = DOWN;
			map->m_modState = SHIFT; // Requires modifier to avoid key conflicts as a player.
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Ctrl+\ is a second key for that pause, which stops a single player game in place without the menu Esc opens.
		// A record holds one key, so this one gets its own, appended so lookups by message still find Shift+P.
		Bool bound = FALSE;
		for (MetaMapRec *rec = m_metaMaps; rec; rec = rec->m_next)
		{
			if (rec->m_key == MK_BACKSLASH && rec->m_modState == CTRL)
			{
				bound = TRUE;
			}
		}
		if (!bound)
		{
			MetaMapRec *map = appendMetaMapRec(GameMessage::MSG_META_TOGGLE_PAUSE_ALT);
			map->m_key = MK_BACKSLASH;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_STEP_FRAME);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_O;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_OBSERVER;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_STEP_FRAME_ALT);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_O;
			map->m_transition = DOWN;
			map->m_modState = SHIFT; // Requires modifier to avoid key conflicts as a player.
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER);
		if (map->m_key == MK_NONE) {
			map->m_key = MK_I;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
			map->m_category = CATEGORY_SELECTION;
			map->m_description = TheGameText->FETCH_OR_SUBSTITUTE("GUI:SelectNextIdleWorkerDescription", L"Select the next idle worker");
			map->m_displayName = TheGameText->FETCH_OR_SUBSTITUTE("GUI:SelectNextIdleWorker", L"Next Idle Worker");
		}
	}
	{
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_ALT_CAMERA_ROTATE_LEFT);
		if (map->m_key == MK_NONE) {
			map->m_key = MK_KP4;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_ALT_CAMERA_ROTATE_RIGHT);
		if (map->m_key == MK_NONE) {
			map->m_key = MK_KP6;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_TAKE_SCREENSHOT);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_F12;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
	{
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_TAKE_SCREENSHOT_PNG);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_F12;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}

#if defined(GENERALS_ONLINE)
	{
		// GO's fixed keys, as commands so the Hotkeys screen can rebind them under GO. Appended,
		// so a command that already has one of these keys, or gets it from Hotkeys.ini (an F5
		// control group, say), keeps it.
		const struct { GameMessage::Type meta; MappableKeyType key; } goKeys[] =
		{
			{ GameMessage::MSG_META_TOGGLE_NETWORK_STATS, MK_F10 },
			{ GameMessage::MSG_META_TAKE_SCREENSHOT_ALT, MK_F11 },
			{ GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY, MK_F5 },
			{ GameMessage::MSG_META_TOGGLE_BUDDY_OVERLAY_ALT, MK_INS },
		};
		for (Int i = 0; i < ARRAY_SIZE(goKeys); ++i)
		{
			MetaMapRec *map = appendMetaMapRec(goKeys[i].meta);
			map->m_category = CATEGORY_GO;
			map->m_key = goKeys[i].key;
			map->m_usableIn = COMMANDUSABLE_EVERYWHERE;
		}
	}
#endif

#if RTS_ZEROHOUR
	{
		const GameMessage::Type facingDragTypes[] = { GameMessage::MSG_META_BEGIN_FACING_DRAG, GameMessage::MSG_META_END_FACING_DRAG };
		for (Int i = 0; i < ARRAY_SIZE(facingDragTypes); ++i)
		{
			const MetaMapRec *existing = getFirstMetaMapRec();
			while (existing && existing->m_meta != facingDragTypes[i])
			{
				existing = existing->m_next;
			}
			if (!existing)
			{
				MetaMapRec *map = getMetaMapRec(facingDragTypes[i]);
				map->m_key = MK_MOUSE_LEFT_RIGHT;
				map->m_modState = NONE;
				map->m_usableIn = COMMANDUSABLE_GAME;
				map->m_category = CATEGORY_CONTROL;
				map->m_transition = i == 0 ? DOWN : UP;
			}
		}
	}
	{
		// Powers 1 to 11 take the bare F1 to F11, and a command found on one moves to Shift plus that key.
		const MappableKeyType powerKeys[] = { MK_F1, MK_F2, MK_F3, MK_F4, MK_F5, MK_F6, MK_F7, MK_F8, MK_F9, MK_F10, MK_F11 };
		for (Int i = 0; GameMessage::MSG_META_SPECIAL_POWER_1 + i <= GameMessage::MSG_META_SPECIAL_POWER_16; ++i)
		{
			const GameMessage::Type meta = (GameMessage::Type)(GameMessage::MSG_META_SPECIAL_POWER_1 + i);
			const MetaMapRec *existing = getFirstMetaMapRec();
			while (existing && existing->m_meta != meta)
			{
				existing = existing->m_next;
			}
			if (existing)
			{
				continue;
			}
			if (i < ARRAY_SIZE(powerKeys))
			{
				for (MetaMapRec *rec = m_metaMaps; rec; rec = rec->m_next)
				{
					if (rec->m_key == powerKeys[i] && rec->m_modState == NONE && (rec->m_usableIn & COMMANDUSABLE_GAME))
					{
						rec->m_modState = SHIFT;
					}
				}
			}
			MetaMapRec *map = getMetaMapRec(meta);
			map->m_category = CATEGORY_CONTROL;
			map->m_transition = DOWN;
			if (i < ARRAY_SIZE(powerKeys))
			{
				map->m_key = powerKeys[i];
				map->m_modState = NONE;
				map->m_usableIn = COMMANDUSABLE_GAME;
			}
		}
	}
#endif

#if defined(RTS_DEBUG)
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_DEMO_REMOVE_PREREQ);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_P;
			map->m_transition = DOWN;
			map->m_modState = ALT;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = getMetaMapRec(GameMessage::MSG_META_DEMO_FREE_BUILD);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_B;
			map->m_transition = DOWN;
			map->m_modState = ALT;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_DEMO_BEGIN_ADJUST_DEFAULTPITCH);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_COMMA;
			map->m_transition = DOWN;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
	{
		// Is useful for Generals and Zero Hour.
		MetaMapRec *map = TheMetaMap->getMetaMapRec(GameMessage::MSG_META_DEMO_END_ADJUST_DEFAULTPITCH);
		if (map->m_key == MK_NONE)
		{
			map->m_key = MK_COMMA;
			map->m_transition = UP;
			map->m_modState = CTRL;
			map->m_usableIn = COMMANDUSABLE_GAME;
		}
	}
#endif // defined(RTS_DEBUG)
}

//-------------------------------------------------------------------------------------------------
void MetaMap::verifyMetaMap()
{
#ifdef DEBUG_CRASHING
	for (const MetaMapRec *map = getFirstMetaMapRec(); map; map = map->m_next)
	{
		DEBUG_ASSERTCRASH(
			map->m_meta > GameMessage::MSG_BEGIN_META_MESSAGES &&
			map->m_meta < GameMessage::MSG_END_META_MESSAGES,
			("hmm, expected only meta-msgs here"));
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void MetaMap::rememberDefaults()
{
	m_defaults.clear();
	for (const MetaMapRec *map = m_metaMaps; map; map = map->m_next)
	{
		DefaultKey key;
		key.key = map->m_key;
		key.transition = map->m_transition;
		key.modState = map->m_modState;
		key.usableIn = map->m_usableIn;
		m_defaults[map->m_meta] = key;
	}
}

//-------------------------------------------------------------------------------------------------
void MetaMap::restoreDefaults()
{
	for (MetaMapRec *map = m_metaMaps; map; map = map->m_next)
	{
		DefaultKeyMap::const_iterator it = m_defaults.find(map->m_meta);
		if (it == m_defaults.end())
		{
			// a record only the player's file made
			map->m_key = MK_NONE;
			map->m_transition = DOWN;
			map->m_modState = NONE;
			map->m_usableIn = COMMANDUSABLE_NONE;
			continue;
		}
		map->m_key = it->second.key;
		map->m_transition = it->second.transition;
		map->m_modState = it->second.modState;
		map->m_usableIn = it->second.usableIn;
	}
}

//-------------------------------------------------------------------------------------------------
Bool MetaMap::getDefaultKey(GameMessage::Type t, DefaultKey *key) const
{
	DefaultKeyMap::const_iterator it = m_defaults.find(t);
	if (it == m_defaults.end())
	{
		key->key = MK_NONE;
		key->transition = DOWN;
		key->modState = NONE;
		key->usableIn = COMMANDUSABLE_NONE;
		return FALSE;
	}
	*key = it->second;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
const LookupListRec *MetaMap::getMetaTypeNames()
{
	return GameMessageMetaTypeNames;
}

//-------------------------------------------------------------------------------------------------
const char *MetaMap::findMetaTypeName(GameMessage::Type t)
{
	for (const LookupListRec *metaNames = GameMessageMetaTypeNames; metaNames->name; metaNames++)
	{
		if (metaNames->value == t)
		{
			return metaNames->name;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseMetaMapDefinition( INI* ini )
{
	MetaMap::parseMetaMap(ini);
}

