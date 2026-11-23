/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//----------------------------------------------------------------------------
//
// Project:   Generals
//
// Module:    Game Engine Common
//
// File name: ArchiveFileSystem.cpp
//
// Created:   11/26/01 TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "PreRTS.h"
#if defined(GENONLINE)
#include <algorithm>
#include <cctype>
#include <vector>
#include <set>
#include <string>
#endif
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#if defined(GENONLINE)
#include "Common/LocalFileSystem.h"
#endif
#include "Common/PerfTimer.h"
#if defined(GENONLINE)
#include "Common/Registry.h"
#include "../GenOnlineInclude.h"
#include "../GenOnlineInit.h"
#endif
#if defined(GENONLINE)
#include "gitinfo.h"
#endif


//----------------------------------------------------------------------------
//         Externals
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Private Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data
//----------------------------------------------------------------------------

ArchiveFileSystem *TheArchiveFileSystem = nullptr;


//----------------------------------------------------------------------------
//         Private Prototypes
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions
//----------------------------------------------------------------------------

#if defined(GENONLINE)
static AsciiString getBaseFilename(const AsciiString& path)
{
	const char* str = path.str();
	const char* p1 = strrchr(str, '\\');
	const char* p2 = strrchr(str, '/');
	const char* sep = (p1 == nullptr) ? p2 : ((p2 == nullptr) ? p1 : ((p1 > p2) ? p1 : p2));
	return sep ? AsciiString(sep + 1) : path;
}

#endif


//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------

//------------------------------------------------------
// ArchivedFileInfo
//------------------------------------------------------
ArchiveFileSystem::ArchiveFileSystem()
{
}

ArchiveFileSystem::~ArchiveFileSystem()
{
	ArchiveFileMap::iterator iter = m_archiveFileMap.begin();
	while (iter != m_archiveFileMap.end()) {
		ArchiveFile *file = iter->second;
		delete file;
		iter++;
	}
#if defined(GENONLINE)
	for (size_t i = 0; i < m_unmountedArchives.size(); ++i)
	{
		delete m_unmountedArchives[i];
	}
#endif
}

#if defined(GENONLINE)
void ArchiveFileSystem::loadIntoDirectoryTree(ArchiveFile *archiveFile, Int priority)
#else
void ArchiveFileSystem::loadIntoDirectoryTree(ArchiveFile *archiveFile, Bool overwrite)
#endif
{
#if defined(GENONLINE)
	archiveFile->setPriority(priority);
#endif

	FilenameList filenameList;

	archiveFile->getFileListInDirectory("", "", "*", filenameList, TRUE);

	FilenameListIter it = filenameList.begin();

	while (it != filenameList.end())
	{
		ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

		AsciiString path;
		AsciiString token;
		AsciiString tokenizer = *it;
		tokenizer.toLower();
		tokenizer.nextToken(&token, "\\/");

		while (!tokenizer.isEmpty())
		{
			path.concat(token);
			path.concat('\\');

			ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
			if (tempiter == dirInfo->m_directories.end())
			{
				dirInfo = &(dirInfo->m_directories[token]);
				dirInfo->m_path = path;
				dirInfo->m_directoryName = token;
			}
			else
			{
				dirInfo = &tempiter->second;
			}

			tokenizer.nextToken(&token, "\\/");
		}

#if defined(GENONLINE)
		// Insert behind every archive of the same or a lower priority number, so the first one mounted wins on equal priority.
		// A plain insert goes to the end of the range of equal keys, the range is rebuilt when the new archive belongs in front of others.
		std::pair<ArchivedFileLocationMap::iterator, ArchivedFileLocationMap::iterator> range = dirInfo->m_files.equal_range(token);
		std::vector<ArchiveFile*> ordered;
		for (ArchivedFileLocationMap::iterator rangeIt = range.first; rangeIt != range.second; ++rangeIt)
			ordered.push_back(rangeIt->second);

		size_t position = 0;
		while (position < ordered.size() && ordered[position]->getPriority() <= priority)
			++position;

		if (position == ordered.size())
#else
		ArchivedFileLocationMap::iterator fileIt;
		if (overwrite)
#endif
		{
#if defined(GENONLINE)
			dirInfo->m_files.insert(std::make_pair(token, archiveFile));
#else
			// When overwriting, try place the new value at the beginning of the key list.
			fileIt = dirInfo->m_files.find(token);
#endif
		}
		else
		{
#if defined(GENONLINE)
			ordered.insert(ordered.begin() + position, archiveFile);
			dirInfo->m_files.erase(range.first, range.second);
			for (size_t k = 0; k < ordered.size(); ++k)
				dirInfo->m_files.insert(std::make_pair(token, ordered[k]));
#else
			// Append to the end of the key list.
			fileIt = dirInfo->m_files.end();
#endif
		}

#if defined(GENONLINE)
		DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s, priority %d", it->str(), archiveFile->getName().str(), priority));

		it++;
	}
}

