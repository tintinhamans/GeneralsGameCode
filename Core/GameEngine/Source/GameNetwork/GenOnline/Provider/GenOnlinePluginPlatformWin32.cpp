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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlinePluginPlatform.h"

#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <cstring>

namespace GenOnlinePluginPlatform
{
namespace
{
std::wstring ToWide(const std::string& text)
{
	if (text.empty())
	{
		return std::wstring();
	}

	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	if (size <= 0)
	{
		return std::wstring();
	}

	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), result.data(), size);
	return result;
}

std::string ToUtf8(const std::wstring& text)
{
	if (text.empty())
	{
		return std::string();
	}

	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0)
	{
		return std::string();
	}

	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
	return result;
}

std::string DescribeLastError(const std::string& what)
{
	return what + " (" + std::to_string(GetLastError()) + ")";
}

bool GetFinalPathWide(HANDLE hFile, DWORD flags, std::wstring& outPath)
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		const DWORD length = GetFinalPathNameByHandleW(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
		if (length == 0)
		{
			return false;
		}

		if (length < buffer.size())
		{
			outPath.assign(buffer.data(), length);
			return true;
		}

		buffer.resize(static_cast<size_t>(length) + 1);
	}
}

// Turns a final path with the \\?\ prefix into the path that the loader gets. A path shorter than MAX_PATH drops
// the prefix and \\?\UNC\ turns back into \\, so the folder of the DLL is a plain path when the loader searches its
// imports. A longer path keeps the prefix.
std::wstring ToLoaderPath(const std::wstring& finalPath)
{
	static const wchar_t kUncPrefix[] = L"\\\\?\\UNC\\";
	static const wchar_t kPrefix[] = L"\\\\?\\";
	std::wstring plainPath;
	if (finalPath.compare(0, std::size(kUncPrefix) - 1, kUncPrefix) == 0)
	{
		plainPath = L"\\\\" + finalPath.substr(std::size(kUncPrefix) - 1);
	}
	else if (finalPath.compare(0, std::size(kPrefix) - 1, kPrefix) == 0)
	{
		plainPath = finalPath.substr(std::size(kPrefix) - 1);
	}
	else
	{
		return finalPath;
	}

	return plainPath.size() < MAX_PATH ? plainPath : finalPath;
}

bool GetModulePath(HMODULE hModule, std::wstring& outPath)
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0)
		{
			return false;
		}

		if (length < buffer.size())
		{
			outPath.assign(buffer.data(), length);
			return true;
		}

		buffer.resize(buffer.size() * 2);
	}
}

bool GetMappedPath(HMODULE hModule, std::wstring& outPath)
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		const DWORD length = K32GetMappedFileNameW(GetCurrentProcess(), hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0)
		{
			return false;
		}

		if (length + 1 < buffer.size())
		{
			outPath.assign(buffer.data(), length);
			return true;
		}

		buffer.resize(buffer.size() * 2);
	}
}

bool IdentityOfPath(const std::wstring& path, FileIdentity& outIdentity)
{
	HANDLE hFile = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	const bool bSuccess = GetFileIdentity(hFile, outIdentity);
	::CloseHandle(hFile);
	return bSuccess;
}
} // namespace

bool IsSameFile(const FileIdentity& a, const FileIdentity& b)
{
	return a.volume == b.volume && a.id == b.id;
}

std::string JoinPath(const std::string& folder, const std::string& relativePath)
{
	std::string path = folder;
	path.push_back('\\');
	for (char c : relativePath)
	{
		path.push_back(c == '/' ? '\\' : c);
	}
	return path;
}

