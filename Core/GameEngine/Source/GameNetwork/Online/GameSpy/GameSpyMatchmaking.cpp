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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Online/GameSpy/GameSpyMatchmaking.h"
#include "GameNetwork/Online/IOnlineChat.h"

IOnlineMatchmaking *TheOnlineMatchmaking = nullptr;

IOnlineMatchmaking *GameSpyMatchmaking::createNewMatchmaking()
{
	return NEW GameSpyMatchmaking;
}

void GameSpyMatchmaking::start( const PeerRequest& params )
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return;

	PeerRequest request = params;
	request.peerRequestType = PeerRequest::PEERREQUEST_STARTQUICKMATCH;
	TheGameSpyPeerMessageQueue->addRequest(request);
}

void GameSpyMatchmaking::widen()
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return;

	PeerRequest request;
	request.peerRequestType = PeerRequest::PEERREQUEST_WIDENQUICKMATCHSEARCH;
	TheGameSpyPeerMessageQueue->addRequest(request);
}

void GameSpyMatchmaking::stop()
{
	if (TheGameSpyPeerMessageQueue == nullptr)
		return;

	PeerRequest request;
	request.peerRequestType = PeerRequest::PEERREQUEST_STOPQUICKMATCH;
	TheGameSpyPeerMessageQueue->addRequest(request);
}
