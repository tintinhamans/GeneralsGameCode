/*
**	Command & Conquer Generals(tm)
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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LobbyUtils.cpp
// Author: Matthew D. Campbell, Sept 2002
// Description: GameSpy lobby utils
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/GOUIColors.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/Version.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Image.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GameSpyOverlay.h"

#include "GameClient/LanguageFilter.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"

#include "Common/STLTypedefs.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineAnticheatKind.h"
#include "GameClient/GenOnlineLobbyGameDetails.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// Note: if you add more columns, you must modify the .wnd files and change the listbox properties (yuck!)

// TODO_GENONLINE: We probably need to modify this in the WND... they set the width for observer to 0, for now we dont show ping, but we should show box
#if defined(GENONLINE)
enum {
	COLUMN_NAME = 0,
	COLUMN_MAP,
	COLUMN_LADDER,
	COLUMN_NUMPLAYERS,
	COLUMN_PASSWORD,
	COLUMN_OBSERVER,
  COLUMN_USE_STATS,
  // GenOnline @feature arcticdolphin 22/11/2026 A shield column shows the anti-cheat the lobby requires.
  COLUMN_ANTICHEAT,
  COLUMN_PING
};
#else
enum {
	COLUMN_NAME = 0,
	COLUMN_MAP,
	COLUMN_LADDER,
	COLUMN_NUMPLAYERS,
	COLUMN_PASSWORD,
	COLUMN_OBSERVER,
#if !RTS_GENERALS
  COLUMN_USE_STATS,
#endif
	COLUMN_PING,
};
#endif

static NameKeyType buttonSortAlphaID = NAMEKEY_INVALID;
static NameKeyType buttonSortPingID = NAMEKEY_INVALID;
static NameKeyType buttonSortBuddiesID = NAMEKEY_INVALID;
static NameKeyType windowSortAlphaID = NAMEKEY_INVALID;
static NameKeyType windowSortPingID = NAMEKEY_INVALID;
static NameKeyType windowSortBuddiesID = NAMEKEY_INVALID;

static GameWindow *buttonSortAlpha = nullptr;
static GameWindow *buttonSortPing = nullptr;
static GameWindow *buttonSortBuddies = nullptr;
static GameWindow *windowSortAlpha = nullptr;
static GameWindow *windowSortPing = nullptr;
static GameWindow *windowSortBuddies = nullptr;

// GenOnline @feature arcticdolphin 08/11/2026 The game list starts newest first, the order a third header click returns to.
static GameSortType theGameSortType = GAMESORT_AGE_ASCENDING;
static Bool sortBuddies = TRUE;

// GenOnline @feature arcticdolphin 08/11/2026 Header labels over the game list columns, a sorting push button cycles ascending, descending, newest first.
struct GameListHeader
{
	const char *name;
	Int column;
	Bool sorts;
	GameSortType ascending;
	GameSortType descending;
	const char *label;
	const WideChar *text;
};

static const GameListHeader gameListHeaders[] =
{
	{ "WOLCustomLobby.wnd:StaticTextHeaderGame", COLUMN_NAME, FALSE, GAMESORT_AGE_ASCENDING, GAMESORT_AGE_ASCENDING, "GUI:GOHeaderGame", L"Game" },
	{ "WOLCustomLobby.wnd:ButtonHeaderMap", COLUMN_MAP, TRUE, GAMESORT_MAP_ASCENDING, GAMESORT_MAP_DESCENDING, "GUI:GOHeaderMap", L"Map" },
	{ "WOLCustomLobby.wnd:ButtonHeaderPlayers", COLUMN_NUMPLAYERS, TRUE, GAMESORT_PLAYERS_ASCENDING, GAMESORT_PLAYERS_DESCENDING, "GUI:GOHeaderPlayers", L"Players" },
	{ "WOLCustomLobby.wnd:StaticTextHeaderLock", COLUMN_PASSWORD, FALSE, GAMESORT_AGE_ASCENDING, GAMESORT_AGE_ASCENDING, nullptr, nullptr },
	{ "WOLCustomLobby.wnd:ButtonHeaderPing", COLUMN_PING, TRUE, GAMESORT_PING_ASCENDING, GAMESORT_PING_DESCENDING, "GUI:GOHeaderPing", L"Ping" },
};
static const Int GAME_LIST_HEADER_COUNT = ARRAY_SIZE(gameListHeaders);
// GenOnline @tweak arcticdolphin 08/11/2026 Space between a header label and its sort triangle.
static const Int HEADER_ARROW_GAP = 3;

static NameKeyType gameListHeaderIDs[ARRAY_SIZE(gameListHeaders)];
static GameWindow *gameListHeaderWindows[ARRAY_SIZE(gameListHeaders)];
static GameWindow *windowHeaderArrow = nullptr;
static GameWindow *windowHeaderCorner = nullptr;

// The .wnd supplies the ascending arrow as the enabled image and the descending arrow as the disabled image.
static void showSortArrow(GameWindow *window, Bool ascending)
{
	if (window == nullptr)
		return;

	window->winHide(FALSE);
	window->winEnable(ascending);
}

// GenOnline @feature arcticdolphin 08/11/2026 Move a window to a screen position inside its parent.
static void setScreenPosition(GameWindow *window, Int x, Int y)
{
	Int parentX = 0;
	Int parentY = 0;
	GameWindow *parentWindow = window->winGetParent();
	if (parentWindow != nullptr)
		parentWindow->winGetScreenPosition(&parentX, &parentY);
	window->winSetPosition(x - parentX, y - parentY);
}

// GenOnline @tweak arcticdolphin 08/11/2026 The label of a sorting header button, in its window font without wrapping.
static DisplayString *getHeaderButtonLabel(GameWindow *window)
{
	DisplayString *text = window->winGetInstanceData()->getTextDisplayString();
	if (text == nullptr || text->getTextLength() == 0)
		return nullptr;

	GameFont *font = TheHeaderTemplateManager->getFontFromTemplate("GOLabelSmall");
	if (font == nullptr)
		font = window->winGetFont();
	if (text->getFont() != font)
		text->setFont(font);
	text->setWordWrap(0);
	return text;
}

// GenOnline @tweak arcticdolphin 22/11/2026 A small down or up triangle that marks the active sort column.
static void drawSortTriangle(Int left, Int centerY, Bool pointsDown, Color color)
{
	const Int size = GameWindowManager::winScaleLayoutSize(4);
	for (Int row = 0; row < size; ++row)
	{
		const Int half = pointsDown ? (size - 1 - row) : row;
		const Int top = centerY - size / 2 + row;
		TheWindowManager->winFillRect(color, 1.0f, left + size - 1 - half, top, left + size + half, top + 1);
	}
}

// GenOnline @tweak arcticdolphin 22/11/2026 The game list header cell that currently sorts the list, or null.
static const GameListHeader *activeSortHeader(GameWindow *window)
{
	for (Int i = 0; i < GAME_LIST_HEADER_COUNT; ++i)
	{
		const GameListHeader &header = gameListHeaders[i];
		if (window == gameListHeaderWindows[i] && header.sorts && (theGameSortType == header.ascending || theGameSortType == header.descending))
			return &header;
	}
	return nullptr;
}

// GenOnline @tweak arcticdolphin 22/11/2026 Every header cell fills the same dark strip with one thin bottom line and a muted label, the active sort label is white with a triangle.
static void drawGameListHeaderCell(GameWindow *window, WinInstanceData *instData)
{
	Int x, y, width, height, textWidth, textHeight;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);

	const Int lineWidth = (std::min)(2, (std::max)(1, GadgetListBoxGetKitScrollLineWidth()));
	TheWindowManager->winFillRect(GOUIColor(GOUICOLOR_FIELD_FILL, GameMakeColor(5, 7, 15, 255)), 1.0f, x, y, x + width, y + height);
	// The list frame below draws the line that separates the header, so the strip only adds the top and the outer sides.
	GadgetListBoxDrawKitLines(x, y, width, height, window == gameListHeaderWindows[0], TRUE, window == windowHeaderCorner, FALSE);

	DisplayString *text = getHeaderButtonLabel(window);
	if (text == nullptr)
		return;

	const GameListHeader *active = activeSortHeader(window);
	Color textColor = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	if (active != nullptr || BitIsSet(instData->getState(), WIN_STATE_HILITED))
		textColor = GameMakeColor(255, 255, 255, 255);

	text->getSize(&textWidth, &textHeight);
	IRegion2D clipRegion;
	clipRegion.lo.x = x;
	clipRegion.lo.y = y;
	clipRegion.hi.x = x + width;
	clipRegion.hi.y = y + height;
	text->setClipRegion(&clipRegion);
	const Int textX = x + GameWindowManager::winScaleLayoutSize(TEXT_X_OFFSET);
	text->draw(textX, y + (height - textHeight) / 2, textColor, GameMakeColor(0, 0, 0, 255));
	if (active != nullptr)
		drawSortTriangle(textX + textWidth + GameWindowManager::winScaleLayoutSize(HEADER_ARROW_GAP), y + (height - lineWidth) / 2, theGameSortType == active->descending, textColor);
}

// GenOnline @feature arcticdolphin 22/11/2026 The empty search field shows its hint, kept as the window text that the entry field itself never draws.
static GameWinDrawFunc s_searchDraw = nullptr;
static void drawSearchWithHint(GameWindow *window, WinInstanceData *instData)
{
	if (s_searchDraw != nullptr)
		s_searchDraw(window, instData);

	DisplayString *hint = instData->getTextDisplayString();
	if (hint == nullptr || hint->getTextLength() == 0 || GadgetTextEntryGetText(window).getLength() > 0)
		return;

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	if (hint->getFont() != window->winGetFont())
		hint->setFont(window->winGetFont());
	hint->setWordWrap(0);
	IRegion2D clipRegion;
	clipRegion.lo.x = x;
	clipRegion.lo.y = y;
	clipRegion.hi.x = x + width;
	clipRegion.hi.y = y + height;
	hint->setClipRegion(&clipRegion);
	const Int textY = y + (height - TheWindowManager->winFontHeight(instData->getFont())) / 2;
	hint->draw(x + GameWindowManager::winScaleLayoutSize(7), textY, GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255)), GameMakeColor(0, 0, 0, 255));
}

// GenOnline @feature arcticdolphin 08/11/2026 Snap the header cells onto the list columns as the engine sized them.
static void alignGameListHeaders(GameWindow *list)
{
	if (list == nullptr)
		return;

	Int listX, listY;
	list->winGetScreenPosition(&listX, &listY);
	const Int columns = GadgetListBoxGetNumColumns(list);
	for (Int i = 0; i < GAME_LIST_HEADER_COUNT; ++i)
	{
		GameWindow *cell = gameListHeaderWindows[i];
		if (cell == nullptr)
			continue;

		// A cell spans the columns up to the next cell, so the header bar has no gaps over unlabelled columns.
		const Int column = gameListHeaders[i].column;
		const Int nextColumn = (i + 1 < GAME_LIST_HEADER_COUNT) ? gameListHeaders[i + 1].column : columns;
		Int width = 0;
		for (Int c = column; c < nextColumn && c < columns; ++c)
			width += GadgetListBoxGetColumnWidth(list, c);
		if (width <= 0)
		{
			cell->winHide(TRUE);
			continue;
		}

		Int left = 0;
		for (Int c = 0; c < column; ++c)
			left += GadgetListBoxGetColumnWidth(list, c);

		Int cellX, cellY, cellWidth, cellHeight;
		cell->winGetScreenPosition(&cellX, &cellY);
		cell->winGetSize(&cellWidth, &cellHeight);
		setScreenPosition(cell, listX + left, cellY);
		cell->winSetSize(width, cellHeight);

		cell->winSetDrawFunc(drawGameListHeaderCell);
	}

	// The corner cell covers the scroll bar column from the last list column to the list edge.
	GameWindow *corner = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("WOLCustomLobby.wnd:StaticTextHeaderCorner"));
	windowHeaderCorner = corner;
	if (corner != nullptr)
	{
		Int columnsWidth = 0;
		for (Int c = 0; c < columns; ++c)
			columnsWidth += GadgetListBoxGetColumnWidth(list, c);
		Int listWidth, listHeight, cornerX, cornerY, cornerWidth, cornerHeight;
		list->winGetSize(&listWidth, &listHeight);
		corner->winGetScreenPosition(&cornerX, &cornerY);
		corner->winGetSize(&cornerWidth, &cornerHeight);
		corner->winSetDrawFunc(drawGameListHeaderCell);
		if (listWidth > columnsWidth)
		{
			setScreenPosition(corner, listX + columnsWidth, cornerY);
			corner->winSetSize(listWidth - columnsWidth, cornerHeight);
		}
	}
}

// GenOnline @tweak arcticdolphin 22/11/2026 The header cells draw their own sort triangle, so the arrow window stays hidden.
static void showHeaderArrow()
{
	if (windowHeaderArrow != nullptr)
		windowHeaderArrow->winHide(TRUE);
}

static void showSortIcons()
{
	if (windowSortAlpha && windowSortPing)
	{
		switch (theGameSortType)
		{
		case GAMESORT_AGE_ASCENDING: // was alpha
			showSortArrow(windowSortAlpha, TRUE);
			windowSortPing->winHide(TRUE);
			break;
		case GAMESORT_AGE_DESCENDING: // was alpha
			showSortArrow(windowSortAlpha, FALSE);
			windowSortPing->winHide(TRUE);
			break;
		case GAMESORT_MAP_ASCENDING: // was ping
			showSortArrow(windowSortPing, TRUE);
			windowSortAlpha->winHide(TRUE);
			break;
		case GAMESORT_MAP_DESCENDING: // was ping
			showSortArrow(windowSortPing, FALSE);
			windowSortAlpha->winHide(TRUE);
			break;
		// GenOnline @feature arcticdolphin 08/11/2026 The old sort buttons have no arrow for the header sorts.
		default:
			windowSortAlpha->winHide(TRUE);
			windowSortPing->winHide(TRUE);
			break;
		}
	}

	if (windowSortBuddies)
	{
		if (sortBuddies)
		{
			// true sorts before false.
			showSortArrow(windowSortBuddies, FALSE);
		}
		else
		{
			windowSortBuddies->winHide(TRUE);
		}
	}

	showHeaderArrow();
}

LobbyGameModeFilter theLobbyFilter = LOBBY_FILTER_ALL;
// GenOnline @feature arcticdolphin 22/11/2026 The lobby menu search text filters the list by lobby and map name.
extern UnicodeString theLobbySearchText;

static UnicodeString lobbyMapDisplayName(const LobbyEntry& lobby)
{
	const MapMetaData* md = TheMapCache->findMap(AsciiString(lobby.map_name.c_str()));
	if (md)
	{
		return md->m_displayName;
	}

	const char* start = lobby.map_name.c_str();
	const char* slashPos = strrchr(start, '\\');
	if (slashPos)
	{
		start = slashPos + 1;
	}

	UnicodeString mapName;
	mapName.format(L"%s", from_utf8(start).c_str());
	return mapName;
}

static std::wstring toLowerWide(const WideChar* text)
{
	std::wstring lower(text);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
	return lower;
}

static Bool lobbyMatchesSearch(const LobbyEntry& lobby, const std::wstring& lowerSearch)
{
	return toLowerWide(from_utf8(lobby.name).c_str()).find(lowerSearch) != std::wstring::npos
		|| toLowerWide(lobbyMapDisplayName(lobby).str()).find(lowerSearch) != std::wstring::npos;
}

static LobbyGameModeFilter detectGameMode(const std::string& name)
{
	std::string modeName = name;
	std::transform(modeName.begin(), modeName.end(), modeName.begin(), tolower);

	// remove spaces
	modeName.erase(std::remove(modeName.begin(), modeName.end(), ' '), modeName.end());

	// handle common variations
	for (size_t i = 0; i < modeName.size(); ++i)
	{
		if (modeName.compare(i, 2, "vs") == 0)
		{
			modeName.erase(i + 1, 1);
			continue;
		}

		if (modeName[i] == 'x')
			modeName[i] = 'v';
	}

	if (modeName.find("aod") != -1)
		return LOBBY_FILTER_AOD;
	if (modeName.find("ffa") != -1 || modeName.find("1v1v1") != -1)
		return LOBBY_FILTER_FFA;
	if (modeName.find("1v1") != -1)
		return LOBBY_FILTER_1V1;
	if (modeName.find("2v2") != -1 || modeName.find("3v3") != -1 || modeName.find("4v4") != -1)
		return LOBBY_FILTER_TEAM;

	return LOBBY_FILTER_ALL;
}

void setSortMode(GameSortType sortType) { theGameSortType = sortType; showSortIcons(); RefreshGameListBoxes(); }
void sortByBuddies(Bool doSort) { sortBuddies = doSort; showSortIcons(); RefreshGameListBoxes(); }

Bool HandleSortButton(NameKeyType sortButton)
{
	if (sortButton == buttonSortBuddiesID)
	{
		sortByBuddies(!sortBuddies);
		return TRUE;
	}
	else if (sortButton == buttonSortAlphaID)
	{
		if (theGameSortType == GAMESORT_AGE_ASCENDING) // was alpha
		{
			setSortMode(GAMESORT_AGE_DESCENDING); // was alpha
		}
		else
		{
			setSortMode(GAMESORT_AGE_ASCENDING); // was alpha
		}
		return TRUE;
	}
	else if (sortButton == buttonSortPingID)
	{
		if (theGameSortType == GAMESORT_MAP_ASCENDING) // was ping
		{
			setSortMode(GAMESORT_MAP_DESCENDING); // was ping
		}
		else
		{
			setSortMode(GAMESORT_MAP_ASCENDING); // was ping
		}
		return TRUE;
	}

	// GenOnline @feature arcticdolphin 08/11/2026 The sorting header cells of the game list.
	for (Int i = 0; i < GAME_LIST_HEADER_COUNT; ++i)
	{
		const GameListHeader &header = gameListHeaders[i];
		if (gameListHeaderWindows[i] == nullptr || !header.sorts || sortButton != gameListHeaderIDs[i])
			continue;

		if (theGameSortType == header.ascending)
			setSortMode(header.descending);
		else if (theGameSortType == header.descending)
			setSortMode(GAMESORT_AGE_ASCENDING);
		else
			setSortMode(header.ascending);
		return TRUE;
	}
	return FALSE;
}

// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
//static NameKeyType parentGameListSmallID = NAMEKEY_INVALID;
static NameKeyType parentGameListLargeID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyGamesSmallID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyGamesLargeID = NAMEKEY_INVALID;
//static NameKeyType listboxLobbyGameInfoID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = nullptr;
//static GameWindow *parentGameListSmall = nullptr;
static GameWindow *parentGameListLarge = nullptr;
       //GameWindow *listboxLobbyGamesSmall = nullptr;
       GameWindow *listboxLobbyGamesLarge = nullptr;
       //GameWindow *listboxLobbyGameInfo = nullptr;

static const Image *pingImages[3] = { nullptr, nullptr, nullptr };

// GenOnline @tweak arcticdolphin 22/11/2026 Every list icon is scaled to the same box and the list centres it in its column.
static void addListIcon(GameWindow *win, const Image *img, Int index, Int column)
{
	const Int box = GameWindowManager::winScaleLayoutSize(10);
	const Int side = (std::max)(1, (std::max)(img->getImageWidth(), img->getImageHeight()));
	GadgetListBoxAddEntryImage(win, img, index, column, (std::max)(1, img->getImageWidth() * box / side), (std::max)(1, img->getImageHeight() * box / side));
}

// GenOnline @feature arcticdolphin 22/11/2026 The ping column draws five signal bars over the list, the latency per row index is kept here.
static std::vector<Int> s_lobbyRowPings;
static GameWinDrawFunc s_gameListDraw = nullptr;

static void drawGameListPing(GameWindow *window, WinInstanceData *instData)
{
	if (s_gameListDraw != nullptr)
		s_gameListDraw(window, instData);

	ListboxData *list = (ListboxData *)window->winGetUserData();
	if (list == nullptr || list->columns <= COLUMN_PING || list->columnWidth[COLUMN_PING] <= 0)
		return;

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	Int columnX = x + GadgetListBoxGetKitScrollLineWidth();
	for (Int j = 0; j < COLUMN_PING; ++j)
		columnX += list->columnWidth[j];

	const Int top = y + 4;
	const Int bottom = y + height - 1;
	Int rowY = top - list->displayPos;
	const Color unlit = GameMakeColor(52, 57, 88, 255);
	for (Int i = 0; i < list->endPos; ++i)
	{
		const Int rowHeight = list->listData[i].height + 1;
		const Int rowTop = rowY;
		rowY += rowHeight;
		if (rowTop < top || rowTop + rowHeight > bottom || (size_t)i >= s_lobbyRowPings.size() || s_lobbyRowPings[i] < 0)
			continue;

		const Int ms = s_lobbyRowPings[i];
		const Int lit = ms < 50 ? 5 : ms < 90 ? 4 : ms < 130 ? 3 : ms < 200 ? 2 : 1;
		const Color lamp = lit >= 4 ? GameMakeColor(56, 224, 122, 255) : lit == 3 ? GameMakeColor(255, 194, 15, 255) : GameMakeColor(255, 106, 74, 255);
		const Int unit = rowHeight >= 36 ? rowHeight / 18 : 1;
		const Int barWidth = unit * 3;
		const Int pitch = barWidth + unit;
		const Int baseline = rowTop + (rowHeight + unit * 12) / 2;
		const Int left = columnX - GadgetListBoxGetKitScrollLineWidth() + GameWindowManager::winScaleLayoutSize(TEXT_X_OFFSET);
		for (Int bar = 0; bar < 5; ++bar)
		{
			const Int barHeight = unit * (3 + bar * 2);
			const Int barLeft = left + bar * pitch;
			TheWindowManager->winFillRect(bar < lit ? lamp : unlit, 1.0f, barLeft, baseline - barHeight, barLeft + barWidth, baseline);
		}
	}
}

static void gameTooltip(GameWindow* window,
	WinInstanceData* instData,
	UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString);//TheGameText->fetch("TOOLTIP:GamesBeingFormed") );
		return;
	}

	Int gameID = (Int)(intptr_t)GadgetListBoxGetItemData(window, row, 0);
#if defined(GENONLINE)
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	LobbyEntry lobbyEntry = pLobbyInterface->GetLobbyFromID(ResolveGameListLobbyID(gameID));
	if (lobbyEntry.lobbyID == -1)
	{
		return;
	}
#else
	GameSpyStagingRoom *room = TheGameSpyInfo->findStagingRoomByID(gameID);
	if (!room)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:UnknownGame") );
		return;
	}
#endif

	if (col == COLUMN_PING)
	{
#if 0 //def DEBUG_LOGGING
		UnicodeString s;
		s.format(L"Ping is %d ms (cutoffs are %d ms and %d ms\n%hs local pings\n%hs remote pings",
			room->getPingAsInt(), TheGameSpyConfig->getPingCutoffGood(), TheGameSpyConfig->getPingCutoffBad(),
			TheGameSpyInfo->getPingString().str(), room->getPingString().str()
		);
		TheMouse->setCursorTooltip( s, 10, nullptr, 2.0f ); // the text and width are the only params used.  the others are the default values.
#else
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:PingInfo"), 10, nullptr, 2.0f ); // the text and width are the only params used.  the others are the default values.
#endif
		return;
	}
	if (col == COLUMN_NUMPLAYERS)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:NumberOfPlayers"), 10, nullptr, 2.0f ); // the text and width are the only params used.  the others are the default values.
		return;
	}
	if (col == COLUMN_PASSWORD)
	{
#if defined(GENONLINE)
		if (lobbyEntry.passworded)
#else
		if (room->getHasPassword())
#endif
		{
			UnicodeString checkTooltip =TheGameText->fetch("TOOTIP:Password");
			if(!checkTooltip.compare(L"Password required to joing game"))
				checkTooltip.set(L"Password required to join game");
			TheMouse->setCursorTooltip( checkTooltip, 10, nullptr, 2.0f ); // the text and width are the only params used.  the others are the default values.
		}
		else
			TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}
#if defined(GENONLINE)
	// GenOnline @feature arcticdolphin 22/11/2026 The shield column names the anti-cheat the lobby requires.
	if (col == COLUMN_ANTICHEAT)
	{
		const EAnticheatKind kind = ParseAnticheatKind(lobbyEntry.anticheat);
		TheMouse->setCursorTooltip( kind == EAnticheatKind::Eac ? AnticheatKindText(kind) : UnicodeString::TheEmptyString, 10, nullptr, 2.0f );
		return;
	}
#endif
#if !RTS_GENERALS
  if (col == COLUMN_USE_STATS)
  {
#if defined(GENONLINE)
	  if (lobbyEntry.track_stats)
#else
	  if (room->getUseStats())
#endif
    {
      TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:UseStatsOn") );
    }
    else
    {
      TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:UseStatsOff") );
    }
    return;
  }
#endif

	UnicodeString tooltip;

	UnicodeString mapName;

#if defined(GENONLINE)
	// GO already has the full map info, don't need the cache
	mapName.translate(lobbyEntry.map_name.c_str());
#else
	const MapMetaData *md = TheMapCache->findMap(room->getMap());
	if (md)
	{
		mapName = md->m_displayName;
	}
	else
	{
		mapName.translate( getFileName(room->getMap().str()) );
	}
#endif
	UnicodeString tmp;

#if defined(GENONLINE)
	UnicodeString gameName;
	gameName.format(L"%s", from_utf8(lobbyEntry.name).c_str());
	tooltip.format(TheGameText->fetch("TOOLTIP:GameInfoGameName"), gameName.str());

	UnicodeString region;
	region.format(L"\n\nRegion: %s", from_utf8(lobbyEntry.region).c_str());
	tooltip.concat(region);

	UnicodeString latency;
	latency.format(L"\nLatency: %d [%dms]\n", ConvertMSLatencyToFrames(lobbyEntry.latency), lobbyEntry.latency);
	tooltip.concat(latency);
#else
	tooltip.format(TheGameText->fetch("TOOLTIP:GameInfoGameName"), room->getGameName().str());
#endif
	
#if defined(GENONLINE)
	// TODO_QUICKMATCH
	if (false)
#else
	if (room->getLadderPort() != 0)

	{
		const LadderInfo *linfo = TheLadderList->findLadder(room->getLadderIP(), room->getLadderPort());
		if (linfo)
		{
			tmp.format(TheGameText->fetch("TOOLTIP:GameInfoLadderName"), linfo->name.str());
			tooltip.concat(tmp);
		}
	}
#endif
#if defined(GENONLINE)
	if (lobbyEntry.exe_crc != TheGlobalData->m_exeCRC || lobbyEntry.ini_crc != TheGlobalData->m_iniCRC)
#else
	if (room->getExeCRC() != TheGlobalData->m_exeCRC || room->getIniCRC() != TheGlobalData->m_iniCRC)
#endif
	{
		tmp.format(TheGameText->fetch("TOOLTIP:InvalidGameVersion"), mapName.str());
		tooltip.concat(tmp);
	}
	tmp.format(TheGameText->fetch("TOOLTIP:GameInfoMap"), mapName.str());
	tooltip.concat(tmp);
	tooltip.concat(L"\n");

#if defined(GENONLINE)
	for (LobbyMemberEntry& member : lobbyEntry.members)
	{
		if (member.IsHuman())
		{
			UnicodeString plrName;
			plrName.format(L"%s [%hs, %dms latency]", from_utf8(member.display_name).c_str(), member.region.c_str(), member.latency);

			// TODO_GENONLINE: We don't have stats info
			//tmp.format(TheGameText->fetch("TOOLTIP:GameInfoPlayer"), plrName.str(), slot->getWins(), slot->getLosses());
			tooltip.concat(L'\n');
			tooltip.concat(plrName);
		}
		else
		{
			switch (member.m_SlotState)
			{
			case SLOT_EASY_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:EasyAI"));
				break;
			case SLOT_MED_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:MediumAI"));
				break;
			case SLOT_BRUTAL_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:HardAI"));
				break;
			}
		}
	}
#else
	AsciiString aPlayer;
	UnicodeString player;
	Int numPlayers = 0;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		GameSpyGameSlot *slot = room->getGameSpySlot(i);
		if (i == 0 && (!slot || !slot->isHuman()))
		{
			DEBUG_CRASH(("About to tooltip a non-hosted game!"));
		}
		if (slot && slot->isHuman())
		{
			tmp.format(TheGameText->fetch("TOOLTIP:GameInfoPlayer"), slot->getName().str(), slot->getWins(), slot->getLosses());
			tooltip.concat(tmp);
			++numPlayers;
		}
		else if (slot && slot->isAI())
		{
			++numPlayers;
			switch(slot->getState())
			{
			case SLOT_EASY_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:EasyAI"));
				break;
			case SLOT_MED_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:MediumAI"));
				break;
			case SLOT_BRUTAL_AI:
				tooltip.concat(L'\n');
				tooltip.concat(TheGameText->fetch("GUI:HardAI"));
				break;
			}
		}
	}
	DEBUG_ASSERTCRASH(numPlayers, ("Tooltipping a 0-player game!"));
#endif

	TheMouse->setCursorTooltip( tooltip, 10, nullptr, 2.0f ); // the text and width are the only params used.  the others are the default values.
}

static Bool isSmall = TRUE;

GameWindow *GetGameListBox()
{
	return listboxLobbyGamesLarge;
}

GameWindow *GetGameInfoListBox()
{
	return nullptr;
}

NameKeyType GetGameListBoxID()
{
	return listboxLobbyGamesLargeID;
}

NameKeyType GetGameInfoListBoxID()
{
	return NAMEKEY_INVALID;
}

void GrabWindowInfo()
{
	isSmall = TRUE;
	parentID = NAMEKEY( "WOLCustomLobby.wnd:WOLLobbyMenuParent" );
	parent = TheWindowManager->winGetWindowFromId(nullptr, parentID);

	pingImages[0] = TheMappedImageCollection->findImageByName("Ping03");
	pingImages[1] = TheMappedImageCollection->findImageByName("Ping02");
	pingImages[2] = TheMappedImageCollection->findImageByName("Ping01");
	DEBUG_ASSERTCRASH(pingImages[0], ("Can't find ping image!"));
	DEBUG_ASSERTCRASH(pingImages[1], ("Can't find ping image!"));
	DEBUG_ASSERTCRASH(pingImages[2], ("Can't find ping image!"));

//	parentGameListSmallID = NAMEKEY( "WOLCustomLobby.wnd:ParentGameListSmall" );
//	parentGameListSmall = TheWindowManager->winGetWindowFromId(nullptr, parentGameListSmallID);

	parentGameListLargeID = NAMEKEY( "WOLCustomLobby.wnd:ParentGameListLarge" );
	parentGameListLarge = TheWindowManager->winGetWindowFromId(nullptr, parentGameListLargeID);

	listboxLobbyGamesSmallID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGames" );
//	listboxLobbyGamesSmall = TheWindowManager->winGetWindowFromId(nullptr, listboxLobbyGamesSmallID);
//	listboxLobbyGamesSmall->winSetTooltipFunc(gameTooltip);

	listboxLobbyGamesLargeID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGamesLarge" );
	listboxLobbyGamesLarge = TheWindowManager->winGetWindowFromId(nullptr, listboxLobbyGamesLargeID);
	listboxLobbyGamesLarge->winSetTooltipFunc(gameTooltip);
	SetListBoxRowAnimMode(listboxLobbyGamesLarge, LIST_ROW_ANIM_ID);

	// GenOnline @feature arcticdolphin 22/11/2026 The game list draws its ping bars after the list itself.
	s_gameListDraw = listboxLobbyGamesLarge->winGetDrawFunc();
	listboxLobbyGamesLarge->winSetDrawFunc(drawGameListPing);
//
//	listboxLobbyGameInfoID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGameInfo" );
//	listboxLobbyGameInfo = TheWindowManager->winGetWindowFromId(nullptr, listboxLobbyGameInfoID);

	buttonSortAlphaID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortAlpha");
	buttonSortPingID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortPing");
	buttonSortBuddiesID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortBuddies");
	windowSortAlphaID = NAMEKEY("WOLCustomLobby.wnd:WindowSortAlpha");
	windowSortPingID = NAMEKEY("WOLCustomLobby.wnd:WindowSortPing");
	windowSortBuddiesID = NAMEKEY("WOLCustomLobby.wnd:WindowSortBuddies");

	buttonSortAlpha = TheWindowManager->winGetWindowFromId(parent, buttonSortAlphaID);
	if (buttonSortAlpha)
	{
		buttonSortAlpha->winSetText(UnicodeString(L"Sort by Newest"));
		buttonSortAlpha->winSetTooltip(UnicodeString::TheEmptyString);
	}
	buttonSortPing = TheWindowManager->winGetWindowFromId(parent, buttonSortPingID);
	if (buttonSortPing)
	{
		buttonSortPing->winSetText(UnicodeString(L"Sort by Map"));
		buttonSortPing->winSetTooltip(UnicodeString::TheEmptyString);
	}
	buttonSortBuddies = TheWindowManager->winGetWindowFromId(parent, buttonSortBuddiesID);
	if (buttonSortBuddies)
	{
		buttonSortBuddies->winSetText(UnicodeString(L"Sort by Friends"));
		buttonSortBuddies->winSetTooltip(UnicodeString::TheEmptyString);
	}

	windowSortAlpha = TheWindowManager->winGetWindowFromId(parent, windowSortAlphaID);
	windowSortPing = TheWindowManager->winGetWindowFromId(parent, windowSortPingID);
	windowSortBuddies = TheWindowManager->winGetWindowFromId(parent, windowSortBuddiesID);

	// GenOnline @feature arcticdolphin 08/11/2026 Look up the header cells and the buddies first check box, a retail or mod layout has the sort buttons instead.
	for (Int i = 0; i < GAME_LIST_HEADER_COUNT; ++i)
	{
		const GameListHeader &header = gameListHeaders[i];
		gameListHeaderIDs[i] = NAMEKEY(header.name);
		gameListHeaderWindows[i] = TheWindowManager->winGetWindowFromId(parent, gameListHeaderIDs[i]);
		if (gameListHeaderWindows[i] == nullptr || header.label == nullptr)
			continue;

		// GenOnline @tweak arcticdolphin 22/11/2026 Every header cell keeps its label as window text, drawGameListHeaderCell draws it.
		gameListHeaderWindows[i]->winSetText(TheGameText->FETCH_OR_SUBSTITUTE(header.label, header.text));
	}
	alignGameListHeaders(listboxLobbyGamesLarge);
	windowHeaderArrow = TheWindowManager->winGetWindowFromId(parent, NAMEKEY("WOLCustomLobby.wnd:WindowHeaderArrow"));

	GameWindow *searchEntry = TheWindowManager->winGetWindowFromId(parent, NAMEKEY("WOLCustomLobby.wnd:TextEntrySearch"));
	if (searchEntry != nullptr)
	{
		s_searchDraw = searchEntry->winGetDrawFunc();
		searchEntry->winSetText(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOSearchGamesHint", L"Search games..."));
		searchEntry->winSetDrawFunc(drawSearchWithHint);
	}

	showSortIcons();
}

void ReleaseWindowInfo()
{
	isSmall = TRUE;
	parent = nullptr;
//	parentGameListSmall = nullptr;
	parentGameListLarge = nullptr;
//	listboxLobbyGamesSmall = nullptr;
	listboxLobbyGamesLarge = nullptr;
//	listboxLobbyGameInfo = nullptr;

	s_gameListDraw = nullptr;

	buttonSortAlpha = nullptr;
	buttonSortPing = nullptr;
	buttonSortBuddies = nullptr;
	windowSortAlpha = nullptr;
	windowSortPing = nullptr;
	windowSortBuddies = nullptr;

	for (Int i = 0; i < GAME_LIST_HEADER_COUNT; ++i)
		gameListHeaderWindows[i] = nullptr;
	windowHeaderArrow = nullptr;
	windowHeaderCorner = nullptr;
	s_searchDraw = nullptr;
}

#if defined(GENONLINE)
typedef std::set<int64_t> BuddyGameSet;
#else
typedef std::set<GameSpyStagingRoom*> BuddyGameSet;
#endif

static BuddyGameSet *theBuddyGames = nullptr;

#if defined(GENONLINE)
static Bool lobbyHasBuddy(int64_t lobbyID)
{
	return theBuddyGames != nullptr && theBuddyGames->count(lobbyID) != 0;
}
#else
static Bool lobbyHasBuddy(GameSpyStagingRoom *room)
{
	return theBuddyGames->find(room) != theBuddyGames->end();
}
#endif

// Lobby IDs are 64-bit, but GadgetListBox item data is a 32-bit void* on this platform, so we
// can't stash the ID itself there. Instead we store the row index and keep the real ID here,
// rebuilt every time the game listbox is repopulated (see RefreshGameListBox/insertGame).
static std::vector<int64_t> s_lobbyRowIDs;

int64_t ResolveGameListLobbyID(Int rowItemData)
{
	if (rowItemData < 0 || (size_t)rowItemData >= s_lobbyRowIDs.size())
	{
		return -1;
	}
	return s_lobbyRowIDs[rowItemData];
}

#if defined(GENONLINE)
static void populateBuddyGames(std::vector<LobbyEntry>& vecLobbies)
#else
static void populateBuddyGames(void)
#endif
{
#if defined(GENONLINE)
	theBuddyGames = NEW BuddyGameSet;

	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface == nullptr)
	{
		return;
	}

	// Snapshot once, the map is rebuilt on the HTTP thread and every lobby must be tested against the same list.
	const auto mapFriends = pSocialInterface->GetCachedFriendsList();
	if (mapFriends.empty())
	{
		return;
	}

	for (const LobbyEntry& lobby : vecLobbies)
	{
		// is host our friend?
		if (mapFriends.contains(lobby.owner))
		{
			theBuddyGames->insert(lobby.lobbyID);
		}
		else // does the lobby contain any of our friends
		{
			for (const LobbyMemberEntry& member : lobby.members)
			{
				if (mapFriends.contains(member.user_id))
				{
					theBuddyGames->insert(lobby.lobbyID);
					break; // its binary, we don't care how many friends
				}
			}
		}
	}
#else
	BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
	theBuddyGames = NEW BuddyGameSet;
	if (!m)
	{
		return;
	}
	for (BuddyInfoMap::const_iterator bit = m->begin(); bit != m->end(); ++bit)
	{
		BuddyInfo info = bit->second;
		if (info.m_status == GP_STAGING)
		{
			StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
			for (StagingRoomMap::iterator srmIt = srm->begin(); srmIt != srm->end(); ++srmIt)
			{
				GameSpyStagingRoom *game = srmIt->second;
				game->cleanUpSlotPointers();
				const GameSpyGameSlot *slot = game->getGameSpySlot(0);
				if (slot && slot->getName() == info.m_locationString)
				{
					theBuddyGames->insert(game);
					break;
				}
			}
		}
	}
#endif
}

static void clearBuddyGames()
{
	delete theBuddyGames;
	theBuddyGames = nullptr;
}

#if defined(GENONLINE)
struct GameSortStruct
{
	bool operator()(const LobbyEntry& g1, const LobbyEntry& g2) const
	{
		const bool g1Join = !(g1.exe_crc != TheGlobalData->m_exeCRC || g1.ini_crc != TheGlobalData->m_iniCRC);
		const bool g2Join = !(g2.exe_crc != TheGlobalData->m_exeCRC || g2.ini_crc != TheGlobalData->m_iniCRC);

		if (g1Join != g2Join)
			return g1Join && !g2Join;

		if (sortBuddies)
		{
			const Bool g1Buddy = lobbyHasBuddy(g1.lobbyID);
			const Bool g2Buddy = lobbyHasBuddy(g2.lobbyID);

			if (g1Buddy != g2Buddy)
				return g1Buddy && !g2Buddy;
		}

		// Push passworded lobbies below open ones
		if (g1.passworded != g2.passworded)
			return !g1.passworded && g2.passworded;

        // NOTE: GO currently does not have private ladders, so this check is moot
		/*
		// sort games with private ladders to the bottom
		Bool g1UnknownLadder = (g1->getLadderPort() && TheLadderList->findLadder(g1->getLadderIP(), g1->getLadderPort()) == NULL);
		Bool g2UnknownLadder = (g2->getLadderPort() && TheLadderList->findLadder(g2->getLadderIP(), g2->getLadderPort()) == NULL);
		if (g1UnknownLadder ^ g2UnknownLadder)
		{
			return g2UnknownLadder;
		}
		*/

		switch (theGameSortType)
		{
		case GAMESORT_MAP_ASCENDING: // was ping
			if (g1.map_name != g2.map_name)
				return g1.map_name < g2.map_name;
			break;

		case GAMESORT_MAP_DESCENDING: // was ping
			if (g1.map_name != g2.map_name)
				return g1.map_name > g2.map_name;
			break;

		case GAMESORT_AGE_ASCENDING:  // was alpha
			return g1.lobbyID > g2.lobbyID;
		case GAMESORT_AGE_DESCENDING: // was alpha
			return g1.lobbyID < g2.lobbyID;

		// GenOnline @feature arcticdolphin 08/11/2026 Player count and ping sorts of the column headers, ties fall through to newest first.
		case GAMESORT_PLAYERS_ASCENDING:
			if (g1.current_players != g2.current_players)
				return g1.current_players < g2.current_players;
			break;
		case GAMESORT_PLAYERS_DESCENDING:
			if (g1.current_players != g2.current_players)
				return g1.current_players > g2.current_players;
			break;
		case GAMESORT_PING_ASCENDING:
			if (g1.latency != g2.latency)
				return g1.latency < g2.latency;
			break;
		case GAMESORT_PING_DESCENDING:
			if (g1.latency != g2.latency)
				return g1.latency > g2.latency;
			break;
		}
		return g1.lobbyID > g2.lobbyID;
	}
};
#else
struct GameSortStruct
{
	bool operator()(GameSpyStagingRoom *g1, GameSpyStagingRoom *g2) const
	{
		// sort CRC mismatches to the bottom
		Bool g1Good = (g1->getExeCRC() == TheGlobalData->m_exeCRC && g1->getIniCRC() == TheGlobalData->m_iniCRC);
		Bool g2Good = (g2->getExeCRC() == TheGlobalData->m_exeCRC && g2->getIniCRC() == TheGlobalData->m_iniCRC);
		if ( g1Good ^ g2Good )
		{
			return g1Good;
		}

		// sort games with private ladders to the bottom
		Bool g1UnknownLadder = (g1->getLadderPort() && TheLadderList->findLadder(g1->getLadderIP(), g1->getLadderPort()) == nullptr);
		Bool g2UnknownLadder = (g2->getLadderPort() && TheLadderList->findLadder(g2->getLadderIP(), g2->getLadderPort()) == nullptr);
		if ( g1UnknownLadder ^ g2UnknownLadder )
		{
			return g2UnknownLadder;
		}

		// sort full games to the bottom
		Bool g1Full = (g1->getNumNonObserverPlayers() == g1->getMaxPlayers() || g1->getNumPlayers() == MAX_SLOTS);
		Bool g2Full = (g2->getNumNonObserverPlayers() == g2->getMaxPlayers() || g2->getNumPlayers() == MAX_SLOTS);
		if ( g1Full ^ g2Full )
		{
			return g2Full;
		}

		if (sortBuddies)
		{
			Bool g1HasBuddies = lobbyHasBuddy(g1);
			Bool g2HasBuddies = lobbyHasBuddy(g2);
			if ( g1HasBuddies ^ g2HasBuddies )
			{
				return g1HasBuddies;
			}
		}

		switch(theGameSortType)
		{
		case GAMESORT_ALPHA_ASCENDING:
			return wcsicmp(g1->getGameName().str(), g2->getGameName().str()) < 0;
			break;
		case GAMESORT_ALPHA_DESCENDING:
			return wcsicmp(g1->getGameName().str(),g2->getGameName().str()) > 0;
			break;
		case GAMESORT_PING_ASCENDING:
			return g1->getPingAsInt() < g2->getPingAsInt();
			break;
		case GAMESORT_PING_DESCENDING:
			return g1->getPingAsInt() > g2->getPingAsInt();
			break;
		}
		return false;
	}
};
#endif

