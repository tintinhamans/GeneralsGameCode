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

#include "PreRTS.h"

#include "GameNetwork/Caster/CasterChatMessage.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/LanguageFilter.h"
#include "GameNetwork/Caster/CasterProtocol.h"

static const UnsignedInt CHAT_NAME_WIDE_CHARS = 64;

ChatMessage::ChatMessage()
	: senderIdentity(0),
		senderSlot(0xFF),
		senderTeam(0),
		isCaster(FALSE),
		scope((UnsignedByte)CasterProtocol::CHAT_LOBBY),
		messageId(0),
		hasColor(FALSE),
		color(0),
		isEmote(FALSE)
{
}

void MakeLanChatMessage(ChatMessage& out, const UnicodeString& player, UnsignedInt ip,
	const UnicodeString& text)
{
	out = ChatMessage();
	out.senderIdentity = ip;
	out.scope = (UnsignedByte)CasterProtocol::CHAT_LOBBY;
	out.displayName = player;
	// TheSuperHackers @feature arcticdolphin Parses the stock "/me " emote
	// convention the same way LANAPI::RequestPlayerChat does, so any caller of
	// this adapter renders emotes consistently even if it never saw the raw
	// LANCHAT_EMOTE type.
	if (text.startsWithNoCase(L"/me "))
	{
		out.isEmote = TRUE;
		out.text = UnicodeString(text.str() + 4);
	}
	else
	{
		out.text = text;
	}
}

Bool MakeCasterChatMessage(ChatMessage& out, UnsignedByte direction, UnsignedByte senderSlot,
	Bool isCaster, UnsignedByte senderTeam, UnsignedInt senderIdentity, UnsignedInt messageId,
	const char* senderNameUtf8, UnsignedInt senderNameLen, const char* textUtf8, UnsignedInt textLen,
	Bool isEmote)
{
	WideChar wideText[CASTER_CHAT_TEXT_BYTES + 1];
	WideChar wideName[CHAT_NAME_WIDE_CHARS + 1];
	UnsignedInt wideTextLen;
	UnsignedInt wideNameLen = 0;

	if (textUtf8 == nullptr || textLen == 0)
	{
		return FALSE;
	}
	wideTextLen = CasterProtocol::utf8ToWide(textUtf8, textLen, wideText,
		CASTER_CHAT_TEXT_BYTES);
	if (wideTextLen > CASTER_CHAT_TEXT_BYTES)
	{
		wideTextLen = CASTER_CHAT_TEXT_BYTES;
	}
	if (wideTextLen == 0)
	{
		return FALSE;
	}
	wideText[wideTextLen] = 0;

	if (senderNameUtf8 != nullptr && senderNameLen != 0)
	{
		wideNameLen = CasterProtocol::utf8ToWide(senderNameUtf8, senderNameLen, wideName,
			CHAT_NAME_WIDE_CHARS);
		if (wideNameLen > CHAT_NAME_WIDE_CHARS)
		{
			wideNameLen = CHAT_NAME_WIDE_CHARS;
		}
	}
	wideName[wideNameLen] = 0;

	out = ChatMessage();
	out.senderIdentity = senderIdentity;
	out.senderSlot = senderSlot;
	out.senderTeam = senderTeam;
	out.isCaster = isCaster;
	out.scope = direction;
	out.messageId = messageId;
	out.text = UnicodeString(wideText);
	if (wideNameLen != 0)
	{
		out.displayName = UnicodeString(wideName);
	}
	out.isEmote = isEmote;
	return TRUE;
}

Bool MakeCasterChatMessage(ChatMessage& out, const CasterInbound& event)
{
	return MakeCasterChatMessage(out, event.direction, event.senderSlot,
		(event.isSenderCaster != 0) ? TRUE : FALSE, event.senderTeam, event.senderIdentity,
		event.commandID, event.senderName, event.senderNameLen, event.text, event.textLen,
		(event.isEmote != 0) ? TRUE : FALSE);
}

Bool MakeCasterChatMessage(ChatMessage& out, const CasterLobby::LineQueue::Line& line)
{
	return MakeCasterChatMessage(out, (UnsignedByte)CasterProtocol::CHAT_LOBBY,
		line.senderSlot, line.senderIsCaster, 0, 0, 0, line.senderName, line.senderNameLen,
		line.text, line.textLen, line.isEmote);
}

Bool ChatColorFromIndex(Int colorIndex, Color& out)
{
	MultiplayerColorDefinition* def;

	if (TheMultiplayerSettings == nullptr)
	{
		return FALSE;
	}
	def = TheMultiplayerSettings->getColor(colorIndex);
	if (def == nullptr)
	{
		return FALSE;
	}
	out = def->getColor();
	return TRUE;
}

void RenderChatMessage(const ChatMessage& message, Color fallbackColor, UnicodeString& line,
	Color& color)
{
	UnicodeString text = message.text;

	if (TheLanguageFilter != nullptr)
	{
		TheLanguageFilter->filterLine(text);
	}

	if (message.isEmote)
	{
		// Stock "/me " convention: "Name text", no brackets.
		line = L"";
	}
	else
	{
		line = L"[";
	}
	if (!message.displayName.isEmpty())
	{
		line.concat(message.displayName);
	}
	else
	{
		line.concat(message.isCaster ? L"Caster" : L"Player");
	}
	line.concat(message.isEmote ? L" " : L"] ");
	line.concat(text);

	if (message.isCaster)
	{
		color = GameMakeColor(255, 0, 255, 255);
	}
	else if (message.hasColor)
	{
		color = message.color;
	}
	else
	{
		color = fallbackColor;
	}
}
