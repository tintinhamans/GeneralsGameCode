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

#include "GameNetwork/LobbyLink.h"
#include "GameNetwork/EnetLink.h"

#include <enet/enet.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <string.h>

namespace
{
	const unsigned int NUDGE_TYPE = 0xFFFF;     // asks the lower address to open the reliable connection
	const unsigned int NUDGE_INTERVAL_MS = 500;
	const unsigned int NUDGE_WINDOW_MS = 10000;
	const int MAX_DESTINATIONS = 16;
	const int MAX_QUEUED = 128;
	const int MAX_DATAGRAM = 2048;
	const int MAX_DATAGRAMS_PER_SERVICE = 64;
}

struct LobbyLink::Impl
{
	struct Destination
	{
		unsigned int ip;
		unsigned int lastSend;
		unsigned int lastNudge;
	};

	struct Queued
	{
		unsigned int ip;
		bool reliable;
		netproto_LanMessage msg;
	};

	Impl() : socket(ENET_SOCKET_NULL), localIp(0), rawPort(0), enetPort(0), broadcastIp(0), destinationCount(0), head(0), count(0), rejected(0)
	{
		queue = new Queued[MAX_QUEUED];
	}

	~Impl()
	{
		delete[] queue;
	}

	EnetLink link;
	ENetSocket socket;
	unsigned int localIp;
	unsigned short rawPort;
	unsigned short enetPort;
	unsigned int broadcastIp;
	Destination destinations[MAX_DESTINATIONS];
	int destinationCount;
	Queued *queue;
	int head;
	int count;
	unsigned int rejected;

	bool sendDatagram(unsigned int ip, const netproto_LanMessage &msg)
	{
		unsigned char buf[MAX_DATAGRAM];
		const int len = LobbyLink::encode(msg, buf, sizeof(buf));
		if (len <= 0 || socket == ENET_SOCKET_NULL)
			return false;

		ENetAddress address;
		address.host = ENET_HOST_TO_NET_32(ip);
		address.port = rawPort;

		ENetBuffer buffer;
		buffer.data = buf;
		buffer.dataLength = len;
		return enet_socket_send(socket, &address, &buffer, 1) == len;
	}

	void push(unsigned int ip, bool reliable, const netproto_LanMessage &msg)
	{
		if (count >= MAX_QUEUED)
		{
			++rejected;
			return;
		}

		Queued &slot = queue[(head + count) % MAX_QUEUED];
		slot.ip = ip;
		slot.reliable = reliable;
		slot.msg = msg;
		++count;
	}

	Destination *findDestination(unsigned int ip)
	{
		for (int i = 0; i < destinationCount; ++i)
		{
			if (destinations[i].ip == ip)
				return &destinations[i];
		}
		return 0;
	}

	void touchDestination(unsigned int ip)
	{
		Destination *dest = findDestination(ip);
		if (!dest)
		{
			if (destinationCount == MAX_DESTINATIONS)
			{
				memmove(&destinations[0], &destinations[1], sizeof(Destination) * (MAX_DESTINATIONS - 1));
				--destinationCount;
			}
			dest = &destinations[destinationCount++];
			dest->ip = ip;
			dest->lastNudge = 0;
		}
		dest->lastSend = enet_time_get();
	}

	void receiveDatagrams()
	{
		unsigned char buf[MAX_DATAGRAM];
		for (int n = 0; n < MAX_DATAGRAMS_PER_SERVICE; ++n)
		{
			ENetAddress from;
			ENetBuffer buffer;
			buffer.data = buf;
			buffer.dataLength = sizeof(buf);
			const int len = enet_socket_receive(socket, &from, &buffer, 1);
			if (len <= 0)
				return;

			netproto_LanMessage msg;
			if (!LobbyLink::decode(buf, len, &msg))
			{
				++rejected;
				continue;
			}

			const unsigned int ip = ENET_NET_TO_HOST_32(from.host);
			if (msg.type == NUDGE_TYPE)
			{
				link.addRemote(ip, enetPort);
				continue;
			}

			push(ip, false, msg);
		}
	}

	void receiveReliable()
	{
		unsigned int ip;
		unsigned short port;
		const unsigned char *data;
		int len;
		while (link.peek(&ip, &port, &data, &len))
		{
			netproto_LanMessage msg;
			if (LobbyLink::decode(data, len, &msg))
				push(ip, true, msg);
			else
				++rejected;
			link.pop();
		}
	}

