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

// FILE: GameSpyProviderFactory.h /////////////////////////////////////////////
// Description: GameSpy implementation of OnlineProviderFactory. createServices()
//              and destroyServices() carry the SetUpGameSpy and TearDownGameSpy
//              sequence.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/Online/OnlineProviderFactory.h"

class GameSpyProviderFactory : public OnlineProviderFactory
{
public:
	virtual ~GameSpyProviderFactory() {}

	virtual OnlineProviderType getType() const override { return ONLINE_PROVIDER_GAMESPY; }
	virtual const char *getName() const override { return "GameSpy"; }
	virtual Bool isAvailable() const override { return TRUE; }

	virtual IOnlineConfig *createConfig( const char *configBuffer ) override;
	virtual IOnlineSession *createSession() override;
	virtual IOnlineChat *createChat() override;
	virtual IOnlineLobbyState *createLobbyState() override;
	virtual IOnlinePresence *createPresence() override;
	virtual IOnlineStats *createStats() override;
	virtual IOnlineLadder *createLadder() override;
	virtual IOnlineResults *createResults() override;
	virtual IOnlineMatchmaking *createMatchmaking() override;

	virtual Int chooseGameTransport( const TransportInfo *transports, Int count ) const override;

	virtual void createServices( const char *motdBuffer, const char *configBuffer ) override;
	virtual void destroyServices() override;
};
