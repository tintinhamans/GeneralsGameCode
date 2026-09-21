/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#pragma once

#include "Lib/BaseType.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameNetwork/Caster/CasterCore.h"
#include "GameNetwork/Caster/CasterLobby.h"

/// One chat line as every surface (LAN lobby, read-only room, in-game) sees it,
/// independent of the transport that delivered it. Filled by an adapter, drawn by
/// RenderChatMessage.
struct ChatMessage
{
	UnsignedInt senderIdentity;		///< LAN address, player slot, or caster-session identity
	UnsignedByte senderSlot;		///< 0xFF when unknown
	UnsignedByte senderTeam;		///< 0 when unknown
	Bool isCaster;
	UnsignedByte scope;				///< CasterProtocol::ChatDirection
	UnsignedInt messageId;			///< sender-scoped id used for deduplication, 0 when the transport has none
	Bool hasColor;					///< color was resolved by the caller
	Color color;
	UnicodeString displayName;		///< empty: the renderer falls back to Caster / Player
	UnicodeString text;
	Bool isEmote;					///< stock "/me " convention: rendered as "Name text", no brackets

	ChatMessage();
};

/// LAN transport adapter (stock lobby chat). Parses and strips a leading
/// "/me " the same way LANAPI::RequestPlayerChat does.
void MakeLanChatMessage(ChatMessage& out, const UnicodeString& player, UnsignedInt ip,
	const UnicodeString& text);

/// Caster-direct-source adapter. FALSE when the text is empty.
Bool MakeCasterChatMessage(ChatMessage& out, UnsignedByte direction, UnsignedByte senderSlot,
	Bool isCaster, UnsignedByte senderTeam, UnsignedInt senderIdentity, UnsignedInt messageId,
	const char* senderNameUtf8, UnsignedInt senderNameLen, const char* textUtf8, UnsignedInt textLen,
	Bool isEmote);
Bool MakeCasterChatMessage(ChatMessage& out, const CasterInbound& event);
Bool MakeCasterChatMessage(ChatMessage& out, const CasterLobby::LineQueue::Line& line);

/// Resolves a multiplayer color index to a chat color. FALSE when it is unknown.
Bool ChatColorFromIndex(Int colorIndex, Color& out);

/// The single renderer: language filter, "[name] text" (or, for an emote,
/// "name text" with no brackets), and the caster identity color (magenta).
/// Without a caller-resolved color the line uses `fallbackColor`.
void RenderChatMessage(const ChatMessage& message, Color fallbackColor, UnicodeString& line,
	Color& color);
