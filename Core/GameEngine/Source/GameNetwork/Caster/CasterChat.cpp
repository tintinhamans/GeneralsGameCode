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

#include "GameNetwork/Caster/CasterChat.h"

using namespace CasterProtocol;

namespace CasterChat
{

UnsignedByte classifyDirection(UnsignedInt playerMask, UnsignedInt activePlayerMask)
{

	if (activePlayerMask == 0)
	{
		return CHAT_ALLIES;
	}

	if ((playerMask & activePlayerMask) == activePlayerMask)
	{
		return CHAT_EVERYONE;
	}
	return CHAT_ALLIES;
}

UnsignedInt routeRecipients(UnsignedByte direction, Bool isSenderCaster)
{

	if (direction == CHAT_LOBBY)
	{
		return AUDIENCE_LOBBY;
	}
	if (direction == CHAT_EVERYONE)
	{

		return isSenderCaster ? (AUDIENCE_PLAYERS | AUDIENCE_CASTERS) : AUDIENCE_CASTERS;
	}
	if (direction == CHAT_ALLIES)
	{

		return AUDIENCE_CASTERS;
	}
	return AUDIENCE_NONE;
}

Bool filterDrops(UnsignedByte direction, Bool filterEnabled)
{

	return filterEnabled && direction == CHAT_ALLIES;
}

UnsignedInt routeDelivery(UnsignedByte direction, Bool isSenderCaster, Bool localCaster,
	Bool privacyFilter)
{
	UnsignedInt audience;
	UnsignedInt delivery = DELIVER_NONE;

	if (direction == CHAT_LOBBY)
	{
		return localCaster ? (UnsignedInt)DELIVER_LOBBY_LINE
			: (UnsignedInt)(DELIVER_LOBBY_LINE | DELIVER_RELAY);
	}
	// CHAT_ALLIES is a private player-team route, but also the wire value used
	// by a caster's "Observers only" selector. Privacy filtering must not
	// suppress the latter.
	if (!isSenderCaster && filterDrops(direction, privacyFilter))
	{
		return DELIVER_NONE;
	}
	audience = routeRecipients(direction, isSenderCaster);
	if (localCaster)
	{
		if ((audience & AUDIENCE_CASTERS) != 0)
		{
			delivery |= DELIVER_IN_GAME;
		}
	}
	else
	{
		if ((audience & AUDIENCE_PLAYERS) != 0)
		{
			delivery |= DELIVER_IN_GAME;
		}
		if ((audience & AUDIENCE_CASTERS) != 0)
		{
			delivery |= DELIVER_RELAY;
		}
	}
	return delivery;
}

UnsignedInt identityFromUid(const char* uid)
{
	UnsignedInt hash = 2166136261U;
	if (uid == nullptr)
	{
		return hash;
	}
	while (*uid != '\0')
	{
		hash ^= (UnsignedByte)*uid++;
		hash *= 16777619U;
	}
	return hash;
}

Dedup::Dedup()
{
	reset();
}

void Dedup::reset()
{
	UnsignedInt i;
	for (i = 0; i < DEDUP_CAPACITY; ++i)
	{
		m_senderIdentities[i] = 0;
		m_senderCasters[i] = 0;
		m_commandIds[i] = 0;
	}
	m_count = 0;
	m_next = 0;
}

Bool Dedup::accept(UnsignedInt senderIdentity, UnsignedByte isSenderCaster, UnsignedInt commandID)
{
	UnsignedInt i;
	for (i = 0; i < m_count; ++i)
	{
		if (m_senderIdentities[i] == senderIdentity && m_senderCasters[i] == isSenderCaster && m_commandIds[i] == commandID)
		{
			return FALSE;
		}
	}

	m_senderIdentities[m_next] = senderIdentity;
	m_senderCasters[m_next] = isSenderCaster;
	m_commandIds[m_next] = commandID;
	m_next = (m_next + 1) % DEDUP_CAPACITY;
	if (m_count < DEDUP_CAPACITY)
	{
		++m_count;
	}
	return TRUE;
}

}