// Retail archive names, without the extension and in lower case. Only the language of the game counts as retail.
static void addRetailNames(std::set<std::string>& names, Bool zeroHour)
{
	auto add = [&names](const std::string& name)
	{
		std::string lower = name;
		for (char& c : lower)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		names.insert(lower);
	};

	const std::string language = GetRegistryLanguage().str();

	if (zeroHour)
	{
		add(language + "ZH");
		add("Audio" + language + "ZH");
		add("Speech" + language + "ZH");
		add("W3D" + language + "ZH");
		for (const char* name : { "AudioZH", "INIZH", "MapsZH", "Music", "MusicZH", "ShadersZH", "SpeechZH", "TerrainZH", "TexturesZH", "W3DZH", "WindowZH", "gensecZH",
			"PatchData", "PatchINI", "PatchWindow", "PatchZH" })
			add(name);
	}
	else
	{
		add(language);
		add("Audio" + language);
		add("Speech" + language);
		for (const char* name : { "Audio", "INI", "Music", "Patch", "PatchData", "PatchINI", "PatchWindow", "Speech", "Terrain", "Textures", "W3D", "Window", "gensec", "maps", "shaders" })
			add(name);
	}
}

Bool ArchiveFileSystem::isGeneralsArchivePath(const AsciiString& path, Bool generalsFolder)
{
	if (generalsFolder)
		return TRUE;

	AsciiString lowerPath = path;
	lowerPath.toLower();
	return strstr(lowerPath.str(), "zh_generals\\") != nullptr || strstr(lowerPath.str(), "zh_generals/") != nullptr;
}

Bool ArchiveFileSystem::isRetailArchive(const AsciiString& path, Bool generalsFolder, Bool zeroHour)
{
	static std::set<std::string> zeroHourNames;
	static std::set<std::string> generalsNames;
	if (zeroHourNames.empty())
	{
		addRetailNames(zeroHourNames, TRUE);
		addRetailNames(generalsNames, FALSE);
	}

	AsciiString name = getBaseFilename(path);
	if (!name.endsWithNoCase(".big"))
		return FALSE;
	name.truncateBy(4);
	std::string lower = name.str();
	for (char& c : lower)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

	// The Generals archives are in the Generals install folder, or in a ZH_Generals folder inside the game folder
	const Bool inGeneralsFolder = isGeneralsArchivePath(path, generalsFolder);

	if (zeroHour && !inGeneralsFolder)
		return zeroHourNames.count(lower) != 0;
	return generalsNames.count(lower) != 0;
}

Int ArchiveFileSystem::getGameFolderArchivePriority(const AsciiString& path, Bool generalsFolder, Bool zeroHour)
{
	// The Zero Hour archives rank above the Generals ones in their tier, whatever the order of the scan
	const Bool generals = zeroHour && isGeneralsArchivePath(path, generalsFolder);
	if (isRetailArchive(path, generalsFolder, zeroHour))
		return generals ? ARCHIVE_PRIORITY_RETAIL_GENERALS : ARCHIVE_PRIORITY_RETAIL;

	if (TheFileSystem != nullptr && TheFileSystem->getExcludedPaths().matchesPath(path.str()))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is excluded by data.exclude", path.str());
		return ARCHIVE_PRIORITY_EXCLUDED;
	}

	m_gameFolderArchives.push_back(path);
	return generals ? ARCHIVE_PRIORITY_GAME_FOLDER_GENERALS : ARCHIVE_PRIORITY_GAME_FOLDER;
}

