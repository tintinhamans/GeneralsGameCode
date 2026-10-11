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

// No engine headers: it also builds into the standalone tests.

#pragma once

class EnetLink
{
public:
	enum { MAX_REMOTES = 8, CHANNELS = 3 };

	EnetLink();
	~EnetLink();

	/// Addresses are in host byte order.
	bool open(unsigned int ip, unsigned short port);
	void close();
	bool isOpen() const;

	/// Keeps the connection up: the lower address connects, the higher one waits.
	bool addRemote(unsigned int ip, unsigned short port);

	/// Reliable and ordered within the channel; queued until the peer is connected.
	bool send(unsigned int ip, unsigned short port, const unsigned char *data, int len, int channel = 0);

	void service();
	void flush();

	/// The data stays valid until pop().
	bool peek(unsigned int *ip, unsigned short *port, const unsigned char **data, int *len) const;
	void pop();

	bool isConnected(unsigned int ip, unsigned short port) const;

	/// True once a connection timed out, until it is up again.
	bool isLost(unsigned int ip, unsigned short port) const;

	/// Closes the connection and does not reopen it; the reason reaches the peer.
	void disconnect(unsigned int ip, unsigned short port, unsigned int reason);

	/// Slowest connected peer, in milliseconds.
	unsigned int getMaxRoundTripMs() const;

	bool isIdle() const;

	unsigned int getBytesSent() const;
	unsigned int getBytesReceived() const;
	unsigned int getPacketsSent() const;
	unsigned int getPacketsReceived() const;

private:
	EnetLink(const EnetLink &);
	EnetLink &operator=(const EnetLink &);

	struct Impl;
	Impl *m_impl;
};
