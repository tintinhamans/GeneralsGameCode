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

// FILE: MapSelectList.cpp ////////////////////////////////////////////////////////////////////////
// TheSuperHackers @feature arcticdolphin 08/11/2026 Search, filters and favourites for the map select popups.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/SkirmishBattleHonors.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/MapPreviewIcons.h"
#include "GameClient/MapSelectList.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"

#include <wctype.h>

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static MapSelectList *s_activeList = nullptr;
static GameWinInputFunc s_listboxInput = nullptr;

static const char *const s_sourceWindowNames[MAP_SOURCE_COUNT] =
{
	"RadioButtonMapsAll",
	"RadioButtonMapsOfficial",
	"RadioButtonMapsUser",
	"RadioButtonMapsFavourites"
};

static const Color s_nameColor = GameMakeColor(255, 255, 255, 255);
static const Color s_detailColor = GameMakeColor(192, 192, 192, 255);

// TheSuperHackers @feature arcticdolphin 08/11/2026 Legend row under the preview: each category's token icon and its count.
struct MapLegendItem
{
	const Image *m_image;
	DisplayString *m_count;
};
static std::vector<MapLegendItem> s_legendItems;

// TheSuperHackers @feature arcticdolphin 08/11/2026 Map count drawn right aligned on the dialog's inner right edge.
static DisplayString *s_countString = nullptr;

// HELPERS ////////////////////////////////////////////////////////////////////////////////////////

MapSelectFilter::MapSelectFilter()
	: m_numPlayers(0)
	, m_size(MAP_SIZE_ANY)
	, m_source(MAP_SOURCE_ALL)
{
}

MapSelectOptions::MapSelectOptions()
	: m_includeSoloUserMaps(FALSE)
	, m_showMedals(FALSE)
	, m_officialOnly(FALSE)
{
}

MapSelectSize getMapSizeClass( const MapMetaData &mapData )
{
	const Real width = mapData.m_extent.hi.x - mapData.m_extent.lo.x;
	const Real height = mapData.m_extent.hi.y - mapData.m_extent.lo.y;
	const Real extent = max(width, height);
	if (extent < MAP_SIZE_MEDIUM_MIN_EXTENT)
		return MAP_SIZE_SMALL;
	if (extent < MAP_SIZE_LARGE_MIN_EXTENT)
		return MAP_SIZE_MEDIUM;
	return MAP_SIZE_LARGE;
}

AsciiString getMapFavouriteKey( const AsciiString &mapPath )
{
	AsciiString key = TheGameState ? TheGameState->realMapPathToPortableMapPath(mapPath) : mapPath;
	key.toLower();
	return key;
}

Bool isMapFavourite( const AsciiString &mapPath )
{
	return GenOnlineServicesManager::Settings.Maps_IsFavourite(std::string(getMapFavouriteKey(mapPath).str()));
}

void setMapFavourite( const AsciiString &mapPath, Bool favourite )
{
	GenOnlineServicesManager::Settings.Maps_SetFavourite(std::string(getMapFavouriteKey(mapPath).str()), favourite != FALSE);
}

static UnicodeString lowered( const UnicodeString &text )
{
	UnicodeString out;
	for (Int i = 0; i < text.getLength(); ++i)
	{
		out.concat((WideChar)towlower(text.getCharAt(i)));
	}
	return out;
}

static UnicodeString mapFileName( const AsciiString &path )
{
	const char *leaf = strrchr(path.str(), '\\');
	UnicodeString name;
	name.translate(AsciiString(leaf ? leaf + 1 : path.str()));
	return name;
}

static UnicodeString sizeClassText( MapSelectSize size )
{
	switch (size)
	{
		case MAP_SIZE_SMALL:
			return TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapSizeSmall", L"Small");
		case MAP_SIZE_MEDIUM:
			return TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapSizeMedium", L"Medium");
		case MAP_SIZE_LARGE:
			return TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapSizeLarge", L"Large");
		default:
			return TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapAny", L"Any");
	}
}

