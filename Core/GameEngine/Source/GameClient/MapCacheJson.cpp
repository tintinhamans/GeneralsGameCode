/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// FILE: MapCacheJson.cpp /////////////////////////////////////////////////////////////////////////
// Desc: Generals Online mapcache.json with the typed supply and tech objects per map
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(GENONLINE)

#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/MapCacheJson.h"
#include "GameClient/MapPreviewIcons.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GenOnline/json.hpp"

#include <math.h>
#include <stdio.h>
#include <string>

// TheSuperHackers @feature arcticdolphin 08/11/2026 Reads and writes mapcache.json in the user Maps folder.

typedef nlohmann::ordered_json MapCacheJsonDoc;

struct MapCacheJsonEntry
{
	UnsignedInt m_fileSize;
	UnsignedInt m_crc;
	MapTypedObjectVector m_objects;
};
typedef std::map<AsciiString, MapCacheJsonEntry> MapCacheJsonEntryMap;

static MapCacheJsonEntryMap s_entries; ///< typed objects read from the file or parsed, keyed by the MapCache key
static std::set<AsciiString> s_unreadableMaps; ///< maps the lazy parse failed on, not retried this session
static Bool s_isLoaded = FALSE;
static Bool s_isDirty = FALSE;

static const char *const s_fileName = "mapcache.json";

//-------------------------------------------------------------------------------------------------
static AsciiString getMapCacheJsonPath( const MapCache &cache )
{
	AsciiString path = cache.getUserMapDir();
	path.concat('\\');
	path.concat(s_fileName);
	return path;
}

//-------------------------------------------------------------------------------------------------
// Map paths are ANSI bytes; store bytes above 127 as the matching Latin-1 code points so the JSON stays valid UTF-8.
static std::string toJsonString( const AsciiString &text )
{
	std::string out;
	for (const char *c = text.str(); *c; ++c)
	{
		const unsigned char b = (unsigned char)*c;
		if (b < 0x80)
		{
			out += (char)b;
		}
		else
		{
			out += (char)(0xC0 | (b >> 6));
			out += (char)(0x80 | (b & 0x3F));
		}
	}
	return out;
}

//-------------------------------------------------------------------------------------------------
static AsciiString fromJsonString( const std::string &text )
{
	std::string out;
	std::string::size_type i = 0;
	while (i < text.size())
	{
		const unsigned char b = (unsigned char)text[i];
		if (b < 0x80)
		{
			out += (char)b;
			++i;
		}
		else if ((b & 0xE0) == 0xC0 && i + 1 < text.size())
		{
			const unsigned int codePoint = ((b & 0x1F) << 6) | ((unsigned char)text[i + 1] & 0x3F);
			out += codePoint <= 0xFF ? (char)codePoint : '?';
			i += 2;
		}
		else
		{
			// code points outside Latin-1 have no ANSI byte
			out += '?';
			++i;
			while (i < text.size() && ((unsigned char)text[i] & 0xC0) == 0x80)
				++i;
		}
	}
	return AsciiString(out.c_str());
}

//-------------------------------------------------------------------------------------------------
static double roundPosition( Real value )
{
	return floor(value * 100.0 + 0.5) / 100.0;
}

//-------------------------------------------------------------------------------------------------
// Only maps under the standard and user map folders have a portable path.
static Bool isCacheablePath( const AsciiString &mapKey, const AsciiString &mapDir, const AsciiString &userMapDir )
{
	return mapKey.startsWithNoCase(mapDir) || mapKey.startsWithNoCase(userMapDir);
}

//-------------------------------------------------------------------------------------------------
static AsciiString portablePathToMapKey( const AsciiString &portablePath )
{
	// portableMapPathToRealMapPath asserts on other prefixes
	if (!portablePath.startsWithNoCase("Maps\\") && !portablePath.startsWithNoCase("UserData\\Maps\\"))
		return AsciiString::TheEmptyString;
	AsciiString mapKey = TheGameState->portableMapPathToRealMapPath(portablePath);
	mapKey.toLower();
	return mapKey;
}

