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

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "PreRTS.h"

#include <cstring>

#include "Common/EmbeddedArchiveFile.h"
#include "Common/RAMFile.h"

//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------

EmbeddedArchiveFile::EmbeddedArchiveFile(const EmbeddedFileRecord* records, unsigned int count)
	: m_records(records)
	, m_count(count)
	, m_name("450_450_GeneralsOnline_Embedded.big")
{
	for (unsigned int i = 0; i < count; ++i)
	{
		if (records[i].path == nullptr)
		{
			continue; // generator emits a placeholder record when Data/ is empty
		}

		const char* str = records[i].path;
		const char* p1 = strrchr(str, '\\');
		const char* p2 = strrchr(str, '/');
		const char* sep = (p1 == nullptr) ? p2 : ((p2 == nullptr) ? p1 : ((p1 > p2) ? p1 : p2));

		// path handed to addFile() must include the trailing separator, matching the
		// convention the on-disk .big parsers use (see Win32BIGFileSystem::openArchiveFile).
		Char dirBuf[1024];
		size_t dirLen = (sep != nullptr) ? (size_t)(sep - str + 1) : 0;
		if (dirLen >= sizeof(dirBuf))
		{
			dirLen = sizeof(dirBuf) - 1;
		}
		memcpy(dirBuf, str, dirLen);
		dirBuf[dirLen] = 0;

		ArchivedFileInfo fileInfo;
		fileInfo.m_filename = (sep != nullptr) ? AsciiString(sep + 1) : AsciiString(str);
		fileInfo.m_filename.toLower();
		fileInfo.m_archiveFilename = m_name;
		fileInfo.m_offset = i; // index back into m_records; there is no on-disk offset
		fileInfo.m_size = records[i].size;

		addFile(AsciiString(dirBuf), &fileInfo);
	}
}

Bool EmbeddedArchiveFile::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	const ArchivedFileInfo* info = getArchivedFileInfo(filename);
	if (info == nullptr)
	{
		return FALSE;
	}

	memset(fileInfo, 0, sizeof(*fileInfo));
	fileInfo->sizeLow = (Int)info->m_size;
	fileInfo->sizeHigh = 0;

	return TRUE;
}

File* EmbeddedArchiveFile::openFile(const Char *filename, Int access)
{
	const ArchivedFileInfo* info = getArchivedFileInfo(AsciiString(filename));
	if (info == nullptr || info->m_offset >= m_count)
	{
		return nullptr;
	}

	const EmbeddedFileRecord& record = m_records[info->m_offset];

	RAMFile* file = newInstance(RAMFile);
	file->deleteOnClose();
	if (file->openFromMemory(AsciiString(record.path), record.data, (Int)record.size) == FALSE)
	{
		file->close();
		return nullptr;
	}

	return file;
}
