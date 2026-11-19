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

#include "GameClient/GUI/ChatCompletionSets.h"

#include <wctype.h>

static bool SameNameNoCase(const std::wstring& a, const std::wstring& b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (towlower(a[i]) != towlower(b[i]))
		{
			return false;
		}
	}
	return true;
}

static size_t FindNameNoCase(const std::vector<std::wstring>& names, const std::wstring& name)
{
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (SameNameNoCase(names[i], name))
		{
			return i;
		}
	}
	return names.size();
}

// The /net command takes transport auto or gns, relay auto or force, gns ice auto, native or webrtc, and reset.
static void AddNetCommand(ChatCommandRegistry& commands)
{
	int net = commands.AddCommand(L"net");
	int transport = commands.AddArgument(net, L"transport");
	commands.AddArgument(transport, L"auto");
	commands.AddArgument(transport, L"gns");
	int relay = commands.AddArgument(net, L"relay");
	commands.AddArgument(relay, L"auto");
	commands.AddArgument(relay, L"force");
	int gns = commands.AddArgument(net, L"gns");
	int ice = commands.AddArgument(gns, L"ice");
	commands.AddArgument(ice, L"auto");
	commands.AddArgument(ice, L"native");
	commands.AddArgument(ice, L"webrtc");
	commands.AddArgument(net, L"reset");
}

void BuildLobbyChatCommands(ChatCommandRegistry& commands)
{
	commands.AddCommand(L"me");
	commands.AddAlias(commands.AddCommand(L"help"), L"commands");
	commands.AddCommand(L"support");
	commands.AddAlias(commands.AddCommand(L"name"), L"nick");
	AddNetCommand(commands);
	commands.AddCommand(L"forcerelay");
	commands.AddCommand(L"allowrelay");
	commands.AddCommand(L"refresh");
}

void BuildGameSetupChatCommands(ChatCommandRegistry& commands)
{
	commands.AddCommand(L"host");
	commands.AddCommand(L"me");
	commands.AddAlias(commands.AddCommand(L"help"), L"commands");
	commands.AddCommand(L"support");
	AddNetCommand(commands);
	commands.AddCommand(L"friendsonly");
	commands.AddCommand(L"public");
	commands.AddCommand(L"maxcameraheight");
	commands.AddAlias(commands.AddCommand(L"steam"), L"advnet");
	commands.AddCommand(L"setpassword");
	int removePassword = commands.AddCommand(L"removepassword");
	commands.AddAlias(removePassword, L"clearpassword");
	commands.AddAlias(removePassword, L"resetpassword");
}

void BuildInGameChatCommands(ChatCommandRegistry& commands)
{
	commands.AddCommand(L"host");
}

void AddRecentChatter(std::vector<std::wstring>& chatters, const std::wstring& author, const std::wstring& self)
{
	if (author.empty() || SameNameNoCase(author, self))
	{
		return;
	}
	size_t found = FindNameNoCase(chatters, author);
	if (found < chatters.size())
	{
		chatters.erase(chatters.begin() + found);
	}
	chatters.insert(chatters.begin(), author);
	if (chatters.size() > static_cast<size_t>(CHAT_RECENT_CHATTERS_MAX))
	{
		chatters.resize(CHAT_RECENT_CHATTERS_MAX);
	}
}

void AppendChatName(std::vector<std::wstring>& names, const std::wstring& name, const std::wstring& self)
{
	if (name.empty() || SameNameNoCase(name, self) || FindNameNoCase(names, name) < names.size())
	{
		return;
	}
	names.push_back(name);
}

void AppendChatNames(std::vector<std::wstring>& names, const std::vector<std::wstring>& more, const std::wstring& self)
{
	for (size_t i = 0; i < more.size(); ++i)
	{
		AppendChatName(names, more[i], self);
	}
}
