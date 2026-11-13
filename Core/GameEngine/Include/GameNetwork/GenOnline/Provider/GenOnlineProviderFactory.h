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

#pragma once

#include "GameNetwork/Online/OnlineProviderFactory.h"
#include "GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h"

class GenOnlineProviderFactory : public OnlineProviderFactory
{
public:
	virtual ~GenOnlineProviderFactory() override {}

	virtual OnlineProviderType getType() const override { return ONLINE_PROVIDER_GENONLINE; }
	virtual const char *getName() const override { return "GenOnline"; }
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

	virtual IOnlineGameHooks *getGameHooks() override { return &m_gameHooks; }

	virtual void createServices( const char *motdBuffer, const char *configBuffer ) override;
	virtual void destroyServices() override;

private:
	GenOnlineGameHooks m_gameHooks;
};
