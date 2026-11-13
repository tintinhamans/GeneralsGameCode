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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineProviderFactory.h"
#include "GameNetwork/GenOnline/Provider/GenOnlineResults.h"
#include "GameNetwork/GenOnline/GenOnlineTransport.h"
#include "GameNetwork/TransportFactory.h"

// GenOnline services come from GenOnlineServicesManager, so the provider creates no GameSpy shaped objects.
IOnlineConfig *GenOnlineProviderFactory::createConfig( const char *configBuffer )
{
	return nullptr;
}

IOnlineSession *GenOnlineProviderFactory::createSession()
{
	return nullptr;
}

IOnlineChat *GenOnlineProviderFactory::createChat()
{
	return nullptr;
}

IOnlineLobbyState *GenOnlineProviderFactory::createLobbyState()
{
	return nullptr;
}

IOnlinePresence *GenOnlineProviderFactory::createPresence()
{
	return nullptr;
}

IOnlineStats *GenOnlineProviderFactory::createStats()
{
	return nullptr;
}

IOnlineLadder *GenOnlineProviderFactory::createLadder()
{
	return nullptr;
}

// The engine keeps one results queue for its lifetime. GenOnline reports results through its own services.
IOnlineResults *GenOnlineProviderFactory::createResults()
{
	return NEW GenOnlineResults;
}

IOnlineMatchmaking *GenOnlineProviderFactory::createMatchmaking()
{
	return nullptr;
}

// GenOnline games run over the GameNetworkingSockets transport.
Int GenOnlineProviderFactory::chooseGameTransport( const TransportInfo *transports, Int count ) const
{
	for (Int i = 0; i < count; ++i)
	{
		if (stricmp(transports[i].name, GENONLINE_TRANSPORT_NAME) == 0)
			return i;
	}
	return -1;
}

// The GenOnline services manager has its own lifetime, so there are no services to create here.
void GenOnlineProviderFactory::createServices( const char *motdBuffer, const char *configBuffer )
{
}

void GenOnlineProviderFactory::destroyServices()
{
}
