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

// Wire side of the LAN lobby: datagrams for discovery, reliable ENet between game members. No engine headers.

#pragma once

#include "GameNetwork/NetProto/netproto.pb.h"

class LobbyLink
{
public:
	// Bump on any incompatible LanMessage change.
	enum { VERSION = 3 };

	LobbyLink();
	~LobbyLink();

	/// Addresses are in host byte order.
	bool open(unsigned int localIp, unsigned short rawPort, unsigned short enetPort, unsigned int broadcastIp);
	void close();
	bool isOpen() const;

	bool sendBroadcast(const netproto_LanMessage &msg);
	bool sendUnicast(unsigned int ip, const netproto_LanMessage &msg);
	bool sendReliable(unsigned int ip, const netproto_LanMessage &msg);

	void service();

	/// Undecodable messages and other versions are dropped and counted.
	bool pop(unsigned int *fromIp, bool *wasReliable, netproto_LanMessage *msg);
	unsigned int getRejectedCount() const;

	/// Stamps the version. Returns the byte count, or zero if it does not fit.
	static int encode(const netproto_LanMessage &msg, unsigned char *buf, int size);
	/// Returns false for malformed data and for another version.
	static bool decode(const unsigned char *data, int len, netproto_LanMessage *msg);

private:
	LobbyLink(const LobbyLink &);
	LobbyLink &operator=(const LobbyLink &);

	struct Impl;
	Impl *m_impl;
};
