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

#include "GameNetwork/GenOnline/GenOnlineLocalApiPipe.h"

#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// NetworkLog

#include <process.h>

#include <cstdio>
#include <cstring>
#include <new>

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

namespace GenOnlineLocalApi
{
namespace
{
// Events that wait for the game. The oldest HelloNeeded is dropped first, so a game that never polls cannot grow the list.
constexpr size_t kMaxEvents = 64;

constexpr DWORD kPipeBufferBytes = 32768;
constexpr DWORD kPipeRetryWaitMs = 1000;
// The worker wakes at least this often, so a missed wake never stalls it for longer.
constexpr DWORD kPipeIdleWaitMs = 250;

// A security descriptor whose only access control entry grants the current user full access to the pipe.
struct PipeSecurity
{
	SECURITY_ATTRIBUTES attributes;
	SECURITY_DESCRIPTOR descriptor;
	// DWORD arrays keep the SID and the ACL aligned.
	DWORD tokenBuffer[128];
	DWORD aclBuffer[128];
};

bool BuildPipeSecurity(PipeSecurity& security)
{
	HANDLE token = nullptr;
	DWORD needed = 0;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		return false;
	}
	const BOOL haveUser = GetTokenInformation(token, TokenUser, security.tokenBuffer, sizeof(security.tokenBuffer), &needed);
	CloseHandle(token);
	if (!haveUser)
	{
		return false;
	}

	PSID userSid = reinterpret_cast<TOKEN_USER*>(security.tokenBuffer)->User.Sid;
	ACL* acl = reinterpret_cast<ACL*>(security.aclBuffer);
	const DWORD aclLength = static_cast<DWORD>((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(userSid) + 3) & ~3u);
	if (aclLength > sizeof(security.aclBuffer))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return false;
	}
	if (!InitializeAcl(acl, aclLength, ACL_REVISION) || !AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, userSid)
		|| !InitializeSecurityDescriptor(&security.descriptor, SECURITY_DESCRIPTOR_REVISION)
		|| !SetSecurityDescriptorDacl(&security.descriptor, TRUE, acl, FALSE))
	{
		return false;
	}

	security.attributes.nLength = sizeof(security.attributes);
	security.attributes.lpSecurityDescriptor = &security.descriptor;
	security.attributes.bInheritHandle = FALSE;
	return true;
}

void CloseEvent(HANDLE& handle)
{
	if (handle != nullptr)
	{
		CloseHandle(handle);
		handle = nullptr;
	}
}
} // namespace

void WriteOutboundHeader(uint8_t* header, uint32_t bodyLength, uint8_t frameType, uint32_t sequence, uint64_t timestampMicros)
{
	for (int i = 0; i < 4; ++i)
	{
		header[i] = static_cast<uint8_t>((bodyLength >> (8 * i)) & 0xFF);
	}
	header[4] = frameType;
	for (int i = 0; i < 4; ++i)
	{
		header[5 + i] = static_cast<uint8_t>((sequence >> (8 * i)) & 0xFF);
	}
	for (int i = 0; i < 8; ++i)
	{
		header[9 + i] = static_cast<uint8_t>((timestampMicros >> (8 * i)) & 0xFF);
	}
}

InboundParse ParseInbound(const uint8_t* buffer, uint32_t length, uint8_t* frameType, uint32_t* bodyLength)
{
	if (length < kInboundHeaderBytes)
	{
		return InboundParse::NeedMore;
	}
	const uint32_t body = static_cast<uint32_t>(buffer[0]) | (static_cast<uint32_t>(buffer[1]) << 8) | (static_cast<uint32_t>(buffer[2]) << 16)
		| (static_cast<uint32_t>(buffer[3]) << 24);
	// A byte mode pipe cannot resynchronize on a bad length, so the caller drops the reader.
	if (body == 0 || body > kMaxInboundBodyBytes)
	{
		return InboundParse::BadLength;
	}
	if (length < kInboundHeaderBytes + body)
	{
		return InboundParse::NeedMore;
	}
	*frameType = buffer[4];
	*bodyLength = body;
	return InboundParse::Frame;
}

