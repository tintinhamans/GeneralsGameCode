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

#include "Common/StatsExporterPipe.h"

#include "Common/GlobalData.h"
#include "StatsExporter.pb.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// NetworkLog

#include <process.h>
#include <string.h>

namespace
{
	const UnsignedInt INBOUND_HEADER_BYTES = 5;

	const UnsignedInt STATS_PIPE_BUFFER_BYTES = 32768;
	const DWORD STATS_PIPE_RETRY_WAIT_MS = 1000;	///< wait before retrying a failed pipe creation
	const DWORD STATS_PIPE_IDLE_WAIT_MS = 250;	///< periodic wake so shutdown is never stuck longer than this

	volatile LONG s_statsPipeUsed = 0;

#ifndef PIPE_REJECT_REMOTE_CLIENTS
	#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

	// The exporter clock: QueryPerformanceCounter relative to its first use, plus the wall clock at that moment.
	struct ExporterClock
	{
		LARGE_INTEGER frequency;
		LARGE_INTEGER start;
		UnsignedInt64 epochUnixMs;

		ExporterClock()
		{
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&start);
			if (frequency.QuadPart <= 0)
			{
				frequency.QuadPart = 1000000;
			}

			FILETIME fileTime;
			GetSystemTimeAsFileTime(&fileTime);
			ULARGE_INTEGER hundredNanos;
			hundredNanos.LowPart = fileTime.dwLowDateTime;
			hundredNanos.HighPart = fileTime.dwHighDateTime;
			epochUnixMs = ((UnsignedInt64)hundredNanos.QuadPart - (UnsignedInt64)116444736000000000) / 10000;
		}
	};

	ExporterClock& exporterClock()
	{
		static ExporterClock s_clock;
		return s_clock;
	}

	void writeOutboundHeader(UnsignedByte* header, UnsignedInt bodyLength, UnsignedByte frameType, UnsignedInt sequence, UnsignedInt64 timestampMicros)
	{
		header[0] = (UnsignedByte)(bodyLength & 0xFF);
		header[1] = (UnsignedByte)((bodyLength >> 8) & 0xFF);
		header[2] = (UnsignedByte)((bodyLength >> 16) & 0xFF);
		header[3] = (UnsignedByte)((bodyLength >> 24) & 0xFF);
		header[4] = frameType;
		header[5] = (UnsignedByte)(sequence & 0xFF);
		header[6] = (UnsignedByte)((sequence >> 8) & 0xFF);
		header[7] = (UnsignedByte)((sequence >> 16) & 0xFF);
		header[8] = (UnsignedByte)((sequence >> 24) & 0xFF);
		for (Int i = 0; i < 8; ++i)
		{
			header[9 + i] = (UnsignedByte)((timestampMicros >> (8 * i)) & 0xFF);
		}
	}

	// Reads one protobuf varint; FALSE when the bytes end first.
	Bool readVarint(const UnsignedByte*& cursor, const UnsignedByte* end, UnsignedInt64& value)
	{
		value = 0;
		for (Int shift = 0; shift < 64 && cursor < end; shift += 7)
		{
			const UnsignedByte b = *cursor++;
			value |= (UnsignedInt64)(b & 0x7F) << shift;
			if ((b & 0x80) == 0)
			{
				return TRUE;
			}
		}
		return FALSE;
	}

	// The id of an encoded Request (field 1, a varint), 0 when it cannot be found. Lets a refusal name its request without a full decode.
	UnsignedInt extractRequestId(const UnsignedByte* body, UnsignedInt length)
	{
		const UnsignedByte* cursor = body;
		const UnsignedByte* end = body + length;

		while (cursor < end)
		{
			UnsignedInt64 tag;
			UnsignedInt64 value;

			if (!readVarint(cursor, end, tag))
			{
				return 0;
			}
			const UnsignedInt field = (UnsignedInt)(tag >> 3);
			const UnsignedInt wire = (UnsignedInt)(tag & 7);
			if (wire == 0)
			{
				if (!readVarint(cursor, end, value))
				{
					return 0;
				}
				if (field == 1)
				{
					return (UnsignedInt)value;
				}
			}
			else if (wire == 1)
			{
				cursor += 8;
			}
			else if (wire == 2)
			{
				if (!readVarint(cursor, end, value) || value > (UnsignedInt64)(end - cursor))
				{
					return 0;
				}
				cursor += (size_t)value;
			}
			else if (wire == 5)
			{
				cursor += 4;
			}
			else
			{
				return 0;
			}
		}
		return 0;
	}

	// A security descriptor whose only access control entry grants the current user full access to the pipe.
	struct PipeSecurity
	{
		SECURITY_ATTRIBUTES attributes;
		SECURITY_DESCRIPTOR descriptor;
		DWORD tokenBuffer[128];	// DWORD arrays keep the SID and the ACL aligned
		DWORD aclBuffer[128];
	};

	Bool buildPipeSecurity(PipeSecurity& security)
	{
		HANDLE token = NULL;
		DWORD needed = 0;
		BOOL haveUser;

		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			return FALSE;
		}
		haveUser = GetTokenInformation(token, TokenUser, security.tokenBuffer, sizeof(security.tokenBuffer), &needed);
		CloseHandle(token);
		if (!haveUser)
		{
			return FALSE;
		}

		PSID userSid = ((TOKEN_USER*)security.tokenBuffer)->User.Sid;
		ACL* acl = (ACL*)security.aclBuffer;
		const DWORD aclLength = (DWORD)((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(userSid) + 3) & ~3);
		if (aclLength > sizeof(security.aclBuffer))
		{
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return FALSE;
		}

		if (!InitializeAcl(acl, aclLength, ACL_REVISION)
			|| !AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, userSid)
			|| !InitializeSecurityDescriptor(&security.descriptor, SECURITY_DESCRIPTOR_REVISION)
			|| !SetSecurityDescriptorDacl(&security.descriptor, TRUE, acl, FALSE))
		{
			return FALSE;
		}

		security.attributes.nLength = sizeof(security.attributes);
		security.attributes.lpSecurityDescriptor = &security.descriptor;
		security.attributes.bInheritHandle = FALSE;
		return TRUE;
	}
}

