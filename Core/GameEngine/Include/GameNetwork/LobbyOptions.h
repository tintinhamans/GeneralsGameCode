/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// The game options string as protobuf fields; the game keeps building and parsing the string.

#pragma once

#include "GameNetwork/NetProto/netproto.pb.h"

namespace LobbyOptions
{
	/// False unless unpack(*out) reproduces the text exactly; then it must be sent as text.
	bool pack(const char *text, netproto_LanOptions *out);

	/// False if it does not fit in size bytes.
	bool unpack(const netproto_LanOptions &options, char *buffer, int size);
}
