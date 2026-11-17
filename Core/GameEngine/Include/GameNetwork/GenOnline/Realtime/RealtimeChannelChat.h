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

// FILE: RealtimeChannelChat.h ///////////////////////////////////////////////
// Description: Chat log of the channel a session is in, GenOnline realtime
//              protocol v1. It decides which messages of MessageCreate and
//              ChannelHistory the chat shows, deduplicated by message id, and
//              which shown line a MessageDelete removes.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

// A message the chat shows. The key names its chat line and is never 0.
struct ChannelChatLine
{
	uint32_t key = 0;
	// A live message came as MessageCreate, otherwise it came with the history.
	bool live = false;
	ChatMessage message;
};

struct ChannelChatUpdate
{
	std::vector<ChannelChatLine> shown;
	std::vector<uint32_t> removed;
};

// Every start waits for the ChannelHistory that follows ChannelMembers. Live messages
// wait with it, so the history shows before them. A message id shows once per channel.
class ChannelChatLog
{
public:
	// The number of message ids remembered, more than the history of a join.
	static constexpr size_t MAX_SHOWN = 200;

	// Starts the chat of a channel. A different channel forgets the shown messages.
	void Start(const std::string& channelID)
	{
		if (channelID != m_channelID)
		{
			m_shown.clear();
			m_channelID = channelID;
		}
		m_waiting.clear();
		m_awaitingHistory = true;
	}

	// Forgets the channel. Nothing shows until the next start.
	void Clear()
	{
		m_channelID.clear();
		m_shown.clear();
		m_waiting.clear();
		m_awaitingHistory = false;
	}

	const std::string& ChannelID() const { return m_channelID; }
	bool IsAwaitingHistory() const { return m_awaitingHistory; }
	size_t ShownCount() const { return m_shown.size(); }

	// The lines a MessageCreate adds. A message waits while the history is awaited.
	std::vector<ChannelChatLine> AddLive(const ChatMessage& message)
	{
		std::vector<ChannelChatLine> lines;
		if (m_channelID.empty() || message.channel_id != m_channelID || message.IsDeleted() || IsKnown(message.id))
		{
			return lines;
		}
		if (m_awaitingHistory)
		{
			m_waiting.push_back(message);
			return lines;
		}
		lines.push_back(Show(message, true));
		return lines;
	}

	// The lines a ChannelHistory adds, oldest first, then the live messages that waited for it.
	// A tombstone never shows and removes the line of its message when that line shows.
	ChannelChatUpdate AddHistory(const ChannelHistoryEvent& history)
	{
		ChannelChatUpdate update;
		if (m_channelID.empty() || history.channel_id != m_channelID)
		{
			return update;
		}
		for (const ChatMessage& message : history.messages)
		{
			if (message.IsDeleted())
			{
				const uint32_t key = Forget(message.id);
				if (key != 0)
				{
					update.removed.push_back(key);
				}
				continue;
			}
			if (!IsKnown(message.id))
			{
				update.shown.push_back(Show(message, false));
			}
		}
		std::vector<ChannelChatLine> waiting = ReleaseWaiting();
		update.shown.insert(update.shown.end(), waiting.begin(), waiting.end());
		return update;
	}

	// Ends the wait for the history and returns the lines of the live messages that waited.
	std::vector<ChannelChatLine> ReleaseWaiting()
	{
		std::vector<ChannelChatLine> lines;
		m_awaitingHistory = false;
		for (const ChatMessage& message : m_waiting)
		{
			if (!IsKnown(message.id))
			{
				lines.push_back(Show(message, true));
			}
		}
		m_waiting.clear();
		return lines;
	}

	// The key of the shown line a MessageDelete removes, or 0 for an unknown id or another channel.
	uint32_t Remove(const std::string& channelID, const std::string& messageID)
	{
		if (m_channelID.empty() || channelID != m_channelID)
		{
			return 0;
		}
		for (std::vector<ChatMessage>::iterator it = m_waiting.begin(); it != m_waiting.end(); ++it)
		{
			if (it->id == messageID)
			{
				m_waiting.erase(it);
				Remember(messageID, 0, 0);
				return 0;
			}
		}
		return Forget(messageID);
	}

	// The message id and author of a shown line, for its context menu. False for a removed line,
	// or one older than the MAX_SHOWN messages the log remembers.
	bool MessageOf(uint32_t key, std::string& messageID, int64_t& authorID) const
	{
		if (key == 0)
		{
			return false;
		}
		for (const ShownID& shown : m_shown)
		{
			if (shown.key == key)
			{
				messageID = shown.id;
				authorID = shown.authorID;
				return true;
			}
		}
		return false;
	}

private:
	struct ShownID
	{
		std::string id;
		// 0 once the line is removed. The id stays known so a late copy does not show again.
		uint32_t key;
		int64_t authorID;
	};

	bool IsKnown(const std::string& messageID) const
	{
		for (const ShownID& shown : m_shown)
		{
			if (shown.id == messageID)
			{
				return true;
			}
		}
		return false;
	}

	void Remember(const std::string& messageID, uint32_t key, int64_t authorID)
	{
		if (m_shown.size() >= MAX_SHOWN)
		{
			m_shown.pop_front();
		}
		m_shown.push_back(ShownID{ messageID, key, authorID });
	}

	// Marks a message as removed and returns the key of its shown line, or 0.
	uint32_t Forget(const std::string& messageID)
	{
		for (ShownID& shown : m_shown)
		{
			if (shown.id == messageID)
			{
				const uint32_t key = shown.key;
				shown.key = 0;
				return key;
			}
		}
		return 0;
	}

	ChannelChatLine Show(const ChatMessage& message, bool live)
	{
		ChannelChatLine line;
		line.key = NextKey();
		line.live = live;
		line.message = message;
		Remember(message.id, line.key, message.author.id);
		return line;
	}

	uint32_t NextKey()
	{
		++m_lastKey;
		if (m_lastKey == 0)
		{
			++m_lastKey;
		}
		return m_lastKey;
	}

	std::string m_channelID;
	std::deque<ShownID> m_shown;
	std::vector<ChatMessage> m_waiting;
	bool m_awaitingHistory = false;
	uint32_t m_lastKey = 0;
};

// Holds a ChannelHistory that arrives before its chat starts, as the lobby history of a join does
// between LobbyState and the JoinLobby Result. Only the newest history is kept.
class PendingChannelHistory
{
public:
	void Hold(const ChannelHistoryEvent& history)
	{
		m_history = history;
	}

	void Clear()
	{
		m_history.reset();
	}

	bool IsHolding(const std::string& channelID) const
	{
		return m_history.has_value() && !channelID.empty() && m_history->channel_id == channelID;
	}

	// Takes the held history of a channel. A held history of another channel is dropped.
	bool Take(const std::string& channelID, ChannelHistoryEvent& history)
	{
		const bool bHolding = IsHolding(channelID);
		if (bHolding)
		{
			history = *m_history;
		}
		m_history.reset();
		return bHolding;
	}

private:
	std::optional<ChannelHistoryEvent> m_history;
};

} // namespace GenOnlineRealtime