Bool ArchiveFileSystem::parseMountEntry(const AsciiString& text, ArchiveMountEntry* entry, AsciiString* reason)
{
	auto fail = [reason](const char* why)
	{
		if (reason != nullptr)
			*reason = why;
		return FALSE;
	};

	std::string value = text.str();
	while (!value.empty() && isspace(static_cast<unsigned char>(value.back())))
		value.pop_back();
	size_t start = 0;
	while (start < value.size() && isspace(static_cast<unsigned char>(value[start])))
		++start;
	value = value.substr(start);
	if (value.empty())
		return fail("empty");

	for (char& c : value)
	{
		if (c == '/')
			c = '\\';
		if (static_cast<unsigned char>(c) < 32 || c == '<' || c == '>' || c == '"' || c == '|')
			return fail("invalid character");
	}

	ArchiveMountEntry::Base base;
	std::string path;
	std::string lowerPrefix = value.substr(0, 5);
	for (char& c : lowerPrefix)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (lowerPrefix == "game:")
	{
		base = ArchiveMountEntry::BASE_GAME;
		path = value.substr(5);
	}
	else if (lowerPrefix == "user:")
	{
		base = ArchiveMountEntry::BASE_USER;
		path = value.substr(5);
	}
	else if (value.size() > 3 && isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && value[2] == '\\')
	{
		base = ArchiveMountEntry::BASE_ABSOLUTE;
		path = value;
	}
	else if (value.size() > 2 && value[0] == '\\' && value[1] == '\\')
	{
		base = ArchiveMountEntry::BASE_ABSOLUTE;
		path = value;
	}
	else
	{
		return fail("needs the prefix game: or user:, or an absolute path");
	}

	if (base != ArchiveMountEntry::BASE_ABSOLUTE)
	{
		if (path.empty() || path[0] == '\\' || path.find(':') != std::string::npos)
			return fail("invalid path");
	}

	size_t pos = 0;
	while (pos <= path.size())
	{
		size_t next = path.find('\\', pos);
		if (next == std::string::npos)
			next = path.size();
		if (path.substr(pos, next - pos) == "..")
			return fail("parent folder");
		pos = next + 1;
	}

	const size_t lastSeparator = path.find_last_of('\\');
	const std::string lastComponent = lastSeparator == std::string::npos ? path : path.substr(lastSeparator + 1);
	const std::string directory = lastSeparator == std::string::npos ? std::string() : path.substr(0, lastSeparator + 1);
	if (directory.find_first_of("*?") != std::string::npos)
		return fail("wildcards are only allowed in the file name");

	std::string lowerLast = lastComponent;
	for (char& c : lowerLast)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

	ArchiveMountEntry::Kind kind;
	if (lastComponent.empty())
	{
		if (directory.empty() || directory == "\\\\")
			return fail("invalid path");
		kind = ArchiveMountEntry::KIND_BIG_GLOB;
		path += "*.big";
	}
	else if (lowerLast.size() < 5 || (lowerLast.compare(lowerLast.size() - 4, 4, ".big") != 0 && lowerLast.compare(lowerLast.size() - 4, 4, ".gpk") != 0))
	{
		return fail("not a BIG or GPK file, a folder or a pattern for them");
	}
	else if (lastComponent.find_first_of("*?") != std::string::npos)
	{
		kind = ArchiveMountEntry::KIND_BIG_GLOB;
	}
	else
	{
		kind = ArchiveMountEntry::KIND_BIG;
	}

	if (entry != nullptr)
	{
		entry->m_kind = kind;
		entry->m_base = base;
		entry->m_path = path.c_str();
	}
	return TRUE;
}

static Bool isNotGenOnlineInterfaceFile(const char* path);

