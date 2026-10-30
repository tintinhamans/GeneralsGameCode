/*
**	Command & Conquer Generals Zero Hour(tm)
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

// Protocol reference: docs/stats-exporter.md
#include "Common/StatsExporterControl.h"
#include "Common/StatsExporterPipe.h"

#include "Common/GameCommon.h"
#include "Common/GameUtility.h"
#include "Common/GlobalData.h"
#include "Common/FramePacer.h"
#include "Common/FrameRateLimit.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "GameClient/ControlBar.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameNetwork/NetworkInterface.h"

#include <float.h>
#include <math.h>
#include <string>

#include "StatsExporter.pb.h"

namespace
{
	typedef statsexporter::ResponseStatus Status;

	const UnsignedInt kMaxTransitionMs = 60000;
	const Real kPi = 3.14159265358979f;

	// A transition made by a pipe command. It runs on wall-clock time from the render loop, so a paused game still moves the camera.
	struct Transition
	{
		Bool active;
		UnsignedInt64 startMicros;
		UnsignedInt64 durationMicros;
		Bool easeInOut;
		Bool hasPosition;
		Bool hasZoom;
		Bool hasAngle;
		Bool hasPitch;
		Real x0, y0, x1, y1;
		Real zoom0, zoom1;
		Real angle0, angleDelta;
		Real pitch0, pitch1;

		Transition()
			: active(FALSE), startMicros(0), durationMicros(0), easeInOut(FALSE), hasPosition(FALSE), hasZoom(FALSE), hasAngle(FALSE), hasPitch(FALSE)
			, x0(0.0f), y0(0.0f), x1(0.0f), y1(0.0f), zoom0(0.0f), zoom1(0.0f), angle0(0.0f), angleDelta(0.0f), pitch0(0.0f), pitch1(0.0f)
		{
		}
	};

	Transition s_transition;

	// The lock a pipe command created, and how it was made.
	ObjectID s_followId = INVALID_ID;
	int s_followMode = statsexporter::CAMERA_FOLLOW_NONE;

	// The view's user action count when the last pipe command ran: a later user action cancels the transition and ends the lock.
	UnsignedInt s_countAtCommand = 0;
	// ... and when the last Camera frame went out.
	UnsignedInt s_countAtSent = 0;
	Bool s_haveCountAtSent = FALSE;
	Bool s_pipeSinceSent = FALSE;
	int s_pendingEnd = statsexporter::CAMERA_FOLLOW_END_NONE;
	ObjectID s_pendingEndId = INVALID_ID;

	// The replay pacing as the game had it before the first pipe change.
	Bool s_pacingSaved = FALSE;
	Bool s_savedScaleEnabled = FALSE;
	Int s_savedScaleFps = 0;

	Bool finiteValue(Real value)
	{
		return _finite((double)value) != 0;
	}

	Real normalizedDelta(Real delta)
	{
		while (delta > kPi)
		{
			delta -= 2.0f * kPi;
		}
		while (delta < -kPi)
		{
			delta += 2.0f * kPi;
		}
		return delta;
	}

	Bool localPlayerIsObserver()
	{
		return ThePlayerList != NULL && ThePlayerList->getLocalPlayer() != NULL && ThePlayerList->getLocalPlayer()->isPlayerObserver();
	}

	Bool inReplayPlayback()
	{
		return TheGameLogic != NULL && TheGameLogic->isInReplayGame() && TheRecorder != NULL && TheRecorder->isPlaybackMode() && TheNetwork == NULL;
	}

	// Camera commands: any running match. The camera is client-side state, it cannot reach the simulation.
	Status matchGate(std::string& message)
	{
		if (TheGameLogic == NULL || ThePlayerList == NULL || TheTacticalView == NULL || !TheGameLogic->isInInteractiveGame())
		{
			message = "no match is running";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// Observer commands: an observer of a match, or a replay playback.
	Status observerGate(std::string& message)
	{
		const Status gate = matchGate(message);
		if (gate != statsexporter::RESPONSE_STATUS_OK)
		{
			return gate;
		}
		if (!TheGameLogic->isInReplayGame() && !localPlayerIsObserver())
		{
			message = "the local player is an active player of the match: only an observer can change the point of view";
			return statsexporter::RESPONSE_STATUS_NOT_ALLOWED;
		}
		return statsexporter::RESPONSE_STATUS_OK;
	}

	void noteCommand()
	{
		s_countAtCommand = TheTacticalView->getUserActionCount();
		s_pipeSinceSent = TRUE;
	}

	void fillCameraState(statsexporter::CameraState& out)
	{
		const Coord3D position = TheTacticalView->getPosition();
		out.set_x(position.x);
		out.set_y(position.y);
		out.set_zoom(TheTacticalView->getZoom());
		out.set_angle(TheTacticalView->getAngle());
		out.set_pitch(TheTacticalView->getPitch());
		const ObjectID lock = TheTacticalView->getCameraLock();
		out.set_follow_object_id(lock);
		out.set_follow_mode(lock == INVALID_ID ? statsexporter::CAMERA_FOLLOW_NONE
			: ((lock == s_followId && s_followMode == statsexporter::CAMERA_FOLLOW_TETHER) ? statsexporter::CAMERA_FOLLOW_TETHER : statsexporter::CAMERA_FOLLOW_FOLLOW));
		out.set_transition_active(s_transition.active);
	}

	// Lets go of whatever the camera is locked on; a lock this module made reports its end.
	void releaseLock(int reason)
	{
		if (TheTacticalView != NULL && TheTacticalView->getCameraLock() != INVALID_ID)
		{
			TheTacticalView->setCameraLock(INVALID_ID);
			TheTacticalView->setCameraLockDrawable(NULL);
		}
		if (s_followId != INVALID_ID)
		{
			s_pendingEnd = reason;
			s_pendingEndId = s_followId;
			s_followId = INVALID_ID;
			s_followMode = statsexporter::CAMERA_FOLLOW_NONE;
		}
	}

	void applyTransition(const Transition& t, Real fraction)
	{
		View* view = TheTacticalView;
		if (t.hasPosition)
		{
			Coord3D target;
			target.x = t.x0 + (t.x1 - t.x0) * fraction;
			target.y = t.y0 + (t.y1 - t.y0) * fraction;
			target.z = 0.0f;
			view->lookAt(&target);
		}
		if (t.hasAngle)
		{
			view->setAngle(t.angle0 + t.angleDelta * fraction);
		}
		if (t.hasPitch)
		{
			view->setPitch(t.pitch0 + (t.pitch1 - t.pitch0) * fraction);
		}
		if (t.hasZoom)
		{
			view->setZoom(t.zoom0 + (t.zoom1 - t.zoom0) * fraction);
		}
	}

	void stepTransition()
	{
		const UnsignedInt64 now = StatsExporterNowMicros();
		Real fraction = 1.0f;
		if (s_transition.durationMicros > 0 && now < s_transition.startMicros + s_transition.durationMicros)
		{
			fraction = (Real)(now - s_transition.startMicros) / (Real)s_transition.durationMicros;
		}
		const Bool last = fraction >= 1.0f;
		if (s_transition.easeInOut && !last)
		{
			fraction = fraction * fraction * (3.0f - 2.0f * fraction);
		}
		applyTransition(s_transition, fraction);
		s_pipeSinceSent = TRUE;
		if (last)
		{
			s_transition.active = FALSE;
		}
	}

	// Pacing a replay playback: remembered once so that the next game does not inherit a slowed-down clock.
	void savePacing()
	{
		if (!s_pacingSaved && TheFramePacer != NULL)
		{
			s_pacingSaved = TRUE;
			s_savedScaleEnabled = TheFramePacer->isLogicTimeScaleEnabled();
			s_savedScaleFps = TheFramePacer->getLogicTimeScaleFps();
		}
	}

	Real effectiveSpeedRatio()
	{
		if (TheFramePacer->isLogicTimeScaleEnabled())
		{
			return (Real)TheFramePacer->getLogicTimeScaleFps() / (Real)LOGICFRAMES_PER_SECOND;
		}
		const Int limit = TheFramePacer->getActualFramesPerSecondLimit();
		return (limit >= (Int)RenderFpsPreset::UncappedFpsValue) ? 0.0f : (Real)limit / (Real)LOGICFRAMES_PER_SECOND;
	}
}

void StatsExporterControlCameraFrame(StatsExporterCameraReport& report)
{
	if (TheTacticalView == NULL)
	{
		return;
	}
	View* view = TheTacticalView;
	const UnsignedInt count = view->getUserActionCount();

	// A human touching the camera ends whatever a command had running.
	if (s_transition.active)
	{
		if (count != s_countAtCommand)
		{
			s_transition.active = FALSE;
		}
		else
		{
			stepTransition();
		}
	}

	if (s_followId != INVALID_ID)
	{
		const Object* followed = (TheGameLogic != NULL) ? TheGameLogic->findObjectByID(s_followId) : NULL;
		const Bool gone = followed == NULL || followed->isEffectivelyDead();
		if (view->getCameraLock() != s_followId)
		{
			releaseLock(gone ? statsexporter::CAMERA_FOLLOW_END_OBJECT_GONE : statsexporter::CAMERA_FOLLOW_END_USER);
		}
		else if (gone)
		{
			releaseLock(statsexporter::CAMERA_FOLLOW_END_OBJECT_GONE);
		}
	}

	report.userInputCount = count;
	const Bool userSince = s_haveCountAtSent && count != s_countAtSent;
	report.changeSource = userSince ? statsexporter::CAMERA_CHANGE_USER
		: (s_pipeSinceSent ? statsexporter::CAMERA_CHANGE_PIPE : statsexporter::CAMERA_CHANGE_OTHER);
	const ObjectID lock = view->getCameraLock();
	report.followObjectId = lock;
	report.followMode = (lock == INVALID_ID) ? statsexporter::CAMERA_FOLLOW_NONE
		: ((lock == s_followId && s_followMode == statsexporter::CAMERA_FOLLOW_TETHER) ? statsexporter::CAMERA_FOLLOW_TETHER : statsexporter::CAMERA_FOLLOW_FOLLOW);
	report.followEnded = s_pendingEnd;
	report.followEndedObjectId = s_pendingEndId;
	report.transitionActive = s_transition.active;
}

void StatsExporterControlCameraSent(const StatsExporterCameraReport& report)
{
	s_countAtSent = report.userInputCount;
	s_haveCountAtSent = TRUE;
	s_pipeSinceSent = s_transition.active;
	s_pendingEnd = statsexporter::CAMERA_FOLLOW_END_NONE;
	s_pendingEndId = INVALID_ID;
}

void StatsExporterControlReset()
{
	s_transition.active = FALSE;
	s_followId = INVALID_ID;
	s_followMode = statsexporter::CAMERA_FOLLOW_NONE;
	s_pendingEnd = statsexporter::CAMERA_FOLLOW_END_NONE;
	s_pendingEndId = INVALID_ID;
	s_pipeSinceSent = FALSE;
	s_haveCountAtSent = FALSE;
	if (s_pacingSaved && TheFramePacer != NULL)
	{
		TheFramePacer->setLogicTimeScaleFps(s_savedScaleFps);
		TheFramePacer->enableLogicTimeScale(s_savedScaleEnabled);
	}
	s_pacingSaved = FALSE;
}

int StatsExporterControlCamera(const statsexporter::CameraControlQuery& query, std::string& result, std::string& message)
{
	const Status gate = matchGate(message);
	if (gate != statsexporter::RESPONSE_STATUS_OK)
	{
		return gate;
	}

	if (query.has_x() != query.has_y())
	{
		message = "x and y go together";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}
	if ((query.has_x() && (!finiteValue(query.x()) || !finiteValue(query.y()) || fabsf(query.x()) > 1000000.0f || fabsf(query.y()) > 1000000.0f))
		|| (query.has_zoom() && (!finiteValue(query.zoom()) || query.zoom() < 0.05f || query.zoom() > 20.0f))
		|| (query.has_angle() && (!finiteValue(query.angle()) || fabsf(query.angle()) > 100.0f))
		|| (query.has_pitch() && (!finiteValue(query.pitch()) || fabsf(query.pitch()) > kPi)))
	{
		message = "a value is not finite or out of range";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}
	if (query.duration_ms() > kMaxTransitionMs)
	{
		message = "duration_ms must be 60000 at most";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}
	if (!query.reset() && !query.cancel_transition() && !query.has_x() && !query.has_zoom() && !query.has_angle() && !query.has_pitch())
	{
		message = "nothing to do";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}

	View* view = TheTacticalView;
	if (query.cancel_transition())
	{
		s_transition.active = FALSE;
	}

	// A new command replaces a running transition; the values it does not mention stay where they are now.
	Transition t;
	t.easeInOut = query.easing() == statsexporter::CAMERA_EASING_EASE_IN_OUT;
	t.durationMicros = (UnsignedInt64)query.duration_ms() * 1000;
	const Coord3D start = view->getPosition();
	t.x0 = start.x;
	t.y0 = start.y;
	t.zoom0 = view->getZoom();
	t.angle0 = view->getAngle();
	t.pitch0 = view->getPitch();
	t.x1 = t.x0;
	t.y1 = t.y0;
	t.zoom1 = t.zoom0;
	t.pitch1 = t.pitch0;
	Real angle1 = t.angle0;

	if (query.reset())
	{
		// The game's own defaults, read by applying them and putting the camera back; the height is restored exactly, the zoom follows it.
		const Real height = view->getHeightAboveGround();
		view->setAngleToDefault();
		view->setPitchToDefault();
		view->setZoomToDefault();
		angle1 = view->getAngle();
		t.pitch1 = view->getPitch();
		t.zoom1 = view->getZoom();
		t.hasAngle = TRUE;
		t.hasPitch = TRUE;
		t.hasZoom = TRUE;
		if (query.duration_ms() > 0)
		{
			view->setAngle(t.angle0);
			view->setPitch(t.pitch0);
			view->setZoom(t.zoom0);
			view->setHeightAboveGround(height);
		}
	}
	if (query.has_zoom())
	{
		t.zoom1 = query.zoom();
		t.hasZoom = TRUE;
	}
	if (query.has_angle())
	{
		angle1 = query.angle();
		t.hasAngle = TRUE;
	}
	if (query.has_pitch())
	{
		t.pitch1 = query.pitch();
		t.hasPitch = TRUE;
	}
	if (query.has_x())
	{
		// A position and a lock cannot both steer the camera.
		releaseLock(statsexporter::CAMERA_FOLLOW_END_COMMAND);
		t.x1 = query.x();
		t.y1 = query.y();
		t.hasPosition = TRUE;
	}
	t.angleDelta = normalizedDelta(angle1 - t.angle0);

	if (query.duration_ms() == 0)
	{
		s_transition.active = FALSE;
		applyTransition(t, 1.0f);
	}
	else
	{
		t.startMicros = StatsExporterNowMicros();
		t.active = TRUE;
		s_transition = t;
	}
	noteCommand();

	statsexporter::CameraState out;
	fillCameraState(out);
	out.SerializeToString(&result);
	return statsexporter::RESPONSE_STATUS_OK;
}

int StatsExporterControlFollow(const statsexporter::CameraFollowQuery& query, std::string& result, std::string& message)
{
	const Status gate = matchGate(message);
	if (gate != statsexporter::RESPONSE_STATUS_OK)
	{
		return gate;
	}

	View* view = TheTacticalView;
	if (query.object_id() == 0)
	{
		releaseLock(statsexporter::CAMERA_FOLLOW_END_COMMAND);
		noteCommand();
	}
	else
	{
		const Bool tether = query.mode() == statsexporter::CAMERA_FOLLOW_TETHER;
		if (query.mode() != statsexporter::CAMERA_FOLLOW_FOLLOW && !tether)
		{
			message = "mode must be FOLLOW or TETHER";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}
		if (tether && (!finiteValue(query.tether_distance()) || query.tether_distance() <= 0.0f || query.tether_distance() > 100.0f))
		{
			message = "tether_distance must be above 0 and at most 100";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}
		const ObjectID id = (ObjectID)query.object_id();
		const Object* target = TheGameLogic->findObjectByID(id);
		if (target == NULL || target->isEffectivelyDead())
		{
			message = "no such object";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}

		releaseLock(statsexporter::CAMERA_FOLLOW_END_COMMAND);
		s_transition.active = FALSE;
		view->setCameraLock(id);
		if (view->getCameraLock() != id)
		{
			message = "the game does not allow camera movement";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}
		if (tether)
		{
			view->setSnapMode(View::LOCK_TETHER, query.tether_distance());
		}
		else if (query.snap())
		{
			view->snapToCameraLock();
		}
		s_followId = id;
		s_followMode = tether ? statsexporter::CAMERA_FOLLOW_TETHER : statsexporter::CAMERA_FOLLOW_FOLLOW;
		noteCommand();
	}

	statsexporter::CameraState out;
	fillCameraState(out);
	out.SerializeToString(&result);
	return statsexporter::RESPONSE_STATUS_OK;
}

int StatsExporterControlReplay(const statsexporter::ReplayControlQuery& query, std::string& result, std::string& message)
{
	if (!inReplayPlayback() || TheFramePacer == NULL)
	{
		message = "replay controls work during a replay playback only: pausing or changing the pace of a live game would break it";
		return statsexporter::RESPONSE_STATUS_NOT_ALLOWED;
	}
	if (query.has_speed_ratio() && (!finiteValue(query.speed_ratio()) || query.speed_ratio() < 0.1f || query.speed_ratio() > 16.0f))
	{
		message = "speed_ratio must be 0.1 to 16";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}
	if (!query.has_paused() && !query.has_speed_ratio() && !query.has_fast_forward())
	{
		message = "nothing to do";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}

	Bool capped = FALSE;
	if (query.has_fast_forward())
	{
		TheWritableGlobalData->m_TiVOFastMode = query.fast_forward() ? TRUE : FALSE;
	}
	if (query.has_speed_ratio())
	{
		savePacing();
		if (!query.has_fast_forward())
		{
			TheWritableGlobalData->m_TiVOFastMode = FALSE;
		}

		// The logic runs once per displayed frame at most, so the pace cannot exceed the frame rate limit; the game's fast forward goes beyond.
		Int fps = (Int)(query.speed_ratio() * (Real)LOGICFRAMES_PER_SECOND + 0.5f);
		const Int limit = TheFramePacer->getActualFramesPerSecondLimit();
		if (fps > limit)
		{
			fps = limit;
			capped = TRUE;
		}
		if (fps < 1)
		{
			fps = 1;
		}
		TheFramePacer->setLogicTimeScaleFps(fps);
		TheFramePacer->enableLogicTimeScale(fps < limit);
	}
	if (query.has_paused())
	{
		TheGameLogic->setGamePaused(query.paused() ? TRUE : FALSE, FALSE, FALSE);
	}

	statsexporter::ReplayState out;
	out.set_paused(TheGameLogic->isGamePaused() != FALSE);
	out.set_speed_ratio(effectiveSpeedRatio());
	out.set_fast_forward(TheGlobalData->m_TiVOFastMode != FALSE);
	out.set_speed_capped(capped);
	out.SerializeToString(&result);
	return statsexporter::RESPONSE_STATUS_OK;
}

int StatsExporterControlObserve(const statsexporter::ObserveQuery& query, std::string& result, std::string& message)
{
	const Status gate = observerGate(message);
	if (gate != statsexporter::RESPONSE_STATUS_OK)
	{
		return gate;
	}
	if (TheControlBar == NULL)
	{
		message = "the control bar is not available";
		return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
	}

	Player* player = NULL;
	if (query.player_index() >= 0)
	{
		player = (query.player_index() < ThePlayerList->getPlayerCount()) ? ThePlayerList->getNthPlayer(query.player_index()) : NULL;
		if (player == NULL || player == ThePlayerList->getNeutralPlayer() || player->isPlayerObserver())
		{
			message = "no such player in the match";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}
	}
	else if (query.player_index() != -1)
	{
		message = "player_index must be a player or -1";
		return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
	}

	// The call the observer panel's player buttons make.
	rts::changeObservedPlayer(player);

	statsexporter::ObserveState out;
	const Player* looked = TheControlBar->getObserverLookAtPlayer();
	out.set_player_index(looked != NULL ? looked->getPlayerIndex() : -1);
	out.set_full_view(player != NULL && TheControlBar->getObservedPlayer() == player);
	out.SerializeToString(&result);
	return statsexporter::RESPONSE_STATUS_OK;
}
