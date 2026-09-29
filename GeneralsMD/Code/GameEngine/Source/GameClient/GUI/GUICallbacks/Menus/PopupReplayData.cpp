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

// FILE: PopupReplayData.cpp /////////////////////////////////////////////////
// See PopupReplayData.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayData.h"

void PopupReplayData::open()
{
	m_rows.clear();
	refresh();

	m_name.clear();
	m_showSaved = FALSE;
	m_savedTime = 0;
	touch();
}

void PopupReplayData::refresh()
{
	ReplayList::scan( m_rows );

	++m_rowsVersion;
	select( m_rows.empty() ? -1 : 0 );
}

void PopupReplayData::select( Int row )
{
	m_selected = ( row >= 0 && row < (Int)m_rows.size() ) ? row : -1;
	if( m_selected >= 0 )
		m_name = m_rows[m_selected].m_name;
	touch();
}

void PopupReplayData::setName( const UnicodeString &name )
{
	m_name = name;
	touch();
}