Handle OpenLockedFile(const std::string& path, std::string& outError)
{
	HANDLE hFile = CreateFileW(ToWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		outError = DescribeLastError("cannot open " + path);
		return nullptr;
	}

	BY_HANDLE_FILE_INFORMATION info = {};
	if (!GetFileInformationByHandle(hFile, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
	{
		outError = path + " is a link or a folder";
		::CloseHandle(hFile);
		return nullptr;
	}

	return hFile;
}

Handle OpenLockedFolder(const std::string& path, std::string& outError)
{
	HANDLE hFolder = CreateFileW(ToWide(path).c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (hFolder == INVALID_HANDLE_VALUE)
	{
		outError = DescribeLastError("cannot lock the folder " + path);
		return nullptr;
	}

	BY_HANDLE_FILE_INFORMATION info = {};
	if (!GetFileInformationByHandle(hFolder, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		outError = path + " is a link or not a folder";
		::CloseHandle(hFolder);
		return nullptr;
	}

	// Older Windows versions, Wine and file systems without the flag can fail the query, and then the folder counts
	// as not case sensitive. The folder walk still refuses names that fold to the same name.
	FILE_CASE_SENSITIVE_INFO caseInfo = {};
	if (GetFileInformationByHandleEx(hFolder, FileCaseSensitiveInfo, &caseInfo, sizeof(caseInfo)) && (caseInfo.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0)
	{
		outError = "the folder " + path + " has case sensitive names";
		::CloseHandle(hFolder);
		return nullptr;
	}

	return hFolder;
}

void CloseHandle(Handle handle)
{
	if (handle != nullptr)
	{
		::CloseHandle(static_cast<HANDLE>(handle));
	}
}

bool ReadWholeFile(Handle file, uint64_t maxSize, std::string& outData, std::string& outError)
{
	HANDLE hFile = static_cast<HANDLE>(file);
	LARGE_INTEGER size = {};
	if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || static_cast<uint64_t>(size.QuadPart) > maxSize)
	{
		outError = "the file is empty or larger than " + std::to_string(maxSize) + " bytes";
		return false;
	}

	LARGE_INTEGER start = {};
	if (!SetFilePointerEx(hFile, start, nullptr, FILE_BEGIN))
	{
		outError = DescribeLastError("cannot seek the file");
		return false;
	}

	outData.resize(static_cast<size_t>(size.QuadPart));
	DWORD bytesRead = 0;
	if (!ReadFile(hFile, outData.data(), static_cast<DWORD>(outData.size()), &bytesRead, nullptr) || bytesRead != outData.size())
	{
		outError = DescribeLastError("cannot read the file");
		return false;
	}

	return true;
}

bool ComputeSha256(Handle file, std::array<uint8_t, 32>& outHash)
{
	HANDLE hFile = static_cast<HANDLE>(file);
	LARGE_INTEGER start = {};
	if (!SetFilePointerEx(hFile, start, nullptr, FILE_BEGIN))
	{
		return false;
	}

	BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
	{
		return false;
	}

	bool bSuccess = false;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	if (BCRYPT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, nullptr, 0, nullptr, 0, 0)))
	{
		std::vector<uint8_t> buffer(64 * 1024);
		bSuccess = true;
		for (;;)
		{
			DWORD bytesRead = 0;
			if (!ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
			{
				bSuccess = false;
				break;
			}

			if (bytesRead == 0)
			{
				break;
			}

			if (!BCRYPT_SUCCESS(BCryptHashData(hHash, buffer.data(), bytesRead, 0)))
			{
				bSuccess = false;
				break;
			}
		}

		if (bSuccess)
		{
			bSuccess = BCRYPT_SUCCESS(BCryptFinishHash(hHash, outHash.data(), static_cast<ULONG>(outHash.size()), 0));
		}

		BCryptDestroyHash(hHash);
	}

	BCryptCloseAlgorithmProvider(hAlgorithm, 0);
	return bSuccess;
}

bool ComputeSha256OfData(const void* data, size_t size, std::array<uint8_t, 32>& outHash)
{
	BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
	{
		return false;
	}

	bool bSuccess = false;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	if (BCRYPT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, nullptr, 0, nullptr, 0, 0)))
	{
		bSuccess = true;
		const uint8_t* pBytes = static_cast<const uint8_t*>(data);
		size_t remaining = size;
		while (remaining > 0)
		{
			const ULONG chunk = static_cast<ULONG>(remaining < 64 * 1024 ? remaining : 64 * 1024);
			if (!BCRYPT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(pBytes), chunk, 0)))
			{
				bSuccess = false;
				break;
			}

			pBytes += chunk;
			remaining -= chunk;
		}

		if (bSuccess)
		{
			bSuccess = BCRYPT_SUCCESS(BCryptFinishHash(hHash, outHash.data(), static_cast<ULONG>(outHash.size()), 0));
		}

		BCryptDestroyHash(hHash);
	}

	BCryptCloseAlgorithmProvider(hAlgorithm, 0);
	return bSuccess;
}

bool GetFileSize(Handle file, uint64_t& outSize)
{
	LARGE_INTEGER size = {};
	if (!GetFileSizeEx(static_cast<HANDLE>(file), &size) || size.QuadPart < 0)
	{
		return false;
	}

	outSize = static_cast<uint64_t>(size.QuadPart);
	return true;
}

bool GetFileIdentity(Handle handle, FileIdentity& outIdentity)
{
	HANDLE hFile = static_cast<HANDLE>(handle);
	FILE_ID_INFO idInfo = {};
	if (GetFileInformationByHandleEx(hFile, FileIdInfo, &idInfo, sizeof(idInfo)))
	{
		outIdentity.volume = idInfo.VolumeSerialNumber;
		static_assert(sizeof(idInfo.FileId.Identifier) == 16, "FILE_ID_128 holds 16 bytes");
		std::memcpy(outIdentity.id.data(), idInfo.FileId.Identifier, outIdentity.id.size());
		return true;
	}

	// Systems and volumes that do not report 128 bit file ids use the volume serial and the 64 bit file index instead.
	BY_HANDLE_FILE_INFORMATION info = {};
	if (!GetFileInformationByHandle(hFile, &info))
	{
		return false;
	}

	outIdentity.volume = info.dwVolumeSerialNumber;
	outIdentity.id = {};
	const uint64_t index = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
	std::memcpy(outIdentity.id.data(), &index, sizeof(index));
	return true;
}

