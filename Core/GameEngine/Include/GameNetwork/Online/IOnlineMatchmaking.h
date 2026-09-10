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

// Command-only compatibility facade. Response ownership remains with existing consumers.
#pragma once

class PeerRequest;

// Reuses the existing queue vocabulary; start copies the complete request and
// sets its command type. No borrowed buffers or response ownership are added.
class IOnlineMatchmaking
{
public:
	virtual ~IOnlineMatchmaking() {}
	virtual void start( const PeerRequest& params ) = 0;
	virtual void widen() = 0;
	virtual void stop() = 0;
};

extern IOnlineMatchmaking *TheOnlineMatchmaking;
