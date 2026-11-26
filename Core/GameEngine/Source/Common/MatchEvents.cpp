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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/MatchEvents.h"

namespace
{
	// The sinks share no state, so their order changes neither the pipe output nor the export.
	template <typename Fn>
	void forEachSink(Fn fn)
	{
		fn(LocalApiLiveMatchSink());
		fn(StatsExporterMatchSink());
	}
}

namespace MatchEvents
{
	void objectLost(Int playerIndex, const Object* victim, const Object* killer)
	{
		const MatchObjectLost event = { playerIndex, victim, killer };
		forEachSink([&event](MatchEventSink& sink) { sink.onObjectLost(event); });
	}

	void objectKilled(const Object* killer, const Object* victim)
	{
		const MatchObjectKilled event = { killer, victim };
		forEachSink([&event](MatchEventSink& sink) { sink.onObjectKilled(event); });
	}

	void objectCaptured(const Object* captured, const Player* oldOwner, const Player* newOwner)
	{
		const MatchObjectCaptured event = { captured, oldOwner, newOwner };
		forEachSink([&event](MatchEventSink& sink) { sink.onObjectCaptured(event); });
	}

	void objectBuilt(const Object* producer, const Object* built)
	{
		const MatchObjectBuilt event = { producer, built };
		forEachSink([&event](MatchEventSink& sink) { sink.onObjectBuilt(event); });
	}

	void frameSample()
	{
		forEachSink([](MatchEventSink& sink) { sink.onFrameSample(); });
	}
}