static void clearLegendItems()
{
	for (size_t i = 0; i < s_legendItems.size(); ++i)
	{
		if (s_legendItems[i].m_count != nullptr)
			TheDisplayStringManager->freeDisplayString(s_legendItems[i].m_count);
	}
	s_legendItems.clear();
}

// Draws the legend row centred in the window: icons as tall as the window, each followed by its count.
static void drawMapLegend( GameWindow *window, WinInstanceData *instData )
{
	if (s_legendItems.empty())
		return;

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);

	const Int iconSize = height;
	const Int count = (Int)s_legendItems.size();
	Int textGap = height / 6 > 2 ? height / 6 : 2;
	Int itemGap = height / 2 > 4 ? height / 2 : 4;

	Int i;
	Int contentWidth = 0;
	for (i = 0; i < count; ++i)
	{
		Int textWidth = 0;
		Int textHeight = 0;
		s_legendItems[i].m_count->getSize(&textWidth, &textHeight);
		contentWidth += iconSize + textWidth;
	}

	// too wide: drop the spacing between items, then between icon and count, then clip
	if (contentWidth + count * textGap + (count - 1) * itemGap > width)
		itemGap = 0;
	if (contentWidth + count * textGap > width)
		textGap = 0;
	const Int rowWidth = contentWidth + count * textGap + (count - 1) * itemGap;

	const Int right = x + width;
	Int cursor = rowWidth < width ? x + (width - rowWidth) / 2 : x;
	const Color textColor = window->winGetEnabledTextColor();
	const Color dropColor = window->winGetEnabledTextBorderColor();

	for (i = 0; i < count; ++i)
	{
		Int textWidth = 0;
		Int textHeight = 0;
		s_legendItems[i].m_count->getSize(&textWidth, &textHeight);
		if (cursor + iconSize + textGap + textWidth > right)
			break;

		if (s_legendItems[i].m_image != nullptr)
			TheDisplay->drawImage(s_legendItems[i].m_image, cursor, y, cursor + iconSize, y + iconSize);
		cursor += iconSize + textGap;

		s_legendItems[i].m_count->draw(cursor, y + (height - textHeight) / 2, textColor, dropColor);
		cursor += textWidth + itemGap;
	}
}

static void clearCountString()
{
	if (s_countString != nullptr)
		TheDisplayStringManager->freeDisplayString(s_countString);
	s_countString = nullptr;
}

// Static text has no right alignment, so the count draws its own text against the window's right edge.
static void drawMapCount( GameWindow *window, WinInstanceData *instData )
{
	if (s_countString == nullptr)
		return;

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);

	Int textWidth = 0;
	Int textHeight = 0;
	s_countString->getSize(&textWidth, &textHeight);
	const Int left = textWidth < width ? x + width - textWidth : x;
	s_countString->draw(left, y + (height - textHeight) / 2, window->winGetEnabledTextColor(), window->winGetEnabledTextBorderColor());
}

struct MapSelectRowSort
{
	struct Entry
	{
		AsciiString m_path;
		UnicodeString m_name;
		Int m_numPlayers;
		Bool m_favourite;
	};

	bool operator()( const Entry &a, const Entry &b ) const
	{
		if (a.m_favourite != b.m_favourite)
			return a.m_favourite != FALSE;
		const Int byName = a.m_name.compareNoCase(b.m_name);
		if (byName != 0)
			return byName < 0;
		if (a.m_numPlayers != b.m_numPlayers)
			return a.m_numPlayers < b.m_numPlayers;
		return a.m_path.compare(b.m_path) < 0;
	}
};

static Int computeMedal( const SkirmishBattleHonors *honors, const AsciiString &path, const MapMetaData &mapData )
{
	const Int numBrutal = honors->getEnduranceMedal(path.str(), SLOT_BRUTAL_AI);
	if (numBrutal)
	{
		return (numBrutal == mapData.m_numPlayers - 1) ? 4 : 3;
	}
	if (honors->getEnduranceMedal(path.str(), SLOT_MED_AI))
		return 2;
	if (honors->getEnduranceMedal(path.str(), SLOT_EASY_AI))
		return 1;
	return 0;
}

