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

// FILE: GenOnlineProviderFactory.cpp /////////////////////////////////////////
// Description: GenOnline implementation of OnlineProviderFactory.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineProviderFactory.h"
#include "GameNetwork/GenOnline/GenOnlineTransport.h"
#include "GameNetwork/TransportFactory.h"

// The menus still read the GameSpy shaped objects, so the GameSpy factory builds them.
IOnlineConfig *GenOnlineProviderFactory::createConfig( const char *configBuffer )
{
	return m_gameSpy.createConfig(configBuffer);
}

IOnlineSession *GenOnlineProviderFactory::createSession()
{
	return m_gameSpy.createSession();
}

IOnlineChat *GenOnlineProviderFactory::createChat()
{
	return m_gameSpy.createChat();
}

IOnlineLobbyState *GenOnlineProviderFactory::createLobbyState()
{
	return m_gameSpy.createLobbyState();
}

IOnlinePresence *GenOnlineProviderFactory::createPresence()
{
	return m_gameSpy.createPresence();
}

IOnlineStats *GenOnlineProviderFactory::createStats()
{
	return m_gameSpy.createStats();
}

IOnlineLadder *GenOnlineProviderFactory::createLadder()
{
	return m_gameSpy.createLadder();
}

IOnlineResults *GenOnlineProviderFactory::createResults()
{
	return m_gameSpy.createResults();
}

IOnlineMatchmaking *GenOnlineProviderFactory::createMatchmaking()
{
	return m_gameSpy.createMatchmaking();
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

// The GenOnline services manager has its own lifetime, so only the menu objects are created here.
void GenOnlineProviderFactory::createServices( const char *motdBuffer, const char *configBuffer )
{
	m_gameSpy.createServices(motdBuffer, configBuffer);
}

void GenOnlineProviderFactory::destroyServices()
{
	m_gameSpy.destroyServices();
}
