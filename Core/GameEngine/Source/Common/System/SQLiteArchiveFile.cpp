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

#if defined(RTS_SQLITE_ARCHIVE)

#include "Common/SQLiteArchiveFile.h"

#include <map>
#include <set>
#include <string>

#include <sqlite3.h>

#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Common/RAMFile.h"
#include "WWLib/mutex.h"

// Schema version the launcher stamps into PRAGMA user_version.
static const int kContentDatabaseVersion = 1;

// One row per file of every included product of the current profile. The load key is the profile_release override,
// else the release override, else the product default; rows come grouped by product.
static const char *const s_indexQuery =
	"SELECT pd.id, COALESCE(pr.load_key, r.load_key, pd.default_load_key), ve.path, ve.blob_id, b.size, b.patch_base IS NOT NULL "
	"FROM profile p "
	"JOIN profile_release pr ON pr.profile_id = p.id "
	"JOIN product pd ON pd.id = pr.product_id "
	"JOIN release r ON r.id = pr.release_id "
	"JOIN variant_entry ve ON ve.variant_id = pr.variant_id "
	"JOIN blob b ON b.id = ve.blob_id "
	"WHERE p.is_current = 1 AND ve.placement = 'db' "
	"AND (pd.kind = 'game' OR (pd.kind IN ('patch', 'addon') AND pr.enabled = 1)) "
	"ORDER BY pd.id;";

static std::string makeImmutableUri(const char *path)
{
	// immutable=1 skips locking and ignores any WAL, so the launcher must have checkpointed.
	static const char *const unreserved = "/:._-~";
	std::string uri = "file:";
	for (const char *c = path; *c != 0; ++c)
	{
		const unsigned char ch = (unsigned char)*c;
		if (ch == '\\')
		{
			uri += '/';
		}
		else if (isalnum(ch) || strchr(unreserved, ch) != nullptr)
		{
			uri += (char)ch;
		}
		else
		{
			char hex[4];
			sprintf(hex, "%%%02X", ch);
			uri += hex;
		}
	}
	uri += "?immutable=1";
	return uri;
}

// Load keys are 1 to 64 characters of [A-Za-z0-9_.-].
static Bool isValidLoadKey(const char *key)
{
	if (key == nullptr)
		return FALSE;
	size_t length = 0;
	for (const char *c = key; *c != 0; ++c, ++length)
	{
		const unsigned char ch = (unsigned char)*c;
		if (!(isalnum(ch) || ch == '_' || ch == '.' || ch == '-'))
			return FALSE;
	}
	return length >= 1 && length <= 64;
}

// The database connection shared by all product archives; the last one to let go closes it.
class SQLiteContentDatabase
{
public:
	SQLiteContentDatabase(const AsciiString& path) : m_path(path), m_db(nullptr), m_blob(nullptr), m_refCount(1) {}

	void addRef() { ++m_refCount; }
	void release()
	{
		if (--m_refCount == 0)
			delete this;
	}

	Bool open();	///< open read-only and check the schema version
	File* readBlob(const ArchivedFileInfo *fileInfo, const Char *filename);

	const AsciiString& getPath() const { return m_path; }
	sqlite3 *getHandle() const { return m_db; }

private:
	~SQLiteContentDatabase();

	AsciiString m_path;
	sqlite3 *m_db;
	sqlite3_blob *m_blob;	///< reused across files with sqlite3_blob_reopen
	FastCriticalSectionClass m_mutex;	///< guards m_db and m_blob
	Int m_refCount;
};

SQLiteContentDatabase::~SQLiteContentDatabase()
{
	if (m_blob != nullptr)
		sqlite3_blob_close(m_blob);
	if (m_db != nullptr)
		sqlite3_close(m_db);
}

Bool SQLiteContentDatabase::open()
{
	const std::string uri = makeImmutableUri(m_path.str());
	if (sqlite3_open_v2(uri.c_str(), &m_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK)
	{
		DEBUG_LOG(("SQLiteContentDatabase::open - could not open %s: %s", m_path.str(), m_db ? sqlite3_errmsg(m_db) : "out of memory"));
		return FALSE;
	}

	sqlite3_exec(m_db, "PRAGMA mmap_size = 0;", nullptr, nullptr, nullptr);

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW)
	{
		DEBUG_LOG(("SQLiteContentDatabase::open - could not read the schema version of %s: %s", m_path.str(), sqlite3_errmsg(m_db)));
		sqlite3_finalize(stmt);
		return FALSE;
	}
	const int version = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	if (version != kContentDatabaseVersion)
	{
		DEBUG_LOG(("SQLiteContentDatabase::open - %s has schema version %d, expected %d", m_path.str(), version, kContentDatabaseVersion));
		return FALSE;
	}

	return TRUE;
}