UnsignedInt64 StatsExporterNowMicros()
{
	ExporterClock& clock = exporterClock();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	const UnsignedInt64 ticks = (UnsignedInt64)(now.QuadPart - clock.start.QuadPart);
	const UnsignedInt64 frequency = (UnsignedInt64)clock.frequency.QuadPart;
	return (ticks / frequency) * 1000000 + (ticks % frequency) * 1000000 / frequency;
}

UnsignedInt64 StatsExporterEpochUnixMs()
{
	return exporterClock().epochUnixMs;
}

// ---------------------------------------------------------------------------
// Pipe gate
// ---------------------------------------------------------------------------

StatsExporterSink* StatsExporterPipeGetSink()
{
	// A batch of replay simulations (headless) must not open pipes.
	if (TheGlobalData != NULL && (TheGlobalData->m_headless || !TheGlobalData->m_simulateReplays.empty()))
	{
		return NULL;
	}
	InterlockedExchange((LONG*)&s_statsPipeUsed, 1);
	StatsExporterPipeServer& server = StatsExporterPipeServer::TheInstance();
	server.ensureStarted();
	return (!server.isStopped() && server.isRunning()) ? &server : NULL;
}

void StatsExporterPipeStart()
{
	StatsExporterPipeGetSink();
}

void StatsExporterPipeShutdown()
{
	if (s_statsPipeUsed != 0)
	{
		StatsExporterPipeServer::TheInstance().shutdown();
	}
}

// ---------------------------------------------------------------------------
// StatsExporterPipeServer::FrameQueue
// ---------------------------------------------------------------------------

StatsExporterPipeServer::FrameQueue::FrameQueue()
	: m_buffer(NULL)
	, m_head(0)
	, m_count(0)
{
}

StatsExporterPipeServer::FrameQueue::~FrameQueue()
{
	delete[] m_buffer;
}

Bool StatsExporterPipeServer::FrameQueue::ensureBuffer()
{
	if (m_buffer == NULL)
	{
		m_buffer = new UnsignedByte[STATS_EXPORTER_QUEUE_CAPACITY_BYTES];
	}
	return m_buffer != NULL;
}

void StatsExporterPipeServer::FrameQueue::clear()
{
	m_head = 0;
	m_count = 0;
}

void StatsExporterPipeServer::FrameQueue::write(UnsignedInt offset, const UnsignedByte* data, UnsignedInt length)
{
	UnsignedInt tail = m_head + m_count + offset;
	UnsignedInt first;

	if (tail >= STATS_EXPORTER_QUEUE_CAPACITY_BYTES)
	{
		tail -= STATS_EXPORTER_QUEUE_CAPACITY_BYTES;
	}
	first = STATS_EXPORTER_QUEUE_CAPACITY_BYTES - tail;
	if (first > length)
	{
		first = length;
	}
	memcpy(m_buffer + tail, data, first);
	if (length > first)
	{
		memcpy(m_buffer, data + first, length - first);
	}
}

UnsignedInt StatsExporterPipeServer::FrameQueue::freeBytes() const
{
	return STATS_EXPORTER_QUEUE_CAPACITY_BYTES - m_count;
}

Bool StatsExporterPipeServer::FrameQueue::push(const UnsignedByte* header, UnsignedInt headerLength, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt reserve)
{
	const UnsignedInt length = headerLength + bodyLength;

	if (header == NULL || body == NULL || bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_BODY_BYTES)
	{
		return FALSE;
	}
	if (length + reserve > STATS_EXPORTER_QUEUE_CAPACITY_BYTES - m_count)
	{
		// Overflow contract: drop the new frame, never block, never split.
		return FALSE;
	}
	if (!ensureBuffer())
	{
		return FALSE;
	}

	write(0, header, headerLength);
	write(headerLength, body, bodyLength);
	m_count += length;
	return TRUE;
}

