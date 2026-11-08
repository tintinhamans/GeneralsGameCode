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

// FILE: MapPreviewIcons.cpp //////////////////////////////////////////////////////////////////////
// Desc: Typed map preview icons
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(GENONLINE)

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/MapCacheJson.h"
#include "GameClient/MapPreviewIcons.h"
#include "GameClient/MapUtil.h"

#include <ctype.h>
#include <string>
#include <wchar.h>

// TheSuperHackers @feature arcticdolphin 08/11/2026 Icon table loaded from Data\GeneralsOnline\MapPreviewIcons*.ini.

struct MapPreviewIconCategory
{
	MapPreviewIconCategory() : m_tooltipFromTemplate(TRUE) {}

	AsciiString m_name;
	AsciiString m_imageName;
	AsciiString m_label;
	AsciiString m_labelTag;
	Bool m_tooltipFromTemplate;
};

struct MapPreviewIconRule
{
	MapPreviewIconRule() : m_isValid(TRUE) {}

	AsciiString m_category;
	std::vector<Int> m_kindOfs;
	std::vector<AsciiString> m_templates;
	std::vector<AsciiString> m_patterns;
	Bool m_isValid; ///< FALSE when the rule names an unknown KindOf, so it never matches
};

static std::vector<MapPreviewIconCategory> s_categories;
static std::vector<MapPreviewIconRule> s_rules;
static Bool s_isTableLoaded = FALSE;

static const char *const s_tableDirectory = "Data\\GeneralsOnline\\";
static const char *const s_tablePattern = "mappreviewicons*.ini"; // archive names are lowercase and matched case sensitively

