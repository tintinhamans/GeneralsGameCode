#include "GameNetwork/GenOnline/GenOnlinePackList.h"
#include "GameNetwork/GenOnline/json.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>

namespace GenOnlinePackList
{
static const LONGLONG kMaxFileBytes = 16 * 1024 * 1024;

static Result Refuse(Status status, const std::string& line)
{
	Result result;
	result.status = status;
	result.log.push_back(line);
	return result;
}

// A JSON whole number, a value above the range of long long reads as its maximum.
static bool ReadWholeNumber(const nlohmann::json& value, long long& result)
{
	if (value.is_number_unsigned())
	{
		result = static_cast<long long>((std::min)(value.get<unsigned long long>(), 0x7fffffffffffffffULL));
		return true;
	}
	if (value.is_number_integer())
	{
		result = value.get<long long>();
		return true;
	}
	return false;
}

static bool ParseTier(const std::string& text, Tier& tier)
{
	if (text == "community_patch")
	{
		tier = Tier::CommunityPatch;
		return true;
	}
	if (text == "generals_online")
	{
		tier = Tier::GeneralsOnline;
		return true;
	}
	if (text == "user")
	{
		tier = Tier::User;
		return true;
	}
	return false;
}

static bool IsAbsoluteFolder(const std::string& folder)
{
	const bool drive = folder.size() >= 3 && isalpha(static_cast<unsigned char>(folder[0])) && folder[1] == ':' && (folder[2] == '\\' || folder[2] == '/');
	const bool unc = folder.size() > 2 && (folder[0] == '\\' || folder[0] == '/') && (folder[1] == '\\' || folder[1] == '/');
	if (!drive && !unc)
	{
		return false;
	}

	// A . or .. part would leave the folder that the launcher named.
	size_t start = 0;
	while (start <= folder.size())
	{
		size_t end = folder.find_first_of("\\/", start);
		if (end == std::string::npos)
		{
			end = folder.size();
		}
		const std::string part = folder.substr(start, end - start);
		if (part == "." || part == "..")
		{
			return false;
		}
		start = end + 1;
	}
	return true;
}

// The folder in the code page of the file system calls of the game. Returns FALSE when a character has no equivalent there.
static bool ToFileSystemCodePage(const std::string& utf8, std::string& result)
{
	result.clear();
	if (utf8.empty())
	{
		return true;
	}
	const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (wideLength <= 0)
	{
		return false;
	}
	std::wstring wide(static_cast<size_t>(wideLength), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &wide[0], wideLength);
	BOOL usedDefault = FALSE;
	const int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), wideLength, nullptr, 0, nullptr, &usedDefault);
	if (length <= 0 || usedDefault)
	{
		return false;
	}
	result.assign(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), wideLength, &result[0], length, nullptr, &usedDefault);
	return !usedDefault;
}

static std::string NormalizeFolder(const std::string& folder)
{
	std::string result = folder;
	std::replace(result.begin(), result.end(), '/', '\\');
	while (result.size() > 3 && result[result.size() - 1] == '\\')
	{
		result.erase(result.size() - 1);
	}
	return result;
}

// An exact game path or a folder ending in /**, relative, without wildcards elsewhere and without . or .. components.
static bool ParseExclude(const std::string& text, std::string& exclude, std::string& reason)
{
	std::string value = NormalizePath(text);
	const bool isFolder = value.size() > 3 && value.compare(value.size() - 3, 3, "/**") == 0;
	if (isFolder)
	{
		value.erase(value.size() - 3);
	}
	if (value.empty())
	{
		reason = "empty";
		return false;
	}
	if (value[0] == '/' || value.find(':') != std::string::npos)
	{
		reason = "not a relative path";
		return false;
	}
	for (size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(value[i]);
		if (c < 32 || c == '*' || c == '?' || c == '<' || c == '>' || c == '"' || c == '|')
		{
			reason = "invalid character, only an exact path or dir/** is allowed";
			return false;
		}
	}
	size_t start = 0;
	while (start <= value.size())
	{
		size_t end = value.find('/', start);
		if (end == std::string::npos)
		{
			end = value.size();
		}
		const std::string component = value.substr(start, end - start);
		if (component.empty() || component == "." || component == "..")
		{
			reason = "empty, . or .. folder";
			return false;
		}
		start = end + 1;
	}
	exclude = isFolder ? value + "/**" : value;
	return true;
}