UnsignedInt StatsExporterPipeServer::FrameQueue::peek(UnsignedByte* dest, UnsignedInt capacity) const
{
	UnsignedInt available;
	UnsignedInt first;

	if (m_buffer == NULL || m_count == 0 || capacity == 0)
	{
		return 0;
	}

	available = m_count;
	if (available > capacity)
	{
		available = capacity;
	}

	first = STATS_EXPORTER_QUEUE_CAPACITY_BYTES - m_head;
	if (first > available)
	{
		first = available;
	}
	memcpy(dest, m_buffer + m_head, first);
	if (available > first)
	{
		memcpy(dest + first, m_buffer, available - first);
	}
	return available;
}

void StatsExporterPipeServer::FrameQueue::consume(UnsignedInt length)
{
	if (length > m_count)
	{
		length = m_count;
	}
	m_head += length;
	if (m_head >= STATS_EXPORTER_QUEUE_CAPACITY_BYTES)
	{
		m_head -= STATS_EXPORTER_QUEUE_CAPACITY_BYTES;
	}
	m_count -= length;
}

// ---------------------------------------------------------------------------
// StatsExporterPipeServer::ReaderSlot
// ---------------------------------------------------------------------------

StatsExporterPipeServer::ReaderSlot::ReaderSlot()
	: state(SLOT_CLOSED)
	, pipe(INVALID_HANDLE_VALUE)
	, connectEvent(NULL)
	, readEvent(NULL)
	, writeEvent(NULL)
	, readPending(FALSE)
	, writePending(FALSE)
	, inboundLen(0)
	, retryAtMs(0)
	, connected(FALSE)
	, welcomed(FALSE)
	, resyncNeeded(FALSE)
	, epoch(0)
	, nextSequence(0)
	, requestHead(0)
	, requestCount(0)
	, requestTokens((double)STATS_EXPORTER_REQUEST_BURST)
	, requestRefillMs(0)
	, chatHead(0)
	, chatCount(0)
{
	ZeroMemory(&connectOverlapped, sizeof(connectOverlapped));
	ZeroMemory(&readOverlapped, sizeof(readOverlapped));
	ZeroMemory(&writeOverlapped, sizeof(writeOverlapped));
}

// ---------------------------------------------------------------------------
// StatsExporterPipeServer
// ---------------------------------------------------------------------------

StatsExporterPipeServer& StatsExporterPipeServer::TheInstance()
{
	static StatsExporterPipeServer instance;
	return instance;
}

StatsExporterPipeServer::StatsExporterPipeServer()
	: m_thread(NULL)
	, m_wakeEvent(NULL)
	, m_shutdownEvent(NULL)
	, m_started(0)
	, m_stopped(0)
	, m_shuttingDown(0)
	, m_connectedCount(0)
	, m_epochCounter(0)
	, m_chatNext(0)
	, m_requestNext(0)
{
	exporterClock();	// the clock starts with the exporter
	InitializeCriticalSection(&m_lock);
}

StatsExporterPipeServer::~StatsExporterPipeServer()
{
	shutdown();
	DeleteCriticalSection(&m_lock);
}

void StatsExporterPipeServer::ensureStarted()
{
	// m_started only ever goes 0 -> 1 once, so the first caller to see 0 creates the thread; after shutdown it never starts again.
	if (m_stopped != 0)
	{
		return;
	}
	if (InterlockedExchange((LONG*)&m_started, 1) != 0)
	{
		return;
	}

	m_wakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_wakeEvent == NULL || m_shutdownEvent == NULL)
	{
		return;
	}
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		m_slots[i].connectEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		m_slots[i].readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		m_slots[i].writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (m_slots[i].connectEvent == NULL || m_slots[i].readEvent == NULL || m_slots[i].writeEvent == NULL)
		{
			return;
		}
	}

	m_thread = (HANDLE)_beginthreadex(NULL, 0, &StatsExporterPipeServer::workerProcThunk, this, 0, NULL);
}

Bool StatsExporterPipeServer::isConnected() const
{
	return m_connectedCount != 0;
}

