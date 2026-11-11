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
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/TransportFactory.h"
#include "GameNetwork/UDPTransport.h"

enum { MAX_TRANSPORT_TYPES = 8, MAX_TRANSPORT_NAME_LEN = 32 };

struct TransportEntry
{
	char name[MAX_TRANSPORT_NAME_LEN];
	UnsignedInt capabilities;
	TransportCreateFunc func;
};

static TransportEntry s_transports[MAX_TRANSPORT_TYPES];
static Int s_transportCount = 0;

static Transport *createUDPTransport()
{
	return new UDPTransport;
}

static TransportEntry *findTransport( const char *name )
{
	for (Int i = 0; i < s_transportCount; ++i)
	{
		if (stricmp(s_transports[i].name, name) == 0)
			return &s_transports[i];
	}
	return nullptr;
}

static void registerBuiltInTransports()
{
	if (findTransport(UDP_TRANSPORT_NAME) == nullptr)
		TransportFactory::registerTransport(UDP_TRANSPORT_NAME, TRANSPORT_CAPABILITY_DIRECT_ADDRESS, createUDPTransport);
}

//-------------------------------------------------------------------------------------------------
void TransportFactory::registerTransport( const char *name, UnsignedInt capabilities, TransportCreateFunc func )
{
	TransportEntry *entry = findTransport(name);
	if (entry == nullptr)
	{
		if (s_transportCount >= MAX_TRANSPORT_TYPES)
		{
			DEBUG_CRASH(("TransportFactory::registerTransport - too many transports, '%s' is ignored", name));
			return;
		}
		entry = &s_transports[s_transportCount++];
		strlcpy(entry->name, name, ARRAY_SIZE(entry->name));
	}
	entry->capabilities = capabilities;
	entry->func = func;
}

//-------------------------------------------------------------------------------------------------
Int TransportFactory::getTransportCount()
{
	registerBuiltInTransports();
	return s_transportCount;
}

//-------------------------------------------------------------------------------------------------
TransportInfo TransportFactory::getTransportInfo( Int index )
{
	registerBuiltInTransports();
	DEBUG_ASSERTCRASH(index >= 0 && index < s_transportCount, ("TransportFactory::getTransportInfo - index %d is out of range", index));

	TransportInfo info;
	info.name = s_transports[index].name;
	info.capabilities = s_transports[index].capabilities;
	return info;
}

//-------------------------------------------------------------------------------------------------
Transport *TransportFactory::createTransport( const char *name )
{
	registerBuiltInTransports();

	const TransportEntry *entry = findTransport(name);
	if (entry == nullptr)
	{
		DEBUG_LOG(("TransportFactory::createTransport - unknown transport '%s', using " UDP_TRANSPORT_NAME, name));
		entry = findTransport(UDP_TRANSPORT_NAME);
	}

	DEBUG_LOG(("TransportFactory::createTransport - creating transport '%s'", entry->name));
	return entry->func();
}