static Int insertGame(GameWindow* win, LobbyEntry& lobbyInfo, Bool showMap)
{
	Color gameColor = GameSpyColor[GSCOLOR_GAME];
	if (lobbyInfo.exe_crc != TheGlobalData->m_exeCRC || lobbyInfo.ini_crc != TheGlobalData->m_iniCRC)
	{
		gameColor = GameSpyColor[GSCOLOR_GAME_CRCMISMATCH];
	}
#if defined(GENONLINE)
	// Buddy lobby highlight:
	if (lobbyHasBuddy(lobbyInfo.lobbyID))
	{
		const bool nonJoinable =
				(gameColor == GameSpyColor[GSCOLOR_GAME_CRCMISMATCH]);

		// GenOnline @feature arcticdolphin 09/11/2026 Lobby row colours come from the GO UI colour table, these colours without it.
		gameColor = nonJoinable
			? GOUIColor(GOUICOLOR_LOBBY_NOT_JOINABLE, GameMakeColor(0, 98, 130, 255))   // darker cyan
			: GOUIColor(GOUICOLOR_LOBBY_JOINABLE, GameMakeColor(20, 177, 255, 255)); // lighter cyan
	}
#endif
	std::wstring strOwnerName = L"";
	for (LobbyMemberEntry& member : lobbyInfo.members)
	{
		if (member.user_id == lobbyInfo.owner)
		{
			strOwnerName = from_utf8(member.display_name);
		}
	}

	UnicodeString gameName;
	gameName.format(L"%s (%s)", from_utf8(lobbyInfo.name).c_str(), strOwnerName.c_str());

	int numPlayers = lobbyInfo.current_players;
	int maxPlayers = lobbyInfo.max_players;

	AsciiString ladder = AsciiString("TODO_GENONLINE");
	USHORT ladderPort = 1;

	bool bHasPassword = lobbyInfo.passworded;

	bool bAllowSpectators = lobbyInfo.allow_observers;
	bool bTrackStats = lobbyInfo.track_stats;

	int latency = lobbyInfo.latency;

	// TODO_GENONLINE: Asian text
	/*
	if (TheGameSpyInfo->getDisallowAsianText())
	{
		const WideChar* buff = gameName.str();
		Int length = gameName.getLength();
		for (Int i = 0; i < length; ++i)
		{
			if (buff[i] >= 256)
				return -1;
		}
	}
	else if (TheGameSpyInfo->getDisallowNonAsianText())
	{
		const WideChar* buff = gameName.str();
		Int length = gameName.getLength();
		Bool hasUnicode = FALSE;
		for (Int i = 0; i < length; ++i)
		{
			if (buff[i] >= 256)
			{
				hasUnicode = TRUE;
				break;
			}
		}
		if (!hasUnicode)
			return -1;
	}
	*/


	Int rowCount = GadgetListBoxGetNumEntries(win);
	bool bAlternate = (rowCount % 2 == 0);
	if (bAlternate && gameColor == GameSpyColor[GSCOLOR_GAME])
	{
		gameColor = GOUIColor(GOUICOLOR_LOBBY_ALT_ROW, GameMakeColor(191, 198, 201, 255));
	}
	Int index = GadgetListBoxAddEntryText(win, gameName, gameColor, -1, COLUMN_NAME);
	if ((size_t)index >= s_lobbyRowIDs.size())
	{
		s_lobbyRowIDs.resize(index + 1);
	}
	s_lobbyRowIDs[index] = lobbyInfo.lobbyID;
	GadgetListBoxSetItemData(win, (void*)(intptr_t)index, index);

	UnicodeString s;

	if (showMap)
	{
		GadgetListBoxAddEntryText(win, lobbyMapDisplayName(lobbyInfo), gameColor, index, COLUMN_MAP);

		// TODO_GENONLINE: Support ladder again
		if (TheLadderList != nullptr)
		{
			const LadderInfo* li = TheLadderList->findLadder(ladder, ladderPort);
			if (li)
			{
				GadgetListBoxAddEntryText(win, li->name, gameColor, index, COLUMN_LADDER);
			}
			else if (ladderPort)
			{
				GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:UnknownLadder"), gameColor, index, COLUMN_LADDER);
			}
			else
			{
				GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:NoLadder"), gameColor, index, COLUMN_LADDER);
			}
		}
		else
		{
			GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:NoLadder"), gameColor, index, COLUMN_LADDER);
		}
	}
	else
	{
		GadgetListBoxAddEntryText(win, UnicodeString(L" "), gameColor, index, COLUMN_MAP);
		GadgetListBoxAddEntryText(win, UnicodeString(L" "), gameColor, index, COLUMN_LADDER);
	}

	s.format(L"%d/%d", numPlayers, maxPlayers);
	const bool bIsFull = (lobbyInfo.current_players == lobbyInfo.max_players || lobbyInfo.current_players == MAX_SLOTS);
	const bool bIsAlmostFull = !bIsFull && (maxPlayers > 0) && ((float)numPlayers / (float)maxPlayers >= 0.6f);
	Color numPlayersColor = bIsFull ? GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 80, 80, 255))
		: bIsAlmostFull ? GOUIColor(GOUICOLOR_LOBBY_ALMOST_FULL, GameMakeColor(16, 173, 144, 255))
		: gameColor;
	GadgetListBoxAddEntryText(win, s, numPlayersColor, index, COLUMN_NUMPLAYERS);

	if (bHasPassword)
	{
		const Image* img = TheMappedImageCollection->findImageByName("Password");
		if (img)
			addListIcon(win, img, index, COLUMN_PASSWORD);
	}
	else
	{
		GadgetListBoxAddEntryText(win, UnicodeString(L" "), gameColor, index, COLUMN_PASSWORD);
	}

	if (bAllowSpectators)
	{
		const Image* img = TheMappedImageCollection->findImageByName("Observer");
		if (img)
		{
			addListIcon(win, img, index, COLUMN_OBSERVER);
		}
	}
	else
	{
		GadgetListBoxAddEntryText(win, UnicodeString(L" "), gameColor, index, COLUMN_OBSERVER);
	}

	if (bTrackStats)
	{
		// GenOnline @bugfix arcticdolphin 21/11/2026 The Generals retail images have no GoodStatsIcon and a missing image crashed the lobby list.
		const Image* img = TheMappedImageCollection->findImageByName("GoodStatsIcon");
		if (img)
		{
			addListIcon(win, img, index, COLUMN_USE_STATS);
		}
	}

	// GenOnline @feature arcticdolphin 22/11/2026 Only an Easy Anti-Cheat lobby shows a shield, dimmed when this client does not run it.
	const EAnticheatKind lobbyAnticheat = ParseAnticheatKind(lobbyInfo.anticheat);
	const Image* shield = lobbyAnticheat == EAnticheatKind::Eac ? TheMappedImageCollection->findImageByName(AnticheatKindImageName(lobbyAnticheat, LocalAnticheatKind() != EAnticheatKind::Eac)) : nullptr;
	if (shield)
	{
		addListIcon(win, shield, index, COLUMN_ANTICHEAT);
	}

	// GenOnline @feature arcticdolphin 22/11/2026 The ping cell stays blank, drawGameListPing draws the bars and the latency.
	GadgetListBoxAddEntryText(win, UnicodeString(L" "), gameColor, index, COLUMN_PING);
	if ((size_t)index >= s_lobbyRowPings.size())
	{
		s_lobbyRowPings.resize(index + 1, -1);
	}
	s_lobbyRowPings[index] = latency;

	return index;

	/*
	game->cleanUpSlotPointers();
	Color gameColor = GameSpyColor[GSCOLOR_GAME];
	if (game->getNumNonObserverPlayers() == game->getMaxPlayers() || game->getNumPlayers() == MAX_SLOTS)
	{
		gameColor = GameSpyColor[GSCOLOR_GAME_FULL];
	}
	if (game->getExeCRC() != TheGlobalData->m_exeCRC || game->getIniCRC() != TheGlobalData->m_iniCRC)
	{
		gameColor = GameSpyColor[GSCOLOR_GAME_CRCMISMATCH];
	}
	UnicodeString gameName = game->getGameName();

	if(TheGameSpyInfo->getDisallowAsianText())
	{
		const WideChar *buff = gameName.str();
		Int length =  gameName.getLength();
		for(Int i = 0; i < length; ++i)
		{
			if(buff[i] >= 256)
				return -1;
		}
	}
	else if(TheGameSpyInfo->getDisallowNonAsianText())
	{
		const WideChar *buff = gameName.str();
		Int length =  gameName.getLength();
		Bool hasUnicode = FALSE;
		for(Int i = 0; i < length; ++i)
		{
			if(buff[i] >= 256)
			{
				hasUnicode = TRUE;
				break;
			}
		}
		if(!hasUnicode)
			return -1;
	}



	Int index = GadgetListBoxAddEntryText(win, game->getGameName(), gameColor, -1, COLUMN_NAME);
	GadgetListBoxSetItemData(win, (void *)game->getID(), index);

	UnicodeString s;

	if (showMap)
	{
		UnicodeString mapName;
		const MapMetaData *md = TheMapCache->findMap(game->getMap());
		if (md)
		{
			mapName = md->m_displayName;
		}
		else
		{
			mapName.translate( getFileName(game->getMap().str()) );
		}
		GadgetListBoxAddEntryText(win, mapName, gameColor, index, COLUMN_MAP);

		const LadderInfo * li = TheLadderList->findLadder(game->getLadderIP(), game->getLadderPort());
		if (li)
		{
			GadgetListBoxAddEntryText(win, li->name, gameColor, index, COLUMN_LADDER);
		}
		else if (game->getLadderPort())
		{
			GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:UnknownLadder"), gameColor, index, COLUMN_LADDER);
		}
		else
		{
			GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:NoLadder"), gameColor, index, COLUMN_LADDER);
		}
	}
	else
	{
		GadgetListBoxAddEntryText(win, L" ", gameColor, index, COLUMN_MAP);
		GadgetListBoxAddEntryText(win, L" ", gameColor, index, COLUMN_LADDER);
	}

	s.format(L"%d/%d", game->getReportedNumPlayers(), game->getReportedMaxPlayers());
	GadgetListBoxAddEntryText(win, s, gameColor, index, COLUMN_NUMPLAYERS);

	if (game->getHasPassword())
	{
		const Image *img = TheMappedImageCollection->findImageByName("Password");
		Int width = 10, height = 10;
		if (img)
		{
			width = img->getImageWidth();
			height = img->getImageHeight();
		}
		GadgetListBoxAddEntryImage(win, img, index, COLUMN_PASSWORD, width, height);
	}
	else
	{
		GadgetListBoxAddEntryText(win, L" ", gameColor, index, COLUMN_PASSWORD);
	}

	if (game->getAllowObservers())
	{
		const Image *img = TheMappedImageCollection->findImageByName("Observer");
		GadgetListBoxAddEntryImage(win, img, index, COLUMN_OBSERVER);
	}
	else
	{
		GadgetListBoxAddEntryText(win, L" ", gameColor, index, COLUMN_OBSERVER);
	}

#if !RTS_GENERALS
  {
    if (game->getUseStats())
    {
      if (const Image *img = TheMappedImageCollection->findImageByName("GoodStatsIcon"))
      {
        GadgetListBoxAddEntryImage(win, img, index, COLUMN_USE_STATS, img->getImageHeight(), img->getImageWidth());
      }
    }
  }
#endif

	s.format(L"%d", game->getPingAsInt());
	GadgetListBoxAddEntryText(win, s, gameColor, index, COLUMN_PING);
	Int ping = game->getPingAsInt();
	Int width = 10, height = 10;
	if (pingImages[0])
	{
		width = pingImages[0]->getImageWidth();
		height = pingImages[0]->getImageHeight();
	}
	// CLH picking an arbitrary number for our ping display
	if (ping < TheGameSpyConfig->getPingCutoffGood())
	{
		GadgetListBoxAddEntryImage(win, pingImages[0], index, COLUMN_PING, width, height);
	}
	else if (ping < TheGameSpyConfig->getPingCutoffBad())
	{
		GadgetListBoxAddEntryImage(win, pingImages[1], index, COLUMN_PING, width, height);
	}
	else
	{
		GadgetListBoxAddEntryImage(win, pingImages[2], index, COLUMN_PING, width, height);
	}

	return index;
	*/

}

