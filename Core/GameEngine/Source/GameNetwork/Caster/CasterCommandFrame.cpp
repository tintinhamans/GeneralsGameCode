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

#include <string.h>
#include <vector>

#include "Common/MessageStream.h"
#include "GameNetwork/Caster/CasterCommandFrame.h"

namespace CasterCommandFrame
{

static Bool put(UnsignedByte*& cursor, UnsignedByte* end, const void* source,
	UnsignedInt bytes)
{
	if ((UnsignedInt)(end - cursor) < bytes)
	{
		return FALSE;
	}

	memcpy(cursor, source, bytes);
	cursor += bytes;
	return TRUE;
}

static Bool get(const UnsignedByte*& cursor, const UnsignedByte* end, void* destination,
	UnsignedInt bytes)
{
	if ((UnsignedInt)(end - cursor) < bytes)
	{
		return FALSE;
	}

	memcpy(destination, cursor, bytes);
	cursor += bytes;
	return TRUE;
}

static Bool isNetworkMessageType(UnsignedInt type)
{
	return type > (UnsignedInt)GameMessage::MSG_BEGIN_NETWORK_MESSAGES
		&& type < (UnsignedInt)GameMessage::MSG_END_NETWORK_MESSAGES;
}

UnsignedInt checksum(const UnsignedByte* data, UnsignedInt length)
{
	UnsignedInt value = 2166136261u;
	UnsignedInt index;

	for (index = 0; index < length; ++index)
	{
		value ^= data[index];
		value *= 16777619u;
	}

	return value;
}

UnsignedInt encode(UnsignedInt frame, GameMessage* first, UnsignedByte* out,
	UnsignedInt capacity)
{
	UnsignedByte* cursor;
	UnsignedByte* end;
	UnsignedInt count = 0;
	GameMessage* message;

	if (out == NULL || capacity > MAX_BYTES || capacity < 12)
	{
		return 0;
	}

	cursor = out;
	end = out + capacity;
	if (!put(cursor, end, &VERSION, sizeof(VERSION))
		|| !put(cursor, end, &frame, sizeof(frame)))
	{
		return 0;
	}

	// Patch this only after every message has been completely validated.
	cursor += sizeof(count);

	for (message = first; message != NULL; message = message->next())
	{
		UnsignedInt type = (UnsignedInt)message->getType();
		UnsignedInt player = (UnsignedInt)message->getPlayerIndex();
		UnsignedByte argumentCount = message->getArgumentCount();
		UnsignedByte argumentIndex;

		if (!isNetworkMessageType(type) || player >= MAX_PLAYER_COUNT
			|| count == MAX_MESSAGES || !put(cursor, end, &type, sizeof(type))
			|| !put(cursor, end, &player, sizeof(player))
			|| !put(cursor, end, &argumentCount, sizeof(argumentCount)))
		{
			return 0;
		}

		for (argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
		{
			GameMessageArgumentDataType kind = message->getArgumentDataType(argumentIndex);
			const GameMessageArgumentType* argument = message->getArgument(argumentIndex);
			GameMessageArgumentType normalized;
			UnsignedByte wireKind = (UnsignedByte)kind;

			if (argument == NULL || kind < ARGUMENTDATATYPE_INTEGER
				|| kind >= ARGUMENTDATATYPE_UNKNOWN)
			{
				return 0;
			}

			memset(&normalized, 0, sizeof(normalized));
			switch (kind)
			{
			case ARGUMENTDATATYPE_INTEGER:
				normalized.integer = argument->integer;
				break;
			case ARGUMENTDATATYPE_REAL:
				normalized.real = argument->real;
				break;
			case ARGUMENTDATATYPE_BOOLEAN:
				normalized.boolean = argument->boolean;
				break;
			case ARGUMENTDATATYPE_OBJECTID:
				normalized.objectID = argument->objectID;
				break;
			case ARGUMENTDATATYPE_DRAWABLEID:
				normalized.drawableID = argument->drawableID;
				break;
			case ARGUMENTDATATYPE_TEAMID:
				normalized.teamID = argument->teamID;
				break;
			case ARGUMENTDATATYPE_LOCATION:
				normalized.location = argument->location;
				break;
			case ARGUMENTDATATYPE_PIXEL:
				normalized.pixel = argument->pixel;
				break;
			case ARGUMENTDATATYPE_PIXELREGION:
				normalized.pixelRegion = argument->pixelRegion;
				break;
			case ARGUMENTDATATYPE_TIMESTAMP:
				normalized.timestamp = argument->timestamp;
				break;
			case ARGUMENTDATATYPE_WIDECHAR:
				normalized.wChar = argument->wChar;
				break;
			default:
				return 0;
			}

			if (!put(cursor, end, &wireKind, sizeof(wireKind))
				|| !put(cursor, end, &normalized, sizeof(normalized)))
			{
				return 0;
			}
		}

		++count;
	}

	memcpy(out + sizeof(VERSION) + sizeof(frame), &count, sizeof(count));
	return (UnsignedInt)(cursor - out);
}

Bool validate(const UnsignedByte* data, UnsignedInt length, UnsignedInt expectedFrame)
{
	const UnsignedByte* cursor;
	const UnsignedByte* end;
	UnsignedInt version;
	UnsignedInt frame;
	UnsignedInt count;
	UnsignedInt messageIndex;

	if (data == NULL || length < 12 || length > MAX_BYTES) return FALSE;
	cursor = data;
	end = data + length;
	if (!get(cursor, end, &version, sizeof(version))
		|| !get(cursor, end, &frame, sizeof(frame))
		|| !get(cursor, end, &count, sizeof(count)) || version != VERSION
		|| frame != expectedFrame || count > MAX_MESSAGES) return FALSE;
	for (messageIndex = 0; messageIndex < count; ++messageIndex)
	{
		UnsignedInt type;
		UnsignedInt player;
		UnsignedByte arguments;
		UnsignedByte argumentIndex;
		if (!get(cursor, end, &type, sizeof(type)) || !get(cursor, end, &player, sizeof(player))
			|| !get(cursor, end, &arguments, sizeof(arguments)) || !isNetworkMessageType(type)
			|| player >= MAX_PLAYER_COUNT) return FALSE;
		for (argumentIndex = 0; argumentIndex < arguments; ++argumentIndex)
		{
			UnsignedByte kind;
			GameMessageArgumentType ignored;
			if (!get(cursor, end, &kind, sizeof(kind)) || kind >= ARGUMENTDATATYPE_UNKNOWN
				|| !get(cursor, end, &ignored, sizeof(ignored))) return FALSE;
		}
	}
	return cursor == end;
}

Bool appendDecoded(const UnsignedByte* data, UnsignedInt length, UnsignedInt expectedFrame,
	CommandList* commands)
{
	const UnsignedByte* cursor;
	const UnsignedByte* end;
	UnsignedInt version;
	UnsignedInt frame;
	UnsignedInt count;
	UnsignedInt messageIndex;
	std::vector<GameMessage*> decoded;

	if (commands == NULL || !validate(data, length, expectedFrame))
	{
		return FALSE;
	}

	cursor = data;
	end = data + length;
	if (!get(cursor, end, &version, sizeof(version))
		|| !get(cursor, end, &frame, sizeof(frame))
		|| !get(cursor, end, &count, sizeof(count))
		|| version != VERSION || frame != expectedFrame || count > MAX_MESSAGES)
	{
		return FALSE;
	}

	for (messageIndex = 0; messageIndex < count; ++messageIndex)
	{
		UnsignedInt type;
		UnsignedInt player;
		UnsignedByte argumentCount;
		UnsignedByte argumentIndex;
		GameMessage* message;

		if (!get(cursor, end, &type, sizeof(type))
			|| !get(cursor, end, &player, sizeof(player))
			|| !get(cursor, end, &argumentCount, sizeof(argumentCount))
			|| !isNetworkMessageType(type) || player >= MAX_PLAYER_COUNT)
		{
			goto fail;
		}

		message = newInstance(GameMessage)((GameMessage::Type)type);
		if (message == NULL)
		{
			goto fail;
		}
		message->friend_setPlayerIndex((Int)player);

		for (argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
		{
			UnsignedByte wireKind;
			GameMessageArgumentType argument;

			if (!get(cursor, end, &wireKind, sizeof(wireKind))
				|| wireKind >= ARGUMENTDATATYPE_UNKNOWN
				|| !get(cursor, end, &argument, sizeof(argument)))
			{
				deleteInstance(message);
				goto fail;
			}

			switch ((GameMessageArgumentDataType)wireKind)
			{
			case ARGUMENTDATATYPE_INTEGER:
				message->appendIntegerArgument(argument.integer);
				break;
			case ARGUMENTDATATYPE_REAL:
				message->appendRealArgument(argument.real);
				break;
			case ARGUMENTDATATYPE_BOOLEAN:
				message->appendBooleanArgument(argument.boolean);
				break;
			case ARGUMENTDATATYPE_OBJECTID:
				message->appendObjectIDArgument(argument.objectID);
				break;
			case ARGUMENTDATATYPE_DRAWABLEID:
				message->appendDrawableIDArgument(argument.drawableID);
				break;
			case ARGUMENTDATATYPE_TEAMID:
				message->appendTeamIDArgument(argument.teamID);
				break;
			case ARGUMENTDATATYPE_LOCATION:
				message->appendLocationArgument(argument.location);
				break;
			case ARGUMENTDATATYPE_PIXEL:
				message->appendPixelArgument(argument.pixel);
				break;
			case ARGUMENTDATATYPE_PIXELREGION:
				message->appendPixelRegionArgument(argument.pixelRegion);
				break;
			case ARGUMENTDATATYPE_TIMESTAMP:
				message->appendTimestampArgument(argument.timestamp);
				break;
			case ARGUMENTDATATYPE_WIDECHAR:
				message->appendWideCharArgument(argument.wChar);
				break;
			default:
				deleteInstance(message);
				goto fail;
			}
		}

		decoded.push_back(message);
	}

	if (cursor != end)
	{
		goto fail;
	}

	// Do not publish a partial frame: append only after the full payload validates.
	for (messageIndex = 0; messageIndex < decoded.size(); ++messageIndex)
	{
		commands->appendMessage(decoded[messageIndex]);
	}
	return TRUE;

fail:
	for (messageIndex = 0; messageIndex < decoded.size(); ++messageIndex)
	{
		deleteInstance(decoded[messageIndex]);
	}
	return FALSE;
}

Assembler::Assembler()
{
	m_data = nullptr;
	m_received = nullptr;
	reset();
}

Assembler::~Assembler()
{
	delete [] m_data;
	delete [] m_received;
	m_data = nullptr;
	m_received = nullptr;
}

Bool Assembler::ensureBuffers()
{
	if (m_data == nullptr)
	{
		m_data = new UnsignedByte[MAX_BYTES];
		if (m_data == nullptr)
		{
			return FALSE;
		}
	}
	if (m_received == nullptr)
	{
		m_received = new UnsignedByte[MAX_BYTES];
		if (m_received == nullptr)
		{
			return FALSE;
		}
	}
	return TRUE;
}

void Assembler::reset()
{
	m_frame = 0;
	m_totalBytes = 0;
	m_expectedChecksum = 0;
	m_receivedBytes = 0;
	m_active = FALSE;
	m_bad = FALSE;
}

Bool Assembler::begin(UnsignedInt frame, UnsignedInt totalBytes, UnsignedInt expectedChecksum)
{
	reset();
	if (totalBytes == 0 || totalBytes > MAX_BYTES)
	{
		return FALSE;
	}
	if (!ensureBuffers())
	{
		return FALSE;
	}

	m_frame = frame;
	m_totalBytes = totalBytes;
	m_expectedChecksum = expectedChecksum;
	m_active = TRUE;
	memset(m_received, 0, totalBytes);
	return TRUE;
}

Bool Assembler::append(UnsignedInt offset, const UnsignedByte* fragment, UnsignedInt length)
{
	UnsignedInt index;

	if (!m_active || m_bad || m_data == nullptr || fragment == NULL || length == 0 || length > MAX_FRAGMENT_BYTES
		|| offset > m_totalBytes || length > m_totalBytes - offset)
	{
		return FALSE;
	}

	for (index = 0; index < length; ++index)
	{
		if (m_received[offset + index] && m_data[offset + index] != fragment[index])
		{
			m_bad = TRUE;
			return FALSE;
		}
	}

	for (index = 0; index < length; ++index)
	{
		if (!m_received[offset + index])
		{
			m_data[offset + index] = fragment[index];
			m_received[offset + index] = 1;
			++m_receivedBytes;
		}
	}

	return TRUE;
}

Bool Assembler::isComplete() const
{
	return m_active && !m_bad && m_data != nullptr && m_receivedBytes == m_totalBytes
		&& checksum(m_data, m_totalBytes) == m_expectedChecksum;
}

} // namespace CasterCommandFrame