//-------------------------------------------------------------------------------------------------
static void readObjects( const MapCacheJsonDoc &list, MapTypedObjectVector &outObjects )
{
	MapCacheJsonDoc::const_iterator it = list.begin();
	for (; it != list.end(); ++it)
	{
		const MapCacheJsonDoc &item = *it;
		MapTypedObject obj;
		obj.m_templateName = fromJsonString(item.at("template").get<std::string>());
		obj.m_isTech = item.at("kind").get<std::string>() == "tech";
		obj.m_category = fromJsonString(item.value("category", std::string()));
		obj.m_pos.x = item.at("x").get<Real>();
		obj.m_pos.y = item.at("y").get<Real>();
		obj.m_pos.z = 0.0f;

		// the icon table may have changed since the file was written
		const ThingTemplate *thingTemplate = TheThingFactory != nullptr ? TheThingFactory->findTemplate(obj.m_templateName, FALSE) : nullptr;
		if (thingTemplate != nullptr)
			obj.m_category = resolveMapPreviewCategory(obj.m_templateName, thingTemplate);

		outObjects.push_back(obj);
	}
}

//-------------------------------------------------------------------------------------------------
static void moveBadFileAside( const AsciiString &path )
{
	AsciiString badPath = path;
	badPath.concat(".bad");
	DEBUG_LOG(("MapCacheJson - '%s' is unreadable, moving it to '%s'", path.str(), badPath.str()));
#ifdef _WIN32
	MoveFileExA(path.str(), badPath.str(), MOVEFILE_REPLACE_EXISTING);
#else
	remove(badPath.str());
	rename(path.str(), badPath.str());
#endif
}

