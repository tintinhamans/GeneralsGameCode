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

// FILE: LanLobbyData.cpp ///////////////////////////////////////////////////////
// See LanLobbyData.h. buildPlayerRows()/buildGameRows() mirror LANAPI::OnPlayerList()/
// LANDisplayGameList()'s traversal; buildGameDetails() mirrors GameInfoWindow.cpp's
// RefreshGameInfoWindow() slot-by-slot logic exactly, just writing into a struct
// instead of a GadgetListBox.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/LanLobbyData.h"

#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"

std::vector<LanLobbyPlayerRow> LanLobbyData::buildPlayerRows( LANPlayer *playerList )
{
	std::vector<LanLobbyPlayerRow> rows;
	for ( LANPlayer *player = playerList; player != nullptr; player = player->getNext() )
	{
		LanLobbyPlayerRow row;
		row.m_name = player->getName();
		row.m_ip = player->getIP();
		rows.push_back(row);
	}
	return rows;
}

std::vector<LanLobbyGameRow> LanLobbyData::buildGameRows( LANGameInfo *gameList )
{
	std::vector<LanLobbyGameRow> rows;
	for ( LANGameInfo *game = gameList; game != nullptr; game = game->getNext() )
	{
		LanLobbyGameRow row;
		row.m_game = game;
		row.m_inProgress = game->isGameInProgress();

		UnicodeString name;
		if ( row.m_inProgress )
			name.concat(L"[");
		name.concat(game->getPlayerName(0));
		if ( row.m_inProgress )
			name.concat(L"]");
		row.m_displayName = name;

		rows.push_back(row);
	}
	return rows;
}

LanLobbyGameDetails LanLobbyData::buildGameDetails( LANGameInfo *game )
{
	static const Image *randomIcon = TheMappedImageCollection->findImageByName("GameinfoRANDOM");
	static const Image *observerIcon = TheMappedImageCollection->findImageByName("GameinfoOBSRVR");

	LanLobbyGameDetails details;
	if ( !game )
		return details;

	details.m_valid = TRUE;
	details.m_gameName = game->getPlayerName(0);

	AsciiString asciiMap = game->getMap();
	asciiMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
	if ( it != TheMapCache->end() )
	{
		details.m_mapDisplayName = it->second.m_displayName;
	}
	else
	{
		// Map will have to be transferred -- use the leaf name (see RefreshGameInfoWindow()).
		const char *noPath = game->getMap().reverseFind('\\');
		if ( noPath )
			++noPath;
		else
			noPath = game->getMap().str();
		details.m_mapDisplayName.translate(noPath);
	}

	for ( Int i = 0; i < MAX_SLOTS; ++i )
	{
		LanLobbyGameDetailSlot &slot = details.m_slots[i];
		GameSlot *gameSlot = game->getSlot(i);
		if ( !gameSlot || !gameSlot->isOccupied() )
			continue;

		slot.m_occupied = TRUE;
		slot.m_colorIndex = gameSlot->getColor();

		if ( gameSlot->isAI() )
		{
			slot.m_isHuman = FALSE;
			switch ( gameSlot->getState() )
			{
				case SLOT_EASY_AI:
					slot.m_label = TheGameText->fetch("GUI:EasyAI");
					break;
				case SLOT_MED_AI:
					slot.m_label = TheGameText->fetch("GUI:MediumAI");
					break;
				case SLOT_BRUTAL_AI:
					slot.m_label = TheGameText->fetch("GUI:HardAI");
					break;
				default:
					break;
			}
		}
		else if ( gameSlot->isHuman() )
		{
			slot.m_isHuman = TRUE;
			slot.m_label = gameSlot->getName();
		}

		Int playerTemplate = gameSlot->getPlayerTemplate();
		if ( playerTemplate == PLAYERTEMPLATE_OBSERVER )
		{
			slot.m_isObserver = TRUE;
			if ( observerIcon )
				slot.m_sideIconImage = observerIcon->getName();
		}
		else if ( playerTemplate < 0 || playerTemplate >= ThePlayerTemplateStore->getPlayerTemplateCount() )
		{
			slot.m_isRandomFaction = TRUE;
			if ( randomIcon )
				slot.m_sideIconImage = randomIcon->getName();
		}
		else
		{
			const PlayerTemplate *fact = ThePlayerTemplateStore->getNthPlayerTemplate(playerTemplate);
			if ( fact )
			{
				const Image *sideIcon = fact->getSideIconImage();
				if ( sideIcon )
					slot.m_sideIconImage = sideIcon->getName();
			}
		}
	}

	return details;
}
