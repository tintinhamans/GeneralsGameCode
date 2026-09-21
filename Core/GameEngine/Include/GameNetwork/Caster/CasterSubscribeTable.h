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

// Per-game caster subscription counts and streaming eligibility.
// The transport serializes mutations and recorder lookups under the same lock.
// Never hold that lock across socket I/O, sleeps or file writes. gameUid 0 is unannounced.

#pragma once

#include "Lib/BaseType.h"

namespace CasterSubscribeTable
{

static const UnsignedInt TABLE_CAPACITY = 16;

enum AckState
{
	ACK_STREAMING_ELIGIBLE = 0,
	ACK_IN_PROGRESS        = 1,	///< already streaming, or not announced: no bytes
};

class Table
{
public:
	Table();

	void reset();


	Bool subscribe(UnsignedInt gameUid);

	Bool unsubscribe(UnsignedInt gameUid);

	Bool isSubscribed(UnsignedInt gameUid) const;

private:
	struct Entry
	{
		Bool active;
		UnsignedInt gameUid;
		UnsignedInt subscriberCount;
	};

	Entry* find(UnsignedInt gameUid);
	const Entry* find(UnsignedInt gameUid) const;
	Entry* findFree();

	Entry m_entries[TABLE_CAPACITY];
	UnsignedInt m_count;
};

}
