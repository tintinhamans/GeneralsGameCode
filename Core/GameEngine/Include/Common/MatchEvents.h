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

// The match event bus. A game hook calls MatchEvents once and every sink gets the event: the caster sink feeds the local API
// pipe (LocalApiLive.h) and the exporter sink feeds the stats export (StatsExporter.h). Everything runs on the game logic thread,
// the pointers are only valid during the call, and a sink never changes game state, so replays and CRCs stay the same.
#pragma once

#include "Lib/BaseType.h"

class Object;
class Player;

struct MatchObjectLost
{
	Int playerIndex;		///< the player that lost the object
	const Object* victim;
	const Object* killer;
};

struct MatchObjectKilled
{
	const Object* killer;
	const Object* victim;
};

struct MatchObjectCaptured
{
	const Object* captured;
	const Player* oldOwner;
	const Player* newOwner;
};

struct MatchObjectBuilt
{
	const Object* producer;	///< the factory or dozer, NULL when unknown
	const Object* built;
};

class MatchEventSink
{
public:
	virtual ~MatchEventSink() {}
	virtual void onObjectLost(const MatchObjectLost&) {}
	virtual void onObjectKilled(const MatchObjectKilled&) {}
	virtual void onObjectCaptured(const MatchObjectCaptured&) {}
	virtual void onObjectBuilt(const MatchObjectBuilt&) {}
	/// Once per logic frame before the recorder runs, with the state at the end of the previous frame.
	virtual void onFrameSample() {}
};

/// The sinks of the bus, defined by their modules.
MatchEventSink& LocalApiLiveMatchSink();
MatchEventSink& StatsExporterMatchSink();

namespace MatchEvents
{
	void objectLost(Int playerIndex, const Object* victim, const Object* killer);
	void objectKilled(const Object* killer, const Object* victim);
	void objectCaptured(const Object* captured, const Player* oldOwner, const Player* newOwner);
	void objectBuilt(const Object* producer, const Object* built);
	void frameSample();
}
