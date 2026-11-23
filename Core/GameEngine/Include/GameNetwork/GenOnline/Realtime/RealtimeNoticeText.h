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
#include "GameNetwork/GenOnline/json.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace GenOnlineRealtime
{

// Turns an ISO 8601 UTC time of the service into the text shown to the player.
using TimeTextFormatter = std::function<std::string(const std::string& utcText)>;

inline bool HasNoticeText(const std::optional<std::string>& text)
{
	return text.has_value() && !text->empty();
}

// The " until <time>" part of a text, or an empty string without an expiry.
inline std::string UntilText(const std::optional<std::string>& expiresAt, const TimeTextFormatter& format)
{
	if (!HasNoticeText(expiresAt))
	{
		return std::string();
	}
	return " until " + (format ? format(*expiresAt) : *expiresAt);
}

// The chat line of a kick or mute notice. A ban shows its own dialog, so it has no line here.
inline std::string ModerationNoticeText(const ModerationNoticeEvent& notice, const TimeTextFormatter& format)
{
	if (notice.action == "kick")
	{
		const std::string reason = HasNoticeText(notice.reason) ? ": " + *notice.reason : std::string();
		return "A moderator removed you from the online service" + reason + ". Reconnecting in 10 seconds.";
	}
	if (notice.action == "mute")
	{
		const std::string reason = HasNoticeText(notice.reason) ? ": " + *notice.reason : std::string(".");
		return "A moderator muted you" + UntilText(notice.expires_at, format) + reason;
	}
	return std::string();
}

// The first line of the ban dialog. Without the ban body of the service the end of the ban stays unsaid.
inline std::string BanDialogText(const std::optional<std::string>& expiresAt, bool bDetailsKnown, const TimeTextFormatter& format)
{
	if (HasNoticeText(expiresAt))
	{
		return "You have been banned from Generals Online" + UntilText(expiresAt, format) + ".";
	}
	if (bDetailsKnown)
	{
		return "You have been permanently banned from Generals Online.";
	}
	return "You have been banned from Generals Online.";
}

// The expires_at of failure params, or empty when it is absent or null.
inline std::optional<std::string> ParamExpiresAt(const nlohmann::json& params)
{
	auto it = params.find("expires_at");
	if (it != params.end() && it->is_string())
	{
		return it->get<std::string>();
	}
	return std::nullopt;
}

// The text of a failed rule from section 7, or an empty string for codes the caller handles.
inline std::string RuleFailureText(const std::string& code, const nlohmann::json& params, const TimeTextFormatter& format)
{
	if (code == "chat.muted")
	{
		return "You are muted and cannot send messages" + UntilText(ParamExpiresAt(params), format) + ".";
	}
	if (code == "moderation.banned")
	{
		return "You are banned from Generals Online" + UntilText(ParamExpiresAt(params), format) + ".";
	}
	if (code == "unauthorized")
	{
		return "Your sign-in is no longer valid. Please sign in again.";
	}
	if (code == "session.build_not_accepted")
	{
		return "This version of the game is not accepted by the online service. Please update the game.";
	}
	if (code == "channel.unavailable")
	{
		return "That room is not available.";
	}
	if (code == "moderation.target_outranks")
	{
		return "You cannot do that to a player of equal or higher rank.";
	}
	if (code == "report.duplicate")
	{
		return "You already reported that.";
	}
	if (code == "group.full")
	{
		auto it = params.find("max");
		if (it != params.end() && it->is_number_integer() && it->get<int64_t>() > 0)
		{
			return "The group is full. Groups can have up to " + std::to_string(it->get<int64_t>()) + " members.";
		}
		return "The group is full.";
	}
	if (code == "group.not_owner")
	{
		return "Only the group owner can do that.";
	}
	if (code == "group.not_member")
	{
		return "You are not a member of that group.";
	}
	return std::string();
}

} // namespace GenOnlineRealtime
