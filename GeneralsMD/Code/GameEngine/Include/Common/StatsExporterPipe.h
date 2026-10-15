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

// Protocol reference: docs/stats-exporter.md
// Client-owned, byte-mode Win32 named pipe (\\.\pipe\generalsonline-stats-<pid>) that streams live match
// stats to out-of-process readers. Always on, except in headless runs and replay-simulation processes, where no thread and no pipe are created.
//
// The game is the pipe SERVER (overlapped I/O on one worker thread); the readers are clients, up to STATS_EXPORTER_MAX_READERS at once.
// Every reader has its own outbound queue, so a slow reader loses its own frames and never stalls the game or another reader.
// Enqueue calls never block the caller and drop the NEW frame when that reader's queue is full.
// Outbound frame envelope: u32 little-endian body length + u8 type + u32 sequence + u64 timestamp in microseconds (see StatsExporter.proto).
// The sequence counts the frames queued for that reader and a dropped frame still uses its number, so readers see gaps.
// Inbound frame envelope: u32 little-endian body length + u8 type. Inbound types: 0x10 ChatSend (parsed on the worker thread into a
// per-reader mailbox), 0x11 Request (rate limited, queued per reader and answered on the main thread); other types are skipped,
// and a bad length drops the reader.
#pragma once

#include "Lib/BaseType.h"
#include <windows.h>
#include <string>

enum StatsExporterFrameType
{
	STATS_EXPORTER_FRAME_MATCH_INFO     = 0x01,
	STATS_EXPORTER_FRAME_STATS_SNAPSHOT = 0x02,
	STATS_EXPORTER_FRAME_MATCH_SUMMARY  = 0x03,
	STATS_EXPORTER_FRAME_CHAT_MESSAGE   = 0x04,
	STATS_EXPORTER_FRAME_UI_STATE       = 0x05,
	STATS_EXPORTER_FRAME_LOBBY_STATE    = 0x06,
	STATS_EXPORTER_FRAME_EVENTS         = 0x07,
	STATS_EXPORTER_FRAME_CAMERA         = 0x08,
	STATS_EXPORTER_FRAME_HELLO          = 0x09,
	STATS_EXPORTER_FRAME_RESPONSE       = 0x0A,
};

/// Inbound frame types.
const UnsignedByte STATS_EXPORTER_INBOUND_CHAT_SEND = 0x10;
const UnsignedByte STATS_EXPORTER_INBOUND_REQUEST = 0x11;

/// Wire protocol version announced in the Hello.
const UnsignedInt STATS_EXPORTER_PROTOCOL_VERSION = 2;

/// Readers served at the same time.
enum { STATS_EXPORTER_MAX_READERS = 4 };

/// Outbound envelope: u32 body length + u8 type + u32 sequence + u64 timestamp_us.
const UnsignedInt STATS_EXPORTER_OUT_HEADER_BYTES = 17;

/// Largest inbound frame body; a length of 0 or above this drops the reader.
const UnsignedInt STATS_EXPORTER_MAX_INBOUND_BODY_BYTES = 4096;

/// Request limits per reader: a token bucket with this sustained rate and burst, and this many requests waiting for the main thread.
const UnsignedInt STATS_EXPORTER_MAX_REQUESTS_PER_SECOND = 20;
const UnsignedInt STATS_EXPORTER_REQUEST_BURST = 40;
const UnsignedInt STATS_EXPORTER_REQUEST_QUEUE_SLOTS = 16;

/// Outbound queue capacity per reader.
const UnsignedInt STATS_EXPORTER_QUEUE_CAPACITY_BYTES = 1048576;

/// Largest frame body that is queued; bigger messages are dropped by the producer.
const UnsignedInt STATS_EXPORTER_MAX_BODY_BYTES = 262144;

/// Queue space only the non-periodic frames (MatchInfo, MatchSummary, UiState, LobbyState, ChatMessage, Response) may use.
const UnsignedInt STATS_EXPORTER_PERIODIC_RESERVE_BYTES = 65536;

/// Largest page a paged query returns.
const UnsignedInt STATS_EXPORTER_MAX_PAGE_SIZE = 200;