void StatsExporterPipeServer::shutdown()
{
	InterlockedExchange((LONG*)&m_stopped, 1);
	InterlockedExchange((LONG*)&m_shuttingDown, 1);
	if (m_shutdownEvent != NULL)
	{
		SetEvent(m_shutdownEvent);
	}
	if (m_thread != NULL)
	{
		WaitForSingleObject(m_thread, 5000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		closeSlot(i);
		if (m_slots[i].connectEvent != NULL)
		{
			CloseHandle(m_slots[i].connectEvent);
			m_slots[i].connectEvent = NULL;
		}
		if (m_slots[i].readEvent != NULL)
		{
			CloseHandle(m_slots[i].readEvent);
			m_slots[i].readEvent = NULL;
		}
		if (m_slots[i].writeEvent != NULL)
		{
			CloseHandle(m_slots[i].writeEvent);
			m_slots[i].writeEvent = NULL;
		}
	}
	if (m_wakeEvent != NULL)
	{
		CloseHandle(m_wakeEvent);
		m_wakeEvent = NULL;
	}
	if (m_shutdownEvent != NULL)
	{
		CloseHandle(m_shutdownEvent);
		m_shutdownEvent = NULL;
	}
	m_shuttingDown = 0;
}

// ---------------------------------------------------------------------------
// Sink interface (main thread, or any thread where noted)
// ---------------------------------------------------------------------------

void StatsExporterPipeServer::getReaders(StatsExporterReaders& out) const
{
	out.clear();
	EnterCriticalSection(&m_lock);
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		if (m_slots[i].connected && m_slots[i].welcomed)
		{
			out.add(i, m_slots[i].epoch);
		}
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::getUnwelcomedReaders(StatsExporterReaders& out) const
{
	out.clear();
	EnterCriticalSection(&m_lock);
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		if (m_slots[i].connected && !m_slots[i].welcomed)
		{
			out.add(i, m_slots[i].epoch);
		}
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::markWelcomed(const StatsExporterReaders& readers)
{
	EnterCriticalSection(&m_lock);
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		if (readers.has(i) && m_slots[i].connected && m_slots[i].epoch == readers.epoch(i))
		{
			m_slots[i].welcomed = TRUE;
		}
	}
	LeaveCriticalSection(&m_lock);
}

Bool StatsExporterPipeServer::pushLocked(ReaderSlot& slot, UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt64 timestampMicros)
{
	UnsignedByte header[STATS_EXPORTER_OUT_HEADER_BYTES];
	const Bool periodic = (frameType == STATS_EXPORTER_FRAME_STATS_SNAPSHOT || frameType == STATS_EXPORTER_FRAME_EVENTS || frameType == STATS_EXPORTER_FRAME_CAMERA
		|| frameType == STATS_EXPORTER_FRAME_VIEW_STATS || frameType == STATS_EXPORTER_FRAME_INTEREST_MAP);
	// the one-shot frames a reader cannot do without; a dropped chat line or response is just lost
	const Bool lifecycle = (frameType == STATS_EXPORTER_FRAME_MATCH_INFO || frameType == STATS_EXPORTER_FRAME_MATCH_SUMMARY
		|| frameType == STATS_EXPORTER_FRAME_UI_STATE || frameType == STATS_EXPORTER_FRAME_LOBBY_STATE || frameType == STATS_EXPORTER_FRAME_HELLO);

	// The number is used up even when the frame is dropped, which is how the reader notices.
	writeOutboundHeader(header, bodyLength, frameType, slot.nextSequence++, timestampMicros);
	const Bool pushed = slot.queue.push(header, STATS_EXPORTER_OUT_HEADER_BYTES, body, bodyLength, periodic ? STATS_EXPORTER_PERIODIC_RESERVE_BYTES : 0);
	if (!pushed && lifecycle)
	{
		slot.resyncNeeded = TRUE;
	}
	return pushed;
}

Bool StatsExporterPipeServer::enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const StatsExporterReaders& to)
{
	Bool pushedAny = FALSE;

	if (body == NULL || bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_BODY_BYTES)
	{
		return FALSE;
	}

	ensureStarted();

	const UnsignedInt64 timestamp = StatsExporterNowMicros();

	// Connection state is checked under the same lock a new connection resets its queue under, so a frame produced
	// for an earlier reader of a slot can never land in the next reader's stream.
	EnterCriticalSection(&m_lock);
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		ReaderSlot& slot = m_slots[i];
		if (!to.has(i) || !slot.connected || slot.epoch != to.epoch(i))
		{
			continue;
		}
		if (!slot.welcomed && frameType != STATS_EXPORTER_FRAME_HELLO)
		{
			continue;
		}
		if (pushLocked(slot, frameType, body, bodyLength, timestamp))
		{
			pushedAny = TRUE;
		}
	}
	LeaveCriticalSection(&m_lock);

	if (pushedAny && m_wakeEvent != NULL)
	{
		SetEvent(m_wakeEvent);
	}
	return pushedAny;
}

