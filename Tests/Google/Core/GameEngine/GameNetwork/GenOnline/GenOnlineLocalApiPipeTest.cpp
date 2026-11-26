#include <gtest/gtest.h>

#include "GameNetwork/GenOnline/GenOnlineLocalApiPipe.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace GenOnlineLocalApi;

namespace
{
struct Header
{
	uint32_t bodyLength;
	uint8_t frameType;
	uint32_t sequence;
	uint64_t timestamp;
};

uint32_t ReadU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16)
		| (static_cast<uint32_t>(bytes[3]) << 24);
}

Header ReadHeader(const uint8_t* bytes)
{
	Header header;
	header.bodyLength = ReadU32(bytes);
	header.frameType = bytes[4];
	header.sequence = ReadU32(bytes + 5);
	header.timestamp = 0;
	for (int i = 0; i < 8; ++i)
	{
		header.timestamp |= static_cast<uint64_t>(bytes[9 + i]) << (8 * i);
	}
	return header;
}

// Takes every queued frame of slot and returns their headers.
std::vector<Header> Drain(ReaderTable& table, int slot)
{
	std::vector<uint8_t> bytes(kQueueCapacityBytes);
	const uint32_t length = table.Peek(slot, bytes.data(), static_cast<uint32_t>(bytes.size()));
	table.Consume(slot, length);
	std::vector<Header> headers;
	uint32_t offset = 0;
	while (offset + kOutboundHeaderBytes <= length)
	{
		const Header header = ReadHeader(bytes.data() + offset);
		headers.push_back(header);
		offset += kOutboundHeaderBytes + header.bodyLength;
	}
	return headers;
}

// Connects slot, queues a Hello for it so its stream starts, and drains that frame. Returns the reader id.
uint32_t ConnectStarted(ReaderTable& table, int slot, uint64_t nowMs = 0)
{
	const uint32_t id = table.OnConnected(slot, nowMs);
	const uint8_t hello[1] = {0x09};
	table.EnqueueFrame(0x09, FrameClass::Hello, hello, sizeof(hello), 0, id);
	std::vector<uint8_t> bytes(kOutboundHeaderBytes + sizeof(hello));
	table.Consume(slot, table.Peek(slot, bytes.data(), static_cast<uint32_t>(bytes.size())));
	return id;
}

bool Enqueue(ReaderTable& table, FrameClass frameClass, uint32_t bodyLength, uint32_t readerId, uint8_t frameType = 0x02)
{
	std::vector<uint8_t> body(bodyLength, 0xAB);
	return table.EnqueueFrame(frameType, frameClass, body.data(), bodyLength, 1234, readerId);
}

struct Event
{
	uint32_t readerId = 0;
	ReaderEvent event = static_cast<ReaderEvent>(-1);
};

std::vector<Event> TakeEvents(ReaderTable& table)
{
	std::vector<Event> events;
	Event event;
	while (table.TakeReaderEvent(&event.readerId, &event.event))
	{
		events.push_back(event);
	}
	return events;
}

void SendInbound(ReaderTable& table, int slot, uint8_t frameType, uint8_t marker, uint64_t nowMs)
{
	const uint8_t body[2] = {0x08, marker};
	table.HandleInboundFrame(slot, frameType, body, sizeof(body), nowMs);
}

std::vector<TakenInbound> TakeAll(ReaderTable& table)
{
	std::vector<TakenInbound> frames;
	TakenInbound taken;
	while (table.TakeInbound(&taken))
	{
		frames.push_back(std::move(taken));
	}
	return frames;
}

constexpr uint32_t kFullFrame = kMaxBodyBytes;

constexpr DWORD kWaitMs = 10000;

// Opens the pipe as a reader, retrying while the server creates its instances.
HANDLE ConnectReader()
{
	const std::string name = PipeName();
	const ULONGLONG deadline = GetTickCount64() + kWaitMs;
	while (GetTickCount64() < deadline)
	{
		HANDLE pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE)
		{
			return pipe;
		}
		Sleep(10);
	}
	return INVALID_HANDLE_VALUE;
}

template <typename Condition>
bool WaitFor(Condition condition)
{
	const ULONGLONG deadline = GetTickCount64() + kWaitMs;
	while (GetTickCount64() < deadline)
	{
		if (condition())
		{
			return true;
		}
		Sleep(5);
	}
	return false;
}

