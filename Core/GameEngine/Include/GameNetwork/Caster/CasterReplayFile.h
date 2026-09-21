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

#include "Common/file.h"
#include "Lib/BaseType.h"

// Owns the wrapped recording file. It carries no caster payload of its own: the
// caster stream is the typed bootstrap plus native command frames. Its presence
// is what tells the facade that a recording (and therefore a match) is running.
class CasterReplayFile : public File
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(CasterReplayFile, "CasterReplayFile")

public:
	CasterReplayFile();

	Bool wrap(File* inner);

	virtual Bool	open( const Char *filename, Int access = NONE, size_t bufferSize = BUFFERSIZE ) override;
	virtual void	close() override;

	virtual Int		read( void *buffer, Int bytes ) override;
	virtual Int		readChar() override;
	virtual Int		readWideChar() override;

	virtual Int		write( const void *buffer, Int bytes ) override;
	virtual Int		writeFormat( const Char* format, ... ) override;
	virtual Int		writeFormat( const WideChar* format, ... ) override;
	virtual Int		writeChar( const Char* character ) override;
	virtual Int		writeChar( const WideChar* character ) override;

	virtual Int		seek( Int bytes, seekMode mode = CURRENT ) override;
	virtual Bool	flush() override;
	virtual void	nextLine(Char *buf = nullptr, Int bufSize = 0) override;
	virtual Bool	scanInt(Int &newInt) override;
	virtual Bool	scanReal(Real &newReal) override;
	virtual Bool	scanString(AsciiString &newString) override;

	virtual char*	readEntireAndClose() override;
	virtual File*	convertToRAMFile() override;

private:

	File* m_inner;

	CasterReplayFile(const CasterReplayFile&);
	CasterReplayFile& operator=(const CasterReplayFile&);
};
