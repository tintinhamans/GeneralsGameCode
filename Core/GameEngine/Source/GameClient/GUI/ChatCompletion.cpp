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

#include "GameClient/GUI/ChatCompletion.h"

#include <algorithm>
#include <wctype.h>

static wchar_t FoldCase(wchar_t c)
{
	return static_cast<wchar_t>(towlower(c));
}

static bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix)
{
	if (prefix.size() > text.size())
	{
		return false;
	}
	for (size_t i = 0; i < prefix.size(); ++i)
	{
		if (FoldCase(text[i]) != FoldCase(prefix[i]))
		{
			return false;
		}
	}
	return true;
}

static bool EqualsNoCase(const std::wstring& a, const std::wstring& b)
{
	return a.size() == b.size() && StartsWithNoCase(a, b);
}

// Orders case-insensitively and breaks ties by exact case so the order is stable.
static bool LessNoCase(const std::wstring& a, const std::wstring& b)
{
	size_t count = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < count; ++i)
	{
		wchar_t left = FoldCase(a[i]);
		wchar_t right = FoldCase(b[i]);
		if (left != right)
		{
			return left < right;
		}
	}
	if (a.size() != b.size())
	{
		return a.size() < b.size();
	}
	return a < b;
}

static void SortUniqueAndCap(std::vector<std::wstring>& matches)
{
	std::sort(matches.begin(), matches.end(), LessNoCase);
	matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
	if (matches.size() > static_cast<size_t>(CHAT_COMPLETION_MAX_MATCHES))
	{
		matches.erase(matches.begin() + CHAT_COMPLETION_MAX_MATCHES, matches.end());
	}
}

int ChatCommandRegistry::Resolve(int node) const
{
	if (node < 0 || node >= static_cast<int>(m_nodes.size()))
	{
		return node;
	}
	return m_nodes[node].target;
}

int ChatCommandRegistry::AddNode(int parent, const std::wstring& word, int target)
{
	if (word.empty())
	{
		return CHAT_COMMAND_NONE;
	}
	Node node;
	node.word = word;
	node.parent = parent;
	node.target = target < 0 ? static_cast<int>(m_nodes.size()) : target;
	m_nodes.push_back(node);
	return static_cast<int>(m_nodes.size()) - 1;
}

int ChatCommandRegistry::AddCommand(const std::wstring& name)
{
	return AddNode(CHAT_COMMAND_ROOT, name, CHAT_COMMAND_NONE);
}

int ChatCommandRegistry::AddArgument(int parent, const std::wstring& word)
{
	if (parent < 0 || parent >= static_cast<int>(m_nodes.size()))
	{
		return CHAT_COMMAND_NONE;
	}
	return AddNode(Resolve(parent), word, CHAT_COMMAND_NONE);
}

void ChatCommandRegistry::AddAlias(int node, const std::wstring& alias)
{
	if (node < 0 || node >= static_cast<int>(m_nodes.size()))
	{
		return;
	}
	int target = Resolve(node);
	AddNode(m_nodes[target].parent, alias, target);
}

int ChatCommandRegistry::FindNext(int parent, const std::wstring& word) const
{
	for (size_t i = 0; i < m_nodes.size(); ++i)
	{
		if (m_nodes[i].parent == parent && EqualsNoCase(m_nodes[i].word, word))
		{
			return m_nodes[i].target;
		}
	}
	return CHAT_COMMAND_NONE;
}

void ChatCommandRegistry::GetNextWords(int parent, std::vector<std::wstring>& words) const
{
	for (size_t i = 0; i < m_nodes.size(); ++i)
	{
		if (m_nodes[i].parent == parent)
		{
			words.push_back(m_nodes[i].word);
		}
	}
}

