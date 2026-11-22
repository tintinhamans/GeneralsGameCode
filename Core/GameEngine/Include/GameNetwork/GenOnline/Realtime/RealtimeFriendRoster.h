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

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

struct FriendsEntry
{
	int64_t user_id = -1;
	std::string display_name;
	bool online = false;
	std::string presence;
	// "online", "idle" or "do_not_disturb"
	std::string status = "online";
};

namespace GenOnlineRealtime
{

typedef std::unordered_map<int64_t, FriendsEntry> FriendsMap;

// What a FriendPresence changed. Only a change of a friend is shown, a pending request only keeps it.
struct FriendPresenceChange
{
	bool friendChanged = false;
	bool onlineChanged = false;
	std::string displayName;
};

// Applies FriendPresence to a friend, or keeps it on the pending request of that user.
// presenceText is the activity as the communicator shows it.
inline FriendPresenceChange ApplyFriendPresence(FriendsMap& friends, FriendsMap& pending, const FriendPresenceEvent& event, const std::string& presenceText)
{
	FriendPresenceChange change;
	auto it = friends.find(event.user_id);
	if (it == friends.end())
	{
		auto request = pending.find(event.user_id);
		if (request != pending.end())
		{
			request->second.online = event.online;
			request->second.presence = presenceText;
			request->second.status = event.status.value_or("online");
		}
		return change;
	}

	change.onlineChanged = it->second.online != event.online;
	change.friendChanged = change.onlineChanged || it->second.presence != presenceText || it->second.status != event.status.value_or("online");
	change.displayName = it->second.display_name;
	it->second.online = event.online;
	it->second.presence = presenceText;
	it->second.status = event.status.value_or("online");
	return change;
}

// Moves an accepted friend request to the friends with the presence it already received.
// False when the user had no pending request.
inline bool MoveAcceptedRequest(FriendsMap& friends, FriendsMap& pending, int64_t userID)
{
	auto it = pending.find(userID);
	if (it == pending.end())
	{
		return false;
	}
	friends.insert_or_assign(userID, it->second);
	pending.erase(it);
	return true;
}

} // namespace GenOnlineRealtime
