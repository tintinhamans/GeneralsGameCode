#pragma once
#include <string>
#include <vector>
#include "GameNetwork/GenOnline/json.hpp"

// The file rules of client.json, shared with the launcher. The client reads the file, never creates it, writes it only when it holds a
// schema_version this client knows and the content changed, and keeps keys it does not know.
namespace GenOnlineClientFile
{
	constexpr int SchemaVersion = 1;
	constexpr const char* SchemaVersionKey = "schema_version";
	constexpr const char* SendCrashReportsKey = "send_crash_reports";

	enum class ReadStatus
	{
		Ok,			// usable and written by this schema version or an older one, the client may save it
		Missing,	// no file, the client uses its built-in defaults and saves nothing
		Unusable,	// the client uses its built-in defaults and leaves the file byte-identical
		Newer,		// a newer schema_version, the client reads it but never writes it
	};

	enum class ValueType
	{
		Object,
		Boolean,
		Number,
		String,
		Array,
	};

	// A key the client reads, as a dotted path from the top level, and the JSON type it must have when present.
	struct KnownKey
	{
		const char* path;
		ValueType type;
	};

	struct ReadResult
	{
		ReadStatus status = ReadStatus::Missing;
		nlohmann::json document = nlohmann::json::object();
		std::string reason;
		int schemaVersion = 0;
	};

	ReadResult Classify(bool bFound, bool bReadable, const std::string& bytes, const std::vector<KnownKey>& knownKeys);

	// Returns TRUE when the file may be written and the document to write differs from the one read from it.
	bool ShouldSave(ReadStatus status, const nlohmann::json& onDisk, const nlohmann::json& toWrite);

	// %LOCALAPPDATA%\GeneralsOnline\client.json, empty when the folder cannot be resolved.
	std::wstring DefaultPath();

	ReadResult Read(const std::wstring& path, const std::vector<KnownKey>& knownKeys);

	// Replaces an existing file through a temporary file in the same folder. Returns FALSE and writes nothing when the file does not exist.
	bool WriteExisting(const std::wstring& path, const std::string& bytes);

	// Returns TRUE when crash reports may be sent. A missing file, an unusable file and a missing key keep crash reports on.
	// A send_crash_reports value other than true turns them off.
	bool SendCrashReports(const ReadResult& file);

	// Reads the file at path for SendCrashReports only, so a wrong type of another key does not matter. Never creates the file.
	bool ReadSendCrashReports(const std::wstring& path);

	// The services this build talks to, "live" or "test". A release candidate tag, vYYYY.M.PATCH-rc.N and nothing after it,
	// gives "test". Every other tag, a development build past a tag and a build without a tag give "live".
	std::string EnvironmentOfTag(const std::string& tag);

	// The network.environment of client.json: "live" or "test". Empty when the file cannot be used, the key is missing, has another
	// type or holds another value. The client never writes this key, it only keeps it.
	std::string EnvironmentOverride(const ReadResult& file);

	// Reads the file at path for EnvironmentOverride only, so a wrong type of another key does not matter. Never creates the file.
	std::string ReadEnvironmentOverride(const std::wstring& path);

	// The override wins over the tag.
	std::string ChooseEnvironment(const std::string& tag, const std::string& overrideValue);

	// Creates the file and its folder with the given content. Returns FALSE and changes nothing when the file already exists.
	bool CreateNew(const std::wstring& path, const std::string& bytes);
}