void ArchiveFileSystem::mountListedArchive(const ArchiveMountEntry& entry, Int priority, std::vector<AsciiString>* genOnlineArchives)
{
	std::string fullPath = entry.m_path.str();
	if (entry.m_base == ArchiveMountEntry::BASE_USER)
		fullPath = std::string(TheGlobalData->getPath_UserData().str()) + fullPath;

	std::vector<std::string> bigPaths;
	if (entry.m_kind == ArchiveMountEntry::KIND_BIG)
	{
		bigPaths.push_back(fullPath);
	}
	else
	{
		const size_t lastSeparator = fullPath.find_last_of('\\');
		const std::string directory = lastSeparator == std::string::npos ? std::string() : fullPath.substr(0, lastSeparator + 1);
		const std::string mask = lastSeparator == std::string::npos ? fullPath : fullPath.substr(lastSeparator + 1);
		FilenameList files;
		TheLocalFileSystem->getFileListInDirectory(AsciiString(directory.c_str()), "", AsciiString(mask.c_str()), files, FALSE);
		for (FilenameListIter it = files.begin(); it != files.end(); ++it)
			bigPaths.push_back(it->str());
	}
#else
		dirInfo->m_files.insert(fileIt, std::make_pair(token, archiveFile));
#endif

#if defined(GENONLINE)
	Int mountedCount = 0;
	for (const std::string& bigPath : bigPaths)
	{
		if (!TheLocalFileSystem->doesFileExist(bigPath.c_str()))
			continue;
		if (m_archiveFileMap.find(AsciiString(bigPath.c_str())) != m_archiveFileMap.end())
#endif
#if defined(GENONLINE)
		{
#if defined(GENONLINE)
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is already mounted", bigPath.c_str());
			continue;
		}
		const Bool genOnline = getBaseFilename(AsciiString(bigPath.c_str())).startsWithNoCase("genonline_");
		ArchiveFile* archiveFile = openArchiveFile(bigPath.c_str(), genOnline && m_retailInterface ? isNotGenOnlineInterfaceFile : nullptr);
		if (archiveFile == nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s could not be opened", bigPath.c_str());
			continue;
		}
		loadIntoDirectoryTree(archiveFile, priority);
		m_archiveFileMap[AsciiString(bigPath.c_str())] = archiveFile;
		if (genOnline && genOnlineArchives != nullptr)
			genOnlineArchives->push_back(AsciiString(bigPath.c_str()));
		++mountedCount;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s at priority %d: %d archives", fullPath.c_str(), priority, mountedCount);
}
#else
			const stl::const_range<ArchivedFileLocationMap> range = stl::get_range(dirInfo->m_files, token, 0);
			if (range.distance() >= 2)
			{
				ArchivedFileLocationMap::const_iterator rangeIt0;
				ArchivedFileLocationMap::const_iterator rangeIt1;
#endif

#if defined(GENONLINE)
// A GO menu layout: Window/Menus without the screens shown during a match (the list of GenOnline/tools/genonline_wnd_layouts.py IN_MATCH_MENUS),
// or the GO header templates those layouts use
static Bool isGenOnlineShellLayout(const char* path)
{
	static const char* const inGameLayouts[] = {
		"crcmismatch.wnd", "defeat.wnd", "disconnectscreen.wnd", "localdefeat.wnd", "observerquit.wnd", "popupsaveload.wnd",
		"quitmenu.wnd", "quitmessagebox.wnd", "quitnosave.wnd", "saveload.wnd", "victorious.wnd" };
#else
				if (overwrite)
				{
					rangeIt0 = range.begin;
					rangeIt1 = std::next(rangeIt0);
#endif

#if defined(GENONLINE)
	AsciiString lower(path);
	lower.toLower();
	const char* name = lower.str();
	if (strcmp(name, "data/generalsonline/headertemplate.ini") == 0 || strcmp(name, "data\\generalsonline\\headertemplate.ini") == 0)
	{
		return TRUE;
	}
	const char* prefixes[] = { "window/menus/", "window\\menus\\" };
	Bool inMenus = FALSE;
	for (size_t p = 0; p < ARRAY_SIZE(prefixes); ++p)
	{
		const size_t len = strlen(prefixes[p]);
		if (strncmp(name, prefixes[p], len) == 0)
		{
			name += len;
			inMenus = TRUE;
			break;
		}
	}
	if (!inMenus || strchr(name, '/') != nullptr || strchr(name, '\\') != nullptr)
	{
		return FALSE;
	}
	for (size_t i = 0; i < ARRAY_SIZE(inGameLayouts); ++i)
	{
		if (strcmp(name, inGameLayouts[i]) == 0)
		{
			return FALSE;
		}
	}
	return TRUE;
}
#else
					DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s, overwriting same file in %s",
						it->str(),
						rangeIt0->second->getName().str(),
						rangeIt1->second->getName().str()
					));
				}
				else
				{
					rangeIt1 = std::prev(range.end);
					rangeIt0 = std::prev(rangeIt1);
#endif
#if defined(GENONLINE)
#endif

void ArchiveFileSystem::mountGenOnlineMenus(const std::vector<AsciiString>& genOnlineArchives)
{
	// the GO menu layouts also rank above -mod and every BIG so a mod cannot replace them, loose files still win
	for (const AsciiString& archivePath : genOnlineArchives)
	{
		ArchiveFile* guiArchive = openArchiveFile(archivePath.str(), isGenOnlineShellLayout);
		if (guiArchive == nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "GO menu layouts of %s could not be opened", archivePath.str());
			continue;
		}
		loadIntoDirectoryTree(guiArchive, ARCHIVE_PRIORITY_GENONLINE_GUI);
		AsciiString mapName;
		mapName.format("%s|menus", archivePath.str());
		m_archiveFileMap[mapName] = guiArchive;
		FilenameList guiFiles;
		guiArchive->getFileListInDirectory("", "", "*", guiFiles, TRUE);
		NetworkLog(ELogVerbosity::LOG_RELEASE, "GO menu layouts of %s at priority %d: %d files", archivePath.str(), (Int)ARCHIVE_PRIORITY_GENONLINE_GUI, (Int)guiFiles.size());
	}
}

