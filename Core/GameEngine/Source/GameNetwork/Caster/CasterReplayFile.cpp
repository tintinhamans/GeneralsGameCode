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

#include "PreRTS.h"

#include <windows.h>
#include <stdarg.h>

#include "Common/file.h"
#include "Utility/stdio_adapter.h"
#include "GameNetwork/Caster/CasterReplayFile.h"

CasterReplayFile::CasterReplayFile()
	: m_inner(nullptr)
{
}

CasterReplayFile::~CasterReplayFile()
{
}

Bool CasterReplayFile::wrap(File* inner)
{
	if (inner == nullptr || m_inner != nullptr)
	{
		return FALSE;
	}

	m_inner = inner;

	// Mirror the base `File` state so `size()`/`position()`/`close()` behave and
	// `File::close()` reaches its self-delete path.
	m_open = TRUE;
	m_access = inner->getAccess();
	setName(inner->getName());
	return TRUE;
}

Bool CasterReplayFile::open(const Char* filename, Int access, size_t bufferSize)
{
	if (m_inner != nullptr)
	{
		if (!m_inner->open(filename, access, bufferSize))
		{
			return FALSE;
		}
	}

	return File::open(filename, access, bufferSize);
}

void CasterReplayFile::close()
{
	// Capture the inner file before anything can free itself. It is
	// `deleteOnClose`; `File::close()` may free `this`. Nothing is touched after.
	File* inner = m_inner;

	m_inner = nullptr;

	if (inner != nullptr)
	{
		inner->close();
	}

	File::close();
}

Int CasterReplayFile::read(void* buffer, Int bytes)
{
	if (m_inner == nullptr)
	{
		return -1;
	}
	return m_inner->read(buffer, bytes);
}

Int CasterReplayFile::readChar()
{
	Char character = '\0';

	Int ret = read(&character, sizeof(character));
	if (ret == sizeof(character))
	{
		return (Int)character;
	}

	return EOF;
}

Int CasterReplayFile::readWideChar()
{
	WideChar character = L'\0';

	Int ret = read(&character, sizeof(character));
	if (ret == sizeof(character))
	{
		return (Int)character;
	}

	return WEOF;
}

Int CasterReplayFile::write(const void* buffer, Int bytes)
{
	if (m_inner == nullptr)
	{
		return -1;
	}

	return m_inner->write(buffer, bytes);
}

Int CasterReplayFile::writeFormat(const Char* format, ...)
{
	char buffer[1024];

	va_list args;
	va_start(args, format);
	Int length = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	return write(buffer, length);
}

Int CasterReplayFile::writeFormat(const WideChar* format, ...)
{
	WideChar buffer[1024];

	va_list args;
	va_start(args, format);
	Int length = vswprintf(buffer, sizeof(buffer) / sizeof(WideChar), format, args);
	va_end(args);

	return write(buffer, length * sizeof(WideChar));
}

Int CasterReplayFile::writeChar(const Char* character)
{
	if (write(character, sizeof(Char)) == sizeof(Char))
	{
		return (Int)character;
	}

	return EOF;
}

Int CasterReplayFile::writeChar(const WideChar* character)
{
	if (write(character, sizeof(WideChar)) == sizeof(WideChar))
	{
		return (Int)character;
	}

	return WEOF;
}

Int CasterReplayFile::seek(Int bytes, seekMode mode)
{
	if (m_inner == nullptr)
	{
		return -1;
	}
	return m_inner->seek(bytes, mode);
}

Bool CasterReplayFile::flush()
{
	if (m_inner == nullptr)
	{
		return FALSE;
	}
	return m_inner->flush();
}

void CasterReplayFile::nextLine(Char* buf, Int bufSize)
{
	if (m_inner != nullptr)
	{
		m_inner->nextLine(buf, bufSize);
	}
}

Bool CasterReplayFile::scanInt(Int& newInt)
{
	if (m_inner == nullptr)
	{
		return FALSE;
	}
	return m_inner->scanInt(newInt);
}

Bool CasterReplayFile::scanReal(Real& newReal)
{
	if (m_inner == nullptr)
	{
		return FALSE;
	}
	return m_inner->scanReal(newReal);
}

Bool CasterReplayFile::scanString(AsciiString& newString)
{
	if (m_inner == nullptr)
	{
		return FALSE;
	}
	return m_inner->scanString(newString);
}

char* CasterReplayFile::readEntireAndClose()
{
	UnsignedInt fileSize = size();
	char* buffer = NEW char[fileSize];

	read(buffer, fileSize);

	close();

	return buffer;
}

File* CasterReplayFile::convertToRAMFile()
{
	if (m_inner == nullptr)
	{
		return this;
	}

	// The inner file may convert-and-delete itself; re-point at whatever it
	// returned so the decorator never holds a dangling inner.
	File* ramFile = m_inner->convertToRAMFile();
	m_inner = ramFile;
	return this;
}
