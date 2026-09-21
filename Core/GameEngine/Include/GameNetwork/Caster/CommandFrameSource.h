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

class CommandList;
class GameMessage;

/// Where the simulation gets the commands of one logic frame from: a network
/// game, a local replay file, or a live caster transport.
class CommandFrameSource
{
public:
	virtual ~CommandFrameSource() {}

	/// TRUE when every command of `frame` can be appended right now.
	virtual Bool isFrameReady(UnsignedInt frame) = 0;

	/// Appends the commands of `frame` to `commands`. FALSE leaves `commands`
	/// untouched and the frame retryable.
	virtual Bool appendFrame(UnsignedInt frame, CommandList* commands) = 0;

	/// TRUE once no further frame will ever become ready.
	virtual Bool hasEnded() = 0;

	/// TRUE when the source is far enough ahead of `frame` that the simulation
	/// should run extra logic ticks to catch up.
	virtual Bool needsCatchUp(UnsignedInt frame) { (void)frame; return FALSE; }
};

/// Optional consumer that keeps a local replay of the frames a live caster
/// accepted. Watching works without one.
class CommandFrameSink
{
public:
	virtual ~CommandFrameSink() {}

	/// `firstNew` starts the messages this frame appended to the command list.
	virtual void onFrameCommands(UnsignedInt frame, GameMessage* firstNew) = 0;

	/// The watch ended; finalize and close the output.
	virtual void onEnd() = 0;
};
