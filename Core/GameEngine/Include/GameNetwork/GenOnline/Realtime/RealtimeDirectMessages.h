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

// FILE: RealtimeDirectMessages.h ////////////////////////////////////////////
// Description: Direct message conversations of the GenOnline realtime
//              protocol v1. Each side of a direct message sees the channel
//              dm:<the other user>. It keeps the unread counts of Conversations,
//              MessageCreate and ReadMarker, and merges GetHistory with the live
//              messages of a conversation once per message id.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeChannelChat.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

// The number of messages GetHistory loads when a conversation opens.
inline constexpr int DIRECT_HISTORY_LIMIT = 50;

// The user id of the other user of a "dm:<user id>" channel, or -1 for any other channel.
inline int64_t DirectMessagePeer(const std::string& channelID)
{
	const size_t prefix = 3;
	if (channelID.size() <= prefix || channelID.compare(0, prefix, "dm:") != 0)
	{
		return -1;
	}
	int64_t id = 0;
	for (size_t i = prefix; i < channelID.size(); ++i)
	{
		const char c = channelID[i];
		if (c < '0' || c > '9')
		{
			return -1;
		}
		const int64_t digit = c - '0';
		if (id > ((std::numeric_limits<int64_t>::max)() - digit) / 10)
		{
			return -1;
		}
		id = id * 10 + digit;
	}
	return id;
}

inline std::string DirectMessageChannel(int64_t peer)
{
	return "dm:" + std::to_string(peer);
}

// The unread state of the direct conversations of the user, keyed by the other user.
class DirectMessageUnread
{
public:
	// Replaces the state with a Conversations snapshot. Group chats are left out.
	// Returns the number of conversations that were read before and are unread now.
	int ApplySnapshot(const ConversationsEvent& event)
	{
		std::map<int64_t, State> states;
		int becameUnread = 0;
		for (const Conversation& conversation : event.conversations)
		{
			const int64_t peer = DirectMessagePeer(conversation.channel_id);
			if (peer < 0)
			{
				continue;
			}
			State state;
			state.unread = (std::max)(0, conversation.unread);
			if (state.unread > 0 && Unread(peer) == 0)
			{
				++becameUnread;
			}
			states[peer] = state;
		}
		m_states.swap(states);
		return becameUnread;
	}

	// Counts a new message of the other user. Returns true when the conversation was read before.
	bool AddUnread(int64_t peer)
	{
		State& state = m_states[peer];
		++state.unread;
		return state.unread == 1;
	}

	void Clear(int64_t peer)
	{
		std::map<int64_t, State>::iterator it = m_states.find(peer);
		if (it != m_states.end())
		{
			it->second.unread = 0;
		}
	}

	int Unread(int64_t peer) const
	{
		std::map<int64_t, State>::const_iterator it = m_states.find(peer);
		return it != m_states.end() ? it->second.unread : 0;
	}

	// A ReadMarker clears the unread count of its conversation. Returns false for a channel that is no direct conversation.
	bool ApplyReadMarker(const ReadMarkerEvent& event)
	{
		const int64_t peer = DirectMessagePeer(event.channel_id);
		if (peer < 0)
		{
			return false;
		}
		Clear(peer);
		return true;
	}

private:
	struct State
	{
		int unread = 0;
	};

	std::map<int64_t, State> m_states;
};

// A line of a direct conversation. A note is a local line without a message, and its key is 0.
struct DirectChatLine
{
	uint32_t key = 0;
	ChatMessage message;
	std::wstring note;

	bool IsNote() const { return key == 0; }
};

// The lines of one direct conversation. Live messages show at once. A history adds the
// messages the conversation does not show yet next to the shown messages it shares with it.
class DirectConversation
{
public:
	explicit DirectConversation(const std::string& channelID) : m_channelID(channelID)
	{
		m_log.Start(channelID);
		m_log.ReleaseWaiting();
	}