bool ListFolder(const std::string& folder, size_t maxEntries, std::vector<FolderEntry>& outEntries, std::string& outError)
{
	outEntries.clear();
	const std::wstring wideFolder = ToWide(folder);
	WIN32_FIND_DATAW findData = {};
	HANDLE hFind = FindFirstFileExW((wideFolder + L"\\*").c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		outError = DescribeLastError("cannot list " + folder);
		return false;
	}

	bool bSuccess = true;
	do
	{
		const std::wstring entryName = findData.cFileName;
		if (entryName == L"." || entryName == L"..")
		{
			continue;
		}

		if (outEntries.size() >= maxEntries)
		{
			outError = folder + " holds more than " + std::to_string(maxEntries) + " entries";
			bSuccess = false;
			break;
		}

		FolderEntry entry;
		entry.name = ToUtf8(entryName);
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			entry.type = EntryType::Link;
		}
		else if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			entry.type = EntryType::Folder;
		}
		else
		{
			entry.type = EntryType::File;
			entry.bHasIdentity = IdentityOfPath(wideFolder + L"\\" + entryName, entry.identity);
		}

		outEntries.push_back(entry);
	}
	while (FindNextFileW(hFind, &findData));

	if (bSuccess && GetLastError() != ERROR_NO_MORE_FILES)
	{
		outError = "cannot list " + folder + " to its end";
		bSuccess = false;
	}

	FindClose(hFind);
	return bSuccess;
}

std::string FoldName(const std::string& name)
{
	const std::wstring wideName = ToWide(name);
	std::wstring folded(wideName);
	if (!wideName.empty())
	{
		LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wideName.c_str(), static_cast<int>(wideName.size()), folded.data(), static_cast<int>(folded.size()), nullptr, nullptr, 0);
	}
	return ToUtf8(folded);
}

bool GetFinalPath(Handle handle, std::string& outPath)
{
	std::wstring finalPath;
	if (!GetFinalPathWide(static_cast<HANDLE>(handle), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS, finalPath))
	{
		return false;
	}

	outPath = ToUtf8(ToLoaderPath(finalPath));
	return true;
}

Module LoadModule(Handle lockedFile, std::string& outError)
{
	// The load path is the final path of the locked file, so the loader opens the file that was hashed.
	std::wstring finalPath;
	if (!GetFinalPathWide(static_cast<HANDLE>(lockedFile), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS, finalPath))
	{
		outError = DescribeLastError("cannot resolve the final path of the locked file");
		return nullptr;
	}

	HMODULE hModule = LoadLibraryExW(ToLoaderPath(finalPath).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (hModule == nullptr)
	{
		outError = DescribeLastError("LoadLibraryExW failed");
	}

	return hModule;
}

void* GetSymbol(Module module, const char* name)
{
	return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}

void UnloadModule(Module module)
{
	if (module != nullptr)
	{
		FreeLibrary(static_cast<HMODULE>(module));
	}
}

bool IsModuleBackedByFile(Module module, Handle lockedFile)
{
	if (module == nullptr || lockedFile == nullptr)
	{
		return false;
	}

	HMODULE hModule = static_cast<HMODULE>(module);
	HANDLE hLocked = static_cast<HANDLE>(lockedFile);

	// The mapped name comes from the file object behind the image section, not from the path that the loader was given.
	std::wstring mappedPath;
	std::wstring lockedPath;
	if (GetMappedPath(hModule, mappedPath) && GetFinalPathWide(hLocked, FILE_NAME_NORMALIZED | VOLUME_NAME_NT, lockedPath)
		&& CompareStringOrdinal(mappedPath.c_str(), static_cast<int>(mappedPath.size()), lockedPath.c_str(), static_cast<int>(lockedPath.size()), TRUE) == CSTR_EQUAL)
	{
		return true;
	}

	// Wine and older systems can name the section in another form. The module path then has to lead to the locked
	// file itself. The locked file and its folder chain deny renames and deletes, so the path cannot lead elsewhere.
	std::wstring modulePath;
	FileIdentity moduleIdentity;
	FileIdentity lockedIdentity;
	return GetModulePath(hModule, modulePath)
		&& IdentityOfPath(modulePath, moduleIdentity)
		&& GetFileIdentity(lockedFile, lockedIdentity)
		&& IsSameFile(moduleIdentity, lockedIdentity);
}
} // namespace GenOnlinePluginPlatform
