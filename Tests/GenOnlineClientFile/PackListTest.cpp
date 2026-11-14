// Standalone checks of the -packs pack list rules in GenOnlinePackList. Builds without the game, see CMakeLists.txt.
#include "GameNetwork/GenOnline/GenOnlinePackList.h"
#include <windows.h>
#include <cstdio>
#include <fstream>

using namespace GenOnlinePackList;

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

static std::string PackJson(const char* id, const char* tier, int order, bool enabled, const char* excludes)
{
	return std::string("{\"id\":\"") + id + "\",\"folder\":\"C:\\\\Packs\\\\" + id + "\\\\1\",\"tier\":\"" + tier + "\",\"order\":" + std::to_string(order)
		+ ",\"enabled\":" + (enabled ? "true" : "false") + ",\"exclude\":[" + excludes + "]}";
}

static std::string ListJson(const std::string& packs, const char* game = "generalsmd", int schemaVersion = 1)
{
	return std::string("{\"schema_version\":") + std::to_string(schemaVersion) + ",\"game\":\"" + game + "\",\"packs\":[" + packs + "]}";
}

static bool AllFoldersExist(const std::string&)
{
	return true;
}

static bool NoFolderExists(const std::string&)
{
	return false;
}

static bool OnlyOkFolderExists(const std::string& folder)
{
	return folder == "C:\\Packs\\ok\\1";
}

static Result ParseList(const std::string& packs)
{
	return Parse(ListJson(packs), "generalsmd", AllFoldersExist);
}

static size_t IndexOf(const Result& result, const char* id)
{
	for (size_t i = 0; i < result.stack.size(); ++i)
	{
		if (result.stack[i].id == id)
		{
			return i;
		}
	}
	return result.stack.size();
}

static void TestRefusedFiles()
{
	const std::string packs = PackJson("generals_online", "generals_online", 0, true, "");

	// A file for the other game is refused with one log line.
	Result mismatch = Parse(ListJson(packs, "generals"), "generalsmd");
	CHECK(mismatch.status == Status::GameMismatch);
	CHECK(mismatch.stack.empty());
	CHECK(mismatch.log.size() == 1);
	CHECK(Parse(ListJson(packs, "generalsmd"), "generals").status == Status::GameMismatch);
	CHECK(Parse(ListJson(packs, "GeneralsMD"), "generalsmd").status == Status::GameMismatch);

	// A newer schema_version is refused.
	Result newer = Parse(ListJson(packs, "generalsmd", 2), "generalsmd");
	CHECK(newer.status == Status::Newer);
	CHECK(newer.stack.empty());
	CHECK(newer.log.size() == 1);
	CHECK(Parse("{\"schema_version\":18446744073709551615,\"game\":\"generalsmd\",\"packs\":[]}", "generalsmd").status == Status::Newer);

	// Unusable files.
	CHECK(Parse("", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":1,", "generalsmd").status == Status::Unusable);
	CHECK(Parse("[1]", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"game\":\"generalsmd\",\"packs\":[]}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":\"1\",\"game\":\"generalsmd\",\"packs\":[]}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":1.5,\"game\":\"generalsmd\",\"packs\":[]}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":0,\"game\":\"generalsmd\",\"packs\":[]}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":1,\"packs\":[]}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":1,\"game\":\"generalsmd\"}", "generalsmd").status == Status::Unusable);
	CHECK(Parse("{\"schema_version\":1,\"game\":\"generalsmd\",\"packs\":{}}", "generalsmd").status == Status::Unusable);
	Result unusable = Parse("not json", "generalsmd");
	CHECK(unusable.stack.empty());
	CHECK(unusable.log.size() == 1);

	// A missing file is unusable, a file on disk is read.
	char tempFolderA[MAX_PATH];
	GetTempPathA(MAX_PATH, tempFolderA);
	const std::string path = std::string(tempFolderA) + "genonline_pack_list_test_" + std::to_string(GetCurrentProcessId()) + ".json";
	DeleteFileA(path.c_str());
	Result missing = ReadFile(path, "generalsmd");
	CHECK(missing.status == Status::Unusable);
	CHECK(missing.log.size() == 1);
	{
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		file << ListJson(packs);
	}
	Result read = ReadFile(path, "generalsmd", AllFoldersExist);
	CHECK(read.status == Status::Ok);
	CHECK(read.stack.size() == 1);
	DeleteFileA(path.c_str());
}