// The GO interface files of the genonline_ archives that the Retail style leaves out: their copies of retail layouts, kit art,
// fonts and header templates. Layouts retail never had (GO popups) stay so their features keep a window
static Bool isNotGenOnlineInterfaceFile(const char* path)
{
	static const char* const prefixes[] = { "fonts/" };
	static const char* const files[] = {
		"art/textures/generalsonline_uikit.tga",
		"art/textures/generalsonline_loadingbar.tga",
		"art/textures/generalsonline_loadlogo.tga",
		"art/textures/generalsonline_van.tga",
		"art/textures/generalsonline_loadroad.tga",
		"art/textures/generalsonline_loadbubble.tga",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_uikit.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_loadingbar.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_loadlogo.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_van.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_loadroad.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_loadbubble.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_checkboxsliderart.ini",
		"data/ini/mappedimages/texturesize_512/zz_generalsonline_logo.ini",
		"data/ini/gouicolors.ini",
		"data/generalsonline/headertemplate.ini" };

	// paths in a BIG use backslashes, the lists above use slashes
	AsciiString lower;
	for (const char* c = path; *c != '\0'; ++c)
	{
		lower.concat(*c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(*c))));
	}
	const char* name = lower.str();
	if (strncmp(name, "window/", 7) == 0)
	{
		// GenOnline @feature arcticdolphin 09/11/2026 The GO shell copy of the quit box goes too, so Retail opens the retail one.
		if (strcmp(name, "window/menus/goshellquitmessagebox.wnd") == 0)
		{
			return FALSE;
		}
		return TheArchiveFileSystem == nullptr || !TheArchiveFileSystem->doesFileExist(name);
	}
	for (size_t p = 0; p < ARRAY_SIZE(prefixes); ++p)
	{
		if (strncmp(name, prefixes[p], strlen(prefixes[p])) == 0)
		{
			return FALSE;
		}
	}
	for (size_t f = 0; f < ARRAY_SIZE(files); ++f)
	{
		if (strcmp(name, files[f]) == 0)
		{
			return FALSE;
		}
	}
	return TRUE;
}

void ArchiveFileSystem::setInterfaceStyle(const AsciiString& style)
{
	m_retailInterface = style.compareNoCase("retail") == 0;
	if (style.compareNoCase("overhaul") == 0)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Interface: Overhaul needs RmlUi, which this build does not have, using Refit");
	}
	if (m_retailInterface)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Interface: Retail, the GO layouts, kit art and fonts are left out");
		return;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Interface: Refit");
}

#if defined(GENONLINE)
// The build id is the client version the services see, so the baseline pack of a build names that build.
static std::string getBaselineDataPackPath()
{
#if RTS_ZEROHOUR
	const char* game = "zerohour";
#else
	const char* game = "generals";
#endif
	return std::string("data\\genonline_") + game + "@" + GenOnlineRealtime::CompatibilityVersion(GitReleaseTag, GitCommitDate) + ".gpk";
}

// A relative data pack path starts at the folder of the executable because the launcher may set the working directory to the retail folder.
static std::string getExecutableFolder()
{
	char path[MAX_PATH];
	const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::string();
	const std::string folder(path, length);
	return folder.substr(0, folder.find_last_of('\\') + 1);
}

static Bool isAbsoluteWindowsPath(const std::string& path)
{
	return (path.size() > 2 && isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && path[2] == '\\') || (path.size() > 1 && path[0] == '\\' && path[1] == '\\');
}

