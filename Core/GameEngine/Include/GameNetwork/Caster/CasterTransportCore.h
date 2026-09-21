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

// Bounded outbound queues and player-side protocol sessions.
// Queue overflow drops whole frames rather than stalling the game.
// CasterTransport serializes calls; HostServices owns retained-window locking.

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Caster/CasterProtocol.h"
#include "GameNetwork/Caster/CasterSubscribeTable.h"

namespace CasterTransportCore
{

/// Bound on one connection's pending outbound bytes. A slow or
/// dead caster fills this and then starts losing whole frames; the game's
/// main thread never blocks on a socket.
// A complete 64 KiB native batch expands into 73 framed fragments, so one
// queue must hold at least that batch without splitting or dropping its tail.
static const UnsignedInt OUTBOUND_CAPACITY_BYTES = 128 * 1024;

/// Bound on concurrent caster connections served by one player.
static const UnsignedInt MAX_CONNECTIONS = 16;

/// The "no id" sentinel for connection and source ids.
static const UnsignedInt INVALID_ID = 0xFFFFFFFF;

/**
 * A bounded byte buffer of whole frames. The shell's worker peeks, sends, then
 * consumes exactly what the socket accepted, so a partial send never loses
 * framing either.
 *
 * The buffer is allocated on first use and owned by the queue, so an inactive
 * connection costs nothing and the transport shell stays small enough to live
 * on the stack.
 */
class OutboundQueue
{
public:
	OutboundQueue();
	~OutboundQueue();

	/// Drops every pending frame but keeps the buffer for reuse.
	void clear();

	/**
	 * Appends one whole frame. FALSE when the frame was dropped: it was longer
	 * than `CasterProtocol::MAX_FRAME_BYTES`, or it did not fit the free
	 * space, or the buffer could not be allocated. A frame is never split, so a
	 * drop never corrupts the stream.
	 */
	Bool push(const UnsignedByte* frame, UnsignedInt length);
	Bool canPushBytes(UnsignedInt length) const;

	/// Copies up to `capacity` contiguous bytes from the head into `dest` and
	/// returns how many were copied; 0 when empty.
	UnsignedInt peek(UnsignedByte* dest, UnsignedInt capacity) const;

	/// Drops `length` bytes from the head after they were sent.
	void consume(UnsignedInt length);

	UnsignedInt size() const;			///< pending bytes

private:
	Bool ensureBuffer();

	UnsignedByte* m_buffer;
	UnsignedInt m_head;
	UnsignedInt m_count;

	OutboundQueue(const OutboundQueue&);
	OutboundQueue& operator=(const OutboundQueue&);
};

/// What one `onFrame` call did. Tests and callers branch on this.
enum HandleResult
{
	HANDLE_IGNORED  = 0,	///< not for this session / not applicable
	HANDLE_ACCEPTED = 1,	///< accepted, no reply emitted
	HANDLE_REPLIED  = 2,	///< accepted and at least one reply frame emitted
	HANDLE_REJECTED = 3,	///< malformed, wrong version, or not welcome
};

/**
 * Where a session hands frames back to the transport. `targetId` is a
 * connection id on the server and a source id on the caster; a sink bound to
 * a single link may ignore it. Returns FALSE when the frame was dropped by a
 * full bounded queue - a drop is not an error, it is the overflow contract.
 */
class FrameSink
{
public:
	virtual ~FrameSink();
	virtual Bool emit(UnsignedInt targetId, const UnsignedByte* frame, UnsignedInt length) = 0;
};

/// A `FrameSink` bound to one `OutboundQueue`; ignores `targetId`.
class QueueSink : public FrameSink
{
public:
	explicit QueueSink(OutboundQueue* queue);

	virtual Bool emit(UnsignedInt targetId, const UnsignedByte* frame, UnsignedInt length);

	void bind(OutboundQueue* queue);

private:
	OutboundQueue* m_queue;
};

/**
 * Where a session hands a received CHAT frame. Engine-free:
 * the shell implements it and pushes into the bounded main-thread queue, so a
 * worker never touches engine or UI state. `senderLinkId` is the connection
 * index on the player side and the source id on the caster side; a relaying
 * player uses it to address every caster but the sender.
 *
 * `commandID` is the sender's command id, carried so the receiver can drop the
 * same line when it arrives from more than one source.
 *
 * `text` is UTF-8 and points into the caller's frame buffer; it is valid only
 * for the duration of the call.
 */
class ChatSink
{
public:
	virtual ~ChatSink();
	virtual void onChat(UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction,
		UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam,
		UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen,
		const char* text, UnsignedInt textLen, UnsignedInt senderLinkId, UnsignedByte isEmote) = 0;

	/**
	 * A received LOBBY frame (the host's serialized lobby state, FRAME_LOBBY). Engine-free, like `onChat`: the shell parses and mirrors
	 * it on the main thread, so a worker never touches engine or UI state.
	 * `text` is UTF-8 and points into the caller's frame buffer.
	 */
	virtual void onLobbyState(UnsignedInt gameUid, const char* text, UnsignedInt textLen,
		UnsignedInt senderLinkId) = 0;
};

/**
 * The player-side facts a session needs, implemented by `TheCaster`. Every read
 * must be safe against the caller's worker thread.
 */
class HostServices
{
public:
	virtual ~HostServices();

	/// The game this player currently announces; 0 = not announced yet.
	virtual UnsignedInt currentGameUid() const = 0;

	/// TRUE once `gameUid`'s match is actually under way, so a subscriber can be
	/// promoted to streaming (HELLO_ACK / SUB_ACK rule).
	virtual Bool matchRunning(UnsignedInt gameUid) const = 0;

	/// The announced game name for `gameUid` (HELLO_ACK); "" when unknown.
	virtual const char* gameName(UnsignedInt gameUid) const = 0;
};

/**
 * One caster connection's player-side protocol session. The
 * session owns only its own subscription; the table is shared and passed in, so
 * the shell's single lock covers every concurrent connection.
 */
class ServerConnection
{
public:
	ServerConnection();
	~ServerConnection();

	/// Clears the session. Does not touch the table (see `onTeardown`).
	void reset();

	/**
	 * Consumes one already-decoded frame. May emit HELLO_ACK and SUB_ACK. `host`
	 * supplies the announced game's name and whether its match is running.
	 */
	HandleResult onFrame(const CasterProtocol::Frame& frame, HostServices& host,
		CasterSubscribeTable::Table& table, FrameSink& out);

	/**
	 * Connection teardown / game end: removes this connection's subscription
	 * (idempotent) and clears the session.
	 */
	void onTeardown(CasterSubscribeTable::Table& table);

	UnsignedInt subscribedGameUid() const;	///< 0 when not subscribed
	Bool isSubscribed() const;
	Bool isStreamingReady() const;
	Bool helloDone() const;

private:
	Bool m_helloDone;
	UnsignedInt m_subscribedGameUid;
	Bool m_streamingReady;
};

} // namespace CasterTransportCore