static void TestExtraKeysIgnored()
{
	const std::string text = "{\"schema_version\":1,\"game\":\"generalsmd\",\"name\":\"Generals Online (official)\",\"channel\":\"live\",\"builtin\":true,\"later\":{\"x\":[1]},"
		"\"packs\":[{\"id\":\"generals_online\",\"folder\":\"C:/Packs/generals_online/250103/\",\"tier\":\"generals_online\",\"order\":0,\"enabled\":true,\"exclude\":[],\"kind\":\"gameplay\"}]}";
	Result result = Parse(text, "generalsmd", AllFoldersExist);
	CHECK(result.status == Status::Ok);
	CHECK(result.log.empty());
	CHECK(result.stack.size() == 1);
	CHECK(result.stack[0].id == "generals_online");
	CHECK(result.stack[0].folder == "C:\\Packs\\generals_online\\250103");
	CHECK(result.stack[0].tier == Tier::GeneralsOnline);
}

static void TestOrdering()
{
	const std::string packs =
		PackJson("src/b", "user", 5, true, "") + "," +
		PackJson("generals_online", "generals_online", 0, true, "") + "," +
		PackJson("src/a", "user", 1, true, "") + "," +
		PackJson("community_patch", "community_patch", 0, true, "") + "," +
		PackJson("src/c", "user", 1, true, "") + "," +
		PackJson("src/d", "user", -3, true, "");
	Result result = ParseList(packs);
	CHECK(result.status == Status::Ok);
	CHECK(result.stack.size() == 6);
	const char* expected[] = { "community_patch", "generals_online", "src/d", "src/a", "src/c", "src/b" };
	for (size_t i = 0; i < 6 && i < result.stack.size(); ++i)
	{
		CHECK(result.stack[i].id == expected[i]);
	}
}

// An invalid entry refuses the whole list with one log line that names the entry, the client then uses its built-in packs.
static void CheckRefused(const std::string& packs, const char* named)
{
	Result result = ParseList(PackJson("ok", "user", 0, true, "") + "," + packs);
	CHECK(result.status == Status::Invalid);
	CHECK(result.stack.empty());
	CHECK(result.log.size() == 1);
	CHECK(result.log.size() == 1 && result.log[0].find(named) != std::string::npos);
}