bool ReadExactly(HANDLE pipe, uint8_t* buffer, DWORD length)
{
	DWORD done = 0;
	while (done < length)
	{
		DWORD read = 0;
		if (!ReadFile(pipe, buffer + done, length - done, &read, nullptr) || read == 0)
		{
			return false;
		}
		done += read;
	}
	return true;
}

bool WriteAll(HANDLE pipe, const std::vector<uint8_t>& bytes)
{
	DWORD written = 0;
	return WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
}

std::vector<uint8_t> InboundFrame(uint8_t frameType, const std::vector<uint8_t>& body)
{
	const uint32_t length = static_cast<uint32_t>(body.size());
	std::vector<uint8_t> frame = {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length >> 16),
		static_cast<uint8_t>(length >> 24), frameType};
	frame.insert(frame.end(), body.begin(), body.end());
	return frame;
}

// Waits for the next reader event.
Event WaitForEvent(ReaderTable& table)
{
	Event event;
	WaitFor([&] { return table.TakeReaderEvent(&event.readerId, &event.event); });
	return event;
}
} // namespace

TEST(GenOnlineLocalApiPipe, OutboundHeaderIsLittleEndian)
{
	uint8_t header[kOutboundHeaderBytes];
	WriteOutboundHeader(header, 0x04030201u, 0x09, 0x08070605u, 0x100F0E0D0C0B0A09ull);
	const uint8_t expected[kOutboundHeaderBytes] = {0x01, 0x02, 0x03, 0x04, 0x09, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
	EXPECT_EQ(std::memcmp(header, expected, sizeof(expected)), 0);
}

TEST(GenOnlineLocalApiPipe, InboundParseWaitsForTheWholeFrame)
{
	const uint8_t frame[] = {0x03, 0x00, 0x00, 0x00, 0x11, 0xAA, 0xBB, 0xCC};
	uint8_t type = 0;
	uint32_t length = 0;
	for (uint32_t have = 0; have < sizeof(frame); ++have)
	{
		EXPECT_EQ(ParseInbound(frame, have, &type, &length), InboundParse::NeedMore);
	}
	ASSERT_EQ(ParseInbound(frame, sizeof(frame), &type, &length), InboundParse::Frame);
	EXPECT_EQ(type, 0x11);
	EXPECT_EQ(length, 3u);
}

TEST(GenOnlineLocalApiPipe, InboundParseRejectsZeroAndOversizedLengths)
{
	uint8_t type = 0;
	uint32_t length = 0;
	const uint8_t zero[] = {0x00, 0x00, 0x00, 0x00, 0x11};
	EXPECT_EQ(ParseInbound(zero, sizeof(zero), &type, &length), InboundParse::BadLength);
	const uint8_t over[] = {0x01, 0x10, 0x00, 0x00, 0x11};
	EXPECT_EQ(ParseInbound(over, sizeof(over), &type, &length), InboundParse::BadLength);
	const uint8_t huge[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x11};
	EXPECT_EQ(ParseInbound(huge, sizeof(huge), &type, &length), InboundParse::BadLength);
	const uint8_t max[] = {0x00, 0x10, 0x00, 0x00, 0x11};
	EXPECT_EQ(ParseInbound(max, sizeof(max), &type, &length), InboundParse::NeedMore);
}

TEST(GenOnlineLocalApiPipe, TokenBucketAllowsTheBurstThenTheRate)
{
	TokenBucket bucket;
	bucket.Reset(1000);
	for (uint32_t i = 0; i < kRequestBurst; ++i)
	{
		EXPECT_TRUE(bucket.Allow(1000, true));
	}
	EXPECT_FALSE(bucket.Allow(1000, true));
	// 20 per second refills one token every 50 ms.
	EXPECT_TRUE(bucket.Allow(1050, true));
	EXPECT_FALSE(bucket.Allow(1050, true));
	EXPECT_FALSE(bucket.Allow(1100, false));
	EXPECT_TRUE(bucket.Allow(1100, true));
	EXPECT_TRUE(bucket.Allow(100000, true));
	EXPECT_LE(bucket.Tokens(), static_cast<double>(kRequestBurst));
}

TEST(GenOnlineLocalApiPipe, FrameQueueDropsAFrameThatDoesNotFit)
{
	FrameQueue queue;
	std::vector<uint8_t> body(kFullFrame, 1);
	uint8_t header[kOutboundHeaderBytes] = {};
	int pushed = 0;
	while (queue.Push(header, kOutboundHeaderBytes, body.data(), kFullFrame, 0))
	{
		++pushed;
	}
	EXPECT_EQ(pushed, 3);
	EXPECT_EQ(queue.FreeBytes(), kQueueCapacityBytes - 3 * (kOutboundHeaderBytes + kFullFrame));
	EXPECT_FALSE(queue.Push(header, kOutboundHeaderBytes, body.data(), 0, 0));
	EXPECT_FALSE(queue.Push(header, kOutboundHeaderBytes, body.data(), kMaxBodyBytes + 1, 0));
}

TEST(GenOnlineLocalApiPipe, FrameQueueWrapsAround)
{
	FrameQueue queue;
	std::vector<uint8_t> body(kFullFrame);
	uint8_t header[kOutboundHeaderBytes] = {};
	for (int round = 0; round < 10; ++round)
	{
		for (size_t i = 0; i < body.size(); ++i)
		{
			body[i] = static_cast<uint8_t>(i + round);
		}
		ASSERT_TRUE(queue.Push(header, kOutboundHeaderBytes, body.data(), kFullFrame, 0));
		std::vector<uint8_t> out(kOutboundHeaderBytes + kFullFrame);
		ASSERT_EQ(queue.Peek(out.data(), static_cast<uint32_t>(out.size())), out.size());
		EXPECT_EQ(std::memcmp(out.data() + kOutboundHeaderBytes, body.data(), kFullFrame), 0);
		queue.Consume(static_cast<uint32_t>(out.size()));
	}
	EXPECT_EQ(queue.FreeBytes(), kQueueCapacityBytes);
}

TEST(GenOnlineLocalApiPipe, DroppedFrameLeavesASequenceGap)
{
	ReaderTable table;
	const uint32_t id = ConnectStarted(table, 0);
	EXPECT_TRUE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	EXPECT_TRUE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	EXPECT_TRUE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	EXPECT_FALSE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	const std::vector<Header> first = Drain(table, 0);
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 10, id, 0x03));
	const std::vector<Header> second = Drain(table, 0);
	// The Hello of ConnectStarted took sequence 0.
	ASSERT_EQ(first.size(), 3u);
	EXPECT_TRUE(first[0].sequence == 1 && first[1].sequence == 2 && first[2].sequence == 3);
	EXPECT_EQ(first[0].bodyLength, kFullFrame);
	EXPECT_EQ(first[0].timestamp, 1234u);
	ASSERT_EQ(second.size(), 1u);
	EXPECT_EQ(second[0].sequence, 5u);
	EXPECT_EQ(second[0].frameType, 0x03);
	EXPECT_EQ(second[0].bodyLength, 10u);
}

