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

// Transport layer - a thin layer around a UDP socket, with queues.
// Author: Matthew D. Campbell, July 2001

#pragma once

#include "GameNetwork/Transport.h"
#include "GameNetwork/udp.h"

/**
 * The transport layer handles the UDP socket for the game, and will packetize and
 * de-packetize multiple ACK/CommandPacket/etc packets into larger aggregates.
 */
class UDPTransport : public Transport
{
public:

	UDPTransport();
	virtual ~UDPTransport() override;

	virtual Bool init( AsciiString ip, UnsignedShort port ) override;
	virtual Bool init( UnsignedInt ip, UnsignedShort port ) override;
	virtual void reset() override;
	virtual Bool update() override;									///< Call this once a GameEngine tick, regardless of whether the frame advances.

	virtual Bool doRecv() override;		///< call this to service the receive packets
	virtual Bool doSend() override;		///< call this to service the send queue.

	virtual Bool queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
		NetMessageFlags flags, Int id */) override;				///< Queue a packet for sending to the specified address and port.  This will be sent on the next update() call.

	virtual Bool allowBroadcasts(Bool val) override { if (!m_udpsock) return false; return (m_udpsock->AllowBroadcasts(val))?true:false; }

	UnsignedShort m_port;
private:
	Bool m_winsockInit;
	UDP *m_udpsock;
};