//-------------------------------------------------------------------------------------------------
static void loadMapCacheJson( const MapCache &cache )
{
	s_entries.clear();

	const AsciiString path = getMapCacheJsonPath(cache);
	FILE *fp = fopen(path.str(), "rb");
	if (fp == nullptr)
		return;

	std::string text;
	char buf[4096];
	size_t num;
	while ((num = fread(buf, 1, sizeof(buf), fp)) > 0)
		text.append(buf, num);
	Bool ok = !ferror(fp);
	fclose(fp);

	if (ok)
	{
		try
		{
			const MapCacheJsonDoc root = MapCacheJsonDoc::parse(text);
			if (root.at("version").get<Int>() != MAP_CACHE_JSON_VERSION)
			{
				// another format version: start over and replace the file
				DEBUG_LOG(("MapCacheJson - '%s' has another version, rebuilding it", path.str()));
				s_isDirty = TRUE;
				return;
			}

			const MapCacheJsonDoc &maps = root.at("maps");
			MapCacheJsonDoc::const_iterator it = maps.begin();
			for (; it != maps.end(); ++it)
			{
				const MapCacheJsonDoc &item = *it;
				if (!item.contains("objects"))
					continue;

				const AsciiString mapKey = portablePathToMapKey(fromJsonString(item.at("path").get<std::string>()));
				if (mapKey.isEmpty())
					continue;

				MapCacheJsonEntry entry;
				entry.m_fileSize = item.at("fileSize").get<UnsignedInt>();
				entry.m_crc = item.at("crc").get<UnsignedInt>();
				readObjects(item.at("objects"), entry.m_objects);
				s_entries[mapKey] = entry;
			}
		}
		catch (...)
		{
			ok = FALSE;
		}
	}

	if (!ok)
	{
		s_entries.clear();
		moveBadFileAside(path);
		s_isDirty = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
static MapCacheJsonDoc objectsToJson( const MapTypedObjectVector &objects )
{
	MapCacheJsonDoc list = MapCacheJsonDoc::array();
	MapTypedObjectVector::const_iterator it = objects.begin();
	for (; it != objects.end(); ++it)
	{
		MapCacheJsonDoc item;
		item["template"] = toJsonString(it->m_templateName);
		item["kind"] = it->m_isTech ? "tech" : "supply";
		item["category"] = toJsonString(it->m_category);
		item["x"] = roundPosition(it->m_pos.x);
		item["y"] = roundPosition(it->m_pos.y);
		list.push_back(item);
	}
	return list;
}

//-------------------------------------------------------------------------------------------------
static MapCacheJsonDoc mapToJson( const AsciiString &mapKey, const MapMetaData &md )
{
	MapCacheJsonDoc entry;
	entry["path"] = toJsonString(TheGameState->realMapPathToPortableMapPath(mapKey));
	entry["fileSize"] = md.m_filesize;
	entry["crc"] = md.m_CRC;
	entry["official"] = md.m_isOfficial ? true : false;
	entry["multiplayer"] = md.m_isMultiplayer ? true : false;
	entry["players"] = md.m_numPlayers;

	MapCacheJsonDoc extent;
	extent["minX"] = roundPosition(md.m_extent.lo.x);
	extent["minY"] = roundPosition(md.m_extent.lo.y);
	extent["maxX"] = roundPosition(md.m_extent.hi.x);
	extent["maxY"] = roundPosition(md.m_extent.hi.y);
	entry["extent"] = extent;

	MapCacheJsonDoc starts = MapCacheJsonDoc::array();
	AsciiString waypointName;
	for (Int i = 0; i < md.m_numPlayers; ++i)
	{
		waypointName.format("Player_%d_Start", i + 1); // start pos waypoints are 1-based
		WaypointMap::const_iterator wpIt = md.m_waypoints.find(waypointName);
		if (wpIt == md.m_waypoints.end())
			continue;
		MapCacheJsonDoc start;
		start["x"] = roundPosition(wpIt->second.x);
		start["y"] = roundPosition(wpIt->second.y);
		starts.push_back(start);
	}
	entry["starts"] = starts;

	// maps never parsed for their types have no "objects" yet
	if (md.m_typedObjects.m_isKnown)
		entry["objects"] = objectsToJson(md.m_typedObjects.m_objects);

	return entry;
}

//-------------------------------------------------------------------------------------------------
static void writeMapCacheJson( MapCache &cache )
{
	const AsciiString mapDir = cache.getMapDir();
	const AsciiString userMapDir = cache.getUserMapDir();

	std::string text;
	try
	{
		MapCacheJsonDoc root;
		root["version"] = (Int)MAP_CACHE_JSON_VERSION;
		root["generator"] = "Generals Online";

		MapCacheJsonDoc maps = MapCacheJsonDoc::array();
		MapCache::const_iterator it = cache.begin();
		for (; it != cache.end(); ++it)
		{
			if (isCacheablePath(it->first, mapDir, userMapDir))
				maps.push_back(mapToJson(it->first, it->second));
		}
		root["maps"] = maps;
		text = root.dump();
	}
	catch (...)
	{
		DEBUG_LOG(("MapCacheJson - could not build the JSON text"));
		return;
	}

	TheFileSystem->createDirectory(userMapDir);

	// write to a temp file and swap it in so a failed write never leaves a truncated mapcache.json
	const AsciiString path = getMapCacheJsonPath(cache);
	AsciiString tempPath = path;
	tempPath.concat(".tmp");
	FILE *fp = fopen(tempPath.str(), "wb");
	if (fp == nullptr)
	{
		DEBUG_LOG(("MapCacheJson - failed to create '%s'", tempPath.str()));
		return;
	}

	Bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
	if (fflush(fp) != 0 || ferror(fp))
		ok = FALSE;
	if (fclose(fp) != 0)
		ok = FALSE;

#ifdef _WIN32
	if (ok && !MoveFileExA(tempPath.str(), path.str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		ok = FALSE;
#else
	if (ok && rename(tempPath.str(), path.str()) != 0)
		ok = FALSE;
#endif
	if (!ok)
	{
		DEBUG_LOG(("MapCacheJson - failed to write '%s'", path.str()));
		remove(tempPath.str());
	}
}

//-------------------------------------------------------------------------------------------------
// Reuse rule: same map path, file size and nonzero CRC; the version was checked on load.
static Bool applyStoredObjects( const AsciiString &mapKey, MapMetaData &md )
{
	MapCacheJsonEntryMap::const_iterator it = s_entries.find(mapKey);
	if (it == s_entries.end() || md.m_CRC == 0)
		return FALSE;
	if (it->second.m_fileSize != md.m_filesize || it->second.m_crc != md.m_CRC)
		return FALSE;

	md.m_typedObjects.m_objects = it->second.m_objects;
	md.m_typedObjects.m_isKnown = TRUE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
static void storeObjects( const AsciiString &mapKey, const MapMetaData &md )
{
	MapCacheJsonEntryMap::iterator it = s_entries.find(mapKey);
	if (it != s_entries.end() && it->second.m_fileSize == md.m_filesize && it->second.m_crc == md.m_CRC)
		return;

	MapCacheJsonEntry &entry = s_entries[mapKey];
	entry.m_fileSize = md.m_filesize;
	entry.m_crc = md.m_CRC;
	entry.m_objects = md.m_typedObjects.m_objects;
	s_isDirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
static void loadOnce( const MapCache &cache )
{
	if (s_isLoaded)
		return;
	s_isLoaded = TRUE;
	loadMapCacheJson(cache);
}

//-------------------------------------------------------------------------------------------------
void syncMapCacheJson( MapCache &cache )
{
	if (TheGameState == nullptr || TheFileSystem == nullptr)
		return;

	loadOnce(cache);

	const AsciiString mapDir = cache.getMapDir();
	const AsciiString userMapDir = cache.getUserMapDir();

	MapCache::iterator it = cache.begin();
	for (; it != cache.end(); ++it)
	{
		if (!isCacheablePath(it->first, mapDir, userMapDir))
			continue;

		MapMetaData &md = it->second;
		if (md.m_typedObjects.m_isKnown)
			storeObjects(it->first, md);
		else
			applyStoredObjects(it->first, md);
	}

	if (s_isDirty)
	{
		s_isDirty = FALSE;
		writeMapCacheJson(cache);
	}
}

//-------------------------------------------------------------------------------------------------
Bool ensureMapTypedObjects( const AsciiString &mapName )
{
	if (TheMapCache == nullptr || TheGameState == nullptr || TheFileSystem == nullptr || mapName.isEmpty())
		return FALSE;

	AsciiString mapKey = mapName;
	mapKey.toLower();
	MapCache::iterator it = TheMapCache->find(mapKey);
	if (it == TheMapCache->end())
		return FALSE;

	MapMetaData &md = it->second;
	if (md.m_typedObjects.m_isKnown)
		return TRUE;

	loadOnce(*TheMapCache);
	if (applyStoredObjects(mapKey, md))
		return TRUE;

	if (s_unreadableMaps.find(mapKey) != s_unreadableMaps.end())
		return FALSE;

	// never parse a preview map while the game loads its own map
	if (TheGameLogic != nullptr && TheGameLogic->isLoadingMap())
		return FALSE;

	MapTypedObjectVector objects;
	if (!loadMapTypedObjects(it->first, objects))
	{
		DEBUG_LOG(("MapCacheJson - could not read the typed objects of '%s'", mapKey.str()));
		s_unreadableMaps.insert(mapKey);
		return FALSE;
	}

	md.m_typedObjects.m_objects = objects;
	md.m_typedObjects.m_isKnown = TRUE;

	if (isCacheablePath(mapKey, TheMapCache->getMapDir(), TheMapCache->getUserMapDir()))
		storeObjects(mapKey, md);

	if (s_isDirty)
	{
		s_isDirty = FALSE;
		writeMapCacheJson(*TheMapCache);
	}
	return TRUE;
}

#endif