//-------------------------------------------------------------------------------------------------
static std::string trimmed( const std::string &text )
{
	std::string::size_type first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return std::string();
	std::string::size_type last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

//-------------------------------------------------------------------------------------------------
static void splitTokens( const std::string &text, std::vector<AsciiString> &outTokens )
{
	std::string::size_type pos = 0;
	while (pos < text.size())
	{
		std::string::size_type first = text.find_first_not_of(" \t", pos);
		if (first == std::string::npos)
			break;
		std::string::size_type last = text.find_first_of(" \t", first);
		if (last == std::string::npos)
			last = text.size();
		outTokens.push_back(AsciiString(text.substr(first, last - first).c_str()));
		pos = last;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool parseYesNo( const std::string &value )
{
	AsciiString text(value.c_str());
	return text.compareNoCase("yes") == 0 || text.compareNoCase("true") == 0 || text.compareNoCase("1") == 0;
}

//-------------------------------------------------------------------------------------------------
static MapPreviewIconCategory *findCategory( const AsciiString &name )
{
	if (name.isEmpty())
		return nullptr;
	for (size_t i = 0; i < s_categories.size(); ++i)
	{
		if (s_categories[i].m_name.compareNoCase(name) == 0)
			return &s_categories[i];
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// Case insensitive match with * and ? wildcards.
static Bool wildcardMatch( const char *pattern, const char *text )
{
	const char *star = nullptr;
	const char *mark = nullptr;
	while (*text)
	{
		if (*pattern == '?' || (*pattern != '*' && tolower((unsigned char)*pattern) == tolower((unsigned char)*text)))
		{
			++pattern;
			++text;
		}
		else if (*pattern == '*')
		{
			star = pattern++;
			mark = text;
		}
		else if (star != nullptr)
		{
			pattern = star + 1;
			text = ++mark;
		}
		else
		{
			return FALSE;
		}
	}
	while (*pattern == '*')
		++pattern;
	return *pattern == '\0';
}

//-------------------------------------------------------------------------------------------------
static void parseTableText( const std::string &text, const AsciiString &fileName )
{
	enum { BLOCK_NONE, BLOCK_CATEGORY, BLOCK_RULE };
	Int block = BLOCK_NONE;
	size_t current = 0;

	std::string::size_type pos = 0;
	while (pos < text.size())
	{
		std::string::size_type end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;

		std::string::size_type comment = line.find(';');
		if (comment != std::string::npos)
			line.erase(comment);
		line = trimmed(line);
		if (line.empty())
			continue;

		if (block == BLOCK_NONE)
		{
			std::vector<AsciiString> tokens;
			splitTokens(line, tokens);
			if (tokens[0].compareNoCase("MapPreviewIconCategory") == 0 && tokens.size() > 1)
			{
				MapPreviewIconCategory *category = findCategory(tokens[1]);
				if (category == nullptr)
				{
					s_categories.push_back(MapPreviewIconCategory());
					s_categories.back().m_name = tokens[1];
					category = &s_categories.back();
				}
				current = (size_t)(category - &s_categories[0]);
				block = BLOCK_CATEGORY;
			}
			else if (tokens[0].compareNoCase("MapPreviewIconRule") == 0)
			{
				s_rules.push_back(MapPreviewIconRule());
				current = s_rules.size() - 1;
				block = BLOCK_RULE;
			}
			else
			{
				DEBUG_LOG(("MapPreviewIcons - unknown block '%s' in '%s'", tokens[0].str(), fileName.str()));
			}
			continue;
		}

		if (AsciiString(line.c_str()).compareNoCase("End") == 0)
		{
			block = BLOCK_NONE;
			continue;
		}

		std::string::size_type equals = line.find('=');
		if (equals == std::string::npos)
		{
			DEBUG_LOG(("MapPreviewIcons - ignoring '%s' in '%s'", line.c_str(), fileName.str()));
			continue;
		}
		AsciiString key(trimmed(line.substr(0, equals)).c_str());
		std::string value = trimmed(line.substr(equals + 1));

		if (block == BLOCK_CATEGORY)
		{
			MapPreviewIconCategory &category = s_categories[current];
			if (key.compareNoCase("Image") == 0)
				category.m_imageName = value.c_str();
			else if (key.compareNoCase("Label") == 0)
				category.m_label = value.c_str();
			else if (key.compareNoCase("LabelTag") == 0)
				category.m_labelTag = value.c_str();
			else if (key.compareNoCase("TooltipFromTemplate") == 0)
				category.m_tooltipFromTemplate = parseYesNo(value);
			else
				DEBUG_LOG(("MapPreviewIcons - unknown category field '%s' in '%s'", key.str(), fileName.str()));
		}
		else
		{
			MapPreviewIconRule &rule = s_rules[current];
			std::vector<AsciiString> tokens;
			splitTokens(value, tokens);
			if (key.compareNoCase("Category") == 0)
			{
				rule.m_category = tokens.empty() ? AsciiString::TheEmptyString : tokens[0];
			}
			else if (key.compareNoCase("KindOf") == 0)
			{
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					const Int bit = KindOfMaskType::getSingleBitFromName(tokens[i].str());
					if (bit < 0)
					{
						DEBUG_LOG(("MapPreviewIcons - unknown KindOf '%s' in '%s'", tokens[i].str(), fileName.str()));
						rule.m_isValid = FALSE;
						continue;
					}
					rule.m_kindOfs.push_back(bit);
				}
			}
			else if (key.compareNoCase("Template") == 0)
			{
				rule.m_templates.insert(rule.m_templates.end(), tokens.begin(), tokens.end());
			}
			else if (key.compareNoCase("TemplatePattern") == 0)
			{
				rule.m_patterns.insert(rule.m_patterns.end(), tokens.begin(), tokens.end());
			}
			else
			{
				DEBUG_LOG(("MapPreviewIcons - unknown rule field '%s' in '%s'", key.str(), fileName.str()));
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Loads every table file once, base file first, so later files (mods) override.
static void loadTable()
{
	if (s_isTableLoaded || TheFileSystem == nullptr)
		return;
	s_isTableLoaded = TRUE;

	FilenameList fileNames;
	TheFileSystem->getFileListInDirectory(s_tableDirectory, s_tablePattern, fileNames, FALSE);

	FilenameListIter it = fileNames.begin();
	for (; it != fileNames.end(); ++it)
	{
		File *file = TheFileSystem->openFile(it->str(), File::READ | File::BINARY);
		if (file == nullptr)
			continue;
		const Int size = file->size();
		char *data = file->readEntireAndClose();
		if (data == nullptr)
			continue;
		std::string text(data, size > 0 ? size : 0);
		delete[] data;
		parseTableText(text, *it);
	}
	DEBUG_LOG(("MapPreviewIcons - loaded %d categories and %d rules from %d files",
		(Int)s_categories.size(), (Int)s_rules.size(), (Int)fileNames.size()));
}

//-------------------------------------------------------------------------------------------------
static Bool ruleMatches( const MapPreviewIconRule &rule, const AsciiString &templateName, const ThingTemplate *thingTemplate )
{
	if (!rule.m_isValid)
		return FALSE;

	size_t i;
	for (i = 0; i < rule.m_kindOfs.size(); ++i)
	{
		if (thingTemplate == nullptr || !thingTemplate->isKindOf((KindOfType)rule.m_kindOfs[i]))
			return FALSE;
	}

	if (rule.m_templates.empty() && rule.m_patterns.empty())
		return TRUE;

	for (i = 0; i < rule.m_templates.size(); ++i)
	{
		if (rule.m_templates[i].compareNoCase(templateName) == 0)
			return TRUE;
	}
	for (i = 0; i < rule.m_patterns.size(); ++i)
	{
		if (wildcardMatch(rule.m_patterns[i].str(), templateName.str()))
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
AsciiString resolveMapPreviewCategory( const AsciiString &templateName, const ThingTemplate *thingTemplate )
{
	loadTable();

	AsciiString category;
	for (size_t i = 0; i < s_rules.size(); ++i)
	{
		if (ruleMatches(s_rules[i], templateName, thingTemplate))
			category = s_rules[i].m_category;
	}
	return category;
}

//-------------------------------------------------------------------------------------------------
const Image *getMapPreviewCategoryImage( const AsciiString &category, Bool isTech )
{
	if (TheMappedImageCollection == nullptr)
		return nullptr;

	loadTable();
	const MapPreviewIconCategory *entry = findCategory(category);
	if (entry != nullptr && !entry->m_imageName.isEmpty())
	{
		const Image *image = TheMappedImageCollection->findImageByName(entry->m_imageName);
		if (image != nullptr)
			return image;
	}
	return TheMappedImageCollection->findImageByName(isTech ? "TecBuilding" : "Cash");
}

//-------------------------------------------------------------------------------------------------
UnicodeString getMapPreviewCategoryLabel( const AsciiString &category, Bool isTech )
{
	loadTable();
	const MapPreviewIconCategory *entry = findCategory(category);
	UnicodeString label;
	if (entry != nullptr && !entry->m_labelTag.isEmpty() && TheGameText != nullptr)
	{
		Bool exists = FALSE;
		UnicodeString text = TheGameText->fetch(entry->m_labelTag, &exists);
		if (exists)
		{
			// object names carry a description after " -- ", the label keeps the name
			const WideChar *description = wcsstr(text.str(), L" -- ");
			if (description != nullptr)
				label.set(text.str(), (int)(description - text.str()));
			else
				label = text;
		}
	}
	if (label.isEmpty() && entry != nullptr && !entry->m_label.isEmpty())
		label.translate(entry->m_label);
	if (label.isEmpty() && TheGameText != nullptr)
		label = TheGameText->fetch(isTech ? "TOOLTIP:TechBuilding" : "TOOLTIP:SupplyDock");
	return label;
}

//-------------------------------------------------------------------------------------------------
static UnicodeString getMarkerTooltip( const MapPreviewMarker &marker )
{
	const MapPreviewIconCategory *entry = findCategory(marker.m_category);
	if ((entry == nullptr || entry->m_tooltipFromTemplate) && TheThingFactory != nullptr)
	{
		const ThingTemplate *thingTemplate = TheThingFactory->findTemplate(marker.m_templateName, FALSE);
		if (thingTemplate != nullptr)
		{
			const UnicodeString &displayName = thingTemplate->getDisplayName();
			if (!displayName.isEmpty() && !displayName.startsWith(L"MISSING"))
				return displayName;
		}
	}
	return getMapPreviewCategoryLabel(marker.m_category, marker.m_isTech);
}

//-------------------------------------------------------------------------------------------------
static void addTypedMarkers( const MapMetaData *mmd, const MapTypedObjectVector &objects, Bool isTech,
	const ICoord2D &ul, Int smallWidth, Int smallHeight )
{
	MapTypedObjectVector::const_iterator it = objects.begin();
	for (; it != objects.end(); ++it)
	{
		if (it->m_isTech != isTech)
			continue;

		MapPreviewMarker marker;
		marker.m_category = it->m_category;
		marker.m_templateName = it->m_templateName;
		marker.m_isTech = it->m_isTech;

		// same placement as positionAdditionalImages
		Real position;
		position = (it->m_pos.x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
		marker.m_pos.x = (position * smallWidth) - SUPPLY_TECH_SIZE /2 + ul.x;

		position = (it->m_pos.y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
		marker.m_pos.y = ((1- position) * smallHeight) - SUPPLY_TECH_SIZE /2 + ul.y;
		TheSupplyAndTechImageLocations.m_typedMarkers.push_back(marker);
	}
}

//-------------------------------------------------------------------------------------------------
Bool positionTypedPreviewImages( const MapMetaData *mmd, const ICoord2D &ul, Int smallWidth, Int smallHeight )
{
	TheSupplyAndTechImageLocations.m_typedMarkers.clear();
	if (mmd == nullptr || TheMapCache == nullptr)
		return FALSE;

	if (!ensureMapTypedObjects(mmd->m_fileName))
		return FALSE;

	const MapMetaData *cached = TheMapCache->findMap(mmd->m_fileName);
	if (cached == nullptr)
		return FALSE;

	// tech markers first so supply markers draw on top, like the retail lists
	addTypedMarkers(mmd, cached->m_typedObjects.m_objects, TRUE, ul, smallWidth, smallHeight);
	addTypedMarkers(mmd, cached->m_typedObjects.m_objects, FALSE, ul, smallWidth, smallHeight);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool getMapPreviewMarkerTooltip( Int x, Int y, UnicodeString &outTooltip )
{
	MapPreviewMarkerList::const_iterator it = TheSupplyAndTechImageLocations.m_typedMarkers.begin();
	for (; it != TheSupplyAndTechImageLocations.m_typedMarkers.end(); ++it)
	{
		if ((x > it->m_pos.x && x < it->m_pos.x + SUPPLY_TECH_SIZE)
				&& (y > it->m_pos.y && y < it->m_pos.y + SUPPLY_TECH_SIZE))
		{
			outTooltip = getMarkerTooltip(*it);
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static void addLegendEntry( MapPreviewLegend &legend, const AsciiString &category, Bool isTech, Int count )
{
	MapPreviewLegendEntry entry;
	entry.m_category = category;
	entry.m_image = getMapPreviewCategoryImage(category, isTech);
	entry.m_label = getMapPreviewCategoryLabel(category, isTech);
	entry.m_count = count;
	entry.m_isTech = isTech;
	legend.push_back(entry);
}

//-------------------------------------------------------------------------------------------------
Bool getMapPreviewLegend( const AsciiString &mapName, MapPreviewLegend &outLegend )
{
	outLegend.clear();
	if (TheMapCache == nullptr || !ensureMapTypedObjects(mapName))
		return FALSE;

	const MapMetaData *md = TheMapCache->findMap(mapName);
	if (md == nullptr)
		return FALSE;

	loadTable();
	const MapTypedObjectVector &objects = md->m_typedObjects.m_objects;

	// table categories in table order
	for (size_t c = 0; c < s_categories.size(); ++c)
	{
		Int count = 0;
		Bool isTech = FALSE;
		MapTypedObjectVector::const_iterator it = objects.begin();
		for (; it != objects.end(); ++it)
		{
			if (it->m_category.compareNoCase(s_categories[c].m_name) == 0)
			{
				++count;
				isTech = it->m_isTech;
			}
		}
		if (count > 0)
			addLegendEntry(outLegend, s_categories[c].m_name, isTech, count);
	}

	// objects no table category covers, grouped by category name and kind
	MapTypedObjectVector::const_iterator objIt = objects.begin();
	for (; objIt != objects.end(); ++objIt)
	{
		if (findCategory(objIt->m_category) != nullptr)
			continue;

		Bool found = FALSE;
		for (size_t i = 0; i < outLegend.size(); ++i)
		{
			if (outLegend[i].m_isTech == objIt->m_isTech && outLegend[i].m_category.compareNoCase(objIt->m_category) == 0)
			{
				++outLegend[i].m_count;
				found = TRUE;
				break;
			}
		}
		if (!found)
			addLegendEntry(outLegend, objIt->m_category, objIt->m_isTech, 1);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
UnicodeString getMapPreviewLegendText( const AsciiString &mapName )
{
	UnicodeString text;
	MapPreviewLegend legend;
	if (!getMapPreviewLegend(mapName, legend))
		return text;

	for (size_t i = 0; i < legend.size(); ++i)
	{
		if (i > 0)
			text.concat(L", ");
		text.concat(legend[i].m_label);
		UnicodeString count;
		count.format(L" x%d", legend[i].m_count);
		text.concat(count);
	}
	return text;
}

#endif