void FrameQueue::Clear()
{
	m_head = 0;
	m_count = 0;
}

void FrameQueue::Write(uint32_t offset, const uint8_t* data, uint32_t length)
{
	uint32_t tail = m_head + m_count + offset;
	if (tail >= kQueueCapacityBytes)
	{
		tail -= kQueueCapacityBytes;
	}
	uint32_t first = kQueueCapacityBytes - tail;
	if (first > length)
	{
		first = length;
	}
	std::memcpy(m_buffer.get() + tail, data, first);
	if (length > first)
	{
		std::memcpy(m_buffer.get(), data + first, length - first);
	}
}

uint32_t FrameQueue::FreeBytes() const
{
	return kQueueCapacityBytes - m_count;
}

bool FrameQueue::Push(const uint8_t* header, uint32_t headerLength, const uint8_t* body, uint32_t bodyLength, uint32_t reserve)
{
	if (header == nullptr || body == nullptr || bodyLength == 0 || bodyLength > kMaxBodyBytes)
	{
		return false;
	}
	const uint32_t length = headerLength + bodyLength;
	if (length + reserve > kQueueCapacityBytes - m_count)
	{
		return false;
	}
	if (!m_buffer)
	{
		m_buffer.reset(new (std::nothrow) uint8_t[kQueueCapacityBytes]);
		if (!m_buffer)
		{
			return false;
		}
	}
	Write(0, header, headerLength);
	Write(headerLength, body, bodyLength);
	m_count += length;
	return true;
}

uint32_t FrameQueue::Peek(uint8_t* dest, uint32_t capacity) const
{
	if (!m_buffer || m_count == 0 || capacity == 0)
	{
		return 0;
	}
	uint32_t available = m_count;
	if (available > capacity)
	{
		available = capacity;
	}
	uint32_t first = kQueueCapacityBytes - m_head;
	if (first > available)
	{
		first = available;
	}
	std::memcpy(dest, m_buffer.get() + m_head, first);
	if (available > first)
	{
		std::memcpy(dest + first, m_buffer.get(), available - first);
	}
	return available;
}

void FrameQueue::Consume(uint32_t length)
{
	if (length > m_count)
	{
		length = m_count;
	}
	m_head += length;
	if (m_head >= kQueueCapacityBytes)
	{
		m_head -= kQueueCapacityBytes;
	}
	m_count -= length;
}

void TokenBucket::Reset(uint64_t nowMs)
{
	m_tokens = static_cast<double>(kRequestBurst);
	m_refillMs = nowMs;
}

bool TokenBucket::Allow(uint64_t nowMs, bool hasRoom)
{
	const double refill = static_cast<double>(nowMs - m_refillMs) * static_cast<double>(kMaxRequestsPerSecond) / 1000.0;
	m_refillMs = nowMs;
	m_tokens += refill;
	if (m_tokens > static_cast<double>(kRequestBurst))
	{
		m_tokens = static_cast<double>(kRequestBurst);
	}
	if (m_tokens < 1.0 || !hasRoom)
	{
		return false;
	}
	m_tokens -= 1.0;
	return true;
}

uint32_t ReaderTable::NextIdLocked()
{
	// Id 0 addresses every reader, so the counter skips it when it wraps.
	if (++m_idCounter == 0)
	{
		++m_idCounter;
	}
	return m_idCounter;
}

void ReaderTable::PushEventLocked(uint32_t readerId, ReaderEvent event)
{
	if (event == ReaderEvent::HelloNeeded)
	{
		for (auto it = m_events.begin(); it != m_events.end(); ++it)
		{
			if (it->readerId == readerId && it->event == event)
			{
				m_events.erase(it);
				break;
			}
		}
	}
	if (m_events.size() >= kMaxEvents)
	{
		auto oldest = m_events.begin();
		for (auto it = m_events.begin(); it != m_events.end(); ++it)
		{
			if (it->event == ReaderEvent::HelloNeeded)
			{
				oldest = it;
				break;
			}
		}
		m_events.erase(oldest);
	}
	m_events.push_back(Event{readerId, event});
}

