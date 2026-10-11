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

#include "GameNetwork/EnetLink.h"

#include <enet/enet.h>

namespace
{
	const int MAX_PENDING = 1024;
	const int MAX_INBOUND = 1024;
	const unsigned int RECONNECT_MS = 1000;
	const unsigned int TIMEOUT_LIMIT = 32;
	const unsigned int TIMEOUT_MIN_MS = 5000;
	const unsigned int TIMEOUT_MAX_MS = 15000;
	const unsigned int GOODBYE = 1;     // disconnect data of an orderly close

	int s_enetUsers = 0;
}

struct EnetLink::Impl
{
	struct Remote
	{
		Remote() : used(false), wanted(false), clearOnDisconnect(false), lost(false), ip(0), port(0), peer(0), connected(false), lastAttempt(0), head(0), count(0)
		{
			for (int i = 0; i < MAX_PENDING; ++i)
				pending[i] = 0;
		}

		bool used;
		bool wanted;                    // a connection is kept up for this remote even when idle
		bool clearOnDisconnect;
		bool lost;                      // the connection timed out and has not been up again
		unsigned int ip;
		unsigned short port;
		ENetPeer *peer;
		bool connected;
		unsigned int lastAttempt;
		ENetPacket *pending[MAX_PENDING];
		unsigned char pendingChannel[MAX_PENDING];
		int head;
		int count;
	};

	struct Inbound
	{
		ENetPacket *packet;
		unsigned int ip;
		unsigned short port;
	};

	Impl() : host(0), localIp(0), localPort(0), inHead(0), inCount(0)
	{
	}

	ENetHost *host;
	unsigned int localIp;
	unsigned short localPort;
	Remote remotes[MAX_REMOTES];
	Inbound inbound[MAX_INBOUND];
	int inHead;
	int inCount;

	bool isInitiator(unsigned int ip, unsigned short port) const
	{
		return localIp < ip || (localIp == ip && localPort < port);
	}

	Remote *find(unsigned int ip, unsigned short port)
	{
		for (int i = 0; i < MAX_REMOTES; ++i)
		{
			if (remotes[i].used && remotes[i].ip == ip && remotes[i].port == port)
				return &remotes[i];
		}
		return 0;
	}

	Remote *findOrCreate(unsigned int ip, unsigned short port)
	{
		Remote *remote = find(ip, port);
		if (remote)
			return remote;

		for (int i = 0; i < MAX_REMOTES; ++i)
		{
			if (!remotes[i].used)
			{
				remotes[i].used = true;
				remotes[i].wanted = false;
				remotes[i].ip = ip;
				remotes[i].port = port;
				remotes[i].peer = 0;
				remotes[i].connected = false;
				remotes[i].lastAttempt = 0;
				remotes[i].head = 0;
				remotes[i].count = 0;
				return &remotes[i];
			}
		}
		return 0;
	}

	void clearPending(Remote *remote)
	{
		while (remote->count > 0)
		{
			enet_packet_destroy(remote->pending[remote->head]);
			remote->pending[remote->head] = 0;
			remote->head = (remote->head + 1) % MAX_PENDING;
			--remote->count;
		}
		remote->head = 0;
	}

	void startConnect(Remote *remote)
	{
		ENetAddress address;
		address.host = ENET_HOST_TO_NET_32(remote->ip);
		address.port = remote->port;

		remote->lastAttempt = enet_time_get();
		remote->peer = enet_host_connect(host, &address, CHANNELS, 0);
		remote->connected = false;
		if (remote->peer)
			remote->peer->data = remote;
	}

	void flushPending(Remote *remote)
	{
		while (remote->count > 0)
		{
			ENetPacket *packet = remote->pending[remote->head];
			const unsigned char channel = remote->pendingChannel[remote->head];
			remote->pending[remote->head] = 0;
			remote->head = (remote->head + 1) % MAX_PENDING;
			--remote->count;
			if (enet_peer_send(remote->peer, channel, packet) < 0)
				enet_packet_destroy(packet);
		}
	}

	void onConnect(ENetPeer *peer)
	{
		const unsigned int ip = ENET_NET_TO_HOST_32(peer->address.host);
		const unsigned short port = peer->address.port;

		Remote *remote = peer->data ? static_cast<Remote *>(peer->data) : findOrCreate(ip, port);
		if (!remote)
		{
			enet_peer_reset(peer);
			return;
		}

		if (remote->peer && remote->peer != peer)
		{
			remote->peer->data = 0;
			enet_peer_reset(remote->peer);
		}

		remote->peer = peer;
		remote->connected = true;
		remote->lost = false;
		peer->data = remote;
		enet_peer_timeout(peer, TIMEOUT_LIMIT, TIMEOUT_MIN_MS, TIMEOUT_MAX_MS);
		flushPending(remote);
	}

