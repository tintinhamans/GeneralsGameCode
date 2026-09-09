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

// FILE: GameSpySession.cpp ///////////////////////////////////////////////////
// GenMP online provider abstraction (Phase 1).
// Description: Thin facade preserving the existing PeerRequest login path.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/GameSpy/GameSpySession.h"

#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"

IOnlineSession *TheOnlineSession = nullptr;

IOnlineSession *GameSpySession::createNewSession()
{
	return NEW GameSpySession;
}

void GameSpySession::login( const OnlineLoginParams& params )
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return;

	PeerRequest request;
	request.peerRequestType = PeerRequest::PEERREQUEST_LOGIN;
	request.nick = params.m_nick ? params.m_nick : "";
	request.password = params.m_password ? params.m_password : "";
	request.email = params.m_email ? params.m_email : "";
	request.login.profileID = params.m_profileID;
	TheGameSpyPeerMessageQueue->addRequest(request);
}

void GameSpySession::logout()
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return;

	PeerRequest request;
	request.peerRequestType = PeerRequest::PEERREQUEST_LOGOUT;
	TheGameSpyPeerMessageQueue->addRequest(request);
}

Bool GameSpySession::isLoggedIn()
{
	return TheGameSpyPeerMessageQueue && TheGameSpyPeerMessageQueue->isConnected();
}

Bool GameSpySession::isLoggingIn()
{
	return TheGameSpyPeerMessageQueue && TheGameSpyPeerMessageQueue->isConnecting();
}

SerialAuthResult GameSpySession::getSerialAuthResult()
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return SERIAL_NONEXISTENT;

	return TheGameSpyPeerMessageQueue->getSerialAuthResult();
}

Int GameSpySession::getLocalProfileID()
{
	if (TheGameSpyInfo == nullptr)
		return 0;

	return TheGameSpyInfo->getLocalProfileID();
}
