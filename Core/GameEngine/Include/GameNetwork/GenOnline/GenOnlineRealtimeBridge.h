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

// FILE: GenOnlineRealtimeBridge.h ////////////////////////////////////////////
// Description: Connects the events of the GenOnline realtime hub to the rooms,
//              social, lobby and moderation interfaces that the menus read.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeHubSession.h"

#include <string>
#include <vector>

namespace GenOnlineRealtimeBridge
{

// Registers the event handlers of a new hub session.
void RegisterHandlers(GenOnlineRealtime::HubSession& session);

// Takes the session data of Ready. bFirstReady is true for the first Ready after a login.
void OnReady(const GenOnlineRealtime::ReadyEvent& ready, bool bFirstReady);

// The colour of the first defined badge of a user. Badges are listed highest priority first and
// operators define them at runtime, so the client never looks at badge ids. Returns false without a badge.
bool BadgeColor(const std::vector<std::string>& badges, int& color);

// True when the local user holds a moderation permission.
bool HasModerationPermission();

// The presence text of a friend activity code, for example "In game".
std::string ActivityText(const std::string& activity);

// The text shown for a hub method that did not succeed.
std::wstring FailureText(const GenOnlineRealtime::InvokeOutcome& outcome);

// A system line in the room chat.
void ShowRoomNotice(const std::wstring& text, bool bError);

} // namespace GenOnlineRealtimeBridge