TEST(GenOnlineLocalApiPipe, PeriodicFramesLeaveTheReserveFree)
{
	ReaderTable table;
	const uint32_t id = ConnectStarted(table, 1);
	for (int i = 0; i < 3; ++i)
	{
		ASSERT_TRUE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	}
	// The free space holds 200000 bytes and the header but not the reserve on top.
	ASSERT_LT(table.FreeBytes(1), 200000 + kOutboundHeaderBytes + kPeriodicReserveBytes);
	ASSERT_GE(table.FreeBytes(1), 200000 + kOutboundHeaderBytes);
	EXPECT_FALSE(Enqueue(table, FrameClass::Periodic, 200000, id));
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 200000, id));
	// A dropped Periodic frame neither ends the stream nor asks for a Hello.
	Drain(table, 1);
	EXPECT_EQ(TakeEvents(table).size(), 1u);
	EXPECT_TRUE(Enqueue(table, FrameClass::Periodic, 4, id));
}

TEST(GenOnlineLocalApiPipe, LostLifecycleFrameEndsTheStreamAndReportsHelloNeededOnceThereIsRoom)
{
	ReaderTable table;
	const uint32_t id = ConnectStarted(table, 2);
	TakeEvents(table);
	for (int i = 0; i < 3; ++i)
	{
		ASSERT_TRUE(Enqueue(table, FrameClass::Periodic, kFullFrame, id));
	}
	EXPECT_FALSE(Enqueue(table, FrameClass::Lifecycle, kFullFrame, id));
	EXPECT_TRUE(TakeEvents(table).empty());
	// The stream has ended, so no class but Hello is taken.
	EXPECT_FALSE(Enqueue(table, FrameClass::Lifecycle, 4, id));
	EXPECT_FALSE(Enqueue(table, FrameClass::Periodic, 4, kAllReaders));
	Drain(table, 2);
	const std::vector<Event> events = TakeEvents(table);
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].readerId, id);
	EXPECT_EQ(events[0].event, ReaderEvent::HelloNeeded);
	EXPECT_TRUE(TakeEvents(table).empty());
	EXPECT_TRUE(Enqueue(table, FrameClass::Hello, 4, id, 0x09));
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 4, id));
	const std::vector<Header> frames = Drain(table, 2);
	ASSERT_EQ(frames.size(), 2u);
	// The reader keeps its sequence numbers, so the lost frames show as a gap.
	EXPECT_EQ(frames[0].frameType, 0x09);
	EXPECT_EQ(frames[1].sequence, frames[0].sequence + 1);
	EXPECT_GT(frames[0].sequence, 4u);
}