static Bool hasArchiveExtension(const std::string& path)
{
	return path.size() > 4 && (_stricmp(path.c_str() + path.size() - 4, ".big") == 0 || _stricmp(path.c_str() + path.size() - 4, ".gpk") == 0);
}

void ArchiveFileSystem::mountDataPacks(const std::vector<std::string>* include)
{
	const std::vector<std::string> baseline(1, getBaselineDataPackPath());
	const std::vector<std::string>& entries = include != nullptr ? *include : baseline;
	const std::string executableFolder = getExecutableFolder();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Data packs: %d entries%s", static_cast<int>(entries.size()), include != nullptr ? "" : ", the baseline");

	std::vector<AsciiString> genOnlineArchives;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		std::string path = entries[i];
		for (char& c : path)
		{
			if (c == '/')
				c = '\\';
		}
		while (!path.empty() && isspace(static_cast<unsigned char>(path.back())))
			path.pop_back();
		size_t start = 0;
		while (start < path.size() && isspace(static_cast<unsigned char>(path[start])))
			++start;
		path = path.substr(start);
		if (path.empty())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack %d is ignored (empty)", static_cast<int>(i));
			continue;
		}
		if (!isAbsoluteWindowsPath(path))
			path = executableFolder + path;

		const Bool archive = hasArchiveExtension(path);
		if (!archive && path.back() != '\\')
			path += '\\';

		ArchiveMountEntry entry;
		AsciiString reason;
		if (!parseMountEntry(AsciiString(path.c_str()), &entry, &reason))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack ignored (%s): %s", reason.str(), path.c_str());
			continue;
		}

		const Int priority = ARCHIVE_PRIORITY_DATA + static_cast<Int>(i) * ARCHIVE_PRIORITY_PACK_STRIDE;
		if (archive)
		{
			if (entry.m_kind == ArchiveMountEntry::KIND_BIG && !TheLocalFileSystem->doesFileExist(path.c_str()))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack is missing: %s", path.c_str());
				continue;
			}
			mountListedArchive(entry, priority, &genOnlineArchives);
			continue;
		}

		mountPackFolderFiles(path, priority + ARCHIVE_PRIORITY_PACK_FILES);
		mountListedArchive(entry, priority + ARCHIVE_PRIORITY_PACK_TOP_BIGS, &genOnlineArchives);
		entry.m_path = (path + "*.gpk").c_str();
		mountListedArchive(entry, priority + ARCHIVE_PRIORITY_PACK_TOP_BIGS, &genOnlineArchives);
	}

	if (m_retailInterface)
	{
		return;
	}
	mountGenOnlineMenus(genOnlineArchives);
}
#endif

