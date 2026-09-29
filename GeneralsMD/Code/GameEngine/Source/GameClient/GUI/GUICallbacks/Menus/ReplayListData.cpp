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

// FILE: ReplayListData.cpp //////////////////////////////////////////////////
// See ReplayListData.h. Bodies moved out of ReplayMenu.cpp's PopulateReplayFileListbox; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ReplayListData.h"

#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameInfo.h"

namespace ReplayList
{

Bool readMapInfo( const AsciiString &filename, RecorderClass::ReplayHeader &header, ReplayGameInfo &info, const MapMetaData *&mapData )
{
	header.forPlayback = FALSE;
	header.filename = filename;

	if (TheRecorder != nullptr && TheRecorder->readReplayHeader(header))
	{
		if (ParseAsciiStringToGameInfo(&info, header.gameOptions))
		{
			if (TheMapCache != nullptr)
				mapData = TheMapCache->findMap(info.getMap());
			else
				mapData = nullptr;

			return true;
		}
	}
	return false;
}

static void removeReplayExtension(UnicodeString& replayName)
{
	const Int extensionLength = TheRecorder->getReplayExtention().getLength();
	replayName.truncateBy(extensionLength);
}

static UnicodeString createReplayName(const AsciiString& filename)
{
	AsciiString lastReplayFName = TheRecorder->getLastReplayFileName();
	lastReplayFName.concat(TheRecorder->getReplayExtention());
	UnicodeString replayName;

	if (lastReplayFName.compareNoCase(filename) == 0)
	{
		replayName = TheGameText->fetch("GUI:LastReplay");
	}
	else
	{
		replayName.translate(filename);
		removeReplayExtension(replayName);
	}
	return replayName;
}

static UnicodeString createMapName(const AsciiString& filename, const ReplayGameInfo& info, const MapMetaData *mapData)
{
	UnicodeString mapName;
	if (!mapData)
	{
		// TheSuperHackers @bugfix helmutbuhler 08/03/2025 Just use the filename.
		// Displaying a long map path string would break the map list gui.
		const char* filename = info.getMap().reverseFind('\\');
		mapName.translate(filename ? filename + 1 : info.getMap());
	}
	else
	{
		mapName = mapData->m_displayName;
	}
	return mapName;
}

// TheSuperHackers @feature Stubbjax 21/10/2025 Show extra info tooltip when hovering over a replay.
static UnicodeString buildReplayTooltip(RecorderClass::ReplayHeader header, ReplayGameInfo info)
{
	UnicodeString tooltipStr;

	if (header.endTime < header.startTime)
		header.startTime = header.endTime;

	time_t totalSeconds = header.endTime - header.startTime;
	UnsignedInt hours = totalSeconds / 3600;
	UnsignedInt mins = (totalSeconds % 3600) / 60;
	UnsignedInt secs = totalSeconds % 60;
	Real fps = totalSeconds > 0 ? header.frameCount / totalSeconds : 0;
	tooltipStr.format(L"%02u:%02u:%02u (%g fps)", hours, mins, secs, fps);

	if (header.localPlayerIndex >= 0)
	{
		// MP game
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot* slot = info.getConstSlot(i);
			if (slot && slot->isHuman())
			{
				tooltipStr.concat(L"\n");
				tooltipStr.concat(info.getConstSlot(i)->getName());
			}
		}
	}

	return tooltipStr;
}

Bool scan( std::vector<ReplayRow> &rows )
{
	if (!TheMapCache)
		return FALSE;

	rows.clear();

	// TheSuperHackers @tweak xezon 08/06/2025 Now shows missing maps in red color.
	enum {
		COLOR_SP = 0,
		COLOR_SP_CRC_MISMATCH,
		COLOR_MP,
		COLOR_MP_CRC_MISMATCH,
		COLOR_MISSING_MAP,
		COLOR_MISSING_MAP_CRC_MISMATCH,
		COLOR_MAX
	};
	const UnsignedInt colors[] = {
		0xFFFFFF,
		0x808080,
		0xFFFFFF,
		0x808080,
		0xF31818,
		0x802020
	};
	static_assert(ARRAY_SIZE(colors) == COLOR_MAX, "Mismatch between colors array size and COLOR_MAX");

	AsciiString asciistr;
	AsciiString asciisearch;
	asciisearch = "*";
	asciisearch.concat(TheRecorder->getReplayExtention());

	FilenameList replayFilenames;
	FilenameListIter it;

	TheFileSystem->getFileListInDirectory(TheRecorder->getReplayDir(), asciisearch, replayFilenames, FALSE);

	TheMapCache->updateCache();

	for (it = replayFilenames.begin(); it != replayFilenames.end(); ++it)
	{
		// just want the filename
		asciistr.set((*it).reverseFind('\\') + 1);

		RecorderClass::ReplayHeader header;
		ReplayGameInfo info;
		const MapMetaData *mapData;

		if (readMapInfo(asciistr, header, info, mapData))
		{
			ReplayRow row;
			row.m_fileName = asciistr;
			row.m_name = createReplayName(asciistr);

			// TheSuperHackers @tweak Caball009 07/02/2026 Display both time and date instead of only time.
			row.m_time = getUnicodeTimeBuffer(header.timeVal);
			row.m_date = getUnicodeDateBuffer(header.timeVal);
			row.m_version = header.versionString;
			row.m_map = createMapName(asciistr, info, mapData);
			row.m_tooltip = buildReplayTooltip(header, info);

			const Bool hasMap = mapData != nullptr;
			const Bool isCrcCompatible = RecorderClass::replayMatchesGameVersion(header);
			const Bool isMultiplayer = header.localPlayerIndex >= 0;

			if (isCrcCompatible)
			{
				row.m_color = isMultiplayer ? colors[COLOR_MP] : colors[COLOR_SP];
				row.m_mapColor = hasMap ? row.m_color : colors[COLOR_MISSING_MAP];
			}
			else
			{
				row.m_color = isMultiplayer ? colors[COLOR_MP_CRC_MISMATCH] : colors[COLOR_SP_CRC_MISMATCH];
				row.m_mapColor = hasMap ? row.m_color : colors[COLOR_MISSING_MAP_CRC_MISMATCH];
			}

			rows.push_back(row);

			// TheSuperHackers @performance Now stops processing when the list is full.
			if ((Int)rows.size() == MAX_ROWS)
				break;
		}
	}

	return TRUE;
}

}
