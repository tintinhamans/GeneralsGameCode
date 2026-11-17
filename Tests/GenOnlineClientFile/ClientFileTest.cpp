// Standalone checks of the client.json file rules in GenOnlineClientFile. Builds without the game, see CMakeLists.txt.
#include "GameNetwork/GenOnline/GenOnlineClientFile.h"
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace GenOnlineClientFile;

static int s_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			++s_failures; \
		} \
	} while (0)

static const std::vector<KnownKey> kKeys = {
	{ "network", ValueType::Object },
	{ "network.proxy", ValueType::Object },
	{ "network.proxy.mode", ValueType::String },
	{ "network.proxy.windows_login", ValueType::Boolean },
	{ "render.fps_limit", ValueType::Number },
	{ "maps.favourites", ValueType::Array },
};

static ReadResult ClassifyText(const std::string& text)
{
	return Classify(true, true, text, kKeys);
}

static std::string ReadAll(const std::wstring& path)
{
	std::ifstream file(path.c_str(), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void WriteAll(const std::wstring& path, const std::string& bytes)
{
	std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
	file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static void TestClassify()
{
	CHECK(Classify(false, false, std::string(), kKeys).status == ReadStatus::Missing);
	CHECK(Classify(true, false, std::string(), kKeys).status == ReadStatus::Unusable);

	CHECK(ClassifyText("").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{ \"network\": ").status == ReadStatus::Unusable);
	CHECK(ClassifyText("[1, 2]").status == ReadStatus::Unusable);
	CHECK(ClassifyText("\"text\"").status == ReadStatus::Unusable);

	CHECK(ClassifyText("{\"schema_version\": \"1\"}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"schema_version\": 1.5}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"schema_version\": 0}").status == ReadStatus::Unusable);

	// A key with the wrong type makes the whole file unusable, at any depth.
	CHECK(ClassifyText("{\"network\": 5}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"network\": {\"proxy\": []}}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"network\": {\"proxy\": {\"windows_login\": \"yes\"}}}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"render\": {\"fps_limit\": true}}").status == ReadStatus::Unusable);
	CHECK(ClassifyText("{\"maps\": {\"favourites\": {}}}").status == ReadStatus::Unusable);
	const ReadResult wrongType = ClassifyText("{\"network\": {\"proxy\": {\"mode\": 3}}}");
	CHECK(wrongType.status == ReadStatus::Unusable);
	CHECK(wrongType.reason == "network.proxy.mode is not a string");

	// A parent that is not an object hides its children, its own entry reports it.
	CHECK(ClassifyText("{\"render\": 7}").status == ReadStatus::Ok);

	const ReadResult ok = ClassifyText("{\"schema_version\": 1, \"network\": {\"proxy\": {\"mode\": \"custom\", \"windows_login\": false}}, \"render\": {\"fps_limit\": 60}, \"unknown\": {\"x\": [1]}}");
	CHECK(ok.status == ReadStatus::Ok);
	CHECK(ok.schemaVersion == 1);
	CHECK(ok.document["unknown"]["x"][0] == 1);

	const ReadResult noVersion = ClassifyText("{}");
	CHECK(noVersion.status == ReadStatus::Ok);
	CHECK(noVersion.schemaVersion == SchemaVersion);

	const ReadResult newer = ClassifyText("{\"schema_version\": 2, \"network\": {\"proxy\": {\"mode\": \"automatic\"}}}");
	CHECK(newer.status == ReadStatus::Newer);
	CHECK(newer.schemaVersion == 2);
	CHECK(newer.document["network"]["proxy"]["mode"] == "automatic");
	CHECK(ClassifyText("{\"schema_version\": 2, \"network\": 1}").status == ReadStatus::Unusable);
}

static void TestShouldSave()
{
	const nlohmann::json onDisk = nlohmann::json::parse("{\"schema_version\": 1, \"a\": 1, \"b\": 100}");
	nlohmann::json same = nlohmann::json::parse("{\"b\": 100.0, \"a\": 1, \"schema_version\": 1}");
	nlohmann::json changed = onDisk;
	changed["a"] = 2;

	CHECK(!ShouldSave(ReadStatus::Ok, onDisk, same));
	CHECK(ShouldSave(ReadStatus::Ok, onDisk, changed));
	CHECK(!ShouldSave(ReadStatus::Newer, onDisk, changed));
	CHECK(!ShouldSave(ReadStatus::Unusable, onDisk, changed));
	CHECK(!ShouldSave(ReadStatus::Missing, onDisk, changed));
}

static void TestFiles()
{
	wchar_t tempRoot[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tempRoot);
	const std::wstring folder = std::wstring(tempRoot) + L"genonline-client-file-test-" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(folder.c_str(), nullptr);
	const std::wstring path = folder + L"\\client.json";
	DeleteFileW(path.c_str());

	// The client never creates the file.
	CHECK(Read(path, kKeys).status == ReadStatus::Missing);
	CHECK(!WriteExisting(path, "{}"));
	CHECK(CreateNew(folder + L"\\created\\client.json", "{\"schema_version\": 1}"));
	CHECK(Read(folder + L"\\created\\client.json", kKeys).status == ReadStatus::Ok);
	CHECK(!CreateNew(folder + L"\\created\\client.json", "{}"));
	CHECK(Read(folder + L"\\created\\client.json", kKeys).status == ReadStatus::Ok);
	DeleteFileW((folder + L"\\created\\client.json").c_str());
	RemoveDirectoryW((folder + L"\\created").c_str());
	CHECK(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);
	CHECK(Read(folder + L"\\missing\\client.json", kKeys).status == ReadStatus::Missing);

	// Reading an unusable file leaves it byte-identical.
	const std::string bad = "{ \"network\": 5, \"keep\": true }\r\n";
	WriteAll(path, bad);
	CHECK(Read(path, kKeys).status == ReadStatus::Unusable);
	CHECK(ReadAll(path) == bad);

	// A save replaces the file and leaves no temporary file behind.
	WriteAll(path, "{\"schema_version\": 1}");
	CHECK(WriteExisting(path, "{\"schema_version\": 1, \"keep\": true}"));
	CHECK(ReadAll(path) == "{\"schema_version\": 1, \"keep\": true}");
	const ReadResult saved = Read(path, kKeys);
	CHECK(saved.status == ReadStatus::Ok);
	CHECK(saved.document["keep"] == true);
	const std::wstring tempPath = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	CHECK(GetFileAttributesW(tempPath.c_str()) == INVALID_FILE_ATTRIBUTES);

	DeleteFileW(path.c_str());
	RemoveDirectoryW(folder.c_str());

	CHECK(!DefaultPath().empty());
	CHECK(DefaultPath().size() > 32 && DefaultPath().substr(DefaultPath().size() - 27) == L"\\GeneralsOnline\\client.json");
}

static void TestSendCrashReports()
{
	// Crash reports are on unless the file turns them off.
	CHECK(SendCrashReports(Classify(false, false, std::string(), {})));
	CHECK(SendCrashReports(Classify(true, true, "{}", {})));
	CHECK(SendCrashReports(Classify(true, true, "{\"send_crash_reports\": true}", {})));
	CHECK(!SendCrashReports(Classify(true, true, "{\"send_crash_reports\": false}", {})));
	CHECK(!SendCrashReports(Classify(true, true, "{\"schema_version\": 2, \"send_crash_reports\": false}", {})));

	// A value of another type is an attempt to turn them off.
	CHECK(!SendCrashReports(Classify(true, true, "{\"send_crash_reports\": \"false\"}", {})));
	CHECK(!SendCrashReports(Classify(true, true, "{\"send_crash_reports\": 0}", {})));

	// Only the top level counts.
	CHECK(SendCrashReports(Classify(true, true, "{\"network\": {\"send_crash_reports\": false}}", {})));

	// A file the client cannot use keeps the default.
	CHECK(SendCrashReports(Classify(true, true, "{ not json", {})));
	CHECK(SendCrashReports(Classify(true, false, std::string(), {})));

	wchar_t tempRoot[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tempRoot);
	const std::wstring folder = std::wstring(tempRoot) + L"genonline-crash-reports-test-" + std::to_wstring(GetCurrentProcessId());
	const std::wstring path = folder + L"\\client.json";

	// No file means on, and reading never creates the file or its folder.
	CHECK(ReadSendCrashReports(path));
	CHECK(GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES);
	CHECK(ReadSendCrashReports(std::wstring()));

	CreateDirectoryW(folder.c_str(), nullptr);
	WriteAll(path, "{\"schema_version\": 1}");
	CHECK(ReadSendCrashReports(path));
	WriteAll(path, "{\"send_crash_reports\": true}");
	CHECK(ReadSendCrashReports(path));

	// A key of the wrong type elsewhere does not hide the choice, and reading leaves the file byte-identical.
	const std::string off = "{\"send_crash_reports\": false, \"network\": 5, \"unknown\": [1]}\r\n";
	WriteAll(path, off);
	CHECK(!ReadSendCrashReports(path));
	CHECK(ReadAll(path) == off);

	DeleteFileW(path.c_str());
	RemoveDirectoryW(folder.c_str());
}

int main()
{
	TestClassify();
	TestShouldSave();
	TestFiles();
	TestSendCrashReports();
	if (s_failures != 0)
	{
		std::printf("%d check(s) failed\n", s_failures);
		return 1;
	}
	std::printf("All client.json checks passed\n");
	return 0;
}
