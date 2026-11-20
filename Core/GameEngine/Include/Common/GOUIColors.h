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

#pragma once

#include "GameClient/Color.h"

// The GO UI colour table: one colour per interface role, read from Data\INI\GOUIColors.ini. A palette replaces that file
// by path and the Retail style leaves it out, so a role the table does not set keeps each site's own colour.

enum GOUIColorRole CPP_11(: Int)
{
	GOUICOLOR_TEXT = 0,
	GOUICOLOR_MUTED,
	GOUICOLOR_DISABLED,
	GOUICOLOR_HELP,
	GOUICOLOR_HILITE,
	GOUICOLOR_LINE,
	GOUICOLOR_NOTICE,
	GOUICOLOR_ERROR,
	GOUICOLOR_SUCCESS,
	GOUICOLOR_ADMIN,
	GOUICOLOR_OFFLINE,
	GOUICOLOR_TOOLTIP_FILL,
	GOUICOLOR_TOOLTIP_BORDER,
	GOUICOLOR_TOOLTIP_TEXT,
	GOUICOLOR_TOOLTIP_SWEEP,
	GOUICOLOR_FIELD_FILL,
	GOUICOLOR_LOBBY_JOINABLE,
	GOUICOLOR_LOBBY_NOT_JOINABLE,
	GOUICOLOR_LOBBY_ALT_ROW,
	GOUICOLOR_LOBBY_ALMOST_FULL,
	GOUICOLOR_CONNECTION_SPINNER,
	GOUICOLOR_CHAT_DEFAULT,		///< OnlineChatColors Default
	GOUICOLOR_PLAYER_NORMAL,	///< OnlineChatColors PlayerNormal
	GOUICOLOR_LOBBY_ROW,			///< OnlineChatColors Game
	GOUICOLOR_BUDDY,					///< OnlineChatColors PlayerBuddy
	GOUICOLOR_MENTION,				///< chat lines that mention the local player

	GOUICOLOR_COUNT
};

Bool GOUIColorFind( GOUIColorRole role, Color *color );	///< TRUE when the table sets the role; color gets its colour
Color GOUIColor( GOUIColorRole role, Color fallback );	///< The role's colour, or fallback when the table does not set it