void StatsExporterPipeServer::takeResyncRequests()
{
	EnterCriticalSection(&m_lock);
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		ReaderSlot& slot = m_slots[i];
		// Only once the reader has drained at least half its queue, so the frames that are sent again fit.
		if (slot.connected && slot.resyncNeeded && slot.queue.freeBytes() >= STATS_EXPORTER_QUEUE_CAPACITY_BYTES / 2)
		{
			slot.resyncNeeded = FALSE;
			if (++m_epochCounter == 0)
			{
				++m_epochCounter;
			}
			slot.epoch = m_epochCounter;
		}
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::bumpEpoch(Int slotIndex, UnsignedInt epoch)
{
	if (slotIndex < 0 || slotIndex >= STATS_EXPORTER_MAX_READERS)
	{
		return;
	}

	EnterCriticalSection(&m_lock);
	ReaderSlot& slot = m_slots[slotIndex];
	if (slot.connected && slot.epoch == epoch)
	{
		slot.resyncNeeded = FALSE;
		if (++m_epochCounter == 0)
		{
			++m_epochCounter;
		}
		slot.epoch = m_epochCounter;
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::getFeedConfig(Int slotIndex, StatsExporterFeedConfig& out) const
{
	out.setDefaults();
	if (slotIndex < 0 || slotIndex >= STATS_EXPORTER_MAX_READERS)
	{
		return;
	}

	EnterCriticalSection(&m_lock);
	if (m_slots[slotIndex].connected)
	{
		out = m_slots[slotIndex].feed;
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::setFeedConfig(Int slotIndex, UnsignedInt epoch, const StatsExporterFeedConfig& config)
{
	if (slotIndex < 0 || slotIndex >= STATS_EXPORTER_MAX_READERS)
	{
		return;
	}

	EnterCriticalSection(&m_lock);
	if (m_slots[slotIndex].connected && m_slots[slotIndex].epoch == epoch)
	{
		m_slots[slotIndex].feed = config;
	}
	LeaveCriticalSection(&m_lock);
}

Bool StatsExporterPipeServer::takeChatSend(StatsExporterChatSend& out)
{
	Bool has = FALSE;

	EnterCriticalSection(&m_lock);
	for (Int k = 0; k < STATS_EXPORTER_MAX_READERS && !has; ++k)
	{
		const Int i = (Int)((m_chatNext + k) % STATS_EXPORTER_MAX_READERS);
		ReaderSlot& slot = m_slots[i];
		if (slot.connected && slot.chatCount > 0)
		{
			out = slot.chats[slot.chatHead];
			slot.chatHead = (slot.chatHead + 1) % CHAT_MAILBOX_SLOTS;
			--slot.chatCount;
			m_chatNext = (UnsignedInt)((i + 1) % STATS_EXPORTER_MAX_READERS);
			has = TRUE;
		}
	}
	LeaveCriticalSection(&m_lock);
	return has;
}

Bool StatsExporterPipeServer::takeRequest(StatsExporterQueuedRequest& out)
{
	Bool has = FALSE;

	EnterCriticalSection(&m_lock);
	for (Int k = 0; k < STATS_EXPORTER_MAX_READERS && !has; ++k)
	{
		const Int i = (Int)((m_requestNext + k) % STATS_EXPORTER_MAX_READERS);
		ReaderSlot& slot = m_slots[i];
		if (slot.connected && slot.welcomed && slot.requestCount > 0)
		{
			out.from.clear();
			out.from.add(i, slot.epoch);
			out.body.swap(slot.requests[slot.requestHead]);
			slot.requestHead = (slot.requestHead + 1) % STATS_EXPORTER_REQUEST_QUEUE_SLOTS;
			--slot.requestCount;
			m_requestNext = (UnsignedInt)((i + 1) % STATS_EXPORTER_MAX_READERS);
			has = TRUE;
		}
	}
	LeaveCriticalSection(&m_lock);
	return has;
}

// ---------------------------------------------------------------------------
// Worker thread: inbound frames
// ---------------------------------------------------------------------------

void StatsExporterPipeServer::queueRefusal(Int index, UnsignedInt requestId, int status, const char* message)
{
	statsexporter::Response response;
	std::string body;

	response.set_id(requestId);
	response.set_status((statsexporter::ResponseStatus)status);
	response.set_message(message);
	if (!response.SerializeToString(&body) || body.empty())
	{
		return;
	}

	const UnsignedInt64 timestamp = StatsExporterNowMicros();
	EnterCriticalSection(&m_lock);
	ReaderSlot& slot = m_slots[index];
	if (slot.connected && slot.welcomed)
	{
		pushLocked(slot, STATS_EXPORTER_FRAME_RESPONSE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), timestamp);
	}
	LeaveCriticalSection(&m_lock);
}

void StatsExporterPipeServer::handleRequestFrame(Int index, const UnsignedByte* body, UnsignedInt bodyLength)
{
	ReaderSlot& slot = m_slots[index];
	const UnsignedInt requestId = extractRequestId(body, bodyLength);
	const ULONGLONG now = GetTickCount64();
	Bool welcomed;
	Bool refused = FALSE;

	// The worker only rate limits and queues the request; the main thread decodes and answers it.
	EnterCriticalSection(&m_lock);
	welcomed = slot.welcomed;
	if (welcomed)
	{
		const double refill = (double)(now - slot.requestRefillMs) * (double)STATS_EXPORTER_MAX_REQUESTS_PER_SECOND / 1000.0;
		slot.requestRefillMs = now;
		slot.requestTokens += refill;
		if (slot.requestTokens > (double)STATS_EXPORTER_REQUEST_BURST)
		{
			slot.requestTokens = (double)STATS_EXPORTER_REQUEST_BURST;
		}

		if (slot.requestTokens < 1.0 || slot.requestCount >= STATS_EXPORTER_REQUEST_QUEUE_SLOTS)
		{
			refused = TRUE;
		}
		else
		{
			slot.requestTokens -= 1.0;
			slot.requests[(slot.requestHead + slot.requestCount) % STATS_EXPORTER_REQUEST_QUEUE_SLOTS].assign((const char*)body, bodyLength);
			++slot.requestCount;
		}
	}
	LeaveCriticalSection(&m_lock);

	// A request that arrives before the Hello was queued is dropped: the reader has not seen the limits yet.
	if (refused)
	{
		queueRefusal(index, requestId, statsexporter::RESPONSE_STATUS_RATE_LIMITED, "too many requests");
	}
}

void StatsExporterPipeServer::handleInboundFrame(Int index, UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength)
{
	if (frameType == STATS_EXPORTER_INBOUND_REQUEST)
	{
		handleRequestFrame(index, body, bodyLength);
		return;
	}
	if (frameType != STATS_EXPORTER_INBOUND_CHAT_SEND)
	{
		return;	// unknown type: skipped, not an error
	}
	statsexporter::ChatSend chat;
	if (!chat.ParseFromArray(body, (int)bodyLength))
	{
		return;	// decode failure: skipped
	}

	// The worker only queues the request; the main thread decides what to do with it.
	ReaderSlot& slot = m_slots[index];
	EnterCriticalSection(&m_lock);
	if (slot.chatCount < CHAT_MAILBOX_SLOTS)
	{
		StatsExporterChatSend& entry = slot.chats[(slot.chatHead + slot.chatCount) % CHAT_MAILBOX_SLOTS];
		entry.channel = (int)chat.channel();
		entry.audience = (int)chat.audience();
		entry.text = chat.text();
		++slot.chatCount;
	}
	LeaveCriticalSection(&m_lock);
}

Bool StatsExporterPipeServer::processInboundBytes(Int index, UnsignedInt bytesTransferred)
{
	ReaderSlot& slot = m_slots[index];
	slot.inboundLen += bytesTransferred;

	for (;;)
	{
		UnsignedInt bodyLength;
		UnsignedInt frameLength;

		if (slot.inboundLen < INBOUND_HEADER_BYTES)
		{
			break;	// need more header bytes
		}

		bodyLength = (UnsignedInt)slot.inbound[0]
			| ((UnsignedInt)slot.inbound[1] << 8)
			| ((UnsignedInt)slot.inbound[2] << 16)
			| ((UnsignedInt)slot.inbound[3] << 24);
		if (bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_INBOUND_BODY_BYTES)
		{
			// Unrecoverable: a byte-mode pipe cannot resynchronize on a bad length, so the reader is dropped.
			return FALSE;
		}

		frameLength = INBOUND_HEADER_BYTES + bodyLength;
		if (slot.inboundLen < frameLength)
		{
			break;	// full frame not yet buffered
		}

		handleInboundFrame(index, slot.inbound[4], slot.inbound + INBOUND_HEADER_BYTES, bodyLength);

		slot.inboundLen -= frameLength;
		if (slot.inboundLen > 0)
		{
			memmove(slot.inbound, slot.inbound + frameLength, slot.inboundLen);
		}
	}
	return TRUE;
}

// ---------------------------------------------------------------------------
// Worker thread: pipe instances and I/O
// ---------------------------------------------------------------------------

unsigned __stdcall StatsExporterPipeServer::workerProcThunk(void* param)
{
	static_cast<StatsExporterPipeServer*>(param)->workerProc();
	return 0;
}

Int StatsExporterPipeServer::startListening(ReaderSlot& slot, DWORD* createError)
{
	char pipeName[64];
	BOOL connectedImmediately;
	DWORD lastError;

	*createError = 0;
	wsprintfA(pipeName, "\\\\.\\pipe\\generalsonline-stats-%lu", GetCurrentProcessId());

	// Only the user the game runs as may open the pipe, and never over the network. If that user cannot be determined the
	// pipe is not created at all rather than falling back to the default (wider) access.
	PipeSecurity security;
	if (!buildPipeSecurity(security))
	{
		*createError = GetLastError();
		return 0;
	}

	slot.pipe = CreateNamedPipeA(
		pipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		STATS_EXPORTER_MAX_READERS,
		STATS_PIPE_BUFFER_BYTES, STATS_PIPE_BUFFER_BYTES,
		0,
		&security.attributes);
	if (slot.pipe == INVALID_HANDLE_VALUE)
	{
		*createError = GetLastError();
		return 0;
	}

	ZeroMemory(&slot.connectOverlapped, sizeof(slot.connectOverlapped));
	slot.connectOverlapped.hEvent = slot.connectEvent;
	ResetEvent(slot.connectEvent);

	connectedImmediately = ConnectNamedPipe(slot.pipe, &slot.connectOverlapped);
	lastError = GetLastError();
	if (connectedImmediately || lastError == ERROR_PIPE_CONNECTED)
	{
		return 2;
	}
	if (lastError == ERROR_IO_PENDING)
	{
		return 1;
	}

	*createError = lastError;
	CloseHandle(slot.pipe);
	slot.pipe = INVALID_HANDLE_VALUE;
	return 0;
}

void StatsExporterPipeServer::onConnected(Int index)
{
	ReaderSlot& slot = m_slots[index];

	slot.state = SLOT_CONNECTED;
	slot.readPending = FALSE;
	slot.writePending = FALSE;
	slot.inboundLen = 0;

	// The reset and the new epoch happen together under the lock, so no frame produced for an earlier reader slips into this one's stream.
	EnterCriticalSection(&m_lock);
	slot.connected = TRUE;
	slot.welcomed = FALSE;
	slot.resyncNeeded = FALSE;
	if (++m_epochCounter == 0)
	{
		++m_epochCounter;
	}
	slot.epoch = m_epochCounter;
	slot.nextSequence = 0;
	slot.queue.clear();
	slot.feed.setDefaults();
	slot.requestHead = 0;
	slot.requestCount = 0;
	slot.requestTokens = (double)STATS_EXPORTER_REQUEST_BURST;
	slot.requestRefillMs = GetTickCount64();
	slot.chatHead = 0;
	slot.chatCount = 0;
	LeaveCriticalSection(&m_lock);
	InterlockedIncrement((LONG*)&m_connectedCount);

	if (!beginRead(slot))
	{
		closeSlot(index);
	}
}

void StatsExporterPipeServer::closeSlot(Int index)
{
	ReaderSlot& slot = m_slots[index];
	DWORD bytesTransferred;

	if (slot.state == SLOT_CLOSED && slot.pipe == INVALID_HANDLE_VALUE)
	{
		return;
	}

	// The worker is the only thread issuing I/O on a slot's pipe, so CancelIo cancels exactly these operations.
	if (slot.pipe != INVALID_HANDLE_VALUE && (slot.readPending || slot.writePending || slot.state == SLOT_LISTENING))
	{
		CancelIo(slot.pipe);
		if (slot.readPending)
		{
			GetOverlappedResult(slot.pipe, &slot.readOverlapped, &bytesTransferred, TRUE);
		}
		if (slot.writePending)
		{
			GetOverlappedResult(slot.pipe, &slot.writeOverlapped, &bytesTransferred, TRUE);
		}
		if (slot.state == SLOT_LISTENING)
		{
			GetOverlappedResult(slot.pipe, &slot.connectOverlapped, &bytesTransferred, TRUE);
		}
	}
	slot.readPending = FALSE;
	slot.writePending = FALSE;

	if (slot.pipe != INVALID_HANDLE_VALUE)
	{
		if (slot.state == SLOT_CONNECTED)
		{
			DisconnectNamedPipe(slot.pipe);
		}
		CloseHandle(slot.pipe);
		slot.pipe = INVALID_HANDLE_VALUE;
	}
	slot.inboundLen = 0;

	Bool wasConnected;
	EnterCriticalSection(&m_lock);
	wasConnected = slot.connected;
	slot.connected = FALSE;
	slot.welcomed = FALSE;
	slot.resyncNeeded = FALSE;
	slot.epoch = 0;
	slot.queue.clear();
	slot.requestCount = 0;
	slot.chatCount = 0;
	LeaveCriticalSection(&m_lock);
	if (wasConnected)
	{
		InterlockedDecrement((LONG*)&m_connectedCount);
	}

	slot.state = SLOT_CLOSED;
	slot.retryAtMs = 0;
}

Bool StatsExporterPipeServer::beginRead(ReaderSlot& slot)
{
	DWORD bytesRead = 0;

	ZeroMemory(&slot.readOverlapped, sizeof(slot.readOverlapped));
	slot.readOverlapped.hEvent = slot.readEvent;
	ResetEvent(slot.readEvent);

	if (slot.inboundLen >= sizeof(slot.inbound))
	{
		return FALSE;
	}

	slot.readPending = TRUE;
	if (!ReadFile(slot.pipe, slot.inbound + slot.inboundLen, (DWORD)(sizeof(slot.inbound) - slot.inboundLen), &bytesRead, &slot.readOverlapped))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			slot.readPending = FALSE;
			return FALSE;
		}
	}
	// Pending or already complete, readEvent is signalled either way on an overlapped handle.
	return TRUE;
}

Bool StatsExporterPipeServer::beginWrite(Int index)
{
	ReaderSlot& slot = m_slots[index];
	UnsignedInt available;

	if (slot.state != SLOT_CONNECTED || slot.writePending)
	{
		return TRUE;
	}

	EnterCriticalSection(&m_lock);
	available = slot.queue.peek(slot.sendBuffer, SEND_CHUNK_BYTES);
	LeaveCriticalSection(&m_lock);
	if (available == 0)
	{
		return TRUE;
	}

	ZeroMemory(&slot.writeOverlapped, sizeof(slot.writeOverlapped));
	slot.writeOverlapped.hEvent = slot.writeEvent;
	ResetEvent(slot.writeEvent);

	// Never waits for the reader: a reader that does not read leaves its write pending while the other readers carry on.
	if (!WriteFile(slot.pipe, slot.sendBuffer, available, NULL, &slot.writeOverlapped))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			return FALSE;
		}
	}
	slot.writePending = TRUE;
	return TRUE;
}