	void onDisconnect(ENetPeer *peer, unsigned int data)
	{
		Remote *remote = static_cast<Remote *>(peer->data);
		peer->data = 0;
		if (remote && remote->peer == peer)
		{
			// A timeout reports no data; an orderly close or a refusal does.
			if (remote->connected && data == 0 && !remote->clearOnDisconnect)
				remote->lost = true;
			remote->peer = 0;
			remote->connected = false;
			if (data != 0 || remote->clearOnDisconnect)
				remote->wanted = false;
		}
	}

	void onReceive(ENetPeer *peer, ENetPacket *packet)
	{
		Remote *remote = static_cast<Remote *>(peer->data);
		if (!remote || inCount >= MAX_INBOUND)
		{
			enet_packet_destroy(packet);
			return;
		}

		Inbound &slot = inbound[(inHead + inCount) % MAX_INBOUND];
		slot.packet = packet;
		slot.ip = remote->ip;
		slot.port = remote->port;
		++inCount;
	}
};

EnetLink::EnetLink() : m_impl(new Impl)
{
}

EnetLink::~EnetLink()
{
	close();
	delete m_impl;
}

bool EnetLink::open(unsigned int ip, unsigned short port)
{
	close();

	if (s_enetUsers++ == 0 && enet_initialize() != 0)
	{
		s_enetUsers = 0;
		return false;
	}

	ENetAddress address;
	address.host = ip ? ENET_HOST_TO_NET_32(ip) : ENET_HOST_ANY;
	address.port = port;

	m_impl->host = enet_host_create(&address, MAX_REMOTES, CHANNELS, 0, 0);
	if (!m_impl->host)
	{
		if (--s_enetUsers == 0)
			enet_deinitialize();
		return false;
	}

	m_impl->localIp = ip;
	m_impl->localPort = port;
	return true;
}

void EnetLink::close()
{
	Impl *impl = m_impl;
	if (!impl->host)
		return;

	for (int i = 0; i < MAX_REMOTES; ++i)
	{
		Impl::Remote &remote = impl->remotes[i];
		if (remote.peer && remote.connected)
			enet_peer_disconnect_now(remote.peer, GOODBYE);

		impl->clearPending(&remote);
		remote.used = false;
		remote.peer = 0;
		remote.connected = false;
	}

	enet_host_flush(impl->host);
	enet_host_destroy(impl->host);
	impl->host = 0;

	while (impl->inCount > 0)
		pop();

	if (--s_enetUsers == 0)
		enet_deinitialize();
}

bool EnetLink::isOpen() const
{
	return m_impl->host != 0;
}

bool EnetLink::addRemote(unsigned int ip, unsigned short port)
{
	Impl *impl = m_impl;
	if (!impl->host)
		return false;

	Impl::Remote *remote = impl->findOrCreate(ip, port);
	if (!remote)
		return false;

	remote->wanted = true;
	if (!remote->peer && impl->isInitiator(ip, port))
		impl->startConnect(remote);

	return true;
}

bool EnetLink::send(unsigned int ip, unsigned short port, const unsigned char *data, int len, int channel)
{
	Impl *impl = m_impl;
	if (!impl->host || len <= 0 || channel < 0 || channel >= CHANNELS)
		return false;

	Impl::Remote *remote = impl->findOrCreate(ip, port);
	if (!remote)
		return false;

	ENetPacket *packet = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
	if (!packet)
		return false;

	if (remote->connected)
	{
		if (enet_peer_send(remote->peer, (enet_uint8)channel, packet) < 0)
		{
			enet_packet_destroy(packet);
			return false;
		}
		return true;
	}

	if (remote->count >= MAX_PENDING)
	{
		enet_packet_destroy(packet);
		return false;
	}

	remote->pendingChannel[(remote->head + remote->count) % MAX_PENDING] = (unsigned char)channel;
	remote->pending[(remote->head + remote->count) % MAX_PENDING] = packet;
	++remote->count;

	if (!remote->peer && impl->isInitiator(ip, port))
		impl->startConnect(remote);

	return true;
}

