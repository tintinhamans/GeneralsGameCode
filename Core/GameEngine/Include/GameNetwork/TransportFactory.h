/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#include "Lib/BaseType.h"

class Transport;

#define UDP_TRANSPORT_NAME "udp"

typedef Transport *(*TransportCreateFunc)();

enum TransportCapability CPP_11(: UnsignedInt)
{
	TRANSPORT_CAPABILITY_NONE = 0,
	TRANSPORT_CAPABILITY_DIRECT_ADDRESS = 1 << 0 ///< Connects by IP address and port without an online service, so LAN and direct connect games can use it.
};

struct TransportInfo
{
	const char *name;
	UnsignedInt capabilities;
};

/**
 * The UDP transport is always registered and used by LAN discovery and NAT negotiation.
 */
class TransportFactory
{
public:
	/// An existing name is replaced.
	static void registerTransport( const char *name, UnsignedInt capabilities, TransportCreateFunc func );

	static Int getTransportCount();

	static TransportInfo getTransportInfo( Int index );

	/// An unknown name creates the UDP transport.
	static Transport *createTransport( const char *name );
};
