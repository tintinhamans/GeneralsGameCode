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

// FILE: ChatCommand.h //////////////////////////////////////////////////////////////////////////
// Desc: Parsing and storage for ChatCommand blocks defined in the optional ChatCommands.ini.
//       Commands carry no behavior yet; key/value attributes and dispatch logic come later.
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ChatCommand_H_
#define _ChatCommand_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/SubsystemInterface.h"
#include <vector>

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class INI;
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** A single chat command parsed from a "ChatCommand <name> ... End" block. */
//-------------------------------------------------------------------------------------------------
class ChatCommand
{
public:
	ChatCommand() {}

	/** Who "SetSelectedOwner" hands the selection to. The relationship values name the first
			player holding that relationship to the local player. */
	enum OwnerTarget CPP_11(: Int)
	{
		OWNER_UNCHANGED = -1,	///< the attribute was absent
		OWNER_ENEMIES = ENEMIES,
		OWNER_NEUTRAL = NEUTRAL,
		OWNER_ALLIES = ALLIES,
		OWNER_SELF				///< the local player, to take objects back
	};

	const AsciiString& getName() const { return m_name; }
	void setName( const AsciiString& name ) { m_name = name; }

	const FieldParse* getFieldParse() const { return s_fieldParseTable; }

	Int getAddMoney() const { return m_addMoney; }
	UnsignedInt getAddRank() const { return m_addRank; }
	Bool getReadyTimers() const { return m_readyTimers; }
	const AsciiString& getSpawnObjectAtCursor() const { return m_spawnObjectAtCursor; }
	Bool isSpawnObjectCommand() const { return m_isSpawnObjectCommand; }
	Bool getTogglePrerequisites() const { return m_togglePrerequisites; }
	Bool getToggleInfiniteEnergy() const { return m_toggleInfiniteEnergy; }
	Bool getGrantAllUpgrades() const { return m_grantAllUpgrades; }
	Int getAddVeterancyLevel() const { return m_addVeterancyLevel; }
	Int getAddSalvageTier() const { return m_addSalvageTier; }
	Real getProductionSpeedMultiplier() const { return m_productionSpeedMultiplier; }
	Int getSetSelectedOwner() const { return m_setSelectedOwner; }
	Real getAddHealth() const { return m_addHealth; }
	Bool getKillSelected() const { return m_killSelected; }
	Bool getKillSelectedPilots() const { return m_killSelectedPilots; }
	Bool getControlPlayer() const { return m_controlPlayer; }
	Bool getSubdueSelected() const { return m_subdueSelected; }

	// For callers that build a command in code, such as the debug panel.
	void setAddMoney( Int amount ) { m_addMoney = amount; }
	void setAddRank( UnsignedInt ranks ) { m_addRank = ranks; }
	void setReadyTimers( Bool enable ) { m_readyTimers = enable; }
	void setTogglePrerequisites( Bool enable ) { m_togglePrerequisites = enable; }
	void setToggleInfiniteEnergy( Bool enable ) { m_toggleInfiniteEnergy = enable; }
	void setGrantAllUpgrades( Bool enable ) { m_grantAllUpgrades = enable; }
	void setAddVeterancyLevel( Int levels ) { m_addVeterancyLevel = levels; }
	void setAddSalvageTier( Int tiers ) { m_addSalvageTier = tiers; }
	void setProductionSpeedMultiplier( Real multiplier ) { m_productionSpeedMultiplier = multiplier; }
	void setSelectedOwner( Int ownerTarget ) { m_setSelectedOwner = ownerTarget; }
	void setAddHealth( Real amount ) { m_addHealth = amount; }
	void setKillSelected( Bool enable ) { m_killSelected = enable; }
	void setKillSelectedPilots( Bool enable ) { m_killSelectedPilots = enable; }
	void setControlPlayer( Bool enable ) { m_controlPlayer = enable; }
	void setSubdueSelected( Bool enable ) { m_subdueSelected = enable; }

	/** Run this command's effects. Inspects the parsed members and acts accordingly.
			"args" is whatever the user typed after the command name, empty when nothing followed.
			Effects that accept an argument use it in place of their INI value; the rest ignore it. */
	void execute( const AsciiString& args ) const;

