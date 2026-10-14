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
	// Frame envelope: u32 little-endian body length + u8 type, shared by both directions.
	const UnsignedInt FRAME_HEADER_BYTES = 5;

	// Ring buffer capacity, allocated lazily on first push; well above the largest frame.
	const UnsignedInt STATS_QUEUE_CAPACITY_BYTES = 1048576;

	const UnsignedInt STATS_PIPE_BUFFER_BYTES = 32768;
	const DWORD STATS_PIPE_RETRY_WAIT_MS = 1000;	///< wait before retrying a failed pipe creation
	const DWORD STATS_PIPE_IDLE_WAIT_MS = 250;	///< periodic wake so shutdown is never stuck longer than this

	volatile LONG s_statsPipeUsed = 0;

#ifndef PIPE_REJECT_REMOTE_CLIENTS
	#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

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
		m_buffer = new UnsignedByte[STATS_QUEUE_CAPACITY_BYTES];
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

	if (tail >= STATS_QUEUE_CAPACITY_BYTES)
	{
		tail -= STATS_QUEUE_CAPACITY_BYTES;
	}
	first = STATS_QUEUE_CAPACITY_BYTES - tail;
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
	return STATS_QUEUE_CAPACITY_BYTES - m_count;
}

Bool StatsExporterPipeServer::FrameQueue::push(const UnsignedByte* header, UnsignedInt headerLength, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt reserve)
{
	const UnsignedInt length = headerLength + bodyLength;

	if (header == NULL || body == NULL || bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_BODY_BYTES)
	{
		return FALSE;
	}
	if (length + reserve > STATS_QUEUE_CAPACITY_BYTES - m_count)
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

	first = STATS_QUEUE_CAPACITY_BYTES - m_head;
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
	if (m_head >= STATS_QUEUE_CAPACITY_BYTES)
	{
		m_head -= STATS_QUEUE_CAPACITY_BYTES;
	}
	m_count -= length;
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
	: m_pipe(INVALID_HANDLE_VALUE)
	, m_thread(NULL)
	, m_wakeEvent(NULL)
	, m_shutdownEvent(NULL)
	, m_started(0)
	, m_stopped(0)
	, m_shuttingDown(0)
	, m_connected(0)
	, m_connectionGeneration(0)
	, m_resyncNeeded(0)
	, m_readEvent(NULL)
	, m_readPending(FALSE)
	, m_inboundLen(0)
	, m_chatHead(0)
	, m_chatCount(0)
{
	InitializeCriticalSection(&m_queueLock);
	InitializeCriticalSection(&m_chatLock);
	ZeroMemory(&m_readOverlapped, sizeof(m_readOverlapped));
}

StatsExporterPipeServer::~StatsExporterPipeServer()
{
	shutdown();
	DeleteCriticalSection(&m_chatLock);
	DeleteCriticalSection(&m_queueLock);
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
	m_readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_wakeEvent == NULL || m_shutdownEvent == NULL || m_readEvent == NULL)
	{
		return;
	}

	m_thread = (HANDLE)_beginthreadex(NULL, 0, &StatsExporterPipeServer::workerProcThunk, this, 0, NULL);
}

Bool StatsExporterPipeServer::isConnected() const
{
	return m_connected != 0;
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
	closePipeInstance();
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
	if (m_readEvent != NULL)
	{
		CloseHandle(m_readEvent);
		m_readEvent = NULL;
	}
	m_shuttingDown = 0;
}

