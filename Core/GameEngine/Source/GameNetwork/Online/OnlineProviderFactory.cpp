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

// FILE: OnlineProviderFactory.cpp ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/OnlineProviderFactory.h"
#include "GameNetwork/Online/GameSpy/GameSpyProviderFactory.h"
#include "GameNetwork/TransportFactory.h"

//-----------------------------------------------------------------------------
// PUBLIC DATA
//-----------------------------------------------------------------------------

OnlineRuntime TheOnline = { ONLINE_PROVIDER_GAMESPY, nullptr };

//-----------------------------------------------------------------------------
// PRIVATE DATA
//-----------------------------------------------------------------------------

static char s_commandLineProvider[64] = { 0 };

// The results binding lasts for the process lifetime so a late provider change cannot mix old results with new services.

static OnlineProviderFactory *s_resultsProvider = nullptr;
static Bool s_resultsProviderBound = FALSE;

// The registry is a function-local static so no namespace-scope STLport container exists under VC6.
struct OnlineProviderRegistry
{
	OnlineProviderFactory *m_factories[MAX_ONLINE_PROVIDERS];
	Int m_count;
};

static OnlineProviderRegistry& getRegistry()
{
	static OnlineProviderRegistry theRegistry = { { 0 }, 0 };
	return theRegistry;
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
//-----------------------------------------------------------------------------

void RegisterOnlineProvider( OnlineProviderFactory *factory )
{
	if (factory == nullptr)
		return;

	OnlineProviderRegistry& reg = getRegistry();

	// Replace an existing entry for the same type so RegisterBuiltInProviders() stays idempotent.
	for (Int i = 0; i < reg.m_count; ++i)
	{
		if (reg.m_factories[i] != nullptr && reg.m_factories[i]->getType() == factory->getType())
		{
			reg.m_factories[i] = factory;
			return;
		}
	}

	if (reg.m_count >= MAX_ONLINE_PROVIDERS)
	{
		DEBUG_LOG(("RegisterOnlineProvider: registry full (%d entries), ignoring '%s'",
			MAX_ONLINE_PROVIDERS, factory->getName()));
		return;
	}

	reg.m_factories[reg.m_count++] = factory;
}

OnlineProviderFactory *GetOnlineProvider( OnlineProviderType type )
{
	OnlineProviderRegistry& reg = getRegistry();
	for (Int i = 0; i < reg.m_count; ++i)
	{
		if (reg.m_factories[i] != nullptr && reg.m_factories[i]->getType() == type)
			return reg.m_factories[i];
	}
	return nullptr;
}

void RegisterBuiltInProviders()
{
	// Function-local statics avoid destruction and static-init ordering problems.
	static GameSpyProviderFactory theGameSpyFactory;
	RegisterOnlineProvider( &theGameSpyFactory );
}

void SetCommandLineOnlineProvider( const char *name )
{
	if (name == nullptr)
		name = "";
	strncpy( s_commandLineProvider, name, sizeof(s_commandLineProvider) - 1 );
	s_commandLineProvider[sizeof(s_commandLineProvider) - 1] = 0;
}

const char *GetCommandLineOnlineProvider()
{
	return s_commandLineProvider;
}


static Bool matchProviderName( const char *name, OnlineProviderType& outType )
{
	if (name == nullptr || name[0] == 0)
		return FALSE;

	OnlineProviderRegistry& reg = getRegistry();
	for (Int i = 0; i < reg.m_count; ++i)
	{
		OnlineProviderFactory *f = reg.m_factories[i];
		if (f != nullptr && f->getName() != nullptr && stricmp(f->getName(), name) == 0)
		{
			outType = f->getType();
			return TRUE;
		}
	}
	return FALSE;
}

OnlineProviderType ResolveConfiguredProvider()
{
	// Built-in providers must be registered so name matching has something to match.
	RegisterBuiltInProviders();

	OnlineProviderType resolved = ONLINE_PROVIDER_GAMESPY;

	const char *cli = GetCommandLineOnlineProvider();
	if (cli != nullptr && cli[0] != 0)
	{
		if (matchProviderName( cli, resolved ))
			return resolved;

		DEBUG_LOG(("ResolveConfiguredProvider: unrecognised -onlineProvider value '%s'; using GameSpy", cli));
		return ONLINE_PROVIDER_GAMESPY;
	}

	return ONLINE_PROVIDER_GAMESPY;
}

IOnlineResults *CreateOnlineResults( OnlineProviderFactory **outFactory )
{
	if (outFactory != nullptr)
		*outFactory = nullptr;

	RegisterBuiltInProviders();
	OnlineProviderFactory *factory = GetOnlineProvider(ResolveConfiguredProvider());
	if (factory == nullptr || !factory->isAvailable())
	{
		DEBUG_LOG(("CreateOnlineResults: selected provider is unavailable"));
		return nullptr;
	}

	IOnlineResults *results = factory->createResults();
	if (results == nullptr)
	{
		DEBUG_LOG(("CreateOnlineResults: provider '%s' failed to create results", factory->getName()));
		return nullptr;
	}

	if (outFactory != nullptr)
		*outFactory = factory;
	return results;
}

Bool BindOnlineResultsProvider( OnlineProviderFactory *factory )
{
	if (factory == nullptr)
		return FALSE;

	if (s_resultsProviderBound)
	{
		if (s_resultsProvider != factory)
		{
			DEBUG_LOG(("BindOnlineResultsProvider: provider change requires engine restart"));
			return FALSE;
		}
		return TRUE;
	}

	s_resultsProvider = factory;
	s_resultsProviderBound = TRUE;
	return TRUE;
}

OnlineProviderFactory *GetBoundOnlineResultsProvider()
{
	return s_resultsProviderBound ? s_resultsProvider : nullptr;
}

void SetUpOnline( OnlineProviderType type, const char *motdBuffer, const char *configBuffer )
{
	RegisterBuiltInProviders();

	OnlineProviderFactory *factory = GetOnlineProvider(type);
	if (factory == nullptr || !factory->isAvailable())
	{
		DEBUG_LOG(("SetUpOnline: provider %d is unavailable", type));
		return;
	}

	OnlineProviderFactory *resultsFactory = GetBoundOnlineResultsProvider();
	if (resultsFactory != nullptr && resultsFactory != factory)
	{
		DEBUG_LOG(("SetUpOnline: provider change requires engine restart"));
		return;
	}

	TearDownOnline();
	factory->createServices(motdBuffer, configBuffer);
	TheOnline.m_provider = type;
	TheOnline.m_factory = factory;
}

const char *GetOnlineGameTransport()
{
	if (TheOnline.m_factory == nullptr)
		return UDP_TRANSPORT_NAME;

	TransportInfo transports[16];
	Int count = TransportFactory::getTransportCount();
	if (count > (Int)ARRAY_SIZE(transports))
		count = (Int)ARRAY_SIZE(transports);

	for (Int i = 0; i < count; ++i)
		transports[i] = TransportFactory::getTransportInfo(i);

	const Int choice = TheOnline.m_factory->chooseGameTransport(transports, count);
	if (choice < 0 || choice >= count)
	{
		DEBUG_LOG(("GetOnlineGameTransport: provider '%s' picked no transport, using " UDP_TRANSPORT_NAME, TheOnline.m_factory->getName()));
		return UDP_TRANSPORT_NAME;
	}

	DEBUG_LOG(("GetOnlineGameTransport: provider '%s' picked '%s'", TheOnline.m_factory->getName(), transports[choice].name));
	return transports[choice].name;
}

void TearDownOnline()
{
	if (TheOnline.m_factory != nullptr)
		TheOnline.m_factory->destroyServices();

	TheOnline.m_provider = ONLINE_PROVIDER_GAMESPY;
	TheOnline.m_factory = nullptr;
}
