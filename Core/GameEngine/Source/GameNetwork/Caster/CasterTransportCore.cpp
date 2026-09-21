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

// See CasterTransportCore.h.

#include "GameNetwork/Caster/CasterTransportCore.h"
#include "GameNetwork/Caster/CasterCommandFrame.h"

namespace CasterTransportCore
{

using namespace CasterProtocol;
// OutboundQueue
OutboundQueue::OutboundQueue()
{
	m_buffer = nullptr;
	clear();
}

OutboundQueue::~OutboundQueue()
{
	delete[] m_buffer;
	m_buffer = nullptr;
}

Bool OutboundQueue::ensureBuffer()
{
	if (m_buffer != nullptr)
	{
		return TRUE;
	}
	m_buffer = new UnsignedByte[OUTBOUND_CAPACITY_BYTES];
	return (m_buffer != nullptr) ? TRUE : FALSE;
}

void OutboundQueue::clear()
{
	m_head = 0;
	m_count = 0;
}

Bool OutboundQueue::push(const UnsignedByte* frame, UnsignedInt length)
{
	UnsignedInt tail;
	UnsignedInt first;

	if (frame == nullptr || length == 0 || length > MAX_FRAME_BYTES)
	{
		// Not a frame the transport can ever put on the wire: drop.
		return FALSE;
	}
	if (length > OUTBOUND_CAPACITY_BYTES - m_count)
	{
		// Overflow contract: drop the WHOLE frame, never block, never split.
		return FALSE;
	}
	if (!ensureBuffer())
	{
		return FALSE;
	}

	tail = m_head + m_count;
	if (tail >= OUTBOUND_CAPACITY_BYTES)
	{
		tail -= OUTBOUND_CAPACITY_BYTES;
	}
	first = OUTBOUND_CAPACITY_BYTES - tail;
	if (first > length)
	{
		first = length;
	}
	memcpy(m_buffer + tail, frame, first);
	if (length > first)
	{
		memcpy(m_buffer, frame + first, length - first);
	}

	m_count += length;
	return TRUE;
}

Bool OutboundQueue::canPushBytes(UnsignedInt length) const
{
	return length <= OUTBOUND_CAPACITY_BYTES - m_count ? TRUE : FALSE;
}

UnsignedInt OutboundQueue::peek(UnsignedByte* dest, UnsignedInt capacity) const
{
	UnsignedInt contiguous;

	if (dest == nullptr || capacity == 0 || m_count == 0 || m_buffer == nullptr)
	{
		return 0;
	}
	contiguous = OUTBOUND_CAPACITY_BYTES - m_head;
	if (contiguous > m_count)
	{
		contiguous = m_count;
	}
	if (contiguous > capacity)
	{
		contiguous = capacity;
	}
	memcpy(dest, m_buffer + m_head, contiguous);
	return contiguous;
}

void OutboundQueue::consume(UnsignedInt length)
{
	if (length >= m_count)
	{
		// Everything was flushed (or the caller over-reported): rewind.
		clear();
		return;
	}
	m_head += length;
	if (m_head >= OUTBOUND_CAPACITY_BYTES)
	{
		m_head -= OUTBOUND_CAPACITY_BYTES;
	}
	m_count -= length;
}

UnsignedInt OutboundQueue::size() const
{
	return m_count;
}

// FrameSink / QueueSink
FrameSink::~FrameSink()
{
}

QueueSink::QueueSink(OutboundQueue* queue)
{
	m_queue = queue;
}

void QueueSink::bind(OutboundQueue* queue)
{
	m_queue = queue;
}

Bool QueueSink::emit(UnsignedInt targetId, const UnsignedByte* frame, UnsignedInt length)
{
	// A sink bound to one queue does not route: `targetId` is the caller's hint.
	(void)targetId;
	if (m_queue == nullptr)
	{
		return FALSE;
	}
	return m_queue->push(frame, length);
}
// ChatSink
ChatSink::~ChatSink()
{
}
// HostServices
HostServices::~HostServices()
{
}
// ServerConnection
ServerConnection::ServerConnection()
{
	reset();
}

ServerConnection::~ServerConnection()
{
}

void ServerConnection::reset()
{
	m_helloDone = FALSE;
	m_subscribedGameUid = 0;
	m_streamingReady = FALSE;
}

UnsignedInt ServerConnection::subscribedGameUid() const
{
	return m_subscribedGameUid;
}

Bool ServerConnection::isSubscribed() const
{
	return (m_subscribedGameUid != 0) ? TRUE : FALSE;
}

Bool ServerConnection::isStreamingReady() const
{
	return m_streamingReady;
}

Bool ServerConnection::helloDone() const
{
	return m_helloDone;
}

HandleResult ServerConnection::onFrame(const Frame& frame, HostServices& host,
	CasterSubscribeTable::Table& table, FrameSink& out)
{
	UnsignedByte reply[MAX_FRAME_BYTES];
	UnsignedInt replyLen;
	UnsignedInt announced;
	UnsignedInt state;

	switch (frame.type)
	{
	case FRAME_HELLO:
	{
		if (frame.protoVer != PROTOCOL_VERSION)
		{
			return HANDLE_REJECTED;
		}
		m_helloDone = TRUE;
		announced = host.currentGameUid();
		replyLen = encodeHelloAck(reply, sizeof(reply),
			(announced != 0) ? host.matchRunning(announced) : FALSE,
			(announced != 0) ? host.gameName(announced) : "");
		if (replyLen == 0)
		{
			return HANDLE_REJECTED;
		}
		out.emit(0, reply, replyLen);
		return HANDLE_REPLIED;
	}

	case FRAME_SUBSCRIBE:
	{
		Bool ok = FALSE;

		announced = host.currentGameUid();
		if (frame.gameUid != 0 && frame.gameUid == announced && frame.modeOrState == 0)
		{
			if (m_subscribedGameUid == frame.gameUid)
			{
				// Duplicate SUBSCRIBE: idempotent, the count must not grow.
				ok = TRUE;
			}
			else
			{
				if (m_subscribedGameUid != 0)
				{
					table.unsubscribe(m_subscribedGameUid);
					m_subscribedGameUid = 0;
					m_streamingReady = FALSE;
				}
				if (table.subscribe(frame.gameUid))
				{
					m_subscribedGameUid = frame.gameUid;
					ok = TRUE;
				}
			}
		}

		// The per-game table owns subscription counting; this link becomes ready
		// once the announced match is actually running, which is also when the
		// typed match-start bootstrap exists. A lobby-only announcement stays in
		// progress and is promoted by the refreshed HELLO_ACK plus re-subscribe.
		state = (ok && host.matchRunning(frame.gameUid))
			? CasterSubscribeTable::ACK_STREAMING_ELIGIBLE
			: CasterSubscribeTable::ACK_IN_PROGRESS;
		replyLen = encodeSubAck(reply, sizeof(reply), frame.gameUid, (UnsignedByte)state);
		if (replyLen == 0)
		{
			m_streamingReady = FALSE;
			return HANDLE_REJECTED;
		}
		// The transport must not send native frames before this exact SUB_ACK was
		// accepted by its bounded queue. A re-subscribe that remains in progress
		// also revokes an earlier ready state for this link.
		m_streamingReady = FALSE;
		if (!out.emit(0, reply, replyLen))
		{
			return HANDLE_ACCEPTED;
		}
		if (ok && state == CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			m_streamingReady = TRUE;
		}
		return HANDLE_REPLIED;
	}

	case FRAME_UNSUBSCRIBE:
		if (m_subscribedGameUid != 0 && frame.gameUid == m_subscribedGameUid)
		{
			table.unsubscribe(m_subscribedGameUid);
			m_subscribedGameUid = 0;
			m_streamingReady = FALSE;
		}
		// Duplicate UNSUBSCRIBE is an idempotent no-op; there is no ack frame.
		return HANDLE_ACCEPTED;

	case FRAME_CHAT:
		// The facade handles the decoded chat type.
		return HANDLE_ACCEPTED;

	default:
		return HANDLE_IGNORED;
	}
}

void ServerConnection::onTeardown(CasterSubscribeTable::Table& table)
{
	if (m_subscribedGameUid != 0)
	{
		table.unsubscribe(m_subscribedGameUid);
	}
	reset();
}

} // namespace CasterTransportCore