	/** Parser for "AddHealth". A blank value means the default amount rather than no amount. */
	static void parseAddHealth( INI *ini, void *instance, void *store, const void *userData );

	/** Parser for "SpawnObjectAtCursor". Stores the name and records that the key was present,
			which is what marks the command as a spawn command -- the name itself may be left blank
			when the object is meant to be typed after the command instead. */
	static void parseSpawnObjectAtCursor( INI *ini, void *instance, void *store, const void *userData );

private:
	AsciiString m_name;
	Int m_addMoney = 0;			///< "AddMoney" attribute; signed amount, defaults to 0.
	UnsignedInt m_addRank = 0;	///< "AddRank" attribute; ranks to grant, capped at the max rank. Defaults to 0.
	Bool m_readyTimers = FALSE;	///< "ReadyTimers" attribute; when TRUE, set all of the player's special power timers to ready.
	AsciiString m_spawnObjectAtCursor;	///< "SpawnObjectAtCursor" attribute; ObjectTemplate name to spawn for the local player at the mouse cursor. May be blank when the name is typed after the command instead.
	Bool m_isSpawnObjectCommand = FALSE;	///< TRUE when "SpawnObjectAtCursor" was present at all, blank value included; separates "is a spawn command" from "has a default object".
	Bool m_togglePrerequisites = FALSE;	///< "TogglePrerequisites" attribute; when TRUE, toggles ignoring unit/building build prereqs (science still applies).
	Bool m_toggleInfiniteEnergy = FALSE;	///< "ToggleInfiniteEnergy" attribute; when TRUE, toggles infinite power for the local player.
	Bool m_grantAllUpgrades = FALSE;		///< "GrantAllUpgrades" attribute; when TRUE, grants the local player all player-type upgrades.
	Int m_addVeterancyLevel = 0;			///< "AddVeterancyLevel" attribute; promote selected units by this many veterancy levels (negative demotes), capped to the valid range.
	Int m_addSalvageTier = 0;				///< "AddSalvageTier" attribute; change selected salvagers' crate-upgrade tier by this much (negative removes), capped 0..2.
	Real m_productionSpeedMultiplier = 0.0f;	///< "ProductionSpeedMultiplier" attribute; build-speed multiplier for the local player (>1 builds faster). 0 means the field was absent (no change).
	Int m_setSelectedOwner = OWNER_UNCHANGED;	///< "SetSelectedOwner" attribute; who to give the selected objects to, as an OwnerTarget.
	Real m_addHealth = 0.0f;				///< "AddHealth" attribute; health added to the selected objects' max and current health, or damage dealt when negative. 0 means the field was absent.
	Bool m_killSelected = FALSE;			///< "KillSelected" attribute; when TRUE, kill the selected objects outright.
	Bool m_killSelectedPilots = FALSE;		///< "KillSelectedPilots" attribute; when TRUE, snipe the crew out of the selected vehicles.
	Bool m_controlPlayer = FALSE;			///< "ControlPlayer" attribute; when TRUE, take control of the player named after the command.
	Bool m_subdueSelected = FALSE;			///< "SubdueSelected" attribute; when TRUE, fill the selected objects' subdual damage so they sit disabled.

	static const FieldParse s_fieldParseTable[];
};

//-------------------------------------------------------------------------------------------------
/** The store that owns all ChatCommands parsed from ChatCommands.ini. */
//-------------------------------------------------------------------------------------------------
class ChatCommandStore : public SubsystemInterface
{
public:
	ChatCommandStore() {}
	virtual ~ChatCommandStore();

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

	/** Return the command with the given name, or NULL if none exists. */
	const ChatCommand* findChatCommand( const AsciiString& name ) const;

	/** Number of parsed commands. */
	UnsignedInt getChatCommandCount() const { return (UnsignedInt)m_commands.size(); }

	// INI block parser, registered in INI's block table.
	static void parseChatCommandDefinition( INI* ini );

private:
	void clear();

	std::vector<ChatCommand*> m_commands;
};

// EXTERNALS //////////////////////////////////////////////////////////////////////////////////////
extern ChatCommandStore* TheChatCommandStore;

#endif // _ChatCommand_H_
