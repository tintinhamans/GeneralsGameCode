#include "GameNetwork/GenOnline/GenOnlineClientFile.h"
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

namespace GenOnlineClientFile
{
// A file larger than this is not a settings file.
static const LONGLONG kMaxFileBytes = 16 * 1024 * 1024;

static bool HasType(const nlohmann::json& value, ValueType type)
{
	switch (type)
	{
	case ValueType::Object:
		return value.is_object();
	case ValueType::Boolean:
		return value.is_boolean();
	case ValueType::Number:
		return value.is_number();
	case ValueType::String:
		return value.is_string();
	case ValueType::Array:
		return value.is_array();
	}
	return false;
}

static const char* TypeName(ValueType type)
{
	switch (type)
	{
	case ValueType::Object:
		return "an object";
	case ValueType::Boolean:
		return "a boolean";
	case ValueType::Number:
		return "a number";
	case ValueType::String:
		return "a string";
	case ValueType::Array:
		return "a list";
	}
	return "known";
}

// Finds the value at a dotted path. Returns nullptr when a part is missing or a parent is not an object, the parent has its own entry.
static const nlohmann::json* Find(const nlohmann::json& document, const std::string& path)
{
	const nlohmann::json* node = &document;
	size_t start = 0;
	for (;;)
	{
		const size_t dot = path.find('.', start);
		const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		if (!node->is_object())
		{
			return nullptr;
		}
		const auto found = node->find(part);
		if (found == node->end())
		{
			return nullptr;
		}
		node = &*found;
		if (dot == std::string::npos)
		{
			return node;
		}
		start = dot + 1;
	}
}

static ReadResult Unusable(const std::string& reason)
{
	ReadResult result;
	result.status = ReadStatus::Unusable;
	result.reason = reason;
	return result;
}

ReadResult Classify(bool bFound, bool bReadable, const std::string& bytes, const std::vector<KnownKey>& knownKeys)
{
	if (!bFound)
	{
		return ReadResult();
	}

	if (!bReadable)
	{
		return Unusable("the file cannot be read or is too large");
	}

	nlohmann::json document = nlohmann::json::parse(bytes, nullptr, false);
	if (document.is_discarded())
	{
		return Unusable("the file is not valid JSON");
	}

	if (!document.is_object())
	{
		return Unusable("the top level is not an object");
	}

	// A file without schema_version counts as the current version and gets one on the next save.
	int schemaVersion = SchemaVersion;
	const auto version = document.find(SchemaVersionKey);
	if (version != document.end())
	{
		if (!version->is_number_integer())
		{
			return Unusable(std::string(SchemaVersionKey) + " is not a whole number");
		}
		const long long value = version->get<long long>();
		if (value < 1)
		{
			return Unusable(std::string(SchemaVersionKey) + " " + std::to_string(value) + " is not a known version");
		}
		schemaVersion = value > 0x7fffffff ? 0x7fffffff : static_cast<int>(value);
	}

	for (const KnownKey& key : knownKeys)
	{
		const nlohmann::json* value = Find(document, key.path);
		if (value != nullptr && !HasType(*value, key.type))
		{
			return Unusable(std::string(key.path) + " is not " + TypeName(key.type));
		}
	}

	ReadResult result;
	result.status = schemaVersion > SchemaVersion ? ReadStatus::Newer : ReadStatus::Ok;
	result.document = std::move(document);
	result.schemaVersion = schemaVersion;
	return result;
}

bool ShouldSave(ReadStatus status, const nlohmann::json& onDisk, const nlohmann::json& toWrite)
{
	return status == ReadStatus::Ok && onDisk != toWrite;
}

std::wstring DefaultPath()
{
	PWSTR folder = nullptr;
	std::wstring path;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder)) && folder != nullptr && folder[0] != L'\0')
	{
		path = std::wstring(folder) + L"\\GeneralsOnline\\client.json";
	}
	CoTaskMemFree(folder);
	return path;
}

ReadResult Read(const std::wstring& path, const std::vector<KnownKey>& knownKeys)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		const DWORD error = GetLastError();
		const bool bFound = error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
		return Classify(bFound, false, std::string(), knownKeys);
	}

	std::string bytes;
	bool bReadable = false;
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= kMaxFileBytes)
	{
		bytes.resize(static_cast<size_t>(size.QuadPart));
		DWORD total = 0;
		bReadable = true;
		while (total < bytes.size())
		{
			DWORD read = 0;
			if (!ReadFile(file, &bytes[total], static_cast<DWORD>(bytes.size()) - total, &read, nullptr) || read == 0)
			{
				bReadable = false;
				break;
			}
			total += read;
		}
	}
	CloseHandle(file);
	return Classify(true, bReadable, bytes, knownKeys);
}