Bool StatsExporterPipeServer::enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, UnsignedInt generation)
{
	UnsignedByte header[FRAME_HEADER_BYTES];
	Bool pushed = FALSE;
	const Bool periodic = (frameType == STATS_EXPORTER_FRAME_STATS_SNAPSHOT || frameType == STATS_EXPORTER_FRAME_EVENTS || frameType == STATS_EXPORTER_FRAME_CAMERA);
	// the one-shot frames a reader cannot do without; a dropped chat line is just lost
	const Bool lifecycle = (frameType == STATS_EXPORTER_FRAME_MATCH_INFO || frameType == STATS_EXPORTER_FRAME_MATCH_SUMMARY
		|| frameType == STATS_EXPORTER_FRAME_UI_STATE || frameType == STATS_EXPORTER_FRAME_LOBBY_STATE);

	if (body == NULL || bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_BODY_BYTES)
	{
		return FALSE;
	}

	header[0] = (UnsignedByte)(bodyLength & 0xFF);
	header[1] = (UnsignedByte)((bodyLength >> 8) & 0xFF);
	header[2] = (UnsignedByte)((bodyLength >> 16) & 0xFF);
	header[3] = (UnsignedByte)((bodyLength >> 24) & 0xFF);
	header[4] = frameType;

	ensureStarted();

	// The connection state is checked under the same lock that a new connection clears the queue under, so a frame produced
	// for the previous reader can never land in the new reader's stream.
	EnterCriticalSection(&m_queueLock);
	if (m_connected != 0 && generation == (UnsignedInt)m_connectionGeneration)
	{
		pushed = m_queue.push(header, FRAME_HEADER_BYTES, body, bodyLength, periodic ? STATS_EXPORTER_PERIODIC_RESERVE_BYTES : 0);
		if (!pushed && lifecycle)
		{
			InterlockedExchange((LONG*)&m_resyncNeeded, 1);
		}
	}
	LeaveCriticalSection(&m_queueLock);

	if (pushed && m_wakeEvent != NULL)
	{
		SetEvent(m_wakeEvent);
	}
	return pushed;
}

Bool StatsExporterPipeServer::takeResyncRequest()
{
	Bool roomAgain;

	if (m_resyncNeeded == 0)
	{
		return FALSE;
	}

	// Only once the reader has drained at least half the ring, so the frames that are sent again fit.
	EnterCriticalSection(&m_queueLock);
	roomAgain = m_queue.freeBytes() >= STATS_QUEUE_CAPACITY_BYTES / 2;
	LeaveCriticalSection(&m_queueLock);
	if (!roomAgain)
	{
		return FALSE;
	}
	return InterlockedExchange((LONG*)&m_resyncNeeded, 0) != 0;
}

Bool StatsExporterPipeServer::takeChatSend(StatsExporterChatSend& out)
{
	Bool has = FALSE;

	EnterCriticalSection(&m_chatLock);
	if (m_chatCount > 0)
	{
		out = m_chatSlots[m_chatHead];
		m_chatHead = (m_chatHead + 1) % CHAT_MAILBOX_SLOTS;
		--m_chatCount;
		has = TRUE;
	}
	LeaveCriticalSection(&m_chatLock);
	return has;
}

void StatsExporterPipeServer::handleInboundFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength)
{
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
	EnterCriticalSection(&m_chatLock);
	if (m_chatCount < CHAT_MAILBOX_SLOTS)
	{
		StatsExporterChatSend& slot = m_chatSlots[(m_chatHead + m_chatCount) % CHAT_MAILBOX_SLOTS];
		slot.channel = (int)chat.channel();
		slot.audience = (int)chat.audience();
		slot.text = chat.text();
		++m_chatCount;
	}
	LeaveCriticalSection(&m_chatLock);
}

Bool StatsExporterPipeServer::processInboundBytes(UnsignedInt bytesTransferred)
{
	m_inboundLen += bytesTransferred;

	for (;;)
	{
		UnsignedInt bodyLength;
		UnsignedInt frameLength;

		if (m_inboundLen < FRAME_HEADER_BYTES)
		{
			break;	// need more header bytes
		}

		bodyLength = (UnsignedInt)m_inboundBuffer[0]
			| ((UnsignedInt)m_inboundBuffer[1] << 8)
			| ((UnsignedInt)m_inboundBuffer[2] << 16)
			| ((UnsignedInt)m_inboundBuffer[3] << 24);
		if (bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_INBOUND_BODY_BYTES)
		{
			// Unrecoverable: a byte-mode pipe cannot resynchronize on a bad length, so the reader is dropped.
			return FALSE;
		}

		frameLength = FRAME_HEADER_BYTES + bodyLength;
		if (m_inboundLen < frameLength)
		{
			break;	// full frame not yet buffered
		}

		handleInboundFrame(m_inboundBuffer[4], m_inboundBuffer + FRAME_HEADER_BYTES, bodyLength);

		m_inboundLen -= frameLength;
		if (m_inboundLen > 0)
		{
			memmove(m_inboundBuffer, m_inboundBuffer + frameLength, m_inboundLen);
		}
	}
	return TRUE;
}

unsigned __stdcall StatsExporterPipeServer::workerProcThunk(void* param)
{
	static_cast<StatsExporterPipeServer*>(param)->workerProc();
	return 0;
}

