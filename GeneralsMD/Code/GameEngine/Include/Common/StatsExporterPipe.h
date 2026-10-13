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
// stats to an out-of-process reader. Always on, except in headless runs and replay-simulation processes, where no thread and no pipe are created.
//
// The game is the pipe SERVER (overlapped ConnectNamedPipe on a worker thread); the reader is the client.
// Enqueue calls never block the caller and drop the NEW frame when the outbound queue is full.
// Frame envelope, both directions: u32 little-endian body length + u8 type (see StatsExporter.proto).
// Stats are on while a reader is connected. The only inbound frame is 0x10 ChatSend, which is parsed
// on the worker thread into a small mailbox; other inbound types are skipped, and a bad length drops the reader.
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
};

/// Inbound frame type.
const UnsignedByte STATS_EXPORTER_INBOUND_CHAT_SEND = 0x10;

/// Largest inbound frame body; a length of 0 or above this drops the reader.
const UnsignedInt STATS_EXPORTER_MAX_INBOUND_BODY_BYTES = 4096;

/// One queued ChatSend request; channel and audience are the StatsExporter.proto enum values.
struct StatsExporterChatSend
{
	int channel;
	int audience;
	std::string text;	///< UTF-8
};

/// Largest frame body that is queued; bigger messages are dropped by the producer.
const UnsignedInt STATS_EXPORTER_MAX_BODY_BYTES = 262144;

/// The seam between the sampler (builds messages) and the transport that carries them.
class StatsExporterSink
{
public:
	virtual ~StatsExporterSink() {}

	virtual Bool isConnected() const = 0;

	/// Increments once per completed reader handshake; 0 before the first.
	virtual UnsignedInt getConnectionGeneration() const = 0;

	/// Queues one already-encoded frame body. Never blocks; a dropped frame is not an error. Callable from any thread.
	virtual void enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength) = 0;

	/// Main thread: pops the oldest pending ChatSend request, FALSE if none.
	virtual Bool takeChatSend(StatsExporterChatSend& out) = 0;
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
	virtual UnsignedInt getConnectionGeneration() const { return (UnsignedInt)m_connectionGeneration; }
	virtual void enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength);
	virtual Bool takeChatSend(StatsExporterChatSend& out);

	/// Stops the worker thread and closes the pipe. Safe to call more than once and when never started.
	void shutdown();

	virtual ~StatsExporterPipeServer();

private:
	StatsExporterPipeServer();
	StatsExporterPipeServer(const StatsExporterPipeServer&);
	StatsExporterPipeServer& operator=(const StatsExporterPipeServer&);

	static unsigned __stdcall workerProcThunk(void* param);
	void workerProc();
	Bool openPipeInstance(DWORD* createError);
	void closePipeInstance();
	Bool flushQueue();

	// Keeps one overlapped read outstanding: it carries ChatSend frames and detects a reader that went away while nothing is written.
	Bool beginRead();
	void cancelPendingRead();

	/// Appends freshly-read bytes and dispatches every whole frame. FALSE only on an unrecoverable
	/// framing error (length 0 or too large); an unknown type or a decode failure just skips that frame.
	Bool processInboundBytes(UnsignedInt bytesTransferred);
	void handleInboundFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength);

	/// A bounded ring buffer of whole frames: a frame that does not fit is dropped, never split.
	class FrameQueue
	{
	public:
		FrameQueue();
		~FrameQueue();

		Bool push(const UnsignedByte* header, UnsignedInt headerLength, const UnsignedByte* body, UnsignedInt bodyLength);
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

	HANDLE m_pipe;
	HANDLE m_thread;
	HANDLE m_wakeEvent;			///< signalled on enqueue and on shutdown
	HANDLE m_shutdownEvent;
	volatile LONG m_started;
	volatile LONG m_stopped;	///< set by shutdown(), never cleared: no restart during engine teardown
	volatile LONG m_shuttingDown;
	volatile LONG m_connected;
	volatile LONG m_connectionGeneration;
	CRITICAL_SECTION m_queueLock;
	FrameQueue m_queue;

	// --- inbound read state (worker thread only) ---
	HANDLE m_readEvent;
	OVERLAPPED m_readOverlapped;
	Bool m_readPending;
	UnsignedByte m_inboundBuffer[5 + STATS_EXPORTER_MAX_INBOUND_BODY_BYTES];
	UnsignedInt m_inboundLen;

	// --- ChatSend mailbox: filled by the worker thread, drained by the main thread; full means drop ---
	enum { CHAT_MAILBOX_SLOTS = 8 };
	CRITICAL_SECTION m_chatLock;
	StatsExporterChatSend m_chatSlots[CHAT_MAILBOX_SLOTS];
	UnsignedInt m_chatHead;
	UnsignedInt m_chatCount;
};