static void TestInvalidAndDisabledPacks()
{
	CheckRefused("{\"id\":\"badtier\",\"folder\":\"C:\\\\P\",\"tier\":\"mods\",\"order\":0,\"enabled\":true}", "badtier");
	CheckRefused("{\"id\":\"relative\",\"folder\":\"packs\\\\x\",\"tier\":\"user\",\"order\":0,\"enabled\":true}", "relative");
	CheckRefused("{\"id\":\"parent\",\"folder\":\"C:\\\\P\\\\..\\\\Q\",\"tier\":\"user\",\"order\":0,\"enabled\":true}", "parent");
	CheckRefused("{\"id\":\"noorder\",\"folder\":\"C:\\\\P\",\"tier\":\"user\",\"enabled\":true}", "noorder");
	CheckRefused("{\"id\":\"noenabled\",\"folder\":\"C:\\\\P\",\"tier\":\"user\",\"order\":0}", "noenabled");
	CheckRefused("{\"folder\":\"C:\\\\P\",\"tier\":\"user\",\"order\":0,\"enabled\":true}", "pack 1");
	CheckRefused("5", "pack 1");
	CheckRefused("{\"id\":\"badexclude\",\"folder\":\"C:\\\\P\",\"tier\":\"user\",\"order\":0,\"enabled\":true,\"exclude\":\"data\"}", "badexclude");
	CheckRefused("{\"id\":\"community_patch\",\"folder\":\"C:\\\\P\",\"tier\":\"community_patch\",\"order\":0,\"enabled\":true,\"exclude\":\"data\"}", "community_patch");

	// A bad exclude entry refuses the whole list, whatever the other entries are.
	CheckRefused(PackJson("wildcard", "user", 0, true, "\"maps/**\",\"data/*.ini\""), "data/*.ini");
	CheckRefused(PackJson("parent", "user", 0, true, "\"../x\""), "../x");
	CheckRefused(PackJson("absolute", "generals_online", 0, true, "\"/abs\""), "/abs");
	CheckRefused(PackJson("number", "user", 0, true, "7"), "number");
	CheckRefused(PackJson("empty", "user", 0, true, "\"\""), "empty");
	// A disabled pack is not mounted, its entry still has to be valid.
	CheckRefused(PackJson("offbad", "user", 0, false, "\"data/*.ini\""), "offbad");

	// A pack folder that does not exist refuses the whole list.
	Result missingFolder = Parse(ListJson(PackJson("ok", "user", 0, true, "") + "," + PackJson("gone", "user", 1, true, "")), "generalsmd", OnlyOkFolderExists);
	CHECK(missingFolder.status == Status::Invalid);
	CHECK(missingFolder.stack.empty());
	CHECK(missingFolder.log.size() == 1);
	CHECK(missingFolder.log.size() == 1 && missingFolder.log[0].find("gone") != std::string::npos);
	CHECK(Parse(ListJson(PackJson("ok", "user", 0, true, "")), "generalsmd", NoFolderExists).status == Status::Invalid);

	// A disabled pack is skipped with one log line, its folder is not checked and it excludes nothing.
	Result disabled = Parse(ListJson(PackJson("ok", "user", 0, true, "") + "," + PackJson("off", "user", 0, false, "\"data/ini/**\"")), "generalsmd", OnlyOkFolderExists);
	CHECK(disabled.status == Status::Ok);
	CHECK(disabled.stack.size() == 1);
	CHECK(IndexOf(disabled, "off") == disabled.stack.size());
	CHECK(disabled.log.size() == 1);
	CHECK(!IsHidden(disabled.stack, 0, "Data/INI/GameData.ini"));

	// An empty list and a list without an enabled pack are valid: retail and nothing above it.
	Result empty = Parse(ListJson(""), "generalsmd", NoFolderExists);
	CHECK(empty.status == Status::Ok);
	CHECK(empty.stack.empty());
	Result allOff = Parse(ListJson(PackJson("off", "user", 0, false, "")), "generalsmd", NoFolderExists);
	CHECK(allOff.status == Status::Ok);
	CHECK(allOff.stack.empty());

	// The folder check on disk.
	char tempFolder[MAX_PATH];
	GetTempPathA(MAX_PATH, tempFolder);
	CHECK(FolderExists(tempFolder));
	CHECK(!FolderExists(std::string(tempFolder) + "genonline_pack_list_test_missing_" + std::to_string(GetCurrentProcessId())));
}

static void TestExcludes()
{
	CHECK(ExcludeMatches("data/ini/**", "Data\\INI\\Object\\Tank.ini"));
	CHECK(ExcludeMatches("data/ini/**", "DATA/ini/gamedata.ini"));
	CHECK(!ExcludeMatches("data/ini/**", "Data/INI"));
	CHECK(!ExcludeMatches("data/ini/**", "Data/INIZH.big"));
	CHECK(ExcludeMatches("data/scripts/skirmishscripts.scb", "Data\\Scripts\\SkirmishScripts.scb"));
	CHECK(!ExcludeMatches("data/scripts/skirmishscripts.scb", "Data/Scripts/SkirmishScripts.scb.bak"));

	const std::string packs =
		PackJson("community_patch", "community_patch", 0, true, "\"Data/INI/**\"") + "," +
		PackJson("generals_online", "generals_online", 0, true, "\"Data/Scripts/MultiplayerScripts.scb\"") + "," +
		PackJson("src/first", "user", 0, true, "") + "," +
		PackJson("src/second", "user", 1, true, "\"Data/INI/**\",\"Data/Scripts/SkirmishScripts.scb\"");
	Result result = ParseList(packs);
	CHECK(result.status == Status::Ok);
	CHECK(result.stack.size() == 4);

	// A community_patch exclude is ignored with one log line.
	CHECK(result.log.size() == 1);
	CHECK(result.stack[IndexOf(result, "community_patch")].excludes.empty());

	const size_t cp = IndexOf(result, "community_patch");
	const size_t go = IndexOf(result, "generals_online");
	const size_t first = IndexOf(result, "src/first");
	const size_t second = IndexOf(result, "src/second");

	// An exclude hides the path from earlier packs of the same tier, lower tiers and retail, case-insensitively.
	CHECK(IsHidden(result.stack, first + 1, "data\\ini\\object\\tank.ini"));
	CHECK(IsHidden(result.stack, go + 1, "DATA/INI/GameData.ini"));
	CHECK(IsHidden(result.stack, cp + 1, "Data/INI/GameData.ini"));
	CHECK(IsHidden(result.stack, 0, "Data/Scripts/SkirmishScripts.scb"));
	// It does not hide the files of the pack itself.
	CHECK(!IsHidden(result.stack, second + 1, "Data/INI/GameData.ini"));
	// A generals_online exclude hides lower tiers and retail, not the user packs above it.
	CHECK(IsHidden(result.stack, cp + 1, "data/scripts/multiplayerscripts.scb"));
	CHECK(IsHidden(result.stack, 0, "data/scripts/multiplayerscripts.scb"));
	CHECK(!IsHidden(result.stack, go + 1, "data/scripts/multiplayerscripts.scb"));
	CHECK(!IsHidden(result.stack, 0, "Data/Movies/EA_Logo.bik"));

	// The game folder entries.
	std::vector<std::string> entries = GameFolderExcludes(result.stack);
	CHECK(entries.size() == 3);
	CHECK(entries.size() == 3 && entries[0] == "game:data/scripts/multiplayerscripts.scb");
	CHECK(entries.size() == 3 && entries[1] == "game:data/ini/");
	CHECK(entries.size() == 3 && entries[2] == "game:data/scripts/skirmishscripts.scb");
}