TEST(GenOnlineLocalApiPipe, ReaderIdsAreNonzeroAndNeverReused)
{
	ReaderTable table;
	std::vector<uint32_t> seen;
	for (int round = 0; round < 6; ++round)
	{
		const uint32_t id = table.OnConnected(round % 2, 0);
		EXPECT_NE(id, 0u);
		for (uint32_t earlier : seen)
		{
			EXPECT_NE(id, earlier);
		}
		seen.push_back(id);
		table.OnClosed(round % 2);
	}
	// The id of a closed reader is dead.
	EXPECT_FALSE(Enqueue(table, FrameClass::Hello, 8, seen[0]));
	EXPECT_FALSE(Enqueue(table, FrameClass::Hello, 8, seen.back()));
}

TEST(GenOnlineLocalApiPipe, AHelloStartsTheStreamAndNothingElseDoes)
{
	ReaderTable table;
	EXPECT_FALSE(table.IsConnected());
	const uint32_t id = table.OnConnected(3, 0);
	EXPECT_TRUE(table.IsConnected());
	EXPECT_FALSE(Enqueue(table, FrameClass::Lifecycle, 8, id));
	EXPECT_FALSE(Enqueue(table, FrameClass::Periodic, 8, id));
	EXPECT_FALSE(Enqueue(table, FrameClass::Periodic, 8, kAllReaders));
	EXPECT_TRUE(Drain(table, 3).empty());
	EXPECT_TRUE(Enqueue(table, FrameClass::Hello, 8, id, 0x09));
	EXPECT_TRUE(Enqueue(table, FrameClass::Hello, 8, id, 0x09));
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 8, id));
	const std::vector<Header> frames = Drain(table, 3);
	ASSERT_EQ(frames.size(), 3u);
	EXPECT_EQ(frames[0].frameType, 0x09);
	EXPECT_EQ(frames[0].sequence, 0u);
	table.OnClosed(3);
	EXPECT_FALSE(table.IsConnected());
	EXPECT_FALSE(Enqueue(table, FrameClass::Hello, 8, id));
}

TEST(GenOnlineLocalApiPipe, EnqueueRejectsBadArguments)
{
	ReaderTable table;
	const uint32_t id = ConnectStarted(table, 0);
	const uint8_t body[1] = {1};
	EXPECT_FALSE(table.EnqueueFrame(1, static_cast<FrameClass>(3), body, 1, 0, id));
	EXPECT_FALSE(table.EnqueueFrame(1, static_cast<FrameClass>(-1), body, 1, 0, id));
	EXPECT_FALSE(table.EnqueueFrame(1, FrameClass::Lifecycle, nullptr, 1, 0, id));
	EXPECT_FALSE(table.EnqueueFrame(1, FrameClass::Lifecycle, body, 0, 0, id));
	EXPECT_FALSE(table.EnqueueFrame(1, FrameClass::Lifecycle, body, kMaxBodyBytes + 1, 0, id));
	EXPECT_FALSE(table.EnqueueFrame(1, FrameClass::Lifecycle, body, 1, 0, id + 100));
	EXPECT_TRUE(Drain(table, 0).empty());
}

