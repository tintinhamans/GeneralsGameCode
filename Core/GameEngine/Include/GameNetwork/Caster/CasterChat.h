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
#include "GameNetwork/Caster/CasterProtocol.h"

namespace CasterChat
{

enum Audience
{
	AUDIENCE_NONE      = 0x00,
	AUDIENCE_PLAYERS   = 0x01,
	AUDIENCE_CASTERS = 0x02,
	AUDIENCE_LOBBY     = 0x04,
};

/// Where one received chat line goes on this instance (see routeDelivery).
enum Delivery
{
	DELIVER_NONE       = 0x00,
	DELIVER_LOBBY_LINE = 0x01,	///< the lobby / read-only room chat box
	DELIVER_IN_GAME    = 0x02,	///< the in-game message feed
	DELIVER_RELAY      = 0x04,	///< forward to subscribed casters
};

static const UnsignedInt DEDUP_CAPACITY = 64;

UnsignedByte classifyDirection(UnsignedInt playerMask, UnsignedInt activePlayerMask);

UnsignedInt routeRecipients(UnsignedByte direction, Bool isSenderCaster);

Bool filterDrops(UnsignedByte direction, Bool filterEnabled);

/// The single scope policy for a received chat line: a Delivery mask. Lobby chat
/// always reaches the lobby box (players also relay it); other scopes follow
/// routeRecipients, and the privacy filter drops player team chat.
UnsignedInt routeDelivery(UnsignedByte direction, Bool isSenderCaster, Bool localCaster,
	Bool privacyFilter);

/// Stable sender identity for caster chat. The CasterUid is shared with
/// every host link, unlike a server-local connection index.
UnsignedInt identityFromUid(const char* uid);

class Dedup
{
public:
	Dedup();

	void reset();

	Bool accept(UnsignedInt senderIdentity, UnsignedByte isSenderCaster, UnsignedInt commandID);

private:
	UnsignedInt m_senderIdentities[DEDUP_CAPACITY];
	UnsignedByte m_senderCasters[DEDUP_CAPACITY];
	UnsignedInt m_commandIds[DEDUP_CAPACITY];
	UnsignedInt m_count;
	UnsignedInt m_next;
};

}
