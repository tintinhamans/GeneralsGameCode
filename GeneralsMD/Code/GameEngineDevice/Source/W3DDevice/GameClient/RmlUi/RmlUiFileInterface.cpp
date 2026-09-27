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

#include "W3DDevice/GameClient/RmlUi/RmlUiFileInterface.h"

#include "Common/FileSystem.h"
#include "Common/file.h"

Rml::FileHandle RmlUiFileInterface::Open(const Rml::String &path)
{
	if (!TheFileSystem)
		return 0;
	File *file = TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
	return (Rml::FileHandle)file;
}

void RmlUiFileInterface::Close(Rml::FileHandle file)
{
	if (file)
		((File *)file)->close();
}

size_t RmlUiFileInterface::Read(void *buffer, size_t size, Rml::FileHandle file)
{
	if (!file)
		return 0;
	Int result = ((File *)file)->read(buffer, (Int)size);
	return result > 0 ? (size_t)result : 0;
}

bool RmlUiFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	if (!file)
		return false;
	// SEEK_SET/SEEK_CUR/SEEK_END (0/1/2) line up with File::START/CURRENT/END.
	((File *)file)->seek((Int)offset, (File::seekMode)origin);
	return true;
}

size_t RmlUiFileInterface::Tell(Rml::FileHandle file)
{
	if (!file)
		return 0;
	return (size_t)((File *)file)->seek(0, File::CURRENT);
}

size_t RmlUiFileInterface::Length(Rml::FileHandle file)
{
	if (!file)
		return 0;
	return (size_t)((File *)file)->size();
}