// The packs above an archive, from the base priorities of the stack: lower wins, each pack spans one stride.
static void TestFirstPackAbove()
{
	// community_patch, generals_online, two user packs with the higher one at the base of the tier
	const std::vector<int> priorities = { 2000000, 1000000, 100010, 100000 };
	const int stride = 10;
	// A retail archive is below every pack, so every exclude of the stack hides its files once the stack is active.
	CHECK(FirstPackAbove(priorities, stride, 3000000) == 0);
	CHECK(FirstPackAbove(priorities, stride, 3000100) == 0);
	// The files of a pack are hidden only by the packs above it.
	CHECK(FirstPackAbove(priorities, stride, 2000000) == 1);
	CHECK(FirstPackAbove(priorities, stride, 2000002) == 1);
	CHECK(FirstPackAbove(priorities, stride, 1000001) == 2);
	CHECK(FirstPackAbove(priorities, stride, 100012) == 3);
	CHECK(FirstPackAbove(priorities, stride, 100000) == 4);
	// Game folder archives and -mod are above every pack.
	CHECK(FirstPackAbove(priorities, stride, 1000) == 4);
	CHECK(FirstPackAbove(priorities, stride, 0) == 4);
	CHECK(FirstPackAbove(std::vector<int>(), stride, 3000000) == 0);

	// Data/INI/** of a user pack hides the retail GameData.ini, the replacement of the pack stays.
	const std::string packs =
		PackJson("generals_online", "generals_online", 0, true, "") + "," +
		PackJson("src/ini", "user", 0, true, "\"Data/INI/**\"");
	Result result = ParseList(packs);
	CHECK(result.stack.size() == 2);
	const std::vector<int> stackPriorities = { 1000000, 100000 };
	CHECK(IsHidden(result.stack, FirstPackAbove(stackPriorities, stride, 3000000), "Data\\INI\\GameData.ini"));
	CHECK(IsHidden(result.stack, FirstPackAbove(stackPriorities, stride, 1000002), "data\\ini\\gamedata.ini"));
	CHECK(!IsHidden(result.stack, FirstPackAbove(stackPriorities, stride, 100000), "data\\ini\\gamedata.ini"));
	CHECK(!IsHidden(result.stack, FirstPackAbove(stackPriorities, stride, 1000), "data\\ini\\gamedata.ini"));
}

int main()
{
	TestRefusedFiles();
	TestExtraKeysIgnored();
	TestOrdering();
	TestInvalidAndDisabledPacks();
	TestExcludes();
	TestFirstPackAbove();
	if (s_failures != 0)
	{
		std::printf("%d check(s) failed\n", s_failures);
		return 1;
	}
	std::printf("All pack list checks passed\n");
	return 0;
}