/// Microseconds on the exporter clock (QueryPerformanceCounter based, starts near process start); monotonic. Any thread.
UnsignedInt64 StatsExporterNowMicros();

/// Wall clock in milliseconds since the Unix epoch at StatsExporterNowMicros() == 0.
UnsignedInt64 StatsExporterEpochUnixMs();

/// One queued ChatSend request; channel and audience are the StatsExporter.proto enum values.
struct StatsExporterChatSend
{
	int channel;
	int audience;
	std::string text;	///< UTF-8
};

/// What one reader gets; set with a FeedConfigQuery, the defaults apply to every new connection.
struct StatsExporterFeedConfig
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

	StatsExporterFeedConfig() { setDefaults(); }

	void setDefaults()
	{
		lightIntervalMs = 100;
		fullIntervalMs = 250;
		events = TRUE;
		camera = TRUE;
		chat = TRUE;
		grids = TRUE;
		supply = TRUE;
		tech = TRUE;
		production = TRUE;
		gridResolution = 32;
	}

	/// The part of the config that decides what a full snapshot contains; readers with the same signature share one build.
	UnsignedInt fullSignature() const
	{
		return (grids ? 1u : 0u) | (supply ? 2u : 0u) | (tech ? 4u : 0u) | (production ? 8u : 0u) | (gridResolution << 4);
	}
};

/// A set of reader slots, each with the connection epoch it was seen at. A slot's epoch changes when a new reader connects
/// to it and when the reader is resynchronized, so "has this site already sent X to this reader" is an epoch comparison, and
/// a frame addressed to an epoch that is gone is dropped by the sink instead of reaching the next reader of that slot.
class StatsExporterReaders
{
public:
	StatsExporterReaders() { clear(); }

	void clear()
	{
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			m_epoch[i] = 0;
		}
	}
	Bool empty() const
	{
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0)
			{
				return FALSE;
			}
		}
		return TRUE;
	}
	Bool has(Int slot) const { return slot >= 0 && slot < STATS_EXPORTER_MAX_READERS && m_epoch[slot] != 0; }
	UnsignedInt epoch(Int slot) const { return (slot >= 0 && slot < STATS_EXPORTER_MAX_READERS) ? m_epoch[slot] : 0; }
	void add(Int slot, UnsignedInt epoch)
	{
		if (slot >= 0 && slot < STATS_EXPORTER_MAX_READERS)
		{
			m_epoch[slot] = epoch;
		}
	}
	void remove(Int slot)
	{
		if (slot >= 0 && slot < STATS_EXPORTER_MAX_READERS)
		{
			m_epoch[slot] = 0;
		}
	}
	void merge(const StatsExporterReaders& other)
	{
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			if (other.m_epoch[i] != 0)
			{
				m_epoch[i] = other.m_epoch[i];
			}
		}
	}
	/// The readers here that `sent` does not hold at the same epoch.
	StatsExporterReaders minus(const StatsExporterReaders& sent) const
	{
		StatsExporterReaders result;
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0 && m_epoch[i] != sent.m_epoch[i])
			{
				result.m_epoch[i] = m_epoch[i];
			}
		}
		return result;
	}
	/// The readers that both sets hold at the same epoch.
	StatsExporterReaders both(const StatsExporterReaders& other) const
	{
		StatsExporterReaders result;
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			if (m_epoch[i] != 0 && m_epoch[i] == other.m_epoch[i])
			{
				result.m_epoch[i] = m_epoch[i];
			}
		}
		return result;
	}

private:
	UnsignedInt m_epoch[STATS_EXPORTER_MAX_READERS];
};

/// One Request waiting for the main thread. `from` holds exactly the reader it came from, with the epoch it was received at.
struct StatsExporterQueuedRequest
{
	StatsExporterReaders from;
	std::string body;	///< the Request message, still encoded
};

/// The seam between the sampler (builds messages) and the transport that carries them.
class StatsExporterSink
{
public:
	virtual ~StatsExporterSink() {}

	/// TRUE while at least one reader is connected.
	virtual Bool isConnected() const = 0;

	/// The readers that got their Hello, with their current epochs. Any thread.
	virtual void getReaders(StatsExporterReaders& out) const = 0;

