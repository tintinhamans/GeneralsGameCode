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

#pragma once

#include <map>
#include <string>

class IniNameRegistry
{
public:
	typedef std::map<int, std::string> CustomNames;

	IniNameRegistry(const char* const* names, int count) : m_names(names), m_count(count)
	{
	}

	int resolve(const char* name)
	{
		if (!name || !*name)
		{
			return -1;
		}

		std::string key = normalize(name);
		for (int i = 0; i < m_count; ++i)
		{
			if (key == normalize(m_names[i]))
			{
				return i;
			}
		}

		// Custom IDs must survive changes to INI order and the built-in name list.
		unsigned int hash = 2166136261u;
		for (std::string::const_iterator it = key.begin(); it != key.end(); ++it)
		{
			hash = (hash ^ static_cast<unsigned char>(*it)) * 16777619u;
		}
		int id = static_cast<int>((hash & 0x3fffffffu) | 0x40000000u);
		CustomNames::const_iterator existing = m_customNames.find(id);
		if (existing != m_customNames.end())
		{
			return existing->second == key ? id : -1;
		}
		m_customNames[id] = key;
		return id;
	}

	const char* getName(int id) const
	{
		if (id >= 0 && id < m_count)
		{
			return m_names[id];
		}
		CustomNames::const_iterator it = m_customNames.find(id);
		return it == m_customNames.end() ? NULL : it->second.c_str();
	}

	const CustomNames& getCustomNames() const
	{
		return m_customNames;
	}

private:
	static std::string normalize(const char* name)
	{
		std::string key(name);
		for (std::string::iterator it = key.begin(); it != key.end(); ++it)
		{
			if (*it >= 'A' && *it <= 'Z')
			{
				*it += 'a' - 'A';
			}
		}
		return key;
	}

	const char* const* m_names;
	int m_count;
	CustomNames m_customNames;
};