static const Image *medalImage( Int medal )
{
	switch (medal)
	{
		case 1:
			return TheMappedImageCollection->findImageByName("Star-Bronze");
		case 2:
			return TheMappedImageCollection->findImageByName("Star-Silver");
		case 3:
			return TheMappedImageCollection->findImageByName("Star-Gold");
		case 4:
		{
			const Image *image = TheMappedImageCollection->findImageByName("RedYell_Star");
			return image ? image : TheMappedImageCollection->findImageByName("Star-Gold");
		}
		default:
			return nullptr;
	}
}

static Int cellImageSize( GameWindow *listbox, Int column, const Image *image )
{
	Int size = image ? image->getImageWidth() : 10;
	size = min(size, GadgetListBoxGetColumnWidth(listbox, column));
	size = min(size, TheWindowManager->winFontHeight(listbox->winGetFont()));
	return max(size, 1);
}

// Clicks in the star column toggle the favourite instead of selecting the row.
static WindowMsgHandledType mapSelectListboxInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if (msg == GWM_LEFT_UP && s_activeList != nullptr && s_activeList->getListbox() == window)
	{
		Int row = -1;
		Int column = -1;
		GadgetListBoxGetEntryBasedOnXY(window, (Int)(mData1 & 0xFFFF), (Int)(mData1 >> 16), row, column);
		if (row >= 0 && row < GadgetListBoxGetNumEntries(window) && column == MapSelectList::STAR_COLUMN)
		{
			s_activeList->toggleFavouriteAtRow(row);
			return MSG_HANDLED;
		}
	}

	if (s_listboxInput != nullptr)
		return s_listboxInput(window, msg, mData1, mData2);
	return MSG_IGNORED;
}

// MapSelectList //////////////////////////////////////////////////////////////////////////////////

MapSelectList::MapSelectList()
	: m_parent(nullptr)
	, m_listbox(nullptr)
	, m_search(nullptr)
	, m_players(nullptr)
	, m_size(nullptr)
	, m_count(nullptr)
	, m_legend(nullptr)
	, m_filter(nullptr)
	, m_includeSoloUserMaps(FALSE)
	, m_showMedals(FALSE)
	, m_officialOnly(FALSE)
	, m_ignoreEvents(FALSE)
{
	for (Int i = 0; i < MAP_SOURCE_COUNT; ++i)
	{
		m_sources[i] = nullptr;
	}
}

MapSelectList::~MapSelectList()
{
	if (s_activeList == this)
		s_activeList = nullptr;
}

GameWindow *MapSelectList::findChild( const char *name ) const
{
	AsciiString fullName;
	fullName.format("%s:%s", m_layoutName.str(), name);
	return TheWindowManager->winGetWindowFromId(m_parent, TheNameKeyGenerator->nameToKey(fullName));
}

