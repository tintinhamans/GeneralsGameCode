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

///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>

namespace GenOnlineRealtime
{

// Staff send direct messages to any user, friend or not.
inline constexpr const char* PERMISSION_MESSAGE_ANYONE = "chat.message_anyone";
// Staff delete any message of a lower-ranked user, and their own.
inline constexpr const char* PERMISSION_DELETE_ANY = "chat.delete_any";

// Friends chat. Staff holding chat.message_anyone chat with anyone. A non-friend chats back
// while the service lists the conversation, which it does for a staff member who wrote.
inline bool CanChatWith(bool isFriend, bool canMessageAnyone, bool hasConversation)
{
	return isFriend || canMessageAnyone || hasConversation;
}

struct MessageMenu
{
	bool report = false;
	bool remove = false;

	bool Any() const { return report || remove; }
};

// Report for a message of another user, Delete for staff holding chat.delete_any. A line
// without a stored message (a note, or a line older than the chat remembers) offers nothing.
inline MessageMenu MessageMenuFor(const std::string& messageID, int64_t authorID, int64_t localUserID, bool canDeleteAny)
{
	MessageMenu menu;
	if (messageID.empty())
	{
		return menu;
	}
	menu.report = authorID != localUserID;
	menu.remove = canDeleteAny;
	return menu;
}

} // namespace GenOnlineRealtime
