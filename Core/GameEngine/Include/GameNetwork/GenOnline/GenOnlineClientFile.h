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

	// Applies the file rules to the bytes of a file. bFound is FALSE when the file does not exist, bReadable is FALSE when it exists
	// but cannot be read.
	ReadResult Classify(bool bFound, bool bReadable, const std::string& bytes, const std::vector<KnownKey>& knownKeys);

	// Returns TRUE when the file may be written and the document to write differs from the one read from it.
	bool ShouldSave(ReadStatus status, const nlohmann::json& onDisk, const nlohmann::json& toWrite);

	// %LOCALAPPDATA%\GeneralsOnline\client.json, empty when the folder cannot be resolved.
	std::wstring DefaultPath();

	// Reads and classifies the file at path.
	ReadResult Read(const std::wstring& path, const std::vector<KnownKey>& knownKeys);

	// Replaces an existing file through a temporary file in the same folder. Returns FALSE and writes nothing when the file does not exist.
	bool WriteExisting(const std::wstring& path, const std::string& bytes);
}
