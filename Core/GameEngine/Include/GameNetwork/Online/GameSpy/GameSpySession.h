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

// FILE: GameSpySession.h /////////////////////////////////////////////////////
// Description: Thin IOnlineSession facade over the existing GameSpy queues.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/Online/IOnlineSession.h"

class GameSpySession : public IOnlineSession
{
public:
	virtual ~GameSpySession() {}

	virtual void login( const OnlineLoginParams& params ) override;
	virtual void logout() override;
	virtual Bool isLoggedIn() override;
	virtual Bool isLoggingIn() override;
	virtual SerialAuthResult getSerialAuthResult() override;
	virtual Int getLocalProfileID() override;

	static IOnlineSession *createNewSession();
};
