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

#if defined(RTS_SQLITE_ARCHIVE)

#include <vector>

#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"

class SQLiteContentDatabase;

// One product (the game, or an enabled patch or addon) of the launcher content database (GeneralsOnlineGameData.db), read-only.
// It acts like a .big file: its name is the product's load key plus ".big", so it orders among the real .big files by name.
class SQLiteArchiveFile : public ArchiveFile
{
public:
	virtual ~SQLiteArchiveFile() override;

	// Open the database and create one archive per included product of the current profile, all sharing one connection.
	// Returns FALSE with no archives and the database closed when it is unusable, so the caller falls back to the .big files.
	static Bool loadProducts(const AsciiString& path, std::vector<SQLiteArchiveFile*>& archives);

	virtual Bool					getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const override;
	virtual File*					openFile(const Char *filename, Int access = 0) override;
	virtual void					closeAllFiles() override;
	virtual AsciiString		getName() override;
	virtual AsciiString		getPath() override;
	virtual void					setSearchPriority(Int new_priority) override;
	virtual void					close() override;

private:
	SQLiteArchiveFile(SQLiteContentDatabase *database, const AsciiString& name);

	SQLiteContentDatabase *m_database;	///< shared with the other products, released on close
	AsciiString m_name;	///< "<database path>\<load key>.big"
};

#endif