static std::vector<std::wstring> CommandMatches(const std::wstring& text, const ChatCommandRegistry& commands, size_t& replaceFrom)
{
	std::vector<std::wstring> matches;
	size_t wordStart = text.find_last_of(L' ');
	wordStart = (wordStart == std::wstring::npos) ? 1 : wordStart + 1;

	int node = CHAT_COMMAND_ROOT;
	size_t pos = 1;
	while (pos < wordStart)
	{
		size_t end = text.find(L' ', pos);
		if (end == std::wstring::npos || end > wordStart)
		{
			end = wordStart;
		}
		if (end > pos)
		{
			node = commands.FindNext(node, text.substr(pos, end - pos));
			if (node == CHAT_COMMAND_NONE)
			{
				return matches;
			}
		}
		pos = end + 1;
	}

	std::vector<std::wstring> candidates;
	commands.GetNextWords(node, candidates);
	const std::wstring word = text.substr(wordStart);
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (StartsWithNoCase(candidates[i], word))
		{
			matches.push_back(candidates[i]);
		}
	}
	SortUniqueAndCap(matches);
	replaceFrom = wordStart;
	return matches;
}

static std::vector<std::wstring> NameMatches(const std::wstring& text, const std::vector<std::wstring>& names, size_t& replaceFrom)
{
	std::vector<std::wstring> matches;
	const size_t maxNames = CHAT_COMPLETION_MAX_NAMES;
	size_t nameCount = names.size() < maxNames ? names.size() : maxNames;
	for (size_t start = 0; start < text.size(); ++start)
	{
		if (start > 0 && text[start - 1] != L' ')
		{
			continue;
		}
		size_t tailStart = start;
		if (text[tailStart] == L'@')
		{
			++tailStart;
		}
		if (tailStart >= text.size())
		{
			continue;
		}
		const std::wstring tail = text.substr(tailStart);
		for (size_t n = 0; n < nameCount; ++n)
		{
			if (StartsWithNoCase(names[n], tail))
			{
				matches.push_back(names[n]);
			}
		}
		if (!matches.empty())
		{
			SortUniqueAndCap(matches);
			replaceFrom = tailStart;
			return matches;
		}
	}
	return matches;
}

std::vector<std::wstring> ChatCompletionMatches(const std::wstring& text, const ChatCompletionContext& context, size_t& replaceFrom)
{
	replaceFrom = text.size();
	if (context.commands != NULL && !text.empty() && text[0] == L'/')
	{
		return CommandMatches(text, *context.commands, replaceFrom);
	}
	return NameMatches(text, context.names, replaceFrom);
}

std::wstring ChatCompletionHint(const std::wstring& text, const ChatCompletionContext& context)
{
	size_t replaceFrom = 0;
	std::vector<std::wstring> matches = ChatCompletionMatches(text, context, replaceFrom);
	if (matches.empty())
	{
		return std::wstring();
	}
	size_t typed = text.size() - replaceFrom;
	if (typed >= matches[0].size())
	{
		return std::wstring();
	}
	return matches[0].substr(typed);
}

ChatTabCycler::ChatTabCycler()
	: m_active(false)
	, m_replaceFrom(0)
	, m_index(0)
{
}

void ChatTabCycler::Reset()
{
	m_active = false;
	m_original.erase();
	m_completed.erase();
	m_matches.clear();
	m_replaceFrom = 0;
	m_index = 0;
}

bool ChatTabCycler::Complete(std::wstring& text, const ChatCompletionContext& context, bool bReverse)
{
	if (m_active && text == m_completed && !m_matches.empty())
	{
		if (bReverse)
		{
			m_index = (m_index == 0) ? m_matches.size() - 1 : m_index - 1;
		}
		else
		{
			m_index = (m_index + 1) % m_matches.size();
		}
	}
	else
	{
		Reset();
		m_matches = ChatCompletionMatches(text, context, m_replaceFrom);
		if (m_matches.empty())
		{
			return false;
		}
		m_original = text;
		m_index = bReverse ? m_matches.size() - 1 : 0;
		m_active = true;
	}

	text = m_original.substr(0, m_replaceFrom) + m_matches[m_index] + L" ";
	m_completed = text;
	return true;
}