// Only once the reader drained half its queue, so the frames that are sent again fit.
void ReaderTable::PromoteHelloLocked()
{
	for (Reader& reader : m_readers)
	{
		if (reader.connected && reader.helloWanted && reader.queue.FreeBytes() >= kQueueCapacityBytes / 2)
		{
			reader.helloWanted = false;
			PushEventLocked(reader.id, ReaderEvent::HelloNeeded);
		}
	}
}

void ReaderTable::ClearInbound(Reader& reader)
{
	reader.inbound.clear();
	reader.requestCount = 0;
	reader.refusalCount = 0;
	reader.chatCount = 0;
}

uint32_t ReaderTable::OnConnected(int slot, uint64_t nowMs)
{
	if (!ValidSlot(slot))
	{
		return 0;
	}
	// The reset and the new id happen under one lock, so no frame produced for an earlier reader reaches this one.
	std::lock_guard<std::mutex> lock(m_lock);
	Reader& reader = m_readers[slot];
	reader.connected = true;
	reader.streamStarted = false;
	reader.helloWanted = false;
	reader.id = NextIdLocked();
	reader.nextSequence = 0;
	reader.queue.Clear();
	reader.requests.Reset(nowMs);
	ClearInbound(reader);
	PushEventLocked(reader.id, ReaderEvent::HelloNeeded);
	return reader.id;
}

void ReaderTable::OnClosed(int slot)
{
	if (!ValidSlot(slot))
	{
		return;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	Reader& reader = m_readers[slot];
	if (!reader.connected)
	{
		return;
	}
	PushEventLocked(reader.id, ReaderEvent::Gone);
	reader.connected = false;
	reader.streamStarted = false;
	reader.helloWanted = false;
	reader.id = 0;
	reader.queue.Clear();
	ClearInbound(reader);
}

void ReaderTable::HandleInboundFrame(int slot, uint8_t frameType, const uint8_t* body, uint32_t bodyLength, uint64_t nowMs)
{
	if (!ValidSlot(slot) || body == nullptr || bodyLength == 0 || bodyLength > kMaxInboundBodyBytes)
	{
		return;
	}
	if (frameType != kInboundRequest && frameType != kInboundChatSend)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_lock);
	Reader& reader = m_readers[slot];
	// A frame that arrives before the Hello was queued is dropped because the reader has not seen the limits yet.
	if (!reader.connected || !reader.streamStarted)
	{
		return;
	}

	InboundKind kind;
	if (frameType == kInboundChatSend)
	{
		if (reader.chatCount >= kChatMailboxSlots)
		{
			return;
		}
		kind = InboundKind::Chat;
	}
	else if (reader.requests.Allow(nowMs, reader.requestCount < kRequestQueueSlots))
	{
		kind = InboundKind::Request;
	}
	else if (reader.refusalCount < kRefusalSlots)
	{
		kind = InboundKind::Refusal;
	}
	else
	{
		return;
	}

	InboundEntry entry;
	entry.kind = kind;
	entry.frameType = frameType;
	entry.body.reset(new (std::nothrow) uint8_t[bodyLength]);
	if (!entry.body)
	{
		return;
	}
	std::memcpy(entry.body.get(), body, bodyLength);
	entry.bodyLength = bodyLength;
	try
	{
		reader.inbound.push_back(std::move(entry));
	}
	catch (...)
	{
		return;
	}

	switch (kind)
	{
		case InboundKind::Request:
			++reader.requestCount;
			break;
		case InboundKind::Refusal:
			++reader.refusalCount;
			break;
		case InboundKind::Chat:
			++reader.chatCount;
			break;
	}
}

