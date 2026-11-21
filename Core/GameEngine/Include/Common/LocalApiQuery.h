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

// Protocol reference: GenOnline/docs/local-api.md
// Request/Response channel of the live stats readers: the plugin's reader thread only queues the Requests, this module decodes and answers them on
// the main thread. Every query reads game state and changes none of it: no random draws, no commands, no object changes.
#pragma once

#include "Lib/BaseType.h"
#include <string>

class LocalApiSink;

namespace google
{
	namespace protobuf
	{
		class Arena;
	}
}

/// The arena the long-lived local API messages live on (aligned blocks, see LocalApiLive.cpp).
google::protobuf::Arena& LocalApiArena();

/// The current map name as MatchInfo reports it (UTF-8, user data folder stripped), empty when no map is loaded.
std::string LocalApiCurrentMapName();

/// Main thread, once per tick: answers the Requests that arrived since the last call (a few per call, the rest wait for the next tick).
void LocalApiQueryPump(LocalApiSink* sink);