Bool StatsExporterPipeServer::openPipeInstance(DWORD* createError)
{
	char pipeName[64];
	HANDLE connectEvent;
	OVERLAPPED overlapped;
	BOOL connectedImmediately;
	DWORD lastError;
	Bool ok = FALSE;

	*createError = 0;
	wsprintfA(pipeName, "\\\\.\\pipe\\generalsonline-stats-%lu", GetCurrentProcessId());

	// Only the user the game runs as may open the pipe, and never over the network. If that user cannot be determined the
	// pipe is not created at all rather than falling back to the default (wider) access.
	PipeSecurity security;
	if (!buildPipeSecurity(security))
	{
		*createError = GetLastError();
		return FALSE;
	}

	m_pipe = CreateNamedPipeA(
		pipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		1,	// single instance: one exporter at a time
		STATS_PIPE_BUFFER_BYTES, STATS_PIPE_BUFFER_BYTES,
		0,
		&security.attributes);
	if (m_pipe == INVALID_HANDLE_VALUE)
	{
		*createError = GetLastError();
		return FALSE;
	}

	connectEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (connectEvent == NULL)
	{
		*createError = GetLastError();
		CloseHandle(m_pipe);
		m_pipe = INVALID_HANDLE_VALUE;
		return FALSE;
	}

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = connectEvent;

	connectedImmediately = ConnectNamedPipe(m_pipe, &overlapped);
	lastError = GetLastError();
	if (connectedImmediately)
	{
		ok = TRUE;
	}
	else if (lastError == ERROR_PIPE_CONNECTED)
	{
		ok = TRUE;
	}
	else if (lastError == ERROR_IO_PENDING)
	{
		HANDLE waitHandles[2];
		waitHandles[0] = connectEvent;
		waitHandles[1] = m_shutdownEvent;
		DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
		if (waitResult == WAIT_OBJECT_0)
		{
			DWORD bytesTransferred;
			ok = GetOverlappedResult(m_pipe, &overlapped, &bytesTransferred, FALSE) ? TRUE : FALSE;
		}
		else
		{
			// The worker is the only thread issuing I/O on m_pipe, so CancelIo cancels exactly this connect.
			CancelIo(m_pipe);
		}
	}

	CloseHandle(connectEvent);

	if (!ok)
	{
		CloseHandle(m_pipe);
		m_pipe = INVALID_HANDLE_VALUE;
	}
	return ok;
}

void StatsExporterPipeServer::closePipeInstance()
{
	cancelPendingRead();

	if (m_pipe != INVALID_HANDLE_VALUE)
	{
		DisconnectNamedPipe(m_pipe);
		CloseHandle(m_pipe);
		m_pipe = INVALID_HANDLE_VALUE;
	}
	m_inboundLen = 0;

	EnterCriticalSection(&m_queueLock);
	m_connected = 0;
	m_queue.clear();
	LeaveCriticalSection(&m_queueLock);
}

Bool StatsExporterPipeServer::flushQueue()
{
	UnsignedByte sendBuffer[8192];
	HANDLE writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	Bool alive = TRUE;

	if (writeEvent == NULL)
	{
		return FALSE;
	}

	for (;;)
	{
		UnsignedInt available;
		OVERLAPPED overlapped;
		DWORD written = 0;
		BOOL immediate;

		EnterCriticalSection(&m_queueLock);
		available = m_queue.peek(sendBuffer, sizeof(sendBuffer));
		LeaveCriticalSection(&m_queueLock);
		if (available == 0)
		{
			break;
		}

		ZeroMemory(&overlapped, sizeof(overlapped));
		overlapped.hEvent = writeEvent;
		ResetEvent(writeEvent);

		immediate = WriteFile(m_pipe, sendBuffer, available, &written, &overlapped);
		if (!immediate)
		{
			DWORD lastError = GetLastError();
			if (lastError != ERROR_IO_PENDING)
			{
				alive = FALSE;
				break;
			}
			HANDLE waitHandles[2];
			waitHandles[0] = writeEvent;
			waitHandles[1] = m_shutdownEvent;
			DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
			if (waitResult != WAIT_OBJECT_0)
			{
				CancelIo(m_pipe);
				alive = FALSE;
				break;
			}
			if (!GetOverlappedResult(m_pipe, &overlapped, &written, FALSE))
			{
				alive = FALSE;
				break;
			}
		}

		EnterCriticalSection(&m_queueLock);
		m_queue.consume((UnsignedInt)written);
		LeaveCriticalSection(&m_queueLock);

		// A short write just loops: the remainder is peeked again, so nothing waits for the next wake.
	}

	CloseHandle(writeEvent);
	return alive;
}