uint32_t ReaderTable::Peek(int slot, uint8_t* dest, uint32_t capacity) const
{
	if (!ValidSlot(slot))
	{
		return 0;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	return m_readers[slot].queue.Peek(dest, capacity);
}

void ReaderTable::Consume(int slot, uint32_t length)
{
	if (!ValidSlot(slot))
	{
		return;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	m_readers[slot].queue.Consume(length);
}

uint32_t ReaderTable::FreeBytes(int slot) const
{
	if (!ValidSlot(slot))
	{
		return 0;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	return m_readers[slot].queue.FreeBytes();
}

bool ReaderTable::IsConnected() const
{
	std::lock_guard<std::mutex> lock(m_lock);
	for (const Reader& reader : m_readers)
	{
		if (reader.connected)
		{
			return true;
		}
	}
	return false;
}

bool ReaderTable::EnqueueFrame(uint8_t frameType, FrameClass frameClass, const void* body, uint32_t bodyLength, uint64_t timestampMicros, uint32_t readerId)
{
	if (body == nullptr || bodyLength == 0 || bodyLength > kMaxBodyBytes)
	{
		return false;
	}
	if (frameClass != FrameClass::Hello && frameClass != FrameClass::Lifecycle && frameClass != FrameClass::Periodic)
	{
		return false;
	}

	const bool hello = frameClass == FrameClass::Hello;
	const bool periodic = frameClass == FrameClass::Periodic;
	const uint8_t* bytes = static_cast<const uint8_t*>(body);

	// Connection state is checked under the lock a new connection resets its queue under, so a frame for an earlier reader never reaches the next one.
	std::lock_guard<std::mutex> lock(m_lock);
	bool pushedAny = false;
	for (Reader& reader : m_readers)
	{
		if (!reader.connected || (readerId == kAllReaders ? !reader.streamStarted : reader.id != readerId))
		{
			continue;
		}
		if (!reader.streamStarted && !hello)
		{
			continue;
		}

		uint8_t header[kOutboundHeaderBytes];
		// The number is used up even when the frame is dropped, which is how the reader notices the gap.
		WriteOutboundHeader(header, bodyLength, frameType, reader.nextSequence++, timestampMicros);
		if (reader.queue.Push(header, kOutboundHeaderBytes, bytes, bodyLength, periodic ? kPeriodicReserveBytes : 0))
		{
			pushedAny = true;
			if (hello)
			{
				reader.streamStarted = true;
				reader.helloWanted = false;
			}
		}
		else if (!periodic)
		{
			// The queued frames are whole and stay, so the reader never sees a cut frame. Only new frames stop until a Hello restarts the stream.
			reader.streamStarted = false;
			reader.helloWanted = true;
		}
	}
	return pushedAny;
}

bool ReaderTable::TakeReaderEvent(uint32_t* readerId, ReaderEvent* event)
{
	if (readerId == nullptr || event == nullptr)
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	PromoteHelloLocked();
	if (m_events.empty())
	{
		return false;
	}
	*readerId = m_events.front().readerId;
	*event = m_events.front().event;
	m_events.pop_front();
	return true;
}

bool ReaderTable::TakeInbound(TakenInbound* out)
{
	if (out == nullptr)
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	for (int k = 0; k < kMaxReaders; ++k)
	{
		const int i = static_cast<int>((m_inboundNext + k) % kMaxReaders);
		Reader& reader = m_readers[i];
		if (!reader.connected || !reader.streamStarted || reader.inbound.empty())
		{
			continue;
		}

		InboundEntry& entry = reader.inbound.front();
		switch (entry.kind)
		{
			case InboundKind::Request:
				--reader.requestCount;
				break;
			case InboundKind::Refusal:
				--reader.refusalCount;
				break;
			case InboundKind::Chat:
				--reader.chatCount;
				break;
		}
		out->frameType = entry.frameType;
		out->readerId = reader.id;
		out->flags = entry.kind == InboundKind::Refusal ? kInboundRateLimited : 0;
		out->bodyLength = entry.bodyLength;
		out->body = std::move(entry.body);
		reader.inbound.pop_front();
		m_inboundNext = static_cast<uint32_t>((i + 1) % kMaxReaders);
		return true;
	}
	return false;
}

std::string PipeName()
{
	char name[64];
	std::snprintf(name, sizeof(name), "\\\\.\\pipe\\genonline-caster-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
	return name;
}

PipeServer::PipeServer(ReaderTable& table)
	: m_table(table)
	, m_slots(new Slot[kMaxReaders])
{
}

PipeServer::~PipeServer()
{
	Stop();
}

bool PipeServer::Start(uint32_t maxReaders)
{
	if (m_thread != nullptr)
	{
		return true;
	}

	m_maxReaders = maxReaders < 1 ? 1 : (maxReaders > static_cast<uint32_t>(kMaxReaders) ? kMaxReaders : static_cast<int>(maxReaders));
	m_shuttingDown = false;
	m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	bool created = m_wakeEvent != nullptr && m_shutdownEvent != nullptr;
	for (int i = 0; i < kMaxReaders; ++i)
	{
		Slot& slot = m_slots[i];
		slot.connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		slot.readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		slot.writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		created = created && slot.connectEvent != nullptr && slot.readEvent != nullptr && slot.writeEvent != nullptr;
	}
	if (created)
	{
		m_thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &PipeServer::WorkerThunk, this, 0, nullptr));
	}
	if (m_thread == nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[LocalApi] ERROR: Starting the pipe failed, error %lu", static_cast<unsigned long>(GetLastError()));
		CloseHandles();
		return false;
	}
	return true;
}

void PipeServer::Stop()
{
	m_shuttingDown = true;
	if (m_shutdownEvent != nullptr)
	{
		SetEvent(m_shutdownEvent);
	}
	if (m_thread != nullptr)
	{
		// The worker returns within kPipeIdleWaitMs.
		WaitForSingleObject(m_thread, INFINITE);
		CloseHandle(m_thread);
		m_thread = nullptr;
	}
	for (int i = 0; i < kMaxReaders; ++i)
	{
		CloseSlot(i);
	}
	CloseHandles();
}

void PipeServer::CloseHandles()
{
	for (int i = 0; i < kMaxReaders; ++i)
	{
		Slot& slot = m_slots[i];
		CloseEvent(slot.connectEvent);
		CloseEvent(slot.readEvent);
		CloseEvent(slot.writeEvent);
	}
	CloseEvent(m_wakeEvent);
	CloseEvent(m_shutdownEvent);
}

void PipeServer::Wake()
{
	if (m_wakeEvent != nullptr)
	{
		SetEvent(m_wakeEvent);
	}
}

bool PipeServer::ProcessInboundBytes(int index, uint32_t bytesTransferred)
{
	Slot& slot = m_slots[index];
	slot.inboundLength += bytesTransferred;
	for (;;)
	{
		uint8_t frameType = 0;
		uint32_t bodyLength = 0;
		const InboundParse parse = ParseInbound(slot.inbound, slot.inboundLength, &frameType, &bodyLength);
		if (parse == InboundParse::NeedMore)
		{
			return true;
		}
		if (parse == InboundParse::BadLength)
		{
			return false;
		}
		m_table.HandleInboundFrame(index, frameType, slot.inbound + kInboundHeaderBytes, bodyLength, GetTickCount64());
		const uint32_t frameLength = kInboundHeaderBytes + bodyLength;
		slot.inboundLength -= frameLength;
		if (slot.inboundLength > 0)
		{
			std::memmove(slot.inbound, slot.inbound + frameLength, slot.inboundLength);
		}
	}
}

unsigned __stdcall PipeServer::WorkerThunk(void* param)
{
	static_cast<PipeServer*>(param)->WorkerProc();
	return 0;
}

int PipeServer::StartListening(Slot& slot, DWORD* createError)
{
	*createError = 0;
	const std::string name = PipeName();

	// Only the user the game runs as may open the pipe, and never over the network. Without that user no pipe is created.
	PipeSecurity security;
	if (!BuildPipeSecurity(security))
	{
		*createError = GetLastError();
		return 0;
	}

	slot.pipe = CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, static_cast<DWORD>(m_maxReaders), kPipeBufferBytes, kPipeBufferBytes, 0,
		&security.attributes);
	if (slot.pipe == INVALID_HANDLE_VALUE)
	{
		*createError = GetLastError();
		return 0;
	}

	ZeroMemory(&slot.connectOverlapped, sizeof(slot.connectOverlapped));
	slot.connectOverlapped.hEvent = slot.connectEvent;
	ResetEvent(slot.connectEvent);
	const BOOL connectedImmediately = ConnectNamedPipe(slot.pipe, &slot.connectOverlapped);
	const DWORD lastError = GetLastError();
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

void PipeServer::OnConnected(int index)
{
	Slot& slot = m_slots[index];
	slot.state = SlotState::Connected;
	slot.readPending = false;
	slot.writePending = false;
	slot.inboundLength = 0;
	m_table.OnConnected(index, GetTickCount64());
	if (!BeginRead(slot))
	{
		CloseSlot(index);
	}
}

void PipeServer::CloseSlot(int index)
{
	Slot& slot = m_slots[index];
	if (slot.state == SlotState::Closed && slot.pipe == INVALID_HANDLE_VALUE)
	{
		return;
	}

	// The worker is the only thread with I/O on the pipe of a slot, so CancelIo cancels exactly these operations.
	if (slot.pipe != INVALID_HANDLE_VALUE && (slot.readPending || slot.writePending || slot.state == SlotState::Listening))
	{
		DWORD bytesTransferred = 0;
		CancelIo(slot.pipe);
		if (slot.readPending)
		{
			GetOverlappedResult(slot.pipe, &slot.readOverlapped, &bytesTransferred, TRUE);
		}
		if (slot.writePending)
		{
			GetOverlappedResult(slot.pipe, &slot.writeOverlapped, &bytesTransferred, TRUE);
		}
		if (slot.state == SlotState::Listening)
		{
			GetOverlappedResult(slot.pipe, &slot.connectOverlapped, &bytesTransferred, TRUE);
		}
	}
	slot.readPending = false;
	slot.writePending = false;

	if (slot.pipe != INVALID_HANDLE_VALUE)
	{
		if (slot.state == SlotState::Connected)
		{
			DisconnectNamedPipe(slot.pipe);
		}
		CloseHandle(slot.pipe);
		slot.pipe = INVALID_HANDLE_VALUE;
	}
	slot.inboundLength = 0;
	m_table.OnClosed(index);
	slot.state = SlotState::Closed;
	slot.retryAtMs = 0;
}

bool PipeServer::BeginRead(Slot& slot)
{
	ZeroMemory(&slot.readOverlapped, sizeof(slot.readOverlapped));
	slot.readOverlapped.hEvent = slot.readEvent;
	ResetEvent(slot.readEvent);
	if (slot.inboundLength >= sizeof(slot.inbound))
	{
		return false;
	}
	slot.readPending = true;
	DWORD bytesRead = 0;
	if (!ReadFile(slot.pipe, slot.inbound + slot.inboundLength, static_cast<DWORD>(sizeof(slot.inbound) - slot.inboundLength), &bytesRead, &slot.readOverlapped))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			slot.readPending = false;
			return false;
		}
	}
	// readEvent is signalled also when the read completes at once on an overlapped handle.
	return true;
}