void MapSelectList::init( GameWindow *parent, const char *layoutName, const MapSelectOptions &options, MapSelectFilter *filter, const AsciiString &mapToSelect )
{
	m_parent = parent;
	m_layoutName = layoutName;
	m_filter = filter;
	m_includeSoloUserMaps = options.m_includeSoloUserMaps;
	m_showMedals = options.m_showMedals;
	m_officialOnly = options.m_officialOnly;
	m_rows.clear();

	m_listbox = findChild("ListboxMap");
	m_search = findChild("TextEntryMapSearch");
	m_players = findChild("ComboBoxMapPlayers");
	m_size = findChild("ComboBoxMapSize");
	m_count = findChild("StaticTextMapCount");
	m_legend = findChild("StaticTextMapLegend");
	for (Int i = 0; i < MAP_SOURCE_COUNT; ++i)
	{
		m_sources[i] = findChild(s_sourceWindowNames[i]);
	}

	if (m_officialOnly && m_filter->m_source == MAP_SOURCE_USER)
		m_filter->m_source = MAP_SOURCE_OFFICIAL;

	m_ignoreEvents = TRUE;

	if (m_players)
	{
		GadgetComboBoxReset(m_players);
		// each entry names its filter, so the combo needs no label: "Any players", "4 players"
		Int index = GadgetComboBoxAddEntry(m_players, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapAnyPlayers", L"Any players"), s_nameColor);
		GadgetComboBoxSetItemData(m_players, index, (void *)0);
		for (Int numPlayers = 2; numPlayers <= MAX_SLOTS; ++numPlayers)
		{
			UnicodeString text;
			text.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapNumPlayers", L"%d players").str(), numPlayers);
			index = GadgetComboBoxAddEntry(m_players, text, s_nameColor);
			GadgetComboBoxSetItemData(m_players, index, (void *)numPlayers);
		}
	}

	if (m_size)
	{
		GadgetComboBoxReset(m_size);
		for (Int size = MAP_SIZE_ANY; size < MAP_SIZE_COUNT; ++size)
		{
			const UnicodeString text = (size == MAP_SIZE_ANY) ? TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapAnySize", L"Any size") : sizeClassText((MapSelectSize)size);
			const Int index = GadgetComboBoxAddEntry(m_size, text, s_nameColor);
			GadgetComboBoxSetItemData(m_size, index, (void *)size);
		}
	}

	if (m_sources[MAP_SOURCE_ALL])
		GadgetRadioSetText(m_sources[MAP_SOURCE_ALL], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapsAll", L"All"));
	if (m_sources[MAP_SOURCE_OFFICIAL])
		GadgetRadioSetText(m_sources[MAP_SOURCE_OFFICIAL], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapsOfficial", L"Official"));
	if (m_sources[MAP_SOURCE_USER])
	{
		GadgetRadioSetText(m_sources[MAP_SOURCE_USER], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapsUser", L"User"));
		m_sources[MAP_SOURCE_USER]->winEnable(!m_officialOnly);
	}
	if (m_sources[MAP_SOURCE_FAVOURITES])
		GadgetRadioSetText(m_sources[MAP_SOURCE_FAVOURITES], TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapsFavourites", L"Favourites"));

	// TheSuperHackers @feature arcticdolphin 08/11/2026 The map select is a dialog with its own title.
	GameWindow *title = findChild("StaticTextTitle");
	if (title)
		GadgetStaticTextSetText(title, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSelectMap", L"Select map"));

	if (m_search)
		GadgetTextEntrySetText(m_search, m_filter->m_search);

	syncControls();

	clearLegendItems();
	if (m_legend)
	{
		GadgetStaticTextSetText(m_legend, UnicodeString::TheEmptyString);
		m_legend->winSetDrawFunc(drawMapLegend);
	}

	clearCountString();
	if (m_count)
		m_count->winSetDrawFunc(drawMapCount);

	if (m_listbox)
	{
		GameWinInputFunc current = m_listbox->winGetInputFunc();
		if (current != mapSelectListboxInput)
		{
			s_listboxInput = current;
			m_listbox->winSetInputFunc(mapSelectListboxInput);
		}
	}
	s_activeList = this;

	m_ignoreEvents = FALSE;

	repopulate(mapToSelect);

	// typing searches at once; Esc still reaches the dialog's input callback through the parents
	if (m_search)
		TheWindowManager->winSetFocus(m_search);
}

void MapSelectList::shutdown()
{
	if (s_activeList == this)
		s_activeList = nullptr;

	m_parent = nullptr;
	m_listbox = nullptr;
	m_search = nullptr;
	m_players = nullptr;
	m_size = nullptr;
	m_count = nullptr;
	m_legend = nullptr;
	clearLegendItems();
	clearCountString();
	for (Int i = 0; i < MAP_SOURCE_COUNT; ++i)
	{
		m_sources[i] = nullptr;
	}
	m_filter = nullptr;
	m_rows.clear();
}

void MapSelectList::syncControls()
{
	if (m_players)
	{
		Int pos = 0;
		if (m_filter->m_numPlayers >= 2 && m_filter->m_numPlayers <= MAX_SLOTS)
			pos = m_filter->m_numPlayers - 1;
		GadgetComboBoxSetSelectedPos(m_players, pos);
	}

	if (m_size)
		GadgetComboBoxSetSelectedPos(m_size, (Int)m_filter->m_size);

	if (m_sources[m_filter->m_source])
		GadgetRadioSetSelection(m_sources[m_filter->m_source], FALSE);
}

Bool MapSelectList::passesScreen( const AsciiString &path, const MapMetaData &mapData, Bool isOfficial ) const
{
	if (mapData.m_displayName.isEmpty())
		return FALSE;

	if (m_officialOnly && !isOfficial)
		return FALSE;

	// skirmish also lists the single player maps of the user map folder
	if (!mapData.m_isMultiplayer && !(m_includeSoloUserMaps && !isOfficial))
		return FALSE;

#if RTS_ZEROHOUR
	//Patch 1.03 -- Purposely filter out these broken maps that exist in Generals.
	if( !path.compare( "maps\\armored fury\\armored fury.map" ) ||
		!path.compare( "maps\\scorched earth\\scorched earth.map" ) )
	{
		return FALSE;
	}
#endif

	return TRUE;
}

Bool MapSelectList::passesFilter( const AsciiString &path, const MapMetaData &mapData, Bool isOfficial, Bool isFavourite, const UnicodeString &needle ) const
{
	switch (m_filter->m_source)
	{
		case MAP_SOURCE_OFFICIAL:
			if (!isOfficial)
				return FALSE;
			break;
		case MAP_SOURCE_USER:
			if (isOfficial)
				return FALSE;
			break;
		case MAP_SOURCE_FAVOURITES:
			if (!isFavourite)
				return FALSE;
			break;
		default:
			break;
	}

	if (m_filter->m_numPlayers > 0 && mapData.m_numPlayers != m_filter->m_numPlayers)
		return FALSE;

	if (m_filter->m_size != MAP_SIZE_ANY && getMapSizeClass(mapData) != m_filter->m_size)
		return FALSE;

	if (!needle.isEmpty())
	{
		if (wcsstr(lowered(mapData.m_displayName).str(), needle.str()) == nullptr &&
			wcsstr(lowered(mapFileName(path)).str(), needle.str()) == nullptr)
		{
			return FALSE;
		}
	}

	return TRUE;
}

Int MapSelectList::setStarCell( Int row, Bool isFavourite )
{
	const Image *on = TheMappedImageCollection->findImageByName("GOMapFavouriteOn");
	const Image *off = TheMappedImageCollection->findImageByName("GOMapFavouriteOff");
	if (on != nullptr && off != nullptr)
	{
		const Image *image = isFavourite ? on : off;
		const Int size = cellImageSize(m_listbox, STAR_COLUMN, image);
		return GadgetListBoxAddEntryImage(m_listbox, image, row, STAR_COLUMN, size, size, TRUE);
	}

	// text star until the kit images exist
	return GadgetListBoxAddEntryText(m_listbox, isFavourite ? UnicodeString(L"*") : UnicodeString(L" "), s_nameColor, row, STAR_COLUMN);
}

void MapSelectList::updateCount( Int shown, Int total )
{
	if (!m_count)
		return;

	UnicodeString text;
	text.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapCount", L"%d of %d maps").str(), shown, total);
	GadgetStaticTextSetText(m_count, text);

	if (s_countString == nullptr)
		s_countString = TheDisplayStringManager->newDisplayString();
	if (s_countString != nullptr)
	{
		s_countString->setFont(m_count->winGetFont());
		s_countString->setText(text);
	}
}

void MapSelectList::repopulate( const AsciiString &mapToSelect )
{
	if (!m_listbox || !m_filter || !TheMapCache)
		return;

	AsciiString selectKey = mapToSelect;
	selectKey.toLower();

	AsciiString officialDir = TheMapCache->getMapDir();
	officialDir.toLower();
	AsciiString userDir = TheGlobalData->getPath_UserData();
	userDir.concat(TheMapCache->getMapDir());
	userDir.toLower();

	const UnicodeString needle = lowered(m_filter->m_search);
	std::vector<MapSelectRowSort::Entry> entries;
	Int total = 0;
	MapCache::const_iterator it = TheMapCache->begin();
	for (; it != TheMapCache->end(); ++it)
	{
		const AsciiString &path = it->first;
		const MapMetaData &mapData = it->second;
		const Bool inUserDir = path.startsWithNoCase(userDir.str());
		const Bool inOfficialDir = !inUserDir && path.startsWithNoCase(officialDir.str());
		if (!inUserDir && !inOfficialDir)
			continue;
		if (!passesScreen(path, mapData, inOfficialDir))
			continue;
		++total;

		const Bool favourite = isMapFavourite(path);
		if (!passesFilter(path, mapData, inOfficialDir, favourite, needle))
			continue;

		MapSelectRowSort::Entry entry;
		entry.m_path = path;
		entry.m_name = mapData.m_displayName;
		entry.m_numPlayers = mapData.m_numPlayers;
		entry.m_favourite = favourite;
		entries.push_back(entry);
	}
	std::sort(entries.begin(), entries.end(), MapSelectRowSort());

	SkirmishBattleHonors *honors = m_showMedals ? new SkirmishBattleHonors : nullptr;

	const Int count = (Int)entries.size();
	m_rows.clear();
	m_rows.reserve(count);
	for (Int entryIndex = 0; entryIndex < count; ++entryIndex)
	{
		Row row;
		row.m_path = entries[entryIndex].m_path;
		const MapMetaData &mapData = TheMapCache->find(row.m_path)->second;
		row.m_medal = (honors && mapData.m_isMultiplayer) ? computeMedal(honors, row.m_path, mapData) : -1;
		m_rows.push_back(row);
	}
	delete honors;

	const Int nameColumn = m_showMedals ? 2 : 1;
	const Int numColumns = GadgetListBoxGetNumColumns(m_listbox);
	Int selection = count > 0 ? 0 : -1;

	GadgetListBoxReset(m_listbox);
	// every result gets a row
	GadgetListBoxSetListLength(m_listbox, max(count, 1));
	{
		GadgetListBoxBatchAddScope batch(m_listbox);
		for (Int i = 0; i < count; ++i)
		{
			const MapSelectRowSort::Entry &entry = entries[i];
			const MapMetaData &mapData = TheMapCache->find(entry.m_path)->second;

			const Int index = setStarCell(-1, entry.m_favourite);
			if (index < 0)
				break;

			if (m_showMedals && numColumns > 1)
			{
				const Image *medal = medalImage(m_rows[i].m_medal);
				const Int size = cellImageSize(m_listbox, 1, medal);
				GadgetListBoxAddEntryImage(m_listbox, medal, index, 1, size, size, TRUE);
				GadgetListBoxSetItemData(m_listbox, (void *)m_rows[i].m_medal, index, 1);
			}

			if (nameColumn < numColumns)
				GadgetListBoxAddEntryText(m_listbox, entry.m_name, s_nameColor, index, nameColumn);

			if (nameColumn + 1 < numColumns)
			{
				UnicodeString players;
				players.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOMapNumPlayers", L"%d players").str(), entry.m_numPlayers);
				GadgetListBoxAddEntryText(m_listbox, players, s_detailColor, index, nameColumn + 1);
			}

			if (nameColumn + 2 < numColumns)
				GadgetListBoxAddEntryText(m_listbox, sizeClassText(getMapSizeClass(mapData)), s_detailColor, index, nameColumn + 2);

			// the row owns a stable copy of the map path, the cache may change while the list is up
			GadgetListBoxSetItemData(m_listbox, (void *)m_rows[i].m_path.str(), index);

			if (entry.m_path == selectKey)
				selection = index;
		}
	}

	updateCount(count, total);

	GadgetListBoxSetSelected(m_listbox, selection);

	if (selection >= 0)
	{
		Int topIndex = GadgetListBoxGetTopVisibleEntry(m_listbox);
		Int bottomIndex = GadgetListBoxGetBottomVisibleEntry(m_listbox);
		Int rowsOnScreen = bottomIndex - topIndex;

		if (selection < topIndex || selection >= bottomIndex)
		{
			GadgetListBoxSetTopVisibleEntry(m_listbox, max(0, selection - max(1, rowsOnScreen / 2)));
		}
	}
}

MapSelectMsgResult MapSelectList::handleSystemMessage( UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if (m_ignoreEvents || !m_filter || !m_listbox)
		return MAP_SELECT_MSG_IGNORED;

	GameWindow *control = (GameWindow *)mData1;
	if (control == nullptr)
		return MAP_SELECT_MSG_IGNORED;

	switch (msg)
	{
		case GEM_UPDATE_TEXT:
		{
			if (control != m_search)
				break;
			UnicodeString search = GadgetTextEntryGetText(m_search);
			search.trim();
			if (search.compare(m_filter->m_search) != 0)
			{
				m_filter->m_search = search;
				repopulate(getSelectedMap());
			}
			return MAP_SELECT_MSG_HANDLED;
		}

		case GCM_SELECTED:
		{
			if (control != m_players && control != m_size)
				break;
			Int pos = -1;
			GadgetComboBoxGetSelectedPos(control, &pos);
			if (pos < 0)
				return MAP_SELECT_MSG_HANDLED;
			const Int value = (Int)GadgetComboBoxGetItemData(control, pos);
			if (control == m_players)
				m_filter->m_numPlayers = value;
			else
				m_filter->m_size = (MapSelectSize)value;
			repopulate(getSelectedMap());
			return MAP_SELECT_MSG_HANDLED;
		}

		case GBM_SELECTED:
		{
			for (Int i = 0; i < MAP_SOURCE_COUNT; ++i)
			{
				if (control == m_sources[i] && m_sources[i] != nullptr)
				{
					m_filter->m_source = (MapSelectSource)i;
					repopulate(getSelectedMap());
					return MAP_SELECT_MSG_SOURCE_CHANGED;
				}
			}
			break;
		}

		case GLM_RIGHT_CLICKED:
		{
			if (control != m_listbox)
				break;
			const RightClickStruct *rc = (const RightClickStruct *)mData2;
			if (rc != nullptr && rc->pos >= 0)
				toggleFavouriteAtRow(rc->pos);
			return MAP_SELECT_MSG_HANDLED;
		}
	}

	return MAP_SELECT_MSG_IGNORED;
}

void MapSelectList::toggleFavouriteAtRow( Int row )
{
	if (!m_listbox || row < 0 || row >= (Int)m_rows.size())
		return;

	const AsciiString &path = m_rows[row].m_path;
	const Bool favourite = !isMapFavourite(path);
	setMapFavourite(path, favourite);
	setStarCell(row, favourite);
}

void MapSelectList::refreshLegend( const AsciiString &mapPath )
{
	clearLegendItems();
	if (!m_legend || mapPath.isEmpty())
		return;

	// the per-type names stay in the preview marker tooltips
	MapPreviewLegend legend;
	if (!getMapPreviewLegend(mapPath, legend))
		return;

	for (size_t i = 0; i < legend.size(); ++i)
	{
		MapLegendItem item;
		item.m_image = legend[i].m_image;
		item.m_count = TheDisplayStringManager->newDisplayString();
		if (item.m_count == nullptr)
			continue;
		UnicodeString countText;
		countText.format(L"%d", legend[i].m_count);
		item.m_count->setFont(m_legend->winGetFont());
		item.m_count->setText(countText);
		s_legendItems.push_back(item);
	}
}

AsciiString MapSelectList::getMapAtRow( Int row ) const
{
	if (row < 0 || row >= (Int)m_rows.size())
		return AsciiString::TheEmptyString;
	return m_rows[row].m_path;
}

AsciiString MapSelectList::getSelectedMap() const
{
	if (!m_listbox)
		return AsciiString::TheEmptyString;
	Int selected = -1;
	GadgetListBoxGetSelected(m_listbox, &selected);
	return getMapAtRow(selected);
}

Int MapSelectList::getMedalAtRow( Int row ) const
{
	if (row < 0 || row >= (Int)m_rows.size())
		return -1;
	return m_rows[row].m_medal;
}
