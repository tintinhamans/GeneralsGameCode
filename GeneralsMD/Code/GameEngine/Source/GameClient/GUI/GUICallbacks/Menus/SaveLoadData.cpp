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

// FILE: SaveLoadData.cpp ////////////////////////////////////////////////////
// See SaveLoadData.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/SaveLoadData.h"

#include "GameClient/GameText.h"

void SaveLoadData::open( SaveLoadLayoutType layoutType, Bool isPopup )
{
	m_layoutType = layoutType;
	m_isPopup = isPopup;
	m_dialog = DIALOG_NONE;
	m_description.clear();
	refresh();
}

void SaveLoadData::refresh()
{
	m_rows.clear();

	// saving is allowed: the first entry is a new save game
	if( m_layoutType != SLLT_LOAD_ONLY )
	{
		SaveLoadRow row;
		row.m_name = TheGameText->fetch( "GUI:NewSaveGame" );
		row.m_color = 0xC8C8FF;
		m_rows.push_back( row );
	}

	UnsignedInt count = 0;
	for( AvailableGameInfo *info = TheGameState->refreshAvailableGames(); info; info = info->next, count++ )
	{
		SaveLoadRow row;
		Int color = TheGameState->describeAvailableGame( info, count, row.m_name, row.m_time, row.m_date );
		row.m_color = color & 0xFFFFFF;
		row.m_info = info;
		m_rows.push_back( row );
	}

	++m_rowsVersion;
	select( m_rows.empty() ? -1 : 0 );
}

void SaveLoadData::select( Int row )
{
	m_selected = ( row >= 0 && row < (Int)m_rows.size() ) ? row : -1;
	touch();
}

AvailableGameInfo *SaveLoadData::selectedInfo() const
{
	return ( m_selected >= 0 && m_selected < (Int)m_rows.size() ) ? m_rows[m_selected].m_info : nullptr;
}
