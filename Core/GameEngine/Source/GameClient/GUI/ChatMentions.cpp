/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#include "GameClient/GUI/ChatMentions.h"

#include <wctype.h>

// The name ends at the end of the text, whitespace, punctuation or #.
static bool IsMentionEnd(const std::wstring& text, size_t pos)
{
	if (pos >= text.size())
	{
		return true;
	}
	const wchar_t c = text[pos];
	return iswspace(c) || iswpunct(c) || c == L'#';
}

// The @ starts the text or follows whitespace or punctuation.
static bool IsMentionStart(const std::wstring& text, size_t at)
{
	if (at == 0)
	{
		return true;
	}
	const wchar_t c = text[at - 1];
	return iswspace(c) || iswpunct(c);
}

static bool NameAtNoCase(const std::wstring& text, size_t pos, const std::wstring& name)
{
	if (text.size() - pos < name.size())
	{
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i)
	{
		if (towlower(text[pos + i]) != towlower(name[i]))
		{
			return false;
		}
	}
	return true;
}

bool ChatTextMentionsName(const std::wstring& text, const std::wstring& name)
{
	if (name.empty())
	{
		return false;
	}
	size_t at = text.find(L'@');
	while (at != std::wstring::npos)
	{
		const size_t start = at + 1;
		if (IsMentionStart(text, at) && NameAtNoCase(text, start, name) && IsMentionEnd(text, start + name.size()))
		{
			return true;
		}
		at = text.find(L'@', start);
	}
	return false;
}
