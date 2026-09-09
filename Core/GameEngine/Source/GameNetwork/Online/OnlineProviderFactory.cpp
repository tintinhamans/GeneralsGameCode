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
// GenMP online provider abstraction.
// Description: Function-local-static provider registry, TheOnline runtime, and
//              the SetUpOnline / TearDownOnline lifecycle seam.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/OnlineProviderFactory.h"

//-----------------------------------------------------------------------------
// PUBLIC DATA
//-----------------------------------------------------------------------------

// POD, statically zero-initialized. No namespace-scope constructor runs.
OnlineRuntime TheOnline = { ONLINE_PROVIDER_GAMESPY, nullptr };

//-----------------------------------------------------------------------------
// PRIVATE DATA
//-----------------------------------------------------------------------------

// Stash for the CLI '-onlineProvider <name>' override. POD, zero-initialized.
static char s_commandLineProvider[64] = { 0 };

//-----------------------------------------------------------------------------
// Registry
//
// A fixed-size array wrapped in a function-local static (Meyers singleton).
// This deliberately avoids any namespace-scope STLport container so there is no
// static-initialization-order hazard under VC6.
//-----------------------------------------------------------------------------
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

	// Replace an existing entry for the same type instead of duplicating it,
	// which keeps RegisterBuiltInProviders() idempotent.
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
	// Slice 1 stub. The GameSpy factory is registered here in the GameSpy
	// provider commit (task 1.6). Kept as an explicit call site so registration
	// never depends on static-init order.
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

//-----------------------------------------------------------------------------
// Provider resolution
//-----------------------------------------------------------------------------

// Case-insensitive lookup of a registered provider by name. Returns true and
// fills 'outType' on a match.
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
	// Safe to call before the online subsystem is set up: make sure the built-in
	// providers are registered so name matching has something to match against.
	RegisterBuiltInProviders();

	OnlineProviderType resolved = ONLINE_PROVIDER_GAMESPY;

	// 1) CLI: -onlineProvider <name>
	const char *cli = GetCommandLineOnlineProvider();
	if (cli != nullptr && cli[0] != 0)
	{
		if (matchProviderName( cli, resolved ))
			return resolved;

		DEBUG_LOG(("ResolveConfiguredProvider: unrecognised -onlineProvider value '%s'; using GameSpy", cli));
		return ONLINE_PROVIDER_GAMESPY;
	}

	// 2) Built-in default
	return ONLINE_PROVIDER_GAMESPY;
}

void SetUpOnline( OnlineProviderType type, const char *motdBuffer, const char *configBuffer )
{
	// Slice 1 stub. Implemented in slice 3 (SetUpOnline / TearDownOnline
	// wrappers). Defined here now so the lifecycle symbol exists without
	// touching any live call site.
	(void)type;
	(void)motdBuffer;
	(void)configBuffer;
}

void TearDownOnline()
{
	// Slice 1 stub. Implemented in slice 3.
}