	/// The readers that connected and still wait for their Hello. Main thread.
	virtual void getUnwelcomedReaders(StatsExporterReaders& out) const = 0;

	/// Main thread: the Hello of these readers has been queued; they now receive the other frame types.
	virtual void markWelcomed(const StatsExporterReaders& readers) = 0;

	/// Queues one already-encoded frame body for every reader in `to` that is still connected at the epoch `to` holds; the
	/// rest are skipped. Only the Hello may go to a reader that was not welcomed yet. Never blocks; a frame that does not fit
	/// a reader's queue is dropped for that reader only (its sequence number is used up, so the reader sees the gap). Callable
	/// from any thread. Periodic frames (snapshots, events, camera) leave the last STATS_EXPORTER_PERIODIC_RESERVE_BYTES of a
	/// queue free; a lifecycle frame that still finds no room marks that reader for a resync. TRUE if at least one reader got it.
	virtual Bool enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const StatsExporterReaders& to) = 0;

	/// Main thread: gives every reader that lost a lifecycle frame and whose queue has room again a new epoch, so the
	/// frames that were dropped are sent again.
	virtual void takeResyncRequests() = 0;

	/// Main thread: starts a resync of one reader (a new epoch). Ignored when the epoch is not current.
	virtual void bumpEpoch(Int slot, UnsignedInt epoch) = 0;

	/// The reader's feed config; defaults for an unknown slot.
	virtual void getFeedConfig(Int slot, StatsExporterFeedConfig& out) const = 0;

	/// Main thread: replaces the config of one reader; ignored when the epoch is not current.
	virtual void setFeedConfig(Int slot, UnsignedInt epoch, const StatsExporterFeedConfig& config) = 0;

	/// Main thread: pops the oldest pending ChatSend request of any reader, FALSE if none.
	virtual Bool takeChatSend(StatsExporterChatSend& out) = 0;

	/// Main thread: pops the oldest pending Request, taking the readers in turn; FALSE if none.
	virtual Bool takeRequest(StatsExporterQueuedRequest& out) = 0;
};

/// The sink, or NULL in headless runs or when the listener could not be created. Starts the listener on first use.
StatsExporterSink* StatsExporterPipeGetSink();

/// Starts the listener; cheap and idempotent. Lets a reader connect from the shell.
void StatsExporterPipeStart();

/// Stops the listener thread; a no-op if it never started.
void StatsExporterPipeShutdown();

class StatsExporterPipeServer : public StatsExporterSink
{
public:
	static StatsExporterPipeServer& TheInstance();

	/// Starts the pipe listener thread on first call; a cheap no-op after that.
	void ensureStarted();
	Bool isRunning() const { return m_thread != NULL; }
	Bool isStopped() const { return m_stopped != 0; }

	virtual Bool isConnected() const;
	virtual void getReaders(StatsExporterReaders& out) const;
	virtual void getUnwelcomedReaders(StatsExporterReaders& out) const;
	virtual void markWelcomed(const StatsExporterReaders& readers);
	virtual Bool enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const StatsExporterReaders& to);
	virtual void takeResyncRequests();
	virtual void bumpEpoch(Int slot, UnsignedInt epoch);
	virtual void getFeedConfig(Int slot, StatsExporterFeedConfig& out) const;
	virtual void setFeedConfig(Int slot, UnsignedInt epoch, const StatsExporterFeedConfig& config);
	virtual Bool takeChatSend(StatsExporterChatSend& out);
	virtual Bool takeRequest(StatsExporterQueuedRequest& out);

	/// Stops the worker thread and closes the pipe. Safe to call more than once and when never started.
	void shutdown();

	virtual ~StatsExporterPipeServer();

