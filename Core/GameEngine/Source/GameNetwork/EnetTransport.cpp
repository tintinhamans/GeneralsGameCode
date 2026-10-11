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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(RTS_NETWORK_ENET)

#include "GameNetwork/EnetTransport.h"

EnetTransport::EnetTransport()
{
	m_port = 0;
	m_statisticsSlot = 0;
	m_lastSecond = 0;
	m_lastBytesIn = m_lastBytesOut = m_lastPacketsIn = m_lastPacketsOut = 0;
	for (Int i = 0; i < MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		m_incomingBytes[i] = m_outgoingBytes[i] = m_incomingPackets[i] = m_outgoingPackets[i] = 0;
	}
}

EnetTransport::~EnetTransport()
{
	reset();
}

Bool EnetTransport::init( UnsignedInt ip, UnsignedShort port )
{
	// The previous owner of the port may still be shutting down.
	Bool opened = FALSE;
	const UnsignedInt start = timeGetTime();
	while (!opened && (timeGetTime() - start) < 1000)
	{
		opened = m_link.open(ip, port);
	}

	if (!opened)
	{
		DEBUG_LOG(("EnetTransport::init - Failure to bind %d.%d.%d.%d:%d", PRINTF_IP_AS_4_INTS(ip), port));
		return FALSE;
	}

	m_port = port;
	m_lastSecond = timeGetTime();
	return TRUE;
}

void EnetTransport::reset()
{
	m_link.close();
}

Bool EnetTransport::update()
{
	return doRecv() && doSend();
}

Bool EnetTransport::doRecv()
{
	if (!m_link.isOpen())
		return FALSE;

	m_link.service();
	return TRUE;
}

Bool EnetTransport::doSend()
{
	if (!m_link.isOpen())
		return FALSE;

	m_link.service();
	m_link.flush();
	updateStatistics();
	return TRUE;
}

Bool EnetTransport::queueSend( UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len, Int channel )
{
	return m_link.send(addr, port, buf, len, channel) ? TRUE : FALSE;
}

Bool EnetTransport::addRemote( UnsignedInt addr, UnsignedShort port )
{
	return m_link.addRemote(addr, port) ? TRUE : FALSE;
}

Bool EnetTransport::peekReceived( UnsignedInt *addr, UnsignedShort *port, const UnsignedByte **data, Int *len )
{
	unsigned int ip;
	unsigned short remotePort;
	if (!m_link.peek(&ip, &remotePort, data, len))
		return FALSE;

	*addr = ip;
	*port = remotePort;
	return TRUE;
}

void EnetTransport::popReceived()
{
	m_link.pop();
}

void EnetTransport::updateStatistics()
{
	const UnsignedInt now = timeGetTime();
	if (m_lastSecond + 1000 < now)
	{
		m_lastSecond = now;
		m_statisticsSlot = (m_statisticsSlot + 1) % MAX_TRANSPORT_STATISTICS_SECONDS;
		m_outgoingPackets[m_statisticsSlot] = 0;
		m_outgoingBytes[m_statisticsSlot] = 0;
		m_incomingPackets[m_statisticsSlot] = 0;
		m_incomingBytes[m_statisticsSlot] = 0;
	}

	const UnsignedInt bytesIn = m_link.getBytesReceived();
	const UnsignedInt bytesOut = m_link.getBytesSent();
	const UnsignedInt packetsIn = m_link.getPacketsReceived();
	const UnsignedInt packetsOut = m_link.getPacketsSent();
	m_incomingBytes[m_statisticsSlot] += bytesIn - m_lastBytesIn;
	m_outgoingBytes[m_statisticsSlot] += bytesOut - m_lastBytesOut;
	m_incomingPackets[m_statisticsSlot] += packetsIn - m_lastPacketsIn;
	m_outgoingPackets[m_statisticsSlot] += packetsOut - m_lastPacketsOut;
	m_lastBytesIn = bytesIn;
	m_lastBytesOut = bytesOut;
	m_lastPacketsIn = packetsIn;
	m_lastPacketsOut = packetsOut;
}

Real EnetTransport::average( const UnsignedInt *values )
{
	Real sum = 0.0f;
	for (Int i = 0; i < MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			sum += values[i];
	}
	return sum / (MAX_TRANSPORT_STATISTICS_SECONDS - 1);
}

Real EnetTransport::getIncomingBytesPerSecond() { return average(m_incomingBytes); }
Real EnetTransport::getIncomingPacketsPerSecond() { return average(m_incomingPackets); }
Real EnetTransport::getOutgoingBytesPerSecond() { return average(m_outgoingBytes); }
Real EnetTransport::getOutgoingPacketsPerSecond() { return average(m_outgoingPackets); }

#endif