TEST(GenOnlineLocalApiPipe, BroadcastReachesEveryReaderWhoseStreamStarted)
{
	ReaderTable table;
	ConnectStarted(table, 0);
	ConnectStarted(table, 2);
	table.OnConnected(1, 0);
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 8, kAllReaders));
	EXPECT_EQ(Drain(table, 0).size(), 1u);
	EXPECT_TRUE(Drain(table, 1).empty());
	EXPECT_EQ(Drain(table, 2).size(), 1u);
	EXPECT_FALSE(Enqueue(table, FrameClass::Lifecycle, 8, 9999));
}

TEST(GenOnlineLocalApiPipe, ReconnectResetsSequenceAndQueue)
{
	ReaderTable table;
	const uint32_t first = ConnectStarted(table, 0);
	EXPECT_TRUE(Enqueue(table, FrameClass::Lifecycle, 8, first));
	table.OnClosed(0);
	const uint32_t second = table.OnConnected(0, 0);
	EXPECT_TRUE(second != 0 && second != first);
	EXPECT_EQ(table.FreeBytes(0), kQueueCapacityBytes);
	EXPECT_FALSE(Enqueue(table, FrameClass::Hello, 8, first));
	EXPECT_TRUE(Enqueue(table, FrameClass::Hello, 8, second));
	const std::vector<Header> frames = Drain(table, 0);
	ASSERT_EQ(frames.size(), 1u);
	EXPECT_EQ(frames[0].sequence, 0u);
}

TEST(GenOnlineLocalApiPipe, EventsComeInOrderAndHelloNeededIsNotDoubled)
{
	ReaderTable table;
	const uint32_t a = table.OnConnected(0, 0);
	const uint32_t b = table.OnConnected(1, 0);
	table.OnClosed(0);
	std::vector<Event> events = TakeEvents(table);
	ASSERT_EQ(events.size(), 3u);
	EXPECT_TRUE(events[0].readerId == a && events[0].event == ReaderEvent::HelloNeeded);
	EXPECT_TRUE(events[1].readerId == b && events[1].event == ReaderEvent::HelloNeeded);
	EXPECT_TRUE(events[2].readerId == a && events[2].event == ReaderEvent::Gone);
	EXPECT_TRUE(TakeEvents(table).empty());

	// A host that never polls cannot grow the list.
	for (int i = 0; i < 200; ++i)
	{
		table.OnConnected(0, 0);
		table.OnClosed(0);
	}
	EXPECT_LE(TakeEvents(table).size(), 64u);
	uint32_t id = 0;
	ReaderEvent event = ReaderEvent::HelloNeeded;
	EXPECT_FALSE(table.TakeReaderEvent(nullptr, &event));
	EXPECT_FALSE(table.TakeReaderEvent(&id, nullptr));
}

TEST(GenOnlineLocalApiPipe, InboundWaitsForTheHello)
{
	ReaderTable table;
	const uint32_t id = table.OnConnected(1, 0);
	SendInbound(table, 1, kInboundRequest, 1, 0);
	SendInbound(table, 1, kInboundChatSend, 2, 0);
	EXPECT_TRUE(TakeAll(table).empty());
	const uint8_t hello[1] = {0x09};
	table.EnqueueFrame(0x09, FrameClass::Hello, hello, sizeof(hello), 0, id);

	SendInbound(table, 1, kInboundRequest, 3, 0);
	TakenInbound taken;
	ASSERT_TRUE(table.TakeInbound(&taken));
	EXPECT_EQ(taken.frameType, kInboundRequest);
	EXPECT_EQ(taken.readerId, id);
	EXPECT_EQ(taken.flags, 0u);
	ASSERT_EQ(taken.bodyLength, 2u);
	EXPECT_TRUE(taken.body[0] == 0x08 && taken.body[1] == 3);
	EXPECT_FALSE(table.TakeInbound(&taken));
}

