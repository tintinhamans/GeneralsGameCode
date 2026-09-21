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

#pragma once

#include "Lib/BaseType.h"

// An append-only, in-memory history of accepted contiguous simulation command frames.
// The game/network owner calls this from one serialized caster-session
// thread; it intentionally performs no locking and never owns live transport.
class CasterFrameHistory
{
public:
	enum
	{
		MAX_FRAME_BYTES = 64 * 1024
	};

	CasterFrameHistory();
	~CasterFrameHistory();

	/// Starts a new in-memory session, discarding every retained frame.
	Bool open();
	/// Releases every retained frame.
	void close();

	/// Appends only the next frame. An existing frame is accepted only if every byte matches.
	Bool append(UnsignedInt frame, const UnsignedByte* data, UnsignedInt length);
	/// Reads one session-history frame. `outLength` is set only when the frame is available.
	Bool read(UnsignedInt frame, UnsignedByte* destination, UnsignedInt capacity, UnsignedInt& outLength);

private:
	struct Entry
	{
		UnsignedByte* data;
		UnsignedInt length;
	};

	Bool grow(UnsignedInt minimumCapacity);
	Bool compareRecord(UnsignedInt index, const UnsignedByte* data, UnsignedInt length);

	Entry* m_entries;
	UnsignedInt m_capacity;
	Bool m_open;
	UnsignedInt m_firstFrame;
	UnsignedInt m_lastFrame;
	UnsignedInt m_count;

	CasterFrameHistory(const CasterFrameHistory&);
	CasterFrameHistory& operator=(const CasterFrameHistory&);
};
