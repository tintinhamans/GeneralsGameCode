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

// Replaces Transport for LANAPI: lobby wide traffic as datagrams, messages within a game over ENet.

#pragma once

#if defined(RTS_NETWORK_ENET)

#include "GameNetwork/LobbyLink.h"
#include "GameNetwork/NetworkDefs.h"

class LanTransport
{
public:
	LanTransport();
	~LanTransport();

	Bool init( UnsignedInt ip, UnsignedShort port );	///< Reliable messages use port + 1.
	void reset();
	Bool update();

	/// buf holds a LANMessage.
	Bool queueSend( UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len );
	Bool allowBroadcasts( Bool val ) { return TRUE; }

	TransportMessage m_inBuffer[MAX_MESSAGES];
	UnsignedShort m_port;

private:
	LobbyLink m_link;
};

#endif
