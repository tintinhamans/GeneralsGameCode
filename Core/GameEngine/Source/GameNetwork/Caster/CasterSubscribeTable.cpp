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

#include "GameNetwork/Caster/CasterSubscribeTable.h"

namespace CasterSubscribeTable
{

Table::Table()
{
	reset();
}

void Table::reset()
{
	UnsignedInt i;
	for (i = 0; i < TABLE_CAPACITY; ++i)
	{
		m_entries[i].active = FALSE;
		m_entries[i].gameUid = 0;
		m_entries[i].subscriberCount = 0;
	}
	m_count = 0;
}

Table::Entry* Table::find(UnsignedInt gameUid)
{
	UnsignedInt i;
	if (gameUid == 0)
	{
		return nullptr;
	}
	for (i = 0; i < TABLE_CAPACITY; ++i)
	{
		if (m_entries[i].active && m_entries[i].gameUid == gameUid)
		{
			return &m_entries[i];
		}
	}
	return nullptr;
}

const Table::Entry* Table::find(UnsignedInt gameUid) const
{
	UnsignedInt i;
	if (gameUid == 0)
	{
		return nullptr;
	}
	for (i = 0; i < TABLE_CAPACITY; ++i)
	{
		if (m_entries[i].active && m_entries[i].gameUid == gameUid)
		{
			return &m_entries[i];
		}
	}
	return nullptr;
}

Table::Entry* Table::findFree()
{
	UnsignedInt i;
	for (i = 0; i < TABLE_CAPACITY; ++i)
	{
		if (!m_entries[i].active)
		{
			return &m_entries[i];
		}
	}
	return nullptr;
}

Bool Table::subscribe(UnsignedInt gameUid)
{
	Entry* entry;

	if (gameUid == 0)
	{
		return FALSE;
	}

	entry = find(gameUid);
	if (entry == nullptr)
	{
		entry = findFree();
		if (entry == nullptr)
		{

			return FALSE;
		}
		entry->active = TRUE;
		entry->gameUid = gameUid;
		entry->subscriberCount = 0;
		++m_count;
	}

	++entry->subscriberCount;
	return TRUE;
}

Bool Table::unsubscribe(UnsignedInt gameUid)
{
	Entry* entry = find(gameUid);
	if (entry == nullptr)
	{

		return FALSE;
	}

	if (entry->subscriberCount > 0)
	{
		--entry->subscriberCount;
	}

	if (entry->subscriberCount == 0)
	{

		entry->active = FALSE;
		entry->gameUid = 0;
		--m_count;
	}
	return TRUE;
}

Bool Table::isSubscribed(UnsignedInt gameUid) const
{
	const Entry* entry = find(gameUid);
	return (entry != nullptr && entry->subscriberCount > 0) ? TRUE : FALSE;
}

}
