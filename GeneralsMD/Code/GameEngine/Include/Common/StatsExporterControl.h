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

// Protocol reference: GenOnline/docs/stats-exporter.md
// Camera, replay and observer control for the live stats pipe. Everything here runs on the main thread, moves client-side state only
// (the local camera, the frame pacing of a replay playback, the point of view of the observer) and refuses to touch the game of an active player:
// the simulation never sees a command from this module.
#pragma once

#include "Lib/BaseType.h"
#include <string>

namespace statsexporter
{
	class CameraControlQuery;
	class CameraFollowQuery;
	class ReplayControlQuery;
	class ObserveQuery;
}

/// The ints are the StatsExporter.proto enum values.
struct StatsExporterCameraReport
{
	UnsignedInt userInputCount;		///< the view's count of user camera actions
	int changeSource;				///< CameraChangeSource since the last StatsExporterControlCameraSent
	int followMode;					///< CameraFollowMode of the lock in force
	UnsignedInt followObjectId;		///< the locked unit, 0 for none
	int followEnded;				///< CameraFollowEnd of a pipe lock that ended since the last StatsExporterControlCameraSent
	UnsignedInt followEndedObjectId;
	Bool transitionActive;

	StatsExporterCameraReport()
		: userInputCount(0), changeSource(0), followMode(0), followObjectId(0), followEnded(0), followEndedObjectId(0), transitionActive(FALSE)
	{
	}
};

/// Once per rendered frame, before the Camera frame is built: advances a running transition, notices the end of a pipe lock and fills the report.
void StatsExporterControlCameraFrame(StatsExporterCameraReport& report);

/// The Camera frame carrying `report` was sent: the "since the last frame" parts start over.
void StatsExporterControlCameraSent(const StatsExporterCameraReport& report);

/// The match is over (or a new one begins): lets go of the lock, stops the transition and puts back the replay pacing this module changed.
void StatsExporterControlReset();

/// The query handlers return a statsexporter::ResponseStatus value, fill `result` with the serialized result message and `message` with the reason of a refusal.
int StatsExporterControlCamera(const statsexporter::CameraControlQuery& query, std::string& result, std::string& message);
int StatsExporterControlFollow(const statsexporter::CameraFollowQuery& query, std::string& result, std::string& message);
int StatsExporterControlReplay(const statsexporter::ReplayControlQuery& query, std::string& result, std::string& message);
int StatsExporterControlObserve(const statsexporter::ObserveQuery& query, std::string& result, std::string& message);
