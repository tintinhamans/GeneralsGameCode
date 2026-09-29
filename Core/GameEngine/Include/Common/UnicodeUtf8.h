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

#pragma once

#include <string>
#include "Common/UnicodeString.h"

// UTF-8 <-> UTF-16 conversion for text that leaves or enters the game as UTF-8 (RmlUi strings, the
// online services' JSON). Never throws: malformed UTF-8 and unpaired surrogates become U+FFFD,
// embedded NULs are kept, and an empty input gives an empty output. Windows' wchar_t is UTF-16, so
// characters beyond the BMP travel as surrogate pairs.
//
// UnicodeString::translate(AsciiString) goes through the process code page, which is only UTF-8
// where the manifest's activeCodePage applies, so don't use it for UTF-8.

std::wstring utf8ToWide(const std::string &utf8);
std::string wideToUtf8(const std::wstring &wide);

UnicodeString utf8ToUnicode(const std::string &utf8);
std::string unicodeToUtf8(const UnicodeString &str);