void EnetLink::service()
{
	Impl *impl = m_impl;
	if (!impl->host)
		return;

	ENetEvent event;
	while (impl->inCount < MAX_INBOUND && enet_host_service(impl->host, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
				impl->onConnect(event.peer);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				impl->onDisconnect(event.peer, event.data);
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				impl->onReceive(event.peer, event.packet);
				break;
			default:
				break;
		}
	}

	// A connect that timed out keeps its queued packets and is tried again.
	const unsigned int now = enet_time_get();
	for (int i = 0; i < MAX_REMOTES; ++i)
	{
		Impl::Remote &remote = impl->remotes[i];
		if (remote.used && !remote.peer && (remote.count > 0 || remote.wanted)
			&& impl->isInitiator(remote.ip, remote.port) && now - remote.lastAttempt >= RECONNECT_MS)
		{
			impl->startConnect(&remote);
		}
	}
}

void EnetLink::flush()
{
	if (m_impl->host)
		enet_host_flush(m_impl->host);
}

bool EnetLink::peek(unsigned int *ip, unsigned short *port, const unsigned char **data, int *len) const
{
	const Impl *impl = m_impl;
	if (impl->inCount == 0)
		return false;

	const Impl::Inbound &slot = impl->inbound[impl->inHead];
	*ip = slot.ip;
	*port = slot.port;
	*data = slot.packet->data;
	*len = (int)slot.packet->dataLength;
	return true;
}

void EnetLink::pop()
{
	Impl *impl = m_impl;
	if (impl->inCount == 0)
		return;

	enet_packet_destroy(impl->inbound[impl->inHead].packet);
	impl->inbound[impl->inHead].packet = 0;
	impl->inHead = (impl->inHead + 1) % MAX_INBOUND;
	--impl->inCount;
}

bool EnetLink::isConnected(unsigned int ip, unsigned short port) const
{
	const Impl::Remote *remote = const_cast<Impl *>(m_impl)->find(ip, port);
	return remote && remote->connected;
}

bool EnetLink::isLost(unsigned int ip, unsigned short port) const
{
	const Impl::Remote *remote = const_cast<Impl *>(m_impl)->find(ip, port);
	return remote && remote->lost;
}

void EnetLink::disconnect(unsigned int ip, unsigned short port, unsigned int reason)
{
	Impl::Remote *remote = m_impl->find(ip, port);
	if (!remote)
		return;

	remote->wanted = false;
	remote->clearOnDisconnect = true;
	if (remote->peer)
		enet_peer_disconnect(remote->peer, reason);
}

int EnetLink::getIncomingFragmentPercent(unsigned int ip, unsigned short port, int channelId) const
{
	const Impl::Remote *remote = const_cast<Impl *>(m_impl)->find(ip, port);
	if (!remote || !remote->peer || channelId < 0 || (size_t)channelId >= remote->peer->channelCount)
		return -1;

	ENetList *commands = &remote->peer->channels[channelId].incomingReliableCommands;
	for (ENetListIterator it = enet_list_begin(commands); it != enet_list_end(commands); it = enet_list_next(it))
	{
		const ENetIncomingCommand *command = (const ENetIncomingCommand *)it;
		if (command->fragmentCount > 1 && command->fragmentsRemaining > 0)
			return (int)((command->fragmentCount - command->fragmentsRemaining) * 100u / command->fragmentCount);
	}
	return -1;
}

unsigned int EnetLink::getRoundTripEstimateMs() const
{
	unsigned int estimate = 0;
	for (int i = 0; i < MAX_REMOTES; ++i)
	{
		const Impl::Remote &remote = m_impl->remotes[i];
		if (!remote.used || !remote.connected || !remote.peer)
			continue;

		// ENet derives its own retransmit timeout the same way.
		const unsigned int peerEstimate = remote.peer->roundTripTime + 4 * remote.peer->roundTripTimeVariance;
		if (peerEstimate > estimate)
			estimate = peerEstimate;
	}
	return estimate;
}

bool EnetLink::isIdle() const
{
	for (int i = 0; i < MAX_REMOTES; ++i)
	{
		const Impl::Remote &remote = m_impl->remotes[i];
		if (!remote.used)
			continue;
		if (remote.count > 0)
			return false;
		if (remote.peer && remote.connected && (!enet_list_empty(&remote.peer->outgoingCommands) || !enet_list_empty(&remote.peer->sentReliableCommands)))
			return false;
	}
	return true;
}

unsigned int EnetLink::getBytesSent() const
{
	return m_impl->host ? m_impl->host->totalSentData : 0;
}

unsigned int EnetLink::getBytesReceived() const
{
	return m_impl->host ? m_impl->host->totalReceivedData : 0;
}

unsigned int EnetLink::getPacketsSent() const
{
	return m_impl->host ? m_impl->host->totalSentPackets : 0;
}

unsigned int EnetLink::getPacketsReceived() const
{
	return m_impl->host ? m_impl->host->totalReceivedPackets : 0;
}