Bool StatsExporterPipeServer::beginRead()
{
	DWORD bytesRead = 0;

	ZeroMemory(&m_readOverlapped, sizeof(m_readOverlapped));
	m_readOverlapped.hEvent = m_readEvent;
	ResetEvent(m_readEvent);

	if (m_inboundLen >= sizeof(m_inboundBuffer))
	{
		return FALSE;
	}

	m_readPending = TRUE;
	if (!ReadFile(m_pipe, m_inboundBuffer + m_inboundLen, (DWORD)(sizeof(m_inboundBuffer) - m_inboundLen), &bytesRead, &m_readOverlapped))
	{
		DWORD lastError = GetLastError();
		if (lastError != ERROR_IO_PENDING)
		{
			m_readPending = FALSE;
			return FALSE;
		}
	}
	// Pending or already complete, m_readEvent is signalled either way on an overlapped handle.
	return TRUE;
}

void StatsExporterPipeServer::cancelPendingRead()
{
	DWORD bytesTransferred;

	if (!m_readPending)
	{
		return;
	}
	CancelIo(m_pipe);
	GetOverlappedResult(m_pipe, &m_readOverlapped, &bytesTransferred, TRUE);
	m_readPending = FALSE;
}

void StatsExporterPipeServer::workerProc()
{
	Bool loggedCreateFailure = FALSE;

	for (;;)
	{
		DWORD createError = 0;

		if (m_shuttingDown)
		{
			break;
		}
		if (!openPipeInstance(&createError))
		{
			// Either shutdown interrupted the connect wait, or pipe creation failed outright.
			if (m_shuttingDown)
			{
				break;
			}
			if (createError != 0)
			{
				if (!loggedCreateFailure)
				{
					loggedCreateFailure = TRUE;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[StatsExporter] Pipe creation failed, error %lu; retrying", (unsigned long)createError);
				}
				// Back off, but wake at once on shutdown.
				if (WaitForSingleObject(m_shutdownEvent, STATS_PIPE_RETRY_WAIT_MS) == WAIT_OBJECT_0)
				{
					break;
				}
			}
			continue;
		}
		// Clearing the queue, the new generation and the connected flag change together under the queue lock, so no frame
		// produced for an earlier reader can slip into this one's stream.
		EnterCriticalSection(&m_queueLock);
		m_queue.clear();
		InterlockedExchange((LONG*)&m_resyncNeeded, 0);
		InterlockedIncrement((LONG*)&m_connectionGeneration);
		InterlockedExchange((LONG*)&m_connected, 1);
		LeaveCriticalSection(&m_queueLock);

		if (!beginRead())
		{
			closePipeInstance();
			if (m_shuttingDown)
			{
				break;
			}
			continue;
		}

		for (;;)
		{
			HANDLE waitHandles[3];
			DWORD waitResult;

			waitHandles[0] = m_wakeEvent;
			waitHandles[1] = m_shutdownEvent;
			waitHandles[2] = m_readEvent;
			waitResult = WaitForMultipleObjects(3, waitHandles, FALSE, STATS_PIPE_IDLE_WAIT_MS);
			if (waitResult == WAIT_OBJECT_0 + 1)
			{
				break;	// shutdown requested
			}
			if (waitResult == WAIT_FAILED)
			{
				break;
			}
			if (waitResult == WAIT_OBJECT_0 + 2)
			{
				DWORD bytesTransferred = 0;
				Bool readOk = GetOverlappedResult(m_pipe, &m_readOverlapped, &bytesTransferred, FALSE) ? TRUE : FALSE;
				m_readPending = FALSE;
				// A failed or empty read means the reader is gone; a bad frame length drops it too.
				if (!readOk || bytesTransferred == 0 || !processInboundBytes(bytesTransferred) || !beginRead())
				{
					break;
				}
				continue;
			}
			// Wake or timeout: flush any queued outbound frames.
			if (!flushQueue())
			{
				break;	// client gone; reopen a fresh pipe instance
			}
		}

		closePipeInstance();	// also cancels the outstanding read, if any
		if (m_shuttingDown)
		{
			break;
		}
	}
}
