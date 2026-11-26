/*
**	Command & Conquer Generals Zero Hour(tm)
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

// Protocol reference: GenOnline/docs/local-api.md
// Live match stats for out-of-process readers. The caster plugin (settings.client.json plugins.caster) owns the named pipe
// (\\.\pipe\genonline-caster-<pid>), its framing and its per reader queues, and the game encodes and decodes every frame body.
// Without the plugin, in headless runs and in replay-simulation processes there is no sink.
//
// Up to LOCAL_API_MAX_READERS readers connect. A slow reader loses its own frames and never stalls the game or another reader.
// Outbound frame envelope: u32 little-endian body length + u8 type + u32 sequence + u64 timestamp in microseconds (see LocalApi.proto).
// The sequence counts the frames queued for that reader and a dropped frame still uses its number, so readers see gaps.
// Inbound frame envelope: u32 little-endian body length + u8 type. Inbound types: 0x10 ChatSend, 0x11 Request (rate limited by
// the plugin and answered on the main thread), other types are skipped, and a bad length drops the reader.
#pragma once

#include "Lib/BaseType.h"
#include <windows.h>
#include <string>

enum LocalApiFrameType
{
	LOCAL_API_FRAME_MATCH_INFO     = 0x01,
	LOCAL_API_FRAME_STATS_SNAPSHOT = 0x02,
	LOCAL_API_FRAME_MATCH_SUMMARY  = 0x03,
	LOCAL_API_FRAME_CHAT_MESSAGE   = 0x04,
	LOCAL_API_FRAME_UI_STATE       = 0x05,
	LOCAL_API_FRAME_LOBBY_STATE    = 0x06,
	LOCAL_API_FRAME_EVENTS         = 0x07,
	LOCAL_API_FRAME_CAMERA         = 0x08,
	LOCAL_API_FRAME_HELLO          = 0x09,
	LOCAL_API_FRAME_RESPONSE       = 0x0A,
	LOCAL_API_FRAME_VIEW_STATS     = 0x0B,
	LOCAL_API_FRAME_INTEREST_MAP   = 0x0C,
};

const UnsignedByte LOCAL_API_INBOUND_CHAT_SEND = 0x10;
const UnsignedByte LOCAL_API_INBOUND_REQUEST = 0x11;

/// Wire protocol version announced in the Hello.
const UnsignedInt LOCAL_API_PROTOCOL_VERSION = 2;

enum { LOCAL_API_MAX_READERS = 4 };

/// Outbound envelope: u32 body length + u8 type + u32 sequence + u64 timestamp_us.
const UnsignedInt LOCAL_API_OUT_HEADER_BYTES = 17;

/// Largest inbound frame body. A length of 0 or above this drops the reader.
const UnsignedInt LOCAL_API_MAX_INBOUND_BODY_BYTES = 4096;

/// Request limits per reader: a token bucket with this sustained rate and burst, and this many requests waiting for the main thread.
const UnsignedInt LOCAL_API_MAX_REQUESTS_PER_SECOND = 20;
const UnsignedInt LOCAL_API_REQUEST_BURST = 40;
const UnsignedInt LOCAL_API_REQUEST_QUEUE_SLOTS = 16;

/// Outbound queue capacity per reader.
const UnsignedInt LOCAL_API_QUEUE_CAPACITY_BYTES = 1048576;

/// Largest frame body that is queued. Bigger messages are dropped by the producer.
const UnsignedInt LOCAL_API_MAX_BODY_BYTES = 262144;

/// Queue space only the non-periodic frames (MatchInfo, MatchSummary, UiState, LobbyState, ChatMessage, Response) may use.
const UnsignedInt LOCAL_API_PERIODIC_RESERVE_BYTES = 65536;

const UnsignedInt LOCAL_API_MAX_PAGE_SIZE = 200;

/// Microseconds on the local API clock (QueryPerformanceCounter based, starts near process start), monotonic and callable from any thread.
UnsignedInt64 LocalApiNowMicros();

/// Wall clock in milliseconds since the Unix epoch at LocalApiNowMicros() == 0.
UnsignedInt64 LocalApiEpochUnixMs();

/// One queued ChatSend request. Channel and audience are the LocalApi.proto enum values.
struct LocalApiChatSend
{
	int channel;
	int audience;
	std::string text;	///< UTF-8
};

/// What one reader gets. It is set with a FeedConfigQuery and the defaults apply to every new connection.
struct LocalApiFeedConfig
{
	UnsignedInt lightIntervalMs;	///< 0 = no light snapshots
	UnsignedInt fullIntervalMs;		///< 0 = no full snapshots
	Bool events;
	Bool camera;
	Bool chat;
	Bool grids;
	Bool supply;
	Bool tech;
	Bool production;
	UnsignedInt gridResolution;		///< 8, 16, 32 or 64 cells per side
	UnsignedInt viewStatsIntervalMs;	///< ViewStats cadence in wall-clock time, 0 = none
	Bool interest;					///< InterestMap frames
	UnsignedInt interestIntervalMs;	///< InterestMap cadence in wall-clock time
	UnsignedInt notableCost;		///< ViewStats: units at least this expensive are listed (heroes always), 0 = heroes only
	UnsignedInt tradeWindowMs;		///< ViewStats: rolling window of killed and lost value, game time
	UnsignedInt intensityWindowMs;	///< ViewStats: rolling window of damage per second, game time

	LocalApiFeedConfig() { setDefaults(); }

	void setDefaults()
	{
		lightIntervalMs = 100;
		fullIntervalMs = 500;
		events = TRUE;
		camera = TRUE;
		chat = TRUE;
		grids = TRUE;
		supply = TRUE;
		tech = TRUE;
		production = TRUE;
		gridResolution = 32;
		viewStatsIntervalMs = 250;
		interest = FALSE;
		interestIntervalMs = 500;
		notableCost = 2000;
		tradeWindowMs = 15000;
		intensityWindowMs = 5000;
	}

	/// The part of the config that decides what a full snapshot contains. Readers with the same signature share one build.
	UnsignedInt fullSignature() const
	{
		return (grids ? 1u : 0u) | (supply ? 2u : 0u) | (tech ? 4u : 0u) | (production ? 8u : 0u) | (gridResolution << 4);
	}
};

/// A set of reader slots, each with the connection epoch it was seen at. The epoch of a slot changes when a new reader connects
/// to it and when the reader is resynchronized, so "has this site already sent X to this reader" is an epoch comparison, and
/// a frame addressed to an epoch that is gone is dropped by the sink instead of reaching the next reader of that slot.
class LocalApiReaders
{
public:
	LocalApiReaders() { clear(); }

	void clear()
	{
		for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
		{
			m_epoch[i] = 0;
		}
	}
	Bool empty() const
	{
		for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0)
			{
				return FALSE;
			}
		}
		return TRUE;
	}
	Bool has(Int slot) const { return slot >= 0 && slot < LOCAL_API_MAX_READERS && m_epoch[slot] != 0; }
	UnsignedInt epoch(Int slot) const { return (slot >= 0 && slot < LOCAL_API_MAX_READERS) ? m_epoch[slot] : 0; }
	void add(Int slot, UnsignedInt epoch)
	{
		if (slot >= 0 && slot < LOCAL_API_MAX_READERS)
		{
			m_epoch[slot] = epoch;
		}
	}
	void remove(Int slot)
	{
		if (slot >= 0 && slot < LOCAL_API_MAX_READERS)
		{
			m_epoch[slot] = 0;
		}
	}
	void merge(const LocalApiReaders& other)
	{
		for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
		{
			if (other.m_epoch[i] != 0)
			{
				m_epoch[i] = other.m_epoch[i];
			}
		}
	}
	/// The readers here that `sent` does not hold at the same epoch.
	LocalApiReaders minus(const LocalApiReaders& sent) const
	{
		LocalApiReaders result;
		for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0 && m_epoch[i] != sent.m_epoch[i])
			{
				result.m_epoch[i] = m_epoch[i];
			}
		}
		return result;
	}
	/// The readers that both sets hold at the same epoch.
	LocalApiReaders both(const LocalApiReaders& other) const
	{
		LocalApiReaders result;
		for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0 && m_epoch[i] == other.m_epoch[i])
			{
				result.m_epoch[i] = m_epoch[i];
			}
		}
		return result;
	}

private:
	UnsignedInt m_epoch[LOCAL_API_MAX_READERS];
};

/// One Request waiting for the main thread. `from` holds exactly the reader it came from, with the epoch it was received at.
struct LocalApiQueuedRequest
{
	LocalApiReaders from;
	std::string body;	///< the Request message, still encoded
};

/// The seam between the sampler (builds messages) and the transport that carries them.
class LocalApiSink
{
public:
	virtual ~LocalApiSink() {}

	virtual Bool isConnected() const = 0;

	/// The readers that got their Hello, with their current epochs. Any thread.
	virtual void getReaders(LocalApiReaders& out) const = 0;

	/// The readers that connected and still wait for their Hello. Main thread.
	virtual void getUnwelcomedReaders(LocalApiReaders& out) const = 0;

	/// Main thread: the Hello of these readers has been queued, so they now receive the other frame types.
	virtual void markWelcomed(const LocalApiReaders& readers) = 0;

	/// Queues one already-encoded frame body for every reader in `to` that is still connected at the epoch `to` holds, the
	/// rest are skipped. Only the Hello may go to a reader that was not welcomed yet. Never blocks. A frame that does not fit
	/// in the queue of a reader is dropped for that reader only (its sequence number is used up, so the reader sees the gap). Callable
	/// from any thread. Periodic frames (snapshots, events, camera) leave the last LOCAL_API_PERIODIC_RESERVE_BYTES of a
	/// queue free. A lifecycle frame that still finds no room marks that reader for a resync. TRUE if at least one reader got it.
	virtual Bool enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const LocalApiReaders& to) = 0;

	/// Main thread: gives every reader that lost a lifecycle frame and whose queue has room again a new epoch, so the
	/// frames that were dropped are sent again.
	virtual void takeResyncRequests() = 0;

	/// Main thread: starts a resync of one reader (a new epoch). Ignored when the epoch is not current.
	virtual void bumpEpoch(Int slot, UnsignedInt epoch) = 0;

	/// The feed config of the reader, with defaults for an unknown slot.
	virtual void getFeedConfig(Int slot, LocalApiFeedConfig& out) const = 0;

	/// Main thread: replaces the config of one reader, ignored when the epoch is not current.
	virtual void setFeedConfig(Int slot, UnsignedInt epoch, const LocalApiFeedConfig& config) = 0;

	/// Main thread: pops the oldest pending ChatSend request of any reader, FALSE if none.
	virtual Bool takeChatSend(LocalApiChatSend& out) = 0;

	/// Main thread: pops the oldest pending Request, taking the readers in turn. FALSE if none.
	virtual Bool takeRequest(LocalApiQueuedRequest& out) = 0;
};

/// The sink over the caster plugin, or NULL in headless runs, without the plugin or when its listener could not start. Starts the listener on first use.
LocalApiSink* LocalApiReadersGetSink();

/// Starts the listener. Cheap and idempotent, and lets a reader connect from the shell.
void LocalApiReadersStart();

/// Detaches the sink from the plugin before the plugin unloads, a no-op if it never started.
void LocalApiReadersShutdown();
