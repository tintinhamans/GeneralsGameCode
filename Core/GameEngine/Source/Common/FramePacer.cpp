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
#include "PreRTS.h"

#include "Common/FramePacer.h"
#include "Common/Recorder.h"
#include "Common/MessageStream.h"
#include "GameNetwork/Caster/Caster.h"

#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"


static CommandFrameSource* passiveFrameSource()
{
	return (TheGameLogic != nullptr && TheGameLogic->isInCasterGame())
		? TheGameLogic->getCommandFrameSource() : nullptr;
}

FramePacer* TheFramePacer = nullptr;

FramePacer::FramePacer()
{
	// Set the time slice size to 1 ms.
	timeBeginPeriod(1);

	m_maxFPS = BaseFps;
	m_logicTimeScaleFPS = LOGICFRAMES_PER_SECOND;
	m_updateTime = 1.0f / (Real)BaseFps; // initialized to something to avoid division by zero on first use
	m_enableFpsLimit = FALSE;
	m_enableLogicTimeScale = FALSE;
	m_isTimeFrozen = FALSE;
	m_isGameHalted = FALSE;
	m_liveReplayCatchUp = FALSE;
}

FramePacer::~FramePacer()
{
	// Restore the previous time slice for Windows.
	timeEndPeriod(1);
}

void FramePacer::update()
{
	// Sample once, so render and logic pacing use the same backlog decision.
	// Reuse ordinary engine ticks (including message/CRC propagation), rather
	// than running GameLogic twice within a render update.
	CommandFrameSource* const passiveSource = passiveFrameSource();
	m_liveReplayCatchUp = !m_isTimeFrozen && !m_isGameHalted
		&& passiveSource != nullptr && passiveSource->needsCatchUp(TheGameLogic->getFrame());

	// TheSuperHackers @bugfix xezon 05/08/2025 Re-implements the frame rate limiter
	// with higher resolution counters to cap the frame rate more accurately to the desired limit.
	const UnsignedInt maxFps = getActualFramesPerSecondLimit();// allowFpsLimit ? getFramesPerSecondLimit() : RenderFpsPreset::UncappedFpsValue;
	m_updateTime = m_frameRateLimit.wait(maxFps);
}

void FramePacer::reset()
{
	m_liveReplayCatchUp = FALSE;
	m_frameRateLimit.reset();
	m_updateTime = 1.0f / (Real)getActualFramesPerSecondLimit();
}

void FramePacer::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("FramePacer::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;
}

Int FramePacer::getFramesPerSecondLimit()  const
{
	return m_maxFPS;
}

void FramePacer::enableFramesPerSecondLimit( Bool enable )
{
	m_enableFpsLimit = enable;
}

Bool FramePacer::isFramesPerSecondLimitEnabled() const
{
	return m_enableFpsLimit;
}

Bool FramePacer::isActualFramesPerSecondLimitEnabled() const
{
	// Live casters use LAN rendering policy without changing saved limits.
	if (passiveFrameSource() != nullptr) return FALSE;
	Bool allowFpsLimit = true;

	if (TheTacticalView != nullptr)
	{
		allowFpsLimit &= TheTacticalView->getTimeMultiplier()<=1 && !TheScriptEngine->isTimeFast();
	}

	if (TheGameLogic != nullptr)
	{
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		allowFpsLimit &= !(!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode);
#else	//always allow this cheat key if we're in a replay game.
		allowFpsLimit &= !(!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame());
#endif
	}

	allowFpsLimit &= TheGlobalData->m_useFpsLimit;
	allowFpsLimit &= isFramesPerSecondLimitEnabled();

	return allowFpsLimit;
}

Int FramePacer::getActualFramesPerSecondLimit() const
{
	return isActualFramesPerSecondLimitEnabled() ? getFramesPerSecondLimit() : RenderFpsPreset::UncappedFpsValue;
}

Real FramePacer::getUpdateTime()  const
{
	return m_updateTime;
}

Real FramePacer::getUpdateFps()  const
{
	return 1.0f / m_updateTime;
}