#if defined(GENONLINE)
void ArchiveFileSystem::applyDataExcludes(const std::vector<std::string>& excludes)
{
	if (TheFileSystem == nullptr)
	{
		return;
	}

	// The loose files and BIGs of the game folder are retail, so the excludes apply to them from here on
	TheFileSystem->setExcludedPaths(excludes);
	if (TheFileSystem->getExcludedPaths().isEmpty())
	{
		return;
	}

	std::vector<ArchiveFile*> unmounted;
	std::vector<AsciiString>::iterator gameFolderArchive = m_gameFolderArchives.begin();
	while (gameFolderArchive != m_gameFolderArchives.end())
	{
		ArchiveFileMap::iterator mounted = m_archiveFileMap.find(*gameFolderArchive);
		if (mounted == m_archiveFileMap.end() || !TheFileSystem->getExcludedPaths().matchesPath(gameFolderArchive->str()))
		{
			++gameFolderArchive;
			continue;
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is excluded by data.exclude", gameFolderArchive->str());
		unmounted.push_back(mounted->second);
		m_unmountedArchives.push_back(mounted->second);
		m_archiveFileMap.erase(mounted);
		gameFolderArchive = m_gameFolderArchives.erase(gameFolderArchive);
	}

	// The archives mounted before were loaded into the lookup in full, so the files of the unmounted ones leave it now
	removeUnmountedFiles(m_rootDirectory, unmounted);
}

void ArchiveFileSystem::removeUnmountedFiles(ArchivedDirectoryInfo& dirInfo, const std::vector<ArchiveFile*>& unmounted)
{
	ArchivedFileLocationMap::iterator file = dirInfo.m_files.begin();
	while (file != dirInfo.m_files.end())
	{
		if (std::find(unmounted.begin(), unmounted.end(), file->second) != unmounted.end())
		{
			dirInfo.m_files.erase(file++);
		}
		else
		{
			++file;
		}
	}

	for (ArchivedDirectoryInfoMap::iterator directory = dirInfo.m_directories.begin(); directory != dirInfo.m_directories.end(); ++directory)
	{
		removeUnmountedFiles(directory->second, unmounted);
#else
		it++;
#endif
	}
}

#if defined(GENONLINE)
void ArchiveFileSystem::mountPackFolderFiles(const std::string& folder, Int priority)
{
	AsciiString root(folder.c_str());
	if (m_archiveFileMap.find(root) != m_archiveFileMap.end())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack folder %s is listed twice, the first one stays", folder.c_str());
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory(AsciiString::TheEmptyString, root, "*", files, TRUE);

	ArchiveFile* folderArchive = nullptr;
	Int count = 0;
	for (FilenameListIter it = files.begin(); it != files.end(); ++it)
	{
		const std::string fullPath = it->str();
		if (fullPath.size() <= static_cast<size_t>(root.getLength()) || it->endsWithNoCase(".big") || it->endsWithNoCase(".gpk"))
		{
			continue;
		}

		FileInfo info;
		if (!TheLocalFileSystem->getFileInfo(*it, &info) || info.sizeHigh != 0)
		{
			continue;
		}

		const std::string relative = fullPath.substr(root.getLength());
		const size_t lastSeparator = relative.find_last_of("\\/");
		ArchivedFileInfo fileInfo;
		fileInfo.m_filename = (lastSeparator == std::string::npos ? relative : relative.substr(lastSeparator + 1)).c_str();
		fileInfo.m_filename.toLower();
		fileInfo.m_archiveFilename = root;
		fileInfo.m_offset = 0;
		fileInfo.m_size = static_cast<UnsignedInt>(info.sizeLow);

		if (folderArchive == nullptr)
		{
			folderArchive = NEW ArchiveFile(root, root);
		}
		folderArchive->addFile(AsciiString(lastSeparator == std::string::npos ? "" : relative.substr(0, lastSeparator + 1).c_str()), &fileInfo);
		++count;
	}

	if (folderArchive != nullptr)
	{
		loadIntoDirectoryTree(folderArchive, priority);
		m_archiveFileMap[root] = folderArchive;
		m_packFolderArchives[folderArchive] = root;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack folder %s: %d loose files at priority %d", folder.c_str(), count, priority);
}

#endif
Bool ArchiveFileSystem::doesFileExist(const Char *filename, FileInstance instance) const
{
	ArchivedDirectoryInfoResult result = const_cast<ArchiveFileSystem*>(this)->getArchivedDirectoryInfo(filename);

	if (!result.valid())
		return false;

	stl::const_range<ArchivedFileLocationMap> range = stl::get_range(result.dirInfo->m_files, result.lastToken, instance);

	return range.valid();
}

ArchivedDirectoryInfo* ArchiveFileSystem::friend_getArchivedDirectoryInfo(const Char* directory)
{
	ArchivedDirectoryInfoResult result = getArchivedDirectoryInfo(directory);

	return result.dirInfo;
}

ArchiveFileSystem::ArchivedDirectoryInfoResult ArchiveFileSystem::getArchivedDirectoryInfo(const Char* directory)
{
	ArchivedDirectoryInfoResult result;
	ArchivedDirectoryInfo* dirInfo = &m_rootDirectory;

	AsciiString token;
	AsciiString tokenizer = directory;
	tokenizer.toLower();
	tokenizer.nextToken(&token, "\\/");

#else
#if defined(DEBUG_LOGGING) && ENABLE_FILESYSTEM_LOGGING
		{
			const stl::const_range<ArchivedFileLocationMap> range = stl::get_range(dirInfo->m_files, token, 0);
			if (range.distance() >= 2)
			{
				ArchivedFileLocationMap::const_iterator rangeIt0;
				ArchivedFileLocationMap::const_iterator rangeIt1;

				if (overwrite)
				{
					rangeIt0 = range.begin;
					rangeIt1 = std::next(rangeIt0);

					DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s, overwriting same file in %s",
						it->str(),
						rangeIt0->second->getName().str(),
						rangeIt1->second->getName().str()
					));
				}
				else
				{
					rangeIt1 = std::prev(range.end);
					rangeIt0 = std::prev(rangeIt1);

					DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s, overwritten by same file in %s",
						it->str(),
						rangeIt1->second->getName().str(),
						rangeIt0->second->getName().str()
					));
				}
			}
			else
			{
				DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s", it->str(), archiveFile->getName().str()));
			}
		}
