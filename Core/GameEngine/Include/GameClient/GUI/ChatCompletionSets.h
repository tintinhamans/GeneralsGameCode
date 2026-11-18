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

// The slash commands and name lists that each chat box completes from.
// The command lists match the slash command parsers of the chat boxes.
// The module uses only the standard library so that tests can build it without the engine.

#pragma once

#include "GameClient/GUI/ChatCompletion.h"

#include <string>
#include <vector>

enum
{
	CHAT_RECENT_CHATTERS_MAX = 16
};

// Adds the slash commands of the lobby channel chat.
void BuildLobbyChatCommands(ChatCommandRegistry& commands);

// Adds the slash commands of the game setup chat.
void BuildGameSetupChatCommands(ChatCommandRegistry& commands);

// Adds the slash commands of the in-game chat.
void BuildInGameChatCommands(ChatCommandRegistry& commands);

// Puts the author first in the list of recent chatters and keeps at most CHAT_RECENT_CHATTERS_MAX names.
// An empty name and the local player are left out. Names compare without case.
void AddRecentChatter(std::vector<std::wstring>& chatters, const std::wstring& author, const std::wstring& self);

// Appends the name unless it is empty, the local player or already in the list. Names compare without case.
void AppendChatName(std::vector<std::wstring>& names, const std::wstring& name, const std::wstring& self);

// Appends each name as AppendChatName does.
void AppendChatNames(std::vector<std::wstring>& names, const std::vector<std::wstring>& more, const std::wstring& self);