TEST(GenOnlineLocalApiPipe, UnknownInboundTypesAreSkipped)
{
	ReaderTable table;
	ConnectStarted(table, 0);
	SendInbound(table, 0, 0x12, 1, 0);
	SendInbound(table, 0, 0x01, 1, 0);
	EXPECT_TRUE(TakeAll(table).empty());
}

TEST(GenOnlineLocalApiPipe, RequestsPastTheLimitBecomeBoundedRefusals)
{
	ReaderTable table;
	ConnectStarted(table, 0, 0);
	// The queue holds kRequestQueueSlots, then refusals take kRefusalSlots, then the rest is dropped.
	for (int i = 0; i < 100; ++i)
	{
		SendInbound(table, 0, kInboundRequest, static_cast<uint8_t>(i), 0);
	}
	const std::vector<TakenInbound> frames = TakeAll(table);
	ASSERT_EQ(frames.size(), kRequestQueueSlots + kRefusalSlots);
	for (size_t i = 0; i < frames.size(); ++i)
	{
		const uint32_t expected = i < kRequestQueueSlots ? 0u : kInboundRateLimited;
		EXPECT_EQ(frames[i].flags, expected);
		EXPECT_EQ(frames[i].frameType, kInboundRequest);
	}
}

TEST(GenOnlineLocalApiPipe, EmptyBucketRefusesEvenWithQueueRoom)
{
	ReaderTable table;
	ConnectStarted(table, 0, 0);
	int accepted = 0;
	int refused = 0;
	for (uint32_t i = 0; i < kRequestBurst + 2; ++i)
	{
		SendInbound(table, 0, kInboundRequest, 0, 0);
		for (const TakenInbound& frame : TakeAll(table))
		{
			(frame.flags == 0 ? accepted : refused) += 1;
		}
	}
	EXPECT_EQ(accepted, static_cast<int>(kRequestBurst));
	EXPECT_EQ(refused, 2);
	// After 50 ms one token is back.
	SendInbound(table, 0, kInboundRequest, 0, 50);
	const std::vector<TakenInbound> later = TakeAll(table);
	ASSERT_EQ(later.size(), 1u);
	EXPECT_EQ(later[0].flags, 0u);
}

TEST(GenOnlineLocalApiPipe, ChatMailboxHoldsEight)
{
	ReaderTable table;
	ConnectStarted(table, 0);
	for (int i = 0; i < 20; ++i)
	{
		SendInbound(table, 0, kInboundChatSend, static_cast<uint8_t>(i), 0);
	}
	const std::vector<TakenInbound> frames = TakeAll(table);
	EXPECT_EQ(frames.size(), kChatMailboxSlots);
	for (const TakenInbound& frame : frames)
	{
		EXPECT_EQ(frame.frameType, kInboundChatSend);
		EXPECT_EQ(frame.flags, 0u);
	}
	SendInbound(table, 0, kInboundChatSend, 1, 0);
	EXPECT_EQ(TakeAll(table).size(), 1u);
}

TEST(GenOnlineLocalApiPipe, InboundTakesTheReadersInTurn)
{
	ReaderTable table;
	const uint32_t a = ConnectStarted(table, 0);
	const uint32_t b = ConnectStarted(table, 2);
	for (int i = 0; i < 3; ++i)
	{
		SendInbound(table, 0, kInboundRequest, static_cast<uint8_t>(i), 0);
		SendInbound(table, 2, kInboundRequest, static_cast<uint8_t>(10 + i), 0);
	}
	const std::vector<TakenInbound> frames = TakeAll(table);
	ASSERT_EQ(frames.size(), 6u);
	const uint32_t expectedIds[6] = {a, b, a, b, a, b};
	for (int i = 0; i < 6; ++i)
	{
		EXPECT_EQ(frames[i].readerId, expectedIds[i]);
	}
}

TEST(GenOnlineLocalApiPipe, ClosingAReaderDropsItsInboundFrames)
{
	ReaderTable table;
	ConnectStarted(table, 0);
	SendInbound(table, 0, kInboundRequest, 1, 0);
	table.OnClosed(0);
	EXPECT_TRUE(TakeAll(table).empty());
}

