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

// Protocol reference: GenOnline/docs/local-api.md
// Camera, replay and observer control for the live stats readers. Everything here runs on the main thread, moves client-side state only
// (the local camera, the frame pacing of a replay playback, the point of view of the observer) and refuses to touch the game of an active player:
// the simulation never sees a command from this module.
#pragma once

#include "Lib/BaseType.h"
#include <string>

class Object;
class ThingTemplate;

namespace localapi
{
	class CameraControlQuery;
	class CameraFollowQuery;
	class ReplayControlQuery;
	class ObserveQuery;
}

/// The ints are the LocalApi.proto enum values.
struct LocalApiCameraReport
{
	UnsignedInt userInputCount;		///< the view's count of user camera actions
	int changeSource;				///< CameraChangeSource since the last LocalApiControlCameraSent
	int followMode;					///< CameraFollowMode of the lock in force
	UnsignedInt followObjectId;		///< the locked unit, 0 for none
	int followEnded;				///< CameraFollowEnd of a reader lock that ended since the last LocalApiControlCameraSent
	UnsignedInt followEndedObjectId;
	Bool transitionActive;

	LocalApiCameraReport()
		: userInputCount(0), changeSource(0), followMode(0), followObjectId(0), followEnded(0), followEndedObjectId(0), transitionActive(FALSE)
	{
	}
};

/// Once per rendered frame, before the Camera frame is built: advances a running transition, notices the end of a reader lock and fills the report.
void LocalApiControlCameraFrame(LocalApiCameraReport& report);

/// The Camera frame carrying `report` was sent: the "since the last frame" parts start over.
void LocalApiControlCameraSent(const LocalApiCameraReport& report);

/// The match is over (or a new one begins): lets go of the lock, stops the transition and puts back the replay pacing this module changed.
void LocalApiControlReset();

/// The local player index while the local API may only show the local player's own state, -1 when it may show everything.
/// Gated: a LAN or online match the local player plays, until the local player is defeated or the match is decided.
Int LocalApiGatedPlayerIndex();

/// TRUE when LocalApiGatedPlayerIndex() is -1.
Bool LocalApiRevealsHiddenState();

/// TRUE when nothing is gated, for own and allied objects, and for an enemy object outside any container that is neither fogged nor
/// shrouded for the local player nor invisible by stealth.
Bool LocalApiObjectVisibleToGated(const Object* obj);

/// The template the local player sees for the object, the disguise of a disguised enemy while gated, else its own template.
const ThingTemplate* LocalApiApparentTemplate(const Object* obj);

/// The query handlers return a localapi::ResponseStatus value, fill `result` with the serialized result message and `message` with the reason of a refusal.
int LocalApiControlCamera(const localapi::CameraControlQuery& query, std::string& result, std::string& message);
int LocalApiControlFollow(const localapi::CameraFollowQuery& query, std::string& result, std::string& message);
int LocalApiControlReplay(const localapi::ReplayControlQuery& query, std::string& result, std::string& message);
int LocalApiControlObserve(const localapi::ObserveQuery& query, std::string& result, std::string& message);
