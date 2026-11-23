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

// The caret is always at the end of the text, so completion works on the end of the text.
// The module uses only the standard library so that tests can build it without the engine.

#pragma once

#include <stddef.h>
#include <string>
#include <vector>

enum
{
	CHAT_COMPLETION_MAX_NAMES = 64,
	CHAT_COMPLETION_MAX_MATCHES = 64
};

enum
{
	CHAT_COMMAND_ROOT = -1,
	CHAT_COMMAND_NONE = -2
};

class ChatCommandRegistry
{
public:
	int AddCommand(const std::wstring& name);

	int AddArgument(int parent, const std::wstring& word);

	void AddAlias(int node, const std::wstring& alias);

	int FindNext(int parent, const std::wstring& word) const;

	void GetNextWords(int parent, std::vector<std::wstring>& words) const;

private:
	struct Node
	{
		std::wstring word;
		int parent;
		int target;
	};

	int Resolve(int node) const;
	int AddNode(int parent, const std::wstring& word, int target);

	std::vector<Node> m_nodes;
};

struct ChatCompletionContext
{
	ChatCompletionContext() : commands(NULL) {}

	// Names may contain spaces.
	std::vector<std::wstring> names;

	// Slash commands. Text that starts with a slash completes commands only when this is set.
	const ChatCommandRegistry* commands;
};

std::vector<std::wstring> ChatCompletionMatches(const std::wstring& text, const ChatCompletionContext& context, size_t& replaceFrom);

std::wstring ChatCompletionHint(const std::wstring& text, const ChatCompletionContext& context);

class ChatTabCycler
{
public:
	ChatTabCycler();

	bool Complete(std::wstring& text, const ChatCompletionContext& context, bool bReverse);

	// Forgets the cycle. The owner calls this on any other edit of the text.
	void Reset();

private:
	bool m_active;
	std::wstring m_original;
	std::wstring m_completed;
	std::vector<std::wstring> m_matches;
	size_t m_replaceFrom;
	size_t m_index;
};