std::string NormalizePath(const std::string& path)
{
	std::string result = path;
	for (size_t i = 0; i < result.size(); ++i)
	{
		if (result[i] == '\\')
		{
			result[i] = '/';
		}
		result[i] = static_cast<char>(tolower(static_cast<unsigned char>(result[i])));
	}
	return result;
}

// key is a path in the form of NormalizePath.
static bool ExcludeMatchesKey(const std::string& exclude, const std::string& key)
{
	if (exclude.size() > 3 && exclude.compare(exclude.size() - 3, 3, "/**") == 0)
	{
		const size_t prefixLength = exclude.size() - 2;
		return key.size() > prefixLength && key.compare(0, prefixLength, exclude, 0, prefixLength) == 0;
	}
	return key == exclude;
}

bool ExcludeMatches(const std::string& exclude, const std::string& path)
{
	return ExcludeMatchesKey(exclude, NormalizePath(path));
}

bool IsHidden(const std::vector<Pack>& stack, size_t firstAbove, const std::string& path)
{
	std::string key;
	for (size_t i = firstAbove; i < stack.size(); ++i)
	{
		for (size_t e = 0; e < stack[i].excludes.size(); ++e)
		{
			if (key.empty())
			{
				key = NormalizePath(path);
			}
			if (ExcludeMatchesKey(stack[i].excludes[e], key))
			{
				return true;
			}
		}
	}
	return false;
}

size_t FirstPackAbove(const std::vector<int>& packPriorities, int stride, int priority)
{
	size_t firstAbove = 0;
	while (firstAbove < packPriorities.size() && packPriorities[firstAbove] + stride > priority)
	{
		++firstAbove;
	}
	return firstAbove;
}

