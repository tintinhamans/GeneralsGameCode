/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// The named pipe server of the local API readers (GenOnline/docs/local-api.md): framing, per reader queues and the inbound rate
// limit. It never reads frame bodies, LocalApiReaders.cpp encodes and decodes them.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace GenOnlineLocalApi
{
// The most readers a listener may serve, and the number of reader slots the table holds.
constexpr int kMaxReaders = 16;
// Outbound envelope: u32 body length, u8 type, u32 sequence and u64 timestamp in microseconds, all little endian.
constexpr uint32_t kOutboundHeaderBytes = 17;
// Inbound envelope: u32 body length and u8 type.
constexpr uint32_t kInboundHeaderBytes = 5;
// Largest inbound frame body. A length of 0 or above this drops the reader.
constexpr uint32_t kMaxInboundBodyBytes = 4096;
// Largest outbound frame body that is queued.
constexpr uint32_t kMaxBodyBytes = 262144;
// Outbound queue capacity of every reader.
constexpr uint32_t kQueueCapacityBytes = 1048576;
// Queue space that PERIODIC frames leave free for the other classes.
constexpr uint32_t kPeriodicReserveBytes = 65536;
// Request limits of every reader: a token bucket with this sustained rate and burst, and this many queued requests.
constexpr uint32_t kMaxRequestsPerSecond = 20;
constexpr uint32_t kRequestBurst = 40;
constexpr uint32_t kRequestQueueSlots = 16;
// Refused requests that wait for the game to answer them. Further refusals are dropped so a flood cannot grow memory.
constexpr uint32_t kRefusalSlots = 4;
// ChatSend frames that wait for the game. Further ones are dropped.
constexpr uint32_t kChatMailboxSlots = 8;

constexpr uint8_t kInboundChatSend = 0x10;
constexpr uint8_t kInboundRequest = 0x11;

// A reader id of 0 addresses every reader whose stream has started.
constexpr uint32_t kAllReaders = 0;
// Set on a request that arrived over the rate limit, so the game answers it with a refusal.
constexpr uint32_t kInboundRateLimited = 0x00000001;

enum class ReaderEvent : int32_t
{
	HelloNeeded = 0,
	Gone = 1,
};

enum class FrameClass : int32_t
{
	Hello = 0,
	Lifecycle = 1,
	Periodic = 2,
};

// Writes the kOutboundHeaderBytes envelope of one frame.
void WriteOutboundHeader(uint8_t* header, uint32_t bodyLength, uint8_t frameType, uint32_t sequence, uint64_t timestampMicros);

enum class InboundParse
{
	NeedMore,
	Frame,
	BadLength,
};

// Reads the inbound envelope at the start of buffer. Frame means the whole frame of kInboundHeaderBytes plus bodyLength is buffered.
InboundParse ParseInbound(const uint8_t* buffer, uint32_t length, uint8_t* frameType, uint32_t* bodyLength);

// A bounded ring buffer of whole outbound frames. A frame that does not fit is dropped and never split.
class FrameQueue
{
public:
	// Keeps reserve bytes of the capacity free, so the frame is dropped when it would use them.
	bool Push(const uint8_t* header, uint32_t headerLength, const uint8_t* body, uint32_t bodyLength, uint32_t reserve);
	uint32_t FreeBytes() const;
	uint32_t Peek(uint8_t* dest, uint32_t capacity) const;
	void Consume(uint32_t length);
	void Clear();

private:
	void Write(uint32_t offset, const uint8_t* data, uint32_t length);

	// Allocated on the first push, so a slot that never gets a reader costs no memory.
	std::unique_ptr<uint8_t[]> m_buffer;
	uint32_t m_head = 0;
	uint32_t m_count = 0;
};

// The request rate limit of one reader.
class TokenBucket
{
public:
	// Fills the bucket to the burst size.
	void Reset(uint64_t nowMs);
	// Refills for the time since the last call and spends one token when one is left and hasRoom is true.
	bool Allow(uint64_t nowMs, bool hasRoom);
	double Tokens() const { return m_tokens; }

private:
	double m_tokens = 0.0;
	uint64_t m_refillMs = 0;
};

// One inbound frame that TakeInbound hands out.
struct TakenInbound
{
	uint32_t readerId = 0;
	uint8_t frameType = 0;
	uint32_t flags = 0;
	std::unique_ptr<uint8_t[]> body;
	uint32_t bodyLength = 0;
};

// The state of the reader slots that the pipe worker and the game share: reader ids, events, outbound queues and inbound frames.
class ReaderTable
{
public:
	// A new reader in slot gets the next reader id, an empty queue and a full request bucket, and a HelloNeeded event. Returns the id.
	uint32_t OnConnected(int slot, uint64_t nowMs);
	// Ends the reader of slot and queues its Gone event.
	void OnClosed(int slot);
	// Queues one inbound frame of a reader whose stream has started. Requests past the rate limit are queued as refusals while there is room for them.
	void HandleInboundFrame(int slot, uint8_t frameType, const uint8_t* body, uint32_t bodyLength, uint64_t nowMs);
	uint32_t Peek(int slot, uint8_t* dest, uint32_t capacity) const;
	void Consume(int slot, uint32_t length);
	bool IsConnected() const;
	bool EnqueueFrame(uint8_t frameType, FrameClass frameClass, const void* body, uint32_t bodyLength, uint64_t timestampMicros, uint32_t readerId);
	bool TakeReaderEvent(uint32_t* readerId, ReaderEvent* event);
	bool TakeInbound(TakenInbound* out);
	// Free outbound queue bytes of slot.
	uint32_t FreeBytes(int slot) const;

private:
	enum class InboundKind
	{
		Request,
		Refusal,
		Chat,
	};

	struct InboundEntry
	{
		InboundKind kind = InboundKind::Request;
		uint8_t frameType = 0;
		std::unique_ptr<uint8_t[]> body;
		uint32_t bodyLength = 0;
	};

	struct Reader
	{
		bool connected = false;
		bool streamStarted = false;
		// The stream ended after a lost frame, and HelloNeeded waits for room in the queue.
		bool helloWanted = false;
		// Reader id, 0 while the slot is empty.
		uint32_t id = 0;
		uint32_t nextSequence = 0;
		FrameQueue queue;
		TokenBucket requests;
		std::deque<InboundEntry> inbound;
		uint32_t requestCount = 0;
		uint32_t refusalCount = 0;
		uint32_t chatCount = 0;
	};

	struct Event
	{
		uint32_t readerId;
		ReaderEvent event;
	};

	static bool ValidSlot(int slot) { return slot >= 0 && slot < kMaxReaders; }

	uint32_t NextIdLocked();
	void PushEventLocked(uint32_t readerId, ReaderEvent event);
	void PromoteHelloLocked();
	static void ClearInbound(Reader& reader);

	mutable std::mutex m_lock;
	Reader m_readers[kMaxReaders];
	std::deque<Event> m_events;
	uint32_t m_idCounter = 0;
	uint32_t m_inboundNext = 0;
};

// The pipe name of this process, \\.\pipe\genonline-caster-<pid>. Readers find the game by this name.
std::string PipeName();

// One worker thread with overlapped I/O on up to kMaxReaders pipe instances.
class PipeServer
{
public:
	explicit PipeServer(ReaderTable& table);
	~PipeServer();
	PipeServer(const PipeServer&) = delete;
	PipeServer& operator=(const PipeServer&) = delete;

	// Creates the events and the worker thread for up to maxReaders readers, clamped to 1..kMaxReaders. Returns true when the worker runs.
	bool Start(uint32_t maxReaders);
	// Stops the worker and closes every pipe instance. Safe to call more than once and without Start.
	void Stop();
	// Tells the worker that new frames are queued.
	void Wake();
	bool IsRunning() const { return m_thread != nullptr; }

private:
	enum { kSendChunkBytes = 8192 };

	enum class SlotState
	{
		Closed,
		Listening,
		Connected,
	};

	// The I/O state of one pipe instance. Only the worker thread touches it.
	struct Slot
	{
		SlotState state = SlotState::Closed;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		HANDLE connectEvent = nullptr;
		HANDLE readEvent = nullptr;
		HANDLE writeEvent = nullptr;
		OVERLAPPED connectOverlapped = {};
		OVERLAPPED readOverlapped = {};
		OVERLAPPED writeOverlapped = {};
		bool readPending = false;
		bool writePending = false;
		uint32_t inboundLength = 0;
		uint8_t inbound[kInboundHeaderBytes + kMaxInboundBodyBytes] = {};
		uint8_t sendBuffer[kSendChunkBytes] = {};
		uint64_t retryAtMs = 0;
	};

	static unsigned __stdcall WorkerThunk(void* param);
	void WorkerProc();
	// 0 when the instance could not be created, 1 while it waits for a reader, 2 when a reader is already connected.
	int StartListening(Slot& slot, DWORD* createError);
	void OnConnected(int index);
	void CloseSlot(int index);
	bool BeginRead(Slot& slot);
	bool BeginWrite(int index);
	void PumpWrites();
	bool ProcessInboundBytes(int index, uint32_t bytesTransferred);
	void CloseHandles();

	ReaderTable& m_table;
	HANDLE m_thread = nullptr;
	HANDLE m_wakeEvent = nullptr;
	HANDLE m_shutdownEvent = nullptr;
	std::atomic<bool> m_shuttingDown{false};
	// Heap allocated because every slot holds its inbound and send buffers.
	std::unique_ptr<Slot[]> m_slots;
	// Only the first m_maxReaders slots are used, and the pipe allows as many instances.
	int m_maxReaders = kMaxReaders;
};
} // namespace GenOnlineLocalApi
