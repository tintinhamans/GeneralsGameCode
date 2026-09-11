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

#pragma once

#include "GameNetwork/Online/IOnlineMatchmaking.h"

class GameSpyMatchmaking : public IOnlineMatchmaking
{
public:
	virtual ~GameSpyMatchmaking() {}
	virtual void start( const PeerRequest& params ) override;
	virtual void widen() override;
	virtual void stop() override;

	static IOnlineMatchmaking *createNewMatchmaking();
};
