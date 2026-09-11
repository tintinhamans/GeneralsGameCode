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
// GenMP online provider abstraction.
// Description: Provider factory base class, a VC6-safe provider registry, and
//              the SetUpOnline / TearDownOnline lifecycle seam. Zero observable
//              behavior change: GameSpy is the only registered provider.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Online/OnlineProviderType.h"

class IOnlineConfig;
class IOnlineSession;
class IOnlineChat;
class IOnlineLobbyState;
class IOnlinePresence;
class IOnlineStats;
class IOnlineLadder;
class IOnlineResults;
class IOnlineMatchmaking;

class OnlineProviderFactory
{
public:
	virtual ~OnlineProviderFactory() {}

	virtual OnlineProviderType getType() const = 0;
	virtual const char *getName() const = 0;			// e.g. "GameSpy"
	virtual Bool isAvailable() const = 0;

	virtual IOnlineConfig *createConfig( const char *configBuffer ) = 0;
	virtual IOnlineSession *createSession() = 0;
	virtual IOnlineChat *createChat() = 0;
	virtual IOnlineLobbyState *createLobbyState() = 0;
	virtual IOnlinePresence *createPresence() = 0;
	virtual IOnlineStats *createStats() = 0;
	// Returned ladder entries stay borrowed until online service teardown.
	virtual IOnlineLadder *createLadder() = 0;
	// The engine owns the returned results for its lifetime.
	virtual IOnlineResults *createResults() = 0;
	virtual IOnlineMatchmaking *createMatchmaking() = 0;

	virtual void createServices( const char *motdBuffer, const char *configBuffer ) = 0;
	virtual void destroyServices() = 0;
};

// POD and zero-initialized so VC6 needs no namespace-scope constructor.
struct OnlineRuntime
{
	OnlineProviderType m_provider;
	OnlineProviderFactory *m_factory;
};

extern OnlineRuntime TheOnline;

enum { MAX_ONLINE_PROVIDERS = 8 };

// Providers register through an explicit call, never through global constructors.
void RegisterOnlineProvider( OnlineProviderFactory *factory );
OnlineProviderFactory *GetOnlineProvider( OnlineProviderType type );		// null if unregistered
void RegisterBuiltInProviders();

// Set from -onlineProvider by CommandLine.cpp. Empty means not supplied.
void SetCommandLineOnlineProvider( const char *name );
const char *GetCommandLineOnlineProvider();

// Precedence: CLI '-onlineProvider' > GameSpy.
// Unrecognised names degrade to GameSpy with one log line. Safe to call before
// the online subsystem is set up.
OnlineProviderType ResolveConfiguredProvider();

// Returns null when the provider is unavailable or construction fails.
IOnlineResults *CreateOnlineResults( OnlineProviderFactory **outFactory );

// The bound provider cannot change until restart.
Bool BindOnlineResultsProvider( OnlineProviderFactory *factory );
OnlineProviderFactory *GetBoundOnlineResultsProvider();

void SetUpOnline( OnlineProviderType type, const char *motdBuffer, const char *configBuffer );
void TearDownOnline();
