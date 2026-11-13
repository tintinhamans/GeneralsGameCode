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

// FILE: GenOnlinePluginPlatform.h ////////////////////////////////////////////
// Description: Operating system layer of the GenOnline plugin trust check and
//              plugin loader. The trust check and the manifest and signature
//              code call only these functions, so another platform implements
//              this file and nothing else.
//
//              GenOnlinePluginPlatformWin32.cpp implements it with CreateFileW
//              share modes, FILE_ID_INFO, BCrypt, GetFinalPathNameByHandleW
//              and LoadLibraryExW.
//
//              A Linux implementation opens every file and folder with open
//              and O_NOFOLLOW, O_CLOEXEC and O_DIRECTORY for folders, and walks
//              a folder with openat and fdopendir on the locked folder fd. The
//              identity of a file is st_dev plus st_ino from fstat. Linux has
//              no share modes, so the Linux layer hashes the bytes it reads
//              into memory with the OpenSSL SHA256 that curl already links,
//              writes those bytes into a memfd_create file and seals it with
//              F_SEAL_WRITE, F_SEAL_GROW and F_SEAL_SHRINK. LoadModule calls
//              dlopen on /proc/self/fd/N of that memfd, so the loaded bytes are
//              the verified bytes. GetSymbol calls dlsym, UnloadModule calls
//              dlclose, and IsModuleBackedByFile compares the fstat identity of
//              the memfd with the file that dl_iterate_phdr reports for the
//              module. The plugin libraries carry an RPATH of $ORIGIN, which
//              the plugin build sets, so they find their dependencies next to
//              them. The layer loads every listed dependency through its own
//              memfd before the main library, so the dynamic loader finds the
//              verified copies by their sonames. Names compare case
//              sensitively, so FoldName returns the name as is.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace GenOnlinePluginPlatform
{
// An open file or folder. nullptr is no handle.
using Handle = void*;

// A loaded plugin module. nullptr is no module.
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

// Hashes the whole file from its start.
bool ComputeSha256(Handle file, std::array<uint8_t, 32>& outHash);

// Hashes size bytes at data.
bool ComputeSha256OfData(const void* data, size_t size, std::array<uint8_t, 32>& outHash);

// The size in bytes of an open file.
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