	// The higher address cannot open the connection itself; it asks the lower one to.
	void nudgeWaitingDestinations()
	{
		const unsigned int now = enet_time_get();
		for (int i = 0; i < destinationCount; ++i)
		{
			Destination &dest = destinations[i];
			if (localIp < dest.ip || link.isConnected(dest.ip, enetPort))
				continue;
			if (now - dest.lastSend > NUDGE_WINDOW_MS || now - dest.lastNudge < NUDGE_INTERVAL_MS)
				continue;

			dest.lastNudge = now;
			netproto_LanMessage nudge;
			memset(&nudge, 0, sizeof(nudge));
			nudge.type = NUDGE_TYPE;
			sendDatagram(dest.ip, nudge);
		}
	}
};

LobbyLink::LobbyLink() : m_impl(new Impl)
{
}

LobbyLink::~LobbyLink()
{
	close();
	delete m_impl;
}

bool LobbyLink::open(unsigned int localIp, unsigned short rawPort, unsigned short enetPort, unsigned int broadcastIp)
{
	close();
	Impl *impl = m_impl;

	if (!impl->link.open(localIp, enetPort))
		return false;

	impl->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (impl->socket == ENET_SOCKET_NULL)
	{
		impl->link.close();
		return false;
	}

	ENetAddress address;
	address.host = localIp ? ENET_HOST_TO_NET_32(localIp) : ENET_HOST_ANY;
	address.port = rawPort;
	enet_socket_set_option(impl->socket, ENET_SOCKOPT_NONBLOCK, 1);
	enet_socket_set_option(impl->socket, ENET_SOCKOPT_BROADCAST, 1);
	if (enet_socket_bind(impl->socket, &address) < 0)
	{
		enet_socket_destroy(impl->socket);
		impl->socket = ENET_SOCKET_NULL;
		impl->link.close();
		return false;
	}

	impl->localIp = localIp;
	impl->rawPort = rawPort;
	impl->enetPort = enetPort;
	impl->broadcastIp = broadcastIp;
	impl->destinationCount = 0;
	return true;
}

void LobbyLink::close()
{
	Impl *impl = m_impl;
	if (impl->socket != ENET_SOCKET_NULL)
	{
		enet_socket_destroy(impl->socket);
		impl->socket = ENET_SOCKET_NULL;
	}
	impl->link.close();
	impl->head = 0;
	impl->count = 0;
}

bool LobbyLink::isOpen() const
{
	return m_impl->socket != ENET_SOCKET_NULL;
}

bool LobbyLink::sendBroadcast(const netproto_LanMessage &msg)
{
	return m_impl->sendDatagram(m_impl->broadcastIp, msg);
}

bool LobbyLink::sendUnicast(unsigned int ip, const netproto_LanMessage &msg)
{
	return m_impl->sendDatagram(ip, msg);
}

bool LobbyLink::sendReliable(unsigned int ip, const netproto_LanMessage &msg)
{
	Impl *impl = m_impl;
	unsigned char buf[MAX_DATAGRAM];
	const int len = encode(msg, buf, sizeof(buf));
	if (len <= 0)
		return false;

	impl->touchDestination(ip);
	return impl->link.send(ip, impl->enetPort, buf, len);
}

void LobbyLink::service()
{
	Impl *impl = m_impl;
	if (!isOpen())
		return;

	impl->receiveDatagrams();
	impl->link.service();
	impl->link.flush();
	impl->receiveReliable();
	impl->nudgeWaitingDestinations();
}

bool LobbyLink::pop(unsigned int *fromIp, bool *wasReliable, netproto_LanMessage *msg)
{
	Impl *impl = m_impl;
	if (impl->count == 0)
		return false;

	const Impl::Queued &slot = impl->queue[impl->head];
	*fromIp = slot.ip;
	*wasReliable = slot.reliable;
	*msg = slot.msg;
	impl->head = (impl->head + 1) % MAX_QUEUED;
	--impl->count;
	return true;
}

unsigned int LobbyLink::getRejectedCount() const
{
	return m_impl->rejected;
}

int LobbyLink::encode(const netproto_LanMessage &msg, unsigned char *buf, int size)
{
	netproto_LanMessage copy = msg;
	copy.version = VERSION;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, size);
	if (!pb_encode(&stream, netproto_LanMessage_fields, &copy))
		return 0;

	return (int)stream.bytes_written;
}

bool LobbyLink::decode(const unsigned char *data, int len, netproto_LanMessage *msg)
{
	if (len <= 0)
		return false;

	memset(msg, 0, sizeof(*msg));
	pb_istream_t stream = pb_istream_from_buffer(data, len);
	return pb_decode(&stream, netproto_LanMessage_fields, msg) && msg->version == VERSION;
}
