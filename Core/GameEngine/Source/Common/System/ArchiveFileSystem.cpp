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
#include <algorithm>
#include <cctype>
#include <vector>
#include <set>
#include <string>
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/Registry.h"
#include "../GenOnlineInclude.h"
#include "../GenOnlineInit.h"


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

static AsciiString getBaseFilename(const AsciiString& path)
{
	const char* str = path.str();
	const char* p1 = strrchr(str, '\\');
	const char* p2 = strrchr(str, '/');
	const char* sep = (p1 == nullptr) ? p2 : ((p2 == nullptr) ? p1 : ((p1 > p2) ? p1 : p2));
	return sep ? AsciiString(sep + 1) : path;
}



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
	for (size_t i = 0; i < m_unmountedArchives.size(); ++i)
	{
		delete m_unmountedArchives[i];
	}
}

void ArchiveFileSystem::loadIntoDirectoryTree(ArchiveFile *archiveFile, Int priority)
{
	archiveFile->setPriority(priority);

	FilenameList filenameList;

	archiveFile->getFileListInDirectory("", "", "*", filenameList, TRUE);

	FilenameListIter it = filenameList.begin();

	while (it != filenameList.end())
	{
		if (isHiddenByPack(*it, priority))
		{
			++it;
			continue;
		}

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
		{
			dirInfo->m_files.insert(std::make_pair(token, archiveFile));
		}
		else
		{
			ordered.insert(ordered.begin() + position, archiveFile);
			dirInfo->m_files.erase(range.first, range.second);
			for (size_t k = 0; k < ordered.size(); ++k)
				dirInfo->m_files.insert(std::make_pair(token, ordered[k]));
		}

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
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is excluded by the -packs stack", path.str());
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
	else if (lowerLast.size() < 5 || lowerLast.compare(lowerLast.size() - 4, 4, ".big") != 0)
	{
		return fail("not a BIG file, a BIG folder or a *.big pattern");
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

void ArchiveFileSystem::mountListedArchive(const ArchiveMountEntry& entry, Int priority, const PathExcludeList* excludes, std::vector<AsciiString>* mounted, ArchivePathFilter includePath)
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

	Int mountedCount = 0;
	for (const std::string& bigPath : bigPaths)
	{
		if (!TheLocalFileSystem->doesFileExist(bigPath.c_str()))
			continue;
		if (excludes != nullptr && !excludes->isEmpty() && excludes->matches(PathExcludeList::makeKey(bigPath.c_str(), TheGlobalData->getPath_UserData().str())))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is excluded", bigPath.c_str());
			continue;
		}
		if (m_archiveFileMap.find(AsciiString(bigPath.c_str())) != m_archiveFileMap.end())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is already mounted", bigPath.c_str());
			continue;
		}
		ArchiveFile* archiveFile = openArchiveFile(bigPath.c_str(), includePath);
		if (archiveFile == nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s could not be opened", bigPath.c_str());
			continue;
		}
		loadIntoDirectoryTree(archiveFile, priority);
		m_archiveFileMap[AsciiString(bigPath.c_str())] = archiveFile;
		if (mounted != nullptr)
			mounted->push_back(AsciiString(bigPath.c_str()));
		++mountedCount;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s at priority %d: %d archives", fullPath.c_str(), priority, mountedCount);
}

// A GO menu layout: Window/Menus without the screens shown during a match (the list of GenOnline/tools/go_wnd_layouts.py IN_MATCH_MENUS),
// or the GO header templates those layouts use
static Bool isGenOnlineShellLayout(const char* path)
{
	static const char* const inGameLayouts[] = {
		"crcmismatch.wnd", "defeat.wnd", "disconnectscreen.wnd", "localdefeat.wnd", "observerquit.wnd", "popupsaveload.wnd",
		"quitmenu.wnd", "quitmessagebox.wnd", "quitnosave.wnd", "saveload.wnd", "victorious.wnd" };

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

// The GO interface files of the generals_online archives that the Retail style leaves out: their copies of retail layouts, kit art,
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

void ArchiveFileSystem::mountGenOnlineLayer(Bool enabled, const std::vector<std::string>& includePaths)
{
	std::vector<AsciiString> genOnlineArchives;
	mountDataPackLayer("generals_online", enabled, includePaths, nullptr, ARCHIVE_PRIORITY_GENONLINE, &genOnlineArchives, m_retailInterface ? isNotGenOnlineInterfaceFile : nullptr);
	if (m_retailInterface)
	{
		return;
	}
	mountGenOnlineMenus(genOnlineArchives);
}

void ArchiveFileSystem::mountDataPackLayer(const char* layer, Bool enabled, const std::vector<std::string>& includePaths, const PathExcludeList* excludes, Int priority, std::vector<AsciiString>* mounted, ArchivePathFilter includePath)
{
	if (!enabled)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack layer %s is disabled", layer);
		return;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack layer %s: %d include_paths", layer, static_cast<int>(includePaths.size()));
	for (size_t i = 0; i < includePaths.size(); ++i)
	{
		ArchiveMountEntry entry;
		AsciiString reason;
		if (!parseMountEntry(AsciiString(includePaths[i].c_str()), &entry, &reason))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Data pack layer %s: path ignored (%s): %s", layer, reason.str(), includePaths[i].c_str());
			continue;
		}
		mountListedArchive(entry, priority + static_cast<Int>(i), excludes, mounted, includePath);
	}
}

static Int getPackTierPriority(GenOnlinePackList::Tier tier)
{
	switch (tier)
	{
	case GenOnlinePackList::Tier::CommunityPatch:
		return ARCHIVE_PRIORITY_COMMUNITY_PATCH;
	case GenOnlinePackList::Tier::GeneralsOnline:
		return ARCHIVE_PRIORITY_GENONLINE;
	default:
		return ARCHIVE_PRIORITY_USER;
	}
}

static const char* getPackTierName(GenOnlinePackList::Tier tier)
{
	switch (tier)
	{
	case GenOnlinePackList::Tier::CommunityPatch:
		return "community_patch";
	case GenOnlinePackList::Tier::GeneralsOnline:
		return "generals_online";
	default:
		return "user";
	}
}

void ArchiveFileSystem::activatePackStack(const std::vector<GenOnlinePackList::Pack>& stack)
{
	m_packStack = stack;
	m_packPriorities.clear();
	m_hasPackExcludes = FALSE;

	// the highest pack of a tier has the base priority of the tier, each pack below it one stride more
	for (size_t i = 0; i < m_packStack.size(); ++i)
	{
		Int above = 0;
		for (size_t j = i + 1; j < m_packStack.size(); ++j)
		{
			if (m_packStack[j].tier == m_packStack[i].tier)
			{
				++above;
			}
		}
		m_packPriorities.push_back(getPackTierPriority(m_packStack[i].tier) + above * ARCHIVE_PRIORITY_PACK_STRIDE);
		if (!m_packStack[i].excludes.empty())
		{
			m_hasPackExcludes = TRUE;
		}
	}

	// The loose files and BIGs of the game folder are retail, every exclude of the stack applies to them from here on
	if (TheFileSystem != nullptr)
	{
		TheFileSystem->setExcludedPaths(GenOnlinePackList::GameFolderExcludes(m_packStack));
	}
	if (!m_hasPackExcludes || TheFileSystem == nullptr)
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
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Archive %s is excluded by the -packs stack", gameFolderArchive->str());
		unmounted.push_back(mounted->second);
		m_unmountedArchives.push_back(mounted->second);
		m_archiveFileMap.erase(mounted);
		gameFolderArchive = m_gameFolderArchives.erase(gameFolderArchive);
	}

	// The archives mounted before were loaded into the lookup in full, the files the stack hides from them leave it now
	removeHiddenFiles(m_rootDirectory, unmounted);
}