bool FolderExists(const std::string& folder)
{
	const DWORD attributes = GetFileAttributesA(folder.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::vector<std::string> GameFolderExcludes(const std::vector<Pack>& stack)
{
	std::vector<std::string> entries;
	for (size_t i = 0; i < stack.size(); ++i)
	{
		for (size_t e = 0; e < stack[i].excludes.size(); ++e)
		{
			std::string entry = "game:" + stack[i].excludes[e];
			if (entry.size() > 3 && entry.compare(entry.size() - 3, 3, "/**") == 0)
			{
				entry.erase(entry.size() - 2);
			}
			if (std::find(entries.begin(), entries.end(), entry) == entries.end())
			{
				entries.push_back(entry);
			}
		}
	}
	return entries;
}

enum class PackEntry
{
	Enabled,
	Disabled,
	Invalid,
};

// Reads one entry of packs. An invalid entry gets the log line that refuses the list, a disabled one the line that skips it.
static PackEntry ParsePack(const nlohmann::json& entry, size_t index, FolderExistsFunc folderExists, Pack& pack, std::string& line)
{
	const std::string where = "Pack list: pack " + std::to_string(index);
	const std::string refused = ", using the built-in packs";
	if (!entry.is_object())
	{
		line = where + " is not an object" + refused;
		return PackEntry::Invalid;
	}
	if (!entry.contains("id") || !entry["id"].is_string() || entry["id"].get<std::string>().empty())
	{
		line = where + ": id is missing or not a string" + refused;
		return PackEntry::Invalid;
	}
	pack.id = entry["id"].get<std::string>();
	const std::string name = where + " (" + pack.id + ")";

	if (!entry.contains("enabled") || !entry["enabled"].is_boolean())
	{
		line = name + ": enabled is missing or not a boolean" + refused;
		return PackEntry::Invalid;
	}
	if (!entry.contains("tier") || !entry["tier"].is_string() || !ParseTier(entry["tier"].get<std::string>(), pack.tier))
	{
		line = name + ": tier is not user, generals_online or community_patch" + refused;
		return PackEntry::Invalid;
	}
	long long order = 0;
	if (!entry.contains("order") || !ReadWholeNumber(entry["order"], order))
	{
		line = name + ": order is missing or not a whole number" + refused;
		return PackEntry::Invalid;
	}
	pack.order = static_cast<int>((std::max)(-0x7fffffffLL, (std::min)(order, 0x7fffffffLL)));

	if (!entry.contains("folder") || !entry["folder"].is_string() || !IsAbsoluteFolder(entry["folder"].get<std::string>()))
	{
		line = name + ": folder is missing, not an absolute path or has a . or .. part" + refused;
		return PackEntry::Invalid;
	}
	std::string folder;
	if (!ToFileSystemCodePage(entry["folder"].get<std::string>(), folder))
	{
		line = name + ": folder has characters the game cannot open" + refused;
		return PackEntry::Invalid;
	}
	pack.folder = NormalizeFolder(folder);

	if (entry.contains("exclude"))
	{
		const nlohmann::json& excludes = entry["exclude"];
		if (!excludes.is_array())
		{
			line = name + ": exclude is not a list" + refused;
			return PackEntry::Invalid;
		}
		// A community_patch pack cannot exclude, its list is ignored and its entries are not read.
		if (pack.tier != Tier::CommunityPatch)
		{
			for (const auto& exclude : excludes)
			{
				std::string value;
				std::string reason = "not a string";
				if (!exclude.is_string() || !ParseExclude(exclude.get<std::string>(), value, reason))
				{
					line = name + ": exclude entry " + (exclude.is_string() ? exclude.get<std::string>() : exclude.dump()) + " is invalid (" + reason + ")" + refused;
					return PackEntry::Invalid;
				}
				pack.excludes.push_back(value);
			}
		}
	}

	if (!entry["enabled"].get<bool>())
	{
		line = name + " is disabled";
		return PackEntry::Disabled;
	}
	if (!folderExists(pack.folder))
	{
		line = name + ": folder " + pack.folder + " does not exist" + refused;
		return PackEntry::Invalid;
	}
	if (pack.tier == Tier::CommunityPatch && entry.contains("exclude") && !entry["exclude"].empty())
	{
		line = name + ": exclude is ignored, community_patch packs cannot exclude";
	}
	return PackEntry::Enabled;
}

Result Parse(const std::string& text, const char* game, FolderExistsFunc folderExists)
{
	nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
	if (document.is_discarded() || !document.is_object())
	{
		return Refuse(Status::Unusable, "Pack list: not a JSON object, using the built-in packs");
	}
	long long schemaVersion = 0;
	if (!document.contains("schema_version") || !ReadWholeNumber(document["schema_version"], schemaVersion) || schemaVersion < 1)
	{
		return Refuse(Status::Unusable, "Pack list: schema_version is missing or not a whole number above 0, using the built-in packs");
	}
	if (schemaVersion > SchemaVersion)
	{
		return Refuse(Status::Newer, "Pack list: schema_version " + std::to_string(schemaVersion) + " is newer than this client knows, using the built-in packs");
	}
	if (!document.contains("game") || !document["game"].is_string())
	{
		return Refuse(Status::Unusable, "Pack list: game is missing or not a string, using the built-in packs");
	}
	if (document["game"].get<std::string>() != game)
	{
		return Refuse(Status::GameMismatch, "Pack list: written for " + document["game"].get<std::string>() + ", this is " + game + ", using the built-in packs");
	}
	if (!document.contains("packs") || !document["packs"].is_array())
	{
		return Refuse(Status::Unusable, "Pack list: packs is missing or not a list, using the built-in packs");
	}

	// Any invalid entry refuses the whole list, so a refused list never mounts a part of the stack.
	Result result;
	result.status = Status::Ok;
	const nlohmann::json& packs = document["packs"];
	for (size_t i = 0; i < packs.size(); ++i)
	{
		Pack pack;
		std::string line;
		const PackEntry entry = ParsePack(packs[i], i, folderExists, pack, line);
		if (entry == PackEntry::Invalid)
		{
			return Refuse(Status::Invalid, line);
		}
		if (!line.empty())
		{
			result.log.push_back(line);
		}
		if (entry == PackEntry::Enabled)
		{
			result.stack.push_back(pack);
		}
	}
	std::stable_sort(result.stack.begin(), result.stack.end(), [](const Pack& a, const Pack& b)
	{
		if (a.tier != b.tier)
		{
			return a.tier < b.tier;
		}
		return a.order < b.order;
	});
	return result;
}

Result ReadFile(const std::string& path, const char* game, FolderExistsFunc folderExists)
{
	HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return Refuse(Status::Unusable, "Pack list: " + path + " cannot be opened, using the built-in packs");
	}

	std::string bytes;
	LARGE_INTEGER size;
	bool bReadable = GetFileSizeEx(file, &size) != FALSE && size.QuadPart <= kMaxFileBytes;
	if (bReadable)
	{
		bytes.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		bReadable = bytes.empty() || (::ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE && read == bytes.size());
	}
	CloseHandle(file);
	if (!bReadable)
	{
		return Refuse(Status::Unusable, "Pack list: " + path + " cannot be read, using the built-in packs");
	}
	return Parse(bytes, game, folderExists);
}
}
