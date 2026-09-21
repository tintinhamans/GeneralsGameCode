/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "PreRTS.h"

#include <string.h>

#include "GameNetwork/Caster/CasterFrameHistory.h"

CasterFrameHistory::CasterFrameHistory()
	: m_entries(nullptr),
		m_capacity(0),
		m_open(FALSE),
		m_firstFrame(0),
		m_lastFrame(0),
		m_count(0)
{
}

CasterFrameHistory::~CasterFrameHistory()
{
	close();
}

Bool CasterFrameHistory::open()
{
	close();
	m_open = TRUE;
	return TRUE;
}

void CasterFrameHistory::close()
{
	for (UnsignedInt i = 0; i < m_count; ++i)
		delete [] m_entries[i].data;
	delete [] m_entries;
	m_entries = nullptr;
	m_capacity = 0;
	m_open = FALSE;
	m_firstFrame = m_lastFrame = m_count = 0;
}

Bool CasterFrameHistory::grow(UnsignedInt minimumCapacity)
{
	UnsignedInt capacity = m_capacity ? m_capacity : 64;
	Entry* entries;

	while (capacity < minimumCapacity)
	{
		if (capacity > 0x3fffffffU)
		{
			return FALSE;
		}
		capacity *= 2;
	}

	if (capacity > 0x7fffffffU / sizeof(Entry))
	{
		return FALSE;
	}
	try
	{
		entries = new Entry[capacity];
	}
	catch (...)
	{
		return FALSE;
	}
	if (entries == nullptr)
	{
		return FALSE;
	}
	for (UnsignedInt i = 0; i < m_count; ++i)
		entries[i] = m_entries[i];
	delete [] m_entries;
	m_entries = entries;
	m_capacity = capacity;
	return TRUE;
}

Bool CasterFrameHistory::append(UnsignedInt frame, const UnsignedByte* data, UnsignedInt length)
{
	UnsignedInt expected;
	UnsignedByte* copy;

	if (!m_open)
	{
		return FALSE;
	}
	if (!data && length)
	{
		return FALSE;
	}
	if (length > MAX_FRAME_BYTES)
	{
		return FALSE;
	}
	if (!m_count)
	{
		if (frame)
		{
			return FALSE;
		}
	}
	else
	{
		expected = m_lastFrame + 1;
		if (!expected || frame > expected)
		{
			return FALSE;
		}
		if (frame < expected)
			return compareRecord(frame - m_firstFrame, data, length);
	}
	if (m_count == m_capacity && !grow(m_count + 1))
		return FALSE;

	try
	{
		copy = length ? new UnsignedByte[length] : nullptr;
	}
	catch (...)
	{
		return FALSE;
	}
	if (length && copy == nullptr)
	{
		return FALSE;
	}
	if (length)
		memcpy(copy, data, length);

	m_entries[m_count].data = copy;
	m_entries[m_count].length = length;
	if (!m_count)
		m_firstFrame = frame;
	++m_count;
	m_lastFrame = frame;
	return TRUE;
}

Bool CasterFrameHistory::compareRecord(UnsignedInt index, const UnsignedByte* data, UnsignedInt length)
{
	if (index >= m_count || m_entries[index].length != length)
	{
		return FALSE;
	}
	if (length && memcmp(m_entries[index].data, data, length))
	{
		return FALSE;
	}
	return TRUE;
}

Bool CasterFrameHistory::read(UnsignedInt frame, UnsignedByte* destination, UnsignedInt capacity, UnsignedInt& outLength)
{
	UnsignedInt index;
	outLength = 0;
	if (!m_open)
	{
		return FALSE;
	}
	if (!m_count || frame < m_firstFrame || frame > m_lastFrame)
	{
		return FALSE;
	}
	if (!destination && capacity)
	{
		return FALSE;
	}

	index = frame - m_firstFrame;
	if (index >= m_count || capacity < m_entries[index].length
		|| (!destination && m_entries[index].length))
	{
		return FALSE;
	}
	if (m_entries[index].length)
		memcpy(destination, m_entries[index].data, m_entries[index].length);
	outLength = m_entries[index].length;
	return TRUE;
}
