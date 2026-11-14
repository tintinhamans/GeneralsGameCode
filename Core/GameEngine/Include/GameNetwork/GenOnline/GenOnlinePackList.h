#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The pack list file that the launcher passes with -packs (PATCH_FORMAT 6.8 and 9.2). The client reads schema_version, game and packs,
// ignores every other key and never writes the file.
namespace GenOnlinePackList
{
	constexpr int SchemaVersion = 1;

	// Lowest first in the stack, retail is below all of them.
	enum class Tier
	{
		CommunityPatch,
		GeneralsOnline,
		User,
	};

	struct Pack
	{
		std::string id;
		std::string folder;	// absolute, with backslashes and no trailing separator
		Tier tier = Tier::User;
		int order = 0;
		std::vector<std::string> excludes;	// lower case with forward slashes, an exact path or a folder ending in /**
	};

	enum class Status
	{
		Ok,				// the stack holds the enabled and valid packs
		Unusable,		// missing, unreadable or not a pack list, the client uses its built-in defaults
		GameMismatch,	// written for the other game, the client uses its built-in defaults
		Newer,			// a newer schema_version, the client uses its built-in defaults
		Invalid,		// an invalid pack or exclude entry or a pack folder that does not exist, the client uses its built-in defaults
	};

	struct Result
	{
		Status status = Status::Unusable;
		std::vector<Pack> stack;	// lowest first: by tier, then by order, equal orders keep the file order
		std::vector<std::string> log;	// one line for a refused file, else one line for each disabled pack and each ignored community_patch exclude
	};

	// TRUE when the folder exists. Parse checks the folder of each enabled pack with it before anything is mounted.
	typedef bool (*FolderExistsFunc)(const std::string& folder);
	bool FolderExists(const std::string& folder);

	// game is "generals" or "generalsmd", the game that is running.
	Result Parse(const std::string& text, const char* game, FolderExistsFunc folderExists = FolderExists);
	Result ReadFile(const std::string& path, const char* game, FolderExistsFunc folderExists = FolderExists);

	// The key form of a game path: lower case with forward slashes.
	std::string NormalizePath(const std::string& path);

	// TRUE when an exclude of the form Pack::excludes matches the path. The path may use either slash and any case.
	bool ExcludeMatches(const std::string& exclude, const std::string& path);

	// TRUE when a pack at index firstAbove or higher in the stack excludes the path. A file of the pack at index i is hidden
	// when this is TRUE for firstAbove i + 1, a retail file when it is TRUE for firstAbove 0.
	bool IsHidden(const std::vector<Pack>& stack, size_t firstAbove, const std::string& path);

	// The firstAbove of IsHidden for a file mounted at priority, where a lower priority wins. packPriorities holds the base priority of
	// each pack of the stack, and a pack spans its base priority and the stride minus 1 priorities after it.
	size_t FirstPackAbove(const std::vector<int>& packPriorities, int stride, int priority);

	// The excludes of the stack as game folder exclude entries (game:<path>, a folder ends in a slash), for the loose files and
	// archives of the game folder, which are retail.
	std::vector<std::string> GameFolderExcludes(const std::vector<Pack>& stack);
}