private:
	StatsExporterPipeServer();
	StatsExporterPipeServer(const StatsExporterPipeServer&);
	StatsExporterPipeServer& operator=(const StatsExporterPipeServer&);

	/// A bounded ring buffer of whole frames: a frame that does not fit is dropped, never split.
	class FrameQueue
	{
	public:
		FrameQueue();
		~FrameQueue();

		/// `reserve` bytes of the capacity stay free for the caller's priority class: the frame is dropped if it would eat into them.
		Bool push(const UnsignedByte* header, UnsignedInt headerLength, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt reserve);
		UnsignedInt freeBytes() const;
		UnsignedInt peek(UnsignedByte* dest, UnsignedInt capacity) const;
		void consume(UnsignedInt length);
		void clear();

	private:
		Bool ensureBuffer();
		void write(UnsignedInt offset, const UnsignedByte* data, UnsignedInt length);

		UnsignedByte* m_buffer;
		UnsignedInt m_head;
		UnsignedInt m_count;

		FrameQueue(const FrameQueue&);
		FrameQueue& operator=(const FrameQueue&);
	};

	enum { CHAT_MAILBOX_SLOTS = 8, SEND_CHUNK_BYTES = 8192 };

	enum SlotState
	{
		SLOT_CLOSED,		///< no pipe instance
		SLOT_LISTENING,		///< instance created, waiting for a reader
		SLOT_CONNECTED
	};

	/// One reader's pipe instance. The I/O members belong to the worker thread; the rest is guarded by m_lock.
	struct ReaderSlot
	{
		ReaderSlot();

		// --- worker thread only ---
		SlotState state;
		HANDLE pipe;
		HANDLE connectEvent;
		HANDLE readEvent;
		HANDLE writeEvent;
		OVERLAPPED connectOverlapped;
		OVERLAPPED readOverlapped;
		OVERLAPPED writeOverlapped;
		Bool readPending;
		Bool writePending;
		UnsignedInt inboundLen;
		UnsignedByte inbound[5 + STATS_EXPORTER_MAX_INBOUND_BODY_BYTES];
		UnsignedByte sendBuffer[SEND_CHUNK_BYTES];
		ULONGLONG retryAtMs;

		// --- guarded by m_lock ---
		Bool connected;
		Bool welcomed;
		Bool resyncNeeded;		///< a lifecycle frame was dropped on the current connection
		UnsignedInt epoch;		///< 0 while not connected
		UnsignedInt nextSequence;
		FrameQueue queue;
		StatsExporterFeedConfig feed;
		std::string requests[STATS_EXPORTER_REQUEST_QUEUE_SLOTS];
		UnsignedInt requestHead;
		UnsignedInt requestCount;
		double requestTokens;
		ULONGLONG requestRefillMs;
		StatsExporterChatSend chats[CHAT_MAILBOX_SLOTS];
		UnsignedInt chatHead;
		UnsignedInt chatCount;
	};

	static unsigned __stdcall workerProcThunk(void* param);
	void workerProc();

	// worker thread: pipe instance life cycle
	/// 0 = could not create the instance, 1 = waiting for a reader, 2 = a reader is already connected.
	Int startListening(ReaderSlot& slot, DWORD* createError);
	void onConnected(Int index);
	void closeSlot(Int index);

	// worker thread: I/O
	Bool beginRead(ReaderSlot& slot);
	Bool beginWrite(Int index);
	void pumpWrites();
	Bool processInboundBytes(Int index, UnsignedInt bytesTransferred);
	void handleInboundFrame(Int index, UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength);
	void handleRequestFrame(Int index, const UnsignedByte* body, UnsignedInt bodyLength);

	/// Queues a frame on one reader; the caller holds m_lock.
	Bool pushLocked(ReaderSlot& slot, UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt64 timestampMicros);
	/// Queues a Response without a result (a refusal) from the worker thread.
	void queueRefusal(Int index, UnsignedInt requestId, int status, const char* message);

	HANDLE m_thread;
	HANDLE m_wakeEvent;			///< signalled on enqueue and on shutdown
	HANDLE m_shutdownEvent;
	volatile LONG m_started;
	volatile LONG m_stopped;	///< set by shutdown(), never cleared: no restart during engine teardown
	volatile LONG m_shuttingDown;
	volatile LONG m_connectedCount;
	UnsignedInt m_epochCounter;	///< guarded by m_lock
	UnsignedInt m_chatNext;		///< round robin cursors, main thread
	UnsignedInt m_requestNext;
	mutable CRITICAL_SECTION m_lock;
	ReaderSlot m_slots[STATS_EXPORTER_MAX_READERS];
};
