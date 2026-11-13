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

///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/PluginApi/GenOnlinePluginApi.h"

// Name of GenOnline Anti-Cheat in its plugin table and in the log.
#define GENONLINE_ANTICHEAT_PLUGIN_NAME "genonline_anticheat"

// Anticheat identifier that the game sends to the service while GenOnline Anti-Cheat is active.
// The client knows no service value for GenOnline Anti-Cheat, so it reports 0 like a game without anticheat.
#define GENONLINE_ANTICHEAT_IDENTIFIER 0

// Fills out with the table of GenOnline Anti-Cheat. It behaves like GenOnlinePlugin_GetApi of a plugin DLL.
int32_t GENONLINE_PLUGIN_CALL GenOnlineAntiCheat_GetApi(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, GenOnlinePluginApi* out);