TEST(GenOnlineLocalApiPipeServer, ReaderGetsHelloAndItsRequestReachesTheGame)
{
	ReaderTable table;
	PipeServer server(table);
	ASSERT_TRUE(server.Start(4));

	HANDLE pipe = ConnectReader();
	ASSERT_NE(pipe, INVALID_HANDLE_VALUE);

	const Event connected = WaitForEvent(table);
	EXPECT_EQ(connected.event, ReaderEvent::HelloNeeded);
	ASSERT_NE(connected.readerId, 0u);
	EXPECT_TRUE(table.IsConnected());

	const uint8_t hello[] = {0x08, 0x02};
	ASSERT_TRUE(table.EnqueueFrame(0x09, FrameClass::Hello, hello, sizeof(hello), 0x1122334455667788ull, connected.readerId));
	server.Wake();
	uint8_t frame[kOutboundHeaderBytes + sizeof(hello)];
	ASSERT_TRUE(ReadExactly(pipe, frame, sizeof(frame)));
	const uint8_t expected[] = {0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x08, 0x02};
	EXPECT_EQ(std::memcmp(frame, expected, sizeof(expected)), 0);

	const std::vector<uint8_t> request = {0x08, 0x2A, 0x12, 0x00};
	ASSERT_TRUE(WriteAll(pipe, InboundFrame(kInboundRequest, request)));

	TakenInbound taken;
	ASSERT_TRUE(WaitFor([&] { return table.TakeInbound(&taken); }));
	EXPECT_EQ(taken.frameType, kInboundRequest);
	EXPECT_EQ(taken.readerId, connected.readerId);
	EXPECT_EQ(taken.flags, 0u);
	ASSERT_EQ(taken.bodyLength, request.size());
	EXPECT_EQ(std::memcmp(taken.body.get(), request.data(), request.size()), 0);

	CloseHandle(pipe);
	const Event gone = WaitForEvent(table);
	EXPECT_EQ(gone.event, ReaderEvent::Gone);
	EXPECT_EQ(gone.readerId, connected.readerId);
	EXPECT_FALSE(table.IsConnected());
	server.Stop();
	server.Stop();
}

TEST(GenOnlineLocalApiPipeServer, BadInboundLengthDropsTheReader)
{
	ReaderTable table;
	PipeServer server(table);
	ASSERT_TRUE(server.Start(4));
	HANDLE pipe = ConnectReader();
	ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
	const Event connected = WaitForEvent(table);
	ASSERT_EQ(connected.event, ReaderEvent::HelloNeeded);

	const std::vector<uint8_t> zeroLength = {0x00, 0x00, 0x00, 0x00, kInboundRequest};
	ASSERT_TRUE(WriteAll(pipe, zeroLength));
	const Event gone = WaitForEvent(table);
	EXPECT_EQ(gone.event, ReaderEvent::Gone);
	EXPECT_EQ(gone.readerId, connected.readerId);
	EXPECT_FALSE(table.IsConnected());

	// The slot listens again for the next reader, which gets a new id.
	HANDLE next = ConnectReader();
	EXPECT_NE(next, INVALID_HANDLE_VALUE);
	const Event again = WaitForEvent(table);
	EXPECT_EQ(again.event, ReaderEvent::HelloNeeded);
	EXPECT_NE(again.readerId, connected.readerId);
	CloseHandle(next);
	CloseHandle(pipe);
	server.Stop();
}

TEST(GenOnlineLocalApiPipeServer, AReaderBeyondMaxReadersIsBusy)
{
	ReaderTable table;
	PipeServer server(table);
	ASSERT_TRUE(server.Start(2));
	HANDLE readers[2];
	for (HANDLE& reader : readers)
	{
		reader = ConnectReader();
		ASSERT_NE(reader, INVALID_HANDLE_VALUE);
	}
	for (int i = 0; i < 2; ++i)
	{
		EXPECT_EQ(WaitForEvent(table).event, ReaderEvent::HelloNeeded);
	}
	HANDLE third = CreateFileA(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	EXPECT_EQ(third, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_PIPE_BUSY));
	for (HANDLE reader : readers)
	{
		CloseHandle(reader);
	}
	server.Stop();
	EXPECT_FALSE(table.IsConnected());
}
