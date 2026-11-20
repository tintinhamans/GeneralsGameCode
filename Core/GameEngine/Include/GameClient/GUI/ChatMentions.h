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

// Local @name matching for chat that carries no service mentions, such as the peer to peer in-game chat.
// The module uses only the standard library so that tests can build it without the engine.

#pragma once

#include <string>

// True when the text holds @ followed by the name without case. The @ starts the text or follows whitespace
// or punctuation. The name ends at the end of the text, whitespace, punctuation or #. An empty name never matches.
bool ChatTextMentionsName(const std::wstring& text, const std::wstring& name);