void ArchiveFileSystem::removeHiddenFiles(ArchivedDirectoryInfo& dirInfo, const std::vector<ArchiveFile*>& unmounted)
{
	ArchivedFileLocationMap::iterator file = dirInfo.m_files.begin();
	while (file != dirInfo.m_files.end())
	{
		AsciiString path = dirInfo.m_path;
		path.concat(file->first);
		if (std::find(unmounted.begin(), unmounted.end(), file->second) != unmounted.end() || isHiddenByPack(path, file->second->getPriority()))
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
		removeHiddenFiles(directory->second, unmounted);
	}
}

Bool ArchiveFileSystem::isHiddenByPack(const AsciiString& path, Int priority) const
{
	if (!m_hasPackExcludes)
	{
		return FALSE;
	}

	// the packs above an archive are the ones whose priorities are all lower than its priority, they end the stack
	return GenOnlinePackList::IsHidden(m_packStack, GenOnlinePackList::FirstPackAbove(m_packPriorities, ARCHIVE_PRIORITY_PACK_STRIDE, priority), path.str());
}

Bool ArchiveFileSystem::isLooseFileShadowedByPack(const Char* filename) const
{
	if (m_packStack.empty() || filename == nullptr || filename[0] == '\\' || filename[0] == '/' || strchr(filename, ':') != nullptr)
	{
		return FALSE;
	}

	ArchivedDirectoryInfoResult result = const_cast<ArchiveFileSystem*>(this)->getArchivedDirectoryInfo(filename);
	if (!result.valid())
	{
		return FALSE;
	}

	const Int highest = m_packPriorities.back();
	const Int lowest = m_packPriorities.front() + ARCHIVE_PRIORITY_PACK_STRIDE;
	std::pair<ArchivedFileLocationMap::const_iterator, ArchivedFileLocationMap::const_iterator> range = result.dirInfo->m_files.equal_range(result.lastToken);
	for (ArchivedFileLocationMap::const_iterator it = range.first; it != range.second; ++it)
	{
		const Int priority = it->second->getPriority();
		if (priority >= highest && priority < lowest)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void ArchiveFileSystem::mountPackFolderFiles(const GenOnlinePackList::Pack& pack, Int priority)
{
	AsciiString root(pack.folder.c_str());
	root.concat('\\');

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory(AsciiString::TheEmptyString, root, "*", files, TRUE);

	ArchiveFile* folderArchive = nullptr;
	Int count = 0;
	for (FilenameListIter it = files.begin(); it != files.end(); ++it)
	{
		const std::string fullPath = it->str();
		if (fullPath.size() <= static_cast<size_t>(root.getLength()) || it->endsWithNoCase(".big"))
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
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack %s: %d loose files at priority %d", pack.id.c_str(), count, priority);
}

void ArchiveFileSystem::mountPackStack(Bool communityPatchEnabled)
{
	// the highest pack goes first, so its menu layouts win over the ones of lower generals_online packs
	std::vector<AsciiString> genOnlineArchives;
	for (size_t i = m_packStack.size(); i-- > 0;)
	{
		const GenOnlinePackList::Pack& pack = m_packStack[i];
		const Int priority = m_packPriorities[i];
		const Bool genOnline = pack.tier == GenOnlinePackList::Tier::GeneralsOnline;
		if (pack.tier == GenOnlinePackList::Tier::CommunityPatch && !communityPatchEnabled)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack %s is skipped for -disableCommunityDataPatch", pack.id.c_str());
			continue;
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack %s, tier %s, order %d, %d excludes: %s", pack.id.c_str(), getPackTierName(pack.tier), pack.order, static_cast<int>(pack.excludes.size()), pack.folder.c_str());

		mountPackFolderFiles(pack, priority + ARCHIVE_PRIORITY_PACK_FILES);

		const std::string folders[] = { pack.folder + "\\", pack.folder + "\\GeneralsOnlineGameData\\" };
		const Int priorities[] = { priority + ARCHIVE_PRIORITY_PACK_TOP_BIGS, priority + ARCHIVE_PRIORITY_PACK_DATA_BIGS };
		for (size_t f = 0; f < ARRAY_SIZE(folders); ++f)
		{
			ArchiveMountEntry entry;
			AsciiString reason;
			if (!parseMountEntry(AsciiString(folders[f].c_str()), &entry, &reason))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack %s: folder ignored (%s): %s", pack.id.c_str(), reason.str(), folders[f].c_str());
				continue;
			}
			mountListedArchive(entry, priorities[f], nullptr, genOnline ? &genOnlineArchives : nullptr, genOnline && m_retailInterface ? isNotGenOnlineInterfaceFile : nullptr);
		}
	}

	if (m_retailInterface)
	{
		return;
	}
	mountGenOnlineMenus(genOnlineArchives);
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

	std::map<ArchiveFile*, AsciiString>::const_iterator folder = m_packFolderArchives.find(archive);
	if (folder != m_packFolderArchives.end())
	{
		AsciiString path = folder->second;
		path.concat(filename);
		return TheLocalFileSystem->getFileInfo(path, fileInfo);
	}

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
		if (!m_hasPackExcludes) {
			it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
		}
		else {
			// the files that a pack above the archive excludes are not listed
			FilenameList archiveFiles;
			it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, archiveFiles, searchSubdirectories);
			for (FilenameListIter file = archiveFiles.begin(); file != archiveFiles.end(); ++file) {
				if (!isHiddenByPack(*file, it->second->getPriority()))
				{
					filenameList.insert(*file);
				}
			}
		}
		it++;
	}
}