void RefreshGameListBox(GameWindow* win, Bool showMap)
{
	if (!win)
		return;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	// save off selection
	Int selectedIndex = -1;
	int64_t selectedID = -1;
	GadgetListBoxGetSelected(win, &selectedIndex);
	if (selectedIndex != -1)
	{
		selectedID = ResolveGameListLobbyID((Int)(intptr_t)GadgetListBoxGetItemData(win, selectedIndex));
	}
	int prevPos = GadgetListBoxGetTopVisibleEntry(win);

	pLobbyInterface->SearchForLobbies(
		[=]()
		{
			win->winEnable(true);
			if (GadgetListBoxGetNumEntries(win) == 0)
			{
				GadgetListBoxAddEntryText(win, UnicodeString(L"Searching for public lobbies..."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
				GadgetListBoxSetSelected(win, -1);
			}
		},
		[=](std::vector<LobbyEntry> vecLobbies)
		{
			// GenOnline @feature arcticdolphin 22/11/2026 The details panel follows the selection every way this refresh ends.
			struct DetailsUpdateOnExit
			{
				~DetailsUpdateOnExit() { GenOnlineLobbyGameDetailsUpdate(); }
			} detailsUpdateOnExit;

			// empty listbox
			GadgetListBoxReset(win);
			s_lobbyRowIDs.clear();
			s_lobbyRowPings.clear();

			size_t numResults = vecLobbies.size();

			if (numResults == 0)
			{
				GadgetListBoxAddEntryText(win, UnicodeString(L"No lobbies were found"), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
				GadgetListBoxSetSelected(win, -1);

			}
			else
			{
				win->winEnable(true);

				populateBuddyGames(vecLobbies);

				// filter lobbies by game mode
				if (theLobbyFilter != LOBBY_FILTER_ALL)
				{
					const EAnticheatKind localAnticheat = LocalAnticheatKind();
					std::vector<LobbyEntry> filtered;
					filtered.reserve(vecLobbies.size());
					for (Int i = 0; i < (Int)vecLobbies.size(); ++i)
					{
						const LobbyEntry &entry = vecLobbies[i];
						Bool matchesFilter = FALSE;
						if (theLobbyFilter == LOBBY_FILTER_BUDDIES)
							matchesFilter = lobbyHasBuddy(entry.lobbyID);
						else if (theLobbyFilter == LOBBY_FILTER_JOINABLE)
							matchesFilter = entry.exe_crc == TheGlobalData->m_exeCRC && entry.ini_crc == TheGlobalData->m_iniCRC && (ParseAnticheatKind(entry.anticheat) != EAnticheatKind::Eac || localAnticheat == EAnticheatKind::Eac);
						else
							matchesFilter = detectGameMode(entry.name) == theLobbyFilter;

						if (matchesFilter)
							filtered.push_back(vecLobbies[i]);
					}
					vecLobbies = filtered;
					if (vecLobbies.empty())
					{
						GadgetListBoxAddEntryText(win, UnicodeString(L"No lobbies currently match this filter"), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
						GadgetListBoxSetSelected(win, -1);
						clearBuddyGames();
						return;
					}
				}

				if (!theLobbySearchText.isEmpty())
				{
					const std::wstring lowerSearch = toLowerWide(theLobbySearchText.str());
					std::vector<LobbyEntry> matching;
					matching.reserve(vecLobbies.size());
					for (const LobbyEntry& lobby : vecLobbies)
					{
						if (lobbyMatchesSearch(lobby, lowerSearch))
							matching.push_back(lobby);
					}
					vecLobbies = matching;
					if (vecLobbies.empty())
					{
						GadgetListBoxAddEntryText(win, UnicodeString(L"No lobbies match your search"), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
						GadgetListBoxSetSelected(win, -1);
						clearBuddyGames();
						return;
					}
				}

				// sort our games
				typedef std::multiset<LobbyEntry, GameSortStruct> SortedGameList;
				SortedGameList sgl;
				for (LobbyEntry& lobby : vecLobbies)
				{
					sgl.insert(lobby);
				}

				// now add the games
				Int indexToSelect = -1;

				int i = 0;
				//for (LobbyEntry lobby : vecLobbies)
				for (SortedGameList::iterator sglIt = sgl.begin(); sglIt != sgl.end(); ++sglIt)
				{
					LobbyEntry lobby = *sglIt;
					Int index = insertGame(win, lobby, showMap);
					if (lobby.lobbyID == selectedID)
					{
						indexToSelect = (int)index; // TODO_GENONLINE: downcast
					}

					++i;
				}

				clearBuddyGames();

				// restore selection
				GadgetListBoxSetSelected(win, indexToSelect); // even for -1, so we can disable the 'Join Game' button
				//	if(prevPos > 10)
				GadgetListBoxSetTopVisibleEntry(win, prevPos);//+ 1

				if (indexToSelect < 0 && selectedID >= 0)
				{
					TheWindowManager->winSetLoneWindow(NULL);
				}
			}
		});

	// Note: clearBuddyGames(), GadgetListBoxSetSelected(), and GadgetListBoxSetTopVisibleEntry()
	// are now handled inside the SearchForLobbies callback above (after async operation completes).
	// The code below was removed to fix a race condition where these operations executed on the
	// empty/reset listbox before the async callback populated it, causing crashes.
}

void RefreshGameInfoListBox( GameWindow *mainWin, GameWindow *win )
{
//	if (!mainWin || !win)
//		return;
//
//	GadgetListBoxReset(win);
//
//	Int selected = -1;
//	GadgetListBoxGetSelected(mainWin, &selected);
//	if (selected < 0)
//	{
//		return;
//	}
//
//	Int selectedID = (Int)GadgetListBoxGetItemData(mainWin, selected);
//	if (selectedID < 0)
//	{
//		return;
//	}
//
//	StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
//	StagingRoomMap::iterator srmIt = srm->find(selectedID);
//	if (srmIt != srm->end())
//	{
//		GameSpyStagingRoom *theRoom = srmIt->second;
//		theRoom->cleanUpSlotPointers();
//
//		// game name
////		GadgetListBoxAddEntryText(listboxLobbyGameInfo, theRoom->getGameName(), GameSpyColor[GSCOLOR_DEFAULT], -1);
//
//		const LadderInfo * li = TheLadderList->findLadder(theRoom->getLadderIP(), theRoom->getLadderPort());
//		if (li)
//		{
//			UnicodeString tmp;
//			tmp.format(TheGameText->fetch("TOOLTIP:LadderName"), li->name.str());
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, tmp, GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//		else if (theRoom->getLadderPort())
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:UnknownLadder"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//		else
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:NoLadder"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//
//		if (theRoom->getExeCRC() != TheGlobalData->m_exeCRC || theRoom->getIniCRC() != TheGlobalData->m_iniCRC)
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:InvalidGameVersionSingleLine"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//
//		// map name
//		UnicodeString mapName;
//		const MapMetaData *md = TheMapCache->findMap(theRoom->getMap());
//		if (md)
//		{
//			mapName = md->m_displayName;
//		}
//		else
//		{
//			const char *start = theRoom->getMap().reverseFind('\\');
//			if (start)
//			{
//				++start;
//			}
//			else
//			{
//				start = theRoom->getMap().str();
//			}
//			mapName.translate( start );
//		}
//
//		GadgetListBoxAddEntryText(listboxLobbyGameInfo, mapName, GameSpyColor[GSCOLOR_DEFAULT], -1);
//
//		// player list (rank, win/loss, side)
//		for (Int i=0; i<MAX_SLOTS; ++i)
//		{
//			const GameSpyGameSlot *slot = theRoom->getGameSpySlot(i);
//			if (slot && slot->isHuman())
//			{
//				UnicodeString theName, theRating, thePlayerTemplate;
//				Int colorIdx = slot->getColor();
//				theName = slot->getName();
//				theRating.format(L" (%d-%d)", slot->getWins(), slot->getLosses());
//				const PlayerTemplate * pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
//				if (pt)
//				{
//					thePlayerTemplate = pt->getDisplayName();
//				}
//				else
//				{
//					thePlayerTemplate = TheGameText->fetch("GUI:Random");
//				}
//
//				UnicodeString theText;
//				theText.format(L"%ls - %ls - %ls", theName.str(), thePlayerTemplate.str(), theRating.str());
//
//				Int theColor = GameSpyColor[GSCOLOR_DEFAULT];
//				const MultiplayerColorDefinition *mcd = TheMultiplayerSettings->getColor(colorIdx);
//				if (mcd)
//				{
//					theColor = mcd->getColor();
//				}
//
//				GadgetListBoxAddEntryText(listboxLobbyGameInfo, theText, theColor, -1);
//			}
//		}
//	}

}

void RefreshGameListBoxes( void )
{
	GameWindow *main = GetGameListBox();
	GameWindow *info = GetGameInfoListBox();

	RefreshGameListBox( main, (info == nullptr) );

	if (info)
	{
		RefreshGameInfoListBox( main, info );
	}
}

void ToggleGameListType()
{
	isSmall = !isSmall;
	if(isSmall)
	{
		parentGameListLarge->winHide(TRUE);
//		parentGameListSmall->winHide(FALSE);
	}
	else
	{
		parentGameListLarge->winHide(FALSE);
//		parentGameListSmall->winHide(TRUE);
	}

	RefreshGameListBoxes();
}

// for use by GameWindow::winSetTooltipFunc
// displays the Army Tooltip for the player templates
void playerTemplateComboBoxTooltip(GameWindow *wndComboBox, WinInstanceData *instData, UnsignedInt mouse)
{
	Int index = 0;
	GadgetComboBoxGetSelectedPos(wndComboBox, &index);
	Int templateNum = (Int)(intptr_t)GadgetComboBoxGetItemData(wndComboBox, index);
	UnicodeString ustringTooltip;
	if (templateNum == -1)
	{
			// the "Random" template is always first
			ustringTooltip = TheGameText->fetch("TOOLTIP:BioStrategyLong_Random");
	}
	else
	{
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		if (playerTemplate)
		{
			ustringTooltip = TheGameText->fetch(playerTemplate->getTooltip());
		}
	}
	TheMouse->setCursorTooltip(ustringTooltip);
}

// -----------------------------------------------------------------------------

// for use by GameWindow::winSetTooltipFunc
// displays the Army Tooltip for the player templates
void playerTemplateListBoxTooltip(GameWindow *wndListBox, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);
	GadgetListBoxGetEntryBasedOnXY(wndListBox, x, y, row, col);
	if (row == -1 || col == -1)
		return;

	Int templateNum = (Int)(intptr_t)GadgetListBoxGetItemData(wndListBox, row, col);
	UnicodeString ustringTooltip;
	if (templateNum == -1)
	{
			// the "Random" template is always first
			ustringTooltip = TheGameText->fetch("TOOLTIP:BioStrategyLong_Random");
	}
	else
	{
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		if (playerTemplate)
		{
			ustringTooltip = TheGameText->fetch(playerTemplate->getTooltip());
		}
	}

	// use no tooltip delay here
	TheMouse->setCursorTooltip(ustringTooltip, 0);
}
