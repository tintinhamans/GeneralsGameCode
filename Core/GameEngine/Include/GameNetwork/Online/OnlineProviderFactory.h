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
//              the SetUpOnline / TearDownOnline lifecycle seam. GameSpy is
//              always registered and GenOnline builds also register GenOnline.
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
struct TransportInfo;

//-----------------------------------------------------------------------------
// OnlineProviderFactory
//
// One provider contributes one factory. createServices() and destroyServices()
// carry the ordered lifecycle of its services.
//-----------------------------------------------------------------------------
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
	// Transfers one provider-owned ladder service to TheLadderList.  Its
	// returned entries/lists remain borrowed by existing consumers until
	// online-service teardown; consumers never delete them.
	virtual IOnlineLadder *createLadder() = 0;
	// Transfers one results subsystem to the engine. The engine registers,
	// updates, resets, Xfers, and deletes exactly this instance for its lifetime.
	virtual IOnlineResults *createResults() = 0;
	virtual IOnlineMatchmaking *createMatchmaking() = 0;

	// Picks the game connection transport from the registered transports.
	// Returns an index into the transports, or -1 when none of them fits.
	virtual Int chooseGameTransport( const TransportInfo *transports, Int count ) const = 0;

	// Ordered lifecycle.
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

// Precedence: CLI '-onlineProvider' > GenOnline in GenOnline builds > GameSpy.
// Unrecognised names degrade to the default provider with one log line. Safe to call before
// the online subsystem is set up.
OnlineProviderType ResolveConfiguredProvider();

// Resolve and construct the engine-owned results subsystem before Online entry.
// A known but unavailable provider or a failed construction returns null, so the
// engine follows its existing fatal startup path rather than registering null.
IOnlineResults *CreateOnlineResults( OnlineProviderFactory **outFactory );

// Latch the provider after successful engine subsystem admission. Later Online
// setup may reuse this factory, but may not select a different one until restart.
Bool BindOnlineResultsProvider( OnlineProviderFactory *factory );
OnlineProviderFactory *GetBoundOnlineResultsProvider();

//-----------------------------------------------------------------------------
// Lifecycle seam of the online provider services.
//-----------------------------------------------------------------------------
void SetUpOnline( OnlineProviderType type, const char *motdBuffer, const char *configBuffer );

// Returns the name of the game connection transport that the active provider picks.
// Without an active provider, or when it picks none, this returns the UDP transport.
const char *GetOnlineGameTransport();
void TearDownOnline();