// Blobs are read whole into a buffer owned by the returned RAM file.
File* SQLiteContentDatabase::readBlob(const ArchivedFileInfo *fileInfo, const Char *filename)
{
	const Int size = (Int)fileInfo->m_size;
	Char *data = MSGNEW("RAMFILE") Char [size > 0 ? size : 1];

	{
		FastCriticalSectionClass::LockClass lock(m_mutex);

		Bool opened = FALSE;
		if (m_blob != nullptr)
		{
			opened = (sqlite3_blob_reopen(m_blob, (sqlite3_int64)fileInfo->m_offset) == SQLITE_OK);
			if (!opened)
			{
				sqlite3_blob_close(m_blob);
				m_blob = nullptr;
			}
		}
		if (!opened)
		{
			opened = (sqlite3_blob_open(m_db, "main", "blob", "data", (sqlite3_int64)fileInfo->m_offset, 0, &m_blob) == SQLITE_OK);
			if (!opened)
			{
				DEBUG_LOG(("SQLiteContentDatabase::readBlob - could not open blob %u for %s: %s", fileInfo->m_offset, filename, sqlite3_errmsg(m_db)));
				m_blob = nullptr;
			}
		}

		if (!opened || sqlite3_blob_bytes(m_blob) != size || sqlite3_blob_read(m_blob, data, size, 0) != SQLITE_OK)
		{
			delete [] data;
			return nullptr;
		}
	}

	RAMFile *ramFile = newInstance( RAMFile );
	ramFile->deleteOnClose();
	if (ramFile->openFromBuffer(fileInfo->m_filename, data, size) == FALSE) {
		delete [] data;
		ramFile->close();
		return nullptr;
	}

	return ramFile;
}

SQLiteArchiveFile::SQLiteArchiveFile(SQLiteContentDatabase *database, const AsciiString& name)
	: m_database(database)
	, m_name(name)
{
	m_database->addRef();
}

SQLiteArchiveFile::~SQLiteArchiveFile()
{
	close();
}