	const std::string& ChannelID() const { return m_channelID; }
	const std::vector<DirectChatLine>& Lines() const { return m_lines; }

	// A conversation loads its history once per connection, and again after a failed load.
	bool NeedsHistory() const { return !m_historyLoaded && !m_historyPending; }
	void BeginHistory() { m_historyPending = true; }
	void HistoryFailed() { m_historyPending = false; }
	void ForgetHistory() { m_historyLoaded = false; }

	// Adds a MessageCreate. Returns false for a duplicate, a tombstone or another channel.
	bool AddLive(const ChatMessage& message, DirectChatLine& added)
	{
		bool bAdded = false;
		for (const ChannelChatLine& line : m_log.AddLive(message))
		{
			if (Find(line.message.id) < 0)
			{
				m_lines.push_back(ToLine(line));
				added = m_lines.back();
				bAdded = true;
			}
		}
		return bAdded;
	}

	void AddNote(const std::wstring& note)
	{
		DirectChatLine line;
		line.note = note;
		m_lines.push_back(line);
	}

	// Adds a GetHistory result, oldest first. Tombstones remove their shown messages.
	void AddHistory(const GetHistoryResult& result)
	{
		m_historyPending = false;
		m_historyLoaded = true;
		ChannelHistoryEvent history;
		history.channel_id = m_channelID;
		history.messages = result.messages;
		history.has_more = result.has_more;
		const ChannelChatUpdate update = m_log.AddHistory(history);
		for (uint32_t key : update.removed)
		{
			EraseKey(key);
		}

		// New messages go before the first shown message the history shares, or after every line.
		size_t cursor = m_lines.size();
		for (const ChatMessage& message : history.messages)
		{
			const int index = message.IsDeleted() ? -1 : Find(message.id);
			if (index >= 0)
			{
				cursor = static_cast<size_t>(index);
				break;
			}
		}
		size_t next = 0;
		for (const ChatMessage& message : history.messages)
		{
			if (message.IsDeleted())
			{
				continue;
			}
			const bool bNew = next < update.shown.size() && update.shown[next].message.id == message.id;
			const int index = Find(message.id);
			if (index >= 0)
			{
				cursor = static_cast<size_t>(index) + 1;
			}
			else if (bNew)
			{
				m_lines.insert(m_lines.begin() + cursor, ToLine(update.shown[next]));
				++cursor;
			}
			if (bNew)
			{
				++next;
			}
		}
	}

	// The key of the line a MessageDelete removes, or 0 for an unknown id.
	uint32_t Remove(const std::string& messageID)
	{
		const uint32_t key = m_log.Remove(m_channelID, messageID);
		if (key != 0)
		{
			EraseKey(key);
		}
		return key;
	}

	// The newest shown message, or nullptr when the conversation shows none.
	const ChatMessage* NewestMessage() const
	{
		for (std::vector<DirectChatLine>::const_reverse_iterator it = m_lines.rbegin(); it != m_lines.rend(); ++it)
		{
			if (!it->IsNote())
			{
				return &it->message;
			}
		}
		return nullptr;
	}

private:
	static DirectChatLine ToLine(const ChannelChatLine& shown)
	{
		DirectChatLine line;
		line.key = shown.key;
		line.message = shown.message;
		return line;
	}

	int Find(const std::string& messageID) const
	{
		for (size_t i = 0; i < m_lines.size(); ++i)
		{
			if (!m_lines[i].IsNote() && m_lines[i].message.id == messageID)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void EraseKey(uint32_t key)
	{
		for (std::vector<DirectChatLine>::iterator it = m_lines.begin(); it != m_lines.end(); ++it)
		{
			if (it->key == key)
			{
				m_lines.erase(it);
				return;
			}
		}
	}

	std::string m_channelID;
	ChannelChatLog m_log;
	std::vector<DirectChatLine> m_lines;
	bool m_historyLoaded = false;
	bool m_historyPending = false;
};

} // namespace GenOnlineRealtime