bool SendCrashReports(const ReadResult& file)
{
	if (file.status != ReadStatus::Ok && file.status != ReadStatus::Newer)
	{
		return true;
	}
	const auto value = file.document.find(SendCrashReportsKey);
	if (value == file.document.end())
	{
		return true;
	}
	return value->is_boolean() && value->get<bool>();
}

bool ReadSendCrashReports(const std::wstring& path)
{
	if (path.empty())
	{
		return true;
	}
	return SendCrashReports(Read(path, {}));
}

static bool SkipDigits(const std::string& text, size_t& pos, size_t minDigits, size_t maxDigits)
{
	size_t count = 0;
	while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
	{
		++pos;
		++count;
	}
	return count >= minDigits && count <= maxDigits;
}

static bool SkipText(const std::string& text, size_t& pos, const char* expected)
{
	const std::string wanted(expected);
	if (text.compare(pos, wanted.size(), wanted) != 0)
	{
		return false;
	}
	pos += wanted.size();
	return true;
}

std::string EnvironmentOfTag(const std::string& tag)
{
	size_t pos = 0;
	const bool bCandidate = SkipText(tag, pos, "v") && SkipDigits(tag, pos, 4, 4) && SkipText(tag, pos, ".") && SkipDigits(tag, pos, 1, 2)
		&& SkipText(tag, pos, ".") && SkipDigits(tag, pos, 1, 9) && SkipText(tag, pos, "-rc.") && SkipDigits(tag, pos, 1, 9) && pos == tag.size();
	return bCandidate ? "test" : "live";
}

std::string EnvironmentOverride(const ReadResult& file)
{
	if (file.status != ReadStatus::Ok && file.status != ReadStatus::Newer)
	{
		return std::string();
	}
	const auto network = file.document.find("network");
	if (network == file.document.end() || !network->is_object())
	{
		return std::string();
	}
	const auto value = network->find("environment");
	if (value == network->end() || !value->is_string())
	{
		return std::string();
	}
	const std::string environment = value->get<std::string>();
	return environment == "live" || environment == "test" ? environment : std::string();
}

std::string ReadEnvironmentOverride(const std::wstring& path)
{
	if (path.empty())
	{
		return std::string();
	}
	return EnvironmentOverride(Read(path, {}));
}

std::string ChooseEnvironment(const std::string& tag, const std::string& overrideValue)
{
	return overrideValue.empty() ? EnvironmentOfTag(tag) : overrideValue;
}

bool CreateNew(const std::wstring& path, const std::string& bytes)
{
	const size_t slash = path.find_last_of(L"\\/");
	if (path.empty() || slash == std::wstring::npos)
	{
		return false;
	}
	const std::wstring folder = path.substr(0, slash);
	if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		return false;
	}

	// CREATE_NEW fails on an existing file, so a file that appears in the meantime is never replaced.
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	bool bWritten = true;
	size_t total = 0;
	while (total < bytes.size())
	{
		DWORD written = 0;
		if (!WriteFile(file, bytes.data() + total, static_cast<DWORD>(bytes.size() - total), &written, nullptr) || written == 0)
		{
			bWritten = false;
			break;
		}
		total += written;
	}
	if (bWritten && !FlushFileBuffers(file))
	{
		bWritten = false;
	}
	if (!CloseHandle(file))
	{
		bWritten = false;
	}
	if (!bWritten)
	{
		DeleteFileW(path.c_str());
	}
	return bWritten;
}

bool WriteExisting(const std::wstring& path, const std::string& bytes)
{
	if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		return false;
	}

	// The process id keeps two clients that save at once from sharing a temporary file.
	const std::wstring tempPath = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	bool bWritten = true;
	size_t total = 0;
	while (total < bytes.size())
	{
		DWORD written = 0;
		if (!WriteFile(file, bytes.data() + total, static_cast<DWORD>(bytes.size() - total), &written, nullptr) || written == 0)
		{
			bWritten = false;
			break;
		}
		total += written;
	}
	if (bWritten && !FlushFileBuffers(file))
	{
		bWritten = false;
	}
	if (!CloseHandle(file))
	{
		bWritten = false;
	}

	if (bWritten)
	{
		bWritten = MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}

	if (!bWritten)
	{
		DeleteFileW(tempPath.c_str());
	}
	return bWritten;
}
}
