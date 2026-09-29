/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#include "PreRTS.h"

#include "Common/UnicodeUtf8.h"

#include <limits.h>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
std::wstring utf8ToWide(const std::string &utf8)
{
	if (utf8.empty() || utf8.size() > (size_t)INT_MAX)
		return std::wstring();

	// Without MB_ERR_INVALID_CHARS malformed sequences come out as U+FFFD instead of failing.
	const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	if (len <= 0)
		return std::wstring();

	std::wstring wide((size_t)len, L'\0');
	if (::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &wide[0], len) != len)
		return std::wstring();
	return wide;
}

//-------------------------------------------------------------------------------------------------
std::string wideToUtf8(const std::wstring &wide)
{
	if (wide.empty() || wide.size() > (size_t)INT_MAX)
		return std::string();

	// Unpaired surrogates are not encodable; blank them to U+FFFD first so the result does not
	// depend on how the OS handles them.
	std::wstring clean;
	const wchar_t *src = wide.data();
	size_t count = wide.size();
	for (size_t i = 0; i < count; ++i)
	{
		const wchar_t c = src[i];
		const bool high = c >= 0xD800 && c <= 0xDBFF;
		const bool low = c >= 0xDC00 && c <= 0xDFFF;
		const bool paired = high && i + 1 < count && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF;
		if (paired)
		{
			clean.push_back(c);
			clean.push_back(src[++i]);
		}
		else
		{
			clean.push_back((high || low) ? (wchar_t)0xFFFD : c);
		}
	}

	const int len = ::WideCharToMultiByte(CP_UTF8, 0, clean.data(), (int)clean.size(), nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return std::string();

	std::string utf8((size_t)len, '\0');
	if (::WideCharToMultiByte(CP_UTF8, 0, clean.data(), (int)clean.size(), &utf8[0], len, nullptr, nullptr) != len)
		return std::string();
	return utf8;
}

//-------------------------------------------------------------------------------------------------
UnicodeString utf8ToUnicode(const std::string &utf8)
{
	UnicodeString text;
	const std::wstring wide = utf8ToWide(utf8);
	if (!wide.empty())
		text.set((const WideChar *)wide.c_str());
	return text;
}

//-------------------------------------------------------------------------------------------------
std::string unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return std::string();
	return wideToUtf8(std::wstring((const wchar_t *)wide));
}
