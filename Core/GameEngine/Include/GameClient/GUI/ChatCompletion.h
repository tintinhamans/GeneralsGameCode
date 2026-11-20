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

// Tab completion and write-ahead hints for chat text boxes.
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

// Slash commands with their aliases and a tree of argument words.
// Each word is a node. An alias is a sibling node that points at the node it stands for.
class ChatCommandRegistry
{
public:
	// Adds a top level command and returns its node, or CHAT_COMMAND_NONE for an empty name.
	int AddCommand(const std::wstring& name);

	// Adds a word that may follow the parent node and returns its node.
	int AddArgument(int parent, const std::wstring& word);

	// Adds another spelling of a command or argument word.
	void AddAlias(int node, const std::wstring& alias);

	// Returns the node that the word names after the parent node, or CHAT_COMMAND_NONE.
	int FindNext(int parent, const std::wstring& word) const;

	// Appends every word, aliases included, that may follow the parent node.
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

// The candidates a chat box completes from.
struct ChatCompletionContext
{
	ChatCompletionContext() : commands(NULL) {}

	// Display names of players. Names may contain spaces.
	std::vector<std::wstring> names;

	// Slash commands. Text that starts with a slash completes commands only when this is set.
	const ChatCommandRegistry* commands;
};

// Returns the sorted unique matches for the end of the text and where the replaced span starts.
std::vector<std::wstring> ChatCompletionMatches(const std::wstring& text, const ChatCompletionContext& context, size_t& replaceFrom);

// Returns the rest of the first match after the typed part, for drawing as ghost text.
std::wstring ChatCompletionHint(const std::wstring& text, const ChatCompletionContext& context);

// Replaces the end of the text with a match on Tab and cycles through the matches on repeated Tab.
class ChatTabCycler
{
public:
	ChatTabCycler();

	// Completes or cycles the text in place and returns false when nothing matches.
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
