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

#pragma once

#if defined(RTS_NETWORK_ENET)

#include "GameNetwork/NetCommandList.h"

namespace NetProto
{
	// Bump on any incompatible schema change.
	static constexpr const UnsignedInt VERSION = 5;

	// A single command larger than this is sent alone.
	static constexpr const UnsignedInt MAX_ENVELOPE_BYTES = 8192;

	static constexpr const UnsignedInt MAX_ARGUMENTS = 4096;
	static constexpr const UnsignedInt MAX_PAYLOAD_BYTES = 32 * 1024 * 1024;

	// Separate channels so that a large file cannot stall the frame traffic.
	enum Channel { CHANNEL_GAMEPLAY, CHANNEL_MESSAGES, CHANNEL_FILES, CHANNEL_COUNT };

	Channel getChannel(NetCommandType type);

	/// Zero outside a game.
	void setSession(UnsignedInt session);
	UnsignedInt getSession();

	struct Hello
	{
		UnsignedInt version;
		UnsignedInt session;
		UnsignedInt slot;
	};

	/// Zero if the command type cannot be sent.
	UnsignedInt getCommandSize(const NetCommandRef *ref);

	/// Zero if it does not fit or a command cannot be sent.
	UnsignedInt encodeEnvelope(const Hello *hello, NetCommandRef *const *refs, Int count, UnsignedByte *buf, UnsignedInt bufSize);

	/// The caller owns the list; nullptr if malformed.
	NetCommandList *decodeEnvelope(const UnsignedByte *data, UnsignedInt size, Hello *hello, Bool *hasHello);
}

#endif