Bool SQLiteArchiveFile::loadProducts(const AsciiString& path, std::vector<SQLiteArchiveFile*>& archives)
{
	SQLiteContentDatabase *database = NEW SQLiteContentDatabase(path);
	if (!database->open())
	{
		database->release();
		return FALSE;
	}

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(database->getHandle(), s_indexQuery, -1, &stmt, nullptr) != SQLITE_OK)
	{
		DEBUG_LOG(("SQLiteArchiveFile::loadProducts - could not query %s: %s", path.str(), sqlite3_errmsg(database->getHandle())));
		database->release();
		return FALSE;
	}

	std::map<std::string, sqlite3_int64> productByKey;	// lower case load key, unique among the included products
	std::set<std::string> seen;	// paths of the current product
	SQLiteArchiveFile *archive = nullptr;
	sqlite3_int64 currentProduct = -1;
	ArchivedFileInfo fileInfo;
	Int numFiles = 0;
	Bool ok = TRUE;

	Int rc;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		const sqlite3_int64 productId = sqlite3_column_int64(stmt, 0);
		if (archive == nullptr || productId != currentProduct)
		{
			const char *loadKey = (const char *)sqlite3_column_text(stmt, 1);
			if (!isValidLoadKey(loadKey))
			{
				DEBUG_LOG(("SQLiteArchiveFile::loadProducts - product %d has an invalid load key '%s'", (Int)productId, loadKey ? loadKey : ""));
				ok = FALSE;
				break;
			}

			std::string lowerKey = loadKey;
			for (std::string::iterator it = lowerKey.begin(); it != lowerKey.end(); ++it)
				*it = (char)tolower((unsigned char)*it);
			const std::pair<std::map<std::string, sqlite3_int64>::iterator, bool> inserted = productByKey.insert(std::make_pair(lowerKey, productId));
			if (!inserted.second)
			{
				DEBUG_LOG(("SQLiteArchiveFile::loadProducts - products %d and %d share the load key '%s'", (Int)inserted.first->second, (Int)productId, loadKey));
				ok = FALSE;
				break;
			}

			// Named like a .big file so it orders by name among the real ones.
			AsciiString name = path;
			name.concat('\\');
			name.concat(loadKey);
			name.concat(".big");
			archive = NEW SQLiteArchiveFile(database, name);
			archives.push_back(archive);
			currentProduct = productId;
			seen.clear();
			fileInfo.m_archiveFilename = name;
			DEBUG_LOG(("SQLiteArchiveFile::loadProducts - product %d is archive %s", (Int)productId, name.str()));
		}

		AsciiString fullPath = (const char *)sqlite3_column_text(stmt, 2);
		fullPath.toLower();

		std::string key = fullPath.str();
		for (std::string::iterator it = key.begin(); it != key.end(); ++it)
		{
			if (*it == '/')
				*it = '\\';
		}
		if (!seen.insert(key).second)
			continue;	// the product lists this path twice

		if (sqlite3_column_int(stmt, 5) != 0)
		{
			DEBUG_LOG(("SQLiteArchiveFile::loadProducts - %s is stored as a patch, the profile's blobs must be full", key.c_str()));
			ok = FALSE;
			break;
		}

		const sqlite3_int64 blobId = sqlite3_column_int64(stmt, 3);
		const sqlite3_int64 size = sqlite3_column_int64(stmt, 4);
		if (blobId < 0 || blobId > 0xFFFFFFFFLL || size < 0 || size > 0x7FFFFFFFLL)
		{
			DEBUG_LOG(("SQLiteArchiveFile::loadProducts - %s has an unsupported blob id or size", key.c_str()));
			ok = FALSE;
			break;
		}

		const std::string::size_type sep = key.find_last_of('\\');
		AsciiString filePath;
		if (sep == std::string::npos)
		{
			fileInfo.m_filename = key.c_str();
		}
		else
		{
			fileInfo.m_filename = key.c_str() + sep + 1;
			filePath = key.substr(0, sep + 1).c_str();
		}
		fileInfo.m_offset = (UnsignedInt)blobId;	// blob rowid, not a byte offset
		fileInfo.m_size = (UnsignedInt)size;

		archive->addFile(filePath, &fileInfo);
		++numFiles;
	}

	if (ok && rc != SQLITE_DONE)
	{
		DEBUG_LOG(("SQLiteArchiveFile::loadProducts - reading %s failed: %s", path.str(), sqlite3_errmsg(database->getHandle())));
		ok = FALSE;
	}

	sqlite3_finalize(stmt);

	if (!ok)
	{
		for (std::vector<SQLiteArchiveFile*>::iterator it = archives.begin(); it != archives.end(); ++it)
			delete *it;
		archives.clear();
	}
	else
	{
		DEBUG_LOG(("SQLiteArchiveFile::loadProducts - indexed %d files in %d products from %s", numFiles, (Int)archives.size(), path.str()));
	}

	database->release();
	return ok;
}

Bool SQLiteArchiveFile::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	const ArchivedFileInfo *tempFileInfo = getArchivedFileInfo(filename);

	if (tempFileInfo == nullptr) {
		return FALSE;
	}

	TheLocalFileSystem->getFileInfo(m_database->getPath(), fileInfo);

	fileInfo->sizeHigh = 0;
	fileInfo->sizeLow = tempFileInfo->m_size;

	return TRUE;
}

File* SQLiteArchiveFile::openFile(const Char *filename, Int access)
{
	const ArchivedFileInfo *fileInfo = getArchivedFileInfo(AsciiString(filename));

	if (fileInfo == nullptr) {
		return nullptr;
	}

	// Streaming requests get a RAM file too: blobs are read whole.
	RAMFile *ramFile = static_cast<RAMFile *>(m_database->readBlob(fileInfo, filename));
	if (ramFile == nullptr) {
		return nullptr;
	}

	if ((access & File::WRITE) == 0) {
		return ramFile;
	}

	constexpr size_t bufferSize = 0;
	File *localFile = TheLocalFileSystem->openFile(filename, access, bufferSize);
	if (localFile != nullptr) {
		ramFile->copyDataToFile(localFile);
	}

	ramFile->close();

	return localFile;
}

void SQLiteArchiveFile::closeAllFiles()
{
}

AsciiString SQLiteArchiveFile::getName()
{
	return m_name;
}

AsciiString SQLiteArchiveFile::getPath()
{
	return m_name;
}

void SQLiteArchiveFile::setSearchPriority(Int new_priority)
{
}

void SQLiteArchiveFile::close()
{
	if (m_database != nullptr)
	{
		m_database->release();
		m_database = nullptr;
	}
}

#endif
