/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

//
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace GenOnlinePluginPlatform
{
using Handle = void*;

using Module = void*;

// The volume and file id of a file, which stay the same under every name of the file.
struct FileIdentity
{
	uint64_t volume = 0;
	std::array<uint8_t, 16> id = {};
};

bool IsSameFile(const FileIdentity& a, const FileIdentity& b);

enum class EntryType
{
	File,
	Folder,
	Link,
};

struct FolderEntry
{
	std::string name;
	EntryType type = EntryType::File;
	bool bHasIdentity = false;
	FileIdentity identity;
};

// Paths are UTF-8. JoinPath appends a relative path with backslashes to folder in the native form.
std::string JoinPath(const std::string& folder, const std::string& relativePath);

// Opens a regular file for reading and denies writes, renames and deletes to every other handle.
// A folder or a link is refused.
Handle OpenLockedFile(const std::string& path, std::string& outError);

// Opens a folder and denies renames and deletes of it to every other handle. A link, a file or a folder with
// case sensitive names is refused. A file system that cannot report case sensitivity counts as not case sensitive.
Handle OpenLockedFolder(const std::string& path, std::string& outError);

void CloseHandle(Handle handle);

// Reads the whole file from its start. A file that is empty or larger than maxSize fails.
bool ReadWholeFile(Handle file, uint64_t maxSize, std::string& outData, std::string& outError);

bool ComputeSha256(Handle file, std::array<uint8_t, 32>& outHash);

bool ComputeSha256OfData(const void* data, size_t size, std::array<uint8_t, 32>& outHash);

bool GetFileSize(Handle file, uint64_t& outSize);

bool GetFileIdentity(Handle handle, FileIdentity& outIdentity);

// Lists the entries of a folder without the . and .. entries. Each file entry carries its identity when it can
// be read. maxEntries bounds the list.
bool ListFolder(const std::string& folder, size_t maxEntries, std::vector<FolderEntry>& outEntries, std::string& outError);

// Folds a name the way the file system compares names, so two names that fold to the same text name one entry.
std::string FoldName(const std::string& name);

// The final path of an open file in the form that LoadModule and the logs use.
bool GetFinalPath(Handle handle, std::string& outPath);

// Loads the module from the locked file. Its own folder is the first place that its imports load from.
Module LoadModule(Handle lockedFile, std::string& outError);

void* GetSymbol(Module module, const char* name);

void UnloadModule(Module module);

// True when the loaded module comes from the locked file. The check runs after the module code ran.
bool IsModuleBackedByFile(Module module, Handle lockedFile);
} // namespace GenOnlinePluginPlatform
