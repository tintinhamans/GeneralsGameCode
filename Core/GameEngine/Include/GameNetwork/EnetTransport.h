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

// Mirrors the Transport surface that Connection and ConnectionManager use.

#pragma once

#if defined(RTS_NETWORK_ENET)

#include "GameNetwork/EnetLink.h"
#include "GameNetwork/NetworkDefs.h"

class EnetTransport
{
public:
	EnetTransport();
	~EnetTransport();

	Bool init( UnsignedInt ip, UnsignedShort port );
	void reset();
	Bool update();
	Bool doRecv();
	Bool doSend();

	/// The channel keeps gameplay, chat and file traffic from blocking each other.
	Bool queueSend( UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len, Int channel = 0 );
	Bool addRemote( UnsignedInt addr, UnsignedShort port );
	Bool isLost( UnsignedInt addr, UnsignedShort port ) const { return m_link.isLost(addr, port); }
	void disconnect( UnsignedInt addr, UnsignedShort port, UnsignedInt reason ) { m_link.disconnect(addr, port, reason); }

	Int getIncomingFragmentPercent( UnsignedInt addr, UnsignedShort port, Int channel ) { return m_link.getIncomingFragmentPercent(addr, port, channel); }
	void setAckHoldMs( UnsignedInt ms ) { m_ackHoldMs = ms; }
	Bool isIdle() { return m_link.isIdle() ? TRUE : FALSE; }

	Bool peekReceived( UnsignedInt *addr, UnsignedShort *port, const UnsignedByte **data, Int *len );
	void popReceived();

	Real getIncomingBytesPerSecond();
	Real getIncomingPacketsPerSecond();
	Real getOutgoingBytesPerSecond();
	Real getOutgoingPacketsPerSecond();
	Real getUnknownBytesPerSecond() { return 0.0f; }
	Real getUnknownPacketsPerSecond() { return 0.0f; }

	UnsignedShort m_port;

private:
	void updateStatistics();
	Real average( const UnsignedInt *values );

	EnetLink m_link;
	UnsignedInt m_ackHoldMs;
	UnsignedInt m_incomingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_incomingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	Int m_statisticsSlot;
	UnsignedInt m_lastSecond;
	UnsignedInt m_lastBytesIn;
	UnsignedInt m_lastBytesOut;
	UnsignedInt m_lastPacketsIn;
	UnsignedInt m_lastPacketsOut;
};

#endif
