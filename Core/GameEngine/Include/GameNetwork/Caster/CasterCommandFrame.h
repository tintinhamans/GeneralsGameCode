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

class GameMessage;
class CommandList;

// Wire representation for one already-accepted simulation frame. This is not
// a replay record: it carries the resulting GameMessages directly.
namespace CasterCommandFrame
{
	static const UnsignedInt VERSION = 1;
	static const UnsignedInt MAX_BYTES = 64 * 1024;
	static const UnsignedInt MAX_MESSAGES = 255;
	static const UnsignedInt MAX_FRAGMENT_BYTES = 1024;

	UnsignedInt encode(UnsignedInt frame, GameMessage* first, UnsignedByte* out,
		UnsignedInt capacity);
	/// Allocation-free wire validation for transport workers. It never creates
	/// GameMessages or touches engine state.
	Bool validate(const UnsignedByte* data, UnsignedInt length, UnsignedInt expectedFrame);
	Bool appendDecoded(const UnsignedByte* data, UnsignedInt length,
		UnsignedInt expectedFrame, CommandList* commands);
	UnsignedInt checksum(const UnsignedByte* data, UnsignedInt length);

	/**
	 * Reassembles one fragmented frame. The two MAX_BYTES working buffers are
	 * owned and allocated on first use, so an idle assembler - and every
	 * structure that embeds one - stays small enough to sit on a stack or be
	 * scanned cheaply.
	 */
	class Assembler
	{
	public:
		Assembler();
		~Assembler();

		void reset();
		Bool begin(UnsignedInt frame, UnsignedInt totalBytes,
			UnsignedInt expectedChecksum);
		Bool append(UnsignedInt offset, const UnsignedByte* fragment,
			UnsignedInt length);
		Bool isComplete() const;

		UnsignedInt frame() const { return m_frame; }
		const UnsignedByte* data() const { return m_data; }
		UnsignedInt length() const { return m_totalBytes; }

	private:
		/// Allocates the owned buffers on first use; FALSE when out of memory.
		Bool ensureBuffers();

		UnsignedByte* m_data;
		UnsignedByte* m_received;
		UnsignedInt m_frame;
		UnsignedInt m_totalBytes;
		UnsignedInt m_expectedChecksum;
		UnsignedInt m_receivedBytes;
		Bool m_active;
		Bool m_bad;

		Assembler(const Assembler&);
		Assembler& operator=(const Assembler&);
	};
}
