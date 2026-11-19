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

// FILE: ScriptCLP.h //////////////////////////////////////////////////////////////////////////////
// Runtime state for the CLP_AI script actions and conditions ported from CookieLandProjects/CLP_AI.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Snapshot.h"

#include <map>
#include <vector>

typedef UnsignedInt TeamID;

class Condition;
class Object;
class Player;
class Team;
class ThingTemplate;
class PartitionFilter;

//-------------------------------------------------------------------------------------------------
/** Picks one object near a position by Parameter::CLOSEST, FARTHEST, RANDOM, YOUNGEST or OLDEST.
	filters is null terminated and may be null. An empty objectType accepts any object; otherwise it
	names a ThingTemplate or an ObjectTypes list. Dead and off-map objects are always rejected. */
//-------------------------------------------------------------------------------------------------
Object *clpSelectObject(const Coord3D *pos, PartitionFilter **filters, const AsciiString &objectType, Int selectionMode);

/** True if obj matches objectType, which names a ThingTemplate or an ObjectTypes list. */
Bool clpObjectMatchesType(const Object *obj, const AsciiString &objectType);

/** True if the player can see obj right now, meaning it is not shrouded or fogged for that player. */
Bool clpIsSeenByPlayer(const Object *obj, const Player *player);

/** Identifies computer players subject to command restrictions in every game mode. */
Bool clpAIPlaysFair(const Player *player);

/** True if this map enables either AI targeting restriction for the player. */
Bool clpAIUsesTargetingRestrictions(const Player *player);

/** Applies this map's AI fog and stealth targeting rules. */
Bool clpIsFairAITarget(const Object *obj, const Player *player);

//-------------------------------------------------------------------------------------------------
/** Runtime state behind the CLP_AI scripts. Saved in its own block, so Object, Team and Player keep
	their save formats. Team tracking only runs for teams a CLP condition has asked about. */
//-------------------------------------------------------------------------------------------------
class ScriptCLPState : public Snapshot
{
public:

	struct KDRatio
	{
		Real kills;
		Real deaths;
		UnsignedInt readKills[MAX_PLAYER_COUNT];		///< newest kill event added per player, not saved
		UnsignedInt readLosses[MAX_PLAYER_COUNT];		///< newest loss event added per player, not saved
	};

	struct Event
	{
		UnsignedInt seq;
		const ThingTemplate *thing;
		Coord3D pos;
		UnsignedInt frame;
	};
	typedef std::vector<Event> EventList;

	ScriptCLPState();
	virtual ~ScriptCLPState();

	void reset();

	/// Called once per logic frame after the scripts ran.
	void update();

	// Kills to deaths ratios, keyed by the qualified parameter string like counters.
	KDRatio *findKDRatio(const AsciiString &name, Bool create);

	// Kills and losses of the last few minutes, filled by Object::onDie and Object::scoreTheKill.
	void onObjectDied(const Object *victim);
	void onObjectKilled(const Player *killer, const Object *victim);
	const EventList &getPlayerKills(Int playerIndex) const;
	const EventList &getPlayerLosses(Int playerIndex) const;
	const EventList &getTeamLosses(TeamID teamID) const;
	UnsignedInt getLastEventSeq() const { return m_eventSeq; }

	/// Events with a larger seq are new to this condition. The first call starts from now.
	UnsignedInt readEvents(Condition *pCondition);

	// Buildings with a capture in progress, set by SpecialAbilityUpdate.
	Bool isBeingCaptured(const Object *obj) const;
	void setBeingCaptured(const Object *obj, Bool beingCaptured);

	// Whether AI units crush infantry: unit override, then player override, then AIData.
	Bool getCrushesInfantry(const Object *obj) const;
	void setPlayerCrushesInfantry(Int playerIndex, Bool crushes);
	void setObjectCrushesInfantry(const Object *obj, Bool crushes);

	// Team tracking. The first call for a team starts tracking it.
	Int getTeamIdleFrames(Team *team);
	Bool isTeamAllClear(Team *team);
	Real getTeamHealthPercent(Team *team);

protected:

	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:

	struct TeamTrack
	{
		Int idleFrames;
		Int allClearFrames;
		Bool sawEnemy;
		Real deadMaxHealth;
	};

	TeamTrack *trackTeam(Team *team);
	void updateTeamTrack(Team *team, TeamTrack *track);

	typedef std::map<AsciiString, KDRatio> KDRatioMap;
	typedef std::map<ObjectID, Bool> ObjectFlagMap;
	typedef std::map<TeamID, TeamTrack> TeamTrackMap;
	typedef std::map<TeamID, EventList> TeamLossMap;

	void addEvent(EventList &events, const Object *obj);

	KDRatioMap m_kdRatios;
	ObjectFlagMap m_beingCaptured;
	ObjectFlagMap m_objectCrushes;
	Int m_playerCrushes[MAX_PLAYER_COUNT];			///< -1 uses AIData, else a Bool
	TeamTrackMap m_teamTracks;

	EventList m_playerKills[MAX_PLAYER_COUNT];
	EventList m_playerLosses[MAX_PLAYER_COUNT];
	TeamLossMap m_teamLosses;
	UnsignedInt m_eventSeq;
};

extern ScriptCLPState *TheScriptCLPState;
