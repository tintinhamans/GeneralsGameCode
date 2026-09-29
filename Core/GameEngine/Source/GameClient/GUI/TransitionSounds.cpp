/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#include "GameClient/TransitionSounds.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "GameClient/GameWindowTransitions.h"

#include <chrono>

namespace
{
	typedef std::chrono::steady_clock Clock;

	struct PendingSound
	{
		AsciiString m_group;
		AsciiString m_sound;
		Clock::time_point m_due;
	};

	std::vector<PendingSound> s_pending;

	const Real TRANSITION_FRAMES_PER_SECOND = 30.0f;

	void fire(const AsciiString &sound)
	{
		if (!TheAudio)
			return;
		AudioEventRTS event(sound);
		TheAudio->addAudioEvent(&event);
	}
}

Int TransitionSounds::play(const char *group, Bool reversed, Int delayFrames)
{
	if (!TheTransitionHandler)
		return 0;

	TransitionSoundCues cues;
	const Int frames = TheTransitionHandler->getGroupSounds(AsciiString(group), reversed, cues);

	// Same speed the transition groups run at (TransitionGroup::update()).
	Real speed = TRANSITION_FRAMES_PER_SECOND;
	if (TheGlobalData && TheGlobalData->m_gameWindowTransitionSpeedMultiplier > 0.0f)
		speed *= TheGlobalData->m_gameWindowTransitionSpeedMultiplier;

	const Clock::time_point now = Clock::now();
	for (size_t i = 0; i < cues.size(); ++i)
	{
		PendingSound pending;
		pending.m_group = group;
		pending.m_sound = cues[i].m_sound;
		const Real seconds = (delayFrames + cues[i].m_frame) / speed;
		pending.m_due = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<Real>(seconds));
		s_pending.push_back(pending);
	}
	return frames;
}

void TransitionSounds::stop(const char *group)
{
	for (size_t i = s_pending.size(); i > 0; --i)
	{
		if (s_pending[i - 1].m_group.compareNoCase(group) == 0)
			s_pending.erase(s_pending.begin() + (i - 1));
	}
}

void TransitionSounds::update()
{
	if (s_pending.empty())
		return;

	const Clock::time_point now = Clock::now();
	for (size_t i = 0; i < s_pending.size();)
	{
		if (s_pending[i].m_due > now)
		{
			++i;
			continue;
		}
		const AsciiString sound = s_pending[i].m_sound;
		s_pending.erase(s_pending.begin() + i);
		fire(sound);
	}
}

void TransitionSounds::reset()
{
	s_pending.clear();
}
