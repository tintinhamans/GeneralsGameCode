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

// FILE: ReplayMenuData.cpp //////////////////////////////////////////////////
// See ReplayMenuData.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuData.h"

void ReplayMenuData::open()
{
	m_rows.clear();
	m_deletePending = FALSE;
	m_copyPending = FALSE;
	refresh();
}

void ReplayMenuData::refresh()
{
	ReplayList::scan( m_rows );

	++m_rowsVersion;
	select( m_rows.empty() ? -1 : 0 );
}

void ReplayMenuData::select( Int row )
{
	m_selected = ( row >= 0 && row < (Int)m_rows.size() ) ? row : -1;
	touch();
}

const ReplayRow *ReplayMenuData::selectedRow() const
{
	return ( m_selected >= 0 && m_selected < (Int)m_rows.size() ) ? &m_rows[m_selected] : nullptr;
}