void StatsExporterPipeServer::pumpWrites()
{
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		if (m_slots[i].state == SLOT_CONNECTED && !beginWrite(i))
		{
			closeSlot(i);
		}
	}
}

void StatsExporterPipeServer::workerProc()
{
	Bool loggedCreateFailure = FALSE;

	for (;;)
	{
		HANDLE handles[2 + 3 * STATS_EXPORTER_MAX_READERS];
		Int handleSlot[2 + 3 * STATS_EXPORTER_MAX_READERS];
		Int handleKind[2 + 3 * STATS_EXPORTER_MAX_READERS];	// 0 shutdown, 1 wake, 2 connect, 3 read, 4 write
		DWORD count = 0;
		DWORD waitResult;

		if (m_shuttingDown)
		{
			break;
		}

		// Every free slot keeps an instance listening, so a reader can connect while the others are busy.
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			ReaderSlot& slot = m_slots[i];
			if (slot.state != SLOT_CLOSED || GetTickCount64() < slot.retryAtMs)
			{
				continue;
			}

			DWORD createError = 0;
			const Int started = startListening(slot, &createError);
			if (started == 0)
			{
				if (!loggedCreateFailure)
				{
					loggedCreateFailure = TRUE;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsExporter] Pipe creation failed, error %lu; retrying", (unsigned long)createError);
				}
				slot.retryAtMs = GetTickCount64() + STATS_PIPE_RETRY_WAIT_MS;
			}
			else if (started == 1)
			{
				slot.state = SLOT_LISTENING;
			}
			else
			{
				slot.state = SLOT_LISTENING;
				onConnected(i);
			}
		}

		handles[count] = m_shutdownEvent;
		handleSlot[count] = -1;
		handleKind[count++] = 0;
		handles[count] = m_wakeEvent;
		handleSlot[count] = -1;
		handleKind[count++] = 1;
		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			ReaderSlot& slot = m_slots[i];
			if (slot.state == SLOT_LISTENING)
			{
				handles[count] = slot.connectEvent;
				handleSlot[count] = i;
				handleKind[count++] = 2;
			}
			else if (slot.state == SLOT_CONNECTED)
			{
				if (slot.readPending)
				{
					handles[count] = slot.readEvent;
					handleSlot[count] = i;
					handleKind[count++] = 3;
				}
				if (slot.writePending)
				{
					handles[count] = slot.writeEvent;
					handleSlot[count] = i;
					handleKind[count++] = 4;
				}
			}
		}

		waitResult = WaitForMultipleObjects(count, handles, FALSE, STATS_PIPE_IDLE_WAIT_MS);
		if (waitResult == WAIT_FAILED)
		{
			break;
		}

		if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + count)
		{
			const DWORD which = waitResult - WAIT_OBJECT_0;
			const Int index = handleSlot[which];

			switch (handleKind[which])
			{
				case 0:
					goto finished;	// shutdown requested
				case 1:
					break;	// new frames are queued: pumped below
				case 2:
				{
					ReaderSlot& slot = m_slots[index];
					DWORD bytesTransferred = 0;
					if (GetOverlappedResult(slot.pipe, &slot.connectOverlapped, &bytesTransferred, FALSE))
					{
						onConnected(index);
					}
					else
					{
						closeSlot(index);	// the reader came and went before it was accepted; listen again
					}
					break;
				}
				case 3:
				{
					ReaderSlot& slot = m_slots[index];
					DWORD bytesTransferred = 0;
					const Bool readOk = GetOverlappedResult(slot.pipe, &slot.readOverlapped, &bytesTransferred, FALSE) ? TRUE : FALSE;
					slot.readPending = FALSE;
					// A failed or empty read means the reader is gone; a bad frame length drops it too.
					if (!readOk || bytesTransferred == 0 || !processInboundBytes(index, bytesTransferred) || !beginRead(slot))
					{
						closeSlot(index);
					}
					break;
				}
				case 4:
				{
					ReaderSlot& slot = m_slots[index];
					DWORD written = 0;
					if (!GetOverlappedResult(slot.pipe, &slot.writeOverlapped, &written, FALSE))
					{
						slot.writePending = FALSE;
						closeSlot(index);
						break;
					}
					slot.writePending = FALSE;
					EnterCriticalSection(&m_lock);
					slot.queue.consume((UnsignedInt)written);	// a short write leaves the rest queued: it is sent again by the pump below
					LeaveCriticalSection(&m_lock);
					break;
				}
			}
		}

		// Wake, completion or timeout: start the next write on every reader that is idle and has frames queued.
		pumpWrites();
	}

finished:
	for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
	{
		closeSlot(i);
	}
}