#endif

		it++;
	}
}

Bool ArchiveFileSystem::doesFileExist(const Char *filename, FileInstance instance) const
{
	ArchivedDirectoryInfoResult result = const_cast<ArchiveFileSystem*>(this)->getArchivedDirectoryInfo(filename);

	if (!result.valid())
		return false;

	stl::const_range<ArchivedFileLocationMap> range = stl::get_range(result.dirInfo->m_files, result.lastToken, instance);

	return range.valid();
}

ArchivedDirectoryInfo* ArchiveFileSystem::friend_getArchivedDirectoryInfo(const Char* directory)
{
	ArchivedDirectoryInfoResult result = getArchivedDirectoryInfo(directory);

	return result.dirInfo;
}

ArchiveFileSystem::ArchivedDirectoryInfoResult ArchiveFileSystem::getArchivedDirectoryInfo(const Char* directory)
{
	ArchivedDirectoryInfoResult result;
	ArchivedDirectoryInfo* dirInfo = &m_rootDirectory;

	AsciiString token;
	AsciiString tokenizer = directory;
	tokenizer.toLower();
	tokenizer.nextToken(&token, "\\/");

#endif
	while (!tokenizer.isEmpty())
	{
		ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
		if (tempiter != dirInfo->m_directories.end())
		{
			dirInfo = &tempiter->second;
			tokenizer.nextToken(&token, "\\/");
		}
		else
		{
			// the directory doesn't exist
			result.dirInfo = nullptr;
			result.lastToken = AsciiString::TheEmptyString;
			return result;
		}
	}

	result.dirInfo = dirInfo;
	result.lastToken = token;
	return result;
}

File * ArchiveFileSystem::openFile(const Char *filename, Int access, FileInstance instance)
{
	ArchiveFile* archive = getArchiveFile(filename, instance);

	if (archive == nullptr)
		return nullptr;

#if defined(GENONLINE)
	// a loose file of a pack folder is read from the folder, which is never written
	std::map<ArchiveFile*, AsciiString>::const_iterator folder = m_packFolderArchives.find(archive);
	if (folder != m_packFolderArchives.end())
	{
		if ((access & (File::WRITE | File::APPEND | File::CREATE | File::TRUNCATE)) != 0)
		{
			return nullptr;
		}
		AsciiString path = folder->second;
		path.concat(filename);
		return TheLocalFileSystem->openFile(path.str(), access);
	}

#endif
	return archive->openFile(filename, access);
}

Bool ArchiveFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo, FileInstance instance) const
{
	if (fileInfo == nullptr) {
		return FALSE;
	}

	if (filename.isEmpty()) {
		return FALSE;
	}

	ArchiveFile* archive = getArchiveFile(filename, instance);

	if (archive == nullptr)
		return FALSE;

#if defined(GENONLINE)
	std::map<ArchiveFile*, AsciiString>::const_iterator folder = m_packFolderArchives.find(archive);
	if (folder != m_packFolderArchives.end())
	{
		AsciiString path = folder->second;
		path.concat(filename);
		return TheLocalFileSystem->getFileInfo(path, fileInfo);
	}

#endif
	return archive->getFileInfo(filename, fileInfo);
}

ArchiveFile* ArchiveFileSystem::getArchiveFile(const AsciiString& filename, FileInstance instance) const
{
	ArchivedDirectoryInfoResult result = const_cast<ArchiveFileSystem*>(this)->getArchivedDirectoryInfo(filename.str());

	if (!result.valid())
		return nullptr;

	stl::const_range<ArchivedFileLocationMap> range = stl::get_range(result.dirInfo->m_files, result.lastToken, instance);

	if (!range.valid())
		return nullptr;
	
	return range.get()->second;
}

void ArchiveFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	ArchiveFileMap::const_iterator it = m_archiveFileMap.begin();
	while (it != m_archiveFileMap.end()) {
		it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
		it++;
	}
}
