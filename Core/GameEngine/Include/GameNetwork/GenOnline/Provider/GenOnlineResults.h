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

// FILE: GenOnlineResults.h ///////////////////////////////////////////////////
// Description: Game results queue of the GenOnline provider.
//              GenOnline reports match results through its own services, so
//              this queue starts no threads and sends nothing.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/Online/IOnlineResults.h"

class GenOnlineResults : public IOnlineResults
{
public:
	virtual ~GenOnlineResults() override {}

	virtual void init() override {}
	virtual void reset() override {}
	virtual void update() override {}

	virtual void startThreads() override {}
	virtual void endThreads() override {}
	virtual Bool areThreadsRunning() override { return FALSE; }

	virtual void addRequest( const GameResultsRequest& req ) override {}
	virtual Bool getRequest( GameResultsRequest& resp ) override { return FALSE; }

	virtual void addResponse( const GameResultsResponse& resp ) override {}
	virtual Bool getResponse( GameResultsResponse& resp ) override { return FALSE; }

	virtual Bool areGameResultsBeingSent() override { return FALSE; }
};
