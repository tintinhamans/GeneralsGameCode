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

// FILE: IOnlineSession.h /////////////////////////////////////////////////////
// GenMP online provider abstraction.
// Description: Provider-neutral login, logout, and local identity contract.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/GameSpy/PeerThread.h"

// POD login request which maps directly to the existing PeerRequest fields.
struct OnlineLoginParams
{
	const char *m_nick;
	const char *m_password;
	const char *m_email;
	Int m_profileID;
};

class IOnlineSession
{
public:
	virtual ~IOnlineSession() {}

	virtual void login( const OnlineLoginParams& params ) = 0;
	virtual void logout() = 0;
	virtual Bool isLoggedIn() = 0;
	virtual Bool isLoggingIn() = 0;
	virtual SerialAuthResult getSerialAuthResult() = 0;
	virtual Int getLocalProfileID() = 0;
};

extern IOnlineSession *TheOnlineSession;