bool PipeServer::BeginWrite(int index)
{
	Slot& slot = m_slots[index];
	if (slot.state != SlotState::Connected || slot.writePending)
	{
		return true;
	}
	const uint32_t available = m_table.Peek(index, slot.sendBuffer, kSendChunkBytes);
	if (available == 0)
	{
		return true;
	}

	ZeroMemory(&slot.writeOverlapped, sizeof(slot.writeOverlapped));
	slot.writeOverlapped.hEvent = slot.writeEvent;
	ResetEvent(slot.writeEvent);
	// A reader that does not read leaves its write pending while the other readers carry on.
	if (!WriteFile(slot.pipe, slot.sendBuffer, available, nullptr, &slot.writeOverlapped))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			return false;
		}
	}
	slot.writePending = true;
	return true;
}

void PipeServer::PumpWrites()
{
	for (int i = 0; i < m_maxReaders; ++i)
	{
		if (m_slots[i].state == SlotState::Connected && !BeginWrite(i))
		{
			CloseSlot(i);
		}
	}
}

void PipeServer::WorkerProc()
{
	enum HandleKind
	{
		kShutdown,
		kWake,
		kConnect,
		kRead,
		kWrite,
	};
	constexpr int kMaxHandles = 2 + 3 * kMaxReaders;
	bool loggedCreateFailure = false;

	while (!m_shuttingDown)
	{
		HANDLE handles[kMaxHandles];
		int handleSlot[kMaxHandles];
		HandleKind handleKind[kMaxHandles];
		DWORD count = 0;

		// Every free slot keeps an instance listening, so a reader can connect while the others are busy.
		for (int i = 0; i < m_maxReaders; ++i)
		{
			Slot& slot = m_slots[i];
			if (slot.state != SlotState::Closed || GetTickCount64() < slot.retryAtMs)
			{
				continue;
			}
			DWORD createError = 0;
			const int started = StartListening(slot, &createError);
			if (started == 0)
			{
				if (!loggedCreateFailure)
				{
					loggedCreateFailure = true;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[LocalApi] WARNING: Pipe creation failed, error %lu, retrying", static_cast<unsigned long>(createError));
				}
				slot.retryAtMs = GetTickCount64() + kPipeRetryWaitMs;
			}
			else
			{
				slot.state = SlotState::Listening;
				if (started == 2)
				{
					OnConnected(i);
				}
			}
		}

		handles[count] = m_shutdownEvent;
		handleSlot[count] = -1;
		handleKind[count++] = kShutdown;
		handles[count] = m_wakeEvent;
		handleSlot[count] = -1;
		handleKind[count++] = kWake;
		for (int i = 0; i < m_maxReaders; ++i)
		{
			Slot& slot = m_slots[i];
			if (slot.state == SlotState::Listening)
			{
				handles[count] = slot.connectEvent;
				handleSlot[count] = i;
				handleKind[count++] = kConnect;
			}
			else if (slot.state == SlotState::Connected)
			{
				if (slot.readPending)
				{
					handles[count] = slot.readEvent;
					handleSlot[count] = i;
					handleKind[count++] = kRead;
				}
				if (slot.writePending)
				{
					handles[count] = slot.writeEvent;
					handleSlot[count] = i;
					handleKind[count++] = kWrite;
				}
			}
		}

		const DWORD waitResult = WaitForMultipleObjects(count, handles, FALSE, kPipeIdleWaitMs);
		if (waitResult == WAIT_FAILED)
		{
			break;
		}
		if (waitResult < WAIT_OBJECT_0 + count)
		{
			const DWORD which = waitResult - WAIT_OBJECT_0;
			const int index = handleSlot[which];
			if (handleKind[which] == kShutdown)
			{
				break;
			}
			if (handleKind[which] == kConnect)
			{
				Slot& slot = m_slots[index];
				DWORD bytesTransferred = 0;
				if (GetOverlappedResult(slot.pipe, &slot.connectOverlapped, &bytesTransferred, FALSE))
				{
					OnConnected(index);
				}
				else
				{
					// The reader came and went before it was accepted, so the slot listens again.
					CloseSlot(index);
				}
			}
			else if (handleKind[which] == kRead)
			{
				Slot& slot = m_slots[index];
				DWORD bytesTransferred = 0;
				const bool readOk = GetOverlappedResult(slot.pipe, &slot.readOverlapped, &bytesTransferred, FALSE) != FALSE;
				slot.readPending = false;
				// A failed or empty read means the reader is gone, and a bad frame length drops it too.
				if (!readOk || bytesTransferred == 0 || !ProcessInboundBytes(index, bytesTransferred) || !BeginRead(slot))
				{
					CloseSlot(index);
				}
			}
			else if (handleKind[which] == kWrite)
			{
				Slot& slot = m_slots[index];
				DWORD written = 0;
				const bool writeOk = GetOverlappedResult(slot.pipe, &slot.writeOverlapped, &written, FALSE) != FALSE;
				slot.writePending = false;
				if (!writeOk)
				{
					CloseSlot(index);
				}
				else
				{
					// A short write leaves the rest queued for the next pump.
					m_table.Consume(index, written);
				}
			}
		}
		PumpWrites();
	}

	for (int i = 0; i < kMaxReaders; ++i)
	{
		CloseSlot(i);
	}
}
} // namespace GenOnlineLocalApi