Real FramePacer::getBaseOverUpdateFpsRatio(Real minUpdateFps)
{
	// Update fps is floored to default 5 fps, 200 ms.
	// Useful to prevent insane ratios on frame spikes/stalls.
	return (Real)BaseFps / std::max(getUpdateFps(), minUpdateFps);
}

void FramePacer::setTimeFrozen(Bool frozen)
{
	m_isTimeFrozen = frozen;
}

void FramePacer::setGameHalted(Bool halted)
{
	m_isGameHalted = halted;
}

Bool FramePacer::isTimeFrozen() const
{
	return m_isTimeFrozen;
}

Bool FramePacer::isGameHalted() const
{
	return m_isGameHalted;
}

void FramePacer::setLogicTimeScaleFps( Int fps )
{
	m_logicTimeScaleFPS = fps;
}

Int FramePacer::getLogicTimeScaleFps() const
{
	return m_logicTimeScaleFPS;
}

void FramePacer::enableLogicTimeScale( Bool enable )
{
	m_enableLogicTimeScale = enable;
}

Bool FramePacer::isLogicTimeScaleEnabled() const
{
	return m_enableLogicTimeScale;
}

Int FramePacer::getActualLogicTimeScaleFps(LogicTimeQueryFlags flags) const
{
	if (m_isTimeFrozen && (flags & IgnoreFrozenTime) == 0)
	{
		return 0;
	}

	if (m_isGameHalted && (flags & IgnoreHaltedGame) == 0)
	{
		return 0;
	}

	if (TheNetwork != nullptr)
	{
		return TheNetwork->getFrameRate();
	}

	CommandFrameSource* const passiveSource = passiveFrameSource();
	if (passiveSource != nullptr)
	{
		// Loading must finish before native command availability can stall simulation.
		if (TheGameLogic->isLoadingMap())
			return LOGICFRAMES_PER_SECOND;
		if (!passiveSource->isFrameReady(TheGameLogic->getFrame())
			&& !passiveSource->hasEnded())
		{
			// Waiting on a passive caster never touches player lockstep. Let a
			// locally queued exit proceed even while the stream is incomplete.
			Bool exiting = FALSE;
			for (GameMessage* message = TheCommandList->getFirstMessage(); message != nullptr;
				message = message->next())
			{
				if (message->getType() == GameMessage::MSG_CLEAR_GAME_DATA)
				{
					exiting = TRUE;
					break;
				}
			}
			if (!exiting)
			{
				return 0;
			}
		}
		// Keep LAN simulation at 30 Hz while rendering remains uncapped; consume
		// an already reconciled backlog with ordinary tick catch-up only.
		return LOGICFRAMES_PER_SECOND * ((m_liveReplayCatchUp && !m_isTimeFrozen
			&& !m_isGameHalted) ? 2 : 1);
	}

	if (isLogicTimeScaleEnabled())
	{
		return getLogicTimeScaleFps();
	}

	// Returns uncapped value to align with the render update as per the original game behavior.
	return RenderFpsPreset::UncappedFpsValue;
}

Real FramePacer::getActualLogicTimeScaleRatio(LogicTimeQueryFlags flags) const
{
	return (Real)getActualLogicTimeScaleFps(flags) / LOGICFRAMES_PER_SECONDS_REAL;
}

Real FramePacer::getActualLogicTimeScaleOverFpsRatio(LogicTimeQueryFlags flags) const
{
	// TheSuperHackers @info Clamps ratio to min 1, because the logic
	// frame rate is currently capped by the render frame rate.
	return min(1.0f, (Real)getActualLogicTimeScaleFps(flags) / getUpdateFps());
}

Real FramePacer::getLogicTimeStepSeconds(LogicTimeQueryFlags flags) const
{
	return SECONDS_PER_LOGICFRAME_REAL * getActualLogicTimeScaleOverFpsRatio(flags);
}

Real FramePacer::getLogicTimeStepMilliseconds(LogicTimeQueryFlags flags) const
{
	return MSEC_PER_LOGICFRAME_REAL * getActualLogicTimeScaleOverFpsRatio(flags);
}
