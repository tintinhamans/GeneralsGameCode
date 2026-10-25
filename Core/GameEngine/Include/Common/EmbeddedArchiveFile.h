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

//----------------------------------------------------------------------------
//           Includes
//----------------------------------------------------------------------------

#include "Common/ArchiveFile.h"

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

// One entry of a generated table of files embedded directly into the executable.
// path is a game-relative path such as "Window/Menus/Example.wnd" (either slash
// style is accepted). data points at static, read-only bytes that outlive the
// program; nothing ever frees them. See cmake/GenerateEmbeddedData.cmake.
struct EmbeddedFileRecord
{
	const char* path;
	const unsigned char* data;
	unsigned int size;
};

//===============================
// EmbeddedArchiveFile
//===============================
/**
	An ArchiveFile backed by data compiled into the executable from the repository's
	GeneralsOnline/GameData folder, instead of an on-disk .big file. TheArchiveFileSystem
	mounts it under the virtual name "GeneralsOnline_Core_Embedded.big" at the start of the
	generals_online priority tier (see mountEmbeddedArchive()): loose files, -mod, the game
	folder BIGs and data_packs.user win over it, and it wins over the community patch and
	the retail archives.

	Read-only: openFile() always hands back a RAMFile that references the static
	table data directly (see RAMFile::openFromMemory), never a writable copy.
*/
//===============================
class EmbeddedArchiveFile : public ArchiveFile
{
public:
	EmbeddedArchiveFile(const EmbeddedFileRecord* records, unsigned int count);
	virtual ~EmbeddedArchiveFile() {}

	virtual Bool	getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const override;
	virtual File*	openFile(const Char *filename, Int access = 0) override;
	virtual void	closeAllFiles() override {}
	virtual void	setSearchPriority(Int new_priority) override {}
	virtual void	close() override {}

private:
	const EmbeddedFileRecord* m_records;
	unsigned int m_count;
};
