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

// FILE: OnlineProviderFactory.h //////////////////////////////////////////////
// GenMP online provider abstraction (Phase 1).
// Description: Provider factory base class, a VC6-safe provider registry, and
//              the SetUpOnline / TearDownOnline lifecycle seam. Zero observable
//              behavior change: GameSpy is the only registered provider.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Online/OnlineProviderType.h"

//-----------------------------------------------------------------------------
// OnlineProviderFactory
//
// One provider contributes one factory. The per-service create*() virtuals are
// added slice by slice through Phase 1. createServices() / destroyServices()
// carry the ordered lifecycle; the GameSpy implementation body is the verbatim
// legacy SetUpGameSpy / TearDownGameSpy sequence.
//-----------------------------------------------------------------------------
class OnlineProviderFactory
{
public:
	virtual ~OnlineProviderFactory() {}

	virtual OnlineProviderType getType() const = 0;
	virtual const char *getName() const = 0;			// e.g. "GameSpy"
	virtual Bool isAvailable() const = 0;

	// Per-service create*() virtuals are introduced by later Phase 1 slices.

	// Ordered lifecycle. GameSpy body == verbatim legacy SetUpGameSpy sequence.
	virtual void createServices( const char *motdBuffer, const char *configBuffer ) = 0;
	virtual void destroyServices() = 0;
};

//-----------------------------------------------------------------------------
// The currently active online provider. POD, statically zero-initialized so it
// is safe under VC6 without namespace-scope constructors.
//-----------------------------------------------------------------------------
struct OnlineRuntime
{
	OnlineProviderType m_provider;
	OnlineProviderFactory *m_factory;
};

extern OnlineRuntime TheOnline;

enum { MAX_ONLINE_PROVIDERS = 8 };

//-----------------------------------------------------------------------------
// Registry + resolution. The registry is a function-local static (no
// namespace-scope STLport container). Providers are entered by an explicit
// RegisterBuiltInProviders() call, never by self-registering global ctors.
//-----------------------------------------------------------------------------
void RegisterOnlineProvider( OnlineProviderFactory *factory );
OnlineProviderFactory *GetOnlineProvider( OnlineProviderType type );		// null if unregistered
void RegisterBuiltInProviders();

// CLI '-onlineProvider <name>' override, stashed by CommandLine.cpp and read by
// ResolveConfiguredProvider(). Empty string means "not supplied".
void SetCommandLineOnlineProvider( const char *name );
const char *GetCommandLineOnlineProvider();

// Precedence: CLI '-onlineProvider' > GameSpy.
// Unrecognised names degrade to GameSpy with one log line. Safe to call before
// the online subsystem is set up.
OnlineProviderType ResolveConfiguredProvider();

//-----------------------------------------------------------------------------
// Lifecycle seam. SetUpGameSpy / TearDownGameSpy become thin wrappers over
// these in slice 3; the bodies are stubs in slice 1.
//-----------------------------------------------------------------------------
void SetUpOnline( OnlineProviderType type, const char *motdBuffer, const char *configBuffer );
void TearDownOnline();
