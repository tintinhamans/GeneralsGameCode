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

// FILE: MapSelectList.h //////////////////////////////////////////////////////////////////////////
// GenOnline @feature arcticdolphin 08/11/2026 Search, filters and favourites for the map select popups.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/STLTypedefs.h"
#include "GameClient/GameWindow.h"

class MapMetaData;

enum MapSelectSource
{
	MAP_SOURCE_ALL,
	MAP_SOURCE_OFFICIAL,
	MAP_SOURCE_USER,
	MAP_SOURCE_FAVOURITES,

	MAP_SOURCE_COUNT
};

enum MapSelectSize
{
	MAP_SIZE_ANY,
	MAP_SIZE_SMALL,
	MAP_SIZE_MEDIUM,
	MAP_SIZE_LARGE,

	MAP_SIZE_COUNT
};

// Size classes by the larger map extent side in world units: about a third of the official and user maps each.
enum
{
	MAP_SIZE_MEDIUM_MIN_EXTENT = 3000,
	MAP_SIZE_LARGE_MIN_EXTENT = 3750
};

enum MapSelectMsgResult
{
	MAP_SELECT_MSG_IGNORED,        ///< not a filter message, the menu handles it
	MAP_SELECT_MSG_HANDLED,        ///< filter or favourite changed
	MAP_SELECT_MSG_SOURCE_CHANGED  ///< a source button was picked
};

/// The filter state a menu keeps for the session.
struct MapSelectFilter
{
	MapSelectFilter();

	UnicodeString m_search;    ///< substring of the display name or file name, any case
	Int m_numPlayers;          ///< 0 for any
	MapSelectSize m_size;
	MapSelectSource m_source;
};

struct MapSelectOptions
{
	MapSelectOptions();

	Bool m_includeSoloUserMaps; ///< skirmish lists single player user maps too
	Bool m_showMedals;          ///< skirmish shows the battle honor medal column
	Bool m_officialOnly;        ///< stats games may only use official maps
};

MapSelectSize getMapSizeClass( const MapMetaData &mapData );
AsciiString getMapFavouriteKey( const AsciiString &mapPath ); ///< lowercased portable map path
Bool isMapFavourite( const AsciiString &mapPath );
void setMapFavourite( const AsciiString &mapPath, Bool favourite );

/// Fills a map select list box from TheMapCache through the search, filter and source windows of the popup.
/// List columns: star, [medal,] name, players, size. Column 0 item data is the map path of the row.
class MapSelectList
{
public:
	MapSelectList();
	~MapSelectList();

	/// Binds the windows named "<layoutName>:..." and fills the list. Call TheMapCache->updateCache() first.
	void init( GameWindow *parent, const char *layoutName, const MapSelectOptions &options, MapSelectFilter *filter, const AsciiString &mapToSelect );
	void shutdown();

	void repopulate( const AsciiString &mapToSelect );
	MapSelectMsgResult handleSystemMessage( UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );

	void toggleFavouriteAtRow( Int row );
	void refreshLegend( const AsciiString &mapPath );

	GameWindow *getListbox() const { return m_listbox; }
	AsciiString getMapAtRow( Int row ) const;
	AsciiString getSelectedMap() const;
	Int getMedalAtRow( Int row ) const;
	Int getMedalColumn() const { return m_showMedals ? 1 : -1; }

	enum { STAR_COLUMN = 0 };

private:
	struct Row
	{
		AsciiString m_path;
		Int m_medal;
	};
	typedef std::vector<Row> RowList;

	Bool passesScreen( const AsciiString &path, const MapMetaData &mapData, Bool isOfficial ) const;
	Bool passesFilter( const AsciiString &path, const MapMetaData &mapData, Bool isOfficial, Bool isFavourite, const UnicodeString &needle ) const;
	Int setStarCell( Int row, Bool isFavourite ); ///< row -1 adds a row; returns the row index
	void updateCount( Int shown, Int total );
	void syncControls();
	GameWindow *findChild( const char *name ) const;

	GameWindow *m_parent;
	GameWindow *m_listbox;
	GameWindow *m_search;
	GameWindow *m_players;
	GameWindow *m_size;
	GameWindow *m_sources[MAP_SOURCE_COUNT];
	GameWindow *m_count;
	GameWindow *m_legend;
	AsciiString m_layoutName;
	MapSelectFilter *m_filter;
	Bool m_includeSoloUserMaps;
	Bool m_showMedals;
	Bool m_officialOnly;
	Bool m_ignoreEvents;
	RowList m_rows;
};
